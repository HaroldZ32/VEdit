#!/usr/bin/env python3
"""V13 pixel gate: Project sub-section boundaries.

Usage: measure-v13.py [<theme> ...]

For each theme it reads v13-project-<theme>.png (the crop made by driver_v13.py),
then:
  * finds each sub-section header as a connected fill of the theme's surfaceAlt
    (width >= 300, 15 <= height <= 60);
  * checks the 1px border rows directly above/below that fill are @border@ with a
    luminance delta >= 15 against @surface@ (title fill itself >= 5);
  * between two headers, finds the content QListWidget frame: long vertical runs
    of @border@ give the left/right border columns, long horizontal runs give the
    top/bottom rows;
  * checks every gap between one content bottom border and the next header top
    border is pure surface, >= 6 rows.

It also runs the SAME detector on the pre-fix reference crop
(ctxmenu-files-yunwu.png, same rectangle) to show the numbers before the change.

Prints JSON and a markdown table; writes v13-measure-<theme>.json.
"""
import json
import os
import re
import sys
from collections import deque

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
THEME_SOURCE = os.path.join(ROOT, "src", "app", "product_theme.cpp")
REFERENCE = os.path.join(ROOT, "evidence", "ui-mosaic", "screenshots",
                         "ctxmenu-files-yunwu.png")
CROP = (0, 60, 620, 640)  # must match driver_v13.py
SECTIONS = ["Files", "Include dirs", "Defines", "Diagnostics"]

MAKE_THEME_RE = re.compile(
    r'makeTheme\("(?P<id>[a-z0-9_]+)",\s*"[^"]*",\s*(?P<dark>true|false),\s*'
    r'(?P<canvas>0x[0-9A-Fa-f]{6}),\s*(?P<surface>0x[0-9A-Fa-f]{6}),'
)


def blend(color, target, percent):
    w = percent / 100.0
    return tuple(round(c * (1.0 - w) + t * w) for c, t in zip(color, target))


def theme_colors(theme_id):
    with open(THEME_SOURCE, encoding="utf-8") as handle:
        source = handle.read()
    for match in MAKE_THEME_RE.finditer(source):
        if match.group("id") != theme_id:
            continue
        canvas = tuple((int(match.group("canvas"), 16) >> s) & 0xFF for s in (16, 8, 0))
        surface = tuple((int(match.group("surface"), 16) >> s) & 0xFF for s in (16, 8, 0))
        dark = match.group("dark") == "true"
        text = (0xE6, 0xE8, 0xEC) if dark else (0x1F, 0x23, 0x28)
        return {
            "surface": surface,
            "surfaceAlt": blend(surface, canvas, 60),
            "border": blend(surface, text, 12),
        }
    raise SystemExit(f"theme {theme_id} not found in {THEME_SOURCE}")


def lum(color):
    r, g, b = [c / 255.0 for c in color]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def delta(a, b):
    return round(abs(lum(a) - lum(b)) * 255)


def near(pixel, color, tol=2):
    return all(abs(pixel[i] - color[i]) <= tol for i in range(3))


def header_components(image, fill):
    """4-connected boxes of the header fill colour, in the Project crop."""
    w, h = image.size
    pix = image.load()
    seen = bytearray(w * h)
    boxes = []
    for y in range(h):
        for x in range(w):
            if seen[y * w + x] or not near(pix[x, y], fill):
                continue
            queue = deque([(x, y)])
            seen[y * w + x] = 1
            x0 = x1 = x
            y0 = y1 = y
            area = 0
            while queue:
                cx, cy = queue.popleft()
                area += 1
                x0, x1 = min(x0, cx), max(x1, cx)
                y0, y1 = min(y0, cy), max(y1, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] \
                            and near(pix[nx, ny], fill):
                        seen[ny * w + nx] = 1
                        queue.append((nx, ny))
            if x1 - x0 + 1 >= 300 and 15 <= y1 - y0 + 1 <= 60 and area >= 2000:
                boxes.append({"left": x0, "right": x1, "top": y0, "bottom": y1,
                              "area": area})
    return sorted(boxes, key=lambda b: b["top"])


def horizontal_run(image, color, y, x_start, x_end):
    """Longest contiguous horizontal run of `color` in the row."""
    pix = image.load()
    best = run = 0
    for x in range(x_start, x_end + 1):
        if near(pix[x, y], color):
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def vertical_run(image, color, x, y_start, y_end):
    pix = image.load()
    best = run = 0
    for y in range(y_start, y_end + 1):
        if near(pix[x, y], color):
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def frame_rows(image, border, left, right, y_start, y_end):
    """Rows in [y_start, y_end] whose border-coloured run spans the frame width."""
    pix = image.load()
    rows = []
    for y in range(y_start, y_end + 1):
        span = sum(1 for x in range(left, right + 1) if near(pix[x, y], border))
        if span >= 0.9 * (right - left + 1):
            rows.append(y)
    return rows


def frame_cols(image, border, x_start, x_end, y_start, y_end):
    """Columns with a long contiguous vertical run of border colour."""
    pix = image.load()
    cols = []
    for x in range(x_start, x_end + 1):
        run = best = 0
        for y in range(y_start, y_end + 1):
            if near(pix[x, y], border):
                run += 1
                best = max(best, run)
            else:
                run = 0
        if best >= 12:
            cols.append(x)
    return cols


def measure(path, theme_id, label):
    colors = theme_colors(theme_id)
    surface, fill, border = colors["surface"], colors["surfaceAlt"], colors["border"]
    image = Image.open(path).convert("RGB")
    if image.size != (CROP[2] - CROP[0], CROP[3] - CROP[1]):
        image = image.crop(CROP)
    pix = image.load()
    headers = header_components(image, fill)
    result = {
        "label": label,
        "theme": theme_id,
        "surface": "#%02X%02X%02X" % surface,
        "surfaceAlt": "#%02X%02X%02X" % fill,
        "border": "#%02X%02X%02X" % border,
        "headerCount": len(headers),
        "panelWideHeaderRows": sum(
            1 for y in range(image.height)
            if sum(1 for x in range(24, 581) if near(pix[x, y], fill)) >= 300),
        "contentWideBorderRows": [y for y in range(90, 521)
                                  if horizontal_run(image, border, y, 24, 580) >= 300],
        "sections": [],
    }
    names = SECTIONS[:len(headers)] if len(headers) <= len(SECTIONS) else \
        [f"extra-{i}" for i in range(len(headers))]
    for index, header in enumerate(headers):
        y_top = header["top"] - 1
        y_bottom = header["bottom"] + 1
        centre_x = (header["left"] + header["right"]) // 2
        top_px = pix[centre_x, y_top]
        bottom_px = pix[centre_x, y_bottom]
        entry = {
            "name": names[index],
            "headerFillBBox": [header["left"], header["top"],
                               header["right"], header["bottom"]],
            "titleTopBorderY": y_top,
            "titleBottomBorderY": y_bottom,
            "titleTopBorder": "#%02X%02X%02X" % top_px,
            "titleBottomBorder": "#%02X%02X%02X" % bottom_px,
            "titleBorderDeltaTop": delta(top_px, surface),
            "titleBorderDeltaBottom": delta(bottom_px, surface),
            "titleTopBorderRun": horizontal_run(image, border, y_top,
                                                header["left"] - 1, header["right"] + 1),
            "titleBottomBorderRun": horizontal_run(image, border, y_bottom,
                                                   header["left"] - 1, header["right"] + 1),
            "titleFillDelta": delta(fill, surface),
            "titleBorderBrighter": lum(top_px) > lum(surface),
        }
        # Content frame: between this header and the next one (last: crop bottom).
        y_start = y_bottom + 2
        y_end = headers[index + 1]["top"] - 3 if index + 1 < len(headers) else image.height - 2
        cols = frame_cols(image, border, header["left"] - 2, header["right"] + 2,
                          y_start, y_end)
        if cols:
            left, right = min(cols), max(cols)
        else:
            left, right = header["left"], header["right"]
        rows = frame_rows(image, border, left, right, y_start, y_end)
        if cols and len(rows) >= 2:
            # The first two width-spanning border rows are the frame's top and
            # bottom; later rows belong to the card's own bottom border.
            top, bottom = rows[0], rows[1]
            mid_y = (top + bottom) // 2
            left_px = pix[left, mid_y]
            right_px = pix[right, mid_y]
            entry["content"] = {
                "bbox": [left, top, right, bottom],
                "leftBorderX": left,
                "rightBorderX": right,
                "topBorderY": top,
                "bottomBorderY": bottom,
                "leftBorder": "#%02X%02X%02X" % left_px,
                "rightBorder": "#%02X%02X%02X" % right_px,
                "leftDelta": delta(left_px, surface),
                "rightDelta": delta(right_px, surface),
                "leftBrighter": lum(left_px) > lum(surface),
                "rightBrighter": lum(right_px) > lum(surface),
                "leftRun": vertical_run(image, border, left, top, bottom),
                "rightRun": vertical_run(image, border, right, top, bottom),
            }
        else:
            entry["content"] = None
        result["sections"].append(entry)

    # Gaps: content bottom border -> next header top border, must be pure surface.
    gaps = []
    for index in range(len(result["sections"]) - 1):
        current = result["sections"][index]
        following = result["sections"][index + 1]
        if current["content"] is None:
            gaps.append({"after": current["name"], "rows": None, "pure": False})
            continue
        y_start = current["content"]["bottomBorderY"] + 1
        y_end = following["titleTopBorderY"] - 1
        left = current["content"]["bbox"][0]
        right = current["content"]["bbox"][2]
        pure_rows = []
        for y in range(y_start, y_end + 1):
            if all(near(pix[x, y], surface, tol=1) for x in range(left, right + 1)):
                pure_rows.append(y)
        runs = []
        for y in pure_rows:
            if runs and y == runs[-1][1] + 1:
                runs[-1][1] = y
            else:
                runs.append([y, y])
        runs = [{"start": a, "end": b, "rows": b - a + 1} for a, b in runs]
        gaps.append({
            "after": current["name"],
            "before": following["name"],
            "spanRows": max(0, y_end - y_start + 1),
            "pureRuns": runs,
            "maxPureRun": max([r["rows"] for r in runs], default=0),
            "firstPureRun": runs[0]["rows"] if runs else 0,
            "lastPureRun": runs[-1]["rows"] if runs else 0,
        })
    result["gaps"] = gaps
    return result


def main():
    themes = sys.argv[1:] or ["yunwu", "yaoshi"]
    all_results = []
    for theme in themes:
        path = os.path.join(HERE, f"v13-project-{theme}.png")
        if not os.path.exists(path):
            raise SystemExit(f"missing {path}")
        result = measure(path, theme, "after")
        with open(os.path.join(HERE, f"v13-measure-{theme}.json"), "w",
                  encoding="utf-8") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2)
        all_results.append(result)
        print(json.dumps(result, ensure_ascii=False, indent=2))

    if os.path.exists(REFERENCE):
        before = measure(REFERENCE, "yunwu", "before")
        with open(os.path.join(HERE, "v13-measure-before.json"), "w",
                  encoding="utf-8") as handle:
            json.dump(before, handle, ensure_ascii=False, indent=2)
        print("\nBEFORE:", json.dumps(before, ensure_ascii=False, indent=2))

    print("\n| theme | section | title top/bottom border y | Δ top/bottom | content L/R x | Δ L/R | gap rows after |")
    print("|---|---|---|---|---|---|---|")
    for result in all_results:
        for index, entry in enumerate(result["sections"]):
            content = entry["content"] or {}
            gap = result["gaps"][index]["maxPureRun"] if index < len(result["gaps"]) else "-"
            print("| {theme} | {name} | {top}/{bottom} | {dt}/{db} | {left}/{right} | "
                  "{dl}/{dr} | {gap} |".format(
                      theme=result["theme"], name=entry["name"],
                      top=entry["titleTopBorderY"], bottom=entry["titleBottomBorderY"],
                      dt=entry["titleBorderDeltaTop"], db=entry["titleBorderDeltaBottom"],
                      left=content.get("leftBorderX", "-"),
                      right=content.get("rightBorderX", "-"),
                      dl=content.get("leftDelta", "-"),
                      dr=content.get("rightDelta", "-"), gap=gap))


if __name__ == "__main__":
    main()
