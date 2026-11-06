#!/usr/bin/env bash
# batch 4a screenshots (F1): renders the refs column / remote action row and the
# four dialogs at 1500x1000 on a REAL X server (Xvfb + xcb) inside the pinned Qt6
# image.
#
#   bash evidence/vcs-git-remote/screenshots.sh
#
# The probe is passed --require-xcb, so a missing xcb platform is a FAIL instead
# of a silently offscreen render. Each shot asserts its byte size, its exact
# dimensions and a minimum number of sampled colours (a flat rectangle is not
# evidence), so this script fails when a "screenshot" is empty or uniform.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SHOTS="$ROOT/evidence/vcs-git-remote/screenshots"
LOG="$ROOT/evidence/vcs-git-remote/logs/20-screenshots.log"
mkdir -p "$SHOTS" "$ROOT/evidence/vcs-git-remote/logs"

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  -e NO_AT_BRIDGE=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -e QT_XCB_GL_INTEGRATION=none \
  vedit-qt6-build:local \
  bash -lc '
    set -euo pipefail
    mkdir -p /tmp/xdg-run && chmod 700 /tmp/xdg-run
    Xvfb :79 -screen 0 1500x1000x24 >/tmp/xvfb-vcs-4a.log 2>&1 &
    xvfb=$!
    trap "kill $xvfb 2>/dev/null || true" EXIT
    sleep 2
    BIN=/vedit-build/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest
    [ -x "$BIN" ] || { echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2; exit 1; }
    {
      echo "# batch 4a 1500x1000 xcb screenshots (Xvfb :79, screen 1500x1000x24)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "xvfb=$(pgrep -a Xvfb | head -1)"
    } | tee /vedit-build/evidence/vcs-git-remote/logs/20-screenshots.log
    DISPLAY=:79 QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
      "$BIN" --ui-screenshots /vedit-build/evidence/vcs-git-remote/screenshots --require-xcb \
      2>&1 | tee -a /vedit-build/evidence/vcs-git-remote/logs/20-screenshots.log
    grep -q "WORKSPACE-VCS-SELFTEST screenshots ok=1 " \
      /vedit-build/evidence/vcs-git-remote/logs/20-screenshots.log || {
        echo "FAIL: the screenshot probe did not report ok=1" >&2
        exit 1
      }
    if grep -E "^CHECK .* FAIL" /vedit-build/evidence/vcs-git-remote/logs/20-screenshots.log; then
      echo "FAIL: a screenshot check is red" >&2
      exit 1
    fi
    echo "--- batch 4a shots ---"
    ls -la /vedit-build/evidence/vcs-git-remote/screenshots
    grep -E "^CHECK (20|21|22|23|24|25)-" \
      /vedit-build/evidence/vcs-git-remote/logs/20-screenshots.log || true
    echo "screenshots=PASS"
  '

echo "[vcs-git-remote] PASS: 1500x1000 xcb screenshots in evidence/vcs-git-remote/screenshots/"
