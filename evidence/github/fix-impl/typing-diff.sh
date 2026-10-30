#!/usr/bin/env bash
# Complement to evidence/github/repro/probe-typing.sh: prints the EXACT bytes
# the injected keystroke added, so "the file changed" is attributable.
# Same image, same injector (evidence/github/repro/xtype.c), fixed binary.
set -u

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

APP_BIN=${APP_BIN:-/vedit-build/target/release/vd-app}
WORK=/tmp/vedit-typing-diff
rm -rf "$WORK"
mkdir -p "$WORK"
cp /vedit-build/crates/vd-app/tests/fixtures/definition/a.sv "$WORK/target.sv"
cp "$WORK/target.sv" "$WORK/original.sv"

gcc -std=gnu99 /vedit-build/evidence/github/repro/xtype.c -o "$WORK/xtype" \
  -lX11 -l:libXtst.so.6 || { echo "RESULT: INJECTOR_BUILD_FAILED"; exit 4; }

Xvfb :95 -screen 0 1400x900x24 >/tmp/xvfb95.log 2>&1 &
sleep 1
export DISPLAY=:95

echo "binary=$APP_BIN"
"$APP_BIN" "$WORK/target.sv" >/tmp/app-typing-diff.log 2>&1 &
APP=$!
sleep 4
if ! kill -0 "$APP" 2>/dev/null; then
  echo "RESULT: APP_DIED"
  tail -5 /tmp/app-typing-diff.log
  pkill -f 'Xvfb :95' 2>/dev/null
  exit 5
fi
"$WORK/xtype" VEdit
echo "xtype_exit=$?"
sleep 1
echo '--- diff original -> saved (empty means the key never reached the buffer) ---'
diff -u "$WORK/original.sv" "$WORK/target.sv" || true
echo '--- saved bytes ---'
od -c "$WORK/target.sv"
kill "$APP" 2>/dev/null
pkill -f 'Xvfb :95' 2>/dev/null
sleep 0.2
exit 0
