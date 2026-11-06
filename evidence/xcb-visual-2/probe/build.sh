#!/usr/bin/env bash
# Builds the batch-1 acceptance probe (AC-1.1 / AC-1.2) INSIDE the pinned
# Qt6/KF6 image. Nothing from src/ is recompiled for the probe: it is compiled
# with the product's own include flags and linked with the product's own link
# line, both read from ninja, so what it observes IS the shipped code.
#
# Call it through container-run.sh (which supplies the container and the mount).
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
BUILD="$WORK/target/qt6-build"
PROBE_DIR="$WORK/evidence/xcb-visual-2/probe"
OUT="$WORK/target/xcb-visual-2-probe/batch1-shell-probe"
OBJ="$WORK/target/xcb-visual-2-probe/batch1-shell-probe.o"
mkdir -p "$(dirname "$OUT")"

if [ ! -f "$BUILD/build.ninja" ]; then
  echo "FATAL: $BUILD/build.ninja is missing; build the product first (scripts/build-qt.sh)" >&2
  exit 1
fi

# --- compile flags ----------------------------------------------------------
# The product's own compile command for a shell source carries the Qt6/KF6/
# KSyntaxHighlighting include paths (and the build's pin defines); the lane
# headers the probe reads are added explicitly.
app_compile=$(ninja -C "$BUILD" -t commands "src/app/CMakeFiles/vedit_app_shell.dir/app_shell.cpp.o" | tail -1)
if [ -z "$app_compile" ]; then
  echo "FATAL: cannot read the product compile command from ninja" >&2
  exit 1
fi
app_includes=$(printf '%s\n' "$app_compile" | grep -oE '(-I[^ ]+|-isystem [^ ]+)' | sort -u | tr '\n' ' ')
# The edit-core kernel pulls KF6 (KTextEditor/KParts/...) through its own target;
# take its include flags verbatim so the probe never spells a KF6 path by hand.
kernel_compile=$(ninja -C "$BUILD" -t commands "src/app/editor-lane/CMakeFiles/vedit_editor_core.dir/kernel/ktexteditor_provider.cpp.o" | tail -1)
kernel_includes=$(printf '%s\n' "$kernel_compile" | grep -oE '(-I[^ ]+|-isystem [^ ]+)' | sort -u | tr '\n' ' ')
echo "[probe] reusing the product include flags: ${app_includes}"
echo "[probe] reuse the edit-core kernel include flags: ${kernel_includes}"

# The product always ships the KTextEditor edit-core provider, so the probe
# enables it too: Save As (AC-1.4) must be observed on the REAL provider. The
# editor kernel's own headers are needed for the View/Document types; the library
# itself is already part of the product link line the probe reuses.
set -x
/usr/bin/c++ -std=c++20 -fPIC -O2 -DNDEBUG -DVEDIT_PROBE_HAVE_EDIT_CORE=1 \
  -I"$WORK/src/app" -I"$WORK/src/workspace" \
  -I"$WORK/src/workspace/filetree" -I"$WORK/src/workspace/search" \
  -I"$WORK/src/workspace/project" -I"$WORK/src/workspace/module_tree" \
  -I"$WORK/src/workspace/vcs" -I"$WORK/src/editor/kernel" \
  $(pkg-config --cflags Qt6Widgets) $app_includes $kernel_includes \
  -c "$PROBE_DIR/main.cpp" -o "$OBJ"

# --- link: the product's own link line, with the probe object in place of main
link=$(ninja -C "$BUILD" -t commands src/app/vedit-app | tail -1)
link="${link#*&& }"
link=$(printf '%s' "$link" \
  | sed -e 's#-Wl,--dependency-file=[^ ]*##' \
        -e "s#src/app/CMakeFiles/vedit-app.dir/main.cpp.o#${OBJ}#" \
        -e "s#-o src/app/vedit-app#-o ${OUT}#")
cd "$BUILD"
eval "$link"
set +x
echo "[probe] built $OUT"
