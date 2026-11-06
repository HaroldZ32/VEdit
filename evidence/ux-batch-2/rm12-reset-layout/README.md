# RM-12 — Reset Layout / New Empty Layout must not destroy unsaved documents

Found while rewriting the manual (the “需要你/别行处理” section of
`evidence/ux-batch-2/rm8-manual/README.md`), **not yet hit by the user**.

**Status: FIXED and regression-locked.** `View → Reset Layout` (Ctrl+Alt+R) and
`File → New Empty Layout` (Ctrl+Alt+N) — and, same code path,
`View → Mixed Split` — now ask the shell’s existing unsaved-document question
before the layout is destroyed. A cancelled answer leaves every pane, every
document and the layout exactly as they were. The same audit found one more
silent data-loss path, **File → Quit / Ctrl+Q**, and closed it too.

---

## 1. Confirmed shape (the red run)

`EditorArea::resetLayout()` (`src/app/editor_area.cpp:1849`) calls
`clearTree()` (`:1892`), which deletes **every pane** — and the pane owns its
editor widget, so with the product editor the KTextEditor document dies with it
(`src/editor/kernel/ktexteditor_provider.cpp:103`: “One document per pane, and
the document dies with its view”). The two command handlers used to call it
directly:

* `file.new-layout` — `src/app/app_shell.cpp:720-723` (pre-fix)
* `view.layout.reset` — `:758-762` (pre-fix)
* `view.layout.mixed` — `:755-757` (pre-fix) reaches the same reset through
  `applyMixedSplitPreset()`
* `file.quit` — `:749` (pre-fix) called `QApplication::quit()`, which **ends the
  process without running `closeEvent()`**, so the window-close guard never ran

The shell already had the confirmation: `ApplicationShell::confirmUnsavedDocuments()`
(`src/app/app_shell.cpp:1613`) collects **every** dirty document across all panes
and asks once (Save All / Discard All / Cancel; the single-document case is the
same `promptUnsaved()` dialog the tab/pane close uses). It is what
`closeEvent()` (`:1748`) and the one-click language restart (`:1793`) already use.
The four handlers above simply bypassed it.

RED run — the final test source against the current tree with **only** the four
guard blocks removed (generated at run time by
`probe/run-prefix-red.sh`, `PREFIX guards_removed=4`,
`sha256 app_shell.cpp 0cfb5b9d…` / prefix `bf02684f…`):

```
SHAPE view.layout.reset            prompt_calls=0 panes=1 document=pane was destroyed/replaced
SHAPE file.new-layout              prompt_calls=0 panes=1 document=pane was destroyed/replaced
SHAPE view.layout.mixed            prompt_calls=0 panes=3 document=pane was destroyed/replaced
SHAPE view.layout.reset(2 dirty)   prompt_calls=0 prompt_docs=0 panes=1
SHAPE view.layout.reset(save)      prompt_calls=0 panes_after=1
SHAPE view.layout.reset(ktexteditor) prompt_calls=0 document=LOST/REPLACED
SHAPE file.quit(dirty)             prompt_calls=0
…
CHECK rm12.save.file-written FAIL module m; endmodule        <- the edit was never written
CHECK rm12.real-editor.document-survives FAIL text intact:   <- KTextEditor document gone
RM12-RESET-LAYOUT-SELFTEST failures=18                       <- exit 1
                                                        [logs/03-red-prefix.log]
```

That is the defect in one line: **`prompt_calls=0`, the dirty pane/document is
gone**. There was no dialog to decline — the user never got a say.

---

## 2. The fix (4 handler bodies in `src/app/app_shell.cpp`)

Each destructive command now asks the SAME confirmation the window close asks,
before touching the layout, and returns without doing anything when the answer
is Cancel:

```cpp
// file.new-layout
if (!confirmUnsavedDocuments()) {
    return;
}
editorArea_->resetLayout();

// view.layout.mixed
if (!confirmUnsavedDocuments()) {
    return;
}
applyMixedSplitPreset();

// view.layout.reset
if (!confirmUnsavedDocuments()) {
    return;
}
editorArea_->resetLayout();

// file.quit  (was: []() { QApplication::quit(); })
if (!confirmUnsavedDocuments()) {
    return;
}
QApplication::quit();
```

No second dialog was invented: `confirmUnsavedDocuments()` is the one existing
all-panes form of `promptUnsaved()`/`promptUnsavedBatch()`, already used by
`closeEvent()` and the language restart. A clean layout (no dirty document) is
answered by `confirmUnsavedDocuments()` returning `true` **before any prompt is
constructed** — a reset with nothing to lose is still instant and silent
(proved by call count 0 below).

No change was needed in `editor_area.{h,cpp}`: the destruction path itself is
correct, the commands were the missing gate. `src/app/app_shell.cpp` changes are
limited to those four handler bodies (no header change; no other lane’s code
touched).

---

## 3. Family audit — every path that destroys/replaces panes or documents

| path | reachable as a user command? | destroys documents? | status |
|---|---|---|---|
| `view.layout.reset` | View menu / Ctrl+Alt+R | **yes**, all panes | **guarded** (`confirmUnsavedDocuments`) |
| `file.new-layout` | File menu / Ctrl+Alt+N | **yes**, all panes | **guarded** |
| `view.layout.mixed` | View menu / Ctrl+Alt+M | **yes**, via `applyMixedSplitPreset()` → `resetLayout()` | **guarded** |
| `file.quit` | File menu / Ctrl+Q | **yes** — process exit, and `QApplication::quit()` never runs `closeEvent()` | **guarded** (new finding, closed here) |
| `file.close-pane` / pane ✕ | View menu / Ctrl+W / pane header | yes | already guarded by `confirmPaneClose()` (`app_shell.cpp:1564`), the per-document tab-close prompt; test proves the dirty document survives |
| window close (✕) | window manager | yes | already guarded: `closeEvent()` → `confirmUnsavedDocuments()` (`:1748`) |
| language restart | Preferences | yes (relaunch) | already guarded (`:1793`), writes nothing on Cancel |
| tab close (tab ✕) | tab strip | yes (one tab) | already guarded: `promptUnsaved()` through `setOnTabCloseRequested` (`:157`) |
| `applyHorizontalSplitPreset` / `applyVerticalSplitPreset` | **no** — no `addCommand`/`setHandler` registers them; only `main.cpp --selfcheck` calls them | would, via `resetLayout()` | not user-reachable; left unchanged. `grep` proof in `logs/05-family-audit.txt` |
| `EditorArea::restoreLayout()` | **no** — only `ApplicationShell::restoreSession()` (`app_shell.cpp:1137`) from `startSession()`, which runs once at startup (`sessionStarted_`), before the first user document exists | `clearTree()` only on the restore path | not reachable with live unsaved documents |
| `ApplicationShell::setEditorProvider()` / `EditorArea::setProvider()` | **no** — the product call site (`main.cpp:1023`) installs the real provider before `startSession()` (no document exists yet); every other caller is a test/lane selftest | **yes** — `EditorPane::setProvider()` recreates every editor and re-reads from disk | not a user command; documented as an API-only path (a future command that swaps providers must add the same confirmation) |
| project switch (`file.open-folder`, `file.switch-project`) | File menu | **no** — `setProjectRoot()` only re-broadcasts the root (`app_shell.cpp:379-405`); panes/documents stay | proven by code path |
| tab open / File > New / split / focus | menus, toolbar | no (adds) | not destructive |

`EditorArea::clearTree()` has exactly two callers: `resetLayout()` and
`restoreLayout()` (`logs/05-family-audit.txt`).

---

## 4. Green run (final test source, ctest)

```
$ bash evidence/ux-batch-2/rm12-reset-layout/probe/run-prefix-red.sh      # red, exit 1
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build/target/qt6-build \
      -e QT_QPA_PLATFORM=offscreen -e XDG_RUNTIME_DIR=/tmp/xdg-rm12c \
      vedit-qt6-build:local bash -lc 'ctest -R vedit-app-reset-layout-guard-selftest -V'
17: SHAPE view.layout.reset            prompt_calls=1 panes=1 document=intact
17: SHAPE file.new-layout              prompt_calls=1 panes=1 document=intact
17: SHAPE view.layout.mixed            prompt_calls=1 panes=1 document=intact
17: SHAPE view.layout.reset(2 dirty)   prompt_calls=1 prompt_docs=2 panes=2
17: SHAPE view.layout.reset(discard)   prompt_calls=1 panes_before=1 panes_after=1
17: SHAPE view.layout.reset(clean)     prompt_calls=0 panes_after=1
17: SHAPE file.new-layout(clean)       prompt_calls=0 panes_after=1
17: SHAPE view.layout.reset(save)      prompt_calls=1 panes_after=1
17: SHAPE view.layout.reset(ktexteditor) prompt_calls=1 document=same-object
17: SHAPE file.close-pane(dirty)       panes=1
17: SHAPE file.quit(dirty)             prompt_calls=1
17: SHAPE file.quit(clean)             prompt_calls=0
17: RM12-RESET-LAYOUT-SELFTEST failures=0
1/1 Test #17: vedit-app-reset-layout-guard-selftest ...   Passed    0.12 sec
100% tests passed, 0 tests failed out of 1          exit 0   [logs/04-green-ctest.log]
```

Every scenario drives the **real command**: the test takes the command
registry’s `QAction` (the object the menu entry, the toolbar button and the
keyboard shortcut are all bound to; `CommandRegistry::action()` +
`triggered`) and triggers it. `resetLayout()` is never called by the test.

What each scenario proves:

* **Cancel keeps everything** — Reset Layout, New Empty Layout and Mixed Split:
  exactly one prompt, the pane id still exists, the text is byte-identical, the
  buffer is still modified, and the layout JSON (pane ids, ratios, active pane,
  sessions) is **unchanged** (compared before/after).
* **One prompt for many dirty buffers** — two dirty panes → one provider call
  whose list has both documents; both survive Cancel.
* **Cancel is not a no-op for the fix** — answering Discard resets the layout
  (the confirmation is wired, not blocking everything).
* **Clean reset stays instant and silent** — two panes, no dirty document,
  `prompt_calls=0`, the layout resets (both commands).
* **Save branch** — a dirty *file-backed* document answered with Save is written
  to disk through the same confirmation, then the reset happens.
* **The real product editor** — a real KTextEditor document (lifetime = its
  view) is still the **same object** with its text after Cancel.
* **Existing guards stay honest** — `file.close-pane` with a dirty buffer keeps
  the document (headless answers Cancel, never “discard because no dialog”).
* **File → Quit** — dirty: one prompt; clean: no prompt.

---

## 5. Bounds

1. **Guard placement.** The confirmation is added at the command-handler level
   (the declared write scope). A direct programmatic call to
   `EditorArea::resetLayout()` — used by `main.cpp --selfcheck` and by the
   `applyHorizontalSplitPreset`/`applyVerticalSplitPreset` helpers that no
   command registers — still resets without asking. Those call sites have no
   user documents to lose, and they are listed in the audit; a future command
   that adds a preset must route through `confirmUnsavedDocuments()` like the
   four handlers now do.
2. **No real modal dialog was shown.** The test answers through the shell’s
   public `setUnsavedPromptProvider()` seam (the same seam the window-close
   tests use); the red run’s `prompt_calls=0` is the observable proof that the
   pre-fix code never asked. The actual dialog text/buttons are the existing
   `promptUnsaved()`/`promptUnsavedBatch()` ones, unchanged.
3. **File → Quit now asks but still does not write the session archive.**
   Pre-fix it did not write it either; the window ✕ path does (`closeEvent()`).
   This is a session-restore nicety, not data loss — the user’s Save/Discard
   answer covers the documents. Not changed here to keep the diff to the guard.
4. **`QApplication::quit()` in the clean-quit scenario is a no-op without a
   running event loop**, so the test asserts the prompt call count (0) rather
   than observing process exit; the actual exit path is unchanged (one extra
   guard in front of the same call).
5. **The `setEditorProvider()` path** (replaces every editor, re-reads from
   disk) is not user-reachable today (audit above) and is not guarded; it is
   recorded here as the next place a confirmation would be required if a
   command ever exposes it.

---

## 6. Files

| path | role |
|---|---|
| `src/app/app_shell.cpp` | the fix: four command handler bodies (Reset Layout, New Empty Layout, Mixed Split, Quit) |
| `src/app/reset_layout_guard_selftest.cpp` | new regression check (real QAction dispatch + prompt seam) |
| `src/app/CMakeLists.txt` | target + ctest entry `vedit-app-reset-layout-guard-selftest` |
| `logs/03-red-prefix.log` | RED run (final test source, pre-fix handlers) |
| `logs/04-green-ctest.log` | GREEN run through ctest |
| `logs/02-green.log` | GREEN run of the binary directly (same scenarios, readable SHAPE lines) |
| `logs/05-family-audit.txt` | the audit greps behind §3 |
| `logs/00-source-sha256.txt` | hashes of the sources exercised |
| `prefix/unguard.diff` | exact inverse patch of the fix (what the red run removed) |
| `probe/run-prefix-red.sh` | reproducible red run: strips only the four guards into a scratch copy, compiles/links the selftest, runs it |
