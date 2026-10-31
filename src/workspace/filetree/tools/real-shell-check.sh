#!/usr/bin/env bash
# INSIDE the pinned image: prove that the file tree panel is really installed in
# the application shell by the production entry point (src/app/main.cpp ->
# src/workspace/integration.cpp -> installFileTree), and capture the visual
# evidence. Invoked by run-real-shell-check.sh.
#
# It reuses the root build produced by `bash scripts/build-qt.sh` (it never
# configures its own copy of the product) and then builds the lane's small
# root-context harness, which adds src/app exactly as the root build does.
#
#   09-real-app-install.log   real `vedit-app` binary run + ELF NEEDED assertion
#   08-real-shell-seamcheck.log  dock content / bridge / laziness assertions
#   screenshots/20-real-shell-file-tree.png
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
BUILD="$WORK/target/qt6-build"
APP="$BUILD/src/app/vedit-app"
ROOTCHECK_BUILD="$WORK/target/filetree-rootcheck"
EVID="$WORK/evidence/ws-filetree"
FIXTURE="$WORK/target/ws-filetree-tmp"
STEPS="$EVID/00-steps.txt"

if [ ! -x "$APP" ]; then
  echo "FATAL: $APP is missing. Run 'bash scripts/build-qt.sh' first (T10, docs/BUILD.md §12.2)." >&2
  exit 2
fi
mkdir -p "$EVID/screenshots"

export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$WORK/target/xdg-runtime"
mkdir -p "$XDG_RUNTIME_DIR"

if [ ! -d "$FIXTURE/project" ]; then
  echo "FATAL: fixture $FIXTURE/project is missing. Run src/workspace/filetree/tools/run-in-container.sh first." >&2
  exit 2
fi

{
  count=$(readelf -d "$APP" | grep -cE 'KTextEditor|tree-sitter' || true)
  echo "NEEDED-KTEXTEDITOR-OR-TREESITTER count=$count (expected 0)"
  rm -rf "$FIXTURE/app-state"
  "$APP" --project "$FIXTURE/project" --state-dir "$FIXTURE/app-state" \
         --no-session-restore --hold 1200 2>&1 | grep -E "filetree: installFileTree" || true
  [ "$count" -eq 0 ] || exit 1
} 2>&1 | tee "$EVID/09-real-app-install.log"
app_status=${PIPESTATUS[0]}
echo "STEP 09-real-app exit=$app_status cmd=readelf NEEDED + vedit-app --project ..." | tee -a "$STEPS"
[ "$app_status" -eq 0 ] || exit "$app_status"

set +e
cmake -S "$WORK/src/workspace/filetree/tools/rootcheck" -B "$ROOTCHECK_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tee "$EVID/10-rootcheck-configure.log" | \
  grep -E "workspace lane|VEdit file tree|Configuring done|Generating done"
echo "STEP 10-rootcheck-configure exit=${PIPESTATUS[0]}" | tee -a "$STEPS"
set -e

set +e
cmake --build "$ROOTCHECK_BUILD" --parallel "$(nproc)" >"$EVID/11-rootcheck-build.log" 2>&1
build_status=$?
set -e
tail -3 "$EVID/11-rootcheck-build.log"
echo "STEP 11-rootcheck-build exit=$build_status cmd=cmake --build target/filetree-rootcheck" | tee -a "$STEPS"
[ "$build_status" -eq 0 ] || exit "$build_status"

seam_status=0
set +e
"$ROOTCHECK_BUILD/vedit-filetree-seamcheck" \
  --project "$FIXTURE/project" \
  --state "$FIXTURE/seam-state" \
  --screenshot "$EVID/screenshots/20-real-shell-file-tree.png" 2>&1 | \
  tee "$EVID/08-real-shell-seamcheck.log" | grep -E "SEAM |SCREENSHOT|FILETREE-SEAMCHECK|filetree:"
seam_status=${PIPESTATUS[0]}
set -e
echo "STEP 12-real-shell-seamcheck exit=$seam_status" | tee -a "$STEPS"

exit "$seam_status"
