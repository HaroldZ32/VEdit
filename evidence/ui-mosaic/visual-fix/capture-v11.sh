#!/usr/bin/env bash
# V11 弹层加固取证管线（自有容器 v11-shot / display :97 / socket .mpd/x11-v11）。
#
# 在真实 Xvfb+xcb 下分别用 QT_STYLE_OVERRIDE=fusion 与 windows 启动产品：
#   * XTest 点工具栏 Open Folder…（约 243,44），再在文件列表内右键开菜单；
#   * 关掉对话框后再点应用自己的 View 菜单。
# 截图由宿主的 python3 + libX11/libXtst/PIL 抓取（镜像里没有 PIL）。
#
# 用法：bash evidence/ui-mosaic/visual-fix/capture-v11.sh
# 产物：evidence/ui-mosaic/visual-fix/v11-*.png
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/visual-fix"
SOCK="$ROOT/.mpd/x11-v11"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":97"
NAME="v11-shot"
mkdir -p "$OUT" "$SOCK"
chmod 1777 "$SOCK"

cleanup() {
  docker rm -f "$NAME" >/dev/null 2>&1 || true
  if [ -L /tmp/.X11-unix ] && [ "$(readlink /tmp/.X11-unix)" = "$SOCK" ]; then
    rm -f /tmp/.X11-unix
  fi
}
trap cleanup EXIT
cleanup

if [ -e /tmp/.X11-unix ] || [ -L /tmp/.X11-unix ]; then
  echo "ERROR: /tmp/.X11-unix already exists; refusing to replace another session's socket link" >&2
  exit 1
fi

docker run -d --name "$NAME" \
  -v "$SOCK:/tmp/.X11-unix:rw" -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X97 ] || { echo "ERROR: X97 socket missing" >&2; exit 1; }

for style in fusion windows; do
  docker exec "$NAME" bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"yunwu\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d "$NAME" bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb QT_STYLE_OVERRIDE=${style} ${APP} --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${style}.log 2>&1"
  sleep 8
  python3 "$ROOT/evidence/ui-mosaic/visual-fix/driver_v11.py" "$OUT" "$style" "$DISP"
  docker exec "$NAME" bash -lc "cp /tmp/app-${style}.log /vedit-build/evidence/ui-mosaic/visual-fix/v11-app-${style}.log" >/dev/null 2>&1 || true
done
docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
echo "OK: screenshots in $OUT"
