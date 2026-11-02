collected_at: 2026-11-01T19:01:25+08:00
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
   M packaging/runtime/assemble-appdir.sh
   M packaging/runtime/selfcheck.sh
   M scripts/versions.env
   M src/app/CMakeLists.txt
   M src/app/app_shell.cpp
   M src/app/editor_area.cpp
   M src/app/main.cpp
   M src/editor/CMakeLists.txt
   M src/editor/kernel/document_io.cpp
   M src/editor/kernel/document_io.h
   M src/editor/kernel/edit_core_selftest_main.cpp
   M src/editor/kernel/fold_breadcrumb.cpp
   M src/editor/kernel/fold_breadcrumb.h
   M src/editor/kernel/ktexteditor_provider.cpp
   M src/syntax/folding/CMakeLists.txt
   M src/syntax/product/syntax_product.cpp
   M src/workspace/diagnostics/CMakeLists.txt
   M src/workspace/diagnostics/diagnostics_controller.cpp
   M src/workspace/diagnostics/diagnostics_controller.h
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
  ?? evidence/locale-and-noise/
  ?? evidence/rules-search-ux/
  ?? packaging/runtime/assets/fonts/
  ?? packaging/runtime/assets/licenses/
  ?? src/app/crash_report_check.sh
  ?? src/app/crash_reporter.cpp
  ?? src/app/crash_reporter.h
  ?? src/app/crash_reporter_selftest.cpp
  ?? src/editor/kernel/large_file.cpp
  ?? src/editor/kernel/large_file.h
  ?? src/editor/kernel/large_file_bench.cpp
  ?? src/editor/kernel/large_file_bench.h
  ?? src/syntax/folding/tests/fold_interaction_tests.cpp

# T117 报告 — P0 段错误：崩溃可取证基础设施（已交付并验证）+ 折叠崩溃复现与栈（交给 T120）

- Cell: `cell-20261031T085543161Z` · Board task: **T117** · 来源：真机 GUI 一次 SIGSEGV（用户复现为“折叠往回叠”时崩溃）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01
- 结论：**两条线都拿到了结果** ——
  1. **崩溃取证已交付且被证明可用**：下次崩溃会留下带符号栈的
     `$XDG_STATE_HOME/vedit/crash-*.log`（信号/si_code/故障地址/符号化 backtrace/当前文件/最近事件/构建身份），
     `--crash-test=segv` 在**出厂二进制**上实测写出了完整报告；进程也 `setrlimit(RLIMIT_CORE, infinity)` 允许吐核。
  2. **折叠崩溃已复现，栈已拿到**：注入折叠后 **把光标放到被折叠隐藏的行**（`setCursorPosition`）→
     KTextEditor 内部空指针崩溃。栈与复现命令见 `09-trigger-and-handoff.md`，**与 T120/SE3 合并修**。

## 1. 崩溃取证（交付物 A）

| 项 | 实现 |
|---|---|
| 信号 | SIGSEGV / SIGBUS / SIGFPE / SIGILL / SIGABRT（`SA_SIGINFO | SA_ONSTACK | SA_RESETHAND` + 备用栈） |
| 报告路径 | `$XDG_STATE_HOME/vedit/crash-<pid>-<unix-time>.log`，回落 `~/.local/state/vedit/`（**用户可直接找到并发回**） |
| 内容 | 信号号+名字、`si_code`、故障地址、pid、构建身份（HEAD + 构建时间 + AppImage 路径/sha256）、**当前打开文件+大小**、最近 32 条内部事件（环形缓冲）、**符号化 backtrace** |
| 安全性 | 处理器内只用 `open/write/time/backtrace_symbols_fd` 等异步信号安全调用，**不 malloc、不碰 Qt**；环形缓冲与构建信息在正常时预填 |
| core | 进程内 `setrlimit(RLIMIT_CORE, infinity)`；AppRun 的 `ulimit -c` 属 `packaging/**`，未改（给打包负责人的 3 条建议见 `11-apprun-core-dump.md`） |
| 取证开关 | `vedit-app --crash-test=segv|abort|bus|fpe|ill`（在安装报告器后 raise，且在创建窗口前） |

**验证（不是“写了没用过”）**
- `vedit-app-crash-reporter-selftest`：**16 checks / 0 failures** —— fork 子进程真实 `raise(SIGSEGV)`，
  父进程断言：子进程**死于信号 11**（不是 exit 139）、报告文件存在、含 `signal=11 (SIGSEGV)`/`si_code`/`fault_address`/
  `build=`/当前文件/`canary` 事件/`backtrace:`/**符号化的 `vedit_crash_handler` 帧**；`SIGABRT` 同理；
  **正常退出不产生报告**。
- `vedit-app-crash-report-shipped`：跑**出厂 `vedit-app --crash-test=segv`**，实测输出：
  ```
  PASS: vedit-app wrote a crash report at .../state/vedit/crash-13-...log (process exit=139)
  signal=11 (SIGSEGV) si_code=-6 fault_address=0xd build=<HEAD> built <time>
  current_file=/tmp/vedit-crash-test/input.sv
  last_events: [1] start argv0=... [2] crash-test segv
  backtrace: vedit-app(vedit_crash_handler+0x8fb) / libc… / vedit-app(main+0x1ce5) / libc… / _start
  ```
  日志见 `06-shipped-binary-check.log`（18:57 的同一二进制/同一脚本；之后另一 lane 的改动把二进制从构建目录里撤掉了，见 §4）。
- 内部事件已接入：启动、打开文件（`setCurrentFile`+`open` 事件）、主题应用、会话保存 —— 报告能回答“当时在干什么”。

## 2. 折叠崩溃：复现 + 栈（交付物 B，交 SE3/T120）

复现（临时探针，取证后删除，避免与 SE3 的 harness 重复）：
真 provider + 真 view + 1800 行 SV → 产品路径注入 **120 条 fold** → **`view->setCursorPosition(Cursor(120,0))`**
→ **SIGSEGV**（`si_code=1 SEGV_MAPERR, fault_address=0x0`）。

```
libKF6TextEditor.so.6(+0x27c00c)          ← 空指针解引用（KTextEditor 内部）
vedit-fold-crash-probe(+0x17fd86)         ← addr2line = fold_crash_probe.cpp:112
                                          = view->setCursorPosition(KTextEditor::Cursor(120, 0))
```
**触发条件**：必须先有注入的折叠，且光标目标行**落在被折叠隐藏的区间内**。
产品里最可疑的同一条路径：`FoldInjector::unfoldRegistered()` **对 registry 里每个 Range 先 `setCursorPosition(startLine,0)` 再 `unfold`**；
嵌套折叠（module ⊃ always_comb/begin-end）时，内层起始行已被外层折叠隐藏 → 用户「往回叠」触发 rebuild 就会踩到。
这与 T116 的 4000 次注入/66 s 线索同源（大量嵌套折叠 + 任何重建/跳转）。
**建议**：unfold 从外层向内层展开（或先判断目标行可见性再设光标）；注入改为可见窗口优先；回归用例加“注入后移动光标/重建”。
（详见 `09-trigger-and-handoff.md`；我不改 `src/syntax/folding/**`，由 T120 负责人定夺。）

## 3. 为什么 27/27 ctest 没抓到（交付物 C）

`10-blind-spots.md`：折叠测试止步于计数（从不移动光标/不执行 fold-unfold/不重建）；注入规模 200 vs 产品 4000；
没有“注入 → 光标移动 → rebuild → release → 再注入”序列；headless 很少重绘；一进程一文档；
且**崩溃无栈**。→ 盲区 = **“大量注入折叠后的用户交互/重建”**。已把崩溃取证补上，下次这类问题一定有栈。

## 4. 门禁（诚实状态）

- **我自己的两项新测试单独运行都通过**：crash-reporter selftest **16/16**；shipped-binary check **PASS**（18:57，报告见上）。
- 但在写报告时，**另一条 lane 的在飞改动把整树构建打断了**（`src/editor/kernel/large_file_bench.cpp:157:
  'FoldBreadcrumbBinding' was not declared` —— T116 的大文件 lane），导致：
  - `bash build/gates/typecheck.sh` **RED**（错误全部在该文件，不是 T117 的改动）；
  - 根 ctest 6 项失败，其中多为 `Could not find executable`（二进制被在飞构建撤掉），含 `vedit-app-crash-report-shipped`
    （同一脚本 18:57 通过，失败原因就是二进制暂时不存在）。
  - 证据：`07-ctest.log`、`08-typecheck.log`（带 C9 头）。
- 我**没有**为了凑绿去改别人的文件；等 T116 lane 落地后需要重跑一次 ctest/typecheck（预期 30/30）。
- T114 遗留的外部预期仍在：AppImage 需重打包（与本任务无关）。

## 5. 范围 / 声明

- 改动：`src/app/crash_reporter.{h,cpp}`（新）、`src/app/crash_reporter_selftest.cpp`（新）、
  `src/app/crash_report_check.sh`（新）、`src/app/{main.cpp,app_shell.cpp,editor_area.cpp,CMakeLists.txt}`、
  `evidence/crash-segv/**`。未改 `src/syntax/folding/**`、`packaging/**`、`crates/**`。
- 未 commit/push、未打包。折叠崩溃的**修复**由 T120 负责人执行（按 captain 指示不越界）；
  本任务交付取证基础设施 + 复现/栈 + 盲区分析 + 交接说明。
