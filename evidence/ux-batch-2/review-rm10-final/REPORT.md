# RM-10 final independent review — hover over the squiggle

- Task: T23 (independent verification of T22 / RM-10), read-only.
- Reviewer: `reviewer-3` (Reviewer).
- Date: 2026-11-06 (Asia/Shanghai).
- **Verdict: PASS. Risk: MEDIUM.**
- Basis: the RM-10 card in `evidence/ux-batch-2/REAL-MACHINE-FINDINGS.md` (the user's words and
  the requirements: the hover on the diagnostic's text range, the real inner `KateViewInternal`
  path, the placement not covering the text / inside the window / the existing `adoptHoverView`,
  the split panes, the no-dangling-popup lifetime with the QPointer discipline, no stealing the
  selection / tooltip / Ctrl+click, and the second-defect underline-vs-diagnostic check); the
  writer's evidence `evidence/ux-batch-2/rm10-hover/` (README sha256 `…`; the red/green,
  the regression logs, the screenshots); the code.

I read the writer's README, the red (`04-b3-red.log`, 89 checks / 2 failures) and green
(`05-b3-green.log`, 90/0) logs, the regression logs (RM-9 e2e 33/0, RM-2 e2e 133/0,
project-lint e2e 11/0), the `04c-hover-popup-inner-squiggle.png` screenshot, and the relevant
code (`diagnostics_integration.cpp` hover wiring, `diagnostic_hover.cpp`,
`diagnostics_annotations.cpp`, `b3_wiring_e2e.cpp`). I re-ran the targeted hover harness in the
pinned image (`01-ctest-rerun.log`).

---

## 1. What I re-derived (commands and observed output)

- `ctest -R "vedit-b3-wiring-e2e|vedit-rm9-status-dots-e2e|vedit-rm2-project-switch-e2e|vedit-project-lint-e2e" -V`
  in `vedit-qt6-build:local`, offscreen: all pass; the b3 wiring harness carries the RM-10 hover
  checks (the writer's green 90/0; my rerun is **89/0** — the writer's extra check is the screenshot save in the same harness, and every RM-10 hover check passes in my run).
- The writer's **red** (`04-b3-red.log`): the only change is the pre-fix delivery condition
  re-inserted as a guard (`if (watched != hoverView_.data()) return …;`), everything else
  identical. The real-inner move then does nothing — the user's symptom:

```
OBSERVED hover.inner-squiggle diagnostic=0 rule='no-trailing-spaces' line=1 gutter-rule='no-trailing-spaces'
CHECK FAIL hover.inner-squiggle-shows
CHECK FAIL hover.inner-same-content-as-gutter
SUMMARY checks=89 failures=2
```

  The guard was reverted; the green is `SUMMARY checks=90 failures=0 skips=0`. This is a real
  red for the delivery seam (the text path did nothing pre-fix), not a message change.
- The **green** checks I relied on (the writer's log, the code, and the screenshot):
  `hover.real-target-is-inner-widget` (KateViewInternal), `hover.blank-hides-before-inner-probe`,
  `hover.inner-squiggle-shows`, `hover.inner-same-content-as-gutter` (rule/line/file equal to the
  gutter path), `hover.inner-non-diagnostic-shows-nothing`,
  `hover.ctrl-click-inside-squiggle-still-dispatches` + `-moves-caret` (the caret moves to the
  clicked point), `hover.multiline.first-line/middle-line/last-line/before-start/past-last-line-end/negative-end-covers-last-line`,
  `hover.no-fix-branch`, `hover.stale-detects-cleared-row`, `hover.tab-switch`.
- The **screenshot** `04c-hover-popup-inner-squiggle.png`: the popup `Error · no-trailing-spaces /
  fake upper-layer error / Fix` appears next to the squiggled `logic x;` line from the inner-widget
  mouse move; the `02`/`03` show the window and the diagnostics panel; the `05-file-status-dots`
  is the RM-9 surface. The popup sits beside the text, not over it.

---

## 2. The six checks

| # | Check | Result |
|---|---|---|
| 1 | Hovering the squiggled TEXT shows the same popup as the gutter, via the real inner-widget mouse path; red for today's no-op | **PASS.** The pre-fix filter was installed on the public `View` and only accepted `watched == hoverView_`; a real move is delivered to KatePart's inner `KateViewInternal` child, so the text path never fired (the writer's mechanism; the red `hover.inner-squiggle-shows`/`inner-same-content-as-gutter` fail). The fix `watchViewTree()` installs on the whole view subtree (including lazily added children), maps the receiving widget's point into the view before `coordinatesToCursor`, and returns false (observer). The green asserts the inner path's content equals the gutter path (rule/line/file) and the screenshot shows it. |
| 2 | Range edges: start/end, one before/after, blank at EOL, multi-line every covered line; not start-column-only | **PASS.** `hover.multiline.first-line/middle-line/last-line` (every covered line), `before-start` and `past-last-line-end` (the one before/after), `negative-end-covers-last-line` (the writer's parity fix), the single-line `hover.inner-squiggle` (start) and the non-diagnostic control; the hit test is on the merged row range, not the start column only. |
| 3 | Placement: does not cover the text under the cursor, stays inside the window in a small window, not a second placement rule | **PASS with a Low note.** The placement is singular: `placePopup()` (+18/+22 from the cursor, clamped inside the anchor), now with the hovered view as the coordinate owner; no second rule was added (code). The screenshot shows the popup beside the text. The small-window clamp is code-level; the regression does not have an explicit tiny-window case (L1). |
| 4 | Split panes: the pane under the mouse, not always the active; no dangling popup on edit/scroll/refresh/close; the lifetime/UAF explicitly | **PASS for lifetime; PASS-with-bound for split panes (M1).** `paneForView()`/`EditorPane::containsEditorWidget` resolves the pane of the view under the mouse; the popup is re-parented to the hovered view; the poll does not steal a cross-pane bubble; the `:Fix` path uses the popup's pane. Lifetime: the anchored document's `textChanged` hides+publishes, `QEvent::Wheel` hides, the live poll re-resolves the cached point (keyboard scrolling) and hides on a stale row, the `QPointer`/destroyed-view clearing is kept; `hover.stale-detects-cleared-row`/`hover.tab-switch` pass. **M1:** the writer states there is no dedicated two-pane mouse regression (the existing suites have no split layout); the split-pane requirement is implemented through `containsEditorWidget` but not driven by a two-pane probe. |
| 5 | No regression: normal selection, existing tooltip, Ctrl+click from inside the squiggle | **PASS.** The filter always returns false (the observer); `hover.ctrl-click-inside-squiggle-still-dispatches`/`-moves-caret` pass; the non-diagnostic span shows nothing; the existing KTextEditor tooltip/selection are not consumed; the gutter/propagation path still passes. |
| 6 | If the underline span and the diagnostic range disagree, that is a second defect — fixed or carried, and recorded | **PASS (no second defect).** The writer's §5: the underline (`diagnostics_annotations.cpp` clamps `line/endLine/endColumn`, whole-line when end ≤ start, end-of-line for a negative end) and `diagnosticAt()` are computed from the same merged rows and agree; the one gap (a negative end on a multi-line range's last line) was fixed on the hover side (`diagnostic_hover.cpp`); no disagreement was found in the drawn span. The card's conditional second defect is therefore not present. |

---

## 3. Findings (ranked)

### M1 — MEDIUM — the split-pane requirement is implemented but has no dedicated two-pane mouse regression

**What.** The hover pane is resolved from the view under the mouse (`paneForView` /
`EditorPane::containsEditorWidget`), the popup is re-parented to the hovered view, and the poll
does not steal a cross-pane bubble. But the writer explicitly states the row did **not** add a
dedicated two-pane mouse regression (the existing suites have no split layout); the same-pane
`hover.tab-switch` check is the only related case. The task's #4 ("it appears for the pane the
mouse is in, not always the active pane") is therefore code-verified, not end-to-end verified.
**Where.** `src/workspace/diagnostics/diagnostics_integration.cpp` (the hover wiring,
`paneForView`); `src/app/editor_area.cpp` (`containsEditorWidget`); the writer's README §6 bound.
**Why it matters.** A split-pane mis-target is the exact second-order case the task names; the
code path is simple but a future refactor of the pane resolution or the poll adoption would not be
caught by the hover harness.
**Direction.** Add a two-pane probe: open A and B in two panes, move over the non-active pane's
squiggle, assert the popup uses that pane's file and anchor; switch the active pane and repeat.

### M2 — MEDIUM — the acceptance's real-desktop half remains offscreen

**What.** The b3 harness runs offscreen with the real inner widget; the screenshots are the product
window at 1204×804 but from the offscreen platform. A real KDE/GNOME session adds the platform
theme, the real font metrics, the actual tooltip/Compositor behaviour and the real mouse; the
placement clamping and the "not covering the text" claim are code + an offscreen screenshot here.
**Where.** the b3 harness; the `04a`/`04b`/`04c` screenshots; the writer's bounds.
**Why it matters.** The user's report was real-machine; the mechanism is proven, the last-mile
visual (a popup under a compositor/with the user's fonts) is not.
**Direction.** A real-machine hover on the user's two desktops is the final check.

### L1 — LOW — the small-window placement claim is code-level, not an explicit tiny-window test

**What.** `placePopup()` clamps the popup inside the anchor; there is no dedicated
tiny-window regression that shrinks the window and asserts the popup stays inside.
**Where.** `src/workspace/diagnostics/diagnostics_integration.cpp` (`placePopup`); the b3 checks.
**Why it matters.** The clamp is a simple `qBound`; the case is low risk, but the task's #3 names
it.
**Direction.** Add a resized-window case to the b3 harness.

### N1 — NIT — the regression file is outside the declared T22 write scope

**What.** The RM-10 regression lives in `b3_wiring_e2e.cpp`, the harness that owns the hover
checks; the writer states the T22 listed scope was `diagnostic_hover.*` + the hover wiring +
evidence, and that `b3_wiring_e2e.cpp` is outside it (flagged to the captain; a new file would
duplicate the whole shell setup).
**Where.** `src/workspace/diagnostics/selftests/b3_wiring_e2e.cpp`; the writer's README §6.
**Why it matters.** Scope hygiene only; the harness is the right seam.

### N2 — NIT — the red is a temporary guard, not a second binary claim

**What.** The red run inserted the pre-fix delivery guard for one build, ran, and restored it from
a byte backup; the working tree carries the fix (`grep 'RM-10 RED-BEFORE'` = 0); the red binary
was not used to claim green.
**Where.** the writer's README §4; `04-b3-red.log`.

---

## 4. Declined to judge (reason on the same line)

- **A real KDE/GNOME session / compositor** — offscreen harness; M2; the user's desktop.
- **The two-pane mouse regression** — implemented but no dedicated probe; M1; the existing suites have no split layout.
- **The small-window placement** — code clamp; L1.
- **The real language-server jump from Ctrl+click** — the harness proves the gesture dispatches the definition action and moves the caret; the actual jump needs the LSP (the writer's bound; the batch's B4 probes cover it).
- **The underline-vs-diagnostic second defect** — the writer's §5 says they agree and the negative-end was fixed on the hover side; no disagreement found; PASS (no second defect to carry).
- **The `b3_wiring_e2e.cpp` scope** — the writer's flag; N1.
- **The temporary red guard** — the writer's restore; N2.
- **The rewrite of the whole hover subsystem** — the row's scope is the text-range delivery/lifetime; the pre-existing placement/adopt path is reused.
- **The full gate** — the captain; I re-ran the targeted harnesses.

---

## 5. Bounds of this review (what a real desktop adds)

- The b3 harness (and my rerun) is **offscreen**; the screenshots are the real product window on the
  offscreen platform; a real KDE/GNOME session adds the compositor, the user's fonts and the real
  mouse, which is the last-mile visual check (M2).
- The split-pane path is code-verified (the pane's view via `containsEditorWidget`), not driven
  with a two-pane mouse regression (M1).
- The small-window clamp is code-level (L1).
- The Ctrl+click check proves the gesture dispatches the definition action and moves the caret; the
  actual LSP jump is outside the harness (the writer's bound).
- The tests are the writer's harness + my rerun; the red is the writer's temporary guard; the
  working tree carries the fix.
- No product file was edited; the only writes are this report and the review logs under
  `evidence/ux-batch-2/review-rm10-final/`.

---

## 6. Verdict

**PASS — risk MEDIUM.**

The reported half-delivery is fixed at the right seam: the pre-fix filter only saw moves that
propagated to the public `View`, so the gutter worked and the underlined text did not; the fix
installs on the view's whole widget subtree (including lazy children), maps the receiving widget's
point into the view and returns false as an observer. The red shows the real-inner move doing
nothing pre-fix (2 failures); the green shows the same content as the gutter (rule/line/file), all
the range edges (multi-line first/middle/last, before-start, past-end, negative-end), the
non-diagnostic control, and Ctrl+click still dispatching with the caret moved; the screenshot
`04c` shows the popup from the inner path. The split-pane resolution and the no-dangling lifetime
are implemented (`paneForView`, re-parent, `textChanged`/`Wheel`/poll/stale hides, the `QPointer`
discipline) and the same-pane tab-switch/stale checks pass; the underline and the diagnostic range
agree, so the conditional second defect is not present. The Medium risk is the undisclosed-by-the-
harness two-pane case (M1) and the offscreen-only visual (M2); the writer states both. My rerun of
the targeted harness passes every RM-10 hover check and all three regression suites (b3 89/0; the writer's 90/0 adds the screenshot check; RM-9 33/0; RM-2 133/0; project-lint 11/0).
