# FIX 1 + FIX 2 — unified path display and `.f` filelist import (Qt6 product)

Date: 2026-11-04 (Asia/Shanghai)
Repository: VEdit (`/home/haroldzhao/MyProj/DshProj/VEdit`)
Branch: `main` (working tree, not committed — see §0)
Gate: `bash scripts/build-qt.sh` → **EXIT=0**, ctest **37/37** (see §4)

## 0. Baseline and scope

The working tree was **already dirty** before this task started (333 porcelain
entries from earlier deliveries, including modified `CMakeLists.txt`,
`docs/USER_GUIDE.md`, `src/app/*`, …). `logs/00-git-status-before.txt` is the
snapshot taken before the first edit, and `logs/00-git-head.txt` is `HEAD`
(`4e2d0a5`). This report therefore describes the change by file and behaviour,
not by a clean-tree diff.

Frozen files were **not touched** by this task: root `CMakeLists.txt`,
`Cargo.toml`/`Cargo.lock`, `rust-toolchain.toml`, `build/gates/**`,
`package.json`, `scripts/build-qt.sh`. The Rust service layer was **not
changed** (no `crates/**` edit), so clippy/fmt are not applicable to this change.
Lane `CMakeLists.txt` files were edited only to add one include directory (see
§1.1); they are not frozen.

## 1. FIX 1 — one path-display rule

User ruling implemented verbatim: **project-root-relative when a root exists and
the path is inside it, absolute otherwise.** The open key and the dedup key stay
absolute; only the rendering consults the rule.

### 1.1 The shared rule

`src/common/path_display.h` (new, header-only, QtCore-only):

- `vedit::paths::normalizedRoot(root)` — absolute + `QDir::cleanPath` (no
  trailing slash, no `.`/`..`); empty stays empty ("no project"). Symlinks are
  deliberately **not** resolved: the project service spells file paths under the
  root the caller passed, so resolving only one side would turn every under-root
  path into an absolute one.
- `vedit::paths::displayPath(root, path)` — returns the path relative to the
  normalised root without a leading `./` when it is strictly inside it, the
  normalised absolute path otherwise. The root itself stays absolute (no bare
  `.`); a sibling such as `/a/root2` next to `/a/root` is outside (the separator
  is part of the prefix test).

It is header-only so a workspace lane can include it in its **standalone** build
without linking `src/app` (the lane seam's §2.4 rule is preserved). Include
directories added (one line per lane): `src/app/CMakeLists.txt`,
`src/workspace/{filetree,palette,search,project}/CMakeLists.txt`.

### 1.2 Surfaces changed

| Surface | File | Behaviour now |
|---|---|---|
| Editor tab **tooltip** | `src/app/editor_area.cpp:570` | `displayPath(root, path)`; tab **text** stays the file name (it renders a *name*, not a path — the open key is unchanged) |
| **Window title** | `src/app/app_shell.cpp` (`updateStatusBar`) | subject = display path; the unsaved-changes tooltip uses the same value |
| Root normalisation | `src/app/app_shell.cpp:86,335,731` | `--project`, the restored session, `setProjectRoot()` and the project model all use `normalizedRoot` |
| **Files tree** tooltip | `src/workspace/filetree/file_tree_model.cpp:173` | `displayPath(treeRoot, node->path)`; `PathRole` keeps the absolute path (opening unchanged) |
| **Project panel** | `src/workspace/project/project_model.cpp:303,313`, `project_panel.cpp:455` | files *and* filelists display by the one rule; the model keeps absolute spellings (`files()`, `filelists()`), the list carries the absolute path in `Qt::UserRole` so Remove still works |
| **Command palette** | `src/workspace/palette/file_index.cpp:72` | `relativeOf()` delegates to `displayPath`; `setRoot()` uses `normalizedRoot` |
| **Search results** | `src/workspace/search/global_search.cpp:186` | `hit.relativePath` (the displayed row) is `displayPath(searchRoot, hit.filePath)`; `hit.filePath` remains the absolute jump key |

No opening or dedup code was changed: `EditorPane::openFile`,
`tabIndexForPath`, `ProjectModel::files()/filelists()`, palette
`absolutePath()`, search `SearchHit::filePath` and the jump registry still use
absolute paths.

### 1.3 Tests added (real assertions, no weakening)

`vedit-app --selfcheck` (`src/app/main.cpp`), all observed **PASS**:

- `path.display.under-root` → `rtl/top.sv`
- `path.display.under-root.normalized-root` (root with a trailing slash)
- `path.display.outside-root` → absolute
- `path.display.no-root` → absolute
- `path.display.sibling-prefix-outside` (`/a/root2` is not inside `/a/root`)
- `path.display.root-itself-absolute` (no bare `.`)
- `path.display.root-normalized`
- `path.display.title.absolute-no-root`, `path.display.tab-tooltip.absolute-no-root`
- `path.display.window-title.relative` (`VEdit — rtl/foo.sv[*]`),
  `path.display.tab-tooltip.relative` (`rtl/foo.sv`),
  `path.display.open-key-still-absolute` (the pane still opens the absolute path)
- `path.display.window-title.outside-absolute`,
  `path.display.tab-tooltip.outside-absolute`

Project lane (`vedit-workspace-project-selftest`), all **PASS**:
`display.files.*` (inside relative, outside absolute, open key absolute),
`display.filelists/panel.*` (an inside-root filelist is displayed relative while
`filelists()` keeps it absolute; an outside one is displayed absolute).

Palette lane: `index.outside-root-absolute`, `index.inside-root-relative`.
File tree lane (standalone suite): `tree.tooltip.relative` (`alpha/deep`) and
`tree.pathrole.absolute`.

## 2. FIX 2 — opening a `.f` / `.F` / `.flist` imports it as the project

### 2.1 One lane-agnostic app signal

`ApplicationShell::fileOpened(const QString &absolutePath)` (`src/app/app_shell.h:205`)
is emitted from the **single open seam**:

- `EditorArea::setOnFileOpened()` / `EditorPane::setOnFileOpened()`
  (`src/app/editor_area.h:150`, `editor_area.cpp:788,837,1288,1832`) — every
  successful `EditorPane::openFile()` raises it, and every pane (current and
  future, including restored panes) is wired by `EditorArea::createPane()`.
- `ApplicationShell` connects that callback to its Qt signal in the constructor
  (`src/app/app_shell.cpp:110`).

Because File > Open, the CLI (`openPaths`), the Files tree double click, Ctrl+P,
the search jump and the project-panel double click all end in
`EditorPane::openFile`, they all fire this one signal. `src/app` does **not**
include or link the project lane: the project lane subscribes to the signal
(`src/workspace/project/project_integration.cpp:85`).

The signal also fires when an already-open tab is focused (the handler is
idempotent); this keeps "open the same `.f` again" able to import it if it was
not imported yet.

### 2.2 The project lane decides

`src/workspace/project/project_filelist.{h,cpp}` (new) — `planOpenedFilelist(path, hasProject)`:

- extension `.f` / `.F` / `.flist`, case-insensitive (the same rule as
  `vd-config`'s `has_f_extension`); non-filelists are ignored;
- **project open** → import into the CURRENT root (`needsRoot=false`);
- **no project** → `needsRoot=true`, root = the filelist's own directory
  (`RootSource::Filelist`).

`installProject()` subscribes **before** the empty-state early return, so the
no-project case is live: `shell.setProjectRoot(root)` (which the lane's existing
`projectRootChanged` connection opens on the model) and then
`ProjectPanel::importFilelists()` → `ProjectModel::importFilelists()` → the
existing Rust parser (`crates/vd-config/src/filelist.rs`). Nothing was
re-implemented in C++.

- **Idempotent:** `ProjectModel::importFilelists` rejects duplicates (returns
  true without a re-parse); tested by opening the same `.f` twice.
- **Diagnostics:** the parse warnings land in `ProjectModel::diagnostics()` and
  are rendered by the existing Project panel diagnostics list
  (`project_panel.cpp`, `updateUi`). No modal is raised from the app layer.
- **Open filter:** `ApplicationShell::openFileDialogFilter()`
  (`src/app/app_shell.h:33`, `app_shell.cpp:39`) is the filter the File > Open
  dialog uses; it now offers `*.v *.sv *.svh *.vh *.f *.F *.flist` and keeps the
  source-only and All Files filters.

The opened filelist still opens as a normal text tab (its document open key is
absolute); it is editable and re-importable.

### 2.3 Tests added (real assertions, no weakening)

`vedit-app --selfcheck`:

- `open.filter.filelist` (contains `*.f`, `*.F`, `*.flist`) and
  `open.filter.keeps-existing-types` (`*.v *.sv *.svh *.vh`);
- `open.signal.opens` / `open.signal.one-per-open` (two opens → the two absolute
  paths, in order); `open.signal.failed-open-silent` (a failed open emits
  nothing); `open.signal.direct-pane-open` (the `EditorPane::openFile` seam used
  by the Files tree / palette / search jump / project panel fires it too).

`vedit-workspace-project-selftest` (lane-only part):

- `filelist.plan.lowercase-f`, `.uppercase-f`, `.flist`, `.non-filelist` (negative
  control), `.empty`.

End to end through a **real `ApplicationShell`** (same selftest, guarded by
`VEDIT_PROJECT_HAVE_APP_SHELL` = the root build):

1. `filelist-open.starts-without-project`;
2. opening `design.f` → `filelist-open.signal`, root becomes the filelist's
   directory (`filelist-open.root-is-filelist-directory`), the filelist is
   imported (`filelist-open.filelist-imported`) and its entry reaches the file
   set (`filelist-open.file-set-imported`); the panel shows it relative
   (`filelist-open.panel-relative`); the document tab is open
   (`filelist-open.document-tab`);
3. opening the same `.f` again → `filelist-open.idempotent-filelists` /
   `-files` (exactly one entry, no duplicate files);
4. opening `extra.flist` elsewhere while the project is open →
   `filelist-open.second-root-unchanged` and the second filelist is kept
   (`filelist-open.second-filelist-kept`) — the root was **not** re-opened;
5. opening `broken.F` **through `EditorPane::openFile`** (the panel/palette/search
   seam, not `openPaths`) → imported (`filelist-open.pane-filelist-kept`), and
   its parser warning reaches the panel
   (`filelist-open.parse-diagnostic`, `filelist-open.panel-shows-diagnostics`).

So all three extensions are exercised end to end (`.f`, `.flist`, `.F`).

## 3. Commands and observed results

All product commands ran inside the pinned image (`vedit-qt6-build:local`,
workspace at `/vedit-build`); the exact command line is the first line of every
log under `logs/`.

| # | Command | Observed |
|---|---|---|
| 1 | `bash scripts/build-qt.sh` | `EXIT=0`; `100% tests passed, 0 tests failed out of 37`; `[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke`; `M0-SMOKE ok=1 platform=xcb …` (`logs/10-build-qt.log`, `logs/10-build-qt.exit`) |
| 2 | `vedit-app --selfcheck --state-dir /tmp/ux-selfcheck-state` | `APP-SHELL-SELFTEST ok=1 checks=312 failures=0` (`logs/20-app-selfcheck.log`, fix lines in `logs/21-app-selfcheck-fix-lines.txt`) |
| 3 | `vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project` | `PROJECT-MODEL-SELFTEST ok=1 checks=150 failures=0` (`logs/22-project-selftest.log`, fix lines in `logs/23-project-selftest-fix-lines.txt`) |
| 4 | `vedit-workspace-palette-selftest` | `PALETTE-SELFTEST ok=1 checks=46 failures=0` (`logs/24-palette-selftest.log`) |
| 5 | `vedit-workspace-search-selftest` | `WORKSPACE-SEARCH-SELFTEST ok=1 checks=106 failures=0 skipped=2` (`logs/26-search-selftest.log`) |
| 6 | standalone file-tree lane: configure + build + `vedit-filetree-tests` | `FILETREE-TESTS checks=113 failures=0`; `tree.tooltip.relative ok=1`, `tree.pathrole.absolute ok=1` (`logs/30…32`) |
| 7 | standalone **project lane** (no `src/app`): configure + build + `ctest` | build `EXIT=0`; `100% tests passed, 0 tests failed out of 1` (`logs/34`, `logs/35`) — the lane still builds and tests without the shell, so `project_filelist.cpp` and the conditional app-shell section are correctly guarded |
| 8 | `bash build/gates/docs.sh` (host) | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` |

Reproduce all focused checks with:
`bash evidence/ux-path-and-filelist/run-checks.sh` (its summary is
`logs/33-gate-summary.txt`; the script writes `logs/20…35`).

### Verification notes (defects found while testing, both fixed)

- The first version of the new app-signal assertion connected a lambda that
  captured a stack `QStringList` and left the connection alive past the block;
  the next open (`save.demo.open`) then wrote through the dead capture and
  segfaulted the self-check. Fixed by an explicit `QObject::disconnect` at the
  end of the block (and the same pattern in the project selftest). This was a
  test-harness defect, not a product defect.
- The first broken-filelist fixture used a plain missing source entry. A-17
  deliberately keeps such an entry silent (generated files are a normal
  workflow), so it produced no diagnostic; the fixture now uses
  `+incdir+no_such_dir`, which the parser reports as `filelist.missing`. The
  product behaviour was correct; the test expectation was wrong.

## 4. What was NOT verified (honest limits)

1. **No real display / no AppImage run.** There is no display server on this
   machine and the AppImage was not launched. The title, tab tooltip and panel
   rows are asserted through the Qt APIs/real widgets in offscreen mode, not by
   looking at a window; the xcb smoke in the gate only proves the window and
   editor come up.
2. **The native File > Open dialog itself is not opened headlessly.** The filter
   *string* is asserted and the handler behind the action is exercised through
   the existing injected-dialog seam (`open.dialog.action-opens`), so
   action → `openPaths` → `EditorPane::openFile` → `fileOpened` is real; the
   platform file dialog is not.
3. **Palette root staleness is pre-existing and out of scope.** `installPalette`
   does not subscribe to `projectRootChanged` (only the Files tree, search and
   project lanes do), so after a *runtime* root switch the palette index keeps
   the old root until restart; the display rule is applied to whatever root it
   holds. Reported as a finding, not absorbed into this change.
4. **Transient status-bar confirmations still name absolute paths**
   (`Opened <abs>` in the palette/file-tree adapters and the search jump). They
   are not in the enumerated surface list (tab text/tooltip, Files tree, project
   panel, palette rows, search result rows, window title) and were left alone to
   keep the diff bounded.
5. **Tab text is the file name, not a path.** The rule is applied to the tab
   *tooltip*; the label renders a name and was not turned into a long path.
6. **Session-restore consequence not separately tested:** a `.f` tab restored
   into a shell with no project now fires `fileOpened` during `startSession()`
   and can set the project root from the filelist's directory. That follows the
   same rule, but the scenario has no dedicated assertion.
7. **Symlinked roots** are normalised lexically, not canonically; a path opened
   through a different symlink spelling than the root stays absolute (safe, but
   not "canonical" in the realpath sense).
8. The file-tree suite is registered in ctest only in its standalone build (by
   its own CMakeLists default), which is why the gate stays at 37 tests; that
   suite was built and run separately (row 6).

## 5. Files touched by this task

New:
- `src/common/path_display.h`
- `src/workspace/project/project_filelist.{h,cpp}`
- `evidence/ux-path-and-filelist/**` (this report, `run-checks.sh`, `logs/`)

Edited (product + tests + docs):
- `src/app/app_shell.{h,cpp}`, `src/app/editor_area.{h,cpp}`, `src/app/main.cpp`
- `src/workspace/project/{project_model.h,project_model.cpp,project_panel.cpp,project_integration.cpp,CMakeLists.txt,project_selftest.cpp}`
- `src/workspace/palette/{file_index.cpp,palette_selftest.cpp,CMakeLists.txt}`
- `src/workspace/search/{global_search.cpp,search_types.h,CMakeLists.txt}`
- `src/workspace/filetree/{file_tree_model.cpp,CMakeLists.txt,tests/filetree_tests.cpp}`
- `src/app/CMakeLists.txt` (include dir only)
- `docs/USER_GUIDE.md` (the old “no file-open → project-root wiring” statement
  is now false and was corrected; the display rule and the `.f` import are
  documented)
