# RM-16b — the file-type filter only takes effect after a second click

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Coverage Engineer (shared task T40). User words: “我说的问题不是过滤器不生效，而是过滤器必须再点一下才生效，默认情况下的不好使” — the dialog shows a filter as selected, but the file view is only filtered after the type control is clicked once more. Both the File→Open dialog and the import-filelist dialog were reported.

## 1. Reproduction attempt (NO user interaction) — and its result

The build pins **Qt 6.10.2** (`scripts/versions-qt.env`) and the packaged AppDir
carries `plugins/{platforms,kf6,xcbglintegrations}` — **no `platformthemes`
plugin** — so the Qt (non-native) file dialog is what ships. Three probes
(`target/rm16-probe/{order,apply,modal}.cpp`, logs `00a/00b/00c`) construct the
dialog exactly as the product does (directory in the constructor, filters set
afterwards, the intended entry selected) and read the dialog's own
`QFileSystemModel` (the model the view renders) and its `nameFilters()`, with a
temp dir holding `a.sv`, `lib.f` and `notes.txt` (a file NO filter claims, so
"filtered" is distinguishable from "everything listed"):

```
case6-created  selected='Verilog filelists (*.f *.F)'  modelFilters='*.f|*.F'  rows=''
case6-shown    selected='Verilog filelists (*.f *.F)'  modelFilters='*.f|*.F'  rows='lib.f'
case7-open-default  modelFilters='*.f|*.F'  rows='lib.f'
case8-combined-default  modelFilters='*.v|*.sv|*.svh|*.vh|*.f|*.F'  rows='a.sv,lib.f'
static-open (QFileDialog::getOpenFileName with the filter string)
               modelFilters='*.v|*.sv|*.svh|*.vh|*.f|*.F'  rows='a.sv,lib.f'
object-exec (import style, selectNameFilter then exec)
               modelFilters='*.f|*.F'  rows='lib.f'
```

**Result: the deferred-apply symptom does NOT reproduce in this engine.** At
creation, with no interaction, the model already carries the selected entry's
globs, the view shows the filtered set (`lib.f` only under the filelist entry;
`notes.txt` hidden under every non-“All files” entry; a source file visible only
under an entry that claims sources), both before and after `show()`. The card's
expected red (“today both fail”) is therefore NOT producible honestly on the
shipped Qt; I did not fabricate one. What the probes also show is that the old
code RELIED on Qt's internal initial apply (the static one-shot, and a configure
helper that never explicitly selected/applied) — that is the risk the fix removes.

*Possible explanation for the user's observation, recorded without claiming it:*
a host that supplies a native platform theme (KDE/GTK) would use a native dialog
with different initial-filter timing — the shipped AppDir bundles no such plugin,
so this is untested here (see §5); or the user's binary predates the batch table.

## 2. The fix (defence in depth — the initial filter is now applied by us)

- `src/common/file_dialog_filters.h` gains `filterGlobs()` and
  `applySelectedFilterToModel(QFileDialog&)`: it resolves the dialog's SELECTED
  entry, takes its globs and fans them into the dialog's own `QFileSystemModel`
  (`setNameFilterDisables(false)` so non-matching entries are HIDDEN like a file
  dialog hides them, then `setNameFilters(globs)`). This runs inside both
  `configureOpenFileDialog()` / `configureImportFilelistDialog()` **before the
  dialog is shown**, so the intended filter is in effect at creation regardless
  of whether (or when) Qt performs its own internal apply. The dialog's own
  type-combo handler keeps the model in sync for later changes.
- `configureOpenFileDialog()` now SELECTS the intended entry explicitly (the
  historical first entry) instead of relying on “Qt selects the first one”.
- `src/app/app_shell.cpp`: the File→Open handler no longer uses the static
  `QFileDialog::getOpenFileName` one-shot; it builds a `QFileDialog` object (the
  `openFileDialogProvider_` test seam is unchanged), configures it with the
  shared helper and calls `exec()`. The import dialog already used an object; its
  configure helper now also applies the filter.
- The FILELIST glob set still comes from the ONE shared table built in RM-16
  (`filelistExtensions()`/`filelistGlobs()`, mirrored from
  `crates/vd-config/src/project.rs`: extension `f`, case-insensitive; globs
  `*.f *.F`). No regression of that work.

## 3. Regression (no interaction) and observed output

`vedit-file-dialog-filters-selftest` (src/app, registered in CMake) now also
constructs both dialogs the product way and reads the dialog's own model with
**zero interaction**: a temp dir with `a.sv` + `lib.f` + `notes.txt`.

GREEN (`01-green-selftest.log`, 46 checks / 0 failures):

```
OBSERVED creation-import         visible='lib.f'
OBSERVED creation-open-default   visible='lib.f,a.sv'
OBSERVED creation-open-filelist  visible='lib.f'
CHECK PASS creation.import.model-present
CHECK PASS creation.import.selected-is-filelist
CHECK PASS creation.import.filters-in-effect-before-show
CHECK PASS creation.import.shows-lib.f / creation.import.hides-a.sv / creation.import.hides-unclaimed
CHECK PASS creation.open.default-is-first
CHECK PASS creation.open.filters-in-effect-before-show
CHECK PASS creation.open.shows-source / creation.open.shows-filelist / creation.open.hides-unclaimed
CHECK PASS creation.open-filelist.filters-in-effect-before-show
CHECK PASS creation.open-filelist.shows-lib.f / creation.open-filelist.hides-source
VE-FILE-DIALOG-FILTERS-SELFTEST checks=46 failures=0
```

These checks are the LOCK: they read the model's `nameFilters()` and the visible
set BEFORE `show()`, so on any Qt/platform combination that defers the filter
apply they fail — and §2 is what makes them pass there. On the shipped Qt they
already passed before the fix (see §1), so they are not a red; the red that stays
recorded for this dialog family is RM-16's guessed `*.flist`
(`evidence/ux-batch-2/rm16-file-dialogs/01-red.log`).

RED status, stated plainly: the follow-up's second red (“both fail today because
the filter is not applied until a click”) could not be demonstrated on the
shipped engine — the probes in §1 show the apply happening at creation — so none
is claimed. This does not contradict the user: his observation and the committed
filter TABLE were compatible (the globs were right); the missing piece he
described is the APPLY timing, and this row makes that timing explicit instead of
inherited.

## 4. Post-fix suites (observed)

| Suite | Observed |
|---|---|
| app self-check (`vedit-app --selfcheck`; the `open.filter.*` assertions + all app UI tests) | `open.filter.filelist PASS`, `open.filter.no-guessed-flist PASS`, `open.filter.keeps-existing-types PASS`, `APP-SHELL-SELFTEST ok=1 checks=321 failures=0` (`02-…`) |
| project lane (Project panel, the import dialog's owner) | `PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0` (`03-…`) |
| RM-9 e2e (shares the Project panel + the status-dot delegate) | `RM9-E2E checks=33 failures=0` (`04-…`) |

## 5. Native/KDE dialog bound

The packaged AppDir bundles `plugins/platforms/*`, `plugins/kf6/*`,
`plugins/xcbglintegrations/*` and **no `plugins/platformthemes/*`**, with Qt
6.10.2 pinned — so `QFileDialog` uses the Qt dialog on the shipped product, which
is what §1/§3 exercise. If a host ever supplied a native platform theme (KDE/GTK
dialog), the native dialog has no `QFileSystemModel` child; `applySelectedFilterToModel()`
then degrades to a no-op and the native dialog's own initial-filter behaviour
applies. That case is NOT tested here (no such plugin in the deliverable) and is
a declared bound, not a claim.

## 6. Files changed

- `src/common/file_dialog_filters.h` — `filterGlobs()`,
  `applySelectedFilterToModel()`, explicit selection + apply in both configure
  helpers.
- `src/app/app_shell.cpp` — the File→Open handler builds a configured
  `QFileDialog` object instead of the static one-shot (provider seam unchanged).
- `src/app/file_dialog_filters_selftest.cpp` — the no-interaction creation checks
  (import default, open default, open filelist entry) + the `notes.txt` fixture.
- `evidence/ux-batch-2/rm16b-filter-apply/` — this README, the three probe logs,
  the green selftest and the three suite logs.
- No new translatable strings; `src/workspace/project/project_panel.cpp` needed
  no edit this row (it already calls `configureImportFilelistDialog()`, which now
  applies the filter).
