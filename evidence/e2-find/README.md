# find-replace — evidence (cell `cell-20261102T050548232Z`, task `T13`)

Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/find-replace.md`
(verifier loop `loop-20261102T063252-9bae63`).
**ADDENDUM A-7 (T13)** grants `src/editor/kernel/find_replace_selftest.cpp`.

## What landed

| path | role |
|---|---|
| `src/editor/kernel/find_replace.h` | `FindOptions` (case / whole word / regex), scoped `findMatches` / `replaceAll`, `replaceOne`, and the in-view `FindReplaceBar` |
| `src/editor/kernel/find_replace.cpp` | matching core + bottom-up single-transaction replace + the bar (Ctrl+F / Ctrl+H / F3 / Shift+F3) |
| `src/editor/kernel/find_replace_selftest.cpp` | **ADDENDUM A-7** — the lane ctest TU (real QTest key events on a real KTextEditor::View) |
| `src/editor/kernel/ktexteditor_provider.cpp` | installs the bar on every view `createEditor()` returns |
| `src/editor/CMakeLists.txt` | `vedit-editor-find-replace-selftest` target + ctest entry; standalone project now enables C (see “external red repaired”) |
| `evidence/e2-find/` | this README, the four logs below, two bar screenshots |

The two original API signatures (`findMatches(doc, needle, bool caseSensitive, ranges)` and
`replaceAll(doc, needle, replacement, bool caseSensitive)`) are kept as thin wrappers:
`edit_core_selftest.cpp` (outside this task's write scope) calls them unchanged, and its
`editcore.find.*` / `editcore.replace.*` checks still pass.

## AC-4.3 mapping (all checks below PASS in `02-find-replace-checks.log`)

| AC-4.3 item | checks |
|---|---|
| Ctrl+F opens the bar | `find.bar.ctrl-f-opens`, `find.bar.find-mode` |
| Ctrl+H opens replace mode | `find.bar.ctrl-h-replace-mode` |
| regex | `find.count.regex`, `find.count.regex-group`, `find.regex.invalid-no-matches`, `find.regex.invalid-reported` |
| whole word | `find.count.whole-word-rejects`, `find.count.whole-word-accepts` |
| case sensitivity | `find.count.case-sensitive`, `find.count.case-insensitive`, `find.count.case-sensitive-pattern`, `find.count.case-insensitive-pattern` |
| selection-scoped replace | `find.scope.in-selection`, `find.scope.line0-replaced`, `find.scope.line1-untouched` (text outside the selection is byte-identical) |
| replace all | `find.replace-all.text`, `find.replace-all.status` |
| **empty replacement deletes every match** (review follow-up) | `find.replace-empty.deletes-matches`, `find.replace-empty.single-undo` |
| **empty pattern is refused, no edit** (review follow-up) | `find.empty-pattern.no-edit` (bar status "Type to search", buffer unchanged), `find.empty-pattern.api-refuses` (`replaceAll` returns the explicit "empty" error, buffer unchanged) |
| replace-all = one undo step | `find.replace-all.single-undo`, `find.scope.replace-all-single-undo`, `find.replace-current.single-undo` (one `documentUndo` restores the pre-replace text) |
| match count + current index | `find.status.current-of-total` (`2 / 4`), `find.next.first`, `find.next.second`, `find.next.first-selection`, `find.next.second-selection` |
| F3 / Shift+F3 next / previous | `find.f3-key-navigates`, `find.shift-f3-key-navigates-back`, `find.previous.wraps-back` |
| the bar is really mounted by the provider | `find.provider.creates-editor`, `find.provider.bar-installed` |
| the bar is in the product path | `04-product-selfcheck.log`: `APP-SHELL-SELFTEST ok=1 checks=245 failures=0` (every selfcheck pane is created by `EditCoreProvider`, so every pane installed and destroyed the bar) |

## Key ownership rule (single owner, not focus-order dispatch)

The bar is the **single owner** of `Ctrl+F`, `Ctrl+H`, `F3`, `Shift+F3`:

1. It claims the sequences as `QEvent::ShortcutOverride` on the editor view. KTextEditor's
   embedded view ships its own find actions with the same sequences at a *higher* Qt
   shortcut-context priority; a competing `QShortcut` therefore either lost silently
   (Ctrl+F) or turned the key into `QAction::event: Ambiguous shortcut overload`
   (Ctrl+H / F3 — both observed before this fix). Claiming the override means the key
   arrives as a `KeyPress` and the kernel action never sees it.
2. The same event filter is installed on every widget inside the bar, so the identical
   public methods run when the focus is in the pattern/replacement fields.
3. Any kernel action still advertising one of the sequences is cleared at install/open;
   `find.keys.single-owner` fails if a second owner exists.

There is no branch on “who has focus”; both mechanisms end in `openFind` / `openReplace` /
`findNext` / `findPrevious`.

## Real-focus verification (ADDENDUM A-14) — honest before/after

**Reported as a product defect; disproved by a fail-first test; hardening applied anyway.**
The misdiagnosis's root cause: **Qt propagates an unhandled key press from KatePart's
inner widget (`KateViewInternal`) to its parent `View`**, so a filter installed only on the
`View` does receive Ctrl+F/H/F3 in the real product. Keep this in mind before raising the
same alarm again — a stale `KateViewInternal` in a stack trace is not evidence of a dead
shortcut.

The experiment that settled it: `find.real-focus.inner-widget` proves the key target is the
inner widget, and the Ctrl+F/H, F3 and Shift+F3 checks **already pass on the unfixed code**
(`06-real-focus-before.log`, checks=46 failures=0). No red could be produced, and no
assertion was adjusted to manufacture one.

The repair was therefore applied as **hardening** (A-14), not as a user-facing fix:
`FindReplaceBar` now installs on the widget tree through the shared kernel helper
(`installEditorEventFilter` / `isEditorOwnedWidget` in `multi_cursor.h`) and claims its four
sequences at `ShortcutOverride` on whichever inner widget receives them, so ownership no
longer depends on propagation and a future kernel action cannot swallow the keys.
`07-real-focus-after.log` is the same check set after the change (green). Both logs are in
this directory.

## Commands and observed results (pinned Qt6 image, workspace at `/vedit-build`)

```
cmake -S src/editor -B target/lane-find -G Ninja            # exit 0
cmake --build target/lane-find --target \
  vedit-editor-find-replace-selftest vedit-edit-core-selftest  # exit 0
ctest --test-dir target/lane-find -R "find-replace|multi-cursor|edit-core"
  -> 3/3 passed, exit 0                                    # 01-lane-ctest.log
     (T13's own test + the edit-core regression; the third is T14's lane test,
      which shares this build because the editor lane is one CMake project)
./target/lane-find/vedit-editor-find-replace-selftest --screenshot evidence/e2-find
  -> 48 checks, 0 failures, exit 0  (02-find-replace-checks.log; 46 in ctest,
     the two screenshot checks only run with --screenshot)
cmake --build target/qt6-build --target vedit-app
  -> last GREEN run of the T13/T14 tree: exit 0, selfcheck 245/245, exit 0
  -> CURRENTLY RED for an EXTERNAL reason: src/workspace/diagnostics/ffi_backends.cpp
     (T9/se-ui, in flight) does not compile — `serverPath_`/`backend_`/... not declared.
     That file is not part of T13 or T14; the editor lane itself is green (3/3, 15/15).
     (03-product-build.log records the external failure; 04-product-selfcheck.log is the
     last product binary from before that break, not a verification of this change.)
cmake --build target/lane-find && ctest --test-dir target/lane-find
  -> 100% tests passed, 0 failed of 15 (05-lane-full-build-ctest.log)
```

Screenshots (the bar renders, not just the functions): `find-bar-find.png` (find row,
status `2 / 4` region) and `find-bar-replace.png` (replace row visible). They are
`QWidget::grab()` of the live bar widget under `QT_QPA_PLATFORM=offscreen`.

## External red repaired (stated, with the before state)

Before this task the standalone editor lane could not link **any** of its test binaries:

```
libvedit_tree_sitter_verilog.a(bindings.cpp.o): undefined reference to `tree_sitter_verilog'
```

Cause: `src/editor/CMakeLists.txt` declared the standalone project as `LANGUAGES CXX`,
then `add_subdirectory()`ed the folding lane, whose tree-sitter grammar library is a
`.c` source (`src/syntax/grammars/CMakeLists.txt:170`). With no C language enabled the
grammar archive contained only `mocs_compilation.cpp.o` (verified with `ar t`), so every
lane test — including the pre-existing `vedit-edit-core-selftest` — failed at link.
The product build already enables C, which is why the product was green while the lane
was not. Fix: `project(VEditEditCore LANGUAGES C CXX)` in the lane's own CMakeLists
(in T13's write scope). After it, the full lane build and all 14 lane tests pass
(`05-lane-full-build-ctest.log`).

## How this was verified / how it was NOT

Verified: real `KTextEditor::Document` + `KTextEditor::View`, real `QTest` key events
(offscreen), the provider's real `createEditor()` path, the product binary's own
`--selfcheck`, the full standalone lane ctest, and the product link.

Not verified / bounds (deliberate, not silently absorbed):

* **Offscreen only.** No real X11 window / mouse interaction was exercised for this lane;
  the fold lane `vedit-fold-gutter-click` owns that kind of proof elsewhere.
* **Replacement text is literal.** With regex enabled, the *search* is a
  `QRegularExpression` (and an invalid pattern is reported, not silently empty), but
  `$1`/`\1` capture-group expansion in the replacement is NOT implemented. AC-4.3 does not
  require it; a caller that wants it needs a follow-up.
* **Whole word is a post-filter** on top of the kernel search (needed because the kernel
  honours `WholeWords` only in plaintext mode), so it also works with regex; the
  predicate is `letter/number/underscore` on the characters flanking the match.
* **Match cap** 100 000: beyond it the status reports the cap instead of freezing.
* **Selection scope** is an explicit choice (the `Sel` toggle): opening Ctrl+F/H with a
  selection does NOT silently restrict the search to it, because that makes “select a
  word, search for another word” return nothing. When `Sel` is on, the scope is captured
  when the bar opens and re-captured when the user changes the selection; the bar never
  re-scopes itself to the match it just selected. Replacing outside the selection is
  asserted byte-identical.
* The bar is one widget per view (child of the view, dies with the pane); no session
  persistence of the bar's own last pattern beyond the live session.

## Scope statement for AC-1

My changed paths: `find_replace.h`, `find_replace.cpp`, `find_replace_selftest.cpp`
(**ADDENDUM A-7**), `ktexteditor_provider.cpp`, `src/editor/CMakeLists.txt`,
`evidence/e2-find/`. No `src/app/**`, no `multi_cursor.*`, no `editor_context_menu.*`,
no `src/workspace/search/**`. `ktexteditor_provider.h` appears modified in `git diff`
because of T12's completed `setModifiedListener` seam (already in the tree when this task
started); this task did not edit it. `diagnostics_annotations.*` in the same directory
belong to T9 and were not touched here.
