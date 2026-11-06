# RM-5c — shipped defaults must also avoid DESKTOP-global keys (+ next-tab, Ctrl+Y, format, L2)

- **Row**: T29 (board), follow-up review findings M1/M2/L2 of `evidence/ux-batch-2/review-rm5-final/REPORT.md`.
- **Branch**: `feat/ux-batch-2`. Evidence files: `red-run.log` (DE assertion), `green-run.log`,
  `product-selfcheck.log` (product-wide binding dump), `ctest-targeted.log`.
- **Verdict**: all four items closed: the DE-global set is DATA with a failing-when-violated assertion,
  the two colliding defaults were moved, next/prev-tab now exist, Ctrl+Y and Shift+Alt+F work as extra
  defaults, and the zero-edit shell-path Ctrl+Z case is green.

## 1. M1 — the DESKTOP-global set as data

New module `src/app/desktop_shortcuts.{h,cpp}` (compiled into `vedit_app_shell`):
`desktopGlobalShortcuts()` returns the well-known KDE/GNOME bindings in the family this product uses;
`desktopGlobalShortcutOwner(sequence)` answers "which desktop action owns this, if any".

| sequence | desktop action |
|---|---|
| `Ctrl+Alt+T` | GNOME/Ubuntu: open terminal |
| `Ctrl+Alt+D` | KDE: show desktop |
| `Ctrl+Alt+L` | GNOME/KDE: lock screen |
| `Ctrl+Alt+Del` | GNOME/KDE: log out / shutdown dialog |
| `Ctrl+Alt+Up` / `Down` / `Left` / `Right` | GNOME/KDE: switch workspace |
| `Ctrl+Alt+Esc` | KDE/X11: kill window |
| `Ctrl+Alt+Backspace` | X11: terminate the X server (legacy) |
| `Ctrl+Alt+F1` … `Ctrl+Alt+F7` | virtual terminals (the kernel grabs these) |

The shell feeds this list into the same **foreign-owner** seam that already carried KTextEditor's own
actions, so:
- the Shortcuts page **refuses** a user assignment to a DE-global sequence and names the desktop action
  (`conflict.desktop-refused` / `conflict.desktop-owner-named`, status contains `desktop: …`);
- `conflicts()` reports any binding that ends up DE-owned;
- the regression asserts, per shipped binding, that no desktop owns it (`de.no-global-default.*`).
- **Static defaults, one value everywhere — no runtime re-home.** The first version moved
  `view.panel.diagnostics` at startup because its registration lives in the diagnostics lane (outside
  this row's write scope). The Lead asked for ONE mechanism and a static value is simpler, testable,
  documentable and cannot change under a user who moves between desktops — so the lane's ONE line was
  changed instead (`diagnostics_integration.cpp`, authorised by that instruction) and the re-home
  mechanism was deleted. Every affected command now ships a value outside both sets; the manual can
  document values rather than a rule.
- Lane commands are covered by the **product** assertion: `vedit-app --selfcheck` runs
  `commands.no-desktop-global-shortcuts` over the LANE-INSTALLED registry (all commands, all extras),
  which is where `view.panel.diagnostics` is visible. The plain-shell regression asserts the same
  property over the shell's own commands (`de.no-global-default.*`).

### The moved defaults

| command | was | now | why |
|---|---|---|---|
| `view.toolbar` | `Ctrl+Alt+T` | `Ctrl+Alt+Shift+T` | GNOME/Ubuntu's “open terminal” is consumed before the app |
| `view.panel.diagnostics` (diagnostics lane) | `Ctrl+Alt+D` | `Ctrl+Alt+Shift+D` | KDE's “show desktop” is consumed before the app; source default changed in place (one line) and asserted by the product selfcheck |

The remaining `Ctrl+Alt+<letter>` defaults (`F/S/J/C/O/H/M/N/R`) are **not** in the DE table and stay —
they keep the VS Code-style familiarity the review asked to preserve; the data file is the one place to
extend if a desktop claims another.

## 2. M2 — Ctrl+Tab, Ctrl+Y, Shift+Alt+F

- **Next/prev tab now EXIST as real commands** (the review's claim was correct: nothing provided them):
  `view.tab.next` = `Ctrl+Tab`, `view.tab.previous` = `Ctrl+Shift+Tab`, in the View menu next to Focus
  Next Pane. They wrap around and are no-ops with fewer than two tabs. Proven through the real key path
  on the real shell + KTextEditor: `tabs.setup-two-tabs` → `tabs.ctrl-tab-next` → `tabs.ctrl-shift-tab-previous`.
- **Ctrl+Y** (VS Code's alternative redo) is an **extra default** on `edit.redo`: the primary stays
  `Ctrl+Shift+Z`; the Shortcuts page shows `Ctrl+Shift+Z / Ctrl+Y`, reset restores both, a user override
  replaces both. Proven by a real key press: `real.ctrl-y-redoes`, freshly added to the RM-5b regression.
- **Shift+Alt+F** (VS Code's format) is an **extra default** on `diagnostics.format`; the primary stays
  `Ctrl+Shift+F`, so the kernel's cross-lane `shortcut.format.registry-equals-fallback` assertion still
  holds (`product-selfcheck.log`). Added by the shell after the diagnostics lane registers.
- The README's old claim "next-prev-tab are the editor's own" was **wrong** and is corrected here: the
  KTextEditor dump has no Ctrl+Tab; the product simply had no such command. The user manual is the manual
  lane's (L3, RM-8b), so this table is the record until that lane updates the guide.

## 3. The final mapping table — checked against THREE sets

36 bindings over 34 commands (product-wide, from `vedit-app --selfcheck`). Columns: every sequence the
command carries (primary + extras), KTextEditor's own action set (105 sequences, `rm5b-shortcuts/probe`),
the desktop set above, and the product duplicate check. “editor copy stripped” = the sequence the shell
owns is removed from KTextEditor's own action by the RM-5b authority (T80 rule), so the two can never be
ambiguous; it is NOT a live collision.

| command | binding(s) | KTE-free | DE-free | product-unique |
|---|---|---|---|---|
| `diagnostics.format` | `Ctrl+Shift+F` + `Alt+Shift+F` | yes | yes | yes |
| `settings.preferences` | `Ctrl+,` | yes | yes | yes |
| `view.panel.vcs` | `Ctrl+Alt+C` | yes | yes | yes |
| `view.panel.file-tree` | `Ctrl+Alt+F` | yes | yes | yes |
| `view.split.horizontal` | `Ctrl+Alt+H` | yes | yes | yes |
| `view.panel.terminal` | `Ctrl+Alt+J` | yes | yes | yes |
| `view.layout.mixed` | `Ctrl+Alt+M` | yes | yes | yes |
| `file.new-layout` | `Ctrl+Alt+N` | yes | yes | yes |
| `view.panel.outline` | `Ctrl+Alt+O` | yes | yes | yes |
| `view.layout.reset` | `Ctrl+Alt+R` | yes | yes | yes |
| `view.panel.search` | `Ctrl+Alt+S` | yes | yes | yes |
| `view.panel.diagnostics` | `Ctrl+Alt+Shift+D` | yes | yes | yes |
| `search.focus` | `Ctrl+Alt+Shift+F` | yes | yes | yes |
| `file.open-folder` | `Ctrl+Alt+Shift+O` | yes | yes | yes |
| `file.switch-project` | `Ctrl+Alt+Shift+P` | yes | yes | yes |
| `file.save-all` | `Ctrl+Alt+Shift+S` | yes | yes | yes |
| `view.toolbar` | `Ctrl+Alt+Shift+T` | yes | yes | yes |
| `file.new` | `Ctrl+N` | yes | yes | yes |
| `file.open` | `Ctrl+O` | yes | yes | yes |
| `file.goto-file` | `Ctrl+P` (editor copy stripped) | yes | yes | yes |
| `file.quit` | `Ctrl+Q` | yes | yes | yes |
| `file.save` | `Ctrl+S` (editor copy stripped) | yes | yes | yes |
| `file.save-as` | `Ctrl+Shift+S` (editor copy stripped) | yes | yes | yes |
| `view.tab.previous` | `Ctrl+Shift+Tab` | yes | yes | yes |
| `view.split.vertical` | `Ctrl+Shift+V` | yes | yes | yes |
| `edit.redo` | `Ctrl+Shift+Z` + `Ctrl+Y` (editor copy stripped) | yes | yes | yes |
| `view.tab.next` | `Ctrl+Tab` | yes | yes | yes |
| `file.close-pane` | `Ctrl+W` | yes | yes | yes |
| `edit.undo` | `Ctrl+Z` (editor copy stripped) | yes | yes | yes |
| `help.user-manual` | `F1` | yes | yes | yes |
| `diagnostics.definition` | `F12` | yes | yes | yes |
| `diagnostics.rename` | `F2` | yes | yes | yes |
| `view.focus.next` | `F6` | yes | yes | yes |
| `diagnostics.references` | `Shift+F12` | yes | yes | yes |

Commands that remain deliberately **unbound** (explicit `(unbound)` in the page): `file.save-session`,
`help.about`, `diagnostics.hover`, `diagnostics.capabilities`, `diagnostics.reloadSettings`,
`project.lint`, `project.reparse`.

**Deviations still standing** (stated rather than omitted): `Ctrl+Shift+F` belongs to Format (the
kernel's pinned single source), so project search uses `Ctrl+Shift+Alt+F`; `Ctrl+B` is KTextEditor's
bookmark; the editor's own keys `Ctrl+F/H/R/G`, `F3/Shift+F3` stay with the editor (no registry command);
two-stroke sequences are not used. `Ctrl+Y` and `Shift+Alt+F` are now real extra defaults, not deviations.

## 4. L2 — zero-edit Ctrl+Z through the SHIPPED path

Added to the RM-5b regression (real `ApplicationShell` + the real editor provider), immediately after a
file is opened with **zero edits**:

```
zero-edit-shell.undo-command-disabled     PASS  (the shell's edit.undo QAction is disabled: no undo step)
zero-edit-shell.buffer-unchanged          PASS  (a real Ctrl+Z through the window leaves the text intact)
zero-edit-shell.identity                  PASS  (same pane/view/document objects)
zero-edit-shell.undo-still-disabled       PASS  (still no undo step was created)
```

This crosses the two lanes in one test: RM-5a's undo floor and RM-5b's shell-owned Ctrl+Z can no longer
trade silently.

## 5. Red → green for the DE assertion

**RED (shell regression)** — `red-run.log`, `view.toolbar` put back on `Ctrl+Alt+T`:

```
CHECK de.no-global-default.view.toolbar.Ctrl+Alt+T FAIL (Ctrl+Alt+T is a desktop-global shortcut (GNOME/Ubuntu: open terminal))
CHECK de.toolbar-moved-off-ctrl-alt-t FAIL (Ctrl+Alt+T)
CHECK authority.no-editor-collision.view.toolbar FAIL (Ctrl+Alt+T also owned by desktop: GNOME/Ubuntu: open terminal)
SUMMARY checks=123 failures=3
```

**RED (product level, the lane-owned command)** — `red-run-product-selfcheck.log`, the diagnostics lane's
default put back on `Ctrl+Alt+D`:

```
CHECK commands.no-desktop-global-shortcuts FAIL view.toolbar: Ctrl+Alt+T (GNOME/Ubuntu: open terminal) | view.panel.diagnostics: Ctrl+Alt+D (KDE: show desktop)
```

**GREEN** (`green-run.log`): `SUMMARY checks=123 failures=0`, including every `de.no-global-default.*`,
the static moved defaults, the DE refusal in the page, the two-tab Ctrl+Tab pair, Ctrl+Y redo and the
zero-edit shell undo case. Product selfcheck (`product-selfcheck.log`): 0 failed checks,
`COMMAND-BINDINGS count=36`, `commands.no-desktop-global-shortcuts PASS`,
`commands.no-duplicate-shortcuts PASS`.

Targeted ctest (`ctest-targeted.log`): `vedit-app-shell-selftest`, `vedit-shortcut-key-path-tests`,
`vedit-native-fold-tests`, `vedit-editor-multi-cursor-selftest`, `vedit-undo-view-lifetime-tests`,
`vedit-workspace-settings-selftest` — **6/6 Passed**. Product selfcheck: 0 failed checks,
`COMMAND-BINDINGS count=36`, `commands.no-duplicate-shortcuts PASS`.

## 6. The i18n one-liner (asked in the same pass)

`src/app/editor_area.cpp` — the active-pane badge was a bare `QStringLiteral("active")` (untranslatable,
never extractable by lupdate). It is now
`QCoreApplication::translate("VEdit", "active")`, the same context as its neighbours. The badge still
renders and the pane-activation test stays green (25/0). **The `translations/vedit_zh_CN.ts` row is
deliberately NOT added here** — the i18n lane is the single writer of the catalogue and will add
`<source>active</source>` (context `VEdit`) in its final pass, per the Lead's instruction.

## 7. Bounds

1. The DE set is **well-known default bindings**, not a live query of the user's desktop: no desktop
   exposes its global-shortcut table through a portable API. A user-remapped desktop can therefore still
   claim a sequence we ship (and a user can free one we avoid); the data file is the single place to
   adjust. KDE/GNOME version differences are the residual risk the review named.
2. The user's two desktops were not driven here (offscreen); the physical desktop remains the user's check.
3. The `view.panel.diagnostics` static default required ONE line in the diagnostics lane's own file
   (`src/workspace/diagnostics/diagnostics_integration.cpp`) — outside this row's listed write scope,
   but explicitly directed by the Lead's "static default, delete the rehome" instruction; it is the
   only change outside the listed scope, and the product selfcheck (`commands.no-desktop-global-shortcuts`)
   now guards it.
4. The full gate was not run in this row (the captain runs it).
5. The fold regression (separate follow-up) is resolved in `evidence/ux-batch-2/fold-regression/`: the
   product was not affected; the fold probe was observing an unlaid-out view, and now asserts fold +
   undo-floor together with the RM-5a regression.

## 8. Files changed

- `src/app/desktop_shortcuts.{h,cpp}` — **new**: the DE-global data set + lookup.
- `src/app/command_registry.{h,cpp}` — extra default bindings (`CommandSpec::extraDefaultShortcuts`,
  `addExtraDefaultShortcut`), `effectiveShortcuts`, and every-sequence iteration in
  `shortcutBindings`/`shortcutOwnerIds`. No runtime re-home: the defaults are static.
- `src/app/app_shell.{h,cpp}` — `view.toolbar` static default `Ctrl+Alt+Shift+T`; Ctrl+Y extra on redo;
  `view.tab.next` / `view.tab.previous` commands + handlers; the foreign-owner provider now includes the
  desktop set and labels owners (`desktop: …` / `editor: …`); the post-registration hook adds
  Shift+Alt+F to `diagnostics.format`.
- `src/workspace/diagnostics/diagnostics_integration.cpp` — `view.panel.diagnostics` static default
  `Ctrl+Alt+Shift+D` (one line; see §7.3).
- `src/app/main.cpp` — the product-wide `commands.no-desktop-global-shortcuts` selfcheck assertion
  (covers lane commands; red shown above).
- `src/workspace/settings/settings_panel.cpp` — the effective cell shows every sequence (extras
  included); the conflict message uses the self-describing owner label (no invented suffix).
- `src/app/shortcut_key_path_test.cpp` — the DE assertion per binding, the toolbar move, the redo extra,
  the planted DE command + rehoming + key, the DE refusal in the page, the two-tab Ctrl+Tab pair, Ctrl+Y
  redo, and the L2 zero-edit shell undo case.
- `src/app/editor_area.cpp` — the `active` badge translation (catalogue row deferred to the i18n lane).
- `evidence/ux-batch-2/rm5c-defaults/` — this README + logs.
