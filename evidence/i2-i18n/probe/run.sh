#!/usr/bin/env bash
# Runs the T18 i18n lane probe in the pinned image:
#   1. rebuild the product targets the probe links (including the new i18n
#      runtime and the compiled .qm under target/qt6-build/src/app/translations),
#   2. build the probe against those static libraries,
#   3. headless assertions (offscreen): install/idempotence, the three AC-4.2
#      branches, the one-dialog rule, and the session.json sha/mtime checks,
#   4. the same cancel branch under Xvfb + xcb with a REAL batch dialog and the
#      required "语言未切换：重启已取消" message, plus the zh/en shell screenshots.
#
#   bash evidence/i2-i18n/probe/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="${VEDIT_QT6_BUILD_IMAGE_REF:-vedit-qt6-build:local}"
OUT="$ROOT/evidence/i2-i18n"
mkdir -p "$OUT/shots"

docker run --rm -e VEDIT_WORKSPACE=/vedit-build -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" bash -lc '
set -uo pipefail
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

if [ -f packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc ]; then
  mkdir -p /usr/share/fonts/truetype/vedit
  cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/vedit/
  fc-cache -f >/dev/null 2>&1 || true
fi

echo "== rebuild the product targets the probe links =="
cmake --build target/qt6-build --target vedit-app --parallel "$(nproc)" 2>&1 | tail -3

echo "== translations =="
translations=target/qt6-build/src/app/translations
ls -la "$translations" || { echo "FATAL: $translations missing"; exit 1; }

echo "== build the probe (links the product static libraries) =="
bash evidence/i2-i18n/probe/build.sh 2>&1 | tail -3
probe=target/i18n-probe/i18n-probe
[ -x "$probe" ] || { echo "FATAL: probe not built"; exit 1; }

echo "== headless assertions (QT_QPA_PLATFORM=offscreen) =="
QT_QPA_PLATFORM=offscreen stdbuf -oL -eL "$probe" --checks \
  --translations "/vedit-build/$translations" 2>&1 | tee evidence/i2-i18n/10-probe-offscreen.log
offscreen_rc=${PIPESTATUS[0]}

echo "== screenshots + real batch dialog (Xvfb, xcb) =="
Xvfb :80 -screen 0 1500x1000x24 >/tmp/xvfb-i18n.log 2>&1 &
xvfb_pid=$!
trap "kill $xvfb_pid 2>/dev/null || true" EXIT
sleep 2
DISPLAY=:80 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 240 stdbuf -oL -eL "$probe" --screenshots evidence/i2-i18n/shots \
  --translations "/vedit-build/$translations" 2>&1 | tee evidence/i2-i18n/11-probe-xcb.log
xcb_rc=${PIPESTATUS[0]}
kill $xvfb_pid 2>/dev/null || true

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc =="

echo "== real relaunch: the default restartApplication spawns this probe and exits =="
marker=/tmp/i18n-restart-marker
rm -rf "$marker" && mkdir -p "$marker"
QT_QPA_PLATFORM=offscreen timeout 30 "$probe" --restart-spawn-check "$marker" \
  > evidence/i2-i18n/13-restart-spawn.log 2>&1
spawn_rc=$?
for _ in $(seq 100); do [ -f "$marker/relaunched.txt" ] && break; sleep 0.1; done
{
  echo "parent-exit=$spawn_rc"
  echo "parent-marker=$(test -f "$marker/parent.txt" && echo yes || echo no)"
  echo "relaunched-marker=$(test -f "$marker/relaunched.txt" && echo yes || echo no)"
  echo "unexpected-return=$(test -f "$marker/unexpected-return.txt" && echo yes || echo no)"
} | tee -a evidence/i2-i18n/13-restart-spawn.log
if [ "$spawn_rc" -ne 0 ] || [ ! -f "$marker/relaunched.txt" ] \
   || [ -f "$marker/unexpected-return.txt" ]; then
  echo "FATAL: restartApplication did not relaunch-and-exit (rc=$spawn_rc)"
  exit 1
fi

if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ]; then exit 1; fi
echo "I18N-PROBE-OK"
' 2>&1 | tee "$OUT/12-probe-run.log"

echo "== shipped binary startup (zh_CN / auto) =="
bash "$HERE/real-start.sh"

echo "T18-EVIDENCE-OK"
