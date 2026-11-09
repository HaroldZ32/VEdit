#!/usr/bin/env python3
"""Reviewer-owned pixel probe for the V1/V3 visual fix (independent of probe.py).

Frozen criteria:
  A  card interior == surface (+-2)
  B  card edge border: dSurface >= 15 AND dCanvas >= 10
  C  outside a card edge: >= 4 px band with dCanvas >= 3 (or double border >= 8)
  D  >= 3 toolbar buttons, outline/fill luminance delta >= 20, still rounded
  E  six themes pass; dark: border LIGHTER than surface
  F1 window outer 8 px ring is ALL canvas (d <= 3) -> first non-canvas >= 8 px
  F2 between two ADJACENT CARDS: >= 6 px continuous canvas (d <= 3), H and V
  F3 across an inter-card gap: >= 3 distinct levels (not a solid fill)

Theme tokens are parsed independently out of src/app/product_theme.cpp; the
border/borderStrong tokens are re-derived with the same blend formula.
"""
import json
import os
import re
import sys
from collections import Counter

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SHOTS = os.path.join(ROOT, "evidence/ui-mosaic/review-visual/shots")


def blend(a, b, pct):
    w = pct / 100.0
    return tuple(round(a[i] * (1 - w) + b[i] * w) for i in range(3))


def parse_themes():
    src = open(os.path.join(ROOT, "src/app/product_theme.cpp"), encoding="utf-8").read()
    pat = re.compile(
        r'makeTheme\("(\w+)",\s*"[^"]*",\s*(true|false),\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6}),')
    out = {}
    for m in pat.finditer(src):
        tid, dark, canvas, surface = m.group(1), m.group(2) == "true", m.group(3), m.group(4)
        rgb = lambda h: (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
        text = (0xE6, 0xE8, 0xEC) if dark else (0x1F, 0x23, 0x28)
        out[tid] = {"dark": dark, "canvas": rgb(canvas), "surface": rgb(surface), "text": text,
                    "border": blend(rgb(surface), text, 12),
                    "borderStrong": blend(rgb(surface), text, 22)}
    return out


def dmax(a, b):
    return max(abs(a[i] - b[i]) for i in range(3))


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


class Shot:
    def __init__(self, path):
        self.im = Image.open(path).convert("RGB")
        self.px = self.im.load()
        self.W, self.H = self.im.size
        self.x0, self.y0, self.x1, self.y1 = self.window_bbox()

    def window_bbox(self):
        xs = [x for x in range(self.W) if any(max(self.px[x, y]) > 8 for y in range(0, self.H, 3))]
        ys = [y for y in range(self.H) if any(max(self.px[x, y]) > 8 for x in range(0, self.W, 3))]
        return (min(xs), min(ys), max(xs), max(ys)) if xs and ys else (0, 0, self.W - 1, self.H - 1)

    def patch_mode(self, cx, cy, r=12):
        c = Counter(self.px[x, y] for y in range(cy - r, cy + r) for x in range(cx - r, cx + r))
        col, n = c.most_common(1)[0]
        return col, n / (4 * r * r)


def analyse(theme_id, t, path):
    sh = Shot(path)
    X0, Y0, X1, Y1 = sh.x0, sh.y0, sh.x1, sh.y1
    canvas, surface, border, bs = t["canvas"], t["surface"], t["border"], t["borderStrong"]

    def is_border(p):
        return dmax(p, border) <= 3

    def is_surface(p):
        return dmax(p, surface) <= 4

    def is_canvas(p):
        return dmax(p, canvas) <= 3

    res = {"theme": theme_id, "dark": t["dark"], "window": [X0, Y0, X1, Y1],
           "tokens": {"canvas": canvas, "surface": surface, "border": border, "borderStrong": bs},
           "A": [], "B": [], "C": [], "D": {}, "E": {}, "F1": {}, "F2": {}, "F3": {}}

    # ---------- A: three card interiors ----------
    samples = {"left-card": (100, 300), "editor-card-chrome": (750, 95), "right-card": (1100, 300)}
    for name, (cx, cy) in samples.items():
        col, share = sh.patch_mode(cx, cy)
        res["A"].append({"where": name, "color": col, "delta": dmax(col, surface),
                         "share": round(share, 3), "ok": dmax(col, surface) <= 2 and share >= 0.8})
    viewport, vshare = sh.patch_mode(700, 300)
    res["A_viewport"] = {"color": viewport, "share": round(vshare, 3),
                         "note": "editor text viewport, follows the syntax theme by design"}

    # ---------- B / C: find a card edge from the inside, then walk outward ----------
    def find_edge(axis, coord, start, step, limit=600):
        for k in range(limit):
            i = start + step * k
            if (axis == "x" and not (0 <= i <= X1)) or (axis == "y" and not (0 <= i <= Y1)):
                return None, None
            p = sh.px[i, coord] if axis == "x" else sh.px[coord, i]
            if is_border(p):
                return i, p
        return None, None

    def band(axis, coord, pos, step, limit=24):
        out = []
        for k in range(1, limit + 1):
            i = pos + step * k
            if (axis == "x" and not (0 <= i <= X1)) or (axis == "y" and not (0 <= i <= Y1)):
                break
            p = sh.px[i, coord] if axis == "x" else sh.px[coord, i]
            if dmax(p, canvas) >= 3:
                out.append({"d": dmax(p, canvas), "color": p})
            elif out:
                break
            elif k > 8:
                break
        return out

    for name, axis, coord, start, step in [
        ("left-card left", "x", 300, 100, -1),
        ("left-card bottom", "y", 300, 500, +1),
        ("left-card top", "y", 300, 300, -1),
        ("editor-card top", "y", 700, 300, -1),
        ("editor-card bottom", "y", 700, 500, +1),
        ("right-card right", "x", 300, 1100, +1),
        ("right-card bottom", "y", 1100, 700, +1),
    ]:
        pos, p = find_edge(axis, coord, start, step)
        if pos is None:
            res["C"].append({"where": name, "found": False, "ok": False})
            continue
        b = band(axis, coord, pos, step)
        res["B"].append({"where": name, "pos": pos, "color": p, "d_surface": dmax(p, surface),
                         "d_canvas": dmax(p, canvas), "d_derived": dmax(p, border),
                         "ok": dmax(p, surface) >= 15 and dmax(p, canvas) >= 10 and dmax(p, border) <= 8})
        res["C"].append({"where": name, "band_px": len(b), "deltas": [x["d"] for x in b],
                         "ok": len(b) >= 4})

    # ---------- D: toolbar buttons = matching top/bottom 1px borderStrong lines ----------
    lines = {}
    for y in range(Y0, min(Y0 + 80, Y1 + 1)):
        run_start = None
        for x in range(X0, X1 + 2):
            ok = x <= X1 and dmax(sh.px[x, y], bs) <= 6
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
                    x0, x1 = a, b
                    fill, share = sh.patch_mode((x0 + x1) // 2, (y + y2) // 2, r=4)
                    dl = abs(lum(bs) - lum(fill))
                    midy = (y + y2) // 2
                    left = next((xx for xx in range(x0 - 1, max(x0 - 24, X0), -1)
                                 if dmax(sh.px[xx, midy], bs) <= 6), x0)
                    right = next((xx for xx in range(x1 + 1, min(x1 + 24, X1), +1)
                                  if dmax(sh.px[xx, midy], bs) <= 6), x1)
                    corners = [sh.px[left, y], sh.px[right, y], sh.px[left, y2], sh.px[right, y2]]
                    cut = sum(1 for c in corners if dmax(c, bs) > 10 and dmax(c, surface) > 10)
                    buttons.append({"box": [x0, y, x1 - x0 + 1, y2 - y + 1], "fill": fill,
                                    "fill_share": round(share, 2), "d_lum": round(dl, 1),
                                    "corner_cut": cut, "ok": dl >= 20 and cut >= 2})
                    break
    res["D"] = {"found": len(buttons), "ok_count": sum(1 for b in buttons if b["ok"]),
                "buttons": buttons[:6], "ok": sum(1 for b in buttons if b["ok"]) >= 3}

    # ---------- E ----------
    res["E"] = {"border_lighter": lum(border) > lum(surface),
                "lum_surface": round(lum(surface), 1), "lum_border": round(lum(border), 1),
                "ok": (lum(border) > lum(surface)) if t["dark"] else (lum(border) < lum(surface))}

    # ---------- F1 ----------
    ring = {}
    for edge in ("left", "right", "top", "bottom"):
        bad = []
        for k in range(8):
            pts = ([(X0 + k, y) for y in range(Y0, Y1 + 1)] if edge == "left" else
                   [(X1 - k, y) for y in range(Y0, Y1 + 1)] if edge == "right" else
                   [(x, Y0 + k) for x in range(X0, X1 + 1)] if edge == "top" else
                   [(x, Y1 - k) for x in range(X0, X1 + 1)])
            for (x, y) in pts:
                if not is_canvas(sh.px[x, y]):
                    bad.append((k, x, y, sh.px[x, y]))
        cardish = []
        for k in range(40):
            pts = ([(X0 + k, y) for y in range(Y0, Y1 + 1)] if edge == "left" else
                   [(X1 - k, y) for y in range(Y0, Y1 + 1)] if edge == "right" else
                   [(x, Y0 + k) for x in range(X0, X1 + 1)] if edge == "top" else
                   [(x, Y1 - k) for x in range(X0, X1 + 1)])
            hit = [1 for (x, y) in pts
                   if dmax(sh.px[x, y], surface) <= 4 or dmax(sh.px[x, y], border) <= 3
                   or dmax(sh.px[x, y], bs) <= 6]
            if hit:
                cardish.append(k)
        ring[edge] = {"bad_in_8px": len(bad), "first_noncanvas_dist": min((b[0] for b in bad), default=None),
                      "first_card_dist": min(cardish, default=None),
                      "ok": not bad, "examples": bad[:2]}
    res["F1"] = {"edges": ring, "ok": all(v["ok"] for v in ring.values())}

    # ---------- F2/F3: consecutive border pairs along a line ----------
    def analyse_line(axis, fixed, lo, hi):
        borders = []
        for i in range(lo, hi + 1):
            p = sh.px[i, fixed] if axis == "x" else sh.px[fixed, i]
            if is_border(p):
                if not borders or i - borders[-1] > 1:
                    borders.append(i)
                else:
                    borders[-1] = i
        spans = []
        for a, b in zip(borders, borders[1:]):
            pix = [sh.px[i, fixed] if axis == "x" else sh.px[fixed, i] for i in range(a + 1, b)]
            if not pix:
                continue
            n_surface = sum(1 for p in pix if is_surface(p))
            n_canvasish = sum(1 for p in pix if dmax(p, canvas) <= 12)
            kind = ("card-interior" if n_surface > len(pix) * 0.6
                    else "gap" if n_canvasish > len(pix) * 0.6 else "unknown")
            longest, cur = 0, 0
            for p in pix:
                cur = cur + 1 if is_canvas(p) else 0
                longest = max(longest, cur)
            spans.append({"span": [a, b], "len": b - a - 1, "kind": kind, "canvas_run": longest,
                          "distinct": len(set(pix))})
        return spans

    f2, f3 = {}, {}
    for label, axis, fixed, lo, hi in [
        ("h-300", "x", 300, X0 + 1, X1 - 1),
        ("h-600", "x", 600, X0 + 1, X1 - 1),
        ("v-right", "y", 1100, Y0 + 40, Y1 - 45),
        ("v-editor", "y", 700, Y0 + 40, Y1 - 45),
    ]:
        spans = analyse_line(axis, fixed, lo, hi)
        gaps = [s for s in spans if s["kind"] == "gap"]
        f2[label] = {"gaps": [{"span": g["span"], "len": g["len"], "canvas_run": g["canvas_run"]} for g in gaps],
                     "longest_canvas_run": max((g["canvas_run"] for g in gaps), default=0),
                     "ok": bool(gaps) and max(g["canvas_run"] for g in gaps) >= 6}
        f3[label] = {"gaps": [{"span": g["span"], "distinct": g["distinct"]} for g in gaps],
                     "ok": bool(gaps) and all(g["distinct"] >= 3 for g in gaps)}
    res["F2"], res["F3"] = f2, f3

    res["PASS_A_E"] = (all(a["ok"] for a in res["A"]) and all(b["ok"] for b in res["B"])
                       and all(c["ok"] for c in res["C"]) and res["D"]["ok"] and res["E"]["ok"])
    res["PASS"] = (res["PASS_A_E"] and res["F1"]["ok"]
                   and all(v["ok"] for v in res["F2"].values())
                   and all(v["ok"] for v in res["F3"].values()))
    return res


def main():
    themes = parse_themes()
    ids = sys.argv[1:] or ["yunwu", "xinghuang", "bohe", "qingkong", "yaoshi", "shenye"]
    out = {}
    for tid in ids:
        path = os.path.join(SHOTS, f"v3-{tid}.png")
        if not os.path.exists(path):
            print(f"MISSING {path}")
            continue
        r = analyse(tid, themes[tid], path)
        out[tid] = r
        print(f"\n===== {tid} (dark={r['dark']}) window={r['window']} A-E={r['PASS_A_E']} ALL={r['PASS']} =====")
        for a in r["A"]:
            print(f"  A {a['where']:20s} {a['color']} d={a['delta']} share={a['share']} {'OK' if a['ok'] else 'FAIL'}")
        print(f"  A viewport(editor text): {r['A_viewport']['color']} (by design)")
        for b in r["B"]:
            print(f"  B {b['where']:18s} @{b['pos']} {b['color']} dSurf={b['d_surface']} dCanvas={b['d_canvas']} "
                  f"dDerived={b['d_derived']} {'OK' if b['ok'] else 'FAIL'}")
        for c in r["C"]:
            print(f"  C {str(c.get('where')):18s} band={c.get('band_px')}px deltas={c.get('deltas')} {'OK' if c['ok'] else 'FAIL'}")
        d = r["D"]
        print(f"  D buttons found={d['found']} ok={d['ok_count']} {'OK' if d['ok'] else 'FAIL'}")
        for b in d["buttons"]:
            print(f"      box={b['box']} fill={b['fill']} dLum={b['d_lum']} corner={b['corner_cut']} {'OK' if b['ok'] else 'FAIL'}")
        print(f"  E border_lighter={r['E']['border_lighter']} L(surf)={r['E']['lum_surface']} L(bord)={r['E']['lum_border']} "
              f"{'OK' if r['E']['ok'] else 'FAIL'}")
        for e, v in r["F1"]["edges"].items():
            print(f"  F1 {e:6s} bad_in_8px={v['bad_in_8px']} first_noncanvas={v['first_noncanvas_dist']} "
                  f"first_card={v.get('first_card_dist')} {'OK' if v['ok'] else 'FAIL'} ex={v['examples'][:1]}")
        for k, v in r["F2"].items():
            print(f"  F2 {k:9s} longest_canvas={v['longest_canvas_run']}px {'OK' if v['ok'] else 'FAIL'} gaps={v['gaps']}")
        for k, v in r["F3"].items():
            print(f"  F3 {k:9s} {'OK' if v['ok'] else 'FAIL'} gaps={v['gaps']}")
    with open(os.path.join(ROOT, "evidence/ui-mosaic/review-visual/probe-review.json"), "w") as f:
        json.dump(out, f, indent=1)
    bad = [t for t, r in out.items() if not r["PASS"]]
    print(f"\nSUMMARY: {len(out)} images; failing themes: {bad or 'none'}")


if __name__ == "__main__":
    main()
