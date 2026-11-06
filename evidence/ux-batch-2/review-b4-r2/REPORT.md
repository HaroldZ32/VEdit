# B4 re-review (r2) — Ctrl+click → definition, Alt+click → add cursor

Reviewer: independent verifier. READ-ONLY for the reviewed work: no source file, test, CMake file or
gate was edited by this review; the only files written are this report and the probe/evidence files in
`evidence/ux-batch-2/review-b4-r2/`.

Contract: `.mpd/plans/ux-batch-2/00-plan.md` §B4, with §0 item 3 (Ctrl+click = go to definition,
multi-cursor moves to Alt+click).
Previous verdict under re-review: `evidence/ux-batch-2/review-b4/REPORT.md` (FAIL: R1 blocker, R2 high,
R3 medium, R4 nit).
Repair evidence reviewed: `evidence/ux-batch-2/b4-repair/` (README, `10-red-selftest.log`,
`20-green-build.log`, `21-green-selftest.log`, `22-green-selftest-repeat.txt`,
`30-fixed-sha256.txt`, `31-mutation-action-gate.log`, `32-mutation-gate-row.log`,
`40-full-build-qt.log`, `41-final-build-and-ctest.log`, `50-ctest-and-smoke.txt`,
`51-check-continuity.txt`, `60-code-pointers.txt`) and the original `evidence/ux-batch-2/b4/` set.
New lane reviewed because it bears on R3: `evidence/ux-batch-2/pane-activation/` (README,
`00-scope.txt`, before/after test logs, `50-ctest.log`, `51-ctest-without-new-test.log`,
`60-build-qt.log`, `80-real-click-delivery.txt`, `probe_real_click_delivery.cpp`).

New evidence produced by this review (all in `evidence/ux-batch-2/review-b4-r2/`):

| File | Content |
|---|---|
| `10-selftest-rerun.log` | lane selftest re-run from the current tree: 119 checks, 0 failures |
| `11-check-continuity.txt` | independent set-diff of the 81 old check names vs the 119 current names |
| `20-probe-output.log` | independent probe run (33 checks, 0 failures) against the real product classes |
| `probe_pane_order.cpp`, `build-probe.sh` | the probe source and its reproducible build/run script |
| `30-ctest-count-and-lane-tests.txt` | `ctest -N` = 50; the two relevant ctest entries pass |
| `31-hashes-and-residue.txt` | `sha256sum -c` over the repair's five files (5/5 OK); no mutation residue |
| `32-pane-activation-rerun.log` | the pane-activation ctest re-run: 25 checks, 0 failures |
| `33-full-ctest.log` | the full suite re-run: 50/50 passed |

## Assessment

**Risk level: LOW. Verdict: PASS.**

All four prior findings are repaired, and the new split-pane activation lane does close R3(a) for the
Ctrl+click path: I traced the event order with a probe that instantiates the real `EditorArea` (two
panes, real edit-core provider, one shell `QAction`, real `MultiCursorController`/`LspActionChannel`,
status bar) and confirmed that the application-level filter in `EditorArea::eventFilter` activates the
clicked pane during the application-filter pass, *before* the object-filter pass that invokes
`MultiCursorController::handleMouse`; a recorder filter installed on the actual delivery widget
(KatePart's `KateViewInternal`) already saw `EditorArea::activePane() == pane-2` at the start of the
object-filter pass, and a handler connected to the real `action-diagnostics.definition` action read
`activePane() = pane-2`, the clicked pane's file and the clicked caret (`1:2`) at trigger time. The
coordinate fix is applied to all five `coordinatesToCursor` conversions and the lane test now drives
the real inner widget; Ctrl+click collapses a three-cursor set to one and the refusal path is a plain
click that collapses natively; the latch re-arms on recovery and surfaces a changed reason. All 81 old
check names still exist (119 checks now, 0 removed) and ctest is 50/50. The two remaining findings are
coverage nits in the lane's own test recorder; both are already covered by other tests or by this
review's probe, and neither is a product defect.

## The five judgements

### 1. R1 coordinate space — FIXED for every call site; the test now drives the real inner widget

- `MultiCursorController::viewPointFor` (`src/editor/kernel/multi_cursor.cpp`) maps the receiving
  widget's local point with `watched->mapTo(view_, event->position().toPoint())` and returns the point
  unchanged only when `watched` is the view itself. Its only caller is `MultiCursorController::eventFilter`
  after `isEditorOwnedWidget(view_, watched)`, so the map always has an ancestor and cannot hit Qt's
  "parent must be an ancestor" case. `handleMouse(watched, event)` passes `watched` through.
- All five `coordinatesToCursor` conversions in `MultiCursorController::handleMouse` use
  `viewPointFor(watched, event)` (Ctrl press, Alt press, Alt-drag move, block-drag move, release); a
  grep of the kernel finds no other `coordinatesToCursor` call in this controller.
- The test now drives the real delivery target: `deliveryTarget` (`view->childAt(viewPoint)`) and
  `pointInTarget` (`target->mapFrom(view, viewPoint)`) in `src/editor/kernel/multi_cursor_selftest.cpp`;
  `definition.ctrl-click.inner-widget`, `edit.block.inner-widget`, `edit.block.inner-widget-same-for-end`
  and `definition.two-pane.inner-widget` assert the target is not the `View` and is a descendant of it.
  The red-before log shows the final test failing against the old behaviour
  (`definition.ctrl-click.line-column FAIL (0:2 (expected 1:2))`, `edit.block.rectangular-insert FAIL`,
  `edit.alt-click.clicked-position FAIL`), so the checks have teeth.
- Independent reproduction (my probe, `20-probe-output.log`): for text `1:2` the view point is
  `(60,15)`, the delivery widget is `KateViewInternal` and its local point is `(14,14)`
  (`mapFrom(view) == (14,14)`, `mapTo(view)` round-trips to `(60,15)`, delta `(46,1)`), and the Ctrl+click
  lands the caret and the dispatched caret at `1:2`. The nonzero delta is the same defect the previous
  review measured at `(36,1)` on its layout; it is now removed by the mapping.

### 2. R2 multi-cursor collapse on success, plain-click fallback on refusal — FIXED

- On success `dispatchDefinitionAt` (`src/editor/kernel/lsp_actions.cpp`) installs
  `view->setCursorPositions({position})` — a one-element set — before `channel->dispatch`. The
  KTextEditor header documents that the first position is used for the primary cursor "just like if you
  would call setCursorPosition()", so making the set single is the correct collapse primitive.
- Probe (real controller, real view): a three-cursor set then a Ctrl+click at `1:2` leaves
  `viewCursors(viewB).size() == 1`, caret `1:2`, and exactly one dispatch. The lane's own checks
  `definition.collapse.plain-setup` / `plain-click` / `ctrl-setup` / `ctrl-click` / `ctrl-click-position` /
  `ctrl-click-dispatched` cover the same property from the test side.
- Refusal: `handleMouse` returns `false` after `reportDefinitionUnavailable`, so the press is not
  consumed and KatePart's own click runs. Probe with the gate and action disabled: no trigger, the
  three-cursor set collapses to one at the clicked position (`1:4`), and the status bar carries the
  reason. The lane's `definition.gate-only.plain-click` / `action-only.plain-click` /
  `refusal`-family checks cover the same.

### 3. R3 pane activation vs dispatch — CLOSED; the ordering is activation first, and it is measured

The order that would break the path, and why it is not the one in the tree:

- **Breaking order (the old R3 defect):** a pane-activation filter installed only on the public
  `KTextEditor::View` (or an activation deferred through a queued connection / `singleShot`) runs after
  (or never reaches) the kernel's object filter on `KateViewInternal`; `dispatchDefinitionAt` then
  triggers the shell action while `EditorArea::active_` is still the previously active pane, and
  `BackendWiring::beginNavigation` reads the old pane's `currentFilePath()`/caret. That is exactly what
  the previous review measured (`01-focus-delivery.txt`: zero FocusIn/MouseButtonPress at the view
  filter; `press.non-active-pane-becomes-active FAIL` in the lane's before-log).
- **Order in the tree now:** `EditorArea` installs itself as an **application** event filter
  (`EditorArea::eventFilter`, `QCoreApplication::instance()->installEventFilter(this)` in the
  `EditorArea` constructor). On `MouseButtonPress`/`FocusIn` it calls `activatePaneContaining`, which
  maps the event target through `EditorPane::containsEditorWidget` (checks every tab, and the whole
  editor subtree, so `KateViewInternal` matches) and calls `setActivePane` **synchronously**;
  `setActivePane` sets `active_`, toggles the pane badges and invokes `onActivePaneChanged_`
  synchronously (`src/app/app_shell.cpp`'s handler only updates the status bar and command states).
  Qt runs application event filters in `QCoreApplication::notify` before the receiver's object filters
  and before the widget's own handler, so the kernel's object filter on `KateViewInternal` runs after
  activation.
- **Measured, not assumed:** my probe installs an object filter on the actual delivery widget and
  records `EditorArea::activePane()` on the press; the log line
  `RECORDER press receiver=KateViewInternal activeAtPress=pane-2` proves the application filter had
  already activated the clicked pane before the object-filter pass began. The handler connected to
  `action-diagnostics.definition` then read `HANDLER active=pane-2 file=/tmp/b4-r2-probe-b.sv caret=1:2`
  — the same two reads (`activePane()->currentFilePath()` and the active pane's view caret) that
  `BackendWiring::beginNavigation` performs in
  `src/workspace/diagnostics/diagnostics_integration.cpp`. The product action is the same one:
  `addCommand("diagnostics.definition", …, QKeySequence(Qt::Key_F12))` and
  `setObjectName("action-" + spec.id)` in `src/app/command_registry.cpp`, with the handler connected to
  `requestDefinition()`.
- The pane-activation ctest is green in my rerun (`32-pane-activation-rerun.log`, 25/25; the lane's
  before-log has 11 failures including `press.non-active-pane-becomes-active FAIL`), and the full suite
  is 50/50. The mechanism probe `80-real-click-delivery.txt` shows the application filter also sees the
  child widget on the window-system route (`QWidgetWindow → child`), so the route a real X11 click
  takes reaches the same filter.

Residual bound, stated explicitly: the probe links `vedit_app_shell` and the real editor core but not
the `diagnostics_integration.cpp` handler body; it replicates that handler's two reads exactly. The
live LSP request and the async response landing in a pane are not driven end-to-end here (the lane
README and the pane-activation README declare the same limit). No *ordering* gap remains: activation is
synchronous and precedes both the kernel dispatch and the action trigger, and `dispatchDefinitionAt`'s
`view->setFocus()` gives an additional FocusIn-based activation before the trigger even if the press
path were missed.

### 4. R4 latch re-arm — FIXED

- The latch now stores the last reason (`definitionNoticeReason_`) and `reportDefinitionUnavailable`
  suppresses only an identical reason while the latch is set; a changed reason is surfaced once.
  `refreshDefinitionNotice` clears the latch whenever `LspActionChannel::availability(Definition)` is
  observed enabled, and it is reached from the host's `vedit.diagnostics.gates` dynamic-property
  change, the wired `QAction::changed` signal, a refresh before every Ctrl+click dispatch, and a
  successful dispatch.
- Probe (real `QMainWindow` status bar, real action/property order and also the reverse order):
  `r4.first-reason-shown` PASS, `r4.same-reason-one-time` PASS, `r4.same-reason-shown-after-rearm` PASS
  (availability restored without a successful Ctrl+click, then the same reason comes back and is shown
  again), `r4.changed-reason-shown` PASS, `r4.changed-reason-one-time` PASS. The lane's
  `definition.rearm.*` checks cover the same and fail in the red log with the reverted latch.

### 5. No test weakened — CONFIRMED (with one stated bound)

- Independent set-diff of the old B4 log's check names against my fresh run (`11-check-continuity.txt`):
  `old_checks=81`, `new_checks=119`, `removed_check_names` empty, 38 added, no duplicate check names in
  either log. The writer's `51-check-continuity.txt` agrees.
- The affected old checks kept their assertions and now drive a harder target: e.g.
  `definition.ctrl-click.line-column` still requires both line and column, `definition.ctrl-click.no-added-cursor`
  still requires exactly one cursor, `edit.block.cursor-lines` still requires cursors `(1,0)…(3,0)`,
  `edit.alt-click.clicked-position` still requires the clicked cursor to be in the set; the block/alt
  checks now deliver through `KateViewInternal` instead of the `View`, which is strictly stronger.
- ctest: `ctest -N` reports **50** tests; the full suite passed 50/50 in this review
  (`33-full-ctest.log`), including `vedit-editor-multi-cursor-selftest` and
  `vedit-editor-pane-activation-tests`. The B4 repair's own 47/47 and the pane lane's 49→50 are
  consistent with later lanes adding the now-present tests; no target was removed.
- Integrity: `sha256sum -c evidence/ux-batch-2/b4-repair/30-fixed-sha256.txt` passes for all five files,
  so the reviewed tree is the green-log tree; the five files contain no `UNFIXED SIMULATION`/`MUTATION`
  marker. The CMake registration of the lane test is unchanged in kind (offscreen, timeout 180,
  `SKIP_RETURN_CODE 77` only for a missing KTextEditor; the binary returns 1 on any check failure).
- Bound: `multi_cursor_selftest.cpp` is untracked and no backup of the pre-repair file exists, so an
  exact old-vs-new textual assertion diff is impossible; "not relaxed" is judged from the preserved
  names, the current assertion bodies for the touched areas, and the red/mutation logs, not from a
  checked-in baseline.

## Findings (ranked)

### Nit — N1: the lane's two-pane recorder still uses keyboard focus as its proxy for "what the shell reads"

- **What:** `definition.two-pane.clicked-file` and `definition.two-pane.keyboard-focus` in
  `src/editor/kernel/multi_cursor_selftest.cpp` derive the recorded file/caret from the view that holds
  keyboard focus inside the test's own QAction handler, not from `EditorArea::activePane()`. The test
  header says the product uses the shell's active pane and calls this seam-only.
- **Where:** `src/editor/kernel/multi_cursor_selftest.cpp` (`definition.two-pane.*`, the `twoPaneAction`
  handler).
- **Why it matters:** the focus proxy agrees with `activePane()` today (the pane-activation lane makes
  them agree), and my probe reads the real `activePane()` values, but this lane test alone would not
  catch a future divergence where focus moves to one pane and `activePane()` names another.
- **Direction of fix (not applied):** extend a product-seam target (or the existing
  `vedit-editor-pane-activation-tests`) with a Ctrl+click case that reads
  `EditorArea::activePane()->currentFilePath()` and the pane's view caret directly, so the assertion is
  against the shell value the handler actually reads.

### Nit — N2: the single-pane `definition.ctrl-click.file` assertion is supplied by the test itself

- **What:** in the single-pane block, `definition.ctrl-click.file` compares the recorder's
  `viewFilePath(definitionView)` with the path the test wrote into the view's `vedit.openFilePath`
  property; it cannot detect a dispatch that used a different file. (The per-view routing half is now
  covered by `definition.two-pane.*`, and the product read is covered by my probe and the pane lane.)
- **Where:** `src/editor/kernel/multi_cursor_selftest.cpp` (`definition.ctrl-click.file`).
- **Why it matters:** it is a vacuous assertion for the file dimension, so the lane's own evidence does
  not prove the file half of acceptance ②; the independent probe does.
- **Direction of fix (not applied):** read the file from the pane/active-pane seam in a shell-linked
  test instead of from a property the test set, or drop the check from the lane and keep the two-pane
  and probe evidence.

Neither finding is a product defect, and neither changes the verdict.

## Declined to judge (with reason)

- **Async navigation response landing** — where the jump lands after the LSP response arrives is the
  shared F12/navigation path and not the click-coordinate/pane mapping under review; not driven.
- **Selection clearing on a successful Ctrl+click** — contract-silent; `setCursorPositions`'s first
  position behaves like `setCursorPosition`, so the previous cursor-set collapse primitive is retained;
  not separately asserted, not judged.
- **Ctrl+click on an invalid/gutter position** — contract-silent; the code returns `false` and falls back
  to KatePart's handling; not material to the frozen acceptance.
- **A real X11 click into the packaged `vedit-app` split pane** — not driven; the application-filter
  mechanism is measured with a window-system route probe and the EditorArea ordering with an offscreen
  probe; the packaged-app end-to-end click remains unproven by any lane.
- **Live LSP definition request over a split layout** — not run; the handler's values are replicated by
  the probe, and `requestDefinition` is the unchanged shared path.
- **Programmatic focus-in in a background pane activating it** — the pane-activation lane's stated
  contract ("a press or focus-in anywhere inside a pane's editor makes that pane active"); no harmful
  product flow found and it is outside B4's scope; not judged as a B4 defect.
- **zh_CN translation of the "Go to Definition is unavailable…" status string** — the captain's
  `lupdate` step in plan §5.1; outside the lane.
- **Gate-row/action-only mutations not re-run** — re-running them would require editing product source,
  which this review does not do; the recorded logs show exactly the claimed teeth (1 failure for the
  action gate, 6 for the gate row, including the reason-dependent checks), and the current source passes
  the separated gate-only/action-only checks.
- **The 47-test count in the B4 repair evidence vs 50 now** — the difference is later lanes' tests
  (pane activation and others), not a B4 change; the current count is larger and the full suite passes.

## Bounds of this review

Read: the frozen contract (§B4, §0 item 3); the previous review's report and its three probe outputs;
all files in `evidence/ux-batch-2/b4-repair/` and `evidence/ux-batch-2/pane-activation/`; the reviewed
sources `src/editor/kernel/multi_cursor.{h,cpp}`, `src/editor/kernel/lsp_actions.{h,cpp}`,
`src/editor/kernel/multi_cursor_selftest.cpp`, `src/app/editor_area.{h,cpp}`,
`src/app/editor_area_activation_test.cpp`, `src/app/command_registry.*`, `src/app/app_shell.cpp` (the
EditorArea construction and the active-pane callback), and the navigation handler in
`src/workspace/diagnostics/diagnostics_integration.cpp`. Not read: the Rust diagnostics/LSP crates, the
whole application shell, other lanes' diffs, translation files, the packaged AppDir.

Ran (inside `vedit-qt6-build:local`): the lane selftest from the current tree (119/0), the
pane-activation test (25/0), the two ctest entries, the full ctest suite (50/50), `ctest -N` (50),
`sha256sum -c` over the repair's five files, the check-name continuity diff, and one independently
written probe (`probe_pane_order.cpp`) linked against `libvedit_app_shell.a` + `libvedit_editor_core.a`
with a real two-pane `EditorArea`, real provider/controller/channel, a shell-shaped
`action-diagnostics.definition` QAction, published gates and a status bar (33 checks, 0 failures).

Did not run: a real X11 mouse click into the packaged `vedit-app` window; a live LSP definition request
end to end; the translation step; the mutation builds (source edits are forbidden to this review).

## Verdict

**PASS** — the repair fixes the coordinate-space blocker at all five conversions and the test now
exercises the real inner widget; Ctrl+click collapses a multi-cursor set while the refusal path remains
a native plain click; the new application-level pane activation runs synchronously in the
application-filter pass *before* the kernel's object filter, so cross-pane Ctrl+click dispatches against
the clicked pane (probe: `activeAtPress=pane-2`, handler read `activePane=pane-2`, file B, caret
`1:2`); the latch re-arms and surfaces changed reasons; and the suite grew from 81 to 119 checks with
none removed and 50/50 ctest green.
