# CHECKPOINT 1 —— 首次 push + 开 PR(base=dev)

generated: 2026-10-31T10:41:47Z
git_head: 8219324042826491e1bca7d5379cbd253225c0fa
git_branch: feat/qt6-ktexteditor-migration
task: T51（船长派发；用户直令「记得及时传GIT，你目前在的服务器没接屏幕看不见GUI」）

## 目标

把已有成果首次落到远端，消除单点丢失风险。**不是完成态**，不合并不打 tag 不发 Release。

## 结果

| 项 | 值 |
|----|----|
| PR | https://github.com/HaroldZ32/VEdit/pull/11 |
| base | `dev` @ `eebbfd14a2311cd4bca533fe98d457a01e46e2e6` |
| head | `feat/qt6-ktexteditor-migration` @ `8219324042826491e1bca7d5379cbd253225c0fa` |
| 本轮提交 | 18 个（`9957f5f..8219324042826491e1bca7d5379cbd253225c0fa`） |
| push | `git push -u origin feat/qt6-ktexteditor-migration` → `* [new branch]`，exit 0；后续两次 push 追加至同一 PR |
| 远端 head 校验 | `git ls-remote --heads origin` 与本地 HEAD 一致 |

## 提交分组（18）

| `f349342` | chore(wip): checkpoint rust service crates and ffi build wiring |  |
| `6204865` | chore(wip): checkpoint in-repo verilog fixtures and verification script |  |
| `057b6f1` | chore(wip): checkpoint app shell and rootcheck cmake |  |
| `4929488` | chore(wip): checkpoint qt6 build scripts |  |
| `e065c77` | chore(wip): checkpoint handover docs and third-party licensing registration |  |
| `a7ce7e9` | chore(wip): checkpoint lane evidence (syn-grammars, term-panel, workspace lanes) |  |
| `3cdbbe9` | chore(wip): checkpoint app shell entry point and cmake |  |
| `3d7e1f8` | docs(evidence): record checkpoint 1 push, PR url and frozen commit set |  |
| `6e6283e` | feat(filetree): checkpoint filetree double-click open fix (T39 complete) |  |
| `a179b6c` | docs(amendments): checkpoint AMENDMENTS rev2 and its evidence |  |
| `0dd3a4a` | test(evidence): checkpoint C9 verdict header batch |  |
| `0904a1d` | chore(ffi): checkpoint slice 0 ffi abi evidence and qt6 build logs |  |
| `2808280` | build(syntax): checkpoint grammar lane cmake wiring |  |
| `8b940f2` | feat(project-model): checkpoint slice 1 project model lane (in flight) |  |
| `13462f3` | feat(lsp): checkpoint slice 1 verible lsp lane (in flight) |  |
| `5668c43` | feat(syntax): checkpoint syntax product seam (in flight) |  |
| `8fedbdd` | fix(ffi): checkpoint vd-ffi project mirror (in flight) |  |
| `8219324` | docs(evidence): record T5 independent review verdict for slice 0 ffi (PASS with findings) |  |

每条提交正文都带 `checkpoint: 船长要求提前落远端，不代表该子系已完成`。

## 判定方法：谁被提交、谁被跳过

并行 lane 在同一棵树上写。判定不靠猜：

1. **冷却线**：路径最新 mtime 距今 > 300s 才进候选。
2. **sha256 稳定性探针**：对候选逐个取样，隔 60–75s 再取一次；两次一致才提交（见提交正文）。探针期间仍在变的 —— `crates/vd-config/src/filelist.rs`、`crates/vd-ffi/src/project.rs`、`crates/vd-config/tests/project_model_fixture.rs`、`evidence/highlight-v2/03-build-qt-gate.log` —— 当轮跳过。
3. **0 字节 / 目录级新生成**：`filetree-testdata/` 10,007 文件（10,006 个 0 字节）整体跳过。
4. 跳过清单见 `04-skipped-hot.txt`、`06-skipped-final.txt`。

## 重要成果：T5 盲审 PASS

`evidence/verdicts/ffi/verdict.md`（`8219324`）：Slice 0 FFI 盲审 **PASS（Medium findings）**，findings 集中在门禁基础设施与证据交付，不阻塞。

## 易失日志口径

`evidence/ffi-abi/logs/19–23,26,27` 与 `evidence/m0-build/build-logs/31–34,37` 被并发构建反复重写，已在 `0904a1d` **整体提交一次**；其后的重写是噪声，按船长指示不再逐条补交。

## 待决问题

`filetree-testdata/`（10,007 文件 / 376K，10,006 个 0 字节）未提交，请裁定入库或 `.gitignore`。

## 验证边界（诚实标注）

- **无头可证**：构建、单测、gates、Xvfb 无头 selftest、诊断日志、ELF/GLIBC、AppImage SHA256。
- **只能由用户在有屏机器上验收**：一切 GUI 现象。执行服务器**无显示服务器**；「截图看起来对」「我认为它能打开」都不构成人工验收。
- 本 checkpoint **未跑完整 gate**，提交内容是并行 lane 快照，**不代表可构建状态**。
- 机器检查：11 个 workspace member 在提交树中全部存在（`git cat-file -e HEAD:<m>/Cargo.toml` 全 OK）。
- `typecheck` 门禁红是已知基础设施缺陷（T54），不是 lane 失败。

## 未做的事（红线遵守）

未合并 PR、未 push dev/main、未打 tag、未发 Release、未 amend/rebase/force-push、未修改任何产品代码。
