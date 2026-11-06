#!/usr/bin/env python3
"""Wrap the B-class literals in `tr()`-equivalent product code (T38 / G7).

Reads `evidence/i18n-l2/03-b-wrap-table.json` (one row per call site: file,
line_hint, exact literal text) and replaces the `QStringLiteral(` of the
matching call with `QCoreApplication::translate("VEdit", `.

Why `QCoreApplication::translate("VEdit", ...)`: the previous writer's decision
(handoff se-i18n-1) keeps the .ts contexts simple (`VEdit`) and works in free
functions and QObject classes alike. The closing parenthesis is unchanged, so a
call with `.arg(...)` chains still compiles.

Safety:
  * an occurrence is matched by its full literal text (adjacent C++ literals are
    concatenated first, exactly like the compiler/lupdate see them), and when the
    same text occurs more than once the occurrence nearest `line_hint` wins;
  * a call site that already has `translate("VEdit"` is skipped (idempotent);
  * every replacement is counted and reported; the script fails loudly when a
    table row cannot be matched.
"""
from __future__ import annotations

import argparse
import json
import re
import sys


def unescape_cpp(text: str) -> str:
    out: list[str] = []
    i = 0
    mapping = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "0": "\0"}
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            out.append(mapping.get(text[i + 1], text[i + 1]))
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def literal_after(text: str, quote: int) -> tuple[str, int]:
    """Parse one or more adjacent C++ string literals starting at `quote`."""
    parts: list[str] = []
    i = quote
    while i < len(text) and text[i] == '"':
        i += 1
        start = i
        while i < len(text) and text[i] != '"':
            i += 2 if (text[i] == "\\" and i + 1 < len(text)) else 1
        parts.append(unescape_cpp(text[start:i]))
        i += 1
        j = i
        while j < len(text) and text[j] in " \t\n":
            j += 1
        if j < len(text) and text[j] == '"':
            i = j
        else:
            break
    return "".join(parts), i


def occurrences(text: str) -> list[dict]:
    found = []
    for match in re.finditer(r"QStringLiteral\(", text):
        start = match.start()
        line_start = text.rfind("\n", 0, start) + 1
        prefix = text[line_start:start]
        if "//" in prefix or prefix.count('"') % 2 == 1:
            continue  # comment, or the position is inside another literal
        quote = text.find('"', start)
        if quote < 0:
            continue
        literal, _ = literal_after(text, quote)
        found.append(
            {
                "start": start,
                "line": text.count("\n", 0, start) + 1,
                "literal": literal,
                # Idempotency only: the call itself was already rewritten.
                "wrapped": text[max(0, start - 40):start].endswith(
                    'QCoreApplication::translate("VEdit", '),
            }
        )
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--table", required=True)
    parser.add_argument("--dry-run", action="store_true", help="report matches, write nothing")
    args = parser.parse_args()

    with open(args.table, encoding="utf-8") as handle:
        table: list[dict] = json.load(handle)

    by_file: dict[str, list[dict]] = {}
    for row in table:
        by_file.setdefault(row["file"], []).append(row)

    total_wrapped = 0
    total_skipped = 0
    failures: list[str] = []

    for path, rows in sorted(by_file.items()):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        edits: list[tuple[int, str]] = []
        for row in rows:
            candidates = [
                occ
                for occ in occurrences(text)
                if occ["literal"] == row["literal"] and occ["line"] <= row["line_hint"] + 40
                and occ["line"] >= row["line_hint"] - 40
            ]
            if not candidates:
                failures.append(f"{path}: no `QStringLiteral(\"{row['literal'][:60]}\")` near line {row['line_hint']}")
                continue
            candidates.sort(key=lambda occ: abs(occ["line"] - row["line_hint"]))
            chosen = candidates[0]
            if chosen["wrapped"]:
                total_skipped += 1
                continue
            if any(edit[0] == chosen["start"] for edit in edits):
                # Second table row resolved to the same call site: try the next
                # occurrence of the same literal (e.g. a duplicated status call).
                rest = [occ for occ in candidates if occ["start"] != chosen["start"]
                        and not occ["wrapped"]]
                if not rest:
                    total_skipped += 1
                    continue
                chosen = rest[0]
            edits.append((chosen["start"], 'QCoreApplication::translate("VEdit", '))
            total_wrapped += 1
        if not args.dry_run:
            for start, replacement in sorted(edits, reverse=True):
                text = text[:start] + replacement + text[start + len("QStringLiteral("):]
        if edits and not args.dry_run and "#include <QCoreApplication>" not in text:
            include_at = text.find('#include "')
            if include_at < 0:
                include_at = 0
            line_end = text.find("\n", include_at)
            text = text[:line_end + 1] + "#include <QCoreApplication>\n" + text[line_end + 1:]
        if not args.dry_run:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
        print(f"{path}: wrapped={len(edits)}")

    print(f"\ntotal wrapped call sites = {total_wrapped}, already wrapped/skipped = {total_skipped}")
    for failure in failures:
        print("FAIL:", failure)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
