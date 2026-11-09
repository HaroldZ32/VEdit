#!/usr/bin/env python3
"""V1 视觉验收探针（UI-Mosaic V1：卡片可辨识 / 可见描边 / 卡片阴影 / 按钮轮廓）。

为什么要有这个文件：用户反馈的三件事（卡片与背景同色、没有框边阴影、圆角按钮
没有轮廓）都不是“看起来差不多”能验收的——它们各自都能被真实截图的像素证伪。
本探针把验收条件 A–E 写成可重跑的判定，对 `capture-themes.sh` 抓到的真图逐套
主题跑一遍，任何一条不满足就以非零退出码结束。

用法：
    python3 evidence/ui-mosaic/visual-fix/probe.py --all
    python3 evidence/ui-mosaic/visual-fix/probe.py --image <png> --theme yunwu
    python3 evidence/ui-mosaic/visual-fix/probe.py --all --json probe.json

判定项（与任务书 A–E 一一对应）：
  A 卡片内部采样区的主色 == surface（±2），且占该区域 >= 80%。
  B 垂直跨过卡片上边缘的扫描里，紧贴卡片填充的那一像素：
      与 surface 的通道差 >= 15、与 canvas 的通道差 >= 10；
      并且它的颜色落在派生描边色 ±8 之内（证明那是描边，不是别的元素）。
  C 卡片下边缘之外存在 >= 4px 连续阴影带（与 canvas 的通道差 >= 3）。
  D 工具栏里 >= 3 个按钮：外轮廓与按钮填充的通道差 >= 20，且按钮是圆角
    （填充矩形的角沿对角线至少要有 2 个非填充像素）。
  E 六套主题全过；深色主题的描边像素必须比 surface **更亮**。
  F1 窗口最外圈 >=8px 内不得出现卡片像素（surface/描边色）；即卡片描边不得
     贴窗口边缘（V3：dock 布局原本把面板贴死在 x=0/1279）。
  F2 横向相邻两卡之间存在 >=6px 连续 canvas（Δ<=3），且这段间隙不是实心填充：
     跨间隙采样必须出现 >=3 个不同色阶（F3）。
  F4（附报）横向间隙里靠窗口一侧的最外圈 8px 也必须是 canvas。

颜色数据不写死：canvas/surface 直接从 src/app/product_theme.cpp 的 makeTheme()
解析（契约表是唯一事实来源），border/borderStrong 用与 product_theme.cpp 相同的
派生规则（surface 向 text 混合 12% / 22%）在 Python 里复算。

注意：外部黑边（Xvfb 根窗口）按“非黑像素的包围盒”裁出应用窗口，所有几何都用
窗口内的相对坐标，所以窗口尺寸变化不会让判定失效。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
THEME_SOURCE = os.path.join(ROOT, "src", "app", "product_theme.cpp")

# 与 product_theme.cpp 一致的派生参数（kBorderBlendPercent / kBorderStrongBlendPercent）
BORDER_BLEND_PERCENT = 12
BORDER_STRONG_BLEND_PERCENT = 22
TEXT_LIGHT = (0x1F, 0x23, 0x28)
TEXT_DARK = (0xE6, 0xE8, 0xEC)

MAKE_THEME_RE = re.compile(
    r'makeTheme\("(?P<id>[a-z0-9_]+)",\s*"[^"]*",\s*(?P<dark>true|false),\s*'
    r'(?P<canvas>0x[0-9A-Fa-f]{6}),\s*(?P<surface>0x[0-9A-Fa-f]{6}),'
)


def _rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def load_themes() -> dict[str, dict]:
    """契约表 -> {id: {canvas, surface, dark, text, border, borderStrong}}。"""
    with open(THEME_SOURCE, encoding="utf-8") as handle:
        source = handle.read()
    themes: dict[str, dict] = {}
    for match in MAKE_THEME_RE.finditer(source):
        dark = match.group("dark") == "true"
        canvas = _rgb(int(match.group("canvas"), 16))
        surface = _rgb(int(match.group("surface"), 16))
        text = TEXT_DARK if dark else TEXT_LIGHT
        themes[match.group("id")] = {
            "dark": dark,
            "canvas": canvas,
            "surface": surface,
            "text": text,
            "border": blend_toward(surface, text, BORDER_BLEND_PERCENT),
            "borderStrong": blend_toward(surface, text, BORDER_STRONG_BLEND_PERCENT),
        }
    if len(themes) != 6:
        raise SystemExit(f"FATAL: expected 6 themes in {THEME_SOURCE}, found {len(themes)}")
    return themes


def blend_toward(color: tuple[int, int, int], target: tuple[int, int, int], percent: int) -> tuple[int, int, int]:
    weight = max(0, min(100, percent)) / 100.0
    return tuple(round(c * (1.0 - weight) + t * weight) for c, t in zip(color, target))


def dist(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    """两个颜色的最大通道差（探针的“亮度差”统一用这个量纲：sRGB 单位）。"""
    return max(abs(x - y) for x, y in zip(a, b))


def luma(color: tuple[int, int, int]) -> float:
    return 0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2]


class Shot:
    """一张真实截图 + 裁出来的应用窗口。"""

    def __init__(self, path: str) -> None:
        self.path = path
        self.image = Image.open(path).convert("RGB")
        self.pixels = self.image.load()
        self.window = self._window_rect()
        self.w, self.h = self.window[2] - self.window[0], self.window[3] - self.window[1]

    def _window_rect(self) -> tuple[int, int, int, int]:
        # Xvfb 根窗口是纯黑；应用窗口是“非黑像素”的包围盒。
        width, height = self.image.size
        min_x, min_y, max_x, max_y = width, height, -1, -1
        for y in range(0, height, 2):
            row = [self.pixels[x, y] for x in range(0, width, 2)]
            for index, color in enumerate(row):
                if color != (0, 0, 0):
                    x = index * 2
                    min_x = min(min_x, x)
                    max_x = max(max_x, x)
                    min_y = min(min_y, y)
                    max_y = max(max_y, y)
        if max_x < 0 or max_x - min_x < 800 or max_y - min_y < 600:
            raise SystemExit(f"FATAL: {self.path}: cannot locate the application window")
        return (min_x, min_y, max_x + 1, max_y + 1)

    def at(self, rel_x: float, rel_y: float) -> tuple[int, int, int]:
        x = self.window[0] + int(rel_x * self.w)
        y = self.window[1] + int(rel_y * self.h)
        return self.pixels[min(x, self.image.size[0] - 1), min(y, self.image.size[1] - 1)]

    def px(self, x: int, y: int) -> tuple[int, int, int]:
        return self.pixels[x, y]


def check_card_interior(shot: Shot, theme: dict, name: str, roi: tuple[float, float, float, float]) -> dict:
    """A：卡片内部的主色必须是 surface（±2）。"""
    x0, y0 = shot.window[0] + int(roi[0] * shot.w), shot.window[1] + int(roi[1] * shot.h)
    x1, y1 = shot.window[0] + int(roi[2] * shot.w), shot.window[1] + int(roi[3] * shot.h)
    counter: Counter = Counter()
    for y in range(y0, y1):
        for x in range(x0, x1):
            counter[shot.px(x, y)] += 1
    dominant, count = counter.most_common(1)[0]
    total = (x1 - x0) * (y1 - y0)
    fraction = count / total if total else 0.0
    ok = dist(dominant, theme["surface"]) <= 2 and fraction >= 0.80
    return {
        "metric": "A",
        "name": name,
        "ok": ok,
        "dominant": "#%02X%02X%02X" % dominant,
        "surface": "#%02X%02X%02X" % theme["surface"],
        "distance": dist(dominant, theme["surface"]),
        "fraction": round(fraction, 4),
    }


def check_border_scan(shot: Shot, theme: dict) -> dict:
    """B：垂直跨过编辑器卡片上边缘，紧贴填充的那一像素必须是可见描边。"""
    x = shot.window[0] + int(0.55 * shot.w)
    start = shot.window[1] + int(0.093 * shot.h)
    end = shot.window[1] + int(0.16 * shot.h)
    fill_y = None
    for y in range(start, end):
        if dist(shot.px(x, y), theme["surface"]) <= 2:
            fill_y = y
            break
    if fill_y is None or fill_y <= start:
        return {"metric": "B", "ok": False, "detail": "no card fill found in the vertical scan"}
    edge_y = fill_y - 1
    edge = shot.px(x, edge_y)
    d_surface = dist(edge, theme["surface"])
    d_canvas = dist(edge, theme["canvas"])
    border_match = dist(edge, theme["border"])
    canvas_gap = dist(theme["canvas"], theme["surface"])
    # 深色主题的描边必须比 surface 更亮（浅色主题反之）。
    brighter = luma(edge) > luma(theme["surface"])
    direction_ok = brighter if theme["dark"] else not brighter
    ok = (
        d_surface >= 15
        and d_canvas >= 10
        and border_match <= 8
        and direction_ok
        # 没有描边时这条扫描会直接撞上 canvas（差 canvas_gap 个单位的过渡），
        # 所以“描边必须比 canvas 离 surface 更远”是描边存在的必要条件。
        and d_surface > canvas_gap
    )
    return {
        "metric": "B",
        "ok": ok,
        "x": x,
        "edge_y": edge_y,
        "fill_y": fill_y,
        "edge": "#%02X%02X%02X" % edge,
        "surface": "#%02X%02X%02X" % theme["surface"],
        "canvas": "#%02X%02X%02X" % theme["canvas"],
        "border_expected": "#%02X%02X%02X" % theme["border"],
        "delta_surface": d_surface,
        "delta_canvas": d_canvas,
        "border_distance": border_match,
        "brighter_than_surface": brighter,
        "surface_canvas_gap": canvas_gap,
    }


def check_shadow_band(shot: Shot, theme: dict) -> dict:
    """C：卡片下边缘之外 >= 4px 的可见阴影带（与 canvas 差 >= 3）。"""
    x = shot.window[0] + int(0.25 * shot.w)
    start = shot.window[1] + int(0.88 * shot.h)
    end = shot.window[1] + int(0.995 * shot.h)
    border_y = None
    for y in range(start, end):
        if dist(shot.px(x, y), theme["surface"]) > 10:
            border_y = y
            break
    if border_y is None:
        return {"metric": "C", "ok": False, "detail": "no card bottom edge found"}
    band = 0
    samples = []
    for y in range(border_y + 1, min(border_y + 24, shot.window[3])):
        color = shot.px(x, y)
        delta = dist(color, theme["canvas"])
        samples.append(delta)
        if delta >= 3:
            band += 1
        else:
            break
    return {
        "metric": "C",
        "ok": band >= 4,
        "x": x,
        "edge_y": border_y,
        "band_px": band,
        "deltas": samples[:10],
    }


def _components(shot: Shot, strip: tuple[int, int, int, int], target: tuple[int, int, int], tol: int) -> list[tuple[int, int, int, int]]:
    """简单 4-邻域连通域，返回填充色的 bbox 列表。"""
    x0, y0, x1, y1 = strip
    width = x1 - x0
    seen = bytearray(width * (y1 - y0))
    boxes = []
    for j in range(y0, y1):
        for i in range(x0, x1):
            index = (j - y0) * width + (i - x0)
            if seen[index]:
                continue
            color = shot.px(i, j)
            if dist(color, target) > tol:
                continue
            stack = [(i, j)]
            seen[index] = 1
            min_x = max_x = i
            min_y = max_y = j
            count = 0
            while stack:
                cx, cy = stack.pop()
                count += 1
                min_x, max_x = min(min_x, cx), max(max_x, cx)
                min_y, max_y = min(min_y, cy), max(max_y, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if x0 <= nx < x1 and y0 <= ny < y1:
                        nindex = (ny - y0) * width + (nx - x0)
                        if not seen[nindex] and dist(shot.px(nx, ny), target) <= tol:
                            seen[nindex] = 1
                            stack.append((nx, ny))
            if count >= 200:
                boxes.append((min_x, min_y, max_x, max_y))
    boxes.sort(key=lambda box: box[0])
    return boxes


def check_buttons(shot: Shot, theme: dict, minimum: int = 3) -> dict:
    """D：工具栏按钮有可见轮廓（轮廓 vs 填充 >= 20），且仍然是圆角。"""
    strip = (shot.window[0], shot.window[1] + int(0.035 * shot.h), shot.window[2], shot.window[1] + int(0.092 * shot.h))
    boxes = _components(shot, strip, theme["surface"], 6)
    checked = []
    for box in boxes:
        x0, y0, x1, y1 = box
        width, height = x1 - x0 + 1, y1 - y0 + 1
        if width < 40 or height < 18:
            continue
        mid_x, mid_y = (x0 + x1) // 2, (y0 + y1) // 2
        edges = {
            "left": shot.px(x0 - 1, mid_y) if x0 - 1 >= 0 else None,
            "right": shot.px(x1 + 1, mid_y),
            "top": shot.px(mid_x, y0 - 1),
            "bottom": shot.px(mid_x, y1 + 1),
        }
        deltas = {side: (dist(color, theme["surface"]) if color else 0) for side, color in edges.items()}
        outline_ok = min(deltas.values()) >= 20
        # 圆角：填充 bbox 的角沿对角线至少要有 2 个非填充像素（直角按钮为 0）。
        corner_gap = 0
        for step in range(8):
            if dist(shot.px(x0 + step, y0 + step), theme["surface"]) <= 6:
                break
            corner_gap += 1
        checked.append(
            {
                "box": [x0, y0, width, height],
                "outline_deltas": deltas,
                "outline_ok": outline_ok,
                "corner_gap": corner_gap,
                "rounded_ok": corner_gap >= 2,
            }
        )
        if len(checked) >= 6:
            break
    passed = [entry for entry in checked if entry["outline_ok"] and entry["rounded_ok"]]
    return {
        "metric": "D",
        "ok": len(passed) >= minimum,
        "buttons_found": len(checked),
        "buttons_passed": len(passed),
        "detail": checked,
    }


# 卡片内部采样区（窗口内相对坐标）。左右面板取空白面板体，编辑器卡片取
# 标签条 chrome——**不取**编辑器的文本视口：文本底色由配对的 KSyntaxHighlighting
# 主题决定（Solarized Light #FDF6E3 / Breeze Dark #232629），那是编辑器内容，
# 不是产品卡片底色，把它算进 A 会把“编辑器跟随语法主题”误判成卡片缺陷。
def _is_canvas(color: tuple[int, int, int], theme: dict) -> bool:
    return dist(color, theme["canvas"]) <= 3


def _is_card_like(color: tuple[int, int, int], theme: dict) -> bool:
    """卡片自身的像素：卡片底或描边色（不算文字/图标/画布）。"""
    return dist(color, theme["surface"]) <= 2 or dist(color, theme["border"]) <= 4


def check_outer_margin(shot: Shot, theme: dict) -> dict:
    """F1：窗口最外圈 8px 内不得出现卡片像素（卡片不能贴窗口边缘）。

    为什么按“卡片像素”而不是“第一个非画布像素”：状态栏文字与菜单文字本来就
    写在窗口最外几行/列上（例如左下角 “Project: no project” 从 y=812 起），
    它们不是卡片。V1/V3 要修的是**卡片描边贴死窗口边**，所以判据取“第一个
    卡片像素离窗口边的距离”，并附报每条边“第一个非画布像素”的距离供对照。
    """
    left, top = shot.window[0], shot.window[1]
    right, bottom = shot.window[2] - 1, shot.window[3] - 1
    rows = [0.25, 0.40, 0.55, 0.70, 0.85]
    cols = [0.30, 0.50, 0.70]
    edges = {"left": 10 ** 9, "right": 10 ** 9, "top": 10 ** 9, "bottom": 10 ** 9}
    first_non_canvas = {"left": 10 ** 9, "right": 10 ** 9, "top": 10 ** 9, "bottom": 10 ** 9}
    for rel in rows:
        y = top + int(rel * shot.h)
        for x in range(left, left + 400):
            color = shot.px(x, y)
            if _is_card_like(color, theme):
                edges["left"] = min(edges["left"], x - left)
                break
            if not _is_canvas(color, theme):
                first_non_canvas["left"] = min(first_non_canvas["left"], x - left)
        for x in range(right, right - 400, -1):
            color = shot.px(x, y)
            if _is_card_like(color, theme):
                edges["right"] = min(edges["right"], right - x)
                break
            if not _is_canvas(color, theme):
                first_non_canvas["right"] = min(first_non_canvas["right"], right - x)
    for rel in cols:
        x = left + int(rel * shot.w)
        for y in range(top, top + 400):
            color = shot.px(x, y)
            if _is_card_like(color, theme):
                edges["top"] = min(edges["top"], y - top)
                break
            if not _is_canvas(color, theme):
                first_non_canvas["top"] = min(first_non_canvas["top"], y - top)
        for y in range(bottom, bottom - 400, -1):
            color = shot.px(x, y)
            if _is_card_like(color, theme):
                edges["bottom"] = min(edges["bottom"], bottom - y)
                break
            if not _is_canvas(color, theme):
                first_non_canvas["bottom"] = min(first_non_canvas["bottom"], bottom - y)
    ok = all(distance >= 8 for distance in edges.values())
    return {
        "metric": "F1",
        "ok": ok,
        "card_distance": edges,
        "first_non_canvas": first_non_canvas,
    }


def _widest_bounded_run(shot: Shot, theme: dict, horizontal: bool) -> dict:
    """在若干扫描线里找“两侧都被非画布像素夹住”的最长 canvas 连续段。"""
    left, top, right, bottom = shot.window
    widest = {"run": 0, "a0": 0, "a1": -1, "line": 0}
    if horizontal:
        lines = [top + int(rel * shot.h) for rel in (0.25, 0.40, 0.55, 0.70)]
        span = range(left, right)
    else:
        lines = [left + int(rel * shot.w) for rel in (0.10, 0.25, 0.50, 0.75)]
        span = range(top, bottom)
    for line in lines:
        run_start = None
        for step in span:
            color = shot.px(step, line) if horizontal else shot.px(line, step)
            canvas = _is_canvas(color, theme)
            if canvas and run_start is None:
                run_start = step
            elif not canvas and run_start is not None:
                length = step - run_start
                # 只认内部间隙：起点不能是窗口的起始边。
                if run_start > (left if horizontal else top) and length > widest["run"]:
                    widest = {"run": length, "a0": run_start, "a1": step - 1, "line": line}
                run_start = None
    return widest


def _distinct_levels(shot: Shot, theme: dict, run: dict, horizontal: bool) -> tuple[int, list[str]]:
    seen = []
    for step in range(run["a0"], run["a1"] + 1):
        color = shot.px(step, run["line"]) if horizontal else shot.px(run["line"], step)
        if color not in seen:
            seen.append(color)
    return len(seen), ["#%02X%02X%02X" % color for color in seen[:8]]


def check_gap_canvas(shot: Shot, theme: dict) -> dict:
    """F2/F3：两卡之间 >=6px 连续 canvas，且间隙不是实心填充（>=3 个色阶）。

    横向按扫描行找“两侧都被非画布像素夹住”的最长画布段；纵向按扫描列做同一件
    事（当前布局里纵向只有卡片与状态栏之间的那条画布带，不是两张主卡片之间，
    报告里会注明）。间隙的定位不写死坐标。
    """
    horizontal = _widest_bounded_run(shot, theme, horizontal=True)
    h_levels, h_seen = _distinct_levels(shot, theme, horizontal, horizontal=True) if horizontal["run"] else (0, [])
    vertical = _widest_bounded_run(shot, theme, horizontal=False)
    v_levels, v_seen = _distinct_levels(shot, theme, vertical, horizontal=False) if vertical["run"] else (0, [])
    ok = horizontal["run"] >= 6 and h_levels >= 3 and vertical["run"] >= 6 and v_levels >= 3
    return {
        "metric": "F2",
        "ok": ok,
        "gap_px": horizontal["run"],
        "gap_range": [horizontal["a0"], horizontal["a1"]],
        "row": horizontal["line"],
        "distinct_levels": h_levels,
        "levels": h_seen,
        "v_gap_px": vertical["run"],
        "v_gap_range": [vertical["a0"], vertical["a1"]],
        "v_distinct_levels": v_levels,
        "v_levels": v_seen,
    }


CARD_ROIS = {
    "left-panel": (0.02, 0.30, 0.20, 0.75),
    "right-panel": (0.75, 0.30, 0.98, 0.42),
    "editor-card": (0.55, 0.105, 0.65, 0.135),
}


def run_one(image_path: str, theme_id: str, themes: dict[str, dict]) -> dict:
    theme = themes[theme_id]
    shot = Shot(image_path)
    interior = [check_card_interior(shot, theme, name, roi) for name, roi in CARD_ROIS.items()]
    border = check_border_scan(shot, theme)
    shadow = check_shadow_band(shot, theme)
    buttons = check_buttons(shot, theme)
    outer = check_outer_margin(shot, theme)
    gap = check_gap_canvas(shot, theme)
    results = interior + [border, shadow, buttons, outer, gap]
    ok = all(entry["ok"] for entry in results)
    return {
        "image": os.path.relpath(image_path, ROOT),
        "theme": theme_id,
        "dark": theme["dark"],
        "ok": ok,
        "window": list(shot.window),
        "checks": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="V1 视觉验收探针（像素量化 A-E）")
    parser.add_argument("--image", help="单张截图")
    parser.add_argument("--theme", help="单张截图对应的主题 id")
    parser.add_argument("--all", action="store_true", help="跑 evidence/ui-mosaic/visual-fix/after-<theme>.png 六套主题")
    parser.add_argument("--dir", default=HERE, help="--all 的截图目录（默认本文件所在目录）")
    parser.add_argument("--prefix", default="after-", help="--all 的截图文件名前缀（V3 用 v3-after-）")
    parser.add_argument("--json", help="把完整结果写到这个 JSON 文件")
    args = parser.parse_args()

    themes = load_themes()

    if args.image:
        if not args.theme or args.theme not in themes:
            parser.error("--image requires --theme <id>")
        reports = [run_one(args.image, args.theme, themes)]
    elif args.all:
        reports = []
        for theme_id in themes:
            path = os.path.join(args.dir, f"{args.prefix}{theme_id}.png")
            if not os.path.exists(path):
                print(f"FATAL: missing screenshot {path}", file=sys.stderr)
                return 2
            reports.append(run_one(path, theme_id, themes))
    else:
        parser.error("give --all or --image/--theme")

    failed = 0
    for report in reports:
        theme = themes[report["theme"]]
        print(f"\n== {report['theme']}  ({report['image']})  " + ("dark" if theme["dark"] else "light"))
        for check in report["checks"]:
            mark = "PASS" if check["ok"] else "FAIL"
            if not check["ok"]:
                failed += 1
            metric = check["metric"]
            if metric == "A":
                print(f"  A {mark}  {check['name']:11s} dominant={check['dominant']} surface={check['surface']} "
                      f"delta={check['distance']} share={check['fraction']:.2f}")
            elif metric == "B":
                print(f"  B {mark}  edge={check.get('edge')} vs surface={check.get('delta_surface')} "
                      f"vs canvas={check.get('delta_canvas')} expected={check.get('border_expected')} "
                      f"match_delta={check.get('border_distance')} brighter={check.get('brighter_than_surface')}")
            elif metric == "C":
                print(f"  C {mark}  band={check.get('band_px')}px deltas={check.get('deltas')}")
            elif metric == "D":
                print(f"  D {mark}  buttons found={check.get('buttons_found')} passed={check.get('buttons_passed')}")
                for entry in check.get("detail", [])[:3]:
                    print(f"          box={entry['box']} outline={entry['outline_deltas']} "
                          f"corner_gap={entry['corner_gap']}")
            elif metric == "F1":
                print(f"  F1 {mark} card-edge distance={check.get('card_distance')} "
                      f"first-non-canvas={check.get('first_non_canvas')}")
            elif metric == "F2":
                print(f"  F2 {mark} h-gap={check.get('gap_px')}px at x={check.get('gap_range')} "
                      f"levels={check.get('distinct_levels')} {check.get('levels')}")
                print(f"          v-gap={check.get('v_gap_px')}px at y={check.get('v_gap_range')} "
                      f"levels={check.get('v_distinct_levels')} {check.get('v_levels')}")
        print(f"  -> {'PASS' if report['ok'] else 'FAIL'}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"themes": reports, "ok": failed == 0}, handle, ensure_ascii=False, indent=2)
        print(f"\nJSON: {os.path.relpath(args.json, ROOT)}")

    print(f"\n{'PASS' if failed == 0 else 'FAIL'}: {failed} failing check(s) over {len(reports)} image(s)")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
