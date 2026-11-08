#!/usr/bin/env python3
"""Geometry helper for the R1 review GUI probe.

The pinned image has no xdotool/xwininfo, so this script reads the X tree
directly (ctypes libX11): the app window's real origin, the mapped override-
redirect popups (Qt menus are override-redirect windows), and the menubar label
runs found by scanning the screenshot. That removes every hand-guessed
coordinate from the review evidence.

Usage:
  geometry.py app                 -> print app window id + root geometry
  geometry.py windows             -> print every mapped root child
  geometry.py menubar             -> print dark-text runs in the app menubar row
  geometry.py shot PATH           -> root screenshot
"""
import ctypes
import sys
import os

from PIL import Image, ImageGrab

X11 = ctypes.CDLL("libX11.so.6")
X11.XOpenDisplay.restype = ctypes.c_void_p
X11.XOpenDisplay.argtypes = [ctypes.c_char_p]
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
X11.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
X11.XFree.argtypes = [ctypes.c_void_p]


class Attrs(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int),
                ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("border_width", ctypes.c_int), ("depth", ctypes.c_int),
                ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong),
                ("class_", ctypes.c_int), ("bit_gravity", ctypes.c_int),
                ("win_gravity", ctypes.c_int), ("backing_store", ctypes.c_int),
                ("backing_planes", ctypes.c_ulong), ("backing_pixel", ctypes.c_ulong),
                ("save_under", ctypes.c_int), ("colormap", ctypes.c_ulong),
                ("map_installed", ctypes.c_int), ("map_state", ctypes.c_int),
                ("all_event_masks", ctypes.c_long), ("your_event_mask", ctypes.c_long),
                ("do_not_propagate_mask", ctypes.c_long),
                ("override_redirect", ctypes.c_int), ("screen", ctypes.c_void_p)]


def display():
    d = X11.XOpenDisplay(os.environ.get("REVIEW_DISPLAY", ":99").encode())
    if not d:
        raise SystemExit("cannot open " + os.environ.get("REVIEW_DISPLAY", ":99"))
    return d


def children(d):
    root = X11.XDefaultRootWindow(d)
    r = ctypes.c_ulong()
    p = ctypes.c_ulong()
    ch = ctypes.POINTER(ctypes.c_ulong)()
    n = ctypes.c_uint()
    if not X11.XQueryTree(d, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(ch), ctypes.byref(n)):
        return []
    out = [ch[i] for i in range(n.value)]
    X11.XFree(ch)
    return out


def geom(d, w):
    gr = ctypes.c_ulong()
    x = ctypes.c_int()
    y = ctypes.c_int()
    ww = ctypes.c_uint()
    hh = ctypes.c_uint()
    bw = ctypes.c_uint()
    dp = ctypes.c_uint()
    ok = X11.XGetGeometry(d, w, ctypes.byref(gr), ctypes.byref(x), ctypes.byref(y),
                          ctypes.byref(ww), ctypes.byref(hh), ctypes.byref(bw), ctypes.byref(dp))
    if not ok:
        return None
    return x.value, y.value, ww.value, hh.value


def attrs(d, w):
    a = Attrs()
    ok = X11.XGetWindowAttributes(d, w, ctypes.byref(a))
    return a if ok else None


def app_window(d):
    for w in children(d):
        g = geom(d, w)
        if g and g[2] >= 1000 and g[3] >= 600:
            return w, g
    raise SystemExit("app window not found")


def menubar(d):
    w, (wx, wy, _, _) = app_window(d)
    shot = ImageGrab.grab(xdisplay=os.environ.get("REVIEW_DISPLAY", ":99")).convert("RGB")
    px = shot.load()
    y0, y1 = wy + 2, wy + 20
    xs = []
    for x in range(wx, wx + 400):
        dark = 0
        for y in range(y0, y1):
            r, g, b = px[x, y]
            if r + g + b < 300:
                dark += 1
        xs.append((x, dark))
    runs = []
    start = None
    for x, dark in xs:
        if dark >= 2 and start is None:
            start = x
        elif dark < 2 and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, wx + 399))
    # merge runs closer than 6 px (letters of one label)
    merged = []
    for run in runs:
        if merged and run[0] - merged[-1][1] <= 6:
            merged[-1] = (merged[-1][0], run[1])
        else:
            merged.append(list(run))
    print("menubar y=%d..%d runs:" % (y0, y1))
    for i, (a, b) in enumerate(merged):
        print("  label[%d] x=%d..%d center=%d" % (i, a, b, (a + b) // 2))


def main():
    d = display()
    op = sys.argv[1]
    if op == "app":
        w, g = app_window(d)
        print("app win=%s geom=(x=%d,y=%d,w=%d,h=%d)" % (hex(w), g[0], g[1], g[2], g[3]))
    elif op == "windows":
        for w in children(d):
            a = attrs(d, w)
            g = geom(d, w)
            if a is None or g is None:
                continue
            if a.map_state != 2:  # IsViewable
                continue
            print("win=%s geom=(%d,%d %dx%d) override=%d" %
                  (hex(w), g[0], g[1], g[2], g[3], a.override_redirect))
    elif op == "menubar":
        menubar(d)
    elif op == "shot":
        im = ImageGrab.grab(xdisplay=os.environ.get("REVIEW_DISPLAY", ":99"))
        im.save(sys.argv[2])
        print("shot %s %s" % (sys.argv[2], im.size))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
