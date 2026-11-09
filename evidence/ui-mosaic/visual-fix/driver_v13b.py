#!/usr/bin/env python3
"""V13b screenshot driver: wait for the app, grab the full screen, crop Project dock.

Usage: driver_v13b.py <out-dir> <theme> <display>

Writes v13b-full-<theme>.png and v13b-project-<theme>.png. The crop box is the
same rectangle capture-v13 used (0,60,620,640) so the two measurements are
comparable row for row.
"""
import sys
import time

from PIL import ImageGrab

OUT, THEME, DISP = sys.argv[1], sys.argv[2], sys.argv[3]

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
full.save(f"{OUT}/v13b-full-{THEME}.png")
full.crop(CROP).save(f"{OUT}/v13b-project-{THEME}.png")
print(f"shot v13b-project-{THEME} full={full.size} crop={CROP}")
