#!/usr/bin/env python3
"""V10 pixel measurements: selected-row band vs surface, text contrast, menu hover.

Usage: measure-v10.py <theme> <selected.png> <ctxmenu.png> <ctxmenu-hover.png>

Uses CIE L* (0..100 perceptual lightness) for the "brightness delta" gate and
WCAG contrast for text. Row band is detected as a wide horizontal run of the
theme's derived selection colour, so no row coordinate is hard-coded.
"""
import json
import sys
from collections import Counter

from PIL import Image

THEME, SEL_PNG, MENU_PNG, HOVER_PNG = sys.argv[1:5]

SELECTION = {"yunwu": (0xE1, 0xE1, 0xE1), "yaoshi": (0x51, 0x42, 0x22)}[THEME]
TEXT = {"yunwu": (0x1F, 0x23, 0x28), "yaoshi": (0xE6, 0xE8, 0xEC)}[THEME]
SURFACE = {"yunwu": (0xFF, 0xFF, 0xFF), "yaoshi": (0x1D, 0x1D, 0x20)}[THEME]


def lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def lum(c):
    r, g, b = c
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def lstar(c):
    y = lum(c)
    return 116.0 * y ** (1.0 / 3.0) - 16.0 if y > 0.008856 else 903.3 * y


def contrast(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def bands(img, color, y0, y1):
    """[(top, bottom, max_run)] for scanlines holding a run of `color` >= 40."""
    p = img.load()
    w, h = img.size
    rows = []
    for y in range(y0, min(y1, h)):
        run = best = 0
        for x in range(w):
            if p[x, y] == color:
                run += 1
                best = max(best, run)
            else:
                run = 0
        if best >= 40:
            rows.append((y, best))
    groups = []
    for y, best in rows:
        if groups and y == groups[-1][1] + 1:
            groups[-1][1] = y
            groups[-1][2] = max(groups[-1][2], best)
        else:
            groups.append([y, y, best])
    return groups


# --- selected row in the Files tree -----------------------------------------
img = Image.open(SEL_PNG).convert("RGB")
groups = [g for g in bands(img, SELECTION, 600, 1000) if g[2] >= 100]
assert groups, "selected row band not found"
top, bottom, _ = groups[0]
mid = (top + bottom) // 2
p = img.load()
sel = p[400, mid]
assert sel == SELECTION, (sel, SELECTION)
surface = p[400, top - 8]
px = [p[x, y] for y in range(top + 2, bottom - 1) for x in range(300, 450)]
mean_sel = tuple(sum(c[i] for c in px) // len(px) for i in range(3))
text_px = [p[x, y] for y in range(top, bottom) for x in range(60, 200)]
text_extreme = min(text_px, key=lum) if THEME == "yunwu" else max(text_px, key=lum)
row = {
    "top": top,
    "bottom": bottom,
    "height": bottom - top + 1,
    "surface": "#%02X%02X%02X" % surface,
    "selected": "#%02X%02X%02X" % mean_sel,
    "surface_L": round(lstar(surface), 1),
    "selected_L": round(lstar(mean_sel), 1),
    "delta_L": round(abs(lstar(mean_sel) - lstar(surface)), 1),
    "delta_srgb_max": max(abs(a - b) for a, b in zip(mean_sel, surface)),
    "text_pixel": "#%02X%02X%02X" % text_extreme,
    "contrast_authored_text": round(contrast(TEXT, mean_sel), 2),
    "contrast_measured_text": round(contrast(text_extreme, mean_sel), 2),
}
assert 8 <= row["delta_L"] <= 40, row
assert row["contrast_authored_text"] >= 4.5, row
assert row["contrast_measured_text"] >= 4.5, row

# --- context menu: surface background + highlighted item is a light fill -----
menu_img = Image.open(MENU_PNG).convert("RGB")
p = menu_img.load()
box = [p[x, y] for y in range(760, 900) for x in range(140, 320)]
menu_bg = Counter(box).most_common(1)[0][0]
assert menu_bg == SURFACE, (menu_bg, SURFACE)

hover_img = Image.open(HOVER_PNG).convert("RGB")
groups = [g for g in bands(hover_img, SELECTION, 725, 1000) if 60 <= g[2] <= 400]
assert groups, "menu item hover band not found"
h_top, h_bottom, h_run = groups[0]
h_mid = (h_top + h_bottom) // 2
p = hover_img.load()
run_color = [p[x, h_mid] for x in range(130, 330) if p[x, h_mid] == SELECTION]
hover_bg = tuple(sum(c[i] for c in run_color) // len(run_color) for i in range(3))
menu_hover = {
    "top": h_top,
    "bottom": h_bottom,
    "background": "#%02X%02X%02X" % hover_bg,
    "delta_L_vs_surface": round(abs(lstar(hover_bg) - lstar(SURFACE)), 1),
    "contrast_authored_text": round(contrast(TEXT, hover_bg), 2),
}
assert 8 <= menu_hover["delta_L_vs_surface"] <= 40, menu_hover
assert menu_hover["contrast_authored_text"] >= 4.5, menu_hover

print(json.dumps({"theme": THEME, "menu_bg": "#%02X%02X%02X" % menu_bg,
                  "row": row, "menu_hover": menu_hover}, ensure_ascii=False, indent=2))
