#!/usr/bin/env python3
"""Fill the 12 unfinished zh_CN entries the 2026-11-06 lupdate added.

Matches on the XML-UNESCAPED <source> inside the VEdit context (never on file
order), replaces whatever body the entry has and drops type="unfinished".
Refuses unless every target matched exactly once and its context is VEdit.
"""
import html
import re
import sys

TS = "translations/vedit_zh_CN.ts"

TR = {
    "cannot open %1: the editor could not load the file":
        "无法打开 %1：编辑器无法加载该文件",
    "Files imported through a .f filelist; they live outside this folder tree":
        "通过 .f filelist 导入的文件；它们位于本文件夹树之外",
    "From filelist (%1)":
        "来自 filelist（%1）",
    "absolute path opened as given; files outside the workspace are allowed":
        "绝对路径按原样打开；允许位于工作区之外的文件",
    "relative path resolved under the workspace root":
        "相对路径已在工作区根目录下解析",
    "relative path, not found under the workspace root":
        "相对路径，未在工作区根目录下找到",
    "relative path and the workspace root is unknown":
        "相对路径，且工作区根目录未知",
    "the module catalog service did not answer within 300 s":
        "模块目录服务在 300 秒内没有响应",
    "Has errors":
        "有错误",
    "Warnings only":
        "仅有警告",
    "Analysed: no problems found":
        "已分析：未发现问题",
    "Not analysed yet (queued, or excluded because the buffer has unsaved changes)":
        "尚未分析（排队中，或因缓冲区有未保存的更改而被排除）",
}

CONTEXT = re.compile(r"<context>\s*<name>(.*?)</name>(.*?)</context>", re.S)
MSG = re.compile(r"<message>(.*?)</message>", re.S)
SRC = re.compile(r"<source>(.*?)</source>", re.S)
TRN = re.compile(r"<translation([^>]*)>(.*?)</translation>", re.S)


def esc(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;").replace("'", "&apos;"))


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
            src = html.unescape(s.group(1))
            if src not in TR:
                continue
            if ctx != "VEdit":
                print(f"REFUSED: {src!r} found in context {ctx!r}, expected VEdit")
                return 2
            hits.setdefault(src, 0)
            hits[src] += 1
            repl = f"<translation>{esc(TR[src])}</translation>"
            start = bm.start(1) + t.start()
            end = bm.start(1) + t.end()
            new_body = new_body[:start] + repl + new_body[end:]
        out.append(text[cursor:cm.start(2)] + new_body)
        cursor = cm.end(2)
    out.append(text[cursor:])
    missing = [s for s in TR if hits.get(s, 0) != 1]
    if missing:
        print(f"REFUSED: {len(missing)} target(s) did not match exactly once:")
        for s in missing:
            print(f"  count={hits.get(s, 0)} {s!r}")
        return 2
    new_text = "".join(out)
    open(TS, "w", encoding="utf-8").write(new_text)
    left = sum(1 for m in MSG.finditer(new_text) if 'type="unfinished"' in m.group(1))
    print(f"WROTE {len(hits)} translation(s); unfinished entries left: {left}")
    return 0 if left == 0 else 3


if __name__ == "__main__":
    sys.exit(main())
