#!/usr/bin/env bash
# C++ lane probe. Runs INSIDE the pinned Qt6 image with cwd NOT the workspace.
#
# Drives `vedit-workspace-vcs-selftest --selfcheck`: the write allow-list checks
# through the C ABI (`vcs.ffi.write.*`) and the fake-git event-loop probe for a
# write job (`vcs.write.*`).
set -euo pipefail

WORKSPACE=/vedit-build
if [ "$(pwd -P)" = "$WORKSPACE" ]; then
  echo "FATAL: the probe must not run inside the VEdit working tree ($WORKSPACE)" >&2
  exit 1
fi

FIXTURE=$(mktemp -d /tmp/vedit-vcs-selftest-XXXXXX)
echo "# C++ lane probe"
echo "cwd=$(pwd -P)"
echo "fixture=$FIXTURE"

BIN="$WORKSPACE/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest"
if [ ! -x "$BIN" ]; then
  echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2
  exit 1
fi
echo "binary=$BIN"

cd "$FIXTURE"
QT_QPA_PLATFORM=offscreen NO_AT_BRIDGE=1 "$BIN" --selfcheck | tee "$FIXTURE/selftest.out"

echo "== write-related checks =="
grep -E 'CHECK vcs\.(ffi\.)?write\.' "$FIXTURE/selftest.out"
if grep -E 'CHECK vcs\.(ffi\.)?write\..* FAIL' "$FIXTURE/selftest.out" > /dev/null; then
  echo "FAIL: a write check is red" >&2
  exit 1
fi
if ! grep -E 'WORKSPACE-VCS-SELFTEST ok=1 ' "$FIXTURE/selftest.out" > /dev/null; then
  echo "FAIL: the lane selftest did not report ok=1" >&2
  exit 1
fi
echo "cwd_still=$(pwd -P)"
