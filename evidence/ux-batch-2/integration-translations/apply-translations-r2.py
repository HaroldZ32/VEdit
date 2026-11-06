#!/usr/bin/env python3
"""Apply the batch-2 zh_CN translations to translations/vedit_zh_CN.ts.

R2 of apply-translations.py. The first script's regex only matched the EMPTY
form `<translation type="unfinished"></translation>`; five messages in this
catalogue carry a partially-translated body instead
(`<translation type="unfinished">界面</translation>`), so it refused with
"177 unfinished messages, 182 translations" and wrote nothing.

This version walks every <message> block, treats ANY message carrying
type="unfinished" as one of the 182 in FILE ORDER (the order lupdate emitted,
see 02-unfinished-before.txt), cross-checks ANCHORS against the English source,
replaces the translation body (partial text included) and refuses unless
exactly 182 messages were rewritten and no unfinished marker is left.
"""
import html
import importlib.util
import re
import sys

TS = "translations/vedit_zh_CN.ts"
BASE = "evidence/ux-batch-2/integration-translations/apply-translations.py"

spec = importlib.util.spec_from_file_location("base", BASE)
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
TR = base.TR
ANCHORS = base.ANCHORS

MESSAGE = re.compile(r"<message>(.*?)</message>", re.S)
SOURCE = re.compile(r"<source>(.*?)</source>", re.S)
TRANSLATION = re.compile(r"<translation([^>]*)>(.*?)</translation>", re.S)


def escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&apos;")
    )


def main() -> int:
    text = open(TS, encoding="utf-8").read()
    blocks = list(MESSAGE.finditer(text))
    unfinished = [b for b in blocks if 'type="unfinished"' in b.group(1)]
    if len(unfinished) != len(TR):
        print(f"REFUSED: {len(unfinished)} unfinished messages, {len(TR)} translations")
        return 2

    problems = []
    replacements = []
    for i, block in enumerate(unfinished, 1):
        body = block.group(1)
        src = SOURCE.search(body)
        tr = TRANSLATION.search(body)
        if src is None or tr is None:
            problems.append(f"#{i}: message without <source>/<translation>")
            continue
        # <source> is XML-escaped in the catalogue, so the plain-English anchor
        # table can only match the UNESCAPED text (anchor 70 is a <a href>).
        plain_source = html.unescape(src.group(1))
        if i in ANCHORS and ANCHORS[i] not in plain_source:
            problems.append(f"anchor {i}: expected {ANCHORS[i]!r} in {plain_source!r}")
        if not TR[i - 1].strip():
            problems.append(f"empty translation at {i} ({src.group(1)!r})")
        new_body = (
            body[: tr.start()]
            + "<translation>" + escape(TR[i - 1]) + "</translation>"
            + body[tr.end():]
        )
        replacements.append((block.start(1), block.end(1), new_body))
    if problems:
        print("REFUSED: anchor/emptiness problems")
        for problem in problems:
            print("  ", problem)
        return 3

    out = []
    pos = 0
    for start, end, new_body in replacements:
        out.append(text[pos:start])
        out.append(new_body)
        pos = end
    out.append(text[pos:])
    new = "".join(out)

    if new.count('<translation type="unfinished"') != 0:
        print("REFUSED: unfinished left over")
        return 4
    open(TS, "w", encoding="utf-8").write(new)
    print(f"applied {len(replacements)} translations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
