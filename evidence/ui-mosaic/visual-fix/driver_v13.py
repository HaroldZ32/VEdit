#!/usr/bin/env python3
"""V13 screenshot driver: wait for the app, grab the full screen, crop Project dock.

Usage: driver_v13.py <out-dir> <theme> <display>

Writes v13-full-<theme>.png and v13-project-<theme>.png. The crop box is the
left dock column above the Files dock (measured on the 1680x1050 Xvfb screen);
it is saved as JSON so the measurement script reads the same rectangle.
"""
import json
import sys
import time

from PIL import ImageGrab

OUT, THEME, DISP = sys.argv[1], sys.argv[2], sys.argv[3]

# Left dock column: x 0..620 covers the Project card (16..585) with margin;
# y 60..640 covers its title bar through the Diagnostics block.
CROP = (0, 60, 620, 640)


def grab():
    return ImageGrab.grab(xdisplay=DISP).convert("RGB")


def window_up(timeout=45):
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
full = grab()
full.save(f"{OUT}/v13-full-{THEME}.png")
full.crop(CROP).save(f"{OUT}/v13-project-{THEME}.png")
print(f"shot v13-project-{THEME} full={full.size} crop={CROP}")
