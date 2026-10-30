#!/usr/bin/env bash
# Issue #1/#2 reproduction probe: does the UI main loop saturate after a file is opened?
# Run inside the pinned image vedit-build:local with the repo mounted at /vedit-build.
# Signal: CPU ticks per wall-clock second (/proc/<pid>/stat fields 14+15, 100 ticks = 1 CPU-second).
set -u

# A bare container has an empty /etc/machine-id; GApplication aborts without one.
if [ ! -s /etc/machine-id ]; then
  printf '%s\n' '0123456789abcdef0123456789abcdef' > /etc/machine-id
fi

export HOME=/vedit-build/target/home
export VEDIT_SLANG=/vedit-build/build/slang/out/slang
export VEDIT_GSV_DATADIR=/opt/gsv4/share/gtksourceview-4
V=/vedit-build/target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin
export VEDIT_VERIBLE_LS=$V/verible-verilog-ls
export VEDIT_VERIBLE_FORMAT=$V/verible-verilog-format
export VEDIT_VERIBLE_LINT=$V/verible-verilog-lint
mkdir -p "$HOME"

Xvfb :97 -screen 0 1400x900x24 >/tmp/xvfb.log 2>&1 &
sleep 1
export DISPLAY=:97

APP_BIN=${APP_BIN:-/vedit-build/build/artifacts/vd-app}
TARGET_FILE=${TARGET_FILE:-/vedit-build/crates/vd-app/tests/fixtures/definition/a.sv}
echo "binary=$APP_BIN"
echo "file=$TARGET_FILE"
# Host contention can only depress a process's measured share, so a busy machine
# could fake a low reading; record the context beside the numbers.
echo "host_nproc=$(nproc) host_loadavg=$(cut -d' ' -f1-3 /proc/loadavg)"

if [ -n "${TARGET_FILE}" ] && [ "${TARGET_FILE}" != "-" ]; then
  "$APP_BIN" "$TARGET_FILE" >/tmp/app.log 2>&1 &
else
  "$APP_BIN" >/tmp/app.log 2>&1 &
fi
APP=$!

sleep 3
if ! kill -0 "$APP" 2>/dev/null; then
  echo "RESULT: app exited within 3s"
  echo '--- app log ---'
  tail -20 /tmp/app.log
  exit 1
fi
echo "alive=yes pid=$APP"
echo '--- toplevel windows ---'
xwininfo -root -tree 2>/dev/null | head -12

WINDOWS=${CPU_WINDOWS:-"2 3 5 10 20 20"}
prev=-1
# The 3 s startup wait below is part of the application's lifetime.
age=3
first_pct=""
last_pct=""
last_window=""
died=0
for t in $WINDOWS; do
  sleep "$t"
  age=$((age + t))
  if ! awk '{print $14, $15}' "/proc/$APP/stat" >/tmp/stat.txt 2>/dev/null; then
    echo "RESULT: APP_DIED_DURING_SAMPLING (after ${age}s of sampling)"
    died=1
    break
  fi
  read -r u s < /tmp/stat.txt
  now=$((u + s))
  if [ "$prev" -lt 0 ]; then
    echo "baseline ticks=$now (app age ~3s + ${t}s)"
  else
    # 1 CPU-second = 100 ticks (USER_HZ), so ticks-per-second == percent of one core.
    pct=$(awk -v d=$((now - prev)) -v t="$t" 'BEGIN { printf "%.1f", d / t }')
    echo "sampling window ${t}s: ticks=$now delta=$((now - prev)) -> ${pct}% of one core"
    if [ -z "$first_pct" ]; then
      first_pct=$pct
    fi
    last_pct=$pct
    last_window=$t
  fi
  prev=$now
done

echo '--- app log (last 8) ---'
tail -8 /tmp/app.log
kill "$APP" 2>/dev/null
pkill -f 'Xvfb :97' 2>/dev/null
sleep 0.2

if [ "$died" -eq 1 ]; then
  exit 5
fi
if [ -z "$last_pct" ] || [ -z "$first_pct" ]; then
  echo "RESULT: NO_SAMPLES"
  exit 6
fi
growth=$(awk -v a="$first_pct" -v b="$last_pct" 'BEGIN { printf "%.1f", b - a }')
echo "SUMMARY: app_age=${age}s first_window_pct=${first_pct} last_window_${last_window}s_pct=${last_pct} growth_pp=${growth}"
# SAMPLED means the sampling protocol completed; it is NOT an AC-7 verdict.
echo "RESULT: SAMPLED"
exit 0
