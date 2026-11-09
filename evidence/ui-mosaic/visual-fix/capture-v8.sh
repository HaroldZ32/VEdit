#!/usr/bin/env bash
# V8 截图管线（Senior Engineer 自有，display :98，socket .mpd/x11-v8）。
# 与 review-visual-v2/capture_v2.sh 同一套做法：容器内 Xvfb + xcb 平台，
# 宿主机用 PIL.ImageGrab 抓同一块 X server 的像素。
#
# 用法：
#   bash evidence/ui-mosaic/visual-fix/capture-v8.sh before|after
#
# 产物（输出目录 evidence/ui-mosaic/visual-fix/shots-v8/）：
#   v8-<phase>-<theme>.png          六套主题，默认状态（无工程）
#   v8-<phase>-project-<theme>.png  六套主题，打开 tests/fixtures/verilog-project
#
# 每个主题的 UI 主题来自容器内 /root/.config/VEdit/vedit-app/settings.toml
# （与实机一致：产品把 uiTheme 持久化在 settings 里）。
set -euo pipefail
PHASE="${1:?usage: capture-v8.sh before|after}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${V8_SHOTS:-$ROOT/evidence/ui-mosaic/visual-fix/shots-v8}"
SOCK="$ROOT/.mpd/x11-v8"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":98"
NAME="v8-shot"
mkdir -p "$OUT" "$SOCK"
chmod 1777 "$SOCK"
: > "$OUT/capture-$PHASE.log"

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
  python3 - "$OUT" "$1" <<'PY' | tee -a "$OUT/capture-$PHASE.log"
import sys
from PIL import ImageGrab
out, name = sys.argv[1], sys.argv[2]
im = ImageGrab.grab(xdisplay=":98")
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

for theme in yunwu xinghuang bohe qingkong yaoshi shenye; do
  start_app "$theme"
  grab "v8-${PHASE}-${theme}"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/visual-fix/shots-v8/app-${theme}-${PHASE}.log" >/dev/null 2>&1 || true
done
stop_app

for theme in yunwu xinghuang bohe qingkong yaoshi shenye; do
  start_app "$theme" --project /vedit-build/tests/fixtures/verilog-project
  grab "v8-${PHASE}-project-${theme}"
done
stop_app
echo "OK: $PHASE 截图写入 $OUT"
