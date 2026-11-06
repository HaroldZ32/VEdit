# T38 / G7 — i18n L2 扩面：最终报告（AC-9.1…9.4）

- 任务：**T38 `i18n-l2`**（cell `cell-20261102T100600176Z`），frozen contract
  `.mpd/cellwork/cell-20261102T100600176Z/contracts/i18n-l2.md`（loop `loop-20261102T155914-7982b2`）。
- 本报告汇总三位写者的产物；本写者（`dw-rust`，第三棒）完成的是 **B 第二批量 + 对账表 +
  THIRD_PARTY_NOTICES + 打包验证 + 最终门禁**，并重跑全部证据。
- 树：HEAD `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f` + 本 cell 工作树（shared，dirty≈240）。
- 硬约束遵守：`evidence/i18n/**` 未被本任务写入（§6 有 mtime 证明）；T18/T21 的条款/写域/证据未动；
  测试/门禁脚本未改；AC-4.7 = **617**（≥617，§5）；466 只作历史参考。

## 1. AC-9.1 — zh_CN 下面板/按钮/搜索/诊断文本为中文

证据：**真实产品 shell 探针**（`ApplicationShell` + `installPanels`，非 `--selfcheck`：
`evidence/i18n-l2/probe/i18n_probe.cpp`，翻译器在 shell 构造前安装，settings.toml 写
`"ui.language" = "zh_CN"`），机器可读日志 `logs/23-t40-probe.log`，截图
`shots/01-shell-zh_CN.png`、`shots/02-qt-standard-buttons-zh_CN.png`。

探针结果（T40 修复后）：**43 checks, 0 failure(s)，`probe-run-exit=0`**（T38 时 32 条）。
本表逐条列出这 43 项（T27 补齐了旧版漏列的 3 项：`probe.temp-state`、`shell.search-panel`、
`text.t40-audit-closures`）。T27 另加一支**独立**的 10 checks 状态栏探针（真实 F8 路径），见 §10。

### 1.1 探针**实际断言**的字符串（逐条；不再用概括措辞）

| check | 断言对象 | 期望值 |
|---|---|---|
| `i18n.selection` / `i18n.vedit-loaded` / `i18n.vedit-file` / `i18n.qtbase-loaded` / `i18n.qtbase-file` | 翻译器选择与两个 `.qm` 装载 | `zh_CN`、`vedit_zh_CN.qm`、`qtbase_zh_CN.qm` |
| `probe.temp-state` **（审计）** | 探针的临时状态目录（写 `ui.language = "zh_CN"` 的 settings.toml） | 目录创建成功 |
| `text.panel-title.files/search/terminal/outline` | 4 个面板标题的 `tr()` | 文件 / 搜索 / 终端 / 大纲 |
| `text.button.new-file` | 新建文件按钮 | 新建文件 |
| `text.placeholder.module-tree` | 模块树过滤框占位符 | 过滤实例 / 网络 / 文件… |
| `text.diagnostic.counts` | 诊断计数行 | 含「错误」 |
| `text.diagnostic.status-line` **（T40/F1）** | 诊断面板**状态行**（真实 widget） | `— 0 条诊断`，且不含 `diagnostic(s)` |
| `text.diagnostic.status-template` **（T40/F1）** | `%1 — %2 diagnostic(s)` 模板 | `x — 0 条诊断` |
| `text.diagnostic.pending` / `text.diagnostic.saved-file` **（T40/F1）** | 同一状态行的两个兄弟片段 | 待分析 / 已保存的文件 |
| `shell.dock.files/search/terminal/outline/module-tree` | 5 个 dock 标题（真实 QDockWidget） | 文件 / 搜索 / 终端 / 大纲 / 模块树 |
| `text.project-dock-title` **（T40/F2）** | **Project dock 标题**（真实 QDockWidget `dock-project`） | 工程 |
| `text.project-title-template` / `text.project-file-nouns` **（T40/F2）** | `Project` / `file` / `files` | 工程 / 个文件 |
| `text.project-header-template` **（T40/F2）** | `Project: %1 — %2 %3` 组合 | `工程：demo — 2 个文件` |
| `text.project-header-suffixes` **（T40/F2）** | 7 条 ` · …` 后缀（单/复数各一条） | ` · %1 个文件列表` / ` · %1 个变量` / ` · 跳过 %1 行` / ` · %1 个未解析变量` |
| `text.project-header` **（T40/F2）** | **Project 面板表头**（真实 `ProjectModel::summary()` + 受控 fixture） | `工程：demo-project — 2 个文件（实时）` |
| `text.t40-audit-closures` **（T40/F3 审计）** | F3 审计打回的 3 条串（`已打开 %1` / `已从 %1 载入设置（%2 个警告）；文件无效处的设置已保留内置默认值。`） | 三条均中文、无英文残留 |
| `shell.search-panel` **（非字符串）** | 搜索面板的真实 widget 存在（`SearchPanel` 可 cast — 占位符断言的宿主） | 非空 |
| `shell.search-placeholder` / `shell.filetree-buttons` / `shell.terminal-buttons` / `shell.module-tree-buttons` / `shell.module-tree-placeholder` | 搜索占位符、文件树/终端/模块树按钮 | 在工程中搜索文本…、打开…/新建文件/新建文件夹/重命名/删除/刷新/折叠、重启/清空/关闭、刷新/跳转到源/跳转到目标、过滤实例 / 网络 / 文件… |
| `shell.diagnostics-panel` / `shell.diagnostics-counts` | 诊断面板存在 + 计数行 | `错误：0 · 警告：0 · 信息：0` |
| `std.ok/cancel/save/open` | Qt 标准按钮 | 确定 / 取消 / 保存 / 打开 |
| `shot.shell` / `shot.std-buttons` | 两张截图 | 落盘成功 |

**边界（诚实）**：探针**不**逐条覆盖全部 942 条 `.ts` message；它断言的是 AC-9.1 的代表面
**加上 T40 新增的 F1/F2 全部串与 F3 审计打回的 3 条**（上表 10 条 T40 行 + 1 条审计串）。其余 T38 batch-2 文本由
`.ts`/lrelease 与源码包装证明，逐行可验证性见 `03-b-reconciliation.csv` 的 `verified` 列，
**没有逐条截图**。

## 2. AC-9.2 — 对账报告：发现条数 = 行数，逐条 decision + reason，方法与命令

交付：`03-b-reconciliation.md`（人读）+ `03-b-reconciliation.csv`（机器可读）。

- 表头 **「发现条数 = 行数 = 319」**（= 304 discovery + 15 supplement），CSV 数据行 **319**；
- 每行 `decision` ∈ {译（已完成）**205**、译（本批）**73**、译（片段）**23**、不译 **18**、待定 **0**} + `reason`；
- 枚举方法与完整命令（必须 `--any-literal`）在 `03-b-reconciliation.md` §1；
  原始抽取器输出 `logs/16-any-literal-batch2.log`（`candidates=1456 unique=1268`），
  UI-sink 过滤后 `logs/17-b-discovery-batch2.csv`（304 行 discovery）；
- 生成器实测（T27 重跑）：`python3 evidence/i18n-l2/tools/make_reconciliation.py` →
  `findings=319 rows=319 supplement=15`、`待定: 0`、**exit 0**（`logs/34-t27-reconciliation.log`）；
- 466 不再作为基线；历史锚点 894/780 不可复现，已如实记录为历史快照。
- **旧版 §2 的 304 / 220·27·23·34 是 T38 的旧计数**（T38 的 304 行表另存于
  `logs/19-t38-reconciliation-reconstructed.csv`）；以本节的 319 / 205·73·23·18 为准（T27 修订）。

## 3. AC-9.3 — `qtbase_zh_CN` 随包可加载

交付：`04-qtbase-vendor.md`。要点：

- vendor 文件 `translations/qtbase_zh_CN.qm`：**147,222 B**，sha256
  `0481a9b92a6f0578400e960a7d5ee70217a6a450eaf7f98b7d32b8dbd0d58616`
  （Debian `qt6-translations-l10n`，Qt 6.10.2+dfsg-7，与镜像 Qt 6.10.2 同版本）；
- 构建树：`<build>/src/app/translations/{vedit,qtbase}_zh_CN.qm`（CMake `qt_add_lrelease` +
  `add_custom_command` 复制 vendored qtbase；`install(FILES ${VEDIT_QM_FILES} DESTINATION bin/translations)`）；
- 运行期：`src/app/i18n.cpp::installTranslations` 从同一目录 `QTranslator::load(qtbase_<tag>.qm)`，
  先装 qtbase 再装 vedit；缺失非致命（`InstallResult::qtbaseLoaded`）；
- 探针实测装载成功（`i18n.qtbase-loaded` / `i18n.qtbase-file`，§1 摘录）；
- 打包：`assemble-appdir.sh --out target/tmp/t38-appdir` → **appdir-exit=0**，
  `bin/translations/` 两个 .qm 都在，`runtime-manifest.txt` 两行含 sha256：
  `0481a9b9…8616  147222  bin/translations/qtbase_zh_CN.qm`、
  `e89c575e…a696  53828  bin/translations/vedit_zh_CN.qm`
  （日志 `logs/29-appdir-assemble.log`）。

## 4. 三类缺口与完成度

| 类 | 内容 | 结果 |
|---|---|---|
| **A** | 102 条已包 tr() 但 `type="unfinished"` | **102/102 已译**；lrelease 391/391（A 后），T40 当轮总计 **534/534，0 unfinished**（**当前值见本节下方 `.ts` 现状**，勿混读）。102 vs T21 的 101 的差已解释：1 条 unfinished 有文本（`Cancel`→`取消`，同文启发式）+ 101 条无文本被 lrelease 计为 `Ignored 101`（`logs/01`、`logs/06`、`logs/07`） |
| **B** | 源码里未包 tr() 的可见字面量 | 第一批 **78 call-site / 74 串**；第二批 **73 call-site / 70 串**（T38）；T40 修复 **39 条新 message 的包装**（3 条诊断状态行 + 11 条 Project 标题/表头组合 + 22 条状态栏消息 + 3 条 F3 审计打回的串，见 §9）；T38 合计 **151 call-site**；T27 再加 **6 条**（F8 状态栏模板 + 5 个 severity 词，见 §10）；`.ts` **942** 条 |
| **C** | Qt 标准按钮 | **随包并可加载**（vendor + 装载 + CMake + 打包 + 探针四重证据） |

`.ts` 现状（**T51 重建后**，见 §10.3 的 runbook 与 `logs/59-t51-final.log`）：
**942 `<message>` / 936 finished / 6 vanished / 0 unfinished**，sha256
`151b3f8448aad71ec4979add10e056d2ea12c1200103a59a3b7f2646e2bacca1`；
lrelease `Generated 936 translation(s) (936 finished and 0 unfinished)`（`logs/56-t51-lrelease.log`），
编译产物 `target/qt6-build/src/app/translations/vedit_zh_CN.qm`
sha256 `1c0a9e068cea51311bb4667d875430c9f42b54a3c6cc80a55d9b4f5ceea9c00e`。

两个口径分开写，否则会自相矛盾：

- **942** = `.ts` 里的 `<message>` 元素数（lupdate 抽到 930 条源码串 + add_t40 复原的 6 条 ternary 后缀
  + 其余手写条目）；
- **936** = lrelease 实际写进 `.qm` 的条目数。差的 **6** 条是 `type="vanished"` 条目：它们的英文源
  已不在 `src/` 里（例如 T40 当时的 `Rename preview: %1 edit(s) from %2`），没有调用点可译，
  所以**正确**地不算 finished。lrelease 的 `0 unfinished` 与这 6 条不冲突（vanished ≠ unfinished）。
  Project 头那 6 条 ternary 后缀（lupdate 看不见）已由 `add_t40_messages.py` 的 `refinish_vanished()`
  复原并计入 finished（`logs/53-t51-repair-scripts.log`，见 §10.3）。

**与 T40 报告 573 的差异（如实记录）**：本轮开始时 `.ts` 实测已是 **938 `<message>` / 917 finished**
（mtime 2026-11-04 12:50），不是 T40 时的 573 —— T40 之后工作区里其他 lane 继续跑
`lupdate -recursive src` 并合并各自译文（同一命令见各 lane 的 memory 记录），`.ts` 因此持续增长。
T27 在此基础上 +6 条（并修回 6 条 vanished）；`573` 只作为 T40 当轮的历史快照保留在 §9，不再是当前数字。

## 5. 门禁（实测 exit code）

**T27 本轮收口实测**（每行都有一条 `logs/` 日志；没有一条是"沿用上一轮"）：

| 门禁 | 命令 | 结果 |
|---|---|---|
| 全量构建 + ctest | `bash scripts/build-qt.sh` | **BUILD_EXIT=0**；`100% tests passed, 0 tests failed out of 38`；`M0-SMOKE ok=1`（`logs/39-t27-final-build-with-ctest.log`） |
| selfcheck | `docker run … vedit-app --selfcheck --state-dir /tmp/sc` | **exit 0**；`EDIT-CORE-SELFTEST ok=1 checks=99 failures=0`、`APP-SHELL-SELFTEST ok=1 checks=290 failures=0`（`logs/44-t27-selfcheck.log`） |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings`（`vedit-build:local`） | **exit 0**，`Finished dev profile`（`logs/41-t27-clippy.log`） |
| fmt | `cargo fmt --all -- --check`（rustc 1.99.0） | **exit 0**，无输出（`logs/42-t27-fmt.log`） |
| docs | `bash build/gates/docs.sh` | **exit 0**，`[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md`（`logs/43-t27-docs-gate.log`） |
| lrelease（0 unfinished） | `lrelease translations/vedit_zh_CN.ts` | **`Generated 936 translation(s) (936 finished and 0 unfinished)`**（`logs/56-t51-lrelease.log`；T27/T29 时为 929/922） |
| i18n 探针（T40 的 43 checks） | `bash evidence/i18n-l2/probe/build-i18n.sh` + 跑同一二进制（截图落新目录） | probe-build-exit=0；**43 checks / 0 failures**；截图 `shots-t27/probe43/`（`logs/45`、`logs/46`） |
| **T27 状态栏探针**（新增） | `bash evidence/i18n-l2/probe/build-status-i18n.sh` + `run-status-i18n.sh` | **10 checks / 0 failures**，`诊断 1/2 - 错误：…` / `诊断 2/2 - 警告：…`（`logs/37`、`logs/38`、`shots-t27/`） |
| 对账表 | `python3 evidence/i18n-l2/tools/make_reconciliation.py` | `findings=319 rows=319`；`205 / 73 / 23 / 18`；`待定 0`；**exit 0**（`logs/34-t27-reconciliation.log`） |
| `verify_claims` 负向自测（新增） | `python3 evidence/i18n-l2/tools/verify_claims_selftest.py` | **4 cases, 0 failure(s)**，`fragment-only` 用例 `verified=ts-fragment-unwrapped → claim=no`（`logs/48-t27-verify-selftest.log`） |
| 构建期编译（warnings-as-errors） | 见第一行 | 0 `error:`（`logs/39`） |

**T40 当轮（历史，保留不删）**：`logs/24-t40-final-build-with-ctest.log`（38/38）、
`logs/23-t40-probe.log`（43 checks）、`logs/25-t40-lrelease.log`（573/573）、
`logs/29-appdir-assemble.log`（AppDir 218 文件）、`logs/22-t40-claim-audit-summary.md`
（A 74 / B 11 / C 15 / D 219）、`logs/27-ac47-callsites.log`（617 断言点）。
这些数字描述的是 T40 当时的状态，**不是当前值**；当前的构建/探针/`.ts` 计数以本节上表为准。
AppDir 打包与 AC-4.7 断言点计数本轮**未重跑**（不在七道门禁内），其 T40 数值原样保留、未改写。

## 6. 写域与「未改写 `evidence/i18n/**`」证明

- 本任务改/增（全部在合同 scope 内）：`src/**` 的产品源码包装（第一批 14 文件 + 第二批 14 文件，
  其中部分重叠）、`translations/vedit_zh_CN.ts`（+`qtbase_zh_CN.qm`）、`CMakeLists.txt`、
  `src/app/CMakeLists.txt`、`packaging/runtime/assemble-appdir.sh`（前序写者）、`evidence/i18n-l2/**`；
  + captain 书面授权的 scope 例外：`THIRD_PARTY_NOTICES.md`（§10，见 `06-license-third-party.md`）。
- **T27（本轮）**——只碰 4 处：`src/editor/kernel/lsp_actions.cpp`（模板）、
  `src/editor/kernel/diagnostics_annotations.h`（5 个 severity 词）、`translations/vedit_zh_CN.ts`
  （+6 条译文）、`evidence/i18n-l2/**`（新增 `probe/status-i18n-probe.cpp` + 其 build/run 脚本、
  `tools/add_t27_messages.py`、`tools/verify_claims_selftest.py`、`logs/30…44`）。
  **未改** `build/gates/**`、`scripts/**`、任何测试、`evidence/i18n/**`。
- `evidence/i18n/**` 未被改写：**本任务开始（2026-11-03 00:15）之后该目录没有任何文件 mtime 变化**
  （`find evidence/i18n -newermt "2026-11-03 00:15" -type f` → 0 行；最新文件仍是
  2026-11-02 23:09 的 T21 产物）。466 基线原样保留。
## 7. 未证明 / 边界（诚实列出）

1. 探针不逐条覆盖全部 942 条 `.ts` message；它断言的是 AC-9.1 的代表面 + **T40 新增的 F1/F2 全部串**
   （§1.1 的逐条表）。其余文本由 `.ts`/lrelease 与源码包装证明，**没有逐条截图**。
2. 计时/视觉细节：截图在 pinned 镜像 + 随包 CJK 字体下生成；宿主字体不同的观感不在本轮证据内。
3. **非句子的机器记号**仍保持英文，但每一条都按 AC-4.3 的命名类别逐条给了理由（§3 的表：
   样式表 7 · 日志行 8 · 开发者 seam 占位 1 · 装饰性 glyph 2 = 18）。判据见 §3 的那一句话；
   **「batch-2 边界」这个大类理由已删除**（那正是 T39 F4 判的自相矛盾点）。
4. **原 5 行 `待定` 已闭环（0 行）**：`filetree_integration.cpp`（`Opened %1`）与
   `settings_panel.cpp`（2 条设置页状态/诊断标题）经 captain 追加授权后包装 + 翻译；
   在授权前这 5 行被如实记为 `待定` 且对账工具 exit 1（**红门禁没有被写成绿**）。
5. `THIRD_PARTY_NOTICES.md` 是 captain 授权的 scope 例外；追加前后 sha256 见 `06-license-third-party.md`。
6. qtbase 许可按 Debian 包事实登记为 **GPL-3 with Qt-1.0 exception**（不是 LGPL；GFDL 只覆盖文档），
   与 captain 早先的 LGPL/GFDL 假设不同——报告以事实为准。
7. **已修（T27）**：T40 登记的残留——`lsp_actions.cpp` 的 `describeDiagnostic()` 模板
   `Diagnostic %1/%2 - %3: %4`（F8 跳转的状态栏文本）与其 `%3` 的 `diagnosticsSeverityName()`
   英文词——现在都走 `QCoreApplication::translate("VEdit", …)`，`.ts` 有中文，lrelease 0 unfinished，
   并在**真实产品 shell 的 F8 路径**上截图取证（`诊断 1/2 - 错误：…`）。证据与命令见 §10。
   它仍然不在这条 319 行的对账发现集里（helper 返回值，discovery 的 sink 窗口看不到），
   所以它由**独立探针**（§10）而不是对账行来证明。

## 8. 复现（关键命令）

```bash
# 抽取 + 对账
python3 evidence/i18n/extract_i18n_candidates.py --any-literal --format csv \
  --out evidence/i18n-l2/logs/16-any-literal-batch2.csv
python3 evidence/i18n-l2/tools/enumerate_b_class.py \
  --csv evidence/i18n-l2/logs/16-any-literal-batch2.csv --ts translations/vedit_zh_CN.ts \
  --out evidence/i18n-l2/logs/17-b-discovery-batch2.csv \
  --summary evidence/i18n-l2/logs/17-b-discovery-batch2-summary.txt
python3 evidence/i18n-l2/tools/make_reconciliation.py

# 翻译编译与门禁
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc '/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/check.qm'
bash scripts/build-qt.sh                     # 含 ctest 38/38
bash evidence/i18n-l2/probe/build-i18n.sh && bash evidence/i18n-l2/probe/run-i18n.sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'bash packaging/runtime/assemble-appdir.sh --out target/tmp/t38-appdir'

# T40：声称校验 + F3 审计（写 .ts 译文见 tools/add_t40_messages.py）
python3 evidence/i18n-l2/tools/add_t40_messages.py            # 幂等；清掉停用的 %2 模板 + 复原被 lupdate 标 vanished 的 ternary 消息
python3 evidence/i18n-l2/tools/make_reconciliation.py         # verified 列；不可验证即 exit 1
python3 evidence/i18n-l2/tools/summarize_audit.py             # logs/22-t40-claim-audit-summary.md

# T27：F8 状态栏文案的译文 + 声称校验收紧 + 收口
python3 evidence/i18n-l2/tools/add_t27_messages.py            # 幂等；+6 条（模板 + 5 词）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc \
  '/usr/lib/qt6/bin/lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts'
#   ^ 必须带 -locations absolute：Qt 6.10 的默认是 relative，会把文件名丢掉（§10.3）
python3 evidence/i18n-l2/tools/add_t40_messages.py            # lupdate 之后复原 6 条 ternary 后缀
python3 evidence/i18n-l2/tools/verify_claims_selftest.py      # 负向自测：fragment-only 必须不是 yes
python3 evidence/i18n-l2/tools/make_reconciliation.py         # 319 行 / 205·73·23·18，exit 0
bash evidence/i18n-l2/probe/build-status-i18n.sh && bash evidence/i18n-l2/probe/run-status-i18n.sh
```

## 9. T40 修复（T39 的 F1–F5 逐条处置）

T39（rec-20261102T164538-1e1608，Review G7 i18n-l2）判 FAIL；contract `T40` 的 `writeScopes` 为空
（captain 建行疏漏），写域经 captain 书面授权后执行。逐条：

### F1 [High] DiagnosticsPanel 状态行 — 已修
- `src/workspace/diagnostics/diagnostics_panel.cpp:306` 的 `QStringLiteral("%1 — %2 diagnostic(s)")`
  → `QCoreApplication::translate("VEdit", …)`；同一状态行的两个兄弟片段 `pending`(:301)、
  `saved file`(:304) 同批包装（否则该行会半英半中）。
- 译文：`%1 — %2 条诊断` / `待分析` / `已保存的文件`。
- 证据（`logs/23-t40-probe.log`）：`text.diagnostic.status-line`（真实 widget，`— 0 条诊断`）、
  `text.diagnostic.status-template`、`text.diagnostic.pending`、`text.diagnostic.saved-file` 全 ok。

### F2 [High] Project 面板标题/表头 — 已修
- `project_integration.cpp:58` dock 标题 + `project_model.cpp:352` 的 `Project: %1 — %2 %3`
  + `file`/`files` 名词 + ` · …` 后缀（单/复数各一条；共 8 条消息）全部包装 + 翻译。
- 对账表新增 **15 条 supplement 行**（T38 的 UI-sink 窗口没选中这些 call-site），逐条带来源与理由。
- 证据：`text.project-dock-title — 工程`；`text.project-header — 工程：demo-project — 2 个文件（实时）`
  （真实 `ProjectModel::summary()`，受控 fixture：2 文件 + 1 文件列表）；
  `text.project-header-template` / `text.project-header-suffixes` / `text.project-file-nouns` 全 ok。

### F3 [Medium] 对账表假声称 — 已修（根因）
- 根因：`tools/make_reconciliation.py` 无条件相信 discovery CSV 的 `already_wrapped`；discovery 的
  「已包」判定是固定窗口（`enumerate_b_class.py` 的 `line-5..line+3`）。
- 修法：每行现场校验（`tools/verify_claims.py`）——字面量是否落在 **自己调用点**的
  `tr(...)`/`translate(...)` 实参括号内（括号扫描跳过字符串字面量），以及 `.ts` 是否有同文
  `<source>`（或同文件、调用点附近的更长消息的片段，避免 `Opened %1` 被另一条消息误判）。
  CSV 增加 `verified` 列；不可验证 → 生成器 **exit 1**。
- F3 点名的 `diagnostics_panel.cpp:306` 现在**真**了（包装 + 译文 + 校验通过），理由改为
  「verified: …（tools/verify_claims.py）」。
- 审计：`logs/20-t40-claim-audit.csv`（逐行）+ `logs/22-t40-claim-audit-summary.md`（分组）。
  审计 CSV 逐行给出 captain 要的「源码调用点 + `.ts` 命中情况」：`path:line`（已按当前树重新定位）、
  `wrap`（`own-call` = 字面量在自己的 translate 实参括号内 / `arg-fragment` / `none`）、
  `ts`（`exact` / `fragment` / `none`）、`statement`（调用点语句片段）、`change` 与 `old_*` 列。数值：
  **A claim-flip 74**（yes→no 4 = 2 条日志行 + 2 条样式表改判「不译」；
  no→yes 70 = T38 batch-2 之后现在确实已包且 `.ts` 有译文）、**B decision-change 11**
  （含原 5 行「待定」→`译（本批）`：captain 追加授权后已包装+翻译）、**C supplement 15**、
  **D reason-verified 219**。旧表可用 `tools/make_reconciliation.t38.py` 逐字节重建
  （`logs/19-t38-reconciliation-reconstructed.csv`，304 行 / 220·27·23·34，与 T38 报告一致）。
- **清零**：`待定` 从 5 → **0**，`make_reconciliation.py` 由 exit 1 → **exit 0**；5 行全部变成
  `译（本批）`（`Opened %1`、设置页 2 条）；工具没被放宽，是缺口被补齐。

### F4 [Medium] 21 条瞬时状态栏 — 逐串判定后 20 译 / 1 命名豁免
- **22 条状态栏消息全部译**（`app_shell.cpp` 4：`Session could not be restored…`、`Saved`、`Saved %1`、
  `no project`；`lsp_actions.cpp` 1；`diagnostics_integration.cpp` 17，逐条见 CSV 的 `译（本批）` 行）。
  即 F4 的 21 行里 20 条用户句子全译（第 21 行是下面那条机器记号），另加 1 条同类兄弟 `Saved`。
- **1 条** `severity=%9 message='%10' file='%11'`（`lsp_actions.cpp:575`）属
  **AC-4.3 technical identifier**：`字段=值` 的开发者日志转储，从不渲染到 UI → 保留英文、逐条命名理由。
- 按同一判据收编的同类假声称：2 条日志行（`app_shell.cpp:246`、`search_jump_target.cpp:120`）+
  2 条样式表（`settings_panel.cpp:1343/1498`）改判「不译」；1 条开发者 seam 占位
  （`panel_registry.cpp:43`）保留英文并命名类别。
- 判据（写进 `03-b-reconciliation.md` §3，可复用）：**读起来像用户句子 → 必须译**；只有机器记号
  （样式表/富文本片段、`字段=值` 日志或转储、装饰性前缀、指向代码位置的开发者占位）可豁免，
  且**逐条**命名。**「batch-2 边界」大类理由已删除。**

### F5 [Nit] 覆盖声明与探针 — 已修
- §1.1 用**逐条表**替换概括措辞；探针 32 → **43 checks**（新增 11：诊断状态行、3 条 F1
  模板/片段、Project dock 标题、4 条 F2 模板/后缀、Project 面板表头、F3 审计打回的 3 条串），
  重跑 **0 failure**。
- 报告 §1 的覆盖声明现在与探针断言一致；未断言部分在 §7 明确为「不逐条」。

### 约束保持
- **AC-9.3**：qtbase_zh_CN 随包可加载——vendor/CMake/装载路径未动，探针 `i18n.qtbase-loaded` /
  `i18n.qtbase-file` 仍 ok。
- **AC-9.4**：`evidence/i18n/**` 未写（`find evidence/i18n -newermt "2026-11-03 00:15" -type f` → 0 行）；
  测试/门禁脚本未改；AC-4.7 断言点 617 → 只增（probe 新增 `check(`）。
- **构建**：`bash scripts/build-qt.sh` **BUILD_EXIT=0**，ctest **38/38**（`logs/24`）。

### 9.6 `tools/verify_claims.py` 登记（captain 追认的新增文件）
- **路径**：`evidence/i18n-l2/tools/verify_claims.py`（T40 新建；captain 书面追认）。
- **做什么**：对账表的**逐行声称校验器**——`wrap_kind()` 找字面量自己的调用点（多行语句用语句边界
  回溯，多行拼接用前缀定位），用**括号扫描**（跳过字符串字面量，因为消息本身含括号）判断该字面量
  是否落在某个 `tr(...)`/`translate(...)` 的实参范围内；`ts_hit()` 判断 `.ts` 是否有同文 `<source>`
  或「同文件、调用点附近」的更长消息片段（`.ts` 的 `<location>` 用来区分 `Opened %1` 与
  `Opened %1 line %2` 这类前缀巧合）。`assess()` 汇总成 `verified` 列。
- **为什么必须在 `tools/`**：它是 `make_reconciliation.py` 的**生成期依赖**（import 进来的库），
  与 `extract_i18n_candidates.py` / `enumerate_b_class.py` 同类；`evidence/` 放的是产物（CSV/日志/截图），
  把生成逻辑放进去会与「evidence 只放证据」的约定冲突，也会让 `find`/对账工具把它当数据。
- **不进入任何构建/门禁路径**：它是**维护期工具，非产品代码**——不被 CMake、`scripts/build-qt.sh`、
  `build/gates/**`、`scripts/gates.sh` 调用；只用标准库；跑它不会改任何源文件
  （写文件的是 `add_t40_messages.py` 与 `make_reconciliation.py`）。
- **下次怎么跑**：`python3 evidence/i18n-l2/tools/make_reconciliation.py`（自动 import 它并逐行校验；
  不可验证即 exit 1），再 `python3 evidence/i18n-l2/tools/summarize_audit.py` 出审计表。
- **T27 收口（两处改动 + 一支自测）**：
  1. `assess()` 里 `fragment` 命中**只有在 `wrap == own-call` 时**才算 `already_wrapped=yes`；
     字面量不在自己语句的 `translate()` 实参里、却恰好是某条更长译文的片段时，新状态
     `ts-fragment-unwrapped` 一律**不是 yes**（这正是 T39/F3 那类假声称）。
  2. `load_ts_messages()` 现在同时读懂 lupdate 的**绝对**与**相对**两种 `<location>`
     （相对形式缺 filename、行号带正负号；旧实现把它读成"文件空 / 第 1 行"，会让 fragment 校验
     静默失效）。
  3. 自测 `tools/verify_claims_selftest.py`：在临时目录造 1 个 .cpp + 1 个 .ts，断言
     `fragment-only → 非 yes`、`fragment+own-call → yes`、`own-call exact → yes`、`no-wrap → no`，
     可无限复跑、不写工作区（`logs/48`，exit 0）。

### 9.7 三条声明（按 captain 要求）
1. **写域**：授权清单之外的改动 = **空**。授权清单 = captain 的 6 条 + 追加的
   `filetree_integration.cpp`/`settings_panel.cpp` + 追认的 `tools/verify_claims.py`；
   实际改动的源文件（8 个 .cpp + `translations/vedit_zh_CN.ts` + `evidence/i18n-l2/**`）逐一在上文 §9
   与 `git status` 中列明。**命令副产物（不是代码改动）**：`bash scripts/build-qt.sh` 会重写
   `evidence/m0-build/build-logs/**` 与 `evidence/ffi-abi/logs/**` 的日志（命令自身的输出位置），
   其余构建产物在 `target/`（gitignored）。
2. **`evidence/i18n/**` 仍 0 文件变更**（AC-9.4）：`find evidence/i18n -newermt "2026-11-03 00:15" -type f`
   → **0 行**。
3. **红门禁不得写成绿**：本轮没有绕过——5 行 `待定` 在授权前被如实记录为 `exit 1` 并写进报告；
   授权补齐后工具才 **exit 0**（不是把校验放宽，是把缺口补上）。若未来再次出现不可验证行，
   `make_reconciliation.py` 会继续 **exit 1**。

## 10. T27 修复（AC-7.1 / AC-7.2 / AC-7.3 收口）

### 10.1 AC-7.1 — F8 状态栏诊断文案 i18n（已修，真实 shell 取证）

- **代码（2 个文件，均在 `src/editor/kernel/`）**
  - `lsp_actions.cpp` `describeDiagnostic()`：`QStringLiteral("Diagnostic %1/%2 - %3: %4")`
    → `QCoreApplication::translate("VEdit", "Diagnostic %1/%2 - %3: %4")`；
  - `diagnostics_annotations.h` `diagnosticsSeverityName()`：`error/warning/information/hint/problem`
    五个词 → `QCoreApplication::translate("VEdit", …)`（并补 `#include <QCoreApplication>`）。
- **译文**：`tools/add_t27_messages.py`（幂等）向 `VEdit` context 加 6 条，带真实 `<location>`：
  `诊断 %1/%2 - %3：%4` / `错误` / `警告` / `信息` / `提示` / `问题`；lrelease **0 unfinished**（§5）。
- **真实产品路径的证据（不是"译文表里有"）**：新探针 `probe/status-i18n-probe.cpp`
  （`build-status-i18n.sh` + `run-status-i18n.sh`）走完整产品链：
  `ApplicationShell` + 真实 `EditCoreProvider` → 打开一个 `.sv`（得到真实 `KTextEditor::View`，
  其创建时由 `ktexteditor_provider.cpp` 装上 `DiagnosticsAnnotations` + `DiagnosticNavigator`）
  → 推两条标注 → `DiagnosticNavigator::navigate(true)`（就是 F8 的路径，
  `lsp_actions.cpp:777` 的 `statusBar()->showMessage(describeDiagnostic(...))`）
  → 读 `QStatusBar::currentMessage()`。
  实测（`logs/38-t27-status-probe-run.log`）：**10 checks / 0 failures**，
  `text.status-bar.error — 诊断 1/2 - 错误：unused signal 'unused'`、
  `text.status-bar.warning — 诊断 2/2 - 警告：constant select`；截图
  `shots-t27/10-shell-status-zh_CN.png`（整窗）与 `shots-t27/11-status-bar-zh_CN.png`（状态栏条）。
- `describeDiagnostic()` 是匿名命名空间里的 helper 返回值（不是 tr() 的直接实参），
  所以它**不在** 319 行对账发现集里 —— 这也正是 §7.7 当初只能登记、不能由对账表证明的原因；
  本轮用独立探针补上。

### 10.2 AC-7.2 — G7 报告修订（已不再自相矛盾）

- §2：`304 / 220·27·23·34` → **`319 / 205·73·23·18`**，并注明旧数是 T38 的（T38 表另存
  `logs/19-t38-reconciliation-reconstructed.csv`）、生成器 `findings=319 rows=319` **exit 0**
  （`logs/34`）。
- §1.1：补 3 条原先漏列的断言（`probe.temp-state`、`shell.search-panel`、`text.t40-audit-closures`），
  表内 43 项与探针实测一致；「13 条 T40 行」→ **「10 条 T40 行」**（另 1 条 F3 审计串单独命名）。
- §4/§5/§6/§7/§8/§9.6 的计数与门禁已按**本轮实测**重写（T27 收口时 943 `<message>` / 929 finished /
  14 vanished；**T51 重建后为 942 / 936 / 6**，见 §4 与 `logs/59-t51-final.log`；七道门禁 exit code、
  新增探针与自测）；T40 的历史数字一律标注为「T40 当轮」。

### 10.3 根因发现：`.ts` 的 `<location>` 格式漂移（不修则对账必红）

- **现象**：本轮接手时 `.ts` 已是 **938 `<message>` / 917 finished**，且是 lupdate 的**相对**
  location 形式（`<location line="+1"/>`，不带文件名 —— Qt 6.10 `lupdate` 的默认）。
  `verify_claims.ts_hit()` 的片段判定要求「同文件、调用点 ±20 行」，相对形式被读成
  「文件空 / 第 1 行」，于是 **47 行静默退化为 `wrap-only`、17 行变 `待定`** →
  `make_reconciliation.py` **exit 1**（红门禁，不是绿）。
- **修法**：① `load_ts_messages()` 现在两种形式都读得懂（相对行号按上下文累加、缺 filename 时沿用
  上一个显式文件）；② 用 `lupdate -locations absolute -recursive src -ts translations/vedit_zh_CN.ts`
  重生成，让每条 location 带文件名（`logs/35`）。之后：319 行、205·73·23·18、**exit 0**。
- **顺带查出的第二个缺陷**：6 条 Project 表头后缀（` · %1 filelist` / `…filelists` / `…variable` /
  `…variables` / `…line skipped` / `…lines skipped`）被 lupdate 标成 `type="vanished"` ——
  它们在源码里是 **ternary 分支**
  （`translate("VEdit", lists == 1 ? " · %1 filelist" : " · %1 filelists")`），
  而 lupdate 只抽取**直接实参**的字面量。后果：43 项探针原本 **FAIL
  `text.project-header-suffixes`**（面板表头悄悄回落英文）。
  修法：`add_t40_messages.py` 新增 `refinish_vanished()` —— 把「本清单仍需要、却被标 vanished」的
  块删掉并按正常路径重加（带 location），**每次 lupdate 之后跑一次即可复原**（`logs/47`）。
- **未修（不在本任务写域，如实记录）**：VCS lane 的 `Git/Subversion/added/modified/deleted/renamed/
  conflict/unknown` 在 `src/workspace/vcs/vcs_source.cpp` 里是 `return "Git";` 这种**裸字面量**，
  没有 translate() 调用，运行时仍是英文；要治必须改那个文件的调用形式（不属 `src/editor/kernel/`）。
  它们旧的 `VEdit` 条目正确地处于 vanished，未被本轮"修绿"。

### 10.4 边界与没做的事（诚实）

- AppDir/AppImage 打包本轮**未重跑**（不在七道门禁内）；AC-4.7 断言点本轮**未重算**——但只增不减：
  新增探针有 10 个 `check(`，没有删任何断言（`617` 的下界仍成立）。
- 未改 `build/gates/**`、`scripts/**`、`tests/**`、`evidence/i18n/**`；`.qm` 由构建产出，未手改。
- 截图是 `QT_QPA_PLATFORM=offscreen` 下抓的（与 T38/T40 同口径），**没有**做 xcb 视觉评审。

### 10.5 变更清单（T27）

| 路径 | 动作 |
|---|---|
| `src/editor/kernel/lsp_actions.cpp` | `describeDiagnostic()` 模板走 `translate("VEdit", …)` |
| `src/editor/kernel/diagnostics_annotations.h` | `diagnosticsSeverityName()` 5 词走 translate + 补 include |
| `translations/vedit_zh_CN.ts` | +6 条中文（模板 + 5 词）；复原 6 条 vanished 后缀；`-locations absolute` 重生成 |
| `evidence/i18n-l2/probe/status-i18n-probe.cpp` | 新增（10 checks：模板/5 词/真实 F8 状态栏/截图） |
| `evidence/i18n-l2/probe/build-status-i18n.sh`、`run-status-i18n.sh` | 新增 |
| `evidence/i18n-l2/tools/add_t27_messages.py` | 新增（幂等加 6 条） |
| `evidence/i18n-l2/tools/verify_claims_selftest.py` | 新增（负向自测，4 cases） |
| `evidence/i18n-l2/tools/verify_claims.py` | fragment 只在 `wrap == own-call` 时算 yes；读相对 location |
| `evidence/i18n-l2/tools/make_reconciliation.py` | yes 状态收紧为 `own-call+ts`；文档串同步 |
| `evidence/i18n-l2/tools/add_t40_messages.py` | `refinish_vanished()` + `GONE_SOURCES`（已下线串） |
| `evidence/i18n-l2/50-i18n-l2-report.md` | §1.1/§2/§4/§5/§6/§7.7/§8/§9.6/§10 |
| `evidence/i18n-l2/logs/30…48-*` | 本轮全部实测日志（lrelease/lupdate/探针/门禁/自测） |
| `evidence/i18n-l2/shots-t27/` | 状态栏截图（新目录，未覆盖 `shots/` 基线） |
