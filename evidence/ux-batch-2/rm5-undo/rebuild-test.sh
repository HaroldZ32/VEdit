#!/usr/bin/env bash
# RM-5a: rebuild ONLY the pieces the undo regression needs, in the pinned Qt6
# image, without rebuilding the shared app-shell sources.
#
# Why: this workspace is shared with other lanes. When another lane leaves one
# app-shell file mid-edit (e.g. a -Werror unused-function in theme_manager.cpp),
# a plain `ninja` on the test target fails on THAT file even though this lane's
# change is fine. The regression only needs:
#   * src/editor/kernel/document_io.cpp  (the fixed open path)
#   * libvedit_editor_core.a             (re-archived)
#   * the test's own objects             (already built)
#   * the final link against the existing libvedit_app_shell.a
# so this script replays exactly the commands ninja would run for those steps.
#
#   bash evidence/ux-batch-2/rm5-undo/rebuild-test.sh [--run]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
run="${1:-}"

docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s -- "$run" <<'INNER'
set -euo pipefail
run="$1"
cd /vedit-build
build=target/qt6-build
target=src/app/editor-lane/vedit-undo-view-lifetime-tests
cmds="$(ninja -C "$build" -t commands "$target")"
compile="$(printf '%s\n' "$cmds" | grep 'kernel/document_io.cpp.o -c' | tail -1)"
archive="$(printf '%s\n' "$cmds" | grep 'ar qc src/app/editor-lane/libvedit_editor_core.a' | tail -1)"
link="$(printf '%s\n' "$cmds" | grep -- '-o src/app/editor-lane/vedit-undo-view-lifetime-tests ' | tail -1)"
for step in compile archive link; do
  eval "cmd=\${!step}"
  if [ -z "$cmd" ]; then
    echo "FATAL: ninja did not expose the $step command" >&2
    exit 2
  fi
  ( cd "$build" && bash -c "$cmd" )
done
echo "REBUILD ok: $build/$target"
if [ "$run" = "--run" ]; then
  export XDG_RUNTIME_DIR=/tmp/xdg-undo
  mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
  env QT_QPA_PLATFORM=offscreen "./$build/$target"
fi
INNER
