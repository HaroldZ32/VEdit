# B3 + B5 repair — report (after the independent FAIL)

**Lane:** B3 (diagnostics experience, items 9/10/12 + R2/R3) and B5 (Rename).
**Contract:** `.mpd/plans/ux-batch-2/00-plan.md` §0.1 (R1/R2/R3), §B3, §B5, plus the
captain's two rulings supplied with this repair task (C8 content rule; cross-file
rename out of scope).
**Review being repaired:** `evidence/ux-batch-2/review-b3/REPORT.md` (verdict FAIL).
**Workspace:** `/home/haroldzhao/MyProj/DshProj/VEdit` (branch `main`, other lanes'
uncommitted changes present; this repair touched only the paths listed in §5).

## Verdict of this repair

All findings in the review are addressed in code, with a regression that is red
before and green after. The official gate `bash scripts/build-qt.sh` is green:
**49/49 ctest passed** (the lane's baseline evidence recorded 47/47; this repair
adds two tests, so the count did not decrease) and the xcb smoke passed with
`M0-SMOKE ok=1`. The B3/B5 checks that passed before still pass, with the same or
a higher check count (71 → 92 for `vedit-lint-experience-selftest`).

| Gate / suite | Before (lane evidence) | After (this repair) |
|---|---|---|
| `bash scripts/build-qt.sh` | `47/47 tests passed`, EXIT=0 | **`49/49 tests passed`**, `M0-SMOKE ok=1`, `[qt6] PASS`, EXIT=0 (`logs/14-official-build-qt.log`) |
| `vedit-lint-experience-selftest` | 71 checks, 0 fail | **92 checks, 0 fail** (`logs/06-green-lint-experience.log`) |
| `vedit-project-lint-e2e` | 11 checks, 0 fail | **11 checks, 0 fail** (`logs/10-green-project-lint-e2e.log`) |
| `vedit-rename-e2e` | 21 checks, 0 fail, **1 SKIP** | **21 checks, 0 fail, 0 skip, 1 NOT-CLAIMED** (`logs/11-green-rename-e2e.log`) |
| `vedit-b3-wiring-e2e` (new) | — (did not exist) | **48 checks, 0 fail** (`logs/09-green-b3-wiring-e2e-offscreen.log`) |
| `vedit-b3-repair-api-selftest` (new) | — (did not exist) | **16 checks, 0 fail** (`logs/05-green-api-selftest.log`) |
| full ctest | 47 tests | **49/49** (`logs/12-ctest-full-retry.log`; the first run had one TRANSIENT failure of `vedit-app-syntax-startup`, which passed in isolation and on the full retry — see §6) |

## 1. BLOCKER — hover popup use-after-free

**Changed:** `src/workspace/diagnostics/diagnostics_integration.cpp`,
`BackendWiring::hoverPopup()`, new `BackendWiring::adoptHoverView()`, new
`BackendWiring::publishHoverState()`, `hideHoverPopup()`, `handleDiagnosticHover()`,
the `eventFilter` view-follow block in `refreshLiveState()`, and the member
`hoverPopup_` (raw pointer → `QPointer<DiagnosticHoverPopup>` + a
`QMetaObject::Connection hoverViewDestroyed_`).

Design now:
* the popup is a `QPointer`; a cached pointer can never outlive its widget;
* `adoptHoverView()` runs from the 400 ms live poll: on a tab switch it removes the
  old filter, **hides and re-parents** the popup to the new active view, connects
  the new view's `destroyed` signal, and re-installs the filter;
* the `destroyed` connection clears `hoverPopup_`, the diagnostic row state and
  the probe serial (so a late async answer cannot touch the dead view);
* `handleDiagnosticHover()` also calls `adoptHoverView(view)` when a hover arrives
  between a tab switch and the next poll;
* `hideHoverPopup()`/`applyDiagnosticFix()`/`beginNavigation()`/`requestHover()`
  are safe with a null popup.

**Observed before (genuine pre-fix build):** `logs/01-red-b3-wiring-e2e.log`
* `OBSERVED hover.after-hover alive=0 parent=0 diagnostic=0` (no product state existed);
* after closing the hovered tab, invoking the hover action killed the process:
  **exit 139 (SIGSEGV)** — the raw pointer was dereferenced after the view (and its
  child popup) had been deleted. The log stops right after
  `CHECK FAIL destroy.cache-cleared` and has no `SUMMARY`.

**Observed after:** `logs/09-green-b3-wiring-e2e-offscreen.log`
* `CHECK PASS hover.popup-created / hover.parent-is-active-view / hover.shows-diagnostic`
* `CHECK PASS hover.tab-switch  parent=1 hidden=1`
* `CHECK PASS destroy.cache-cleared  alive=0 stale=0`
* `CHECK PASS destroy.actions-clean`, `destroy.no-stale-pointer  stale=0`, exit 0.

## 2. HIGH — R2 single-sourced across ALL open files

**Changed:** `src/workspace/diagnostics/diagnostics_integration.cpp`
`ActiveFileWatcher`: new `trackDocument()` + `noteDocumentEdited()` and a rewritten
`poll()`. The watcher now enumerates **every pane and every tab** and subscribes to
each open document's own change signal (`KTextEditor::Document::textChanged`, or
`QTextDocument::contentsChanged` for the plain-text fallback). A background edit
calls `DiagnosticsController::markFileEdited(path)` immediately, independent of
the active tab. The active-file text comparison remains as a fallback.
**Outside the usual lane text:** `src/app/editor_area.{h,cpp}` gained
`EditorPane::tabEditorWidget(int)` (the pane previously exposed the editor widget
of the active tab only). This is the minimal pane/tab-lifecycle seam the change
genuinely requires; nothing else in `editor_area.*` changed.

**Observed before (mutation restoring active-file-only tracking, the pre-fix
behaviour):** `logs/13-red-r2-active-only-mutation.log`
`CHECK FAIL r2.background-tab-invalidated  upper=...a.sv,...b.sv,...rename.sv`
(B's upper verdict survived the background edit + switch).

**Observed after:** `logs/09-green-b3-wiring-e2e-offscreen.log`
`CHECK PASS upper.initial-covers-b  upper=a.sv,b.sv,rename.sv` and
`CHECK PASS r2.background-tab-invalidated  upper=a.sv,rename.sv` (B dropped).

**Open-file set (C8, see also §5):** `BackendWiring::openFileSet()` is replaced by
`modifiedOpenFiles()` (all panes/tabs; only modified buffers) plus `allOpenFiles()`
for observability. The pass therefore skips only buffers whose disk bytes are
stale, never a clean background tab.

## 3. HIGH — follow `projectRootChanged`

**Changed:** `src/workspace/diagnostics/diagnostics_integration.cpp`
`installDiagnostics()` now connects `ApplicationShell::projectRootChanged`; the
connection calls `BackendWiring::followProjectRoot(root)` (new), destroys and
re-creates the `ProjectFileSetPoller` for the new root, and the new poller calls
`BackendWiring::adoptProjectFiles()` (new). `followProjectRoot()`:
* updates `workspaceRoot_`, clears `projectFiles_`/include dirs/defines;
* cancels an in-flight pass (`cancelProjectPass()`);
* clears the presentation (`ProjectLintNotice::clear()`);
* drops the old project's rows via the new
  `DiagnosticsController::clearProjectDiagnostics()` (CLI rows, covered files,
  analyzed lower-layer files, pass snapshot/request);
* re-pins the session (`setConfiguration`) and resets the LSP file set.
`adoptProjectFiles()` sets the new request and the new LSP file set.
`BackendWiring::publishProjectState()` publishes `vedit.diagnostics.project`
(root/files/pending/open files/modified open files).

**Observed before:** `logs/01-red-b3-wiring-e2e.log`
* `OBSERVED root.after-switch files='.../project1/a.sv'`
* `CHECK FAIL root.repinned`, `CHECK FAIL root.old-rows-cleared`,
  `CHECK FAIL root.new-rows-covered` — the pass still linted project1 after
  `setProjectRoot(project2)`.

**Observed after:** `logs/09-green-b3-wiring-e2e-offscreen.log`
* `PROJECT-ROOT-CHANGED root='.../project2' files=0 (waiting for the new file set)`
* `PROJECT-ROOT-FILES root='.../project2' files=1`
* `OBSERVED root.after-switch files='.../project2/c.sv'`
* `CHECK PASS root.repinned`, `root.old-rows-cleared`, `root.new-rows-covered`.

## 4. HIGH — one count (merged view)

**Changed:** `src/workspace/diagnostics/diagnostics_controller.{h,cpp}`:
new private `composeSessionMessage()`; `finaliseState()` now composes the session
message through it, using `mergedDiagnostics().size()` and storing the same number
in `session_.diagnostics`. `refresh()` recomposes when the merged view moved while
the LSP is not reparsing (`viewRevision_ != lastSessionMessageRevision_`), so a
finished pass / fallback updates the visible count without a reparse. The panel's
header already used the merged count; both now have one source.

**Observed before (mutation restoring `diagnostics_.size()`):**
`logs/04-red-unit-mutations.log`
`CHECK FAIL count.session-uses-merged  message='degraded: lsp 1/1 file(s), diagnostics 0' merged=2 session.diagnostics=0`.

**Observed after:** `logs/06-green-lint-experience.log`
`CHECK PASS count.session-uses-merged  message='degraded: lsp 1/1 file(s), diagnostics 2' merged=2 session.diagnostics=2`;
the product panel screenshot (`screenshots/03-diagnostics-panel.png`) shows
`ready: lsp 3/3 file(s), diagnostics 1 — 1 diagnostic(s)`.

## 5. HIGH — C8 content rule (captain's ruling)

**Changed:** `BackendWiring::modifiedOpenFiles()` (semantics above) is the only
open-file source for both the explicit (`lintProject()`) and automatic
(`ProjectLintTrigger`) passes. `startProjectPass()` still snapshots it; the skip
set now contains only modified buffers. The diagnostics README documents the rule
(`src/workspace/diagnostics/README.md`, R3/C8 paragraph).

**Two-file test** (`vedit-b3-wiring-e2e`, `c8.*`): A and B open, B dirty:
* `OBSERVED c8.after-save-a files='.../a.sv,.../rename.sv'` (A is covered, B is not);
  `CHECK PASS c8.saved-file-included`, `CHECK PASS c8.dirty-buffer-excluded`;
* save B, next pass: `OBSERVED c8.after-save-b files='.../a.sv,.../b.sv,.../rename.sv'`;
  `CHECK PASS c8.saved-b-now-included`, `CHECK PASS c8.unmodified-open-included`.

**Observed before:** `logs/01-red-b3-wiring-e2e.log`
`OBSERVED c8.after-save-a files='.../b.sv'` (the just-saved A was skipped) and
`OBSERVED c8.after-save-b files='.../a.sv'` (B still skipped); both checks failed.

## 6. MEDIUM — the six sub-findings

**(a) Rename validation.**
`src/workspace/diagnostics/symbol_scan.{h,cpp}`: new `renameTargetAt(text, line,
column)` walks the document lexically (line comments, block comments across lines,
strings) and classifies the identifier run. It rejects: no token, number literals
(decimal and based, e.g. `123`, `1'b0`), comment text, string text, system tasks
(`$display`) and keywords; it accepts only a run starting with a letter or `_`.
`BackendWiring::requestRename()` uses it and publishes the reason as the shell
property `vedit.diagnostics.rename` (refused/reason/symbol). The name dialog is
never opened for a rejected position.
`DiagnosticsController::requestRename()` now refuses `row == nullptr` and
`row->supported != 1`, with the same reason the UI gate uses.
* Red: `logs/07-red-api-mutations.log` — 8 failures with the pre-fix acceptance
  restored (`rename.line-comment`, `rename.block-comment`, `rename.string`,
  `rename.system-task`, `rename.number-decimal`, `rename.number-based`);
  `logs/04-red-unit-mutations.log` — `CHECK FAIL rename.null-row-refused
  requests=1` (a no-row backend received the request).
* Green: `logs/05-green-api-selftest.log` (16/16) and
  `logs/09-green-b3-wiring-e2e-offscreen.log` — the product path refuses all six
  positions through the real menu action with the real verible capability table:
  `rename.number-literal-digit`, `rename.number-literal-based`, `rename.comment`,
  `rename.string`, `rename.system-task`, `rename.keyword`, each with its reason.
* The core `vedit-workspace-diagnostics-selftest` fake now advertises the measured
  capability table it stands for (rename=1); the stricter gate is not relaxed —
  the no-row refusal is asserted in the new unit test.

**(b) Autofix cache/config/binary/failure.**
`src/workspace/diagnostics/autofix.{h,cpp}`:
* `cacheKey(rule, filePathHint, text)` now includes the file **base name**, the
  active **rules config** identity, text size and hash; `setRulesConfig()` is a
  real setter that clears the cache when the path changes; `setLintBinary()` still
  clears on change. The cache is bounded by count (64) **and bytes** (8 MiB).
* `BackendWiring::applySettings()` calls `autofix_.setLintBinary(state_.toolchain().veribleLint.path)`
  and `autofix_.setRulesConfig(state_.rulesPath())` on every applied settings set.
* `applyAutofixToDocument()` returns `Document::replaceText()`'s result; the null
  document case is still false. `BackendWiring::applyDiagnosticFix()` branches on
  it: on failure it prints `AUTOFIX-FAILED ...` and shows a real failure message
  instead of "Fixed …".
* Red: `logs/04-red-unit-mutations.log` — `autofix.cache-basename-in-key count=1
  expected=2`, `autofix.cache-rules-config-in-key count=1 expected=2`,
  `autofix.replace-refused applied=1 text-unchanged=1`.
* Green: `logs/06-green-lint-experience.log` — `autofix.cache-hit count=1`,
  `cache-basename-in-key count=2`, `cache-rules-config-in-key count=3`,
  `autofix.replace-refused applied=0 text-unchanged=1`, `replace-writable` PASS.

**(c) Probe off the mouse-move path.**
`AutofixEngine::probeAsync()` (new) runs `verible-verilog-lint` through
`QProcess` signal/slot, with a 20 s kill timer; the hover handler renders the
diagnostic immediately (`fixPending`), returns, and updates the popup when the
answer lands. The cache is consulted first and a hit is delivered on the next
event-loop turn. `BackendWiring` tracks `hoverProbeSerial_` so a stale answer can
never overwrite a newer hover.
* Red: `logs/07-red-api-mutations.log` —
  `autofix.async-not-called-inline` and `autofix.async-cache-is-deferred` fail
  when `probeAsync()` is mutated back to a synchronous probe.
* Green: `logs/05-green-api-selftest.log` — `autofix.async-returns-immediately
  elapsed-ms=0 called=0`, `async-not-called-inline`, `async-completes
  supported=1`; `logs/09-green-b3-wiring-e2e-offscreen.log` —
  `hover.probe-off-mouse-path pending=1`, `hover.fixable-after-async-probe`,
  `hover.no-fix-branch`.
* Screenshots: `screenshots/04a-hover-popup-fixable.png` (「修复」 button) and
  `04b-hover-popup-no-fix.png` ("No automatic fix: line-length has no autofix
  here").

**(d) Green dot for a lower-layer clean file.**
`DiagnosticsController` gained `lowerAnalyzed_` (files the lower layer really
reported on; inserted when a diagnostics poll returns, even with zero rows),
`lowerLayerAnalyzed()`. `statusForFile()` returns `Clean` for such a file and
`fileStatuses()` publishes 3 for it; a file no layer ever evaluated stays
`Unknown` (no dot).
* Red: `logs/04-red-unit-mutations.log` — `dot.lower-clean-green status=0`,
  `dot.lower-clean-map value=-1`.
* Green: `logs/06-green-lint-experience.log` — `status=3`, `value=3`,
  `dot.never-analyzed-unknown` still PASS.

**(e) The automatic-no-modal assertion detects a RE-SHOW.**
`BackendWiring` publishes `vedit.diagnostics.presentation` (mode, automatic, rows,
`dialogsShown`, `noticesShown`, `noticeVisible`, `dialogVisible`, serial) from
`reactToFinishedPass()`. `ProjectLintNotice::dialogsShown()` increments on every
`showDialog()`, so a re-show of the same hidden object is visible; `dialogVisible`
reads the object's visibility. The product regression hides the explicit dialog,
forces a fresh automatic pass, and asserts the counter is unchanged and no dialog
is visible. A policy-level unit check also proves the counter catches a re-show of
the same object (`modal.reshow-counted dialogsShown=2`).
* Red: `logs/01-red-b3-wiring-e2e.log` — `OBSERVED presentation automatic=0
  dialogsShown=-1` (the old detector only counted objects; the new observable did
  not exist).
* Green: `logs/09-green-b3-wiring-e2e-offscreen.log` — `OBSERVED presentation
  automatic=1 dialogsBefore=2 dialogsAfter=2 ... visible=0`,
  `modal.automatic-never-shows`, `modal.dialog-not-visible-after-auto`,
  `modal.counter-detects-reshow`.

**(f) B3 screenshots via xcb/Xvfb.**
`vedit-b3-wiring-e2e` has a `VEDIT_B3_SHOT_DIR` screenshot mode; it was run under
`Xvfb :77` with `QT_QPA_PLATFORM=xcb`, `QT_OPENGL=software`,
`LIBGL_ALWAYS_SOFTWARE=1` (and the host Noto CJK fonts mounted so 「修复」 renders):
`screenshots/01-elab-explicit-dialog.png`, `02-window-with-dialog.png`,
`03-diagnostics-panel.png`, `04a-hover-popup-fixable.png`,
`04b-hover-popup-no-fix.png`, `05-file-status-dots.png` (red/yellow/green dots
between icon and name). Run log: `logs/03-green-b3-wiring-e2e-xcb.log`
(`SUMMARY checks=48 failures=0`).

| screenshot | sha256 |
|---|---|
| `01-elab-explicit-dialog.png` | `19c8bd5b49ed83ec8dd262813acc9d1d33f4883984ab2a7028564860939a6263` |
| `02-window-with-dialog.png` | `de37b0042ea26e14c349537625a099a9948af5fa782024659b78c190f070e3f3` |
| `03-diagnostics-panel.png` | `42e6c1f9935b0de4d71c938c13633319ace58fa82769bf9b43e0df867a2c52bc` |
| `04a-hover-popup-fixable.png` | `ce19837c0387d3a7d1ee83d3c4c978e15ea5d33b5fe58c991e0e9def3b7a8752` |
| `04b-hover-popup-no-fix.png` | `ef8d6060dcb0b1a1cfa8d71f243d85e260a207b728219479aa7c11a588125096` |
| `05-file-status-dots.png` | `b8f780a73112c5906e9ff11b4439bca29781894affb02de8ecfbebb4f36940f3` |

## 7. NITs

* `AnnotationBridge::signatureOf()` now includes the annotation's `rule` and a hash
  of its `message`, so a same-span/same-severity row with a different rule or
  message re-pushes.
* Legacy `mergeDiagnostics()` is explicitly marked LEGACY in
  `diagnostics_types.h` and `diagnostics_controller.cpp`: it still implements the
  pre-layering `Both` stacking, has no product caller (only
  `evidence/w4-lint/lint_orchestration_probe.cpp`), and must not gain one. It is
  intentionally not removed because removing it would delete an existing ctest.
* `diagnosticMergeKey()` normalizes the path to one absolute form (the old ternary
  returned `entry.file` in both branches). Red/green:
  `logs/04-red-unit-mutations.log` → `logs/06-green-lint-experience.log`
  (`'/tmp/.../a.sv' vs '/tmp/..././a.sv'` now equal).
* "Lint Project" before the async file-set load distinguishes loading from no
  project: `LINT-PROJECT-NO-FILES loading=1 root='…'` and the status
  "The project file set is still loading — try again in a moment".
  Red: `logs/01-red-b3-wiring-e2e.log`
  `status='No project file set — open a folder first'`; green:
  `logs/09-green-b3-wiring-e2e-offscreen.log`.

## 8. B5 ruling 2 — cross-file rename is NOT CLAIMED

`src/workspace/diagnostics/selftests/rename_e2e.cpp` no longer emits a SKIP:
when the real server answers for the open file only, it prints

```
NOT-CLAIMED rename.crossfile-atomic-save  verible answers for the OPEN document only;
cross-file rename is NOT CLAIMED in this batch (other.sv unchanged: yes). The
non-open-file WRITE path is proven separately below.
```

and the summary is `checks=21 failures=0 skips=0 not-claimed=1`
(`logs/11-green-rename-e2e.log`). The reason is recorded in
`src/workspace/diagnostics/README.md` (B5 section). The non-open-file **write**
path is still proven end to end (`rename.non-open-atomic-save ok=1 written=1`).
The older lane README's bound section was updated with a repair note and the SKIP
line replaced by a NOT-CLAIMED pointer (see §9).

## 9. Files touched outside the lane's usual scope, and why

* `src/app/editor_area.h`, `src/app/editor_area.cpp` — added
  `EditorPane::tabEditorWidget(int)`. Tracking every open document (R2) needs the
  editor widget of each background tab; the pane only exposed the active tab's
  widget. Ten lines; no other behaviour changed.
* `src/workspace/diagnostics/diagnostics_selftest.cpp` — the fake LSP now
  advertises the measured capability table it represents. This follows from the
  captain's stricter request-path gate (no-row must refuse); the gate itself was
  not weakened, and the no-row refusal is covered by a new test.
* `src/workspace/diagnostics/selftests/rename_e2e.cpp` — SKIP → NOT-CLAIMED per
  the captain's ruling.
* `src/workspace/diagnostics/README.md` — C8 content rule, hover lifetime, async
  probe, one count, lower-clean dot, project-root follow, and the cross-file bound.
* `evidence/ux-batch-2/b3/README.md` — the captain asked to remove the SKIP from
  the acceptance matrix; the old bound section now carries a clearly-marked
  repair note and a NOT-CLAIMED pointer to this report (the original text is kept
  as history).
* New evidence directory `evidence/ux-batch-2/b3-repair/` (this report, logs,
  screenshots). No FORBIDDEN path was touched (`src/app/app_shell.cpp`,
  `src/app/main.cpp`, `src/workspace/search/**`, `src/workspace/module_tree/**`,
  `src/editor/**`, `crates/**`, `docs/USER_GUIDE.md`, `translations/**`).
  The crates/** modifications visible in `git status` predate this repair (other
  lanes); this repair did not write there.

## 10. What I could NOT prove

* **Not a true pre-fix binary for the unit-level fixes.** The lane's original
  implementation was uncommitted/untracked and had already been edited in place
  when the unit regressions were written; the product-level red run
  (`logs/01-red-b3-wiring-e2e.log`) is a genuine pre-fix build (and crashed with
  SIGSEGV), but for the controller/autofix unit fixes the red evidence was
  captured by temporarily restoring the exact pre-fix behaviour (mutations,
  `logs/04-red-unit-mutations.log`) and then restoring the fix
  (`logs/06-green-lint-experience.log`). The mutations are the same code the
  review described; this is disclosed rather than presented as a historical run.
* **`applySettings()` propagation of the autofix binary/rules** is asserted by
  code inspection and by the unit semantics of `setLintBinary`/`setRulesConfig`;
  no product test drives the settings watcher through a settings-file change.
* **`ProjectLintNotice` re-show under a genuinely buggy automatic path** cannot be
  produced without breaking the product; the assertion is proven to detect a
  re-show at the policy level (`modal.reshow-counted`) and the product path is
  asserted to leave `dialogsShown` unchanged.
* **The first full ctest run failed once** on `vedit-app-syntax-startup` (missing
  regex `syntax-product: grammar=verilog`); it passes in isolation and in the
  full retry, and the official `build-qt.sh` run (which includes ctest) passed
  49/49. I treat it as transient, not as a repair claim.
* **Cross-file rename** remains not claimed (documented bound), per ruling.
* **The screenshots' visual quality on a real desktop** (fonts/theme/DPI) is not
  claimed; the images are real xcb/Xvfb grabs of the product widgets.
* **Slang rule-id ↔ verible rule-id collisions in the autofix probe** remain out
  of scope (the review also declined to judge them).
* **Environment note:** the rootless Docker bind mount mapped the host uid to
  container root, so `.mpd/recon/verible/bin` (mode 700) was not executable by the
  test process; the directory was made 755 on the host (an ignored local
  checkout path, not a product file). Without that, the engine-driven legs skip.

## 11. Evidence index (`evidence/ux-batch-2/b3-repair/`)

| file | content |
|---|---|
| `logs/00-baseline-lint-experience.log` | 71 checks, the lane's pre-repair state |
| `logs/00-baseline-project-lint-e2e.log` | 11 checks; shows the pre-repair `No project file set`, `files=1` auto pass, vacuous modal check |
| `logs/01-red-b3-wiring-e2e.log` | genuine pre-fix product E2E: hover state absent, C8 excludes the saved file, root not followed, **SIGSEGV after the destroyed-view action** |
| `logs/02-green-b3-wiring-e2e.log` | earlier green run (38 checks) |
| `logs/03-green-b3-wiring-e2e-xcb.log` | xcb+Xvfb run that produced the screenshots (48 checks) |
| `logs/04-red-unit-mutations.log` | 9 FAILs with the exact pre-fix unit behaviour restored |
| `logs/05-green-api-selftest.log` | renameTargetAt + probeAsync, 16 checks |
| `logs/06-green-lint-experience.log` | 92 checks, all B3/B5 invariants |
| `logs/07-red-api-mutations.log` | 8 FAILs with the old rename acceptance / synchronous probe |
| `logs/08-rebuild-all.log` | full `cmake --build` after restoring the fixes |
| `logs/09-green-b3-wiring-e2e-offscreen.log` | product E2E, 48 checks |
| `logs/10-green-project-lint-e2e.log` | presentation E2E, 11 checks |
| `logs/11-green-rename-e2e.log` | real-server rename E2E, NOT-CLAIMED line |
| `logs/12-ctest-full.log` | first full ctest (48/49; transient syntax-startup failure) |
| `logs/12-ctest-full-retry.log` | **49/49 passed** |
| `logs/13-red-r2-active-only-mutation.log` | R2 red with active-file-only tracking restored |
| `logs/14-official-build-qt.log` | `bash scripts/build-qt.sh`: 49/49 ctest, `M0-SMOKE ok=1`, `[qt6] PASS` |
| `screenshots/01…05` | ELAB dialog, window, panel, hover fixable/no-fix, dots (sha256 in §6f) |

## 12. Reproduction commands (container paths prefixed `/vedit-build`)

```bash
# official gate (image reused via VEDIT_SKIP_IMAGE_BUILD=1; the script's own switch)
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh

# product wiring (new)
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-b3-wiring-e2e
# unit/API (new)
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-b3-repair-api-selftest \
  --verible-lint .mpd/recon/verible/bin/verible-verilog-lint
# lane suites
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-lint-experience-selftest \
  --verible-lint .mpd/recon/verible/bin/verible-verilog-lint
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-project-lint-e2e
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-rename-e2e \
  --verible-ls .mpd/recon/verible/bin/verible-verilog-ls
# full suite
ctest --test-dir target/qt6-build --output-on-failure
# screenshots: same binary under Xvfb/xcb with VEDIT_B3_SHOT_DIR=<dir>
```
