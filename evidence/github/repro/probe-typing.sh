#!/usr/bin/env bash
# Issue #1 end-to-end probe: do keystrokes reach the editor buffer?
#
# Types "a" into the running VEdit window, then Ctrl+S, and compares the saved
# file with the original. RESULT: TYPING_WORKS means the key reached the buffer;
# TYPING_BROKEN means it did not (the symptom reported in issue #1).
#
# Run inside the pinned image vedit-build:local with the repo at /vedit-build.
# APP_BIN may point at a copied binary (the pre-fix build is not stable across a
# rebuild); it defaults to the repository's release artefact.
set -u

if [ ! -s /etc/machine-id ]; then
  printf '%s\n' '0123456789abcdef0123456789abcdef' > /etc/machine-id
fi

export HOME=/vedit-build/target/home
export VEDIT_SLANG=/vedit-build/build/slang/out/slang
export VEDIT_GSV_DATADIR=/opt/gsv4/share/gtksourceview-4
V=${VEDIT_VERIBLE_DIR:-/vedit-build/target/verible-pin/extract/verible-v0.0-4298-g085eef05/bin}
export VEDIT_VERIBLE_LS=$V/verible-verilog-ls
export VEDIT_VERIBLE_FORMAT=$V/verible-verilog-format
export VEDIT_VERIBLE_LINT=$V/verible-verilog-lint
mkdir -p "$HOME"

APP_BIN=${APP_BIN:-/vedit-build/build/artifacts/vd-app}
WORK=/tmp/vedit-typing
rm -rf "$WORK"
mkdir -p "$WORK"
cp /vedit-build/crates/vd-app/tests/fixtures/definition/a.sv "$WORK/target.sv"
cp "$WORK/target.sv" "$WORK/original.sv"

gcc -std=gnu99 /vedit-build/evidence/github/repro/xtype.c -o "$WORK/xtype" -lX11 -l:libXtst.so.6 \
  || { echo "RESULT: BUILD_FAILED"; exit 4; }

Xvfb :98 -screen 0 1400x900x24 >/tmp/xvfb98.log 2>&1 &
sleep 1
export DISPLAY=:98

echo "binary=$APP_BIN"
"$APP_BIN" "$WORK/target.sv" >/tmp/app-typing.log 2>&1 &
APP=$!
sleep 4
if ! kill -0 "$APP" 2>/dev/null; then
  echo "RESULT: APP_DIED"
  tail -5 /tmp/app-typing.log
  pkill -f 'Xvfb :98' 2>/dev/null
  exit 5
fi

# The WM title is the application name; the document name is not part of it.
"$WORK/xtype" VEdit
INJECT=$?
echo "xtype_exit=$INJECT"
sleep 1

# A failed injection is an instrument failure, never a product verdict: without
# this guard a missing window would read as TYPING_BROKEN.
if [ "$INJECT" -ne 0 ]; then
  echo "RESULT: INJECTION_FAILED (xtype exit=$INJECT; no conclusion about the app)"
  kill "$APP" 2>/dev/null
  pkill -f 'Xvfb :98' 2>/dev/null
  exit 6
fi

if cmp -s "$WORK/target.sv" "$WORK/original.sv"; then
  echo "RESULT: TYPING_BROKEN (keystroke did not reach the buffer)"
  echo "--- saved file (first 2 lines) ---"
  head -2 "$WORK/target.sv"
else
  echo "RESULT: TYPING_WORKS (keystroke reached the buffer)"
  echo "--- saved file (first 2 lines) ---"
  head -2 "$WORK/target.sv"
fi

kill "$APP" 2>/dev/null
pkill -f 'Xvfb :98' 2>/dev/null
sleep 0.2
exit 0
