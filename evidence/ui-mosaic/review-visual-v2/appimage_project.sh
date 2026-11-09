#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual-v2/shots"
SOCK="$ROOT/.mpd/x11-rv2b"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
DISP=":96"; NAME="rv2-appimage-proj"
mkdir -p "$OUT" "$SOCK"; chmod 1777 "$SOCK"
docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" -v "$SOCK:/tmp/.X11-unix:rw" -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4
if [ -e /tmp/.X11-unix ] && [ ! -L /tmp/.X11-unix ]; then echo "ERROR real /tmp/.X11-unix"; exit 1; fi
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X96 ] || { echo "ERROR X96"; exit 1; }
docker exec "$NAME" bash -lc "mkdir -p /tmp/rv2home/.config/VEdit/vedit-app && printf 'uiTheme = \"yunwu\"\n' > /tmp/rv2home/.config/VEdit/vedit-app/settings.toml"
docker exec -d "$NAME" bash -lc "cd /vedit-build && HOME=/tmp/rv2home DISPLAY=${DISP} QT_QPA_PLATFORM=xcb APPIMAGE_EXTRACT_AND_RUN=1 ./packaging/runtime/out/VEdit-0.2.1-x86_64.AppImage --project /vedit-build/tests/fixtures/verilog-project >/tmp/appimage-project.log 2>&1"
sleep 30
python3 - "$OUT" <<'PY'
import sys
from PIL import ImageGrab
out=sys.argv[1]
im=ImageGrab.grab(xdisplay=":96")
im.save(f"{out}/appimage-project-yunwu.png")
print("grab appimage-project-yunwu",im.size)
PY
docker exec "$NAME" bash -lc "cp /tmp/appimage-project.log /vedit-build/evidence/ui-mosaic/review-visual-v2/shots/appimage-project.log" >/dev/null 2>&1 || true
docker rm -f "$NAME" >/dev/null 2>&1 || true
echo OK
