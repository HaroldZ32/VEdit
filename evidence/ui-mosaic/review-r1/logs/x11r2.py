#!/usr/bin/env python3
"""Host-side X11 driver for the R1 review: REAL pointer/keys via XTest.

Why this exists: the pinned build image has Xvfb but no xdotool / xte /
python-xlib, and the review must prove the three theme entry points through
their real UI, not by re-writing state files. The host has libXtst + libX11 +
Pillow and can reach the container's Xvfb through the X socket shared into the
workspace, so ctypes is enough for real clicks and key presses.

Usage:
  x11.py click X Y
  x11.py move X Y
  x11.py key NAME [MOD ...]     (e.g. key comma Control_L)
  x11.py shot PATH
  x11.py sleep MS
  x11.py chain "click 1 2; key t; ..."   (one display connection, sequential)
"""
import ctypes
import sys
import os
import time

X11 = ctypes.CDLL("libX11.so.6")
XTST = ctypes.CDLL("libXtst.so.6")

X11.XOpenDisplay.restype = ctypes.c_void_p
X11.XOpenDisplay.argtypes = [ctypes.c_char_p]
X11.XCloseDisplay.argtypes = [ctypes.c_void_p]
X11.XFlush.argtypes = [ctypes.c_void_p]
X11.XDefaultRootWindow.restype = ctypes.c_ulong
X11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
X11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
X11.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                             ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                             ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
                             ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
X11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
X11.XFree.argtypes = [ctypes.c_void_p]
X11.XStringToKeysym.restype = ctypes.c_ulong
X11.XStringToKeysym.argtypes = [ctypes.c_char_p]
X11.XKeysymToKeycode.restype = ctypes.c_uint
X11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
XTST.XTestFakeMotionEvent.argtypes = [
    ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
XTST.XTestFakeButtonEvent.argtypes = [
    ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
XTST.XTestFakeKeyEvent.argtypes = [
    ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]

_display = None


def display():
    global _display
    if _display is None:
        _display = X11.XOpenDisplay(os.environ.get("REVIEW_DISPLAY", ":99").encode())
        if not _display:
            raise SystemExit("cannot open display " + os.environ.get("REVIEW_DISPLAY", ":99"))
    return _display


def settle(ms=250):
    X11.XFlush(display())
    time.sleep(ms / 1000.0)


def move(x, y):
    XTST.XTestFakeMotionEvent(display(), 0, int(x), int(y), 0)
    settle(200)


def click(x, y):
    move(x, y)
    XTST.XTestFakeButtonEvent(display(), 1, 1, 0)
    XTST.XTestFakeButtonEvent(display(), 1, 0, 0)
    settle(450)


def key(name, mods=()):
    dpy = display()
    for mod in mods:
        code = X11.XKeysymToKeycode(dpy, X11.XStringToKeysym(mod.encode()))
        XTST.XTestFakeKeyEvent(dpy, code, 1, 0)
        settle(60)
    code = X11.XKeysymToKeycode(dpy, X11.XStringToKeysym(name.encode()))
    XTST.XTestFakeKeyEvent(dpy, code, 1, 0)
    settle(60)
    XTST.XTestFakeKeyEvent(dpy, code, 0, 0)
    for mod in reversed(mods):
        code = X11.XKeysymToKeycode(dpy, X11.XStringToKeysym(mod.encode()))
        XTST.XTestFakeKeyEvent(dpy, code, 0, 0)
        settle(60)
    settle(400)


def focus():
    """Give the app window X input focus.

    Without a window manager the X focus stays PointerRoot, and a freshly
    started Qt window has not seen a click yet, so menu mnemonics / Ctrl+Q are
    dropped. A real user click gives focus implicitly; the probe sets it
    explicitly before key-driven steps.
    """
    dpy = display()
    root = X11.XDefaultRootWindow(dpy)
    r = ctypes.c_ulong()
    p = ctypes.c_ulong()
    ch = ctypes.POINTER(ctypes.c_ulong)()
    n = ctypes.c_uint()
    X11.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(ch), ctypes.byref(n))
    target = 0
    for i in range(n.value):
        w = ch[i]
        gr = ctypes.c_ulong()
        x = ctypes.c_int()
        y = ctypes.c_int()
        ww = ctypes.c_uint()
        hh = ctypes.c_uint()
        bw = ctypes.c_uint()
        dp = ctypes.c_uint()
        if X11.XGetGeometry(dpy, w, ctypes.byref(gr), ctypes.byref(x), ctypes.byref(y),
                            ctypes.byref(ww), ctypes.byref(hh), ctypes.byref(bw), ctypes.byref(dp)):
            if ww.value >= 1000 and hh.value >= 600:
                target = w
                break
    X11.XFree(ch)
    if target:
        X11.XSetInputFocus(dpy, target, 2, 0)  # RevertToParent, CurrentTime
        X11.XFlush(dpy)
        time.sleep(0.2)
        print("focus win=%s" % hex(target))
    else:
        raise SystemExit("no app window to focus")


def shot(path):
    from PIL import ImageGrab
    im = ImageGrab.grab(xdisplay=os.environ.get("REVIEW_DISPLAY", ":99"))
    im.save(path)
    print("shot %s %s" % (path, im.size))


def main(argv):
    if not argv:
        raise SystemExit(__doc__)
    op = argv[0]
    if op == "click":
        click(argv[1], argv[2])
    elif op == "move":
        move(argv[1], argv[2])
    elif op == "key":
        key(argv[1], argv[2:])
    elif op == "shot":
        shot(argv[1])
    elif op == "focus":
        focus()
    elif op == "sleep":
        time.sleep(int(argv[1]) / 1000.0)
    elif op == "chain":
        for part in argv[1].split(";"):
            words = part.split()
            if not words:
                continue
            if words[0] == "click":
                click(words[1], words[2])
            elif words[0] == "move":
                move(words[1], words[2])
            elif words[0] == "key":
                key(words[1], words[2:])
            elif words[0] == "shot":
                shot(words[1])
            elif words[0] == "focus":
                focus()
            elif words[0] == "sleep":
                time.sleep(int(words[1]) / 1000.0)
            else:
                raise SystemExit("unknown chain op: %r" % (words[0],))
    else:
        raise SystemExit("unknown op: %r" % (op,))


if __name__ == "__main__":
    main(sys.argv[1:])
