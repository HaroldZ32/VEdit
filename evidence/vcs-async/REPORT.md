# T99 报告 — VCS 收口（T24 N-1/N-2）

- Cell: `cell-20261031T085543161Z` · Board task: **T99**（来源：T24 VCS 只读面板评审的 N-1/N-2）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01（Asia/Shanghai）· 基线 `HEAD 4856960`
- 结论：**N-1 完成（info/diff 改成异步 job + 真·非阻塞断言），N-2 选 (b) 明写 stub-only**。
  整树门禁：`ctest 22/22`、`typecheck TYPECHECK-EXIT=0`、`cargo test -p vd-vcs -p vd-ffi = 0`、
  面板 selftest **35 checks / 0 failures**（原 24 条全部保留）。
- 只读闸门**未变松**：`crates/vd-vcs/**` 零改动；生成的 C 头里写操作符号数 = 0；runner 的
  `ensure_read_only` 仍在 spawn 前拒绝写子命令。

## 1. N-1：三个读全部是异步 job

API 对照（旧→新）见 **`01-api-table.md`**。要点：

- `vd_vcs_status`（原有 job）**不变**；
- `vd_vcs_info` / `vd_vcs_diff` 两个同步 C 调用被 **`vd_vcs_info_job` + `vd_vcs_info_poll`**、
  **`vd_vcs_diff_job` + `vd_vcs_diff_poll`** 取代（start 只登记 worker 并立即返回 job id；
  poll 返回 PENDING/READY/FAILED，READY 填结构体；free 函数与结构体不变）；
- `JobValue` 增加 `VcsInfo` / `VcsDiff` 两个 arm（`crates/vd-ffi/src/service.rs`），poll 对
  跨服务的 job id 返回 `POLL_MISUSE` 并记录 `VD_ERR_WRONG_JOB_KIND`（Rust 测试覆盖）；
- C++ seam 新增 `requestInfo/requestStatus/requestDiff`（回调式）；`FfiVcsSource` 用
  **一个懒创建的 10 ms QTimer** 轮询，面板在回调里渲染 —— **GUI 线程不再 spawn 任何子进程**；
- 基类默认实现直接调用同步方法当场回调，`StubVcsSource` 与既有面板测试语义不变。

## 2. N-1 的关键证据：非阻塞断言（`06-nonblocking.md`）

假 `git`（`sleep 0.4` 后 exec 真 git）放在 PATH 最前 + 临时目录放 `.git` 标记 → 真的会 spawn
子进程的 job 上：

```
CHECK vcs.nonblocking.start-returns-fast PASS start took 0 ms while the fake git sleeps 400 ms
CHECK vcs.nonblocking.event-loop-ticks PASS ticks=12
CHECK vcs.nonblocking.job-still-pending PASS polls=24
CHECK vcs.nonblocking.result-delivered PASS git repository has no commit yet
CHECK vcs.nonblocking.metric-detects-block PASS timer fired at 300 ms after scheduling (expected late)
```

灵敏度：让线程阻塞 300 ms 时同一个 20 ms 定时器在 300 ms 才触发 → **同步实现必被抓住**。
（第一版测试在「无 `.git` 标记」的目录上跑，job 因 marker walk 不 spawn 而瞬间完成，
`job-still-pending` 立刻红 —— 这也证明断言依赖的是真子进程，不是假的等待。）

## 3. 面板行为不变（不降 checks）

`03-vcs-selftest.log`：**35 checks / 0 failures**。原有 24 条全部保留并仍绿，包括
空状态（`empty-state.*`）、Git 分支+修订+变更文件+行文本（`git.*`）、diff 展示与只读
（`diff.*`）、SVN stub 面板（`svn.*`）、无 source / refresh 迁移（`no-source.*`、`refresh.*`）、
FFI 契约（`ffi.*`）以及写尝试计数（`no-write-attempt`，stub 的 `writeAttempts()==0`）。
新增 11 条：FFI 段 3→7 条（+4：service、info.start、diff.start、invalid-root-rejected），非阻塞段 +7 条（temp-dir、fake-git、start-returns-fast、event-loop-ticks、job-still-pending、result-delivered、metric-detects-block）；24 + 4 + 7 = 35。

## 4. 顺带修掉的一个真实缺陷（T23 遗留）

`FfiVcsSource` 旧实现用 `take()` 先 `std::free()` 每个字符串，再调用 `vd_vcs_status_free()`
对同一批指针**再 free 一次**（并且 Rust `CString` 的内存用 `std::free` 释放，分配器不匹配）。
stub-only 的面板测试从不走真实 FFI，所以一直没炸。本次改成 `copyText()` 只拷贝、
由 `vd_vcs_*_free()` **释放一次**；同步/异步两条路径都走同一转换。这是本次触到的代码里的
真实 UB，一并修掉并写进这里。

## 5. N-2：SVN 真路径 — 选 (b) 明写 stub-only（`07-svn-stub-only.md`）

- 主机与 pinned 镜像**都没有** `svn` / `svnadmin`（`command -v` 输出已存档），
  因此 `svnadmin create`/`svn checkout` 的真冒烟在这个环境里**跑不了**；
- Git 的真子进程路径已有端到端证据（本任务的慢 git 断言 + `--selfcheck-ffi` 的 VCS status job）；
- SVN 只有 canned-runner Rust 测试与 `StubVcsSource` 面板测试；
- 已在 **`AGENT.md` 新增 VCS 段**写明：三个读是异步 job、Git 已端到端验证、
  **SVN 是 stub-only、未端到端验证，不得声称 SVN 支持已验证**，并写出关闭该缺口需要的
  一步（镜像装 subversion + 真 svn 工作副本冒烟任务）。

## 6. 门禁（同一棵最终树）

| 门 | 命令 | 结果 |
|---|---|---|
| 根 ctest | `ctest --test-dir target/qt6-build` | **22/22 passed**，`ROOT-CTEST-EXIT=0`（`04-ctest.log`） |
| typecheck | `bash build/gates/typecheck.sh` | **TYPECHECK-EXIT=0**（Rust clippy `--deny=clippy::expect_used` 通过 + Qt6 warnings-as-errors，`05-typecheck.log`） |
| Rust 服务 | `cargo test -p vd-vcs -p vd-ffi` | **EXIT=0**：vd-ffi 7 unit + 12 abi；vd-vcs 6 unit + 9 readonly（`02-rust-tests.log`） |
| VCS 面板 | `vedit-workspace-vcs-selftest --selfcheck` | **35 checks / 0 failures**（`03-vcs-selftest.log`） |

captain 报的 `clippy::expect_used`（`crates/vd-ffi/src/vcs.rs` 测试辅助）已按**可传播错误**
修掉：两个辅助函数改为 `Result<_, String>`，三个测试签名改为 `-> Result<(), String>` 并用 `?`；
**没有 allow、没有吞错误**。`cargo fmt -p vd-ffi -- --check` 也干净。

## 7. 范围 / 诚实声明

- 改动：`crates/vd-ffi/src/{vcs.rs,service.rs}`、生成的 `crates/vd-ffi/include/vd_ffi.h`、
  `src/workspace/vcs/{vcs_source,vcs_panel,vcs_selftest}.{h,cpp}`、`AGENT.md`、`evidence/vcs-async/**`。
- **未改动** `crates/vd-vcs/**`（只读闸门）、`src/syntax/**`、`src/editor/**`、其他 lane 在飞文件；
  未 commit/push。
- 无 GUI 视觉验证（headless offscreen）：面板的异步路径由 selftest 的断言覆盖，没有“看起来对”的判断。
- 面板 API 语义：每类查询同一时刻最多一个在飞 job，后到的请求替换先到的（旧结果由服务丢弃，
  不泄漏、不串结果）；QTimer 只在有 job 时运行。
- 未在真实大仓/网络盘上压测 GUI 帧率（容器内无显示服务器）；非阻塞断言的证据是
  事件循环 tick 计数 + 慢子进程计时，而不是帧率。

## 8. 证据清单（`evidence/vcs-async/`）

`00-pins.txt`（HEAD/镜像/改动路径/只读面符号数）、`01-api-table.md`（旧→新 API 表）、
`02-rust-tests.log`、`03-vcs-selftest.log`（35 checks 全文）、`04-ctest.log`、`05-typecheck.log`、
`06-nonblocking.md`（装置+数字+灵敏度）、`07-svn-stub-only.md`（N-2(b) 决策与证据）、本报告。
