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
  edited before a tab switch invalidates its file at once. The 400 ms tick itself
  copies **no text**: it follows the active path and an O(1) document revision
  (see the cap section below).
* **The realtime session is bounded by the GOLDEN project file set (G1-a,
  batch-20261120).** The watcher reads `vedit.diagnostics.projectFiles` — the one
  expansion this lane adopted from the project lane's inputs — and only a document
  that is a member of it contributes to the session. `realtimeFileSetFor(file)` is
  `{file} ∪ closure[file]` for a member and `{file}` **alone** for a file outside
  the set (single-file diagnostics: no closure, no project file), and closure
  children are filtered against the set again (G1-b defence). `realtimeSets`
  publishes `goldenFileCount` and `outsideFileCount` so the exclusion is
  observable. An EMPTY set (no imported filelist) means "no project file set":
  the rule does not apply, and the module tree is gated on a filelist, so no
  closure exists in that mode either. The rule lives in `realtime_file_scope.h`
  and is asserted by `vedit-workspace-diagnostics-selftest` (unit) and
  `vedit-module-closure-e2e` (real catalog, real session compile set).
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

### Active-file watcher cap (batch 20261110d — BLOCKER crash fix)

The 400 ms `ActiveFileWatcher` used to copy the **whole** active document on
every tick (`document()->text()`), keep a second full copy in `lastText_`, and
re-read the file (`readAll()` + `QString::fromUtf8`) on the first tick, with no
size cap. A several-hundred-MB file therefore asked for >1.5 GB inside one
event-loop callback and died with `std::bad_alloc` → `terminate` while the user
was idle.

The rule now is **cap the work, not just the string**:

* `kActiveFileWatchByteCap` (`diagnostics_integration.h`) is **4 MiB**: larger
  than any hand-written HDL source here (all < 1 MiB) and large enough for a
  generated netlist, while bounding this path's own peak at ~5× cap = **20 MiB**
  (disk bytes + UTF-8 `QString` + one document copy), independent of document
  size. The tick itself allocates **nothing** proportional to the document.
* `poll()` no longer reads text at all: edits arrive through each document's
  `textChanged` signal, with `KTextEditor::Document::revision()` /
  `QTextDocument::revision()` as an O(1) signal-miss fallback (consulted only
  while the buffer is dirty, so a save changes nothing).
* `analyze()` decides from **metadata first**: `QFileInfo::size()` (O(1)) and a
  no-copy character count (`document()->lines()`/`lineLength()` with early exit,
  `QTextDocument::characterCount()`). Above the cap neither the document nor the
  file is read. The disk read is still bounded (`read(cap + 1)`, never
  `readAll()`) so even a file that grows between stat and read cannot exceed the
  cap.
* Above the cap is a **named state**, never a silent "no diagnostics": one
  `analysis disabled: file too large …` line (status bar + shell diagnostics,
  emitted once per file), the `vedit.diagnostics.analysisAvailability` property
  with `available:false` and the reason, and the counters published as
  `vedit.diagnostics.activeWatch` (cap bytes, buffer copies, analyses, capped
  skips/notices) so the bound is measurable rather than asserted.
* `VEDIT_NO_ACTIVE_FILE_WATCHER=1` keeps its exact meaning: the constructor
  returns before publishing stats, starting timers or subscribing to anything.
* Regression lock: `vedit-active-watcher-cap-selftest` generates an oversized
  document at runtime (never committed), asserts **zero** buffer copies and
  exactly one named line, and proves a normal small file is still copied and
  handed to the analysis. `vedit-diagnostics-product-seam` keeps proving the
  end-to-end small-file analysis against the real verible LSP.

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

### Lint volume limits (U9b): count bounds, not byte caps

The `--diag-json` document is **streamed** (`crates/vd-deeplint/src/diagnostic.rs`
`parse_diagnostics_reader`): the raw document is never held in memory and the old
8 MiB byte cap no longer exists, so a real project's warning flood cannot fail a
file or drop it from the pass.

* **De-duplication** is keyed on (reported file, line, column, code, message):
  a warning slang repeats per elaborated instance becomes ONE row with its
  repeat count; two different findings at the same location (different code or
  message) are never merged.
* **Retention is bounded by count**: **500 distinct findings per file**
  (`DEFAULT_MAX_DIAGNOSTICS`) and **20,000 distinct findings per project pass**
  (`DEFAULT_MAX_PASS_DIAGNOSTICS`, tunable through
  `EngineOptions.max_pass_diagnostics`). Findings past the bounds are counted,
  never silently discarded.
* The pass never fails on volume: its `VdLintProgress.message` summary reports
  `N repeat(s) collapsed` and `N dropped` (exact when every file answered, "at
  least N" when a file failed and its volume is unknown).
* A file that could not be processed is **named with its reason** in the pass
  summary (first two, then "and N more"), so a truncated pass is still a
  truthful pass.
* The pass-level bounds are engine-agnostic (slang and verible-verilog-lint
  both flow through the same collection code).

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
`vedit.diagnostics.fileStatus`; the tree never rescans the project.

**T9 (batch 20261110d) changed the publish CONTRACT** — one keystroke must not
rebuild the project view:

| property | meaning |
|---|---|
| `vedit.diagnostics.fileStatus` | the last FULL map. Written on a structural change (finished pass, lane rows, scope), not per keystroke. |
| `vedit.diagnostics.viewRevision` | token of that full map. |
| `vedit.diagnostics.fileStatusDelta` | the entries whose verdict moved in the latest per-file publish (an edit, a retained verdict). |
| `vedit.diagnostics.fileStatusDeltaRevision` | token of the delta; consumers apply the delta on top of the full map. |
| `vedit.diagnostics.statusRevision` | moves for BOTH kinds — the single "something changed, repaint" token the file tree and the Project panel poll. |

`DiagnosticsController::takeLivePublish()` is the ONE decision (full vs delta,
plus cheap exact counts); the per-file upper-row store makes dropping one file's
rows and reading one file's verdict O(that file). A hidden dock does no widget
work (the 300 ms panel poll still advances the analysis, never the paint), and a
typing burst publishes one delta for the edited file, not the project.

**Three independent clocks — publishing is synchronous, CONSUMING is not**
(batch-20261114 T1; corrects the earlier "the group is synchronous, so there is
no phase difference" reading). The pieces move on different timers:

| hop | clock |
|---|---|
| full status map published to the shell properties | 400 ms `liveTimer_` → `refreshLiveState()` |
| "From filelist" grouping (`vedit.diagnostics.projectFilesRevision`) | published synchronously with the project set, **consumed by `FileTreeWidget` on its 500 ms tick** (`syncFileStatuses()`), which also reapplies the dots |
| Project panel repaint | its own 500 ms tick (the delegate still reads `fileStatusAt()` lazily) |

The property `vedit.diagnostics.viewRevision` is the token of the published full
map: a consumer that refreshes lazily (the tree's `FileStatusRole`, the Project
panel's `fileStatusAt()`) rebuilds its cache only when that token moves. So a
reader that asserts a dot as soon as the group appears can still be reading the
map published BEFORE the lint pass finished — a test-timing defect, never a
product one (the product surface is eventually consistent by design). The RM-9
end-to-end test now waits for `viewRevision` to pass the pass's completion point
(`rm9_status_dots_e2e.cpp`, `OBSERVED rm9.published-past-pass`) instead of
sleeping for a fixed period.

Both consumers read the published revision lazily on every query, so a correct state is not
tied to a tick at all — only the *first observation* can lag by up to one tick; after that the
query itself refreshes the cache. "Late by one tick" is therefore a visibility delay, never a
wrong dot.

### Project file set (found while testing R3; G1-a "expand once" in batch-20261120)

`ProjectFileSetPoller` no longer expands anything when a PROJECT LANE is
installed: that lane owns the ONE expansion (`.f` expansion + `[project]
filelists` + the tools page's `-I`/`-D`) and publishes it as
`vedit.project.files` + `vedit.project.filesRevision` (with
`includeDirs`/`defines`/`hasFilelist`/`filesPending`). This lane adopts that
answer and republishes it as `vedit.diagnostics.projectFiles` — one publisher,
one consumer, no second expansion. A shell WITHOUT a project lane (a standalone
diagnostics harness) keeps the historical fallback to its own
`vd_project_expand_v3` job: there, its expansion is the only one in the process.
`vedit.diagnostics.projectFilesState.source` reports which path ran
(`project-lane` / `self-expansion`), so a test never has to read a log line.

History: the lane used to call the legacy filelist-only `vd_project_parse()`, so
a folder of `.sv` files with no `.f` produced ZERO files and "Lint Project"
reported "No project file set" — dead for exactly the projects a new user opens.
The modern expansion replaced it, and AC-2.1 gated it on an imported filelist.

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
