# B1 repair report (after review FAIL)

- Contract: `.mpd/plans/ux-batch-2/00-plan.md` §B1
- Review being repaired: `evidence/ux-batch-2/review-b1/REPORT.md` (verdict FAIL)
- Write scope touched: `src/workspace/module_tree/**` (+ its CMakeLists, fixture
  untouched, lane README). No `translations/**`, no `docs/USER_GUIDE.md`, no
  `src/app/**`, no `src/workspace/{search,diagnostics}/**`, no `src/editor/**`,
  no `crates/**`.
- Evidence: `evidence/ux-batch-2/b1-repair/` (README + `logs/01..09`).

## H1 — the project-model watch must not swallow the first appearance or a change seen during a run

**Fix (`src/workspace/module_tree/module_tree_integration.cpp`)**

* New `projectModelStamp(stateDirectory)` — `mtime_ms|size` of
  `<state dir>/project-model.json`, empty when absent (the project lane writes it
  atomically, so the token changes when the effective file set may have changed).
* `ModuleTreeHolder` now keeps three pieces of state instead of the two
  "last seen" stamps: `modelStamp_` (last observed token), `runModelStamp_` (the
  token the current/last catalog run actually evaluated — taken immediately
  before `CatalogClient::start`, which loads the persisted model synchronously),
  and `modelPending_` (a change observed while a run was in flight).
* `checkProjectModel()` no longer returns after stamping. `first appearance`
  becomes "token differs from `runModelStamp_`" and, when no run is in flight,
  starts a run; when a run is in flight it sets `modelPending_` instead.
  Idempotence: a token equal to `runModelStamp_` never starts a run (the run
  already read it), so a model that existed before the first run does not cause a
  redundant startup run.
* New `finishRun(bool failed)`, called from `onReady` and `onFailed`: if
  `modelPending_` is set, or the token on disk differs from `runModelStamp_`
  (a write the watch has not ticked for yet), it schedules **exactly one**
  follow-up run (`QTimer::singleShot(0, refresh)`); otherwise it leaves the
  single 1.5 s failure retry in place (moved out of the `onFailed` lambda).
* `refresh()` records `runModelStamp_` and clears the pending flag (the new run
  reads the current model); `setProjectRoot()` clears both tokens and the flag.
* Probe surface for the regression tests: `Q_INVOKABLE refreshNow()` /
  `checkProjectModelNow()` and the properties `catalogRuns`,
  `catalogGeneration` (set in `onReady` to the run that produced the state) and
  `modelPending`. Production never calls the invokables.
* `VEDIT_DEBUG` diagnostics (consistent with the existing lane prints):
  `module-catalog: run source='…' files=N root='…'` in
  `CatalogClient::beginCatalog`, `module-tree: catalog ready/failed …` in the
  holder.

**New regression tests (`module_tree_catalog_test.cpp`)**

* (a) `h1.first-appearance.*` — the state directory starts empty (the normal
  first open), so the first catalog is the root-scan fallback; the test then
  writes a real `project-model.json` (through `ProjectStateStore::save`) whose
  file set differs from the scan (orphan.sv left out) and asserts the catalog
  converges to the seeded set: `fileSet=6`, 5 modules, `orphan` gone, source
  `project model (project-model.json)`, a new run started.
* (b) `h1.in-flight.*` — a run is started (`refreshNow`), a different model is
  written while it is in flight, and the watch is invoked: `modelPending=1`, a
  follow-up run happens (`runs 2 -> 4`, generation 4) and the final tree is the
  written model's set (`fileSet=7`, 6 modules, `orphan` back); then two 1 s watch
  ticks leave the run count unchanged (`h1.in-flight.settles — runs 4 -> 4`), so
  the follow-up is one run, not a loop.

**Observed before/after**

* Before (`logs/01-red-catalog-test.log`): `h1.first-appearance.file-set —
  fileSet=7` (fallback), `runs 1 -> 1`; `h1.in-flight.pending-observed —
  modelPending=0`; the catalog stayed on the seeded 5-module set; `FAIL: 79
  check(s), 12 failure(s)`, exit 1.
* After (`logs/08-green-final-catalog-test.log`): both groups pass;
  `PASS: 80 check(s), 0 failure(s)`, exit 0.

**Not proven:** the live project-lane expansion is not driven (the file is
written through the real store, not by a project-panel run); a same-size rewrite
inside the same millisecond would not change the token; the root-switch +
model-change combination is not separately tested.

## M2 — wrap the remaining user-visible strings in `tr()`

Wrapped with `QCoreApplication::translate("VEdit", …)`:

| File | Strings |
|------|---------|
| `module_catalog.cpp` | `ModuleCatalog::summaryLine` (error prefix, empty state, the summary format, the `⚠ %1` suffix); all five `portWidthFromType` problem texts |
| `module_hierarchy.cpp` | the four guard/marker notes (depth limit, truncation, cycle, missing definition) |
| `module_tree_panel.cpp` | `"nothing to refresh: no catalog client"`, `"\nroot: %1"`, all four `ModuleTreePanel::deliver` statuses (`"%1 → %2"`, no target, rejected, invalid request) |
| `interface_panel.cpp` | all four `InterfacePanel::deliver` statuses, the instantiation-chooser action label `"%1 · %2 · %3"` |
| `catalog_client.cpp` | no-Rust-build, no-project-root, expansion-rejected, no-source-files, catalog-rejected, 30 s timeout, expansion-failed, catalog-failed, `"unknown error"` |

`ModuleTreePanel::rowLabel` markers were already wrapped and stay so.
`describePosition`/`"%1:%2:%3"`/`"%1|%2"`/`"%1 → %2"`-style positional formats are
punctuation/number joins and are intentionally not translated (the enclosing
sentences are).

**Evidence:** `lupdate` over the six lane translation units extracts **72**
source texts (`logs/06-lupdate-module-tree.ts`, list with PRESENT/NEW marks in
`logs/06-lupdate-strings.txt`); **65** are not yet in
`translations/vedit_zh_CN.ts`. Before the repair several of these were raw
`QStringLiteral` and could never be extracted (the review lists the locations);
`lupdate` cannot reproduce that "before" now because the fix was applied in
place, so the before-evidence is the review's finding plus the diff. The batch's
catalogue is not edited here: the captain's `lupdate -locations absolute` +
`lrelease` step (plan §5.1) must pick up the 65 strings.

**README claim:** the lane's `README.md` now states the honest translation
status ("every user-visible string is wrapped … until the captain runs
`lupdate`/`lrelease` those strings render as English"), replacing the earlier
overstatement. The old evidence README
(`evidence/ux-batch-2/b1/README.md` item 2) still says "Every string is
wrapped"; it is an older batch record outside this lane's write scope and is
superseded by this report - the captain should correct or strike that sentence
when integrating.

## M3 — a skipped test must not pass

`module_tree_catalog_test.cpp::main`: a missing fixture or slang now prints
`FAIL catalog.real — …` and `FAIL: 0 check(s), 1 failure(s)` and returns **1**
(the `SKIP … return 0` branch is gone; a `SKIP_RETURN_CODE` ctest property is
not needed because the test only exists in the product build, where the fixture
is in-tree and slang is a build dependency). A green run also asserts
`test.checks-ran` (`g_checks > 0`) before the summary, and the CMake `add_test`
carries a comment stating the policy.

**Observed:** `/nonexistent/fixture /nonexistent/slang` → exit **1**
(`logs/05-selftest-m3-counts.log`); real run → exit 0 with 80 checks.
**Not proven:** the pre-repair exit-0 behavior was not re-run here (the lane is
untracked, so there is no old binary); the review's reproduction
(`0 check(s)`, exit 0) is the before-evidence.

## M4 — prove declaration ≠ instantiation

Added to `module_tree_catalog_test.cpp` after the `top_a` case (kept as the
secondary case): `shared` is catalogued as instantiated twice
(`instantiationsOf(...).size() == 2`), the tree `shared` row is double-clicked,
and the test asserts the file is `shared.sv` (not `top_a.sv`/`top_b.sv`, the two
instance sites), the real KTextEditor cursor is on the module's definition
(`cursor=1:8 expected=1:8`) and the line reads `module shared …`. The port
declaration check now also asserts the **column**
(`acceptance.vi.declaration-cursor — cursor=1:67 expected=1:67`).

**Observed:** passes in both runs; this is an evidence gap closed, not a code
defect (the reviewer had read the path and found it correct).

## M5 — persisted-project-model path

Covered by the H1 tests: every H1 run uses a `project-model.json` written by the
real `ProjectStateStore` (source `project model (project-model.json)` in the
logs), including a file set that differs from the root scan.

## M6 — user-guide sentence

**Carry, not done.** `docs/USER_GUIDE.md` is outside this lane's write scope
(forbidden by the task). The sentence remains to be added by the captain/A2:
"the module catalog has no per-connection position, so Ctrl+double-click targets
the instance statement, not the connected port." The tooltip side is present
and asserted (`acceptance.v.abi-limit-tooltip`).

## Nits

* **Nit 7 — fixed:** `ModuleTreePanel::addRow` now fills the row's file/line/
  column roles from `jumpFromPosition(...)` (`module_tree_panel.cpp`), the same
  helper the interface panel uses; `ModuleCatalog`'s header comment was corrected
  to describe both panels using the helpers (`describePosition` converts for
  display only). Verified by the selftest and the catalog test.
* **Nit 8 — carry:** the production `QMenu` chooser is still exercised through
  the injected seam only; the reviewer found no defect in the menu code.
* **Nit 9 — carry:** the marker row labels still use the compile-time
  `kModuleTreeMaxDepth`/`kModuleTreeMaxRows` while the note texts use the
  effective options; production uses the defaults (64 / 20000), so this is not
  user-facing today.

## Gate results (final tree, `logs/09-build-qt-final.log`)

* `bash scripts/build-qt.sh` → **exit 0**
* ctest: `100% tests passed, 0 tests failed out of 47` (same count as before the
  repair; no test removed), including `#34 vedit-module-tree-selftest` and
  `#35 vedit-module-tree-catalog-test`.
* `vedit-module-tree-selftest`: `PASS: 76 check(s), 0 failure(s)`.
* `vedit-module-tree-catalog-test`: `PASS: 80 check(s), 0 failure(s)`.
