#!/usr/bin/env bash
# Control run for probe-typing.sh: the same XTest keystroke against a minimal
# GTK3 text view that DOES take focus. PASS means the probe's input path works,
# so a TYPING_BROKEN result for VEdit is a property of VEdit.
#
# Run inside the pinned image vedit-build:local with the repo at /vedit-build.
set -u

if [ ! -s /etc/machine-id ]; then
  printf '%s\n' '0123456789abcdef0123456789abcdef' > /etc/machine-id
fi

WORK=/tmp/xtype-control
rm -rf "$WORK"
mkdir -p "$WORK"

gcc -std=gnu99 /vedit-build/evidence/github/repro/xtype-control.c -o "$WORK/xtype-control" \
  $(pkg-config --cflags --libs gtk+-3.0) \
  || { echo "RESULT: CONTROL_BUILD_FAILED"; exit 4; }
gcc -std=gnu99 /vedit-build/evidence/github/repro/xtype.c -o "$WORK/xtype" -lX11 -l:libXtst.so.6 \
  || { echo "RESULT: INJECTOR_BUILD_FAILED"; exit 4; }

Xvfb :99 -screen 0 1200x800x24 >/tmp/xvfb99.log 2>&1 &
sleep 1
export DISPLAY=:99

"$WORK/xtype-control" >/tmp/control.log 2>&1 &
CTRL=$!
sleep 3
if ! kill -0 "$CTRL" 2>/dev/null; then
  echo "RESULT: CONTROL_DIED"
  tail -5 /tmp/control.log
  pkill -f 'Xvfb :99' 2>/dev/null
  exit 5
fi

"$WORK/xtype" xtype-control
echo "xtype_exit=$?"
sleep 1

if grep -q 'a' /tmp/control-out.txt 2>/dev/null; then
  echo "RESULT: CONTROL_WORKS (injected keystroke reached a focused GtkTextView)"
  echo "control buffer: $(cat /tmp/control-out.txt)"
else
  echo "RESULT: CONTROL_BROKEN (probe itself cannot type; VEdit results are inconclusive)"
  echo "control buffer: $(cat /tmp/control-out.txt 2>/dev/null | head -1)"
fi

kill "$CTRL" 2>/dev/null
pkill -f 'Xvfb :99' 2>/dev/null
sleep 0.2
exit 0
