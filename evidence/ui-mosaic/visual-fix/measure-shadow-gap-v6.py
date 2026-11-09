#!/usr/bin/env python3
"""V6 验收探针：右栏三张卡的逐卡四边阴影 + 相邻卡片之间的纵向画布间隙。

与 measure-shadow-gap.py 同一套几何检测（描边色 / surface 色都取自
src/app/product_theme.cpp，y 坐标全部从截图像素检测），区别是 V5 脚本只量
Interface 一张卡，V6 要求逐卡数字（Module Tree / Interface / Outline）。

判定：
  * 每张卡四条外边向外扫，"与 canvas 色 Δ>=3" 的连续像素数每边 >= 3；
  * 相邻两张卡的描边之间，与 canvas 色 Δ<=3 的最长连续像素数 >= 6。

用法：
    python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap-v6.py --both
    python3 evidence/ui-mosaic/visual-fix/measure-shadow-gap-v6.py \
        evidence/ui-mosaic/screenshots/after-yaoshi.png --theme yaoshi
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BASE = os.path.join(HERE, "measure-shadow-gap.py")

_spec = importlib.util.spec_from_file_location("measure_shadow_gap", BASE)
BASE_MOD = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(BASE_MOD)

CARD_NAMES = ("Module Tree", "Interface", "Outline")


def measure_card(shot, theme, rect) -> dict:
    x0, y0, x1, y1 = rect
    mid_x = (x0 + x1) // 2
    mid_y = (y0 + y1) // 2
    edges = {
        "top": BASE_MOD.edge_run(shot, theme, [(mid_x, y) for y in range(y0 - 1, y0 - 30, -1)]),
        "bottom": BASE_MOD.edge_run(shot, theme, [(mid_x, y) for y in range(y1 + 1, y1 + 30)]),
        "left": BASE_MOD.edge_run(shot, theme, [(x, mid_y) for x in range(x0 - 1, x0 - 30, -1)]),
        "right": BASE_MOD.edge_run(shot, theme, [(x, mid_y) for x in range(x1 + 1, x1 + 30)]),
    }
    return {"edges_px": edges, "ok": all(v >= 3 for v in edges.values())}


def measure(path: str, theme_id: str, themes: dict) -> dict:
    theme = themes[theme_id]
    shot = BASE_MOD.Shot(path)
    cards = BASE_MOD.find_card_rects(shot, theme)

    rows = []
    for name, rect in zip(CARD_NAMES, cards):
        row = {"name": name, "rect": list(rect)}
        row.update(measure_card(shot, theme, rect))
        rows.append(row)

    gaps = []
    for i in range(len(cards) - 1):
        upper, lower = cards[i], cards[i + 1]
        gaps.append({
            "between": f"{CARD_NAMES[i]} -> {CARD_NAMES[i + 1]}",
            "canvas_run_px": BASE_MOD.canvas_run(shot, theme, 1100, upper[3] + 1, lower[1]),
        })

    return {
        "image": os.path.relpath(path, ROOT),
        "theme": theme_id,
        "canvas": "#%02X%02X%02X" % theme["canvas"],
        "cards": rows,
        "gaps": gaps,
        "edges_ok": all(r["ok"] for r in rows),
        "gaps_ok": all(g["canvas_run_px"] >= 6 for g in gaps),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", nargs="?")
    parser.add_argument("--theme", default="yunwu")
    parser.add_argument("--both", action="store_true")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    themes = BASE_MOD.load_themes()
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
        for card in r["cards"]:
            e = card["edges_px"]
            print(f"  {card['name']:<11} top={e['top']} bottom={e['bottom']} "
                  f"left={e['left']} right={e['right']} "
                  f"{'OK' if card['ok'] else 'FAIL'} (need each >=3)")
        for gap in r["gaps"]:
            print(f"  gap {gap['between']}: {gap['canvas_run_px']}px "
                  f"{'OK' if gap['canvas_run_px'] >= 6 else 'FAIL'} (need >=6)")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, ensure_ascii=False, indent=2)
    return 0 if all(r["edges_ok"] and r["gaps_ok"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
