# T46 — split panes with a REAL mouse: hover, the 400 ms poll, multi-cursor and Ctrl+click

Status: **GREEN-ONLY — the gap is closed, no product defect found.** Both claims
the RM-6/RM-10 reviews marked unproven hold on a real two-pane layout:
`vedit-split-pane-mouse-e2e` → **38 checks / 0 failures**, twice in a row.

Test: `src/workspace/diagnostics/selftests/split_pane_mouse_e2e.cpp`, registered
as `vedit-split-pane-mouse-e2e` in `src/workspace/diagnostics/CMakeLists.txt`.
Evidence: `01-green-run.log`, `02-checks.log` (raw `/tmp/...` paths normalised).

## 1. The layout is built through the REAL product path

* real `ApplicationShell` + the real diagnostics lane installed
  (`installDiagnostics`), offscreen, shown;
* a deterministic fake `ProjectLintBackend` installed BEFORE the first
  event-loop turn supplies two DIFFERENT squiggles: `a.sv` → `no-trailing-spaces`
  (error) and `b.sv` → `line-length` (warning) at line 1, cols 2..10 — so the
  hover test can prove WHOSE diagnostic is on screen, not merely that one exists;
* `paneA` opens `a.sv`; the user's own command
  `view.split.horizontal` (the command registry action, not a test-only API)
  creates the second pane; `paneB` opens `b.sv`;
* `view.focus.next` — the product's keyboard action, **no mouse involved** — makes
  pane A active while pane B stays visible (`split.a-active activeIsA=1`,
  `split.b-visible`), and later switches the active pane both ways.

## 2. Assertions and observed values (`02-checks.log`)

### a) A real mouse move into a squiggle in the INACTIVE pane B
The event is delivered to `viewB->childAt(point)` — KatePart's inner
`KateViewInternal`, exactly the widget a real mouse hits (the b3 suite proved the
public View is not the user's path).

```
OBSERVED hover.after-move alive=1 diagnostic=1 rule='line-length'
         path='.../project/b.sv' line=1 parentIsActiveView=0
CHECK PASS hover.shows-b-diagnostic          rule='line-length' line=1 path='.../b.sv'
CHECK PASS hover.popup-parented-to-inactive-b   B
CHECK PASS hover.popup-inside-b-geometry      popup=110,37 382x56 bView=638x701
CHECK PASS hover.popup-visible
CHECK PASS hover.parent-is-not-the-active-view
```

The popup is a child of **B's view**, its geometry lies inside B's view rect, the
content is B's `line-length` row (A's is `no-trailing-spaces`), and
`parentIsActiveView=0` — the bubble follows the MOUSE, not the active pane.

### b) The 400 ms live poll must not steal a cross-pane bubble
With the mouse stationary, 900 ms (two poll ticks) later:

```
OBSERVED hover.after-poll alive=1 visible=1 rule='line-length' parent='B'
CHECK PASS poll.does-not-steal-bubble   diagnostic=1 visible=1 parentB=1
```

### c) Switching the ACTIVE pane without moving the mouse
`view.focus.next` twice (A→B, then B→A), 900 ms after each:

```
CHECK PASS poll.bubble-survives-b-active           activeB=1 parentB=1
CHECK PASS poll.bubble-does-not-jump-to-active-a   activeA=1 parentB=1 visible=1
```

### d) `Ctrl+Alt+click` inside B adds a cursor in B only

```
OBSERVED gesture.ctrl-alt-click a=1->1 b=1->2 clicked-in-b=1
CHECK PASS gesture.b-gained-cursor        b=1->2
CHECK PASS gesture.cursor-at-click-point  cursors=2
CHECK PASS gesture.a-unchanged            a=1->1
CHECK PASS gesture.native-view-cursors    b=2 a=1
```

### e) `Ctrl+click` inside B dispatches the definition action from B's document

```
OBSERVED definition.ctrl-click enabled=1 triggers=1 b-caret=0:8 a-caret=0:0
         doc='.../project/b.sv'
CHECK PASS definition.ctrl-click-dispatches  triggers=1
CHECK PASS definition.b-caret-at-click       b-caret=0:8
CHECK PASS definition.b-document-is-b
CHECK PASS definition.a-carets-untouched     a=0:0
```

### f) Controls that prove the mouse really entered B
* the hover point maps to a real coordinate inside B (`hover.b-point 92,15`);
* the rule on screen is B's `line-length`, never A's `no-trailing-spaces`;
* a non-diagnostic position in B hides the bubble through the same real path:
  `hover.control-non-diagnostic-hides diagnostic=0 visible=0`;
* the gesture and the definition changed B's state only (A's cursor set and
  caret untouched).

## 3. Red-first: nothing failed

The card's red-first step was run: on the current tree all 38 checks pass
(quoted above), so there is **no product defect to report and no product code was
changed**. This is the "all pass, quote the numbers" outcome the card allows; the
coverage gap is closed by the new ctest.

## 4. Bounds

* Offscreen Qt; the mouse events are synthesized `QMouseEvent`s delivered to the
  inner KatePart widget (the same delivery path a real X11 click uses inside the
  app). A real Xvfb/xcb click is not used here — the real-xcb-click path is
  covered by the existing `vedit-fold-gutter-click` probe for a different
  surface, not claimed for this one.
* The squiggles come from a deterministic fake CLI pass (the hover content
  pipeline, the popup placement, the poll, the gesture and the definition
  dispatch are the product's); the definition action is the real shell action and
  was `enabled=1` before the click.
* The multi-cursor gesture is asserted through the product's own
  `MultiCursorController` (`cursorCount()`/`cursors()`) and the native
  `viewCursors()` set, not through pixels.

## 5. Files

* `src/workspace/diagnostics/selftests/split_pane_mouse_e2e.cpp` — the test (38 checks).
* `src/workspace/diagnostics/CMakeLists.txt` — `vedit-split-pane-mouse-e2e`
  (offscreen, TIMEOUT 300), registered next to the b3 e2e.
* `evidence/ux-batch-2/leftovers-split/` — this README + green run + checks.
