#!/usr/bin/env bash
# Batch 1b acceptance probe — runs INSIDE the pinned Qt6/KF6 image.
#
#   * builds the probe from the product's own ninja compile/link lines (build.sh,
#     nothing under src/ is recompiled for it);
#   * runs the AC-1.3 / AC-1.4 assertions headless (QT_QPA_PLATFORM=offscreen);
#   * runs the same scenario under Xvfb :78 with QT_QPA_PLATFORM=xcb and saves the
#     View-menu and restored-panels screenshots.
#
# Invoked by panels-run.sh, which supplies the container and the workspace mount.
set -uo pipefail
WORK="${VEDIT_WORKSPACE:-/vedit-build}"
cd "$WORK" || exit 2

OUT="$WORK/evidence/batch1-shell"
SHOTS="$OUT/shots"
mkdir -p "$OUT" "$SHOTS"

overall=0

echo "== probe build (product ninja compile + link lines, edit-core enabled) =="
bash evidence/batch1-shell/probe/build.sh 2>&1 | tee "$OUT/20-probe-panels-build.log"
build_rc=${PIPESTATUS[0]}
if [ "$build_rc" -ne 0 ]; then
  echo "FATAL: probe build failed (rc=$build_rc)" >&2
  exit "$build_rc"
fi
probe="target/batch1-shell-probe/batch1-shell-probe"

slang="${VEDIT_SLANG:-$WORK/build/slang/out/slang}"
if [ -x "$slang" ]; then
  export VEDIT_SLANG="$slang"
fi
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

echo "== offscreen pass: panels recoverable (AC-1.3) + Save As (AC-1.4) =="
QT_QPA_PLATFORM=offscreen timeout 600 "$probe" --panels-checks 2>&1 \
  | tee "$OUT/21-probe-panels-offscreen.log"
offscreen_rc=${PIPESTATUS[0]}
echo "offscreen exit=$offscreen_rc"

echo "== xcb pass under Xvfb :78 (QT_QPA_PLATFORM=xcb, software rendering) =="
Xvfb :78 -screen 0 1500x1000x24 >/tmp/xvfb-batch1b.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 2
DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 600 "$probe" --panels-screenshots "$SHOTS" 2>&1 \
  | tee "$OUT/22-probe-panels-xcb.log"
xcb_rc=${PIPESTATUS[0]}
kill "$xvfb_pid" 2>/dev/null || true
trap - EXIT
echo "xcb exit=$xcb_rc"

shots_missing=0
for shot in "$SHOTS/30-view-menu-panels.png" "$SHOTS/31-panels-restored.png"; do
  if [ -s "$shot" ]; then
    echo "shot: $(stat -c '%n %s bytes' "$shot")"
  else
    echo "FATAL: missing screenshot $shot" >&2
    shots_missing=1
  fi
done

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc shots_missing=$shots_missing =="
if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ] || [ "$shots_missing" -ne 0 ]; then
  overall=1
fi
if [ "$overall" -ne 0 ]; then
  echo "BATCH1B-PROBE-FAILED"
  exit 1
fi
echo "BATCH1B-PROBE-OK"
