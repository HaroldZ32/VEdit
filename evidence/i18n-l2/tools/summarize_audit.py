#!/usr/bin/env python3
"""Summarize the T40 claim audit (`logs/20-t40-claim-audit.csv`) for the report.

T39's F3 asked for an audit table of every reconciliation row whose
`already_wrapped` / translated claim changed. The machine-readable source of
truth is `logs/20-t40-claim-audit.csv` (written by `make_reconciliation.py`);
this script turns it into the markdown table the report embeds, grouped by the
kind of change so each group can be checked against the source call site and the
`.ts`:

  A. `claim-flip`             — already_wrapped changed direction;
  B. `decision-change`        — same claim, different translation decision;
  C. `supplement-added`       — a T40 row the T38 UI-sink window never listed;
  D. `reason-verified`        — reason text only (counted, not listed).

Usage: python3 evidence/i18n-l2/tools/summarize_audit.py
Output: evidence/i18n-l2/logs/22-t40-claim-audit-summary.md
"""
from __future__ import annotations

import collections
import csv
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
AUDIT = ROOT / "evidence/i18n-l2/logs/20-t40-claim-audit.csv"
OUT = ROOT / "evidence/i18n-l2/logs/22-t40-claim-audit-summary.md"


def row_kind(row: dict[str, str]) -> str:
    change = row["change"]
    if change == "supplement-added":
        return "C"
    if change.startswith("claim-flip"):
        return "A"
    if change.startswith("decision-change"):
        return "B"
    return "D"


def main() -> int:
    rows = list(csv.DictReader(AUDIT.open(encoding="utf-8")))
    groups: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    for row in rows:
        groups[row_kind(row)].append(row)

    lines = [
        "# T40 — F3 claim audit (generated)",
        "",
        "Source: `evidence/i18n-l2/logs/20-t40-claim-audit.csv` "
        "(produced by `tools/make_reconciliation.py`).",
        "",
        "| kind | rows |",
        "|---|---|",
    ]
    for kind, title in (
        ("A", "A · claim-flip（already_wrapped 改了方向）"),
        ("B", "B · decision-change（声称不变，翻译决定改了）"),
        ("C", "C · supplement-added（T38 的 UI-sink 过滤没发现的 T40 行）"),
        ("D", "D · reason-verified（只有理由文本改成可验证说法）"),
    ):
        lines.append(f"| {title} | {len(groups.get(kind, []))} |")
    lines.append("")

    for kind, title in (
        ("A", "A · claim-flip"),
        ("B", "B · decision-change"),
        ("C", "C · supplement-added"),
    ):
        rows_kind = groups.get(kind, [])
        lines.append(f"## {title}（{len(rows_kind)}）")
        lines.append("")
        if not rows_kind:
            lines.append("（无）")
            lines.append("")
            continue
        lines.append(
            "| # | path:line | string | 判定（tools/verify_claims.py） | 旧 → 新 |"
        )
        lines.append("|---|---|---|---|---|")
        for index, row in enumerate(sorted(rows_kind, key=lambda r: (r["path"], int(r["line"]))), 1):
            old = f"{row['old_already_wrapped'] or '—'} / {row['old_decision'] or '—'}"
            new = f"{row['already_wrapped']} / {row['decision']}"
            text = row["string"].replace("|", "\\|").replace("\n", "\\n")[:80]
            verified = row["verified"]
            lines.append(
                f"| {index} | `{row['path']}:{row['line']}` | `{text}` | "
                f"{verified} | {old} → {new} |"
            )
        lines.append("")

    if groups.get("D"):
        lines.append("## D · reason-verified")
        lines.append("")
        lines.append(
            f"{len(groups['D'])} 行的 `already_wrapped` 与 decision 均未变；理由文本从 T38 的"
            "无条件断言改为「tools/verify_claims.py 已验证」的说法。逐行内容见 CSV 的 "
            "`change=reason-verified` 行。"
        )
        lines.append("")

    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: " + ", ".join(f"{k}={len(v)}" for k, v in sorted(groups.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
