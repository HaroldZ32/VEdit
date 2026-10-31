# 人工停点 1（T16）—— 冻结树全绿，停在 in_progress 等用户验收

generated: 2026-10-31T12:03:34Z
git_head: 06fd6821562e28d77ad61b0d5c0461167cb95d3c
git_branch: feat/qt6-ktexteditor-migration
git_status_porcelain: (clean)

## 结论

Slice 1 已冻结并推到 `origin/feat/qt6-ktexteditor-migration`，PR **#11**（base=`dev`）为该交付的唯一出口。
**未合并、未打 tag、未发 Release。** 任务保持 `in_progress`：完成权在用户，不在任何 agent。

## 门禁（冻结树，真实输出）

| 门禁 | 命令 | 退出码 | 观察到的结论 |
|---|---|---|---|
| typecheck | `bash build/gates/typecheck.sh` | **0** | `[typecheck] PASS (pinned image vedit-build:local)`，容器横幅 `CentOS Linux release 7.9.2009 (Core) \| rustc 1.99.0 \| cargo 1.99.0` |
| 构建 + 测试 + 冒烟 | `bash scripts/build-qt.sh` | **0** | `100% tests passed, 0 tests failed out of 16`；`M0-SMOKE ok=1 ... window=1 editor=1` |

原始日志：`21-typecheck-final.log`（38KB）、`22-build-qt-final.log`（40KB）。

### 第一次尝试是红的（如实保留）

| 项 | 值 |
|---|---|
| 时间窗 | 19:51:37 → 19:52:17 (+0800) |
| typecheck | `TYPECHECK-EXIT=0` |
| build-qt | **`BUILD-QT-EXIT=8`** |
| ctest | `94% tests passed, 1 tests failed out of 16` |
| 唯一失败 | `13 - vedit-workspace-search-selftest`（`ok=0 checks=102 failures=7`，7 条全 `jump.product.*`） |
| 根因 | **构建竞态**：T69 的 `src/workspace/search/search_selftest.cpp` mtime = `19:52:24`，落在构建窗口内 —— 编译进来的是中间态 |

原始日志：`12-build-qt.log`、`13-ctest-red.txt`、`10-pre-run-tree.txt`。
按 captain 指令，红就是红：**没有改测试、没有改门禁、没有把红写成绿**，等 T69 收敛后原样重跑才得到 16/16。

## 提交

- 本轮 `95c496e..06fd6821562e28d77ad61b0d5c0461167cb95d3c` 共 **14** 个提交（12 个分组提交 + C9 补记 + PR 证据）
- 分支相对 `dev` 共 **46** 个提交
- `dev` 仍为 `eebbfd14a2311cd4bca533fe98d457a01e46e2e6`，`main` 仍为 `fa3e8b13e976c25cfddf46b195d3cf21619d1ba4`（未推送）
- 远端 head 与本地 HEAD 一致（`25-push.txt` 有 IDENTICAL 校验）

## PR

- URL：https://github.com/HaroldZ32/VEdit/pull/11
- 标题：`feat(qt6): v2 Slice 1 迁移 — 人工停点 1（冻结树全绿，待用户在有屏机器验收）`
- `state=OPEN`、`base=dev`、`mergeable=MERGEABLE`、非 draft、**45 commits**
- CI：本 PR 现在**已有 workflow**（`build the pinned image`、`gates and release build (pinned image)`），记录时刻为 `pending`，尚未出结论（见 `26-pr.txt`）

## 积压落盘（captain 指定）

1. `evidence/captain-exemptions.md` —— **7 条**写域豁免（第 7 条 T10 edit-core 由 captain 于 2026-10-31T11:56Z 补认）+ `src/app/{app_shell.h,app_shell.cpp,main.cpp}` 的 **T47+T71 双 lane 共写**判读提示。
2. **C9 重采/补记**（原始采集行一格未改，补记为独立文件）：
   - `evidence/ffi-abi/00-collect-recollect-20261031T120207Z.txt`（原 HEAD `9957f5f`）
   - `evidence/highlight-v2/00-collect-recollect-20261031T120207Z.txt`（原 HEAD `8fedbdd`）
   - `evidence/docs-amendments/05-builtin-kde-folding.backfill-20261031T120207Z.txt`（原文件整份无 C9 头，未改原文件）
   - `evidence/fix-filetree-open/00-c9-backfill-20261031T120207Z.txt`（原 01–05 五份无头）
3. **两份只存在于邮件的 PASS verdict**：
   - `evidence/verdicts/gate-infra/verdict.md`（T55，含对「`exit 7`」的更正：实测 **101**（clippy 失败）/ **1**（`no_gate`），全树无 `exit 7`）
   - `evidence/verdicts/repo-hygiene/verdict.md`（T57，四项论断逐条对了一手证据）
   两份均注明「由 captain 依据 cell mail 记录整理落盘、**Reviewer 本人未二次签署**」。

## 未验证项（如实）

- **GUI 一律未验收**：本机无显示服务器。窗口交互、高亮观感、折叠/面包屑、Ctrl+S 的实际观感与诊断是否在界面上出现 —— 只有用户在有屏机器上能确认。
- `M0-SMOKE ok=1` 是 **xcb + 软件渲染**的无头冒烟，不等于人眼验收。
- CI workflow 尚未出结论（记录时刻 pending）。
- 未跑全量 MPD 门禁：`rows`/`comments` 为仓库声明式 N/A→RED，与本停点无关。

## 用户验收步骤

```bash
git pull
bash scripts/build-qt.sh
vedit-app --project <真实工程目录>
```
然后：双击打开 3 个文件 → 改一行 → **Ctrl+S** → 看诊断是否出现。

## 红线自检

| 红线 | 状态 |
|---|---|
| 不合并 | PR `state=OPEN` ✅ |
| 不打 tag | HEAD 上 0 个 tag；远端 tag 仍只有既有 `v0.1.0`/`v0.1.1` ✅ |
| 不发 Release | 仍是既有 `v0.1.1`/`v0.1.0` ✅ |
| 不 push `dev`/`main` | dev=`eebbfd1`、main=`fa3e8b1`（与会话开始时一致）✅ |
| 不 force-push / 不 amend 已发布历史 | 本轮全部为追加提交 ✅ |
| 不改测试/门禁凑绿 | 未改任何测试或门禁；第一次的红原样保留 ✅ |
| 不用截图冒充 GUI 验收 | 未做任何此类声称 ✅ |
| 停在 in_progress | 任务状态 `in_progress`，不 complete ✅ |
