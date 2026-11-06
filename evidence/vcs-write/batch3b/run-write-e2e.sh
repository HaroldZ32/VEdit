#!/usr/bin/env bash
# AC-3.3 real-repository probe: stage / unstage / discard / commit / amend run
# `git` for real and the result is asserted with `git status --porcelain` and
# `git log`. The fixtures are fresh mktemp directories under /tmp; the probe
# asserts its own cwd is NOT the VEdit working tree before it starts.
#
#   bash evidence/vcs-write/batch3b/run-write-e2e.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
LOG="$ROOT/evidence/vcs-write/batch3b/logs/10-write-e2e.log"
mkdir -p "$ROOT/evidence/vcs-write/batch3b/logs"

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
    FIXTURE=$(mktemp -d /tmp/vedit-vcs-write-XXXXXX)
    mkdir -p "$FIXTURE/xdg"
    LOG="$WORKSPACE/evidence/vcs-write/batch3b/logs/10-write-e2e.log"
    {
      echo "# AC-3.3 Git write E2E (lane selftest, real git)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "cwd=$(pwd -P)"
      echo "fixture_root=$FIXTURE  (every repository the probe uses is created under it)"
      echo "git=$(git --version)"
      echo "binary_sha256=$(sha256sum "$BIN" | cut -d" " -f1)"
      echo
    } > "$LOG"
    cd "$FIXTURE"
    QT_QPA_PLATFORM=offscreen NO_AT_BRIDGE=1 XDG_RUNTIME_DIR="$FIXTURE/xdg" \
      "$BIN" --selfcheck >> "$LOG" 2>&1 || {
        echo "FAIL: the lane selftest exited non-zero (see $LOG)" >&2
        exit 1
      }
    echo "# --- the AC-3.3 assertions (git status --porcelain / git log -1) ---" >> "$LOG"
    grep -E "^CHECK vcs\.write\.e2e\." "$LOG" >&2
    count=$(grep -cE "^CHECK vcs\.write\.e2e\." "$LOG" || true)
    echo "write_e2e_checks=$count"
    [ "$count" -ge 19 ] || { echo "FAIL: expected >=19 write E2E checks, saw $count" >&2; exit 1; }
    if grep -E "^CHECK vcs\.write\.e2e\..* FAIL" "$LOG"; then
      echo "FAIL: a write E2E check is red" >&2
      exit 1
    fi
    grep -E "^CHECK vcs\.write\.e2e\.(staged-porcelain|unstaged-porcelain|discard-cancel-zero-write|discarded-porcelain|commit-subject|commit-author|amend-subject|amend-rewrote-one-commit)" "$LOG"
    echo "write_e2e=PASS"
  ' 2>&1 | tee "$ROOT/evidence/vcs-write/batch3b/logs/10-write-e2e.console.log"

echo "[vcs-write] PASS: Git A writes verified against a real repository; log evidence/vcs-write/batch3b/logs/10-write-e2e.log"
