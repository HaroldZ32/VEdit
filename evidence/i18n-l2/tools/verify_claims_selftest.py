#!/usr/bin/env python3
r"""Self-test for `verify_claims.py`'s claim states — re-runnable, no repo writes.

T27 tightened the verifier: a `fragment` hit in the .ts (the literal is a piece of
a longer translated message) is a YES only when the literal also sits inside the
translate() call of its OWN statement (`wrap == own-call`). A fragment hit with the
literal OUTSIDE that call is `ts-fragment-unwrapped` and must NOT be counted as
`already_wrapped=yes` — that was the T39/F3 false claim.

The cases are built in a temporary directory (a two-file fixture: one .cpp, one
.ts), so this file can be run as often as wanted and leaves the workspace
untouched:

    python3 evidence/i18n-l2/tools/verify_claims_selftest.py

Exit 0 = every case behaved as documented; 1 = a regression.
"""
from __future__ import annotations

import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import verify_claims  # noqa: E402  (path set above)

# The ONE state make_reconciliation.py treats as `already_wrapped=yes`.
YES_STATES = ("own-call+ts",)

SOURCE = """void lane()
{
    const QString plain = QStringLiteral("lines skipped");
    const QString wrapped = tr("Opened %1 lines skipped");
    const QString saved = tr("Saved");
    const QString joined = tr("Opened %1 "
                              "lines skipped");
    const QString untranslated = QStringLiteral("Nowhere to be found");
}
"""

TS = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>VEdit</name>
    <message>
        <location filename="../src/lane.cpp" line="4" />
        <source>Opened %1 lines skipped</source>
        <translation>已打开 %1 行已跳过</translation>
    </message>
    <message>
        <location filename="../src/lane.cpp" line="6" />
        <source>Opened %1 lines skipped</source>
        <translation>已打开 %1 行已跳过</translation>
    </message>
    <message>
        <source>Saved</source>
        <translation>已保存</translation>
    </message>
</context>
</TS>
"""

# (label, line, literal, expected verified state, expected claim)
CASES = [
    # The T27 negative case: a piece of a translated message, but the literal is
    # NOT inside the translate() call of its own statement -> NOT a yes.
    ("fragment-only (outside the call)", 3, "lines skipped", "ts-fragment-unwrapped", "no"),
    # The same fragment INSIDE the multi-line translate() -> the literal is part of
    # the message this call translates -> a yes.
    ("fragment+own-call", 7, "lines skipped", "own-call+ts", "yes"),
    ("own-call exact", 5, "Saved", "own-call+ts", "yes"),
    # Negative control: neither wrapped nor in the .ts.
    ("no-wrap", 8, "Nowhere to be found", "no-wrap", "no"),
]


def main() -> int:
    failures = 0
    with tempfile.TemporaryDirectory(prefix="verify-claims-selftest-") as tmp:
        root = pathlib.Path(tmp)
        (root / "src").mkdir()
        (root / "translations").mkdir()
        (root / "src/lane.cpp").write_text(SOURCE, encoding="utf-8")
        ts_path = root / "translations/test.ts"
        ts_path.write_text(TS, encoding="utf-8")
        messages = verify_claims.load_ts_messages(ts_path)

        print(f"fixture={root}  contexts=VEdit  messages={len(messages)}")
        for label, line, literal, expected_state, expected_claim in CASES:
            verified = verify_claims.assess(root, "src/lane.cpp", line, literal, messages)
            state = verified["verified"]
            claim = "yes" if state in YES_STATES else "no"
            ok = state == expected_state and claim == expected_claim
            failures += 0 if ok else 1
            print(
                f"{'ok  ' if ok else 'FAIL'} {label:<34} "
                f"wrap={verified['wrap']:<10} ts={verified['ts']:<8} "
                f"verified={state:<20} claim={claim}"
                + ("" if ok else f"  (expected {expected_state}/{expected_claim})")
            )

        # The invariant the whole verifier exists for: a fragment hit alone never
        # becomes a yes, whatever the row's other columns say.
        fragment_only = verify_claims.assess(root, "src/lane.cpp", 3, "lines skipped", messages)
        if fragment_only["ts"] != "fragment":
            print(f"FAIL fixture: expected ts=fragment, got {fragment_only['ts']}")
            failures += 1
        elif fragment_only["verified"] in YES_STATES:
            print("FAIL fragment-only row was counted as already_wrapped=yes")
            failures += 1

    print(f"{len(CASES)} cases, {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
