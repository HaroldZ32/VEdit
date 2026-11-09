#!/usr/bin/env bash
# Reviewer-V2 delivered-artefact check: run the SHIPPED AppImage inside Xvfb.
# Container rv2-appimage, display :98, socket .mpd/x11-rv2a.
# APPIMAGE_EXTRACT_AND_RUN=1 avoids FUSE; HOME is a private directory so the
# product theme setting written here cannot leak into the reviewer's account.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual-v2/shots"
SOCK="$ROOT/.mpd/x11-rv2a"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APPIMAGE="packaging/runtime/out/VEdit-0.2.1-x86_64.AppImage"
DISP=":98"
NAME="rv2-appimage"
mkdir -p "$OUT" "$SOCK"
chmod 1777 "$SOCK"

cleanup() { docker rm -f "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup

docker run -d --name "$NAME" \
  -v "$SOCK:/tmp/.X11-unix:rw" -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4
if [ -e /tmp/.X11-unix ] && [ ! -L /tmp/.X11-unix ]; then
  echo "ERROR: /tmp/.X11-unix is a real directory; refusing to replace it" >&2
  exit 1
fi
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X98 ] || { echo "ERROR: X98 socket missing" >&2; exit 1; }

grab() { # grab <name>
  python3 - "$OUT" "$1" <<'PY'
import sys
from PIL import ImageGrab
out, name = sys.argv[1], sys.argv[2]
im = ImageGrab.grab(xdisplay=":98")
im.save(f"{out}/{name}.png")
print(f"grab {name} {im.size}")
PY
}

run_theme() { # run_theme <theme> <shot-name>
  local theme="$1" name="$2"
  docker exec "$NAME" bash -lc "mkdir -p /tmp/rv2home/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /tmp/rv2home/.config/VEdit/vedit-app/settings.toml"
  docker exec "$NAME" bash -lc "pkill -f 'VEdit-0.2.1' || true; pkill -f 'vd-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d "$NAME" bash -lc "cd /vedit-build && HOME=/tmp/rv2home DISPLAY=${DISP} QT_QPA_PLATFORM=xcb APPIMAGE_EXTRACT_AND_RUN=1 ./${APPIMAGE} >/tmp/appimage-${theme}.log 2>&1"
  sleep 28
  grab "$name"
  docker exec "$NAME" bash -lc "cp /tmp/appimage-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual-v2/shots/appimage-${theme}.log" >/dev/null 2>&1 || true
}

run_theme yunwu appimage-yunwu
run_theme shenye appimage-shenye
echo "OK: AppImage screenshots in $OUT"
