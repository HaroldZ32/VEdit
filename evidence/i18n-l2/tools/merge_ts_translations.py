#!/usr/bin/env python3
"""Merge i18n translations into a Qt .ts file (T38 / G7).

Input JSON shape (both A-class and B-class use it):

    { "<context>": { "<source text>": "<Chinese translation>" }, ... }

A value may also be a list of strings for a `numerus=\"yes\"` message (one
`<numerusform>` per form). Every matched `<message>` gets its text filled and
its `translation type="unfinished"` cleared; a value that is an empty string is
refused (an empty translation is indistinguishable from "untranslated").

The script reports:
  * filled / already-translated / still-unfinished counts,
  * every still-unfinished (context, source) pair, so a missing key is loud.

It is deliberately idempotent: a second run fills nothing and changes nothing.
"""
from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ts", required=True, help="the .ts file to update (in place)")
    parser.add_argument("--json", required=True, help="context -> source -> translation JSON")
    parser.add_argument("--report", default="-", help="report path ('-' = stdout)")
    args = parser.parse_args()

    with open(args.json, encoding="utf-8") as handle:
        translations: dict[str, dict[str, object]] = json.load(handle)

    tree = ET.parse(args.ts)
    root = tree.getroot()

    filled = 0
    already = 0
    unfinished: list[tuple[str, str]] = []
    unknown_contexts: set[str] = set()

    for context in root.findall("context"):
        name = (context.findtext("name") or "").strip()
        table = translations.get(name)
        if table is None:
            unknown_contexts.add(name)
            table = {}
        for message in context.findall("message"):
            source = (message.findtext("source") or "")
            translation = message.find("translation")
            if translation is None:
                continue
            if translation.get("type") == "unfinished":
                pass
            elif (translation.text or "").strip() or translation.find("numerusform") is not None:
                already += 1
                continue
            value = table.get(source)
            if value is None:
                if translation.get("type") == "unfinished":
                    unfinished.append((name, source))
                continue
            if isinstance(value, list):
                # numerus="yes": one numerusform per plural form.
                for child in list(translation):
                    translation.remove(child)
                translation.text = None
                translation.attrib.pop("type", None)
                for form in value:
                    if not str(form).strip():
                        raise SystemExit(f"empty numerus form for {name} / {source!r}")
                    node = ET.SubElement(translation, "numerusform")
                    node.text = str(form)
                filled += 1
                continue
            text = str(value)
            if not text.strip():
                raise SystemExit(f"empty translation for {name} / {source!r}")
            for child in list(translation):
                translation.remove(child)
            translation.text = text
            translation.attrib.pop("type", None)
            filled += 1

    ET.indent(tree, space="    ")
    tree.write(args.ts, encoding="utf-8", xml_declaration=True)

    lines = [
        f"merge {args.json} -> {args.ts}",
        f"  filled={filled} already-translated={already} still-unfinished={len(unfinished)}",
    ]
    for name, source in unfinished:
        lines.append(f"  UNFINISHED {name} | {source}")
    if unknown_contexts:
        lines.append("  note: .ts contexts with no JSON table: " + ", ".join(sorted(unknown_contexts)))
    text = "\n".join(lines) + "\n"
    if args.report == "-":
        sys.stdout.write(text)
    else:
        with open(args.report, "w", encoding="utf-8") as handle:
            handle.write(text)
        print(text, end="")
    return 0 if not unfinished else 1


if __name__ == "__main__":
    raise SystemExit(main())
