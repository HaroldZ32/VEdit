# UX batch 2 — split-pane activation (click into a non-active pane's editor)

Objective: a mouse press or focus-in that lands inside a pane's editor must make
that pane the ACTIVE pane of `EditorArea`, so subsequent commands, the status bar
and the definition/rename/toolbar paths that read `activePane()` follow the pane
the user is working in. The activation path must not rely on tab clicks only.

## The defect (measured before the fix)

Each pane hosts a KTextEditor view, but the real mouse target is KatePart's inner
widget `KateViewInternal`, a CHILD of the public `KTextEditor::View`
(`evidence/ux-batch-2/review-b4/00-coordinate-space.txt`: inner widget at
`(36,1)` inside the view). `EditorPane` installed its event filter on the VIEW
only, and `EditorArea::activePane()` returns the area's own `active_`, which is
driven by that filter. A click on the inner widget delivers neither
`MouseButtonPress` nor `FocusIn` to the view filter
(`evidence/ux-batch-2/review-b4/01-focus-delivery.txt`), so the previously
active pane kept `active_` — and the click DID move the keyboard focus
(`press.focus-is-in-clicked-pane PASS` in the failing run), which is why the bug
was invisible in a single pane.

## What changed

Write scope: `src/app/editor_area.{h,cpp}` + the CMake entry for the new test.
No other product file was touched.

1. `EditorPane::containsEditorWidget(const QWidget *)` — true when the widget is
   one of the pane's editor widgets or a DESCENDANT of one (every tab checked,
   not only the active one).
2. `EditorArea::activatePaneContaining(const QWidget *)` — maps an event target
   to the pane whose editor subtree contains it and calls `setActivePane()`.
   No-op when the target belongs to the already-active pane (so a press followed
   by its own focus-in activates exactly once) and while `rebuilding_` (a
   re-layout's reparent/focus churn is not the user choosing a pane).
3. `EditorArea::eventFilter` — the area already installs itself as an
   APPLICATION event filter (T90/F-2). It now handles `MouseButtonPress` and
   `FocusIn` on that filter and calls (2). The application level is required:
   a filter on the view cannot see events delivered to its nested children.
4. The existing pane-side paths are untouched: `EditorPane::eventFilter`
   (view Show/FocusIn handling, save-shortcut re-application, pending session
   position) and `EditorPane::mousePressEvent` keep their behaviour, as do the
   tab helpers and the document-saved seam (`setOnDocumentSaved`, `saveTab`,
   `saveAsFile`).

New test: `src/app/editor_area_activation_test.cpp`, registered as ctest
`vedit-editor-pane-activation-tests` (offscreen; two panes in a real
`EditorArea` with the real edit-core provider; exit 77 = SKIP if KTextEditor is
missing). It delivers real `QTest` mouse events to the deepest child under the
click point (the inner widget — the log names it) and asserts:

* `press.non-active-pane-becomes-active`, `press.new-pane-reports-active`,
  `press.previous-pane-reports-inactive` (the previously active pane is no
  longer reported active), `press.exactly-one-activation`,
  `press.active-editor-is-clicked-pane` (what the command paths read),
  `press.badges-follow-the-active-pane`;
* the symmetric control: `control.already-active-pane-stays-active`,
  `control.no-double-activation`, `control.previous-pane-stays-inactive`;
* the tab path still works: `tab.click-activates-its-pane`,
  `tab.exactly-one-activation`;
* the focus-in half of the contract: `focus-in.activates-its-pane`,
  `focus-in.exactly-one-activation`;
* a single-pane layout is unchanged: `single-pane.press-keeps-active`,
  `single-pane.no-activation-callback`.

## Before / after (both runs observed; binary built with the pinned image)

Baseline (fix not applied, new test present):

```
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
    -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-runtime-pane \
    vedit-qt6-build:local \
    /vedit-build/target/qt6-build/src/app/vedit-editor-pane-activation-tests
EXIT=1
CHECK press.non-active-pane-becomes-active FAIL (active=pane-1 (expected pane-2))
CHECK press.new-pane-reports-active FAIL
CHECK press.previous-pane-reports-inactive FAIL
CHECK press.exactly-one-activation FAIL (switches=0 to=none)
CHECK press.active-editor-is-clicked-pane FAIL
CHECK press.badges-follow-the-active-pane FAIL
CHECK control.already-active-pane-stays-active FAIL
CHECK control.previous-pane-stays-inactive FAIL
CHECK tab.exactly-one-activation FAIL (0)
CHECK focus-in.precondition-b-active FAIL
CHECK focus-in.exactly-one-activation FAIL (0)
SUMMARY checks=25 failures=11
```

After the fix (same test, rebuilt):

```
EXIT=0
SUMMARY checks=25 failures=0
```

Raw logs: `20-test-before-fix.log`, `40-test-after-fix.log`; the builds that
produced them: `10-build-before-fix.log`, `30-build-after-fix.log`.

## Gate

`bash scripts/build-qt.sh` (the repository's documented product build) — PASS,
exit 0 (`60-build-qt.log`): configure + build inside `vedit-qt6-build:local`,
ctest, ELF facts, and the xcb + software-rendering smoke
(`M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ... window=1
editor=1 document=1`).

ctest count: **50/50 passed** with the new test (`50-ctest.log`, and the gate log
line `16/50 Test #16: vedit-editor-pane-activation-tests ... Passed`), and
**49/49 passed** with the new test excluded (`51-ctest-without-new-test.log`,
`ctest -E vedit-editor-pane-activation-tests`) — i.e. the pre-existing suite is
unchanged and green, and the count went 49 → 50 (the CMake diff adds exactly one
`add_test`). No existing test or gate was edited or weakened.

## Not proven (limits)

* **A real X11 click inside the packaged app.** The activation test is OFFSCREEN
  and delivers QTest events (`qApp->notify`) to the same widget a real click
  reaches. A separate mechanism probe
  (`probe_real_click_delivery.cpp` → `80-real-click-delivery.txt`, run under
  Xvfb/xcb) shows the APPLICATION filter also observes a mouse press delivered
  through the WINDOW SYSTEM (`QWidgetWindow` → the deepest child `QLabel`), i.e.
  the route a real click takes — but no real mouse click was driven into the
  packaged `vedit-app` split pane.
* **End-to-end definition/rename/toolbar against a live LSP backend.** The test
  asserts `activePane()` and `activePane()->editorWidget()` — the values those
  paths read (`src/workspace/diagnostics/diagnostics_integration.cpp`
  `beginNavigation`, `app_shell.cpp` `setOnActivePaneChanged` →
  `updateStatusBar`/`syncEditorCommandStates`) — but does not run those commands
  through a real backend in a split layout.
* **Status-bar pixels.** The test observes the EditorArea callback and the
  pane's active badge; the shell's `updateStatusBar()` is wired to that callback
  (code pointer §8) but was not rendered/screenshotted in a two-pane split.

## Reproduction

```
# build (host, pinned image; rootless docker -> no --user flag)
bash scripts/build-qt.sh

# the new test alone
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-runtime-pane \
  -e LANG=C.UTF-8 vedit-qt6-build:local \
  /vedit-build/target/qt6-build/src/app/vedit-editor-pane-activation-tests

# the whole suite
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  -e XDG_RUNTIME_DIR=/tmp/xdg-runtime-pane -e LANG=C.UTF-8 \
  vedit-qt6-build:local ctest --test-dir /vedit-build/target/qt6-build --output-on-failure
```
