# multicursor-editing — evidence (cell `cell-20261102T050548232Z`, task `T14`)

Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/multicursor-editing.md`
(verifier loop `loop-20261102T063252-0bbd08`).
**ADDENDUM A-10 (T14)** grants `src/editor/kernel/multi_cursor_selftest.cpp`.

## What landed

| path | role |
|---|---|
| `src/editor/kernel/multi_cursor.h` | native cursor-set helpers + `deleteAtCursors`, the `MultiCursorController` (block selection / Ctrl+click / batch keys) and `EditorEnhancements` (AC-4.4). Both classes live here because the frozen scope grants only this pair for implementation. |
| `src/editor/kernel/multi_cursor.cpp` | both controllers + the batch edit core (one `EditingTransaction` per keystroke, tracked by KTextEditor moving cursors) |
| `src/editor/kernel/multi_cursor_selftest.cpp` | **ADDENDUM A-10** — the lane ctest TU (real QTest mouse drags/clicks and real key events on a real view) |
| `src/editor/kernel/ktexteditor_provider.cpp` | installs both controllers on every view `createEditor()` returns |
| `src/editor/CMakeLists.txt` | `vedit-editor-multi-cursor-selftest` target + ctest entry |
| `evidence/e3-edit/` | this README, the logs below, three screenshots |

The four pre-existing multi-cursor helpers (`setViewCursors`, `viewCursors`,
`insertAtCursors`, `viewInsertText`) keep their signatures and behaviour; the existing
`edit_core_selftest` checks still pass (it is test #2 in `01-lane-ctest.log`).

## AC-4.2 mapping (all checks PASS in `02-multi-cursor-checks.log`)

| AC-4.2 item | checks |
|---|---|
| Alt+drag rectangular selection | `edit.block.drag-active`, `edit.block.drag-native-mode` (the kernel's own `View::setBlockSelection` is on during the drag; coordinates come from the public `View::coordinatesToCursor`) |
| block selection really is a *column* | `edit.block.cursor-count` (3 lines), `edit.block.left-column` (one cursor per line at the block's left column), `edit.block.rectangular-insert` (typing `X` → `Xone` / `Xtwo` / `Xthree`, line 4 untouched), `edit.block.one-undo` |
| Ctrl+click adds a cursor | `edit.ctrl-click.add` (count 1→2), `edit.ctrl-click.typing` (`ayaa` / `bybb`), `edit.ctrl-click.one-undo` |
| Ctrl+click on an existing cursor **removes** it (toggling is implemented, not a no-op) | `edit.ctrl-click.toggle-off` (2→1), `edit.ctrl-click.readd` |
| multi-cursor typing / delete, ONE undo per key | `edit.multi.type` + `edit.multi.type-one-undo`, `edit.multi.backspace` + `…-one-undo`, `edit.multi.delete` + `…-one-undo`, `edit.multi.return` + `…-one-undo` (each starts from a fresh 3-cursor set) |

## AC-4.4 mapping (each key asserts the observed buffer, not just "PASS")

| AC-4.4 item | key | checks |
|---|---|---|
| auto-indent | Return | `edit.return.auto-indent` (4-space indent copied), `edit.return.cursor-after-indent`, `edit.return.brace-indent` (one extra unit after `{`) |
| bracket/quote auto-pair | `(` / `)` / `"` | `edit.pair.round` (`()`), `edit.pair.inside`, `edit.pair.skip-over`, `edit.pair.backspace-deletes-both`, `edit.pair.quote` (`"a"`) |
| Ctrl+/ comment toggle | Ctrl+/ | `edit.comment.on` (`    // code;`), `edit.comment.off` |
| Alt+↑/↓ move line | Alt+Up / Alt+Down | `edit.move.up`, `edit.move.down` (text and cursor line) |
| Shift+Alt+↓ duplicate line | Shift+Alt+Down | `edit.duplicate.line` (`a b b c`) |
| Ctrl+Shift+K delete line | Ctrl+Shift+K | `edit.delete.line` |
| case conversion | Ctrl+Shift+U / Ctrl+Shift+L | `edit.case.upper`, `edit.case.lower` |
| bracket-match highlight | (native, always on) | `edit.bracket.native-highlight` — pixel differential: with the cursor right after `(` KatePart paints the bracket and its match; one column away the same glyph area is unchanged. See `bracket-match.png` / `bracket-match-crop.png`. |
| word wrap toggle | Ctrl+Shift+W | `edit.wrap.flip`, `edit.wrap.config` (`dynamic-word-wrap` flips), `edit.wrap.back` |
| font zoom | Ctrl+= / Ctrl+- | `edit.zoom.in`, `edit.zoom.config-changed` (the `font` config value really changes), `edit.zoom.out` (back to the exact starting size) |

## Two mechanism facts this lane had to solve (and asserts)

1. **Where keys actually land.** KatePart inserts text in an inner child widget; a filter
   installed only on the outer `KTextEditor::View` never sees typing. The filters are
   installed on the whole widget tree below the view (`installOnWidgetTree`) and on
   children added later (`QEvent::ChildAdded`).
2. **Who owns the key.** KatePart backs Backspace/Delete/Return with actions, and those
   are resolved at the `ShortcutOverride` stage — without claiming the override, the key
   is consumed before any `KeyPress` handler runs. The controllers therefore claim the
   override **only for the keys they will handle** (multi-cursor text/Backspace/Delete/
   Return; the AC-4.4 command keys; Return always; Backspace only when an empty pair is
   under the cursor). Everything else stays the kernel's. Competing QActions lose their
   sequences (`clearCompetingActions`), and `edit.keys.single-owner` fails if a second
   owner exists — the same rule the find/replace bar uses.

## Real-focus verification (ADDENDUM A-14 follow-up)

The lane's key checks drive `keyTarget()` = `QApplication::focusWidget()` — KatePart's
inner widget — not the public `View`. Six explicit checks make that visible:
`edit.real-focus.inner-widget` (the target really is a descendant of the view, not the
view itself), `edit.real-focus.multi-cursor-typing` (3 cursors, one key through the inner
widget → 3 insertions), `edit.real-focus.line-op` (Alt+Up through the inner widget),
`edit.real-focus.block-typing-target` (the rectangular insert after the Alt+drag is typed
through the inner widget), plus their setup checks. All pass, so T14's keys do not have the
"green test, dead product" problem.

## Commands and observed results (pinned Qt6 image, workspace at `/vedit-build`)

```
cmake -S src/editor -B target/lane-find -G Ninja                 # exit 0
cmake --build target/lane-find --target \
  vedit-editor-multi-cursor-selftest vedit-editor-find-replace-selftest \
  vedit-edit-core-selftest                                       # exit 0
ctest --test-dir target/lane-find -R "find-replace|multi-cursor|edit-core"
  -> 3/3 passed, exit 0                                          # 01-lane-ctest.log
./target/lane-find/vedit-editor-multi-cursor-selftest --screenshot evidence/e3-edit
  -> 61 checks, 0 failures, exit 0                               # 02-multi-cursor-checks.log
cmake --build target/qt6-build --target vedit-app
  -> last GREEN run: exit 0; selfcheck 245/245, exit 0 — that run covered this T14 tree
     before the external break below
  -> CURRENTLY RED for an EXTERNAL reason: src/workspace/diagnostics/ffi_backends.cpp
     (T9/se-ui, in flight) does not compile (`serverPath_`, `backend_`, ... were not
     declared). Not a T14 path; the editor lane is green (3/3 and 15/15).
     (03-product-build.log records that failure; 04-product-selfcheck.log is the last
     product binary from before the break, not a verification of the A-14 hardening.)
cmake --build target/lane-find && ctest --test-dir target/lane-find
  -> 100% tests passed, 15/15                                    # 05-lane-full-build-ctest.log
```

Screenshots: `block-selection-drag.png` (the real `[BLOCK]` mode rectangle while the
mouse is still down), `multi-cursor-result.png` (the rectangular `X` insertion on lines
1–3, line 4 untouched), `bracket-match.png` + `bracket-match-crop.png` (the native
bracket highlight, yellow background on `(` and its match).

## How this was verified / how it was NOT

Verified: real `KTextEditor::Document` + `View`, real QTest **mouse** drags/clicks at
coordinates produced by `View::cursorToCoordinate()`, real key events, the provider's
`createEditor()` install path, the product link, the product's own `--selfcheck`, and the
full standalone lane ctest.

Bounds (deliberate, stated rather than implied):

* **Offscreen QTest only** (same as T13); no real X11 window.
* **Typing is literal**: multi-cursor typing inserts the key's text at every cursor; no
  IME/compose handling.
* **No column-mode paste** from the clipboard (not in AC-4.2; typing and deletion are).
* **Block drag → cursors**: after Alt+drag the block selection is converted to one cursor
  per line at the block's left column (typing inserts, it does not overwrite the block);
  the selection itself is removed.
* **Line operations** act on the cursor line or the selected line span; moving a block
  down at the last line is a no-op (no wrap).
* **Auto-pair** is a simple quote/bracket rule: it is not string/comment aware, and a
  quote before a word is left alone (apostrophe heuristic).
* **Bracket match** is KatePart's native renderer, always on. KF6 6.24.0 documents
  `disable-bracket-match-highlight-if-inactive` in `view.h` but the View interface does
  **not** accept it (`configKeys()` omits it; a set/get round-trip stays invalid), so the
  lane proves the feature with pixels instead of a config key and adds no second
  renderer. This is the one place where the AC is verified visually rather than through
  a public knob.
* **Case conversion** applies to the selection, else the current line.

## Scope statement for AC-1

My changed paths: `multi_cursor.h`, `multi_cursor.cpp`, `multi_cursor_selftest.cpp`
(**ADDENDUM A-10**), `ktexteditor_provider.cpp`, `src/editor/CMakeLists.txt`,
`evidence/e3-edit/`. No `find_replace.*`, no `editor_context_menu.*` (does not exist yet),
no `src/app/**`.
