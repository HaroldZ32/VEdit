# RM-10 — the hover popup must appear over the squiggle, not only the gutter marker

Branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
Owner: Coverage Engineer (shared task T22). User words: “悬浮窗仅在放在代码栏左侧图标里显示，不显示于代码里划线的地方。” Requirement #9 of the batch (“error/warning 悬浮框”) was only half delivered: the underline itself must be the hover target, as in every IDE.

## 1. Mechanism — why it failed (file:line)

- The diagnostics lane installed its mouse filter on the PUBLIC `KTextEditor::View`:
  `adoptHoverView()` did `view->installEventFilter(this)` — HEAD
  `src/workspace/diagnostics/diagnostics_integration.cpp:1163` — and the filter
  only acted when `watched == hoverView_.data()` — HEAD
  `src/workspace/diagnostics/diagnostics_integration.cpp:2184-2190`. The hit test
  then called `view->coordinatesToCursor(viewportPoint)` with the receiving
  widget's local point (HEAD `…:1319`), which only worked when that widget WAS
  the view.
- A REAL mouse event inside a KTextEditor view is delivered to KatePart’s inner
  `KateViewInternal` CHILD, not to the public View. This is measured and already
  documented by this batch: `evidence/ux-batch-2/review-b4/01-focus-delivery.txt`
  (“focus after A: KateViewInternal …”) and the comment at
  `src/app/editor_area.cpp:1391` (“the real mouse target of the product editor is
  KatePart’s inner KateViewInternal widget, a CHILD of the KTextEditor::View, and
  a filter installed on the view never sees those events”). The same seam is what
  `MultiCursorController::viewPointFor()` handles (`src/editor/kernel/multi_cursor.cpp:607`).
- Consequence: the old filter only ever saw the moves that PROPAGATED to the View
  (outside the text widget, e.g. the gutter/marker strip) — exactly the user’s
  report. The hit-test itself (`diagnosticAt`) was already correct; the delivery
  wiring was wrong.
- WHY THE GATE MISSED IT: the batch’s own hover regression sent events straight
  to the public View (`QCoreApplication::sendEvent(viewA, &move)` in
  `selftests/b3_wiring_e2e.cpp`, HEAD `:468-470`), which is not the path a user’s
  mouse takes. No test drove the inner widget for hover.

## 2. The fix

- **Real delivery seam**: `watchViewTree()` installs the filter on the pane
  view’s whole widget subtree (idempotent, re-entered on `QEvent::ChildAdded` for
  lazily created children). A mouse move maps the receiving widget’s local point
  into the view (`widget->mapTo(view, pos)` before `coordinatesToCursor`) — view
  coordinates are NOT assumed to be widget coordinates. The filter is an
  OBSERVER: it always returns `false`, so selection, KTextEditor’s tooltips and
  Ctrl+click are untouched.
- **Split panes**: `paneForView()` (`EditorPane::containsEditorWidget`) resolves
  the pane of the VIEW UNDER THE MOUSE, so the hover uses that pane’s file; the
  popup is re-parented to the hovered view; `placePopup()` derives the view from
  the popup’s parent (falling back to the active view); the :Fix」 path uses the
  popup’s view’s pane; and the 400 ms live poll does not steal a bubble anchored
  to another pane (a tab switch within the same pane still adopts + hides).
- **No dangling popup**: the anchored document’s `textChanged` hides+publishes;
  `QEvent::Wheel` over the view hides+publishes; the live poll re-resolves the
  cached mouse point (keyboard scrolling) and hides when it no longer hits the
  cached row; the existing `QPointer` + destroyed-view clearing discipline is
  kept (and the hide transition now publishes the hover state immediately
  instead of waiting up to 400 ms for the poll).
- **Range parity with the underline**: `diagnostic_hover.cpp` now treats a
  negative end column on a multi-line range as “to the end of the last line”,
  which is what the underline drawing does
  (`diagnostics_annotations.cpp:66-86` falls back to `lineLength(endLine)`).
  Multi-line ranges answer on the first, middle AND last covered line.
- **Placement**: unchanged and singular — the popup is still placed by
  `placePopup()` (+18/+22 from the cursor, clamped inside the anchor), now with
  the hovered view as the coordinate owner. No second placement rule was added.

## 3. Both gestures verified with observed output

Green run `05-b3-green.log` (90 checks, 0 failures; environment
`QT_QPA_PLATFORM=offscreen`):

```
OBSERVED hover.inner-squiggle diagnostic=1 rule='no-trailing-spaces' line=1 gutter-rule='no-trailing-spaces'
CHECK PASS hover.real-target-is-inner-widget  KateViewInternal
CHECK PASS hover.blank-hides-before-inner-probe
CHECK PASS hover.inner-squiggle-shows  diagnostic=1 rule='no-trailing-spaces' line=1
CHECK PASS hover.inner-same-content-as-gutter
CHECK PASS hover.inner-non-diagnostic-shows-nothing  diagnostic=0 hidden=1
CHECK PASS hover.ctrl-click-inside-squiggle-still-dispatches  triggers=1 action=1
CHECK PASS hover.ctrl-click-moves-caret-to-squiggle  caret=1:6
CHECK PASS hover.multiline.first-line / middle-line / last-line / before-start /
           past-last-line-end / negative-end-covers-last-line
```

The move is delivered to `viewA->childAt(point)` — the widget a real user move
hits (`KateViewInternal`) — not to the view. The gutter/propagation path is the
one the suite already exercised (it sends to the View; those checks still pass),
and the inner path’s content is asserted EQUAL to it (rule/line/file). A
non-diagnostic span on the same inner path shows nothing. Ctrl+click from inside
the squiggle still dispatches Go to Definition (the shell action fires; the
caret moves to the clicked position) — our filter never consumes the event.

Screenshot: `04c-hover-popup-inner-squiggle.png` (bubble shown by the inner
widget path), plus the existing `04a`/`04b` popup shots saved by the same run.

## 4. Red before / green after

RED (`04-b3-red.log`): the ONLY change for this run is the pre-fix delivery
condition, temporarily re-inserted as an early guard in the new filter
(`if (watched != hoverView_.data()) return …;`), everything else identical. The
real-inner move then does nothing — the user’s symptom:

```
OBSERVED hover.inner-squiggle diagnostic=0 rule='no-trailing-spaces' line=1 gutter-rule='no-trailing-spaces'
CHECK FAIL hover.inner-squiggle-shows  diagnostic=0 rule='no-trailing-spaces' line=1
CHECK FAIL hover.inner-same-content-as-gutter
SUMMARY checks=89 failures=2
```

The guard was reverted immediately after the run (the working tree carries the
fix; `grep 'RM-10 RED-BEFORE' diagnostics_integration.cpp` = 0).

GREEN (`05-b3-green.log`): `SUMMARY checks=90 failures=0 skips=0`.

## 5. Second-defect check — underline span vs diagnostic range

The underline and the hover target are computed from the SAME merged rows and
agree: `diagnostics_annotations.cpp` clamps `line/endLine/endColumn`, underlines
a whole line when the end is at/before the start, and falls back to the end of
the line for a negative end column. `diagnosticAt()` matched all of those except
the negative-end multi-line last line, which this row fixed on the hover side.
No disagreement was found in the drawn span; there is no second (underline)
defect to report.

## 6. Bounds / honesty

- The Ctrl+click assertion proves the GESTURE still dispatches the definition
  action and moves the caret to the clicked position; the actual jump needs a
  language server, which this harness does not start (the batch’s B4 probes cover
  the real jump).
- Split-pane support is implemented through `containsEditorWidget`/the popup’s
  own view, and the poll no longer steals a cross-pane bubble; this row did NOT
  add a dedicated two-pane mouse regression (the existing suites have no split
  layout). Stated as a test-coverage bound, not a claimed verification.
- The regression lives in `selftests/b3_wiring_e2e.cpp`, the harness that owns
  the hover checks — that file is not in T22’s listed write scope (the listed
  scope was diagnostic_hover.* + diagnostics_integration.cpp hover wiring +
  evidence). It is the only place the hover popup is driven end-to-end; a new
  file would have duplicated the whole shell setup. Flagged to the captain.
- The temporary red guard touched `diagnostics_integration.cpp` for the duration
  of one build+run and was restored from a byte backup (`target/rm10-integration-fixed.bak`);
  the red binary was never used to claim green.

## 7. Files changed

- `src/workspace/diagnostics/diagnostics_integration.cpp` (hover wiring only:
  filter installation/mapping, pane resolution, poll adoption, hide-publish,
  document/scroll invalidation).
- `src/workspace/diagnostics/diagnostic_hover.cpp` (multi-line negative-end
  parity with the underline).
- `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp` (RM-10 regression:
  real inner-widget mouse move, non-diagnostic control, Ctrl+click, multi-line
  decision checks, screenshot).
- `evidence/ux-batch-2/rm10-hover/**` (this README, red/green logs, screenshots).
