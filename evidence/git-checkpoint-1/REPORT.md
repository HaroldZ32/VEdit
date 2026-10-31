# CHECKPOINT 1 —— 首次 push + 开 PR(base=dev)

generated: 2026-10-31T10:34:14Z
git_head: 3cdbbe92d4b13183cceadc43c18c49fe088be84e
git_branch: feat/qt6-ktexteditor-migration
task: T51（船长派发；用户直令「记得及时传GIT，你目前在的服务器没接屏幕看不见GUI」）

## 目标

把已有成果第一次落到远端，消除单点丢失风险。**不是完成态**，不合并不打 tag 不发 Release。

## 结果

| 项 | 值 |
|----|----|
| PR | https://github.com/HaroldZ32/VEdit/pull/11 |
| base | `dev` @ `eebbfd14a2311cd4bca533fe98d457a01e46e2e6` |
| head | `feat/qt6-ktexteditor-migration` @ `3cdbbe9` |
| checkpoint 提交 | 7 个（`f349342` `6204865` `057b6f1` `4929488` `e065c77` `a7ce7e9` `3cdbbe9`） |
| 变更规模 | 133 文件 / +12,841 / −12 |
| push | `git push -u origin feat/qt6-ktexteditor-migration` → `* [new branch]`，exit 0 |
| 远端 head 校验 | `3cdbbe92d4b13183cceadc43c18c49fe088be84e refs/heads/feat/qt6-ktexteditor-migration` |

## 提交分组

| commit | 子系 | 文件数 |
|--------|------|--------|
| `f349342` | Rust 服务层 + FFI 构建接线（Cargo.toml/Cargo.lock/CMakeLists.txt、cmake/VEditRustFfi.cmake、crates/vd-{ffi,search,settings,vcs}） | 32 |
| `6204865` | 仓库内 fixture + verify-fixture.sh | 22 |
| `057b6f1` | rootcheck cmake | 1 |
| `4929488` | Qt6 构建脚本 | 2 |
| `e065c77` | 文档 + 第三方许可登记 | 3 |
| `a7ce7e9` | 各 lane 证据（syn-grammars / term-panel / ws-filetree / ws-palette / ws-search / fixture / highlight-v2） | 66 |
| `3cdbbe9` | app shell 入口与 cmake | 2 |

每条提交正文都带：`checkpoint: 由船长要求提前落远端，不代表该子系已完成`。

## 未提交（并行 lane 在飞）

见 `04-skipped-hot.txt`。判定规则：该路径最新 mtime 距今 ≤ 300s 即跳过，避免把半写文件固化。
突出项：`crates/vd-config/`、`crates/vd-lsp/`、`src/workspace/filetree/`、`src/syntax/`、`docs/handover/appendix/amendments-rev2.md`、`evidence/{m0-build/…,verdicts,ffi-abi,docs-amendments,fix-filetree-open}`。

## 待决问题（请船长裁定）

`filetree-testdata/` 有 **10,007 个文件 / 376K**，是 filetree 性能 lane 的生成数据，采集时仍在生成。
本 checkpoint **未提交**。建议：入库为 fixture，或加 `.gitignore` 由脚本生成。
其它 lane 若也生成了大目录，请用同一标准裁定。

## 验证边界（诚实标注）

- **无头可证**：构建、单测、gates、Xvfb 无头 selftest、诊断日志、ELF/GLIBC 检查、AppImage 产物与 SHA256。
- **只能由用户在有屏机器上验收**：一切 GUI 现象。执行服务器**无显示服务器**；"截图看起来对""我认为它能打开"都不构成人工验收。
- 本 checkpoint **未运行完整 gate**：提交内容是多个并行 lane 的快照，**不代表可构建状态**。
- 已做的机器检查：`Cargo.toml` 的 11 个 workspace member 在提交树中均存在（`git cat-file -e HEAD:<member>/Cargo.toml` 全部 OK）。

## 未做的事（红线遵守）

未合并 PR、未 push dev/main、未打 tag、未发 Release、未 amend/rebase/force-push、未修改任何产品代码。
