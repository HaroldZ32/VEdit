# T21 — i18n 对账 + 215 条 UI 翻译 + T45 恢复：实现与证据

Cell `cell-20261102T100600176Z` · task **T23** (`i18n-t21`) · member **se-i18n** · verifier **Reviewer**
Frozen contract: `.mpd/cellwork/cell-20261102T100600176Z/contracts/i18n-t21.md`
Requirements: `.mpd/cellwork/cell-20261102T100600176Z/requirements.md` **AC-4.5 / AC-4.6 / AC-4.7 / AC-4.9**
本任务未编辑任何门禁；测试改动只替换断言的**期望值来源**（英文字面量 → 翻译键查找），未增删断言。

## 0. 结论摘要

| 验收 | 结果 | 证据 |
|---|---|---|
| AC-4.5 466 逐条对账，条数=466，每条 decision+理由 | **PASS** | `11-reconciliation-466.{csv,md}`（466 行；UI 215 / non-UI 195 / already 31 / obsolete 15 / uncertain 10） |
| AC-4.5 uncertain 默认「保留英文 + 登记理由」 | **PASS** | 10 条 decision=`待定→保留英文`，理由含 R25 + 默认口径；见 §2 |
| AC-4.6 215 UI 按可核对块落盘，无残留未翻译 | **PASS** | `12-ui-blocks.md`（按源文件分块，每行 id/源串/中文/落点/上下文/.ts 状态）；`13-coverage.md`：213 译 + 2 不译（主题标识符，理由登记） |
| AC-4.6 剩余 tr() 包装 + 术语一致 | **PASS** | `apply_tr_wrapping.py`：251 处包装、54 个源文件；213 条译项全部有包装落点（`12-ui-blocks.md` 落点列） |
| AC-4.7 selftest 解耦，覆盖点数不变 | **PASS** | `14-selftest-coverage.md`：617 个断言调用点；3 个显式解耦点；`ctest` 38/38、`--selfcheck` 245/0 |
| AC-4.9 T45 恢复（复用既有抽取器/triage/diff，不自造语料） | **PASS** | §1：只读复用 `extract_i18n_candidates.py`、`07-triage-466.*`、`06-extractor-diff.*`；466 行全部来自 07 |
| 门禁：Qt6 构建 + ctest 通过 | **PASS** | `logs/22-build-qt.log`（**BUILD_QT_EXIT=0**）、`logs/23-final-selfcheck-ctest.log`（SELFCHECK_EXIT=0 / CTEST_EXIT=0，38/38） |

## 1. 语料与 T45 恢复（AC-4.9）

- **唯一语料**：`evidence/i18n/07-triage-466.json`（466 行分类 + 规则码）、
  `03-glossary.md`（术语）、`02-bilingual-table-draft.md`（已核译文）、
  `01-string-inventory.log`（原始清单）。**没有重新生成 466 清单**，也没有自造候选。
- 复用 `extract_i18n_candidates.py` 对当前树做过一次只读重扫（T45 的"复用而非重做"）：
  工具本身存在一个已记录的口径问题——strict profile 的 C9 要求 `call_site == qstringliteral`，
  而 `classify_call_site()` 会先剥掉 `QStringLiteral(` 包装，因此 strict 模式今天产出 0 候选
  （`literals=8187 candidates=0`）。这不影响本任务：**466 的权威来源是冻结的 07**，
  06 的 diff 也保持原样复用。该观察记在此处，作为工具维护线索，不修改抽取器、不重做 triage。
- `07-triage-466.*`、`06-extractor-diff.*` 未重写；本任务新增文件见 §8。

## 2. 466 逐条对账（AC-4.5）

产物：`11-reconciliation-466.csv`（机器可核对，466 数据行 + 表头）与
`11-reconciliation-466.md`（计数 + 决策分布 + 逐条表）。

- 行数 **466**；分类计数与 07 完全一致：

| 分类 | 行数 | 决策 |
|---|---|---|
| UI | 215 | 译 213；不译 2（`Breeze Dark`/`Breeze Light`：主题库标识符，按术语表保留英文） |
| non-UI | 195 | 不译（规则码 Rxx + 理由） |
| already-translated | 31 | 已译（tr() 已包装；英文源串补中文，中文源串登记已知显示缺陷） |
| obsolete | 15 | 删除（当前树已无该字面量） |
| uncertain | 10 | **待定→保留英文**（AC-4.5 默认口径，理由含 R25） |

- 决策分布：`删除 15 / 译 213 / 不译 197 / 已译 31 / 待定→保留英文 10`，合计 466。
- 每条 row 的 `reason` 都带 triage 规则码与处置依据；UI 译项另标"译文/落点见 09/12"。

## 3. 215 条 UI 块与 .ts（AC-4.6）

产物：`12-ui-blocks.md`——按**源文件**分块（约 40 个文件），每块一张表：

| 列 | 含义 |
|---|---|
| `id` | 07-triage 行号（可回溯到 466 报告） |
| `源串` | 冻结 triage 里的英文字面量 |
| `中文` | 本任务译文（术语按 03-glossary.md；技术名词保留英文，`%1/%2` 占位符原样） |
| `落点` | 代码中包装调用所在的 `file:line`（可逐条 grep 核对） |
| `上下文` | 翻译上下文（`VEdit` 或 lupdate 产出的类名） |
| `.ts` | 该源串在 `translations/vedit_zh_CN.ts` 中有无译文 |

覆盖率（`13-coverage.md`）：**213 条决定译的候选全部有 .ts 译文、全部有包装落点**；
不译 2 条（主题标识符）有理由、无译文（不进 .ts）。.ts 现状：

- 391 条 message；lrelease：**290 条有译文（289 finished + 1 unfinished 同文提示）**、101 条未译被忽略；7 个上下文：
  `VEdit`（本次服务层包装）、`vedit::app::ApplicationShell`、
  `vedit::workspace::settings::SettingsPanel`、`vedit::workspace::FileTreeWidget`、
  `vedit::workspace::search::SearchPanel`、`vedit::editor::FindReplaceBar`、`QObject`。
- 未译的 101 条是 466 判定为 non-UI/不译的串，或冻结清单之后其它 lane 新增的 tr() 串；
  **不属于 AC-4.6 的 215 UI 集合**（逐条在 466 报告里有 decision）。

## 4. tr() 包装与术语（AC-4.6 ③）

- `apply_tr_wrapping.py`（可重跑、dry-run 默认）把 213 条候选的 251 处字面量包装为
  `QCoreApplication::translate("VEdit", "<源串>")`，去掉原 `QStringLiteral/QLatin1String/
  fromUtf8` 包装，并在需要的文件加 `#include <QCoreApplication>`。
  跳过规则：`setObjectName/setProperty/#include/constexpr/const char*` 上下文不动
  （对象名/键不是 UI 文本）；已 `tr()` 包装的 14 条保持原类上下文。
- 术语：`03-glossary.md` 的操作词（打开/保存/关闭/新建/删除/重命名/刷新/查找/替换/分屏/设置…）
  与面板名（文件树/搜索/诊断/大纲/版本控制/终端）统一；`lint/LSP/filelist/atomic save/
  Verilog/SystemVerilog/KTextEditor/tree-sitter/vd-ffi/Rust` 等保留英文；
  路径、代码片段、`%1` 占位符原样。
- 运行时截图：`shots/20-zh-ui.png`（发行二进制，`ui.language=zh_CN`，中文菜单/状态栏）与
  `shots/21-en-ui.png`（无语言键 → Auto → 英文）。截图脚本与退出码见
  `logs/21-zh-en-ui-screens.log`（两例 `shot_rc=0 app_rc=0`）。

## 5. selftest 解耦（AC-4.7）

- 显式解耦 2 处（3 个覆盖点）：
  1. `src/app/main.cpp` `settings.preferences.menu`：`&Settings` 期望值改为
     `QCoreApplication::translate("vedit::app::ApplicationShell", "&Settings")`；
  2. `src/workspace/settings/settings_selftest.cpp` `settings.pages.tabs-titles`：
     `Shortcuts/Tools/FIM` 改为 `SettingsPanel` 上下文的 translate 查找。
- 其余断言在 §4 的包装变更里**同步两侧**（测试里的期望值也变成同一个 translate 调用），
  因此断言在任意界面语言下成立；证据是 `12-ui-blocks.md` 中落在 selftest 文件的条目。
- 覆盖点数量：`14-selftest-coverage.md` 统计 10 个 selftest/自检文件的
  `check/QVERIFY/QVERIFY2/QCOMPARE/expect` 调用点共 **617**；解耦改动只替换比较右侧，
  未增删断言（我在两处改动中只改了操作数；文件里其它 `check(` 增删是开工基线里其它 lane 的既有改动）。
- 验证：`ctest` **38/38** exit 0；`--selfcheck` **245/0** exit 0（`logs/20-build-selfcheck-ctest.log`）。

## 6. 门禁与证据

```bash
# 文档化门禁（全部目标，-Werror）——观察：BUILD_QT_EXIT=0
bash scripts/build-qt.sh --no-test
# 本机固定镜像内的等价复跑（全量 target 构建 + 自检 + 测试）——观察：
#   cmake --build target/qt6-build  → BUILD_EXIT=0（297/297）
#   vedit-app --selfcheck           → APP-SHELL 245/0，EDIT-CORE 67/0，SELFCHECK_EXIT=0
#   ctest --test-dir target/qt6-build → CTEST_EXIT=0，100% tests passed, 0 failed out of 38
# 报告与解码链（可重跑）：
python3 evidence/i18n/apply_tr_wrapping.py          # dry-run：251 处已包装后应为 0 项待改
python3 evidence/i18n/merge_ui_translations.py      # → 220 语料行：218 present，0 untranslated
python3 evidence/i18n/build_reports.py              # → 11/12/13 三份报告
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/x.qm
                                                    # → 290 translation(s)（289 finished），ignored 101
```

## 7. 未证明 / 边界

1. **AC-4.3/4.4（全 UI 中文/全英文）不在本任务 AC 集合**：215 条按冻结 triage 完成；
   466 中标为 non-UI 的可见串（如 `No folder open`，07 分类 non-UI）保持英文，
   `11-reconciliation-466.md` 有逐条 decision 可核。
2. **中文源串的 already-translated 项**（FIM 状态语，如 `FIM 不可用（未链接 Rust 服务）`）
   源串本身是中文：英文界面下仍显示中文。已在 466 报告登记为已知显示缺陷
   （英文侧文案需另开工作项，本任务写域内不改写这些既有中文串的语义）。
3. **翻译语料边界**：215 条中文是按 02/03 语料口径**撰写**的（既有语料没有 466 条完整中译），
   术语一致；"禁止自造语料"针对候选清单/triage，本次未自造任何候选或计数。
4. `.ts` 中 101 条未译 message 属于 non-UI/不译/其它 lane 新串，不在 AC-4.6 的 215 内。
5. 截图是 Xvfb+xcb 的发行二进制（按 T18 惯例装随包 CJK 字体），未在真实桌面 WM 下复验。

## 8. 产物与复现

| 产物 | 说明 |
|---|---|
| `09-ui-translations.json` | 215 条 UI 的 decision + 中文（213 译 + 2 不译） |
| `10-ui-translations-supplementary.json` | already-translated 中的 5 条英文 UI 补译 |
| `11-reconciliation-466.csv/.md` | AC-4.5 逐条对账 |
| `12-ui-blocks.md` | AC-4.6 可核对块 + 落点 |
| `13-coverage.md` / `14-selftest-coverage.md` | 覆盖率/解耦计数 |
| `apply_tr_wrapping.py` / `merge_ui_translations.py` / `build_reports.py` | 可重跑生成链 |
| `logs/20-build-selfcheck-ctest.log`、`logs/22-build-qt.log`、`logs/21-zh-en-ui-screens.log` | 门禁与截图日志 |
| `shots/20-zh-ui.png`、`shots/21-en-ui.png` | 运行时中英界面 |
| `translations/vedit_zh_CN.ts` | 391 message / 290 译（由 lupdate 从真实调用点生成上下文） |

树状态：HEAD `4e2d0a5` + 既有在途工作树。本任务改动路径：`src/`（54 个文件的 251 处包装 +
2 个 selftest 的期望值解耦）、`translations/vedit_zh_CN.ts`、`evidence/i18n/`。未改门禁。

说明：`bash scripts/build-qt.sh` 自身按仓库设计把日志写入 `evidence/ffi-abi/logs/` 与
`evidence/m0-build/build-logs/`（文档化门禁的副作用，不是本任务手工扩写域；这些路径在开工基线里已脏）。

### 8.1 产物 sha256

| 产物 | sha256 |
|---|---|
| `09-ui-translations.json` | `ecdd9e1dca7443fce6e24ea952db1e3ca5d1da3becd46f01d9153b1703aecc71` |
| `10-ui-translations-supplementary.json` | `377a24c52d99d6ca8a4940d0df3f468082b53d434dc2c14b611ef23bd7a1cdff` |
| `11-reconciliation-466.csv` | `274b24ba302d896aea76e2e1576187889a8ee33dd133f6e789dbd5ab3baa2010` |
| `11-reconciliation-466.md` | `bc1a526e37bb786ba39d237bc2de22034fa07b6110f876cb19c88264fe3d7227` |
| `12-ui-blocks.md` | `2b5698aecd612784ff77a820bb36da850b990663d14776d267262e620d2e2b51` |
| `13-coverage.md` | `c1e67e45f9e77decd63153a35fb22d315d58e602c26551574e17f69c56a25d61` |
| `14-selftest-coverage.md` | `641471833d8e7b1c66455c7565673fa50f869409757202a0e4a3453feda87f2c` |
| `16-wrap-changes.txt` | `8a64e269b8f894754a47953218d0d27f0c2024236e2b1888eeb24f1854f6d64d`（251 处包装清单） |
| `translations/vedit_zh_CN.ts` | `cf1d6e63c3faa320173a4e523b512fb7326001fb94f1a7c2e4e180fe5c284c15` |
| `.qm`（构建树） | `fba97c84a1f7d27ac3865a083eba2a83a9c52ddbd266722a04eb20487f6b8b86` |
| `shots/20-zh-ui.png` | `6b13b01ae1457d384ddf5e5f1ad6f9d89c00c4b66ddeb120988963bbd7dae3c3` |
| `shots/21-en-ui.png` | `68cb61e762e8df6036de63f7761417486288cdb8f05200b1997553ee07ad2417` |
| `apply_tr_wrapping.py` | `a9a1eb63aa212aa23a6975992c5dcaabb21ba443b062e43b680f11cb6e1992d5` |
| `merge_ui_translations.py` | `2a92d296c8a197d80cd78790aeb56105b9aa99f6be033b1cd9b7ace846711ad4` |
| `build_reports.py` | `e30fb239d54ec33d6adef37cb52447f7b65f0d4b85d264c10aeddd1ccbe8bd70` |
| `logs/22-build-qt.log` | `c8ad12003cc66b16e2460fe67245bfde2654e3cca64ee52e9ea8009830022e9f` |
| `logs/23-final-selfcheck-ctest.log` | `9ef29a18e5014cd4e9465200f6e7911ebfb6d17eeeac69d1b98734749a635ac0` |
| `logs/21-zh-en-ui-screens.log` | `d0a2d292fc7c96b5fe326f5201748809250ef34843fda744331670025ab06810` |
| `logs/24-full-build-all.log` | `3a0d4ec301f25924e5994956d4a916ea22c9acd4259b061f30771f2436711517` |
