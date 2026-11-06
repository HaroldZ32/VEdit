# B4 review — Ctrl+click → definition, Alt+click → add cursor

Reviewer: independent verifier (READ-ONLY for the reviewed work; no source, test or gate was edited).
Contract: `.mpd/plans/ux-batch-2/00-plan.md` §B4, with §0 item 3, §1, §2 and §5.1.
Evidence reviewed: `evidence/ux-batch-2/b4/` (README, `10-build-qt.log`, `20-multi-cursor-selftest.log`,
`21-gesture-checks.txt`, `30-ctest-and-smoke.txt`, `40-code-pointers.txt`).
New evidence produced by this review: `00-coordinate-space.txt`, `01-focus-delivery.txt`,
`02-multicursor-state.txt` and the two probe sources in this directory.

## Assessment

**Risk level: HIGH. Verdict: FAIL.**

The seam design is genuinely single: `dispatchDefinitionAt` has no capability table and no request code — it
asks the existing `LspActionChannel::availability(Definition)` and then calls the channel's own
`dispatch(Definition)`, which triggers the shell `QAction` `action-diagnostics.definition`; `grep` finds no
`requestDefinition` in `src/editor/`, and the context menu, F12 and this new Ctrl+click all end in the same
`BackendWiring::beginNavigation`. The refusal path is also real: the press is not consumed, KatePart's inner
widget performs the plain click (the lane test measures the caret through that widget), one status-bar reason
is shown, and no request is made. **But the green evidence is green in the wrong coordinate space.** The
controller installs its filter on the view *and every descendant*, and a real mouse press is delivered to
KatePart's `KateViewInternal`; `handleMouse` passes that inner-widget-local `QMouseEvent::position()` straight
to `KTextEditor::View::coordinatesToCursor`, which expects view-local coordinates. The inner widget sits at
`(36, 1)` inside the view (measured in the pinned image), so in the product a Ctrl+click asks for a definition
several columns to the left — sometimes on the previous line — and Alt+click / Alt+drag are offset by the same
amount. The lane test misses this because every gesture check sends `QTest::mousePress`/`mouseMove` to the
`View` object itself; only the plain-click control uses the inner widget. Independently, Ctrl+click over an
existing multi-cursor set keeps the stale cursors alive: `setCursorPosition` moves the primary and preserves
the rest, while the consumed press suppresses the native click that would collapse them; the test only starts
from one cursor. The one-request-path claim survives review; the delivered gesture does not.

## Findings (ranked)

### Blocker — R1: every new click gesture is computed in the wrong coordinate space
- **What:** `MultiCursorController::handleMouse` computes the clicked position with
  `view_->coordinatesToCursor(event->position().toPoint())` for press, move and release. The filter is
  installed by `installEditorEventFilter`/`installOnWidgetTree` on the view and all its child widgets, and a
  real click is delivered to `KateViewInternal`, not the `KTextEditor::View`. `QMouseEvent::position()` is then
  in the receiving child's coordinates; `View::coordinatesToCursor` expects view coordinates.
- **Where:** `src/editor/kernel/multi_cursor.cpp` (`MultiCursorController::handleMouse`,
  `installEditorEventFilter`), consumed by `src/editor/kernel/lsp_actions.cpp` (`dispatchDefinitionAt`).
- **Why it matters:** the frozen acceptance ② is “Ctrl+click dispatches the definition request with the correct
  file/line/column”. In the product it dispatches the wrong line/column; Alt+click adds the cursor at the wrong
  location and Alt+drag anchors the block at the wrong rectangle. This is the gesture a user actually performs.
- **Evidence:** `00-coordinate-space.txt` (probe: `KateViewInternal` at `(36,1)` inside the view; the physical
  point for `1:2` is view `(50,15)` / inner `(14,14)`; `coordinatesToCursor(viewPoint)=1:2` but
  `coordinatesToCursor(innerPoint)=0:0`; a real Ctrl+click delivered to the inner widget triggered the action
  with caret `0:0`, while KatePart's own plain click at the same point landed `1:2`; the same click sent to the
  `View` — what the lane test does — dispatched `1:2`).
- **Direction of fix (not applied):** pass the watched widget into `handleMouse` and map the event point to the
  view before calling `coordinatesToCursor`; change the gesture checks to drive the inner widget the way the
  plain-click control already does, so the test would fail today.
- **Note:** the controller may predate this lane, but the new Ctrl+click (and the Alt+click/Alt+drag checks the
  lane claims) are wrong because of it, and both files are in the lane's write scope.

### High — R2: a successful Ctrl+click does not collapse an existing multi-cursor set
- **What:** on success `dispatchDefinitionAt` calls `view->setCursorPosition(position)`. KTextEditor keeps the
  other `cursorPositions()` alive (measured), and `handleMouse` has already consumed the press, so the native
  click that normally collapses a multi-cursor set never runs.
- **Where:** `src/editor/kernel/lsp_actions.cpp` (`dispatchDefinitionAt`), `src/editor/kernel/multi_cursor.cpp`
  (`handleMouse` Ctrl branch).
- **Why it matters:** a user with three cursors who Ctrl+clicks (a click, i.e. the gesture that used to be a
  plain click at that spot) is left in multi-edit mode with the stale cursors after a same-file jump or after
  “No definition found”; the next typing edits all of them. The test's `.no-added-cursor` starts from one
  cursor, so it cannot see this.
- **Evidence:** `02-multicursor-state.txt` (plain click through the inner widget: 3 cursors → 1 at `2:2`;
  Ctrl+click through the inner widget: still 3 cursors, primary moved to `1:0` under R1 instead of `2:2`).
  `00-coordinate-space.txt` also shows `cursorCount=3` after a Ctrl+click.
- **Direction of fix (not applied):** install the clicked position as a single-position cursor set (the lane's
  own `setViewCursors`) before dispatch; add a check that starts from three cursors.

### Medium — R3: the injected seam does not cover the product wiring it is named after
- **What:** the test creates the `QAction` `action-diagnostics.definition` itself and its recorder reads
  `viewFilePath(definitionView)` (the property the test set) and `definitionView->cursorPosition()`.
  Production `BackendWiring::beginNavigation` reads `activePane()->currentFilePath()` and `activeView()`.
  `definition.action-is-the-f12-path` only proves that `shellAction(Definition)` finds the action the test just
  named; it does not prove F12's shortcut, its handler, or the pane source. The gate-off scenario turns off
  **both** the published gate row and the QAction, so a regression in either gate alone still passes.
  `dispatchDefinitionAt` calls `view->setFocus()` on the claim that this activates the shell's active pane;
  `EditorArea::activePane()` returns its own `active_` (set through `EditorPane::onFocusRequested_`), and
  `EditorPane::eventFilter` is installed on the view, which receives no FocusIn/MouseButtonPress from
  KatePart's inner widget (measured: zero events in a two-view focus/click probe). A Ctrl+click in a non-active
  split pane therefore dispatches the previously active pane's file/caret, not the clicked one; the lane README
  admits cross-pane is not proven but not that the activation mechanism is inert.
- **Where:** `src/editor/kernel/multi_cursor_selftest.cpp` (`definition.*` block),
  `src/workspace/diagnostics/diagnostics_integration.cpp` (`BackendWiring::beginNavigation`),
  `src/app/editor_area.cpp` (`EditorArea::activePane`, `EditorPane::eventFilter`),
  `src/editor/kernel/lsp_actions.cpp` (`dispatchDefinitionAt`).
- **Why it matters:** this is exactly the “looks green, wiring wrong” gap the review was asked to find: a
  handler reading a different position/pane, a gate wired differently, or a non-active-pane click would not be
  caught. The `file` check is test-owned and vacuous; only the line/column check exercises the code under test,
  and only in view space.
- **Evidence:** `01-focus-delivery.txt`; `40-code-pointers.txt` itself records the production read site
  (`diagnostics_integration.cpp`, `beginNavigation`), which differs from the recorder.
- **Direction of fix (not applied):** cover this in a product-seam/two-pane test (the diagnostics product seam
  already exists as a ctest target), activate the pane explicitly through `EditorArea` rather than relying on
  `setFocus`, and test the gate row alone and the disabled action alone as separate cases.

### Nit — R4: the one-time latch re-arms only on a successful Ctrl+click
- The latch is reset only when `dispatchDefinitionAt` returns `dispatched`. A recovery proven by F12 (or by a
  changed backend state that never gets a successful Ctrl+click) does not re-arm it, and a changed refusal
  reason while latched is never surfaced. No permanent silence was found while the backend stays available and
  the user keeps Ctrl+clicking (the next success resets it), and the contract only says “one-time”, so this is
  a Nit; no test covers the re-arm.

## Answers to the five review questions

1. **One definition request implementation?** Yes. `dispatchDefinitionAt` contains no request code and no
   capability table; it calls the existing channel. There is one request implementation
   (`diagnostics_integration.cpp` `requestDefinition`/`beginNavigation`), one shell `QAction`, and one
   capability source (`state_.gate`) used by both `publishGates` and `applyGate`. No second LSP call site was
   found in `src/editor/` or `src/app/`.
2. **Gesture correctness?** Document: unchanged in the test (pass). Caret: moved to the clicked position in
   view space (pass), but in the product it is moved to the wrong position (R1). Refusal: behaves as a plain
   click through KatePart's inner widget and shows one reason (verified). Alt+drag: still block-selects in the
   test; in the product it is offset (R1). Alt+click: add/toggle-off/re-add/typing/one-undo pass; keep-last is
   implemented in `addCursor`; a pre-existing multi-cursor set is not collapsed by Ctrl+click (R2).
3. **One-time latch?** It re-arms on a successful Ctrl+click and cannot go permanently silent while the user
   keeps making successful Ctrl+clicks; a recovery via F12 does not re-arm it and a changed reason is not
   re-surfaced (Nit R4). No test for re-arm.
4. **Meaning of the injected seam?** It meaningfully proves “the click reaches the channel dispatch and the
   caret was set before the action fires” in view space. It does **not** prove: the shell's real F12
   shortcut/handler, the `activePane()`/`currentFilePath()` source, pane activation, the product `applyGate`
   wiring, or the real delivered coordinate space. The `file` assertion is supplied by the test itself.
5. **Anything weakened?** ctest is 47/47 (the plan's floor is 38, “only increase”), with the same target
   extended in place; the zoom/wrap/shortcut/editor-enhancement checks are still present and pass. The old
   `edit.ctrl-click.*` checks were retargeted to `edit.alt-click.*`; the current suite still covers add,
   toggle-off, re-add, typing, one-undo, block drag/rectangular insert and the plain-click control. The
   selftest file is untracked, so I could not diff against a pre-B4 baseline to prove no check was deleted.

## Declined to judge (with reason)

- **Single-request-path claim** — checked and confirmed; not a finding.
- **F12 end-to-end through the real app** — not driven; the shell is unchanged by the lane and other lanes are
  mid-flight; the code path is shared and reviewed statically.
- **Refusal fallback** — verified through the inner widget by the lane test and by reading `handleMouse`; no
  defect found.
- **Alt+click “keep-last” semantics** — `addCursor` re-adds the sole position when the set becomes empty; the
  same rule as the old gesture; not separately asserted (no finding).
- **Block-selection lazy start** — coherent (press consumed, block begins on the first different character);
  test passes; in the product it is shifted by R1.
- **ctest count** — 47/47, unchanged and above the plan's floor; no target was removed.
- **Zoom / wrap / shortcut / EditorEnhancements** — checks present and passing; the lane did not touch them.
- **Old selftest baseline** — the file has no git history (untracked), so “retargeted, not relaxed” cannot be
  diff-proven; the current suite's coverage is recorded above.
- **Translation of the new status string** — `tr()`-wrapped but the zh_CN entry is the captain's `lupdate`
  step (README §4.5); outside the lane.
- **Ctrl+Alt+click precedence, Ctrl+click on an invalid/gutter position, selection clearing on success** —
  contract-silent and not material to the frozen acceptance; not judged.

## Bounds of this review

Read: the frozen contract (§B4, §0 item 3, §1, §2, §5.1); all `evidence/ux-batch-2/b4/` files; the reviewed
sources (`lsp_actions.*`, `multi_cursor.*`, `multi_cursor_selftest.cpp`); the production dispatch and gate
wiring (`diagnostics_integration.cpp`, `command_registry.cpp`, `editor_context_menu.cpp`) and the editor-area
pane/focus code enough to trace `activePane()`. Not read: the whole app shell, the Rust diagnostics/LSP lane,
other lanes' diffs, translations.
Ran: the lane selftest from the current tree (81 checks, 0 failures) in `vedit-qt6-build:local` (offscreen);
three independent read-only probes compiled against the same pinned image and `libvedit_editor_core.a`
(`00-coordinate-space.txt`, `01-focus-delivery.txt`, `02-multicursor-state.txt`; sources in this directory).
Did not run: the full 47-test ctest or the xcb smoke again (the gate log and the extracted
`30-ctest-and-smoke.txt` were consistent: 47/47, `M0-SMOKE ok=1`); did not drive `vedit-app` with real X11
mouse clicks. Evidence timestamps: the lane's build log (06:33) is after the reviewed sources (06:30), so the
recorded run corresponds to the tree I read; the selftest was rebuilt later by other lanes and I re-ran it
green.

## Verdict

**FAIL** — the lane's green evidence is produced by sending Ctrl/Alt clicks to the `View` while the product
delivers them to KatePart's offset inner widget, so the real Ctrl+click asks for the wrong definition and
Alt+click/Alt+drag land at the wrong place; Ctrl+click also leaves a pre-existing multi-cursor set alive.
