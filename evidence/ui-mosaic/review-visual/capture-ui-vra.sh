#!/usr/bin/env bash
# Reviewer-owned interactive capture: menu, terminal panel, preferences dialog.
# Container vra-shot2, display :93, X socket .mpd/x11-vra (re-linked in this call).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/evidence/ui-mosaic/review-visual/shots"
SOCK="$ROOT/.mpd/x11-vra"
IMG="${VEDIT_QT6_IMAGE:-vedit-qt6-build:local}"
APP="./target/qt6-build/src/app/vedit-app"
DISP=":93"

mkdir -p "$OUT" "$SOCK"; chmod 1777 "$SOCK"
docker rm -f vra-shot2 >/dev/null 2>&1 || true
docker run -d --name vra-shot2 \
  -v "$SOCK:/tmp/.X11-unix:rw" \
  -v "$ROOT:/vedit-build" -w /vedit-build "$IMG" \
  bash -lc "Xvfb ${DISP} -screen 0 1680x1050x24 -ac -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2; sleep 100000" >/dev/null
sleep 4
if [ -e /tmp/.X11-unix ] && [ ! -L /tmp/.X11-unix ]; then echo "ERROR: /tmp/.X11-unix real dir"; exit 1; fi
rm -f /tmp/.X11-unix
ln -sfn "$SOCK" /tmp/.X11-unix
[ -S /tmp/.X11-unix/X93 ] || { echo "ERROR: X93 missing"; exit 1; }

run_theme() {
  local theme="$1"
  docker exec vra-shot2 bash -lc "mkdir -p /root/.config/VEdit/vedit-app && printf 'uiTheme = \"${theme}\"\n' > /root/.config/VEdit/vedit-app/settings.toml"
  docker exec vra-shot2 bash -lc "pkill -f 'src/app/vedit-app' || true" >/dev/null 2>&1 || true
  sleep 1
  docker exec -d vra-shot2 bash -lc "cd /vedit-build && DISPLAY=${DISP} QT_QPA_PLATFORM=xcb ${APP} --project /vedit-build/tests/fixtures/verilog-project >/tmp/app-${theme}.log 2>&1"
  sleep 12
  python3 - "$OUT" "$theme" <<'PY'
import ctypes, sys, time
from PIL import ImageGrab

out, theme = sys.argv[1], sys.argv[2]
x11 = ctypes.CDLL("libX11.so.6")
xtst = ctypes.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
dpy = x11.XOpenDisplay(b":93")
assert dpy, "no display"
PointerRoot, RevertToPointerRoot = 1, 1
x11.XSetInputFocus(ctypes.c_void_p(dpy), ctypes.c_ulong(PointerRoot), ctypes.c_int(RevertToPointerRoot), ctypes.c_ulong(0))
xtst.XTestFakeMotionEvent(ctypes.c_void_p(dpy), -1, 640, 400, ctypes.c_ulong(0))
x11.XFlush(ctypes.c_void_p(dpy))

def kc(name):
    return x11.XKeysymToKeycode(ctypes.c_void_p(dpy), x11.XStringToKeysym(name.encode()))

def key(name, mods=()):
    for m in mods:
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc(m), True, ctypes.c_ulong(0))
    k = kc(name)
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), k, True, ctypes.c_ulong(0))
    xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), k, False, ctypes.c_ulong(0))
    for m in reversed(mods):
        xtst.XTestFakeKeyEvent(ctypes.c_void_p(dpy), kc(m), False, ctypes.c_ulong(0))
    x11.XFlush(ctypes.c_void_p(dpy))

def click(x, y):
    xtst.XTestFakeMotionEvent(ctypes.c_void_p(dpy), -1, x, y, ctypes.c_ulong(0))
    x11.XFlush(ctypes.c_void_p(dpy)); time.sleep(0.2)
    xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), 1, True, ctypes.c_ulong(0))
    xtst.XTestFakeButtonEvent(ctypes.c_void_p(dpy), 1, False, ctypes.c_ulong(0))
    x11.XFlush(ctypes.c_void_p(dpy))

def shot(tag):
    time.sleep(1.0)
    im = ImageGrab.grab(xdisplay=":93")
    im.save(f"{out}/{theme}-{tag}.png")
    print(f"shot {theme}-{tag} {im.size}")

def esc():
    key("Escape"); time.sleep(0.6)

shot("ui-base")
# File menu via Alt+F
key("f", ["Alt_L"]); shot("ui-menu-file"); esc()
# Terminal panel via Ctrl+Alt+J
key("j", ["Control_L", "Alt_L"]); time.sleep(2.0); shot("ui-terminal")
# Preferences dialog via Ctrl+,
key("comma", ["Control_L"]); time.sleep(2.5); shot("ui-dialog")
esc(); time.sleep(0.8)
# Help menu
key("h", ["Alt_L"]); shot("ui-menu-help")
esc()
PY
  docker exec vra-shot2 bash -lc "cp /tmp/app-${theme}.log /vedit-build/evidence/ui-mosaic/review-visual/shots/app2-${theme}.log" >/dev/null 2>&1 || true
}

run_theme yunwu
run_theme shenye
docker rm -f vra-shot2 >/dev/null 2>&1 || true
echo "OK: interactive captures in $OUT"
