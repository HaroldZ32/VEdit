# CELL-20261102-STATUS · VEdit 本轮（cell-20261102T050548232Z）跨会话交接

> 编写：cell-planner（board T47），2026-11-02 17:20 (+08:00)。
> 依据：`.mpd/cellwork/cell-20261102T050548232Z/{state.json,journal.jsonl,mail.jsonl,requirements.md,handoff/*.md}`、`split/1.json`、`evidence/**`、`.mpd/verify/records/*.json`、各任务报告邮件。
> 基线：HEAD `4e2d0a5` + 本 cell 的工作树（最后一次测量时 **156 个脏路径**）。**本 cell 按用户裁定不管理 git**；本文件不含任何提交/分支/PR 建议，交付形态 = 工作树状态。
> 读法：先读 §0 与 §5；§1/§2 找证据；§3 是本轮真实缺陷；§4 是 18 条增补令；§6/§7 是环境与流程教训。

> **2026-11-05 更正（UX 批次 2）**：本文件里的 T17「工程级模块树 + 信号 source/target 一键跳转」是**旧 lane**——
> 它已被 UX 批次 2 item 1 替换为**纯模块层级树**（列只有 模块/例化次数/定义位置；没有网络/驱动/负载行，也没有
> 信号跳转；双击模块行打开其声明）。因此下文关于 T17 模块树的 §C/D 路径段、信号跳转叙述，以及“跨文件替换是
> 非目标”的条目**均已作废**，保留只为历史记录。当前口径见 `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md`。

## 0. 一句话结论

本 cell 把 requirements rev1 的 10 条目拆成 40 个任务（20 work + 20 review，10 成员），并因写域缺口追加 T43–T48（当前看板 48 行：31 completed / 15 pending / 2 in_progress）。**M0 基础设施与 M1 编辑器基本盘基本落地**：工程模型（开文件夹即根、.f 导入、并集、-I/-D、持久化）、工程级 lint 编排（后台 job+poll、进度、取消/重跑、去重）、诊断呈现（warn/error 分栏 + 行内波浪线 + 双击跳转）、文件标签/未保存标记、查找替换、多光标/块选择、会话恢复、设置三页、模块树/信号跳转（旧 lane；见文首更正）。**最大成果是 Rust 引擎与规模证据**：1k 文件/10k 实例实测通过，T46 修复层次服务两个 walker 缺陷后 **11000/11000 实例与 ground truth 一致**。**最大未完成**：T11 的 slang 方向切换（被 `vd-lsp` 客户端 router 缺陷阻塞），以及 T20→T18→T21→T22 这条「主壳 → i18n 运行时 → i18n 覆盖 → 集成验收」链尚未开始。

本轮结束时：root `cmake` configure / `vedit-app` 构建绿；全量 `ctest` **36/37**（唯一红 = `vedit-workspace-search-selftest`，属 T20/A-2）；5 个写席（dw-rust、se-project、se-ui、se-shell、lead-editor）被平台 compaction barrier fenced（reseed 失败：`subagent creation requires the exact live parent agent`），cell 因此提前结束。

## 1. 已完成任务与证据（含审查记录）

### 1.1 已完成的 work 任务

| 任务 | 内容 | 证据路径 | 审查 |
| --- | --- | --- | --- |
| T1 | requirements rev1（10 条目、M0–M3、item 10 三子项） | `.mpd/cellwork/cell-20261102T050548232Z/requirements.md` | 用户/captain 通过 |
| T2 | 测得拆分 rev1（40 任务：20 work + 20 review；10 成员；5 roots；max estimate 63,700 / budget 262,144） | `.mpd/cellwork/cell-20261102T050548232Z/split/1.json` | 接受 |
| T3 | Rust 引擎层：双后端选择 + -I/-D 透传 + 批量 lint job 的 C ABI | `evidence/w1-rust-engine/` | **PASS `rec-20261102T072221-516a5b`** |
| T4 | Spike：slang-server 在 CentOS7 可运行 + elaboration 可行性 | `evidence/w2-spike/`；配方 `docs/BUILD.md §13` | **FAIL `rec-20261102T074556-a768e4` → 修复后 PASS `rec-20261102T075255-a14585`** |
| T5 | 统一跳转契约 IJumpTarget + 注册表 + 面包屑可点 | `evidence/jump/` | **PASS `rec-20261102T071117-5c71da`** |
| T6 | 工程模型：directoryOpened→根、Filelist 导入/移除、scan∪.f 并集去重、-I/-D、project-model.json 重启恢复 | `evidence/w3-project/` | **PASS `rec-20261102T081126-85c268`** |
| T7 | 层次/信号服务：实例层级 + driver/load 的 Rust 服务与 C ABI（AST-JSON 路线） | `evidence/w6-hier/`（01–08 + A-11 guard） | **PASS `rec-20261102T084733-845300`** |
| T8 | 工程级 lint 编排：job+poll、底部进度、取消/重跑、去重、失败原因 | `evidence/w4-lint/` | **PASS `rec-20261102T083952-8880e2`** |
| T9 | 诊断呈现：warn/error 分栏 + 行内波浪线 + 行双击跳转 | `evidence/d-diagnostics/` | **PASS `rec-20261102T074432-5ed24f`** |
| T10 | 设置三页签（快捷键/工具/FIM）+ 新键持久化 + api_key 安全 | `evidence/s1-settings/`（含 `KEY-CONTRACT.md`） | **PASS `rec-20261102T073727-b06160`** |
| T11 | 双后端在线切换与能力门控（Qt 侧）——“半证明” | `evidence/w5-backend/`（README、w5-probe.log、截图） | captain 09:15 接受为半证明；**T30 仍 pending** |
| T12 | 文件标签栏 + 未保存显式标记 + 窗口标题/状态栏 | `evidence/e1-tabs/` | **PASS `rec-20261102T073108-236373`** |
| T13 | 查找/替换完整化（正则/全词/大小写/选区/全部替换/计数/F3） | `evidence/e2-find/` | **PASS `rec-20261102T080652-617038`** |
| T14 | 列/块选择 + 多光标 + 编辑增强 | `evidence/e3-edit/` | **PASS `rec-20261102T085336-96d401`** |
| T17 | 工程级模块树 + 信号 source/target 一键跳转（Qt 侧；**旧 lane，已被 UX 批次 2 item 1 的纯模块层级树取代**） | `evidence/m-tree/`（REPORT.md、截图、probe） | **T37 review in_progress**；规模完整性受 T46 影响 |
| T19 | 会话恢复（窗口几何/分栏/活动标签/光标滚动/工程/面板） | `evidence/i3-session/` | captain 09:09 接受；**T39 仍 pending** |
| T43 | A-12 规模证明（1k/10k + 2k 超规模告警/取消） | `evidence/w4-lint/scale/`（`run.sh` 可复现，RUN-EXIT=0） | 无独立审查行 |
| T44 | A-12 嵌套变体（depth 5–6 + 参数传播 + generate） | `evidence/w4-lint/scale/logs/50-*`、README | captain 09:15 接受；**elaboration 段作废（见 §3.2）** |
| T46 | 修复层次服务两处 walker 缺陷（+ 第三处 expr_net） | `evidence/w6-hier/09-t46-hierarchy-fix.md`、`09-t46-gate-*`、`evidence/w4-lint/scale/logs/50-elaboration-t46.log` | captain 09:15 接受；**独立复审仍欠**（见 §5） |
| T48 | 跨会话 handoff（se-tree） | `.mpd/cellwork/cell-20261102T050548232Z/handoff/se-tree-2.md` | n/a |

已完成的 review 任务：T23、T24（FAIL→修复复审 PASS）、T25、T26、T27、T28、T29、T31、T32、T33、T34。

### 1.2 PASS 审查记录编号（验证库实际内容）

`.mpd/verify/records/` 中属于本 cell 的记录共 **12 条：11 PASS + 1 FAIL**（T24 首审 FAIL，修复后复审 PASS）。PASS 编号：

```
rec-20261102T071117-5c71da  T28 jump
rec-20261102T072221-516a5b  T3  rust engine
rec-20261102T073108-236373  T32 tabs/dirty
rec-20261102T073727-b06160  T31 settings
rec-20261102T074432-5ed24f  T29 diagnostics
rec-20261102T075255-a14585  T24 spike (re-review)
rec-20261102T080652-617038  T33 find/replace
rec-20261102T081126-85c268  T26 project model
rec-20261102T083952-8880e2  T27 lint orchestration
rec-20261102T084733-845300  T25 hierarchy service
rec-20261102T085336-96d401  T34 multicursor
```

> 注：任务单里写「13 个 PASS」，与验证库不一致（实为 11 条 PASS 记录）。另有两类 PASS 没有记录编号：① 安全复审（FIM key 0600-from-birth，mail 07:56:37 报告 PASS）；② captain 直接接受的 T11（半证明）、T17、T19、T44、T46。若下一会话需要「13」这个数，请以 `.mpd/verify/records/` 为准并核对上述两类。

## 2. 部分完成 / 未开始（确切缺口）

| 任务 | 状态 | 确切缺口 |
| --- | --- | --- |
| **T11** | completed（半证明） | **切到 verible 已证明**（`ready-ms=441`，状态行 `LSP backend=verible · lint=slang · live`）；**切到 slang 被外部客户端缺陷阻塞**：`crates/vd-lsp/src/client.rs` 的 MainLoop router 只登记 publishDiagnostics/logMessage/showMessage，slang-server v0.3.1 在 initialize 响应**之前**先发 `slang/setConfig` 通知 → mainloop 死 → “service stopped”（≈573–600 行；owner 记录为 T3/dw-rust）。第二个外部红：verible-verilog-lint 批量引擎返回 0 条诊断（`crates/vd-deeplint` / `vd-ffi` 批量路径，T8/T3）。复现：`evidence/w5-backend/w5-probe-takeover.log`。**T30 review 仍 pending**。对用户的准确说法必须是「在线切换已做到可以切到 verible；切到 slang 仍被一个客户端 bug 阻塞」，不得说成“后端切换已实现”。 |
| **T15** | pending | 未开始：编辑器右键菜单 + LSP 动作通道 + 诊断导航键（F8/Shift+F8）。owner lead-editor 已被 fenced。 |
| **T16** | pending | 仅 A-8 的 `signatureHelp` 切片写了文件但**未编译**：`src/editor/kernel/signature_help.{h,cpp}` 已写、`signature_help_selftest.cpp` 未写、未进 `src/editor/CMakeLists.txt`（未加入 `VEDIT_EDIT_CORE_SOURCES`）；其余进阶项（补全/代码动作/inlay/snippet/粘性滚动/缩进参考线等）被 T15 阻塞。证据 `evidence/e5-advanced/`（仅 ts_probe 与两个形状探针）。 |
| **T18** | pending | 未开始：i18n 运行时（语言切换 + 一键重启 + .ts/.qm 打包）。前置 T17 已完成、T20 未做。**重启契约已定**（来自 T19 owner）：`ApplicationShell::confirmUnsavedDocuments()`（已 public）→ false = 用户取消 → **中止重启、什么都不写**；true → `saveSession()` → 重启；**绝不写 `session.json`**（T19 所有）；取消时 UI 显示「语言未切换：重启已取消」；所有脏文档一个对话框。不要建立在 T19 不完整的恢复之上（A-18 已落，见 §3/§4）。 |
| **T20** | pending | 未开始：主壳集成。captain 已授权的 7 项：① 去掉 search 的本地 `IJumpTarget`/`EditorJumpTarget`（`src/workspace/search/search_types.h`），注册 `jump::Registry` target；② 修 search selftest 缓存（`src/workspace/search/search_selftest.cpp`，当前 ctest 36/37 的唯一红，A-2；不许削弱断言）；③ `global_search.cpp` 的 `-Werror=unused-parameter`；④ 面包屑挂载（`src/app/editor_area.*` 用 `vedit::breadcrumb::Bar` + 授权 CMake hunk + `pathEntries()`；`--selfcheck` 不调 `installPanels`，不能当证据）；⑤ 双窗格双标签验证；⑥ Qt ABI guard 作为 compile-only CMake target（A-11）；⑦ 不吸收已由 T19 完成的标签会话持久化。 |
| **T21** | pending | 未开始：i18n 覆盖对账（466 候选 triage + 剩余 tr() 包装 + 术语一致 + 对账报告）。被 T18/T20 阻塞（字符串必须先冻结）。T45 的部分产物已存在：`evidence/i18n/` 有可重现抽取器（`extract_i18n_candidates.py`）、triage（`07-triage-466.{json,csv}`：UI 215 / non-UI 195 / 已译 31 / obsolete 15 / uncertain 10）、`06-extractor-diff.*`。**陷阱**：很多 `*_selftest.cpp` 断言比较英文 UI 原文，翻译会打破它们。 |
| **T22** | pending | 未开始：集成验收。已定口径：`docs` 绿；`typecheck` 看两项实质检查（Qt6 `-Werror` build + `cargo clippy --workspace --all-targets -D warnings`）；`check_ffi_header_drift` 是**仓库状态检查**（committed header vs worktree header），不是代码正确性检查 → 写明“非代码检查、带原因排除”，不做补救；`tests`/`vendor`/`dist`/`pack` 对本产品不适用（带原因）；`comments`/`rows` 设计上 exit 1。**T22 含零 git 内容**（无提交计划/分支/PR）。还必须对账 fixture 漂移（captain 07:12:11）。 |
| **T45** | pending（已释放） | i18n 前哨被 captain 暂停（让位 T46）。抽取器 + triage 已落 `evidence/i18n/`；**不要在没有决定的情况下恢复**。 |
| **A-17** | 未开始 | filelist 字面 `$` 语义：`crates/vd-config/src/filelist.rs:439`（raw `$`）与 `:406`（trailing `$`）把合法路径当硬错误丢文件（fixture：`out_dollar$.sv`、`out_tail.sv$` → files=1 而非 3）；`$$` 已可转义但**用户文档没有**。要求：`$` 后不跟 `[A-Za-z_]` 视为字面 `$`；不存在的展开路径要诊断；`docs/USER_GUIDE.md` 写变量/转义/优先级；depth 限制要命名被跳过的目标。写域：`crates/vd-config/src/filelist.rs`、`project.rs`、`src/workspace/project/project_selftest.cpp`、`evidence/w3-project/**`、`docs/USER_GUIDE.md`（仅该小节）。 |
| **T30/T35/T36/T37/T38/T39/T40/T41/T42** | pending / T37 in_progress | 对应 work 的 review 未完成；T37（模块树）在 T46 之后必须重跑 lane 的 §C/D（`g_leaf[0]` 路径段）再给 verdict。 |

## 3. 已知真实缺陷与归属

### 3.1 T46 — 层次服务 walker 缺陷（已修复，待独立复审）

文件：`crates/vd-deeplint/src/hierarchy.rs`（修复前 HEAD `4e2d0a5` 行号）。

1. **只走第一个 design root** — `parse_design` **625–633**：`.find(kind == "Instance")` 对 `design.members` 只取一个；whole-project AST 每个 root 一个 Instance member，ground truth **roots=1000**，服务返回 **5**（nested）/ **11**（flat）。
2. **不进 `GenerateBlockArray`** — `walk_instance` **840–843**：只遍历直接 `Instance` 子节点；slang 把 ``generate for`` 序列化为 `GenerateBlockArray → GenerateBlock(constructIndex) → Instance`，ground truth **instance_nodes=11000**，服务返回 5-cycle 一条链。`if`/`case` generate（带 label 的 `GenerateBlock`）与 `InstanceArray` 同样盲区。
3. **第三处（修复中发现）** — `expr_net` 不解析 `ElementSelect/RangeSelect/MemberAccess/Parenthesized/Conversion`；generate 循环通常绑 `b[g]`，不归一到底层 net 时 driver endpoint 仍会丢。

修复：walk 全部 root；新增 `scope_instances`/`push_generate_blocks` 穿过 generate/instance-array 并用 slang 的层级路径（`top.u_mid.g_leaf[0].u`、`top.u_mid.g_if.u_if`、`top.u_arr[3]`）；同一列表驱动 port 连接，generate 内实例的端点落在 parent scope 的 net 上；`expr_net` 归一底层 net；按地址去重的 body 仍按“每实例展开一次”。

修复后：flat 1k whole-project **11000**、nested 1k whole-project **11000**、nested `--top` 11、nested 100/250/500 = 1100/2750/5500；回归测试 4 条 + 真实 slang fixture，`cargo test -p vd-deeplint -p vd-ffi` exit 0、clippy `-D warnings` exit 0、`cargo fmt -p vd-deeplint -- --check` exit 0。证据：`evidence/w6-hier/09-t46-hierarchy-fix.md`、`09-t46-gate-tests.txt`、`09-t46-gate-clippy-fmt.txt`。

未证明/注意：墙钟时间前后不可比（共享机、其他 agent 在构建）——**没有速度结论，只有计数**；bit 级归因只到 base net；**Qt 模块树对 `g_leaf[0]` 路径段的渲染未在 T46 中构建/点击**（T17 lane 的 §C/D 需重跑——**该 lane 已被 UX 批次 2 item 1 删除，此句作废**）。**归属**：代码已由 se-i18n 修复；独立复审由下一个 reviewer 做（不同 agent，从证据与契约出发）。

### 3.2 T44 — elaboration 数字作废（已正确标注）

`evidence/w4-lint/scale/README.md` 有 “T46 correction — the elaboration block in `logs/50-elaboration.log` is VOID” 段：旧 elaboration 数字测于 5/11 实例的坏服务上，**不得引用**。旧块保留但明确作废。**仍有效**：lint 编排数字（nested 全量 ≈5.8–6.1 s，flat 5.86–8.74 s，worst UI tick 0 ms，cancel 0 ms，pass 峰值 16.4 MiB）——批量 lint 不走 hierarchy walker。**替代数字**（`logs/50-elaboration-t46.log`）：flat 1k whole-project **11000 inst / 0.69 s / 265 MiB**；nested 1k whole-project **11000 inst / 1.34 s / 527 MiB**；nested 500 files 5500 inst / 0.51 s / 227 MiB；>10000 实例告警正确触发。注意 `run.sh` 仍内嵌 pre-T46 的 elaboration 块，重跑会覆盖 `50-elaboration.log`；README 的 VOID 段要保留到该次重跑发生。

### 3.3 T12 — 三处生命周期陷阱（已修复）

1. `setStyleSheet` → StyleChange → 递归；
2. **shell teardown 时 provider 先于其 pane 被销毁** → `--selfcheck` 在 `src/app/main.cpp:669` 析构 `ApplicationShell` 时 SIGSEGV（T5 lane 复现；一度被当成神秘崩溃）；
3. swap 时 provider 被成员赋值提前释放，pane 来不及退订。

三处均已修；`evidence/e1-tabs/` 的 `teardown.round-*` 回归探针覆盖；`--selfcheck` 不再崩。T12 的证据形态：真实 shell + 真实 KTextEditor provider，6 张截图，offscreen + xcb。

### 3.4 A-15 — 首次工具缺失失败后的“死重跑按钮”（已修复）

缺陷（T8 review 发现）：`DiagnosticsController::rerunProjectPass()` 在**首次** `resolve` 类失败后拒绝（“no project lint pass has run yet”），因为 `passRequest_` 在 backend-availability 检查**之后**才赋值；而 strip 的 Re-run 仍 enabled → 静默 no-op（最常见的首次使用失败）。修复：把 `passRequest_` 赋值提前 + fail-first（先红后绿）。证据：`evidence/w4-lint/logs/15-a15-red.log`、`15-a15-green.log`、`15-a15-ctest.log`。**归属**：se-shell 已修并报告。

### 3.5 T13 / A-14 — 误报定性（不是缺陷）

A-14 原本被当作“Ctrl+F/H/F3 在真实焦点下不工作”的缺陷。**fail-first 证明是误报**：把按键发给 `KateViewInternal`（真正接收焦点的子控件）时，**未修复的代码**也能打开查找栏，因为 Qt 会把未处理的 key press 从子控件冒泡给父 View。故 A-14 只是**加固**：把 filter 装到 widget tree 并在真实焦点控件上用 `ShortcutOverride` 认领 Ctrl+F/H/F3（复用 T14 的 `installOnWidgetTree` 思路）。证据：`evidence/e2-find/README.md` 的 “Real-focus verification (ADDENDUM A-14)” + `06-real-focus-before.log` / `07-real-focus-after.log`；captain 已接受并传播。**教训**：没有 fail-first，我们会对一个不存在的缺陷“修”一遍。

### 3.6 T11 的两个外部红（不是本 lane 的缺陷）

1. **slang 方向切换**：`crates/vd-lsp/src/client.rs` MainLoop router（≈573–600）缺乏对未知 server→client 通知的 catch-all，slang-server v0.3.1 的 `slang/setConfig` 预初始化通知把 mainloop 打死 → `service stopped`。修法方向：加 catch-all handler（忽略未知通知/请求），再用 `w5-probe` 复跑；预期把 3 个 `switch.b.*` FAIL 变绿。
2. **verible-verilog-lint 批量引擎 0 诊断**：真实 CLI 有发现，而 `vd_deeplint_batch_start(engine=1, …)` 返回 0 条（`crates/vd-deeplint` / `vd-ffi` 批量路径）。复现：`w5-probe` 的 `pass.verible-lint.diagnostics`。

另：T11 probe 期间见过 module-tree 报 “slang binary not found (tried 4 location(s))”，而文件在 `build/slang/out/slang`；T17 后来的产品探针通过，下一会话用 T17 的真实路径复核一次即可。

### 3.7 其他未决/残留

- **search selftest 缓存红**（`vedit-workspace-search-selftest`，`jump.product.second-*`）：T20 第 2 项，已知根因（测试按 pane 缓存 view/document，第二跳进了第二个 tab），不许削弱断言。
- **api_key 0600 竞态（T31 Medium，未证实修复）**：C++ 侧 key 文件创建存在 0644 窗口后才 chmod；captain 要求“0600 from birth”，T31 复审把它标为 Medium residual 并上报。下一会话应确认该窗口是否已消除（umask-000 + 负面控制测试）。
- **`check_ffi_header_drift` 门禁**：比较 committed 与 worktree 头文件；ABI 变更期间必红——**这是仓库状态/交付事实，不是代码缺陷**，T22 不得把它写成代码问题，也不得写 git 补救。
- **`src/workspace/module_tree/CMakeLists.txt:124`**：曾因 selftest 源文件尚未落盘而破坏 root configure；se-tree 已用 `if(EXISTS …)` 包裹修好，并证明 configure/build/ctest 全绿（`evidence/m-tree/30-gates.log`）。
- **`ffi_backends.cpp` 未声明成员**：T11 在飞时一度让所有引用它的 lane 无法做产品验证（T13/T14 的 `evidence/*/03-product-build.log` 记录该红，04 的 selfcheck 日志是旧二进制、**不可引用**）；captain 08:58 确认已修复。共享树里不要留半编译状态。

## 4. captain 增补令登记（A-1..A-18）

> 规则：增补令只授予**点**，不授予“邻近范围”；作者必须在路径旁引用 addendum id；reviewer 按 addenda 核对，不在冻结文本也不在本表里的路径 = FAIL。

| # | 归属任务 | 授予路径 / 内容 | 条件要点 |
| --- | --- | --- | --- |
| A-1 | T20 | `src/workspace/search/search_types.h`、`search_jump_target.{h,cpp}` | 统一到 `jump::Registry` |
| A-2 | T20 | `src/workspace/search/search_selftest.cpp` | 修测试缓存（view/document 按 pane 缓存）；断言不得削弱 |
| A-3 | T20 | `src/editor/kernel/fold_breadcrumb.{h,cpp}`、`src/workspace/search/global_search.cpp` | 面包屑/编译警告修复 |
| A-4 | T16 | `crates/vd-ffi/src/fim.rs` | FIM 请求路径消费 settings-store 的 key |
| A-5 | T16 | `src/workspace/settings/settings_integration.cpp`（一行 seam） | 必须在 T30 terminal **之后**落 |
| A-6 | T3 | `crates/vd-app/src/{slang_worker.rs,selftest.rs,diagnostics.rs}` | 关闭 workspace clippy；无 Default、无其它文件 |
| A-7 | T13 | `src/editor/kernel/find_replace_selftest.cpp` | 只新增一个文件；测试覆盖 regex/全词/大小写/选区替换/全部替换/计数/F3；旧 `findMatches`/`replaceAll` 保持薄包装；测试源不进 `evidence/` |
| A-8 | T16 | `src/editor/kernel/signature_help.{h,cpp}`、`signature_help_selftest.cpp` | 独立成对不内联进 `editor_advanced.*`；Esc 只关弹窗不吞别的；光标离开调用上下文弹窗**立即**关；逗号计数边界（嵌套调用/字符串内逗号）；无类型解析/重载选择，未知/歧义不出弹窗 |
| A-9 | T6 | filelist 预处理器：`$VAR`/`${VAR}`/`$(VAR)`/`~` + 嵌套 `-f`（相对包含它的 .f）+ 循环检测（命名两个文件）+ 深度上限 | 优先级：**项目变量表 > 系统环境**；项目表持久化、可编辑；缺失变量不得静默丢文件 |
| A-10 | T14 | `src/editor/kernel/multi_cursor_selftest.cpp` | 块选择走公共 API；断言块左列/矩形编辑；括号匹配只验证原生、不重实现 |
| A-11 | T7 | `crates/vd-ffi/tests/qt_abi_guard.{cpp,rs}` | SKIP 必须响亮（打印原因+命令）；跑树里的生成头（不是副本）；保留 Rust 单测与负面控制 |
| A-12 | T6/T8（证据） | 生成确定性 1k 文件/10k 实例 fixture（`.f` 用 `$VAR` + 嵌套 `-f`），测展开/lint/UI tick/cancel/超规模告警 | 生成器进 evidence；生成树进 `target/tmp` |
| A-13 | T7（docs） | `docs/USER_GUIDE.md` **仅 FIM API-key 段** | 事实以代码为准：`VEDIT_FIM_API_KEY` 优先；`fim_api_key` 0600 从创建起；绝不进 settings.toml/日志/UI；UI 报告 key 来源；权限不对则忽略并提示 |
| A-14 | T13 | `src/editor/kernel/find_replace.{h,cpp}`（约 10 行） | 把 filter 装到 widget tree，在 `KateViewInternal` 上用 `ShortcutOverride` 认领 Ctrl+F/H/F3；**失败测试先行**（先红后绿） |
| A-15 | T8 | `diagnostics_controller` 的 `passRequest_` 赋值提前 | 失败测试先行；重跑入口不得是静默死按钮；失败原因含工具名+阶段 |
| A-16 | T45 | i18n 前哨：可重现抽取器 + 466 候选 triage（UI/非UI/已译/废弃）+ 术语一致性 | 只以 `evidence/i18n/` 三份产出为输入，禁止自造语料 |
| A-17 | 未开始（T44 后） | `crates/vd-config/src/filelist.rs`、`project.rs`、`src/workspace/project/project_selftest.cpp`、`evidence/w3-project/**`、`docs/USER_GUIDE.md`（该小节） | `$` 后不跟标识符起始 = 字面 `$`；`$$` 保留；不存在的展开路径要诊断；depth 限制命名被跳过目标；文档写变量/转义/优先级 |
| A-18 | T19 | `src/app/editor_provider.h`、`src/editor/kernel/ktexteditor_provider.{h,cpp}`（仅 ViewState seam） | `ViewState {caretLine, caretColumn, scrollTop}` + `viewState/setViewState`；KTextEditor 用 `cursorPosition/setCursorPosition` + `firstDisplayedLine(RealLine)/setScrollPosition`；archive provider-gated |

补充：A-11 的 CMake 落地（Qt ABI guard 作为 compile-only target）由 captain 08:33 追加给 T20。

## 5. 下一步建议（依赖顺序 / 可并行 / 已证明不可行）

### 5.1 必须先做（按依赖顺序）

1. **T46 独立复审 + T17 完整性复核**（最高价值）。复审从 `evidence/w6-hier/09-t46-hierarchy-fix.md` + 两份 gate 日志出发，不读实现；然后重跑 T17 module-tree lane 的 §C/D（新的 `g_leaf[0]`/`u_arr[3]` 路径段）再给 T37 verdict。T46 已把 ground truth 从 5/11000 修到 11000/11000，但**没有第三方 verdict**。
2. **T11 解锁**：修 `crates/vd-lsp/src/client.rs` 的未知通知 catch-all → 复跑 `w5-probe`（3 个 `switch.b.*` 预期转绿）；修 `crates/vd-deeplint`/`vd-ffi` 的 verible-verilog-lint 批量路径 → `pass.verible-lint.diagnostics` 转绿；然后 T30 review。**注意**：在修好前，对用户的措辞保持“verible 方向可切、slang 方向被客户端 bug 阻塞”。
3. **T20 主壳集成**（T17 已完成，解锁）：按 §2 T20 的 7 项，一次一项；先做 search selftest A-2（让 ctest 回到 37/37），再做 jump 统一/面包屑/双窗格/Qt ABI guard。
4. **T15 → T16**（编辑器）：右键菜单/LSP 动作通道/诊断导航键 → 进阶 5 组；T16 的 signatureHelp 切片已写，先把它加进 `VEDIT_EDIT_CORE_SOURCES` 并写 selftest，再把其余项接上。
5. **T18 → T21 → T22**：T18（i18n 运行时 + 一键重启；restart 契约见 §2）需要 T20 的壳字符串冻结；T21 用 T45 的抽取器/triage（`evidence/i18n/`）；T22 最后做，且必须按 §2 T22 的门禁口径写（零 git 内容、`check_ffi_header_drift` 是非代码检查）。

### 5.2 可以并行（写域互不重叠）

- **A-17**（`crates/vd-config` + 项目 selftest + USER_GUIDE 小节）可与 T15/T16（`src/editor`）或 T20（`src/app`+`src/workspace/search`）并行；它是 Rust 侧、独立测试。
- **T45/triage 收尾**可在 T18 之前做（只动 `evidence/i18n/`），但 `tr()` 包装必须等 UI 字符串冻结。
- T46 复审与 T11 的 Rust 修复可并行（reviewer vs writer；不要同一个 agent 又写又审）。

### 5.3 已证明不可行 / 不要重试

- **预编译 slang-server 在 CentOS 7 直接跑**：实测要求 `GLIBC_2.27/2.25/2.18`，CentOS 7 只有 2.17 → 必须源码构建（`docs/BUILD.md §13`；440 s，最高 GLIBC 2.17）。
- **让 `check_ffi_header_drift` 在 ABI 变更未成为 HEAD 时变绿**：该检查比较 committed vs worktree，按定义必红；不要把它当代码信号。
- **用 QTest 打公共 `View` 来证明真实焦点行为**：真实焦点控件是 `KateViewInternal`；A-14 证明这会给出假信号。
- **在 KF6 6.24.0 里按视图关掉括号匹配高亮**：`configKeys` 不含该项，set/get 无效；只验证原生行为。
- **指望 KTextEditor 有公共语义高亮 API**：没有；行内下划线只有 `MovingRange` + `Attribute`（SpellCheckUnderline），且每行最多 1024 个属性范围。
- **tree-sitter overlay 着色已经在产品里**：`vedit_syntax_overlay` 只链进 selftest，未链进 `vedit-app`；运行期 `view.treesitter` 开关只影响面包屑解析，不影响着色。
- **非目标不要重开**：Vim 模态、DAP、WASI 插件、远程开发、minimap、which-key、Windows/macOS、系统安装包、内置 AI 默认端点、自动发现作为工程模型默认。
  （**2026-11-05 更正：“跨文件替换”已由 UX 批次 2 item 5 交付〔全工程搜索 + 替换 / 全部替换〕，不再是非目标。**）

## 6. 环境与工具链事实

### 6.1 构建与运行

- 固定镜像：`vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24.0，CentOS 7 基底）。
- 产品构建：`bash scripts/build-qt.sh`（进镜像 → cmake/ninja → ctest）→ `target/qt6-build/src/app/vedit-app`。
- **`vedit-app` 运行需要 `libtree-sitter.so.0.25`**：宿主直接跑会 exit 127；在固定镜像内跑，或 `LD_LIBRARY_PATH` 指到构建树里的 `libtree-sitter`。
- 自检：`QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc`（本轮多次 245/0，exit 0）；`--selfcheck-ffi` 用 `crates/vd-ffi/tests/fixtures/project`。
- **`--selfcheck` 不调用 `installPanels()`**（`src/app/main.cpp` 在 :1448–1450 return，installPanels 在 :1519）：它**不能**作为 lane 接线/面包屑/模块树的产品证据；需要真实 shell 的 probe。
- Rust：钉住的 1.99.0 toolchain 目录里**没有 cargo**；用 `CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable cargo … --offline`（`stable` 就是 1.99.0）。
- 规模 harness：`evidence/w4-lint/scale/scale_harness.cpp` 在固定镜像里用产品自己的静态库链接；`run.sh` 可复现（RUN-EXIT=0）。
- 探针惯例：lane probe 是独立二进制，链接产品自己的静态库（编译/链接参数从 ninja 提取，见各 `probe/build.sh`）；产物进 `target/<lane>-probe/`，`evidence/` 只放日志+截图。固定镜像只有 DejaVu，探针脚本会安装随包的 `NotoSansCJK-Regular.ttc`，否则中文状态文本是豆腐块。

### 6.2 工具链

- `slang`：`build/slang/out/slang`，版本 **12.1.0+0**（本轮所有真实测量都用它，从不用 fake backend）。
- verible：`.mpd/recon/verible/bin/{verible-verilog-ls,verible-verilog-format,verible-verilog-lint}`（M2 spike 用）。
- **slang-server**：第三方 `hudson-trading/slang-server` v0.3.1（MIT）。预编译 linux-x64 需要 GLIBC_2.27 → CentOS 7 不可跑；**源码构建**在容器内成功（440 s，最高 GLIBC 2.17，干净 centos:7 内 `--version` + `initialize` 0.189 s / 15 组能力）。配方：`docs/BUILD.md §13`；证据 `evidence/w2-spike/`。运行约束：**cwd 必须是工程根且 `rootUri` 指向它**；`.f` **条目必须相对工程根**（文件本身可在任意位置）——绝对路径会让 URI/双击解析出问题（captain 07:56 的四配置对照实验）。
- 层次/信号：T7 走 `slang --ast-json`（CLI）+ AST JSON 解析，不走 LSP 命令；实例 body 按地址去重，解析时按实例展开。

### 6.3 门禁（以本轮实测为准）

| 门禁 id / 脚本 | 本轮状态 | 说明 |
| --- | --- | --- |
| `docs` / `build/gates/docs.sh` | **绿**（A-13 后） | 文档门禁可跑 |
| `typecheck` / `build/gates/typecheck.sh` | 两项实质检查（Qt6 `-Werror` 构建 + `cargo clippy --workspace --all-targets -D warnings`）本轮绿；`check_ffi_header_drift` 必红 | drift 是仓库状态检查，不是代码检查；T6 收尾前的 vd-config clippy 红是 in-flight 造成 |
| `gates` / `build/gates/gates.sh` | 可跑；本轮红在外部 ctest | 会执行 Qt6 configure+build+ctest+xcb smoke；红点 = `jump.product.second-*`（search selftest 缓存，T20/A-2）与 T11 的两个外部红 |
| `ctest --test-dir target/qt6-build` | **36/37** | 唯一红 `vedit-workspace-search-selftest`；T11 相关 ctest 子集 9/9 绿 |
| `tests` | 不可用 | MPD ledger 固定 argv 是 `bun test packages`，可达性为 0 |
| `comments` / `rows` | 设计上 exit 1（`no_gate`） | 不要当阻塞 |
| `manifest` / `vendor` / `dist` / `pack` | 本轮未作为验收门禁运行 | 对本产品是否适用需按 `package.json` 映射与脚本重新确认；不要假设可跑 |

`cargo fmt --all -- --check` 本轮曾因 in-flight lane 红（`crates/vd-config/src/{filelist,project}.rs`、`crates/vd-ffi/src/project.rs`）；T46 的 `cargo fmt -p vd-deeplint -- --check` 已绿。

### 6.4 KTextEditor / UI 硬限制

- 行内下划线：唯一公共路径 `KTextEditor::MovingRange` + `KTextEditor::Attribute`（`SpellCheckUnderline` + `setUnderlineColor`，z-depth `-90000`），Kate 的 LSP 诊断同款；**每行最多 1024 个属性范围**，超出该行属性整体丢弃。
- 无公共语义高亮 API；无公共 renderer hook；`AnnotationModel` 只画 gutter，`TextHintProvider` 只出 tooltip，`InlineNoteProvider` 只画行内小控件。
- CJK：DejaVu Sans Mono + Noto Sans Mono CJK 回退，CJK advance = 1.60× Latin（AGENT.md §4/T119）。

## 7. 本轮流程教训

1. **写域缺口 = 18 条增补令**。冻结契约没预见到 seam 文件（search、fim.rs、settings_integration、qt ABI guard、docs 等），执行期只能一条条开 A-n。下一轮拆分应把已知接缝文件**预先显式授予**，或把 addendum 做成最小的“点授予”清单（本轮规则是对的：点，不是邻近范围；作者须在路径旁引用 id）。
2. **共享树编译中断是最贵的协作税**。T6 在飞时三次打断 typecheck（project.rs clippy、project_service.cpp `.append`）；T11 的 `ffi_backends.cpp` 未声明成员让 T13/T14 的产品验证无法做；`module_tree/CMakeLists.txt:124` 引用未落盘的 selftest 源直接打断 root configure。**规矩**：每次落盘保持树可编译；CMake 用 `if(EXISTS …)` 护住“未完全落地”的 lane；不要用旧 selfcheck 二进制冒充当前树。
3. **失败测试先行能拆掉假缺陷**。A-14 的“真实焦点缺陷”在 fail-first 下没复现——不是 bug；没有这一步，我们会“修”一个不存在的问题并把误报写进历史。
4. **ground truth 对账是规模功能的唯一可信判据**。T44 用 slang AST 计数（1000 roots / 11000 instances）对账服务输出（5 / 11），才挖出 T46 两个 walker 缺陷；只看“没崩、有输出”会放过 99.9% 数据丢失。
5. **自检有盲区**：`--selfcheck` 不跑 `installPanels`；QTest 打公共 `View` 而不是 `KateViewInternal`。两者都能让坏产品看起来绿。产品级证据必须用真实 shell/provider 的 probe。
6. **fencing 会终结一个 cell**：5 个写席被 compaction barrier fenced，reseed 因“subagent creation requires the exact live parent agent”失败，只剩 3 个写席，用户决定换会话。**handoff 要在 checkpoint 前写**，且要写“当前真实状态 + 下一步 + 复现命令”，不是愿望。
7. **用户的真实 `.f` 形态来得晚**（`$VAR`、嵌套 `-f`）→ A-9 是执行中才补的；下一轮应在需求阶段就向用户要样例 `.f` 并冻结语义。
8. **生成头文件 + Qt 宏是地雷**：`vd_ffi.h` 的 `signals` 字段被 `<QObject>` 的 `#define signals public` 打死所有 Qt TU；A-11 的 guard（真实 `<QObject>` + 负面控制）是必须保留的资产；`capi.rs`/`vd_ffi.h` 同一时间只能有一个写者。
9. **i18n 产物是输入，不是可自由发挥的翻译**：`evidence/i18n/` 的 466 候选/术语表/对照表必须复用；T45 的 triage（UI 215 / non-UI 195 / 已译 31 / obsolete 15 / uncertain 10）是下一会话的起点；翻译前先冻结 UI 字符串，并注意 selftest 断言比较英文原文的陷阱。
10. **不要在 cell 里碰 git**：用户裁定 cell 不管理 git；T22/交接文档都不写提交、分支、PR、rebase。

## 8. 接手第一步（checklist）

```bash
cd /home/haroldzhao/MyProj/DshProj/VEdit
docker images | grep vedit-qt6-build                      # 固定镜像必须在
bash scripts/build-qt.sh --no-test                        # Rust 层 + Qt6 产品构建
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/sc   # 期望 exit 0 / 245 checks
ctest --test-dir target/qt6-build                         # 期望 36/37；红=search selftest（T20/A-2）
bash evidence/e1-tabs/probe/run.sh                        # T12 探针（offscreen + xcb，0 failures）
bash evidence/i3-session/probe/run.sh                     # T19 探针（0 failures）
# T46 复审：读 evidence/w6-hier/09-t46-hierarchy-fix.md + 09-t46-gate-*.txt，跑：
CARGO_HOME=$PWD/target/cargo-home RUSTUP_TOOLCHAIN=stable cargo test --offline -p vd-deeplint -p vd-ffi
# 然后重跑 T17 lane 的 §C/D（g_leaf[0]/u_arr[3] 路径段）再给 T37 verdict
```

若需要真实 shell 的产品证据：按 `evidence/*/probe/build.sh` 的模式链接产品静态库（`target/qt6-build/src/app/...`），在 `vedit-qt6-build:local` 里跑，不要用 `--selfcheck` 代替。
