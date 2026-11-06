# B3 repair — round 2 (after the r2 re-review)

**Lane:** B3 (diagnostics experience: items 9/10/12 + R2/R3) and the R2/R3 state bugs.
**Review being repaired:** `evidence/ux-batch-2/review-b3-r2/REPORT.md` (verdict FAIL: N1–N4 + nits).
**Contract:** `.mpd/plans/ux-batch-2/00-plan.md` §0.1 (R1/R2/R3), §B3; captain ruling C8.
**Write scope:** `src/workspace/diagnostics/**` only. No forbidden path was touched
(`src/app/app_shell.cpp`, `src/app/main.cpp`, `src/workspace/search/**`,
`src/workspace/module_tree/**`, `src/editor/**`, `crates/**`, `docs/USER_GUIDE.md`,
`translations/**`). `src/app/editor_area.*` was NOT needed this round.

## Verdict of this repair

Every N item has a regression that was observed **red before the fix and green after**,
`bash scripts/build-qt.sh` is green at **50/50 ctest** (the count did not decrease;
it was 50 before), and the previously passing B3/B5 suites still pass:
`vedit-lint-experience-selftest` **98/98**, `vedit-b3-repair-api-selftest` **16/16**,
`vedit-b3-wiring-e2e` **60/60** offscreen and **61/61** xcb,
`vedit-project-lint-e2e` **11/11**, `vedit-rename-e2e` **21/21, 0 skips, 1 NOT-CLAIMED**
(the cross-file rename claim stays out of scope, as the captain ruled).

| Gate / suite | Observed before | Observed after | Log |
|---|---|---|---|
| `bash scripts/build-qt.sh` (`VEDIT_SKIP_IMAGE_BUILD=1`) | 50 tests (baseline) | **50/50 passed**, `M0-SMOKE ok=1`, `EXIT=0` | `logs/30-official-build-qt.log` |
| `vedit-lint-experience-selftest` | 98 checks | **98 checks, 0 failures, 0 skips** | `logs/05-green-lint-experience.log` |
| `vedit-b3-repair-api-selftest` | 16 checks | **16 checks, 0 failures** | `logs/06-green-api-selftest.log` |
| `vedit-b3-wiring-e2e` (offscreen) | 60 checks | **60 checks, 0 failures** | `logs/09-green-b3-wiring-e2e-offscreen.log` |
| `vedit-b3-wiring-e2e` (xcb/Xvfb + screenshots) | 61 checks | **61 checks, 0 failures** | `logs/20-xcb-screenshots.log` |
| `vedit-project-lint-e2e` | 11 checks | **11 checks, 0 failures** | `logs/10-green-project-lint-e2e.log` |
| `vedit-rename-e2e` | 21 checks, 1 NOT-CLAIMED | **21 checks, 0 skips, 1 NOT-CLAIMED** | `logs/11-green-rename-e2e.log` |

Red-before-green logs (each produced by temporarily restoring the pre-fix behaviour,
then reverting it; the product source in this tree is the FIXED one):

| Item | Mutation that made it red | Red log |
|---|---|---|
| N1 | remove the two `lowerAnalyzed_.clear()` calls | `logs/11-red-n1-loweranalyzed-not-cleared.log` |
| N2 | remove the path-change `markFileEdited()` | `logs/12-red-n2-pathchange-not-marked.log` |
| N3 | remove `clearProjectDiagnostics()` from `applySettings()` | `logs/13-red-n3-applysettings-keeps-upper.log` |
| N4 (`stale`) | hard-code `stale=false` again | `logs/14-red-n4a-stale-hardcoded.log` |
| N4 (`destroy.no-stale-pointer`) | keep the cached row after the view dies | `logs/15-red-n4b-destroyed-cache-not-cleared.log` |
| N4 (`hover.parent-is-active-view`) | parent the popup to the shell instead of the view | `logs/16-red-n4c-stable-parent.log` |
| R2 background test | active-file-only `noteDocumentEdited()` | `logs/17-red-r2-active-only-tracking.log` |

## N1 (HIGH) — the Clean verdict is now tied to the current analysis

**Changed:** `src/workspace/diagnostics/diagnostics_controller.cpp`,
`DiagnosticsController::reparse()` and `DiagnosticsController::analyzeFile()` (the
new-file-scope-session branch) now clear `lowerAnalyzed_` whenever they clear
`diagnostics_`; the member's contract in `diagnostics_controller.h` says the set
describes the CURRENT analysis. `BackendWiring::applySettings()`
(`diagnostics_integration.cpp`) additionally calls `clearProjectDiagnostics()` before
the new backend answers, so the mark cannot survive a backend/settings switch either.

**Regression:** `lint_experience_selftest.cpp` — the LSP fake reports an ERROR for
`a.sv`; the test then opens `b.sv` (a new file-scope session) and asserts
`statusForFile(a) != Clean` and that the dot map has no green entry for `a.sv`;
then it runs the switch path (`clearProjectDiagnostics()` + `setFileSet({})`) and
asserts the same.

* **Before (mutation):** `CHECK FAIL n1.a-not-clean-after-file-switch status=3 merged=0`
  and `CHECK FAIL n1.a-no-green-dot-after-file-switch value=3` — status 3 is `Clean`,
  i.e. the false green the review reported.
* **After:** `CHECK PASS n1.a-error-before status=1`, `n1.a-not-clean-after-file-switch
  status=0 merged=0` (0 = Unknown, no dot), `n1.a-no-green-dot-after-file-switch
  value=-1`, `n1.a-not-clean-after-backend-switch status=0`.

**Not proven:** in the N1 red run the `n1.a-not-clean-after-backend-switch` leg stayed
green because `clearProjectDiagnostics()` already cleared `lowerAnalyzed_` before the
mutation could matter; the `reparse()` clear is therefore defence-in-depth, and the
product-level backend-switch symptom is proven red by the N3 log (upper rows survive,
`dot-map=3`) and green by `n3.old-upper-rows-cleared`/`n1.old-file-not-covered-after-switch`.
The product's real verible-LSP error path was not driven end to end for N1; the
regression uses the controller's own backend seam.

## N2 (MEDIUM) — the first edit of a newly opened covered file is seen

**Changed:** `diagnostics_integration.cpp`, `ActiveFileWatcher`:
new `bufferModified()` reads the document's own dirty flag
(`KTextEditor::Document::isModified()` / `QTextDocument::isModified()`), and
`poll()`'s path-change branch calls `controller_->markFileEdited(path)` when the
newly active buffer is modified — before `analyze()`. A tab that was edited before
the 400 ms poll subscribed to its document emitted no signal we could hear; the dirty
flag is the surviving evidence.

**Regression:** `b3_wiring_e2e.cpp`, N2 leg: `d.sv` is project-pass covered and has
never been opened; the test opens it and edits it **in the same event-loop turn**
(no timer can run between `openFile` and `insertText`), then asserts the upper layer
for `d.sv` is gone.

* **Before (mutation):** `CHECK FAIL n2.first-edit-before-poll-invalidates
  upper=…a.sv,…b.sv,…d.sv,…rename.sv` — the fresh lower analysis stays suppressed.
* **After:** `CHECK PASS n2.file-covered-before-open`, `n2.d-is-dirty`,
  `n2.first-edit-before-poll-invalidates upper=…a.sv,…b.sv,…rename.sv` (d.sv dropped).

**Not proven:** subscription-on-document-creation was not added (the editor area has
no tab-lifecycle signal; the path-change dirty check closes the same window). The
test proves the first edit is now seen; it cannot prove every ordering of a
non-timer-driven widget.

## N3 (MEDIUM) — a backend/settings switch drops the previous pass's upper rows

**Changed:** `diagnostics_integration.cpp`, `BackendWiring::applySettings()` calls
`controller_->clearProjectDiagnostics()` before `setFileSet(QStringList{})`, so both
layers are cleared before the new engine answers. The switch result is published for
tests as the shell property `vedit.diagnostics.switch` (`ok`, `backend`, `lint`, and
`reason` on failure).

**Regression:** `b3_wiring_e2e.cpp` writes `"tools.lint_engine" = "verible"` into
`<stateDir>/settings.toml` (the flat key is quoted, as the vd-settings store writes
it), waits for the product's 300 ms settings watcher to apply it, then asserts the
upper layer is empty.

* **Before (mutation):** `CHECK FAIL n3.old-upper-rows-cleared
  upper=…a.sv,…b.sv,…rename.sv` and `CHECK FAIL n1.old-file-not-covered-after-switch
  covered-a=1 dot-map=3` — the previous engine's rows/dots survived.
* **After:** `CHECK PASS switch.applied ok=1 reason=''` (backend=verible,
  lint=verible-verilog-lint), `n3.old-upper-rows-cleared upper=`,
  `n1.old-file-not-covered-after-switch covered-a=0 dot-map=0`.

**Not proven:** the settings watcher was driven through the real C ABI in the
container, but only one switch direction (slang → verible) and only on a machine
where `verible-verilog-lint` resolves (this tree does); the test fails rather than
skips if the apply never succeeds.

## N4 (MEDIUM, test integrity) — the checks can fail now

**Changed:** `diagnostics_integration.cpp` and `b3_wiring_e2e.cpp`.

* `BackendWiring::publishHoverState()` now computes `stale` (`hoverRowIsStale()`:
  the popup's cached file/line/rule is no longer present in the controller's current
  merged rows). It replaced `state.insert("stale", false)`.
  `hover.stale-detects-cleared-row` shows a diagnostic hover, calls
  `clearProjectDiagnostics()`, and asserts the next live poll reports
  `diagnostic=1 stale=1`. **Red** with the hard-coded field
  (`logs/14-red-n4a-stale-hardcoded.log`: `stale=0`).
* `hover.probe-off-mouse-path` now reads the hover state at the **hover instant**
  (immediately after `sendEvent`, before any event-loop turn) and requires
  `probePending=true && diagnostic=true`. A synchronous probe would already have
  delivered its answer; the check can fail. (The real engine path is separately
  proven by `autofix.async-returns-immediately` / `async-not-called-inline`.)
* `destroy.no-stale-pointer` asserts the real cached state after the view is gone:
  `alive=false && diagnostic=false && stale=false`, from the published map (the old
  check read a hard-coded `stale`). **Red** when the `destroyed` handler keeps the
  cached row: `CHECK FAIL destroy.no-stale-pointer alive=0 diagnostic=1 stale=0`
  (`logs/15-…`).
* `destroy.actions-clean` is no longer `check(..., true)`: it requires the state map
  to be present and, if a popup exists after the hover/definition actions, that it
  is parented to the **active** view and holds no stale row. **Red** with the popup
  parented to the shell (`logs/16-red-n4c-stable-parent.log`:
  `CHECK FAIL hover.parent-is-active-view`; that run then died before SUMMARY — the
  failure is recorded, the later death is not diagnosed here).
* The R2 product test edits a **genuinely non-active** tab: it keeps A active and
  edits B's document through `EditorPane::tabEditorWidget()`, asserting
  `r2.edit-was-background` (active=a.sv, edited=b.sv) and then
  `r2.background-tab-invalidated`. **Red** with active-only tracking
  (`logs/17-red-r2-active-only-tracking.log`: background check FAILs, the
  edit-was-background check PASSes — the old "activate B then edit" test could not
  tell the two behaviours apart).

**Not proven:** `hover.probe-off-mouse-path` was not red-mutated to a synchronous
probe (the unit test `autofix.async-not-called-inline` is the red for that
mechanism, carried from the previous repair); the product check is now capable of
failing but its red is argued from the mechanism, not re-recorded.

## Screenshots — real window-level captures (replaces the synthetic/misleading ones)

`b3_wiring_e2e.cpp` now installs the **real file tree** (`installFileTree`, the real
`FileTreeWidget` with the dot delegate), and `shootScreen()` captures an X11 screen
region under Xvfb/xcb instead of `QWidget::grab()`:

| file | content (xcb/Xvfb screen grab) |
|---|---|
| `01-elab-explicit-dialog.png` | the ELAB dialog's screen region (582×332) — a real dialog |
| `02-window-with-dialog.png` | the whole 1400×900 screen with the **dialog visible over the real window** (previously "no dialog") |
| `03-diagnostics-panel.png` | the real diagnostics panel widget (unchanged, accepted) |
| `04a-hover-popup-fixable.png` | the real window with the hover popup **inside it** (previously a widget-only grab) |
| `04b-hover-popup-no-fix.png` | same window, the no-autofix branch |
| `05-file-status-dots.png` | the **real file tree in the real window**: a.sv red, b.sv amber, d.sv/rename.sv green (previously a synthetic `QTreeWidget`) |

`logs/21-screenshots-sha256.txt` pins the bytes. Two real defects were found by making
the captures honest and are fixed: (a) the hover popup was moved with
`view->mapToGlobal(...)` added to a parent-relative point (`placePopup()`), i.e. about
twice the offset to the right/below — a widget grab could not see it, and the real
window grab showed no popup; `placePopup()` now converts through the popup's actual
parent. (b) The tree's dots needed a forced repaint before the screen grab (Qt had not
flushed the changed rows to Xvfb); the capture path now repaints the tree first.

**Bound:** in this Xvfb image the CJK button label 「修复」 renders as two tofu boxes
(no CJK UI font in the image); the button is present and is the real fix button. The
screenshots are one 1400×900 Xvfb resolution, not a desktop/font/DPI claim.

## Nits taken

* **Panel count printed twice** — `diagnostics_panel.cpp`, `refreshFromController()`:
  a session message that already ends with "diagnostics N" is shown verbatim; the
  count is appended only when the message carries none.
* **`AUTOFIX-APPLIED applied=0`** — `diagnostics_integration.cpp`,
  `applyDiagnosticFix()`: success prints `AUTOFIX-APPLIED … applied=1`; a refused
  replacement prints `AUTOFIX-FAILED … applied=0 reason='document refused the
  replacement'` (no success tag before the failure).
* **C8 empty-pass message** — `diagnostics_controller.cpp`, `startProjectPass()`:
  "no files to lint (every requested file is a modified buffer; the language server
  lints unsaved edits)" instead of claiming every file has an open LSP session.
* **Double reparse after Open Folder** — `ProjectFileSetPoller::poll()` no longer
  calls `setFileSet()` itself when a sink exists; the initial poller uses the same
  `adoptProjectFiles()` sink as the root-switch poller (one LSP session per root).
* **Stale hover popup across a project switch** — `followProjectRoot()` hides the
  popup, clears the cached file/rule/line/entry, invalidates an in-flight probe
  serial and republishes the hover state.
* **README `.rules.verible_lint` claim** — `src/workspace/diagnostics/README.md`
  now says the discovered rules file is NOT exposed by the C ABI (only the
  `tools.verible_rules` setting reaches the autofix engine), matching
  `ffi_backends.cpp`.

## Additional finding caught during verification (fixed)

The double-reparse fix initially made the initial file-set adoption overwrite the
controller's LSP binary with `state_.lspServerPath()` **before any settings apply**,
i.e. with an empty string: `vedit-rename-e2e` then reported
`SKIP rename.e2e the LSP session never became ready`. That would have silently
downgraded a previously passing real-server test. Fixed with
`BackendWiring::configuredLspPath()` (the selected path, else the resolved
`toolchain_.verible.path`), used by `followProjectRoot()` and `adoptProjectFiles()`.
`vedit-rename-e2e` is back to **21 checks, 0 skips** (`logs/11-green-rename-e2e.log`).

## Bounds (what this repair does NOT prove)

* **Real desktop:** every GUI observation is offscreen or Xvfb/xcb in the pinned
  container. No real window manager, DPI, theme or CJK UI font is claimed.
* **N1 backend-switch leg:** the controller-level switch leg uses
  `clearProjectDiagnostics()` (which already cleared the mark); the `reparse()`
  clear is defence-in-depth. The false-green red is recorded for the file-switch
  path; the product-level backend-switch red is the N3 log.
* **N2 subscription:** no document-creation subscription was added; the dirty-flag
  path-change branch closes the pre-poll window. A widget that reports neither
  `isModified()` nor a change signal would still be missed (no such editor exists in
  this product).
* **N4 stable-parent mutation:** the red log ends without `SUMMARY` (the process
  died later); the failing check is real, the cause of the later death was not
  investigated.
* **Official gate:** run through the repository's own script with the existing
  image (`VEDIT_SKIP_IMAGE_BUILD=1`), as the previous round did; the image itself
  was not rebuilt.
* **ctest count:** 50 → 50 (no test added; the new assertions live in the existing
  binaries). It did not decrease.

## Evidence index (`evidence/ux-batch-2/b3-repair-r2/`)

| file | content |
|---|---|
| `logs/05-green-lint-experience.log` | 98 checks, 0 failures (N1 green) |
| `logs/06-green-api-selftest.log` | 16 checks, 0 failures |
| `logs/09-green-b3-wiring-e2e-offscreen.log` | 60 checks, 0 failures (N2/N3/N4/R2 green) |
| `logs/10-green-project-lint-e2e.log` | 11 checks, 0 failures |
| `logs/11-green-rename-e2e.log` | 21 checks, 0 skips, 1 NOT-CLAIMED |
| `logs/11-red-n1-loweranalyzed-not-cleared.log` | N1 red: false green `status=3 value=3` |
| `logs/12-red-n2-pathchange-not-marked.log` | N2 red: upper still covers d.sv |
| `logs/13-red-n3-applysettings-keeps-upper.log` | N3 red: old upper rows survive the switch |
| `logs/14-red-n4a-stale-hardcoded.log` | N4 red: `stale` hard-coded |
| `logs/15-red-n4b-destroyed-cache-not-cleared.log` | N4 red: cached row survives the view |
| `logs/16-red-n4c-stable-parent.log` | N4 red: popup parented to the shell |
| `logs/17-red-r2-active-only-tracking.log` | R2 red: active-only tracking misses a background edit |
| `logs/20-xcb-screenshots.log` | xcb/Xvfb run, 61 checks, 0 failures, 6 captures |
| `logs/21-screenshots-sha256.txt` | sha256 of the six PNGs |
| `logs/30-official-build-qt.log` | `bash scripts/build-qt.sh`: 50/50 ctest, `M0-SMOKE ok=1`, `EXIT=0` |
| `screenshots/01…05` | real window-level captures (see the table above) |

## Reproduction (inside the pinned image; paths are `/vedit-build` in the container)

```bash
# official gate (image reused; the script's own switch)
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh

# lane suites (offscreen)
B=target/qt6-build/src/app/workspace-lane-diagnostics
QT_QPA_PLATFORM=offscreen VEDIT_QUIET=1 $B/vedit-lint-experience-selftest \
  --verible-lint .mpd/recon/verible/bin/verible-verilog-lint
QT_QPA_PLATFORM=offscreen VEDIT_QUIET=1 $B/vedit-b3-wiring-e2e
$B/vedit-rename-e2e --verible-ls .mpd/recon/verible/bin/verible-verilog-ls

# screenshots (Xvfb :77, xcb, software rendering)
Xvfb :77 -screen 0 1400x900x24 &
DISPLAY=:77 LIBGL_ALWAYS_SOFTWARE=1 QT_OPENGL=software QT_QPA_PLATFORM=xcb VEDIT_QUIET=1 \
  VEDIT_B3_SHOT_DIR=evidence/ux-batch-2/b3-repair-r2/screenshots $B/vedit-b3-wiring-e2e
```
