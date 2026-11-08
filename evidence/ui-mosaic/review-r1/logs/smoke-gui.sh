#!/usr/bin/env bash
# Smoke the review's GUI chain on the PRE-FIX binary kept under logs/:
# shared X socket -> Xvfb in the pinned image -> real click through XTest ->
# host Pillow screenshot. This proves the harness can drive the real UI before
# it is used to judge the repair.
set -euo pipefail
ROOT="/home/haroldzhao/MyProj/DshProj/VEdit"
LOGS="$ROOT/evidence/ui-mosaic/review-r1/logs"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
XDIR="$LOGS/x11"
NAME=review-r1-x

cleanup() { docker rm -f "$NAME" >/dev/null 2>&1 || true; }
cleanup
mkdir -p "$XDIR"
chmod 1777 "$XDIR"

docker run -d --name "$NAME" \
  -v "$XDIR:/tmp/.X11-unix:rw" \
  -v "$ROOT:/vedit-build" \
  -w /vedit-build "$IMG" \
  bash -lc 'Xvfb :99 -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000' >/dev/null
sleep 4

rm -rf /tmp/.X11-unix
ln -sfn "$XDIR" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X99 ] || { echo "ERROR: X99 socket missing" >&2; exit 1; }

docker exec "$NAME" bash -lc 'mkdir -p /state && printf "uiTheme = \"bohe\"\n" > /state/settings.toml'

docker exec -d "$NAME" bash -lc \
  'DISPLAY=:99 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 /vedit-build/evidence/ui-mosaic/review-r1/logs/prefix-vedit-app --state-dir /state --hold 60000 >/tmp/app.log 2>&1'
sleep 12

python3 "$LOGS/x11.py" shot "$LOGS/smoke-00-start.png"
# Real click on the View menu (observed menubar: File Edit View Project Help).
python3 "$LOGS/x11.py" chain "click 103 11; sleep 500; shot $LOGS/smoke-01-viewmenu.png"
python3 "$LOGS/x11.py" chain "key t; sleep 600; shot $LOGS/smoke-02-thememenu.png"

echo '--- app log tail ---'
docker exec "$NAME" bash -lc 'tail -5 /tmp/app.log' || true
echo '--- settings.toml now ---'
docker exec "$NAME" bash -lc 'cat /state/settings.toml' || true
echo 'SMOKE-DONE'
