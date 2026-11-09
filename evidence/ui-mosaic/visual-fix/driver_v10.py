#!/usr/bin/env python3
"""V10 interactive driver: XTest click a Files-tree row and open its context menu.

Usage: driver_v10.py <out-dir> <theme> <display>

Produces <theme>-tree-selected.png, <theme>-ctxmenu.png, <theme>-ctxmenu-hover.png
on the real X server, so the selected-row background is measured from pixels.
"""
import ctypes
import sys
import time

from PIL import Image, ImageGrab

OUT, THEME, DISP = sys.argv[1], sys.argv[2], sys.argv[3]

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
    im.save(f"{OUT}/{THEME}-{tag}.png")
    print(f"shot {THEME}-{tag} {im.size}")


def has_selection_band(im, color, y0=620, y1=1000, min_run=100):
    """True when some scanline below y0 holds a long run of `color`."""
    p = im.load()
    w, h = im.size
    for y in range(y0, min(y1, h)):
        best = run = 0
        for x in range(0, w):
            if p[x, y] == color:
                run += 1
                best = max(best, run)
            else:
                run = 0
        if best >= min_run:
            return True, y
    return False, -1


SELECTION = {"yunwu": (0xE1, 0xE1, 0xE1), "yaoshi": (0x51, 0x42, 0x22)}[THEME]

# focus the window (no WM under Xvfb)
click(668, 417)

# single left click on the "expected" row of the Files tree
click(125, 712)
move(900, 520)
shot("tree-selected")

# right click on the same row -> context menu
click(125, 712, button=3)
time.sleep(1.0)
shot("ctxmenu")

# find the first highlighted menu item with the real pointer
found = False
for dy in range(30, 141, 10):
    move(270, 712 + dy)
    time.sleep(0.35)
    ok, row = has_selection_band(grab(), SELECTION)
    if ok:
        shot("ctxmenu-hover")
        print(f"hover row y={row} offset={dy}")
        found = True
        break
if not found:
    print("WARN: no highlighted menu row found with the pointer")
    shot("ctxmenu-hover")

key("Escape")
print("done")
