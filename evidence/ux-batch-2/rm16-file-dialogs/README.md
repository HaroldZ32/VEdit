# RM-16 — file dialogs mis-filter: “Verilog/SV/Filelist” hides `.f`, and the import dialog shows no filelist

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Coverage Engineer (shared task T36). User words: “打开文件的时候，不知道为啥，Verilog/SV/Filelist 那个选项看不到 .f 文件，切到 Filelists 能看到，而导入 filelist 那个也是干脆看不见 filelist，文件格式匹配有点奇怪”. Corrected observation (captain, after the user re-tested): the import dialog DOES list files and only omits `.f`; after changing the file-type filter once, `.f` appears. So one root cause (a filelist glob missing from a filelist-claiming filter) plus one default (a dialog opened to import a filelist must not default to a source filter).

## 0. IMPORTANT — the premise conflict, stated first

The card and its correction say “today `.f` is absent from the source filter” and
“the import dialog's selected filter is not the filelist one”. **On the committed
source neither is true**, and I verified it before changing anything:

```
HEAD src/app/app_shell.cpp:49
  "Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist);;"
HEAD src/workspace/project/project_panel.cpp:435
  QCoreApplication::translate("VEdit", "Verilog filelists (*.f *.F *.flist);;All files (*)"));
HEAD src/app/main.cpp:777        check("open.filter.filelist", … *.f … *.F … *.flist …)
```

So the open-file filter already offered `.f`/`.F` next to the HDL sources, and the
import dialog's first (therefore default) filter was already a filelist filter.
The defect WAS reproducible in a different, equally real form — the filters
advertised a GUESSED extension set — and that is what this row fixes:

- both filelist-claiming filter entries carried `*.flist`, which **no producer or
  consumer of a filelist recognises** (see §2). A filter that claims a type it
  cannot actually load/mis-list is exactly the “文件格式匹配有点奇怪” the user felt;
- the filelist glob set was spelled independently in the open dialog, the import
  dialog and the main self-check — three copies that could (and did) drift;
- the import dialog relied on “the first filter is the default” rather than
  selecting the filelist filter explicitly.

If the user's binary really hid `.f` under that filter, that binary does not
match this source tree (the filter globs were introduced in commit `aaf07de`,
2026-11-06); we cannot inspect the binary the user ran. The regression added here
now locks the VISIBLE behaviour so the table cannot regress in either direction.

## 1. The filter table, as data (before → after)

| Dialog / call site | Before | After |
|---|---|---|
| File → Open (`src/app/app_shell.cpp` `openFileDialogFilter()`, call at `:736`) | `Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist);;Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;Filelists (*.f *.F *.flist);;All Files (*)` | `Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F);;Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;Filelists (*.f *.F);;All Files (*)` — composed from `src/common/file_dialog_filters.h` |
| Import Filelist (`src/workspace/project/project_panel.cpp` `onImportRequested()`) | `QFileDialog::getOpenFileNames(… "Verilog filelists (*.f *.F *.flist);;All files (*)")` (first filter only implicitly the default) | a `QFileDialog` configured by `common::configureImportFilelistDialog()`: filters `Verilog filelists (*.f *.F);;All files (*)` **and** `selectNameFilter(the filelist entry)` — the default is the filelist filter by construction, not by ordering luck |
| File → Save As (`src/app/app_shell.cpp` `:1320`) | `Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;All Files (*)` | unchanged — its NAME does not claim filelists, and a filelist is not a save target |
| Settings → tool binary “…” (`src/workspace/settings/settings_panel.cpp:275`) | `QFileDialog::getOpenFileName(…, start)` with **no** name filter | unchanged — a tool binary is not a filelist, any file is selectable |
| Open Folder / project switch (`src/app/app_shell.cpp:436`, `file_tree_widget.cpp:476`) | `getExistingDirectory` (no name filter) | unchanged |

The full before/after product strings are in `03-filter-table.txt`.

## 2. Where the filelist extension set comes from (do not guess)

Read from the code that actually decides what a filelist is, not from the dialogs:

- `crates/vd-config/src/project.rs` — the discovery predicate:
  `.is_some_and(|extension| extension.eq_ignore_ascii_case("f"))`.
  → a filelist manifest is the **`f` extension, case-insensitively** (`.f` / `.F`).
- `crates/vd-config/src/filelist.rs` — the parser is **content-based**: it parses
  any path handed to it (`filelist::parse`), and imported lists are read by
  content. That is why “All files (*)” stays in every dialog: an unusual spelling
  remains importable, it is simply not ADVERTISED as a filelist extension.
- `src/workspace/filetree/file_kind.cpp` classifies `f` **and** `filelist` for the
  tree icon — a cosmetic classification, not a dialog fact; left untouched (out of
  this row’s dialog scope, and not a correctness claim about parsing).

`src/common/file_dialog_filters.h` is now the ONE place the dialog set lives:
`filelistExtensions() = {"f"}` and the derived `filelistGlobs() = {"*.f", "*.F"}`
(the upper-case glob is listed because `QFileSystemModel` glob matching is
case-sensitive on Linux while the parser’s comparison is not; that is the single
place the spelling is decided). Both dialogs and the app self-check consume it.

## 3. Fix

- **(A)** Every filter whose NAME claims filelists (the combined
  `Verilog/SystemVerilog/filelist` entry and the `Filelists` entry) now offers
  exactly the recognised globs, composed from the shared header; the guessed
  `*.flist` is gone from all of them. The source-only entry
  `Verilog/SystemVerilog (…)` keeps its historical types and does not claim
  filelists.
- **(B)** The import-filelist dialog is built by
  `common::configureImportFilelistDialog()`: multi-select, the filelist table,
  and `selectNameFilter(filelist entry)` — the filelist filter is the SELECTED
  one regardless of the table's order.
- The app self-check's `open.filter.filelist` assertion was updated (see §5).

## 4. Regression: red before / green after

New target `vedit-file-dialog-filters-selftest`
(`src/app/file_dialog_filters_selftest.cpp`, registered in
`src/app/CMakeLists.txt`). It treats the PRODUCT tables as data, and it also
drives the real filter ENGINE: a `QFileSystemModel` (the model Qt's non-native
file dialog uses) with the selected import filter's globs over a temp directory
holding `a.sv` and `lib.f`.

**RED** (`01-red.log`, the final test against the committed pre-fix filter
table - the product filter function was temporarily restored to its committed
literal for this one run, then the shared-table version was put back): 31 checks
/ 4 failures, exactly the guessed extension:

```
CHECK FAIL open.entry0.no-guessed-flist  Verilog/SystemVerilog/filelist (*.v *.sv *.svh *.vh *.f *.F *.flist)
CHECK FAIL open.entry2.no-guessed-flist  Filelists (*.f *.F *.flist)
CHECK FAIL open.entry0.no-unrecognised-globs  entry='Verilog/SystemVerilog/filelist' guessed='*.flist'
CHECK FAIL open.entry2.no-unrecognised-globs  entry='Filelists' guessed='*.flist'
OBSERVED open-filter-engine index=0 filter='Verilog/SystemVerilog/filelist' names='lib.f,a.sv'
OBSERVED open-filter-engine index=1 filter='Filelists' names='lib.f'
VE-FILE-DIALOG-FILTERS-SELFTEST checks=31 failures=4
```

**Read this honestly:** on the committed table the VISIBILITY proof already
passes - `lib.f` is visible under both filelist-claiming filters (§0). That is
why the red is the guessed extension and the missing single source, not a hidden
`.f`. The visibility checks are still in the regression as the LOCK: if any
future edit drops a filelist glob from a filelist-claiming filter, they fail.

**GREEN** (`02-green.log`): 31 checks / 0 failures, including the real-engine
proof of the dialog default and of BOTH open-file filelist-claiming filters:

```
OBSERVED import-default-filter names='lib.f'
OBSERVED open-filter-engine index=0 filter='Verilog/SystemVerilog/filelist' names='lib.f,a.sv'
OBSERVED open-filter-engine index=1 filter='Filelists' names='lib.f'
CHECK PASS dialog.default-filter-shows-lib.f
CHECK PASS dialog.default-filter-hides-a.sv
CHECK PASS dialog.all-files-shows-both
CHECK PASS import.selected-filter-is-filelist
CHECK PASS import.selected-offers-exact-filelist-globs
CHECK PASS open.default-entry-offers-filelists
CHECK PASS open.engine0.filelist-visible / open.engine1.filelist-visible
CHECK PASS open.entry0.filelist-globs-exact / open.entry0.no-unrecognised-globs
VE-FILE-DIALOG-FILTERS-SELFTEST checks=31 failures=0
```

The model is set to `setNameFilterDisables(false)` so it HIDES non-matching
entries like the dialog does (the default only greys them).

## 5. Requirements-driven assertion change (recorded)

`src/app/main.cpp`'s self-check `open.filter.filelist` used to REQUIRE the
guessed `*.flist` to be present (`HEAD main.cpp:777`). With the set now derived
from the parser, that assertion is inverted and split:

```cpp
check("open.filter.filelist", <every shared filelist glob is in the filter>);
check("open.filter.no-guessed-flist", !filter.contains("*.flist"));
```

This is a requirements change (the parser fact), not gate-tuning: the old check
encoded the guess that this row removes.

## 6. Post-unblock verification (observed)

The RM-17 lane's in-flight `vd_ffi.h` (a duplicate `const char *top;`) blocked
the C++ build for a while; after the captain confirmed the regeneration, the
build-dependent targets were rebuilt and run:

| Target | Command | Observed |
|---|---|---|
| app self-check (the updated `open.filter.*` assertions) | `vedit-app --selfcheck --state-dir /tmp/rm16-selfcheck` | `CHECK open.filter.filelist PASS …;;Filelists (*.f *.F);;All Files (*)`; `open.filter.no-guessed-flist PASS`; `open.filter.keeps-existing-types PASS`; `APP-SHELL-SELFTEST ok=1 checks=321 failures=0` (`04-…`) |
| project lane (Project panel compiles with the new QFileDialog path) | `vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project` | `PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0` (`05-…`) |
| RM-9 e2e (the Project panel + the status-dot delegate that shares it) | `vedit-rm9-status-dots-e2e` | `RM9-E2E checks=33 failures=0` (`06-…`) |
| file tree lane (shares the dot vocabulary with the Project panel) | `vedit-filetree-tests` | `FILETREE-TESTS checks=127 failures=0` (`07-…`) |

## 7. Bounds

- The dialogs are exercised with Qt's NON-native dialog
  (`DontUseNativeDialog`; offscreen). A GTK/KDE native dialog receives the SAME
  filter strings from the same table and parses the same glob syntax through Qt's
  platform theme, but its pixels/native file-choice UX are not driven here.
- `translations/vedit_zh_CN.ts`: the changed filter SOURCE strings
  (`Verilog/SystemVerilog/filelist (%1 …)`, `Filelists (%1)`,
  `Verilog filelists (%1)`) have no zh entry yet; under zh they fall back to
  English until the i18n lane re-extracts. The globs are identical either way.
  The i18n lane is the single writer for that file; I did not touch it.
- `docs/USER_GUIDE.md:73,96,612` still list `*.flist` (the manual lane owns that
  file). Reported for correction, not edited here.
- The `.flist` alias was REMOVED from the advertised filelist set (parser fact).
  An unusual spelling remains importable via “All files”; if the captain wants
  the alias kept, it can be re-added in ONE place (the shared header) without
  touching either dialog.

## 8. Files changed

- NEW `src/common/file_dialog_filters.h` — the ONE filter table + filelist
  extension set (parser mirror).
- `src/app/app_shell.cpp` / `src/app/app_shell.h` — the open-file filter
  delegates to the shared table; the comment no longer advertises `.flist`.
- `src/app/main.cpp` — the self-check assertion follows the parser set (§5).
- `src/workspace/project/project_panel.cpp` — the import dialog is built with the
  shared table and the filelist filter SELECTED.
- NEW `src/app/file_dialog_filters_selftest.cpp` +
  `src/app/CMakeLists.txt` — the regression target.
- `evidence/ux-batch-2/rm16-file-dialogs/` — this README, red/green logs, the
  before/after table.
