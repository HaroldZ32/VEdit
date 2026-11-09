#!/usr/bin/env python3
"""Fill the 15 unfinished zh_CN entries the 2026-11-07 extraction added.

Keyed on (context, XML-unescaped <source>) — never on file order. Refuses
unless every target matches exactly once.

File-dialog filter strings keep ASCII parentheses: both Qt's QFileDialog and
file_dialog_filters.h::filterGlobs() parse the globs out of the LAST
parentheses, so a full-width （） would break the filter.
"""
import html
import re
import sys

TS = "translations/vedit_zh_CN.ts"

TR = {
    # --- the focused-pane badge (this task) ---
    ("VEdit", "active"): "活动",
    # --- File -> Open / import-filelist filters (file_dialog_filters.h) ---
    ("VEdit", "Verilog/SystemVerilog/filelist (%1 *.v *.sv *.svh *.vh)"):
        "Verilog/SystemVerilog/filelist (%1 *.v *.sv *.svh *.vh)",
    ("VEdit", "Verilog/SystemVerilog (*.v *.sv *.svh *.vh)"):
        "Verilog/SystemVerilog (*.v *.sv *.svh *.vh)",
    ("VEdit", "All Files (*)"): "所有文件 (*)",
    ("VEdit", "Verilog filelists (%1)"): "Verilog filelist (%1)",
    ("VEdit", "All files (*)"): "所有文件 (*)",
    # --- RM-17 top-scope disclosure (module tree / diagnostics) ---
    ("VEdit", "%1 file(s) outside the top %2 are not analysed"):
        "%1 个文件位于顶层 %2 之外，未被分析",
    ("VEdit", " · top: %1"): " · 顶层：%1",
    ("VEdit", "Top:"): "顶层：",
    ("VEdit", "Auto"): "自动",
    ("VEdit", "Top module for the module tree and the lint scope (empty = Auto)"):
        "模块树与 lint 范围使用的顶层模块（留空 = 自动）",
    # --- shortcut-owner labels / tab commands (ApplicationShell) ---
    ("vedit::app::ApplicationShell", "desktop: %1"): "桌面：%1",
    ("vedit::app::ApplicationShell", "editor: %1"): "编辑器：%1",
    ("vedit::app::ApplicationShell", "&Next Tab"): "下一个标签页(&N)",
    ("vedit::app::ApplicationShell", "Pre&vious Tab"): "上一个标签页(&P)",
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
    hits = {}
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
            if not s or not t or 'type="unfinished"' not in t.group(1):
                continue
            key = (ctx, html.unescape(s.group(1)))
            if key not in TR:
                continue
            hits[key] = hits.get(key, 0) + 1
            ts_at = bm.start(1) + t.start()
            te_at = bm.start(1) + t.end()
            new_body = new_body[:ts_at] + f"<translation>{esc(TR[key])}</translation>" + new_body[te_at:]
        out.append(text[cursor:cm.start(2)] + new_body)
        cursor = cm.end(2)
    out.append(text[cursor:])

    missing = [k for k in TR if hits.get(k, 0) != 1]
    if missing:
        print(f"REFUSED: {len(missing)} target(s) did not match exactly once:")
        for k in missing:
            print(f"  count={hits.get(k, 0)} {k!r}")
        return 2
    new_text = "".join(out)
    open(TS, "w", encoding="utf-8").write(new_text)
    left = new_text.count('type="unfinished"')
    print(f"WROTE {len(hits)} translation(s); unfinished entries left: {left}")
    for (ctx, src), zh in TR.items():
        print(f"  [{ctx}] {src!r} -> {zh!r}")
    return 0 if left == 0 else 3


if __name__ == "__main__":
    sys.exit(main())
