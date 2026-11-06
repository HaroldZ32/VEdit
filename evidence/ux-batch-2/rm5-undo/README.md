# RM-5a — repeated Ctrl+Z blanks the editor (BLOCKER): mechanism, fix, evidence

- **Task**: T10 (board), branch `feat/ux-batch-2`, workspace `/home/haroldzhao/MyProj/DshProj/VEdit`.
- **Report**: 在撤无可撤的情况下，再按 Ctrl+Z 会直接把当前显示的文件清空；多个 Ctrl+Z 之后文件变成一片大白，
  再按 Ctrl+Z 没反应，编辑/撤销都没反应；关闭标签选“丢弃”再打开，磁盘上的文件内容完好。
- **Verdict**: **REPRODUCED, MECHANISM PROVEN, FIXED, REGRESSION RED→GREEN.**
  The buffer (not only the view) was emptied by KTextEditor's own **undo of the file LOAD**, because the
  product opened files with `KTextEditor::Document::setText()`, which is an **undoable step**.
  The view was never detached, no document was re-created, and there is no "reset on empty stack" branch
  in our code — those hypotheses are disproven by the measurements below.

## 1. The mechanism (file:line)

| # | Where | What it does |
|---|-------|--------------|
| 1 | `src/editor/kernel/ktexteditor_provider.cpp:167` | `openFile()` calls `loadFileIntoDocument(view->document(), …)` |
| 2 | `src/editor/kernel/document_io.cpp:47` (**pre-fix**) | `document->setText(QString::fromUtf8(data))` — **the whole file load becomes ONE undo step from the initial empty buffer** |
| 3 | `src/app/editor_area.cpp:916` | the pane opens a file through `provider_->openFile(...)` |
| 4 | `src/app/app_shell.cpp:729-734` | `edit.undo` handler → `EditorArea::triggerActiveEditorAction` |
| 5 | `src/app/editor_area.cpp:832-856` | `triggerEditorAction()` finds `edit_undo`/`undo` and `action->trigger()`s it — KTextEditor's own undo |
| 6 | `src/app/app_shell.cpp:524-529` | the shell's `edit.undo` command has **no key sequence**: Ctrl+Z stays KTextEditor's own binding (T80 rule) |

Consequence chain:

1. A freshly opened file has exactly **one** undo entry — the load. KTextEditor's undo of it returns the
   buffer to the pre-load state, i.e. **empty** (measured: `ZERO A1 one_undo text=0`).
2. So **a file that was never edited goes blank on the very first Ctrl+Z** (measured at the pane level, both
   dispatch paths — see §3 red run). The user's "撤无可撤" is this: after undoing everything *they* typed,
   one hidden step remained (the load), and the next press consumed it.
3. Once the buffer is empty the undo stack is empty too: KTextEditor disables its `edit_undo` action,
   `triggerEditorAction()` returns `false` (`editor_area.cpp:840-842`) and the shell's toolbar/menu entry is
   greyed (`app_shell.cpp` `syncEditorCommandStates`). Ctrl+Z is a no-op — "再按 Ctrl+Z 也没反应".
4. The **file on disk was never touched** (nothing was saved), which is why close + discard + reopen showed the
   content again. The *in-memory document* was empty; it was **not** replaced or detached.

No destructive branch exists on our side: `grep` over product sources finds no `document->clear()`,
`setText({})` / `setPlainText("")` / "restore snapshot" on any undo path; `triggerEditorAction()` has no
else-branch that writes. Redo with nothing to redo is a no-op (`ZERO B2`, and the regression's
`zero-edit.redo-*` checks) — matching the user's "redo is fine" and ruling out a shared
"stack-exhausted" branch.

### 1.1 Ruled out, and what rules each one out

| Candidate seam (from the task card) | Verdict | Evidence |
|---|---|---|
| app-level `eventFilter` → `activatePaneContaining` → `setActivePane` | **not involved** | the red run's `*.editor-identity` checks pass on every press: the active pane, the editor widget and the document pointer never change during the undo |
| editor-provider / tab switch (`editor_` lifetime, `setProvider`) | **not involved** | `setProvider()` is only called by `ApplicationShell::setEditorProvider()` (`app_shell.cpp`), never on undo; identity checks pass |
| multi-cursor layer (`src/editor/kernel/multi_cursor.cpp`) | **not involved** | it has no undo/redo handling at all (`grep -n "undo" src/editor/kernel/multi_cursor.cpp` → none) |
| document reload/replace (`src/workspace/search/document_sync.cpp`) | **not involved** | `refresh()` refuses a modified document and a token mismatch (`document_sync.cpp:151-157`), so it never replaces a live buffer behind the user |
| "reset to the saved state" fallback on refused undo | **does not exist** | `EditorPane::triggerEditorAction()` returns `false` when KTextEditor's action is disabled (`editor_area.cpp:839-845`); no write follows |

The destructive step is KTextEditor's own, correct undo of an edit *we should not have created*: the load.

## 2. Instrumentation (what was measured, with the probe sources)

All probes are standalone KTextEditor probes built and run inside the pinned Qt6 image
(`bash evidence/ux-batch-2/rm5-undo/probe/run-probe.sh <probe.cpp> [--xcb]`; the build output stays in the
container's `/tmp`).

| Probe | Question | Measured result (log) |
|---|---|---|
| `kt_undo_probe.cpp` | what does undo do after a `setText()` load + edits? | `after_load text=32`; `undo=3 text=0`; `view_doc_same=1` every press; typing after undo lands (`before=0 after=5`). **Blank = buffer emptied, view still bound.** → `log-kt_undo_probe.txt` |
| `zero_edit_undo_probe.cpp` | zero edits, one undo / one redo (the user's minimal repro) | `A1 one_undo text=0` (**blank**), `A2 one_undo_one_redo text=32`, `B1 openUrl+undo after=32`, `B2 openUrl+redo after=32` → `log-zero_edit_undo_probe.txt` |
| `openurl_probe.cpp` | is `KParts::ReadOnlyPart::openUrl()` a usable local-file open, and does it leave the load as the undo floor? | offscreen: `openUrl returned=1 elapsed_ms=4 text=32 modified=0`; `undo=1..8 floor_hit=1` (text always 32, never 0); typing lands. xcb: same (`elapsed_ms=7`). → `log-openurl_probe-offscreen.txt`, `log-openurl_probe-xcb.txt` |
| `openurl_modified_probe.cpp` | why did the kernel selftest hang on the openUrl path? | modified document + `openUrl` → **HANG** (rc=124, modal `closeUrl()` query); `setModified(false)` first → returns at once. → §2.1 |
| `doc_api_probe.cpp` | is there a public "clear undo history" API? | no: only `undo()/redo()`, no `setUndoMergeAllEdits`, no clear-history slot → `log-doc_api_probe.txt` |
| `render_after_undo_probe.cpp` | after the blank, does the VIEW repaint typing (is it "dead")? | `after_load ink=23568`; `after_undo_to_empty text=0 ink=20988 changed_vs_loaded=8133`; `after_typing text=8 changed_vs_empty=495` → **the view repaints the typed text; it is not dead** → `log-render_after_undo_probe.txt` |

### 2.1 A trap found on the way: `openUrl()` prompts on a modified document

`KParts::ReadWritePart::openUrl()` calls `closeUrl()` first, and `closeUrl()` queries the user
("save changes?") when the document is modified. In every headless caller that is a **hang**: the kernel
selftest (`editcore.open.file`) hung exactly there after the first version of the fix. `setModified(false)`
before the load fixes it — semantically right, because a load REPLACES the buffer and the pane only ever
loads into a fresh/unmodified tab. Reproduced deterministically:
`modified` → rc=124; `cleared` → `openUrl_returned=1 text=22 modified=0`.

## 3. The fix

`src/editor/kernel/document_io.cpp` — `loadFileIntoDocument()` now loads through the part's documented
open entry instead of `setText()`:

```cpp
    document->setModified(false);   // openUrl() -> closeUrl() must not prompt (headless hang)
    if (!document->openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) { … }
```

`openUrl()` makes the loaded text the **undo floor**: undo stops at the state the file was opened with and
never walks into the empty pre-load buffer (measured in §2 `B1` and in the green run). The existing
pre-checks (exists / regular file / readable / NUL-byte binary refusal) are unchanged and still run BEFORE
any open; `document_io.h` now records the real contract (it previously documented "reads + setText" and the
KParts-spins-under-offscreen concern, which did **not** reproduce for local files: 4-7 ms under offscreen
and xcb).

Deliberately **not** changed: undo/redo stay enabled and unchanged (the shell command still drives
KTextEditor's own action); no document is re-created or re-bound; no new event loop or dialog can be entered
by the open path.

## 4. Red → green

New regression: `src/editor/kernel/undo_view_lifetime_test.cpp` (target
`vedit-undo-view-lifetime-tests`, ctest id of the same name). It drives the **product seam**
`EditorArea → EditorPane → EditCoreProvider::openFile → EditorPane::triggerEditorAction` plus the real
Ctrl+Z key delivered to the editor's inner widget, and asserts: identical view/document pointers, the
buffer never leaves the opened text, the tab still shows the file, typing after the undo walk still lands,
and the zero-edit undo/redo pair is a no-op.

**RED** (`red-run.log`, fix reverted to `setText()` only):

```
CHECK zero-edit.undo-command-buffer-unchanged FAIL (ran=1 text=0 opened=51)
CHECK zero-edit.undo-key-buffer-unchanged     FAIL (text=0 opened=51)
CHECK zero-edit.undo-command-identity PASS
CHECK zero-edit.undo-key-identity PASS
CHECK zero-edit.redo-command-buffer-unchanged PASS
CHECK zero-edit.redo-key-buffer-unchanged PASS
...
CHECK undo.3.buffer-not-empty FAIL (press=3 text=0)
CHECK undo.floor-is-opened-text FAIL (text=0 opened=51)
CHECK key-undo.1..4.buffer-not-empty FAIL (text=0)
SUMMARY checks=66 failures=19
```

Both dispatch paths are destructive (the shell command goes through the same KTextEditor action, and the
key path *is* KTextEditor's binding) — the user pressed the key. Identity checks pass **in the red run**,
which is the proof that the view/document binding was never the problem.

**GREEN** (`green-run.log`, fix applied):

```
CHECK zero-edit.undo-command-buffer-unchanged PASS
CHECK zero-edit.undo-key-buffer-unchanged PASS
CHECK zero-edit.redo-command-buffer-unchanged PASS
CHECK zero-edit.redo-key-buffer-unchanged PASS
...
CHECK undo.floor-is-opened-text PASS
CHECK key-undo.floor-is-opened-text PASS
CHECK edit.typing-after-undo PASS / edit.typing-after-key-undo PASS
SUMMARY checks=65 failures=0
```

`ctest` (`ctest-targeted.log`):

```
1/4 vedit-app-shell-selftest .........  Passed
2/4 vedit-edit-core-selftest .........  Passed
3/4 vedit-undo-view-lifetime-tests ... Passed   (SUMMARY checks=65 failures=0)
4/4 vedit-search-replace-tests .......  Passed   (the other consumer of loadFileIntoDocument)
100% tests passed, 0 tests failed out of 4
```

## 5. Bounds — what this evidence does NOT prove

1. **The "editing is dead" half of the report did not reproduce.** After the pre-fix blank, typing still
   lands in the buffer (`kt_undo_probe` `C_type_after_undo landed=1`) and the view repaints it
   (`render_after_undo_probe` `changed_vs_empty=495`). What the user saw as "no response" is reproduced as
   **the exhausted undo stack** (the editor action is disabled → menu/toolbar greyed → Ctrl+Z does nothing)
   combined with a blank page. The fix removes the blank state that made the editor look dead; a *separate*
   typing defect, if one exists, is not covered here.
2. **No document/view detach exists to fix.** In the red run both identity checks pass and the blank is
   `document()->text().size()==0`. The earlier "the view was detached from a live document" framing is not
   supported by measurement.
3. **Behaviour change at the seams:** the document URL is now set by the load (KTextEditor detects encoding
   and mode from it; `EditorDocument::filePath()` now reports it). A reload through the same helper
   (`src/workspace/search/document_sync.cpp` `refresh()`, which only reloads an *unmodified, unchanged*
   document) now resets the undo history to the reloaded content instead of adding an undoable reload step —
   standard "reload from disk" semantics, but it is a change; its lane's tests pass.
4. **Environment**: the regression runs offscreen (`QT_QPA_PLATFORM=offscreen`); the KTextEditor probes were
   also run under `xvfb-run` + xcb, but the full product was not driven with a real mouse/keyboard on the
   user's machine, and the **full gate** (`scripts/gates.sh`, `build/gates/qt6-cxx.sh`) was **not** run here
   (the task leaves it to the captain). The Qt6 build was exercised in the pinned image
   (`vedit-qt6-build:local`), the app binary was relinked, and `vedit-app --selfcheck` passed.
5. Only local-file opens were re-measured; non-local URLs are not used by the product.
6. `crates/vd-deeplint/**` was not touched.

## 6. Reproduce / re-run

```bash
# the whole regression (build + run) inside the pinned image
bash evidence/ux-batch-2/rm5-undo/rebuild-test.sh --run
# or the ctest entries
docker run --rm -v "$PWD:/vedit-build" -e HOME=/tmp vedit-qt6-build:local bash -lc \
  'cd /vedit-build/target/qt6-build && QT_QPA_PLATFORM=offscreen ctest -R vedit-undo-view-lifetime-tests -V'
# the standalone probes (build + run, offscreen; pass --xcb for a real X server)
bash evidence/ux-batch-2/rm5-undo/probe/run-probe.sh zero_edit_undo_probe.cpp
bash evidence/ux-batch-2/rm5-undo/probe/run-probe.sh openurl_probe.cpp --xcb
```

To see the pre-fix behaviour again, replace the two lines in `loadFileIntoDocument()` with the old
`document->setText(QString::fromUtf8(data))` and run the regression: the `zero-edit.*-buffer-unchanged`
and `undo.*-buffer-not-empty` checks fail.

## 7. Files changed

- `src/editor/kernel/document_io.cpp` — the fix (openUrl + setModified(false) before the load).
- `src/editor/kernel/document_io.h` — the documented contract updated (it described the old read+setText path).
- `src/editor/kernel/undo_view_lifetime_test.cpp` — **new** regression.
- `src/editor/CMakeLists.txt` — **scope note**: one block appended to register the new test target
  (`vedit-undo-view-lifetime-tests`, ctest id of the same name). This file is outside the declared write
  scope; registering the regression needs it, and nothing else in that file was touched.
- `evidence/ux-batch-2/rm5-undo/` — this README, `red-run.log`, `green-run.log`, `ctest-targeted.log`,
  `rebuild-test.sh`, `probe/` (probe sources + captured logs).
