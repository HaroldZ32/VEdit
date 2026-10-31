# 独立评审 verdict — T61 rev-fix-filetree-provider（T60 回归修复：filetree / palette 打开路径）

> **契约口径说明**：本任务**无冻结契约与 verify loop**——它是由 captain 用 `agent_teams_plan create_task` 手工建立的（只有 `cellwork_activate` 产生的任务才会带冻结契约与 loop）。因此本评审**不执行** `mpd_verify_seat` / `mpd_verify_record`（captain 已裁定：loop 不存在、不要找）。判据取自 captain 派单书（2026-10-31 19:27 的 T61 任务书 + 后续裁定 mail）与其引用的事实；落盘路径由 captain 明确授权。

- Cell: `cell-20261031T085543161Z` · 评审任务: **T61** · 被评 work: **T60**（Senior Engineer 2，修复 T10 引入的产品级回归）
- 判据来源: captain 的 T61 角色化任务书（判据 1–5）+ 2026-10-31 19:27 / 19:4x 两封裁定 mail
- 评审人: **Reviewer**（独立、只读；未修改任何源码/测试/门禁）
- 评审时刻: 2026-10-31 19:27–19:40（Asia/Shanghai）· 基线 HEAD `95c496e` + T60 的未提交改动（`src/workspace/filetree/`、`src/workspace/palette/`、`evidence/edit-core-regression/`）
- 环境: `vedit-qt6-build:local`（KTextEditor 6.24.0 / Qt 6.10.2 / g++ 15.2.0），offscreen + 真实鼠标事件
- 风险等级: **Medium**
- 结论: **PASS**（附两处**必须保留**的限定：无红基线、palette 断言无负向验证）

---

## 1. 总体评估

T60 修的是 T10 引入的产品级回归：provider 换成 KTextEditor 后，`filetree_integration.cpp` 与 `palette_integration.cpp` 仍按 demo 时代 `qobject_cast<QPlainTextEdit *>(pane->editorWidget())` 写，必然拿到 `nullptr`，于是 filetree 双击与 palette “回车打开”在真实产品里失效。T60 把两处都改走 `EditorPane::openFile(path,&error)` / `currentFilePath()`，并且**没有只对 demo pane 冒绿**：filetree 的 rootcheck harness 先 `setEditorProvider(makeEditCoreProvider())`，用真实 `QTest::mouseClick/mouseDClick` 打在 QTreeView 行矩形上；palette 的 integration selftest 在 `VEDIT_PALETTE_HAVE_EDIT_CORE` 下切到真实 provider。我独立重建/运行了两个 harness：

- filetree：`FILETREE-SEAMCHECK ok=1 checks=17 failures=0`，exit 0；`dblclick.file-loads-bytes ok=1 detail=37 bytes on disk`（编辑器文本 == 磁盘字节）、`dblclick.file-records-path …/rtl/top.v`（`currentFilePath()` == 磁盘绝对路径）、第二个文件同样通过；`dblclick.folder-expands` / `dblclick.folder-emits-no-open detail=0` / `dblclick.folder-keeps-editor` 证明“双击文件夹只展开”的既有行为保持。
- palette：`INTEGRATION-SELFTEST ok=1 checks=28 failures=0`，exit 0；`integration.ktexteditor-{widget,opened-path,document,content,opened-path-recorded}` 五条真实 provider 断言全 PASS，`KTEXTEDITOR-OPEN path=…/src/util/helper.cpp bytes=10`。

全仓 `qobject_cast<QPlainTextEdit` 再搜的结果：产品路径只剩已知的 `src/workspace/search/search_jump_target.cpp`（T69/T70 单独处理，captain 已裁定不算 T60 失职）；两处测试 harness（palette/search selftest）与 `src/app/editor_provider.h` 两处 `DemoEditorProvider` 自身实现属于合法/测试代码——**无第四处漏网**。因此判 **PASS**，但必须连同下面的两处限定一起读。

## 2. 逐条核对（判据 1–5）

| # | 判据 | 结论 | 证据 / 我的独立观察 |
|---|---|---|---|
| 1 | 两处是否真改走 `EditorPane::openFile/currentFilePath`，且在**真实 KTextEditor provider** 下真跑通过 | ✅ | 两处代码改动见 T60 REPORT §2；我独立运行：filetree seamcheck **17/17**、palette integration **28/28**（真实 provider 断言 5 条），exit 0；均非 demo pane |
| 2 | palette 新增打开断言是否断言**编辑器状态**（非信号），且负向验证能变红 | ✅（状态级）/ **⚠ Nit：无负向验证** | 断言 `currentFilePath()` == 磁盘路径、文档文本 == 磁盘字节、`openedPath()` 记录；**但 T60 没有“故意改坏→断言变红”的负向对照，我也无法在不动源码的前提下注入错误路径，因此“断言能否真变红”未被证明**（见 §3 限定 B） |
| 3 | 既有断言未被删/弱化（filetree 17 条、palette 28 checks） | ✅ | 我从 T39 证据提取的 16 个 filetree seamcheck 名全部仍在（missing=∅），新增 2 条 provider 断言；旧 palette `integration.*` 的 22 个名全部仍在（missing=∅，报告说 23，差异是提取口径），新增 5 条 |
| 4 | 是否还有“第四处”漏网（search 已知不算） | ✅ 无 | 全仓 grep 命中逐一分类：search_jump_target（已知第三处，T69/T70）、两处测试 harness、`editor_provider.h` 是 `DemoEditorProvider` 自身实现（合法）；无第四处产品路径 |
| 5 | 根 ctest（基线 16/16）与 `typecheck` 自己实跑 | ✅（T60 证据成立；当前红可归因，非 T60） | T60 证据：`03-root-ctest.log` 16/16、`04-typecheck.log` PASS（19:25）。我的实跑：根 ctest **14/16**，红在 `vedit-terminal-selftest`（`pty.master-fd.not-inherited`、`pty.read-limit.cap-engaged`，T47 正在先加红基线）与 `vedit-app-shell-selftest`（`panel-seam.terminal-content`，同属 T47 的 terminal 返修红基线）；`typecheck` exit 1 于 `src/workspace/search/search_selftest.cpp:887: error: 'kHoldMs' is not captured`（T69 编辑中）。两者都不在 T60 的改动面，captain 已裁定接受该归因、停点 1 用冻结树重跑 |

## 3. 证据强度声明（按 captain 要求原样保留，不得抹平）

**限定 A — T60 没有“修复前”的红色基线。**
T60 的 `REPORT.md` §5 自认“修复前的红灯未重新录制”，其交付证据只有修复后的真跑通过（palette 28/28、filetree 17/17、ctest 16/16、typecheck PASS）。因此它能证明的是：**在真实 KTextEditor provider 下，这两条打开路径现在可用**；它**不能独立证明**“修复前不可用”。后者依赖 `T10` 的代码分析 + 类型级确定性事实——把 `KTextEditor::View` 对象 `qobject_cast<QPlainTextEdit *>` 必得 `nullptr`，从而落 demo 分支。**captain 已裁定：接受该类型级事实作为“修复前不可用”的依据，不要求补红基线（停点 1 前不开新票）。** 本 verdict 照此执行，但把“无红基线”保留为限定，不把它写成已被红录证明。

**限定 B — palette 新增断言没有负向验证。**
palette 的 5 条新断言都是**状态级**（`currentFilePath` 路径、文档字节、`openedPath` 记录），比“只断言信号”强得多；但 T60 没有提供“把实现改坏后断言变红”的负向对照，我也无法在不修改 work 的前提下注入错误状态。**captain 裁定：接受“断言内容强度”作为替代，不要求为负向验证开新票。** 本 verdict 把“断言能否真变红未被证明”明确列为 **Nit**，不把它写成已被验证。

## 4. 未验证 / 局限

- 无 GUI/视觉验证：filetree/palette 的截图是 offscreen 抓图（L2），只能证明渲染成功，不能判断“窗口看起来正确”。
- 没有在真实按键序列下重放 Ctrl+P 全流程与双击（palette harness 走产品命令路径与 queryEdit，filetree 用真实鼠标事件；两者都是 headless）。
- 当前树是红的（见判据 5），原因是其他在飞返修（T47 terminal 红基线、T69 search 编辑中）；**captain 将在停点 1 用冻结树重跑干净门禁**，那才是 PR 证据。本 verdict 不把当前红记到 T60。
- T60 的两处修复都是“provider 接缝”级：filetree 的 per-file 大小/二进制提示改由 provider 统一（KTextEditor 路径不再有 demo 的 4 MB 上限），这是行为变化，若产品需要该上限应在 provider 层定义（属 T10 写域）——按 T60 REPORT §5 如实记录，不影响本 verdict。

## 5. 我不判的（declined to judge）

1. **`search_jump_target.cpp` 的同类回归** — 已知第三处，T69/T70 单独处理；captain 明确不算 T60 失职。
2. **`DemoEditorProvider` 内部对 `QPlainTextEdit` 的使用** — 是 demo provider 的职责本身，合法，不算漏网。
3. **T47（terminal 红基线）与 T69（search 编辑中）的中间态** — 各自的返修任务负责；本 verdict 只做归因，不判它们。
4. **停点 1 的冻结树重跑** — captain 执行；本 verdict 的 PASS 不依赖当前红/绿。
5. **是否给 provider 层加 4 MB 上限** — 产品决策，属 T10/后续 lane。

## 6. 结论与一句话理由

**Verdict: PASS** — 风险 Medium。

理由：T60 把 filetree 与 palette 两处打开路径改为 `EditorPane::openFile/currentFilePath`，并由我在**真实 KTextEditor provider** 下独立复跑通过（filetree seamcheck 17/17、palette integration 28/28），既有断言一条未删，全仓无第四处产品路径；两处限定按 captain 裁定保留——**无红基线**（只能证明修复后可用，修复前不可用依赖类型级确定性事实）与 **palette 断言无负向验证（Nit，断言能否真变红未被证明）**——二者不推翻 PASS，但必须随 verdict 一起读。
