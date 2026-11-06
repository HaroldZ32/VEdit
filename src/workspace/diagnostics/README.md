# src/workspace/diagnostics — Verible LSP + SLANG diagnostics lane (T8)

One **effective file set** (the project model's `.f` expansion) feeds two
backends; the lane reports an observable reparse state while it does so.

- **LSP**: `crates/vd-ffi/src/lsp.rs` session exports (`vd_lsp_session_*`) over
  the Rust client `crates/vd-lsp` (session facade added in
  `crates/vd-lsp/src/session.rs`): initialize, `didOpen` for every file in the
  set, diagnostics wait, definition / references / documentSymbol / rename /
  formatting.
- **SLANG**: `crates/vd-ffi/src/deeplint.rs` (`vd_deeplint_lint`/`poll`), one
  run per file of the same set.
- **Controller**: `diagnostics_controller.*` — `setFileSet()` → `Reparsing`,
  `refresh()` → `Ready` / `Degraded` / `Failed`, with the A3c report
  (`fileSetReport()`) and the A3d snapshot (`session()`, `generation()`).
- **Outline**: `diagnostics_outline_model.*` + `diagnostics_panel.*`; the shell's
  `outline` panel factory is replaced by `diagnostics_integration.cpp`.

## Build / test (headless)

```sh
# fakes only (no Rust FFI, no server needed)
cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja
cmake --build target/diagnostics-build
ctest --test-dir target/diagnostics-build --output-on-failure

# with the Rust C ABI and a real verible-verilog-ls
cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja \
      -DVEDIT_RUST_FFI_LIB=target/release/libvd_ffi.a \
      -DVEDIT_RUST_FFI_INCLUDE=crates/vd-ffi/include
cmake --build target/diagnostics-build
ctest --test-dir target/diagnostics-build --output-on-failure
```

The ctest suite contains:

| test | proves |
|---|---|
| `vedit-workspace-diagnostics-selftest` | A3c same-set fan-out (fake backends), A3d Reparsing→Ready, the six entry points, degraded/failed paths — no display, no server |
| `vedit-diagnostics-ffi-selftest` | the same properties through the real C ABI with a real `verible-verilog-ls`; **prints `SKIP:` and exits 0** when the binary or fixture is absent |

`SKIP` is explicit and is reported as such in the evidence: it never counts as a
passing interaction.

## What this lane does NOT prove (no display, honest limits)

- The underlines, the dock rendering and the editor jump itself are user-visible
  behaviour; this lane proves the **entry points and their typed results** in
  headless runs only.
- `slang` is not installed on the build host used for the first evidence run, so
  the SLANG leg is proven as a *typed degradation* (`slangFailures` in the A3c
  report, message in `lastError()`), not as successful diagnostics. A host with
  slang runs the same test with a healthy leg.
- Wiring this lane into `vedit-app` needs the lane list in
  `src/app/CMakeLists.txt` and a call in `src/workspace/integration.cpp`; both
  are outside this lane's write scope and are reported to the captain.

## Integration contract

`installDiagnostics(ApplicationShell &, const IntegrationOptions &)`:

1. creates the controller with the real FFI backends when linked;
2. registers the "outline" panel factory (document symbols);
3. registers `project.reparse` (re-run Verible + SLANG);
4. starts the project-model job and feeds `setFileSet()` when it finishes
   (timer-driven, so startup is not blocked);
5. exposes the controller as the shell property `vedit.diagnosticsController`
   (`QObject *` holder) so the editor lane can call definition/references/rename/
   format with the caret position.

## Toolchain discovery and missing-tool visibility (T82)

The external tools are resolved with the delivery order **bundled > PATH > env**:

- `bundled`: next to the product binary (`bin/` beside `vd-app`, plus the legacy
  `lib/vedit/bin/` layout). The delivered AppDir installs
  `bin/verible-verilog-{ls,lint,format}` and `bin/slang` (sha256 pinned).
- `PATH`: the user's shell PATH.
- `env`: `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` (explicit override, last).

The chosen source (`bundled` / `PATH` / `env:<name>`) and a `missing: [...]` list are
printed by `vedit-app --selfcheck` (`TOOLCHAIN ...`), appended to the shell diagnostic
list (status bar) and shown in the outline/diagnostics panel when a tool is absent —
including what is missing, what it disables and how to fix it.

```sh
# bundled / missing proofs on an assembled payload
vedit-toolchain-selftest --appdir <AppDir> --expect bundled \
                         --lint-file crates/vd-lsp/fixtures/project/bad_line_length.sv
vedit-toolchain-selftest --appdir <AppDir-with-tools-moved> --expect missing
```

The `missing` case is also a ctest entry (`vedit-toolchain-missing-selftest`) so a clean
build always asserts the explicit-failure path.

## UX batch 2 (B3/B5): one authoritative diagnostics view, save-triggered lint

The plan is `.mpd/plans/ux-batch-2/00-plan.md` §B3/§B5 and the BINDING rulings
§0.1 R2/R3. This lane is where they are implemented.

### R2 — one layered view (project pass = upper, per-file/LSP = lower)

* `DiagnosticsController::mergedDiagnostics()` / `mergedDiagnosticsForFile()` are
  the ONE view every surface reads: the editor squiggles/gutter
  (`AnnotationBridge`), the diagnostics panel, the counts, the diagnostic hover
  and the file-tree dots. `displayDiagnostics()` returns the same layering with
  the origin attached.
* A file covered by the last FINISHED project pass shows ONLY that pass's rows —
  even when the pass found nothing (a covered-and-clean file is green, not
  dot-less). Files the pass excluded (modified open buffers, see C8 below) keep
  their previous verdict until edited or saved.
* **Every user-visible count comes from the merged view**: the panel header and
  the session/status line both use `mergedDiagnostics().size()` (the session
  snapshot's `diagnostics` field too). The lower-only vector is never presented
  as "diagnostics".
* **The green dot is not limited to project-pass coverage**: a file the lower
  layer analysed and reported clean (`lowerLayerAnalyzed()`) is green as well; a
  file no layer ever looked at stays dot-less.
* `ActiveFileWatcher` subscribes to EVERY open document's own change signal (all
  panes, all tabs), not to the active tab's text at poll time: a background tab
  edited before a tab switch invalidates its file at once.
* `projectRootChanged` is followed: the file set, the session root and the rule
  context are re-pinned, the in-flight pass is cancelled and the previous
  project's rows are dropped, so Open Folder cannot lint the old project.
* `markFileEdited(file)` (called by `ActiveFileWatcher` on the first buffer
  change, before the 700 ms analysis debounce) drops that file's upper rows; it
  falls back to the per-file/LSP layer until the next pass covers it again. The
  pass records the edit counter when it starts, so a file edited DURING a pass is
  not covered by it either.
* **Severity scales are converted exactly once**: the CLI (`vd-deeplint`) ABI
  encodes `0 = error, 1 = warning, 2/3 = note`, the LSP and the editor model use
  `1 = error, 2 = warning`. Before this the upper layer's errors were painted as
  information (and its warnings as errors) on every surface. The conversion lives
  in the merge layer; `cliDiagnostics()` still returns the raw ABI scale.

### R3 — project lint after every successful save

* `EditorPane::saveTab()` / `saveAsFile()` report every SUCCESSFUL write through
  `EditorPane::setOnDocumentSaved` (forwarded by `EditorArea` to current and
  future panes). Nothing fires on a failed save, and the callback runs after the
  bytes are on disk and the tab is refreshed.
* `ProjectLintTrigger` owns the policy: 700 ms debounce (a burst of saves is ONE
  pass), single-flight (a running pass is CANCELLED and the newest one started,
  never queued) and non-blocking (`noteSaved()` only restarts a timer).
* Ordering fix: the file snapshot is recorded BEFORE `startProjectPass()` (both
  the explicit and the automatic path); previously the first pass linted every
  file because the snapshot was still empty.
* **C8 content rule (captain's ruling, rev 2)**: the pass EXCLUDES only open
  documents whose BUFFER is modified — their bytes on disk are stale, so a disk
  read would lint the wrong text. The file that was just saved IS included (its
  bytes reached disk before the trigger fires), and any other open file that is
  unmodified on disk is included too. `BackendWiring::modifiedOpenFiles()` walks
  every pane and every tab. Consequence: saving file A while file B is dirty
  covers A and not B; after B is saved, the next pass covers B as well
  (`vedit-b3-wiring-e2e`, checks `c8.*`).
* Automatic passes are marked on the controller (`setNextPassAutomatic`), which is
  what keeps them silent (no modal dialog) and observable
  (`lastPassWasAutomatic()`, `projectPassSerial()`).

### item 12 — the ELAB/error list

`ProjectLintNotice` is the presentation policy: an EXPLICIT `project.lint` opens
the modal jumpable list (`veditElabDialog`, double click = jump); an automatic
pass only updates the clickable status-bar notice (`veditProjectLintNotice`,
each click jumps to the next error). A pass with no error-severity rows presents
nothing at all (an empty modal is worse than none).

### item 9 — diagnostic hover + one-click fix

* Hovering a diagnostic range shows severity, rule id and message
  (`diagnosticAt()` + `describeDiagnostic()`); the un-covered symbols keep the
  LSP hover, which is no longer greyed out by the server's
  `hoverProvider:false` (measured: verible answers anyway).
* The 「修复」 button appears ONLY when the engine really can fix that rule.
  `AutofixEngine` answers by running `verible-verilog-lint --ruleset=none
  --rules=<id> --autofix=inplace` on a temp copy of the editor text that keeps
  the original base name; the answer is cached per (rule, BASE NAME, the
  **configured** rules path, text), because `module-filename` and a rules file
  can change the answer for identical text. **Bound:** the rules path is the
  `tools.verible_rules` setting only. A `.rules.verible_lint` discovered by
  project discovery is NOT exposed through the C ABI (the same limitation is
  recorded in `ffi_backends.cpp`), so changing that discovered file does not
  change this answer. Otherwise the popup states the reason and offers no button.
* The probe is OFF the mouse-move path: `probeAsync()` starts the child process
  asynchronously and the popup is updated when it exits, so hovering a large
  file cannot freeze the GUI. The cache is bounded by count AND bytes.
* The popup's lifetime is tied to the editor view: it is a `QPointer`, it is
  re-parented on a tab switch (a child of the old hidden view would never show
  again) and the view's `destroyed` signal clears the cache before the next
  hover/navigation can use it. Closing a tab can no longer leave a dangling
  pointer.
* A failed whole-document replacement (read-only/refusing document) is reported
  as a failure; a claimed success is printed only when `replaceText()` returned
  true.
* Applying the fix replaces the whole document inside ONE
  `KTextEditor::Document::EditingTransaction` using `replaceText` over the whole
  range. `setText()` was measured to create TWO undo steps (one undo left the
  old text plus an empty line); `replaceText` is the single undoable edit.

### item 10 — file-tree status dots

`FileTreeModel::FileStatusRole` answers `1 error / 2 warning / 3 clean / 0
unknown` from the SAME merged view; `src/workspace/filetree/file_status_delegate.*`
paints the dot between the icon and the name (the text is shifted right, so the
two never overlap). The map is published by this lane as the shell property
`vedit.diagnostics.fileStatus` and refreshed only when
`vedit.diagnostics.viewRevision` moves — the tree never rescans the project.

### Project file set (found while testing R3)

`ProjectFileSetPoller` now calls `vd_project_expand()` (the C7 project model: the
`.f` expansion UNION a recursive scan of the folder) instead of the legacy
filelist-only `vd_project_parse()`. Measured: a folder of `.sv` files with no
`.f` produced ZERO files with the legacy call, so "Lint Project" and every
save-triggered pass reported "No project file set" — the feature was dead for
exactly the projects a new user opens.

### B5 — Rename

* The capability gate is enforced on the REQUEST path
  (`DiagnosticsController::requestRename` consults the backend's rename row and
  returns 0 with the server's reason), not only by greying the menu item.
* The cursor is validated BEFORE the name dialog: an empty identifier or a
  language keyword (`symbol_scan.cpp`) is refused with the position/reason, so
  the user never types a name for a rename that cannot happen. The dialog is
  prefilled with the symbol.
* The server's refusal is surfaced in the status bar (`RENAME-REFUSED ...`).
* Open documents are edited in ONE undo step per file; files that are not open
  are written through `atomicSaveText` (verified in `vedit-rename-e2e`).
* **Cross-file rename is NOT CLAIMED in this batch** (captain's ruling): the
  verible server answers for the OPEN document only, so the E2E prints an
  explicit `NOT-CLAIMED rename.crossfile-atomic-save` line with that reason
  instead of a SKIP. The non-open-file WRITE path itself is still proven
  (`rename.non-open-atomic-save`).
* The pre-dialog validation rejects: no token, number literals (decimal and
  based, e.g. `1'b0`), text inside `//` or `/* */` comments, text inside
  strings, system tasks/functions (`$display`) and language keywords. The
  request-path capability gate also refuses a NULL/unknown `rename` row, not
  only `supported == 0`.
