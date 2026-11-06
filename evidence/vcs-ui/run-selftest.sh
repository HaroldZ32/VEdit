#!/usr/bin/env bash
# Lane probe for batch 3b (AC-3.3 / AC-3.4 / AC-3.5 / AC-3.7): drives
# `vedit-workspace-vcs-selftest --selfcheck` INSIDE the pinned Qt6 image with the
# cwd outside the workspace. Every git fixture the selftest creates is a fresh
# mktemp directory under /tmp, never the VEdit worktree.
#
#   bash evidence/vcs-ui/run-selftest.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOG="$ROOT/evidence/vcs-ui/logs/10-selftest.log"
mkdir -p "$ROOT/evidence/vcs-ui/logs"

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  -e QT_QPA_PLATFORM=offscreen \
  -e NO_AT_BRIDGE=1 \
  vedit-qt6-build:local \
  bash -lc '
    set -euo pipefail
    WORKSPACE=/vedit-build
    if [ "$(pwd -P)" = "$WORKSPACE" ]; then
      echo "FATAL: the probe must not run inside the VEdit working tree ($WORKSPACE)" >&2
      exit 1
    fi
    BIN="$WORKSPACE/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest"
    [ -x "$BIN" ] || { echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2; exit 1; }
    FIXTURE=$(mktemp -d /tmp/vedit-vcs-3b-XXXXXX)
    LOG="$WORKSPACE/evidence/vcs-ui/logs/10-selftest.log"
    mkdir -p "$FIXTURE/xdg"
    {
      echo "# lane selftest (batch 3b: three-column UI, Git A writes, auto refresh)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "cwd=$(pwd -P)"
      echo "binary=$BIN"
      echo "binary_sha256=$(sha256sum "$BIN" | cut -d" " -f1)"
      echo "git=$(git --version)"
      echo "image=$(cat /etc/centos-release 2>/dev/null || echo "<unknown>")"
      echo
    } > "$LOG"
    cd "$FIXTURE"
    echo "fixture_cwd=$(pwd -P)" >> "$LOG"
    QT_QPA_PLATFORM=offscreen NO_AT_BRIDGE=1 XDG_RUNTIME_DIR="$FIXTURE/xdg" \
      "$BIN" --selfcheck >> "$LOG" 2>&1 || {
        echo "FAIL: the lane selftest exited non-zero (see $LOG)" >&2
        exit 1
      }
    grep -E "^CHECK vcs\.(layout|state|write|refresh|i18n|seam|panel\.close)" "$LOG"
    grep -q "WORKSPACE-VCS-SELFTEST ok=1 " "$LOG" || {
      echo "FAIL: the lane selftest did not report ok=1" >&2
      exit 1
    }
    if grep -E "^CHECK .* FAIL" "$LOG"; then
      echo "FAIL: a lane check is red" >&2
      exit 1
    fi
    echo "cwd_still=$(pwd -P)"
    echo "selftest=PASS"
  ' 2>&1 | tee "$ROOT/evidence/vcs-ui/logs/10-selftest.console.log"

echo "[vcs-ui] PASS: lane selftest green; log evidence/vcs-ui/logs/10-selftest.log"
