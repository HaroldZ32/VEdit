# T130 — close a split pane (View menu + per-pane button), no silent data loss

## Entry points

- **View → Close Pane**, shortcut **Ctrl+W**. The command keeps its stable id
  `file.close-pane` (shortcut-store compatibility) but its menu is now `view`;
  Ctrl+W was already bound to this command, so the new View entry adds no second
  binding and conflicts with nothing.
- **Every pane header has a `×` button** (`objectName=pane-close-button`,
  tooltip "Close Pane"); clicking it closes that pane, not just the active one.

## Behaviour

- Closing a split removes exactly that pane; the surviving pane becomes active
  and its editor takes keyboard focus.
- Closing until one pane remains returns to the single-document view; the main
  window stays open. Closing the **last** pane replaces it with a fresh pane
  (the existing invariant: the editor area is never empty), never `close()`.
- **Unsaved changes block the close**: `EditorPane::isModified()` asks the
  provider (`EditCoreProvider` → `KTextEditor::Document::isModified()`, demo
  provider → `QPlainTextEdit::document()->isModified()`). The shell's
  `setOnClosePaneRequested` returns false, appends a diagnostic, shows a status
  bar message, and (in a real GUI, not offscreen) a `QMessageBox::warning`.
  Nothing is discarded; atomic save is untouched, the user saves (Ctrl+S) and
  closes again.
- No panel/layout/shortcut behaviour was otherwise changed. T129 sync is in the
  same shell file but untouched by this change.

## Tests (headless)

`01-selfcheck.log` (clean worktree `target/wt-55a874d` + this T130 code):
`APP-SHELL-SELFTEST ok=1 checks=215 failures=0`, including:

```
CHECK close.command.exists
CHECK close.command.in-view
CHECK close.command.shortcut-ctrl-w
CHECK close.shortcuts.no-conflicts
CHECK close.shortcuts.binding-count
CHECK close.split.pane-count
CHECK close.split.editor-editable
CHECK close.unsaved.blocked          (pane count unchanged + status says unsaved)
CHECK close.reverse-control         (entry disabled -> trigger closes nothing)
CHECK close.unsaved-saved-close     (after clearing modified, one trigger closes)
CHECK close.remaining-editable
CHECK close.last-single-view        (paneCount==1 and window still visible)
CHECK close.button.split
CHECK close.button.present
CHECK close.button.single-view      (per-pane × closes exactly one split)
```

The reverse control disables the command action before triggering it, so step 1
cannot pass without a working entry. Unsaved is exercised against the demo
provider in selfcheck; the product provider implements the same seam through
`KTextEditor::Document::isModified()`.

## Gates

- Clean worktree: ctest **27/27**, `CTEST-EXIT=0`; `typecheck` **PASS**.
- Main worktree `vedit-app` now builds; its selfcheck reports **232 checks,
  6 failures, all `settings` panel assertions** — caused by the in-flight T132
  change that removed the settings *dock* (selfcheck still expects six panels).
  Every `close.*` assertion passes; ctest #11 fails for the same T132 mismatch.
  This is an external stale-test issue, not a T130 regression; the clean-worktree
  suite is green. Reported to the Lead.

## Files

`src/app/editor_provider.h` (isModified seam), `src/editor/kernel/ktexteditor_provider.{h,cpp}`,
`src/app/editor_area.{h,cpp}` (closePane/guard/× button/focus), `src/app/app_shell.cpp`
(View menu + guard), `src/app/main.cpp` (selfcheck). No `src/syntax/**`,
`packaging/**`, tests/gates modified.
