# B4 repair — Ctrl+click → definition, Alt+click → add cursor

Repair lane for the independent review that returned **FAIL** on B4
(`evidence/ux-batch-2/review-b4/REPORT.md`, blocker R1, high R2, medium R3, nit R4).
This directory is the repair's evidence; `evidence/ux-batch-2/b4/` is left untouched.

Scope of this repair (task-granted write scope):
`src/editor/kernel/multi_cursor.{h,cpp}`, `src/editor/kernel/lsp_actions.{h,cpp}`,
`src/editor/kernel/multi_cursor_selftest.cpp` and their CMake entries.
**No CMake entry needed to change**: no target, source or Qt module was added, so the
ctest registry is unchanged at 47 tests. `src/app/**`, `src/workspace/**`, `crates/**`,
`docs/USER_GUIDE.md`, `translations/**` were not modified.

Environment: everything below ran inside the pinned image
`vedit-qt6-build:local` (Qt 6.10.2 / KF6 6.24.0), workspace mounted at `/vedit-build`.
The lane test runs with `QT_QPA_PLATFORM=offscreen` (same as its ctest definition).

## Verdict

All four findings are repaired or narrowed with evidence, except the part of R3 that
lives outside the granted scope (shell pane activation — see R3 Bounds). The full
gate `bash scripts/build-qt.sh` is green: 47/47 ctest, xcb smoke `ok=1`.

One retry was needed under the task's build-contention rule: a re-run failed
`vedit-project-lint-e2e (SIGPIPE)` — a test outside this lane that passed in the run
before and after, with no source outside this lane changed between runs. I waited 90 s and
retried once; the retry passed 47/47 (`40-full-build-qt-attempt1-sigpipe.log` keeps the
failed attempt).
Lane test: **119 checks, 0 failures** (was 81 checks before this repair; all 81 old
check names are still present — `51-check-continuity.txt`).

## What changed (path + symbol)

| File | Symbol | Change |
|---|---|---|
| `src/editor/kernel/multi_cursor.h` | `MultiCursorController::viewPointFor` | new: maps the *receiving widget's* local point into the View's coordinate space |
| `src/editor/kernel/multi_cursor.h` | `handleMouse(QWidget *watched, QMouseEvent *)` | takes the watched widget (was `handleMouse(QMouseEvent *)`) |
| `src/editor/kernel/multi_cursor.h/.cpp` | `reportDefinitionUnavailable`, `definitionNoticeReason_` | latch is now per reason; a changed reason is surfaced inside one episode |
| `src/editor/kernel/multi_cursor.h/.cpp` | `watchDefinitionAvailability`, `refreshDefinitionNotice`, `availabilityHost_`, `definitionAction_` | watch the host's `vedit.diagnostics.gates` dynamic property and the `QAction::changed` signal; re-arm the latch whenever availability is observed enabled (any path) |
| `src/editor/kernel/multi_cursor.cpp` | `MultiCursorController::handleMouse` | all 5 `coordinatesToCursor` call sites now use `viewPointFor(watched, event)` (press / move / release, Ctrl / Alt / block paths) |
| `src/editor/kernel/multi_cursor.cpp` | `eventFilter` | passes the watched widget to `handleMouse`; handles `DynamicPropertyChange` on the host |
| `src/editor/kernel/lsp_actions.cpp` | `dispatchDefinitionAt` | after the availability check, installs the clicked position as the **single** cursor position (`setCursorPositions({position})`) before dispatch (was `setCursorPosition`, which kept stale cursors) |
| `src/editor/kernel/lsp_actions.h` | `dispatchDefinitionAt` doc | states the single-cursor contract |
| `src/editor/kernel/multi_cursor_selftest.cpp` | `deliveryTarget`, `pointInTarget` | new helpers: route every gesture to the real delivery target (`view->childAt(point)` = KatePart's `KateViewInternal`) and convert the point into that widget's coordinates |
| `src/editor/kernel/multi_cursor_selftest.cpp` | `edit.block.*`, `edit.alt-click.*`, `definition.*` | gestures driven through the inner widget; new `definition.collapse.*`, `definition.gate-only.*`, `definition.action-only.*`, `definition.rearm.*`, `definition.two-pane.*` checks |

Code pointers with line numbers: `60-code-pointers.txt`.

## Per finding

### BLOCKER R1 — wrong coordinate space (FIXED, test-proven)

*What changed.* The controller filter is installed on the view and its descendants, so a
real press arrives at `KateViewInternal` with **child-local** coordinates.
`viewPointFor()` now maps `watched->mapTo(view_, event->position())` before every
`coordinatesToCursor` call (5 call sites). The gesture checks now deliver through the
same inner widget a real click reaches, not the `View` object.

*Red before* (`10-red-selftest.log`, temporary behaviour revert — see Method):
```
CHECK definition.ctrl-click.line-column FAIL (0:2 (expected 1:2))
CHECK edit.alt-click.clicked-position FAIL (cursors=2)
CHECK edit.block.cursor-lines FAIL (cursors=3)
CHECK edit.block.rectangular-insert FAIL ([0]='Xone', [1]='Xtwo', [2]='Xthree', [3]='four')
CHECK definition.two-pane.clicked-caret FAIL (caret=0:0 recorded=0:0)
```
*Green after* (`21-green-selftest.log`): `definition.ctrl-click.line-column PASS`,
`edit.alt-click.clicked-position PASS`, `edit.block.cursor-lines PASS`,
`edit.block.rectangular-insert PASS`, `definition.two-pane.clicked-caret PASS`.
The new checks assert the delivery target explicitly
(`edit.block.inner-widget`, `definition.ctrl-click.inner-widget`,
`definition.two-pane.inner-widget`).

*Acceptance ②*: the physical point of text `1:2` is clicked through the inner widget and
the definition action observes file `/tmp/b4-definition.sv`, line `1`, column `2`.

### HIGH R2 — Ctrl+click must collapse an existing multi-cursor set (FIXED)

*What changed.* `dispatchDefinitionAt` sets `view->setCursorPositions({position})` (one
position) instead of `setCursorPosition`, before the dispatch. The refusal path is
unchanged: the press is not consumed and KatePart's plain click does the collapse itself.

*Red before*: `CHECK definition.collapse.ctrl-click FAIL (count=3 (a Ctrl+click is a click and must collapse the set))`
and `definition.collapse.ctrl-click-position FAIL (caret=0:0 dispatched=0:0)`.
*Green after*: both PASS; the plain-click control starts from the same 3 cursors and also
collapses to 1 (`definition.collapse.plain-click PASS`), and the Ctrl+click case asserts
count 1, caret `1:2`, and that the action fired (`definition.collapse.ctrl-click-dispatched PASS`).

### MEDIUM R3 — product-wiring gap (narrowed + two gate-separation proofs)

R3 has three requested parts:

**(b) Gate row vs QAction must be tested separately — DONE.**
The old scenario disabled both together. The new test has:
* `definition.gate-only.*`: gate row off, **action enabled** → availability false for the
  gate's reason, no dispatch/trigger, plain-click fallback, one status reason.
* `definition.action-only.*`: gate row on, **action disabled** → availability false for the
  action's tooltip reason, no dispatch/trigger, plain-click fallback, one status reason.

To prove these new checks have teeth (they pass on the pre-repair product because
`availability()` already honours both gates — the defect was test coverage), two
mutations of `LspActionChannel::availability` were built and run:

| Mutation | Observed failures (`31-…`, `32-…`) |
|---|---|
| ignore the disabled-`QAction` gate (`if (false && !wired->isEnabled())`) | `definition.action-only.availability FAIL` (exactly 1 failure of 119) |
| ignore the published gate row (`result.enabled = true`) | `definition.gate-only.availability FAIL`, `definition.gate-only.no-request FAIL`, `definition.gate-only.status-reason FAIL` (+ the R4 reason checks, which depend on the refusal existing) |

Both mutations were reverted and the source hashes re-verified
(`30-fixed-sha256.txt`).

Honest attribution of the red run: the per-view routing assertions themselves were already
true before this repair (the old kernel also routed per view); no new code was needed for
them. Their red evidence is the coordinate-space failure (`definition.two-pane.clicked-caret
FAIL (caret=0:0 recorded=0:0)` in `10-red-selftest.log`) and, for the gate-separation
assertions, the two mutations above. They were added because the old test could not see a
product divergence at all, not because the kernel code for them was broken.
`definition.gate-only.status-reason` / `definition.action-only.status-reason` also fail in
the red log; those two are the reverted R4 latch (a changed reason was suppressed), not the
gate separation, which is covered by the mutations.

**(a) Two-pane case — DONE at the kernel boundary, shell activation REPORTED (see bounds).**
`definition.two-pane.*` builds one `QMainWindow` with two `KTextEditor::View`s and ONE
`action-diagnostics.definition` (the product's single shell action), with view A focused
("previously active") and the Ctrl+click delivered to view B's inner widget. Asserts:
B's channel dispatch count 1, A's channel 0, the action triggered once, B caret `1:2`,
A caret untouched, B cursor count 1, and keyboard focus ends inside B.
This is a genuine per-view routing property of the repaired kernel. It is **not** a proof
of the shell's active-pane change; see R3 bounds.

**(c) What is seam-only — stated precisely below.**

### NIT R4 — one-time latch re-arms (FIXED)

*What changed.* The latch stores the reason and re-arms when availability is observed
enabled by: the host's `vedit.diagnostics.gates` dynamic-property change, the wired
`QAction::changed` signal (only when availability then evaluates enabled), each Ctrl+click
before dispatch, and a successful dispatch. The product order is `applyGate()` (action)
first, `publishGates()` (property) second — the test reproduces exactly that order, so the
host-property path is the one that observes the final state.

*Red before*: `definition.rearm.first-message FAIL`, `definition.rearm.message-after-restore FAIL`,
`definition.rearm.changed-reason FAIL`.
*Green after*: `definition.rearm.restored PASS`, `definition.rearm.message-after-restore PASS`,
`definition.rearm.changed-reason PASS`, `definition.rearm.changed-reason-one-time PASS`.
A recovered episode (availability returns, seen by F12 / a reconnect, with no successful
Ctrl+click) re-arms the reason; a changed reason inside one episode is surfaced once.

## R3 bounds — exactly what this test cannot reach

The shell side cannot be linked from this test target, and the code that must change is
outside this repair's write scope:

* `vedit::app::EditorArea::activePane()` / `setActivePane()` and
  `EditorPane::eventFilter` are implemented in `src/app/editor_area.cpp`, which is compiled
  into the `vedit_app_shell` static library (`src/app/CMakeLists.txt:27,50`). The
  `vedit-editor-multi-cursor-selftest` target links only `vedit_editor_core` + `Qt6::Test`
  (`src/editor/CMakeLists.txt:271-291`); `VEDIT_EDIT_CORE_HAVE_APP_SEAM` only adds
  `src/app` to the include path, it does not link that implementation.
* The product's navigation handler reads `activePane()->currentFilePath()` and
  `activeView()` (`src/workspace/diagnostics/diagnostics_integration.cpp`,
  `BackendWiring::beginNavigation`), not the clicked view. The two-pane recorder in this
  test reads the **keyboard-focus-derived** view as a proxy; `definition.two-pane.clicked-file`
  and the recorder caret are therefore **seam-only** — they would pass even if the shell
  read the wrong pane.
* Measured shell behaviour unchanged by this repair: review probe
  `evidence/ux-batch-2/review-b4/01-focus-delivery.txt` shows `b->setFocus()` moves
  `QApplication::focusWidget()` to B's `KateViewInternal` but delivers **no**
  FocusIn/MouseButtonPress to the filter installed on the `KTextEditor::View`, so
  `EditorPane`'s focus-request callback never runs and `EditorArea::active_` stays on the
  previous pane. Consequence: **cross-pane Ctrl+click in the product still dispatches the
  previously active pane's file/caret**. The kernel cannot change `EditorArea` state from
  inside `dispatchDefinitionAt`; sending a synthetic FocusIn to the View would rely on a
  private shell convention and cannot be asserted here, so it was not done.
* Needed follow-up (out of scope for this task): in `EditorPane::wireEditor`, install the
  pane's event filter on the editor widget TREE (or connect to the inner widget's
  FocusIn) so a click in a non-active pane activates it; then extend this two-pane test to
  instantiate the real `EditorArea` from a product-seam target.
* Other seam-only assertions, unchanged in kind from the review: the test triggers the
  `QAction` named `action-diagnostics.definition` (the object name the app registers) but
  does not exercise `src/app/command_registry.cpp`'s F12 shortcut/handler; the zh_CN
  translation of the status string is the captain's `lupdate` step (outside the lane).
* R1 is proven for Qt's real delivery target in the offscreen platform, not by a real X11
  click in `vedit-app`; the coordinate mapping under test is exactly the widget-local →
  view-local map Qt applies before delivery.

## Method (reproducible)

Fixed source hashes: `30-fixed-sha256.txt`.

```bash
# lane build + run (inside the pinned image)
docker run --rm -v "$PWD":/vedit-build -w /vedit-build \
  -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp/xdg-runtime -e LANG=C.UTF-8 \
  vedit-qt6-build:local bash -lc '
    cmake --build target/qt6-build --target vedit-editor-multi-cursor-selftest &&
    QT_QPA_PLATFORM=offscreen \
      ./target/qt6-build/src/app/editor-lane/vedit-editor-multi-cursor-selftest'

# full gate
bash scripts/build-qt.sh        # log: 40-full-build-qt.log
```

The **red-before** log (`10-red-selftest.log`) was produced with the *final* test file and
a temporary behaviour revert of exactly the repaired lines:

1. `viewPointFor(watched, event)` → `event->position().toPoint()` at the 5 `handleMouse`
   call sites (plus `Q_UNUSED(watched)` to compile);
2. `setCursorPositions({position})` → `setCursorPosition(position)`;
3. `refreshDefinitionNotice()` body emptied and the latch condition back to
   `if (definitionNoticeShown_)`.

Then the fixed sources were restored and byte-verified with `sha256sum -c
30-fixed-sha256.txt` before the green run and before the full gate. No product code in the
final tree carries any `UNFIXED SIMULATION`/`MUTATION` text (`grep` returns nothing).
The red/mutation logs predate one whitespace-only normalisation of the file end in
`multi_cursor.cpp` (a trailing blank line removed to satisfy `git diff --check`); that is
the only content difference between the files those logs used and the hashes recorded in
`30-fixed-sha256.txt`.

## Evidence files

| File | Content |
|---|---|
| `10-red-selftest.log` | 119 checks, 13 failures — the final checks against the reverted behaviour |
| `20-green-build.log` | lane target build with the repair |
| `21-green-selftest.log` | **119 checks, 0 failures** |
| `22-green-selftest-repeat.txt` | 3 consecutive green runs (stability) |
| `30-fixed-sha256.txt` | hashes of the five repaired files |
| `31-mutation-action-gate.log` | mutation "action enabled ignored": exactly 1 failure (`definition.action-only.availability`) |
| `32-mutation-gate-row.log` | mutation "gate row ignored": `definition.gate-only.*` failures |
| `40-full-build-qt.log` | **final `bash scripts/build-qt.sh`**: Rust layer + configure + build + 47/47 ctest + xcb smoke `ok=1` |
| `40-full-build-qt-attempt1-sigpipe.log` | retried attempt: unrelated `vedit-project-lint-e2e (SIGPIPE)` (passed before and after) |
| `41-final-build-and-ctest.log` | rebuild + 47/47 ctest immediately after restoring the gate mutations |
| `50-ctest-and-smoke.txt` | extracted ctest/smoke summary |
| `51-check-continuity.txt` | 81 old check names all preserved; 38 added |
| `60-code-pointers.txt` | file:line pointers for every changed symbol |
