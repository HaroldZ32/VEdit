#!/usr/bin/env bash
# Runs the T12 lane probe in the pinned image: headless assertions (offscreen)
# and the screenshot/dialog pass (Xvfb + xcb, software rendering).
#
#   bash evidence/e1-tabs/probe/run.sh
#
# Evidence lands in evidence/e1-tabs/ (logs + shots/).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
OUT="$ROOT/evidence/e1-tabs"
mkdir -p "$OUT/shots"

docker run --rm -e VEDIT_WORKSPACE=/vedit-build -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc '
set -uo pipefail
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

# The pinned build image carries DejaVu only. The DELIVERED runtime installs
# NotoSansCJK-Regular.ttc (packaging/runtime/assets/fonts), and the unsaved
# indicator is Chinese, so reproduce that font set here instead of recording a
# screenshot full of tofu boxes.
if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
  mkdir -p /usr/share/fonts/truetype/vedit
  cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
  fc-cache -f >/dev/null 2>&1 || true
fi

echo "== rebuild the two product libraries the probe links =="
cmake --build target/qt6-build --target vedit_app_shell vedit_editor_core --parallel "$(nproc)" 2>&1 | tail -8

echo "== build the probe (links the product static libraries) =="
bash evidence/e1-tabs/probe/build.sh 2>&1 | tail -3
probe=target/e1-tabs-probe/tabs-probe
if [ ! -x "$probe" ]; then echo "FATAL: probe not built"; exit 1; fi

echo "== headless assertions (QT_QPA_PLATFORM=offscreen) =="
QT_QPA_PLATFORM=offscreen stdbuf -oL -eL "$probe" --checks 2>&1 | tee evidence/e1-tabs/10-probe-offscreen.log
offscreen_rc=${PIPESTATUS[0]}

echo "== screenshot + dialog pass (Xvfb, xcb, software rendering) =="
Xvfb :78 -screen 0 1500x1000x24 >/tmp/xvfb-e1-tabs.log 2>&1 &
xvfb_pid=$!
trap "kill $xvfb_pid 2>/dev/null || true" EXIT
sleep 2
DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 180 stdbuf -oL -eL "$probe" --screenshots evidence/e1-tabs/shots 2>&1 \
  | tee evidence/e1-tabs/11-probe-xcb.log
xcb_rc=${PIPESTATUS[0]}
kill $xvfb_pid 2>/dev/null || true

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc =="
if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ]; then exit 1; fi
echo "TABS-PROBE-OK"
' 2>&1 | tee "$OUT/12-probe-run.log"
