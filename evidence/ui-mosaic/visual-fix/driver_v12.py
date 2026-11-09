#!/usr/bin/env python3
"""V12 interactive driver: open the file dialog with a real XTest click, right-click
inside its list, and open the app's own View menu.

Usage: driver_v12.py <out-dir> <style> <display>

Produces, for yunwu (surface #FFFFFF):
  v12-dialog-<style>.png      file dialog before the context menu
  v12-menu-<style>.png        file-dialog context menu open
  v12-closed-<style>.png      after Escape (menu + dialog closed)
  v12-view-before-<style>.png app window before the View menu
  v12-view-<style>.png        View menu open

All clicks are XTest events on a real Xvfb X server; nothing is synthesised at
the widget level. The same run appends one JSON line per QMenu Show to
<state-dir>/ui-menu-diag.log; capture-v12.sh copies it next to these shots.
"""
import sys
import time

from PIL import Image, ImageGrab

OUT, STYLE, DISP = sys.argv[1], sys.argv[2], sys.argv[3]

import ctypes

x11 = ctypes.CDLL("libX11.so.6")
xtst = ctypes.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
d = x11.XOpenDisplay(DISP.encode())
assert d, f"cannot open {DISP}"
D = ctypes.c_void_p(d)


def flush():
    x11.XFlush(D)
    time.sleep(0.05)


def move(x, y):
    xtst.XTestFakeMotionEvent(D, -1, int(x), int(y), ctypes.c_ulong(0))
    flush()
    time.sleep(0.2)


def click(x, y, button=1):
    move(x, y)
    xtst.XTestFakeButtonEvent(D, button, True, ctypes.c_ulong(0))
    flush()
    xtst.XTestFakeButtonEvent(D, button, False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.5)


def key(name):
    ks = x11.XStringToKeysym(name.encode())
    kc = x11.XKeysymToKeycode(D, ks)
    xtst.XTestFakeKeyEvent(D, kc, True, ctypes.c_ulong(0))
    xtst.XTestFakeKeyEvent(D, kc, False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.4)


def grab():
    return ImageGrab.grab(xdisplay=DISP).convert("RGB")


def shot(tag):
    time.sleep(0.6)
    im = grab()
    im.save(f"{OUT}/v12-{tag}-{STYLE}.png")
    print(f"shot v12-{tag}-{STYLE} {im.size}")


def diff_count(current, reference, threshold=8):
    a, b = current.load(), reference.load()
    return sum(1 for y in range(current.height) for x in range(current.width)
               if max(abs(a[x, y][i] - b[x, y][i]) for i in range(3)) > threshold)


def window_up(timeout=45):
    """Wait until the app paints (non-black pixels above 2% of the screen)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        im = grab()
        nonblack = sum(1 for p in im.getdata() if sum(p) > 30)
        if nonblack > im.width * im.height * 0.02:
            return True
        time.sleep(1.0)
    return False


if not window_up():
    raise SystemExit(f"app window never appeared on {DISP}")
time.sleep(2.0)

# Toolbar "Open Folder..." (measured position on the 1680x1050 Xvfb screen).
click(243, 44)
time.sleep(3.0)
shot("dialog")

# Right-click in the file list's blank area -> file-dialog context menu.
click(700, 420, button=3)
time.sleep(1.2)
shot("menu")

# Escape closes the menu; the second Escape closes the modal dialog.
key("Escape")
time.sleep(0.6)
key("Escape")
time.sleep(1.2)
shot("closed")

# The application's own menu bar: View. A neutral click first makes sure the
# main window owns the pointer focus again in both styles.
click(600, 400)
time.sleep(0.6)
shot("view-before")
for attempt in range(3):
    click(125, 10)
    time.sleep(1.0)
    shot("view")
    if diff_count(grab(), Image.open(f"{OUT}/v12-view-before-{STYLE}.png").convert("RGB")):
        break
    print(f"WARN: View menu did not open on attempt {attempt + 1}")
    click(600, 400)
    time.sleep(0.5)
key("Escape")
print("done")
