# B1 repair — evidence (lane B1: module tree + Interface panel)

Repair of lane B1 after the independent review returned **FAIL**
(`evidence/ux-batch-2/review-b1/REPORT.md`), against the frozen contract
`.mpd/plans/ux-batch-2/00-plan.md` §B1.

Write scope actually touched: `src/workspace/module_tree/**` only (sources,
the acceptance test, `CMakeLists.txt`, `README.md`). Nothing under
`src/app/**`, `src/workspace/search/**`, `src/workspace/diagnostics/**`,
`src/editor/**`, `crates/**`, `docs/USER_GUIDE.md` or `translations/**` was
edited.

## Findings → what changed

| Id | Verdict | Where |
|----|---------|-------|
| **H1** project-model watch swallowed the first appearance / a change seen during a run | **fixed** | `module_tree_integration.cpp`: `ModuleTreeHolder::checkProjectModel`, new `ModuleTreeHolder::finishRun`, `ModuleTreeHolder::refresh`, `ModuleTreeHolder::setProjectRoot`, new `projectModelStamp()` |
| **M2** remaining user-visible strings not wrapped in `tr()` | **fixed** (catalogue itself left to the captain) | `module_catalog.cpp`, `module_hierarchy.cpp`, `module_tree_panel.cpp`, `interface_panel.cpp`, `catalog_client.cpp`; lane `README.md` corrected |
| **M3** a skipped test passed with zero checks | **fixed** | `module_tree_catalog_test.cpp` `main`, `CMakeLists.txt` comment |
| **M4** declaration ≠ instantiation not proven | **fixed** (evidence gap, not a code defect) | `module_tree_catalog_test.cpp`: `acceptance.iii.shared-*`; port-column assertion in `acceptance.vi.declaration-cursor` |
| **M5** persisted-project-model path not exercised | **fixed by the H1 tests** | `h1.first-appearance.*`, `h1.in-flight.*` |
| **M6** user-guide sentence | **carry** (docs are outside this lane's scope) | not edited; see REPORT |
| **Nit 7** "one conversion" claim | **fixed** | `module_tree_panel.cpp` `addRow` now fills the row from `jumpFromPosition`; `module_catalog.h` comment corrected |
| **Nit 8** chooser menu seam-only | **carry** | no defect found by the reviewer; still seam-only |
| **Nit 9** guard-label constants | **carry** | production uses the defaults (64 / 20000); not user-facing today |

## Commands and observed results

All commands run inside the pinned image (`vedit-qt6-build:local`), workspace
mounted at `/vedit-build`; the fixture is the in-tree
`src/workspace/module_tree/fixtures/ux-batch-2`, slang is
`build/slang/out/slang`.

| # | Command | Observed | Log |
|---|---------|----------|-----|
| 01 | `vedit-module-tree-catalog-test <fixture> <slang>` **before the H1 fix, with the new tests** | `FAIL: 79 check(s), 12 failure(s)`, exit **1** | `logs/01-red-catalog-test.log` |
| 02 | same with `VEDIT_DEBUG=1` (run sequence diagnostic) | first run = `vd_project_expand_v2(...)` 7 files; the model is never re-read | `logs/02-red-debug.log` |
| 03 | same **after the H1 fix** (intermediate) | `PASS: 79 check(s), 0 failure(s)`, exit 0 | `logs/03-green-catalog-test.log` |
| 08 | same **final tree** (after Nit 7 + the "settles" check) | `PASS: 80 check(s), 0 failure(s)`, exit 0; selftest `PASS: 76 check(s), 0 failure(s)` | `logs/08-green-final-catalog-test.log` |
| 04 | `ctest --test-dir target/qt6-build -R module-tree` | `2/2` passed (`#34` selftest, `#35` catalog test) | `logs/04-selftest-and-ctest-module-tree.log` |
| 05 | catalog test with `/nonexistent/fixture /nonexistent/slang` (M3) | `FAIL catalog.real …` / `FAIL: 0 check(s), 1 failure(s)`, exit **1** (was exit 0 + SKIP) | `logs/05-selftest-m3-counts.log` |
| 05 | `ctest -N` | `Total Tests: 47` (unchanged; no test was removed) | `logs/05-selftest-m3-counts.log` |
| 06 | `lupdate` over the lane's 6 TUs → `/tmp/module-tree-lupdate.ts` (M2) | `Found 72 source text(s)`; **65 are not yet in `translations/vedit_zh_CN.ts`** | `logs/06-lupdate-module-tree.ts`, `logs/06-lupdate-strings.txt` |
| 09 | `bash scripts/build-qt.sh` (final tree) | exit **0**; `100% tests passed, 0 tests failed out of 47`; `#34` and `#35` passed | `logs/09-build-qt-final.log` |
| 10 | `ctest --test-dir target/qt6-build -R module-tree` (final tree) | `2/2` passed | `logs/10-final-module-tree-ctest.log` |
| 07 | `bash scripts/build-qt.sh` (first green, before Nit 7/settles) | exit 0; 47/47 | `logs/07-build-qt.log` |

## H1 before/after, in numbers

* **Before** (`logs/01-red-catalog-test.log`):
  `h1.first-appearance.file-set — fileSet=7` (the root-scan fallback, source
  `vd_project_expand_v2`), `runs 1 -> 1`; `h1.in-flight.pending-observed —
  modelPending=0`, the catalog stayed on the seeded 5-module set and
  `runs 1 -> 2`. Four downstream checks also failed because the tree never
  converged to the project model.
* **After** (`logs/08-green-final-catalog-test.log`):
  `h1.first-appearance.file-set — fileSet=6 modules=5`, source `project model
  (project-model.json)`, `runs 1 -> 2`; `h1.in-flight.pending-observed —
  modelPending=1`, `fileSet=7 modules=6 generation=4`, `runs 2 -> 4`, and
  `h1.in-flight.settles — runs 4 -> 4 pending=0` (one follow-up, no loop).

## Translation strings for the captain's `lupdate` step

`logs/06-lupdate-strings.txt` is the full list of the 72 sources `lupdate`
extracts from this lane, each marked `PRESENT` (already in
`translations/vedit_zh_CN.ts`) or `NEW` (65). The repair did **not** touch
`translations/**`. The captain's step must run `lupdate -locations absolute`
(plan §5.1) so the existing 936 entries keep their locations.

## What is NOT proven

1. The **live project-lane expansion** is not driven: the tests write
   `project-model.json` through the real `ProjectStateStore::save`, which is the
   same file/format the project lane produces, but no project-panel expansion
   runs in the test process.
2. The change token is `mtime_ms|size`; a rewrite with the same size inside the
   same millisecond would be missed. The project lane writes atomically after an
   expansion, so this is a bounded assumption, not a proven invariant.
3. The **root-switch + model-change** combination is not separately tested (the
   existing root-switch checks still pass; the state machine resets its tokens
   on `setProjectRoot`).
4. M4's new checks also pass on the **pre-repair** code (`logs/01`): they close
   an evidence gap, not a code defect - the reviewer had already read the path
   and found it correct.
5. The production `QMenu` chooser (`Nit 8`) is still exercised through the
   injected chooser seam only.
