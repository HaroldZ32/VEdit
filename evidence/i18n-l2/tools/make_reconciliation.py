#!/usr/bin/env python3
"""Build the B-class reconciliation table (AC-9.2) — T40 repair version.

Inputs:
  * `evidence/i18n-l2/logs/17-b-discovery-batch2.csv` — the T38 discovery set
    (`--any-literal` extractor + UI-sink window filter). The SET is frozen (it
    records what was still unwrapped in T38); the call-site LINE of every row is
    re-resolved against the current tree, and the `already_wrapped` claim is no
    longer copied from the input — it is recomputed by `tools/verify_claims.py`
    against (a) the literal's own call site and (b) the .ts.
  * `evidence/i18n-l2/logs/18-b2-selected-rows.json`, `18-b2-extra.json` — the
    T38 batch-2 wrap rows.
  * `tools/add_t40_messages.py::MESSAGES` — the T40 repair wraps (F1/F2/F4); it
    doubles as the SUPPLEMENT list: a T40 message the T38 UI-sink window did not
    select becomes a supplementary row, because F2 showed the window rule misses
    model-getter → panel-header strings.
  * `translations/vedit_zh_CN.ts` — the translation ground truth.

Outputs:
  * `evidence/i18n-l2/03-b-reconciliation.csv` — one row per discovered item,
    with the recomputed claim and a `verified` column:
      `own-call+ts`  the literal is a translate() argument of its own call site and
                     the message (or the longer message it is a piece of) is in the
                     .ts — the ONE claim-verifiable state (T27);
      `ts-fragment-unwrapped`  a piece of a translated message, but the literal is
                     NOT inside this call's translate() arguments — the claim is
                     NOT verifiable;
      `no-wrap`/`no-ts`/`wrap-only`/`ts-only` — the claim is NOT verifiable.
  * `evidence/i18n-l2/logs/20-t40-claim-audit.csv` — every row whose
    already_wrapped / decision / reason changed versus the T38 table.

The T38 generator is preserved as `tools/make_reconciliation.t38.py` so the
repair can be diffed.

Invariant printed and asserted: `findings = rows` (T38 discovery + supplement).

Exit code: 0 when every row is decided and every `already_wrapped=yes` row is
verified; 1 otherwise (`待定` rows or an unverifiable claim are defects, not a
table to publish silently).
"""
from __future__ import annotations

import collections
import csv
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import add_t40_messages
import verify_claims

ROOT = pathlib.Path(__file__).resolve().parents[3]
DISCOVERY = ROOT / "evidence/i18n-l2/logs/17-b-discovery-batch2.csv"
BATCH2 = ROOT / "evidence/i18n-l2/logs/18-b2-selected-rows.json"
BATCH2_EXTRA = ROOT / "evidence/i18n-l2/logs/18-b2-extra.json"
TS = ROOT / "translations/vedit_zh_CN.ts"
OUT = ROOT / "evidence/i18n-l2/03-b-reconciliation.csv"
AUDIT = ROOT / "evidence/i18n-l2/logs/20-t40-claim-audit.csv"

STYLE_TEXT = re.compile(
    r"(^color:|font-weight|padding:|border-radius|background-color|^&nbsp;|<span|<b>)"
)
LOG_TEXT = re.compile(
    r"^(open\.paths:|removed=|DIAG-NAV|FORMAT |search: jump failed|search: jump opened|"
    r"enabled-by=|backend=%|formatter=%|changed=%d|severity=)"
)
LOG_CALL = re.compile(
    r"(qInfo|qDebug|qWarning|qCritical|printf|logEditorActionLine|logAction|appendLog)\s*\("
)
DEVELOPER_TEXT = re.compile(r"^(Content seam:|factory through)")
GLYPH = ("● %1", "⚠ %1")

# Named AC-4.3 categories: an entry here is NOT a user sentence, and the reason
# names the category so the next maintainer can apply the same rule. F4 forbids
# the old catch-all "batch-2 boundary": every one of the 21 transient
# status-bar strings is a user sentence and is translated; only the rows below
# stay English, each under a NAME.
EXEMPT_KINDS = {
    "style": "AC-4.3 code snippet: Qt style-sheet / rich-text markup fragment, never a user sentence",
    "log": "AC-4.3 technical identifier: developer log line (qInfo/printf/logEditorActionLine), never rendered in the UI",
    "developer": "AC-4.3 code snippet: developer-only seam placeholder naming the C++ factory, not user documentation",
    "glyph": "AC-4.3 technical identifier: decorative glyph prefix, not a sentence",
}


def normalise(text: str) -> str:
    return re.sub(r"\s+", " ", verify_claims.unescape_c(text)).strip()


def non_ui_kind(row: dict[str, str], verified: dict[str, str]) -> str:
    """The named non-UI category for a row, or "" when it is user-visible."""
    text = row["string"]
    statement = verified.get("statement", "")
    if text in GLYPH:
        return "glyph"
    if DEVELOPER_TEXT.search(text):
        return "developer"
    if STYLE_TEXT.search(text) or "setStyleSheet" in statement:
        return "style"
    if LOG_TEXT.search(text) or LOG_CALL.search(statement) or row["call_site"] == "log":
        return "log"
    return ""


def sink_of(statement: str) -> str:
    for sink in (
        "showMessage",
        "setWindowTitle",
        "setText",
        "setTitle",
        "setToolTip",
        "setPlaceholderText",
        "addDockWidget",
        "QDockWidget",
        "setStatus",
    ):
        if sink in statement:
            return sink
    return "ui_text"


def main() -> int:
    old_rows = list(csv.DictReader(OUT.open(encoding="utf-8"))) if OUT.exists() else []
    rows = list(csv.DictReader(DISCOVERY.open(encoding="utf-8")))
    batch2 = json.loads(BATCH2.read_text(encoding="utf-8"))
    batch2 += json.loads(BATCH2_EXTRA.read_text(encoding="utf-8"))
    by_key = {(row["file"], row["literal"]) for row in batch2}
    t40 = {(path, normalise(literal)) for path, literal, _ in add_t40_messages.MESSAGES}
    t40_by_path: dict[str, list[str]] = {}
    for path, literal, _ in add_t40_messages.MESSAGES:
        t40_by_path.setdefault(path, []).append(normalise(literal))
    by_path: dict[str, list[dict]] = {}
    for row in batch2:
        by_path.setdefault(row["file"], []).append(row)

    ts_messages = verify_claims.load_ts_messages(TS)
    out: list[dict[str, object]] = []
    counts: dict[str, int] = {}
    verified_counts: collections.Counter[str] = collections.Counter()
    seen: set[tuple[str, str]] = set()

    def decide(
        row: dict[str, str], verified: dict[str, str], key: tuple[str, str]
    ) -> tuple[str, str, str]:
        claim = "yes" if verified["verified"] == "own-call+ts" else "no"
        # A discovery row may be a FRAGMENT of a multi-line T40 message (the
        # settings-panel strings are split across adjacent C literals); it counts
        # as a T40 row when it is a same-file piece of one.
        row_text = normalise(row["string"])
        fragment_of_t40 = any(
            len(row_text) >= 12 and row_text in message
            for message in t40_by_path.get(row["path"], [])
        )
        if key in t40 or fragment_of_t40:
            return (
                claim,
                "译（本批）",
                "T40: user-visible status/header text; wrapped with "
                'QCoreApplication::translate("VEdit", …) and translated in the .ts '
                "(claim re-verified by tools/verify_claims.py)",
            )
        # Provenance first: a row T38 wrapped itself stays 「本批」 even now that
        # the claim verifies — the reordering keeps the T38 history readable.
        if (row["path"], row["string"]) in by_key or (
            row["path"],
            normalise(row["string"]),
        ) in by_key:
            return (
                claim,
                "译（本批）",
                'T38 batch-2: QCoreApplication::translate("VEdit", …) + Chinese translation',
            )
        if any(
            entry["file"] == row["path"]
            and abs(entry["line_hint"] - int(row["line"])) <= 2
            and normalise(row["string"])
            and normalise(row["string"]) in normalise(entry["literal"])
            for entry in by_path.get(row["path"], [])
        ):
            return (
                claim,
                "译（片段）",
                "fragment of a T38 batch-2 wrapped concatenation (translated as one message)",
            )
        if claim == "yes":
            return (
                claim,
                "译（已完成）",
                "verified: tr()/translate() is this literal's own call site and the "
                "message is in the .ts (tools/verify_claims.py)",
            )
        kind = non_ui_kind(row, verified)
        if kind:
            return claim, "不译", EXEMPT_KINDS[kind]
        return (
            claim,
            "待定",
            "user-visible text that is neither wrapped nor in the .ts — a defect",
        )

    for row in rows:
        verified = verify_claims.assess(
            ROOT, row["path"], int(row["line"]), row["string"], ts_messages
        )
        key = (row["path"], normalise(row["string"]))
        claim, decision, reason = decide(row, verified, key)
        counts[decision] = counts.get(decision, 0) + 1
        verified_counts[verified["verified"]] += 1
        seen.add(key)
        out.append(
            {
                "path": row["path"],
                "line": int(verified["line"]),
                "string": row["string"],
                "call_site": row["call_site"],
                "sink": row["sink"],
                "already_wrapped": claim,
                "decision": decision,
                "reason": reason,
                "verified": verified["verified"],
                "wrap": verified["wrap"],
                "ts": verified["ts"],
                "statement": verified["statement"],
            }
        )

    # Supplementary rows: T40 strings the T38 UI-sink window did not select. F2
    # showed the window misses model-getter → panel-header paths, so the
    # supplement is the fix for the FILTER, not a hand edit of individual rows.
    supplement = 0
    for path, literal, _translation in add_t40_messages.MESSAGES:
        key = (path, normalise(literal))
        if key in seen:
            continue
        verified = verify_claims.assess(ROOT, path, 1, literal, ts_messages)
        if verified["wrap"] == "none":
            print(f"FATAL: T40 literal not wrapped at its call site: {path}: {literal!r}")
            return 1
        supplement += 1
        counts["译（本批）"] = counts.get("译（本批）", 0) + 1
        verified_counts[verified["verified"]] += 1
        seen.add(key)
        out.append(
            {
                "path": path,
                "line": int(verified["line"]),
                "string": literal,
                "call_site": "ui_text (supplement)",
                "sink": sink_of(verified["statement"]),
                "already_wrapped": "yes",
                "decision": "译（本批）",
                "reason": "T40 supplement: the T38 UI-sink window did not select this "
                "call site (model getter / panel header path); the string is wrapped "
                'with QCoreApplication::translate("VEdit", …) and in the .ts '
                "(tools/verify_claims.py verified)",
                "verified": verified["verified"],
                "wrap": verified["wrap"],
                "ts": verified["ts"],
                "statement": verified["statement"],
            }
        )

    discovered = len(rows) + supplement
    if discovered != len(out):
        print(f"FATAL: discovered={discovered} != rows={len(out)}")
        return 1

    MAIN_FIELDS = [
        "path",
        "line",
        "string",
        "call_site",
        "sink",
        "already_wrapped",
        "decision",
        "reason",
        "verified",
    ]
    with OUT.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=MAIN_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(out)

    # The audit of the T38 table: which claim/decision/reason changed, and how.
    # Kinds: `claim-flip` (already_wrapped changed), `decision-change`, and
    # `reason-verified` (same claim, the reason text now names the verification).
    changed: list[dict[str, str]] = []
    old_by_key = {(row["path"], normalise(row["string"])): row for row in old_rows}
    for row in out:
        current = {k: str(v) for k, v in row.items()}
        old = old_by_key.get((row["path"], normalise(str(row["string"]))))
        if old is None:
            changed.append(
                {
                    **current,
                    "change": "supplement-added",
                    "old_already_wrapped": "",
                    "old_decision": "",
                    "old_reason": "",
                }
            )
            continue
        kinds: list[str] = []
        if old["already_wrapped"] != row["already_wrapped"]:
            kinds.append(
                "claim-flip"
                f"({old['already_wrapped']}->{row['already_wrapped']})"
            )
        if old["decision"] != row["decision"]:
            kinds.append(f"decision-change({old['decision']}->{row['decision']})")
        if old["reason"] != row["reason"] and not kinds:
            kinds.append("reason-verified")
        if kinds:
            changed.append(
                {
                    **current,
                    "change": ";".join(kinds),
                    "old_already_wrapped": old["already_wrapped"],
                    "old_decision": old["decision"],
                    "old_reason": old["reason"],
                }
            )
    with AUDIT.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = list(changed[0]) if changed else ["path", "change"]
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(changed)

    unverifiable = [
        row
        for row in out
        if row["already_wrapped"] == "yes"
        and row["verified"] != "own-call+ts"
    ]
    print(
        f"findings={discovered} rows={len(out)} supplement={supplement} -> {OUT.relative_to(ROOT)}"
    )
    for decision, count in sorted(counts.items()):
        print(f"  {decision}: {count}")
    print("  verified:", dict(sorted(verified_counts.items())))
    print(f"audit rows changed={len(changed)} -> {AUDIT.relative_to(ROOT)}")
    for kind, count in sorted(
        collections.Counter(row["change"] for row in changed).items(), key=lambda kv: -kv[1]
    ):
        print(f"  audit {kind}: {count}")
    if unverifiable:
        print(f"FAIL: {len(unverifiable)} row(s) claim already_wrapped=yes without verification:")
        for row in unverifiable:
            print(f"  {row['path']}:{row['line']} {row['verified']} :: {row['string']}")
    if counts.get("待定", 0):
        print(f"FAIL: {counts['待定']} 待定 row(s)")
    return 0 if not unverifiable and not counts.get("待定", 0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
