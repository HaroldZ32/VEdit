# 独立评审 verdict — T11 rev-edit-core：KTextEditor 编辑内核进产品

- Cell: `cell-20261031T085543161Z` · 评审任务: `T11`（rev-edit-core）· 被评 work: `T10`（edit-core）
- 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/edit-core.md`（work）+ `contracts/rev-edit-core.md`（本评审）
- 验证 loop: `loop-20261031T095153-e78674`（已绑定 seat；verdict 记录 `rec-20261031T112037-0d25b7`，PASS）
- gate evidence id: `ev-20261031T111945-8f7039`（`bun run typecheck`，**exit 0**）
- 评审人: **Reviewer**（盲审：先读契约/文档/证据，跑门禁与产品自检，**先记录 verdict**；未读 `src/editor/kernel` 实现源码）
- 评审时刻: 2026-10-31 19:16–19:20（Asia/Shanghai）· HEAD `95c496e` + 该 work 的未提交改动（`src/editor/kernel/`、`src/app/*`、`evidence/edit-core-v2/`）
- 环境: `vedit-qt6-build:local`（KTextEditor 6.24.0 / Qt 6.10.2 / g++ 15.2.0）
- 风险等级: **High**（本 lane 合同内为 Medium；High 来自它引入的跨 lane 回归，见 H1）
- 结论: **PASS（本 lane 的 AC-S1.1 成立；H1 的跨 lane 回归必须另开返修任务，不得读成“产品已就绪”）**

---

## 1. 总体评估

编辑内核确实进了产品，而且是“真内核”不是占位：`src/app/main.cpp` 调用 `shell.setEditorProvider(vedit::editor::makeEditCoreProvider())`（主路径 + selfcheck 路径各一处），产品 `--selfcheck` 里 pane 的 widget 是 KTextEditor view，pane 级 open/save 成功；`EDIT-CORE-SELFTEST` **55/55**、`APP-SHELL-SELFTEST` **133/133**、exit 0（我独立跑过）。55 条检查不是“看起来对”：原子保存建新文件/替换/写失败/rename 失败/dir-fsync 非致命五类故障注入都断言目标字节不变且临时文件清理；查找替换断言 3/3 与“一次 undo 回原文、redo 回替换后”；跳行断言 0→1、越界→末行、列 0 的 clamp；撤销重做跑满 100 个逻辑操作逐步比对（mismatches=0）；多光标有原生 API 与文档层批量插入两组断言；跨 ABI 边界有静态 + 运行期双重证据。多光标（Open Q3）的结论有据：6.24 头文件与导出符号证明原生“光标位置集合”，运行期实测 `View::insertText` 只改主光标，因此编辑语义用单 `EditingTransaction` 批量插入保证 A1b 的“一次 undo 全回滚”，且明确声明无头环境无法验证真实按键驱动的多光标键入。

**必须强调的 High**：provider 换成 KTextEditor 后，两条既有“打开文件”路径仍然按 demo 时代 `qobject_cast<QPlainTextEdit *>(pane->editorWidget())` 写，现在会拿到 `nullptr`——palette 的“回车打开”回归为 `no editor seam connected yet`，filetree 双击无法打开（后者在我的 T21 verdict 里本来就是 High）。这条不是本 lane 写域能修的（`src/workspace/filetree/`、`src/workspace/palette/` 都不在 T10 写域），work 已在 REPORT §7 如实上报并请求 captain 决定，还给了统一入口 `EditorPane::openFile(path,&error)` 与“每处约 3 行”的估算。评审层面：T11 自己的验收成立，但**产品当前有回归**，captain 必须为那两个文件另开返修任务（建议顺带给 palette 加“回车确实打开到编辑器”的断言），在 AC-S1.6 用户验收前关闭。

## 2. 逐条核对

| 条款 | 结论 | 证据 / 我的独立观察 |
|---|---|---|
| **AC-S1.1：main.cpp 调 setEditorProvider** | ✅ | `src/app/main.cpp` 的 `shell.setEditorProvider(vedit::editor::makeEditCoreProvider())`（主路径 + selfcheck 路径）；产品 `--selfcheck` 断言 `editor.pane.ktexteditor-widget`、`editor.pane.open`、`editor.pane.save` 全 PASS |
| **打开** | ✅ | `editcore.open.file` / `open.records-path`；pane 级 `editor.pane.open-path` |
| **原子保存** | ✅ | `editcore.atomic.fresh-file/content/no-temp-left`、`replace/replace-no-temp`、`fault.write-*`、`fault.rename-*`、`fault.dir-fsync-nonfatal`（故障注入 `VEDIT_ATOMIC_FAULT=fsync-dir`）+ `save.kernel/bytes/clears-modified` |
| **查找替换** | ✅ | `find.count=3`、`find.adjacent`、`replace.count=3`、`replace.text`、`replace.one-undo`、`replace.redo` |
| **跳行** | ✅ | `goto.line`、`goto.clamp-low`、`goto.clamp-high`（line=3）、`goto.column-zero` |
| **撤销/重做** | ✅ | `undo.100-operations`、`undo.100-back-to-original mismatches=0`、`redo.100-forwards mismatches=0` |
| **多光标（Open Q3）** | ✅（结论有据） | 6.24 头文件/导出符号 + 运行期 3 光标回读；`View::insertText` 只改主光标（x=1）；文档层单事务批量插入（XaXaXa、一次 undo/redo）；真实按键路径明确未验证（C6 无头限制） |
| **编辑事务不过 C ABI** | ✅ | 静态：`nm -u libvedit_editor_core.a \| grep -c vd_` = 0、defined vd_ = 0（我复跑，产品构建 + 独立构建）；运行期：`editcore.boundary.zero-calls before=0 after=0` + 正控制 `counter-live 0→1`（我复跑） |
| **门禁 / 自检全绿** | ✅ | `typecheck` exit 0（`ev-…8f7039`）；`ctest -R 'edit-core\|app-shell\|ffi-abi\|syntax'` = 7/7；产品 `--selfcheck` 55+133 全绿；独立 lane 构建 53 checks 全绿。**注**：19:09 我看到的 `vedit-edit-core-selftest` Timeout(300 s) 是修复前 `Document::openUrl` 挂死（REPORT §2.1 已记录并改掉），当前二进制 <1 s 通过 |

## 3. Findings（按严重度）

### H1 [High — 跨 lane 回归，本 work 写域外] provider 换成 KTextEditor 后，palette 的“回车打开”回归，filetree 的打开路径也失效
- 是什么：`src/workspace/palette/palette_integration.cpp`（`openInEditor`）与 `src/workspace/filetree/filetree_integration.cpp` 仍 `qobject_cast<QPlainTextEdit *>(pane->editorWidget())`；新 provider 返回的是 `KTextEditor::View`，cast 得到 nullptr → palette 落到 `Open … (no editor seam connected yet)`，filetree 仍只到状态栏文案。work 的 REPORT §7 明确列了这两处、给了统一入口 `EditorPane::openFile(path,&error)` / `currentFilePath()` / `saveFile()`，并请求 captain 决定扩写域或另开返修。
- 为什么重要：这是**已验收 lane 的用户可见回退**（A2b 的“回车打开”在真实产品里不再工作；A2a 的双击打开本来就被我 T21 verdict 判过/待修）。本 lane 的 AC-S1.1 成立不等于产品可用；AC-S1.6 用户验收前必须修掉。
- 修复方向（描述，不实施）：为 `src/workspace/filetree/`、`src/workspace/palette/` 各开一条小返修（vendor 的 3 行级改动），改调 `EditorPane::openFile`；palette 侧加一条“回车后编辑器内容/路径 == 目标”的断言；filetree 侧保持“双击文件夹只展开”。
- 证据：`evidence/edit-core-v2/REPORT.md` §7；`src/app/editor_area.h` 的 `EditorPane::openFile`（work 报告引用的统一入口）；我 T21/T22 verdict 里对这两处 `QPlainTextEdit` cast 的原文记录。

### M2 [Medium — 产品补齐，声明为写域外] Ctrl+S / 菜单快捷键未接
- 是什么：REPORT §6 明说“Ctrl+S / 菜单命令未接，属后续 lane”。`--selfcheck` 只验证 pane 级 open/save，没有按键绑定。
- 为什么重要：AC-S1.6 的用户验收动作是“改几行 → Ctrl+S → 看到诊断与高亮”；没有快捷键则用户路径不完整（文件仍可通过菜单/API 保存，但验收脚本会按 Ctrl+S 判）。
- 方向：在 app-shell/命令注册 lane 里把 Ctrl+S 接到 `EditorPane::saveFile()`，并加一条 selftest 级断言。

### M3 [Medium — 版本脆弱性] `undo()`/`redo()` 靠运行期 metaobject 名字调用
- 是什么：REPORT §2.2 记录 6.24 公开头文件没有声明 `undo()/redo()`，实现用 `QMetaObject::invokeMethod(doc, "undo")` / `"redo"`（`indexOfMethod` 要 `"undo()"`）。
- 为什么重要：这依赖 `KTextEditor::DocumentPrivate` 的 QObject slot 名，跨版本可能改名/移除；当前有运行期断言兜底（`undo.100-*`/`redo.100-*` 全过），但属于“测试能发现、产品也能挂”的耦合。若将来升级 KF6，这条会在真实用户路径上炸。
- 方向：在 `src/editor/kernel` 里集中一处封装并加注释/版本断言；升级 KF6 时优先跑 `editcore.undo/redo` 组。

### M4 [Medium — 无头边界，已声明] 没有 GUI/键盘级验证
- 是什么：REPORT §6 明确“没有窗口可见/渲染正确/真实按键”的判断；`QTest::keyClicks` 在 offscreen 下写不进字符（`evidence/edit-core/00-probe.md` §4）。多光标的按键路径、Ctrl+S、真实窗口渲染都未验证。
- 为什么重要：C6 允许，但 AC-S1.6 的用户验收仍是最终判据；本 verdict 不能替代用户的有屏验收。
- 方向：保持声明；用户有屏验收时按 AC-S1.6 执行。

### N5 [Nit — 证据状态] `evidence/edit-core-v2/` 在评审时未入库
- 与 T46/T13 同：cell 的 checkpoint 节奏；`00-pins.txt` 有 HEAD/镜像/改动路径。PR 前随 checkpoint 入库即可。

## 4. 我不判的（declined to judge，逐条给理由）

1. **`Document::openUrl()` 挂死的上游行为** — work 记录并绕开（读字节 + `setText`）；是否向上游报 bug 不是本轮判据。
2. **A1a 性能基线（10 万行）** — REPORT §6 明说不在本任务合同内；属后续 lane。
3. **多光标“真实按键键入”** — 无头环境不可断言（C6，T14 探针已证 `QTest::keyClicks` 写不进）；用户有屏验收判。
4. **Ctrl+S/菜单/命令注册的最终形态** — 属 app-shell 命令 lane；见 M2。
5. **`fsync(dir)` 的真实 EINVAL 场景** — 只能故障注入覆盖（REPORT §6）；判据允许。
6. **palette/filetree 两处回归的具体修复** — 写域不在本 work，见 H1；不据此外判 T11 FAIL（修复属另一任务）。
7. **KTextEditor 6.24 原生多光标 API 的交互质量（光标绘制/选区）** — 无头不可观；结论只到“位置集合原生 + 编辑语义自实现”。

## 5. 本次评审的边界

- **盲审**：先读契约/requirements/docs/`evidence/edit-core-v2/` 与构建产物（nm、ctest 注册、自检输出），**先记录 verdict**；未读 `src/editor/kernel` 实现源码（唯一源码接触是 `grep setEditorProvider src/app/main.cpp`，这是契约 AC 本身要求的检查）。
- 独立复跑：`ctest -R 'edit-core|app-shell|ffi-abi|syntax'` 7/7；`vedit-app --selfcheck`（55+133，exit 0）；独立 lane 二进制 53 checks；`nm -u/--defined-only` 两条为 0；`typecheck` exit 0。
- 没有跑完整 16 条 ctest（会包含其他 lane 的 `vedit-diagnostics-ffi-selftest` 等，耗时且与 T11 无关）；没有 Xvfb 截图/窗口验证（C6）。
- 没有修改任何源码/测试/门禁；本文件是唯一写入（`evidence/verdicts/edit-core/verdict.md`），验证记录由 `mpd_verify_record` 写入 `.mpd/verify/`。

## 6. 结论与一句话理由

**Verdict: PASS（本 lane 合同内）** — 风险 **High**（H1 跨 lane 回归）。

理由：AC-S1.1 的交付面逐条成立且由我独立复跑（`setEditorProvider` 已接、55 条 editcore 检查覆盖打开/原子保存含故障注入/查找替换一次 undo/跳行 clamp/100 步撤销重做/多光标/跨 ABI 零调用且正控制有效），`typecheck` exit 0；多光标结论有头文件+符号+运行期三重依据；但 provider 切换让已验收的 palette “回车打开”回退（filetree 打开路径同样失效），这条 High 在本 work 写域外，**必须由 captain 另开返修任务并在 AC-S1.6 用户验收前关闭**——因此本 PASS 只表示“编辑内核 lane 交付成立”，不表示“产品当前可用”。
