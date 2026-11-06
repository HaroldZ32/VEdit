#!/usr/bin/env python3
"""Independent U2 catalogue comparison (reviewer artefact).

Compares the pre-work backup (00-catalogue-before.ts) with the finished
catalogue (translations/vedit_zh_CN.ts), checks the 182-new list against
02-unfinished-before.txt, and cross-checks the 21 removed vanished entries
against 01-removed-vanished-entries.json. Read-only.
"""
import html
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path("/home/haroldzhao/MyProj/DshProj/VEdit")
BEFORE = ROOT / "evidence/ux-batch-2/integration-translations/00-catalogue-before.ts"
AFTER = ROOT / "translations/vedit_zh_CN.ts"
NEWLIST = ROOT / "evidence/ux-batch-2/integration-translations/02-unfinished-before.txt"
REMOVED = ROOT / "evidence/ux-batch-2/u2-final/01-removed-vanished-entries.json"


def parse_ts(path):
    tree = ET.parse(path)
    root = tree.getroot()
    msgs = []
    for ctx in root.findall("context"):
        name = ctx.findtext("name")
        for m in ctx.findall("message"):
            src_el = m.find("source")
            tr_el = m.find("translation")
            src = src_el.text if src_el is not None and src_el.text is not None else ""
            tr = tr_el.text if tr_el is not None and tr_el.text is not None else ""
            msgs.append({
                "context": name,
                "source": src,
                "translation": tr,
                "type": m.get("type") or "",
                "tr_type": (tr_el.get("type") if tr_el is not None else None) or "",
                "locs": [(l.get("filename"), l.get("line")) for l in m.findall("location")],
            })
    return msgs


def key(m):
    return (m["context"], m["source"])


def finished(m):
    return m["type"] == "" and m["tr_type"] == "" and m["translation"].strip() != ""


def main():
    before = parse_ts(BEFORE)
    after = parse_ts(AFTER)
    out = []
    p = out.append

    def dump():
        print("\n".join(out))

    p(f"before messages={len(before)}  active={sum(1 for m in before if m['type']=='')}  "
      f"vanished={sum(1 for m in before if m['type']=='vanished')}  finished={sum(1 for m in before if finished(m))}")
    p(f"after  messages={len(after)}  active={sum(1 for m in after if m['type']=='')}  "
      f"vanished={sum(1 for m in after if m['type']=='vanished')}  finished={sum(1 for m in after if finished(m))}")

    # --- duplicates in the finished file
    seen = {}
    dups = []
    for m in after:
        k = key(m)
        if k in seen:
            dups.append(k)
        seen[k] = m
    p(f"duplicate (context,source) keys in finished file: {len(dups)}")
    for d in dups:
        p(f"  DUP {d}")

    # --- before active finished -> after
    lost, changed, still = [], [], 0
    before_active = [m for m in before if m["type"] == ""]
    for m in before_active:
        k = key(m)
        if k not in seen:
            lost.append(m)
            continue
        a = seen[k]
        if not finished(a):
            changed.append((m, a, "after-not-finished"))
        elif a["translation"] != m["translation"]:
            changed.append((m, a, "text-changed"))
        else:
            still += 1
    p(f"\nbefore-active carried into finished catalogue, same text: {still}")
    p(f"before-active missing from finished catalogue: {len(lost)}")
    for m in lost:
        p(f"  MISSING [{m['context']}] {m['source']!r} (prior tr {m['translation']!r})")
    p(f"before-active present but text changed / not finished: {len(changed)}")
    for b, a, why in changed:
        p(f"  {why}: [{b['context']}] {b['source']!r} prior={b['translation']!r} now={a['translation']!r}")

    # --- before vanished entries that are still in the file (would be a stale dup)
    p("\nbefore-vanished entries:")
    for m in before:
        if m["type"] == "vanished":
            p(f"  VAN [{m['context']}] {m['source']!r} prior={m['translation']!r} "
              f"still-in-after={key(m) in seen}")

    # --- the 182-new list
    raw = NEWLIST.read_text(encoding="utf-8").splitlines()
    new_entries = []
    i = 0
    while i < len(raw):
        line = raw[i].rstrip()
        mline = re.match(r"^\s*(\d+)\s+(\S+?):(\d+)\s+\[([^\]]*)\]\s*$", line)
        if mline:
            idx, fname, lineno, ctx = mline.groups()
            src_line = raw[i + 1].strip() if i + 1 < len(raw) else ""
            src = html.unescape(src_line[1:-1] if src_line.startswith("'") and src_line.endswith("'") else src_line)
            new_entries.append({"n": int(idx), "file": fname, "line": int(lineno), "context": ctx, "source": src})
            i += 2
        else:
            i += 1
    p(f"\n182-list parsed: {len(new_entries)} entries; numbering contiguous: "
      f"{[e['n'] for e in new_entries] == list(range(1, len(new_entries) + 1))}")

    after_by_key = {}
    for m in after:
        after_by_key.setdefault(key(m), []).append(m)
    missing_new, empty_new, not_finished_new, english_copy = [], [], [], []
    for e in new_entries:
        k = (e["context"], e["source"])
        if k not in after_by_key:
            missing_new.append(e)
            continue
        a = after_by_key[k][0]
        if a["translation"].strip() == "":
            empty_new.append(e)
        elif not finished(a):
            not_finished_new.append((e, a))
        elif a["translation"].strip() == e["source"].strip():
            english_copy.append(e)
    p(f"182-list present in finished catalogue: {len(new_entries) - len(missing_new)}/{len(new_entries)}")
    p(f"  missing: {len(missing_new)}")
    for e in missing_new:
        p(f"    MISSING {e['n']} [{e['context']}] {e['source']!r} ({e['file']}:{e['line']})")
    p(f"  empty translation: {len(empty_new)}; not-finished: {len(not_finished_new)}; identical-to-source: {len(english_copy)}")
    for e in empty_new:
        p(f"    EMPTY {e['n']} [{e['context']}] {e['source']!r}")
    for e in english_copy:
        p(f"    SAME-AS-SOURCE {e['n']} [{e['context']}] {e['source']!r}")
    for e, a in not_finished_new:
        p(f"    NOT-FINISHED {e['n']} [{e['context']}] {e['source']!r} type={a['tr_type']!r}")

    # --- new entries relative to the before backup
    before_keys = {key(m) for m in before if m["type"] == ""}
    after_new = [m for m in after if key(m) not in before_keys]
    p(f"\nkeys in finished catalogue that were not active in the backup: {len(after_new)}")
    newlist_keys = {(e["context"], e["source"]) for e in new_entries}
    extra = [m for m in after_new if key(m) not in newlist_keys]
    p(f"  of those, NOT in the 182-list: {len(extra)}")
    for m in extra[:40]:
        p(f"    EXTRA [{m['context']}] {m['source']!r} -> {m['translation']!r}")

    # --- removed-vanished JSON cross-check
    removed = json.loads(REMOVED.read_text(encoding="utf-8"))
    p(f"\nremoved-json entries: {len(removed)}")
    lost_keys = {key(m) for m in lost}
    vanished_keys = {key(m) for m in before if m["type"] == "vanished"}
    stated = {(r.get("source")) for r in removed}
    p(f"  lost-from-before sources == stated removed sources: "
      f"{sorted(m['source'] for m in lost) == sorted(stated)}")
    p(f"  before-vanished sources subset of stated: {all(m['source'] in stated for m in before if m['type']=='vanished')}")

    # --- vocabulary sanity across the whole finished file
    bad = []
    for m in after:
        t = m["translation"]
        if not finished(m):
            bad.append(("not-finished", key(m), t))
        elif t.strip() in {"TODO", "FIXME", "???", "…", "..."}:
            bad.append(("placeholder", key(m), t))
        elif "TODO" in t or "FIXME" in t or "Translation" in t or "待翻译" in t:
            bad.append(("garbage", key(m), t))
    p(f"\nwhole-file placeholder/garbage/not-finished scan: {len(bad)}")
    for b in bad:
        p(f"  {b}")

    # --- no CJK in a translation whose source is a full English sentence (heuristic)
    no_cjk = []
    for m in after:
        t = m["translation"]
        if not finished(m):
            continue
        if re.search(r"[A-Za-z]{3,}", m["source"]) and not re.search(r"[\u4e00-\u9fff]", t):
            no_cjk.append(m)
    p(f"\nfinished translations with English source but NO CJK chars: {len(no_cjk)}")
    for m in no_cjk[:40]:
        p(f"  NO-CJK [{m['context']}] {m['source']!r} -> {m['translation']!r}")

    dump()
    return 0


if __name__ == "__main__":
    sys.exit(main())
