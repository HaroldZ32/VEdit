#!/usr/bin/env python3
"""V5 验收探针：右栏三张纵向卡片之间的画布间隙 + 卡片四边阴影像素。

判定（与任务书 a/b 一一对应）：
  a 在 x=1100 列，Module Tree 卡底边框与 Interface 卡顶边框之间，
    "与 canvas 色最大通道差 Δ<=3" 的最长连续像素数必须 >= 6。
  b 从 Interface 卡四条外边（含 1px 描边）向外扫，连续阴影像素的
    "与 canvas 色 Δ>=3" 长度：每边必须 >= 3。

用法：
    python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap.py \
        evidence/ui-mosaic/screenshots/after-yunwu.png --theme yunwu
    python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap.py --both

canvas / border 颜色取自 src/app/product_theme.cpp 的契约表（yunwu / yaoshi），
几何全部从截图像素里检测，不写死 y 坐标。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
THEME_SOURCE = os.path.join(ROOT, "src", "app", "product_theme.cpp")
TEXT_LIGHT = (0x1F, 0x23, 0x28)
TEXT_DARK = (0xE6, 0xE8, 0xEC)
BORDER_BLEND_PERCENT = 12

MAKE_THEME_RE = re.compile(
    r'makeTheme\("(?P<id>[a-z0-9_]+)",\s*"[^"]*",\s*(?P<dark>true|false),\s*'
    r'(?P<canvas>0x[0-9A-Fa-f]{6}),\s*(?P<surface>0x[0-9A-Fa-f]{6}),'
)


def rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def blend_toward(color, target, percent):
    weight = percent / 100.0
    return tuple(round(c * (1.0 - weight) + t * weight) for c, t in zip(color, target))


def load_themes() -> dict[str, dict]:
    source = open(THEME_SOURCE, encoding="utf-8").read()
    themes: dict[str, dict] = {}
    for match in MAKE_THEME_RE.finditer(source):
        dark = match.group("dark") == "true"
        canvas = rgb(int(match.group("canvas"), 16))
        surface = rgb(int(match.group("surface"), 16))
        text = TEXT_DARK if dark else TEXT_LIGHT
        themes[match.group("id")] = {
            "dark": dark,
            "canvas": canvas,
            "surface": surface,
            "border": blend_toward(surface, text, BORDER_BLEND_PERCENT),
        }
    return themes


def dmax(a, b) -> int:
    return max(abs(x - y) for x, y in zip(a, b))


class Shot:
    def __init__(self, path: str) -> None:
        self.image = Image.open(path).convert("RGB")
        self.px = self.image.load()
        self.w, self.h = self.image.size

    def column(self, x: int, y0: int, y1: int):
        return [self.px[x, y] for y in range(y0, y1)]


def find_card_rects(shot: Shot, theme: dict) -> list[tuple[int, int, int, int]]:
    """在右栏用描边色找 3 张卡的 (x0, y0, x1, y1)，含 1px 描边。"""
    border = theme["border"]

    def is_border(c):
        return dmax(c, border) <= 3

    # 右栏竖直描边：在 x 920..1280 里找命中行数最多的两列。
    cols = []
    for x in range(920, 1281):
        n = sum(1 for y in range(60, 820) if is_border(shot.px[x, y]))
        cols.append((n, x))
    cols.sort(reverse=True)
    if len(cols) < 2 or cols[1][0] < 200:
        raise SystemExit("FATAL: cannot locate the right column card borders")
    x0, x1 = sorted((cols[0][1], cols[1][1]))

    # 卡片内部 = 该行任意像素是 surface 或 surfaceAlt；卡片上下 1px 描边
    # 因此落在内部行带之外，不会和表头分隔线混淆。
    surface_alt = blend_toward(theme["surface"], theme["canvas"], 60)

    def is_content(c):
        return dmax(c, theme["surface"]) <= 2 or dmax(c, surface_alt) <= 2

    interior = []
    for y in range(70, 805):
        n = sum(1 for x in range(x0 + 4, x1 - 3) if is_content(shot.px[x, y]))
        if n >= 20:  # 排除圆角/抗锯齿产生的零星卡片色像素
            interior.append(y)
    runs = []
    for y in interior:
        if runs and y - runs[-1][1] <= 2:  # 表头分隔线也会落进同一个卡片
            runs[-1][1] = y
        else:
            runs.append([y, y])
    if len(runs) != 3:
        raise SystemExit(f"FATAL: expected 3 card interior runs, got {runs}")
    cards = []
    for y0, y1 in runs:
        top, bottom = y0 - 1, y1 + 1
        if not (is_border(shot.px[x0 + 20, top]) and is_border(shot.px[x0 + 20, bottom])):
            raise SystemExit(f"FATAL: card border missing around interior {y0}..{y1}")
        cards.append((x0, top, x1, bottom))
    return cards


def canvas_run(shot: Shot, theme: dict, x: int, y0: int, y1: int) -> int:
    canvas = theme["canvas"]
    best = cur = 0
    for y in range(y0, y1):
        if dmax(shot.px[x, y], canvas) <= 3:
            cur += 1
            best = max(best, cur)
        else:
            cur = 0
    return best


def edge_run(shot: Shot, theme: dict, pts) -> int:
    canvas = theme["canvas"]
    n = 0
    for x, y in pts:
        if dmax(shot.px[x, y], canvas) >= 3:
            n += 1
        else:
            break
    return n


def measure(path: str, theme_id: str, themes: dict) -> dict:
    theme = themes[theme_id]
    shot = Shot(path)
    cards = find_card_rects(shot, theme)
    (_, mt_y0, _, mt_y1), (x0, if_y0, x1, if_y1), _ = cards
    mid = (if_y0 + if_y1) // 2

    gap = canvas_run(shot, theme, 1100, mt_y1 + 1, if_y0)
    edges = {
        "top": edge_run(shot, theme, [(1100, y) for y in range(if_y0 - 1, if_y0 - 30, -1)]),
        "bottom": edge_run(shot, theme, [(1100, y) for y in range(if_y1 + 1, if_y1 + 30)]),
        "left": edge_run(shot, theme, [(x, mid) for x in range(x0 - 1, x0 - 30, -1)]),
        "right": edge_run(shot, theme, [(x, mid) for x in range(x1 + 1, x1 + 30)]),
    }
    return {
        "image": os.path.relpath(path, ROOT),
        "theme": theme_id,
        "canvas": "#%02X%02X%02X" % theme["canvas"],
        "cards": [{"x0": c[0], "y0": c[1], "x1": c[2], "y1": c[3]} for c in cards],
        "a_vertical_canvas_run_px": gap,
        "a_ok": gap >= 6,
        "b_edges_px": edges,
        "b_ok": all(v >= 3 for v in edges.values()),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", nargs="?")
    parser.add_argument("--theme", default="yunwu")
    parser.add_argument("--both", action="store_true")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    themes = load_themes()
    targets = []
    if args.both:
        shots = os.path.join(ROOT, "evidence", "ui-mosaic", "screenshots")
        targets = [(os.path.join(shots, f"after-{t}.png"), t) for t in ("yunwu", "yaoshi")]
    else:
        if not args.image:
            parser.error("image or --both is required")
        targets = [(args.image, args.theme)]

    results = [measure(path, theme, themes) for path, theme in targets]
    for r in results:
        print(f"[{r['theme']}] {r['image']} canvas={r['canvas']}")
        print(f"  a vertical canvas run (x=1100): {r['a_vertical_canvas_run_px']}px "
              f"{'OK' if r['a_ok'] else 'FAIL'} (need >=6)")
        e = r["b_edges_px"]
        print(f"  b edge shadow run: top={e['top']} bottom={e['bottom']} "
              f"left={e['left']} right={e['right']} "
              f"{'OK' if r['b_ok'] else 'FAIL'} (need each >=3)")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, ensure_ascii=False, indent=2)
    return 0 if all(r["a_ok"] and r["b_ok"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
