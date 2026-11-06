# RM-6 final independent review — WM-safe gesture remap

- Task: T14 (independent verification of T13 / RM-6), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Basis: the RM-6 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` (the user's binding
  ruling: `Shift+Alt+drag` = block select and `Ctrl+Alt+click` = add/remove cursor are the
  primaries; the Alt forms stay secondary; `Ctrl+click` = definition must not be disturbed; a
  plain click/drag stays plain; one press must not be handled by two paths; one undo reverts the
  block; the manual's table states the new primaries and why); the writer's evidence
  `evidence/ux-batch-2/rm6-gestures/` (README sha256 `2e54998f…`, `red-run.log` 10 failures,
  `green-run.log` 135/0); the code (`multi_cursor.cpp`, `multi_cursor_selftest.cpp`,
  `docs/USER_GUIDE.md` §7.2).
- I re-ran the real-widget multi-cursor selftest in the pinned image:
  **`vedit-editor-multi-cursor-selftest` → SUMMARY checks=135 failures=0** (0.37 s). The red is
  the writer's strict-matcher run (10 failures); it is a HEAD-style matcher build, read, not
  re-run (reverting the product is not the reviewer's job).

---

## 1. The shipped single predicate (the Lead's target 1)

`isMultiCursorGesture()` in `src/editor/kernel/multi_cursor.cpp`:

```cpp
if (!modifiers.testFlag(Qt::AltModifier)) return false;
const Qt::KeyboardModifiers companions =
    modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
return companions == Qt::NoModifier || companions == Qt::ShiftModifier
       || companions == Qt::ControlModifier
       || companions == (Qt::ShiftModifier | Qt::ControlModifier);
```

The modifier matrix, as shipped:

| modifiers | predicate | handler | result | regression |
|---|---|---|---|---|
| none | false | editor | plain click/drag | `rm6.plain-click.single-cursor` |
| Shift | false | editor | plain selection | (editor's own; not a multi-cursor path) |
| Ctrl | false | definition | jump | `definition.ctrl-click.*`, `definition.collapse.*` |
| Alt | true | click add / drag block | secondary (WM permitting) | `edit.alt-click.*`, `edit.block.*` |
| Shift+Alt | true | click add / drag block | **primary block** | `rm6.shift-alt-block.*` |
| Ctrl+Alt | true | click add / drag block | **primary add/remove** | `rm6.ctrl-alt-click.*` |
| Shift+Ctrl+Alt | true | click add / drag block | works | manual; not separately tested (M2) |
| Meta+Alt (+Shift/Ctrl) | **false** | not handled (editor/WM) | excluded, no half-handling | manual; not separately tested (M2) |
| Meta/Shift/Ctrl without Alt | false | editor/definition | plain | `definition.ctrl-click.*` |

The handler (`multi_cursor.cpp`, the `MouseButtonPress` branch) calls
`isMultiCursorGesture(event->modifiers())` **before** the Ctrl branch, so `Ctrl+Alt` is never
mistaken for go-to-definition, and returns true (consuming the press) for every predicate-true
combination. The predicate is deliberately **not** `modifiers == Qt::AltModifier`: the strict
matcher is exactly what the writer's red run shows double-handling (`Shift+Alt+drag` falls through
to a text selection, `Ctrl+Alt+click` reaches go-to-definition; `red-run.log` 10 failures). The
shipped predicate consumes those combinations itself, so they cannot also reach the text-selection
or definition path — **verified for the two primaries by `rm6.shift-alt-block.*` and the
`rm6.ctrl-alt-click.no-definition-dispatch` block; the remaining predicate-true combinations are
the same branch and the same consumption, and the manual states the same rule (M2: not each
tested).**

The **no-second-handler** check: the controller's event filter is installed on the whole editor
widget tree (public `View` + KatePart's inner `KateViewInternal` + `ChildAdded`), and it returns
true for the consumed press; the app-level `EditorArea::eventFilter` only activates the pane and
never consumes (inspected). The red run's double-handling comes from a matcher that declines the
combination, not from a second handler.

## 2. Ctrl+click inside a multi-cursor territory (the Lead's target 2)

The regression sets **three cursors** and then Ctrl+clicks (no Alt) at a point:
`definition.collapse.ctrl-setup` / `.ctrl-click` (the set collapses to one), `.ctrl-click-position`
(the caret and the dispatched coordinates are the clicked point), `.ctrl-click-dispatched` (the
definition action fired once). The plain-click control (`definition.collapse.plain-click`) shows
the editor's own collapse from the same three-cursor set. The `Ctrl+Alt+click` **inside the
definition block** is separately asserted not to dispatch (`rm6.ctrl-alt-click.no-definition-dispatch`,
`.dispatch-unchanged`) and to add a cursor (`.adds-cursor`). So the two gestures do not steal from
each other in either direction: Ctrl+click from a multi-cursor territory still jumps and
collapses; Ctrl+Alt+click inside the definition block adds a cursor and never jumps. The plain
click stays plain (`rm6.plain-click.single-cursor`).

## 3. The manual/regression describe the NEW matrix (the Lead's target 3)

- `docs/USER_GUIDE.md` §7.2 now lists `Shift+Alt+拖拽` and `Ctrl+Alt+点击` as **主手势**, the
  Alt forms as **次选手势**, the plain click/drag as plain, the matching rule (“带这些修饰键的
  点击 = 加/减光标，拖拽 = 块选”; `Shift+Alt+点击` / `Ctrl+Alt+拖拽` also work), the
  `Meta+Alt` exception (“Linux 上它是窗口管理器的缩放手势，程序刻意不处理”), the
  `Ctrl+点击` definition line (`Ctrl+Alt+点击` is multi-cursor first, never the jump), the
  one-undo line, and the why (“KDE/GNOME 把 Alt+拖拽/Alt+点击 当移动窗口，事件到不了应用;
  Shift+Alt/Ctrl+Alt 不会被占用; VS Code 同理”).
- The regression's comments say the same (the WM grabs plain Alt; the new primaries are the
  testable, WM-safe forms), and the `rm6.*` checks are named after the new matrix.
- The **old accident** — “the inner layer already received Shift+Alt/Ctrl+Alt by accident, only
  pure Alt was eaten by the WM” — is a statement about the pre-fix code; it appears in the
  writer's README §3 as an honest measurement note, **not** in the manual's gesture table and not
  as the regression's contract. The manual/table text and the regression describe the new,
  explicit matrix.

---

## 4. Acceptance

| Item | Result |
|---|---|
| `Shift+Alt+drag` = rectangular block, one cursor per column | **PASS.** `rm6.shift-alt-block.drag-active/native-mode/cursor-count=3/rectangle/rectangular-insert`; the real inner `KateViewInternal` target (`inner-widget`). |
| `Ctrl+Alt+click` = add/remove cursor | **PASS.** `rm6.ctrl-alt-click.add/added-position/toggle-off/readd`; the same inner-widget seam. |
| Rectangle insert + one undo reverts the block | **PASS.** `rm6.shift-alt-block.rectangular-insert`, `.one-undo`; the Alt path's `edit.block.*` guarantees are kept. |
| `Ctrl+click` = definition, never disturbed | **PASS.** `definition.ctrl-click.dispatched/action-triggered/file/line-column/document-unchanged/no-added-cursor`; the 3-cursor collapse case above. |
| Plain click/drag untouched | **PASS.** `rm6.plain-click.single-cursor`; `definition.collapse.plain-click`. |
| One press not handled by two paths | **PASS by construction + tests.** The single predicate consumed before the Ctrl branch; the filter returns true; the app filter does not consume; the strict-matcher red shows what declining does (the double-handling the task warned about), the shipped one does not. |
| Manual table = new primaries + the reason | **PASS.** The §7.2 rows above; the Alt fallbacks documented. |
| Red-before / green | **PASS with the provenance caveat (L1).** The red (10 failures) is a strict “plain Alt only” matcher — the natural implementation of the old contract — and demonstrates the fall-through; the writer **honestly measures** that the actual pre-fix code (Alt checked, Ctrl/Shift ignored) incidentally made the new combos work in-process. The real-world red (the WM eating plain Alt) cannot be reproduced in-process (no WM in the container). The fix's value is making the matrix explicit/tested and WM-safe, not fixing an in-process failure. |

My rerun: `vedit-editor-multi-cursor-selftest` **135/0**; the writer's `green-run.log` 135/0; the
red 135/10 (the strict matcher).

---

## 5. Findings (ranked)

### M1 — MEDIUM — the WM half is a desktop policy that no in-process test can prove

**What.** The defect the user hit (plain `Alt+drag`/`Alt+click` captured by KWin/GNOME before the
application sees them) is a window-manager behaviour; the container runs no WM, so the regression
proves the new primaries reach the widget and are handled, but not that a real KDE/GNOME session
delivers `Shift+Alt`/`Ctrl+Alt` to the app (it does in the user's report and in VS Code's
precedent) nor that a remapped WM does not take plain `Alt` (the secondary forms). The writer
states this.
**Where.** `src/editor/kernel/multi_cursor.cpp` (`isMultiCursorGesture`, the `install`/event
filter), `multi_cursor_selftest.cpp`; README §3/§5; `docs/USER_GUIDE.md` §7.2/§10.
**Why it matters.** This whole class of bug is WM-specific; a real KDE/GNOME session is the only
proof of the user-visible acceptance. A future WM change (or a different compositor/DE) can
re-break plain Alt without any test failing.
**Direction.** Keep the WM-safe primaries and the documented reason; a real-machine run on both
desktops is the final check; if a DE reserves one of the new primaries, the same matrix can move
again.

### M2 — LOW — the regression covers the two primaries, not every predicate-true combination

**What.** The selftest covers `Shift+Alt+drag`, `Ctrl+Alt+click`, the Alt forms, plain click and
Ctrl+click/definition (including the collapse and the Ctrl+Alt-inside-definition). It does not
individually exercise `Shift+Alt+click`, `Ctrl+Alt+drag`, `Shift+Ctrl+Alt(+click/drag)` or
`Meta+Alt(+drag)`; the predicate and the manual state their behaviour (click = add, drag = block;
`Shift+Ctrl+Alt` works; `Meta+Alt` is deliberately not handled).
**Where.** `multi_cursor_selftest.cpp`; the predicate; `docs/USER_GUIDE.md` §7.2.
**Why it matters.** The predicate is explicit, so the risk is low; a matrix-driven test would lock
the shipped decision against a future edit of the companions mask.
**Direction.** Add table-driven checks for the uncovered combinations (the predicate and the
event-handling are already centralised; the test would be small).

### L1 — LOW — the red is a strict-matcher model; the actual pre-fix code worked incidentally

**What.** The writer's red run reverts the matcher to `modifiers == Qt::AltModifier` — the natural
implementation of the old contract — and shows the fall-through (text selection, definition jump;
10 failures). The writer also honestly measured that the **actual** pre-fix code (Alt checked,
Ctrl/Shift ignored) made the new combinations work in-process; the in-process red for the shipped
matrix therefore does not exist, and the real defect (the WM) is untestable here. The task's #5
(“the new combos did nothing before”) is contradicted by that measurement.
**Where.** `evidence/ux-batch-2/rm6-gestures/README.md` §3; `red-run.log`.
**Why it matters.** The fix's in-process value is making the matrix explicit and tested; the
user-visible value is the WM-safe primaries, proven only by the desktop. The evidence is honest;
the residual risk is the real desktop (M1).
**Direction.** State the red's scope in the acceptance matrix (strict-matcher model, not the
actual pre-fix behaviour); the real desktop is the acceptance for the WM half.

### L2 — LOW — the manual is the RM-8 lane's file, currently in flight

**What.** The RM-6 gesture rows in `docs/USER_GUIDE.md` are new and match the shipped matrix; the
RM-8/manual lane is rewriting that document (T19 is blocked on that writer). The current §7.2 is
the RM-6's new text; a later RM-8 rewrite must keep the primaries/secondaries/matrix and the WM
reason.
**Where.** `docs/USER_GUIDE.md` §7.2; the RM-8 card; T19.
**Why it matters.** A manual rewrite can silently revert the gesture table to the old Alt-first
rows; T19 must check the final text.
**Direction.** T19 (when ready) re-checks §7.2 against the shipped matrix; the RM-6 text is the
reference.

### N1 — NIT — the app-level filter only activates the pane

**What.** `EditorArea::eventFilter` sees the press but only activates the containing pane; it
never consumes the event, so the multi-cursor filter (and the editor) still receive it.
**Where.** `src/app/editor_area.cpp`; README §2.
**Why it matters.** It is the reason the app-level lane does not double-handle; verified by
inspection.

### N2 — NIT — the `Meta+Alt` case is documented but not tested

**What.** The manual states “`Meta+Alt` is deliberately not handled”; the predicate excludes it;
the selftest has no `Meta+Alt` case (a real desktop would send it to the WM's resize gesture).
**Where.** the predicate; `docs/USER_GUIDE.md` §7.2; M2.

---

## 6. Declined to judge (reason on the same line)

- **A real KDE/GNOME session with a real WM** — not available offscreen; the WM half is M1; the user runs both desktops and this class is WM-specific.
- **The plain-Alt secondary on a WM that grabs it** — by design it is dead where the WM grabs Alt; documented as such; not a defect.
- **The `Meta+Alt` resize gesture** — deliberately not handled; documented (N2).
- **The `Shift+Alt+click`/`Ctrl+Alt+drag`/`Shift+Ctrl+Alt` combinations** — the predicate/manual state them; not individually tested (M2).
- **The strict-matcher red vs the actual pre-fix incidental behaviour** — the writer's own measurement; the real red is the WM (L1).
- **The manual rewrite (RM-8/T19)** — the manual lane's file; the current rows are the RM-6's new matrix; T19 will re-check (L2).
- **The full workspace gate** — the captain runs it; I re-ran the multi-cursor selftest.
- **The editor's own Shift-selection / context-menu / clipboard paths** — unchanged by this task; the plain click/selection controls pass.

---

## 7. Bounds of this review (what a real KDE/GNOME session would add)

- The selftest is **offscreen** in the pinned Qt6 image, driving **real `QTest` mouse events** at
  `View::cursorToCoordinate()` points on the real `KateViewInternal` inner widget; it is not a
  mock and not a synthetic event.
- The **window-manager half** (plain Alt grabbed before the process; `Meta+Alt` resize) cannot be
  reproduced without a real WM. A real KDE/GNOME session would confirm: the plain Alt secondaries
  are dead, `Shift+Alt`/`Ctrl+Alt` reach the app, the new primaries work with a real pointer, and
  no DE-level shortcut steals `Ctrl+Alt+click`.
- The **red** is the writer's strict-matcher build; I read it and did not revert the product; the
  actual pre-fix code's incidental behaviour is the writer's measured note (L1).
- The **manual** is currently the RM-8 lane's in-flight document; the RM-6 rows are the new
  matrix; T19 will own the final text (L2).
- I did not edit any product file; the only writes are this report and `01-ctest-rerun.log` /
  `05-provenance.txt` under `evidence/ux-batch-2/review-rm6-final/`.

---

## 8. Verdict

**PASS — risk MEDIUM.**

The shipped implementation is the explicit matrix the user asked for: one
`isMultiCursorGesture()` decision point (Alt + Shift/Ctrl/Shift+Ctrl companions; `Meta+Alt`
excluded; no-Alt false), checked before the Ctrl/go-to-definition branch and consumed by the
single editor-tree filter, so the strict-matcher double-handling the writer's red demonstrates
(`Shift+Alt+drag` → text selection, `Ctrl+Alt+click` → definition jump) cannot happen; the
`Shift+Alt` block and `Ctrl+Alt` add/remove work through the real `KateViewInternal` seam, the
rectangular insert and one-undo guarantees are kept, `Ctrl+click` still jumps and collapses a
multi-cursor set, and a plain click stays plain. My rerun of `vedit-editor-multi-cursor-selftest`
is 135/0; the writer's green is 135/0 and the strict-matcher red is 135/10. The manual and the
regression describe the new matrix; the old “it already worked by accident” statement lives only
in the writer's provenance note. The Medium risk is the WM half: no offscreen test can prove the
real KDE/GNOME delivery or the plain-Alt secondaries — that is the user's real-machine test — plus
the untested predicate-true combinations (M2) and the RM-8 lane's in-flight manual (L2).
