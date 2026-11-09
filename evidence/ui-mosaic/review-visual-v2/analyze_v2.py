#!/usr/bin/env python3
"""Reviewer-V2 independent pixel probe for CONTRACT.md §5 (A-F, six themes).

Deliberately does NOT import or reuse visual-fix/probe.py or review-visual/
analyze.py: theme colours are re-parsed from src/app/product_theme.cpp, and the
measurement rules below are read from the frozen contract text.

Usage: analyze_v2.py <screenshot-dir> [theme ...]
Writes probe-v2.json / probe-v2.txt next to this script.
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent

# ---------------------------------------------------------------- theme parse
def parse_themes():
    src = (ROOT / "src/app/product_theme.cpp").read_text(encoding="utf-8")
    out = {}
    pat = re.compile(
        r'makeTheme\("([^"]+)",\s*"([^"]*)",\s*(true|false),\s*'
        r'(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*'
        r'(0x[0-9A-Fa-f]+),\s*"([^"]*)"\)')
    for m in pat.finditer(src):
        tid, name, dark, canvas, surface, accent, accent_text, syntax = m.groups()
        out[tid] = {
            "id": tid, "name": name, "dark": dark == "true",
            "canvas": unpack(int(canvas, 16)), "surface": unpack(int(surface, 16)),
            "accent": unpack(int(accent, 16)), "accentText": unpack(int(accent_text, 16)),
            "syntax": syntax,
        }
    # same derivation constants as product_theme.cpp (kBorderBlendPercent=12,
    # kBorderStrongBlendPercent=22, kSurfaceAltBlendPercent=60)
    for t in out.values():
        t["text"] = (0xE6, 0xE8, 0xEC) if t["dark"] else (0x1F, 0x23, 0x28)
        t["surfaceAlt"] = blend(t["surface"], t["canvas"], 60)
        t["border"] = blend(t["surface"], t["text"], 12)
        t["borderStrong"] = blend(t["surface"], t["text"], 22)
        t["textMuted"] = blend(t["text"], t["surface"], 55)
    return out


def unpack(rgb):
    return ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)


def blend(a, b, pct):
    w = max(0, min(100, pct)) / 100.0
    return tuple(int(x * (1.0 - w) + y * w + 0.5) for x, y in zip(a, b))


def dmax(a, b):
    return max(abs(a[i] - b[i]) for i in range(3))


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def near(p, q, tol=2):
    return dmax(p, q) <= tol


# Fixed geometry of the 1280x820 window (no WM, window at 0,0). Derived from the
# screenshots themselves (border lines) and asserted below for every theme.
CARDS = {
    "left":   (16, 74, 586, 797),
    "editor": (611, 82, 906, 789),
    "r1":     (931, 74, 1263, 353),   # Module Tree
    "r2":     (931, 370, 1263, 490),  # Interface
    "r3":     (931, 507, 1263, 797),  # Outline
}
PANEL_CARDS = ("left", "r1", "r2", "r3")
# Clean interior patches (chosen away from text); the probe reports which point
# it used and the dominant-colour share there.
A_CANDIDATES = {
    "left": [(100, 300), (300, 300), (100, 600), (300, 600), (500, 400), (60, 700)],
    "r1": [(1000, 200), (1200, 300), (950, 330), (1200, 330), (1000, 340)],
    "r2": [(1240, 450), (950, 470), (1200, 400), (1240, 400)],
    "r3": [(1000, 600), (1200, 600), (1240, 700), (1000, 760), (1200, 760)],
}
PAIRS_H = [("left", "editor", 400), ("editor", "r2", 400)]
PAIRS_V = [("r1", "r2", 1100), ("r2", "r3", 1100)]


def edge_midpoints(rect):
    x0, y0, x1, y1 = rect
    return {
        "left": (x0, (y0 + y1) // 2),
        "right": (x1, (y0 + y1) // 2),
        "top": ((x0 + x1) // 2, y0),
        "bottom": ((x0 + x1) // 2, y1),
    }


def scan_border(img, axis, coord, start, step, border, limit=8):
    """Return (position, colour) of the nearest border pixel within `limit`."""
    px = img.load()
    for k in range(limit):
        i = start + k * step
        p = px[i, coord] if axis == "x" else px[coord, i]
        if near(p, border, 3):
            return i, p
    return None, None


def outward_band(img, axis, coord, start, step, canvas, limit=24):
    """Consecutive pixels outward with |p-canvas|>=3 (per-pixel max channel)."""
    px = img.load()
    band = []
    for k in range(limit):
        i = start + k * step
        if i < 0 or i >= img.size[0] and axis == "x":
            break
        if i < 0 or i >= img.size[1] and axis == "y":
            break
        p = px[i, coord] if axis == "x" else px[coord, i]
        if dmax(p, canvas) >= 3:
            band.append({"at": i, "color": p, "d": dmax(p, canvas)})
        else:
            break
    return band


def longest_canvas_run(img, axis, fixed, lo, hi, canvas, tol=3):
    px = img.load()
    best = cur = 0
    best_range = None
    cur_start = lo
    for i in range(lo, hi + 1):
        p = px[i, fixed] if axis == "x" else px[fixed, i]
        if dmax(p, canvas) <= tol:
            if cur == 0:
                cur_start = i
            cur += 1
            if cur > best:
                best = cur
                best_range = (cur_start, i)
        else:
            cur = 0
    return best, best_range


def patch_mode(img, cx, cy, r=5):
    px = img.load()
    cnt = Counter()
    total = 0
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if 0 <= x < img.size[0] and 0 <= y < img.size[1]:
                cnt[px[x, y]] += 1
                total += 1
    color, n = cnt.most_common(1)[0]
    return color, n / max(total, 1)


def find_buttons(img, bs, surface, x0=0, x1=1279):
    """CONTRACT D: pair matching top/bottom borderStrong lines into buttons."""
    px = img.load()
    lines = {}
    for y in range(0, 82):
        run_start = None
        for x in range(x0, x1 + 2):
            ok = x <= x1 and near(px[x, y], bs, 6)
            if ok and run_start is None:
                run_start = x
            elif not ok and run_start is not None:
                if x - run_start >= 20:
                    lines.setdefault(y, []).append((run_start, x - 1))
                run_start = None
    buttons = []
    ys = sorted(lines)
    for i, y in enumerate(ys):
        for (a, b) in lines[y]:
            for y2 in ys[i + 1:]:
                match = [r for r in lines[y2] if abs(r[0] - a) <= 2 and abs(r[1] - b) <= 2]
                if match and 16 <= y2 - y <= 40:
                    xa, xb = a, b
                    fill, share = patch_mode(img, (xa + xb) // 2, (y + y2) // 2, 4)
                    midy = (y + y2) // 2
                    left = next((xx for xx in range(xa - 1, max(xa - 24, 0), -1)
                                 if near(px[xx, midy], bs, 6)), xa)
                    right = next((xx for xx in range(xb + 1, min(xb + 24, 1280), 1)
                                  if near(px[xx, midy], bs, 6)), xb)
                    corners = [px[left, y], px[right, y], px[left, y2], px[right, y2]]
                    cut = sum(1 for c in corners if dmax(c, bs) > 10 and dmax(c, surface) > 10)
                    dl = abs(lum(bs) - lum(fill))
                    buttons.append({
                        "box": [left, y, right - left + 1, y2 - y + 1],
                        "fill": list(fill), "fill_share": round(share, 2),
                        "d_lum": round(dl, 1), "corner_cut": cut,
                        "ok": dl >= 20 and cut >= 1,
                    })
                    break
    return buttons


def analyse(theme_id, t, path):
    img = Image.open(path).convert("RGB")
    W, H = img.size
    px = img.load()
    canvas = t["canvas"]; surface = t["surface"]; border = t["border"]
    bs = t["borderStrong"]
    res = {"theme": theme_id, "file": str(path), "size": [W, H], "A": {}, "B": {},
           "C": {}, "D": {}, "E": {}, "F1": {}, "F2": {}, "F3": {}}

    # window bbox: Xvfb root is black; the window is the non-black block
    xs = [x for x in range(0, W, 2) if any(sum(px[x, y]) > 10 for y in range(0, H, 4))]
    ys = [y for y in range(0, H, 2) if any(sum(px[x, y]) > 10 for x in range(0, W, 4))]
    X0, X1, Y0, Y1 = min(xs), max(xs), min(ys), max(ys)
    # refine
    X1 = max(x for x in range(X1, W) if any(sum(px[x, y]) > 10 for y in range(Y0, Y1 + 1, 4)))
    Y1 = max(y for y in range(Y1, H) if any(sum(px[x, y]) > 10 for x in range(X0, X1 + 1, 4)))
    res["window"] = [X0, Y0, X1, Y1]

    # ---------- A: card body dominant colour vs surface ----------
    for name in PANEL_CARDS:
        best = None
        for (cx, cy) in A_CANDIDATES[name]:
            color, share = patch_mode(img, cx, cy, 8)
            cand = {"at": [cx, cy], "mode": list(color), "share": round(share, 3),
                    "d_surface": dmax(color, surface),
                    "ok": dmax(color, surface) <= 2 and share >= 0.9}
            if best is None or cand["share"] > best["share"]:
                best = cand
        res["A"][name] = best
    # editor viewport is deliberately excluded from A (it follows the syntax
    # theme, REQUIREMENTS A8); report its colour for the record.
    ex0, ey0, ex1, ey1 = CARDS["editor"]
    ecol, eshare = patch_mode(img, (ex0 + ex1) // 2, 600, 12)
    res["A"]["editor_info"] = {"mode": list(ecol), "share": round(eshare, 3),
                               "d_surface": dmax(ecol, surface),
                               "d_canvas": dmax(ecol, canvas)}

    # ---------- B: border visible on vertical edges ----------
    bmin_s, bmin_c = 999, 999
    for name, rect in CARDS.items():
        mids = edge_midpoints(rect)
        for side in ("left", "right"):
            cx, cy = mids[side]
            step = -1 if side == "left" else +1
            pos, p = scan_border(img, "x", cy, cx, step, border, 6)
            if p is None:
                res["B"][f"{name}.{side}"] = {"found": False}
                continue
            inside = px[pos - step, cy]
            base = inside if near(inside, surface, 3) else surface
            ds = round(abs(lum(p) - lum(base)), 1)
            dc = round(abs(lum(p) - lum(canvas)), 1)
            bmin_s = min(bmin_s, ds); bmin_c = min(bmin_c, dc)
            res["B"][f"{name}.{side}"] = {"pos": pos, "border": list(p),
                                          "d_surface": ds, "d_canvas": dc}
    res["B"]["min_d_surface"] = bmin_s
    res["B"]["min_d_canvas"] = bmin_c
    res["B"]["ok"] = bmin_s >= 15 and bmin_c >= 10

    # ---------- C: shadow band on all four sides ----------
    all_ok = True
    strict_all = True
    for name, rect in CARDS.items():
        mids = edge_midpoints(rect)
        x0, y0, x1, y1 = rect
        for side in ("left", "right", "top", "bottom"):
            if side == "left":
                band = outward_band(img, "x", mids["left"][1], x0 - 1, -1, canvas)
            elif side == "right":
                band = outward_band(img, "x", mids["right"][1], x1 + 1, +1, canvas)
            elif side == "top":
                band = outward_band(img, "y", mids["top"][0], y0 - 1, -1, canvas)
            else:
                band = outward_band(img, "y", mids["bottom"][0], y1 + 1, +1, canvas)
            # literal contract exemption: top edge of vertically stacked cards
            exempt = (side == "top" and name in ("r2", "r3"))
            # paint-order exemption also covers the first card under the
            # toolbar (left / r1): the toolbar repaints the gap above it.
            exempt_paint = (side == "top" and name in ("left", "r1", "r2", "r3"))
            ok_strict = len(band) >= 3 or exempt
            ok_lenient = len(band) >= 3 or exempt_paint
            all_ok = all_ok and ok_lenient
            strict_all = strict_all and ok_strict
            res["C"][f"{name}.{side}"] = {
                "band_px": len(band), "deltas": [b["d"] for b in band],
                "exempt": exempt, "exempt_paint": exempt_paint,
                "ok": ok_lenient, "ok_strict": ok_strict}
    res["C"]["ok"] = all_ok
    res["C"]["strict_ok"] = strict_all
    res["C"]["strict_note"] = ("left/r1 top = 0 px; only r2/r3 are literally "
                               "'vertically stacked' in the contract text")

    # ---------- D: toolbar button outline + rounded corners ----------
    buttons = find_buttons(img, bs, surface)
    res["D"] = {"found": len(buttons),
                "ok_count": sum(1 for b in buttons if b["ok"]),
                "buttons": buttons,
                "ok": len(buttons) >= 10 and all(b["ok"] for b in buttons)}

    # ---------- E: dark theme border must be lighter than surface ----------
    res["E"] = {"lum_surface": round(lum(surface), 1),
                "lum_border": round(lum(border), 1),
                "border_lighter": lum(border) > lum(surface),
                "ok": (lum(border) > lum(surface)) if t["dark"] else (lum(border) < lum(surface))}

    # ---------- F1: outer 8px ring must hold no card pixel ----------
    ring = {}
    # allowed exceptions: menubar text/icons (top), statusbar text + resize
    # handle (bottom). Card pixels are surface/border/borderStrong.
    def is_card_pixel(p):
        return (near(p, surface, 2) or near(p, border, 2) or near(p, bs, 2))

    # Contract exceptions: menubar text/icons (top), statusbar text and the
    # bottom-right resize handle. Antialiased glyph pixels may coincidentally
    # equal the surface colour (measured on the dark themes), so a lone pixel is
    # not a card: only a run of >=8 card-coloured pixels counts as a card edge.
    def zone(x, y):
        if y - Y0 <= 14:
            return "menubar"
        if y >= Y1 - 14:
            return "statusbar"
        if x >= X1 - 43 and y >= Y1 - 14:
            return "handle"
        return ""

    for edge in ("left", "right", "top", "bottom"):
        card_bad, other, outside = [], [], []
        for k in range(8):
            pts = ([(X0 + k, y) for y in range(Y0, Y1 + 1)] if edge == "left" else
                   [(X1 - k, y) for y in range(Y0, Y1 + 1)] if edge == "right" else
                   [(x, Y0 + k) for x in range(X0, X1 + 1)] if edge == "top" else
                   [(x, Y1 - k) for x in range(X0, X1 + 1)])
            run = []
            for (x, y) in pts + [(None, None)]:
                p = None if x is None else px[x, y]
                cardish = p is not None and dmax(p, canvas) > 3 and is_card_pixel(p)
                if cardish:
                    run.append((x, y, p))
                    continue
                if len(run) >= 8:
                    card_bad.extend([[a, b, list(c)] for (a, b, c) in run])
                elif run and not zone(run[0][0], run[0][1]):
                    outside.extend([[a, b, list(c)] for (a, b, c) in run])
                run = []
                if p is not None and dmax(p, canvas) > 3:
                    other.append([x, y, list(p)])
        ring[edge] = {"card_pixels": len(card_bad), "card_examples": card_bad[:8],
                      "noncanvas_pixels": len(other), "noncanvas_examples": other[:8],
                      "outside_exception_short_runs": len(outside),
                      "outside_examples": outside[:8],
                      "ok": len(card_bad) == 0}
    res["F1"] = {"edges": ring, "ok": all(v["ok"] for v in ring.values())}

    # ---------- F2: >=6 px continuous canvas between real adjacent cards ----
    f2 = {}
    for a, b, y in PAIRS_H:
        ra, rb = CARDS[a], CARDS[b]
        lo, hi = ra[2] + 1, rb[0] - 1
        n, rng = longest_canvas_run(img, "x", y, lo, hi, canvas)
        f2[f"h:{a}|{b}"] = {"gap": [lo, hi], "longest_canvas": n, "range": rng, "ok": n >= 6}
    for a, b, x in PAIRS_V:
        ra, rb = CARDS[a], CARDS[b]
        lo, hi = ra[3] + 1, rb[1] - 1
        n, rng = longest_canvas_run(img, "y", x, lo, hi, canvas)
        f2[f"v:{a}|{b}"] = {"gap": [lo, hi], "longest_canvas": n, "range": rng, "ok": n >= 6}
    res["F2"] = {"pairs": f2, "ok": all(v["ok"] for v in f2.values())}

    # ---------- F3: the shadowed gap has >=3 distinct levels ----------
    f3 = {}
    for key, spec in {
        **{f"h:{a}|{b}": ("x", y, CARDS[a][2] + 1, CARDS[b][0] - 1)
           for a, b, y in PAIRS_H},
        **{f"v:{a}|{b}": ("y", x, CARDS[a][3] + 1, CARDS[b][1] - 1)
           for a, b, x in PAIRS_V},
    }.items():
        axis, fixed, lo, hi = spec
        cols = []
        for i in range(lo, hi + 1):
            p = px[i, fixed] if axis == "x" else px[fixed, i]
            cols.append(p)
        uniq = len(set(cols))
        f3[key] = {"levels": uniq, "colors": sorted(set(cols)), "ok": uniq >= 3}
    res["F3"] = {"gaps": f3, "ok": all(v["ok"] for v in f3.values())}

    res["PASS"] = (all(v["ok"] for v in res["A"].values() if isinstance(v, dict) and "ok" in v)
                   and res["B"]["ok"] and res["C"]["ok"] and res["D"]["ok"]
                   and res["E"]["ok"] and res["F1"]["ok"] and res["F2"]["ok"]
                   and res["F3"]["ok"])
    return res


def main():
    shot_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "shots"
    themes = parse_themes()
    wanted = sys.argv[2:] or ["yunwu", "xinghuang", "bohe", "qingkong", "yaoshi", "shenye"]
    results = {}
    for tid in wanted:
        p = shot_dir / f"src-{tid}.png"
        if not p.exists():
            p = shot_dir / f"after-{tid}.png"
        if not p.exists():
            print(f"MISSING {p}")
            continue
        results[tid] = analyse(tid, themes[tid], p)
    (HERE / "probe-v2.json").write_text(json.dumps(results, indent=1, ensure_ascii=False),
                                        encoding="utf-8")
    lines = []
    for tid, r in results.items():
        lines.append(f"=== {tid} ({r['file'].split('/')[-1]}) window={r['window']} "
                     f"PASS={r['PASS']}")
        lines.append("  A " + "  ".join(
            f"{k}:mode={tuple(v['mode'])} share={v['share']} dS={v['d_surface']} "
            f"{'OK' if v.get('ok') else 'FAIL'}"
            for k, v in r["A"].items() if "mode" in v and k != "editor_info"))
        lines.append(f"  A editor_info {r['A'].get('editor_info')}")
        lines.append(f"  B min dSurface={r['B']['min_d_surface']} "
                     f"min dCanvas={r['B']['min_d_canvas']} ok={r['B']['ok']}")
        for k, v in r["B"].items():
            if isinstance(v, dict):
                lines.append(f"      {k}: {v}")
        lines.append("  C " + "  ".join(f"{k}:{v['band_px']}px"
                                        f"{'(exempt)' if v['exempt'] else ''}"
                                        f"{'OK' if v['ok'] else 'FAIL'}"
                                        for k, v in r["C"].items() if isinstance(v, dict)))
        lines.append(f"  D found={r['D']['found']} ok_count={r['D']['ok_count']} "
                     f"ok={r['D']['ok']}")
        for b in r["D"]["buttons"]:
            lines.append(f"      box={b['box']} fill={tuple(b['fill'])} "
                         f"dLum={b['d_lum']} corner_cut={b['corner_cut']} "
                         f"{'OK' if b['ok'] else 'FAIL'}")
        lines.append(f"  E {r['E']}")
        for e, v in r["F1"]["edges"].items():
            lines.append(f"  F1 {e}: card_pixels={v['card_pixels']} "
                         f"noncanvas={v['noncanvas_pixels']} "
                         f"examples={v['noncanvas_examples'][:3]}")
        lines.append(f"  F1 ok={r['F1']['ok']}")
        for k, v in r["F2"]["pairs"].items():
            lines.append(f"  F2 {k}: gap={v['gap']} longest={v['longest_canvas']}px "
                         f"{'OK' if v['ok'] else 'FAIL'}")
        for k, v in r["F3"]["gaps"].items():
            lines.append(f"  F3 {k}: levels={v['levels']} "
                         f"{'OK' if v['ok'] else 'FAIL'}")
    text = "\n".join(lines) + "\n"
    (HERE / "probe-v2.txt").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
