#!/usr/bin/env bash
# Batch 2 acceptance probe — runs INSIDE the pinned Qt6/KF6 image.
#
#   * builds the probe from the product's own ninja compile/link lines;
#   * runs the AC-2.1/AC-2.2/AC-2.3 assertions headless (offscreen);
#   * runs the same scenario under Xvfb :78 with QT_QPA_PLATFORM=xcb and saves the
#     references-list and rename screenshots.
set -uo pipefail
WORK="${VEDIT_WORKSPACE:-/vedit-build}"
cd "$WORK" || exit 2

OUT="$WORK/evidence/batch2-editor"
SHOTS="$OUT/shots"
mkdir -p "$OUT" "$SHOTS"

echo "== probe build =="
bash evidence/batch2-editor/probe/build.sh 2>&1 | tee "$OUT/00-probe-build.log"
build_rc=${PIPESTATUS[0]}
if [ "$build_rc" -ne 0 ]; then
  echo "FATAL: probe build failed (rc=$build_rc)" >&2
  exit "$build_rc"
fi
probe="target/batch2-editor-probe/batch2-editor-probe"

# Documented runtime toolchain path (AGENT.md §4, option 3): the checkout's own
# verible, which is the LSP backend this evidence needs.
verible="${VEDIT_VERIBLE_LS:-$WORK/.mpd/recon/verible/bin/verible-verilog-ls}"
if [ -x "$verible" ]; then
  export VEDIT_VERIBLE_LS="$verible"
  echo "[probe] VEDIT_VERIBLE_LS=$verible"
else
  echo "[probe] WARNING: no verible-verilog-ls at $verible"
fi
slang="${VEDIT_SLANG:-$WORK/build/slang/out/slang}"
if [ -x "$slang" ]; then
  export VEDIT_SLANG="$slang"
fi
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

echo "== offscreen pass (QT_QPA_PLATFORM=offscreen) =="
QT_QPA_PLATFORM=offscreen timeout 900 "$probe" --checks 2>&1 | tee "$OUT/10-probe-offscreen.log"
offscreen_rc=${PIPESTATUS[0]}
echo "offscreen exit=$offscreen_rc"

echo "== xcb pass under Xvfb :78 (QT_QPA_PLATFORM=xcb, software rendering) =="
Xvfb :78 -screen 0 1500x1000x24 >/tmp/xvfb-batch2.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 2
DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 900 "$probe" --screenshots "$SHOTS" 2>&1 | tee "$OUT/11-probe-xcb.log"
xcb_rc=${PIPESTATUS[0]}
kill "$xvfb_pid" 2>/dev/null || true
trap - EXIT
echo "xcb exit=$xcb_rc"

shots_missing=0
for shot in "$SHOTS/10-references-list.png" "$SHOTS/20-rename-applied.png"; do
  if [ -s "$shot" ]; then
    echo "shot: $(stat -c '%n %s bytes' "$shot")"
  else
    echo "FATAL: missing screenshot $shot" >&2
    shots_missing=1
  fi
done

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc shots_missing=$shots_missing =="
if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ] || [ "$shots_missing" -ne 0 ]; then
  echo "BATCH2-EDITOR-PROBE-FAILED"
  exit 1
fi
echo "BATCH2-EDITOR-PROBE-OK"
