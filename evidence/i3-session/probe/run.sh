#!/usr/bin/env bash
# Runs the T19 lane probe in the pinned image: the A/B restart scenario (offscreen,
# with the real KTextEditor provider), then the screenshot/dialog pass under
# Xvfb + xcb (software rendering) plus the archive sample.
#
#   bash evidence/i3-session/probe/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
OUT="$ROOT/evidence/i3-session"
mkdir -p "$OUT/shots"

docker run --rm -e VEDIT_WORKSPACE=/vedit-build -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc '
set -uo pipefail
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

# The pinned build image carries DejaVu only; the delivered runtime installs
# NotoSansCJK-Regular.ttc, so reproduce that font set for the screenshots.
if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
  mkdir -p /usr/share/fonts/truetype/vedit
  cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
  fc-cache -f >/dev/null 2>&1 || true
fi

echo "== rebuild the two product libraries the probe links =="
cmake --build target/qt6-build --target vedit_app_shell vedit_editor_core --parallel "$(nproc)" 2>&1 | tail -4

echo "== build the probe (links the product static libraries) =="
bash evidence/i3-session/probe/build.sh 2>&1 | tail -3
probe=target/i3-session-probe/session-probe
if [ ! -x "$probe" ]; then echo "FATAL: probe not built"; exit 1; fi

echo "== headless assertions (QT_QPA_PLATFORM=offscreen) =="
QT_QPA_PLATFORM=offscreen stdbuf -oL -eL "$probe" --checks 2>&1 | tee evidence/i3-session/10-probe-offscreen.log
offscreen_rc=${PIPESTATUS[0]}

echo "== screenshot + dialog pass (Xvfb, xcb, software rendering) =="
Xvfb :79 -screen 0 1500x1000x24 >/tmp/xvfb-i3-session.log 2>&1 &
xvfb_pid=$!
trap "kill $xvfb_pid 2>/dev/null || true" EXIT
sleep 2
DISPLAY=:79 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 240 stdbuf -oL -eL "$probe" --screenshots evidence/i3-session/shots \
  --archive-out evidence/i3-session/30-session.json 2>&1 \
  | tee evidence/i3-session/11-probe-xcb.log
xcb_rc=${PIPESTATUS[0]}
kill $xvfb_pid 2>/dev/null || true

echo "== fail-first control: destroy the archived position of A -> the new assertions MUST go RED =="
QT_QPA_PLATFORM=offscreen stdbuf -oL -eL "$probe" --checks --corrupt-a-position 2>&1 \
  | tee evidence/i3-session/13-fail-first-offscreen.log
corrupt_rc=${PIPESTATUS[0]}
red_hits=$(grep -c -E "^FAIL after\\.a-(caret-line|caret-column|scroll-line)" evidence/i3-session/13-fail-first-offscreen.log || true)
echo "fail-first: rc=$corrupt_rc red-assertions=$red_hits"
if [ "$corrupt_rc" -eq 0 ] || [ "$red_hits" -lt 2 ]; then
  echo "FATAL: the fail-first control did not go red (rc=$corrupt_rc red=$red_hits): the A assertions cannot detect a lost position"
  exit 1
fi

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc fail-first rc=$corrupt_rc (expected non-zero) =="
if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ]; then exit 1; fi
echo "SESSION-PROBE-OK"
' 2>&1 | tee "$OUT/12-probe-run.log"
