# T38 / AC-9.2 — B 类扩展语料对账（发现条数 = 行数）

- 报告日期：2026-11-03（+08:00）
- 任务：T38 `i18n-l2`（cell `cell-20261102T100600176Z`）
- 清单基线（rev3 硬约束）：**lupdate 的 .ts = 「已包 tr()」的 ground truth**，与 `--any-literal` 全字面量扫描的
  **并集**；466 旧清单只作历史参考，**本报告不使用 466 作为基线**。
- 本文件对应的机器可读表：`evidence/i18n-l2/03-b-reconciliation.csv`。

## 0. 计数口径

**发现条数 = 行数 = 319**（T38 的 304 行 + T40 的 15 行补充，见 §1.4）。表头与 CSV 数据行逐条对应，
没有折叠、没有去重后计数；每一行的 `already_wrapped` 都由 `tools/verify_claims.py` 对**当前树**重算
（不再照抄 discovery 列），并写进 CSV 的 `verified` 列：

| 项 | 值 |
|---|---|
| CSV 数据行 | **319**（= 304 discovery + 15 supplement） |
| 译（已完成：已包 tr()/translate、`.ts` 有译文，且校验通过） | 205 |
| 译（本批：T38 batch-2 + T40 修复新包 + 新译） | 73 |
| 译（片段：属于某条已包多行拼接字面量的片段） | 23 |
| 不译（逐条命名类别 + 理由，见 §3） | 18 |
| 待定 | **0** |
| `verified` 分布 | `own-call+ts` 301 · `no-wrap` 18 |

口径说明（务必与 §1 的发现数区分）：
- **304** 是 T38「UI sink 窗口过滤后的候选行数」，是 T38 逐行决策的对象；
- **15** 是 T40 的补充行：T38 的 UI-sink 窗口规则**没有**选中这些 call-site（例如
  `ProjectModel::summary()` 这个 model getter 的返回值最后由面板 QLabel 呈现），这正是 T39 F2
  指出的过滤缺口——补充行逐条带来源与理由，见 §1.4；
- **1456 行 / 1268 唯一串** 是抽取器的**原始候选**（未做 UI sink 过滤，含日志/CSS/测试）；
- 实际包装的 call-site 是 `03-b-wrap-table.json` 的 **151** 条（T38：78 + 73）加 T40 的 **33** 条
  （20 条瞬时状态栏 + 3 条诊断状态行 + 10 条 Project 标题/表头组合）。

## 1. 枚举方法与完整命令（可复现）

1. **全字面量扫描**（未包 tr() 的部分；必须用完整 flag `--any-literal`）：

   ```bash
   python3 evidence/i18n/extract_i18n_candidates.py --any-literal --format csv \
       --out evidence/i18n-l2/logs/16-any-literal-batch2.csv
   ```

   本次输出（`logs/16-any-literal-batch2.log`）：

   ```
   [v1] scanned=148 literals=8589 candidates=1456 unique=1268 skipped_files=0
     dropped by rule: C1-empty=110, C10-no-whitespace=1418, C2-preprocessor=15, C3-path=970,
                      C4-format-only=227, C5-lowercase-token=3634, C7-single-char=34,
                      C8-embedded-newline=725
     candidate call-sites: other=903, selftest_out=239, tr=156, ui_text=102, log=46, ui_action=10
   ```

   复现性说明：默认模式（规则 C9：字面量必须直接位于 `QStringLiteral()` 内）在 T21 包装后
   **产出 0 候选**，`evidence/i18n/05-extracted-current.log` 已无法在当前树重生成；所以本任务全程
   只用 `--any-literal`。历史锚点 894 次/780 串是 **T21 前的快照**，今天的树复现不出来，
   本报告不把它当作当前基线。

2. **与 .ts 求并集 + UI sink 窗口过滤 + 已包判定**（304 行表）：

   ```bash
   python3 evidence/i18n-l2/tools/enumerate_b_class.py \
       --csv evidence/i18n-l2/logs/16-any-literal-batch2.csv \
       --ts translations/vedit_zh_CN.ts \
       --out evidence/i18n-l2/logs/17-b-discovery-batch2.csv \
       --summary evidence/i18n-l2/logs/17-b-discovery-batch2-summary.txt
   ```

   输出：`discovered=304 rows (wrapped=220, unwrapped=84)`。

3. **本批包装选择与落盘**：
   - 选择脚本：`evidence/i18n-l2/tools/select_b2_rows.py`（复用 wrap 工具的 lexer，取拼接后的完整字面量）
     → `logs/18-b2-selected-rows.json`（72 行）+ `logs/18-b2-extra.json`（1 行 `the Rust settings store rejected the write`）。
   - 包装：`python3 evidence/i18n-l2/tools/wrap_b_class.py --table evidence/i18n-l2/logs/18-b2-only.json`
     → `total wrapped call sites = 72`；extra → `wrapped=1`。
   - 合并后的总表：`evidence/i18n-l2/03-b-wrap-table.json` = **151 行**（第一批 78 + 第二批 73）。

4. **对账表生成**（决策 + reason 逐行写入）：

   ```bash
   python3 evidence/i18n-l2/tools/make_reconciliation.py
   # findings=317 rows=317 -> evidence/i18n-l2/03-b-reconciliation.csv
   ```

### 1.4 T40 修复：声称校验（F3）与补充行（F2）

T39 判 FAIL 的 F3 根因是 `make_reconciliation.py` **无条件相信** discovery CSV 的
`already_wrapped` 列，而 discovery 的「已包」判定来自**固定窗口**（`enumerate_b_class.py` 的
`line-5..line+3` 窗口）——一个裸 `QStringLiteral("saved file")` 只要坐在多行 `translate(...)`
实参里就会被记成「已包」。T40 把它改成**每行现场校验**：

```bash
python3 evidence/i18n-l2/tools/make_reconciliation.py     # 内置校验，发现条数 = 行数
python3 evidence/i18n-l2/tools/summarize_audit.py         # F3 审计表
```

- 校验器 `tools/verify_claims.py`：对每一行找**该字面量自己的调用点**（先按记录行，再按全文；
  多行语句用语句边界回溯），然后：
  * `wrap`：字面量是否落在某个 `tr(...)`/`translate(...)` 的**实参括号范围**内
    （括号扫描会跳过字符串字面量，因为消息本身含括号）；落在 `.arg(...)` 里不算；
  * `ts`：`.ts` 里是否有同文 `<source>`（`exact`），或该行是某条更长的已译多行消息的
    **片段**（`fragment`，要求 `.ts` `<location>` 指向**同一文件、调用点附近**，
    否则 `Opened %1` 会被另一条消息 `Opened %1 line %2` 误判成已译）；
  * 多行 C 拼接（`"第一段 " "第二段"`）按**前缀**定位语句（前缀不要求闭合引号），
    整串再按 join 后的文本比对 `.ts`——设置页那两条就是这么校验的。
- CSV 新增 `verified` 列：`own-call+ts`（可验证）/ `no-ts`/`no-wrap`/`wrap-only`；
  任何一行 `already_wrapped=yes` 却不可验证，生成器直接 **exit 1**，不再静默出版。
- 补充行（15）：T40 新包的串里，T38 的 UI-sink 窗口没有选中的 call-site（`Project` 标题、
  `Project: %1 — %2 %3` 表头及其 `file/files`/` · …` 组合、`pending`、`Saved`）作为
  `call_site=ui_text (supplement)` 补进表里——这是**修过滤缺口**，不是手工挑行。
- 审计：`logs/20-t40-claim-audit.csv`（逐行）+ `logs/22-t40-claim-audit-summary.md`（分组）。
  T38 的表可用保留的 `tools/make_reconciliation.t38.py` 逐字节重建，重建结果与本轮起点一致
  （`findings=304`，220/27/23/34，见 `logs/19-t38-reconciliation-reconstructed.csv`）。

## 2. 本批（B 第二批量）实际包装的 73 个 call-site

按文件（全部用 `QCoreApplication::translate("VEdit", …)`，含多行拼接整串）：

| 文件 | 包装点 | 说明 |
|---|---|---|
| `src/workspace/diagnostics/diagnostics_outline_model.cpp` | 3 | 表头 `Symbol` / `Detail` / `Line` |
| `src/workspace/diagnostics/diagnostics_panel.cpp` | 9 | 空态/下一步/分析状态/悬浮提示 |
| `src/workspace/module_tree/module_tree_integration.cpp` | 3 | 重试状态、空态、面板空态 |
| `src/workspace/module_tree/module_tree_panel.cpp` | 4 | 悬浮提示、两处 `No hierarchy yet…`、就绪状态 |
| `src/workspace/module_tree/hierarchy_client.cpp` | 2 | 展开/elaborate 进度文本 |
| `src/workspace/diagnostics/lint_progress.cpp` | 4 | `linting…` / `idle` / 重跑不可用两处 |
| `src/workspace/palette/command_palette.cpp` | 2 | 状态行两处 |
| `src/workspace/filetree/file_tree_widget.cpp` | 4 | `Opened/Deleted/Refreshed '%1'`、`%1 - %2` |
| `src/workspace/project/project_panel.cpp` | 11 | 导入/移除/变量/重新展开/无法打开 等面板状态 |
| `src/editor/kernel/editor_context_menu.cpp` | 9 | 右键菜单节标题（`Language Server`/`Diagnostics`）与 7 条动作说明 |
| `src/editor/kernel/editor_advanced.cpp` | 14 | 10 个特性显示名、tooltip `%1 — available (%2)`、3 个片段名 |
| `src/editor/kernel/fim_wiring.cpp` | 1 | FIM 幽灵文本 tooltip |
| `src/workspace/project/project_variables_dialog.cpp` | 1 | 对话框说明标签（多行拼接整串） |
| `src/workspace/settings/settings_panel.cpp` | 5 | 主题/快捷键/预览/保存失败状态 |
| `src/workspace/diagnostics/diagnostics_integration.cpp` | 1 | 状态栏 `LSP backend=%1 · lint=%2 · %3` |

lupdate 后新增 69 条 message（另有 3 条同文已存在、1 条被同文启发式直接命中），合并后
`.ts` = **534 messages / 0 unfinished**，lrelease `Generated 534 translation(s) (534 finished and 0 unfinished)`
（`logs/19-lupdate-after-b2.log`、`logs/23-lupdate-after-b2-extra.log`、`logs/25-b2-final-lrelease.log`）。

## 3. 34 条「不译」的决定与理由（逐条在 CSV 里）

| 类别 | 条数 | 理由（逐条在 CSV 里） |
|---|---|---|
| AC-4.3 code snippet：Qt 样式表 / 富文本标记片段 | 7 | 不是用户句子；翻译会破坏样式（`color: …`、`font-weight: …`、`padding:` 等） |
| AC-4.3 technical identifier：开发者日志行 | 8 | 只进 `qInfo`/`printf`/`logEditorActionLine` 的日志，从不渲染到 UI |
| AC-4.3 code snippet：开发者专用的 seam 占位文本 | 1 | 指出替换 dock 的 C++ 工厂（`PanelRegistry::setFactory(...)`），不是用户文档 |
| AC-4.3 technical identifier：装饰性 glyph 前缀 | 2 | `● %1` / `⚠ %1` 是标记符号，不是句子；翻译会破坏标记语义 |

**判据（一句话，供下一个维护者复用）**：**读起来像用户面句子（有动词/名词短语、会出现在状态栏、标签、
标题、对话框里）→ 必须译**；只有**不含用户语言的机器记号**——样式表/富文本片段、`字段=值` 形式的
日志或开发者转储、装饰性前缀、指向代码位置的开发者占位文本——才可以归入 AC-4.3 豁免，并且**每条**都要在
CSV 的 `reason` 里指名类别。**禁止**再用「某批量边界」这类大类理由。

### 3.2 原「待定」5 行：已由 captain 追加授权后闭合（0 行）

| 位置 | 字符串 | 现状 |
|---|---|---|
| `src/workspace/filetree/filetree_integration.cpp:48` | `Opened %1` | **已包装 + 已译**（`已打开 %1`），CSV 里 `译（本批）` |
| `src/workspace/settings/settings_panel.cpp:675-678` | `Settings loaded from %1 with %2 warning(s); the built-in defaults were kept where the file is invalid.` | **已包装 + 已译**（多行拼接整串） |
| `src/workspace/settings/settings_panel.cpp:934-937` | `%1 configuration problem(s) — the built-in defaults were kept where needed:` | **已包装 + 已译** |

T38 的表把这 5 行记成「已包且已译」；F3 的现场校验把它们打回 `already_wrapped=no` + `待定`
（**表不再撒谎**，缺口可见），captain 追加授权写域后包装 + 译文闭环，工具从 `待定` 自动改判为
`译（本批）`。`make_reconciliation.py` 现在 **exit 0**。

## 4. 与 seed 的差异（详见 `05-seed-diff.md`）

- seed `target/tmp/t22-precheck/g7-seed.md`（sha256 `ed76171a…b162`）写 **B 类 77 条**；
- 本轮实际包装 **151 个 call-site**（78 + 73）：seed 是只读盘点（未编译、未逐条佐证，见其 §345），
  漏掉了第二批的面板本地文本（表头/空态/菜单项/片段名/tooltip）——这正是本任务 B 类扩面的增量；
- 77 → 78 的差已在第一批解释（seed 的 HTML 片段合并、`All files (*)` 拼写等）；本批 73 条属于
  seed 清单之外的新发现，逐条在 `03-b-wrap-table.json` 与 `logs/18-b2-selected-rows.json` 里。
