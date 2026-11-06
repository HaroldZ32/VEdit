# Independent verification — packaged slang, unified path display, `.f` import

- Repository: `/home/haroldzhao/MyProj/DshProj/VEdit`
- HEAD: `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f` (uncommitted worktree)
- Date: 2026-11-04 (Asia/Shanghai)
- Reviewed: writer A's `evidence/module-tree-slang-fix/REPORT.md` and writer B's
  `evidence/ux-path-and-filelist/REPORT.md`, the changed code/tests/docs, and the
  relevant existing call paths. The writers' evidence was read only; it was not
  modified.

## Verdict

**FAIL — request changes.** Risk level: **Medium-High**.

The three fixes are functionally real and independently reproduced: the gate is
green (ctest 37/37), every focused suite passes with a check-name set identical
to the writers' logs, the slang RED→GREEN reproduces both at the resolver level
and end-to-end through the real AppDir loader (`VEDIT_SLANG` unset), the `.f`
import really sets the root, imports, is idempotent and surfaces parser
diagnostics in the Project panel, and the display rule is applied at every
enumerated surface while open/dedup keys stay absolute. The blocking defect is in
the packaging guard added by Fix 3: `packaging/runtime/AppRun` unconditionally
overwrites a caller's `VEDIT_SLANG` (and exports a path even when the bundled
file is absent), so the documented env override no longer works in the packaged
launch and a stale/dead value can mask the diagnostics-resolved toolchain. That
is an undeclared regression and must be repaired. The palette's index also does
not follow the runtime root set by a `.f` (pre-existing, declared, Medium
follow-up); the remaining items are Nits.

## What I ran, and what I observed

All product commands ran in the pinned image (`vedit-qt6-build:local`; the two
Rust gates in `vedit-build:local`). Exact commands are in the logs named below.

| # | Command | Observed |
|---|---|---|
| 1 | `git rev-parse HEAD`; `git status --porcelain=v1 -uall`; `git diff --stat`; mtime audit | HEAD `4e2d0a5`; the worktree was already dirty (writer B's pre-task snapshot has 340 porcelain lines). Source files modified after 16:05 are exactly the two writers' claimed sets (`src/app/*`, `src/common/path_display.h`, the project/palette/search/filetree lanes, `docs/USER_GUIDE.md`, `packaging/runtime/AppRun`, `crates/vd-deeplint/src/binary.rs`, `crates/vd-config/src/tools.rs`, module-tree lane). No frozen file has a task-time mtime (root `CMakeLists.txt` 2026-11-02; `Cargo.*` 2026-11-02; `rust-toolchain.toml`/`package.json` 2026-10-30; `scripts/build-qt.sh`/`build/gates/**` 2026-11-02). No `crates/**` outside `vd-deeplint`/`vd-config` changed after 16:05. No test file was deleted by these fixes (the only deletions are the pre-existing T29 `src/syntax/folding/**` set). |
| 2 | `bash scripts/build-qt.sh` | **EXIT=0**; Rust layer + Qt build + ctest `100% tests passed, 0 tests failed out of 37`; xcb/software smoke `M0-SMOKE ok=1`. |
| 3 | `target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/...` | **312 checks, 0 failures**; the new `path.display.*`, `path.display.{title,tab-tooltip}.*`, `path.display.open-key-still-absolute`, `open.filter.filelist`, `open.filter.keeps-existing-types`, `open.signal.*` checks all PASS. |
| 4 | `target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project` | **150 checks, 0 failures**; the new `display.files.*`, `display.panel.*`, `display.filelist.*`, `filelist.plan.*`, and `filelist-open.*` checks all PASS. |
| 5 | palette selftest (`--screenshots /tmp/...`) and search selftest | palette **46/0** (`index.outside-root-absolute`, `index.inside-root-relative` PASS); search **106 checks, 0 failures, 2 skipped** (same skip count in the writer's log). |
| 6 | `ctest -R vedit-module-tree-selftest`, then the same binary directly | **61 checks, 0 failures**; `toolchain.*`, `packaged.appdir-slang`, `packaged.answered`, `packaged.instances — instances=3` PASS. |
| 7 | standalone filetree lane build + `vedit-filetree-tests` | **113 checks, 0 failures**; `tree.tooltip.relative` (`alpha/deep`) and `tree.pathrole.absolute` PASS. |
| 8 | check-name set comparison: writer logs vs my logs | Distinct labels are **identical** for app (312), project (150), palette (46), search (112 distinct labels; harness summary 106 checks/0 failures/2 skipped), and module-tree (61). No new check was renamed away or deleted. |
| 9 | real AppDir loader E2E, `VEDIT_SLANG` unset: `$APPDIR/ld-linux-x86-64.so.2 --library-path $APPDIR/lib .../vedit-module-tree-selftest probes/slang-spike/fixture $APPDIR/bin/slang "" evidence/m-tree/fixture-generate` | **EXIT=0, 61 checks, 0 failures**; `packaged.appdir-slang — pre-existing .../packaging/runtime/out/AppDir/bin/slang`; `packaged.answered`; `packaged.instances — instances=3`. The loader makes the AppDir root the executable dir, exactly the packaged shape. |
| 10 | independent resolver RED→GREEN harness (`rustc`, HEAD `binary.rs` vs current `binary.rs`; `resolve_binary(None, None, <tmp>/AppDir)`, tool at `<tmp>/AppDir/bin/slang`) | old resolver: **4 attempts, chosen=None**, tarball base still works; current resolver: **5 attempts, chosen=`<tmp>/AppDir/bin/slang`**, with `exe_dir/bin/slang [found]`. `VEDIT_SLANG` absent in both. |
| 11 | controlled E2E RED: built a pre-fix static library from a scratch copy (only the new `ExeRelativeRootBin` probe/variant/arm removed; product tree untouched), temporarily relinked the module-tree selftest, ran the AppDir-loader E2E; then restored the original library and relinked | RED: `FAIL packaged.answered — elaboration failed: slang failed: slang binary not found (tried 4 location(s); set VEDIT_SLANG)`, **60 checks, 1 failure, EXIT=1**. Restored `target/release/libvd_ffi.a` to hash `4e262aad…` (verified, including its hardlinked `deps/` copy); GREEN re-run: **61/0, EXIT=0**. |
| 12 | `cargo test -p vd-deeplint -p vd-config` in `vedit-build:local` | **EXIT=0**; all suites pass, including `binary::tests::appdir_root_layout_is_found_relative_to_the_executable`, `..._finds_verible_lint`, `tools::tests::bundled_payload_is_found_in_the_appdir_root_bin`, and `all_attempts_are_listed_when_nothing_is_found` (5 attempts; every old base still asserted). |
| 13 | `cargo fmt --all -- --check`; `cargo clippy -p vd-deeplint -p vd-config --all-targets -- -D warnings`; `bash build/gates/docs.sh` | FMT **EXIT=0**; CLIPPY **EXIT=0**; docs gate **PASS**. |
| 14 | AppRun caller-env probe (fake AppDir whose "loader" prints the environment) | caller `VEDIT_SLANG=/caller/slang`, `VEDIT_VERIBLE_LS=/caller/ls` → observed `VEDIT_SLANG=<fake>/bin/slang` (overwritten), `VEDIT_VERIBLE_LS=/caller/ls` (preserved). The slang export is unconditional. |
| 15 | display/open-key audit (code + selftests) | `displayPath()` call sites: `EditorPane::refreshTab` (tab tooltip), `ApplicationShell::updateStatusBar` (title/dirty tooltip), `FileTreeModel::data` (tooltip), `ProjectModel::displayFiles/displayFilelists` + `ProjectPanel::updateUi`, `WorkspaceFileIndex::relativeOf`, `GlobalSearch` hit construction. Absolute keys: `EditorPane::openFile`/`tabIndexForPath`, project-panel `Qt::UserRole`, palette `absolutePath`, search `SearchHit::filePath`, filetree `PathRole`. Tests cover inside-root relative, outside-root absolute, no-root absolute, sibling prefix, root itself, trailing-slash root, title/tooltip, and `open-key-still-absolute`. |

## Findings (ranked)

### F1 — High — `AppRun` overwrites a caller's `VEDIT_SLANG`; the env fallback is dead in the packaged launch

- **Symptom.** Launching the packaged AppDir/AppImage through `packaging/runtime/AppRun`
  with `VEDIT_SLANG=/caller/slang` results in the child seeing
  `VEDIT_SLANG=$APPDIR/bin/slang`. The verible variables are guarded
  (`[ -z ... ] && [ -x ... ]`), but the slang export is unconditional and does
  not even test that the bundled file exists. `moduleTreeSlangBinary()` treats
  `VEDIT_SLANG` as the highest-precedence source, and the Rust resolver treats
  the value it hands over as the explicit option, so the diagnostics lane's
  published `vedit.diagnostics.toolchainSlang` (which can be a local checkout or
  a PATH-resolved tool when the package does not carry a working bundled slang)
  is masked. Result: the documented `VEDIT_SLANG` escape hatch cannot be used in
  the packaged product, and a package with a missing/broken `$APPDIR/bin/slang`
  can still fail the module tree even though the diagnostics lane resolved a
  working tool.
- **Expected.** Preserve a caller-provided `VEDIT_SLANG` and only export the
  bundled path when it is unset and executable, exactly like the verible block
  (and add a probe/test case asserting the preservation, since the writer's
  `13-apprun-env.log` only checks `VEDIT_VERIBLE_LS`).
- **Where.** `packaging/runtime/AppRun` (the `export VEDIT_SLANG` in the bundled
  toolchain block); `src/workspace/module_tree/module_tree_integration.cpp`
  (`moduleTreeSlangBinary`, env-first); `src/workspace/diagnostics/diagnostics_integration.cpp`
  (publishes `vedit.diagnostics.toolchainSlang`). Contract sources:
  `docs/USER_GUIDE.md` §8 troubleshooting ("set `VEDIT_SLANG` to the actual
  path") and `AGENT.md` §Runtime toolchain discovery (env is one of the three
  supported ways).
- **Evidence.** `logs/41-apprun-override-probe.log` (caller value overwritten);
  `logs/42-display-and-docs-audit.txt` (status/env audit); the writer's
  `evidence/module-tree-slang-fix/13-apprun-env.log` (only the verible
  preservation case); the RED scenario is the `packaged.answered` failure in
  `logs/35-appdir-loader-red.log` — the same failure mode a dead bundled env
  would reproduce when the diagnostics path is available.
- **Why this is not a Nit.** It is a regression introduced by this task's
  AppRun change (previously AppRun did not set `VEDIT_SLANG`, so a caller's
  override survived), it contradicts a documented control, and its failure mode
  is the exact "slang binary not found" class the fix is meant to eliminate.

### F2 — Medium — the palette index does not follow the runtime root set by opening a `.f`

- **Symptom.** `PaletteIntegration::install()` sets the index root once from
  `options.projectRoot`; `PaletteIntegration` has no `projectRootChanged`
  subscription. In the new no-project workflow, opening a `.f` calls
  `ApplicationShell::setProjectRoot(filelistDir)` and the project panel imports
  the file set, but the palette index still holds the old (usually empty) root,
  so `Ctrl+P` shows nothing until restart. The same staleness already applied to
  File → Open Folder; the `.f` flow makes it a primary path.
- **Expected.** Subscribe to `ApplicationShell::projectRootChanged` and call
  `PaletteIntegration::setProjectRoot` / `WorkspaceFileIndex::setRoot` (the
  search lane already does exactly this in `search_integration.cpp`), or state
  explicitly that the palette is out of scope for runtime root changes.
- **Where.** `src/workspace/palette/palette_integration.cpp`
  (`PaletteIntegration::install`, `setProjectRoot`), the new root set in
  `src/workspace/project/project_integration.cpp` (`fileOpened` handler), and
  the pattern in `src/workspace/search/search_integration.cpp`.
- **Evidence.** `logs/42-display-and-docs-audit.txt` (root-subscriber list:
  filetree/search/vcs/module-tree/project subscribe; palette is absent); the
  project selftest check `filelist-open.root-is-filelist-directory` proves the
  root really changes.
- **Declared by writer B** (REPORT §4 limit 3: "pre-existing and out of scope").
  It is a real user-visible gap in the new flow, but it predates this change and
  is not a regression; treat it as a Medium follow-up rather than the blocking
  defect.

### F3 — Nit — transient status-bar "Opened <absolute>" messages, and the guide's "all display paths" wording

- **Symptom.** The palette, Files-tree and search-jump status messages still
  interpolate the absolute path (`Opened %1`), while the title, tooltips and
  panel rows use the display rule. The guide's path-display section opens with
  "all places that display a path use the same rule" and then lists the
  surfaces; these transient messages are path displays but are not in the list.
- **Expected.** Use `displayPath(shell.projectRoot(), path)` for those
  confirmations, or narrow the guide sentence to the enumerated surfaces (the
  writer already declared them out of scope).
- **Where.** `src/workspace/palette/palette_integration.cpp` (`openInEditor`),
  `src/workspace/filetree/filetree_integration.cpp` (`openFileInEditor`),
  `src/workspace/search/search_jump_target.cpp` (`EditorJumpTarget::jumpTo`),
  `docs/USER_GUIDE.md` path-display section.
- **Evidence.** `logs/42-display-and-docs-audit.txt`; the writer's REPORT §4
  limit 4 declares the status messages unchanged. Transient and unambiguous, so
  Nit, not a blocker.

### F4 — Nit — "edit then re-import" is not what re-opening the tab does

- **Symptom.** `ProjectModel::importFilelists()` returns early when the accepted
  filelist list is unchanged, so re-opening an already-imported `.f` tab is a
  no-op (and the panel still prints "Imported 1 filelist"). Editing the `.f` and
  re-opening the document therefore does not refresh the file set; the user has
  to press the Project panel's Refresh (which re-expands and re-parses) or
  remove and re-add the filelist. The guide's "filelist … can be edited and
  re-imported" can be read as "re-open to re-import".
- **Expected.** Say how to re-import (Refresh, or remove + import), or make the
  re-open path trigger a re-expansion while keeping the filelist entry unique;
  do not print "Imported" for a no-op.
- **Where.** `src/workspace/project/project_model.cpp` (`ProjectModel::importFilelists`),
  `src/workspace/project/project_panel.cpp` (`ProjectPanel::refresh`,
  `ProjectPanel::importFilelists`), `docs/USER_GUIDE.md` filelist section.
- **Evidence.** project selftest `filelist.import-no-duplicate`,
  `filelist-open.idempotent-filelists/-files`; `logs/22-project-selftest.log`.
  Nit (documentation/UX), not a functional failure of the required idempotence.

## Declined to judge

- **Session-restored `.f`** (writer B limit 6): no dedicated assertion; the code
  follows the same open rule. With a saved root, the shell loads it before the
  lanes and `model->hasProject()` is true, so restore imports into the saved
  root; with no saved root it sets the filelist's directory, which is the same
  effect as opening it. I did not run a session-restore probe. Not counted as a
  defect.
- **Second filelist file-set assertion** (writer B's "project open → import"
  case): the end-to-end check asserts the root is unchanged and the filelist is
  kept, but the second fixture's entry is an absolute path already in the set,
  so it does not prove a *new* file from that filelist reaches `files()`. The
  same import/expansion path is proven by the no-project case
  (`filelist-open.file-set-imported`) and the `filelist.import` checks; this is
  a test-strength note, not a code defect.
- **`filelist.missing` fixture change**: the writer changed the diagnostic
  fixture from a missing source entry (deliberately silent) to
  `+incdir+no_such_dir` (documented `filelist.missing`). Legitimate; not a
  weakened test (the Rust parser tests still cover silent missing sources and
  nested `-f` diagnostics).
- **Search `skipped=2`**: identical in the writer's and my logs;
  environment-dependent; `checks=106 failures=0` in both. Not a weakening.
- **The `.f` bad-nested-include trigger**: the end-to-end test exercises a
  missing `+incdir+` line, not a nested `-f` line; the panel path is generic and
  the Rust parser tests cover nested `-f`. Test-trigger nuance, not a defect.
- **AppRun's `PATH` export block**: the diff carries a "batch 6a" comment and
  the writer's report does not claim it; no task-time evidence it was written
  here. Not counted.
- **`AGENT.md` "bundled > PATH > env" vs `crates/vd-config/src/tools.rs`**
  (config > env > bundled, no PATH): pre-existing, outside the touched slang
  resolver; the writer's "PATH is never consulted" is true for the code they
  changed.
- **`moduleTreeSlangBinary` env-first precedence**: deliberate and documented
  (the header and writer A's REPORT §3 call it the lane's historical override);
  the packaging consequence is F1, not a separate precedence bug.
- **Module-tree first-refresh ordering**: checked; the holder's first refresh is
  queued with `QTimer::singleShot(0, ...)` and `installDiagnostics` runs
  synchronously before the event loop returns, so the published property is
  available. No defect.
- **No GUI/AppImage run, no native QFileDialog, symlink/realpath normalisation,
  status-bar messages**: declared limits; the real-loader E2E covers the
  packaged layout, and the filter/signal contract is asserted. F3 covers the
  status messages.

## Bounds of the review

- Read: the two writer reports, the changed source/tests/docs and the relevant
  existing call paths (`src/app`, project/palette/search/filetree lanes,
  diagnostics, module-tree, `crates/vd-deeplint`, `crates/vd-config`, packaging
  `AppRun`), plus the writer logs used for check-name comparison. Did not read
  the entire ~40k-line pre-existing dirty diff, every evidence file from earlier
  batches, or the full Rust workspace.
- Ran: the Qt build gate, focused app/project/palette/search/module-tree/filetree
  suites, the AppDir-loader E2E, the resolver RED/GREEN harness, a controlled
  pre-fix-library relink RED, Rust unit tests/fmt/clippy, the docs gate, and the
  AppRun probe. Did not run the delivered AppImage as a GUI, a real display
  server, the native QFileDialog, the writers' `run-checks.sh` verbatim (used
  equivalent commands), a package *without* a bundled slang, a session-restore
  probe, or a symlinked-root probe. The `.f` external-relative filelist case was
  verified by code/Rust-path reading, not a new app-level probe.
- No product source, test, or writer evidence was modified. The controlled RED
  swapped only the build artifact `target/release/libvd_ffi.a` (and its
  hardlinked `deps/` copy) with a pre-fix library, then restored it to the
  original hash `4e262aad…` and relinked; the restored GREEN is 61/0. Running
  the gate refreshed its own `build/evidence/**`, `evidence/ffi-abi/logs/**` and
  `evidence/m0-build/**` logs (gate behavior, not writer evidence);
  `crates/vd-ffi/include/vd_ffi.h` was regenerated by the build and is
  byte-identical to the writer's gate copy. The current `git status` count
  includes this verification's untracked `evidence/ux-fixes-verification/` and
  the gate-refreshed logs, so it is not a clean-tree measure.

## Evidence index

- `logs/00-inventory.txt` — HEAD, status counts/baseline, frozen mtimes, recent
  source list, crates, deleted files, claimed-file lists.
- `logs/05-check-name-compare.txt` — writer vs independent check-label sets and
  suite summaries.
- `logs/10-build-qt.log` — full gate (`EXIT=0`, ctest 37/37, smoke).
- `logs/20-app-selfcheck.log`, `logs/22-project-selftest.log`,
  `logs/24-palette-selftest.log`, `logs/26-search-selftest.log`,
  `logs/29-module-tree-direct.log`, `logs/37-filetree-standalone.log` — focused
  suites.
- `logs/30-appdir-loader-e2e.log` — real AppDir-loader GREEN (61/0,
  `packaged.answered`, `instances=3`).
- `logs/31-resolver-red-green.log` — HEAD resolver vs current resolver harness
  (4 attempts/None → 5 attempts/AppDir bin).
- `logs/33-prefix-lib-build.log`, `logs/34-relink-red.log`,
  `logs/35-appdir-loader-red.log` — pre-fix library build, relink/restore hashes,
  and the exact `packaged.answered` RED (60 checks, 1 failure, EXIT=1).
- `logs/36-appdir-loader-green-restored.log` — GREEN after restoring the
  original library.
- `logs/32-cargo-test-rust.log` — Rust unit tests for the touched crates.
- `logs/38-39-rust-gates-console.log`, `logs/38-cargo-fmt.log`,
  `logs/39-cargo-clippy.log` — fmt/clippy.
- `logs/40-docs-gate.log` — docs gate.
- `logs/41-apprun-override-probe.log` — caller `VEDIT_SLANG` overwritten,
  `VEDIT_VERIBLE_LS` preserved.
- `logs/42-display-and-docs-audit.txt` — display call sites, absolute keys,
  root subscribers, status messages, guide wording.
- `scratch/*.rs` — the HEAD/current/pre-fix resolver copies and the RED/GREEN
  harness sources (large build artifacts removed).
