# VEdit UX 批次 2 —— 交接文档

- 停工时间：**2026-11-05 18:15 CST**
- 停工原因：用户中止（MPD 编排器缺陷导致并行度无法达标，先修插件）
- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`，分支 **`main`**，**420 个未提交改动**（本批次 + 上一批次累积）
- 契约（冻结）：`.mpd/plans/ux-batch-2/00-plan.md`（含用户三轮裁定 §0 / §0.1 / §1 实测事实 / §3.WaveA / §4.WaveB / §5.1 集成步骤）

> **读这份文档的顺序**：§0 结论 → §1 需求矩阵 → §3 未完成清单 → §7 恢复步骤。§5 是"别再重查一遍"的实测事实，§6 是给插件作者的缺陷复盘。

---

## 0. 一句话结论

用户提的 **14 项 + 1 个额外发现（SDC/XDC）+ 1 个额外 bug（分屏激活）** 中：

| 类别 | 数量 | 明细 |
|---|---|---|
| **已交付并通过独立复核（PASS）** | 10 | item 1、2（模块树/接口面板）、3、7、11（顶栏/菜单/翻译框架/内置手册）、6、8（设置/缩放/format/lint 面板）、13（Ctrl+点击）、14（VHDL/MD/Tcl/SDC/XDC 高亮）＋分屏激活 |
| **已实现、返修完成、**待终审** | 1 | item 5（搜索对齐 + 全工程替换，B2 第三轮返修完成） |
| **已实现、复审还剩 2 个状态 bug** | 3 | item 4/9/10/12（诊断体验 + Rename，B3 的 N2/N3） |
| **收尾工程未做** | — | 翻译收尾、用户手册、语法测试完整性、selfcheck 覆盖、全量 gate、AppImage 重打包、提交 |

**⚠ 当前树里有一处中间态必须优先收拾**：`translations/vedit_zh_CN.ts` 已跑过 `lupdate` 但**翻译未落盘**——1207 条、**182 条 unfinished**、21 条 vanished。**`lrelease` 现在是红的**。见 §3.U2。

---

## 1. 需求矩阵（用户 14 项 + 追加）

| # | 用户原话要点 | 状态 | 落点（代码） | 证据 / 复核 |
|---|---|---|---|---|
| 1 | 模块树要"有顶层的 module 树"，仅模块信息，双击打开模块代码 | **PASS** | `src/workspace/module_tree/**`（`module_catalog` / `module_hierarchy` / `catalog_client` / `module_tree_panel`），Rust 侧 `crates/vd-deeplint/src/catalog.rs` + `vd_catalog_*` ABI | `evidence/ux-batch-2/b1/` + `b1-repair/`；复核 `review-b1/`(FAIL) → `review-b1-r2/`(**PASS**) |
| 2 | 接口信息单开面板，跟焦点 module，显示类型/方向/位宽，双击跳声明 / Ctrl+双击跳例化 | **PASS** | `src/workspace/module_tree/interface_panel.*`（dock id `interface-panel`） | 同上；宽度由 `type` 字符串推导（`logic[3:0]`→4） |
| 3 | 右键菜单与顶栏 Project/面板开关中文翻译不全 | **PASS（框架）** | `src/app/app_shell.cpp`（菜单固定 File/Edit/View/Project/Help）、`src/editor/kernel/lsp_actions.cpp`、`diagnostics_integration.cpp` 命令文本全部 `tr()` | `evidence/ux-batch-2/a2/`；**中文条目收尾未做** → §3.U2 |
| 4 | Rename Symbol 不好使 | **B3 未收口** | 修复：`symbol_scan.cpp`（`renameTargetAt` 词法校验）+ 请求路径能力门控 + 对话框前校验 | `evidence/ux-batch-2/b3-repair*/`；**实测 verible renameProvider=true**，协议层本来就可用，真 bug 是"非符号位置也弹框发请求" |
| 5 | 搜索面板与 Ctrl+F 对齐 + 全工程搜索 + 搜索替换 | **待终审** | `src/workspace/search/**`（`search_query.h` 共享选项、`project_replace.*`、`document_sync.*`）+ `src/editor/kernel/find_replace.*` | `evidence/ux-batch-2/b2-repair-r3/`；**第三轮返修完成**（reviewer 自己的探针 0 failure），终审未出 |
| 6 | format 可配项、LINT 可配项做成设置 | **PASS** | `src/workspace/settings/**`（23 个 format 旋钮 + 统一 Lint 页两套持久化）、`ui_scale.*`、`crates/vd-settings` | `evidence/ux-batch-2/a4/`；**6 个旋钮是 transport-pending**（见 §3.U6） |
| 7 | 顶栏混乱，参考其他编辑器重排 | **PASS** | `app_shell.cpp`：菜单顺序 + 主工具栏（新建/打开/保存/全部保存/撤销/重做/搜索/构建/格式），Preferences 并入 Edit | `evidence/ux-batch-2/a2/`（撤销/重做真接 KTextEditor） |
| 8 | 可选 UI 字体大小（高分屏看不清） | **PASS** | `ui.font_point_size`（即时）+ `ui.scale_percent`（重启生效，`QT_SCALE_FACTOR` 在 QApplication 之前） | `evidence/ux-batch-2/a4/` |
| 9 | error/warning 悬浮框 + 一键修复 | **B3 未收口** | `diagnostic_hover.*`、`autofix.*`（引擎实测可修才给按钮，一次撤销可回退）、去掉了 `hoverProvider:false` 的错误置灰 | `evidence/ux-batch-2/b3-repair-r2/` |
| 10 | 文件名前黄/红/绿问题点 | **B3 未收口** | `src/workspace/filetree/file_status_delegate.*` + `FileStatusRole` | 同上（有真机窗口截图） |
| 11 | 帮助里塞真手册 | **PASS** | `src/app/help_viewer.*` + `resources/help/user_manual.qrc`（qrc 内嵌 `docs/USER_GUIDE.md`，`QTextEdit::setMarkdown`），F1 | `evidence/ux-batch-2/a2/`（终端聚焦时 F1 仍透传 PTY，已双向实测） |
| 12 | LINT 时 ELAB 错误弹窗引导跳转 | **B3 未收口** | `project_lint_notice.*`：**仅显式 Lint Project 弹模态**，自动跑给可点状态栏 | 同上 |
| 13 | Ctrl+鼠标点击 = Go to definition | **PASS** | `multi_cursor.cpp`（`viewPointFor` 坐标映射）+ `lsp_actions.cpp`（`dispatchDefinitionAt`，走 F12 同一条 dispatch）；多光标改 Alt+点击 | `evidence/ux-batch-2/b4-repair/`；复核 `review-b4-r2/`(**PASS**) |
| 14 | VHDL/markdown/tcl 语法高亮 | **PASS** | `resources/syntax/vedit-{vhdl,markdown,tcl}.xml`（自研，LGPL-2.1-or-later）+ `highlight_mode.cpp`（表优先、仓库兜底）+ `ksyntax_data.cpp` | `evidence/ux-batch-2/a3-repair/`；复核 `review-a3/`(FAIL) → `review-a3-r2/`(**PASS**) |
| 附 | SDC/XDC 用 Tcl 高亮 | **PASS** | 扩展名表 `.sdc/.xdc → Tcl` | 同上 |
| 附加 | （评审发现）点非活动分屏不切换 | **已修** | `src/app/editor_area.*`：应用级事件过滤器（KateViewInternal 也能命中） | `evidence/ux-batch-2/pane-activation/`（49→50 ctest） |

**用户裁定（BINDING，勿改）**：Ctrl+点击=定义 / 多光标改 Alt+点击；接口面板双击跳声明、Ctrl+双击跳例化；替换是**两个按钮**（替换 / 全部替换）；工程 LINT 上位、模块 LINT 下位（只针对代码框内显示）；**工程 LINT 每次保存后自动跑**（去抖 700ms、单飞、**自动跑绝不弹模态框**）；Lint 面板统一 UI 但**两套独立配置**，后端不支持的旋钮置灰并写原因；菜单 Preferences 并入 Edit。

---

## 2. 各 lane 最终状态

| lane | 交付 | 门禁（自证） | 独立复核 |
|---|---|---|---|
| A1 Rust ABI | 模块目录 `vd_catalog_*`、搜索 `vd_search_tree_query_ex`（含精确 range）、Lint 参数 `VdLintBatchOptions` 类型化 + `VdProjectInfo.rules_path` | 189 tests / clippy / fmt / header drift 绿 | 由 B1/B3 消费侧间接验证 |
| A2 顶栏/i18n/手册 | 菜单重排 + 工具栏 + 撤销重做 + 全部 tr() + qrc 手册 + `.ts` 重生成（1018 finished） | `vedit-app --selfcheck` 312 PASS / ctest 42→ | 内含于 B 系评审 |
| A3 高亮 | 5 份定义（新增 3 份自研）+ 表优先映射 + `verify-f2-isolation` 重写 + 许可登记 | AppDir 104 checks / F2 11 pass | `review-a3-r2` **PASS** |
| A4 设置 | 字体/缩放 + 23 format 旋钮 + 双后端 Lint 页 | ctest 42/42 + 228 checks | 内含于 B 系评审 |
| B1 模块树 | 纯模块层级 + 接口面板 | ctest 43→（#33 76 checks / #34 56→80 checks） | `review-b1` FAIL → `review-b1-r2` **PASS** |
| B2 搜索 | 共享选项模型 + 全工程替换 + 冻结快照 + 逐命中指纹 | suite 275/0/0；reviewer 探针 0 failure | **终审未出**（`review-b2-r4` 待跑） |
| B3 诊断 | R2 分层诊断 + R3 保存自动 lint + 悬浮/修复/文件点/ELAB | lint-experience 98/98、wiring 60/60、rename 21/21 | `review-b3-r3` FAIL（仅剩 N2/N3） |
| B4 导航 | Ctrl+点击 + Alt 多光标 + 坐标映射 + 折叠陈旧光标 | 119 checks / ctest 50 | `review-b4-r2` **PASS** |
| pane-activation | 分屏点击激活 | 25/25（修前 11 失败） | 被 `review-b4-r2` 独立探针覆盖 |

**最新已知 ctest 总数：50**（基线 38；只增未减）。**尚未跑过全量 7 gate**。

---

## 3. 未完成工作清单（可直接派活；每项已按文件域切好，互不重叠 → 可全并行）

### U1 —— B3 最后两个状态 bug（诊断）（写：`src/workspace/diagnostics/**`）
来源 `evidence/ux-batch-2/review-b3-r3/REPORT.md`：
- **N2**：一个被工程 pass 覆盖过的文件，"打开 → 在 400ms 订阅之前编辑 → 在该次 poll 之前切走" → 永远不被标记为已编辑，保留旧的上位结论（**错误已被清除却仍显示绿点**），且 C8 把它排除在后续 pass 之外 → 状态一直错到下次激活/保存。
  修法：挂 `ApplicationShell::fileOpened`，或在 `ActiveFileWatcher::trackDocument()` 里对**每个新跟踪文档**检查 `bufferModified()`，不要只看"poll 时刻的活动文件"。回归用例按上述时序写。
- **N3**：`applySettings()` 清了库但**没取消在飞的 pass** → 老引擎的 pass 继续跑并写入未覆盖行，且新的一次显式 Lint Project 会被"already running"拒掉。
  修法：在 `BackendWiring::applySettings()` 里、`clearProjectDiagnostics()` 之前调用既有 cancel 路径；补一个"pass 进行中切后端"的回归。
- 三个 nit：分屏下 `applyDiagnosticFix` 的目标视图；`renderHover` 跨视图定位（走 adopt 路径）；自动触发在文件集加载期误报 "no project file set"。
验收：两条功能修复 red→green 输出；定向 ctest 绿；不要跑全量 build。

### U2 —— 翻译收尾（**优先做，否则 `lrelease` 红**）（写：`translations/**`）
当前状态（实测）：
- `evidence/ux-batch-2/integration-translations/` 里已有：`00-catalogue-before.ts`（备份）、`01-lupdate.log`、`02-unfinished-before.txt`（182 条待译清单）、`_parsed.json`、**`apply-translations.py`（已生成，未执行）**。
- `lupdate -locations absolute` 已跑：**Found 1186 source text(s) (182 new, 1004 existing)，Kept 14 obsolete**。
- `.ts` 现状：**1207 messages / 182 unfinished / 21 vanished** → **`lrelease` 会失败**。
执行步骤：
1. `git diff --stat translations/` 确认改动范围；如需回滚可回到 `00-catalogue-before.ts`。
2. 审一遍 `apply-translations.py`（它是被中断的 agent 写的，**未经验证**），确认 182 条与 `02-unfinished-before.txt` 的顺序/内容一致；一致就执行，不一致就以 `02-unfinished-before.txt` 为输入重做。
3. `lrelease` → 必须 `0 unfinished`；记录 总数/完成数/新增数。
4. 逐条核查 21 条 vanished：仍可达的要恢复为 active，真死掉的写清原因（**上一批曾因 lupdate 相对路径丢掉 6 条译文**，务必用 `-locations absolute`）。
5. 术语抽查：`替换`/`全部替换`/`修复`、接口面板列 `名称/方向/类型/位宽/位置`、模块树列 `模块/例化次数/定义位置`、三种点状态。
宿主机有 `/usr/lib/qt6/bin/lrelease`；`lupdate` 在镜像内（`scripts/` 里的 Qt 镜像）。

### U3 —— 用户手册 + 文档纠错（写：`docs/USER_GUIDE.md`；`docs/handover/**`；`src/syntax/kde/README.md`；`evidence/ux-batch-2/b1/README.md`）
手册要加（任务导向，"用户做什么/看到什么"）：模块树（根=从未被例化的模块、共享模块出现在每个父下、双击开声明、不再有网络/驱动行）；接口面板（列、跟随焦点模块、双击→声明、Ctrl+双击→**例化语句**）；搜索（共享选项、范围、两个替换按钮、RE2-vs-PCRE、先保存后替换/可拒绝）；诊断（保存后自动工程 lint 且静默、ELAB 只在显式时弹、悬浮+条件修复按钮、红/黄/绿点含义）；导航（Ctrl+点击、Alt+点击、Alt+拖拽、点分屏即激活）；设置（字体即时/缩放重启、format 默认不发 flag、双后端 lint 面板与置灰）；语法高亮（完整扩展名表，含 `.sdc/.xdc→Tcl`）。
再写一节"已知限制"（四条诚实边界，见 §8）。
纠错：`docs/handover/04-hard-facts.md` 的 `builtin_valid_defs=2` → 5；`docs/handover/**` 里"模块树是例化树/没有跨文件替换/hover 不可用"之类过时叙述；`src/syntax/kde/README.md` 的两份定义；`evidence/ux-batch-2/b1/README.md` 的 "Every string is wrapped"（返修后才成立）。

### U4 —— 语法启动测试完整性（写：`src/syntax/product/CMakeLists.txt`）
缺陷：该文件用 **DEFERRED** CMake 调用设置测试环境变量 `VEDIT_SYNTAX_DIR`，`${CMAKE_CURRENT_SOURCE_DIR}` 在顶层调用点求值 → 生成属性解析成 `/resources`（错误路径）；且 PASS 正则只要求弱字符串 `syntax-product: grammar=verilog` → **定义没加载也能绿**。
修法：改成 `${CMAKE_SOURCE_DIR}/resources` 或在 deferred 之前提前捕获目录；PASS 条件加强为"语法数据已加载 + 五份定义名都在"。
验证三点：①生成的 ctest 属性行 ②测试输出含五个定义名 ③故意写错路径时测试 **FAIL**，再恢复。

### U5 —— 打包自检覆盖（写：`packaging/runtime/selfcheck.sh`）
现在只 grep 两份 XML，实际随包 **五份**（Verilog/SystemVerilog/VHDL/Markdown/Tcl，见 `usr/share/vedit/ksh-whitelist.txt`）→ 缺三份也能过。扩到五份并保持 fail-closed。
注意：**不要动 `AppRun` 的 `ksh_guard`**——它刚修好（两边都用 `LC_ALL=C sort` 归一），是本批次最严重的一个打包 bug（见 §5.F6）。

### U6 —— A1/A4 之间的 lint 接线收尾（写：`src/workspace/settings/**` + `src/workspace/diagnostics/ffi_backends.cpp`）
A1 已交付 `vd_lint_unsupported_knobs()`、`vd_lint_batch_command_line()`、`VdProjectInfo.rules_path`、`vd_lint_batch_command_line`。A4 的面板把 6 个旋钮标成 `transport-pending`（可编辑/持久化，但没进 argv）：`lint.verible.{ruleset,rules,waiver_files,autofix}`、`lint.slang.{warnings,error_limit}`。接线点**只有一个**：`src/workspace/settings/lint_options.h` 的 `LintJobOptions` + `buildLintJobOptions()`。
另：`buildLintJobOptions()` 的 rules 优先级已实现为 `lint.verible.rules_config → tools.verible_rules → 发现路径(今天恒空) → 不传`；拿到 `VdProjectInfo.rules_path` 后把"发现路径"作为默认值接上即可（手填优先、发现兜底、都没有就不传）。
验收：每个旋钮面板值→argv 的断言；默认值不发 flag；两套配置互不覆盖。

### U7 —— B2 终审（只读）
`evidence/ux-batch-2/b2-repair-r3/` 已备好（reviewer 自己的探针源码被原样重编译并 0 failure）。终审只需：原样重跑该探针、复核 229 条基线 check 全在且 PASS、判定三条披露的残留边界是否在门禁句（"Replace All / 替换 是否还能在用户刚保存的文件上写错字符"）之外。

### U8 —— 最终 gate + 打包 + 验收矩阵（captain）
1. 全量 7 gate：`build-qt`、`ctest`、`--selfcheck`、`clippy`、`fmt`、`docs`、`lrelease`（`package.json` 是 gate id → `build/gates/*.sh` 的映射；`bun run verify:gates`）。
2. AppImage 重打：`packaging/runtime/` 下的 `assemble-appdir.sh` + 打包脚本；**必须验证** AppRun 守卫在 `zh_CN.UTF-8`/`en_US.UTF-8` 下都 PASS（见 §5.F6）。
3. 真机烟测项：模块树/接口面板、搜索替换两个按钮、悬浮+修复、文件点、Ctrl+点击、Alt 多光标、分屏激活、语言切换、重启按钮、VHDL/MD/Tcl/SDC 高亮。
4. 写验收矩阵（`evidence/final-acceptance-*/AC-MATRIX.md` 的既有格式）。

### U9 —— 提交（captain 是唯一 commit 者）
按 `AGENT.md §6`：**不在 `main` 直接提交**。当前在 `main` 且 420 个未提交改动（含上一批次）。建议：`git checkout -b feat/ux-batch-2`，按 lane 分次提交（A1/A2/A3/A4/B1/B2/B3/B4/pane/integration），提交信息 `<type>(<scope>): <summary>`。**提交前必须与用户确认**。

---

## 4. 环境与命令

| 用途 | 命令 | 备注 |
|---|---|---|
| 产品构建 + 全 ctest + xcb 烟测 | `bash scripts/build-qt.sh` | **30–60 分钟**（Docker 镜像）；这是本批次的时间黑洞 |
| 跳过镜像重建的快速构建 | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh` | 仍会跑全 ctest |
| 定向测试（推荐） | `ctest --test-dir target/qt6-build -R "<name>"` | 修复轮**只该跑这个** |
| 产品自检 | `target/qt6-build/src/app/vedit-app --selfcheck` | 期望 312 PASS / exit 0 |
| Rust 侧 | `cargo test -p <crate>` / `cargo clippy --workspace --all-targets -- -D warnings` | 需在钉死镜像里 |
| 翻译 | `lupdate -locations absolute` / `lrelease` | 宿主 `lrelease` = `/usr/lib/qt6/bin/lrelease` |
| 打包 | `packaging/runtime/assemble-appdir.sh` + 打包脚本 | 产物 `packaging/runtime/out/` |

已知噪声：并发 lane 同时构建同一 `target/qt6-build` 会出现**无关的瞬时失败**（例：`vedit-project-lint-e2e` 偶发 SIGPIPE、`vedit-app-syntax-startup` 偶发失败），单独跑/重跑即绿。修复轮的规矩应是：等 90s 重试一次，第二次仍失败就上报。

---

## 5. 关键实测事实（下个会话**不要再重查**）

**F1 verible-verilog-ls（随包 v0.0-4298-g085eef05）能力**（本轮探针实测）：
`renameProvider: true`（真实 `textDocument/rename` 返回 3 处 WorkspaceEdit）、`prepareRename` 有、`definitionProvider/referencesProvider/documentSymbolProvider/documentFormattingProvider` 均 true；`codeActionProvider: true` 但实测返回 `[]`；**`hoverProvider: false` 但 hover 请求实际有内容**（据此撤掉了错误的置灰）。

**F2 slang `--ast-json --ast-json-source-info` 字段**：
`Instance` 有 `source_file/line/column` = **例化处**；`InstanceBody` 有 `source_file/line/column` = **模块定义处**，`name` = 模块名；`Port` 有 `type`（如 `logic[3:0]`）与 `direction`（`In|Out|InOut`，ref 为 `Ref`）；design 的 root `Instance` = slang 选出的 top（= 未被例化的模块，含"定义了但没人例化"的）。**interface 的类型端口序列化为 `InterfacePort` 且 interface 实例有自己的 InstanceBody** → 目录必须按 `definitions[].definitionKind == "Module"` 过滤，否则 interface 会被当成模块。端口只能取 `kind == "Port"`（同名 Net/Variable 会并列）。

**F3 verible-verilog-format 可配参数**（30+）：`--column_limit --indentation_spaces --wrap_spaces --line_terminator --try_wrap_long_lines --compact_indexing_and_selections` + 11 个 alignment + 4 个 indentation + 2 个 right_align + `--flagfile`。

**F4 verible-verilog-lint**：`--autofix=[no|patch|patch-interactive|inplace|inplace-interactive|generate-waiver]`、`--rules=`、`--ruleset=[default|all|none]`、`--rules_config=`、`--rules_config_search`、`--waiver_files=`、`--print_rules_file`、`--help_rules`。

**F5 保存路径**：全仓**没有** save 信号，KTextEditor `Document::saved` 从未连接；**唯一公共接缝**是 `EditorPane::saveTab` / `saveAsFile`（`src/app/editor_area.cpp`，Ctrl+S、关闭提示、另存为都经过它）→ R3 的"保存后自动 lint"就挂这里（已实现，700ms 去抖 + 单飞 + 不阻塞保存）。

**F6 AppRun 语言环境（本批次最严重的打包 bug，已修）**：
`assemble-appdir.sh` 用 Python 排序写白名单（`…Tcl,VHDL,Verilog`），而 `AppRun` 的 `ksh_guard` 用宿主 locale 的 `sort` 比较（`…Tcl,Verilog,VHDL`）→ **在 zh_CN.UTF-8 和 en_US.UTF-8 下都会 `exit 2`，包直接起不来**（只有 `LC_ALL=C` 能过）。修法：两边都用 `LC_ALL=C sort` 归一，且补了非 C locale 自检用例。**任何重打包都必须验证 zh/en 两种 locale。**

**F7 工程 LINT 原本"跑完看不见"**：结果进 `cliDiagnostics_`，`displayDiagnostics()/mergeDiagnostics()` 零调用点、`LintProgressBar` 编译但从未挂载；且 `setOpenFiles()` 在 `startProjectPass()` **之后**调用（用的是上一轮文件快照）。B3 已修（分层视图 + 顺序修正 + 计数单一来源）。

**F8 分屏激活（本轮新发现）**：KatePart 的真实鼠标目标是内层 `KateViewInternal`（约在 view 内 `(36,1)`），装在 view 上的过滤器收不到点击 → 点非活动分屏不会切换 `activePane()`。修法：`EditorArea` 在 **qApp 级**装过滤器（应用级过滤器先于对象级运行），命中 `MouseButtonPress/FocusIn` 即 `setActivePane()`。

**F9 Xvfb 下 CJK 缺字**：「修复」按钮会渲染成豆腐块（镜像里没有 CJK UI 字体），但按钮存在且位置正确——截图证据判"位置/存在"，不判字形。

---

## 6. MPD 编排器缺陷复盘（**给插件作者**；这是本次停工的直接原因）

> 事实基础：本会话从 `2026-11-05 00:34` 到 `18:15`（约 17.7 小时），实际并发峰值只到"4–5 个 lane 同时跑"，**大段区间并发度 = 1**；同一批产品工作里 `build-qt.sh` 被跑了 ≥10 次。

### D1（Blocker）`mpd_role_spawn` 是**同步一次性** consult —— 队长被阻塞，没有"并发扇出后继续干"的写入面
- 现象：一次 `mpd_role_spawn` 调用直到子代理**报告返回**才释放队长。要并行只能"一条消息里塞多个 spawn，然后等**全部**返回"。
- 后果：队长无法在 lane 跑着的时候继续编排/整合/写文档；任何"边跑边推进"的形态都不存在。
- 附加限制：spawn 出来的子代理是 **leaf**（两个 spawn 工具都被 deny），且 **depth ≥ 2 的调用者直接被拒** → 无法树形并行（一个 lane 想再分包给 3 个子任务做不到）。
- 建议：提供 `spawn_async(...) -> handle` + `await(handle)/poll(handle)`，或让 spawn 立即返回句柄；并允许 depth ≥ 2 的受控树形委派（带预算）。

### D2（High）`agent_teams_plan add_member` 的参数形状反直觉
- 现象：把 `name/prompt` 放顶层 → `Error: a teammate needs a non-empty name`；必须写成 `member: {name, prompt, description?, role?}`。
- 建议：接受扁平参数或在错误信息里直接给出正确形状示例。

### D3（Blocker）staged 任务**没有 id**，approve 时才分配 → 评审任务无法声明依赖
- 现象：`create_task` 返回 `staged task id=- …`；`blocked_by` 只能引用尚不存在的 id。
- 后果：**"work 任务 → review 任务"的 DAG 无法在一次 staging 里表达**，必须 approve 后再补一次 staging（而 D5 让补 staging 变成"新建 team"）。
- 建议：staging 时给稳定 key（`key`/`slug`），`blocked_by` 接受 key；或 approve 立即返回 id 映射并允许 append。

### D4（Blocker）approve 之后成员**没有看板任务**，而成员的纪律是"无板任务不得读写/发消息"
- 现象：approve 成功 spawn 了成员，但它们随即回报"无看板任务，按纪律不读不跑不写不发消息"；队长用 `send_message` 追派任务也被同样拒绝。
- 更糟：本可补救的动词（`team_task_create` 等）**被 team-door guard 拒绝**。
- 后果：**唯一出路是重新 staging 一整个 plan**（→ D5 又导致新 team）。
- 建议：approve 时若 plan 有任务就一并派发（不要只 spawn 成员）；或提供 captains-only 的 `team_task_add(member, ...)`；或让 `send_message` 的指派等价于看板任务（带记录）。

### D5（Blocker）一 plan = 一 team，**无法向已 approve 的 team 追加任务**
- 现象：wave A 的 team (`team-…1yw7-002`) 在 approve 后无法追加 wave B 的任务；再 `create` 只会得到**新的 plan/team**。
- 后果：多波次工作只能退回 `mpd_role_spawn`（= D1 的同步路径）→ **这就是"串行到死"的机制根源**。
- 建议：`agent_teams_plan {action:"append", team_id, tasks:[…]}`；或允许同一 plan 多次 approve 增量。

### D6（High）`agent_teams_plan status` 打印**旧 cell 的看板**
- 现象：plan id 是新的，打印出来却是旧 archived cell 的 12 members / 54 tasks（其中若干 `running`）。
- 后果：队长无法信任看板，只能自己记账（进一步削弱 team 的使用价值）。
- 建议：status 默认只报**当前活动 team**；历史 cell 需显式 `--all`。

### D7（Medium）并行任务的 write scope **目录级重叠直接被拒**
- 现象：两个都要写 `evidence/mpd-retro/` 的文档任务，approve 被拒：`declare OVERLAPPING write scopes … NEITHER depends on the other`。
- 后果：**并行的文档/证据类任务必须把 scope 声明到文件级**；否则要么串行（加 dependency edge），要么改 scope。
- 建议：错误信息里提示"把 scope 细化到文件"；或对纯新增文件（不存在的路径）自动判定不重叠。

### D8（Medium）成员路由：非 roster 名 + 无 `role` 字段 → `none (no-role)`
- 现象：approve 输出 `Retro Tool — none (no-role)`（四个复盘成员全部如此）。
- 后果：这些成员没有自己的 route，是否继承 staging 会话 route 取决于实现，行为不可预期。
- 建议：approve 时对 `no-role` 给出明确警告并把最终 route 打出来（provider/model）。

### D9（High）缺"批量评审"与"severity 门控"
- 现象：评审只能逐 lane 触发，每次都是对一个 lane 的完整独立复核；一个 FAIL 就回到"修复 → 再复审"整轮。
- 后果：本批次 B2 走了 **4 轮**、B3 走了 **4 轮**，每轮都是数十（到上百）分钟；合同里只好用**散文**写了一条"只阻 Blocker/High"的规则，靠评审自觉执行。
- 建议：`review(mode:"batch", lanes:[…], block_on:["Blocker","High"])` 原语；并让 verdict 结构化（finding → severity → 是否阻断）。

### D10（High）gate 成本对编排层不可见
- 现象：没有任何机制知道"跑一次 gate 要 30–60 分钟"，也没有"task-scoped gate"或"final gate 任务"的概念；于是每个修复轮都跑了全量 `build-qt.sh`。
- 建议：任务可声明 `gates:["targeted"]`，引擎据此只跑受影响用例；另设显式 `final-gate` 任务类型；并在 captain 的工具面暴露"上次 gate 耗时"。

### D11（Medium）并发 lane 共用一棵 build tree，无隔离/无锁
- 现象：多个 lane 同时 `build-qt.sh` 进同一个 `target/qt6-build`，出现**与改动无关**的瞬时失败（SIGPIPE、偶发 syntax-startup 失败），每个都要 wait-and-retry。
- 建议：per-lane build dir 或由引擎持有的构建锁（任务可声明 `needs_build_lock`）。

### D12（Medium）`mpd_verify_*`（verifier seat + loop + 冻结合同）没有接到 team/task 面
- 现象：`mpd_verify_open/seat/record` 恰好建模了"写者/验证者 + 冻结合同 + FAIL→repair 回到同一 loop"，但 captain 驱动的 team 面里没有入口；cell 归档后整批一次没用上。
- 建议：把 verify loop 作为 team 任务的一等属性（`task.verify_loop`），approve 时自动开 loop，FAIL 自动生成 repair 任务并回派同一 writer。

### D13（High，队长侧）没有"让位/交接"原语
- 现象：本轮只能由用户手动中断；队长没有 `pause/dispose/checkpoint-and-handoff` 的工具化路径，长任务一旦跑偏只能硬停。
- 建议：`checkpoint(summary)` + `handoff(document)` 原语，供队长在成本失控时体面收尾（本文件就是手工版）。

### D14（Medium，实测现象）一条消息里的多个 spawn 只有第一个被记录
- 现象：同一条消息里 6 个 `mpd_role_spawn`，第 1 个返回 `interrupted after it was recorded, but no result was durably recorded`，其余 5 个 `interrupted before the Harness recorded it as started`。
- 说明：其中部分中断是用户手动触发的，但**至少一次是自动记录失败**；无论如何，结果是"同批 spawn 大部分没启动"，这直接摧毁了"一条消息塞多个 spawn = 并行"的假设。
- 建议：spawn 排队与持久化要么全成功要么明确逐步返回；不要把"多调用并行"当作并行机制。

### 复盘结论（给插件改造的优先级）
1. **P0：异步 spawn 句柄 + 允许 depth≥2 的受控树形委派**（D1）——没有它，并行度上不去。
2. **P0：team 可增量追加任务 + approve 即派发 + captain 可补任务**（D3/D4/D5）——这是"用 team 而不是 spawn"的前提。
3. **P1：批量评审 + severity 门控 + verify loop 接进 team**（D9/D12）——这是返修轮数爆炸的直接原因。
4. **P1：gate 成本可见 + task-scoped gate + 构建锁**（D10/D11）——这是 30–60 分钟 × N 次的来源。
5. **P2：status 作用域、错误信息、write scope 文件级提示、route 打印**（D2/D6/D7/D8）。

---

## 7. 恢复步骤（下个会话照此走，别再重做侦察）

```
Step 0  读本文件 + .mpd/plans/ux-batch-2/00-plan.md（契约与用户裁定）
Step 1  收翻译（U2）——先把 unfinished 清零，否则 lrelease gate 红
        ├─ 审 apply-translations.py → 执行 → lrelease（0 unfinished）
        └─ 逐条处理 21 条 vanished（用 -locations absolute 重跑）
Step 2  并行四路（写域互不重叠，可同时派）：
        U1 诊断 N2/N3  → src/workspace/diagnostics/**
        U3 手册+文档   → docs/USER_GUIDE.md / docs/handover/** / src/syntax/kde/README.md
        U4 CMake 语法路径 → src/syntax/product/CMakeLists.txt
        U5 selfcheck 覆盖率 → packaging/runtime/selfcheck.sh
Step 3  跑一次全量 gate（U8.1）；只此一次
Step 4  B2 终审（U7）+ 全批终审（一个 reviewer，只阻 Blocker/High）
Step 5  AppImage 重打包 + zh/en 双 locale 验证 + 真机烟测
Step 6  写验收矩阵；与用户确认后提交到 feat/ux-batch-2
```

**并行度建议**：Step 2 的四路必须**同时**放；Step 3–5 是队长自己做（不派）；评审与修复不要串成单链，Step 4 一次性评完。

---

## 8. 风险与未验证项（各 lane 披露边界的合并）

1. **没有任何真机 GUI 验证**：全部 offscreen 或 Xvfb；观感/DPI/字体由用户验收。Xvfb 下 CJK「修复」为豆腐块（F9）。
2. **跨文件 Rename 不声明支持**：verible 只对当前打开文件作答（LSP 会话是文件作用域）。已作为 `NOT-CLAIMED` 明示，不再算 SKIP。
3. **工程级正则 = RE2 风格**（无反向引用/lookaround），与 Ctrl+F 的 PCRE 语义不同。
4. **替换的最窄残留边界**（B2 披露）：外部改写且 size+mtime 完全不变；扫描窗口外插入与命中行**完全相同**的文本行；非产品桥（无 `bufferText()`）。
5. **自动工程 lint 的"当前文件"语义**：C8 已按用户裁定改为"包含刚保存的文件 + 未修改的打开文件，排除已修改缓冲区"。
6. **未打包**：AppImage 未重打；`svnadmin` 仍 absent（build-only，符合设计）。
7. **未跑全量 7 gate**；`typecheck`(check_ffi_header_drift) 与 `tests`(`bun test packages`) 仍是历史豁免项。
8. **未提交**：420 个未提交改动在 `main` 上，提交前需与用户确认（AGENT.md §6 不允许直提 main）。

---

## 9. 证据索引（`evidence/ux-batch-2/`）

| 目录 | 内容 |
|---|---|
| `a1/` | Rust ABI（模块目录/搜索/ lint 参数）证据 |
| `a2/` | 顶栏/菜单/i18n/手册；含 xcb 截图 |
| `a3/` + `a3-repair/` + `review-a3/` + `review-a3-r2/` | 高亮：交付 → 返修 → FAIL → **PASS** |
| `a4/` | 设置（字体/缩放/format/lint） |
| `b1/` + `b1-repair/` + `review-b1/` + `review-b1-r2/` | 模块树+接口面板 → **PASS** |
| `b2/` + `b2-repair/` + `b2-repair-r2/` + `b2-repair-r3/` + `review-b2*/` | 搜索替换：3 轮返修 → 待终审 |
| `b3/` + `b3-repair/` + `b3-repair-r2/` + `review-b3*/` | 诊断体验：2 轮返修 → 剩 N2/N3 |
| `b4/` + `b4-repair/` + `review-b4/` + `review-b4-r2/` | Ctrl+点击 → **PASS** |
| `pane-activation/` | 分屏激活修复 |
| `integration-translations/` | **翻译中间态**（备份 + lupdate 日志 + 182 条清单 + 未执行的脚本） |
| `docs/handover/` | 原有交接系列（本文件是续篇） |

**MPD 插件复盘的 4 个分节任务**：team `mpd-retro`（`team-…yirt-001`）的 T1–T4 在停工时刻仍是 `in_progress`，**`evidence/mpd-retro/` 无产出**。§6 是我本人基于一手操作记录写的，可直接作为插件改造依据；若那 4 个成员将来产出，可作补充证据。
