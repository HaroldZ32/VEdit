#!/usr/bin/env bash
# Builds the T19 lane probe INSIDE the pinned Qt6/KF6 image. It links the
# product's own static libraries: nothing from src/ is recompiled for the probe,
# so the probe observes the shipped code (not a copy of it).
#
# Call it through run.sh (host side), which supplies the container and the mount.
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
BUILD="$WORK/target/qt6-build"
PROBE_DIR="$WORK/evidence/i3-session/probe"
OUT="$WORK/target/i3-session-probe/session-probe"
mkdir -p "$(dirname "$OUT")"
OBJ="$WORK/target/i3-session-probe/session-probe.o"

if [ ! -f "$BUILD/build.ninja" ]; then
  echo "FATAL: $BUILD/build.ninja is missing; build the product first (scripts/build-qt.sh)" >&2
  exit 1
fi

# --- compile flags ----------------------------------------------------------
# Reuse the product's own compile command for a shell source (include paths of
# Qt6/KSyntaxHighlighting) and the editor kernel's (KF6TextEditor headers), so
# the probe is built exactly like the product code it calls.
app_compile=$(ninja -C "$BUILD" -t commands "src/app/CMakeFiles/vedit_app_shell.dir/editor_area.cpp.o" | tail -1)
kernel_compile=$(ninja -C "$BUILD" -t commands \
  "src/app/editor-lane/CMakeFiles/vedit_editor_core.dir/kernel/ktexteditor_provider.cpp.o" | tail -1)
if [ -z "$app_compile" ] || [ -z "$kernel_compile" ]; then
  echo "FATAL: cannot read the product compile commands from ninja" >&2
  exit 1
fi
kernel_includes=$(printf '%s\n' "$kernel_compile" | grep -oE '\-isystem [^ ]+' | sort -u | tr '\n' ' ')
echo "[probe] kernel include dirs: $kernel_includes"

# The probe also includes src/app (the shell seam) and src/editor/kernel (the
# edit-core provider) directly.
set -x
/usr/bin/c++ -std=c++20 -fPIC -O2 -DNDEBUG \
  -I"$WORK/src/app" -I"$WORK/src/editor/kernel" -I"$WORK/src/workspace/project" \
  $(pkg-config --cflags Qt6Widgets) $kernel_includes \
  -c "$PROBE_DIR/main.cpp" -o "$OBJ"

# --- link: the product's own link line, with the probe object in place of main
link=$(ninja -C "$BUILD" -t commands src/app/vedit-app | tail -1)
link="${link#*&& }"
link=$(printf '%s' "$link" \
  | sed -e 's#-Wl,--dependency-file=[^ ]*##' \
        -e "s#src/app/CMakeFiles/vedit-app.dir/vedit-app_autogen/mocs_compilation.cpp.o#${OBJ}#" \
        -e 's#src/app/CMakeFiles/vedit-app.dir/main.cpp.o##' \
        -e "s#-o src/app/vedit-app#-o ${OUT}#")
cd "$BUILD"
eval "$link"
set +x
echo "[probe] built $OUT"
