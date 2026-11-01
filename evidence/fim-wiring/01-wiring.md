# T104 — FIM 产品接线（最小改动清单）

T27（`src/editor/fim`）交付了状态机 + Rust C-ABI 后端，并明确声明**未接线**（其 REPORT §5）。
T104 只做接线，不改 lane 本体、不改 Rust、不改 `src/app/**`：

| 文件 | 改动 |
|---|---|
| `src/editor/CMakeLists.txt` | guarded `add_subdirectory(fim)`；把 checkout 的 `target/release/libvd_ffi.a` + `crates/vd-ffi/include` 映射到该 lane 的 `VEDIT_FFI_STATICLIB/VEDIT_FFI_INCLUDE_DIR`（否则 lane 会以“无 Rust 链”构建，接线永远只能报 disabled）；`kernel/fim_wiring.cpp` 加入 core sources；link `vedit_editor_fim` + `VEDIT_EDIT_CORE_HAVE_FIM=1`；新增 `vedit-fim-wiring-selftest` ctest 条目。 |
| `src/editor/kernel/fim_wiring.{h,cpp}` | **新**：`FimBinding`，每个 `KTextEditor::View` 一个（作为 view 的 QObject 子对象，随 pane 销毁，和 T94 `FoldBreadcrumbBinding` 同款生命周期）。`Alt+\` → 状态机 `request()`；建议到达后 ghost-text 叠层（view 的子 `QLabel`，`View::cursorToCoordinate()` 定位，**不修改文档**）；仅在有建议时安装 `Tab`（接受 → `Document::insertText` 一次）和 `Esc`（取消）；`Document::textChanged`/`View::cursorPositionChanged` → 走 lane 的 `onTextEdited()` 让待定建议作废；滚轮/尺寸变化隐藏叠层。 |
| `src/editor/kernel/ktexteditor_provider.cpp` | `createEditor()` 里在 T94 的 folding binding 之后 `new FimBinding(view, view)`（guarded by `VEDIT_EDIT_CORE_HAVE_FIM`）。 |
| `src/editor/kernel/fim_wiring_selftest.cpp` | **新**：ctest 条目 `vedit-fim-wiring-selftest`，29 条断言（见 02-selftest.log）。 |

## 已知代价（如实说明）

1. **同步触发**：lane 的 `InlineCompletion::request()` 是同步 API（Rust 后端在调用线程轮询 C ABI，预算 15 s），
   所以按 `Alt+\` 时 GUI 线程会等待请求结束。T104 **没有**改 lane 的契约（`src/editor/fim/**` 不在写域）；
   如果要异步，需要改 lane 的 `request()` 签名，属后续任务。默认关闭时不发网络请求，所以这个代价只在
   用户显式按 `Alt+\` 且服务不可达时出现（最长 15 s）。
2. **ghost-text 用叠加 QLabel**：KTextEditor 6.24 没有公开 ghost-text API（T27 报告已说明），所以用 view 的
   半透明子 label 叠在游标位置；它是纯视觉，不写入文档，`Tab` 前文本不变。多行建议只显示首行 + `↵` 提示，
   完整文本在 `Tab` 时插入。
