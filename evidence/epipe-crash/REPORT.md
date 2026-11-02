collected_at: 2026-11-02T11:37:13+08:00
git_head: af852255acf8979cd25ffc0bd882c1dcbbec6220
git_branch: feat/qt6-ktexteditor-migration
git_status:
   M AGENT.md
   M build/evidence/typecheck/00-run.txt
   M build/evidence/typecheck/10-typecheck-image-build.log
   M build/evidence/typecheck/11-typecheck-image-id.txt
   M evidence/ffi-abi/logs/10-rust-image.txt
   M evidence/ffi-abi/logs/19-rust-stage-console.log
   M evidence/ffi-abi/logs/20-cargo-build-release.log
   M evidence/ffi-abi/logs/21-staticlib.txt
   M evidence/ffi-abi/logs/22-rust-targets.txt
   M evidence/ffi-abi/logs/23-cargo-test.log
   M evidence/m0-build/build-logs/20-qt6-image-build.log
   M evidence/m0-build/build-logs/21-qt6-image-id.txt
   M evidence/m0-build/build-logs/31-qt6-configure.log
   M evidence/m0-build/build-logs/32-qt6-build.log
   M evidence/m0-build/build-logs/34-qt6-elf.txt
   M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
   M src/app/app_shell.cpp
   M src/app/app_shell.h
   M src/app/editor_area.cpp
   M src/app/editor_area.h
   M src/app/editor_provider.h
   M src/app/main.cpp
   M src/app/panel_registry.cpp
   M src/app/panel_registry.h
   M src/editor/CMakeLists.txt
   M src/editor/kernel/ktexteditor_provider.cpp
   M src/editor/kernel/ktexteditor_provider.h
   M src/workspace/diagnostics/CMakeLists.txt
   M src/workspace/diagnostics/diagnostics_integration.cpp
   M src/workspace/diagnostics/diagnostics_panel.cpp
   M src/workspace/diagnostics/diagnostics_panel.h
   M src/workspace/diagnostics/selftests/diagnostics_product_seam.cpp
   M src/workspace/settings/CMakeLists.txt
   M src/workspace/settings/settings_integration.cpp
   M src/workspace/settings/settings_panel.cpp
   M src/workspace/settings/settings_panel.h
  ?? crates/vd-lsp/tests/pipe_lifecycle.rs
  ?? evidence/diagnostics-inline/
  ?? evidence/epipe-crash/
  ?? evidence/i18n/
  ?? evidence/split-close/
  ?? evidence/view-menu-sync/
  ?? src/editor/kernel/diagnostics_annotations.cpp
  ?? src/editor/kernel/diagnostics_annotations.h
  ?? src/workspace/settings/settings_dialog.cpp
  ?? src/workspace/settings/settings_dialog.h

# T134 报告 — 真机必现 "Broken pipe" 闪退：SIGPIPE（Rust 静态链进 C++ 宿主的默认行为缺失）

- Cell: `cell-20261031T085543161Z` · Board task: **T134**（P0）· 日期: 2026-11-02
- **结论一句话**：**是 EPIPE/SIGPIPE**。`vedit-app` 把 Rust 静态链接进 C++ 宿主，**Rust 运行时启动时装 SIGPIPE 忽略的那一步从不运行**；
  切 `.f`/`.v` 时 verible LSP 子进程被拆掉，下一次写它的 stdin 就触发 SIGPIPE 默认处置 → **进程直接死，crash reporter 也来不及跑**
  （SIGPIPE 不在它覆盖的信号里 → `~/.local/state/vedit/` 空着）。修在**宿主层**：`src/app/main.cpp` 在 `main()` 最前 `std::signal(SIGPIPE, SIG_IGN)`
  （带注释说明原因）；**反向控制证明这就是根因**。

## 1. 反向控制（决定性证据）

| 场景 | 证据 | 结果 |
|---|---|---|
| 裸 pipe，默认处置 | `01a-pipe-without-ignore.log` | 进程 **被 SIGPIPE 杀死（exit 141）** |
| 裸 pipe，`SIG_IGN` | `01b-pipe-with-ignore.log` | `write=-1 errno=32 (Broken pipe)`，**进程存活 exit 0** |
| **产品 C ABI**：20 次「session start → 子进程已退出 → 写请求」+ `SIG_IGN` | `02-churn-with-sigign.log` | **20/20 session 启动并存活，exit 0** |
| 同上去掉 `SIG_IGN` | `03-churn-without-sigign.log` | **跑到第 14 次被 SIGPIPE 杀死（exit 141）** —— 真机现象复现 |

fake language server（`/tmp/t134-exiting-ls.sh`：`#!/bin/sh; exit 0`）保证写发生时 pipe 的读端已关闭；探针等 150 ms 再写，**确定性**。

## 2. 修复分层

| 层 | 改动 | 说明 |
|---|---|---|
| **宿主进程** | `src/app/main.cpp`：`std::signal(SIGPIPE, SIG_IGN)`（`main()` 第一行，早于 Qt/Rust/子进程），**带注释**（Rust runtime init 不跑） | 根因修复；之后写死管道返回 `EPIPE`，不再杀进程 |
| **架构文档** | `AGENT.md` §11（Rust 静态链入 C++ 宿主的事实：SIGPIPE 及其它进程级默认行为必须由宿主显式设置） | 防止下一个人删掉那一行 |
| **Rust 会话/写失败** | `crates/vd-lsp/tests/pipe_lifecycle.rs`（新）：server 立即退出 → 会话进入 Failed/Stopped，请求得到**类型化失败**（不是 Pending、不是 panic），`wait_for_stop()` 回收子进程 | 已绿（`06-rust-tests.log`：vd-lsp 全 26 项含该测试） |
| **去抖** | **不做**：归 T131（`src/workspace/diagnostics/**` 在飞，captain 裁定） | 已在 §5 说明 |

## 3. 无头 20 次交替（`started=20 rejected=0`）

`.f`/`.v` 真机切换的等价物是「高频 LSP 会话起/退 + 写」：`02-churn-with-sigign.log` 跑 20 轮
（每轮 start → 等子进程死亡 → 写请求 → free），**进程存活、无未处理 EPIPE、无 crash log（根本没有信号死亡）**。
真正的 GUI 文件切换复现需要真机（captain 会让用户验证）；本无头探针覆盖了同一条崩溃机制。

## 4. 门禁（最终树）

| 门 | 命令 | 结果 |
|---|---|---|
| 根 ctest | `ctest --test-dir target/qt6-build --output-on-failure` | **31/31 passed，exit 0**（`04-ctest.log`） |
| typecheck | `bash build/gates/typecheck.sh` | **PASS，exit 0**（`05-typecheck.log`） |
| Rust | `cargo test -p vd-lsp` | **26 项全绿**，含 `pipe_lifecycle`（`06-rust-tests.log`） |

> 注：期间为保持整树可编，顺带修了 T132 引入的 app-shell 自检期望（settings dock 移除后 5 个面板 +
> `settings.preferences` 的 `Ctrl+,` 唯一性检查）；T132 的完整范围仍待做（见 §5）。

## 5. 未做 / 边界

- **去抖（300–500 ms）与 `.f` 不重启 LSP**：归 T131（diagnostics 目录在飞），captain 已裁定。
- **真机 20 次交替**：需用户配合（captain 转告）；无头侧证据见 §1/§3。
- 崩溃报告器**不覆盖 SIGPIPE**（它只覆盖 SEGV/BUS/FPE/ILL/ABRT）；本次修复后 SIGPIPE 不再致命，
  所以不需要把它加入报告器（如需观测可在后续加计数器，非本次必需）。
- **T132 尚未完成**：我先把树修绿并完成 T134；T132 的完整范围（分页/工具页/FIM 页/表格/截图/字符串清单）待后续。
