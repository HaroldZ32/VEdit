#!/usr/bin/env bash
# batch 6b screenshots (AC-5.2): renders the workspace dialog (real stash +
# worktree listings) at 1500x1000 on a REAL X server (Xvfb + xcb) inside the pinned Qt6
# image.
#
#   bash evidence/svn-e2e/screenshots.sh
#
# The probe is passed --require-xcb, so a missing xcb platform is a FAIL instead
# of a silently offscreen render. Each shot asserts its byte size, its exact
# dimensions and a minimum number of sampled colours (a flat rectangle is not
# evidence), so this script fails when a "screenshot" is empty or uniform.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SHOTS="$ROOT/evidence/svn-e2e/screenshots"
LOG="$ROOT/evidence/svn-e2e/logs/20-screenshots.log"
mkdir -p "$SHOTS" "$ROOT/evidence/svn-e2e/logs"

docker run --rm \
  -v "$ROOT":/vedit-build \
  -w /tmp \
  -e NO_AT_BRIDGE=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -e QT_XCB_GL_INTEGRATION=none \
  -e VEDIT_SVN=/vedit-build/packaging/runtime/out/AppDir/bin/svn \
  -e VEDIT_SVN_BIN=/vedit-build/packaging/runtime/out/AppDir/bin/svn \
  -e VEDIT_SVNADMIN=/vedit-build/build/vcs-tools/svnadmin \
  vedit-qt6-build:local \
  bash -lc '
    set -euo pipefail
    mkdir -p /tmp/xdg-run && chmod 700 /tmp/xdg-run
    Xvfb :83 -screen 0 1500x1000x24 >/tmp/xvfb-vcs-6b.log 2>&1 &
    xvfb=$!
    trap "kill $xvfb 2>/dev/null || true" EXIT
    sleep 2
    BIN=/vedit-build/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest
    [ -x "$BIN" ] || { echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2; exit 1; }
    {
      echo "# batch 4a 1500x1000 xcb screenshots (Xvfb :83, screen 1500x1000x24)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "xvfb=$(pgrep -a Xvfb | head -1)"
    } | tee /vedit-build/evidence/svn-e2e/logs/20-screenshots.log
    DISPLAY=:83 QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
      "$BIN" --ui-screenshots /vedit-build/evidence/svn-e2e/screenshots --require-xcb \
      2>&1 | tee -a /vedit-build/evidence/svn-e2e/logs/20-screenshots.log
    grep -q "WORKSPACE-VCS-SELFTEST screenshots ok=1 " \
      /vedit-build/evidence/svn-e2e/logs/20-screenshots.log || {
        echo "FAIL: the screenshot probe did not report ok=1" >&2
        exit 1
      }
    if grep -E "^CHECK .* FAIL" /vedit-build/evidence/svn-e2e/logs/20-screenshots.log; then
      echo "FAIL: a screenshot check is red" >&2
      exit 1
    fi
    echo "--- batch 4a shots ---"
    ls -la /vedit-build/evidence/svn-e2e/screenshots
    grep -E "^CHECK (60|61)-" \
      /vedit-build/evidence/svn-e2e/logs/20-screenshots.log || true
    echo "screenshots=PASS"
  '

echo "[svn-e2e] PASS: 1500x1000 xcb screenshots in evidence/svn-e2e/screenshots/"
