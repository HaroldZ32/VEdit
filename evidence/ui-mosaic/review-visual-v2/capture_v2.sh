#!/usr/bin/env bash
# Reviewer-V2-owned capture: container rv2-shot, display :97, socket .mpd/x11-rv2.
# Self-contained on purpose: the host /tmp and the container /tmp are different
# views, so /tmp/.X11-unix is re-created in the SAME bash call that grabs pixels.
#
# Passes:
#   1. six themes, default (no project)  -> src-<theme>.png
#   2. project fixture, yunwu + yaoshi   -> project-<theme>.png
#   3. interactive yunwu + shenye        -> <theme>-{menu,terminal,dialog,hover,pressed}.png
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual-v2/shots"
SOCK="$ROOT/.mpd/x11-rv2"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":97"
NAME="rv2-shot"
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
[ -S /tmp/.X11-unix/X97 ] || { echo "ERROR: X97 socket missing" >&2; exit 1; }

grab() { # grab <name>
  python3 - "$OUT" "$1" <<'PY'
import sys
from PIL import ImageGrab
out, name = sys.argv[1], sys.argv[2]
im = ImageGrab.grab(xdisplay=":97")
im.save(f"{out}/{name}.png")
print(f"grab {name} {im.size}")
PY
}

stop_app() { docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true; sleep 1; }

start_app() { # start_app <theme> [extra args...]
  local theme="$1"; shift
  docker exec "$NAME" bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  stop_app
  docker exec -d "$NAME" bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb ${APP} $* >/tmp/app-${theme}.log 2>&1"
  sleep 13
}

# ---- pass 1: six themes, default state -------------------------------------
for theme in yunwu xinghuang bohe qingkong yaoshi shenye; do
  start_app "$theme"
  grab "src-${theme}"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual-v2/shots/app-${theme}.log" >/dev/null 2>&1 || true
done
stop_app

# ---- pass 2: project fixture (for the project-state left column / tree rows)
for theme in yunwu yaoshi; do
  start_app "$theme" --project /vedit-build/tests/fixtures/verilog-project
  grab "project-${theme}"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual-v2/shots/project-app-${theme}.log" >/dev/null 2>&1 || true
done
stop_app

# ---- pass 3: interactive surfaces -----------------------------------------
for theme in yunwu shenye; do
  start_app "$theme" --project /vedit-build/tests/fixtures/verilog-project
  python3 "$ROOT/evidence/ui-mosaic/review-visual-v2/driver_v2.py" "$OUT" "$theme" ":97"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual-v2/shots/interactive-app-${theme}.log" >/dev/null 2>&1 || true
done
stop_app

echo "OK: reviewer-v2 screenshots in $OUT"
