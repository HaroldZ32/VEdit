#!/usr/bin/env python3
"""Reviewer-V2 interactive driver for the VEdit window on :97 (XTest).

Saves <theme>-{menu,terminal,dialog,hover-toolbar,pressed-toolbar,
hover-button,pressed-button}.png so the QSS states can be measured from real
pixels, not only read from the sheet.
"""
import ctypes
import sys
import time

from PIL import ImageGrab

OUT, THEME, DISP = sys.argv[1], sys.argv[2], sys.argv[3]

x11 = ctypes.CDLL("libX11.so.6")
xtst = ctypes.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
d = x11.XOpenDisplay(DISP.encode())
assert d, f"cannot open {DISP}"
D = ctypes.c_void_p(d)


def kc(name):
    return x11.XKeysymToKeycode(D, x11.XStringToKeysym(name.encode()))


def flush():
    x11.XFlush(D)


def move(x, y):
    xtst.XTestFakeMotionEvent(D, -1, int(x), int(y), ctypes.c_ulong(0))
    flush()
    time.sleep(0.25)


def press():
    xtst.XTestFakeButtonEvent(D, 1, True, ctypes.c_ulong(0))
    flush()
    time.sleep(0.35)


def release():
    xtst.XTestFakeButtonEvent(D, 1, False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.35)


def click(x, y):
    move(x, y)
    press()
    release()


def key(name, mods=()):
    for m in mods:
        xtst.XTestFakeKeyEvent(D, kc(m), True, ctypes.c_ulong(0))
    k = kc(name)
    xtst.XTestFakeKeyEvent(D, k, True, ctypes.c_ulong(0))
    xtst.XTestFakeKeyEvent(D, k, False, ctypes.c_ulong(0))
    for m in reversed(mods):
        xtst.XTestFakeKeyEvent(D, kc(m), False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.4)


def shot(tag):
    time.sleep(0.7)
    im = ImageGrab.grab(xdisplay=DISP)
    im.save(f"{OUT}/{THEME}-{tag}.png")
    print(f"shot {THEME}-{tag}")


# give the window keyboard focus (no WM in Xvfb)
click(640, 400)

# File menu popup
click(33, 12)
shot("menu")
key("Escape")

# toolbar QToolButton "New" (x 33..112, y ~36..59)
move(72, 47)
shot("hover-toolbar")
press()
shot("pressed-toolbar")
release()

# Module Tree "Refresh" QPushButton (right column, title at y=74; button row ~y=100..135)
move(990, 117)
shot("hover-button")
press()
shot("pressed-button")
release()

# terminal panel
key("j", ["Control_L", "Alt_L"])
time.sleep(2.0)
shot("terminal")

# preferences dialog
key("comma", ["Control_L"])
time.sleep(2.5)
shot("dialog")
key("Escape")
time.sleep(0.8)
print("done")
