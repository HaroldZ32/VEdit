# RM-5b — shortcuts must be visible, effective and have sane defaults

- **Task**: T11 (board), branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
- **Report**: “快捷键设置不好使” → “能改，但不生效，并且看不见”; “为什么很多功能没有默认快捷键？抄一个吧”.
- **Verdict**: **all three defects reproduced, fixed and locked by a key-driven regression.**
- Evidence files: `product-selfcheck.log` (full `--selfcheck`, PASS), `product-bindings.txt` (the
  product-wide effective binding dump), `red-run.log` / `green-run.log` (the new regression),
  `probe/log-kte-action-dump.txt` (every sequence KTextEditor itself owns).

## 1. What was actually wrong (measured, not guessed)

### Defect 2 — “能改，但不生效” is TWO independent causes

**(a) The editor already owns the sequence, so Qt drops both.** `CommandRegistry::add()` warned about
duplicate *registry* defaults (U3b), but nothing compared a binding with **KTextEditor's own action
collection**. Qt treats two enabled actions with the same sequence as AMBIGUOUS and triggers NEITHER, so
a perfectly stored binding was a dead key. The probe (`probe/kte_action_dump.cpp`, log
`probe/log-kte-action-dump.txt`) enumerated 105 sequences owned by the live editor; four product
bindings collided:

| product command | sequence | the editor action that also owned it | consequence |
|---|---|---|---|
| `file.save-as` | Ctrl+Shift+S | `file_save_as` | Ctrl+Shift+S fired NOTHING (Save As dead) |
| `view.split.horizontal` | Ctrl+Shift+H | `edit_find_selected_backwards` | split horizontally fired NOTHING |
| `view.panel.vcs` | Ctrl+Alt+V | `switch_next_input_mode` | VCS panel toggle fired NOTHING |
| `file.goto-file` (palette lane) | Ctrl+P | `file_print` | quick open fired NOTHING |

`Ctrl+S` was already safe because task T80 stripped the editor's copy. **Red proof**:
`CHECK real.split-horizontal-fires FAIL (panes 1 -> 1)` and four
`authority.no-editor-collision.* FAIL (… also owned by …)` lines.

**(b) A user edit could create a duplicate.** The Shortcuts page stored whatever the user typed, even a
sequence another command already had. The duplicate is then stored, survives a restart, and both actions
are dead. **Red proof**: `CHECK conflict.registry-refused FAIL` (assigning Ctrl+S was accepted).

### Defect 1 — “看不见” (invisible): the page could not show the effective/default binding

The table had `Command | Shortcut | State` with an **empty cell** for every command without a binding,
which reads like a bug rather than “unbound”, and it never showed the **default** a custom binding
overrides (so “reset” had no visible target). It is now
`Command | Effective shortcut | Default | State`, with explicit `(unbound)` / `(none)` values and a state of
`default` / `custom` / `unbound`.

### Defect 3 — no defaults

Only 16 of the shell's commands carried a default; 6 user-facing commands had none, and four of the 16 were
dead (above). The new scheme follows VS Code wherever the product has the command.

## 2. The final mapping table (product-wide, effective bindings)

`product-bindings.txt` is the machine dump from `vedit-app --selfcheck` (32 non-empty bindings). “Conflict
check” = no other registry command and no live editor action owns the same sequence; the regression asserts
this per command (`authority.no-editor-collision.<id>`, green).

| # | command id | binding | state | conflict check |
|---|---|---|---|---|
| 1 | `edit.undo` | Ctrl+Z | default | shell-owned; editor `edit_undo` copy stripped |
| 2 | `edit.redo` | Ctrl+Shift+Z | default | shell-owned; editor `edit_redo` copy stripped |
| 3 | `settings.preferences` | Ctrl+, | default | free |
| 4 | `file.new` | Ctrl+N | default | free |
| 5 | `file.open` | Ctrl+O | default | free |
| 6 | `file.open-folder` | Ctrl+Shift+Alt+O | default | free |
| 7 | `file.switch-project` | Ctrl+Shift+Alt+P | default | free |
| 8 | `file.new-layout` | Ctrl+Alt+N | default | free |
| 9 | `file.save` | Ctrl+S | default | shell-owned (T80); editor copy stripped |
| 10 | `file.save-as` | Ctrl+Shift+S | default | shell-owned **(was dead)**; editor `file_save_as` stripped |
| 11 | `file.save-all` | Ctrl+Shift+Alt+S | default | free |
| 12 | `file.save-session` | — | **unbound** | explicit `(unbound)` in the page |
| 13 | `file.close-pane` | Ctrl+W | default | free |
| 14 | `file.quit` | Ctrl+Q | default | free |
| 15 | `view.split.horizontal` | Ctrl+Alt+H | default | free **(was Ctrl+Shift+H = dead)** |
| 16 | `view.split.vertical` | Ctrl+Shift+V | default | free |
| 17 | `view.focus.next` | F6 | default | free |
| 18 | `view.layout.mixed` | Ctrl+Alt+M | default | free |
| 19 | `view.layout.reset` | Ctrl+Alt+R | default | free |
| 20 | `view.toolbar` | Ctrl+Alt+T | default | free |
| 21 | `search.focus` | Ctrl+Shift+Alt+F | default | free (see §5: Ctrl+Shift+F is `diagnostics.format`) |
| 22 | `view.panel.file-tree` | Ctrl+Alt+F | default | free |
| 23 | `view.panel.search` | Ctrl+Alt+S | default | free |
| 24 | `view.panel.terminal` | Ctrl+Alt+J | default | free |
| 25 | `view.panel.vcs` | Ctrl+Alt+C | default | free **(was Ctrl+Alt+V = dead)** |
| 26 | `view.panel.outline` | Ctrl+Alt+O | default | free |
| 27 | `view.panel.diagnostics` | Ctrl+Alt+D | default | free (diagnostics lane) |
| 28 | `help.user-manual` | F1 | default | free |
| 29 | `help.about` | — | **unbound** | explicit `(unbound)` |
| 30 | `file.goto-file` | Ctrl+P | default | **was dead** (editor Print); editor copy stripped (palette lane) |
| 31 | `diagnostics.definition` | F12 | default | free |
| 32 | `diagnostics.references` | Shift+F12 | default | free |
| 33 | `diagnostics.rename` | F2 | default | free |
| 34 | `diagnostics.format` | Ctrl+Shift+F | default | free, one source (`lsp_actions.cpp`); unchanged |
| 35 | `diagnostics.hover` | — | **unbound** | explicit |
| 36 | `diagnostics.capabilities` | — | **unbound** | explicit |
| 37 | `diagnostics.reloadSettings` | — | **unbound** | explicit |
| 38 | `project.lint` | — | **unbound** | explicit |
| 39 | `project.reparse` | — | **unbound** | explicit |

Commands the task asked for that the product does not have (find/replace/goto-line/next-prev-tab/rename/
go-to-definition are the editor's own KTextEditor/lane actions; quick open, toggle-panel and format map to
the commands above): **not invented**. `Ctrl+F`, `Ctrl+H`, `Ctrl+R`, `Ctrl+G`, `F3`, `Shift+F3`,
`Ctrl+Shift+B`, `Ctrl+Tab` remain KTextEditor's own bindings and are listed in
`probe/log-kte-action-dump.txt`; the editor's action list is asserted UNCHANGED for every sequence the shell
does not own (`authority.editor-find-kept`).

**Duplicate assertion**: `commands.no-duplicate-shortcuts PASS` in `vedit-app --selfcheck`, and the
regression's `defaults.no-duplicate-bindings` + the per-command editor-collision loop, green.

## 3. The fix

- **Ownership authority (the teeth).** `CommandRegistry` now exposes the registered default, the effective
  binding, user-override state, the registry owners and the *foreign* (editor) owners of a sequence
  (`setForeignShortcutProvider`). After every `refreshShortcuts()` the registry calls a shell-installed
  **applier**; the shell pushes every effective sequence into `EditorArea::setOwnedShortcuts()`, and each
  pane removes those sequences from the editor's own actions on creation, tab add, provider swap, focus/show
  and the 2-second T80 guard. So a shell command and the editor can never be ambiguous, and a rebinding is
  effective **immediately** — no restart.
- **Defaults** in `ApplicationShell::registerCommands()`: the VS Code-like table above, avoiding every
  sequence KTextEditor owns (verified against the live action table).
- **Shortcuts page** (`SettingsPanel`): four columns with the effective binding, the default, and an explicit
  `unbound`; **Reset to Default** (per selected command) and **Reset All to Defaults**; assigning a sequence
  owned by another command or by the editor is **refused with the owner named**
  (“Shortcut Ctrl+S is already used by Save (file.save) — nothing was changed.”); conflicts that already
  exist (hand-edited/legacy store) are still rendered in red.
- **Translations**: the 14 new strings are in `translations/vedit_zh_CN.ts`; `bash build/gates/lrelease.sh`
  → `1236 translations (1236 finished, 0 unfinished, 0 untranslated)` PASS.

## 4. Red → green (the “生效” proof)

`src/app/shortcut_key_path_test.cpp` → ctest `vedit-shortcut-key-path-tests` (new). It drives the REAL key
path — `QTest::keyClick` to the main window's `QWindow`, so Qt's shortcut map dispatches exactly like a
physical keystroke — plus the real Shortcuts page (`QTableWidget` row select + `QKeySequenceEdit` + Assign)
and the modal Preferences dialog (preview + OK).

**RED** (`red-run.log`, only the defaults/authority reverted to the pre-fix behaviour):

```
CHECK defaults.edit.undo-has-binding FAIL … defaults.file.new/save-all/search.focus/view.toolbar FAIL
CHECK defaults.split-horizontal-free-sequence FAIL (Ctrl+Shift+H)
CHECK conflict.registry-refused FAIL (Ctrl+Shift+H)
CHECK authority.no-editor-collision.file.save-as FAIL (Ctrl+Shift+S also owned by Save As… (file_save_as))
CHECK authority.no-editor-collision.view.split.horizontal FAIL (Ctrl+Shift+H also owned by Find Selected Backwards (edit_find_selected_backwards))
CHECK authority.no-editor-collision.view.panel.vcs FAIL (Ctrl+Alt+V also owned by Switch to Next Input Mode (switch_next_input_mode))
CHECK authority.no-editor-collision.test.ctrl-p-probe FAIL (Ctrl+P also owned by Print… (file_print))
CHECK real.split-horizontal-fires FAIL (panes 1 -> 1)
CHECK real.editor-conflict-refused FAIL (Ctrl+Shift+H)
SUMMARY checks=66 failures=19
```

**GREEN** (`green-run.log`):

```
CHECK custom.effective-updated / custom.new-key-fires / custom.old-default-inert PASS
CHECK persist.restart-loaded / persist.restart-key-fires PASS
CHECK dialog.preview-effective / dialog.ok-persisted / dialog.ok-restart-value PASS
CHECK visible.split-effective-default / visible.split-default-column / visible.unbound-is-explicit PASS
CHECK conflict.registry-refused / conflict.registry-owner-named PASS
CHECK reset.per-command / reset.all-at-once PASS
CHECK authority.editor-find-kept PASS
CHECK authority.no-editor-collision.<every command> PASS (32)
CHECK authority.ctrl-p-shell-owned / authority.ctrl-p-fires PASS
CHECK real.split-horizontal-fires / real.split-vertical-fires PASS
CHECK real.ctrl-z-undoes / real.ctrl-shift-z-redoes / real.ctrl-z-undoes-again PASS
CHECK real.editor-conflict-refused / real.editor-conflict-named PASS
SUMMARY checks=75 failures=0
```

The **persistence** half: the custom binding is written through the existing `IShortcutStore`
(`shortcuts.json`, plus the settings lane's `keymap.*` mirror) and a SECOND `ApplicationShell` on the same
state dir loads it and fires it (`persist.restart-key-fires`).

Undo/redo deserve one note: with Ctrl+Z / Ctrl+Shift+Z now owned by the shell commands, a real key press
runs `edit.undo` → the active editor's own `edit_undo` action (the same operation the toolbar uses), proven
by `real.ctrl-z-undoes` (text reverts) and `real.ctrl-shift-z-redoes` (text returns). The RM-5a undo-floor
fix is unaffected — `vedit-undo-view-lifetime-tests` and `vedit-edit-core-selftest` still pass.

### Adjacent suites re-run (observed)

```
vedit-app-shell-selftest ............ Passed   (319 checks, 0 failures; no-duplicate + Ctrl+S authority)
vedit-shortcut-key-path-tests ....... Passed   (75 checks, 0 failures)
vedit-workspace-settings-selftest ... Passed   (conflict contract updated, §6)
vedit-workspace-palette-selftest / palette-integration ... Passed
vedit-workspace-diagnostics-selftest ...................... Passed
vedit-edit-core-selftest .................................. Passed
```

## 5. Deviations from the VS Code list, stated plainly

- **`Ctrl+Shift+F` for project-wide search is NOT used.** It is already the pinned default of
  `diagnostics.format` (single source `src/editor/kernel/lsp_actions.cpp`, asserted by
  `shortcut.format.registry-equals-fallback`). Changing it is a cross-lane contract change outside this
  task's write scope, so `search.focus` takes the next free sequence, **Ctrl+Shift+Alt+F**.
- **`Ctrl+B` for a panel toggle is NOT used.** KTextEditor owns Ctrl+B (Toggle Bookmark); the panel toggles
  keep the `Ctrl+Alt+*` family and `view.panel.vcs` moved off Ctrl+Alt+V to Ctrl+Alt+C.
- **`Ctrl+P`, `Ctrl+Shift+S`, `Ctrl+Z`, `Ctrl+Shift+Z`** are kept as VS Code bindings by taking them from the
  editor's own copies (the T80 single-authority pattern, now generalised). The editor's *feature* is not
  removed: only the duplicate key binding, and the shell command drives the same editor operation.
- Two-stroke VS Code sequences (Ctrl+K Ctrl+O / Ctrl+K S) were not copied: the Shortcuts page has one
  sequence field per command, so free single-stroke siblings were chosen instead.
- Commands that still have no binding are deliberate (Save Session, About, hover/capabilities/reload,
  lint/reparse) and are now an explicit `(unbound)` state.

## 6. Bounds — what this evidence does NOT prove

1. The regression runs **offscreen**; the key delivery uses `QTest` on the `QWindow` (the real shortcut path),
   but a physical keyboard on the user's machine was not exercised by me.
2. The **full gate was not run** here (`scripts/gates.sh`, `build/gates/qt6-cxx.sh` — the captain runs it).
   The targeted suites above, the lrelease gate and `vedit-app --selfcheck` were run.
3. The palette's `file.goto-file` command is proven **by mechanism** (the regression plants a Ctrl+P command
   in the same registry and proves the editor's Print copy is stripped and the key fires); the palette
   integration selftest passes, but I did not open the palette dialog with a real Ctrl+P press (it is a
   modal editor surface).
4. A user edit that collides with the **editor's** keys is REFUSED with the owner named, not applied. That
   is a product decision (the alternative — silently stripping the editor's binding on a user edit — would
   make the editor's keys change under the user without a visible record).
5. `settings_selftest.cpp` encoded the OLD conflict contract (“bind Ctrl+S to a second command, then see the
   hint”). It was updated to the new one (refusal + owner named; an already-existing conflict from a legacy
   store is still shown in red) — this is a test of a changed contract, not a test relaxed to pass.
6. The **default table for lane commands** (palette/diagnostics) is documented but only the palette's Ctrl+P
   was changed by this task (via the authority); their defaults were already free.
7. **`docs/USER_GUIDE.md` still lists the OLD sequences** for split-horizontal (Ctrl+Shift+H) and the VCS
   panel (Ctrl+Alt+V) — lines 166, 390-403. That file is outside this task's write scope AND is being edited
   by another lane right now (`git status` shows it modified), so it was left alone deliberately: the guide
   needs a follow-up edit from its owner. The shell's own demo-editor placeholder text WAS updated
   (`src/app/editor_provider.cpp`, Ctrl+Alt+H), because it is rendered by the product itself.

## 7. Files changed

- `src/app/command_registry.{h,cpp}` — default/effective/override/owner introspection + foreign provider +
  shortcut applier.
- `src/app/editor_area.{h,cpp}` — shell-owned sequence list; generalised editor authority
  (`clearOwnedShortcuts`); `editorShortcutOwners()` for the conflict UI.
- `src/app/app_shell.{h,cpp}` — new default table; foreign provider wiring; `syncOwnedShortcuts()` after
  registration and on every rebinding (shortcut/command-registry region only).
- `src/workspace/settings/settings_panel.{h,cpp}` — four-column table with explicit unbound/default state,
  Reset to Default / Reset All, conflict refusal with the owner named, editor-conflict reporting.
- `src/workspace/settings/settings_selftest.cpp` — the conflict contract updated (§6.5).
- `src/app/editor_provider.cpp` — the demo editor's on-screen shortcut hint (split-horizontal now
  Ctrl+Alt+H); no behaviour change.
- `src/app/shortcut_key_path_test.cpp` (new) + `src/app/CMakeLists.txt` registration.
- `translations/vedit_zh_CN.ts` — 14 new strings (lrelease gate PASS).
- `evidence/ux-batch-2/rm5b-shortcuts/` — this README, `red-run.log`, `green-run.log`,
  `product-selfcheck.log`, `product-bindings.txt`, `probe/`.

## 8. Re-run

```bash
docker run --rm -v "$PWD:/vedit-build" -e HOME=/tmp vedit-qt6-build:local bash -lc \
  'cd /vedit-build && cmake --build target/qt6-build --target vedit-shortcut-key-path-tests && \
   cd target/qt6-build && QT_QPA_PLATFORM=offscreen ctest -R vedit-shortcut-key-path-tests -V'
bash build/gates/lrelease.sh
# product-wide binding dump
docker run --rm -v "$PWD:/vedit-build" -e HOME=/tmp vedit-qt6-build:local bash -lc \
  'cd /vedit-build && QT_QPA_PLATFORM=offscreen ./target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc 2>&1 | grep COMMAND-BINDING'
```
