#!/usr/bin/env python3
"""Zero-loss proof for the i18n close-out.

Parses two .ts catalogues into {(context, source): (translation, type)} and
reports:
  * LOST     - translated before, still present in sources, but missing or empty now
  * CHANGED  - translated before, present now, but the Chinese text differs
  * ADDED    - (context, source) pairs the new catalogue has beyond the old one
  * REMOVED  - pairs the old catalogue had that the new one no longer has
A non-zero LOST/CHANGED count is a hard failure.
"""
import html
import re
import sys

CTX = re.compile(r"<context>\s*<name>(.*?)</name>(.*?)</context>", re.S)
MSG = re.compile(r"<message>(.*?)</message>", re.S)
SRC = re.compile(r"<source>(.*?)</source>", re.S)
TRN = re.compile(r"<translation([^>]*)>(.*?)</translation>", re.S)


def parse(path):
    out = {}
    text = open(path, encoding="utf-8").read()
    for cm in CTX.finditer(text):
        ctx = cm.group(1).strip()
        for bm in MSG.finditer(cm.group(2)):
            body = bm.group(1)
            s = SRC.search(body)
            t = TRN.search(body)
            if not s or not t:
                continue
            attrs, val = t.group(1), t.group(2)
            kind = "vanished" if 'type="vanished"' in attrs else (
                "unfinished" if 'type="unfinished"' in attrs else "finished")
            out[(ctx, html.unescape(s.group(1)))] = (html.unescape(val), kind)
    return out


# Entries lupdate marked type="vanished" and this close-out deliberately removed
# (each one is verified absent from src/; see 04-vanished-removed.txt). A removal
# in this list is intended, NOT a loss — anything else missing IS a loss.
ALLOWED_REMOVED = {
    ("VEdit", "Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist);;"
              "Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;Filelists (*.f *.F *.flist);;All Files (*)"),
    ("VEdit", "Verilog filelists (*.f *.F *.flist);;All files (*)"),
    ("vedit::workspace::settings::SettingsPanel", "%1 (the editor's own action)"),
}


def main():
    old = parse(sys.argv[1])
    new = parse(sys.argv[2])
    lost, changed = [], []
    for key, (val, kind) in old.items():
        if kind != "finished" or not val.strip():
            continue
        n = new.get(key)
        if n is None:
            if key not in ALLOWED_REMOVED:
                lost.append((key, val))
            continue
        elif not n[0].strip():
            lost.append((key, val))
        elif n[0] != val:
            changed.append((key, val, n[0]))
    added = sorted(k for k in new if k not in old)
    removed = sorted(k for k in old if k not in new)
    print(f"old messages: {len(old)}  new messages: {len(new)}")
    print(f"old finished+translated: {sum(1 for v,k in old.values() if k=='finished' and v.strip())}")
    print(f"new finished+translated: {sum(1 for v,k in new.values() if k=='finished' and v.strip())}")
    print(f"ADDED: {len(added)}  REMOVED: {len(removed)}  CHANGED: {len(changed)}  LOST: {len(lost)}")
    for k, v in lost:
        print(f"  LOST   ctx={k[0]!r} src={k[1]!r} was={v!r}")
    for k, a, b in changed:
        print(f"  CHANGED ctx={k[0]!r} src={k[1]!r}\n    old={a!r}\n    new={b!r}")
    for k in removed:
        print(f"  REMOVED ctx={k[0]!r} src={k[1]!r} kind={old[k][1]} val={old[k][0]!r}")
    for k in added:
        print(f"  ADDED  ctx={k[0]!r} src={k[1]!r} kind={new[k][1]}")
    return 1 if (lost or changed) else 0


if __name__ == "__main__":
    sys.exit(main())
