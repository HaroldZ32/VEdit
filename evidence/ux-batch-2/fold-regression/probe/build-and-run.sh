#!/usr/bin/env bash
# Build the fold probe against the kernel static library with the EXACT flags of
# the vedit-native-fold-tests target, then run it offscreen.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
img="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
docker run --rm -i -e HOME=/tmp -v "$root:/vedit-build" "$img" bash -s <<'INNER'
set -euo pipefail
cd /vedit-build
b=target/qt6-build
cmds="$(ninja -C "$b" -t commands src/app/editor-lane/vedit-native-fold-tests)"
compile="$(printf '%s\n' "$cmds" | grep 'native_fold_tests.cpp.o -c' | tail -1 | sed 's#/vedit-build/src/editor/kernel/native_fold_tests.cpp#/vedit-build/evidence/ux-batch-2/fold-regression/probe/fold_after_openurl_probe.cpp#; s#-o src/app/editor-lane/CMakeFiles/vedit-native-fold-tests.dir/kernel/native_fold_tests.cpp.o#-o /tmp/fold_probe.o#; s#-isystem#-I/vedit-build/src/editor/kernel -isystem#')"
link="$(printf '%s\n' "$cmds" | grep -- '-o src/app/editor-lane/vedit-native-fold-tests ' | tail -1 | sed 's#src/app/editor-lane/CMakeFiles/vedit-native-fold-tests.dir/kernel/native_fold_tests.cpp.o#/tmp/fold_probe.o#; s#-o src/app/editor-lane/vedit-native-fold-tests#-o /tmp/fold_probe#')"
( cd "$b" && bash -c "$compile" )
( cd "$b" && bash -c "$link" )
export XDG_RUNTIME_DIR=/tmp/xdg-fold-probe; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cd "$b"
QT_QPA_PLATFORM=offscreen /tmp/fold_probe 2>&1 | grep -E "^FOLD"
INNER
