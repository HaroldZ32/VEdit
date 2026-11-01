collected_at: 2026-11-01T19:21:33+08:00
git_head: 60c7ae9c95fe769e2bd6c86dd4c56e1514603e44
git_branch: feat/qt6-ktexteditor-migration
git_status:
   M AGENT.md
   M THIRD_PARTY_NOTICES.md
   M build/evidence/typecheck/00-run.txt
   M build/evidence/typecheck/10-typecheck-image-build.log
   M build/evidence/typecheck/11-typecheck-image-id.txt
   M crates/vd-config/src/project.rs
   M crates/vd-config/src/warnings.rs
   M crates/vd-ffi/include/vd_ffi.h
   M crates/vd-ffi/src/lsp.rs
   M crates/vd-ffi/src/project.rs
   M crates/vd-lsp/src/client.rs
   M crates/vd-lsp/src/documents.rs
   M crates/vd-lsp/src/session.rs
   M evidence/ffi-abi/logs/10-rust-image.txt
   M evidence/ffi-abi/logs/19-rust-stage-console.log
   M evidence/ffi-abi/logs/20-cargo-build-release.log
   M evidence/ffi-abi/logs/21-staticlib.txt
   M evidence/ffi-abi/logs/23-cargo-test.log
   M evidence/ffi-abi/logs/26-vd_ffi.h
   M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
   M evidence/m0-build/build-logs/20-qt6-image-build.log
   M evidence/m0-build/build-logs/21-qt6-image-id.txt
   M evidence/m0-build/build-logs/31-qt6-configure.log
   M evidence/m0-build/build-logs/32-qt6-build.log
   M evidence/m0-build/build-logs/33-qt6-ctest.log
   M evidence/m0-build/build-logs/34-qt6-elf.txt
   M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
   M packaging/runtime/AppRun
   M packaging/runtime/README.md
   M packaging/runtime/assemble-appdir.sh
   M packaging/runtime/selfcheck.sh
   M scripts/versions.env
   M src/app/CMakeLists.txt
   M src/app/app_shell.cpp
   M src/app/editor_area.cpp
   M src/app/main.cpp
   M src/syntax/product/syntax_product.cpp
   M src/workspace/diagnostics/CMakeLists.txt
   M src/workspace/diagnostics/diagnostics_controller.cpp
   M src/workspace/diagnostics/diagnostics_controller.h
   M src/workspace/diagnostics/diagnostics_ffi_selftest.cpp
   M src/workspace/diagnostics/diagnostics_integration.cpp
   M src/workspace/diagnostics/diagnostics_panel.cpp
   M src/workspace/diagnostics/diagnostics_panel.h
   M src/workspace/diagnostics/diagnostics_selftest.cpp
   M src/workspace/diagnostics/ffi_backends.cpp
   M src/workspace/diagnostics/lsp_backend.h
   M src/workspace/filetree/filetree_integration.cpp
   M src/workspace/palette/README.md
   M src/workspace/palette/palette_integration.cpp
   M src/workspace/search/CMakeLists.txt
   M src/workspace/search/README.md
   M src/workspace/search/global_search.cpp
   M src/workspace/search/global_search.h
   M src/workspace/search/search_integration.cpp
   M src/workspace/settings/settings_integration.cpp
   M src/workspace/vcs/vcs_integration.cpp
  ?? crates/vd-lsp/tests/buffer_change.rs
  ?? evidence/crash-segv/
  ?? evidence/fonts-cjk/
  ?? evidence/large-file/
  ?? evidence/lint-trigger/
  ?? evidence/locale-and-noise/
  ?? evidence/rules-search-ux/
  ?? evidence/search-segv/
  ?? packaging/runtime/assets/fonts/
  ?? packaging/runtime/assets/licenses/
  ?? src/app/crash_report_check.sh
  ?? src/app/crash_reporter.cpp
  ?? src/app/crash_reporter.h
  ?? src/app/crash_reporter_selftest.cpp
  ?? src/workspace/diagnostics/selftests/

# T121 — search selftest SIGSEGV：栈、二分、引入点

## 结论（给 captain 的一句话）

**不是 search lane 自己的逻辑错，也不是今天大文件 lane 单独引入的**：崩溃**今天下午就已存在**
（14:27–14:50 的 core 与现在**同一栈**：`Kate::TextFolding::foldingRangesStartingOnLine` ← katepart 绘制），
当时**间歇**（15:40 我连跑 10 次全绿）；18:56 大文件 lane 在 `document_io.cpp` 加的事件循环让出
（`yieldToEventLoop()`，未提交）把它**变成 3/3 必现**（让出时立刻有一次绘制）。
根因在折叠接线/编辑器：**同一 view 第二次 open（`releaseFolds()` + `setText`）之后，katepart 的折叠树
被绘制路径查询时不一致 → 空指针**；相关文件在 `src/editor/kernel/**`（T121 写域之外，且该 lane 正在改），
交给其所有者 / 与 T120 合并修。

## 现场

- `vedit-workspace-search-selftest --selfcheck`：**3/3 次 exit=139（core dumped）**
  （`01-repro-run.log`；最后一次 CHECK 是 `jump.product.first-status`，随后第二次 open 输出
  `open: read done … setText done lines=6` 后立即崩溃）。
- 对比：今天 ~15:40 同一二进制 **10/10 全绿**；`document_io.cpp` 的 mtime 是 **18:56**（未提交的
  大文件 lane 改动），19:19 起 3/3 崩溃 → **时间线与引入点吻合**。

## 栈（core 2366300，符号来自 libKF6TextEditor）

```
#0  libKF6TextEditor.so.6 + 0xc5a00
#1  libKF6TextEditor.so.6 + 0xc5b09
#2  Kate::TextFolding::foldingRangesStartingOnLine(int)      // KTextEditor 折叠树查询
#3  libKF6TextEditor.so.6 + 0x23f729                         // katepart 绘制
#4  QWidget::event(QEvent*)                                  // Paint
#9  QWidgetRepaintManager::paintAndFlush()
#13 QCoreApplicationPrivate::sendPostedEvents(...)
#19 QCoreApplication::processEvents(...)
#21 vedit-workspace-search-selftest + 0xede80
     addr2line => vedit::editor::loadFileIntoDocument(...)  src/editor/kernel/document_io.cpp:92
#22 n/a
```

## 触发条件与机制（与你给的线索一致）

1. `loadFileIntoDocument()` 在 `document->setText()` 之后调用 `yieldToEventLoop()`
   （`document_io.cpp:86`；`large_file.cpp:47` 实现）。
2. 该测试在**同一 pane 里打开第二个文件**：T94 的 `openFile()` 先 `releaseFolds()`，再 `setText`；
   折叠树此时被替换/展开过，katepart 的绘制路径仍持有/查询旧的折叠行数据。
3. 让出事件循环 → `QWidgetRepaintManager::paintAndFlush` → katepart 绘制 →
   `Kate::TextFolding::foldingRangesStartingOnLine()` **空指针**（`si_code=1 SEGV_MAPERR`，fault=0x0）。

即：**“文档替换 + 让出事件循环 + 已注册折叠”三者叠加**；与 T117（光标落到折叠隐藏行）同属
“折叠状态在 KTextEditor 内部不一致”这一类，但触发点是**绘制**而不是 `setCursorPosition`。

## 为什么根 ctest 之前 27/27 全绿（以及现在为什么红）

- 崩溃**是今天下午引入的**（14:04 T94 折叠接线落地后才开始出现 14:27–14:50 的 core），但**间歇**：
  取决于第二次 open 之后是否恰好发生一次绘制。15:40 的 10 次连跑全绿就是“没踩到绘制时机”。
- 18:56 大文件 lane 的 `yieldToEventLoop()`（未提交）在 open 路径中**主动派发了一次绘制**，
  把“偶然”变成“必然”（19:19 起 3/3 必崩）。
- 盲区：**没有测试在“已注入折叠的同一 view 里做第二次 open、并让出事件循环/绘制”**；
  折叠 lane 的测试不打开第二个文件，search lane 的测试不关心折叠 —— 两边单看都绿。
  27/27 是“每个 lane 单独绿”的加总，缺的是交叉序列。

## 建议的修复（写域外，交给大文件 lane / editor kernel 所有者）

1. **最小**：不要在 `setText` 之后、折叠重建之前 `yieldToEventLoop()`；把让出移到
   `applyHighlightingMode`/fold 重建完成之后，或在让出前先 `releaseFolds()`（open 路径已有这个能力）。
2. 或在 `setText` 期间给 view 装一个“禁止绘制”的挂起标志（`view->setUpdatesEnabled(false)` 包围
   setText+让出），完成后再恢复。
3. 大文件 lane 自己的 66 s 目标不受影响（它要让出的是**大文件读取**，不是 setText 后的那一刻）。

我没有改 `src/editor/kernel/**`（T121 写域外，且该 lane 正在改这个文件）；这里只交栈与归因。
