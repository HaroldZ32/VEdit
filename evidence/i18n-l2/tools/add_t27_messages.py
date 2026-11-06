#!/usr/bin/env python3
"""Add the T27 (F8 status-bar diagnostics text) messages to the .ts.

T27 closes the last English surface named in the G7 report §7.7:

  * the F8 / Shift+F8 status line template
    `Diagnostic %1/%2 - %3: %4` (`src/editor/kernel/lsp_actions.cpp`
    `describeDiagnostic()`), and
  * the five words `diagnosticsSeverityName()` returns for it
    (`error` / `warning` / `information` / `hint` / `problem`,
    `src/editor/kernel/diagnostics_annotations.h`).

Both now use `QCoreApplication::translate("VEdit", …)`, so the messages go into the
`VEdit` context with a real `<location>`. Translations carry no `type` and are
finished, so `lrelease` keeps reporting 0 unfinished. `arg-fragment` message
content (`message`) is already translated by its own call site.

Idempotent: a `<source>` already present in the context is skipped, so a re-run
cannot duplicate a message. The helper functions are the ones `add_t40_messages.py`
established for the same job, imported rather than copied.

Usage: python3 evidence/i18n-l2/tools/add_t27_messages.py [--check]
  --check   do not write; report what would be added.
"""
from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import add_t40_messages as t40  # noqa: E402  (path set above)

ROOT = pathlib.Path(__file__).resolve().parents[3]
TS = t40.TS
CONTEXT = t40.CONTEXT

# (source file relative to the workspace root, English source, Chinese translation)
MESSAGES: list[tuple[str, str, str]] = [
    (
        "src/editor/kernel/lsp_actions.cpp",
        "Diagnostic %1/%2 - %3: %4",
        "诊断 %1/%2 - %3：%4",
    ),
    ("src/editor/kernel/diagnostics_annotations.h", "error", "错误"),
    ("src/editor/kernel/diagnostics_annotations.h", "warning", "警告"),
    ("src/editor/kernel/diagnostics_annotations.h", "information", "信息"),
    ("src/editor/kernel/diagnostics_annotations.h", "hint", "提示"),
    ("src/editor/kernel/diagnostics_annotations.h", "problem", "问题"),
]


def main() -> int:
    check = "--check" in sys.argv[1:]
    lines = TS.read_text(encoding="utf-8").splitlines()
    lines, stale = t40.drop_stale(lines, 0, len(lines))
    start, end = t40.context_span(lines, CONTEXT)
    block = "\n".join(lines[start:end])
    sources = re.findall(r"<source>(.*?)</source>", block, re.S)

    added: list[str] = []
    skipped: list[str] = []
    rendered: list[str] = []
    for relative, literal, translation in MESSAGES:
        escaped = t40.xml_escape(literal)
        if escaped in sources:
            skipped.append(literal)
            continue
        line = t40.find_line(ROOT / relative, literal)
        if line == 0:
            raise SystemExit(f"FATAL: {literal!r} not found in {relative}")
        rendered.append(
            f"        <message>\n"
            f'            <location filename="../{relative}" line="{line}" />\n'
            f"            <source>{escaped}</source>\n"
            f"            <translation>{t40.xml_escape(translation)}</translation>\n"
            f"        </message>"
        )
        added.append(literal)

    print(f"context={CONTEXT} existing={len(sources)} add={len(added)} skip={len(skipped)} stale={stale}")
    for literal in added:
        print(f"  + {literal}")
    for literal in skipped:
        print(f"  = {literal} (already present)")
    if check:
        return 0
    if not rendered and not stale:
        print("nothing to write")
        return 0
    lines[end:end] = "\n".join(rendered).splitlines()
    TS.write_text("\n".join(lines) + "\n", encoding="utf-8")
    total = len(re.findall(r"<message>", TS.read_text(encoding="utf-8")))
    print(f"wrote {TS.relative_to(ROOT)}; messages now {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
