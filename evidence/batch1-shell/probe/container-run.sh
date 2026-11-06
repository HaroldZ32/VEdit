#!/usr/bin/env bash
# Batch-1 acceptance probe — runs INSIDE the pinned Qt6/KF6 image.
#
#   * builds the probe from the product's own ninja compile/link lines (build.sh;
#     NOTHING under src/ is recompiled for it);
#   * runs it headless (QT_QPA_PLATFORM=offscreen) — the assertions;
#   * runs the same scenario under Xvfb :78 with QT_QPA_PLATFORM=xcb and saves
#     the two screenshots (ordinary start on root A / after the folder switch).
#
# Invoked by run.sh, which supplies the container and the workspace mount.
set -uo pipefail
WORK="${VEDIT_WORKSPACE:-/vedit-build}"
cd "$WORK" || exit 2

OUT="$WORK/evidence/batch1-shell"
SHOTS="$OUT/shots"
BIN="$WORK/target/batch1-shell-probe"
mkdir -p "$OUT" "$SHOTS" "$BIN"

overall=0

echo "== probe build (product ninja compile + link lines) =="
bash evidence/batch1-shell/probe/build.sh 2>&1 | tee "$OUT/00-probe-build.log"
build_rc=${PIPESTATUS[0]}
if [ "$build_rc" -ne 0 ]; then
  echo "FATAL: probe build failed (rc=$build_rc)" >&2
  exit "$build_rc"
fi
probe="$BIN/batch1-shell-probe"

# Documented runtime toolchain path 3 (AGENT.md §4): the checkout's own slang,
# which is what elaborates the module tree. The probe prints the resolved source
# in its first line, so the log states which binary was used.
slang="${VEDIT_SLANG:-$WORK/build/slang/out/slang}"
if [ -x "$slang" ]; then
  export VEDIT_SLANG="$slang"
  echo "[probe] VEDIT_SLANG=$slang"
else
  echo "[probe] WARNING: $slang is missing; the module tree will report a missing toolchain"
fi
mkdir -p /tmp/xdg-runtime && chmod 700 /tmp/xdg-runtime
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export QT_LOGGING_RULES="kf.*=false"

echo "== offscreen pass (QT_QPA_PLATFORM=offscreen) =="
QT_QPA_PLATFORM=offscreen timeout 600 "$probe" --checks 2>&1 \
  | tee "$OUT/10-probe-offscreen.log"
offscreen_rc=${PIPESTATUS[0]}
echo "offscreen exit=$offscreen_rc"

echo "== xcb pass under Xvfb :78 (QT_QPA_PLATFORM=xcb, software rendering) =="
Xvfb :78 -screen 0 1500x1000x24 >/tmp/xvfb-batch1.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 2
DISPLAY=:78 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb \
  timeout 600 "$probe" --screenshots "$SHOTS" 2>&1 \
  | tee "$OUT/11-probe-xcb.log"
xcb_rc=${PIPESTATUS[0]}
kill "$xvfb_pid" 2>/dev/null || true
trap - EXIT
echo "xcb exit=$xcb_rc"

# The screenshots are part of the acceptance; a missing PNG is a failure even if
# the assertion run passed.
shots_missing=0
for shot in "$SHOTS/10-ordinary-start-root-a.png" "$SHOTS/20-after-switch-root-b.png"; do
  if [ -s "$shot" ]; then
    echo "shot: $(stat -c '%n %s bytes' "$shot")"
  else
    echo "FATAL: missing screenshot $shot" >&2
    shots_missing=1
  fi
done

# The probe mirrors src/app/main.cpp, so also drive the REAL vedit-app entry
# point: a seeded session, NO --project, ordinary start. The lanes log the root
# they received, which is the product-level half of AC-1.1.
echo "== real vedit-app ordinary start (offscreen, seeded session, no --project) =="
REAL_PROJECT=$(mktemp -d)/real-project
mkdir -p "$REAL_PROJECT/rtl"
printf 'module real_top;\nendmodule\n' >"$REAL_PROJECT/rtl/real.sv"
REAL_STATE=$(mktemp -d)
printf '{"version":2,"project":"%s","panels":["file-tree","search","vcs","module-tree"]}' \
  "$REAL_PROJECT" >"$REAL_STATE/session.json"
VEDIT_DEBUG=1 QT_QPA_PLATFORM=offscreen timeout 120 \
  "$WORK/target/qt6-build/src/app/vedit-app" --state-dir "$REAL_STATE" --hold 3000 \
  >"$OUT/13-real-binary-ordinary-start.log" 2>&1
real_rc=$?
echo "real-binary exit=$real_rc"
grep -E "filetree: installFileTree|search: root=|vcs: root=|module-tree: installed" \
  "$OUT/13-real-binary-ordinary-start.log" || true
real_missing=0
for expected in "filetree: installFileTree projectRoot='$REAL_PROJECT'" \
                "search: root='$REAL_PROJECT'" \
                "vcs: root='$REAL_PROJECT'" \
                "module-tree: installed (root='$REAL_PROJECT'"; do
  if ! grep -Fq "$expected" "$OUT/13-real-binary-ordinary-start.log"; then
    echo "FATAL: the real binary did not hand the restored root to: $expected" >&2
    real_missing=1
  fi
done
echo "real-binary root-lines-missing=$real_missing"

echo "== result: offscreen rc=$offscreen_rc xcb rc=$xcb_rc shots_missing=$shots_missing real_rc=$real_rc real_missing=$real_missing =="
if [ "$offscreen_rc" -ne 0 ] || [ "$xcb_rc" -ne 0 ] || [ "$shots_missing" -ne 0 ] \
   || [ "$real_rc" -ne 0 ] || [ "$real_missing" -ne 0 ]; then
  overall=1
fi
if [ "$overall" -ne 0 ]; then
  echo "BATCH1-SHELL-PROBE-FAILED"
  exit 1
fi
echo "BATCH1-SHELL-PROBE-OK"
