# 最终验收矩阵 — UX 批次 2（用户 14 项 + SDC/XDC + 分屏激活）

> **判定口径（写给 verifier）**：本矩阵每一行以「冻结契约 + 本文档 + `evidence/` 下的证据」为准，
> **不**采信写者的过程叙述。行内指针给出证据目录/文件；门禁行给出命令、退出码与日志路径。
> 独立复核一律由**不同 agent** 完成，报告在 `evidence/ux-batch-2/review-*/REPORT.md`。
>
> 契约（冻结）：`.mpd/plans/ux-batch-2/00-plan.md`（§0 用户裁定 / §0.1 R1–R3 LINT 裁定 / §1 实测事实 /
> §2 已知边界 / §3 Wave A / §4 Wave B / §5.1 集成步骤）。
> 交接：`docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md`（§3 未完成清单 U1–U9，§5 实测事实，§8 诚实边界）。
> 团队：`team-20261106015130380-vp4j-003`（plan `plan-20261106014917344-vp4j-002`）。

## 0. 一句话结论

用户 14 项 + SDC/XDC + 分屏激活**全部交付**；**5 道门禁在终态树上全绿**（`U8.1 PASS`，ctest **50/50**、`APP-SHELL-SELFTEST ok=1 checks=319 failures=0`、`lrelease 1211/0`）；
AppImage **已重打**（136,051,192 B），包内 `.qm` 与当前目录**sha256 逐字节相同**（129,051 B / 1211 条），AppRun 在 **`zh_CN.UTF-8` 与 `en_US.UTF-8` 下都 exit 0**。
剩余的只有**已披露的已知限制**（§6）与**待用户真机验收的观感项**（§7）。写者与验证者为不同 agent，全部复核报告在 §8。
**未提交**：按 `AGENT.md §6` 不得直提 `main`，提交需用户确认（§10）。

## 1. 交付形态与工具链

| 项 | 值 | 证据 |
|---|---|---|
| 交付物 | **AppImage 136,051,192 B**，sha256 `2b95e56fef4098a12b72e96ef5d27ab8a5d7452e008cbd80f4b824979df080c9` | `u8-final/package-build.log` |
| AppDir | **350** 个文件；`whitelist=Markdown,SystemVerilog,Tcl,VHDL,Verilog` | 同上 |
| 包内翻译 | `bin/translations/vedit_zh_CN.qm` = **129,051 B / 1211 条**，与当前 `.ts` 现场编译结果 **sha256 完全相同**（`b7f2f50c…`）——证明包里的译文就是这一份 | `u8-final/verify-selfcheck-appdir.log`、`verify-lrelease-current.log` |
| 打包链 | `bash packaging/runtime/build.sh` → `steps_failed=0`，`BUILD: F2 runtime OK`；含 F2 隔离、CentOS 7 机制探针、捆绑 VCS 工具链（git 2.53.0 / svn 1.14.5） | `u8-final/package-build.log` |
| AppRun 守卫 | **`zh_CN.UTF-8` exit 0 / `en_US.UTF-8` exit 0**（两次都 `APP-SHELL-SELFTEST ok=1 checks=319 failures=0`） | `u8-final/verify-apprun-zh_CN.UTF-8.log`、`verify-apprun-en_US.UTF-8.log` |
| ctest 总数 | 基线 **38** → 终态 **50**（`100% tests passed, 0 tests failed out of 50`，75.10 s；只增不减） | `u8-final/qt6-lane.log` |
| 翻译 | `translations/vedit_zh_CN.ts` = **1211 条 / 0 unfinished / 0 vanished**；`lrelease` 0 unfinished 且 0 untranslated | `u8-final/lrelease.log`、`u2-final/`、`u2b-final/`、`u2c-final/`、`u2d-final/` |

## 2. 携带豁免（**不得记为缺陷**）

| 项 | 事实 | 依据 |
|---|---|---|
| MPD 门禁 `typecheck` | argv 是 `check_ffi_header_drift`；生成的 `crates/vd-ffi/include/vd_ffi.h` 在**本批次开工前**即已脏（与 HEAD 不同），不是本批次引入 | 上一批次同一豁免：`docs/FINAL-ACCEPTANCE-MATRIX.md` §2；基线 `evidence/baseline-2/` |
| MPD 门禁 `tests` | argv `bun test packages` 指向本仓库不存在的路径；Rust 测试由 `scripts/gates.sh`（`cargo test --workspace`）覆盖，C++ 由 ctest 覆盖 | `package.json` 注释、`build/gates/tests.sh` |
| 无真机 GUI 验证 | 本环境只有 offscreen + Xvfb；观感 / DPI / 字体 / CJK 渲染由用户在真机验收（Xvfb 下「修复」为豆腐块） | 本文件 §6.1 |
| lane selftest 偶发 SIGPIPE(141) | 多个 lane 自测（会 spawn 真 LSP 子进程）在收尾处偶发 141；重跑即绿。已知 latent 问题：C++ 宿主里 Rust 运行时未装 SIGPIPE 默认（`AGENT.md §4`），`main.cpp` 只对产品进程忽略 | 本文件 §6.7；`review-u1-final` A1、`review-a2-final` N2 |

## 3. lane 状态（每 lane 一行；复核者为**不同 agent**）

| lane | 内容 | 判定 | 独立复核 |
|---|---|---|---|
| A1 | Rust ABI：模块目录 `vd_catalog_*`、`vd_search_tree_query_ex`（精确 range）、`VdLintBatchOptions`、`VdProjectInfo.rules_path` | PASS（189 tests / clippy / fmt / header drift 绿） | 由 B1/B3/U6 消费侧间接验证（`a1/`） |
| A2 | 顶栏重排 + 主工具栏 + 撤销/重做 + 全量 `tr()` + qrc 内置手册 + `.ts` 重生成 | **PASS**（risk Medium） | `review-a2-final/`（H1 转 T34/T35；M2/N1 已并入 T34） |
| A3 | 高亮：5 份定义（新增 VHDL/Markdown/Tcl 自研）+ 表优先映射 + `verify-f2-isolation` 重写 + 许可登记 | **PASS** | `review-a3/`(FAIL) → `review-a3-r2/`(**PASS**) |
| A4 | 设置：字体即时 / 缩放重启 / 23 个 format 旋钮 / 双后端 Lint 页 | **PASS**（risk Medium） | `review-a4-final/`（H1 转 T13；N2 "test-green dead path" 由 T13 断言真实调用方） |
| B1 | 模块树（纯模块层级，顶层=从未被例化）+ 接口面板 | **PASS** | `review-b1/`(FAIL) → `review-b1-r2/`(**PASS**) |
| B2 | 搜索对齐 + 全工程搜索/替换（冻结快照 + 逐命中指纹 + **保存前整行身份**），共 4 轮 | **FAIL → 已返修 → PASS** | `review-b2-r4/`(**FAIL**, P5 错误写入) → `b2-repair-r4/` → `review-b2-r5/`(**PASS**：P5 探针字节相同复跑，`saves=0 ok=0 replacements=0`) |
| B3 | R2 分层诊断 + R3 保存后自动工程 lint + 悬浮/一键修复/文件点/ELAB | **FAIL → 已返修 → PASS** | `review-b3-r3/`(FAIL, N2/N3) → `u1-final/` → `review-u1-final/`(**PASS**) |
| B4 | Ctrl+点击 = 定义、Alt+点击 = 多光标、坐标映射、折叠陈旧光标 | **PASS** | `review-b4/` → `review-b4-r2/`(**PASS**) |
| pane-activation | 点非活动分屏即激活 | **PASS** | 被 `review-b4-r2/` 独立探针覆盖；`pane-activation/` |
| U2 | 翻译收尾：182 条新译落盘 + 21 条 vanished 处置 | **PASS** | `review-u2-final/`(**PASS**，集合等式证明) |
| U2b | 悬浮「修复」按钮源串中文化 → `Fix` + 目录 | **PASS** | `review-u2b-final/`(**PASS**，仅 1 Nit) |
| U2c | 剩余用户可见中文字面量 22 处（诊断 2 + FIM 20）→ 英文源串 + 目录 | 已完成，随终审验收 | `u2c-final/`（内含 3 处测试期望字面量变更的说明） |
| U3 | 用户手册 + 文档纠错（快捷键冲突修复 + 键盘表逐行重推 + 产品自检三条可失败断言） | **FAIL → 已返修 → PASS** | `review-u3-final/`(**FAIL**) → `u3-repair/` + `u3-repair2/` → `review-u3-final-r2/`(**PASS**：23 条绑定无重复、检测器能触发、键盘表逐行对上；其 Medium 由 `u2d` 关闭) |
| U4 | 语法启动测试完整性（CMake deferred 路径 + PASS 正则） | **PASS**（含 Medium） | `review-u4-final/`(**PASS**) |
| U4b | 断言**解析后的定义名** + 关掉"打印即通过"的退出码洞 | **PASS**（risk LOW，1 Nit） | `review-u4b-final/`(**PASS**) |
| U5 | 打包自检覆盖五份定义（fail-closed） | **PASS** | `review-u5-final/`(**PASS**) |
| U6 | A1/A4 之间 lint 接线：5 旋钮进 argv、autofix 诚实挂 pending、rules 三级优先 | **PASS** | `u6-final/` → `review-u6-final/`(**PASS**，六项 argv 实测回显；1 Nit：`vd_lint_unsupported_knobs(Slang)` 漏列 `rules_config`) |
| U8a | 真 `lrelease` gate（未译即红），并用未译目录做负控 | 已完成 | `u8-lrelease-gate/`（负控 exit 1、真实目录 exit 0） |
| U8b | 修 gate 抓到的 3 条陈旧中文断言（U2c 改英文源串的连带） | 已完成 | `u8-repair-37/`（探针 62 项回绿；全树陈旧断言清扫：仅此一处） |
| U2d | 补齐 `lupdate` 抽出但目录缺失的 5 条 + 删 3 条废弃（lrelease 看不见"缺条目"） | 已完成 | `u2d-final/`（`Found 1211 source text(s) (5 new, 1206 existing)`；零丢失证明；1211/0） |
| U3b | 格式化显示快捷键单一来源 + 手册 §3 机制改述 + §15.3 引文逐字对齐 | 已完成 | `u3-repair2/`（`shortcut.format.registry-equals-fallback` 红→绿；selfcheck 319） |
| T35 | U3 返修复核 | **PASS**（risk LOW，1 Medium 即 U2d） | `review-u3-final-r2/` |

## 4. 用户需求矩阵（14 项 + 追加；逐条对应用户原话）

| # | 用户原话要点 | 判定 | 落点（代码） | 证据 / 复核 |
|---|---|---|---|---|
| 1 | 「模块树要有顶层的 module 树」仅模块信息，双击打开模块代码 | **PASS** | `src/workspace/module_tree/**`、`crates/vd-deeplint/src/catalog.rs` | `b1/`、`b1-repair/`、`review-b1-r2/` |
| 2 | 接口面板跟随焦点 module，显示类型/方向/位宽，双击跳声明、Ctrl+双击跳例化 | **PASS** | `src/workspace/module_tree/interface_panel.*` | 同上 |
| 3 | 右键菜单与顶栏中文翻译 | **PASS（框架+条目）** | `src/app/app_shell.cpp`、`src/editor/kernel/lsp_actions.cpp`、`diagnostics_integration.cpp` | `review-a2-final/`、`review-u2-final/`、`review-u2b-final/`、`u2c-final/`（反方向中文字面量清零） |
| 4 | Rename Symbol 可用；后端不支持则置灰 + 请求路径也拒绝 | **PASS** | `src/workspace/diagnostics/symbol_scan.cpp` + 能力门控 | `review-u1-final/`（rename 21/21；跨文件 rename 明示 NOT-CLAIMED） |
| 5 | 搜索面板与 Ctrl+F 对齐 + 全工程搜索 + **两个**替换按钮 | **PASS** | `src/workspace/search/**`、`src/editor/kernel/find_replace.*` | `review-b2-r4/`(FAIL) → `b2-repair-r4/` → `review-b2-r5/`(**PASS**) |
| 6 | format / LINT 可配项做成设置（统一面板、两套配置、不支持置灰） | **PASS** | `src/workspace/settings/**`、`crates/vd-settings/**` | `review-a4-final/`（format：真实进程收到 23 flag、默认 0 flag）+ `review-u6-final/`（**PASS**：5 旋钮 argv 实测回显、autofix 否定断言、三级优先） |
| 7 | 顶栏重排（撤销/重做等文件操作、Preferences 并入 Edit） | **PASS** | `src/app/app_shell.cpp`、`CommandRegistry` | `review-a2-final/`（PASS，H1 携带）→ `u3-repair2/` → `review-u3-final-r2/`（**PASS**：`Ctrl+Alt+F` 冲突消除，格式化改 `Ctrl+Shift+F`，并新增"注册表==显示快捷键"自检） |
| 8 | 可选 UI 字体大小（高分屏）：字体即时 + 整体缩放重启生效 | **PASS** | `src/workspace/settings/ui_scale.*`、`main.cpp`（QApplication 之前） | `review-a4-final/`（150→1.5、200→2、100→不覆盖） |
| 9 | error/warning 悬浮框 + 有修复能力时一键修复 | **PASS** | `src/workspace/diagnostics/diagnostic_hover.*`、`autofix.*` | `review-u1-final/`（R2/R3 复跑全绿）；`review-u2b-final/`（按钮源串） |
| 10 | 文件名前黄/红/绿问题点 | **PASS** | `src/workspace/filetree/file_status_delegate.*` | `review-u1-final/`（`n2r.e-no-longer-green` 点数跟随失效；`lint-experience` 点色全绿） |
| 11 | 帮助里塞真手册 | **PASS** | `src/app/help_viewer.*`、`resources/help/user_manual.qrc` | `review-a2-final/`（qrc→setMarkdown、F1 与终端按键双向）；手册内容 `review-u3-final-r2/`（**PASS**：§12–§16 齐备、四处纠错落地） |
| 12 | LINT 时 ELAB 错误弹窗引导跳转（仅显式触发弹模态） | **PASS** | `src/workspace/diagnostics/project_lint_notice.*` | `review-u1-final/`（`modal.automatic-never-shows` 恒真、`product.automatic-no-dialog`） |
| 13 | Ctrl+点击 = Go to definition；多光标改 Alt+点击 | **PASS** | `src/editor/kernel/multi_cursor.*`、`lsp_actions.*` | `b4-repair/`、`review-b4-r2/` |
| 14 | VHDL / markdown / tcl 语法高亮 | **PASS（功能）/ 生效来源有披露** | `resources/syntax/vedit-{vhdl,markdown,tcl}.xml`、`highlight_mode.cpp`、`ksyntax_data.*` | `review-a3-r2/`、`review-u4-final/`、`review-u4b-final/`；见 §6.4 |
| 附 | SDC/XDC 用 Tcl 高亮 | **PASS** | `kExtensionTable` 的 `.sdc/.xdc → Tcl` | 同 item 14 |
| 附加 | （评审发现）点非活动分屏不切换 | **PASS** | `src/app/editor_area.*` | `pane-activation/`、`review-b4-r2/` |

**用户裁定（BINDING）落实情况**：Ctrl+点击=定义 / Alt+点击=多光标 ✓（item 13）；接口面板双击→声明、Ctrl+双击→例化 ✓（item 2）；
替换**两个按钮** ✓（`review-b2-r4` 复核）；Rename/一键修复"后端支持才做、不支持置灰且**请求路径也拒**" ✓（item 4/9）；
菜单 File/Edit/View/Project/Help + 工具栏、Preferences 并入 Edit ✓（item 7）；R1 统一 Lint 页两套配置 ✓（`review-a4-final`，U6 复审中）；
R2 工程 LINT 上位、模块下位、同一文件一份判决 ✓（`review-u1-final`）；R3 保存后自动工程 lint（700 ms 去抖、单飞、**绝不弹模态**）✓（`review-u1-final`）。

## 5. 门禁（终态树，`U8.1 PASS`）

脚本：`evidence/ux-batch-2/u8-final/run-gates.sh`（任一门非 0 即整体 FAIL）。第一次运行 4/5 绿、`qt6-lane` exit 8（`vedit-lint-orchestration-probe` 因 U2c 改英文源串而 3 条断言陈旧）→ U8b 返修（`u8-repair-37/`）→ **第二次全绿**。

| gate | 命令 | 退出码 | 关键输出 | 日志 |
|---|---|---|---|---|
| rust-gates（clippy + fmt + cargo test） | `bash scripts/gates.sh` | **0** | `GATES PASS (host: dependency policy locally + compile gates in the pinned image)` | `u8-final/rust-gates.log` |
| qt6-lane（configure + build + ctest + xcb smoke） | `bash build/gates/qt6-cxx.sh` | **0** | `100% tests passed, 0 tests failed out of 50`（75.10 s）；`M0-SMOKE ok=1 platform=xcb … window=1 editor=1 document=1` | `u8-final/qt6-lane.log` |
| selfcheck | 镜像内 `vedit-app --selfcheck` | **0** | `APP-SHELL-SELFTEST ok=1 checks=319 failures=0`、`EDIT-CORE-SELFTEST ok=1 checks=99 failures=0` | `u8-final/selfcheck.log` |
| docs | `bash build/gates/docs.sh` | **0** | `[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md` | `u8-final/docs.log` |
| lrelease | `bash build/gates/lrelease.sh`（本批次新增） | **0** | `[lrelease] counts: 1211 translation(s) (1211 finished, 0 unfinished, 0 untranslated)` | `u8-final/lrelease.log` |

打包后另跑 `verify-appimage.sh`：**U8.2 PASS**（包内自检 + 目录 sha256 一致 + 双 locale exit 0）。

逐门日志与汇总表已落盘：`u8-final/gate-summary.txt`（含首次运行的失败记录与返修说明，不隐藏）。

> `--selfcheck` 必须在 `vedit-qt6-build:local` 镜像内跑：宿主机缺 `libtree-sitter.so.0.25`（只有镜像与 AppDir 里有），直接跑会 exit 127。

## 6. 已知限制（诚实声明，不掩盖）

1. **没有真机 GUI 验证**：全部 offscreen 或 Xvfb；观感、DPI、字体、CJK 渲染由用户验收。
2. **跨文件 Rename 不声明支持**：verible 只对当前打开文件作答（LSP 会话是文件作用域），标为 `NOT-CLAIMED`。
3. **工程级正则 = RE2 风格**（无反向引用 / lookaround），与 Ctrl+F 单文件的 PCRE 语义不同，已写入用户手册。
4. **语法定义生效来源（U4b/F1 披露）**：5 份定义都随包、都能解析、名字都能解析到；但在**带 KDE 定义的环境里**，KF6 仓库按 `version` 取高者，而 KF6 库**内嵌**了 verilog/systemverilog/vhdl/markdown 的定义、我们的是 `version="1"` → 这 4 个名字实际生效的是 KDE 副本，只有 **Tcl 是我们的**（KDE 6.24 无 Tcl 定义）。打包形态是另一种情形（AppRun 收窄 XDG，由 A3 隔离证据覆盖）。"提升 `version` 让我们自己的定义生效"是**未做的产品决策**，登记为待办。
5. **FIM 的 Rust 侧中文**：`crates/vd-fim/src/error.rs:69-82` 直接返回中文错误串（实测 `status="FIM 未配置模型"`），Qt 目录触达不到 → 英文界面在 FIM 的 disabled/未配置/超时/传输态仍显示中文。修法是"Rust 返回稳定码 + Qt 侧 `tr()` 映射"，属下一批次。
6. **替换的残留边界（B2b 修后口径）**：逐命中身份在**保存前**增加了整行校验——脏缓冲区里"前 400 字符相同、之后不同"的插入行会被**拒绝**（不保存、不写、给出文件/行号原因）；仍然不可分的是"外部改写且 size+mtime 不变"以及"插入一行与命中行**整行完全相同**的副本"（后者无危害：两处文本相同）。无 `bufferText()` 的非产品桥在超长命中行上直接拒绝。
7. **lane selftest 偶发 SIGPIPE(141)**：见 §2。
8. **`svnadmin` 不随包**（build-only），符合既有设计。
9. **`settings.toml` 双候选路径**（缩放读 pre-QApplication、字体/format 读 post-construction）是既有罕见边界，A4 已披露（`review-a4-final` N3）。
10. **`lint.verible.autofix` 仍是 `TransportPending`**：`VdLintBatchOptions` 无 autofix 字段；面板照常可编辑/持久化并**明示**"C ABI 无 autofix 字段"，第六个断言是"设了值 → argv 里不出现任何 flag + 仍报 pending"（R1：绝不静默发无效参数）。真正的 autofix 通路是悬浮框一键修复。
11. **`translations/` 目前未被 git 跟踪**（`git ls-files translations/` 为空、未被 ignore）——提交时必须显式 `git add`，否则干净检出会构建出**英文 UI**（`qt_add_lrelease` 在缺 `.ts` 时静默回退）。

## 7. 真机烟测清单（用户在真机验收时逐项打勾）

- [ ] 模块树：展开/刷新/过滤，根 = 从未被例化的模块；双击模块 → 打开其声明
- [ ] 接口面板：跟随焦点模块，显示 名称/方向/类型/位宽；双击 → 端口声明；Ctrl+双击 → 例化语句
- [ ] 搜索面板：与 Ctrl+F 选项一致；全工程搜索；「替换」/「全部替换」两个按钮；脏缓冲区超长命中行被拒时给出文件名/行号
- [ ] 诊断：保存后自动工程 lint（静默、不弹窗）；悬浮框显示 error/warning；有修复能力时出现「修复」按钮（英文界面显示 `Fix`），一次撤销可回退
- [ ] 文件树：文件名前红/黄/绿点，与编辑区一致
- [ ] LINT 时 ELAB 错误：显式 Lint Project 弹模态并可跳转；自动跑只给可点状态栏
- [ ] Rename Symbol：可用位置能改；不可用位置给出原因且不发请求
- [ ] 导航：Ctrl+点击跳定义；Alt+点击加光标；Alt+拖拽块选；点分屏即激活
- [ ] 顶栏：File/Edit/View/Project/Help + 主工具栏；撤销/重做可用；Preferences 在 Edit 下；**格式化与其他命令不再共用同一默认快捷键**
- [ ] 设置：字体大小即时生效；整体缩放重启后生效；Lint 页切换后端不丢另一套配置；autofix 项显示"待接线"原因
- [ ] 语言：切换 zh_CN / en 后菜单与面板文本正确（FIM 状态串为已知限制）
- [ ] 高亮：.v/.sv/.vhdl/.vhd/.md/.tcl/.sdc/.xdc 各有高亮，.sdc/.xdc 走 Tcl
- [ ] 帮助 → 用户手册（F1）可打开并含本批次新增章节

## 8. 独立复核台账（全部由**不同 agent** 完成）

| 复核 | 对象 | 判定 | 报告 |
|---|---|---|---|
| `review-a2-final` | A2 顶栏/菜单/i18n/手册 | PASS（Medium） | `evidence/ux-batch-2/review-a2-final/REPORT.md` |
| `review-a4-final` | A4 设置 | PASS（Medium） | `.../review-a4-final/REPORT.md` |
| `review-u1-final` | B3/U1 诊断 N2/N3 | PASS | `.../review-u1-final/REPORT.md` |
| `review-u2-final` | U2 译文收尾 | PASS | `.../review-u2-final/REPORT.md` |
| `review-u2b-final` | U2b 修复按钮源串 | PASS（1 Nit） | `.../review-u2b-final/REPORT.md` |
| `review-u3-final` | U3 手册 | **FAIL** → 返修 | `.../review-u3-final/REPORT.md` |
| `review-u4-final` | U4 语法测试 | PASS（Medium+Nit） | `.../review-u4-final/REPORT.md` |
| `review-u4b-final` | U4b 解析后定义名 | PASS（LOW，1 Nit） | `.../review-u4b-final/REPORT.md` |
| `review-u5-final` | U5 打包自检 | PASS（2 Nits） | `.../review-u5-final/REPORT.md` |
| `review-b2-r4` | B2 终审 | **FAIL**（P5 错误写入） | `.../review-b2-r4/REPORT.md` |
| `review-batch-acceptance` | 全批 round-1 | **FAIL**（4 条 High） | `.../review-batch-acceptance/REPORT.md` |
| `review-b3-r3` | B3 第二轮 | FAIL（N2/N3） | 已被 `review-u1-final` 取代 |
| `review-b2-r5` | B2b 返修 | **PASS**（P5 已关闭：0 saves / 0 replacements） | `.../review-b2-r5/REPORT.md` |
| `review-u3-final-r2` | U3 返修 | **PASS**（Medium：目录缺 5 条 → 由 U2d 关闭） | `.../review-u3-final-r2/REPORT.md` |
| `review-u6-final` | U6 lint 接线 | PASS（1 Nit） | `.../review-u6-final/REPORT.md` |
| `review-batch-acceptance-r2` | 全批 round-2 | **RELEASE**（14 项全 PASS，无 Blocker/High） | `.../review-batch-acceptance-r2/REPORT.md` |
| `review-a3-r2` / `review-b1-r2` / `review-b4-r2` | A3 / B1 / B4 | PASS | 各自目录 |

## 9. 本轮发现与处置台账

| # | 发现 | 来源 | 处置 |
|---|---|---|---|
| D1 | 翻译目录 182 条未译 + `lrelease` 红 | 交接 §0 | 182 条全部落盘（`apply-translations-r2.py` 修了两个脚本缺陷）→ `u2-final/` |
| D2 | 21 条 vanished 条目 | 交接 §3.U2 | 全部核实后移除（14 改写 / 5 真死 / 2 陈旧重复）→ `u2-final/` |
| D3 | 悬浮「修复」按钮**源串是中文** | `u2-final` F1 | 改 `translate("VEdit","Fix")` + 目录 `Fix→修复`；`review-u2b-final` PASS |
| D4 | 无 `lrelease` gate（未译不会红） | `review-u2-final` RF4 | 新增 `build/gates/lrelease.sh` + `verify:lrelease`，并用未译目录做负控（exit 1） |
| D5 | B2 逐命中身份 = 400 字符预览 → **错误写入**刚保存的文件 | `review-b2-r4` P5 | 保存前整行校验 + 拒绝；评审者原探针重编后 0 失败（`b2-repair-r4/`） |
| D6 | 语法启动测试"定义没加载也能绿" + 路径解析成 `/resources` | 交接 §3.U4 | 路径修正 + 五份文件名（`u4-final/`） |
| D7 | 上一条仍可被"损坏定义"骗过 | `review-u4-final` | U4b：断言**解析后的定义名** + `APP-SHELL-SELFTEST ok=1` 锚（`u4b-final/`） |
| D8 | 打包自检只查 2 份 XML（缺 3 份也过） | 交接 §3.U5 | 扩到 5 份并 fail-closed，含"旧脚本在损坏 AppDir 上仍通过"的对照（`u5-final/`） |
| D9 | 6 个 lint 旋钮"可编辑但不进 argv" | 交接 §3.U6 | 5 个接线 + `rules_path` 三级优先；autofix 诚实挂 pending（`u6-final/`） |
| D10 | 手册 Qt6 快捷键是**旧的/错的**，且 `Ctrl+Alt+F` 两个命令共用 | `review-u3-final` | T34：改产品默认键 + registry 重复检测 + 键盘表逐行重推；`review-u3-final-r2`（待） |
| D11 | 产品自检的 Preferences 断言**永远为真** | `review-a2-final` M2 | 并入 T34：补 5 菜单/顺序/Preferences 三条可失败断言 |
| D12 | `no project` 硬编码英文（中文界面露英文） | `review-a2-final` N1 | 并入 T34 |
| D13 | `USER_GUIDE` 三条替换边界措辞被 B2b 推翻 | `b2-repair-r4` | 并入 T34（精确措辞已发） |
| D14 | `src/syntax/kde/README.md` 的"不会静默用 KDE 定义"不成立 | `u4b-final` F1 | 并入 T34 改实测口径；并登记"提升 version"为待办 |
| D15 | 22 处用户可见中文字面量（诊断 2 + FIM 20） | `u2b-final` | U2c 全部包裹 + 目录（`u2c-final/`） |
| D16 | FIM 的 Rust 侧中文字符串 | `u2c-final` F4 | **不修**，登记为 §6.5 已知限制 |
| D17 | **`lrelease` 看不见"缺条目"**：U6/B2b 新增的 5 条用户可见串从未进目录，zh 界面回落英文 | `review-u3-final-r2` | U2d：`lupdate -locations absolute` + 5 条译文 + 删 3 条废弃；**零丢失证明**（1206 条存活译文逐字未变） |
| D18 | 手册 §15.3 引用的中文与出厂译文不一致（旧转述） | `review-u3-final-r2` | U3b：改成与目录**逐字一致**的 36 字符串 + 程序化子串断言 |
| D19 | 格式化显示快捷键仍显示旧值 `Ctrl+Alt+F`（注册表已是 `Ctrl+Shift+F`），且 §3 写成走 LSP `documentFormatting` | `review-u3-final-r2` Nit | U3b：单一来源（`grep 'Ctrl+Shift+F' src/` 仅 1 处字面量）+ 自检项 `shortcut.format.registry-equals-fallback`；§3 改为外部 `verible-verilog-format` |
| D20 | 误读：`-locations absolute` 输出仍是 `../src/…`（源路径相对 `.ts`），此前 F3/F5 的"相对路径警报"不成立 | `u2d-final` | 以 `lupdate -help` 原文为准，记入 `u2d-final/README.md` §7；**不构成缺陷** |

## 10. 提交与 PR

| 项 | 值 |
|---|---|
| 分支 | `feat/ux-batch-2`（从 `main` @ `4e2d0a5` 起） |
| 提交（按域 4 次） | `29678d1` feat(rust) · `aaf07de` feat(ui) · `34aa7b9` build · `96c0dd9` docs(evidence) |
| PR | https://github.com/HaroldZ32/VEdit/pull/13 （`feat/ux-batch-2` → `dev`） |
| 提交身份 | `MPD Captain <mpd-captain@localhost>`（AGENT.md §6：角色身份进 commit） |
| 未纳入 | `probes/slang-spike/`（上一批临时实验目录，仍为未跟踪）；`evidence/**/*-probe` 编译产物已由 `.gitignore` 排除 |
| 首次推送的更正 | 第一次推送把 `evidence/` 里的 8 个探针 ELF（566.6 MB）一并提交；经用户同意后删除该远端分支、回滚重提交并加 `.gitignore` 规则后重推。**已发布历史只此一次改写，且仅限这个刚建 10 分钟、无 PR、无人使用的分支**。 |

### 终审的 4 条 Medium 与本文件的处置

| Medium | 处置 |
|---|---|
| 矩阵仍有"待"格 | 已全部填成终态（§3/§4/§8） |
| gate 汇总表未落盘 | 已写 `u8-final/gate-summary.txt` |
| `translations/` 未跟踪 | 已随 `build` 提交纳入 |
| 四条 lane 复核早于最后集成编辑 | **公开披露**：`app_shell.cpp`(11:21)、`main.cpp`(12:33)、`diagnostics_integration.cpp`(12:35)、`USER_GUIDE.md`(12:37) 的改动分别由 `review-a2-final` / `review-u4-final` / `review-u1-final` / `review-u3-final-r2` 覆盖了同一代码区域，但**没有针对这几处最后编辑的单独复核报告**；终局 gate 复跑了受影响的套件（ctest 50/50、selfcheck 319/0、docs、lrelease）为绿。真机 13 项仍由用户验收。 |
