# RM-6 — Alt gestures are eaten by the window manager: Shift+Alt / Ctrl+Alt become the primaries

- **Task**: T13 (board), branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
- **Report**: “Alt+点击、拖拽好像和窗口的移动重合了”.
- **Verdict**: the WM-safe primaries are implemented, locked by a real-widget regression, and documented.
  **One honest correction to the task's premise**: the application layer ALREADY received Shift+Alt /
  Ctrl+Alt (measured below, before any change) — the unreachable gestures are only the plain-Alt forms that
  the WM grabs. The fix therefore makes the matrix *explicit and tested* (the old behaviour was incidental:
  the Alt branch ignored Ctrl/Shift, so `Ctrl+Alt+click` accidentally worked and `Ctrl+click` precedence
  safety was accidental too), and the red run shows what the natural "plain Alt only" matcher does:
  Shift+Alt+drag falls through to a TEXT selection, and Ctrl+Alt+click reaches **go-to-definition**.

## 1. Gesture matrix

| gesture | before | after |
|---|---|---|
| `Shift+Alt+drag` | reached the Alt branch **incidentally** (Alt checked, Ctrl/Shift ignored) | **PRIMARY** block/column selection (explicit, tested) |
| `Ctrl+Alt+click` | reached the Alt branch **incidentally**, and only because `alt` was tested before `ctrl` | **PRIMARY** add/remove cursor (explicit, ordered before go-to-definition) |
| `Alt+drag` | block selection | **secondary** block selection (works where the WM does not grab Alt, and for direct event delivery) |
| `Alt+click` | add/remove cursor | **secondary** add/remove cursor |
| `Ctrl+click` | go to definition | unchanged (never a multi-cursor gesture) |
| plain click/drag | editor's own cursor/selection | unchanged |
| `Meta+Alt+…` | reached the Alt branch | **excluded** now (on Linux Meta+Alt+click is the WM's resize gesture; it must not be half-handled) |

## 2. The seam that receives the event

- `MultiCursorController::install(view)` uses `installEditorEventFilter(view, this)`
  (`multi_cursor.cpp`), which installs the filter on the **whole editor widget tree** — the public
  `KTextEditor::View` **and** KatePart's inner `KateViewInternal`, plus children added later
  (`QEvent::ChildAdded`). A QObject event filter runs before the receiving widget's own handler, so our
  press handler sees `Shift+Alt` / `Ctrl+Alt` before KateViewInternal's mouse handling and can consume it.
  Measured: `rm6.shift-alt-block.inner-widget` is `KateViewInternal`, and the gesture works there.
- The **app-level** filter (`EditorArea::eventFilter`, `activatePaneContaining`) also sees the press but
  only activates the pane — it never consumes the event, so it does not interfere.
- KTextEditor does **not** pre-consume `Shift+Alt`/`Ctrl+Alt` mouse events (measured). The only sequence
  it eats is the plain-`Alt` one, via the **window manager**, before the process sees it — no in-process
  seam can recover that; hence the new primaries.
- The matching is centralised in one helper now (`isMultiCursorGesture()` in `multi_cursor.cpp`), with the
  matrix documented next to it, so no future branch can shadow the primaries.

## 3. Red → green

The regression lives in the lane's existing real-widget probe
(`src/editor/kernel/multi_cursor_selftest.cpp`, ctest `vedit-editor-multi-cursor-selftest`), reusing the
`deliveryTarget()`/`pointInTarget()` seam that addresses **KatePart's inner `KateViewInternal`** — the same
seam the batch already used for the Alt path. No mock: real `QTest` mouse events at coordinates from
`View::cursorToCoordinate()`.

New checks: `rm6.shift-alt-block.*` (drag active, native block mode, 3 cursors, exact rectangle, mode off,
rectangular insert, ONE undo), `rm6.ctrl-alt-click.*` (add, position, toggle off, re-add, and — inside the
definition block with the real F12 action recorder — no definition dispatch, dispatch coordinates unchanged,
cursor added), `rm6.plain-click.single-cursor`.

**RED** (`red-run.log`): the strict pre-RM-6 matcher (`modifiers == Qt::AltModifier`, i.e. “plain Alt only”)
— the natural implementation of the old contract:

```
CHECK rm6.shift-alt-block.drag-active FAIL
CHECK rm6.shift-alt-block.native-mode FAIL
CHECK rm6.shift-alt-block.cursor-count FAIL (1)
CHECK rm6.shift-alt-block.rectangle FAIL (cursors=1)
CHECK rm6.shift-alt-block.rectangular-insert FAIL ([0]='xur', [1]='', [2]='', [3]='')
        <- without consuming the press, Shift+Alt+drag becomes a TEXT selection and typing replaces it
CHECK rm6.ctrl-alt-click.add FAIL (1)
CHECK rm6.ctrl-alt-click.readd FAIL (1)
CHECK rm6.ctrl-alt-click.no-definition-dispatch FAIL (2 -> 3)
CHECK rm6.ctrl-alt-click.dispatch-unchanged FAIL (1:2 -> 1:4)
CHECK rm6.ctrl-alt-click.adds-cursor FAIL (count=1)
SUMMARY checks=135 failures=10
```

This is the double-handling the task warned about, and the conflict-matrix failure: under a plain-Alt
matcher `Ctrl+Alt+click` falls into the Ctrl branch and runs go-to-definition instead of adding a cursor.

**GREEN** (`green-run.log`, fix applied):

```
CHECK edit.block.*                    PASS   (the Alt path keeps every guarantee)
CHECK edit.alt-click.*                PASS   (add, position, toggle-off, re-add, typing, one undo)
CHECK rm6.shift-alt-block.*           PASS   (same rectangle/count as the Alt path, one undo)
CHECK rm6.ctrl-alt-click.add/add-position/toggle-off/readd   PASS
CHECK rm6.ctrl-alt-click.no-definition-dispatch/dispatch-unchanged/adds-cursor PASS
CHECK rm6.plain-click.single-cursor   PASS
CHECK definition.ctrl-click.* (dispatched, triggered, file, line-column, document-unchanged)  PASS
CHECK definition.collapse.*           PASS   (Ctrl+click still collapses the cursor set)
SUMMARY checks=135 failures=0
```

**Honest measurement, before any code change** (recorded during development): with the ACTUAL pre-fix
code the new checks passed except one wrong-case expectation of mine — the Alt branch ignored Ctrl/Shift,
so the new combos *incidentally* worked in-process. The red above is produced by the strict matcher a
straightforward implementation of “Alt+drag / Alt+click” would use; it demonstrates the fragility the fix
removes and the destructive fall-through when our handler declines. The real-world red (the WM moving the
window) cannot be reproduced in-process: the container runs no window manager. That half is a desktop
policy, documented in the user guide.

## 4. User-manual diff (`docs/USER_GUIDE.md`, gesture rows only)

Before (§7.2):

```
| `Alt+左键` | 加一个光标；点到已有光标上则移除该光标 |
| `Alt+拖拽` | 块选择（矩形选区，每行一个光标） |
| 普通点击 / 拖拽 | 普通光标与普通选区 |
...
注意：在 KDE / GNOME 等桌面环境里，`Alt+拖拽` 常被窗口管理器先截获（表现为移动整个窗口），
这时块选手势收不到事件。这是当前版本的已知限制，见第 10 节。
```

After:

```
| `Shift+Alt+拖拽` | 块选择（矩形选区，每行一个光标）——**主手势** |
| `Ctrl+Alt+左键` | 加一个光标；点到已有光标上则移除该光标——**主手势** |
| `Alt+拖拽` / `Alt+左键` | 同样的块选择 / 加光标；窗口管理器不占用 `Alt` 时可用（次选手势） |
| 普通点击 / 拖拽 | 普通光标与普通选区 |
...
为什么主手势是 `Shift+Alt` / `Ctrl+Alt`：KDE、GNOME 等桌面默认把 `Alt+拖拽`（以及 `Alt+点击`）
当作窗口管理器的“移动窗口”手势，事件根本到不了应用；`Shift+Alt` / `Ctrl+Alt` 不会被它占用。
VS Code 在 Linux 上把列选择改成 `Shift+Alt+拖拽` 也是同一个原因。
```

§10 known-limitation item 6 was updated in the same spirit (it previously said “no alternative gesture”).

**Concurrency note:** `docs/USER_GUIDE.md` was being rewritten by another lane at the same time; the final
file is that lane's new manual with these two gesture edits applied (the file mode was restored to 644
after the write). No other section was touched by this task.

## 5. Files changed

- `src/editor/kernel/multi_cursor.cpp` — `isMultiCursorGesture()` (the documented matrix, Meta+Alt
  excluded) used by the press branch, with the Ctrl+Alt-before-Ctrl ordering stated in a comment.
- `src/editor/kernel/multi_cursor.h` — no code change needed (matrix is a .cpp helper); the header comment
  is unchanged because the existing filter contract text already applies (`installEditorEventFilter`).
- `src/editor/kernel/multi_cursor_selftest.cpp` — the RM-6 regression block (real widget seam) + the
  Ctrl+Alt vs go-to-definition conflict checks. **Scope note**: this test file is not in the row's explicit
  file list, but it IS the “existing probe that uses KateViewInternal” the row asked to reuse; no product
  behaviour lives there.
- `docs/USER_GUIDE.md` — §7.2 gesture table + note, §10 item 6.
- `evidence/ux-batch-2/rm6-gestures/` — this README + `red-run.log` / `green-run.log`.

## 6. Bounds

1. **No window manager in the test environment**, so “the WM grabs Alt+drag” cannot be exercised by a test
   here; the new primaries are chosen precisely because the WM does not own them. The user's desktop is the
   real-world check for that half.
2. `Shift+Alt+click` (no drag) behaves like the secondary `Alt+click` (adds/removes a cursor) — the task did
   not specify a click-only meaning for the block modifier; VS Code's Alt-click mult-cursor behaviour is
   preserved and documented in the matrix.
3. `Alt+↑/↓` and `Shift+Alt+↓` are keyboard gestures, not mouse; they were not changed (the WM does not
   grab them) and their existing checks stay green.
4. The full gate was not run here (the captain runs it). The targeted lane selftest was run: 135 checks,
   0 failures, plus the surrounding Alt/definition checks.

## 7. Re-run

```bash
docker run --rm -v "$PWD:/vedit-build" -e HOME=/tmp vedit-qt6-build:local bash -lc \
  'cd /vedit-build && cmake --build target/qt6-build --target vedit-editor-multi-cursor-selftest && \
   QT_QPA_PLATFORM=offscreen ./target/qt6-build/src/app/editor-lane/vedit-editor-multi-cursor-selftest'
```
