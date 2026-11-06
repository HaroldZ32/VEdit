#!/usr/bin/env bash
# Lane probe for batch 4a (AC-4.1 / AC-4.2 / AC-4.4 / AC-4.5).
#
#   bash evidence/vcs-git-remote/run-selftest.sh
#
# Runs `vedit-workspace-vcs-selftest --selfcheck` INSIDE the pinned Qt6 image
# with the cwd OUTSIDE the VEdit worktree. Every git fixture the lane creates is
# a fresh mktemp directory under /tmp (bare repository, two working copies, a
# 401 HTTP transport and a `git` argv recorder), never the VEdit worktree.
#
# The lane itself asserts that its fixture is not the worktree
# (`vcs.write.e2e.not-worktree`, `vcs.remote.e2e.not-worktree`).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOG="$ROOT/evidence/vcs-git-remote/logs/10-selftest.log"
mkdir -p "$ROOT/evidence/vcs-git-remote/logs"

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
    FIXTURE=$(mktemp -d /tmp/vedit-vcs-4a-XXXXXX)
    LOG="$WORKSPACE/evidence/vcs-git-remote/logs/10-selftest.log"
    mkdir -p "$FIXTURE/xdg"
    {
      echo "# lane selftest (batch 4a: git remote sync, refs, credentials)"
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
    grep -E "^CHECK vcs\.(refs|op|cred|remote)" "$LOG" | tail -40
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
  ' 2>&1 | tee "$ROOT/evidence/vcs-git-remote/logs/10-selftest.console.log"
