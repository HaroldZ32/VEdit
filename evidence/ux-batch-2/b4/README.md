# B4 — Ctrl+click = go to definition, Alt+click = add cursor (UX batch 2, item 13)

Contract: `.mpd/plans/ux-batch-2/00-plan.md` §B4 (and the user ruling in §0 item 3).
Lane: B4. Write scope used: `src/editor/kernel/multi_cursor.{h,cpp}`,
`src/editor/kernel/lsp_actions.{h,cpp}`, plus the lane's own test
`src/editor/kernel/multi_cursor_selftest.cpp` (extended, no new CMake target).
Nothing in `src/app/**`, `src/workspace/**`, `crates/**`, `docs/`, `translations/`
was touched.

## 1. What changed

### `src/editor/kernel/lsp_actions.{h,cpp}` — new seam `dispatchDefinitionAt`

```cpp
ActionDispatch dispatchDefinitionAt(KTextEditor::View *view,
                                    const KTextEditor::Cursor &position);   // h:228
ActionDispatch dispatchDefinitionAt(KTextEditor::View *view,
                                    const KTextEditor::Cursor &position)    // cpp:650
```

The ONE new entry point Ctrl+click uses. It contains **no capability table and no
request code**:

1. focuses the view (the press is consumed, so a plain click's focus request must
   be reproduced; the shell's request path navigates the ACTIVE pane),
2. asks the existing `LspActionChannel::availability(EditorAction::Definition)`,
3. on refusal returns the channel's own `reason` and leaves the caret untouched,
4. on success places the caret on the clicked position and calls the existing
   `LspActionChannel::dispatch(EditorAction::Definition)`, which triggers the
   shell `QAction` `action-diagnostics.definition` — the exact object F12, the
   Project menu and the editor context menu trigger.

⇒ exactly one definition request path in the tree (the diagnostics lane's
`BackendWiring::requestDefinition`, reached through the shell action); no second
LSP call site. Proof in `40-code-pointers.txt`: `grep -rn requestDefinition
src/editor/` has **no matches**, while the product's single call site remains
`src/workspace/diagnostics/diagnostics_integration.cpp:1352`.

### `src/editor/kernel/multi_cursor.cpp` — `MultiCursorController::handleMouse`

* **Ctrl+LeftButton (no Alt)** → `dispatchDefinitionAt(view_, coordinatesToCursor(pos))`.
  * dispatched: press + its release are consumed (`definitionClickActive_`), caret
    is on the clicked position, definition action triggered.
  * refused (gate off / no request path / no wired action / no file): the press is
    **not** consumed (`return false`) → the real widget performs a plain click, and
    `reportDefinitionUnavailable(reason)` shows ONE status-bar reason.
* **Alt+LeftButton** → the old Ctrl+click add/remove-cursor gesture:
  * press records `altPressAnchor_`; the block selection now starts **lazily on the
    first move to a different character** (`beginBlock` + `extendBlock`);
  * release without a drag → `addCursor(anchor)` (adds, or removes when the click
    lands exactly on an existing cursor — the pre-existing toggle rule);
  * release after a drag → `finishBlock()` (block select still yields one cursor per
    line at the left column, exactly as before).
* No other gesture changed: plain click, Ctrl+wheel/zoom, every key shortcut and
  `EditorEnhancements` are untouched.
* New private `reportDefinitionUnavailable()`: `QMainWindow` status bar message,
  8000 ms, latched once per unavailable episode (`definitionNoticeShown_`, reset by
  a successful dispatch), plus one machine line
  `DEFINITION-CLICK unavailable=1 reason='…'`.

### `src/editor/kernel/multi_cursor_selftest.cpp` — extended offscreen ctest

The pre-existing `edit.ctrl-click.*` checks asserted the OLD gesture, so they were
retargeted to `edit.alt-click.*` (same real QTest mouse events, Alt instead of Ctrl).
A new `definition.*` group was appended; ctest count is unchanged (the suite is
extended in place, no new executable).

## 2. Acceptance — observed results

Command (pinned image, offscreen), full log `20-multi-cursor-selftest.log`:

```
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen \
  vedit-qt6-build:local \
  bash -lc 'target/qt6-build/src/app/editor-lane/vedit-editor-multi-cursor-selftest'
EXIT=0
SUMMARY checks=81 failures=0
```

| # | Acceptance | Checks (all PASS) |
|---|---|---|
| ① | Alt+click adds a cursor (count increases) | `edit.alt-click.add` (1→2), `.toggle-off` (2→1), `.readd` (1→2), `.typing`, `.one-undo` |
| ② | Ctrl+click dispatches the definition action with the correct file/line/column | `definition.ctrl-click.dispatched` (channel dispatchCount==1), `.action-triggered` (QAction triggered==1), `.file` (`/tmp/b4-definition.sv`), `.line-column` (1:2), `.document-unchanged`, `.no-added-cursor` (count stays 1) |
| ③ | Gate disabled: document unchanged, plain click, exactly one status message, no request | `.gate-off.no-request` (triggered+dispatch unchanged), `.document-unchanged`, `.plain-click` (KatePart moved the caret to 1:4), `.status-reason` (message contains the backend reason), `.one-time-message` (2nd Ctrl+click after `clearMessage()` leaves it empty), `.second-plain-click`, `.still-no-request` |
| ④ | Plain click and Alt+drag unaffected | `definition.plain-click.caret` (control click through the same inner widget), `edit.block.drag-active`, `.drag-native-mode`, `.cursor-count`==3, `.left-column`, `.native-mode-off`, `.rectangular-insert`, `.one-undo` |

Channel machine line observed during ②:

```
ACTION action=definition dispatched=1 via='diagnostics.definition'
```

Gate-off machine line observed during ③ (exactly one for two clicks):

```
DEFINITION-CLICK unavailable=1 reason='verible-verilog-ls does not advertise definitionProvider'
```

## 3. Real widget vs injected seam (explicit)

**Real widget.** Every gesture assertion is a real `QTest::mousePress` /
`QTest::mouseRelease` on a real `KTextEditor::View` (offscreen). The block/Alt checks
address the public view exactly like the previous lane test; the "plain click"
fallback of ③ addresses **KatePart's inner widget** (`QApplication::focusWidget()`,
asserted non-null, != view, descendant of view: `definition.plain-click.inner-widget`)
— the widget a user's mouse actually lands on — so ③ proves the click was not
swallowed by measuring KatePart's own caret move, with a plain-click control
(`definition.plain-click.caret`) through the same path.

**Real dispatch seam.** The channel's `dispatch()` is the production object; it
triggers a `QAction` whose object name is exactly the shell's
(`action-diagnostics.definition`; production sets `"action-" + id` in
`src/app/command_registry.cpp:152`). The test asserts
`channel->shellAction(Definition) == that action` (`definition.action-is-the-f12-path`).

**Injected part.** The `QAction` itself is created by the test (the kernel target
cannot link the app shell / diagnostics lane). Its `triggered` handler is a
recorder that reads `viewFilePath(view)` and `view->cursorPosition()` at the moment
the action fires — the same two values the shell's
`BackendWiring::beginNavigation` reads (`src/workspace/diagnostics/
diagnostics_integration.cpp:1344-1352`). So the assertion is "the click reached the
F12 action with the clicked file/line/column at the handler boundary", not "a mock
of my own dispatch returned true".

## 4. What I could NOT prove

1. **The real LSP request and the resulting jump** are not exercised by this lane
   test (the editor kernel does not link the diagnostics lane / Rust ABI). The test
   proves the gesture reaches the same shell action F12 triggers with the right
   file/line/column; the request itself (`textDocument/definition`) and the jump are
   covered by the diagnostics lane's own tests, unchanged by this lane.
2. **Cross-pane Ctrl+click in the product shell.** `dispatchDefinitionAt` calls
   `view->setFocus(Qt::MouseFocusReason)` before reading availability so the shell's
   `activePane()` should switch first, but the kernel test has a single view and
   `src/app/editor_area.*` is outside this lane's write scope. Not proven end-to-end.
3. **Full-app F12 sanity after the change** — not re-driven through `vedit-app`; the
   dispatch path is shared and all 47 ctest targets pass, but no GUI click through the
   real shell window was performed.
4. **One-time message across backend reconnects** — latched per controller and reset
   by a successful dispatch; not tested across a real backend reconnect.
5. **Translation**: the new status-bar string is `tr()`-wrapped
   (`QCoreApplication::translate("VEdit", …)`), but `translations/**` is owned by A2
   and forbidden to this lane, so no zh_CN entry exists yet. The captain's lupdate
   step must include it.

## 5. Gate evidence (commands + observed results)

Full log `10-build-qt.log` (the documented one-command gate; image builds were reused
with `VEDIT_SKIP_IMAGE_BUILD=1`, the pinned `vedit-qt6-build:local` image):

```
$ VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh
[rust-ffi] build + unit tests + dependency-tree + header checks in vedit-build:local
test result: ok. … (all crates; log lines 328-584)
22/47 Test #22: vedit-editor-multi-cursor-selftest .............   Passed    0.09 sec
100% tests passed, 0 tests failed out of 47
Total Test time (real) =  60.55 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ... verilog=1 custom_path=1
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
EXIT=0   (inline `cmake --build target/qt6-build`: 333/333 targets, no warnings-as-errors failure)
```

`30-ctest-and-smoke.txt` is the extraction of the ctest/smoke lines from that log.
`40-code-pointers.txt` lists the changed symbols with current line numbers.

## 6. Evidence files

| file | content |
|---|---|
| `10-build-qt.log` | full `bash scripts/build-qt.sh` output (Rust stage + Qt configure/build + ctest + xcb smoke) |
| `20-multi-cursor-selftest.log` | full lane test output (`SUMMARY checks=81 failures=0`) |
| `21-gesture-checks.txt` | the Alt-click / definition checks (all PASS) |
| `30-ctest-and-smoke.txt` | ctest summary + `M0-SMOKE ok=1` + gate PASS lines |
| `40-code-pointers.txt` | `path:line` of the changed symbols |

Note on build contention: one `ninja` invocation initially failed with
`.ninja_deps: Permission denied` because the command passed `--user` to a rootless
Docker daemon; rerunning without `--user` (exactly what `scripts/lib-qt.sh` does for
a rootless daemon) succeeded. No source change was involved.
