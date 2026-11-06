# B3 — 诊断体验（R2/R3 + item 9/10/12）与 B5 Rename 修复

契约：`.mpd/plans/ux-batch-2/00-plan.md` §B3、§B5，以及 §0.1 的 BINDING 裁定 R2/R3；
实测事实 §1（F1/F4/F6）。写作用域见 `logs/05-write-scope.txt`（未触碰任何 FORBIDDEN 路径）。

本轮实现顺序与契约一致：**R2 → R3 → item 12 → item 9 → item 10 → B5**。

## 0. 复现命令（容器内路径前缀 `/vedit-build`）

```bash
# 官方门禁（Rust 层 + configure + build + 全量 ctest + xcb smoke）
bash scripts/build-qt.sh                     # -> logs/01-official-build.log  (EXIT=0)

# 本 lane 的三个新 ctest（也可 ctest -R 分别跑）
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-lint-experience-selftest \
  --verible-lint .mpd/recon/verible/bin/verible-verilog-lint   # -> logs/02 (71 checks)
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-rename-e2e \
  --verible-ls .mpd/recon/verible/bin/verible-verilog-ls       # -> logs/03 (21 checks)
target/qt6-build/src/app/workspace-lane-diagnostics/vedit-project-lint-e2e  # -> logs/04 (11 checks)
```
环境：`QT_QPA_PLATFORM=offscreen`、`XDG_RUNTIME_DIR=<tmp>`、`VEDIT_QUIET=1`。

## 1. 交付内容（路径 + 符号）

| # | 交付 | 位置（路径 + 符号） |
|---|---|---|
| R2-1 | **唯一的合并视图**：上位（工程 pass）覆盖下位（LSP/单文件），同文件不叠加、不双算 | `diagnostics_controller.{h,cpp}`：`mergedDiagnostics()`、`mergedDiagnosticsForFile()`、`upperLayerDiagnostics()`、`displayDiagnostics()`（改为分层实现） |
| R2-2 | 上位层可见性：编辑区波浪线/gutter、诊断面板、计数、悬浮框、文件点全部读同一视图 | `diagnostics_integration.cpp` `AnnotationBridge::push()`；`diagnostics_panel.cpp` `rebuildDiagnostics()`/状态行计数；`diagnostic_hover.cpp`；`file_tree_model.cpp` `FileStatusRole` |
| R2-3 | pass 覆盖集合 + 编辑失效（`markFileEdited`，pass 开始时的编辑序号防止运行中被编辑的文件被覆盖） | `diagnostics_controller.cpp`：`replaceUpperLayer()`、`markFileEdited()`、`passStartEditSeq_`；产品侧调用点 `ActiveFileWatcher::poll()` |
| R2-4 | 严重级刻度修正：CLI ABI 是 `0=error/1=warning`，编辑器是 `1=error/2=warning`，只转换一次 | `diagnostics_controller.cpp`：`canonicalSeverity()`/`toDisplayEntry()` 接入全部合并输出与 `statusForFile()`/`fileStatuses()` |
| R2-5 | 工程文件集用 C7 模型（`.f` ∪ 递归扫描）而不是只读 filelist 的旧接口 | `diagnostics_integration.cpp` `ProjectFileSetPoller`：`vd_project_expand()`（原 `vd_project_parse()`） |
| R3-1 | 保存接缝：每次**成功**写入后通知（`saveTab`/`saveAsFile`，失败不通知、不延迟保存） | `src/app/editor_area.{h,cpp}`：`EditorPane::setOnDocumentSaved()`、`EditorArea::setOnDocumentSaved()`（仅保存接缝十余行） |
| R3-2 | 700 ms 去抖 + 单飞（在跑则取消重启，不排队）+ 非阻塞 + 自动标记 | `project_lint_trigger.{h,cpp}`：`ProjectLintTrigger::noteSaved()/flushPending()/passStarts()/cancelledRunning()` |
| R3-3 | `setOpenFiles()` 顺序缺陷修复（显式与自动两条路都在 `startProjectPass()` **之前**快照） | `diagnostics_integration.cpp` `BackendWiring::lintProject()` / `ProjectLintTrigger::fire()` |
| item 12 | ELAB/工程错误列表：显式跑才弹模态（可双击跳转）；自动跑只给可点击状态栏提示（点击跳到下一条）；无 error 时不弹空框 | `project_lint_notice.{h,cpp}`：`ProjectLintNotice::present(rows, Mode)`、`veditElabDialog`、`veditProjectLintNotice`；接线 `BackendWiring::reactToFinishedPass()` |
| item 9-1 | 悬浮诊断范围：严重级 + 规则 id + 消息；未覆盖符号保留原 LSP hover；取消 `hoverProvider:false` 造成的置灰 | `diagnostic_hover.{h,cpp}`：`diagnosticAt()`、`describeDiagnostic()`、`DiagnosticHoverPopup`；`ffi_backends.cpp` `decideActionGate("hover")`；`BackendWiring` 的 view 事件过滤器 |
| item 9-2 | 「修复」按钮仅在引擎确实能修时出现；修复=临时副本 autofix + 整文一次可撤销替换 | `autofix.{h,cpp}`：`autofixRuleInTempCopy()`、`AutofixEngine::probe()/fix()`；`autofix_document.cpp` `applyAutofixToDocument()` |
| item 10 | 文件树在图标与文件名之间画状态点（红/黄/绿），数据来自 R2 合并视图，按 revision 刷新不重扫 | `file_status_delegate.{h,cpp}`；`file_tree_model.{h,cpp}` `FileStatusRole`/`setFileStatusSource()`/`syncFileStatuses()`；`file_tree_widget.cpp`；`filetree_integration.cpp`（经 shell 属性 `vedit.diagnostics.fileStatus` / `viewRevision`） |
| B5-1 | 能力门控落到**请求路径**（后端无 renameProvider → 请求被拒并给理由），菜单照旧置灰 | `diagnostics_controller.cpp` `requestRename()`；`diagnostics_integration.cpp` `BackendWiring::requestRename()` |
| B5-2 | 弹输入框**之前**校验光标（非符号/关键字直接拒绝并给位置与原因），对话框预填符号名，服务端拒绝原因上状态栏 | `diagnostics_integration.cpp` `requestRename()`；`symbol_scan.{h,cpp}` `isVerilogKeyword()` |

## 2. 验收矩阵（全部为实测输出；行号指向 `logs/`）

| 验收 | 结果 | 实测输出（日志:行） |
|---|---|---|
| ① 去抖窗口内 3 次保存 = **1 次**工程 pass | PASS | `02-lint-experience.log:29` `CHECK PASS r3.exactly-one-pass  starts=1 backend=1 saves=3`；`02:27` `r3.non-blocking starts=0`（保存不被 lint 拖住）；`02:31` `r3.single-flight-cancelled cancelled=1`、`02:32` `r3.single-flight-restarted backend starts=3`；`02:33` `r3.automatic-flag automatic=1` |
| ② 同一文件上位覆盖下位、不重复 | PASS | `02:13` `r2.upper-overrides-lower  rows=1 upper=1 lower=0`；`02:20` `r2.cli-severity-scale upper row severity=1`（CLI 0=error 已换算）；`02:19` `r2.merged-count rows=2`（a 1 行 + c 1 行，b 干净 0 行） |
| ③ pass 后编辑该文件回落下位 | PASS | `02:22` `r2.edit-falls-back  rows=1 code=lsp-warning`；`02:23` `r2.edit-status-yellow`；`02:24` `r2.repass-recovers`（下一次 pass 重新覆盖） |
| ④ 三种点颜色 | PASS | `02:18` `r2.dot-colors`；`02:14-16` `r2.dot-red/yellow/green`；像素级：`02:92` `dot.red sampled=#d32f2f`、`02:94` `dot.yellow sampled=#f9a825`、`02:95` `dot.green sampled=#2e7d32`、`02:93` `dot.after-icon dot=2..10 text=15`（图标→点→名字）、`02:99` `dot.repainted-green`（revision 变化后重绘） |
| ⑤ 悬浮内容 + 可修/不可修两分支 | PASS | `02:51` `hover.fixable-content  Error · no-trailing-spaces`；`02:52-53` `hover.not-fixable-content`（含 `No automatic fix: line-length has no autofix`）；`02:55` `hover.fixable-button`；`02:57` `hover.not-fixable-no-button`；`02:58` `hover.not-fixable-reason-visible`；真实引擎：`02:63` `autofix.probe-fixable`、`02:65` `autofix.probe-not-fixable`、`02:66` `autofix.cache-hit` |
| ⑥ autofix 为一次可撤销编辑，一次撤销即还原 | PASS | `02:88` `autofix-undo.one-undo-reverts  module a;|  logic x;   |endmodule|`（撤销后**逐字节**回到原文，含尾随空格；`setText` 实测是两步撤销，已改用 `replaceText` 单步） |
| ⑦ ELAB 模态只在显式 `project.lint` 出现 | PASS | 产品接线 `04-project-lint-e2e.log:14` `product.explicit-dialog dialogs=1 automatic=0`；`:24` `product.automatic-no-dialog before=1 after=1`（保存触发的自动 pass 没有新模态）；`04:22` `product.auto-pass automatic=1`；`04:13` `product.pass ... dialogs=1 notice=1`。策略单测 `02:36` `elab.automatic-no-dialog dialogs=0`、`02:40` `elab.notice-jumps jumps=1`、`02:42` `elab.explicit-dialog dialogs=1`、`02:43` `elab.dialog-rows rows=2` |
| ⑧ rename E2E 先红后绿 / 磁盘一致 / 不可改名处拒绝 / 无 renameProvider 置灰且请求被拒 | PASS | 真实 verible 全链路 `03:28` `rename.buffer-applied w2-count=3`、`:32` `rename.disk-after-save`（保存后磁盘 3 处 `w2`、0 处旧名）；拒绝分支 `03:15` 光标不在符号上（无对话框）、`03:20` 关键字 `endmodule`（无对话框）；能力门控 `02:77` `rename.gate-greyed`、`02:78` `rename.request-refused`、`02:79` `rename.request-never-sent`；未打开文件原子写 `03:43` `rename.non-open-atomic-save ok=1 written=1` |

汇总：`02` = `SUMMARY checks=71 failures=0 skips=0`；`03` = `checks=21 failures=0 skips=0 not-claimed=1`（cross-file rename 按 captain 裁定 **NOT CLAIMED**，见 §4 与 `../b3-repair/REPORT.md`）；`04` = `checks=11 failures=0 skips=0`；官方门禁 `47/47 tests passed`、`M0-SMOKE ok=1`、`EXIT=0`（`logs/01`）。

## 3. 本轮修掉的两个真实缺陷（都由新测试抓出来）

1. **工程 pass 的错误被当成“信息”**（刻度错位）。`vd-deeplint` 的 C ABI 用
   `0=error,1=warning,2/3=note`，而编辑器/面板/波浪线用 LSP 的 `1=error,2=warning`。
   旧代码只在 `mergeDiagnostics()` 的单条 CLI 分支里转换，上位层进入合并视图时是**原始刻度**：
   工程 LINT 的 error 在面板里落进 “Information”，warning 会被画成红色。现在在合并层
   (`mergedDiagnostics*`/`displayDiagnostics`/`statusForFile`/`fileStatuses`/`upperLayerDiagnostics`)
   统一换算一次；`cliDiagnostics()` 仍保留 ABI 原始值（证据/探针不破）。
   证据：`02:20`；修改前实测该行 severity=0（`product` 探针打印 `severity=0 code='ExpectedToken'`）。
2. **没有 `.f` 的工程文件集为空**。`ProjectFileSetPoller` 用的是 `vd_project_parse()`
   （旧接口，只展开 filelist）。实测：一个只有 `rtl/*.sv` 的文件夹返回 `files=0`，
   于是 “Lint Project” 与每次保存后的自动 pass 都只报 “No project file set”，
   R2/R3 对新用户最常见的工程形态完全无效。改用 C7 的 `vd_project_expand()`
   （`.f` ∪ 递归扫描）后同一文件夹 `files=2`，pass 立即产出 2 条诊断。
   证据：修复前后探针 `OBSERVED product.abi-probe files=0` → 修复后 `LINT-PROJECT engine=slang files=2`。

## 4. 没能证明 / 明确 SKIP 的部分

> **Repair update (2026-11-05).** 本节是 lane 原始边界清单；独立评审 FAIL 后的修复见
> `../b3-repair/REPORT.md`。下面的 SKIP 已按 captain 裁定改为显式 **NOT CLAIMED**
> （跨文件 rename 本批不做），并且 hover 生命周期/异步探针/窗口截图均已修复或补齐。

- `rename.crossfile-atomic-save` **NOT CLAIMED**（原 `03:42` 的 SKIP）：verible 的
  `textDocument/rename` 在本产品里只回答打开的那个文件（LSP 会话是 file-scope，
  `other.sv` 未打开也不在会话里），所以“跨文件重命名写盘”**不在本批验收内**。
  E2E 现在打印显式 `NOT-CLAIMED rename.crossfile-atomic-save ...`（不是 SKIP），
  原因同时写在 `src/workspace/diagnostics/README.md`；**未打开文件的原子写**仍由同一份
  E2E 直接驱动 `applyWorkspaceEdit()` 证明（`03:43`，`writtenFileCount=1`、磁盘内容为
  `wire renamed;`），并保留 `rename.non-open-atomic-save`。
- 无 **窗口截图**：B3 的可见部分全部以 offscreen 断言验收（点颜色是**渲染像素采样**，比截图更硬）。
  真机观感（字体/间距/主题）仍需用户验收；本轮没有跑 xcb 截图（未新增 GUI 布局）。
- hover 的“鼠标悬停”只在 `BackendWiring` 的事件过滤器里接线，测试用
  `diagnosticAt()`+`describeDiagnostic()`+`DiagnosticHoverPopup` 覆盖内容与两个分支；
  “真实鼠标移动到波浪线上”的 xcb 手势没有单独做（同一 popup 的显示由 `placePopup()` 负责，
  未在 xcb 下重放）。
- `AutofixEngine::probe()` 是同步子进程（首次 规则+文本 组合约数十毫秒，之后命中缓存）；
  没有做成异步。已记录为已知边界，不是缺陷隐瞒。
- 自动 pass 在 **open 文件**上被 C8 跳过（既有规则，契约只要求修 `setOpenFiles` 顺序，未要求删除 C8）：
  因此对“当前打开并保存的文件”，保存后的 pass 不会覆盖它，诊断仍来自 LSP 下位层。
  这与 R2 的“上位优先”不冲突（打开文件后仍显示上一次 pass 的上位结果，编辑后回落），
  但用户若期待“保存后立即用工程结果覆盖当前文件”，需要后续裁定是否取消 C8。
- 没有修改 `crates/**`：`vd_project_parse` 的旧语义与 CLI 刻度都保留在 ABI 语义里，
  修复全部落在 C++ lane（契约允许的写作用域内）。

## 5. 证据文件

```
logs/01-official-build.log      bash scripts/build-qt.sh 全量：Rust 层 + configure + build + 47 ctest + xcb smoke，EXIT=0
logs/02-lint-experience.log     vedit-lint-experience-selftest（R2/R3/item9/10/12 + rename 门控）71 checks
logs/03-rename-e2e.log          vedit-rename-e2e（真实 verible-verilog-ls，菜单→请求→应用→磁盘）21 checks
logs/04-project-lint-e2e.log    vedit-project-lint-e2e（真实 shell + 真实 vd-deeplint，显式 vs 自动）11 checks
logs/05-write-scope.txt         本 lane 写入的文件清单（含 FORBIDDEN 路径零改动的声明）
```
