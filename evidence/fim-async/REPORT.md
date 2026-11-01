collected_at: 2026-11-01T15:52:46+08:00
git_head: 90176858e5f8d5123c573826df8c0ccc7375c015
git_branch: feat/qt6-ktexteditor-migration
git_status:
   M build/evidence/typecheck/00-run.txt
   M build/evidence/typecheck/10-typecheck-image-build.log
   M build/evidence/typecheck/11-typecheck-image-id.txt
   M crates/vd-ffi/include/vd_ffi.h
   M crates/vd-ffi/src/service.rs
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
   M packaging/appimage/README.md
   M packaging/runtime/AppRun
   M packaging/runtime/README.md
   M packaging/runtime/assemble-appdir.sh
   M packaging/runtime/build.sh
   M packaging/runtime/selfcheck.sh
   M packaging/runtime/verify-f2-isolation.sh
   M packaging/runtime/verify-not-shipped.sh
   M src/editor/fim/README.md
   M src/editor/fim/fim_backend.cpp
   M src/editor/fim/fim_backend.h
   M src/editor/fim/fim_inline_completion.cpp
   M src/editor/fim/fim_inline_completion.h
   M src/editor/fim/fim_selftest.cpp
   M src/editor/kernel/fim_wiring.cpp
   M src/editor/kernel/fim_wiring.h
   M src/editor/kernel/fim_wiring_selftest.cpp
   M src/workspace/palette/README.md
   M src/workspace/search/CMakeLists.txt
   M src/workspace/search/README.md
   M src/workspace/search/global_search.cpp
   M src/workspace/search/global_search.h
  ?? evidence/fim-async/
  ?? evidence/slice1-stop/71-typecheck-fim.log
  ?? evidence/slice1-stop/72-build-qt-fim.log
  ?? evidence/slice3-fim/
  ?? evidence/slice3-pack/
  ?? evidence/verdicts/fim-reeval/

# T106 报告 — FIM 请求异步化（job + poll + QTimer，超时=取消+提示，不再冻结 GUI）

- Cell: `cell-20261031T085543161Z` · Board task: **T106** · 来源：T104 上报的同步 15 s 代价
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01
- 结论：**`InlineCompletion::request()` 的同步路径已拆成 job + poll**；编辑器绑定用 **15 ms QTimer**
  轮询，`Alt+\` 的 start 调用**立即返回**（实测 0 ms，端点故意 sleep 400 ms），事件循环在等待期间持续运行
  （实测 ticks=12 / 250 ms、polls=16），建议**晚到但仍正确显示**；15 s 预算变成**取消 + 一句短状态**
  （实测 300 ms 预算下 301 ms 触发 `FIM 超时（300 ms 无结果，已取消）`），**不再冻结界面**。
- 门禁：**根 ctest 25/25 passed（exit 0）**、**`bash build/gates/typecheck.sh` → PASS（exit 0）**，
  两个日志都带 C9 头（`03-ctest.log` / `04-typecheck.log`）。

## 1. 改动

| 文件 | 改动 |
|---|---|
| `src/editor/fim/fim_backend.h/.cpp` | `Backend` 增加异步 seam：`supportsAsync()`、`start(prefix, suffix, &error) -> token`、`poll(token) -> AsyncReply{Pending/Text/Disabled/Error/Stale}`、`cancel(token)`。`RustBackend` 实现为 **C ABI job**：`start` 只 `vd_fim_complete`（立即返回 token），`poll` 只 `vd_fim_poll` 一次（**绝不等待**），`cancel` 释放 service/job。`complete()` 保留为阻塞便利调用（lane selftest / 显式调用者），内部就是 start + 有界轮询。 |
| `src/editor/fim/fim_inline_completion.h/.cpp` | 状态机增加 `requestJob()` / `pollRequest()` / `cancelRequest()` / `inFlight()` / `activeJob()`；**同一时刻只有一个 job**（新请求先取消旧的）；`onTextEdited()` 同时作废待定建议**并取消在飞 job**；`request()`（阻塞版）改为 `requestJob + 有界轮询`，语义不变。 |
| `src/editor/kernel/fim_wiring.h/.cpp` | `FimBinding` 用 **QTimer(15 ms)** 轮询；`Alt+\` 立即返回；`setPollBudgetMs()`（默认 15000，测试可缩短）；超时 → `cancelRequest` + 状态 `FIM 超时（N ms 无结果，已取消）`；typing/cursor/新请求都会取消在飞 job；`inFlightJobs()`/`pollRounds()` 供断言。 |
| `src/editor/fim/README.md` | 写明异步形态、超时语义、以及 **ghost 只预览首行**（多行显示 `↵`，Tab 插入完整文本）的限制。 |
| ghost 叠层 | 新增 tooltip，明说“只预览首行、Tab 插入完整建议”。 |

未改 `crates/vd-fim/**`、`crates/vd-ffi/**`、`src/syntax/**`、`src/workspace/**`（Rust 侧本来就是异步 job，
T106 只是把 Qt 侧从“阻塞轮询”改成“QTimer 轮询”）。

## 2. 非阻塞断言（`02-wiring-selftest.log`，50 checks / 0 failures，exit 0）

慢端点：HTTP 响应前 `sleep 400 ms`；黑洞端点：接受连接但永不响应。

| 断言 | 结果 |
|---|---|
| `fim.async.start-returns-fast` | **start took 0 ms**（端点 sleep 400 ms） |
| `fim.async.in-flight` | 请求后恰有 1 个在飞 job |
| `fim.async.event-loop-ticks` | **ticks=12** / 250 ms（事件循环在跳） |
| `fim.async.still-pending` | 250 ms 时仍 in_flight=1、无建议 |
| `fim.async.polls-ran` | 期间轮询 **16** 次（QTimer 在工作） |
| `fim.async.suggestion-arrives-late` | 建议晚到但仍到达；`fim.async.ghost-after-arrival` 叠层显示 |
| `fim.async.typing-cancels-inflight` | 输入立即取消在飞请求；`cancelled-never-arrives` 600 ms 后也不冒出来 |
| **灵敏度对照** `fim.async.metric-detects-block` | 人为阻塞线程 300 ms → 同一 20 ms 定时器 **300 ms 才触发**（指标能抓住同步实现） |
| 超时 `fim.async.timeout-fires` | 黑洞 + 300 ms 预算 → **301 ms** 触发取消 |
| `fim.async.timeout-status` | `FIM 超时（300 ms 无结果，已取消）`（可诊断，不冻结） |
| `fim.async.timeout-no-suggestion` / `document-unchanged` | 无建议、文档不变 |

默认关闭与 T104 语义回归（同一日志）：`default-off.zero-connections`（live 端点 0 连接）、
`default-off.tab-free`、`configured.async-started`、`accept.text/exactly-once`、`cancel.document-unchanged`、
`invalidate.typing-drops-it`、`binding-owned-by-view` 全部仍 PASS。

## 3. lane 状态机断言（`01-lane-selftest.log`，51 checks / 0 failures）

`fim.async.start-token`、`poll-pending`、`poll-ready`、`accept-once`、`typing-cancels-inflight`、
`single-inflight`（新请求取代旧 job）、`new-cancels-old`、`cancel-releases`、`stale-poll-after-cancel`。

## 4. C2 结论

GUI 线程不再等待 FIM：`Alt+\` 只是启动 Rust job；15 s 预算由 QTimer 侧的**取消 + 诊断状态**兑现；
慢端点/黑洞端点都只影响“建议晚到或被取消”，不影响事件循环。同步的 `complete()` 仍存在，但只被
lane selftest 与显式调用者使用，产品路径（`FimBinding`）不再调用它。

## 5. 边界

- ghost 多行仍只预览首行（KTextEditor 6.24 无公开 ghost API）——README + tooltip 已明写。
- 外部 flake：`vedit-folding-tests`（T94）在部分整树运行会红、单独重跑通过；如本任务门禁日志里出现，
  如实归因，不记到 T106 头上。
- 无 GUI 视觉验证（headless offscreen）：断言覆盖状态、文档文本与计时。

## 6. 证据（全部带 C9 头：collected_at + git rev-parse HEAD + git status --porcelain）

`01-lane-selftest.log`、`02-wiring-selftest.log`、`03-ctest.log`、`04-typecheck.log`、
`run-evidence.sh`（可复跑，头部由同一个 emit_header 生成）、本报告。
