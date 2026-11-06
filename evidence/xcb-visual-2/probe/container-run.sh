#!/usr/bin/env bash
# Runs INSIDE the pinned Qt6/KF6 image (invoked by run-panels.sh).
#
# Builds the batch-1 acceptance probe from the product's own ninja compile and
# link lines (nothing under src/ is recompiled for it), then captures the
# 1500x1000 xcb screenshots that prove the persisted project root reaches the
# four panels. The probe source is a copy of evidence/batch1-shell/probe/main.cpp
# (unchanged); only the build/output paths point into this batch's evidence dir.
set -uo pipefail

WORK="${VEDIT_WORKSPACE:-/vedit-build}"
cd "$WORK" || exit 2
OUT="$WORK/evidence/xcb-visual-2"
LOGS="$OUT/logs"
SHOTS="$OUT/shots/panels"
mkdir -p "$LOGS" "$SHOTS"

overall=0

echo "== probe build (product ninja compile + link lines, edit-core enabled) =="
bash evidence/xcb-visual-2/probe/build.sh 2>&1 | tee "$LOGS/10-probe-build.log"
build_rc=${PIPESTATUS[0]}
echo "probe_build_exit=$build_rc"
if [ "$build_rc" -ne 0 ]; then
  echo "FATAL: probe build failed (rc=$build_rc)" >&2
  exit "$build_rc"
fi
probe="target/xcb-visual-2-probe/batch1-shell-probe"

slang="${VEDIT_SLANG:-$WORK/build/slang/out/slang}"
if [ -x "$slang" ]; then
  export VEDIT_SLANG="$slang"
fi
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

Xvfb :71 -screen 0 1500x1000x24 >/tmp/xvfb-7c-panels.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 2
echo "xvfb=$(pgrep -a Xvfb | head -1)"

echo "== --screenshots: ordinary start (seeded session, no --project) + root switch =="
DISPLAY=:71 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 600 "$probe" --screenshots "$SHOTS" 2>&1 | tee "$LOGS/11-probe-screenshots.log"
rc1=${PIPESTATUS[0]}
echo "screenshots_exit=$rc1"

echo "== --panels-screenshots: View menu + restored panels =="
DISPLAY=:71 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 600 "$probe" --panels-screenshots "$SHOTS" 2>&1 | tee "$LOGS/12-probe-panels.log"
rc2=${PIPESTATUS[0]}
echo "panels_screenshots_exit=$rc2"

kill "$xvfb_pid" 2>/dev/null || true
trap - EXIT

shots_missing=0
for shot in 10-ordinary-start-root-a.png 20-after-switch-root-b.png \
            30-view-menu-panels.png 31-panels-restored.png; do
  if [ -s "$SHOTS/$shot" ]; then
    echo "shot: $(stat -c '%n %s bytes' "$SHOTS/$shot")"
  else
    echo "FATAL: missing screenshot $SHOTS/$shot" >&2
    shots_missing=1
  fi
done
grep -E '^PASS .*screenshot|^CHECK .*screenshot' "$LOGS/11-probe-screenshots.log" "$LOGS/12-probe-panels.log" || true

echo "== result: probe_build=$build_rc screenshots=$rc1 panels=$rc2 shots_missing=$shots_missing =="
if [ "$rc1" -ne 0 ] || [ "$rc2" -ne 0 ] || [ "$shots_missing" -ne 0 ]; then
  overall=1
fi
if [ "$overall" -ne 0 ]; then
  echo "PANELS-SHOTS FAILED"
  exit 1
fi
echo "PANELS-SHOTS OK"
