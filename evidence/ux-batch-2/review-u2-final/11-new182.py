#!/usr/bin/env python3
"""Second pass: 182-new placeholder parity, escaping normalisation, terminology rows."""
import html
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path("/home/haroldzhao/MyProj/DshProj/VEdit")
AFTER = ROOT / "translations/vedit_zh_CN.ts"
NEWLIST = ROOT / "evidence/ux-batch-2/integration-translations/02-unfinished-before.txt"

tree = ET.parse(AFTER)
root = tree.getroot()
after = []
for ctx in root.findall("context"):
    name = ctx.findtext("name")
    for m in ctx.findall("message"):
        s = m.find("source")
        t = m.find("translation")
        after.append({
            "context": name,
            "source": s.text or "" if s is not None else "",
            "translation": t.text or "" if t is not None else "",
            "tr_type": (t.get("type") if t is not None else None) or "",
        })
by_key = {(m["context"], html.unescape(m["source"])): m for m in after}
by_key_nl = {(m["context"], m["source"].replace("\\n", "\n")): m for m in after}

raw = NEWLIST.read_text(encoding="utf-8").splitlines()
entries = []
i = 0
while i < len(raw):
    ml = re.match(r"^\s*(\d+)\s+(\S+?):(\d+)\s+\[([^\]]*)\]\s*$", raw[i].rstrip())
    if ml:
        idx, fname, lineno, ctx = ml.groups()
        line = raw[i + 1].strip() if i + 1 < len(raw) else ""
        src = line[1:-1] if line.startswith("'") and line.endswith("'") else line
        src = html.unescape(src)
        entries.append({"n": int(idx), "file": fname, "line": int(lineno), "context": ctx, "source": src})
        i += 2
    else:
        i += 1

print(f"entries={len(entries)}")
missing = []
found = []
for e in entries:
    k = (e["context"], e["source"])
    m = by_key.get(k) or by_key_nl.get(k) or by_key.get((e["context"], e["source"].replace("\\n", "\n")))
    if m is None:
        missing.append(e)
    else:
        found.append((e, m))
print(f"found={len(found)} missing={len(missing)}")
for e in missing:
    print(f"  MISSING {e['n']} [{e['context']}] {e['source']!r}")

# placeholder parity
ph_bad = []
for e, m in found:
    sp = re.findall(r"%\d+", e["source"])
    tp = re.findall(r"%\d+", m["translation"])
    if sorted(sp) != sorted(tp):
        ph_bad.append((e["n"], e["source"], m["translation"], sp, tp))
print(f"\nplaceholder-set mismatches among the 182: {len(ph_bad)}")
for n, s, t, sp, tp in ph_bad:
    print(f"  #{n} src={s!r} tr={t!r} src_ph={sp} tr_ph={tp}")

# accelerator (&) parity for the 182
amp_bad = []
for e, m in found:
    if ("&" in e["source"]) != ("&" in m["translation"]) and "&&" not in e["source"]:
        amp_bad.append((e["n"], e["source"], m["translation"]))
print(f"accelerator '&' presence mismatches among the 182: {len(amp_bad)}")
for n, s, t in amp_bad:
    print(f"  #{n} src={s!r} tr={t!r}")

# dump all 182 for manual spot check
print("\n--- all 182 (n | source | translation) ---")
for e, m in found:
    print(f"{e['n']:3d} | {e['source']} | {m['translation']}")

# terminology rows
print("\n--- terminology rows ---")
terms = ["替换", "全部替换", "修复", "名称", "方向", "类型", "位宽", "模块", "例化次数", "定义位置", "接口", "界面"]
for t in terms:
    hits = [m for m in after if t in (m["translation"] or "")]
    print(f"{t!r}: {len(hits)} translation(s); samples: "
          + "; ".join(f"[{m['context']}] {m['source']!r}->{m['translation']!r}" for m in hits[:3]))

print("\n--- key single rows ---")
for k in [("VEdit", "Interface"), ("VEdit", "Replace"), ("VEdit", "Replace All"),
          ("VEdit", "Module"), ("VEdit", "Instantiations"), ("VEdit", "Definition"),
          ("VEdit", "Name"), ("VEdit", "Direction"), ("VEdit", "Type"), ("VEdit", "Width"),
          ("VEdit", "Fix"), ("VEdit", "修复"),
          ("VEdit", "Save As failed: %1"),
          ("VEdit", "Save As refused (headless): %1 exists and cannot be overwritten without confirmation"),
          ("vedit::app::ApplicationShell", "Save As failed: %1")]:
    m = by_key.get(k)
    print(f"  {k} -> {None if m is None else m['translation']!r}")
