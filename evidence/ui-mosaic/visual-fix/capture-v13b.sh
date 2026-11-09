#!/usr/bin/env bash
# V13b 取证管线：标题 padding 4px→1px 后重拍 Project 面板（容器 v13b-shot / display :99）。
#
# 分别以 yunwu 与 yaoshi 启动产品（每次独立 --state-dir，settings.toml 写 uiTheme），
# 等窗口画出后抓整屏，再裁出左上角 Project dock 作为 v13b-project-<theme>.png。
#
# 用法：bash evidence/ui-mosaic/visual-fix/capture-v13b.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/visual-fix"
SOCK="$ROOT/.mpd/x11-v13b"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":99"
NAME="v13b-shot"
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
[ -S /tmp/.X11-unix/X99 ] || { echo "ERROR: X99 socket missing" >&2; exit 1; }

for theme in yunwu yaoshi; do
  STATE="/tmp/v13b-state-${theme}"
  docker exec "$NAME" bash -lc "rm -rf '${STATE}'; mkdir -p '${STATE}' && printf 'uiTheme = \"${theme}\"\n' > '${STATE}/settings.toml'"
  docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d "$NAME" bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb QT_STYLE_OVERRIDE=fusion ${APP} --state-dir '${STATE}' --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${theme}.log 2>&1"
  sleep 8
  python3 "$ROOT/evidence/ui-mosaic/visual-fix/driver_v13b.py" "$OUT" "$theme" "$DISP"
  docker exec "$NAME" bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/visual-fix/v13b-app-${theme}.log" >/dev/null 2>&1 || true
done
docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
echo "OK: V13b screenshots in $OUT"
