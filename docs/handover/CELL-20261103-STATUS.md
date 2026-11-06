# CELL-20261103-STATUS · VEdit（cell-20261102T100600176Z）跨会话交接

> 编写：lead-integ（board **T42**），2026-11-03 01:55 (+08:00)。
> 依据：`evidence/final-acceptance/AC-MATRIX.md`（46 条 AC 全覆盖）、`docs/ACCEPTANCE-MATRIX.md`（公开镜像）、
> `.mpd/verify/records/*.json`、`evidence/**`、`.mpd/cellwork/cell-20261102T100600176Z/{requirements.md,split/,contracts/}`。
> 基线：HEAD `4e2d0a57`（`4e2d0a5`，main）+ 本 cell 工作树（验收时 `porcelain` **241** 条 / `-uall` **843** 条）。
> **本 cell 按用户裁定不管理 git**：本文件不含提交/分支/PR 建议，交付形态 = 工作树状态。
> 读法：先读 §0 / §3 / §4；§1 找证据；§5 是复现；**§7 是插件陷阱（下一次开工前必读）**。

> **2026-11-05 更正（UX 批次 2）**：本文件里的 T37「模块树 §C/D」是**旧 lane**（实例层级树 + 信号跳转）——
> 它已被 UX 批次 2 item 1 替换为**纯模块层级树**（只有模块/例化次数/定义位置，没有网络/驱动/负载行，双击打开声明）。
> 关于 `g_leaf[0]` 路径段渲染与“§D generate 2/5→5/5”的叙述保留只为历史记录，不再是当前模块树的行为。
> 另外，rev2 hover 一节“verible 不 advertise 时菜单置灰”的结论已被 UX 批次 2 实测推翻（见该节更正）。
> 当前口径见 `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md`。

## 0. 一屏摘要

**目标**：把 requirements rev1–rev3 的 46 条 AC 在最终树上验收并交付（T22 集成验收）。

**本轮已落地**：G0 遗留全收口（T46 walker 第三方 PASS、T37 模块树 §D 2/5→5/5（旧 lane，见文首更正）、T39 会话恢复旧 FAIL 收口、
T11 两个外部红、A-17 `$` 语义）；T20 主壳七项（search 106/4→106/0）；编辑器 T15 右键菜单 + LSP 动作通道 + F8、
T16 signature_help + 进阶 5 组、rev2 hover 真通道；i18n T18 运行时 + T21 466 对账/215 翻译 + rev3 G7 扩面
（A 类 102 完成、B 类 151 call-site、319 行对账、随包 `qtbase_zh_CN`）；AC-8.1 诊断 lane 的 Format 假理由缺陷修复。
**T22 验收 PASS**（`rec-20261102T175048-57cef3`）。

**当前已验证数字（全部在最终树上实跑）**：

| 项 | 值 |
|---|---|
| 构建 | `bash scripts/build-qt.sh --no-test` **exit 0**，二进制 `target/qt6-build/src/app/vedit-app` **103,716,024 B** |
| ctest | **38/38，0 failed**（DoD 基线 37/37；T19 新增 `vedit-signature-help-selftest` = `Test #18`，故 38） |
| `--selfcheck` | **245 checks / 0 failures**，exit 0 |
| clippy / fmt / docs | 三项 **exit 0** |
| lrelease | **573/573 finished，0 unfinished** |
| AC 矩阵 | **46/46 行全 PASS**，0 行未验 |
| HEAD / git | `4e2d0a5`，**零 git 内容**（无提交/分支/PR） |

## 1. 本轮做了什么（按工作线）

### (a) G0 遗留收口

| 工作 | 结果 | 证据 | 审查 |
|---|---|---|---|
| T46 层次 walker | 第三方盲审 PASS；flat/nested 1k 均 11000/11000 对独立 AST ground truth | `evidence/w6-hier/09-t46-hierarchy-fix.md`、`10-t46-recheck-*` | `rec-20261102T105951-afc97d` |
| T37 模块树 §C/D | `g_leaf[0]`/`u_arr[3]`/`g_if.u_if` 可见可跳；§D generate 观测 **2/5 → 5/5**（**旧 lane，已被 UX 批次 2 item 1 的纯模块层级树取代；见文首更正**） | `evidence/m-tree/50-t37-recheck.md:10,78,86` | `rec-20261102T112658-809446` |
| T39 会话恢复 | 旧 FAIL 的唯一发现 F1 已闭环 | `evidence/i3-session/` | `rec-20261102T121046-39474e` |
| T11 两个外部红 | ①`vd-lsp` router 未登记 `slang/setConfig` → catch-all（不再 mainloop 死）②verible-lint 批量诊断对齐 | `evidence/w5-backend/README.md`、`cap-*-live.txt` | `rec-20261102T111031-94587c` |
| A-17 filelist `$` | `$` 后不跟标识符 = 字面 `$`；`$$` 保留；缺失路径出诊断；fixture files=3 | `evidence/config/`（fixture + `print-config-*.txt`） | `rec-20261102T111732-5bc74a` |

### (b) T20 主壳七项

search 本地 `IJumpTarget` 删除并统一注册到 `jump::Registry`；search selftest 缓存修复
`106 checks / 4 failures → 106 checks / 0 failures`（断言条数不变）；当时 ctest **37/37**。
证据：`evidence/t20-shell/50-t20-report.md:9,29-30`、`evidence/fix-search-jump/02-after-green.log`、
`evidence/e4-actions/02-ctest.log`。审查：`rec-20261102T120036-ab3a72`（T14 复审）。

### (c) 编辑器：T15 → T16 → rev2 hover

- T15：右键菜单 + LSP 动作通道 + F8/Shift+F8 —— `evidence/e4-actions/`，`rec-20261102T124952-dc5399`。
- T16：`signature_help.{h,cpp}` 进 `VEDIT_EDIT_CORE_SOURCES` + 进阶 5 组 ——
  `evidence/e5-advanced/REPORT.md`，ctest `#18`；`rec-20261102T140727-16d8a4`。
- rev2 hover（Q7=A）：`SessionRequest::Hover`/`SignatureHelp` → `vd-ffi` → `src/workspace/diagnostics` →
  真实 `textDocument/hover` 请求与渲染（`evidence/e5-advanced/21-hover-rendered.png`）；
  verible 不 advertise 时菜单置灰并给真实原因。AC-3.2 口径 = **4 项可用 + hover 按后端能力**。
  **（2026-11-05 更正：verible 虽然 `hoverProvider:false`，但 hover 请求实测有内容；按 `false` 置灰是错的，
  UX 批次 2 B3 已撤掉该置灰——见 `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §5.F1。）**

### (d) i18n：T18 运行时 → T21 对账 → rev3 G7 扩面

- T18：`ui.language` Auto/English/简中 + 一键重启契约 + `.ts/.qm` —— `evidence/i2-i18n/REPORT.md`，
  `rec-20261102T145109-ed6786`。
- T21：466 逐条对账（466 = 215 UI + 195 non-UI + 31 已译 + 15 obsolete + 10 uncertain）+ 215 条 UI 翻译 ——
  `evidence/i18n/11-reconciliation-466.md`、`12-ui-blocks.md`，`rec-20261102T152121-4e59e9`。
- rev3 G7（Q8=B 扩语料）：A 类 **102** 条补全、B 类 **151 call-site** 包装、扩展对账 **319 行**
  （205 译已完成 / 73 本批 / 23 片段 / 18 不译 / 待定 0），随包 `qtbase_zh_CN.qm`（147,222 B，
  sha256 `0481a9b9…8616`）。G7 由**三位写者经 handoff 接力**（se-i18n → se-shell → dw-rust）。
  审查链：T39 **FAIL** `rec-20261102T164538-1e1608` → T40 修复 → T41 **PASS** `rec-20261102T172944-896e89`。

### (e) AC-8.1 诊断 lane

确认并修复真实缺陷：真·无 `settings.toml` 首启时 Format 门 `enabled=0` 且理由为假的
“verible-verilog-format not found”（TOOLCHAIN 实为 `verible-format:local`）。修后：enabled=1、
真实格式化 59→73 B 且与 CLI 参考输出 sha256 一致、缺失时给真实理由。
证据：`evidence/g6-diag/`（`00-red-before-fix-*`、`30-green-after-fix-*`），`rec-20261102T155333-2d3e39`。

### (f) A-17

同 (a) 最后一行；文档面 `docs/USER_GUIDE.md`（变量/转义/优先级），docs 门禁 exit 0。

## 2. 现在的验证状态

| 门禁 | 命令 | 观测结果 | 证据路径 |
|---|---|---|---|
| 构建 | `bash scripts/build-qt.sh --no-test` | exit 0；二进制 103,716,024 B | `evidence/final-acceptance/logs/gate-1-*.log`、`gate-1-binary.txt` |
| ctest（主判据） | `bash scripts/build-qt.sh`（容器内） | **38/38 passed，0 failed**（+ xcb smoke `M0-SMOKE ok=1`） | `.../gate-2-build-qt-full.log`、`evidence/m0-build/build-logs/33-qt6-ctest.log` |
| ctest（主机直跑） | `ctest --test-dir target/qt6-build` | exit 8，全部 `Not Run` —— **容器绝对路径 `/vedit-build/...` artefact，不是用例失败** | `.../gate-2b-ctest-host.log`（文件内写明原因） |
| selfcheck | `QT_QPA_PLATFORM=offscreen … --selfcheck --state-dir /tmp/sc` | exit 0，`checks=245 failures=0`（镜像内；主机缺 `libtree-sitter.so.0.25`） | `.../gate-3-selfcheck.log` |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings` | exit 0（`vedit-build:local` 内） | `.../gate-4-clippy.log` |
| fmt | `cargo fmt --all -- --check` | exit 0（rustc 1.99.0 b940084d7） | `.../gate-5-fmt.log` |
| docs | `bash build/gates/docs.sh` | exit 0 | `.../gate-6-docs.log` |
| lrelease（附加） | `lrelease translations/vedit_zh_CN.ts` | 573/573 finished，0 unfinished | `.../ac-9-lrelease-recheck.log` |

矩阵与逐条 AC 指针：`evidence/final-acceptance/AC-MATRIX.md`（权威）+ `docs/ACCEPTANCE-MATRIX.md`（镜像）。

## 3. 已知缺陷与没做的事（不软化）

1. **G7 最终报告 §2 仍带 T38 旧计数**：`evidence/i18n-l2/50-i18n-l2-report.md` §2 写 **304 / 220·27·23·34**；
   当前权威对账是 `evidence/i18n-l2/03-b-reconciliation.md` §0 的 **319 行 / 205·73·23·18**。
   **以对账 md §0 与 CSV 的 319 数据行为准**（本轮已独立复核 CSV 行数）。
2. **G7 报告 §1.1 少报**：探针实际 **43** 项，表内列 **40** 项（漏 3 个非字符串/审计检查）；
   文中「13 条 T40 行」实际 **10** 条。方向是**少报**，但仍登记。
3. **`evidence/i18n-l2/tools/verify_claims.py` 的 `fragment` 分支**可把字面量误判为已包；
   当前 **0/319 行**走该分支。建议收紧为：**只有 `wrap == own-call` 才算 yes**。
4. **`describeDiagnostic()` 的 F8 状态栏模板仍英文**（`src/editor/kernel/lsp_actions.cpp`，
   模板 `Diagnostic %1/%2 - %3: %4`；G7 报告 §7.7 已披露），属 T40 范围外。
5. **本轮没做**：AppDir/AppImage 打包验证（只验了 `assemble-appdir.sh` 的 `.qm` 装配规则）；
   xcb 视觉/UX 评审（截图存在但未做视觉判断）；各 lane 探针未重跑（矩阵 §5.3 声明，只重跑六道门禁 +
   三项独立复核）；T26 终审的三个文档小瑕疵未修（本轮禁止改 `docs/ACCEPTANCE-MATRIX.md` 与 `evidence/`）：
   - `AC-MATRIX.md` §5.1 把基线对账文件写成 `logs/03-baseline-accounting.txt`，实际在
     `evidence/final-acceptance/03-baseline-accounting.txt`（少一层目录）；
   - 公开镜像 `docs/ACCEPTANCE-MATRIX.md` 缺证据版的 H1 标题（正文其余一致）；
   - AC-0.7 的分流程行 T27/T28/T31/T36/T37（requirements/split/contract 维护）没有独立 verdict 记录
     —— 设计上不是 split-graph 的 verify=true 行，属范围说明、非缺口。

## 4. 下一轮建议（按价值排序）

1. **修文档 (1)(2)**：G7 报告 §2 计数改为 319/205·73·23·18（以对账 md §0 为准）、§1.1 补 3 条断言并改正 13→10。
2. **收紧 (3)**：`verify_claims.py` 的 fragment 分支改为 `wrap == own-call` 才算 yes，并加一条负向自测。
3. **翻译 (4)**：`describeDiagnostic()` 的状态栏模板 + `diagnosticsSeverityName()` 三词。
4. **打包验证**：AppDir/AppImage 端到端（`packaging/runtime/assemble-appdir.sh` 已有 `.qm` 规则，
   `evidence/i18n-l2/04-qtbase-vendor.md` §3 有 appdir 证据），须跑真实安装/启动。
5. **xcb 视觉 QA**：用真实 Xvfb+xcb 截图评审（现有截图未做视觉判断）。
6. **处理 §7 的 fencing 风险**：先决定是否升级插件/改协作方式，再排大任务。

## 5. 复现指南

```bash
# 构建 / ctest（ctest 在固定镜像内；主机的字面 ctest 会因容器绝对路径失败，属环境限制）
bash scripts/build-qt.sh --no-test
bash scripts/build-qt.sh
tail -3 evidence/m0-build/build-logs/33-qt6-ctest.log        # 100% tests passed, 0 failed out of 38

# selfcheck（镜像内；主机缺 libtree-sitter.so.0.25）
docker run --rm -e HOME=/tmp -v "$PWD":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local \
  target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc

# clippy（固定 Rust 镜像）/ fmt（栈上 rustc 1.99.0）/ docs
docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -e NO_AT_BRIDGE=1 \
  -v "$PWD":/vedit-build -w /vedit-build vedit-build:local \
  cargo clippy --workspace --all-targets -- -D warnings
RUSTUP_TOOLCHAIN=stable cargo fmt --all -- --check
bash build/gates/docs.sh

# lrelease + i18n 探针（43 checks）
docker run --rm -e HOME=/tmp -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm
bash evidence/i18n-l2/probe/build-i18n.sh && bash evidence/i18n-l2/probe/run-i18n.sh

# i18n 对账（发现条数 = 行数）
python3 evidence/i18n/extract_i18n_candidates.py --any-literal --format csv \
  --out evidence/i18n-l2/logs/16-any-literal-batch2.csv
python3 evidence/i18n-l2/tools/make_reconciliation.py       # 不可验证即 exit 1
```

**证据目录索引**

| 目录 | 内容 |
|---|---|
| `evidence/baseline/` | 开工基线（157 条，快照 sha256 `19f6f207…`，含 `SUMMARY.md` 口径说明） |
| `evidence/m-tree/` | T37 模块树 §C/D 重跑（`50-t37-recheck.md`，2/5→5/5） |
| `evidence/w6-hier/` | T46 walker 修复与 ground-truth 对账（`09-*`、`10-*`） |
| `evidence/w5-backend/` | T11 双后端证据（capability/format/截图） |
| `evidence/e4-actions/` | T15 右键菜单 + LSP 动作通道（含 ctest 37/37 日志） |
| `evidence/e5-advanced/` | T16 进阶 5 组 + hover 真实渲染（`REPORT.md`、`21-hover-rendered.png`） |
| `evidence/i2-i18n/` | T18 i18n 运行时探针/截图（`REPORT.md`） |
| `evidence/i18n/` | 466 对账与 215 条翻译（**本轮未改写**，mtime ≤ 11-02 23:09） |
| `evidence/i18n-l2/` | rev3 G7 扩面（`03-b-reconciliation.*`、`04-qtbase-vendor.md`、`probe/`、`shots/`） |
| `evidence/g6-diag/` | AC-8.1 Format 假理由缺陷（红→绿） |
| `evidence/final-acceptance/` | T22 验收（`AC-MATRIX.md` + `logs/` 全部门禁日志 + 基线对账 + 终态快照） |

## 6. 继承的开放问题

1. **规模性能（1k/10k）**：完整性已被 AST ground truth 证明（11000/11000，`evidence/w6-hier/09-*`），
   但 **wall-time / RSS 不是承诺**；T44 的 elaboration 数字作废（见旧交接 `CELL-20261102-STATUS.md` §3.2），
   只有“与 AST 对账”可信。下一轮若要性能结论，必须重新测。
2. **瞬时状态栏字符串策略**：G7 已订判据——**读起来像用户句子 → 必须译；只有机器记号**
   （样式表/富文本片段、`字段=值` 日志转储、装饰性前缀、指向代码位置的开发者占位）**可豁免且必须逐条命名**
   （`evidence/i18n-l2/50-i18n-l2-report.md` §F4 / `03-b-reconciliation.md` §3）。
   新增状态栏文案请沿用该判据，否则对账工具会 exit 1。
3. **qtbase 许可**：已解决并记录为 **GPL-3 with the Qt-1.0 exception**（不是 LGPL；GFDL 只覆盖文档），
   见 `THIRD_PARTY_NOTICES.md` §10 与 `evidence/i18n-l2/06-license-third-party.md`。
4. **AC 矩阵留下的边界**：`evidence/final-acceptance/AC-MATRIX.md` §4.1 的三类环境限制
   （主机 ctest 路径、缺运行时库、rustup 1.99.0 目录缺 cargo/无 GTK dev）不是产品缺陷，但会改变门禁写法。

## 7. MPD 插件告警（下一次开工前必读）

本 cell **两次**撞上插件的 checkpoint/reseed 缺陷：
`the compaction barrier gave up and fenced it (no-engine; the reseed failed: subagent creation requires the exact live parent agent)`。
**se-i18n 与 se-shell 两名成员在写完 handoff 后被 fenced**。已验证可用的修复：

- 被 fenced 成员的任务用 **`cellwork_dispatch {reassign:[{task_id, owner}]}`** 转给存活成员（会先 claim 再投递）。
- **`cellwork_send` 在成员有 8 条未投递消息后会拒发**；此时用官方 **`agent_teams_mail`** 作为 fallback。
- **G7 由三位写者经 handoff 接力完成**（se-i18n → se-shell → dw-rust）：任务测得体量大时，
  直接把“多棒接力”写进计划，别假设一个人扛完。
- **`agent_teams_plan create_task` 不传 `write_scopes` 会生成空 writeScopes 的冻结契约，直接卡死写者**：
  永远显式给 `write_scopes`，或在第一次写之前书面授权。
