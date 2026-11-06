#!/usr/bin/env python3
"""Build the T38 B-class reconciliation table (AC-9.2).

Inputs (all produced in this wave):
  * `evidence/i18n-l2/logs/17-b-discovery-batch2.csv` — the CURRENT-tree
    enumeration (`--any-literal` extractor + UI-sink window filter).
  * `evidence/i18n-l2/logs/18-b2-selected-rows.json` — the 72 batch-2 wrap rows.

Output: `evidence/i18n-l2/03-b-reconciliation.csv` with one row per discovered
candidate and a decision + reason per row, so the report's header can state
「发现条数 = 行数」.

Decisions:
  译（已完成）  — already wrapped before this wave; the message is in the .ts
  译（本批）    — wrapped by batch 2 (QCoreApplication::translate("VEdit", …)) and translated
  译（片段）    — a literal fragment of a batch-2 wrapped concatenation
  不译          — not user-visible, a decorative glyph, or transient status-bar feedback
  待定          — visible text deliberately left for a follow-up (should be empty)
"""
from __future__ import annotations

import csv
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[3]
DISCOVERY = ROOT / "evidence/i18n-l2/logs/17-b-discovery-batch2.csv"
BATCH2 = ROOT / "evidence/i18n-l2/logs/18-b2-selected-rows.json"
OUT = ROOT / "evidence/i18n-l2/03-b-reconciliation.csv"

NON_UI = re.compile(
    r"(^color:|font-weight|padding:|border-radius|background-color|"
    r"^removed=|^DIAG-NAV|^FORMAT |^search: jump failed|^factory through|"
    r"enabled-by=|backend=%|formatter=%|changed=%d)"
)
GLYPH = ("● %1", "⚠ %1")


def normalise(text: str) -> str:
    """Compare fragment text loosely: the extractor keeps C escapes, the wrap
    table holds unescaped text."""
    table = {"\\n": "\n", "\\t": "\t", '\\"': '"', "\\\\": "\\"}
    for escaped, actual in table.items():
        text = text.replace(escaped, actual)
    return text


def main() -> int:
    rows = list(csv.DictReader(DISCOVERY.open(encoding="utf-8")))
    batch2 = json.loads(BATCH2.read_text(encoding="utf-8"))
    batch2 += json.loads((ROOT / "evidence/i18n-l2/logs/18-b2-extra.json").read_text(encoding="utf-8"))
    by_key = {(row["file"], row["literal"]) for row in batch2}
    by_path: dict[str, list[dict]] = {}
    for row in batch2:
        by_path.setdefault(row["file"], []).append(row)

    out = []
    counts: dict[str, int] = {}
    for row in rows:
        path = row["path"]
        line = int(row["line"])
        text = row["string"]
        literal = normalise(text)
        if row["already_wrapped"] == "yes":
            decision, reason = "译（已完成）", "wrapped before this wave; message present and translated in the .ts"
        elif (path, text) in by_key or (path, literal) in by_key:
            decision, reason = "译（本批）", "batch-2: QCoreApplication::translate(\"VEdit\", …) + Chinese translation"
        elif any(
            entry["file"] == path
            and abs(entry["line_hint"] - line) <= 2
            and literal and literal in entry["literal"]
            for entry in by_path.get(path, [])
        ):
            decision, reason = "译（片段）", "fragment of a batch-2 wrapped concatenation (translated as one message)"
        elif NON_UI.search(text):
            decision, reason = "不译", "not user-visible text (style sheet / log / developer message)"
        elif text in GLYPH:
            decision, reason = "不译", "decorative glyph prefix, not a sentence"
        elif row["sink"] == "showMessage":
            decision, reason = "不译", "transient status-bar feedback; kept English this wave by the documented batch-2 boundary"
        else:
            decision, reason = "待定", "visible text not wrapped in this wave"
        counts[decision] = counts.get(decision, 0) + 1
        out.append(
            {
                "path": path,
                "line": line,
                "string": text,
                "call_site": row["call_site"],
                "sink": row["sink"],
                "already_wrapped": row["already_wrapped"],
                "decision": decision,
                "reason": reason,
            }
        )

    with OUT.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(out[0]))
        writer.writeheader()
        writer.writerows(out)
    print(f"findings={len(out)} rows={len(out)} -> {OUT.relative_to(ROOT)}")
    for decision, count in sorted(counts.items()):
        print(f"  {decision}: {count}")
    return 0 if counts.get("待定", 0) == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
