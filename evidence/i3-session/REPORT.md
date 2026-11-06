# T19 — 会话恢复：窗口几何 / 分栏 / 活动标签 / 光标滚动 / 工程 / 面板可见性

Cell `cell-20261102T050548232Z` · task **T19** · member **lead-shell** · verifier **Reviewer**
Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/session-restore.md` (+ **ADDENDUM A-18**:
`src/app/editor_provider.h`, `src/editor/kernel/ktexteditor_provider.{h,cpp}` for the `ViewState` seam)
Acceptance: **AC-10.1.3.1**, **AC-10.1.3.2**. No test and no gate was edited.

## 1. What is restored, and where it lives

| Requirement | Restored from | Verified by |
|---|---|---|
| window size + position | `session.json` `geometry` (QMainWindow::saveGeometry) | `after.window-size` (exact), `after.window-position` |
| split / pane layout (count + H/V + ratio) | `session.json` `editorLayout.root` | `archive.layout-split`, `after.pane-count`, screenshot |
| active tab per pane | `editorLayout` leaf `activeTab` | `archive.leaf-active-tab`, `after.pane1-active-tab-is-b` |
| open documents per pane (tabs) | leaf `documents[].path` | `archive.leaf-documents`, `after.pane1-two-tabs`, `after.pane1-file-names`, `after.pane2-file` |
| **per-file caret + scroll** | leaf `documents[].caret/{line,column}`, `documents[].scroll` | `archive.caret-and-scroll-match-live-view`, `after.caret-line`, `after.caret-column`, `after.scroll-line` |
| project root | `session.json` `project` (applied before panels are inserted) | `archive.project-root`, `after.project-root` |
| project FILE SET | `project-model.json` (project lane's store, schema v2) | `after.project-model` (root + files + filelists + -I + -D) |
| panel visibility | `session.json` `panels` + `dockState` | `archive.panels`, `after.panel-file-tree`, `after.panel-outline`, `after.panel-terminal-hidden` |

Archive schema v2 is documented in `src/app/session_store.h` (`SessionState`/`SessionPane`/`SessionDocument`);
the sample written by the probe is `30-session.json` — e.g. `pane-1` holds `alpha.sv`
(`caret {line:59,column:0} scroll 8`) and the active `bravo.sv` (`caret {line:45,column:5} scroll 25`),
`pane-2` holds `charlie.sv`, root is a horizontal `0.5` split, plus `geometry`/`dockState` base64.
Scope is exactly requirements C13: terminal scrollback, VCS/search transient state and FIM ghost text are
NOT archived (nothing in the schema carries them).

## 2. The one place the acceptance was FALSE — found by audit, fixed (A-18)

`EditorArea` captured and applied caret/scroll only for the **demo `QPlainTextEdit`** provider; a
KTextEditor document reported no position, so the product would have reopened B at **line 1, scroll 0**.
That is exactly what AC-10.1.3.2 forbids, and it would have passed a probe written against the demo
provider. Fixed with the addendum:
- `EditorProvider::ViewState {caretLine, caretColumn, scrollTop}` + `viewState()/setViewState()`
  (default = the plain-text behaviour that used to live in `editor_area.cpp`);
- `EditCoreProvider` overrides them: `KTextEditor::View::cursorPosition()`/`setCursorPosition()` and
  `firstDisplayedLine(View::RealLine)`/`setScrollPosition(Cursor(line,0))`, with the scroll applied
  immediately **and** on the next event-loop turn (a hidden tab's viewport is not laid out yet — the same
  trap the plain-text path documents);
- the archive is provider-gated: `applyPositions` is false when the archive's provider differs from the
  one restoring, so a line number is never replayed into an editor that numbers lines differently.
The probe runs on the **real** provider: `after.caret-line — now=45 want=45`,
`after.caret-column — now=5 want=5`, `after.scroll-line — now=25/26 want=25/26`, with `branch`-level
`archive.caret-and-scroll-match-live-view` proving the archive itself carried those numbers.

### T51 — BOTH tabs are now proven on the shipped provider, with a fail-first control

T39's review found this report proved only **B** (the active tab) on KTextEditor; **A** — the *hidden* tab,
which is exactly the deferred-scroll case §2 describes — was only "present and ordered". Fixed by extending
the same probe (option (b): one probe, one script, no second build path to rot):

- before the restart A is activated, given a distinctive caret/scroll (`12:3`, first line 6) and captured
  while it is the VISIBLE tab (`before.a-position`, `19-before-restart-a-active.png`);
- after the restart (B active, A hidden) the probe activates tab 0 and reads A's **real
  `KTextEditor::View`**: `after.a-file`, `after.a-view`, `after.a-caret-line — now=12 want=12`,
  `after.a-caret-column — now=3 want=3`, `after.a-scroll-line` (6 offscreen / 5 under the taller Xvfb
  viewport, compared against the value captured in the same run), plus
  `21b-after-restart-a-active.png`;
- **fail-first control** (`--corrupt-a-position`): the archive's A entry has its `caret` and `scroll`
  destroyed before the restart, and the same three assertions go **RED** —
  `FAIL after.a-caret-line — now=0 want=12`, `...column — now=0 want=3`, `...scroll-line — now=0 want=6`,
  `SESSION-PROBE failures=3`, rc=1 (`13-fail-first-offscreen.log`). The pipeline fails if that control ever
  stops going red, so "restore works" can no longer be confused with "the probe never looked".
- **Result: A (hidden tab) and B (active tab) are both proven on the shipped KTextEditor provider.**
  This is the second time a demo-provider-only claim nearly shipped; the check that catches it is the
  activation-and-read, not the tab-list equality.

Chosen option (the task asked for one): **(b) extend `probe/main.cpp`**. The T12/T19 probe already links the
product's own static libraries and drives the real provider; repairing the separate
`container-run.sh VEDIT_PROBE_KEDITOR` mode would duplicate the same build (its include/link set would need
to be maintained in parallel, and the reviewer already reproduced three successive breakages in it).

## 3. Unsaved buffers are never lost (the ordering, proven)

`ApplicationShell::confirmUnsavedDocuments()` is now **public** and is the one rule for both a window
close and a one-click restart:
```
confirmUnsavedDocuments() -> false: ABORT, write no archive      -> true: saveSession() -> restart
```
- offscreen (nobody to ask): `dirty.guard-cancels` + `dirty.archive-not-written` + `dirty.disk-not-written`
  + `dirty.window-stays`; a clean buffer then passes (`dirty.clean-close-allowed`).
- Xvfb/xcb with the REAL dialog (`shots/22-unsaved-restart-prompt.png`): **Cancel** writes neither the
  archive nor the file (`dirty.archive-not-written`, `dirty.disk-not-written`); **Save** writes the buffer
  (`dirty.save-wrote-disk`) and only then `saveSession()` succeeds (`dirty.archive-written-after-save`).
The archive therefore never has to carry unsaved text: a dirty document is saved (or the user cancelled)
BEFORE it is written — and an untitled tab is not archived at all, which is why the guard, not the
archive, is what protects it.

## 4. Corrupt / missing archive

- `corrupt.clean-start` + `corrupt.diagnostic` + `corrupt.visible-message` — a garbage `session.json`
  starts a clean shell AND puts the reason on the status bar
  (`Session could not be restored (invalid session …: unterminated object) — starting clean`), never a
  silent reset.
- `missing.clean-start` + `missing.no-alarm` — a missing file is the ordinary first start and reports
  nothing.

## 5. Commands, observed output, exit codes (pinned image `vedit-qt6-build:local`)

```
# lane probe (the A/B scenario; real shell + real KTextEditor provider)
bash evidence/i3-session/probe/run.sh
→ SESSION-PROBE failures=0 platform=offscreen
  SESSION-PROBE failures=0 platform=xcb
  SESSION-PROBE-OK            (rc=0)
  logs: 10-probe-offscreen.log, 11-probe-xcb.log, 12-probe-run.log,
        13-fail-first-offscreen.log (T51: same run with A's archived position destroyed -> RED),
        shots/20-before-restart.png, 21-after-restart.png, 22-unsaved-restart-prompt.png,
        archive sample 30-session.json

# product level (40-product-gates.log, one block with the binary sha)
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/i3-selfcheck
→ APP-SHELL-SELFTEST ok=1 checks=245 failures=0 ; EDIT-CORE-SELFTEST ok=1 checks=67 failures=0
  SELFCHECK_EXIT=0   (binary sha256 4c2cc2e6…ca2b90)
ctest --test-dir target/qt6-build
→ 97% tests passed, 1 tests failed out of 37
  27 - vedit-workspace-search-selftest (Failed)   ← NOT this lane: see §6
  CTEST_EXIT=8
```
Build: `cmake --build target/qt6-build --target vedit-app` → `APP-BUILD-OK` (warnings-as-errors).

## 6. Residual uncertainty / what this report does NOT prove

1. **ctest 36/37**: the sole red is `vedit-workspace-search-selftest`, whose cached `view`/`document`
   predates multi-tab panes; the fix is approved under my T20 scope (A-2) and is not a session defect.
2. **T18's restart command is not exercised here**: the one-click restart belongs to the other lane. This
   lane proves the guard + archive ordering it must call (§3) and documents the contract in
   `app_shell.h`; the actual "立即重启" button is T18's acceptance.
3. **Window POSITION under a WM-less Xvfb** comes back as `1,23` instead of `0,0` (no window manager to
   honour the move); the size round-trip is exact. The check allows a 40 px position tolerance and says
   so in its detail string.
4. **Unsaved buffers are saved-or-refused, never archived**: "the change is still there after a restart"
   is true because the restart path refuses to proceed (or the user saved) — not because the archive
   carries dirty text. That is the documented design (requirements C13 + the user's data-loss rule).
5. **Untitled tabs are not archived** (no path); their content is protected only by the close/restart
   guard. There is no Save-As yet (item 4's lane).
6. The **project file set** is proven through `project-model.json` (the project lane's store, extended to
   schema v2 with `filelists`/`variables`); the session archive stores only the project root, which is the
   key that store resolves against.
