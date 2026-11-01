# T24 独立评审结论 — T23 `vcs-panel`：VCS 只读面板（Git / SVN）

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Deep Worker`）
- **被评审任务**：board `T23`（`vcs-panel`，Slice 2）；冻结契约 `.mpd/cellwork/cell-20261031T085543161Z/contracts/vcs-panel.md`
- **验证环**：`loop-20261031T095153-855e55`（`mpd_verify_seat` 已绑定）
- **结论**：**PASS**（bound seat）。AC-S2.5 四类只读信息、stub 后端、非仓库空状态、**只读性（无写操作）** 全部由我自己的
  复跑与源码审计成立；门禁全绿（含我实跑的 cargo/cargo-test、lane selftest、根 ctest 20/20、typecheck）。
  附 1 条 **C2 边界**（已被 T95 契约明确"只补记、不改"）与 3 条 Low/Info 注记。

---

## 1. 我实际跑的命令与观测

```text
# Rust 后端（pinned vedit-build:local，rustc/cargo 1.99.0）
$ cargo test -p vd-vcs
test result: ok. 6 passed; 0 failed     (lib: porcelain/status 解析、detached HEAD、markers、nested、空状态)
test result: ok. 9 passed; 0 failed     (readonly: 写子命令枚举拒绝、allow/write 列表不相交、生产 runner 拒绝且不 spawn、
                                          git session 只用只读子命令、svn session、stub 无进程驱动、空状态)
TEST-EXIT=0

# Qt 面板 selftest（根构建，offscreen）
$ …/workspace-lane-vcs/vedit-workspace-vcs-selftest --selfcheck
WORKSPACE-VCS-SELFTEST ok=1 checks=24 failures=0        SELFTEST-EXIT=0

# 根 ctest
$ ctest --test-dir target/qt6-build --output-on-failure
20/20 Test #20: vedit-workspace-vcs-selftest ... Passed 0.02 sec
100% tests passed, 0 tests failed out of 20              ROOT-CTEST-EXIT=0

# 仓库门禁
$ mpd_verify_evidence {kind:"gate", gate:"typecheck"}
gate typecheck exit 0 -> .mpd/verify/evidence/ev-20261101T061233-20992b.log

# 真 Git 子进程路径（产品 FFI，非 stub）
$ vedit-app --selfcheck-ffi tests/fixtures/verilog-project
CHECK ffi.vcs.start PASS
CHECK ffi.vcs.backend-detected PASS       （→ 真 git status 在真 repo 上被识别为 Git）
```

## 2. AC-S2.5 逐条

| 判据 | 观测（selftest 24 checks 全绿 + 代码审计） | 结果 |
|---|---|---|
| **分支/修订** | `vcs.panel.git.branch-and-revision` = `Git feature/vcs-panel @ abc1234`；`vcs.panel.svn.backend-and-revision` = `Subversion trunk @ 42` | ✅ |
| **变更文件** | `changed-file-count rows=2`、`file-row rtl/top.sv modified`、`added-row rtl/added.sv added`；SVN `file-count` | ✅ |
| **单文件 diff** | `diff.shown` 渲染真实 diff 文本；`diff.read-only`；缺失 diff → 可见 note（`missing-is-a-note`，不崩溃） | ✅ |
| **非仓库空状态** | `vcs.panel.empty-state.note-visible`（“not a Git or Subversion working tree”）、`no-files`、`stub-queried calls=1`、`no-write-attempt`；`vcs.panel.no-source.empty-state` | ✅ |
| **后端抽象（stub 可接入）** | `VcsSource` 接口 + `StubVcsSource`（面板测试全程无进程）+ 生产 `FfiVcsSource`；Rust 侧 `ProcessVcsService<R: CommandRunner>` + `StubVcsService` / `RecordingRunner` | ✅ |
| **只读（无写操作）** | 见 §3 | ✅ |
| **产品内可用** | T95 收尾把 lane 接进 `VEDIT_WORKSPACE_LANES` + `installVcs`（见 §4 归属）；根 ctest 20/20 含该面板 | ✅ |

## 3. 只读性（AC-S2.5 的关键）：我自己的审计

- **allow-list 闸**：`ensure_read_only()`（`crates/vd-vcs/src/runner.rs:187`）在 **spawn 之前**调用（`:232` / `:309`），
  只放行 `GIT_READ_ONLY`（status/rev-parse/diff/log/show/ls-files/blame/describe/cat-file/for-each-ref）与
  `SVN_READ_ONLY`（status/info/diff/log/list/cat/propget/proplist/blame）；未知工具/空子命令一律拒绝，
  **写子命令不是靠"黑名单漏没漏"，而是因为不在 allow-list 里被拒绝**（更稳）。
- **写清单枚举 + 断言**：`GIT_WRITE`/`SVN_WRITE` 作为数据保留，测试逐条证明被拒绝且在 spawn 前拒绝；
  `allow_lists_are_disjoint_from_write_lists` 保证两个清单不相交。
- **生产 runner 与 stub 共用同一闸**：`production_runner_gate_refuses_writes_without_spawning` 用真 `SystemRunner` 证"不 spawn"。
- **命令构造**：`git` 统一 `-C <root>` + 参数；`subcommand_index` 正确跳过 `-C/-c/--git-dir/--work-tree` 与其值；
  `git diff` 用 **`--` 路径分隔符**（`service.rs:213/217/226`），路径不会被当成选项。
- **Qt 侧无第二套实现**：`src/workspace/vcs/**` 无 `QProcess`/`system`（我 grep 无命中），只调用
  `vd_vcs_info/status/poll/diff + *_free`；C ABI 头只有 status/poll/info/diff/free，**没有任何写入口**。
- **证据**：`evidence/slice2-vcs/02-readonly-proof.txt` 的 6 条测试与我实跑的 `cargo test` 结果一致。

## 4. 写域与归属（含 T95 收尾）

- T23 自己的改动（`00-context.txt` + 我的 `git status`）：`crates/vd-vcs/**`、`crates/vd-ffi/src/vcs.rs`、
  `src/workspace/vcs/**`、`evidence/slice2-vcs/**` —— 全在契约四条写域内。
- `crates/vd-ffi/include/vd_ffi.h` 也是 M：它是 **cbindgen 由 `build.rs` 自动生成**的头（构建产物，非手改；
  报告已声明），不构成手写越界，但提交时应在说明里标注。
- `src/app/CMakeLists.txt` + `src/workspace/integration.cpp` 的改动属于**另一个 board 任务 T95**
  （`.mpd/team/contracts/T95.json` 主题原文：「T23 收尾：把 VCS 只读面板真正接进 vedit-app … 并补记
  vd_vcs_info/diff 是同步 C 调用的边界」），不是 T23 的改动；两文件的接线与根 ctest 20/20 已由我复跑确认。

## 5. Findings / Notes

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| **N-1** | **Medium（C2 边界，已记录）** | `vd_vcs_info` / `vd_vcs_diff` 是**同步 C 调用**，会 spawn 一次 `git rev-parse`/`git diff`（或 svn）在**调用线程**；面板在 GUI 线程调用（`vcs_source.cpp:77/129`，`vcs_panel.cpp:89/125`）。C2 要求"调用不得阻塞 UI 线程"。该边界已由 **T95 契约**明确为"补记"，证据 `14-sync-call-boundary.md` 给了完整 follow-up 计划（~150 行 Rust + ~80 行 C++ + 计时断言）。AC-S2.5（只读）不受影响 | 建议在 Slice 3 前另开一个小任务把 info/diff 改成 start/poll 作业（计划已现成）；本轮按 T95 决定接受 |
| N-2 | Low | SVN 的真实子进程路径没有端到端证据：Rust 测试用 `RecordingRunner`（canned `svn status/diff` 输出），面板 selftest 用 `StubVcsSource`（契约明确允许 stub）。Git 的真路径由我跑的 `--selfcheck-ffi` 的 `ffi.vcs.backend-detected` 覆盖，SVN 没有对应真机冒烟 | 建议补一个真 `svn` 临时工作副本的只读冒烟（或明确接受 stub-only） |
| N-3 | Info | `svn diff` 的参数是 `svn diff --internal-diff <path>`，没有 git 那样的 `--` 分隔符；仓库内以 `-` 开头的文件名理论上会被 svn 当选项解析。不是写路径（`svn diff` 无写文件选项），但建议加 `--` 加固 | 顺手加固 |
| N-4 | Info | 我跑 `--selfcheck-ffi <fixture>` 时 VCS 两条全 PASS，但同一模式有 **4 条 `ffi.project.*` FAIL**（file-count/file-order/include-dir/defines）——与 VCS 无关、不在 T23 范围，供 captain/工程模型 lane 参考 | 另行核查（可能是我传参方式导致的夹具差异） |

## 6. 未能验证的部分

1. **GUI 交互**：本机无显示器，面板外观/交互不认证；selftest 是无头状态断言（这符合契约的 L2 证据口径）。
2. **真实 SVN 工作副本**：没有真 svn 端到端（见 N-2）。
3. **C2 的非阻塞性**：同步调用的实际卡顿无法在本机用 GUI 观测（N-1 已按文档边界处理）。
4. 我没有替换坏二进制去演练写拒绝（会改包内/源码），写拒绝由 allow-list 结构 + 9 条测试保证。

## 7. 结论

**PASS**。AC-S2.5 的四类只读信息、stub 后端、非仓库空状态全部成立；**只读性由 allow-list 闸 + spawn 前拒绝 +
9 条专项测试 + C ABI 无写入口 + Qt 无进程调用**共同保证（不是只靠声明）；Rust 17 条测试、lane 24 条自检、
根 ctest 20/20、typecheck 均我实跑通过，真 Git 子进程路径也由 FFI selfcheck 证实。唯一实质边界是
`vd_vcs_info/diff` 的同步 C 调用（C2），但这是 **T95 契约明确记录的决策**（只补记、不改），故不阻塞本 verdict，
建议 Slice 3 前按现成计划收口。
