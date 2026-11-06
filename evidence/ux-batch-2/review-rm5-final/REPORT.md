# RM-5a / RM-5b final independent review — undo blank-view Blocker + shortcuts

- Task: T12 (independent verification of T10 / RM-5a and T11 / RM-5b), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Basis: the RM-5a/RM-5b cards in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md`; the writers'
  evidence `rm5-undo/` (README sha256 `9180867d…`) and `rm5b-shortcuts/` (README sha256
  `cd82872f…`); the code.
- Reviewed tree: `document_io.cpp` `1810e779…` (the fix), `undo_view_lifetime_test.cpp`
  `e17133ef…`, `shortcut_key_path_test.cpp` `207fa71f…`, `command_registry.cpp` `d469c8d3…`,
  `app_shell.cpp` `e01a273e…`, `ktexteditor_provider.cpp` `cdc33ab2…`. Provenance:
  `05-provenance.txt`.

---

## 1. What I re-derived (independent of the writers' narrative)

### 1.1 The targeted suites, re-run in the pinned image (`01-ctest-rerun.log`)

```
1/4 Test #11  vedit-app-shell-selftest ............ Passed 0.79 s   APP-SHELL-SELFTEST 319/0
2/4 Test #19  vedit-shortcut-key-path-tests ....... Passed 0.68 s   SUMMARY checks=75 failures=0
3/4 Test #29  vedit-undo-view-lifetime-tests ...... Passed 0.21 s   SUMMARY checks=65 failures=0
4/4 Test #57  vedit-workspace-settings-selftest ... Passed 1.36 s   SETTINGS-SELFTEST 242/0
```

The writers' red runs are real: `rm5-undo/red-run.log` = **66 checks / 19 failures** (zero-edit
undo command and key both empty the buffer: `text=0 opened=51`; `undo.3…8` and `key-undo.1…4`
not-empty fail); `rm5b-shortcuts/red-run.log` = **66 checks / 19 failures** (six missing defaults,
Ctrl+Shift+H split dead, four editor collisions, `conflict.registry-refused` fail). I did not
reproduce the red myself — it requires reverting the product source (the reviewer fixes nothing) —
so I read the red logs and re-ran the green. The writers' READMEs state this is exactly how the red
was produced (`rm5-undo/rebuild-test.sh` and the "replace the two lines" note).

### 1.2 RM-5a — the mechanism, the red/green, and the identity

- The writer's mechanism matches the code: pre-fix `loadFileIntoDocument()` used
  `KTextEditor::Document::setText()` (verified in `git show HEAD:src/editor/kernel/document_io.cpp`),
  which records the whole file load as one undo step from the empty buffer; the shell's `edit.undo`
  → `EditorPane::triggerEditorAction("edit_undo"/"undo")` executes KTextEditor's own undo. The first
  Ctrl+Z therefore returns the document to the empty pre-load state. The current
  `loadFileIntoDocument()` (symbol: `loadFileIntoDocument` in `src/editor/kernel/document_io.cpp`)
  clears `setModified(false)` and opens through `document->openUrl(QUrl::fromLocalFile(...))`, which
  makes the loaded text the undo floor.
- The red behaves exactly as the user described (buffer empty, undo disabled, disk intact); the
  red's `editor-identity` checks pass on every press (the view was never detached — the writer's
  claim that the view was not the problem is correct, and the buffer was).
- The green regression (`undo_view_lifetime_test.cpp`) checks the identity itself:
  `pane->editorWidget() == view && vedit::editor::viewFor(...)->document() == document` after every
  undo, plus text never empty, the floor, typing after undo, and the tab still showing the file.
  I re-ran it: 65/0. The standalone probes (`zero_edit_undo_probe`, `kt_undo_probe`,
  `openurl_probe` offscreen and xcb) support the same story (`A1 one_undo text=0` pre-fix;
  `openUrl+undo after=32`).
- The fix does not disable undo, recreate the document, or hide the tab: the test undoes real edits
  after the fix (`undo.*`, `key-undo.*`), the document pointer never changes, and the tab label
  stays. The `setModified(false)` before `openUrl` is what prevents KParts' `closeUrl()` save prompt
  (the writer measured a 124 hang without it); the product pane only loads into a fresh/unmodified
  tab, and `document_sync` uses the same seam only after refusing a modified document.
- The earlier editor-provider/dangling-editor hypothesis is ruled out by measurement (identity
  checks on every press; `setProvider()` is never called on undo); the writer records the ruling-out
  rather than smoothing it over. No evidence that the earlier provider fix is incomplete.

### 1.3 RM-5b — the surface, the real key path, the defaults, the conflicts

- The shortcut regression (`shortcut_key_path_test.cpp`, 75/0 re-run) drives Qt's real shortcut map
  (`QTest::keyClick` on the main window's `QWindow`), the real Shortcuts page (`QTableWidget` row +
  `QKeySequenceEdit` + Assign), and the modal Preferences dialog. The checks cover:
  - `BINDING` line for every registered command; `visible.*` effective/default/state columns;
    explicit `(unbound)` / `(none)` / `unbound` for `help.about`.
  - `custom.effective-updated`, `custom.new-key-fires` (panes 1→2), `custom.old-default-inert`;
    `persist.restart-loaded` / `persist.restart-key-fires` on a SECOND `ApplicationShell` with the
    same state dir; `dialog.preview-effective` / `dialog.ok-persisted` / `dialog.ok-restart-value`.
  - `conflict.registry-refused` + owner named (`file.save`); `real.editor-conflict-refused` +
    `Find` named; `reset.per-command` / `reset.all-at-once`; `defaults.no-duplicate-bindings`.
  - `authority.no-editor-collision.<every command>` (32), `authority.ctrl-p-shell-owned` /
    `ctrl-p-fires` (the palette lane's Ctrl+P), `authority.editor-find-kept`.
  - `real.split-horizontal-fires`, `real.split-vertical-fires`, `real.ctrl-z-undoes`,
    `real.ctrl-shift-z-redoes`, `real.ctrl-z-undoes-again`.
- Independent checks: `vedit-app --selfcheck` (my run, `03-selfcheck.log`) reports
  `COMMAND-BINDINGS count=32`, `commands.no-duplicate-shortcuts PASS`,
  `APP-SHELL-SELFTEST 319/0`; I checked the 32 sequences for duplicates (`06-bindings-check.txt`)
  — none. `bash build/gates/lrelease.sh` (my run, `02-lrelease.log`): 1236 translations,
  0 unfinished/untranslated, PASS.
- The KTextEditor-eaten list is real and the four pre-fix collisions are correctly named
  (`file_save_as`, `edit_find_selected_backwards`, `switch_next_input_mode`, `file_print`); the
  fixes move `view.split.horizontal` to Ctrl+Alt+H, `view.panel.vcs` to Ctrl+Alt+C, strip the
  editor copies of Save/Save As/Undo/Redo/Print, and keep the editor's feature via the shell
  command (proven by `real.ctrl-z-undoes` / `real.ctrl-shift-z-redoes`). The deviations from the
  VS Code list are stated honestly in README §5: Ctrl+Shift+F stays `diagnostics.format` (its
  pinned default); Ctrl+B is KTextEditor's Toggle Bookmark; two-stroke sequences are not used; the
  shell's `search.focus` takes Ctrl+Shift+Alt+F.

---

## 2. RM-5a — the five checks

| # | Check | Result |
|---|---|---|
| 1 | Mechanism named on the real path, not a plausible story | **PASS.** `loadFileIntoDocument` pre-fix `setText` → one undo step; open chain `ktexteditor_provider.cpp::openFile` → `EditorPane::openFile` → `ApplicationShell::edit.undo` → `EditorPane::triggerEditorAction` → KTextEditor's undo. The red/green and the standalone probes match the code; the view was never detached (the buffer was emptied). |
| 2 | Repeated undo never blanks; same document; typing after | **PASS.** The regression checks the document pointer identity, `buffer-not-empty`, the opened-text floor, typing after undo, and the tab on every press; red shows the empty buffer, green does not; my re-run 65/0. The standalone `kt_undo_probe` shows `view_doc_same=1` on every press. |
| 3 | Red run before the fix; residual risk stated | **PASS with a stated residual.** The red run (66/19) is the pre-fix source; the writer reproduced the user's minimal sequence and the product seam. **Unproven:** the user's actual AppImage/KF6 build and a physical keyboard on the real desktop; the writer's xcb probe and the pane test cover the mechanism but not the user's machine. The full gate was not run (captain). |
| 4 | No papering over (disable undo / recreate doc / hide tab) | **PASS.** Undo still works for real edits after the fix (`undo.*`, `key-undo.*`), the document pointer is unchanged, the tab remains; the fix only changes the load entry to `openUrl` + `setModified(false)`. |
| 5 | Earlier provider/dangling-editor fix incomplete? | **PASS (recorded, not smoothed).** The diagnosis rules the provider/event-filter/multi-cursor/document-sync candidates out by measurement; the earlier A2 provider crash is not implicated in this defect. No evidence of an incomplete provider fix. |

---

## 3. RM-5b — the four checks

| # | Check | Result |
|---|---|---|
| 6 | Surface shows effective binding for every command, defaults and explicit none | **PASS.** Four-column Shortcuts page (`Effective shortcut` / `Default` / `State`), `(unbound)`/`(none)`/`unbound` for About/Save Session/hover/capabilities/reload/lint/reparse; `visible.*` green; the regression prints a `BINDING` line for every registered command. |
| 7 | User-set binding immediate and survives restart, proven on the real key path | **PASS.** `custom.effective-updated`, `custom.new-key-fires` via Qt's shortcut map on the shell `QWindow`, `custom.old-default-inert`, `persist.restart-loaded` + `persist.restart-key-fires` on a second shell with the same state dir; the modal dialog path (`dialog.preview-effective` / `ok-persisted` / `ok-restart-value`) is covered too. |
| 8 | No duplicate defaults in the shipped mapping; user conflict reported | **PASS.** `commands.no-duplicate-shortcuts` in `--selfcheck`, `defaults.no-duplicate-bindings` green, the 32-line product dump unique (my check), and the per-command `authority.no-editor-collision.*` green; a user assigning an owned sequence is refused with the owner named (`conflict.registry-refused` / `-owner-named`, `real.editor-conflict-*`). |
| 9 | VS Code-style defaults listed command-by-command; KTextEditor-eaten sequences honest | **PASS with two gaps (M1/M2).** The table lists 39 product commands; the four pre-fix editor collisions and the fixed ones are named; the deviations for Ctrl+Shift+F and Ctrl+B are honest. **M1:** the collision check is against KTextEditor, not the desktop's global shortcuts — several `Ctrl+Alt+*` defaults are DE-reserved (see below). **M2:** the user's list items Ctrl+Tab, Ctrl+Y and Shift+Alt+F are not in the table or the deviation list; the README's "next-prev-tab are the editor's own" is not supported by the KTE dump (no Ctrl+Tab) and the product has no next-tab command. |

---

## 4. Findings (ranked)

### M1 — MEDIUM (top residual) — the default table is checked against KTextEditor, not the desktop's global shortcuts

**What.** The `authority.*`/`real.*` checks prove no KTextEditor action shares a sequence with the
shell defaults. They do not check the window manager / desktop environment's global shortcuts.
Many defaults use the `Ctrl+Alt+*` family: `view.toolbar` = **Ctrl+Alt+T**,
`view.panel.diagnostics` = **Ctrl+Alt+D**, plus Ctrl+Alt+F/S/J/C/O/H/M/N/R. `Ctrl+Alt+T` is the
common Ubuntu/GNOME "open terminal" shortcut and `Ctrl+Alt+D` is the common KDE "show desktop"
(KDE/GNOME are the user's two desktops, RM-6). If either is reserved on the user's desktop, the
default is a shipped dead binding — exactly the class RM-6 records for Alt gestures.
**Where.** `src/app/app_shell.cpp` (`ApplicationShell::registerCommands` default table);
`src/app/command_registry.cpp` (foreign/editor authority); README §6.1 (bounds mention only the
physical keyboard, not the DE); the RM-6 card.
**Why it matters.** The user asked for defaults that work; the task says a shipped dead binding is
a finding. The writer's bounds are honest about the physical keyboard but omit the DE-global
class, which the user's own RM-6 proves is live on this machine.
**Direction (described, not applied).** Check the KDE/GNOME global shortcut tables (or use a less
contested family for the panel/toolbar toggles), call out every DE-eaten default in the table, and
coordinate the choice with RM-6/T13; the user will test the real desktop.

### M2 — LOW/MEDIUM — the user's Ctrl+Tab, Ctrl+Y and Shift+Alt+F are not listed, and the "editor's own" claim for next/prev-tab is inaccurate

**What.** The user's RM-5 list included Ctrl+Tab (and Ctrl+Y / Shift+Alt+F as VS Code-style
neighbours). The final table has no next/prev-tab command; the KTE action dump contains **no
Ctrl+Tab, no Ctrl+Y and no Shift+Alt+F**; the README §5 says "next-prev-tab … are the editor's own
KTextEditor/lane actions", which the dump does not support (and the product has no such command).
The Ctrl+Y (VS Code Windows redo) and Shift+Alt+F (format) are Linux-vs-Windows choices, not
stated as deviations.
**Where.** `evidence/ux-batch-2/rm5b-shortcuts/README.md` §5; `probe/log-kte-action-dump.txt`;
`src/app/app_shell.cpp` (no next-tab command); `EditorPane` tab bar (mouse / F6 focus only).
**Why it matters.** The user's "抄一个" list is only partly honoured, and one sentence in the
deviation list attributes a key to the editor that the editor does not own; a user pressing
Ctrl+Tab gets nothing.
**Direction.** State plainly that the product has no next/prev-tab command (or add one and bind
Ctrl+Tab / Ctrl+Shift+Tab), and list Ctrl+Tab/Ctrl+Y/Shift+Alt+F in the deviation table with their
actual status.

### L1 — LOW — `setModified(false)` before `openUrl` relies on the fresh-tab invariant, not an enforced precondition

**What.** `loadFileIntoDocument` clears the modified flag before `openUrl` to avoid the save
prompt. Today every caller is safe: `EditorPane::openFile` reuses only an untouched tab, the
provider swap happens before the user edits, and `document_sync::refresh` refuses a modified
document before calling the seam. A future caller that loads a **modified** document through this
function would silently discard the dirty flag.
**Where.** `src/editor/kernel/document_io.cpp` (`loadFileIntoDocument`); `document_sync.cpp`
(caller); README §1's fix note.
**Why it matters.** The fix's safety depends on an unwritten contract; an assert or a documented
precondition would prevent a future data-loss path.
**Direction.** Add an explicit precondition/assert (`!document->isModified()` or a `bool
allowReplaceModified` argument) or document the invariant at the seam.

### L2 — LOW — the RM-5a regression's real-key checks drive the editor's own Ctrl+Z, while the final product routes Ctrl+Z through the shell command

**What.** The RM-5a test builds an `EditorArea` without an `ApplicationShell`, so its `keyTarget`
Ctrl+Z is KTextEditor's own `edit_undo`. RM-5b then strips the editor's Ctrl+Z in the product and
gives it to the shell's `edit.undo` command. The shell command calls the same
`triggerEditorAction`, and RM-5a's `zero-edit.undo-command` checks that command path; RM-5b's
`real.ctrl-z` checks the shell key after a typing edit. The exact cross-lane case "zero-edit shell
Ctrl+Z" is not a single end-to-end test.
**Where.** `src/editor/kernel/undo_view_lifetime_test.cpp` (`deliveryTarget`/`keyTarget`);
`src/app/shortcut_key_path_test.cpp` (`real.ctrl-z-*`); `src/app/editor_area.cpp`
(`clearOwnedShortcuts`/`setOwnedShortcuts`).
**Why it matters.** Low: the two paths converge on the same editor action; the zero-edit shell path
is covered by the command check + the RM-5b key check, but not by one test that presses the physical
key on a zero-edit buffer through the shell.
**Direction.** Add a zero-edit shell-key case to either regression when the lanes are combined.

### L3 — LOW — `docs/USER_GUIDE.md` still lists the old split-horizontal and VCS sequences

**What.** The writer's bound #7 says the guide still lists Ctrl+Shift+H (split) and Ctrl+Alt+V
(VCS); the writer left it to its owner (another lane is editing it). The product now uses
Ctrl+Alt+H and Ctrl+Alt+C.
**Where.** `docs/USER_GUIDE.md` (bound in README §6.7); `src/app/app_shell.cpp` default table.
**Why it matters.** A user reading the guide will press the dead old sequences; the writer
disclosed it, but it is a user-facing stale document.
**Direction.** The guide owner updates the two sequences and any Ctrl+B/Ctrl+Shift+F notes.

### N1 — NIT — the red/green check counts differ, by construction

**What.** RM-5a red 66 vs green 65: the pre-fix third undo actually changed the buffer, so the
conditional `undo.N.changed-something` check is emitted; after the fix the same press is a floor
no-op and is not emitted. RM-5b red 66 vs green 75: the red is a **targeted partial revert**
(defaults/authority only) and predates the 9 `custom.*`/`persist.*`/`dialog.*` checks; the green
final test has 75. The counts are explained, not a defect, but the red is not the final test source.
**Where.** `rm5-undo/red-run.log`, `rm5-undo/green-run.log`; `rm5b-shortcuts/README.md` §4.
**Why it matters.** Evidence hygiene: say "targeted pre-fix" in the red table so a reader does not
expect 75.

### N2 — NIT — `EditorDocument::setText` still uses the undoable `Document::setText`

**What.** The product file-open path is fixed, but the `EditorDocument::setText` API in the same
file still calls `KTextEditor::Document::setText`. It is a text setter (used by the kernel
selftest/API), not the product's file open; a future caller using it to load a file into an empty
document would recreate the load-as-undo-step bug.
**Where.** `src/editor/kernel/document_io.cpp` (`EditorDocument::setText`); `document_io.h`.
**Why it matters.** Latent; the contract note could make the distinction explicit.

---

## 5. Declined to judge (reason on the same line)

- **RM-6 Alt gestures / the window manager** — a separate card (T13); the DE-global risk of the `Ctrl+Alt+*` defaults is M1 here, not a re-judgment of RM-6.
- **The full gate (`scripts/gates.sh`, `build/gates/qt6-cxx.sh`)** — the captain runs it; I ran the four targeted suites, lrelease and `--selfcheck` myself.
- **The physical keyboard / the user's desktop** — the writers' bound; the probe is offscreen/xcb; the user will test; M1 is the closest measured-adjacent risk.
- **The exact AppImage and its bundled KF6** — not in this tree; the writer's probe uses the pinned image's KTextEditor 6.24; the AppImage version was not matched.
- **The palette's Ctrl+P dialog key press** — the writer's bound #3; the mechanism is proven via a planted Ctrl+P command and the editor Print copy stripped; the modal dialog was not opened with a physical key.
- **Two-stroke VS Code sequences (Ctrl+K Ctrl+O / Ctrl+K S)** — not copied; the Shortcuts page has one sequence field; the writer states it.
- **`Ctrl+Shift+F` for project search** — stays `diagnostics.format` (its pinned default; a cross-lane contract); the writer's §5 states it; `search.focus` takes Ctrl+Shift+Alt+F.
- **`Ctrl+B`** — KTextEditor owns Toggle Bookmark (the dump confirms); the writer's §5 states it.
- **`docs/USER_GUIDE.md`** — the writer's bound #7 and another lane's file; L3, not a code finding.
- **`document_sync`/`EditorDocument::setText` load-undo risk** — `document_sync` refuses a modified document; `EditorDocument::setText` is a text setter; L1/N2, not the user path.
- **The RM-5a regression's editor-vs-shell key path** — L2; the cross-lane zero-edit shell key is covered by the command path + the RM-5b key path; not a defect.
- **The red/green check counts** — N1; the conditional check / the targeted partial revert; the writers' READMEs explain.
- **The `save.shortcut.runtime-duplicate-enumerated` selfcheck** — the T80 authority test's own duplicate enumeration; `commands.no-duplicate-shortcuts` is the shipped check and passes.
- **The user's exact KDE/GNOME builds** — not measured; M1.

---

## 6. Bounds of this review (what I did not read or run)

- I re-ran the four targeted suites (`app-shell` 319/0, `shortcut-key-path` 75/0,
  `undo-view-lifetime` 65/0, `settings` 242/0), the lrelease gate (1236/0/0) and
  `vedit-app --selfcheck` (32 bindings, no duplicates, 319/0) in `vedit-qt6-build:local`; logs
  `01`/`02`/`03` here.
- I did **not** reproduce the red runs myself: that requires reverting the product source, which
  the reviewer does not do. I read the writers' red logs and re-ran the green; the reds are
  plausible and match the code paths.
- I did **not** run the full gate; the captain owns it.
- I did **not** drive a physical keyboard or a real KDE/GNOME session; the undo test and the
  shortcut test are offscreen/QTest; the writer's xcb probe is the only Xvfb/xcb undo evidence.
- I did **not** drive the user's AppImage or its bundled KF6/KTextEditor; the pinned image is
  6.24.
- I did **not** open the command palette dialog with a real Ctrl+P; the mechanism test plants a
  Ctrl+P command and strips the editor's Print copy (`authority.ctrl-p-*`).
- I did **not** check the desktop's global shortcut tables; M1 is the residual risk I could not
  measure from here.
- I did **not** edit any product file. The only writes are this report and the review logs/fixture
  under `evidence/ux-batch-2/review-rm5-final/`: `01-ctest-rerun.log`, `02-lrelease.log`,
  `03-selfcheck.log`, `05-provenance.txt`, `06-bindings-check.txt`.

---

## 7. Verdict

**PASS — risk MEDIUM.**

RM-5a is the highest-severity defect in this batch, and its evidence is the strongest part of the
submission: the mechanism is the file load's own undo step (`loadFileIntoDocument` pre-fix
`setText`), the view was never detached, the buffer was; the red run reproduces the user's minimal
sequence (zero-edit undo empties the buffer, `text=0`), the green run keeps the opened text as the
undo floor with the document pointer and the tab unchanged; the fix opens through the documented
`openUrl` path and keeps undo working for real edits; the earlier provider hypothesis is ruled out
by measurement and recorded, not smoothed over. I re-ran the regression (65/0). The residual is the
user's actual AppImage/KF6 and a physical keyboard, which the writer's bounds state and which the
user's next run will close.

RM-5b's three original defects are fixed and locked: the surface shows effective/default/state
with an explicit unbound; a custom binding fires through Qt's real shortcut map, survives a restart
and the modal dialog; the shipped 32-binding table has no duplicates; the four KTextEditor
collisions are found and fixed; a user conflict is refused with the owner named; the VS Code
deviations (Ctrl+Shift+F, Ctrl+B, two-stroke) are stated honestly. The residual risk I would not
sign off as proven is the **desktop-global** class: the collision check is against KTextEditor, not
KDE/GNOME, and the `Ctrl+Alt+*` family includes `Ctrl+Alt+T` (common GNOME terminal) and
`Ctrl+Alt+D` (common KDE show-desktop) on the user's own two desktops — if those are reserved, the
toolbar/diagnostics defaults are shipped dead bindings (M1). The user's Ctrl+Tab/Ctrl+Y/Shift+Alt+F
are also not listed and the "editor's own" next-tab claim is not supported (M2). These are Medium/Low
findings with a clear direction; the core RM-5a Blocker and the RM-5b defects are closed.
