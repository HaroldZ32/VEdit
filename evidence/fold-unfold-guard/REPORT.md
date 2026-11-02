# T122 — unfold guard: implemented, verified in isolation, but BLOCKED at integration

collected_at: 2026-11-01T19:47:00+08:00
git_head: 60c7ae9
## 做了什么（代码已回退，补丁留档在 attempt.patch）

`FoldInjector::unfoldRegistered()` 的守卫版实现：
- outer-first（跨度大者优先，tie 按 startLine 升序）；
- 设光标前检查目标行是否被仍折叠的 Range 覆盖，覆盖则**跳过并计数**（`unfoldSkippedHidden`）；
- 记录 `unfoldHiddenTargetMoves`（不变量：正常模式必须为 0）；
- `unfold` 返回 false 的 Range **保留在 registry**（`unconfirmed_`），不无条件清空；
- 恢复原光标；原光标若被剩余折叠覆盖则不恢复（`cursorRestoreSkipped`）；
- 3 轮重试（外层展开后重试内层/失败项）。

## 隔离验证结果（证据 03-clean-worktree-label.log 中的 FOLD-UNFOLD-GUARD 段）

- 干净 release：`hidden 975 → 0`，registry 清空，`unfoldHiddenTargetMoves=0`，光标恢复；
- 用户手动 unfold 后再 release：registry 保留未确认项（8 条），不变量仍为 0；
- **反向控制：旧 inner-first 行为 → `unfoldHiddenTargetMoves=175`（与 T120 期望一致）**；
- `FOLD-UNFOLD-GUARD ok=1 checks=19 failures=0`。

## 为什么仍然不能交付

1. **集成崩溃**：在干净 worktree（HEAD + 本补丁）里 `vedit-workspace-search-selftest` **SEGFAULT**；
   同一 worktree 把 `fold_injector.*` 换回 HEAD 后**105/0 PASS** → 崩溃由本补丁引入。
   trace 显示崩溃点在第二次打开（b.sv）的 `rebuild`（注入阶段），而不是本补丁的光标移动本身；
   推测为 release 后仍有 KTextEditor 折叠状态残留（a.v 的 `unfold` 返回 false），
   与带折叠文档的 `setText()`/再次 `fold` 交互导致。没有足够时间在不破坏真实打开路径的前提下解决。
2. **干净 HEAD 本身不可构建**（另一个 lane 的未提交改动）：HEAD 60c7ae9 的
   `crates/vd-ffi/include/vd_ffi.h` **没有** `vd_vcs_info_job`/`vd_vcs_diff_poll` 声明，而
   `src/workspace/vcs/vcs_source.cpp` 已调用它们 → 干净 worktree configure/build 失败
   （证据 `02-head-not-self-consistent.log`）。因此"干净 worktree ctest 30/30"这个验收门槛本轮
   无法满足（不是本补丁的问题，但挡住了验证）。
3. 本轮主树里所有折叠改动已 `git checkout` 回退；`attempt.patch` 保存完整实现与测试，供下一位接手。

## 建议

- 先修 HEAD 自一致（提交 vd_ffi.h 的 VCS 声明），再开"折叠 release 集成"专项；
- 或许根本方向应改为**依赖 KSH 原生折叠**（我们的 XML 已有 beginRegion），命令注入折叠与
  KTextEditor 的 folding 状态反复交互是这次崩溃的温床。
