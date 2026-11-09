#!/usr/bin/env python3
"""Drive the VEdit window on :93 with XTest (click to activate, then keys).

Usage: input_driver.py <outdir> <theme>
Saves <theme>-ui2-*.png for each step and prints a summary.
"""
import ctypes
import sys
import time

from PIL import ImageGrab

OUT, THEME = sys.argv[1], sys.argv[2]

x11 = ctypes.CDLL("libX11.so.6")
xtst = ctypes.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XDefaultRootWindow.restype = ctypes.c_ulong
d = x11.XOpenDisplay(b":93")
assert d, "cannot open :93"
D = ctypes.c_void_p(d)


def kc(name):
    return x11.XKeysymToKeycode(D, x11.XStringToKeysym(name.encode()))


def flush():
    x11.XFlush(D)


def move(x, y):
    xtst.XTestFakeMotionEvent(D, -1, int(x), int(y), ctypes.c_ulong(0))
    flush()
    time.sleep(0.15)


def click(x, y):
    move(x, y)
    xtst.XTestFakeButtonEvent(D, 1, True, ctypes.c_ulong(0))
    xtst.XTestFakeButtonEvent(D, 1, False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.5)


def key(name, mods=()):
    for m in mods:
        xtst.XTestFakeKeyEvent(D, kc(m), True, ctypes.c_ulong(0))
    k = kc(name)
    xtst.XTestFakeKeyEvent(D, k, True, ctypes.c_ulong(0))
    xtst.XTestFakeKeyEvent(D, k, False, ctypes.c_ulong(0))
    for m in reversed(mods):
        xtst.XTestFakeKeyEvent(D, kc(m), False, ctypes.c_ulong(0))
    flush()
    time.sleep(0.35)


def shot(tag):
    time.sleep(0.8)
    im = ImageGrab.grab(xdisplay=":93")
    im.save(f"{OUT}/{THEME}-ui2-{tag}.png")
    print(f"shot {THEME}-ui2-{tag}")


def changed(a, b):
    if not a or not b:
        return 0
    return sum(1 for x in range(0, 1680, 11) for y in range(0, 1050, 11) if a.getpixel((x, y)) != b.getpixel((x, y)))


# 1) click in the editor area to give the window keyboard focus
click(640, 400)
base = ImageGrab.grab(xdisplay=":93")

# 2) Alt+F
key("f", ["Alt_L"])
menu = ImageGrab.grab(xdisplay=":93")
print("alt+f changed:", changed(base, menu))
menu.save(f"{OUT}/{THEME}-ui2-menu-file.png")
key("Escape")

# 3) click the File menu directly (menubar item near x=33,y=12)
click(33, 12)
menu2 = ImageGrab.grab(xdisplay=":93")
print("click-file changed:", changed(base, menu2))
menu2.save(f"{OUT}/{THEME}-ui2-menu-click.png")
key("Escape")

# 4) terminal: Ctrl+Alt+J
key("j", ["Control_L", "Alt_L"])
time.sleep(2.0)
term = ImageGrab.grab(xdisplay=":93")
print("terminal changed:", changed(base, term))
term.save(f"{OUT}/{THEME}-ui2-terminal.png")

# 5) preferences: Ctrl+,
key("comma", ["Control_L"])
time.sleep(2.5)
dlg = ImageGrab.grab(xdisplay=":93")
print("dialog changed:", changed(base, dlg))
dlg.save(f"{OUT}/{THEME}-ui2-dialog.png")
key("Escape")
time.sleep(0.8)
print("done")
