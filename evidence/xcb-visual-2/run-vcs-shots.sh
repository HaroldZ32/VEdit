#!/usr/bin/env bash
# 7c: 1500x1000 xcb screenshots of the VCS lane — three-column Git working tree,
# the History tab (commits/blame/empty/error), the Workspace dialog and a REAL
# SVN working-copy view (bundled svn 1.14.5) — plus the other panel states.
#
#   bash evidence/xcb-visual-2/run-vcs-shots.sh
#
# The harness is the lane's own `vedit-workspace-vcs-selftest --ui-screenshots`
# (same one evidence/vcs-ui and evidence/svn-e2e used), so every shot asserts
# bytes, exact dimensions and a minimum number of sampled colours.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SHOTS="$ROOT/evidence/xcb-visual-2/shots/vcs"
LOG="$ROOT/evidence/xcb-visual-2/logs/20-vcs-screenshots.log"
mkdir -p "$SHOTS" "$ROOT/evidence/xcb-visual-2/logs"

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
    export XDG_RUNTIME_DIR=/tmp/xdg-run
    Xvfb :83 -screen 0 1500x1000x24 >/tmp/xvfb-7c-vcs.log 2>&1 &
    xvfb=$!
    trap "kill $xvfb 2>/dev/null || true" EXIT
    sleep 2
    BIN=/vedit-build/target/qt6-build/src/app/workspace-lane-vcs/vedit-workspace-vcs-selftest
    [ -x "$BIN" ] || { echo "FATAL: $BIN is missing; run bash scripts/build-qt.sh first" >&2; exit 1; }
    {
      echo "# 7c 1500x1000 xcb screenshots (Xvfb :83, screen 1500x1000x24)"
      echo "collected_at: $(date --iso-8601=seconds)"
      echo "xvfb=$(pgrep -a Xvfb | head -1)"
      echo "svn=$(/vedit-build/packaging/runtime/out/AppDir/bin/svn --version --quiet 2>&1)"
      echo "git=$(/vedit-build/packaging/runtime/out/AppDir/bin/git --version 2>&1)"
    } | tee /vedit-build/evidence/xcb-visual-2/logs/20-vcs-screenshots.log
    DISPLAY=:83 QT_QPA_PLATFORM=xcb QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1 \
      "$BIN" --ui-screenshots /vedit-build/evidence/xcb-visual-2/shots/vcs --require-xcb \
      2>&1 | tee -a /vedit-build/evidence/xcb-visual-2/logs/20-vcs-screenshots.log
    grep -q "WORKSPACE-VCS-SELFTEST screenshots ok=1 " \
      /vedit-build/evidence/xcb-visual-2/logs/20-vcs-screenshots.log || {
        echo "FAIL: the screenshot probe did not report ok=1" >&2
        exit 1
      }
    if grep -E "^CHECK .* FAIL|^FAIL " /vedit-build/evidence/xcb-visual-2/logs/20-vcs-screenshots.log; then
      echo "FAIL: a screenshot check is red" >&2
      exit 1
    fi
    ls -la /vedit-build/evidence/xcb-visual-2/shots/vcs
    echo "vcs-screenshots=PASS"
  '

echo "[xcb-visual-2] PASS: 1500x1000 xcb VCS screenshots in evidence/xcb-visual-2/shots/vcs/"
