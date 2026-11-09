#!/usr/bin/env bash
# Reviewer-owned capture: container vra-shot, display :93, socket .mpd/x11-vra.
# Self-contained on purpose: the host /tmp and the container /tmp are different
# views, so the /tmp/.X11-unix symlink must be re-created in the SAME bash call
# that grabs the pixels.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual/shots"
SOCK="$ROOT/.mpd/x11-vra"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":93"
THEMES=("$@")
if [ ${#THEMES[@]} -eq 0 ]; then THEMES=(yunwu xinghuang bohe qingkong yaoshi shenye); fi

[ -x "$ROOT/target/qt6-build/src/app/vedit-app" ] || { echo "ERROR: build the app first" >&2; exit 1; }
mkdir -p "$OUT" "$SOCK"
chmod 1777 "$SOCK"

cleanup() { docker rm -f vra-shot >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup

docker run -d --name vra-shot \
  -v "$SOCK:/tmp/.X11-unix:rw" \
  -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4

# Same process boundary as the grab below.
if [ -e /tmp/.X11-unix ] && [ ! -L /tmp/.X11-unix ]; then
  echo "ERROR: /tmp/.X11-unix is a real directory; refusing to delete it" >&2
  exit 1
fi
rm -f /tmp/.X11-unix
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X93 ] || { echo "ERROR: X93 socket missing" >&2; exit 1; }

for theme in "${THEMES[@]}"; do
  docker exec vra-shot bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec vra-shot bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d vra-shot bash -lc "DISPLAY=${DISP} QT_QPA_PLATFORM=xcb ${APP} >/tmp/app-${theme}.log 2>&1"
  sleep 12
  python3 - "$OUT" "$theme" <<'PY'
import sys
from PIL import ImageGrab
out, theme = sys.argv[1], sys.argv[2]
im = ImageGrab.grab(xdisplay=":93")
im.save(f"{out}/v3-{theme}.png")
print(f"captured {theme} {im.size}")
PY
  docker exec vra-shot bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual/shots/app-${theme}.log" >/dev/null 2>&1 || true
done

echo "OK: reviewer screenshots in $OUT"
