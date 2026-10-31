# Verdict — save-binding (T72, independent review of T71)

captured: 2026-10-31T11:52Z
git_head: 95c496e682e3841add1a081edd78d29c34957aac
branch: feat/qt6-ktexteditor-migration
reviewer: Reviewer (instance 2), session b0af789c-16ef-42ef-ad05-9e137a41d604
work brief: T71 (`.mpd/team/contracts/T71.json`) + T72 dispatch text
review contract: **none frozen** — no `fix-*`/`rev-*` contract for T71/T72 in
`.mpd/cellwork/cell-20261031T085543161Z/contracts/`, no verification loop for
T72 in `.mpd/verify/loops/`. No `mpd_verify_record`; this verdict is
**ADVISORY** and is delivered as this file + the completion note.

## Verdict: PASS

All four required properties hold in the code and in a real headless run.
No test or gate was modified by T71; the save entry is one shared path, the
atomic-save chain is intact, and failures are visible. Two real-interaction
paths (OS keyboard routing, mouse click on the menu) are
**UNVERIFIABLE-IN-CONTAINER** and remain the user's stop-1 check.

## Documents / sources the verdict rests on

- `.mpd/team/contracts/T71.json` and the T72 dispatch text — expected wiring,
  provider requirement, end-to-end disk evidence, atomic-save reuse, failure
  visibility, and the regression baseline.
- `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` — AC-S1.1 (editing
  kernel in the product, including save) and C10 (never modify a test/gate).
- `AGENT.md` §4/§5 (declared gates; never weaken a gate).
- `src/editor/kernel/atomic_save.h` — the documented atomic-save contract
  (tmp → write → fsync(file) → close → rename → fsync(dir); fault injection is
  test-only).
- T71's evidence package `evidence/save-binding/{REPORT.md,01-design-notes.md,02-selfcheck-save.log,03-root-ctest.log,05-typecheck.log,06-changed-files.txt}`.

## Evidence produced by the reviewer (commands + observed output)

| # | check | observed |
| --- | --- | --- |
| 1 | `git diff -- src/app/app_shell.h src/app/app_shell.cpp src/app/main.cpp`; `grep -rn 'Ctrl+S' src/` | one command declaration: `file.save`, text `&Save`, menu `file`, `QKeySequence("Ctrl+S")` (`app_shell.cpp:221-222`); **exactly one** `Ctrl+S` binding in `src/`; handler `commands_->setHandler("file.save", …saveActiveDocument())` (`app_shell.cpp:282`); `saveActiveDocument()` takes `editorArea_->activePane()` and calls `pane->saveFile(&error)` (`app_shell.cpp:473-497`) — a single shared entry, no second save implementation. `file.save-session` is a different command (session file, not the document). |
| 2 | menu/action construction: `src/app/command_registry.cpp:145-176`, `src/app/app_shell.cpp:329-345` | `CommandRegistry::add()` creates the **QAction**, sets the shortcut from the store/fallback, connects `QAction::triggered` to the registered handler; `buildMenuBar()` adds **that same QAction** into the `file` menu. T71's selftest also finds `saveAction` inside a `menuBar()` menu and triggers it — the menu and Ctrl+S are the same QAction. |
| 3 | provider swap evidence: `src/app/main.cpp` (selftest block) | The KTextEditor checks run **after** `shell->setEditorProvider(vedit::editor::makeEditCoreProvider())` on the live shell; assertions are made with the real provider: `editor.provider.id == ktexteditor`, `isEditCoreWidget`, `documentFor(editPane->editorWidget()) != nullptr`. **Not demo-only.** The demo-provider checks are a separate, explicitly labelled provider-agnostic prelude. |
| 4 | end-to-end path (shortcut): `src/app/main.cpp` selftest, my run in the pinned image | `document->setText(...)`; `shell->activateWindow()`; `QKeyEvent(KeyPress/KeyRelease, Key_S, ControlModifier)` sent to `shell->windowHandle()` → Qt shortcut map → QAction; then `readFileBytes(savePath)` equals the new bytes; `!document->isModified()`; `currentFilePath()` unchanged. `save.shortcut.disk-bytes`, `save.shortcut.modified-cleared`, `save.shortcut.path-unchanged` all PASS in my run. |
| 5 | end-to-end path (menu) | The selftest triggers the real `saveAction` from the File menu; disk bytes change to the second content, `currentFilePath()` unchanged, and no `*.vedit-tmp-*` residue remains. `save.menu.disk-bytes`, `save.menu.path-unchanged`, `save.no-temp-file` PASS. |
| 6 | atomic-save reuse chain | `EditorPane::saveFile` → `provider_->saveFile(editor_, error)` (`editor_area.cpp:114-123`) → `EditCoreProvider::saveFile` calls `atomicSaveText(path, document->text().toUtf8())` (`ktexteditor_provider.cpp:121-143`), clears modified only on success. `atomic_save.cpp` implements tmp `O_CREAT|O_EXCL` → write-all → `fsync(fd)` → close → `rename` → `fsync(dir)`, unlinks the temp file on any pre-rename failure. No `QFile::write` shortcut on the product path. (The demo placeholder provider's `QSaveFile` fallback is pre-existing and is not the product path.) |
| 7 | failure visibility | `saveActiveDocument()` reports failure through **status bar `Save failed: <reason>` (8 s) + `diagnostics()` + `qWarning("[app-shell] …")`**, does not clear modified, does not touch disk (`app_shell.cpp`). Selftest injects `VEDIT_ATOMIC_FAULT=write` (documented test hook) through the **real KTextEditor → atomic path**: disk unchanged, status bar contains `Save failed`, diagnostics contains `Save failed`, document still modified, then the same entry saves again after the fault is removed. All `save.failure.*` / `save.recovery.*` PASS in my run. |
| 8 | my headless run: `docker run … vedit-qt6-build:local bash -lc 'target/qt6-build/src/app/vedit-app --selfcheck …'` (QT_QPA_PLATFORM=offscreen) | `APP-EXIT=0`; `21` `save.*` CHECK lines, **21 PASS, 0 FAIL**; `APP-SHELL-SELFTEST ok=1 checks=161 failures=0`; `EDIT-CORE-SELFTEST ok=1 checks=55 failures=0`; zero `CHECK … FAIL` lines in the whole run. Binary tested: sha256 `e78440d129bae7c7e3c126c62af5ce76079e280a82759a285534d3155482099a` (rebuilt 19:44 after the evidence run; it contains all T71 changes — app_shell files are 19:32-19:33, main.cpp 19:42 at build time). `src/app/main.cpp` was edited again at 19:47:52 by a concurrent lane, after this build; unrelated to the save code. |
| 9 | root ctest: `docker run … bash -lc 'ctest --test-dir target/qt6-build --output-on-failure'` | **`100% tests passed, 0 tests failed out of 16`**, `CTEST-EXIT=0` — the 16/16 baseline is met in my run (the writer's 15/16 was an in-flight search-lane failure that has since been fixed; `#9 vedit-app-shell-selftest` passes). |
| 10 | `bash build/gates/typecheck.sh` | `[typecheck] PASS (pinned image vedit-build:local)`, `TYPECHECK-EXIT=0`; the log includes the Rust clippy half and the Qt6 configure/build-with-warnings-as-errors half. |
| 11 | checks total and no weakened assertions | baseline evidence: `evidence/edit-core-v2/04-app-selfcheck.log` → `checks=133`; `evidence/fix-shell-terminal/logs/11-app-before.log` → `checks=136 failures=1` (pre-existing other-lane failure). My run → `checks=161 failures=0` (**+28**). The only deletions in the `main.cpp` diff are the old `check()` helper signature/printf (extended with an optional detail argument, identical counting semantics); **no existing assertion line was removed or weakened**, and 21 new `save.*` assertions were added. |
| 12 | no test/gate edited by T71 | T71's own changed set (evidence `06-changed-files.txt`, mtimes 19:32-19:42) is `src/app/app_shell.{h,cpp}`, `src/app/main.cpp` (the product `--selfcheck`, not a gate file) and `evidence/save-binding/**`. The modified `build/gates/*.sh` files come from other lanes (gate-infra work) and are untouched by T71; no `tests/**` or ctest definition was changed. |

## Findings

No blocking findings.

### Non-blocking observations

- **N1 — real OS interaction is not certified here (by design).**
  `UNVERIFIABLE-IN-CONTAINER`: there is no display server. The headless path
  covers a real `QKeyEvent` through Qt's shortcut map and the real QAction; the
  OS keyboard → Qt routing and the mouse click on the File menu must be
  confirmed by the user at stop 1 on a display machine.
- **N2 — the writer's root-ctest run was 15/16; my run is 16/16.** The writer's
  failure was `vedit-workspace-search-selftest` in a lane that was in flight at
  that moment; it now passes, so the 16/16 baseline is met and no regression is
  attributed to T71.
- **N3 — literal permission-denied save was not separately exercised.** Failure
  visibility is proven through `VEDIT_ATOMIC_FAULT=write`, which runs the real
  KTextEditor → `atomicSaveText` path and the same error-reporting code;
  `atomicSaveText` also rejects a non-writable directory and reports
  `open/write/fsync/rename` errors as user-visible strings. A literal
  `chmod`-read-only run is the only untested variant of the same path.
- **N4 — shared-tree drift.** The current working tree contains other lanes'
  edits in `src/app/app_shell.{h,cpp}` (`deferSessionRestore`/`startSession`) and
  `src/app/main.cpp` (up to 19:47:52), and `build/gates/*.sh` changes; these are
  not T71's (T71's files/mtimes and its evidence set are as listed in row 12).
  The binary I tested was built at 19:44 from source that already contained all
  T71 changes, so the certification is of T71's code, not of the later unrelated
  edits.
- **N5 — process.** T72 has no frozen contract and no verification loop, so this
  verdict is advisory and cannot be recorded via `mpd_verify_record`; the
  captain owns the flow.

## Not verified / bounds

- Real X11/Wayland keyboard and mouse interaction (no display server) — must be
  checked by the user at stop 1.
- Saving from a *different* pane than the active one (the entry is defined as
  "save the active pane"; only the active-pane behaviour is asserted/required).
- A literal read-only-file save (the error-reporting path is the same as the
  injected-fault path; see N3).
