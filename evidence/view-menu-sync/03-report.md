# T129 — View-menu check marks follow dock visibility (all panels)

## Root cause

`ApplicationShell::registerCommands()` creates the six View-menu panel actions
as **custom checkable `QAction`s** (not `QDockWidget::toggleViewAction()`). The
menu handler set `action->setChecked(visible)` only when the menu itself was used.
Closing a dock with its own X emitted no signal to the action, so the check mark
stayed checked; the next menu click looked like a no-op (the reported “click
twice” bug).

## Fix

Bidirectional sync, in the dock layer (all panels, not one-off):

- `PanelRegistry::setVisibilityListener(std::function<void(const QString&,bool)>)`;
  `insert()` connects the dock's `QDockWidget::visibilityChanged` to that
  listener and stores the connection.
- `ApplicationShell` installs one listener that mirrors
  `visible` onto `commands()->action("view.panel." + id)->setChecked(visible)`.
  Menu → dock (existing handler), dock X / programmatic hide/show → menu (new).
- Destructor disconnects the stored connections and clears the listener before
  the `PanelRegistry`/`std::function` die. Without this, dock destruction during
  shell teardown called into a dead registry and `--selfcheck` segfaulted
  (found while testing; fixed in the same change).

## Which panels had it

All six registered panels had the same one-way behaviour; none was special:

`file-tree`, `search`, `terminal`, `vcs`, `settings`, `outline`.

(There is no separate “diagnostics” dock in the registry; diagnostics content is
a panel lane, not a View-menu dock.)

## Headless proof (all six, reverse control)

`01-selfcheck.log`: `APP-SHELL-SELFTEST ok=1 checks=220 failures=0`.

For every panel id the selfcheck asserts:
1. action shows the dock → dock visible and action checked;
2. **dock’s own close** → dock hidden and action **unchecked** (the bug);
3. one action trigger reopens it → visible and checked (not two clicks);
4. reverse control: `QSignalBlocker` suppresses `visibilityChanged`, close then
   asserts the action **stays checked** — i.e. step 2 would be red if the sync
   were removed.

The 220-count includes `panels.sync.*` for all six plus
`panels.sync.reverse-control`.

## Gates

- root ctest: **31/31 passed**, `CTEST-EXIT=0` (`02-gates.log`).
- `bash build/gates/typecheck.sh`: **PASS** (`TYPECHECK-EXIT=0`).
- No change to panel behaviour, layout, shortcuts, or session format.

## Files

`src/app/panel_registry.{h,cpp}`, `src/app/app_shell.cpp`,
`src/app/main.cpp` (selfcheck only). No `src/syntax/**`, no `packaging/**`.
