#!/usr/bin/env bash
# Reviewer-owned interactive capture v2: click-to-focus then keys.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual/shots"
SOCK="$ROOT/.mpd/x11-vra"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
DISP=":93"

mkdir -p "$OUT" "$SOCK"; chmod 1777 "$SOCK"
docker rm -f vra-ui2 >/dev/null 2>&1 || true
docker run -d --name vra-ui2 \
  -v "$SOCK:/tmp/.X11-unix:rw" -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4
if [ -e /tmp/.X11-unix ] && [ ! -L /tmp/.X11-unix ]; then echo "ERROR: /tmp/.X11-unix real dir"; exit 1; fi
rm -f /tmp/.X11-unix
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X93 ] || { echo "ERROR: X93 missing"; exit 1; }

for theme in yunwu shenye; do
  docker exec vra-ui2 bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec vra-ui2 bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d vra-ui2 bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb ./target/qt6-build/src/app/vedit-app --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${theme}.log 2>&1"
  sleep 14
  python3 "$OUT/../input_driver.py" "$OUT" "$theme"
  docker exec vra-ui2 bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual/shots/app3-${theme}.log" >/dev/null 2>&1 || true
done
docker rm -f vra-ui2 >/dev/null 2>&1 || true
echo "OK"
