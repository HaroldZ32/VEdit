# 批 2 验收报告 — 编辑器导航：Rename 应用 WorkspaceEdit + References 可跳转列表（AC-2.1 / AC-2.2 / AC-2.3 / AC-2.4）

- 任务：board **T9**（`lsp-navigation`），cell `cell-20261103T123639841Z`，split rev 1
- 契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/lsp-navigation.md`（冻结 2026-11-03T13:10:27.019Z）
- 写者：**Senior Engineer**（member `Senior Engineer`）· 验证者：**Architect**（loop `loop-20261103T131047-39484d`）
- 证据目录：`evidence/batch2-editor/`（本文件、`probe/`、`shots/`、`logs/`）

## 1. 交付内容（一句话）

F2 Rename 现在**真的应用** LSP `WorkspaceEdit`：已打开文档直接在缓冲区里改（整个文件一次 `EditingTransaction` ⇒ **一步 undo 恢复**），未打开的落点由内核读盘→改→原子写回；状态栏/机器行给出 `N edit(s) / M file(s)` 与「编辑器内 M1 / 磁盘 M2」的真实归属。Shift+F12 References 弹出**可跳转列表**（`文件:行:列` + 片段），双击跳到该位置；0 结果走真实空态；两个动作继续由后端能力表门控，不可用时置灰并显示后端给出的**真实原因**。

## 2. 改了什么（全部在 T9 写域内）

| 文件 | 改动 |
|---|---|
| `src/editor/kernel/lsp_actions.h/.cpp` | 新增 LSP 结果侧 API（放在既有 LSP 动作通道里，沿用该文件的 `KTextEditor`/`atomic_save` 依赖）：`WorkspaceTextEdit` / `WorkspaceLocation` / `ReferenceRow` / `WorkspaceEditReport`；`applyWorkspaceEdit()`、`buildReferenceRows()`、`openDocumentCount()`。**已打开文档**：用 `QApplication` 顶层窗口里所有 `KTextEditor::View` 反查（每个 pane 的每个 tab 都算，不只当前 tab），按路径匹配，逐文件一个 `KTextEditor::Document::EditingTransaction` + 逆序 `replaceText` ⇒ 一次 undo 撤销整次重命名；**未打开文件**：读盘→按 UTF-16 列偏移改→`atomicSaveText()` 原子写回。两阶段（先把所有文件解析并校验所有 range，再应用），所以定位失败**什么都不改**并给出真实错误 |
| `src/editor/kernel/edit_core_selftest.cpp` | 新增 `checkWorkspaceEdit()`（19 条）与 `checkLspActionGates()`（10 条）：rename 应用、多文件（1 个打开 + 1 个不打开）、一步 undo/redo、越界整体拒绝且缓冲区/磁盘都不变、缺失文件真错误、空 edits 拒绝、references 行排序/去重/片段/`file:line:col`/空结果、能力门控（不可用→禁用 + 后端真实原因 + 通道拒绝；可用→启用） |
| `src/workspace/diagnostics/diagnostics_integration.cpp` | `pollNavigation()` 的 rename 分支从「只打印 preview」改为**转换并应用** `WorkspaceEdit`（打印 `RENAME-APPLIED edits/files/in-editor/on-disk` 或 `RENAME-FAILED reason`，状态栏用报告摘要）；references 分支改为弹出 `ReferencesDialog`（新类：`Location` / `Snippet` 两列、`N reference(s) for <symbol>` 表头、空态文案、双击经**唯一 jump seam** 跳转并打印 `REFERENCES-JUMP`）；`beginNavigation()` 在 ABI 拒绝请求时打印 `NAV-REFUSED` 并给出状态栏原因（不再静默无反应）；`refreshLiveState()` 新增发布 `vedit.diagnostics.session`（state/ready/filesOpened/…）作为可观测的会话快照 |
| `translations/vedit_zh_CN.ts` | 新增 18 条译文（kernel + lane 的新可见串）；`lrelease` **607/607 finished，0 unfinished** |

没有改任何冻结文件；没有改 `document_io.*`/`atomic_save.*`（批 1b 的现状被**复用**：未打开文件走 `atomicSaveText()`，已打开文件走 KTextEditor 事务；写入路径没有第二套实现）。

## 3. 证据（全部在最终树上实跑）

### 3.1 真实 shell probe + **真实 verible LSP 后端**（不是 mock）

`evidence/batch2-editor/probe/main.cpp`：按 `main.cpp` 的真实顺序构建主壳（构造 → `installPanels` → 装 KTextEditor provider → `startSession`），打开 fixture 工程里的 `rtl/top.sv`，等**真实会话**就绪（`vedit.diagnostics.session.filesOpened > 0`），然后驱动真实命令 `diagnostics.references` / `diagnostics.rename`。探针链接产品自己的静态库（`build.sh` 用 `ninja -t commands` 取编译/链接行，**不重编 src/**）。

复现：

```bash
bash scripts/build-qt.sh                          # 产品必须先构建
bash evidence/batch2-editor/probe/run.sh          # offscreen + Xvfb :78 xcb + 截图
```

| 运行 | 结果 | 日志 |
|---|---|---|
| offscreen | `BATCH2-EDITOR-PROBE ok=1 checks=24 failures=0`，exit 0 | `10-probe-offscreen.log` |
| Xvfb **:78** + `QT_QPA_PLATFORM=xcb` | `BATCH2-EDITOR-PROBE ok=1 checks=26 failures=0`，exit 0（+2 张截图） | `11-probe-xcb.log` |

逐条（节选真实输出）：

```
INFO a2.session ready=1 filesOpened=1 message='backend=verible · ready: … 1/1 file(s) open…'
PASS a2.gates.rename          # enabled=true（verible 真的 advertise renameProvider）
PASS a2.gates.references
PASS a2.commands.enabled-by-gate
REFERENCES-LIST backend=verible count=1 empty=0 first='…/rtl/top.sv:3:10' header='1 reference(s) for old_sig'
PASS a2.references.dialog
PASS a2.references.rows — 1
PASS a2.references.header — 1 reference(s) for old_sig
PASS a2.references.location-text — /tmp/…/rtl/top.sv:3:10
PASS a2.references.snippet — assign old_sig = 1'b0;
PASS a2.references.double-click-jumps — /tmp/…/rtl/top.sv     # 真实鼠标双击序列
PASS a2.references.jump-line — cursor-line=3 expected=3
RENAME-FILE /tmp/…/rtl/top.sv: 2 edit(s) [editor]
RENAME-APPLIED backend=verible edits=2 files=1 in-editor=1 on-disk=0
PASS a2.rename.applied-to-open-document — bytes-before=63 bytes-after=71
PASS a2.rename.all-occurrences — remaining-old=0 new=2
PASS a2.rename.buffer-dirty
PASS a2.rename.one-undo-restores
INFO a2.existing-action definition enabled=1 source=backend
INFO a2.existing-action formatting enabled=1 source=always-verible-format reason='formatting always uses verible-verilog-format, independent of the LSP backend'
INFO a2.existing-action hover enabled=0 source=backend reason='the running server advertises no hoverProvider (live initialize result)'
PASS a2.existing-actions.gates-present   # definition/references/rename/formatting/hover 五行仍在（AC-2.3 不回归）
```

截图（`xcb`，`QScreen::grabWindow(0)`，因为引用列表是独立顶层窗口）：

| 文件 | 内容 |
|---|---|
| `shots/10-references-list.png` | 「References」窗口：表头 `1 reference(s) for old_sig`，两列 `Location`（`…/rtl/top.sv:3:10`）与 `Snippet`（`assign old_sig = 1'b0;`），状态栏 `1 reference(s) from verible` |
| `shots/20-rename-applied.png` | 编辑器缓冲区里两处 `renamed_sig`（`remaining-old=0`）、tab 脏标记 `● top.sv`、状态栏 `Rename applied: 2 edit(s) in 1 file(s) — 1 document(s) changed in the editor, 0 file(s) written on disk` + `● Unsaved changes` |

### 3.2 自测断言（AC-2.4 / AC-1.5 口径）

`--selfcheck`（自测）**258 → 290 checks / 0 failures**；其中 `EDIT-CORE-SELFTEST` **67 → 99 / 0 failures**。新增覆盖点（全部保留、没有删除或放宽任何既有断言）：

- `editcore.wsedit.*`（19）：`fixture-open`、`fixture-disk`、`view`、`open-visible`、`applied`、`counts`（`edits=3 files=2 editor=1 disk=1`）、`open-text`、`open-dirty`、`disk-text`、`summary`、`one-undo`、`one-redo`、`out-of-range-refused`、`refusal-keeps-buffer`、`refusal-keeps-file`、`missing-file-refused`、`empty-refused`、`references.*`（排序/去重/片段/`file:line:col`/空结果）
- `editcore.gates.*`（10）：`document`、`view`、`channel`、`rename-disabled`、`rename-real-reason`、`rename-action-greyed`、`rename-refused`、`rename-enabled`、`rename-action-enabled`

### 3.3 门禁（最终树）

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 | `bash scripts/build-qt.sh` | **EXIT=0**；`100% tests passed, 0 tests failed out of 38`；`M0-SMOKE ok=1 platform=xcb …` | `logs/gate-1-build-qt.log` |
| selfcheck | `vedit-app --selfcheck`（镜像内 offscreen） | **EXIT=0**，`APP-SHELL-SELFTEST ok=1 checks=290 failures=0`（含 `EDIT-CORE-SELFTEST ok=1 checks=99 failures=0`） | `logs/gate-3-selfcheck.log` |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings` | `CLIPPY_EXIT=0` | `logs/gate-4-clippy.log` |
| fmt | `cargo fmt --all -- --check` | `FMT_EXIT=0` | `logs/gate-5-fmt.log` |
| docs | `bash build/gates/docs.sh` | `[docs] PASS`，`DOCS_EXIT=0` | `logs/gate-6-docs.log` |
| lrelease | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts` | `Generated 607 translation(s) (607 finished and 0 unfinished)` | `logs/gate-7-lrelease.log` |
| 探针 | `bash evidence/batch2-editor/probe/run.sh` | **exit 0**（offscreen 24/0、xcb 26/0、2 张截图） | `10-*`/`11-*`；最终一次整跑另存 `logs/12-probe-final-run.log` |

> 门禁实测退出码见 §5 的实际日志；本报告填写的是最终树上的观测。

## 4. 未证明 / 边界（请验证者按此复核）

1. **verible 的 rename 只返回了「已打开文件」的编辑**（`files=1 in-editor=1 on-disk=0`，fixture 里 `other.sv` 未被服务端索引/未进 workspaceEdit）。因此**多文件 + 未打开文件**那条路径由自测覆盖（`editcore.wsedit.disk-text`：`// old_name` → `// new_name` 原子写盘；`counts` 断言 `editor=1 disk=1`），而不是用一个假后端伪造。这是**服务端行为**，报告按实际观测如实记录（`INFO a2.rename.not-open-file`）。
2. **重命名后缓冲区是脏的、不自动保存**（这是有意设计：用户可一步 undo 或自己保存）；磁盘内容在保存前不变。
3. **重命名不会通知 LSP 会话更新**：改的是编辑器文档/磁盘文件，服务器的内存副本要等下一次 didChange/重解析才同步（本批不在契约内）。
4. **撤销依赖运行时槽位**：KTextEditor 6.24 的 `document.h` 没有公开 undo()，内核用既有 `undo_ops`（按名字找 `undo()` 槽）——这是仓库既有机制，自测断言其可用。
5. **References 列表是独立顶层窗口**（不是 dock），所以截图用全屏抓取；无窗口管理器下位置由 Qt 决定。
6. **能力门控的“置灰 + 真实原因”**：真实 verible 这两个能力都 advertise（所以探针里是启用态）；不可用态的证据来自自测 `editcore.gates.*`（喂一张 enabled=false + 明确 reason 的门表，断言动作被禁用、tooltip 就是该 reason、通道拒绝并回传同一 reason），不使用假 backend 冒充产品证据。
7. `NAV-REFUSED`：会话尚未就绪时请求会被 ABI 拒绝，现在会打印机器行并给状态栏原因（此前静默无反应）。探针先等 `vedit.diagnostics.session.filesOpened > 0` 再发请求。

## 5. 改动文件清单

产品代码（全部在写域内）：
`src/editor/kernel/lsp_actions.h`、`src/editor/kernel/lsp_actions.cpp`、`src/editor/kernel/edit_core_selftest.cpp`、
`src/workspace/diagnostics/diagnostics_integration.cpp`、`translations/vedit_zh_CN.ts`

证据：
`evidence/batch2-editor/{REPORT.md,probe/{main.cpp,build.sh,container-run.sh,run.sh},shots/{10-references-list.png,20-rename-applied.png},00-probe-build.log,10-probe-offscreen.log,11-probe-xcb.log,logs/{12-probe-final-run.log,gate-*}}`
