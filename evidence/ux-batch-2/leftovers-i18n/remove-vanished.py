#!/usr/bin/env python3
"""Remove the 3 entries lupdate marked type="vanished" in the 2026-11-07 pass.

Refuses unless the vanished set is EXACTLY these three (context, source) pairs.
"""
import html
import re
import sys

TS = "translations/vedit_zh_CN.ts"

VANISHED = {
    ("VEdit", "Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist);;"
              "Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;Filelists (*.f *.F *.flist);;All Files (*)"):
        "replaced: File->Open filters now come from src/common/file_dialog_filters.h as 4 separate entries",
    ("VEdit", "Verilog filelists (*.f *.F *.flist);;All files (*)"):
        "replaced: the import-filelist dialog filters are now 'Verilog filelists (%1)' + 'All files (*)'",
    ("vedit::workspace::settings::SettingsPanel", "%1 (the editor's own action)"):
        "replaced: foreign shortcut owners are now labelled '%1 (desktop/editor)' by the shell",
}

CONTEXT = re.compile(r"<context>\s*<name>(.*?)</name>(.*?)</context>", re.S)
MSG = re.compile(r"<message>(.*?)</message>", re.S)
SRC = re.compile(r"<source>(.*?)</source>", re.S)
TRN = re.compile(r"<translation([^>]*)>(.*?)</translation>", re.S)


def main():
    text = open(TS, encoding="utf-8").read()
    found = set()
    out, cursor = [], 0
    for cm in CONTEXT.finditer(text):
        ctx = cm.group(1).strip()
        new_body = cm.group(2)
        for bm in reversed(list(MSG.finditer(new_body))):
            blk = bm.group(1)
            s, t = SRC.search(blk), TRN.search(blk)
            if not s or not t or 'type="vanished"' not in t.group(1):
                continue
            found.add((ctx, html.unescape(s.group(1))))
            start, end = bm.start(), bm.end()
            while start > 0 and new_body[start - 1] in " \t\r\n":
                start -= 1
            new_body = new_body[:start] + new_body[end:]
        out.append(text[cursor:cm.start(2)] + new_body)
        cursor = cm.end(2)
    out.append(text[cursor:])
    if found != set(VANISHED):
        print("REFUSED: vanished set differs")
        print("  unexpected:", sorted(found - set(VANISHED)))
        print("  missing   :", sorted(set(VANISHED) - found))
        return 2
    new_text = "".join(out)
    open(TS, "w", encoding="utf-8").write(new_text)
    print(f"REMOVED {len(found)} vanished entries:")
    for k in sorted(found):
        print(f"  [{k[0]}] {k[1]!r}\n    reason: {VANISHED[k]}")
    print("vanished+unfinished left:", new_text.count('type="vanished"') + new_text.count('type="unfinished"'))
    return 0


if __name__ == "__main__":
    sys.exit(main())
