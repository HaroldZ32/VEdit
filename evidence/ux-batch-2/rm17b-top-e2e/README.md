# RM-17b (T41) — the top scope end to end: the dot now tells the truth, and the per-file layer skips too

Status: **fixed and green.** The end-to-end test (54 checks, 3 scenarios) was RED
on the pre-fix tree (4 failures) and is GREEN on the fixed tree; the five
required regression suites are green. Test:
`src/workspace/diagnostics/selftests/top_scope_e2e.cpp`, registered as
`vedit-rm17b-top-scope-e2e`.

## THE RULING (recorded so the two rulings cannot be read as contradictory)

> While a top is ACTIVE, “excluded by the top scope” is **authoritative** for the
> dot and for whether the file is analysed: it outranks both the upper project
> layer and the lower per-file layer, because the user's own explicit choice was
> “do not analyse these”. This does NOT contradict RM-9 ruling 1 (a fresh verdict
> beats a stale one for a dirty buffer) — that ruling resolves **staleness**,
> this one resolves **exclusion by instruction**, and an instruction outranks a
> verdict.

## 1. Defects found (pre-fix, RED: 54 checks / 4 failures)

Fixture: `top.sv` (`module A` instantiating B), `b.sv` (`module B`), `c.sv`
(`module C`, reachable from neither), `sources.f`; scope `analysed={top,b}`,
`excluded={c}`; `pkg.sv` (`package P`) for the definitions-only rule. The test
drives the REAL shell + real project expansion + real diagnostics lane + the
documented seam `publishTopScope()` (what the module tree calls), and observes
the lint request at the backend boundary. The pre-fix failures and their causes
(product code, not test artefacts):

1. `s1.outside-still-notanalysed`: `c=3` (Clean) while `c-covered=0`,
   `c-merged-rows=0` — the lower per-file layer had analysed C (a zero-row answer
   retained as Clean) and its verdict outranked the scope mark.
2. `s2.excluded-is-hollow` / `s2.still-hollow-after-pass`: `c=2, covered=1` — the
   on-open pass had covered C before the top was named; `setTopScope` did not
   evict it, and `replaceUpperLayer` kept the coverage AND cleared the mark.
3. `s3.package-still-hollow`: `pkg=3` — same lower-layer path as (1).

What already worked (and still does): the project PASS filter — the backend
request never contains C/pkg (`files=2`), A/B get real verdicts, the aggregate
line and the project membership are right, the mark is set when inspected before
the lower layer touches it, and Auto restores C.

## 2. The repair (applied; `10-fix.patch`, `11-fix-summary.txt`)

ONE derivation: the controller stores `scopeExcluded_` (clean paths) while
`topScopeTop_` is non-empty; the publisher's `analysed` half is accepted for the
seam but is NOT stored (with a top, a requested file is analysed exactly when it
is not excluded — a second list could only drift). A private
`isScopeExcluded(file)` is consulted by every path below.

- `statusValueForFile()` (:1225): the excluded set is consulted BEFORE
  `coveredFiles_` and before any lower verdict → excluded ⇒ `NotAnalyzed` with
  the `outside-top:<name>` reason, full stop.
- `fileStatuses()`: skips excluded files in the upper/row/retained loops and
  inserts `NotAnalyzed` for the whole excluded set LAST — the tree map and
  `statusForFile` cannot disagree.
- `setTopScope()` (:1307): clears only the marks this seam set, rebuilds
  `scopeExcluded_` from the excluded list and marks each excluded file (the
  `analysed` parameter is `Q_UNUSED`; see the header note).
- `replaceUpperLayer()` (:1414): a pass that did not lint a file no longer clears
  that file's scope mark (`clearNotAnalyzed` is skipped for excluded files), and
  an excluded file is not covered for status purposes.
- The per-file layer skips excluded files entirely: the LSP request loop, the
  slang `startLints()` loop, and both collectors (`collectLints`,
  `collectLspDiagnostics`) drop a ticket whose file is (or became) excluded — no
  request, no zero-row answer retained as Clean.
- `reparse()` (called by every `setFileSet`) re-applies the active scope's marks
  after its wholesale `notAnalyzedReason_.clear()`.
- The pass filter uses `isScopeExcluded()` (the same derivation).
- Auto (`top` empty) clears the excluded set and the marks; nothing else is
  touched, so the files the scope removed become analysable again.

Diff (vs the pre-fix controller): **95 added / 16 removed / 16 hunks** across
`diagnostics_controller.{h,cpp}` — no other product file changed.

## 3. Green proof (same test source as the red; `04-green-run.log`)

```
CHECK PASS rm17b.s1.excluded-mark                                   c=4
CHECK PASS rm17b.s1.outside-still-notanalysed                       c=4
CHECK PASS rm17b.s1.request-excludes-outside                        top.sv,b.sv
OBSERVED  rm17b.s1.after-scoped-pass c-status-map=4 c-status-direct=4 c-covered=0
          c-merged-rows=0 passFiles=2
CHECK PASS rm17b.s2.control-request-includes-c                      top.sv,b.sv,c.sv
CHECK PASS rm17b.s2.control-c-analysed                              c=2
CHECK PASS rm17b.s2.excluded-is-hollow                              c=4 covered=1
CHECK PASS rm17b.s2.request-excludes-outside                        top.sv,b.sv
CHECK PASS rm17b.s2.still-hollow-after-pass                         c=4 covered=1
OBSERVED  rm17b.s2.after-pass c-status=4 covered=1
CHECK PASS rm17b.s3.package-hollow                                  pkg=4
CHECK PASS rm17b.s3.package-still-hollow                            pkg=4
CHECK PASS rm17b.s3.request-excludes-package                        top.sv,b.sv
VE-RM17B-TOP-SCOPE-E2E checks=54 failures=0
```

The control still proves the test drives a real pass: with NO top, the request
contains `c.sv` and C is analysed (c=2, covered). The four pre-fix failures pass
for the right reason: the dot is hollow (4) even where the upper layer still has
coverage (`covered=1` — the scope outranks it), and the request boundaries
exclude C/pkg.

## 4. Regression suites (all required ones; logs 05-09)

| Suite | Observed |
|---|---|
| `vedit-rm9-status-dots-e2e` | `RM9-E2E checks=33 failures=0` |
| `vedit-filetree-tests` | `FILETREE-TESTS checks=127 failures=0` |
| `vedit-lint-experience-selftest` (with its `--verible-lint` argument) | `SUMMARY checks=103 failures=0 skips=0` |
| `vedit-app --selfcheck` | `APP-SHELL-SELFTEST ok=1 checks=321 failures=0` |
| `vedit-project-lint-e2e` | `SUMMARY checks=11 failures=0 skips=0` |

None of them disagreed with the scope rule. Note for the record: run WITHOUT its
`--verible-lint <path>` argument the lint-experience suite reports `90/0/3`
(three autofix checks SKIP because the tool path is empty) — that is the test's
documented behaviour, not a regression; the CMake registration passes the
argument.

## 5. Bounds / notes

- The CLI pass is a deterministic fake backend (the request boundary is what the
  card asks to assert); the real Rust expansion, shell, sink, pass filter and
  status derivations are the product ones, and the real slang per-file layer is
  what produced the pre-fix green dots.
- Rows of an excluded file that were RETAINED before the top was named are not
  purged from the merged view (the ruling covers the dot and the lint); the dot
  is authoritative and shows `NotAnalyzed`. A future polish could suppress those
  rows; it is not part of this fix.
- Offscreen, no full gate.
- Red-before/green-after on the SAME test source: `01-red-run.log` (54/4) and
  `04-green-run.log` (54/0); no check was weakened.

## 6. Files

- `src/workspace/diagnostics/diagnostics_controller.cpp` / `.h` — the repair.
- `src/workspace/diagnostics/selftests/top_scope_e2e.cpp` — the end-to-end test.
- `src/workspace/diagnostics/CMakeLists.txt` — `vedit-rm17b-top-scope-e2e`
  (offscreen, TIMEOUT 300).
- `evidence/ux-batch-2/rm17b-top-e2e/` — this README, red/green runs, the five
  suite logs, `10-fix.patch`, `11-fix-summary.txt`, `03-checks.log`.
