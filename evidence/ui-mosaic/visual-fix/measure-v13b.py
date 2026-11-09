#!/usr/bin/env python3
"""V13b gate: header padding 4px->1px must give the list rows their height back.

Usage: measure-v13b.py [<theme> ...]

Reuses the V13 detector (surfaceAlt header fill + @border@ frames) from
measure-v13.py, then adds the row-level judgement the user asked for:

  * viewport height = content frame bottomBorderY - topBorderY - 1 (the two 1px
    border rows are excluded);
  * item height = the pitch between consecutive text bands inside the list
    (setUniformItemSizes(true) in project_panel.cpp makes it constant);
  * remainder = viewport_height % item_height;
  * the LAST text band must keep its full glyph height (a clipped row shows a
    short band, e.g. 3px instead of 9px) and must end above the viewport bottom.

Also crops the Defines list at 2x for human inspection and keeps the V13 gap
check (6px pure surface between sub-sections).
"""
import importlib.util
import json
import os
import statistics
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
V13 = os.path.join(HERE, "measure-v13.py")
spec = importlib.util.spec_from_file_location("measure_v13", V13)
v13 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v13)

SECTIONS = v13.SECTIONS


def text_bands(image, surface, left, right, top, bottom):
    """Rows inside the frame that carry text: luminance differs from surface."""
    pix = image.load()

    def is_text(pixel):
        return v13.delta(pixel, surface) >= 20

    # x range stops well before the right edge: a visible vertical scrollbar
    # would otherwise put a non-surface pixel on every row and merge the bands.
    x_end = min(right - 8, left + 300)
    bands = []
    for y in range(top + 1, bottom):
        count = sum(1 for x in range(left + 8, x_end) if is_text(pix[x, y]))
        if count >= 2:
            if bands and y == bands[-1][1] + 1:
                bands[-1][1] = y
            else:
                bands.append([y, y])
    return [{"start": a, "end": b, "height": b - a + 1} for a, b in bands]


def row_metrics(image, section, theme_id):
    colors = v13.theme_colors(theme_id)
    content = section["content"]
    if content is None:
        return None
    surface = colors["surface"]
    left, top, right, bottom = content["bbox"]
    bands = text_bands(image, surface, left, right, top, bottom)
    if len(bands) < 2:
        return {"bands": bands, "error": "fewer than two text bands"}
    starts = [b["start"] for b in bands]
    pitches = [b - a for a, b in zip(starts, starts[1:])]
    row_height = int(statistics.median(pitches))
    viewport = content["bottomBorderY"] - content["topBorderY"] - 1
    heights = [b["height"] for b in bands]
    glyph = int(statistics.median(heights))
    last = bands[-1]
    return {
        "section": section["name"],
        "frame": [left, top, right, bottom],
        "viewportHeight": viewport,
        "rowHeight": row_height,
        "remainder": viewport % row_height,
        "fullRows": viewport // row_height,
        # A partial next row that is missing <= 2px is visually complete; a
        # remainder of 0/1 means no partial row is shown at all.
        "clipDeficit": 0 if viewport % row_height <= 1 else row_height - viewport % row_height,
        "lastRowComplete": (viewport % row_height <= 1
                            or row_height - viewport % row_height <= 1),
        "bandPitches": pitches,
        "textBands": bands,
        "glyphHeightMedian": glyph,
        "lastBand": last,
        "lastBandFullGlyph": last["height"] >= glyph - 1,
        "lastBandInsideViewport": last["end"] <= content["bottomBorderY"] - 1,
        "rowsNeeded": len(bands),
    }


def main():
    themes = sys.argv[1:] or ["yunwu", "yaoshi"]
    results = []
    for theme in themes:
        path = os.path.join(HERE, f"v13b-project-{theme}.png")
        if not os.path.exists(path):
            raise SystemExit(f"missing {path}")
        measured = v13.measure(path, theme, "after")
        image = Image.open(path).convert("RGB")
        rows = [row_metrics(image, section, theme)
                for section in measured["sections"]]
        measured["rows"] = rows
        # Human-inspection crop of the Defines list at 2x.
        defines = next((r for r in rows if r and r["section"] == "Defines"), None)
        if defines is not None:
            left, top, right, bottom = defines["frame"]
            crop = image.crop((left - 4, top - 22, right + 4, bottom + 4))
            crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST).save(
                os.path.join(HERE, f"v13b-defines-{theme}-2x.png"))
        with open(os.path.join(HERE, f"v13b-measure-{theme}.json"), "w",
                  encoding="utf-8") as handle:
            json.dump(measured, handle, ensure_ascii=False, indent=2)
        results.append(measured)

    print("| theme | section | viewport h | item h | remainder | full rows | "
          "last band | band full glyph | clip | lists needed |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for result in results:
        for row in result["rows"]:
            if row is None or "error" in row:
                print(f"| {result['theme']} | - | {row} | | | | | | |")
                continue
            print("| {theme} | {sec} | {vp} | {rh} | {rem} | {fr} | {ls} | {full} | {clip} | {need} |"
                  .format(theme=result["theme"], sec=row["section"],
                          vp=row["viewportHeight"], rh=row["rowHeight"],
                          rem=row["remainder"], fr=row["fullRows"],
                          ls=row["lastBand"], full=row["lastBandFullGlyph"],
                          clip=row["clipDeficit"], need=row["rowsNeeded"]))
    print()
    print("| theme | section | title border ΔL top/bottom | title fill ΔL | gaps |")
    print("|---|---|---|---|---|")
    for result in results:
        for index, entry in enumerate(result["sections"]):
            gap = result["gaps"][index]["maxPureRun"] if index < len(result["gaps"]) else "-"
            print("| {} | {} | {}/{} | {} | {} |".format(
                result["theme"], entry["name"], entry["titleBorderDeltaTop"],
                entry["titleBorderDeltaBottom"], entry["titleFillDelta"], gap))


if __name__ == "__main__":
    main()
