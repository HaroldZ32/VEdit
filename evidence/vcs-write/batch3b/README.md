# 批 3b · Git 基础写（stage/unstage/discard/commit/amend）证据（AC-3.3）

任务：`vcs-ui-git-a`（board **T13**，cell `cell-20261103T123639841Z`）。
契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/vcs-ui-git-a.md`。
写者：Senior Engineer（成员 `Senior Engineer 2`）。写范围：`src/workspace/vcs/`、`translations/vedit_zh_CN.ts`。
UI/自动刷新/i18n 的证据在 `evidence/vcs-ui/`；批 3a 的后端地基证据（写白名单 + 异步 job + C ABI）
在 `evidence/vcs-write/` 顶层，本目录只覆盖**本批的 UI 写路径**。

## 0. 一屏结论

| AC-3.3 要求 | 观测 |
|---|---|
| stage / unstage / discard 在 fixture 仓库真实执行 | `git status --porcelain` 断言：stage 后 `M  rtl/a.sv`、unstage 后 ` M rtl/a.sv`、discard 后 porcelain 为空 |
| commit（message + author）真实执行 | `git log -1 --pretty=%s` = `fixture: add new.sv`；`--pretty=%an <%ae>` = `VEdit Test <test@vedit.local>` |
| amend 真实执行 | `git log -1 --pretty=%s` = `fixture: add new.sv (amended)`，`git rev-list --count HEAD` 不变（2 → 2） |
| discard 前确认，取消零写入 | 确认回调返回 false → 面板拒绝、porcelain 保持 ` M rtl/a.sv`（真仓库）；stub 侧 `writeAttempts()==0` |
| amend 前确认，取消零写入 | 同上：`vcs.write.amend-confirm-zero-write PASS writes=4`；真仓库侧 amend 仅在确认后发请求 |
| 断言方式 | 真仓库：`git status --porcelain` + `git log`/`rev-list`（不是面板自述）；UI 侧：stub 记录到达 seam 的请求 |

## 1. 写路径的形状

面板 → `VcsSource::requestWrite()` → `FfiVcsSource` → `vd_vcs_write_job/poll/free`（批 3a 的 C ABI）
→ `vd-vcs::write` 的独立写白名单 → `git` 子进程（`Command::current_dir`，无全局路径选项）。

- 操作码：`GitStage=1, GitUnstage=2, GitDiscard=3, GitCommit=4`（与 `vd_ffi.h` 的
  `VD_VCS_WRITE_GIT_*` 逐值一致，lane 断言 `vcs.seam.write-op-codes`）。
- 实际命令（`crates/vd-vcs/src/write.rs`）：`git add -- <paths>`、`git reset HEAD -- <paths>`、
  `git checkout -- <paths>`、`git commit -m <msg> [--author=…] [--amend]`。
- GUI 线程只发 job + 轮询（10 ms QTimer），不阻塞。
- 不可逆操作（Discard / SVN revert）在面板层强制确认；确认器可注入（`setConfirmProvider`），
  默认是 `QMessageBox`；**offscreen 且无注入时拒绝**（不静默毁数据）。

## 2. 关键缺陷：读/写并发撞 `index.lock`（本批发现并修复）

E2E 探针最初是**间歇性**红的：

```
CHECK vcs.write.e2e.stage-new-ok FAIL The VCS operation failed:
  fatal: Unable to create '…/.git/index.lock': File exists.
```

根因：`git status` 会顺带刷新/写回 index，因此**也会取 `.git/index.lock`**。面板的合并刷新
可能在写 job 进门时正好有一个 status job 在飞，两个 `git` 进程就对同一把锁并发。
这是真实用户会遇到的竞态（保存后刷新与点 Stage 撞在一起），不是测试噪声。

修复（`src/workspace/vcs/vcs_source.h/.cpp`，本任务写范围内）：`FfiVcsSource` 把读与写**串行化**——
- 写请求在 `status_job_` 在飞时进入 `queued_write_`，等读结束由 `pumpQueued()` 启动；
- 状态请求在写 job 在飞时进入 `deferred_status_`（只保留最新一条，读可重放），写结束后再读；
- 定时器在「无 job 且无排队」时才停。

修复后连续 3 次全量 lane selftest 均 `ok=1 checks=144 failures=0`（`logs/` 中为最后一次）。
这与需求 Non-goals C9「读路径仍拒绝写 + 写路径按白名单放行」不冲突：读表/写表未改动，只改了
调度顺序。

## 3. 探针证据（`logs/10-write-e2e.log`）

真仓库段（`QTemporaryDir` 下新建仓库，先断言 `fixture=/tmp/…`、`cwd=/tmp/vedit-vcs-write-…`，
即 **cwd 不是 VEdit 工作树**）：

```
CHECK vcs.write.e2e.not-worktree        PASS fixture=/tmp/…/write-repo cwd=/tmp/vedit-vcs-write-…
CHECK vcs.write.e2e.stage-ok            PASS Staged rtl/a.sv
CHECK vcs.write.e2e.staged-porcelain    PASS M  rtl/a.sv
CHECK vcs.write.e2e.unstage-ok          PASS
CHECK vcs.write.e2e.unstaged-porcelain  PASS  M rtl/a.sv
CHECK vcs.write.e2e.discard-cancel      PASS           # 确认返回 false
CHECK vcs.write.e2e.discard-cancel-zero-write PASS  M rtl/a.sv
CHECK vcs.write.e2e.discard-ok          PASS Discarded the working-tree changes of rtl/a.sv
CHECK vcs.write.e2e.discarded-porcelain PASS（空）
CHECK vcs.write.e2e.commit-subject      PASS fixture: add new.sv
CHECK vcs.write.e2e.commit-author       PASS VEdit Test <test@vedit.local>
CHECK vcs.write.e2e.amend-subject       PASS fixture: add new.sv (amended)
CHECK vcs.write.e2e.amend-rewrote-one-commit PASS 2 -> 2
```

UI 侧（stub 记录到达 seam 的写请求，验证「取消 = 零写入」与参数完整性）：

```
CHECK vcs.write.discard-confirm-declines  PASS
CHECK vcs.write.discard-confirm-zero-write PASS writes=0
CHECK vcs.write.commit-requires-a-message PASS
CHECK vcs.write.commit-empty-is-zero-write PASS
CHECK vcs.write.amend-confirm-declines    PASS
CHECK vcs.write.amend-confirm-zero-write  PASS writes=4
CHECK vcs.write.amend-op-and-flag         PASS writes=5     # op=GitCommit, amend=true, message=rewrite
CHECK vcs.write.failure-reason-visible    PASS The VCS operation failed: fatal: pathspec …
```

## 4. 边界

1. **SVN 写操作**：seam 与 C ABI 已支持 `SvnCommit/Update/Revert/Add/Remove`（枚举值有断言），
   但本批面板只暴露 Git A 操作条；SVN 面板的同等易用性属批 6（契约 `svn-backend-panel` / 批 6b）。
2. **commit 的 paths 参数**：面板提交走「已暂存内容」（`git commit -m …`，不带 `--` 路径），
   与 SourceGit 的暂存区提交一致；按路径提交在 `WriteRequest.paths` 里已可用，但本批 UI 不使用。
3. **暂存列是会话内覆盖**（读 ABI 不区分 index/worktree 两侧）——详见
   `evidence/vcs-ui/README.md` §4.2。
4. **E2E 只在 `/tmp` fixture**：脚本与探针都断言 cwd/仓库不在 VEdit 工作树内。

## 5. 复现

```bash
bash evidence/vcs-write/batch3b/run-write-e2e.sh
# 期望：write_e2e_checks=26、无 FAIL、write_e2e=PASS
```

七道门禁见 `evidence/vcs-ui/README.md` §5（`logs/gate-summary.log`）。
