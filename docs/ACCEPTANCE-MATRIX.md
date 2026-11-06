# T25 / T22 集成验收 — 验收矩阵（公开镜像）

> **本轮最终矩阵**：cell `cell-20261103T123639841Z`（requirements rev3）的验收矩阵不在本文件，
> 而是 `docs/FINAL-ACCEPTANCE-MATRIX.md`（权威版 `evidence/final-acceptance-2/AC-MATRIX.md`）。
> 本文件仍镜像**上一轮**的 46-AC 矩阵（`evidence/final-acceptance/AC-MATRIX.md`），未改写其判定。

<!-- 本文件是证据目录的公开镜像；权威版本与全部逐条证据指针在 evidence/final-acceptance/AC-MATRIX.md -->

> **镜像说明**：本文件内容与 `evidence/final-acceptance/AC-MATRIX.md` 一致（同一交付物，便于文档入口阅读）。
> 若两者出现差异，**以 `evidence/final-acceptance/AC-MATRIX.md` 为准**（其同目录 `logs/` 为门禁原始日志）。


- 任务：**T25 / T22 集成验收**（board `T25`，frozen contract `.mpd/cellwork/cell-20261102T100600176Z/contracts/final-t22.md`，sha256 `06b62f28…6a42`）
- Owner：**Lead / lead-integ**（`b75e0010-0cac-4c7b-9c86-d03a03b737ef`）· Verifier：**rev-a**（board `T26`，不同 agent）
- 验收树：HEAD `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f`（`4e2d0a5`，main，**零 git 动作**：不提交/不分支/不开 PR）
- 采集时刻：2026-11-03T01:31–01:45 +08:00
- 脏路径两套口径（**禁止相减**）：`porcelain`（无 `-uall`）**241** 条（开工基线 157 条）｜`porcelain -uall` **843** 条（本窗口另采）
- 契约 rev1 原文写 `ctest 37/37` 与 `docs/handover/`；captain 于 2026-11-03 书面修订为 **38/38**、并把交付说明位置改到矩阵内、`docs/handover/` 交后续独立任务（本交付遵循修订，见 §5）。

---

## 0. 结论摘要

| 项 | 结果 |
|---|---|
| 门禁 ① 构建 | **PASS** exit 0，最终二进制 `target/qt6-build/src/app/vedit-app` **103,716,024 B**，sha256 `5060ed39…d96ac`（G2 完整构建重建后复核） |
| 门禁 ② ctest（容器内主判据） | **PASS 38/38，0 failed** |
| 门禁 ② 主机直跑（如实留档） | CTest exit 8 / 全部 "Not Run"——**容器绝对路径差异，非用例失败** |
| 门禁 ③ `--selfcheck` | **PASS** `checks=245 failures=0` exit 0（固定镜像内；主机缺 `libtree-sitter.so.0.25`） |
| 门禁 ④ clippy / fmt / docs | **PASS** 三项 exit 0（clippy 在固定 Rust 镜像内；fmt 在 rustc 1.99.0 上） |
| 附加复核 lrelease | **PASS** 573/573 finished，0 unfinished（我在当前 `.ts` 上重跑） |
| AC 行数 | **46 行**（需求文档 AC-0.1 … AC-9.4 全部覆盖），**0 行未验** |
| 带原因排除项 | 7 项，见 §2 |

**一句话**：在 HEAD `4e2d0a5` 的最终树上，6 道门禁全部通过；7 项带原因排除项与 3 处环境限制已逐条列明；4 条已知产物缺陷如实登记（§4）。

---

## 1. 门禁总览（本轮实跑，非引用旧证据）

| # | 命令 | exit | 证据文件（本目录 `logs/`） | 说明 |
|---|---|---|---|---|
| G1 | `bash scripts/build-qt.sh --no-test` | **0** | `gate-1-build-qt-no-test.log`、`gate-1-binary.txt` | 尾部：`[qt6] PASS: … tests skipped (--no-test)`；当时二进制 sha256 `302dd8f5a29e…34004e`，G2 完整构建重建后最终为 `5060ed3970c1…d96ac`（同尺寸 103,716,024 B） |
| G2 | `bash scripts/build-qt.sh`（完整版，容器内 ctest） | **0** | `gate-2-build-qt-full.log` + `evidence/m0-build/build-logs/33-qt6-ctest.log` | `100% tests passed, 0 tests failed out of 38`；含 xcb 软件渲染 smoke `M0-SMOKE ok=1` |
| G2b | `ctest --test-dir target/qt6-build`（主机，按契约字面） | **8** | `gate-2b-ctest-host.log` | 全部 `Not Run`：CTest 文件记录 `/vedit-build/...` 容器路径；**非用例失败、非产品缺陷**（文件内已写解释） |
| G3 | `QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc` | **0** | `gate-3-selfcheck.log` | `APP-SHELL-SELFTEST ok=1 checks=245 failures=0`；主机直跑 exit 127（缺 `libtree-sitter.so.0.25`，文件头已留档） |
| G4 | `cargo clippy --workspace --all-targets -- -D warnings` | **0** | `gate-4-clippy.log` | 在 `vedit-build:local` 内：`Finished dev profile … in 26.92s`；主机两次失败原因（rustup 1.99.0 目录缺 cargo / 无 GTK dev 文件）在同一文件留档 |
| G5 | `cargo fmt --all -- --check` | **0** | `gate-5-fmt.log` | 在 rustc **1.99.0 (b940084d7)** 上零输出；字面命令因主机 rustup 1.99.0 目录缺 `cargo` 无法启动（同文件留档） |
| G6 | `bash build/gates/docs.sh` | **0** | `gate-6-docs.log` | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` |
| G7 | `lrelease translations/vedit_zh_CN.ts`（附加复核） | **0** | `ac-9-lrelease-recheck.log` | `Generated 573 translation(s) (573 finished and 0 unfinished)` |

**契约修订对照（ctest）**：DoD 基线为 37/37；T19 新增 `vedit-signature-help-selftest`（`ctest -N` = `Test #18`，只读清单确认）后本轮实测 **38/38，全通过 0 failed**（用例数 = 38 ≥ 37）。`#38 vedit-workspace-settings-selftest` 本来就在既有 37 内，不是新增项。

---

## 2. 带原因排除项（AC-0.5）

| 项 | 原因 | 结论 |
|---|---|---|
| `check_ffi_header_drift` | **仓库状态检查，不是代码检查**：`crates/vd-ffi/include/vd_ffi.h` 的漂移来自①开工基线已脏（`evidence/baseline/git-status-porcelain-v1.txt` 第 13 行 `M crates/vd-ffi/include/vd_ffi.h`）②其他 lane 的 ABI 新增③本 cell **不管理 git**（Q3=A）。任何 lane 都不因此被记缺陷。 | 带原因排除 |
| `tests` | 不适用于本 cell 的最终验收口径：Rust 单测由固定镜像内 `cargo test` 覆盖（T46/T37/T30 记录），C++ 用例由 ctest 38/38 覆盖；`build/gates/tests.sh` 是另一套（Rust 工作区）入口。 | 不适用 |
| `vendor` | 不适用：本 cell 不改 vendor 目录；第三方新增仅 `translations/qtbase_zh_CN.qm`（许可与归属见 `evidence/i18n-l2/06-license-third-party.md` + `THIRD_PARTY_NOTICES.md`）。 | 不适用 |
| `dist` | 不适用：分发产物（AppImage/AppDir）由专门 lane 负责，本轮只验证 AppDir 装配规则含两个 `.qm`（AC-9.3 证据）。 | 不适用 |
| `pack` | 不适用：打包不在本 cell 范围。 | 不适用 |
| `comments` | **设计 exit 1**（注释检测器：发现注释即非零），不是可用性门禁。 | 带原因排除 |
| `rows` | **设计 exit 1**（行数/记录完整性检查的另一入口），不是可用性门禁。 | 带原因排除 |

---

## 3. AC 覆盖矩阵（需求文档 46 条，每条一行）

> 状态含义：**PASS** = 本轮实跑或本 cell 内第三方 verdict 记录支撑；**PASS（lane 记录）** = 由本 cell 内另一 agent 的 verdict 记录 + 落盘证据支撑，本矩阵不重跑该 lane 探针；**带原因排除** = 见 §2。

### AC-0 基线/门禁

| AC | 判据（摘要） | 状态 | 证据（记录 id / 路径 / 命令） |
|---|---|---|---|
| AC-0.1 | `build-qt.sh --no-test` exit 0，产物存在 | **PASS** | G1；`logs/gate-1-build-qt-no-test.log`、`logs/gate-1-binary.txt` |
| AC-0.2 | ctest 通过（rev1 记 37/37） | **PASS** | G2 容器内 **38/38**（`evidence/m0-build/build-logs/33-qt6-ctest.log`）；G2b 主机失败如实留档 |
| AC-0.3 | `--selfcheck` exit 0 且 245 项检查 | **PASS** | G3；`checks=245 failures=0` |
| AC-0.4 | clippy / fmt / docs 均 exit 0 | **PASS** | G4（`rec` 无；容器内 clippy exit 0）、G5、G6 |
| AC-0.5 | 显式列出带原因排除项 | **PASS** | §2（7 项） |
| AC-0.6 | `evidence/baseline/` 有开工基线 | **PASS** | `evidence/baseline/SUMMARY.md`（157 条，快照 sha256 `19f6f207…`）+ 本目录 `00-environment.md`、`01-git-status-porcelain-v1.txt` |
| AC-0.7 | 每个 work 任务有不同 agent 的 PASS verdict 记录 | **PASS** | 映射见 §3.1；in-window 记录清单 `logs/verify-records-inwindow.txt`。**范围说明（非缺口）**：分流程行 T27/T28/T31/T36/T37 是 requirements / split / contract 的维护动作，设计上**不是** split-graph 里 `verify=true` 的行，因此没有独立 verdict —— 这是判定口径的说明，不是漏记；其余每个 work 行都有不同 agent 的记录。 |

### AC-1 上一轮遗留（T46 / T37 / T11 / 措辞）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-1.1 | T46 verdict 存在；flat/nested 1k 均 11000/11000 | **PASS（lane 记录）** | `rec-20261102T105951-afc97d`（第三方盲审 PASS，复跑 AST ground truth）；`evidence/w6-hier/09-t46-hierarchy-fix.md`；`cargo test --offline -p vd-deeplint -p vd-ffi` |
| AC-1.2 | T17 lane §C/D 重跑 + T37 verdict | **PASS（lane 记录）** | `rec-20261102T112658-809446`（T37 第三方 PASS）；`evidence/w6-hier/01-c-abi-end-to-end.txt`、`02-qt-macro-guard.txt` |
| AC-1.3 | T11 后端切换：3 个 `switch.b.*` 转 PASS；verible-lint 诊断 >0 且与 CLI 一致；T30 verdict | **PASS（lane 记录）** | `rec-20261102T111031-94587c`（T11/T30 盲审 PASS）；`evidence/w5-backend/`（`cap-verible-live.txt`、`cap-slang-live.txt`、`format-*`） |
| AC-1.4 | AC-1.3 全绿前文档不得写“后端切换已实现” | **PASS** | 全库 grep `后端切换\|backend switch`：唯一命中为 `docs/handover/CELL-20261102-STATUS.md:67`，其原文正是「不得说成“后端切换已实现”」的**禁止性措辞**；无违规宣称 |

### AC-2 主壳集成（T20 七项）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-2.1 | `search_types.h` 无本地 `IJumpTarget`；注册到 `jump::Registry` | **PASS** | 本轮复核：`grep IJumpTarget src/workspace/search/search_types.h` 无命中；实现移入 `search_jump_target.h:40`（适配 `jump::IJumpTarget`）；selftest `search_selftest.cpp` |
| AC-2.2 | search selftest 红转绿且断言不减少 | **PASS** | G2 ctest 38/38（含 `vedit-workspace-search-selftest`）；`evidence/fix-search-jump/02-after-green.log` |
| AC-2.3 | `global_search.cpp` 无 `-Werror=unused-parameter` | **PASS** | G1/G2 `-Werror` 构建 exit 0 |
| AC-2.4 | 面包屑真实 shell 挂载 | **PASS（lane 记录）** | `evidence/app-shell/06-seams.md`、`07-conclusion.md`；截图 `evidence/i18n-l2/shots/`（zh_CN 真实 shell） |
| AC-2.5 | 双窗格各自落点正确 | **PASS（lane 记录）** | `rec-20261102T140727-16d8a4`（T20 修复复审 PASS）；`evidence/e4-actions/`、`evidence/t20-shell/` |
| AC-2.6 | Qt ABI guard compile-only，SKIP 响亮 | **PASS（lane 记录）** | `evidence/w6-hier/02-qt-macro-guard.txt`、`04-a11-qt-guard.txt` |
| AC-2.7 | T19 标签会话持久化未被本轮改动 | **PASS** | `src/app/session_store.cpp` 在基线即脏（`evidence/baseline/git-status-porcelain-v1.txt`，2 处）；本轮唯一改动为 5 行 `tr()` 包装（`evidence/i18n/16-wrap-changes.txt:52`），行为未动 |

### AC-3 编辑器动作（T19/T20）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-3.1 | 右键菜单项齐全且逐项有效 | **PASS（lane 记录）** | `rec-20261102T124952-dc5399`（T17/T15 验收 PASS）；`evidence/e4-actions/03-probe.log`、`04-probe-xcb.log` |
| AC-3.2 | 定义/引用/重命名/格式化 4 项可用；hover 按后端能力；F8/Shift+F8 | **PASS（lane 记录）** | `rec-20261102T140727-16d8a4`；`evidence/e5-advanced/REPORT.md`（hover 真请求 + 渲染） |
| AC-3.3 | `signature_help.{h,cpp}` 进 `VEDIT_EDIT_CORE_SOURCES`；selftest 经真实通道 | **PASS** | G2 ctest `#18 vedit-signature-help-selftest` Passed；`src/editor/kernel/signature_help_selftest.cpp` |
| AC-3.4 | 进阶 5 组“可用或按能力置灰并说明” | **PASS（lane 记录）** | `evidence/e5-advanced/REPORT.md`（逐项 + 证据） |
| AC-3.5 | hover 真实请求证据，不得静态占位 | **PASS（lane 记录）** | `evidence/e5-advanced/REPORT.md` §hover；`rec-20261102T121046-39474e`（T39 复验 PASS） |

### AC-4 i18n（T18/T21 + rev3 扩面）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-4.1 | 语言 Auto/English/简体中文 + 持久化 | **PASS（lane 记录）** | `rec-20261102T145109-ed6786`（T21 复审 PASS，AC-4.1/4.2/4.8 三条款）；`src/app/i18n.cpp` |
| AC-4.2 | 一键重启契约（取消不写文件、不写 session.json、只弹一个框） | **PASS（lane 记录）** | `rec-20261102T145109-ed6786`；`evidence/i2-i18n/12-probe-run.log` |
| AC-4.3 | rev3：面板标题/按钮/对话框/错误/搜索占位符/诊断文本中文；故意不译须有一行 decision+reason；Qt 标准串经 `qtbase_zh_CN` | **PASS（lane 记录）** | `rec-20261102T172944-896e89`（T41 G7 复验 PASS）；`evidence/i18n-l2/50-i18n-l2-report.md`、`03-b-reconciliation.md` |
| AC-4.4 | English 下全英文；后端原文诊断保持英文正确 | **PASS（lane 记录）** | `evidence/i2-i18n/REPORT.md`；`rec-20261102T145109-ed6786` |
| AC-4.5 | 466 对账报告：条数 = 466，逐条 decision | **PASS** | 本轮复核：`07-triage-466.json` rows = **466**；`11-reconciliation-466.csv` 数据行 = **466**（`logs/ac-9.4-i18n-untouched.txt`）；报告 `evidence/i18n/11-reconciliation-466.md` |
| AC-4.6 | 215 条 UI 候选按可核对块落盘 | **PASS（lane 记录）** | `evidence/i18n/12-ui-blocks.md`、`09-ui-translations.json`、`13-coverage.md` |
| AC-4.7 | 断言覆盖点不减少（rev3：617 call-site 下限） | **PASS** | `evidence/i18n-l2/logs/27-ac47-callsites.log`：`TOTAL=617`（九个 selftest 文件逐项相加） |
| AC-4.8 | .ts/.qm 随包，离线切换可用 | **PASS** | G7 附加复核 `lrelease` **573/573 finished**；`src/app/CMakeLists.txt` `qt_add_lrelease` + `install`；`logs/ac-9-lrelease-recheck.log` |
| AC-4.9 | 只以 `evidence/i18n/` 三份产出为语料，不自造 | **PASS（lane 记录）** | `evidence/i18n/06-extractor-diff.*`、`07-triage-466.*` 被复用（mtime 未变，见 AC-9.4） |
| AC-4.10 | 扩面对账：每条恰一行 + 头部“发现条数 = 行数” | **PASS（lane 记录）** | `evidence/i18n-l2/03-b-reconciliation.md` §0：**发现条数 = 行数 = 319**；CSV 数据行 **319**（我复核 `wc -l` = 320 含表头） |

### AC-5 filelist `$` 语义（A-17）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-5.1 | `$` 字面语义；fixture files=3 | **PASS（lane 记录）** | `rec-20261102T111732-5bc74a`（A-17 盲审 PASS）；`evidence/config/`（fixture + `print-config-*.txt`） |
| AC-5.2 | 不存在路径产生诊断；depth 限制命名 | **PASS（lane 记录）** | 同上；`evidence/config/keys-rename.txt` 等 |
| AC-5.3 | `docs/USER_GUIDE.md` 写清变量/转义/优先级 | **PASS** | G6 docs 门禁 exit 0；`docs/USER_GUIDE.md`（基线即脏，本轮 T21 仅 `tr()` 包装） |

### AC-6 上轮非终态行

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-6.1 | 上轮 9 条非终态 review 行全部终态 | **PASS** | 板面复核（`agent_teams_plan status`）：T30/T37/T39/T41 等全部 `completed`；对应 verdict 记录见 §3.1（T39 先 FAIL `rec-20261102T164538-1e1608` → T40 修复 → T41 PASS `rec-20261102T172944-896e89`） |

### AC-7 交付质量

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-7.1 | 产品级结论有真实 shell/probe 证据 | **PASS** | 各 lane 探针路径（§3 AC-2/3/4 行）；本矩阵不把 `--selfcheck` 当唯一证据 |
| AC-7.2 | 结束时复跑 build-qt + ctest，树无半编译 | **PASS** | G1 + G2（本轮最终树上重跑，exit 0，38/38） |
| AC-7.3 | T22 产出 AC 覆盖矩阵，每条 AC 一行 | **PASS** | 本文件 |
| AC-7.4 | 交付说明区分本轮改动面与上轮遗留 | **PASS** | §5 |

### AC-8 诊断 lane（rev2）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-8.1 | 真·无 `settings.toml` 首启时 Format 状态与 TOOLCHAIN 一致，无假理由 | **PASS（lane 记录）** | `rec-20261102T155333-2d3e39`（T30 盲审 PASS）；`evidence/g6-diag/`（`00-red-before-fix*`、`30-green-after-fix*`） |

### AC-9 i18n L2 扩面（rev3，G7）

| AC | 判据（摘要） | 状态 | 证据 |
|---|---|---|---|
| AC-9.1 | 面板标题/按钮/搜索占位符/诊断文本中文，每类截图或 probe | **PASS** | `rec-20261102T172944-896e89`；`evidence/i18n-l2/logs/23-t40-probe.log`：**43 checks, 0 failure(s)**；截图 `evidence/i18n-l2/shots/01-shell-zh_CN.png`、`02-qt-standard-buttons-zh_CN.png` |
| AC-9.2 | `evidence/i18n-l2/` 报告头部“发现条数 = 行数”，每条 decision+reason | **PASS** | `03-b-reconciliation.md` §0（**319 = 319**；205·73·23·18；待定 0）；CSV 数据行 319（本轮复核）；枚举命令 `--any-literal` 见报告 §1 |
| AC-9.3 | `qtbase_zh_CN` 随包且可加载 | **PASS** | 本轮复核 `translations/qtbase_zh_CN.qm` = **147,222 B**，sha256 `0481a9b92a6f0578400e960a7d5ee70217a6a450eaf7f98b7d32b8dbd0d58616`；`src/app/i18n.cpp` 装载两个 `.qm`（先 qtbase 后 vedit）；probe `i18n.qtbase-loaded`；CMake `install` + AppDir 装配（`04-qtbase-vendor.md` §2–3） |
| AC-9.4 | 617 call-site ≥617；`evidence/i18n/**` 未被改写 | **PASS** | `27-ac47-callsites.log` `TOTAL=617`；`evidence/i18n/` 全部 mtime ≤ 2026-11-02 23:09（早于 G7 开工 ~23:59），466 条数不变（`logs/ac-9.4-i18n-untouched.txt`） |

---

### 3.1 AC-0.7 — work → 不同 agent verdict 记录映射（本 cell 窗口内）

| work | 写者（board） | 不同 agent 的 verdict 记录 | 结果 |
|---|---|---|---|
| 开工基线 T3 | je-baseline | `rec-20261102T072221-516a5b` / `rec-20261102T104456-7af167` | PASS |
| 复现台 T4 | je-baseline | `rec-20261102T105926-039051` | PASS |
| T46 walker（上轮遗留复审） | se-i18n | `rec-20261102T105951-afc97d` | PASS |
| T37 复现台 T9 | dw-rust | `rec-20261102T113117-d1a76d` | PASS |
| T11 解锁 T5 | dw-rust | `rec-20261102T111031-94587c` | PASS |
| A-17 T6 | se-config | `rec-20261102T111732-5bc74a` | PASS |
| T20 主壳 T13 | se-shell | `rec-20261102T120036-ab3a72`（T14 复审） | PASS |
| T39 复现台 T15 | se-i18n | `rec-20261102T121308-40d72a` | PASS |
| T15 编辑器 T17 | se-editor | `rec-20261102T124952-dc5399` | PASS |
| T16 进阶 T19 | se-editor | `rec-20261102T140727-16d8a4`（T20 复审） | PASS |
| T18 i18n T21 | se-i18n | `rec-20261102T145109-ed6786` | PASS |
| T21 对账 T23 | se-i18n | `rec-20261102T152121-4e59e9`（T24 复审） | PASS |
| AC-8.1 T29 | se-diag | `rec-20261102T155333-2d3e39`（T30 复审） | PASS |
| fmt 修复 T34 | se-editor | `rec-20261102T154759-420a35`（T35 复审） | PASS |
| G7 i18n L2 T38 | se-i18n → se-shell → dw-rust（handoff 接力） | `rec-20261102T164538-1e1608`（T39 FAIL）→ T40 修复 → `rec-20261102T172944-896e89`（T41 PASS） | PASS（经一次 FAIL/REPAIR） |

> 如实说明：本 cell 的 review 行在 `.mpd/verify/records` 中记录的 `taskId` 用的是**被审 work 任务**的编号（如 G7 work = T38，记录落在 T39/T41 复审），与板面 review 行编号不完全同名；上表按“work 行 → 复审记录”映射，全部 review 均有不同 agent 的 PASS。

---

## 4. 已知问题（如实登记，不修）

> 以下 4 条来自 G7 交付与 captain 2026-11-03 书面确认；本任务**不修改**任何其他 lane 的文件。

1. **G7 报告 §2 仍是 T38 旧计数**（304 / 220·27·23·34），与 `03-b-reconciliation.md` §0 的 **319 / 205·73·23·18** 不一致。**以 `03-b-reconciliation.md` §0 与 CSV 的 319 行为准**（本轮已独立复核 CSV 数据行 = 319）。
2. **G7 报告 §1.1 断言表少报**：探针共 43 项，表内列出 40 项（漏 3 个非字符串/审计检查）；文中「13 条 T40 行」实际为 10 条。方向是**少报而非虚报**，仍登记。
3. **`verify_claims.py` 的 fragment 分支有子串误判路径**（复审已合成复现）；当前 **0 行**使用该分支，属于潜在风险，建议下轮收紧为 `wrap == own-call`。
4. **`describeDiagnostic()` 的 F8 状态栏模板仍英文**（G7 报告 §7.7 已披露），属 T40 范围外，未修。

### 4.1 环境限制（非产品缺陷，证据已留档）

- 主机 `ctest --test-dir target/qt6-build` 因 CTest 记录容器绝对路径（`/vedit-build/...`）无法运行 → G2b。
- 主机直接运行产品二进制缺 `libtree-sitter.so.0.25` 等运行时库 → G3 文件头。
- 主机 rustup 的 `1.99.0` 工具链目录缺 `cargo`，且主机无 GTK3 dev 文件 → clippy/fmt 改用同版本 `stable`（rustc 1.99.0 b940084d7，与 pin 同一 commit）/ 固定 Rust 镜像 → G4/G5 文件头。

### 4.2 门禁脚本自写证据目录（写域说明）

`bash scripts/build-qt.sh` 自身按设计把日志写到 `evidence/m0-build/build-logs/`、`evidence/ffi-abi/logs/`（脚本内 `tee`），这不是本任务手写的交付物；本任务**手写**的路径仅 `evidence/final-acceptance/`、`docs/ACCEPTANCE-MATRIX.md`。门禁脚本写入的路径与开工基线同属既有脏路径集合（基线 157 条中已含 `build/evidence/…` 同类文件），特此披露以免被误判越界。

---

## 5. 交付说明

### 5.1 本轮改动面 vs 上轮遗留（AC-7.4；依据 `evidence/baseline/`）

- **上轮遗留**：开工基线 `git status --porcelain=v1`（**无 `-uall`**）= **157** 条（93 修改 + 64 未跟踪；evidence/ 占 47 条），快照 sha256 `19f6f207…`（`evidence/baseline/SUMMARY.md`）。
- **本窗口**（2026-11-03T01:31）：同口径 **241** 条；文件粒度 `-uall` **843** 条（两套口径**并列陈述、不相减**）。
- 顶层路径集合比对（`evidence/final-acceptance/03-baseline-accounting.txt`）：基线 157 条**全部仍在**（removed = 0）；**新增 84 条**顶层/文件路径；当前 241 条中有 **151 条** mtime 落在本轮时间窗（≥ 2026-11-02 18:06）。
- 新增 84 条构成：**产品/构建 62 条**（`src/` 编辑器/搜索/面板/诊断/i18n 等 + `CMakeLists.txt` + `translations/` + `THIRD_PARTY_NOTICES.md`）、**evidence 22 条**（`evidence/i18n/`、`evidence/i18n-l2/`、`evidence/final-acceptance/` 等本轮产物）。
- **边界盲区（必须与结论同写）**：基线快照**未带 `-uall`**，未跟踪目录被折叠为一行，**目录内部新增文件在基线中不可见**。因此本交付对“与基线对比”的结论只断言**顶层路径集合**；未跟踪目录内部新增不做“无新增”断言，文件粒度以本窗口另采的 `-uall` 快照（`02-git-status-porcelain-v1-uall.txt`）为准。
- **越界越判**：不用脏路径条数做差，改用 **mtime 时间窗 + 路径集合**（captain 2026-11-03 裁定）。G7 新增的 `translations/*.qm` 与 `THIRD_PARTY_NOTICES.md` 属 captain 书面授予的 scope 例外，**不记越界**。

### 5.2 零 git 内容（AC dispatch ⑥）

- 本轮**未**执行任何写库动作：无 `git add`、无 commit、无分支、无 PR、无 tag；HEAD 仍为 `4e2d0a57`（本 cell 开工值与验收值相同）。
- 全部改动为**工作树未提交状态**，由 cell 统一处理。

### 5.3 本 cell 还没做什么（如实列出）

1. **`docs/handover/` 未更新**：按 captain 2026-11-03 裁定，交接文档是 T26 之后的**独立任务**；`docs/handover/CELL-20261102-STATUS.md` 现仍写着「T30 review 仍 pending」，已过时（T30 已于 `rec-20261102T155333-2d3e39` PASS），留待交接任务更新。
2. **未重跑各 lane 的探针**：AC-1/2/3/5/8 与 AC-9.1 的产品级探针由本 cell 内已验证的 lane 证据支撑，本任务只重跑六道门禁与三项独立复核（ctest 38/38、selfcheck 245、lrelease 573/573）。
3. **未运行 `bun run verify:gates` 全量**（`typecheck` 不在本 cell DoD 清单内；`check_ffi_header_drift` 为带原因排除项）。
4. **未做 AppDir/AppImage 重新打包验证**（仅验证装配规则含两个 `.qm`；打包属发布 lane）。
5. **已知问题 §4 的 4 条未修**（按 captain 指令：不修、只登记）。
6. **未验证 UI 视觉观感**（截图存在但未做视觉评审；本任务判据是门禁与 AC 覆盖）。

---

## 6. 复现命令（全部可在仓库根执行）

```bash
# ① 构建
bash scripts/build-qt.sh --no-test
# ② ctest（主判据，容器内；主机的字面 ctest 会因容器绝对路径失败，见日志）
bash scripts/build-qt.sh
tail -3 evidence/m0-build/build-logs/33-qt6-ctest.log   # 100% tests passed, 0 failed out of 38
# ③ selfcheck（容器内；主机缺 libtree-sitter）
docker run --rm -e HOME=/tmp -v "$PWD":/vedit-build -w /vedit-build \
  -e QT_QPA_PLATFORM=offscreen vedit-qt6-build:local \
  target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc
# ④ clippy（固定 Rust 镜像）/ fmt（rustc 1.99.0）/ docs
docker run --rm -e HOME=/vedit-build/target/home -e CARGO_HOME=/vedit-build/target/cargo-home \
  -e RUSTUP_HOME=/opt/rust -e RUSTUP_TOOLCHAIN=1.99.0 -e NO_AT_BRIDGE=1 \
  -v "$PWD":/vedit-build -w /vedit-build vedit-build:local \
  cargo clippy --workspace --all-targets -- -D warnings
RUSTUP_TOOLCHAIN=stable cargo fmt --all -- --check
bash build/gates/docs.sh
# 附加复核
docker run --rm -e HOME=/tmp -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm   # 573/573 finished
```
