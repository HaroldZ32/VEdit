#!/usr/bin/env bash
# Runs INSIDE the pinned Qt6/KF6 image (T39 evidence command; never on the host).
#
#   bash evidence/fix-filetree-open/run-seamcheck.sh <before|after>
#
# <before> is run with src/workspace/filetree/filetree_integration.{h,cpp} restored
# to HEAD (9957f5f, the code the T21 verdict judged); <after> is run with the
# repair in place. Everything else is identical, so the two logs are a real
# before/after pair.
#
# It builds (1) the lane's standalone suite (regression: the panel behaviour must
# not change), (2) the lane's fixture via its own --prepare, and (3) the
# root-context harness, which drives the REAL ApplicationShell through
# installPanels() and asserts editor content / the vedit.openFilePath property.
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
EVID="$WORK/evidence/fix-filetree-open"
label="${1:?usage: run-seamcheck.sh <before|after>}"
BUILD="$WORK/target/filetree-build"
ROOTCHECK="$WORK/target/filetree-rootcheck"
FIXTURE="$WORK/target/ws-filetree-tmp"

mkdir -p "$WORK/target/xdg-runtime" "$EVID/screenshots"
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$WORK/target/xdg-runtime"

echo "== image/toolchain =="
. /etc/os-release && echo "container: $PRETTY_NAME"
echo "qt6-base-dev: $(dpkg-query -W -f='${Version}' qt6-base-dev)"
echo "cmake: $(cmake --version | head -1)"
echo "g++: $(g++ --version | head -1)"
echo "glibc: $(ldd --version | head -1)"
echo "uid: $(id -u)"
echo "source: $(cd "$WORK" && git rev-parse --short HEAD) (HEAD; working tree carries the T39 repair)"

echo "== 1/4 configure the lane standalone =="
cmake -S "$WORK/src/workspace/filetree" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -6

echo "== 2/4 build the lane (panel + ctest suite + demo) =="
cmake --build "$BUILD" --parallel "$(nproc)" 2>&1 | tail -6

echo "== 2b/4 lane ctest ($label) =="
ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tail -12

echo "== 2c/4 fixture via the lane's own --prepare =="
"$BUILD/vedit-filetree-demo" --prepare "$FIXTURE" --big-count 200

echo "== 3/4 configure+build the root-context harness (real ApplicationShell) =="
cmake -S "$WORK/src/workspace/filetree/tools/rootcheck" -B "$ROOTCHECK" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tee "$EVID/10-rootcheck-configure-$label.log" | tail -8
# Only the harness target: this root-context project adds src/app alone (the
# real root CMakeLists also adds src/terminal and the Rust FFI), so the product
# binary of this tiny project cannot compile main.cpp; the harness constructs
# ApplicationShell directly and does not need vedit-app.
cmake --build "$ROOTCHECK" --target vedit-filetree-seamcheck --parallel "$(nproc)" \
  2>&1 | tee "$EVID/11-rootcheck-build-$label.log" | tail -8

echo "== 4/4 seamcheck ($label) =="
status=0
"$ROOTCHECK/vedit-filetree-seamcheck" \
  --project "$FIXTURE/project" \
  --state "$FIXTURE/seam-state-$label" \
  --screenshot "$EVID/screenshots/$label-shell.png" || status=$?
echo "SEAMCHECK $label exit=$status"
exit "$status"
