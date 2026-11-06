# RM-9 — the red/yellow/green dots must cover the WHOLE project

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Coverage Engineer (shared task T20). User words: “现在貌似只在你打开的文件上会显示，其他文件不显示，如果是 FILELIST 导入的貌似根本没地方显示”; the refinement that drove the fix: “刚刚说的点不是‘打开过才有’，是‘正在打开的文件’才有，甚至是‘正在聚焦的文件’才有”.

The root cause had two layers: (1) the project pass was never run on a project
OPEN (only an explicit Lint Project / a save), so unopened files had no verdict;
(2) the lower-layer verdicts were kept for ONE document (`diagnostics_` +
`lowerAnalyzed_`) and cleared by `analyzeFile()` on every tab switch, so a file
lost its dot the moment it was not the active tab. There is now ONE retained
per-file verdict store in the controller, the project open runs the full pass,
and every surface (tree dot, Project-panel Files list, counts, hover source)
reads that same store.

## 1. What changed (by defect)

| # | Where | Before | After |
|---|-------|--------|-------|
| (i) verdicts not retained | `src/workspace/diagnostics/diagnostics_controller.{h,cpp}` | `statusForFile()`/`fileStatuses()` derived from the CURRENT analysis rows; `analyzeFile(B)` cleared `diagnostics_`/`lowerAnalyzed_`, so A answered `Unknown` after the switch. | A per-file retained store (`retainedLowerRows_`, `retainedLowerEvaluated_`, `notAnalyzedReason_`, `projectFiles_`) fed at the SAME collection points as the merged view. `analyzeFile()` no longer clears other files. `statusValueForFile()` is the ONE derivation used by `statusForFile()` and `fileStatuses()`; `mergedDiagnostics()`/`mergedDiagnosticsForFile()`/`composeSessionMessage()` read the same store. |
| (ii) no project-open pass | `src/workspace/diagnostics/diagnostics_integration.cpp` | `startProjectPass()` was reached only from the explicit action and the save trigger. | `adoptProjectFiles()` now calls `startFullProjectAnalysis()`: ONE automatic pass over the whole expanded set at project open (and after every re-root). Explicit click cancels a running automatic pass first. |
| (iii) save re-linted the project | `src/workspace/diagnostics/project_lint_trigger.{h,cpp}` + integration | The save-triggered pass requested every project file (cost grows with the project). | The trigger takes an INCREMENTAL request builder; the integration supplies exactly the saved file. `replaceUpperLayer()` already keeps the rows/coverage of files the pass did not lint, so every other verdict survives. |
| (iv) “no verdict” looked like “no file” | `src/common/file_status.h` (NEW), file tree + Project panel | Only 0..3 existed; an unanalysed file drew NOTHING. | State 4 `NotAnalyzed` = hollow grey ring + tooltip meaning (“Not analysed yet (queued, or excluded because the buffer has unsaved changes)”). Project files are seeded with it while the pass/analysis is pending; `Unknown` (no mark at all) is reserved for files outside every analysed set. |
| (v) filelist-only files had no tree row | `src/workspace/filetree/file_tree_model.{h,cpp}`, `file_tree_widget.*`, `filetree_integration.cpp` | The tree is a directory scan; a path outside the root had no row, so it could not show a dot. | The diagnostics lane publishes the EXPANDED project set (`vedit.diagnostics.projectFiles` + revision); the tree shows the outside-root members in a synthetic last group “From filelist (N)”, with the SAME `FileStatusRole` dots and tooltip meaning. |
| (vi) no status in the Project panel list | `src/workspace/project/project_panel.{h,cpp}` | The Files list painted text only. | A delegate paints the shared mark before the name (`fileStatusAt(row)` + paint-time lookup, visible rows only); the production source reads `vedit.diagnostics.fileStatus` from the top-level window, so `project_integration.cpp` is untouched and no link dependency is added. |
| (vii) per-file repaint | `src/workspace/filetree/file_tree_model.cpp` | `syncFileStatuses()` emitted one `dataChanged` per materialised row on every revision change. | ONE `dataChanged` per LOADED DIRECTORY RANGE. A 1176-row listing now emits 3 signals (measured below), not 1176. |
| (viii) runtime `.f` imports were ignored | `src/workspace/diagnostics/diagnostics_integration.cpp` (`ProjectFileSetPoller`) | The expansion ran once at install with the inputs captured then; a `.f` imported later never joined the analysis set. | The poller watches the published expansion inputs (800 ms; compare filelists/variables) and re-expands on change, including when the change lands during a running expansion. One project model for the panel and the analysis. |
| (ix) type-vocabulary duplication | `src/common/file_status.h` + diagnostics CMake | Three colours/enums were about to be duplicated between surfaces. | ONE shared header (`FileStatus` enum + `fileStatusColor()` + `fileStatusHint()`); the file-tree delegate and the Project panel delegate both consume it. `src/workspace/diagnostics/CMakeLists.txt` is the only CMake file that needed the `../../common` include path (the filetree and project lanes already had it). |
| (x) standalone build break found on the way | `src/workspace/diagnostics/ffi_backends.cpp` | Used `QCoreApplication::translate` without including `<QCoreApplication>`; only the root build got the header transitively. | One include added (the new standalone-buildable test target requires it). |

## 2. Coverage policy and its measured cost

**Policy.** A project OPEN and an explicit Lint Project run the pass over the
WHOLE expanded set (directory scan ∪ imported `.f` lists), skipping ONLY dirty
buffers (their disk bytes are stale). A SAVE re-lints exactly the saved file;
the other files keep their verdicts (they remain valid until edited — the R2
rule). The pass is asynchronous, debounced, single-flight and cancellable, and
switching projects/applying settings cancels it.

**Measured** (the product seam `createFfiProjectLintBackend()` =
`vd_deeplint_batch_*`, the same backend the Lint Project action and the
project-open pass use; generated 1176-file project; probe:
`probe/coverage_cost_probe.cpp`):

```
PASS-COST files=1176 engine=verible elapsed_ms=6647 phase=3 rows=1176 failed=0   # 20-cost-verible-1176.log
PASS-COST files=1176 engine=slang   elapsed_ms=6747 phase=3 rows=0    failed=0   # 22-cost-slang-1176.log
PASS-COST files=1    engine=verible elapsed_ms=20   phase=3 rows=1    failed=0   # 21-cost-verible-save-1.log
```

So a full 1176-file pass costs ~6.6–6.7 s wall time **on the worker/job path
(async)** while a save costs one lint run (~20 ms). That is why the open/explicit
path is full and the save path is incremental: re-linting 1176 files on every
save would multiply the save cost by ~330× for no new verdict (the other files
did not change). The full pass stays cancellable: `cancelProjectPass()` is a
true stop (release + running cleared), exercised by the existing U8 scale probe
and by the rm9 e2e’s explicit-pass leg; a project switch/settings apply cancels
it before clearing the rows.

## 3. The three states as the user sees them

`10-rm9-window.png` (offscreen, the real product window): the Project panel’s
Files list shows `/…/shared/outside.sv` with a **yellow** dot (warnings only) —
that file exists only through the imported `.f`, outside the opened folder; the
tree shows it under the synthetic **“From filelist (1)”** group with the same
mark, next to `a.sv` (**red**) and `b.sv` (**green**). A file in the analysed set
with no trustworthy verdict yet is a **hollow grey ring**, and the tooltip says
which case it is (“Not analysed yet (queued, or excluded because the buffer has
unsaved changes)”). A file outside every analysed set still has no mark.

Ruling on dirty buffers (T20 addendum, ruling 1): a FRESH verdict of the current
buffer wins (red/yellow/green, dirty or not); the hollow/excluded state is used
only when the edit voided the verdict and nothing trustworthy replaced it yet.
An edit voids the UPPER (project-pass) verdict immediately — R2 unchanged.

## 4. Red before / green after

**RED — the new controller test run against the committed HEAD controller
(`01-red-before-controller.log`, 8 failures):**

```
CHECK FAIL rm9.a-retained-after-switch  0            <- the other tab has NO verdict
CHECK FAIL rm9.a-retained-in-map  -1
CHECK FAIL rm9.a-retained-rows-feed-merged  0
CHECK FAIL rm9.count-follows-retained  count=0
CHECK FAIL rm9.pending-a-visible  0                  <- "no verdict" is invisible
CHECK FAIL rm9.pending-b-in-map  -1
CHECK FAIL rm9.e-not-green-after-edit  3             <- a STALE GREEN on an edited file
CHECK FAIL rm9.e-map-says-not-analysed  3
VE-VERDICT-RETENTION-SELFTEST checks=24 failures=8
```

The HEAD controller is byte-identical to the working tree’s controller before
this task (`git show HEAD:…diagnostics_controller.cpp`), so this is the real
“today” behaviour. The red harness is reproducible: it compiles the new test
against the HEAD controller in the pinned image (see §6).

**GREEN — after the change (`02-green-after-controller.log`):**

```
CHECK PASS rm9.a-retained-after-switch  1
CHECK PASS rm9.pending-a-visible  4
CHECK PASS rm9.e-not-green-after-edit  4
CHECK PASS rm9.e-dirty-fresh-verdict-wins  3
VE-VERDICT-RETENTION-SELFTEST checks=24 failures=0
```

## 5. Requirements-driven assertion changes (recorded, not gate-tuning)

The user’s report IS the requirements change, so three existing assertions that
encoded the OLD behaviour were replaced by STRONGER ones (quotes from HEAD in
`30-old-behaviour-quotes.txt`):

1. `n1.a-no-green-dot-after-file-switch` / `n1.a-not-clean-after-file-switch`
   asserted that opening B makes A **disappear** from the map. That is the
   defect. Replaced by `n1.a-retains-error-after-file-switch`,
   `n1.a-retained-error-in-map`, `n1.a-error-after-return`: A keeps its REAL
   Error verdict across the switch and after coming back. The stale-green defect
   N1 originally guarded is still guarded (the retained value is the pre-switch
   ERROR, never a Clean), and the edit direction is asserted separately
   (`r2.edit-voids-lower` / `r2.edit-status-not-analysed` / `r2.edit-never-green`
   then a fresh re-analysis restores the real verdict).
2. `r2.edit-falls-back` / `r2.edit-status-yellow` asserted that an edit falls
   back to the PRE-EDIT lower rows. That is a stale verdict by definition.
   Replaced by “the edit voids the lower verdict too; the file is NotAnalyzed
   (never green) until a fresh analysis lands”.
3. `c8.unmodified-open-included` asserted that every save re-lints the whole
   project. Replaced by `c8.save-pass-is-incremental` (exactly the saved file),
   `c8.save-b-pass-is-incremental`, `c8.other-file-verdict-kept` and
   `c8.unmodified-open-verdict-kept` (the other files keep their verdicts) —
   the T20 contract’s explicit save policy.
4. `selftests/project_lint_e2e.cpp` assumed the FIRST pass it saw after a
   trigger was its explicit one. A project open now starts an automatic pass,
   so the harness waits until the set has arrived AND every automatic open pass
   (including one deferred by the pass timer) is terminal before it triggers
   the explicit run; its terminal wait no longer depends on a serial snapshot
   taken after the pass may already have finished. Not a relaxation: the
   explicit run still asserts the modal dialog (automatic=0, dialogs=1) and the
   save run still asserts no dialog.

## 6. Verification runs (observed)

Commands are run in the pinned image via `scripts/lib-qt.sh`
(`run_in_qt6_container`), offscreen, from `/vedit-build`:

| Test | Command | Observed |
|------|---------|----------|
| controller retention (new, standalone-buildable) | `./target/qt6-build/src/app/workspace-lane-diagnostics/vedit-verdict-retention-selftest` | `checks=24 failures=0` (`02-…`) |
| lint experience (R2 + dots) | `…/vedit-lint-experience-selftest --verible-lint /vedit-build/.mpd/recon/verible/bin/verible-verilog-lint` | `SUMMARY checks=103 failures=0 skips=0` (`04-…`) |
| B3 product wiring | `…/vedit-b3-wiring-e2e` | `SUMMARY checks=76 failures=0 skips=0` (`05-…`) |
| RM-9 product e2e (new) | `…/vedit-rm9-status-dots-e2e --shot …/10-rm9-window.png` | `RM9-E2E checks=33 failures=0` (`06-…`) |
| RM-2 project switch (owns the project/file-set path) | `…/vedit-rm2-project-switch-e2e` | `SUMMARY checks=133 failures=0` (`07-…`); its log shows the new `LINT-PROJECT-OPEN engine=slang files=2 skipped-open=1 automatic=1` line |
| Project lane selftest (the panel compiles against the new delegate) | `…/vedit-workspace-project-selftest --selfcheck …/tests/fixtures/verilog-project` | `PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0` (`08-…`) |
| Project-lint presentation (explicit dialog vs automatic save notice) | `…/vedit-project-lint-e2e` | `SUMMARY checks=11 failures=0 skips=0` (`09-…`) |
| Diagnostics lane selftest | `…/vedit-workspace-diagnostics-selftest --selfcheck` | `VE-DIAGNOSTICS-SELFTEST checks=43 failures=0` (`11-…`) |
| Pass orchestration probe (cancel/dedupe/1k interactivity) | `…/vedit-lint-orchestration-probe` | `LINT-ORCHESTRATION-PROBE ok=1 checks=62 failures=0` (`12-…`) |
| file tree (new group + repaint) | `./vedit-filetree-tests` (isolated standalone build) | `FILETREE-TESTS checks=127 failures=0` (`03-…`), `MEASURE rm9.big-data-changed-emissions 3` for a 1176-row listing |
| red-before | HEAD controller + new test | `failures=8` (`01-…`) |
| cost probe | `probe/build-coverage-cost-probe.sh` then the probe | §2 logs (`20/21/22-…`) |

The RM-9 e2e (33 checks) asserts, on the real shell: the project-open pass
requested the WHOLE expanded set (a.sv, b.sv AND the imported outside.sv);
every project file has a non-zero status; a.sv (never opened) = Error, b.sv =
Clean, the filelist-only outside.sv = Warning; the tree’s “From filelist” group
contains outside.sv with status 2; the Project panel’s Files list has the row,
`fileStatusAt(row) == 2`, and its delegate PAINTS the shared warning colour; a
dirty buffer is NotAnalyzed at once, is excluded from the next explicit pass
(`filesSeen` without it) and is never covered by that pass, while the pass still
covers the other files.

## 7. Bounds / limitations (honest)

- The ZERO-verdict / NotAnalyzed policy needs a project set: a standalone file
  opened without a project has no “pending” row until its analysis starts (it is
  marked pending by `analyzeFile()` when it has no verdict).
- The project-panel mark reads the diagnostics map from the TOP-LEVEL window’s
  properties. A ProjectPanel instantiated outside a shell (standalone lane test)
  paints no marks unless a source is injected via `setFileStatusSource()`; the
  e2e injects the real shell environment.
- The new translatable strings (“From filelist (%1)”, the status hints) have no
  zh_CN entry yet: `translations/vedit_zh_CN.ts` is outside this task’s write
  scope and is being edited by the i18n/manual lane. Under zh they fall back to
  English until that entry lands. Recorded as a follow-up, not a defect.
- One intermediate run of `vedit-rm2-project-switch-e2e` failed on a temporary
  state of ANOTHER lane’s in-flight edit (an undeclared `FakeLintBackend` plus a
  debug loop in that lane’s harness). After that edit settled, the target built
  and passed 133/133 (`07-…`). Several lanes edit these harnesses; the gate’s
  full run is the authoritative pass.
- The automatic project-open pass is DEFERRED (never started) while another pass
  is running, so it cannot replace an explicit Lint Project with a silent
  automatic run. The flip side: a project whose set is re-adopted during a long
  running pass gets its full pass right after that pass, not during it.
- The 1176-file repaint bound is asserted at the MODEL level (dataChanged
  emissions), not by pixels: Qt repaints only visible rows of each emitted range.
- The cost probe measures the batch backend wall time; it does not claim the
  project-open pass is free — it is ~6.6 s of ASYNC work on a 1176-file project,
  cancellable, and it is the price of “every file gets a verdict”.

## 8. Files changed

New: `src/common/file_status.h`, `src/workspace/diagnostics/selftests/verdict_retention_selftest.cpp`,
`src/workspace/diagnostics/selftests/rm9_status_dots_e2e.cpp`,
`evidence/ux-batch-2/rm9-dots/**`.

Changed: `src/workspace/diagnostics/{diagnostics_controller.h,diagnostics_controller.cpp,diagnostics_integration.h,diagnostics_integration.cpp,project_lint_trigger.h,project_lint_trigger.cpp,ffi_backends.cpp,CMakeLists.txt,selftests/lint_experience_selftest.cpp,selftests/b3_wiring_e2e.cpp}`,
`src/workspace/filetree/{file_status_delegate.h,file_status_delegate.cpp,file_tree_model.h,file_tree_model.cpp,file_tree_widget.h,file_tree_widget.cpp,filetree_integration.cpp,tests/filetree_tests.cpp}`,
`src/workspace/project/{project_panel.h,project_panel.cpp}`.
