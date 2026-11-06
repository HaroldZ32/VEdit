# B3 re-review — round 3 (after repair round 2)

**Reviewer:** Reviewer (independent)
**Subject:** UX batch 2, lane B3 (diagnostics experience, items 9/10/12 + R2/R3), re-review after
`evidence/ux-batch-2/b3-repair-r2/` (the second repair).
**Contract:** `.mpd/plans/ux-batch-2/00-plan.md` §0.1 (R1/R2/R3, BINDING), §B3; captain ruling C8;
cross-file rename stays NOT CLAIMED.
**Prior verdict:** `evidence/ux-batch-2/review-b3-r2/REPORT.md` — FAIL (N1 false green, N2 pre-subscription
window, N3 backend-switch stale rows, N4 vacuous checks, screenshots, nits).
**Evidence read:** the repair report; `logs/05,06,09,10,11,20,30` (green); `logs/11–17` (red mutations);
all six PNGs (sha256 verified against `logs/21`, visually inspected); the prior review; the frozen contract;
`src/workspace/diagnostics/{diagnostics_controller.cpp,diagnostics_integration.cpp,diagnostics_panel.cpp,
project_lint_trigger.cpp,project_lint_notice.cpp,diagnostic_hover.cpp,README.md}`; the B3 product E2E and the
lint-experience selftest; `src/app/app_shell.{h,cpp}`, `src/app/editor_area.cpp` (open/save seams).
**Risk level:** High (no reachable crash found; a persistent stale/false-clean verdict remains reachable).
**Verdict:** **FAIL** (request changes).

## Assessment

The repair is real, not a test-only story. N1's lower-layer green is now tied to the current analysis: both
`DiagnosticsController::reparse()` and `analyzeFile()`'s new-file-scope branch clear `lowerAnalyzed_` with
`diagnostics_`, `clearProjectDiagnostics()` clears it, and `applySettings()` calls it — so the file-switch red
(`n1.a-not-clean-after-file-switch status=3 merged=0` in `logs/11`) becomes Unknown (`status=0`) in `logs/05`.
N3's stale upper rows are gone (`n3.old-upper-rows-cleared upper=`; red `upper=…a.sv,…b.sv,…rename.sv`), the
switch result is published as `vedit.diagnostics.switch`, and the switch test drives the real settings watcher.
N4 is genuinely repaired: I read `hoverRowIsStale()`/`publishHoverState()` and the E2E assertions; the stale
field is computed from the merged rows, the destroy check reads the published cache, the parent check reads the
real parent, and the R2 test edits B while A stays active — each has a red log (`14`, `15`, `16`, `17`) and none
is `check(..., true)`. The screenshots are now real window-level xcb grabs: 02 shows the dialog over the real
window, 04a/04b show the popup inside the window (the `placePopup()` coordinate bug is visibly fixed), and 05
shows the real file tree with a.sv red / b.sv amber / d.sv and rename.sv green; the previous synthetic
`QTreeWidget` and the dialog-less 02 are gone. Six of the nine carried nits are taken.

I nevertheless fail the lane, because **the N2 pre-subscription window is not closed**: the dirty-flag check in
`ActiveFileWatcher::poll()` runs only for the file that is active at the poll. A covered file that is opened,
edited before the 400 ms poll subscribes its document, and then switched away before that poll is never marked
edited; it keeps its old upper verdict — a green dot for a buffer that now has an error — and C8 keeps the dirty
file out of subsequent passes, so the stale state persists until the user re-activates or saves that file. The
writer's own "no tab-lifecycle signal" note overlooks the shell's `ApplicationShell::fileOpened` open seam
(not a tab-lifecycle signal, but the subscription point; already consumed by the project lane).
N3's row clearing is also incomplete as an operation: `applySettings()` clears the store but does **not** cancel
an in-flight pass, so the old engine's pass keeps running, later writes its rows into `cliDiagnostics_` (hidden
only because `coveredFiles_` was emptied) and refuses a new explicit Lint Project with "already running" until
it ends.

## Per-item status

| Item | Status | Artefact / probe |
|---|---|---|
| **N1** false green | **FIXED** (one test-coverage gap) | Code: `lowerAnalyzed_.clear()` in `reparse()`, `analyzeFile()` new-session branch and `clearProjectDiagnostics()`; `applySettings()` calls it. `logs/11` red `n1.a-not-clean-after-file-switch status=3 merged=0` / `value=3`; `logs/05` green `status=0` / `value=-1`; `logs/09` `n1.old-file-not-covered-after-switch covered-a=0 dot-map=0`. The backend-switch leg of the controller test is insensitive to the `reparse()` clear (the writer's disclosure); the product switch path's mark is cleared by `clearProjectDiagnostics()`, so this is a coverage gap, not a product hole (see M1). |
| **N2** pre-subscription window | **PARTIALLY FIXED** | The active-at-first-poll case is fixed: `ActiveFileWatcher::bufferModified()` + the path-change `markFileEdited()`; `logs/12` red `n2.first-edit-before-poll-invalidates upper=…d.sv…` vs `logs/09` green `upper=…a.sv,…b.sv,…rename.sv`. The switched-away case is not: see High finding N2-r. No test covers it. |
| **N3** backend-switch stale rows | **PARTIALLY FIXED** | Rows: `applySettings()` → `clearProjectDiagnostics()` + `setFileSet({})`; `logs/13` red `upper=…a.sv,…b.sv,…rename.sv`, `covered-a=1 dot-map=3`; `logs/09` green `upper=`, `covered-a=0`. In-flight pass: **not cancelled** — `applySettings()` never calls `cancelProjectPass()`, while `followProjectRoot()` does; see Medium finding N3-p. |
| **N4** vacuous checks | **FIXED** | `hover.stale-detects-cleared-row` (real `hoverRowIsStale()`, red `stale=0` / green `stale=1`), `destroy.no-stale-pointer` (real published cache, red `diagnostic=1` / green 0), `hover.parent-is-active-view` (real parent, red `parent=0`), `r2.background-tab-invalidated` (real non-active edit, red `logs/17`). Spot-checked code and logs; no check is a literal `true`. Bounds: `hover.probe-off-mouse-path` has no recorded red (argued from the mechanism; the unit `autofix.async-*` is the red), and `destroy.actions-clean`'s green run passes through the no-popup branch (`alive=0`); its red is not recorded (16 dies before it). |
| **Screenshots** | **FIXED** | 02: 1400×900 screen grab, dialog visible over the real window (previous 02 had none); 04a/04b: 1200×800 window grabs, popup inside the window at the cursor (the old double-offset is gone); 05: 1200×800 window grab of the real `FileTreeWidget` with a.sv red, b.sv amber, d.sv/rename.sv green (previous 05 was a 3-row synthetic tree); `logs/20` 61/61 and `screenshot.file-tree-ready rows=1 statuses=4`. 03 remains a widget grab and 01 is a dialog-region crop (the table says so; the prose "six window-level" overstates). The 「修复」 tofu is a disclosed Xvfb CJK-font bound; it does not undermine the window/placement evidence but does not visually prove the label (see findings). |
| **Nits** | **SIX FIXED / THREE NOT FIXED** | Fixed: panel double count, `AUTOFIX-APPLIED applied=0`, C8 empty-pass message, double reparse, stale hover across a project switch, README `.rules.verible_lint` claim. Not fixed: split-pane fix target, server-hover cross-view placement, automatic-trigger "no project file set" during load (details below). |

## Ranked findings

### High

**N2-r. The pre-subscription window survives for an edited tab that is switched away before the first poll.**
- **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `ActiveFileWatcher::poll()` (the
  path-change branch) and `ActiveFileWatcher::trackDocument()`; `DiagnosticsController::markFileEdited()`
  (`src/workspace/diagnostics/diagnostics_controller.cpp`); the open seam `ApplicationShell::fileOpened`
  (`src/app/app_shell.h`), which the diagnostics lane does not subscribe.
- **What:** `poll()` enumerates every tab and subscribes new documents, but the dirty-flag check
  (`bufferModified(widget)`) runs only for the *currently active* path. A file covered by the project pass,
  opened and edited before that first poll, and switched away from before the poll is subscribed after the
  edit (the `textChanged` was emitted with no listener) and is never marked edited: the active path is another
  file, so the path-change branch checks that file. The covered file keeps its old upper verdict; C8's
  `modifiedOpenFiles()` excludes it from every pass, and `replaceUpperLayer()` keeps its coverage, so the stale
  verdict persists until the user re-activates the tab (the next poll's dirty check) or saves it (the
  save-triggered pass re-lints from disk). The writer's note "the editor area has no tab-lifecycle signal"
  overlooks `ApplicationShell::fileOpened`: not a tab-lifecycle signal, but the documented ONE open seam (already
  used by the project lane) and the natural place to subscribe.
- **Why it matters:** this is the original R2 defect in a narrower ordering: requirement R2 says an edited
  file's upper result is void and falls back to the lower layer. Here a file with an error still shows the
  pass's green dot in the file tree and its old rows in the diagnostics panel — a persistent false clean (or
  stale red) produced by normal use, not a transient.
- **Exact user sequence:**
  1. Run Lint Project (or save a file) so the project pass covers `d.sv` clean; the tree shows `d.sv` green.
  2. Open `d.sv`; the 400 ms `ActiveFileWatcher` has not run, so its document is not subscribed.
  3. Within that window, type a change that introduces an error (the document becomes modified) and switch to
     another tab (Ctrl+Tab or a click) before the next poll.
  4. The next poll subscribes `d.sv` after the edit; it only consults `isModified()` for the newly active
     file, so `d.sv` is never marked edited.
  5. `d.sv` stays covered: the tree keeps its green dot, the diagnostics list keeps the old pass rows, and a
     subsequent Lint Project skips the dirty file (C8) — the stale state lasts until `d.sv` is re-activated or
     saved.
- **Direction:** subscribe on `ApplicationShell::fileOpened` (call `trackDocument()` for the opened document,
  and `markFileEdited()` if it is already modified) rather than only in the poll; or, at minimum, check
  `bufferModified()` for every newly tracked document in `trackDocument()`, not just the active path. Add the
  regression: open a covered file, edit it and switch away in the same event-loop turn, assert its upper layer
  is gone (the current `n2` test keeps `d.sv` active, so it cannot catch this ordering).

### Medium

**N3-p. A settings/backend switch clears the rows but does not cancel a running project pass.**
- **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::applySettings()`;
  `src/workspace/diagnostics/diagnostics_controller.cpp`, `clearProjectDiagnostics()`,
  `cancelProjectPass()`, `pollProjectPass()`; contrast `followProjectRoot()`, which cancels first.
- **What:** `applySettings()` calls `clearProjectDiagnostics()` and `setFileSet({})`, but neither cancels a
  pass that is already running. `clearProjectDiagnostics()` clears `passRequest_`, `passFiles_`,
  `passLintedFiles_` and `pass_`, but leaves `passRunning_`/`passJob_` alive. The old engine's job keeps
  running; when it finishes, `pollProjectPass()` calls `replaceUpperLayer(passLintedFiles_ = {}, oldRows)`,
  which writes the old engine's rows into `cliDiagnostics_` (uncovered, hence not displayed) and republishes the
  pass. Until the old job ends, `startProjectPass()` refuses a new explicit pass with "a project lint pass is
  already running" (status bar "Project lint did not start: …"), and an old-pass failure shows "Project lint
  failed: …" after the switch. The N3 block runs only after the previous pass has published its upper rows
  (`n2.file-covered-before-open`), so no pass is in flight at the switch; it never starts one and changes
  settings mid-pass, so it cannot catch this.
- **Why it matters:** the switch's comment promises "a stale row can never be shown under the new backend's
  name"; the rows are indeed hidden by the coverage gate, but the store and the pass state are stale and the
  user's next explicit Lint Project is wrongly refused. It is the operational half of the N3 class, and the
  fix is one call in a place the codebase already does correctly (`followProjectRoot()`).
- **Direction:** call `cancelProjectPass()` in `applySettings()` before `clearProjectDiagnostics()` (or make
  `clearProjectDiagnostics()` cancel), and add a regression that starts a slow fake pass, changes settings
  mid-pass, and asserts `projectPassRunning()` is false, no old rows are stored, and a fresh pass starts.

### Minor

**M1. The N1 test's backend-switch leg is not evidence for the `reparse()` clear (disclosed).**
- **Where:** `src/workspace/diagnostics/selftests/lint_experience_selftest.cpp`, the N1 block;
  `DiagnosticsController::clearProjectDiagnostics()`/`reparse()`.
- **What:** the leg calls `clearProjectDiagnostics()` + `setFileSet({})`; `clearProjectDiagnostics()` clears
  `lowerAnalyzed_` itself, so the leg passes under the mutation that removes the `reparse()` clear
  (`logs/11` `n1.a-not-clean-after-backend-switch status=0`). The product switch path is covered by
  `clearProjectDiagnostics`, so this is defence-in-depth, not a product hole — but the item-level red covers the
  `analyzeFile()` clear, not the `reparse()` clear, and the `project.reparse` command path (a real user command)
  has no regression; the table's "every N item was observed red" should not be read as covering every clear/leg.
- **Direction:** add a direct `reparse()` leg (analyze an errored file, call `reparse()`, assert not Clean) and
  state the backend-switch leg's insensitivity in the evidence table.

**M2. Two screenshots are not window-level, and the report's prose overstates the set.**
- **Where:** `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp`, `shoot(panel, "03-diagnostics-panel.png")`
  (a `QWidget::grab()`), `shootScreen("01-…", dialog->frameGeometry())` (a dialog region); repair report §
  "Screenshots — real window-level captures".
- **What:** 03 is a widget render and 01 is a dialog crop, not window-level captures; the table itself says
  03 is the widget and accepted, but the prose says "six ... window-level". The task's specific items (02,
  04a/b, 05) are genuine. The 「修复」 button renders as two tofu boxes in 04a (no CJK UI font in the Xvfb
  image); the report discloses it, the button is present, and `hover.fixable-after-async-probe fixable=1`
  plus the source label carry the function, so the placement/window-level evidence stands but the label text is
  not visually proven.
- **Direction:** correct the prose/table to "02/04a/04b/05 window-level; 03 widget; 01 dialog region"; keep the
  tofu bound; render the UI with a CJK font if the label itself is to be evidenced.

**M3. Two product checks remain weaker than the report suggests (not vacuous, but not red-proven).**
- **Where:** `b3_wiring_e2e.cpp`, `destroy.actions-clean` (green passes with `alive=0`; its red run 16 dies
  before it) and `modal.counter-detects-reshow` (`dialogsShown` present and ≥1 only); the substantive proofs are
  the parent check/red 16 and the unit `modal.reshow-counted`.
- **What:** `destroy.actions-clean` is no longer `true` and can fail (live popup mis-parented/stale), but its
  green path exercises only "no popup"; `modal.counter-detects-reshow` still cannot detect a re-show that does
  not increment (the unit does). Neither is a N4 non-failing check; both are weaker product evidence than the
  surrounding text implies.
- **Direction:** make `destroy.actions-clean` force a live popup and assert its parent/cache, or rely on the
  unit/parent red and say so; keep the unit for the reshow counter.

## The two required answers

**(a) Can a user still see a false-clean or stale diagnostic state by normal use?**
**Answer: not a defensible "no" — yes, a user can.** The N1 lower-green after a file switch, a backend/settings
switch, a project switch and a reparse is fixed; the N3 upper rows are cleared. But the **N2-r** sequence above
leaves a file that was edited before its first watcher subscription and switched away before the 400 ms poll with
its old covered verdict: the tree keeps a green dot and the panel keeps the old rows until the file is
re-activated or saved. The **N3-p** path additionally leaves a running pass uncancelled (wrong "already running"
refusal and stale store rows), though those rows are not displayed. Therefore the required defensible "no" is
not available.

**(b) Can an automatic save-triggered lint ever open a modal?**
**No.** `BackendWiring::reactToFinishedPass()` selects `ProjectLintNotice::Mode::Automatic` when
`DiagnosticsController::lastPassWasAutomatic()` is true, and `ProjectLintNotice::present()` calls `showDialog()`
only for `Mode::Explicit`; the automatic branch only updates the clickable status-bar notice. The product run
shows `presentation automatic=1 dialogsBefore=3 dialogsAfter=3 visible=0` and `modal.automatic-never-shows`
(`logs/09`). An already-open explicit dialog is left alone (it is not re-opened), which does not violate
"the automatic run never pops a modal".

## Screenshots — judgment

- **02-window-with-dialog.png** — genuine: `shootScreen(primaryScreen->geometry())` after the ELAB dialog is
  visible; the 1400×900 image shows the dialog over the real window. The previous "no dialog" is fixed.
- **04a/04b** — genuine window-region grabs (1200×800, `shell->frameGeometry()`); the popup is inside the window
  at the cursor; the old `placePopup()` double-offset is visibly gone. The 「修复」 label is two tofu boxes
  (disclosed Xvfb font bound); the button is present and `hover.fixable-after-async-probe fixable=1` proves the
  branch. This does not undermine the window/placement evidence, but it does not visually prove the label text.
- **05-file-status-dots.png** — genuine: 1200×800 window grab of the real `FileTreeWidget` (project1, a.sv red,
  b.sv amber, d.sv/rename.sv green); the previous 3-row synthetic `QTreeWidget` is gone; `logs/20`
  `screenshot.file-tree-ready rows=1 statuses=4`.
- **03** remains a widget grab and **01** is a dialog region (the table says so; the "six window-level" prose
  does not). `logs/21` sha256 verifies all six from the workspace root.

## Nits — status

| Nit | Status | Artefact / probe |
|---|---|---|
| Panel prints the merged count twice | **FIXED** | `DiagnosticsPanel::refreshFromController()` only appends when the message carries none; screenshot 03 shows `diagnostics 3` once. |
| `AUTOFIX-APPLIED … applied=0` before the failure | **FIXED** | `BackendWiring::applyDiagnosticFix()` prints `AUTOFIX-FAILED … applied=0`; `autofix.replace-refused` in `logs/05`. |
| C8 empty-pass message | **FIXED** | `DiagnosticsController::startProjectPass()` now names the modified-buffer reason. |
| Double reparse after Open Folder | **FIXED** | `ProjectFileSetPoller::poll()` no longer calls `setFileSet()` when the sink exists; one `PROJECT-ROOT-FILES` line in `logs/09`. |
| Stale hover popup across a project switch | **FIXED** | `BackendWiring::followProjectRoot()` hides, clears and republished the hover state; code. (No product test; the E2E's root check covers only the rows.) |
| Split-pane fix target | **NOT FIXED** | `BackendWiring::applyDiagnosticFix()` still uses `activeView()`/`hoverFilePath_` without requiring `pane->currentFilePath() == hoverFilePath_`; a focus change inside the popup's lifetime can apply a fix computed for A to B's buffer. Carried as a Nit (narrow, no fixture). |
| Server hover cross-view | **NOT FIXED** | `BackendWiring::renderHover()` still does not `adoptHoverView()`; an LSP hover answer after a tab switch can be placed against the new view in a popup parented to the old one. Carried as a Nit. |
| Automatic trigger's "no project file set" during load | **NOT FIXED** | `ProjectLintTrigger::fire()` still sinks "no project file set" when the request is empty; the explicit `lintProject()` message is fixed, the automatic sink is not. Carried as a Nit. |
| README `.rules.verible_lint` overclaim | **FIXED** | `src/workspace/diagnostics/README.md` now states the C ABI does not expose the discovered rules file. |

## Not weakened

- **No E2E check deleted or renamed away:** the previous round's log (`b3-repair/logs/09`, 48 checks) vs the
  current 60; the check-name diff is additive (12 new names: `n1.old-file-not-covered-after-switch`, `n2.*`,
  `n3.old-upper-rows-cleared`, `switch.*`, `r2.background-tab-*`, `hover.stale-detects-cleared-row`); no old
  name disappeared. The R2 test was strengthened (edits a genuinely non-active tab; `r2.edit-was-background`),
  and `destroy.no-stale-pointer` now reads the real published state.
- **Lint-experience suite:** 92 checks in `b3-repair/logs/06` → 98 in `logs/05` (six new `n1.*`); no old check
  removed; the `save.seam-*` checks are present (the old interleaved-sonnet split is gone).
- **Other suites unchanged:** API selftest 16/16 (I reproduced it locally with the real
  `verible-verilog-lint`), project-lint 11/11, rename 21/21 with 0 skips and 1 NOT-CLAIMED, official
  `scripts/build-qt.sh` 50/50 ctest, `M0-SMOKE ok=1`, `EXIT=0` (`logs/30`). ctest count did not decrease.
- **Report-table inaccuracy (minor):** the repair's "Observed before" column says 98 (lint) and 60 (E2E); the
  previous round's logs are 92 and 48. The additive conclusion ("did not decrease") is correct, but the before
  numbers do not match the prior round's artefacts.
- **Coverage gaps, not relaxations:** the N1 backend-switch leg cannot fail for the `reparse()` clear
  (disclosed); `hover.probe-off-mouse-path` has no recorded red (the unit is the red); `destroy.actions-clean`'s
  red is not recorded (16 dies before it); the N3 test has no in-flight-pass leg.

## Declined to judge

- `project.reparse` not clearing the upper project-pass rows — prior decline; it is an explicit command that
  does not run a project pass, and the current pass result is still the latest project analysis (the lower mark
  *is* cleared, so the N1 class is fixed).
- A failed LSP refresh keeping the last lower verdict — a failed refresh is not a cleared analysis; no
  requirement, transient.
- The in-flight pass's rows being stored but not displayed — I judge the missing cancel / wrong refusal (N3-p),
  not a displayed stale row; `coveredFiles_` hides the rows from every consumer.
- The 16-red run dying after `hover.parent-is-active-view` — the mutation is not the shipped product; the
  failing check is recorded; not treated as a reachable product crash.
- The tofu 「修复」 as a product font defect — the screenshot claim under review is window-level/placement; the
  label is proven by the source and the unit check, and the Xvfb font bound is disclosed.
- The save-before-first-poll variant — the save-triggered pass re-lints the file from disk; the stale window is
  bounded by the pass (the failed-pass case falls under the failed-refresh decline).
- The background-pane programmatic document edit (the R2 test's own path) — not normal mouse use; the R2 test's
  non-active tab in the same pane is the user-reachable case.
- The diagnostics panel listing rows for all files — intended (one global merged view).
- Note/info rows → clean/green — the contract specifies red/yellow/green for error/warning/clean.
- The `destroy.actions-clean` live-popup branch not being exercised in the green run — noted as M3; the check
  is not `true` and the parent check/red 16 covers the parent property.
- External disk edits (`git checkout`) leaving the old upper verdict — no requirement/trigger in this lane; out
  of the changed surface.
- The modal dialog being `setModal(true)+show()` rather than `exec()` — modal and readable; carried from the
  prior review.

## Bounds of this re-review

- **Read:** the repair report and the logs listed above, the six PNGs (sha256 matched and visually inspected),
  the prior review, the contract, the diagnostics controller/integration/panel/trigger/notice/hover source and
  README, the B3 E2E and lint-experience tests, the shell `fileOpened` seam and `EditorPane::openFile`.
- **Not fully read:** the whole 107 kB `diagnostics_integration.cpp` (targeted regions plus callers), the whole
  controller, every line of the test binaries, other lanes' uncommitted diffs.
- **Could not run:** the official `scripts/build-qt.sh`/ctest and the xcb smoke (the container logs are
  accepted as evidence, not independently reproduced). On this host the GUI binaries (`vedit-b3-wiring-e2e`,
  `vedit-lint-experience-selftest`) still hang at `ApplicationShell`/editor startup under offscreen (with the
  AppDir `libtree-sitter` on `LD_LIBRARY_PATH`); `probe-b3-e2e.log` stops after `product.fixture`, and
  `probe-lint-exp.log` produced no checks before the 120 s timeout. I **did** reproduce
  `vedit-b3-repair-api-selftest` 16/16 with the real verible lint (`probe-api2.log`). The mutation runs and the
  pre-fix build were not reproduced; their logs are self-recorded.
- **Workspace state:** a mixed uncommitted tree; I attribute no other lane's changes to B3.

## Verdict

**FAIL (request changes).** N1's lower false green, N3's upper rows, N4, the screenshots and six nits are
genuinely fixed, and the red-before-green evidence is real for those items; but the N2 pre-subscription window
is only closed while the edited tab remains active at the first poll — the exact sequence above leaves a
persistent stale/false-clean upper verdict, which fails requirement R2 and the lane rule — and the
settings/backend switch still leaves an in-flight pass uncancelled (a wrong "already running" refusal and stale
store rows, though not displayed). The lane needs one more repair round for N2-r and N3-p (and the carried
nits).
