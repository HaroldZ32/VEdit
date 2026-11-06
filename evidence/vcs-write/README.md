# 批 3a · VCS 写入口地基证据（AC-3.1 / AC-3.2 / AC-3.6 后端部分）

任务：`vcs-write-foundation`（board **T11**，cell `cell-20261103T123639841Z`）。
契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/vcs-write-foundation.md`。
写者：Deep Worker。评审：Architect（`review-vcs-foundation`）。

本文件只记录**命令、观测结果与边界**；实现细节在代码里，评审按「契约 + 文档 + 证据」判定。

## 0. 一屏结论

| 判据 | 观测 |
|---|---|
| 读写分离 | 新增独立写表 `GIT_WRITE_ALLOWED` / `SVN_WRITE_ALLOWED` + 独立入口 `WriteRunner::run_write`；`ensure_read_only` / `GIT_READ_ONLY` / `SVN_READ_ONLY` **未改一行** |
| 写白名单 | 9 个类型化操作（Git 4 + SVN 5），逐操作放行；未知操作码 / 非仓库 / 后端不匹配 / 越界路径默认拒绝 |
| 异步 job | `vd_vcs_write_job` / `vd_vcs_write_poll` / `vd_vcs_write_free` 三件套，走同一 job registry；GUI 线程只 start/poll |
| C ABI | `vd_ffi.h` 由 cbindgen 重新生成，与构建输出**逐字节一致**（sha256 `53a53985…3bc247a`，54,754 B） |
| 旧断言替换 | `readonly.rs` 与 `vcs_selftest.cpp` 的旧写拒绝断言替换为更强的「读拒绝写 + 写按白名单」；覆盖点只增不减（selftest 35 → 48 checks） |
| 事件循环 | 假 git（sleep 0.4 s）下写 job 保持 PENDING，QTimer tick=12、poll=235、start=0 ms |
| E2E 隔离 | 所有 E2E 在 `mktemp -d` fixture；探针启动即断言 cwd ≠ VEdit 工作树；工作树快照前后一致（19 条） |
| 七道门禁 | 全部 exit 0（见 §5） |

## 1. 设计：第二条接缝，不是放宽读表

```
panel / C++                        读路径（未改）                    写路径（新增）
  │  vd_vcs_status / info / diff     SystemRunner ── ensure_read_only ── 表: GIT_READ_ONLY / SVN_READ_ONLY
  │        (job/poll/free)                  │
  └────  vd_vcs_write_job/poll/free   SystemWriteRunner ─ ensure_write_allowed ─ 表: GIT_WRITE_ALLOWED / SVN_WRITE_ALLOWED
                                           │
                                      WriteOp（9 个码）→ 只由类型化字段构造 argv
```

- 读闸门 `ensure_read_only()`、`GIT_READ_ONLY`、`SVN_READ_ONLY` **保持原样**（`git diff` 可核对）。
- 写表与读表**不相交**：`readonly.rs::read_and_write_tables_are_disjoint_and_default_deny`
  逐项断言（读子命令不能经写路径放行、写子命令不在读表里）。
- 参数由 `WriteRequest::invocation()` 构造，调用方不能传任意 argv：路径不得绝对、不得含 `..`、
  不得以 `-` 开头；`--amend` 只允许 commit；未列参数（如 stage 带 message）直接拒绝。

## 2. 写操作白名单（C ABI 码 = `vd_vcs::write::WriteOp`）

| 码 | 操作 | 后端 | 实际 argv（cwd = 仓库根） | 参数约束 |
|---|---|---|---|---|
| 1 | GitStage | Git | `add -- <paths>` | ≥1 路径 |
| 2 | GitUnstage | Git | `reset HEAD -- <paths>` | ≥1 路径 |
| 3 | GitDiscard | Git | `checkout -- <paths>` | ≥1 路径；不可逆（`is_irreversible`） |
| 4 | GitCommit | Git | `commit -m <msg> [--author=<a>] [--amend] [-- <paths>]` | 非空 message |
| 11 | SvnCommit | SVN | `commit -m <msg> [paths…]` | 非空 message；不接受 author/amend |
| 12 | SvnUpdate | SVN | `update [paths…]` | 路径可选 |
| 13 | SvnRevert | SVN | `revert <paths…>` | ≥1 路径；不可逆 |
| 14 | SvnAdd | SVN | `add <paths…>` | ≥1 路径 |
| 15 | SvnRemove | SVN | `remove <paths…>` | ≥1 路径；不可逆 |

未列码（0/5/10/16/999…）→ `from_code` 返回 `None`，C ABI 直接 `0` + `VD_ERR_INVALID_ARGUMENT`。

**cwd 而非 `git -C`**：交付目标是 CentOS 7（git 1.8.3），该版本**没有 `-C`**（见 §6 发现 1），
所以写路径用 `Command::current_dir(仓库根)` + 仓库相对路径。

## 3. C ABI 新增面（`crates/vd-ffi/include/vd_ffi.h`）

```c
#define VD_VCS_WRITE_GIT_STAGE 1        /* … 直到 VD_VCS_WRITE_SVN_REMOVE 15 */

typedef struct VdVcsWriteRequest {      /* op, root, message, author, amend, count, paths */
  int32_t op; const char *root; const char *message; const char *author;
  int32_t amend; size_t count; const char *const *paths;
} VdVcsWriteRequest;

typedef struct VdVcsWriteResult {       /* op, ok, stdout_text, stderr_text, note */
  int32_t op; int32_t ok; char *stdout_text; char *stderr_text; char *note;
} VdVcsWriteResult;

uint64_t vd_vcs_write_job(struct VdService *service, const struct VdVcsWriteRequest *request);
int32_t  vd_vcs_write_poll(struct VdService *service, uint64_t job, struct VdVcsWriteResult *out);
void     vd_vcs_write_free(struct VdVcsWriteResult *result);
```

错误模型：
- **同步拒绝**（不建 job，返回 0）：`request == NULL`、未知操作码、root/message/author/paths 非 UTF-8；
- **job 失败**（`VD_STATUS_FAILED` + `vd_service_error`）：非工作树、后端不匹配、缺 message、
  越界路径 → `VD_ERR_INVALID_ARGUMENT`；进程无法启动 → `VD_ERR_SERVICE`；
- **正常结果**（`VD_STATUS_READY`）：`ok=1/0` 是工具退出码，`stderr_text` 是真实原因；
- poll 误用：null out → `-1`；未知 job id → `-1` + `VD_ERR_JOB_UNKNOWN`；跨服务 job → `-1` +
  `VD_ERR_WRONG_JOB_KIND`；`vd_vcs_write_free(NULL)` / 未填充结构安全。

## 4. 两处旧断言替换（AC-3.6；Non-goals C9 允许的唯二例外）

| 位置 | 之前 | 之后 |
|---|---|---|
| `crates/vd-vcs/tests/readonly.rs` | 「所有写子命令被读闸门拒绝」（6 个测试） | 保留全部原断言，**加**写白名单正/反断言 + 双表不相交 + 默认拒绝（10 个测试，净 +1；断言覆盖点更多） |
| `src/workspace/vcs/vcs_selftest.cpp` | `vcs.ffi.read-only-surface`（恒真，1 check） | `vcs.ffi.write.*` 9 checks + `vcs.write.*` 5 checks；selftest 总 checks 35 → **48**，failures 0 |

理由：C ABI 已有写入口，旧的「恒真」断言不再证明任何东西；新断言证明的是**读路径仍拒绝写、
写路径按白名单逐项放行、未列操作默认拒绝**，覆盖点只增不减。

## 5. 观测到的门禁结果（命令 → 结果 → 日志）

| # | 门禁命令 | 结果 | 日志 |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh` | **exit 0**（Rust 层 + configure + build + ctest + xcb smoke） | `logs/40-gate-build-qt-summary.log` |
| 2 | ctest（同上） | **38/38 passed, 0 failed**；`vedit-workspace-vcs-selftest` Passed | `logs/40-gate-build-qt-summary.log` |
| 3 | `cargo clippy --workspace --all-targets -- -D warnings` | **exit 0**（`vedit-build:local`，容器内） | `logs/50-gate-clippy-fmt.log` |
| 4 | `cargo fmt --all -- --check` | **exit 0**（容器 + `RUSTUP_TOOLCHAIN=stable` 主机） | `logs/50-gate-clippy-fmt.log`、`logs/70-gate-docs-fmt.log` |
| 5 | `bash build/gates/docs.sh` | **exit 0** | `logs/70-gate-docs-fmt.log` |
| 6 | `vedit-app --selfcheck`（镜像内 offscreen） | **exit 0，290 checks / 0 failures** | `logs/60-gate-selfcheck-lrelease.log` |
| 7 | `lrelease translations/vedit_zh_CN.ts` | **607 finished, 0 unfinished** | `logs/60-gate-selfcheck-lrelease.log` |

Rust 测试（`cargo test -p vd-vcs -p vd-ffi`，同一镜像）：`vd-ffi` lib **45/45**、`abi` **16/16**、
`qt_abi_guard` **1/1**；`vd-vcs` lib **6/6**、`readonly` **10/10**、`write` **9/9**，0 failed。

`check_ffi_header_drift`（typecheck 门禁的一部分）本轮沿用 Non-goal B2 豁免（无 git 交付形态，
比较 committed vs worktree 按定义必红）；其**目的**由 `logs/30-header.log` 以更强方式证明：
强制 build.rs 重跑后 cbindgen 输出与消费中的头文件 **逐字节相同**（同一 sha256）。

## 6. 复现

```bash
bash evidence/vcs-write/run-all.sh
# 产出 logs/: 00-context, 10-rust-e2e, 20-selftest, 30-header
```

探针即 AC-0.4 的实现：
- `run-rust-e2e.sh` / `run-selftest.sh` / `check-header.sh` 启动第一句断言
  `pwd -P != /vedit-build`（VEdit 工作树），然后在 `mktemp -d` fixture 内执行；
- `run-rust-e2e.sh` 在 fixture 内跑 `cargo test`，并对比工作树 `git status` 快照
  （before/after 均 19 条 → `worktree_unchanged=yes`）；
- 真实 git 往返（stage → unstage → commit → 修改→stage→unstage→discard → `git status --porcelain -uall` 为空）
  在 `tempfile::tempdir()` 内完成，断言 fixture 不在 `CARGO_MANIFEST_DIR` 之下。

## 7. 我如实登记的发现与边界（未修，不属于本任务合同）

1. **读路径在交付基线 git 1.8.3 上不可用**（重要，影响批 3/4/6 的「VCS 面板」验收口径）：
   `vd-vcs` 的读路径给 `git` 传全局 `-C <root>` 和 `--porcelain=v1`，二者在 CentOS 7 的
   git 1.8.3.1 上都被拒绝：
   ```
   $ git -C "$d" status --porcelain=v1 --branch   # git 1.8.3.1
   Unknown option: -C          (exit 129)
   $ git status --porcelain=v1 --branch           # 无 -C 也失败
   …usage…                     (exit 129, --porcelain=v1 不被接受)
   ```
   读闸门与读表按合同**保持不动**，所以本轮没有修；判断：真正的修复需要给 `CommandRunner` 增加
   工作目录参数（或把端口降到 `--porcelain`），属读路径/面板批次的决策。写路径已规避（cwd + 仓库相对路径）。
2. **`svn info --show-item` 需要 SVN 1.9+**：`service.rs` 的 SVN 读路径用 `--show-item revision/url`，
   而批 6 捆绑的是 **1.7.14**（Q6 裁定），1.7 无 `--show-item`。这是批 6b 的阻塞点，登记于此。
3. **SVN 写路径无真实 E2E**：本镜像与主机都没有 `svn`（`NO_SVN`），SVN 5 个写操作只有
   门禁/参数/argv 级测试，没有真实 subprocess E2E；按 AC-6.2 在批 6 补齐。不把 SVN 说成已实测。
4. **新增字符串未进 `.ts`**：3a 只加后端日志/错误文案（与既有 `vd-vcs` 文案同级），面板尚无写 UI；
   AC-3.7 的用户可见字符串随 3b 一起进 `translations/vedit_zh_CN.ts`。`lrelease` 仍 0 unfinished。
5. **证据目录不在契约的 write scope 列表里**：`evidence/vcs-write/` 是契约 Deliverable 明文要求
   （「证据落 evidence/vcs-write/」），故创建；代码改动全部在允许的写域内。
6. **gate 自身会重写日志**：`scripts/build-qt.sh` 会写 `evidence/m0-build/build-logs/`、
   `evidence/ffi-abi/logs/`（项目门禁的既有行为，非本次手工改动）。

## 8. 改动清单（代码）

| 路径 | 内容 |
|---|---|
| `crates/vd-vcs/src/write.rs` | **新增**：写操作枚举、写白名单、参数校验、`WriteRunner::run_write`、`RecordingWriteRunner`、`write()` |
| `crates/vd-vcs/src/lib.rs` | `pub mod write;` |
| `crates/vd-vcs/tests/readonly.rs` | 旧断言保留 + 写白名单/双表不相交/默认拒绝（9 → 10 tests） |
| `crates/vd-vcs/tests/write.rs` | **新增** 9 tests：操作码、后端不匹配、参数拒绝、argv 精确构造、真实 git 往返、tool-failure 语义 |
| `crates/vd-ffi/src/vcs.rs` | **新增** 写 job 三件套 + 9 个操作码常量 + `VcsWriteData` + 错误映射；读接口未改 |
| `crates/vd-ffi/src/service.rs` | `JobValue::VcsWrite` 变体 |
| `crates/vd-ffi/include/vd_ffi.h` | cbindgen 重新生成（+9 宏、2 结构、3 函数） |
| `crates/vd-ffi/tests/abi.rs` | **新增** 4 tests：码值稳定性、真实 git 往返、无效参数 5 类、POLL misuse/free 安全 |
| `src/workspace/vcs/vcs_selftest.cpp` | 旧恒真断言 → 9 个 ABI 写断言 + 5 个异步写探针（35 → 48 checks） |
| `translations/vedit_zh_CN.ts` | 未改（见 §7.4） |
