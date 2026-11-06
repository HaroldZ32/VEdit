# T19 session-restore — handoff state (paused for T17, per captain's priority)

Task row: **T19**, member `se-tree`. Contract:
`.mpd/cellwork/cell-20261102T050548232Z/contracts/session-restore.md`.
This note records exactly what exists in the working tree, what was observed, and
what is still open — so the next owner (or a resumed `se-tree`) does not redo it.

## 1. Code implemented (all inside T19's write scopes)

| File | What it now does |
|---|---|
| `src/app/session_store.h/.cpp` | Archive **schema v2**: typed `SessionState` / `SessionPane` / `SessionDocument` with tolerant `fromJson` (v1 archives still load), atomic `saveState`, and the JSON shape documented at the top of the header. Keys `version/project/theme/geometry/dockState/panels/editorLayout` are unchanged, so `src/workspace/project/project_integration.cpp:52` (project lane) keeps reading `project`. |
| `src/app/editor_area.h/.cpp` | Every layout leaf now carries `activeTab` + `documents:[{path, caret:{line,column}, scroll}]`. `EditorArea::restoreLayout()` re-opens every archived document of every pane, activates the archived tab, skips a vanished file with a diagnostic, replays caret/scroll only when the archiving provider matches, and applies a background tab's position on that tab's first `Show` event (a hidden stacked page would clamp a scroll offset). Pending positions are dropped on tab close / provider swap. `saveLayout()` writes `version: 2`. |
| `src/app/app_shell.h/.cpp` | `saveSession()`/`restoreSession()` go through `SessionState`; project root is restored before panels are inserted; `closeEvent()` asks **Save / Discard / Cancel for every dirty document of every pane before the archive is written** (Cancel aborts the close); `confirmUnsavedDocuments()` is public so the one-click restart path (T18, AC-10.1.2.2) can reuse the same prompt. |
| `src/workspace/project/project_state.h/.cpp` | **Not modified** — the project file set already persists in `project-model.json` (project lane, AC-2.4). The session stores the root only; that is deliberate, one source of truth. |

`vedit_app_shell` compiles clean with `-Wall -Wextra -Wpedantic -Werror`;
`vedit-app --selfcheck --state-dir …` (offscreen) reports
`APP-SHELL-SELFTEST ok=1 checks=245 failures=0`, exit 0.

## 2. Evidence already produced (re-runnable)

```
bash evidence/i3-session/probe/run.sh        # builds + runs the probe, both platforms
```

Probe source: `evidence/i3-session/probe/session_restore_probe.cpp`.
It constructs TWO real `ApplicationShell` instances over ONE state directory and
asserts the restart (AC-10.1.3.2 A/B scenario, AC-10.1.3.1 split scenario, v1 and
deleted-file tolerance, dirty-close rule).

Last observed results (2026-11-02, tree at the working-tree state described above):

| Run | Log | Result |
|---|---|---|
| offscreen | `logs/10-probe-run-offscreen-demo.txt` | `62 checks, 0 failure(s)`, probe exit=0 |
| xcb under Xvfb 1440x900 | `logs/10-probe-run-xcb-demo.txt` | `64 checks, 0 failure(s)`, probe exit=0 — includes exact `restore.geometry` 1111x733 and `restore.geometry.position` 140,90 and the real Save/Discard/Cancel dialog answers |
| product selfcheck | (run: `vedit-app --selfcheck`, offscreen) | `APP-SHELL-SELFTEST ok=1 checks=245 failures=0`, exit 0 |

Archive sample: `run-xcb-demo/session-before.json`; screenshots
`run-*/before-close.png`, `run-*/after-restart.png`.

## 3. What is NOT done

1. **Caret/scroll for the real KTextEditor editor (AC-10.1.3.1 item 4).**
   The demo provider (QPlainTextEdit) path is verified end to end. The KTextEditor
   view's caret/scroll needs a small provider seam that T19's write scope does not
   contain (`src/app/editor_provider.h` + `src/editor/kernel/ktexteditor_provider.*`
   are T12/lead-shell's files). KTextEditor::View exposes no Q_PROPERTY /
   Q_INVOKABLE / QAccessibleTextInterface for its caret, so it cannot be reached
   from the shell library (which must not include KTextEditor headers).
   Proposed patch (ready to consume, `captureViewState`/`applyViewState` in
   `src/app/editor_area.cpp` are the only two call sites):
   ```cpp
   // editor_provider.h, class EditorProvider
   struct EditorViewState { int caretLine = -1; int caretColumn = -1; int scrollTop = -1; };
   virtual EditorViewState viewState(QWidget *editor) const;                 // default: QPlainTextEdit
   virtual void setViewState(QWidget *editor, const EditorViewState &state); // default: QPlainTextEdit
   // ktexteditor_provider.cpp override:
   //   view->cursorPosition() / view->setCursorPosition() + view->verticalScrollBar()
   ```
   The probe already supports this mode: build with `-DVEDIT_PROBE_HAVE_EDIT_CORE=1`
   / run `bash evidence/i3-session/probe/run.sh --ktexteditor`.
2. **`evidence/i3-session/REPORT.md`** (the acceptance matrix document the verifier
   reads) is not written yet; this file is the raw state, not the report.
3. **Repository gates not yet quoted for this task.** At handoff time the full
   `bash build/gates/typecheck.sh` is red for cross-lane reasons (uncommitted
   Rust/vd-ffi header drift in `build/gates/typecheck.sh`'s drift check, and the
   Rust clippy half runs on the whole workspace); `cmake --build target/qt6-build`
   itself is green and `vedit-app` links. Re-run the gate once the tree settles and
   record the exit code in REPORT.md.
4. Offscreen-only caveat: the offscreen QPA plugin has one fixed virtual screen, so
   Qt's `restoreGeometry()` clamps a window wider than it; the exact size/position
   assertion therefore lives in the xcb/Xvfb run (`restore.geometry`,
   `restore.geometry.position`), and the offscreen run only compares the height.

---

## 3. Superseded by the T19 reassignment (lead-shell, 2026-11-02 17:06)

The work described above was audited, kept, and completed by the new owner. `REPORT.md` in this
directory is the acceptance report; the artifacts below are **superseded** and kept only as history:

- `logs/*demo*`, `run-demo/`, `run-offscreen-demo/`, `run-xcb-demo/`, `probe/session_restore_probe.cpp`
  (if present) were produced BEFORE the `ViewState` seam existed. The audit found the one acceptance
  hole they could not see: caret/scroll were captured only for the demo `QPlainTextEdit` provider, so
  the shipped KTextEditor editor reopened every file at line 1. The new probe
  (`probe/main.cpp` + `probe/build.sh` + `probe/run.sh`) runs on the REAL provider and asserts
  `after.caret-line` / `after.caret-column` / `after.scroll-line` — see `REPORT.md` §2.
- The claims in §1 (code) and §2 (selfcheck) still hold and were re-verified: `APP-SHELL-SELFTEST
  ok=1 checks=245 failures=0` (see `40-product-gates.log`).
- `project_state.*` IS modified now (schema v2: `filelists`/`variables`), contrary to the §1 note; the
  project file set still lives in `project-model.json`, the session still stores only the root.
