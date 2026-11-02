#!/usr/bin/env bash
# Runs INSIDE the pinned Qt6/KF6 image (invoked by run-in-container.sh).
#
# Produces, under evidence/ws-filetree/:
#   00-pins.txt        image/toolchain/user facts of this run
#   00-steps.txt       every command with its observed exit code
#   01-configure.log   an INDEPENDENT cmake configure of this lane
#   02-build.log       the -Werror build
#   03-ctest.log       the acceptance test suite (offscreen)
#   04-screenshots.log the interaction driver + laziness measurements
#   screenshots/*.png  the visual evidence
#
# Scratch data lives under target/ (gitignored) and is never /tmp: this session
# isolates /tmp per command.
set -euo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
SRC="$WORK/src/workspace/filetree"
BUILD="$WORK/target/filetree-build"
SCRATCH="$WORK/target/ws-filetree-tmp"
EVID="$WORK/evidence/ws-filetree"
SHOTS="$EVID/screenshots"
STEPS="$EVID/00-steps.txt"

mkdir -p "$BUILD" "$EVID" "$SHOTS" "$BUILD/xdg-runtime"
: >"$STEPS"

{
  echo "date: $(date -Is)"
  echo "workspace: $WORK"
  echo "uid: $(id -u)"
  echo "container: $(. /etc/os-release && echo "$PRETTY_NAME")"
  echo "qt6-base-dev: $(dpkg-query -W -f='${Version}' qt6-base-dev)"
  echo "cmake: $(cmake --version | head -1)"
  echo "g++: $(g++ --version | head -1)"
  echo "glibc: $(ldd --version | head -1)"
  echo "source: src/workspace/filetree @ $(cd "$SRC" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "source files: $(find "$SRC" -maxdepth 1 -type f | wc -l) top-level + $(find "$SRC/demo" "$SRC/tests" -type f | wc -l) tooling files"
} >"$EVID/00-pins.txt"

run_step() {
  local name="$1"
  shift
  echo "=== STEP $name: $*"
  local status=0
  set +e
  "$@" 2>&1 | tee "$EVID/$name.log"
  status=${PIPESTATUS[0]}
  set -e
  echo "STEP $name exit=$status cmd=$*" | tee -a "$STEPS"
  return "$status"
}

# Every step runs headless (the image has no display); ctest also sets this per test.
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$BUILD/xdg-runtime"

# The test suite creates its scratch tree relative to this variable (never /tmp
# and never the repository root): keep it inside the build tree.
export VEDIT_FILETREE_SCRATCH="$BUILD/filetree-testdata"

run_step 01-configure cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
run_step 02-build cmake --build "$BUILD" --parallel "$(nproc)"
run_step 03-ctest ctest --test-dir "$BUILD" --output-on-failure
# ctest only prints the output of FAILING tests, so the full CHECK list of the
# passing suite is captured by running the binary once more.
run_step 03a-tests-full "$BUILD/vedit-filetree-tests"

# The permission-denied cases cannot run as container root (root bypasses DAC),
# so the suite runs them in a second process as an unprivileged uid. The root
# run above prints them as SKIPPED instead of pretending they passed.
PERM="$WORK/target/ws-filetree-perm"
rm -rf "$PERM"
mkdir -p "$PERM"
chmod 777 "$PERM"
if command -v setpriv >/dev/null 2>&1; then
  perm_status=0
  set +e
  setpriv --reuid=1000 --regid=1000 --clear-groups env \
    VEDIT_FILETREE_SCRATCH="$PERM" \
    VEDIT_FILETREE_SUITE=permissions \
    QT_QPA_PLATFORM=offscreen \
    XDG_RUNTIME_DIR=/tmp/xdg-runtime \
    "$BUILD/vedit-filetree-tests" 2>&1 | tee "$EVID/06-permissions-nonroot.log"
  perm_status=${PIPESTATUS[0]}
  set -e
  echo "STEP 06-permissions-nonroot exit=$perm_status cmd=setpriv --reuid=1000 VEDIT_FILETREE_SUITE=permissions vedit-filetree-tests" | tee -a "$STEPS"
  if [ "$perm_status" -ne 0 ]; then
    echo "FATAL: the permission-denied suite failed (exit $perm_status)" >&2
    exit "$perm_status"
  fi
else
  echo "STEP 06-permissions-nonroot exit=SKIPPED setpriv-missing" | tee -a "$STEPS"
fi

# Fixture for the screenshot driver: a small Verilog-ish project, an empty
# folder, a folder for the permission-denied state and a 10k-file folder.
rm -rf "$SCRATCH"
run_step 04-fixture "$BUILD/vedit-filetree-demo" --prepare "$SCRATCH" --big-count 10000

# The driver runs as an UNPRIVILEGED user: container root bypasses DAC, so the
# permission-denied state would silently look like an empty folder. The fixture
# is world-writable first, then the "denied" folder is closed off for real.
chmod -R a+rwX "$SCRATCH"
chmod 000 "$SCRATCH/denied"
chmod 777 "$EVID" "$SHOTS"
# Drop PNGs from an earlier (possibly root-owned) run: an existing 0644 file
# cannot be truncated by the unprivileged driver.
rm -f "$SHOTS"/*.png

status=0
set +e
setpriv --reuid=1000 --regid=1000 --clear-groups env \
  QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR=/tmp/xdg-runtime \
  "$BUILD/vedit-filetree-demo" --screenshots "$SHOTS" \
  --project "$SCRATCH/project" \
  --empty "$SCRATCH/empty" \
  --denied "$SCRATCH/denied" \
  --big "$SCRATCH/big" \
  --missing "$SCRATCH/does-not-exist" 2>&1 | tee "$EVID/05-screenshots.log"
status=${PIPESTATUS[0]}
set -e
chmod 755 "$SCRATCH/denied"
# Hand the evidence back to the workspace owner (container root == host user in
# the rootless daemon used by scripts/lib-qt.sh).
chown -R 0:0 "$EVID" "$SCRATCH" 2>/dev/null || true
echo "STEP 05-screenshots exit=$status cmd=setpriv --reuid=1000 vedit-filetree-demo --screenshots ..." | tee -a "$STEPS"

echo "[filetree] evidence: $EVID"
exit "$status"
