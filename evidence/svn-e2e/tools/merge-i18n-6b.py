#!/usr/bin/env python3
"""Merge a JSON translation table into translations/vedit_zh_CN.ts.

Run AFTER lupdate added the new messages (they arrive with
`type="unfinished"` and an empty <translation>):

    python3 evidence/vcs-git-workspace/tools/merge-i18n-5b.py \\
        evidence/vcs-git-workspace/tools/translations-5b.json

The table maps a CONTEXT (e.g. "VEdit") to {source: chinese}. Existing
translations are never overwritten; only empty or `unfinished` ones are filled.
Each <context> block is edited with a regular expression, so the rest of the file
keeps its original formatting byte for byte (an XML re-serialisation would
rewrite every line of every other context). Prints the summary the previous
batches recorded: `filled=N still-unfinished=K`.
"""
import html
import json
import re
import sys

TS = "translations/vedit_zh_CN.ts"
CONTEXT = re.compile(r"<context>\s*<name>(.*?)</name>", re.S)
MESSAGE = re.compile(r"<message>(.*?)</message>", re.S)
SOURCE = re.compile(r"<source>([^<]*)</source>")
UNFINISHED = re.compile(r'<translation type="unfinished"\s*(?:/>|></translation>)')


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    with open(sys.argv[1], encoding="utf-8") as handle:
        table = json.load(handle)

    with open(TS, encoding="utf-8") as handle:
        text = handle.read()

    starts = [match.start() for match in CONTEXT.finditer(text)]
    bounds = list(zip(starts, starts[1:] + [len(text)]))
    filled = 0
    missing_contexts = set()

    def fill_block(block: str, context_name: str) -> str:
        nonlocal filled
        sources = table.get(context_name)
        if sources is None:
            missing_contexts.add(context_name)
            return block

        def replace_message(match: re.Match) -> str:
            nonlocal filled
            body = match.group(1)
            if not UNFINISHED.search(body):
                return match.group(0)
            source = SOURCE.search(body)
            if source is None:
                return match.group(0)
            chinese = sources.get(html.unescape(source.group(1)))
            if chinese is None:
                return match.group(0)
            filled += 1
            body = UNFINISHED.sub(
                f"<translation>{html.escape(chinese, quote=False)}</translation>",
                body,
                count=1,
            )
            return f"<message>{body}</message>"

        return MESSAGE.sub(replace_message, block)

    parts = []
    previous = 0
    for start, end in bounds:
        parts.append(text[previous:start])
        block = text[start:end]
        name = CONTEXT.search(block)
        name = html.unescape(name.group(1)) if name else ""
        parts.append(fill_block(block, name))
        previous = end
    parts.append(text[previous:])
    merged = "".join(parts)

    unfinished = 0
    for context in CONTEXT.finditer(merged):
        block = merged[context.start():]
        for message in MESSAGE.finditer(block):
            if UNFINISHED.search(message.group(1)):
                unfinished += 1

    with open(TS, "w", encoding="utf-8") as handle:
        handle.write(merged)
    print(
        f"merge {sys.argv[1]} -> {TS}\n"
        f"  filled={filled} still-unfinished={unfinished}"
    )
    if missing_contexts:
        print(
            "  note: .ts contexts with no JSON table: "
            + ", ".join(sorted(name for name in missing_contexts if name))
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
