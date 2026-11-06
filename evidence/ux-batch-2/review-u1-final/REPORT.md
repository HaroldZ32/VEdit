# U1 — diagnostics N2/N3 repair — independent review (T8)

Reviewer: reviewer-1 (read-only lane; no product code and no evidence of the writer
was edited by this review). Repair reviewed: T1, `evidence/ux-batch-2/u1-final/`.
Contract: `.mpd/plans/ux-batch-2/00-plan.md` §0.1 (R2/R3) and §4.B3; assignment:
`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U1; prior findings:
`evidence/ux-batch-2/review-b3-r3/REPORT.md` (High N2-r, Medium N3-p, three carried
nits).

Risk level: **Low-Medium** — both state bugs are genuinely closed and the fix is
covered by regressions that provably fail on the pre-fix source; the carried risks are
the known latent SIGPIPE flakiness of the wiring selftest and one stale evidence hash
(both disclosed / attributable to other lanes).

## 1. Result

**PASS.** N2 (the pre-subscription window, including the switched-away ordering) and
N3 (in-flight pass across a backend/settings switch) are both fixed in the product
paths, the three nits are implemented and honestly disclosed as lacking dedicated
regressions, R2 layering and R3 automatic-pass behaviour are intact, and the new
regression checks are real: the red run fails exactly the four fix-dependent checks on
the pre-fix source, and all 70 U1 checks pass in my own run.

## 2. What I ran (independent re-derivation)

Test binaries were built by the lanes in the shared tree; I ran them (no full build,
no relink — per the shared-build-tree notice). Logs in this directory:

| run | command | observed |
|---|---|---|
| three targeted suites | `ctest --test-dir target/qt6-build -R '^(vedit-b3-wiring-e2e\|vedit-lint-experience-selftest\|vedit-rename-e2e)$'` (pinned image `vedit-qt6-build:local`) | **3/3 Passed** (0.50 s / 1.16 s / 6.90 s) — `00-ctest.log` |
| wiring, direct | `vedit-b3-wiring-e2e` | **71 PASS, 0 FAIL**, 0 skips; process exited **141** (SIGPIPE) after the last check — `10-direct-runs.log`. Single re-run: `SUMMARY checks=71 failures=0 skips=0`, exit 0 — `12-rerun.log` (transient per the build-tree notice; see A1) |
| lint-experience | `vedit-lint-experience-selftest --verible-lint …` | `SUMMARY checks=98 failures=0 skips=0` — `12-rerun.log`; without the flag it is 85 checks + 3 skips because the autofix block is skipped (pre-existing ctest definition, unrelated to U1) — `11-lint-experience-full.log` |
| project lint E2E | `vedit-project-lint-e2e` | `checks=11 failures=0 skips=0`; `product.automatic-no-dialog before=1 after=1`, `product.automatic-pass-was-automatic` PASS — `10-direct-runs.log` |
| product seam | `vedit-diagnostics-product-seam` | `checks=16 failures=0 skipped=0` — `10-direct-runs.log` |
| rename E2E | `vedit-rename-e2e --verible-ls …` | `checks=21 failures=0 skips=0 not-claimed=1` — `10-direct-runs.log` |

Test-source provenance: `b3_wiring_e2e.cpp` now hashes `7d591ab0…` (mtime 10:26), not
the `2d95bd5d…` quoted in the U1 README: the U2b lane added one check
(`hover.fix-button-label-english`) after the U1 report. The U1 check set is unchanged —
U1's green log has 70 names, mine has the same 70 plus exactly that one U2b addition —
so the U1 red/green evidence still applies to the U1 checks (A2).

## 3. The five required items

### 3.1 N2 — the pre-subscription window (High N2-r) — FIXED

Two independent subscriptions now close the hole, in
`src/workspace/diagnostics/diagnostics_integration.cpp`:

- **Open seam:** `ActiveFileWatcher` connects `ApplicationShell::fileOpened` to
  `trackOpenedPath(path)`, which walks the panes/tabs and calls `trackDocument()` for
  the opened document *before* the user's first keystroke. The seam is the documented
  single open path (`EditorPane::openFile` → `EditorArea::setOnFileOpened` →
  `ApplicationShell::fileOpened`), so every open route lands there.
- **Dirty check for every newly tracked document:** `trackDocument()` calls
  `bufferModified()` for each document it tracks for the first time (both the
  KTextEditor `Document::isModified()` and the `QPlainTextEdit` branch) and calls
  `controller_->markFileEdited(path)`. The poll's old "active path only" dirty check
  is still there as a third fallback; `noteDocumentEdited()` continues to invalidate
  any background tab immediately.

`markFileEdited()` records the edit and, if the file was covered, removes it from
`coveredFiles_`, drops its project rows (`dropUpperRows`) and invalidates the view —
so the file falls back to the lower layer at once (the per-file merge in
`mergedDiagnostics()` / `mergedDiagnosticsForFile()` / `displayDiagnostics()` keys the
decision on `coveredFiles_`).

Reproduction and evidence:

- The regression sequence in `b3_wiring_e2e.cpp` (`n2r.*`) is exactly the review's
  ordering: `e.sv` is a clean, covered project file; the block opens it, edits it and
  calls `pane->setActiveTab(a.sv)` in one synchronous turn (no event loop), then
  asserts `n2r.e-switched-away-before-poll` (active is `a.sv`) and waits for the upper
  layer to drop. My run: `n2r.e-covered-before-open` PASS (upper contains `e.sv`),
  `n2r.e-is-dirty` PASS, `n2r.e-switched-away-before-poll` PASS,
  `n2r.edit-before-poll-invalidates` PASS (upper no longer contains `e.sv`),
  `n2r.e-no-longer-green` PASS (`status=0` — no stale green dot).
- "Not excluded from later passes": the merge exclusion is removed immediately by
  `markFileEdited` (the file shows its lower rows), and after the buffer is saved it
  is no longer in `modifiedOpenFiles()`, so the save-triggered pass includes it and
  `replaceUpperLayer` re-covers it fresh. The suite's `c8.saved-file-included`,
  `c8.saved-b-now-included` and `c8.dirty-buffer-excluded` checks passed in my run;
  while the buffer is dirty it is still skipped by the pass — that is C8's documented
  design (the disk is stale), and the review accepted it; the defect was the *stale
  upper verdict*, which is gone.
- Red proof: `logs/10-red-before-fix.log` (same 70-name test set) fails exactly
  `n2r.edit-before-poll-invalidates` (`upper=…e.sv…`) and `n2r.e-no-longer-green`
  (`status=3`) while `n2r.e-covered-before-open` passes first — so the failure is the
  stale covered verdict, not a fixture artefact.

### 3.2 N3 — the in-flight pass across a switch (Medium N3-p) — FIXED

`BackendWiring::applySettings()` now calls `controller_->cancelProjectPass()` when
`projectPassRunning()`, **before** `clearProjectDiagnostics()` and `setFileSet({})` —
the same order `followProjectRoot()` already used. `DiagnosticsController::
cancelProjectPass()` cancels and releases the backend job, sets
`pass_.phase = Cancelled`, clears the current row, and sets `passRunning_ = false`,
`passJob_ = 0`; `pollProjectPass()` early-returns on `passJob_ == 0`, so the old
engine's job cannot write rows after the switch, and `startProjectPass()` no longer
refuses the next explicit run as "already running".

Evidence:

- The suite holds the next fake pass RUNNING (`FakeProjectLint::holdNextPass`,
  `poll()` pending until cancelled) and then rewrites the real `<stateDir>/settings.toml`
  (`tools.lint_engine = "verible"` — a genuine slang→verible switch, visible in my run
  as `LINT-PROJECT engine=slang` before and `SWITCH … lint=verible-verilog-lint` after).
  My run: `n3p.pass-in-flight-before-switch` PASS (`starts=1 running=1`),
  `n3p.in-flight-pass-cancelled` PASS (`cancels=1 running=0`),
  `n3.old-upper-rows-cleared` PASS (`upper=`), `n3p.new-explicit-pass-accepted` PASS
  (`starts=1 running=1`, status `Linting project with verible-verilog-lint…`).
- Red proof: the same checks fail pre-fix — `n3p.in-flight-pass-cancelled`
  (`cancels=0 running=1`) and `n3p.new-explicit-pass-accepted` (starts=0, the
  product's "a project lint pass is already running" refusal).
- The old job's rows: after `release(job)`/`passJob_ = 0` the timer-driven
  `pollProjectPass()` has no job to poll, so no later write is possible; the test's
  subsequent wait would surface a late upper row, and `n3.old-upper-rows-cleared`
  passed.

### 3.3 The three carried nits — implemented, and honestly disclosed as untested

- **(a) split-pane fix target** — `BackendWiring::applyDiagnosticFix()` now returns
  unless `pane->currentFilePath() == hoverFilePath_`, so a focus move during the
  popup's lifetime cannot apply a fix computed for file A to file B.
- **(b) server-hover cross-view placement** — `BackendWiring::renderHover()` calls
  `adoptHoverView(view)` when the active view is not the popup's current view, before
  creating/placing the popup (so a late answer after a tab switch is parented to the
  active view, not the old hidden one).
- **(c) automatic trigger during load** — the save-trigger result sink returns
  silently when `projectFiles_.isEmpty()` while a root is known (the same "pending"
  predicate the explicit path uses), instead of blaming the user with "no project
  file set".

The U1 README's §4 discloses that (a), (b) and (c) have no dedicated regression
(the fake backends cannot produce those conditions deterministically) and states they
are compile-verified plus covered by the existing hover/R2 and `c8.*` checks. That
satisfies "fixed or honestly disclosed as unfixed": they are fixed, and the missing
regression coverage is stated rather than implied.

### 3.4 R2 layering still holds — VERIFIED

- One verdict per file, project over module: `displayDiagnostics()`,
  `mergedDiagnostics()` and `mergedDiagnosticsForFile()` all iterate the upper layer
  only for `coveredFiles_` and skip the lower rows for those files; keys are deduped,
  so no file can contribute two layers.
- One count everywhere: `composeSessionMessage()` uses `mergedDiagnostics().size()`
  (the old double count is gone); the diagnostics panel (`refreshFromController`)
  iterates `mergedDiagnostics()`; the hover uses `mergedDiagnosticsForFile()`.
- The file-tree dot agrees: `BackendWiring` feeds `controller_->fileStatuses()` to the
  tree model (`FileStatusRole`) and `FileStatusDotDelegate` paints that role;
  `fileStatuses()` derives the per-file status from the SAME `coveredFiles_` /
  `cliDiagnostics_` / `diagnostics_` sets (green covered-clean, red error, yellow
  warnings-only, no dot = never reported). `n2r.e-no-longer-green` is the end-to-end
  proof that the dot follows the invalidation (`status=0`).
- The lint-experience suite's R2 block passed in my run:
  `r2.upper-overrides-lower`, `r2.dot-red`, `r2.dot-yellow`, `r2.dot-green`,
  `r2.dot-none`, `r2.dot-colors`, `r2.merged-count`, `r2.edit-drops-upper`,
  `r2.edit-falls-back`, `r2.edit-status-yellow`, `r2.repass-recovers`,
  `count.session-uses-merged` (`SUMMARY checks=98 failures=0` with `--verible-lint`).
- The wiring suite's `r2.background-tab-view` / `r2.edit-was-background` /
  `r2.background-tab-invalidated` passed in my run.

### 3.5 R3 automatic pass after save — VERIFIED

- **Silent:** `BackendWiring::reactToFinishedPass()` chooses
  `ProjectLintNotice::Mode::Automatic` when `lastPassWasAutomatic()` is true, and
  `ProjectLintNotice::present()` calls `showDialog()` only for `Mode::Explicit`; the
  automatic branch only updates the clickable status-bar notice. My runs:
  `modal.automatic-never-shows` (`automatic=1 before=4 after=4`),
  `modal.dialog-not-visible-after-auto`, and the product-level
  `vedit-project-lint-e2e` `product.automatic-no-dialog before=1 after=1`.
- **Debounced:** `ProjectLintTrigger` owns a single-shot `QTimer` at 700 ms and
  `noteSaved()` restarts it, so N quick saves leave one pending pass.
- **Single-flight:** `ProjectLintTrigger::fire()` cancels a running pass
  (`cancelProjectPass()`) before starting the new one; the controller's
  `startProjectPass()` refuses only if a pass is still running, and the cancelled job
  is released, never queued. `c8.auto-pass-after-save` /
  `c8.auto-pass-after-save-b` passed.
- **Does not block or delay the save:** the save seam only calls
  `saveTrigger_->noteSaved(path)` (a timer start); the pass runs from the timer.

## 4. The regression checks are real (a test that cannot fail — checked)

- The new checks are additive: baseline 60 → green 70 names, with `n2r.*` ×7 and
  `n3p.*` ×3 added; **no** baseline check removed (verified by set comparison of the
  check names in `00-baseline-wiring.log` and `20-green-after-fix.log`).
- The RED run (`10-red-before-fix.log`) was produced with the SAME 70-name test set on
  the pre-fix source: `SUMMARY checks=70 failures=4`, and the four failures are exactly
  the fix-dependent checks (`n2r.edit-before-poll-invalidates`, `n2r.e-no-longer-green`,
  `n3p.in-flight-pass-cancelled`, `n3p.new-explicit-pass-accepted`), while every
  precondition (`n2r.e-covered-before-open`, `n2r.e-is-dirty`,
  `n3p.pass-in-flight-before-switch`) passed first. A check that passed for an
  unrelated reason could not produce that pattern.
- My own run (post-fix) passes all 71 (the 70 U1 checks + U2b's added label check).
- Bounds: I did **not** revert the fix to re-observe red — that would mean editing
  product code, which this review must not do. Sensitivity rests on the recorded red
  run above (same test set, pre-fix source) plus the code-path analysis that identifies
  the only two mechanisms that can remove `e.sv`'s coverage (the seam subscription and
  the per-document dirty check) and the only mechanism that can cancel the held pass
  (`applySettings` → `cancelProjectPass`) — none of them existed pre-fix.

## 5. Findings

### A1 — Medium (carried; not introduced by U1) — the wiring selftest can die of SIGPIPE at teardown
My first **direct** `vedit-b3-wiring-e2e` run printed all 71 checks (all PASS) and then
exited **141** before the summary; the single re-run printed
`SUMMARY checks=71 failures=0 skips=0` and exited 0 (`10-direct-runs.log`,
`12-rerun.log`). This is the latent SIGPIPE documented in `AGENT.md` §4 (lane
selftests spawn real LSP children and do not install the Rust runtime's SIGPIPE
ignore); the U1 report discloses one such transient in its 12 post-fix runs. It does
not invalidate the fix, but it can flake the final gate. Direction: install the
documented `signal(SIGPIPE, SIG_IGN)` / `vedit_runtime_init()` in the lane selftest
entry point (AGENT.md §4), or treat the EPIPE as a state in the LSP path.
Where: `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp` `main`, and the
LSP-child write path it exercises.

### A2 — Nit — the U1 evidence page's test-source hash is stale
`evidence/ux-batch-2/u1-final/README.md` quotes `b3_wiring_e2e.cpp` sha256
`2d95bd5dde97…` for both the red and green runs; the tree now hashes `7d591ab0…`
because the U2b lane added `hover.fix-button-label-english` at 10:26 (after the U1
report at 10:22). The U1 check set itself is unchanged (the 70-name comparison shows
only that addition in the current binary), so the red/green evidence remains valid for
U1, but the quoted hash cannot be reproduced from the tree. Direction: note the
concurrent edit in the evidence page, or re-anchor file hashes in the captain's final
freeze.
Where: `evidence/ux-batch-2/u1-final/README.md` §0/§2;
`src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp`.

## 6. Declined to judge

- **The three nits have no dedicated regressions** — disclosed in the U1 README §4;
  the conditions are not deterministically producible with the fake backends; I verified
  the code paths directly and the surrounding suites remain green. Not a defect.
- **`applySettings()` cancels a pass on ANY settings change**, not only a backend/lint
  switch — broader than the finding, but it is the direction the prior review gave
  ("call `cancelProjectPass()` in `applySettings()`"); a mid-pass form/theme change can
  cancel the pass and the user must re-run Lint Project. Harmless, accepted.
- **Dirty open buffers are still skipped by project passes** (C8, by design — the pass
  lints disk); "not excluded from later passes" is verified in the senses that matter:
  the merge exclusion is removed at once and the file is included again once saved
  (`c8.saved-*-now-included`).
- **The official `ctest -R vedit-lint-experience-selftest` form skips the 13 autofix
  checks** (the ctest definition passes no `--verible-lint`, so 85 checks + 3 skips vs
  the with-flag 98/0/0 I reproduced). A pre-existing harness property, unrelated to
  U1; the U1 verdict does not rest on the autofix block.
- **The real engine's pass path vs the fake** — the N3 test drives the fake backend
  for the held pass; the real engine path is covered by `vedit-project-lint-e2e`
  (11/11 in my run), and the switch itself uses the real settings store + watcher.
- **Full gate, product relink, xcb screenshots** — captain's final gate; not run here.
- **A mutation/revert run to re-observe RED** — not performed (would require editing
  product code); see §4 for the evidence used instead.

## 7. Verdict

**PASS** — N2 and N3 are closed in the product paths with regressions that provably
fail on the pre-fix source, the three nits are implemented and their missing test
coverage is disclosed, and R2/R3 remain intact and independently re-run green; the
carried items (SIGPIPE flakiness, stale evidence hash) are not defects of this repair.

## 8. Bounds of the review

- Ran only the already-built binaries/ctest in the shared tree (no full build, no
  `build/gates/*`), per the shared-build-tree notice; one direct wiring run hit the
  documented transient SIGPIPE and the single re-run was clean.
- No product relink and no GUI: the R2/R3 evidence is controller/panel/model level and
  the offscreen suites; no xcb screenshot of the dots/popup was taken.
- No revert mutation (see §4); the RED evidence is the writer's recorded run plus my
  code-path and check-name continuity analysis.
- Did not re-run `lupdate`/`lrelease` (U1 does not touch translations); did not run the
  full 50-test ctest, `--selfcheck`, or the packaging step.
- Raw evidence: `00-ctest.log`, `10-direct-runs.log`, `11-lint-experience-full.log`,
  `12-rerun.log` in this directory.
