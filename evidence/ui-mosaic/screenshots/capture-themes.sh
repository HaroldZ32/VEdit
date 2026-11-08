#!/usr/bin/env bash
# 抓 6 套产品主题的真实运行截图（Xvfb + xcb + 宿主 Pillow）。
#
# 为什么这么写：
#  * 容器里只有 Xvfb，没有 import/scrot/xwd/ffmpeg，所以抓屏必须由宿主做；
#  * 宿主 bash 的 /tmp 与 docker 不共享（同一路径看到两套内容），因此 X socket 放在工作区
#    目录里，再在**同一次进程边界内**把 /tmp/.X11-unix 指向它 —— 跨调用会失效；
#  * 主题选择走 settings.toml 的 uiTheme（应用启动时读取），避免依赖 xdotool 之类的 GUI 自动化。
#
# 用法：bash evidence/ui-mosaic/screenshots/capture-themes.sh [theme-id ...]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/screenshots"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
THEMES=("$@")
if [ ${#THEMES[@]} -eq 0 ]; then THEMES=(yunwu xinghuang bohe qingkong yaoshi shenye); fi

[ -x "$ROOT/target/qt6-build/src/app/vedit-app" ] || { echo "ERROR: 先构建 vedit-app" >&2; exit 1; }
mkdir -p "$OUT" "$ROOT/.mpd/x11"
chmod 1777 "$ROOT/.mpd/x11"

cleanup() { docker rm -f vedit-shot >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup

docker run -d --name vedit-shot \
  -v "$ROOT/.mpd/x11:/tmp/.X11-unix:rw" \
  -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc 'Xvfb :99 -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000' >/dev/null
sleep 4

# 必须在本次调用内建立，否则下一次 bash 调用看不到（/tmp 与 docker 视图不同）
rm -rf /tmp/.X11-unix
ln -sfn "$ROOT/.mpd/x11" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X99 ] || { echo "ERROR: X99 socket 未出现" >&2; exit 1; }

for theme in "${THEMES[@]}"; do
  docker exec vedit-shot bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec vedit-shot bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d vedit-shot bash -lc "DISPLAY=:99 QT_QPA_PLATFORM=xcb ${APP} >/tmp/app.log 2>&1"
  sleep 12
  python3 - "$OUT" "$theme" <<'PY'
import sys
from PIL import ImageGrab
out, theme = sys.argv[1], sys.argv[2]
im = ImageGrab.grab(xdisplay=":99")
im.save(f"{out}/after-{theme}.png")
print(f"captured {theme} {im.size}")
PY
  docker exec vedit-shot bash -lc "cp /tmp/app.log /vedit-build/evidence/ui-mosaic/screenshots/app-${theme}.log" >/dev/null 2>&1 || true
done

echo "OK: 截图写入 $OUT"
