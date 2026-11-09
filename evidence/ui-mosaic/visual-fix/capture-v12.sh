#!/usr/bin/env bash
# V12 取证管线（自有容器 v12-shot / display :98 / socket .mpd/x11-v12）。
#
# 在真实 Xvfb+xcb 下分别用 QT_STYLE_OVERRIDE=fusion 与 windows 启动产品：
#   * 每个风格用独立的 --state-dir（/tmp/v12-state-<style>），这样
#     ui-menu-diag.log 不会混入其它运行；
#   * XTest 点工具栏 Open Folder…（约 243,44），在文件列表内右键开菜单；
#   * 关掉对话框后再点应用自己的 View 菜单；
#   * 抓 v12-*.png，并把 ui-menu-diag.log 原样复制进 evidence。
#
# 用法：bash evidence/ui-mosaic/visual-fix/capture-v12.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/visual-fix"
SOCK="$ROOT/.mpd/x11-v12"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":98"
NAME="v12-shot"
mkdir -p "$OUT" "$SOCK"
chmod 1777 "$SOCK"

cleanup() {
  docker rm -f "$NAME" >/dev/null 2>&1 || true
  # 只清理自己建的 socket 链接：别的会话仍在使用时不动它。
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
[ -S /tmp/.X11-unix/X98 ] || { echo "ERROR: X98 socket missing" >&2; exit 1; }

for style in fusion windows; do
  STATE="/tmp/v12-state-${style}"
  docker exec "$NAME" bash -lc "rm -rf '${STATE}'; mkdir -p '${STATE}' && printf 'uiTheme = \"yunwu\"\n' > '${STATE}/settings.toml'"
  docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d "$NAME" bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb QT_STYLE_OVERRIDE=${style} ${APP} --state-dir '${STATE}' --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${style}.log 2>&1"
  sleep 8
  python3 "$ROOT/evidence/ui-mosaic/visual-fix/driver_v12.py" "$OUT" "$style" "$DISP"
  docker exec "$NAME" bash -lc "cp /tmp/app-${style}.log /vedit-build/evidence/ui-mosaic/visual-fix/v12-app-${style}.log" >/dev/null 2>&1 || true
  if docker exec "$NAME" test -f "${STATE}/ui-menu-diag.log"; then
    docker exec "$NAME" cat "${STATE}/ui-menu-diag.log" > "$OUT/v12-menu-diag-${style}.log"
    echo "--- ${style}: ${STATE}/ui-menu-diag.log ---"
    cat "$OUT/v12-menu-diag-${style}.log"
  else
    echo "ERROR: ${style}: ${STATE}/ui-menu-diag.log was not written" >&2
    exit 1
  fi
done
docker exec "$NAME" bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
echo "OK: screenshots + menu diagnostics in $OUT"
