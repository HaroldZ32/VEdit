#!/usr/bin/env bash
# V10 选中行取证管线（自有容器 v10-shot / display :96 / socket .mpd/x11-v10）。
# 在真实 Xvfb+xcb 下用 XTest 左键点文件树一行、右键开菜单，再由宿主抓像素。
#
# 用法：bash evidence/ui-mosaic/visual-fix/capture-v10.sh
# 产物：evidence/ui-mosaic/visual-fix/shots-v10/<theme>-{tree-selected,ctxmenu,ctxmenu-hover}.png
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/visual-fix/shots-v10"
SOCK="$ROOT/.mpd/x11-v10"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":96"
NAME="v10-shot"
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
[ -S /tmp/.X11-unix/X96 ] || { echo "ERROR: X96 socket missing" >&2; exit 1; }

for theme in yunwu yaoshi; do
  docker exec "$NAME" bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d "$NAME" bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb ${APP} --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${theme}.log 2>&1"
  sleep 14
  python3 "$ROOT/evidence/ui-mosaic/visual-fix/driver_v10.py" "$OUT" "$theme" "$DISP"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/visual-fix/shots-v10/app-${theme}.log" >/dev/null 2>&1 || true
done
docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
echo "OK: screenshots in $OUT"
