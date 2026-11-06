# T12 — 文件标签栏 + 未保存显式标记 + 窗口标题/状态栏

Cell `cell-20261102T050548232Z` · task **T12** · member **lead-shell** · verifier **Reviewer**
Frozen contract: `.mpd/cellwork/cell-20261102T050548232Z/contracts/tabs-dirty.md`
Scope of this report: AC-8.1–8.3, AC-9.1–9.5. Everything below was produced on the tree at
`HEAD 4e2d0a5` with **no test and no gate edited**.

## 1. What was delivered

Each editor pane now carries a **file tab strip** instead of the old one-file header line:

| before | after |
|---|---|
| `pane-1 · KTextEditor (edit-core) ◀ active` header (provider name leaked) | `QTabBar` of documents: `name` / `● name` when dirty, per-tab `×`, active tab = highlight + bold + top accent bar, `active` badge on the focused pane |
| one document per pane | N documents per pane, one editor widget per tab (`QStackedWidget`), provider swap recreates each tab's widget and re-reads its file |
| save/close never told the user anything | tab marker **+** window title `[*]`/star **+** status-bar `● 未保存的更改`; closing a dirty tab asks **Save / Discard / Cancel** |

Files changed (all inside the contract's allowed scopes):
`src/app/editor_area.{h,cpp}` (tab model, strip, close buttons, style, active badge),
`src/app/app_shell.{h,cpp}` (title, status indicator, prompt, provider lifetime),
`src/app/editor_provider.h` (`setModifiedListener` seam + demo provider clears `modified` on open/save),
`src/editor/kernel/ktexteditor_provider.{h,cpp}` (KTextEditor `Document::modifiedChanged` → seam).
`src/editor/kernel/document_io.*` was in scope and **not needed** (untouched).

### The dirty-state seam (why the three markers always agree)
`EditorProvider::setModifiedListener(QWidget*, std::function<void(bool)>)` is the one signal;
`EditCoreProvider` bridges `KTextEditor::Document::modifiedChanged` (connection context = the tab's
view, so it dies with the tab); the demo `QPlainTextEdit` gets the same seam from the base class.
`EditorPane` never caches the flag: every refresh reads `provider->isModified(widget)`, so the tab,
`setWindowModified()` and the status label are painted from the **document's** flag — that is why a
save clears all three in one step (AC-8.2) and a programmatic `setModified` cannot desync them.

### Ownership note (the `--selfcheck` teardown crash)
`ApplicationShell` owns the provider as a member, but its panes are children of the central widget:
members are destroyed **before** `~QMainWindow` deletes the children. A pane destructor that
unsubscribes from the provider is therefore a use-after-free. Fixed here:
`setEditorProvider()` keeps the old provider alive across the swap (`previous` local), and
`~EditorPane` never touches the provider (the *area* calls `releaseListeners()` while it is alive).
Regression: `teardown.round-0/1/2` in the probe (shell with 2 panes + swapped provider + dirty tab,
destroyed 3×) — PASS offscreen and xcb.

## 2. Acceptance matrix

| AC | Evidence (file → check name) | Result |
|---|---|---|
| **8.1** 编辑后：标签脏点 / 标题 `[*]` / 状态栏“未保存的更改” | `shots/02-dirty-three-markers.png`; `10/11-probe*.log`: `dirty.tab-marker` (`● charlie_unit.sv`), `dirty.tab-marker-only-on-active-tab`, `dirty.title-placeholder` (`VEdit — /tmp/…/charlie_unit.sv[*]`), `dirty.title-star-rendered` (resolved platform title `…charlie_unit.sv*`), `dirty.window-modified`, `dirty.status-label` (`● 未保存的更改`) | PASS |
| **8.2** 保存后三处同时清除 | `shots/03-after-save-clean.png`; `save.tab-marker-cleared`, `save.window-modified-cleared`, `save.title-star-cleared`, `save.status-label-hidden`, `save.disk-bytes` | PASS |
| **8.3** 关闭脏标签弹保存/放弃/取消且行为一致 | `shots/04-unsaved-close-prompt.png` (real `QMessageBox`: title, file name, full path, **Save / Discard / Cancel**); `prompt.cancel-keeps-tab`, `prompt.cancel-disk-unchanged`, `prompt.discard-closes`, `prompt.discard-disk-unchanged`, `prompt.save-closes`, `prompt.save-writes-disk` | PASS |
| **9.1** 每窗格顶部标签栏；3 文件 → 3 标签，文本=文件名 | `shots/01-three-tabs.png`; `tabs.strip-exists` (`editor-tab-bar`), `tabs.three-opened`, `tabs.same-pane`, `tabs.file-names` (`alpha.sv | bravo.sv | charlie_unit.sv`) | PASS |
| **9.2** active 高亮、点击切换、栏顶可见 | `shots/01/02` (selected tab: highlight fill, bold, 2px top accent; `active` badge on the focused pane); `tabs.style-has-selected-rule`, `tabs.switch-file`, `tabs.switch-index`, `tabs.switch-editor` | PASS |
| **9.3** 头部不再出现 `KTextEditor`/provider 名；标题含当前文件与路径 | `tabs.no-provider-name-in-pane` (no QLabel in the pane mentions `KTextEditor`/`edit-core`), `dirty.title-names-file` (absolute path in the title) | PASS |
| **9.4** 每标签有关闭按钮；关闭脏标签走 8 的提示；关闭后相邻标签 active | `shots/01/02` (`×` on every tab); `close.tab-button-present` (`tab-close-button`), `close.tab-closed`, `close.neighbour-active` (index 1 / `charlie_unit.sv`), `prompt.*` | PASS |
| **9.5** 溢出可滚动/可切换、不撑破布局 | `shots/05-tab-overflow.png` (12 tabs, 760px window: strip clipped, active tab scrolled into view); `overflow.twelve-tabs`, `overflow.scroll-buttons`, `overflow.clipped-at-narrow-width` (`bar=685 hint=1243`), `overflow.active-tab-visible-narrow`, `overflow.no-layout-growth` (`min=113`) | PASS |

Regression kept: `F6`/split/close/pane-close/unsaved-pane-close, session save/restore, single Ctrl+S
authority, `openPaths` multi-file split (T137) — all re-asserted by the product selfcheck
(`APP-SHELL-SELFTEST ok=1 checks=245 failures=0`).

## 3. Commands, observed output, exit codes

All commands were run **inside the pinned image** `vedit-qt6-build:local` (Qt 6.10.2, KF6 6.24.0);
the exact block with binary sha256, versions and outputs is `20-product-gates.log`.

```
# [1] product-level shell selfcheck (the shipped binary)
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/selfcheck-state
→ EDIT-CORE-SELFTEST ok=1 checks=67 failures=0
  APP-SHELL-SELFTEST ok=1 checks=245 failures=0
  SELFCHECK_EXIT=0

# [2] full C++ test suite
ctest --test-dir target/qt6-build
→ 97% tests passed, 1 tests failed out of 33
  25 - vedit-workspace-search-selftest (Failed)      ← see §5, owned by this member under T20
  CTEST_EXIT=8

# [3] lane probe (this task's own acceptance, offscreen + Xvfb/xcb)
bash evidence/e1-tabs/probe/run.sh
→ TABS-PROBE failures=0 platform=offscreen
  TABS-PROBE failures=0 platform=xcb
  TABS-PROBE-OK            (rc=0)
  logs: 10-probe-offscreen.log, 11-probe-xcb.log, 12-probe-run.log, shots/01..06

# [4] the project's declared gate (AGENT.md §4) — log: 21-typecheck-gate.log
bash build/gates/typecheck.sh
→ [qt6] PASS: Rust service layer + configure + build (warnings-as-errors) inside vedit-qt6-build:local
  cargo clippy --workspace --all-targets -- -D warnings        → exit 0 (re-checked on its own:
                                                                CLIPPY_EXIT=0)
  [typecheck] FAIL: crates/vd-ffi/include/vd_ffi.h drifted from its source
  EXIT=1                                                       ← see §5 item 2: "commit pending",
                                                                not a code defect and not this lane
```
The gate's C++/Qt and Rust halves are green; it fails on `check_ffi_header_drift`, which compares
the **committed** `vd_ffi.h` against the **worktree** copy. The worktree header is the freshly
regenerated one for another lane's uncommitted C-ABI work (`git diff --stat` on that file:
`+415 −30`), so the check can only go green once that change is committed — by construction, no
uncommitted FFI change can pass it.

`bash scripts/build-qt.sh --no-test` is the build this tree's binary comes from; the probe's
`build.sh` links the product's own `libvedit_app_shell.a` / `libvedit_editor_core.a` with the
product's own compile+link flags, i.e. no product source is recompiled for the probe.

## 4. How the evidence was produced (re-runnable)

```
bash evidence/e1-tabs/probe/run.sh        # rebuilds the 2 linked product libs, builds + runs the probe
```
`probe/main.cpp` drives the **real** `ApplicationShell` + **real** `EditCoreProvider` (`KTextEditor`):
opens 3 files into one pane, edits the active document through its `KTextEditor::Document`, saves via
`saveActiveDocument()`, closes a tab through the real per-tab `×`, opens 12 tabs and shrinks the
window, and answers the real modal dialog (a timer grabs the dialog, records
`04-unsaved-close-prompt.png`, then clicks Cancel / Discard / Save in turn).
Note: the pinned image ships DejaVu only; `run.sh` installs the **delivered** runtime font
(`packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc`, the same file the AppImage ships) so the
Chinese status string renders as it will for the user instead of tofu boxes.

## 5. Residual uncertainty / things a verifier should know

1. **ctest 32/33 — the one red is mine but not T12's.** `vedit-workspace-search-selftest` caches
   `view`/`document` for the pane before its first jump; with tabs, the second jumped-to file opens
   in a second tab, so the cached document is the previous file. Root cause is the test's
   one-document-per-pane assumption, not product behaviour (`pane->currentFilePath()` and all other
   checks pass). The captain approved the fix under **T20** scope (`src/workspace/search/…`), where the
   same jump path is being rewired to the global `jump::Registry`; no assertion will be weakened.
2. **Declared gate: one red, commit-timing only.** `typecheck.sh` is red solely on
   `check_ffi_header_drift` = "committed header vs worktree header": the worktree carries the
   freshly regenerated `crates/vd-ffi/include/vd_ffi.h` for another lane's **uncommitted** C-ABI
   additions (+415/−30 against HEAD). Both real checks of that gate are green: the Qt6/C++
   warnings-as-errors build and `cargo clippy --workspace --all-targets -D warnings` (exit 0,
   re-verified standalone). Nothing in this task touches Rust or the C ABI.
3. **Tabs are not persisted in the session layout** (AC-9 does not ask for it). Item 10.1.3
   (“每窗格活动标签”) will need `EditorArea::saveLayout/restoreLayout` extended with each leaf's tab
   list + active index; the model is already there (`EditorPane::openFilePaths/activeTabIndex`).
4. **Provider swap re-reads open documents from disk.** A tab with no file path cannot survive a
   provider swap with its typed text (the product swaps the demo placeholder for the real editor
   once, before the user can open a file, so this is not reachable in the shipped flow).
5. **Untitled tab + Save in the prompt** cannot write: the prompt's message says so and the tab stays
   open (`Save` reports the provider error; there is no Save-As yet — item 4's lane).
6. Screenshots have no window decoration (no window manager under Xvfb), so the **title bar** is
   evidenced by the resolved platform title (`dirty.title-star-rendered`), not by pixels.
