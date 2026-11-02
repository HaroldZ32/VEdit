# T104 报告 — FIM 产品接线（第五次接线缺口）

- Cell: `cell-20261031T085543161Z` · Board task: **T104** · 上游：T27 的 `src/editor/fim` 与 REPORT §5
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01 · 基线 `HEAD d36bff9`
- 结论：**已把 FIM 接进真实编辑器路径**：每个 `KTextEditor::View` 一个 `FimBinding`；`Alt+\` 触发、
  ghost-text 叠层预览、`Tab` 接受（**文本只在这一刻改，且恰好一次**）、`Esc` 取消、输入/移动使建议作废；
  **默认关闭时零网络**（live 端点实测 0 连接）、且 `Tab`/`Esc` 在没有建议时不安装（不打断编辑器按键）。
- 门禁：**根 ctest 25/25 passed（exit 0）**、**`bash build/gates/typecheck.sh` → TYPECHECK-EXIT=0**、
  FIM wiring selftest **30 checks / 0 failures**。

## 1. 接线（只动 T104 写域，见 `01-wiring.md`）

| 文件 | 改动 |
|---|---|
| `src/editor/CMakeLists.txt` | guarded `add_subdirectory(fim)`；把 checkout 的 `libvd_ffi.a`/`vd_ffi.h` 映射到 lane 的 `VEDIT_FFI_STATICLIB/VEDIT_FFI_INCLUDE_DIR`（**否则 lane 会以无 Rust 链构建，接线永远 disabled**）；`kernel/fim_wiring.cpp` 加入 core；link `vedit_editor_fim` + `VEDIT_EDIT_CORE_HAVE_FIM=1`；新增 `vedit-fim-wiring-selftest` ctest 条目 |
| `src/editor/kernel/fim_wiring.{h,cpp}` | **新** `FimBinding`：view 的 QObject 子对象（生命周期同 T94 `FoldBreadcrumbBinding`，随 pane 销毁）；`Alt+\` → lane 的 `InlineCompletion::request()`；建议 → 半透明 `QLabel` ghost-text（`View::cursorToCoordinate()` 定位，**不改文档**）；仅在有建议时安装 `Tab`/`Esc`；`Document::textChanged`/`cursorPositionChanged` → `onTextEdited()` 作废；滚轮/resize 隐藏叠层 |
| `src/editor/kernel/ktexteditor_provider.cpp` | `createEditor()` 中 T94 binding 之后 `new FimBinding(view, view)`（guarded） |
| `src/editor/kernel/fim_wiring_selftest.cpp` | **新**：真实 provider（`makeEditCoreProvider()->createEditor()`）+ 真实 lane + 本地假端点，30 条断言 |

**没有改** `src/editor/fim/**`（lane 本体）、`crates/vd-fim/**`、`src/app/**`、`src/workspace/**`。

## 2. 核心验收 ↔ 断言（`02-selftest.log`，30 checks / 0 failures）

| 要求 | 证据 |
|---|---|
| 每视图一个 InlineCompletion | `fim.wiring.binding-installed`、`fim.wiring.binding-owned-by-view`（parent==view，随视图销毁） |
| 默认关闭：`Alt+\` 不发请求 | live 假端点（127.0.0.1 监听）在配置但**未启用**时：`default-off.zero-connections connections=0`；状态 `FIM 未启用`；无 ghost；文档不变 |
| 默认关闭：Tab/Esc 不被打断 | `default-off.tab-free`（`acceptShortcutActive/cancelShortcutActive` 都 false，只在有建议时才装） |
| 启用+配置假端点：`Alt+\` → 请求 → 建议 | `configured.request connections=1`、`configured.suggestion " assign y = 2;"`、`configured.ghost-visible/text` |
| Ghost-text 不修改文档 | `configured.document-untouched-before-tab`（按 Tab 前文本原样） |
| `Tab` 接受：文本才改变且恰好一次 | `accept.text`（精确期望文本）、`accept.exactly-once`（建议串出现 1 次）、`accept.shortcuts-removed`、`accept.ghost-hidden`、`accept.no-pending` |
| `Esc` 取消：文本不变 | `cancel.dropped`、`cancel.document-unchanged` |
| 输入使待定建议作废 | `invalidate.typing-drops-it`、`invalidate.tab-disarmed`、`invalidate.accept-inserts-nothing`（不绕过 T27 状态机） |

## 3. 接线前后

- 接线前（T27 REPORT §5 明写未接线；根 ctest 23 项）：产品里没有 FIM 行为。
- 接线后：`ctest -N` **25 项**（新增 `vedit-fim-wiring-selftest` 1 项；另一项是别的 lane 的），
  `nm -C target/qt6-build/src/app/vedit-app | grep -c 'vedit::editor::FimBinding'` = **50**（含
  `FimBinding::FimBinding(KTextEditor::View*, QObject*)`、`FimBinding::requestCompletion()`）。

## 4. 已知代价 / 诚实声明

1. **同步触发（重要）**：lane 的 `InlineCompletion::request()` 是同步 API（Rust 后端在调用线程轮询 C ABI，
   预算 15 s，`src/editor/fim/fim_backend.cpp` 的 `kPollBudgetMs`）。因此用户按 `Alt+\` 且端点不可达时，
   **GUI 线程最长会等 15 s**。T104 未改 lane 的契约（`src/editor/fim/**` 不在写域）；异步化需要改 lane 的
   `request()` 签名，属后续任务。默认关闭时不发网络请求，所以这个代价只在显式触发+服务异常时出现。
2. **ghost-text 是叠加 QLabel**：KTextEditor 6.24 没有公开 ghost-text API（T27 已说明）。叠加层是纯视觉、
   不进入文档模型；多行建议只显示首行 + `↵`，完整文本在 `Tab` 时插入。
3. **无 GUI 视觉验证**（headless offscreen）：断言覆盖状态与文档文本，没有“看起来对不对”的像素判断。
4. 外部 flake 如实记录：`vedit-folding-tests` 在部分整树运行中红（与 T104 无关，T25 期间已见到；
   单独重跑通过）；最终整树 25/25 绿。

## 5. 证据

`00-pins.txt`、`01-wiring.md`（改动清单）、`02-selftest.log`（30 checks 全文）、`03-ctest.log`（25/25）、
`04-typecheck.log`（TYPECHECK-EXIT=0）、本报告。
