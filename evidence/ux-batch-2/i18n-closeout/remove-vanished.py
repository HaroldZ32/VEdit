#!/usr/bin/env python3
"""Remove the 6 entries lupdate marked type="vanished" (verbatim gone from src)
and align two "文件清单" renderings to "filelist" (the term the user approved in
REAL-MACHINE-FINDINGS.md: "来自 filelist" 分组).

Refuses unless the vanished set is EXACTLY the expected six (context, source)
pairs, so a future lupdate that publishes a different set cannot silently
delete live translations.
"""
import html
import re
import sys

TS = "translations/vedit_zh_CN.ts"

# (context, source) -> one-line reason. All confirmed absent from src/.
VANISHED = {
    ("VEdit", "KTextEditor::Document::setText() failed for %1"):
        "replaced: document_io.cpp now uses 'KTextEditor::Document::setText() failed' (no %1)",
    ("VEdit", "No variables are used by this project yet"):
        "dead: the empty-table copy was replaced by 'Project variables unchanged'",
    ("VEdit", "Project variables updated; re-expanding"):
        "replaced: project_panel.cpp now says 'Project variables unchanged' / 'Saved %1 variable(s); re-expanding'",
    ("VEdit", "the module catalog service did not answer within 30 s"):
        "replaced: the timeout is 300 s (catalog_client.cpp:436)",
    ("vedit::workspace::settings::SettingsPanel", "Shortcut"):
        "dead: the shortcuts page now uses the 'Command/Effective shortcut/Default/State' headers",
    ("vedit::workspace::settings::SettingsPanel", "Clear"):
        "replaced: the shortcut buttons are 'Reset to Default' / 'Reset All to Defaults'",
}

ALIGN = {
    "From filelist (%1)": ("来自文件清单（%1）", "来自 filelist（%1）"),
    "Files imported through a .f filelist; they live outside this folder tree":
        ("通过 .f 文件清单导入的文件；它们位于此目录树之外",
         "通过 .f filelist 导入的文件；它们位于此目录树之外"),
}

CONTEXT = re.compile(r"<context>\s*<name>(.*?)</name>(.*?)</context>", re.S)
MSG = re.compile(r"<message>(.*?)</message>", re.S)
SRC = re.compile(r"<source>(.*?)</source>", re.S)
TRN = re.compile(r"<translation([^>]*)>(.*?)</translation>", re.S)


def esc(t):
    return (t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;").replace("'", "&apos;"))


def main():
    text = open(TS, encoding="utf-8").read()
    found = set()
    aligned = []
    out = []
    cursor = 0
    for cm in CONTEXT.finditer(text):
        ctx = cm.group(1).strip()
        body = cm.group(2)
        new_body = body
        for bm in reversed(list(MSG.finditer(body))):
            blk = bm.group(1)
            s = SRC.search(blk)
            t = TRN.search(blk)
            if not s or not t:
                continue
            src = html.unescape(s.group(1))
            if 'type="vanished"' in t.group(1):
                found.add((ctx, src))
                # delete the block plus the whitespace that precedes it
                start, end = bm.start(), bm.end()
                while start > 0 and new_body[start - 1] in " \t\r\n":
                    start -= 1
                new_body = new_body[:start] + new_body[end:]
                continue
            if src in ALIGN and html.unescape(t.group(2)) != ALIGN[src][1]:
                old, new = ALIGN[src]
                if html.unescape(t.group(2)) != old:
                    print(f"REFUSED: unexpected body for {src!r}: {t.group(2)!r}")
                    return 2
                ts_at = bm.start(1) + t.start(2)
                te_at = bm.start(1) + t.end(2)
                new_body = new_body[:ts_at] + esc(new) + new_body[te_at:]
                aligned.append((ctx, src, old, new))
        out.append(text[cursor:cm.start(2)] + new_body)
        cursor = cm.end(2)
    out.append(text[cursor:])

    if found != set(VANISHED):
        print("REFUSED: vanished set differs from the expected six")
        print("  unexpected:", sorted(found - set(VANISHED)))
        print("  missing   :", sorted(set(VANISHED) - found))
        return 2
    if len(aligned) != len(ALIGN):
        print(f"REFUSED: aligned {len(aligned)} of {len(ALIGN)} entries")
        return 2
    new_text = "".join(out)
    open(TS, "w", encoding="utf-8").write(new_text)
    print(f"REMOVED {len(found)} vanished entries:")
    for ctx, src in sorted(found):
        print(f"  [{ctx}] {src!r}\n    reason: {VANISHED[(ctx, src)]}")
    print(f"ALIGNED {len(aligned)} term(s):")
    for ctx, src, old, new in aligned:
        print(f"  [{ctx}] {src!r}: {old!r} -> {new!r}")
    left = new_text.count('type="vanished"') + new_text.count('type="unfinished"')
    print(f"vanished+unfinished left: {left}")
    return 0 if left == 0 else 3


if __name__ == "__main__":
    sys.exit(main())
