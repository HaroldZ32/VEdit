# A3 log index — what is current and what is superseded

The A3 review (`evidence/ux-batch-2/review-a3/REPORT.md`, High 3) found that the delivered
`09-appdir-checks-full.log` contained a line the selftest in the tree cannot print:
`CHECK definition-parsed.top.v PASS  no definition named 'Verilog' parsed from …`.
That detail string is only passed to `check()` **when the result is `FAIL`** (the current source is
`check(name, !shipped.isEmpty(), shipped.isEmpty() ? "no definition named …" : shipped)`), so the old
log predates the reviewed source. The repair rebuilt the tree and replaced 09/10 with complete output.

| Log | Status | Why |
|---|---|---|
| `09-appdir-checks-full.log` | **CURRENT** (replaced by the A3 repair) | Complete, unedited `vedit-highlight-mode-selftest --appdir target/a3-appdir-repair/AppDir` run on the rebuilt tree: header (command, binary sha256, source revision/mtime) + every check line incl. `definition-parsed.*`; `checks=104 failures=0`, exit 0. |
| `10-final-green.log` | **CURRENT** (replaced by the A3 repair) | Complete outputs of (1) ctest 6/6 incl. `vedit-app-syntax-startup`, (2) `vedit_syntax_kde_selftest`, (3) the same AppDir assertion. Header names command/binary sha256/source revision. |
| `01-build-and-ctest.log` | **SUPERSEDED** | Pre-repair, `resources-temp` mode; contains `definition-parsed.run.tcl PASS  no definition named 'Tcl' …` — the same unreachable combination as the old 09. Kept as history only, not acceptance evidence. |
| `02-ctest.log` | SUPERSEDED | Pre-repair ctest excerpt (2 lines), no command/sha256 header. |
| `03-assemble-appdir.log` | SUPERSEDED | Assembled the old `target/a3-appdir/AppDir`; the VHDL XML changed in the repair (new sha256), so this is not the current payload. |
| `04-appdir-offscreen.log` | SUPERSEDED | Pre-repair AppDir run; contains the same unreachable `PASS no definition named …` class of line. |
| `05-f2-isolation-before.log`, `06-f2-isolation-after.log`, `12-f2-isolation-final.log` | SUPERSEDED | F2 gate before the repair: the positive check only compared names/counts (a shadow cannot be observed that way) and the `UserLeakProbe` fixture was inert (wrong mount level + pinned `XDG_DATA_HOME`). Current evidence: `evidence/ux-batch-2/a3-repair/11-f2-isolation.log`, `12-r2-gate-fails-on-shadow.log`. |
| `07-construction-checks.log`, `08-final-verification.log` | SUPERSEDED | Pre-repair runs (86 checks); the current selftest prints 104 checks (folding + drift guards added). |
| `11-edit-core-rerun.log` | SUPERSEDED | Historical single-test rerun of a concurrent-tree `BAD_COMMAND`; the current `10-final-green.log` runs it inside the complete ctest pass. |
| `13-ctest-final-clean.log`, `14-app-syntax-startup-rerun.log` | SUPERSEDED (historical) | Record the `vedit-app-syntax-startup` SEGFAULT that was later attributed to another lane's dangling `editor_` pointer and fixed. The rebuilt tree passes the test (see `10-final-green.log`; `APP-SHELL-SELFTEST ok=1 checks=312 failures=0` in `evidence/ux-batch-2/a3-repair/10-selfcheck-appdir.log` and the cross-lane note in `a3-repair/00-crosslane.txt`). |

The current repair evidence (R1/R2/R3/R4) lives in `evidence/ux-batch-2/a3-repair/`; this directory keeps
the A3 acceptance logs, with 09/10 regenerated from the repaired tree.
