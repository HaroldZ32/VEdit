# T45 conditional PASS → upgrade audit (T100, part C)

C9 header

- collected_at / HEAD: see `01-blockcomment-red.log`, `02-blockcomment-green.log`,
  `04-gates.log`, and the files cited below (each keeps its own header).
- This is an audit record only: **no verdict file was edited** (verdict owners do
  that).

## Checklist

| # | item | observed |
|---|---|---|
| 1 | old `10-folding-before.log` / `11-folding-after.log` carry an explicit invalid banner | **YES** — both start with `# INVALID (T66): this log measured the DISTRO library (F2_DIR path bug) and the SV sample branch never ran; superseded by …-F2.log` |
| 2 | re-recorded F2 logs exist and are complete | **YES** — `10-folding-before-F2.log` (before: Verilog `begins=0 ends=0`, SystemVerilog **26-line** sample `0/0`, `PROBE-SUMMARY mode=before expected=zero all-checks=PASS`, `RUNNER-EXIT=0`) and `11-folding-after-F2.log` (after: Verilog `7/7`, SystemVerilog **26-line** sample `11/11`, `PROBE-SUMMARY … all-checks=PASS`, `RUNNER-EXIT=0`) |
| 3 | the F2 logs carry a link fact, not a label | **YES** — both contain `# ldd guard: … resolves inside the F2 tree: …/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6` and an `LDD library=F2 resolves=…F2…` line before every result block |
| 4 | the runner's guard is falsifiable | **YES** — `14-ldd-guard-negative-control.log`: the old flags resolve `/usr/lib/…/libKF6SyntaxHighlighting.so.6` (DISTRO) and the guard returns `GUARD-ON-OLD-BINARY=REJECTED exit=1` |
| 5 | documents still citing the old 10/11 as valid | **one stale citation found and fixed**: `FOLDING-fix-T45.md` said the zero-fold measurement was `10-folding-before.log`; it now cites `10-folding-before-F2.log` and names the old one superseded. Its other mentions (lines about the first recording) explicitly say **invalid**; `03-folding.log` has the KDE-built-ins annotation pointing at the `-F2` logs; `06-conclusion.md` points at the `-F2` logs; `T66-REPORT.md` mentions the old logs only as historical/invalid |
| 6 | docs outside this evidence set | `docs/handover/appendix/amendments-rev2.md`, `docs/decisions/r2-license.md` and the requirements no longer cite the old logs as valid (grep over `docs/`, `AGENT.md`, `requirements.md`: no hit) |
| 7 | stale verdict text | `evidence/verdicts/fix-xml-folding/verdict.md` still reads "PASS（有条件）… 在 T66 修好前" and `evidence/verdicts/a4/verdict.md` F-3 still says "T66 仍 open". Those are **verdict records**, not evidence; the owners must update them |

## Conclusion

**T45 can be upgraded to an unconditional PASS.** The conditional clause in
`evidence/verdicts/fix-xml-folding/verdict.md` was "until T66 re-records 10/11
with an ldd-guarded runner". That condition is met:

- **X** = `evidence/syn-kde-data/10-folding-before-F2.log` — real F2 (ldd),
  before = 0/0 on the 18-line Verilog and 26-line SystemVerilog samples;
- **Y** = `evidence/syn-kde-data/11-folding-after-F2.log` — real F2 (ldd),
  after = Verilog 7/7, SystemVerilog 11/11 on the 26-line sample;
- **Z** = `evidence/syn-kde-data/14-ldd-guard-negative-control.log` — the guard
  rejects a DISTRO-linked binary with `exit=1`;
- plus the old logs' `# INVALID (T66)` banners and the corrected
  `FOLDING-fix-T45.md` citation above.

No evidence gap remains. The only follow-up is administrative: the verdict owners
(`fix-xml-folding`/`a4`) should replace "有条件 PASS" / "T66 仍 open" with the
unconditional PASS and cite X/Y/Z; this task did not edit their files.
