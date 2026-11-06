#!/usr/bin/env python3
"""Third pass: compare against the ACTUAL post-lupdate baseline (_parsed.json, 1207 msgs).

Baseline fields: ctx, src (XML-escaped), attr ('', ' type="unfinished"',
' type="vanished"'), tr (XML-escaped), locs.  After html.unescape the rows are
directly comparable with the ElementTree view of the finished catalogue.
"""
import html
import json
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

ROOT = Path("/home/haroldzhao/MyProj/DshProj/VEdit")
BASE = ROOT / "evidence/ux-batch-2/integration-translations/_parsed.json"
CUR = ROOT / "translations/vedit_zh_CN.ts"
REMOVED = ROOT / "evidence/ux-batch-2/u2-final/01-removed-vanished-entries.json"


def kind(row):
    a = row["attr"].strip()
    if "vanished" in a:
        return "vanished"
    if "unfinished" in a:
        return "unfinished"
    return "finished"


rows = []
for r in json.loads(BASE.read_text(encoding="utf-8")):
    rows.append({
        "ctx": r["ctx"],
        "src": html.unescape(r["src"]),
        "tr": html.unescape(r["tr"]),
        "kind": kind(r),
        "locs": r["locs"],
    })
counts = Counter(r["kind"] for r in rows)
print(f"baseline rows={len(rows)}  {dict(counts)}")

root = ET.parse(CUR).getroot()
cur = {}
for ctx in root.findall("context"):
    n = ctx.findtext("name")
    for m in ctx.findall("message"):
        s = m.findtext("source") or ""
        t = m.findtext("translation") or ""
        cur[(n, s)] = {"tr": t,
                       "type": (m.find("translation").get("type") if m.find("translation") is not None else None) or ""}
print(f"current messages={len(cur)}")

fin = [r for r in rows if r["kind"] == "finished"]
unf = [r for r in rows if r["kind"] == "unfinished"]
van = [r for r in rows if r["kind"] == "vanished"]

lost = [r for r in fin if (r["ctx"], r["src"]) not in cur]
changed = [(r, cur[(r["ctx"], r["src"])]["tr"]) for r in fin
           if (r["ctx"], r["src"]) in cur and cur[(r["ctx"], r["src"])]["tr"] != r["tr"]]
print(f"\n[1] baseline-finished={len(fin)} present={len(fin)-len(lost)} lost={len(lost)} changed={len(changed)}")
for r in lost:
    print(f"    LOST [{r['ctx']}] {r['src']!r} tr={r['tr']!r}")
for r, now in changed:
    print(f"    CHANGED [{r['ctx']}] {r['src']!r} was={r['tr']!r} now={now!r}")

missing = [r for r in unf if (r["ctx"], r["src"]) not in cur]
empty = [r for r in unf if (r["ctx"], r["src"]) in cur and cur[(r["ctx"], r["src"])]["tr"].strip() == ""]
print(f"\n[2] baseline-unfinished={len(unf)} missing={len(missing)} still-empty={len(empty)}")
for r in missing:
    print(f"    MISSING [{r['ctx']}] {r['src']!r} locs={r['locs'][:1]}")
for r in empty:
    print(f"    EMPTY [{r['ctx']}] {r['src']!r}")

rem = json.loads(REMOVED.read_text(encoding="utf-8"))
rem_keys = {(x["source"], x["prior_translation"]) for x in rem}
still = [r for r in van if (r["ctx"], r["src"]) in cur]
unrecorded = [r for r in van if (r["src"], r["tr"]) not in rem_keys]
print(f"\n[3] baseline-vanished={len(van)} still-present={len(still)} unrecorded-in-removed-json={len(unrecorded)}")
for r in unrecorded:
    print(f"    UNRECORDED [{r['ctx']}] {r['src']!r} tr={r['tr']!r}")

base_active = {(r["ctx"], r["src"]) for r in rows if r["kind"] != "vanished"}
extra = [k for k in cur if k not in base_active]
missing_active = [(r["ctx"], r["src"]) for r in rows if r["kind"] != "vanished" and (r["ctx"], r["src"]) not in cur]
print(f"\n[4] current keys outside baseline-active: {len(extra)}")
for k in extra[:20]:
    print(f"    EXTRA {k}")
print(f"[5] baseline-active keys missing from current: {len(missing_active)}")
for k in missing_active[:20]:
    print(f"    MISS-ACTIVE {k}")

ok = not lost and not changed and not missing and not empty and not still and not unrecorded and not extra and not missing_active
print(f"\nVERDICT SET-EQUALITY: {'OK' if ok else 'MISMATCH'}")
