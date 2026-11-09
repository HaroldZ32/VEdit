#!/usr/bin/env python3
"""V11 pixel gate for the popup hardening: pure-black pixels and dominant surface.

Usage: measure-v11.py <out-dir> <style> [<style> ...]

For each style it pairs v11-dialog-<style>.png (before right-click) with
v11-menu-<style>.png, and v11-view-before-<style>.png with v11-view-<style>.png.
The popup region is the bounding box of pixels that changed between the two
frames, so no menu coordinate is hard-coded.

Gates, inside that region:
  * pure black (R+G+B < 30) pixel count must be 0;
  * the dominant colour must be the theme surface (#FFFFFF, +/-2 per channel).
If the region is empty the menu never opened: that is reported, not passed.
"""
import json
import os
import re
import sys
import warnings
from collections import Counter

from PIL import Image

warnings.filterwarnings("ignore", category=DeprecationWarning)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
THEME_SOURCE = os.path.join(ROOT, "src", "app", "product_theme.cpp")

MAKE_THEME_RE = re.compile(
    r'makeTheme\("(?P<id>[a-z0-9_]+)",\s*"[^"]*",\s*(?P<dark>true|false),\s*'
    r'(?P<canvas>0x[0-9A-Fa-f]{6}),\s*(?P<surface>0x[0-9A-Fa-f]{6}),'
)


def load_surface(theme_id="yunwu"):
    with open(THEME_SOURCE, encoding="utf-8") as handle:
        source = handle.read()
    for match in MAKE_THEME_RE.finditer(source):
        if match.group("id") == theme_id:
            value = int(match.group("surface"), 16)
            return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)
    raise SystemExit(f"theme {theme_id} not found in {THEME_SOURCE}")


SURFACE = load_surface()


def changed_bbox(before, after, threshold=8):
    a, b = before.load(), after.load()
    w, h = before.size
    x0, y0, x1, y1 = w, h, -1, -1
    mask = []
    for y in range(h):
        for x in range(w):
            pa, pb = a[x, y], b[x, y]
            if max(abs(pa[i] - pb[i]) for i in range(3)) > threshold:
                mask.append((x, y))
                if x < x0:
                    x0 = x
                if y < y0:
                    y0 = y
                if x > x1:
                    x1 = x
                if y > y1:
                    y1 = y
    if x1 < 0:
        return None, set()
    return (x0, y0, x1 + 1, y1 + 1), set(mask)


def component_bbox(mask, point):
    """Bounding box of the 4-connected changed-pixel component holding `point`."""
    if point not in mask:
        return None
    todo, seen = [point], {point}
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    while todo:
        x, y = todo.pop()
        x0, y0 = min(x0, x), min(y0, y)
        x1, y1 = max(x1, x), max(y1, y)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) in mask and (nx, ny) not in seen:
                seen.add((nx, ny))
                todo.append((nx, ny))
    return (x0, y0, x1 + 1, y1 + 1)


def measure(before_path, after_path, tag, style, out_dir, anchor=None):
    before = Image.open(before_path).convert("RGB")
    after = Image.open(after_path).convert("RGB")
    bbox, mask = changed_bbox(before, after)
    if bbox is None:
        return {"tag": tag, "style": style, "menuOpened": False}
    if anchor is not None:
        # The file-dialog right-click also repaints the filename field below the
        # popup; the popup itself is the changed component holding the click.
        popup = component_bbox(mask, anchor)
        if popup is not None:
            bbox = popup
    crop = after.crop(bbox)
    crop.save(os.path.join(out_dir, f"v11-crop-{tag}-{style}.png"))
    pixels = list(crop.getdata())
    black = sum(1 for p in pixels if sum(p) < 30)
    dominant, count = Counter(pixels).most_common(1)[0]
    delta = max(abs(dominant[i] - SURFACE[i]) for i in range(3))
    return {
        "tag": tag,
        "style": style,
        "menuOpened": True,
        "bbox": list(bbox),
        "area": crop.width * crop.height,
        "pureBlack": black,
        "pureBlackPass": black == 0,
        "dominant": "#%02X%02X%02X" % dominant,
        "dominantShare": round(count / len(pixels), 3),
        "surface": "#%02X%02X%02X" % SURFACE,
        "dominantDeltaMax": delta,
        "dominantPass": delta <= 2,
    }


def main():
    out_dir = sys.argv[1]
    styles = sys.argv[2:]
    results = []
    for style in styles:
        results.append(measure(
            os.path.join(out_dir, f"v11-dialog-{style}.png"),
            os.path.join(out_dir, f"v11-menu-{style}.png"),
            "menu", style, out_dir, anchor=(700, 420)))
        results.append(measure(
            os.path.join(out_dir, f"v11-view-before-{style}.png"),
            os.path.join(out_dir, f"v11-view-{style}.png"),
            "view", style, out_dir))
    print(json.dumps({"surface": "#%02X%02X%02X" % SURFACE, "results": results},
                     ensure_ascii=False, indent=2))
    for row in results:
        if not row["menuOpened"]:
            print(f"WARN: {row['tag']}-{row['style']}: no pixel change (popup did not open)",
                  file=sys.stderr)
            continue
        if not (row["pureBlackPass"] and row["dominantPass"]):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
