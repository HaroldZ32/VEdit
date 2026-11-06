# B3 + B5 — Independent Review

**Reviewer:** Reviewer (independent)
**Subject:** UX batch 2, lane B3 (R2/R3 + items 9/10/12) and lane B5 (Rename)
**Contract:** `.mpd/plans/ux-batch-2/00-plan.md`, `§0.1` (R1/R2/R3, BINDING), `§1`, `§2`, `§B3`, `§B5`
**Evidence read:** `evidence/ux-batch-2/b3/README.md`, `logs/01-official-build.log` (tail), `logs/02-lint-experience.log`, `logs/03-rename-e2e.log`, `logs/04-project-lint-e2e.log`, `logs/05-write-scope.txt`
**Risk level:** High
**Verdict:** FAIL

## Assessment

The lane does the hard mechanical work well: the 700 ms debounce and single-flight trigger are real, the save seam fires after a successful write for both save and save-as, the CLI severity conversion is applied at the merged-view boundary, the engine-backed autofix probe and the one-undo replacement are genuine, the ELAB dialog policy is coded so that an automatic pass cannot select the dialog, the file-tree dots use the requested colours with a revision-cached repaint, and the Rename E2E drives the real verible server through the menu action to an applied buffer edit and a saved disk change. The review nevertheless fails because several **product wirings are not the layer the tests certified**: the hover popup is a raw pointer to a widget parented to a deletable editor view (a reachable use-after-free), R2's "edited file falls back" is driven by a single-active-file poll and a C8 open set that contains only the active tab of each pane, a project switch is never followed by the diagnostics lane at all, and the user-visible diagnostic count in the panel still comes from the lower layer while the panel header counts the merged view. The B5 "validate before the dialog" fix rejects empty positions and keywords but still opens the dialog and sends a request for number literals, comment words, strings and system tasks, and the product-level "automatic never shows a modal" assertion cannot detect the very regression it claims to rule out. The green lane logs are therefore not sufficient for the frozen contract.

## Ranked findings

### Blocker

1. **Hover popup is a raw pointer to a widget parented to a deletable editor view — use-after-free, and hover is bound to the first view**
   - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::hoverPopup()`, member `hoverPopup_`, `BackendWiring::hideHoverPopup()`, `BackendWiring::handleDiagnosticHover()`; `src/app/editor_area.cpp`, `EditorPane::removeTab()`.
   - **What:** `hoverPopup()` creates `DiagnosticHoverPopup` with `activeView()` as its Qt parent and stores the result in a raw `hoverPopup_`. The class already knows that closing a tab destroys the view — it uses a `QPointer<QObject> hoverView_` for exactly that — but the popup pointer is not guarded and is not reset. `EditorPane::removeTab()` calls `delete editor`, which deletes the view and with it the child popup. The next `requestHover()`, `beginNavigation()`, `handleDiagnosticHover()` or `applyDiagnosticFix()` dereferences the freed pointer (`hide()`, `setContent()`, `placePopup()`, `show()`). Separately, on a tab switch without destruction the popup remains parented to the old, hidden view: `placePopup()` positions it against the new `activeView()` but a hidden parent keeps it invisible, so diagnostic hover silently stops working.
   - **Why it matters:** a user who hovers a squiggle (or invokes the Hover command), closes the tab, and then uses any diagnostic action hits undefined behaviour; this is a product crash, not an assertion gap.
   - **Direction:** parent the popup to a stable widget (the shell or the status bar), or hold a `QPointer<DiagnosticHoverPopup>` and recreate it when `hoverView_` changes; hide/reset it in the same place the view is replaced or destroyed. A popup bound to a view must not outlive that view.

### High

2. **R2 edit invalidation and the C8 open-file set both follow only the active file; a background tab can keep the wrong layer**
   - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `ActiveFileWatcher::poll()`, `BackendWiring::openFileSet()`, `ProjectLintTrigger::fire()`, `BackendWiring::lintProject()`; `src/workspace/diagnostics/diagnostics_controller.cpp`, `DiagnosticsController::startProjectPass()`; `src/app/editor_area.cpp` has `EditorPane::tabCount()` / `EditorPane::tabFilePath()` but neither is used by these seams.
   - **What:** the watcher polls only `activePane()->currentFilePath()`. If the user edits file B and switches to file A before the next 400 ms tick, the path-change branch resets `lastPath_`/`lastText_` and returns without calling `markFileEdited(B)`. B keeps its project-pass upper layer indefinitely (or until it is linted from disk by a later pass). `openFileSet()` likewise adds only one path per pane, so other tabs in the same pane are not in the C8 snapshot: a dirty background tab can be linted from its stale disk contents, become `coveredFiles_`, and then its upper verdict overrides the accurate LSP buffer diagnostics. This is exactly R2's "file edited after the pass → fall back" and the pass-after-edit rule; the selftest exercises `markFileEdited()` directly and therefore cannot see either gap.
   - **Why it matters:** users work with more than one tab; a file edited after a pass can show the project pass's stale opinion, and a dirty background file can be "clean" (covered, no rows) while its buffer has errors.
   - **Direction:** track text per open document (prefer the KTextEditor document `textChanged` signal) and invalidate the file on every edit, not just the active file at poll time; enumerate every tab when building the C8 open set (`EditorPane::tabCount()` / `tabFilePath()`), or have the pass consume buffer text for all open documents. Add a multi-tab regression test that edits a background tab and switches before the poll.

3. **The diagnostics lane does not follow `projectRootChanged`; after Open Folder the automatic and explicit project passes lint the previous project**
   - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `installDiagnostics()`, `ProjectFileSetPoller` (constructed once with `options.projectRoot`), `BackendWiring::buildLintRequest()`, `workspaceRoot_`/`projectFiles_`; `src/app/app_shell.cpp`, `ApplicationShell::projectRootChanged`; `src/workspace/filetree/filetree_integration.cpp` already subscribes to the same signal, showing the intended seam.
   - **What:** the file set is produced by one `vd_project_expand()` call at install time. There is no `projectRootChanged` connection in the diagnostics lane, so `projectFiles_`, the controller's workspace root, the rule path context and the C8/open-file context all stay pinned to the first folder. After the user opens another folder (file tree `directoryOpened` → `setProjectRoot()`), saving a file there triggers the automatic pass over the **old** file list; explicit Lint Project does the same; `ProjectLintNotice`/`upperLayerDiagnostics()` keep the old project's errors and the panel/file-tree status map keeps the old project's paths.
   - **Why it matters:** the new automatic lint now makes a stale, wrong-project result user-visible after a routine Open Folder; the user can be sent to files from a project they left, and the new project never gets a correct project-pass verdict.
   - **Direction:** connect `projectRootChanged` (as the project, file-tree, palette, VCS, module-tree and search lanes do), rebuild the file-set poller for the new root, clear `cliDiagnostics_`/`coveredFiles_`/`passRequest_`, update `workspaceRoot_`/rule context, and add a product-level switch test. At minimum, re-read the current root inside `buildLintRequest()` instead of using the captured list.

4. **R2 counts are still computed twice: the panel status line mixes the lower-layer session count with the merged count**
   - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, `DiagnosticsController::finaliseState()` (writes `session_.message` with `diagnostics_.size()`); `src/workspace/diagnostics/diagnostics_panel.cpp`, `DiagnosticsPanel::refreshFromController()` (appends `controller_->mergedDiagnostics().size()`); the same lower-only number is published in the session snapshot's `diagnostics` field.
   - **What:** a covered file's CLI rows are visible in the merged view but absent from `diagnostics_`. Before any active-file analysis, or whenever `analyzeFileName()` is empty, the panel renders the session message and then the merged count, e.g. a session line that says `diagnostics 0` next to a header that says `Errors: 2`. The upper layer can make the two counts diverge by construction. R2 is explicit that any statistic/count must not be computed by two sources.
   - **Why it matters:** the panel gives two different totals for the same state; this is the layering inconsistency the ruling was written to prevent, and the 71-check log does not assert the status line.
   - **Direction:** compute every user-visible count from one source (`mergedDiagnostics().size()` and the merged severity groups); keep the lower-only count internal or explicitly labelled as an LSP-session diagnostic count, but do not present both as "diagnostics".

5. **The save-triggered pass never lints the file that was just saved (C8), so R3's user-visible purpose is not met for the active file**
   - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, `DiagnosticsController::startProjectPass()` (filters `openFiles_` before `passLintedFiles_`); `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::openFileSet()`/`lintProject()`; `ProjectLintTrigger::fire()`.
   - **What:** both paths take the open-file snapshot before starting (the F6 ordering fix is correctly applied), which means the target file is excluded because the editor has it open. The product E2E confirms the consequence: after opening and saving the broken file, the automatic pass logs `LINT-PASS-FINISHED automatic=1 errors=2 files=1` for a two-file fixture — the one linted file is the other file, and the two errors are the live upper-layer rows of the skipped, just-saved file. `evidence/ux-batch-2/b3/README.md` §4 states this openly and asks for a later ruling.
   - **Why it matters:** the contract's literal trigger requirement is met, but the most common user action (save the file I am editing) does not refresh that file's project-level diagnostics; fixing the file and saving also leaves it with no green dot because it is never re-covered. This is a product-intent gap against R3/R2 and needs an explicit ruling or a design change, not only a disclosure.
   - **Direction:** include the saved document's current buffer in the automatic pass (file-scope path with buffer text, or a dedicated "lint the saved buffer" pass) while keeping the pass asynchronous/non-blocking; at minimum, say "open file skipped" in the notice and get a user decision on C8.

### Medium

6. **Rename still opens the input dialog and sends a request for non-symbol positions**
   - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::requestRename()` and `BackendWiring::symbolUnderCursor()`; `src/workspace/diagnostics/symbol_scan.cpp`, `isVerilogKeyword()`.
   - **What:** `symbolUnderCursor()` treats any run of letters, digits, `_` or `$` as a symbol. The pre-dialog check rejects only empty strings and keywords. A cursor on `1'b0`, on `123`, on a word inside `// comment`, inside a string, or on `$display` therefore passes validation, opens the dialog, and sends a `rename` request; only after the user types a name does the server fail. That is the same class of defect the contract's B5 says must be stopped before the dialog. The E2E covers a blank position and `endmodule` only.
   - **Why it matters:** acceptance ② ("不可改名处给出原因且不发请求") is not met for these positions, and the user still pays the type-then-fail cost.
   - **Direction:** validate that the token can start an identifier (letter/`_`/`$`, not a digit-only run) and do at least a lexical comment/string check before the dialog; if the ABI can be extended, use `prepareRename` for the authoritative answer. Add numeric, comment and string-literal cases to the E2E.

7. **Rename request-path gate lets an unknown/absent capability row through**
   - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, `DiagnosticsController::requestRename()`; compare `src/workspace/diagnostics/ffi_backends.cpp`, `decideActionGate()` (an unknown row leaves `enabled=false`, so the UI is grey).
   - **What:** the request path refuses only `row != nullptr && row->supported == 0`. If the table has no `rename` row (or the row is unknown) the request proceeds while the menu is greyed. That is precisely the "UI grey only" hole the contract forbids.
   - **Why it matters:** a backend without a rename row can still receive a request through the controller, so the gate is not enforced at the ABI-facing seam.
   - **Direction:** treat `row == nullptr` and `supported != 1` as refusal with the same reason the UI uses; add a unit case for an absent row.

8. **Autofix "is this rule fixable?" is cached without the file name or rules config, and the engine is never refreshed on settings changes**
   - **Where:** `src/workspace/diagnostics/autofix.h` / `autofix.cpp`, `AutofixEngine::cacheKey()` and `setRulesConfig()`; `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring` constructor and `applySettings()`.
   - **What:** the header correctly notes that the temp copy keeps the original basename because rules such as `module-filename` depend on it, but `cacheKey(rule, text)` omits the path/base name. The same rule and text in two files can therefore return the first file's fixability answer, showing a fix button that then fails (or hiding one that would work). `setRulesConfig()` is never called at all, so the project's `.rules.verible_lint` is ignored by the probe, and `setLintBinary()` is only called in the constructor, so a settings change of `tools.verible_lint` leaves the autofix engine on the old binary. The cache is also not cleared when the rules config changes.
   - **Why it matters:** the "measured, not hard-coded" claim is undermined in exactly the rule families where the file name matters, and the feature can silently use the wrong engine/configuration.
   - **Direction:** include the basename (and rules config identity) in the cache key; clear the cache when either changes; propagate `state_.toolchain().veribleLint.path` and `state_.rulesPath()` into `autofix_` in `applySettings()`.

9. **A failed whole-document autofix application is reported as success**
   - **Where:** `src/workspace/diagnostics/autofix_document.cpp`, `applyAutofixToDocument()`; `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::applyDiagnosticFix()`.
   - **What:** `applyAutofixToDocument()` ignores the boolean return of `document->replaceText(...)` and always returns true. `applyDiagnosticFix()` prints `applied=%d` but does not branch on it and always shows "Fixed … (one undo reverts it)". A read-only/refusing KTextEditor document therefore silently does nothing while the user is told the fix succeeded.
   - **Why it matters:** item 9 explicitly asks that a failed fix be communicated, not silently ignored.
   - **Direction:** return the replace result; on false show the real failure in the status bar and do not claim success; add a read-only/refusal regression case.

10. **The hover autofix probe is synchronous on the GUI thread and caches whole fixed documents**
    - **Where:** `src/workspace/diagnostics/autofix.cpp`, `autofixRuleInTempCopy()` and `AutofixEngine::probe()`; `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::handleDiagnosticHover()`.
    - **What:** the first hover of each rule/text pair writes the entire editor buffer to a temp file and runs `verible-verilog-lint` with a 20 s wait on the mouse-move path. The cache stores the complete `fixedText` for up to 64 entries; for a large file that is 64 whole-document copies. The README calls the synchronous probe a known bound, but the bound is on the GUI thread and the cache is only count-bounded, not size-bounded.
    - **Why it matters:** a hover can freeze the editor for seconds on a large file, and the cache can consume hundreds of megabytes; this is a user-visible interaction risk, not a hidden implementation detail.
    - **Direction:** make the probe asynchronous (or lazy, behind the fix button) with a short deadline; cache only `attempted/supported/error` (the fix re-runs anyway); cap the cache by text size as well as count.

11. **The green dot is only implemented for project-pass-covered clean files; a lower-layer clean file has no green dot**
    - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, `statusForFile()` / `fileStatuses()`; `src/workspace/filetree/file_status_delegate.cpp`, `FileStatusDotDelegate::dotColor()`.
    - **What:** `FileStatus::Clean` is produced for a covered file with no upper rows; an uncovered file with no lower rows returns `Unknown` and no dot. The selftest's green case is explicitly `covered and clean`. For a file the LSP analyzed cleanly (or a file fixed and saved under C8), the dot disappears/never appears instead of turning green.
    - **Why it matters:** item 10's semantic is red/yellow/green = error/warning/clean; the most common clean case (the current file, or a project with no lint tool) does not render green, so the feature is only half visible.
    - **Direction:** record a lower-layer clean verdict for analyzed files (or for the project file set once the LSP session has processed them) and feed green from that; add a lower-only clean test.

12. **The product-level "automatic pass never shows a modal" assertion cannot detect a re-show of the same dialog**
    - **Where:** `src/workspace/diagnostics/selftests/project_lint_e2e.cpp`, `dialogsOf()`, the explicit-dialog `hide()` and the automatic comparison; `src/workspace/diagnostics/project_lint_notice.cpp`, `showDialog()` reuses `dialog_`.
    - **What:** the test hides the explicit dialog but counts `QDialog` child objects by object name. `showDialog()` reuses the existing hidden object, so a buggy automatic call to `present(..., Explicit)` would call `show()` on the same object and leave `dialogsAfterExplicit == dialogsAfterSave == 1`; the check passes. The log's `product.automatic-no-dialog before=1 after=1` therefore proves only that no *new object* was created, not that no modal was presented.
    - **Why it matters:** item 12's highest-risk property (no modal in the automatic path) is not actually evidenced at product level; the policy unit test is good, but the product wiring needs a truthful assertion.
    - **Direction:** count `dialogsShown()`/visible transitions instead of child objects, or run the automatic pass in a fresh shell with no prior dialog; assert the dialog is not visible after the automatic pass.

13. **The contract-required screenshot evidence for B3 is missing**
    - **Where:** `.mpd/plans/ux-batch-2/00-plan.md` `§B3` acceptance ("证据含截图"); `evidence/ux-batch-2/b3/README.md` §4 ("无窗口截图"); `logs/02` is offscreen pixel sampling only.
    - **What:** the lane provides a stronger pixel assertion for the three dot colours, but no xcb+Xvfb screenshot of the hover popup, the ELAB dialog/notice, the dots in a real tree, or the panel. The README declares this, but it is still a missing acceptance artefact.
    - **Why it matters:** the frozen acceptance asks for screenshots; a pixel sample of one delegate does not cover layout, theme or integration.
    - **Direction:** add xcb+Xvfb screenshots of the dot column, the diagnostic hover with both fix branches, and the explicit/automatic ELAB presentation, or obtain a contract amendment in writing.

14. **B5's cross-file acceptance is SKIPped, and the "must be red before the fix" record is absent**
    - **Where:** `evidence/ux-batch-2/b3/logs/03-rename-e2e.log` (`SKIP rename.crossfile-atomic-save`); `evidence/ux-batch-2/b3/README.md` §4; contract `§B5` (E2E must be red before the fix; acceptance ④ non-open files).
    - **What:** the real-server E2E never exercises a non-open-file edit because the product's verible session only answers for the open file; the non-open atomic-save claim is proven by directly calling `applyWorkspaceEdit()`, not through menu → server → apply. There is no recorded red run of the new E2E. The explanation for the server limitation is plausible and documented, but acceptance ④ is only indirectly covered.
    - **Why it matters:** the contract asked for the full chain and a red-first proof; the current evidence is a unit-level substitute and a post-fix green run.
    - **Direction:** add a real LSP fixture that returns edits for another file (a fake server is acceptable only if the product path is still driven end to end), or get the limitation accepted explicitly; save the failing pre-fix E2E log with the lane evidence.

### Nit

15. **Annotation cache signature ignores the message and rule id**
    - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `AnnotationBridge::signatureOf()`.
    - **What:** the signature includes line/column/end/severity but not `message` or `code`; a project pass that replaces a diagnostic at the same span and severity with a different rule/message will not re-push the annotation, leaving the old annotation tooltip.
    - **Direction:** include `code` and a hash of `message` in the signature.

16. **The legacy `mergeDiagnostics()` still implements the forbidden stacking and is still exercised by the old probe**
    - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, free `mergeDiagnostics()`; `evidence/w4-lint/lint_orchestration_probe.cpp`; `DiagnosticsController::displayDiagnostics()` is now unused by the product.
    - **What:** the free function still merges CLI and LSP rows into `DiagnosticOrigin::Both`; taken together with the five new layered methods, R2 now has two contradictory policies compiled into the same library. The product uses the new methods, so this is not a live user bug, but it is exactly the "one place" maintainability failure the ruling warns about.
    - **Direction:** retire/reimplement the legacy function on top of the layered view, and update or remove the old probe.

17. **`diagnosticMergeKey()` contains a no-op path expression**
    - **Where:** `src/workspace/diagnostics/diagnostics_controller.cpp`, `diagnosticMergeKey()`.
    - **What:** `.arg(QFileInfo(entry.file).absoluteFilePath().isEmpty() ? entry.file : entry.file)` returns `entry.file` in both branches, so the intended path normalization is not performed. Different spellings of the same file will not dedupe within a layer.
    - **Direction:** normalize to one absolute form once and use it in the key.

18. **Lint Project clicked before the asynchronous file-set poll completes says the wrong thing**
    - **Where:** `src/workspace/diagnostics/diagnostics_integration.cpp`, `BackendWiring::lintProject()`.
    - **What:** the log shows `product.attempt=0 status='No project file set — open a folder first'` while a folder is open but the 50 ms poll has not delivered the file set. The message sends the user to the wrong remedy.
    - **Direction:** distinguish "project files are still loading" from "no project"; keep the action enabled but say the load is in progress.

## Declined to judge

- **Whether mouse-hover over a normal symbol must issue an LSP hover automatically** — the contract says "未覆盖到的普通符号仍走既有 LSP hover"; I read that as the existing Hover command remaining enabled (the `decideActionGate("hover")` change), and the lane's README explicitly bounds this to a manual/command path. The ambiguity is noted, not scored as a defect.
- **The unmounted `LintProgressBar` re-run path and the stale `nextPassAutomatic_` after an automatic start failure that never reaches `startProjectPass()`'s flag consumption** — `LintProgressBar` is not mounted in the product (F6), so no user reaches it; the controller's flag handling is still fragile, but there is no live path.
- **Whether a hint/information row should produce no dot, green, or another colour** — the contract defines only red/yellow/green for error/warning/clean; an information row is not a clean file, but no colour is specified for it. Not judged.
- **The slang-rule → verible-rule id collision in the autofix probe** — the probe is measured, but the same id can mean different rules in the two engines; deciding that needs a rule-by-rule matrix, which is outside this lane's evidence.
- **The one-per-paint `sizeHint`/status-source hash lookups in the file tree** — they are revision-cached and bounded by loaded rows; no project rescan per frame, so no performance finding.
- **The save seam's failure path (callback not fired on a failed save)** — I read the code and it returns before the callback on both save paths; the selftest does not inject a failure, but I found no code defect. This is an evidence gap only.
- **`project.reparse` not clearing `cliDiagnostics_`/`coveredFiles_`** — a separate explicit command; R2 defines the next project LINT as the recovery point, so I did not score it independently of the project-switch finding.
- **Symlink/normalisation mismatches between the file tree model and the diagnostics map** — speculative without a product fixture; not judged.
- **The `applyWorkspaceEdit()` partial-application behaviour when a later file fails** — pre-existing implementation outside B5's changed surface; not judged as a B5 defect.
- **The `ProjectLintNotice` dialog being `show()`-modal rather than `exec()`** — it is modal and the test reads it; no user-visible failure found, and the automatic path never reaches it.

## Bounds of this review

- **Read:** the frozen contract `§0.1`, `§1`, `§2`, `§B3`, `§B5`; the B3 README and all four evidence logs; the key product symbols in `diagnostics_controller.{h,cpp}`, `diagnostics_integration.cpp` (constructor, save seam, pass presentation, hover/autofix, rename, file-status publication), `project_lint_trigger.{h,cpp}`, `project_lint_notice.{h,cpp}`, `autofix.{h,cpp}`, `autofix_document.cpp`, `diagnostic_hover.{h,cpp}`, `symbol_scan.cpp`, `file_status_delegate.cpp`, the file-tree model/widget status path, `editor_area.{h,cpp}` save/remove paths, `ffi_backends.cpp` capability gate, the relevant `vd-deeplint` batch/cancel and diagnostic path, and the key sections of the three lane selftests.
- **Not fully read:** all 1,917 lines of `diagnostics_integration.cpp` (key regions only), all of `file_tree_model.cpp`/`file_tree_widget.cpp`, the full `01-official-build.log` (tail and grep only), every line of the lane selftests, the complete Rust ABI changes outside the batch/diagnostic path, and other lanes' diffs (the workspace was mid-flight on `main` with many uncommitted changes).
- **Could not run on this host:** the official `scripts/build-qt.sh`/ctest and the container's 47/47 run — the log is the only evidence; `vedit-project-lint-e2e` fails to load `libtree-sitter.so.0.25` on the host and hangs before its first check when a workspace copy is added to `LD_LIBRARY_PATH`; `vedit-lint-experience-selftest` reaches the real-KTextEditor autofix-undo section and then blocks in a nested event loop in this host environment; `vedit-rename-e2e` was not run for the same library/environment reasons. The three lane logs are therefore accepted as container evidence, not independently reproduced.
- **Workspace state:** the repository is on `main` with other lanes' uncommitted changes; `logs/05-write-scope.txt` is the lane's own declaration, not a git-verifiable attribution. I attribute no other lane's breakage to B3/B5.

## Verdict

**FAIL** — a reachable use-after-free in the hover popup, plus R2 state that is not single-sourced (active-file-only edit invalidation/open-file set, project switches never followed, lower-layer counts in the panel), means the green lane logs do not certify the frozen R2/R3/item 9 contract; the Rename validation and the product-level modal assertion are also incomplete.
