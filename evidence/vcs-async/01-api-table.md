# T99 N-1 — VCS API: 旧 → 新（三个读全部是异步 job）

## C ABI（`crates/vd-ffi/src/vcs.rs`，头文件由 cbindgen 生成）

| 操作 | 旧（T23/T95） | 新（T99） | 语义 |
|---|---|---|---|
| status | `uint64_t vd_vcs_status(service, root)` + `int32_t vd_vcs_poll(service, job, VdVcsStatus *out)` | **不变**（本来就是 job） | start 只登记 worker 并返回 id；poll 返回 PENDING/READY/FAILED，READY 时填充 out |
| info | `VdVcsInfo vd_vcs_info(const char *root)`（**同步**，在调用线程 spawn `git rev-parse`×2） | `uint64_t vd_vcs_info_job(service, root)` + `int32_t vd_vcs_info_poll(service, job, VdVcsInfo *out)` | start 立即返回 id（0=参数被拒）；poll 同 status；结果用 `vd_vcs_info_free` 释放 |
| diff | `VdVcsDiff vd_vcs_diff(const char *root, const char *path)`（**同步**，调用线程 spawn `git diff`） | `uint64_t vd_vcs_diff_job(service, root, path)` + `int32_t vd_vcs_diff_poll(service, job, VdVcsDiff *out)` | 同上；结果用 `vd_vcs_diff_free` 释放 |

- 结构体 `VdVcsInfo` / `VdVcsDiff`、free 函数**保持原样**（现在是 poll 的输出类型）。
- error 语义沿用既有约定：`*_poll` 返回 `-1`（POLL_MISUSE）并把原因写进 `vd_service_error`
  （未知 job / 把别的服务的 job 交给本 poll / out 为空）；`READY` 之外的失败通过
  `vd_service_error` 取消息。
- **只读闸门不变**：三个 job 都走同一个 `ProcessVcsService` → `SystemRunner`，
  `ensure_read_only(program, args)` 在 **spawn 之前**拒绝写子命令；C ABI 里没有任何
  commit/update/revert/switch 入口（生成的 `vd_ffi.h` 里写操作符号数 = 0）。
  `crates/vd-vcs/**` 本次**零改动**。

## C++ 面板 seam（`src/workspace/vcs/vcs_source.h`）

| 方法 | 旧 | 新 | 谁调用 |
|---|---|---|---|
| `Info info(root)` | 同步 C 调用 | **保留**（seam 完整性；FfiVcsSource 实现为「start job + 有界等待」，StubVcsSource 直接返回） | 只有测试/旧代码 |
| `Status status(root)` | 同步 C 调用 | 保留（同上） | 只有测试/旧代码 |
| `Diff diff(root, path)` | 同步 C 调用 | 保留（同上） | 只有测试/旧代码 |
| `requestInfo(root, cb)` | — | **新增**：FfiVcsSource = `vd_vcs_info_job` + 10 ms QTimer 轮询，READY 时在 GUI 线程回调 | 面板 |
| `requestStatus(root, cb)` | — | **新增**：`vd_vcs_status` + QTimer | 面板 |
| `requestDiff(root, path, cb)` | — | **新增**：`vd_vcs_diff_job` + QTimer | 面板 |

- 基类默认实现：直接调用同步方法并**当场回调** → StubVcsSource 与既有面板测试的同步语义不变
  （35 条 selftest 里原有 24 条全部保留）。
- `FfiVcsSource` 现在持有：一个 `VdService`（构造创建、析构销毁）、一个懒创建的单例 QTimer
  （10 ms；有 job 时启动、全部完成后停止，因此空闲时没有定时器开销）、每类最多一个在飞 job
  （新的请求替换旧的；旧结果由服务端丢弃，不会泄漏）。
- 面板：`refresh()` → `requestStatus`；状态到达后渲染文件列表并 `requestInfo` 填 header；
  `selectFile()` → `requestDiff`。三条路径都不会在 GUI 线程 spawn 子进程。
