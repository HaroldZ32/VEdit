#!/usr/bin/env python3
"""Enumerate B-class i18n candidates (T38 / G7 / AC-9.2).

B class = USER-VISIBLE string literals in non-test `src/**` sources that are NOT
already wrapped in tr()/translate() and are not yet in the `.ts`.

Method (recorded so the list is reproducible):

1. Reuse the repository's durable extractor
   (`evidence/i18n/extract_i18n_candidates.py --any-literal --format csv`); it
   lexes every C/C++ literal with file/line/context. This is the same extractor
   T21/T45 shipped; the `--any-literal` flag is required because the default
   mode only sees literals directly wrapped in QStringLiteral() (the C9 rule),
   which yields 0 candidates after T21's wrapping work.
2. Keep literals in non-test files only: `*_selftest.cpp`, `*_tests.cpp`,
   `src/**/tests/**` and `src/**/probes/**` are developer surfaces (rule C8).
3. Keep a literal only when a UI SINK appears in a +/-4 line window around it:
   the text setters (setText/setTitle/setWindowTitle/setToolTip/...), action
   constructors, dock/panel titles, and the modal-dialog entry points
   (QFileDialog/QInputDialog/QMessageBox). A window is used because a call may
   span several lines.
4. Mark a candidate as WRAPPED when `translate(` or a `tr(` call is visible in
   the same window, or when the exact literal is already a `<source>` in
   `translations/vedit_zh_CN.ts` and the .ts records a location in this file.
   (A literal can be a fragment of a multi-line wrapped string: the .ts then
   stores the concatenated text, so the fragment itself is not a .ts source;
   the wrapping marker check above catches those.)
5. Everything else is a B candidate. Each row carries the decision the writer
   made (`translate` / `keep-english` / `fragment-of-wrapped` / `not-ui` /
   `deferred`) with a reason, so the report's header count equals its row count.

Output: CSV with one row per discovered literal, plus a summary line.
"""
from __future__ import annotations

import argparse
import collections
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

SINKS = (
    "setText", "setTitle", "setWindowTitle", "setToolTip", "setPlaceholderText",
    "setHeaderLabels", "setHeaderLabel", "setTabText", "setStatusTip", "setWhatsThis",
    "setAccessibleName", "setLabelText", "setInformativeText", "setDetailedText",
    "setFilter", "setNameFilter", "showMessage", "addAction", "addPanel", "addDockWidget",
    "addTab", "new QPushButton", "new QToolButton", "new QLabel", "new QCheckBox",
    "new QRadioButton", "QFileDialog", "QInputDialog", "QMessageBox", "QDialogButtonBox",
    "getOpenFileName", "getSaveFileName", "getExistingDirectory", "getText", "setStatus",
    "PanelSpec", "spec.title", "setDocumentMode", "QPushButton(", "QToolButton(",
)


def is_test(path: str) -> bool:
    base = os.path.basename(path)
    return (
        "_selftest.cpp" in base
        or base.endswith("_tests.cpp")
        or "/tests/" in path
        or "/probes/" in path
        or "test" in base.lower()
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", required=True, help="the --any-literal extractor CSV")
    parser.add_argument("--ts", required=True, help="the .ts ground truth (wrapped messages)")
    parser.add_argument("--out", required=True, help="discovery CSV to write")
    parser.add_argument("--summary", default="-", help="summary path ('-' = stdout)")
    args = parser.parse_args()

    ts_sources: set[str] = set()
    ts_locations: dict[str, set[str]] = collections.defaultdict(set)
    tree = ET.parse(args.ts)
    for context in tree.getroot().findall("context"):
        for message in context.findall("message"):
            source = message.findtext("source") or ""
            ts_sources.add(source)
            for location in message.findall("location"):
                ts_locations[source].add(location.get("filename", "").replace("../", ""))

    with open(args.csv, encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))

    file_lines: dict[str, list[str]] = {}
    discovered: list[dict[str, object]] = []
    for row in rows:
        path = row["path"]
        if is_test(path):
            continue
        if path not in file_lines:
            try:
                with open(path, encoding="utf-8") as handle:
                    file_lines[path] = handle.read().split("\n")
            except OSError:
                file_lines[path] = []
        line = int(row["line"])
        window_lines = file_lines[path][max(0, line - 5):line + 3]
        window = "\n".join(window_lines)
        sink = next((name for name in SINKS if name in window), "")
        if not sink:
            continue
        wrapped = "translate(" in window or re.search(r"\btr\s*\(", window) is not None
        in_ts = row["string"] in ts_sources and path in ts_locations.get(row["string"], set())
        discovered.append(
            {
                "path": path,
                "line": line,
                "string": row["string"],
                "call_site": row["call_site"],
                "sink": sink,
                "already_wrapped": "yes" if (wrapped or in_ts) else "no",
                "decision": "",
                "reason": "",
            }
        )

    with open(args.out, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["path", "line", "string", "call_site", "sink", "already_wrapped",
                        "decision", "reason"],
        )
        writer.writeheader()
        writer.writerows(discovered)

    wrapped_count = sum(1 for row in discovered if row["already_wrapped"] == "yes")
    summary = (
        f"discovered={len(discovered)} rows (wrapped={wrapped_count}, "
        f"unwrapped={len(discovered) - wrapped_count})"
    )
    print(summary)
    if args.summary != "-":
        with open(args.summary, "w", encoding="utf-8") as handle:
            handle.write(summary + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
