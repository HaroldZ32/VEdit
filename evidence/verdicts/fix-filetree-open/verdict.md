# T40 独立评审结论 — T39 `fix-filetree-open`（filetree 双击真正打开文件 · H1/High 返修）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z` 的 Reviewer 实例；verifier ≠ writer `e1acec86…`）
- **被评审任务**：board `T39`（kind=work，owner=Senior Engineer，status=completed；改动已被 checkpoint 提交 `6e6283e`）
- **冻结契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-filetree-open.md`（split revision 3）
- **验证环**：`loop-20261031T101645-f4b9e5`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（bound seat；契约判据 1/3/4 满足；判据 2 在"本改动的门禁"层面满足，见 §2.2 的树级红解释）
- **非阻塞发现**：1 条 Low（F-1，C9 证据头）；1 条集成提示（树级 gate 红，外部 lane 在飞，已逐项归因）
- **证据目录**：`evidence/verdicts/fix-filetree-open/`（本文件 + 01/02/03/03b/03c/04/05）

> 记录顺序声明：结论先由"契约 + 文档 + 门禁/自检复跑"形成；我只在**契约明确要求审计的项**上读了
> 测试与 diff（判据 ③"不得删除/弱化既有断言"、判据 ④"不改测试迁就实现"——这两条不读测试无法核验），
> 且这两项的结论是客观的文本比对，不是"看实现顺手过"。没有读 writer 的推理并据此调判。

---

## 1. 我实际跑的证据（命令 + 观测）

### 1.1 绿：本改动自己的门禁（我独立复跑，不用 writer 的日志）

`selfcheck/lane`：独立构建 + ctest + 真实 `ApplicationShell` 的 seamcheck（容器内 `/tmp` 构建，不写工作区）

```text
$ bash evidence/verdicts/fix-filetree-open/run-review-seamcheck.sh green
== 1/4 lane standalone build ==        ... (ninja, ok)
== 2/4 lane ctest (111-check suite) ==
100% tests passed, 0 tests failed out of 1
== 3/4 fixture ==                      PREPARE ok=1 root=/tmp/fx project_files=7 big_files=200
== 4/4 root-context seamcheck (REAL ApplicationShell) ==
SEAM CHECK dblclick.file-loads-bytes ok=1 detail=37 bytes on disk
SEAM CHECK dblclick.file-records-path ok=1 detail=/tmp/fx/project/rtl/top.v
SEAM CHECK dblclick.file-status-message ok=1 detail=Opened /tmp/fx/project/rtl/top.v
SEAM CHECK dblclick.second-file-loads ok=1
SEAM CHECK dblclick.second-file-records-path ok=1 detail=/tmp/fx/project/rtl/alu.sv
SEAM CHECK dblclick.folder-expands ok=1
SEAM CHECK dblclick.folder-emits-no-open ok=1 detail=0
SEAM CHECK dblclick.folder-keeps-editor ok=1
FILETREE-SEAMCHECK ok=1 checks=15 failures=0
SEAMCHECK green exit=0
EXIT=0
```

### 1.2 红：我独立做的**证伪**（证明自检不是"只断言信号"）

把 `filetree_integration.{h,cpp}` 还原到修复前提交 `9957f5f`（副本 `src/`，**不碰工作区**），
同一套 15 条断言重跑：

```text
SEAM CHECK dblclick.file-loads-bytes ok=0 detail=37 bytes on disk
SEAM CHECK dblclick.file-records-path ok=0
SEAM CHECK dblclick.file-status-message ok=0 detail=Open /tmp/fx/project/rtl/top.v
SEAM CHECK dblclick.second-file-loads ok=0
SEAM CHECK dblclick.folder-keeps-editor ok=0
FILETREE-SEAMCHECK ok=0 checks=15 failures=6
SEAMCHECK before exit=1
```

→ 修复前 6 条红、修复后 15/15 绿：**断言确实绑在真实编辑器状态上**（内容字节/路径属性），
不是"信号发出即绿"；且文件夹两条路径（`folder-expands ok=1`、`folder-emits-no-open ok=1`）在修复前后都绿，
说明"双击文件夹只展开"没有被改动。

### 1.3 约束审计（契约 ③④：不删断言、不改测试迁就实现）

```text
$ git diff 9957f5f 6e6283e -- src/workspace/filetree/tests/filetree_tests.cpp
（唯一改动：+5 行注释，说明面板级套件无 shell、产品级行为在 seamcheck 断言；无任何断言行变动）
check(...) 调用数：修复前 136 / 现在 136（111 条执行的 CHECK 不变）
$ git diff 9957f5f 6e6283e -- src/workspace/filetree/tools/rootcheck/seamcheck.cpp | grep '^-'
- 旧 harness 的单条 programmatic `activateIndex` + 状态栏 `"Open "` 断言与其单一 ok 汇总
+ 新 harness 的 15 条断言（其中 `panel-installed`/`root-path` 原样保留；状态栏断言保留为
  `dblclick.file-status-message`，期望文本随真实行为由 `Open ` 改为 `Opened `；新增内容/路径/第二条文件/文件夹三条断言）
```

结论：既有断言**没有被删除或弱化**（名称保留、断言数从 1 组汇总扩到 15 条真实断言），
`tests/filetree_tests.cpp` 只加注释、零断言改动 —— 约束 ③④ 成立。

### 1.4 证据与代码版本对得上

`06-source-sha256.txt` 的 5 个 sha256 与 checkpoint 提交 `6e6283e` 的树**逐字节一致**
（`filetree_integration.cpp 43da23c2… / .h 2230cfab… / seamcheck.cpp 02de30fa… / CMakeLists 8e7d6f52… / filetree_tests.cpp ba9f3993…`）；
`CMakeLists.txt` 的 `Qt6::Test` 增加确在提交里（`git diff 9957f5f 6e6283e` 可见），
`git status` 现在显示干净是因为 captain 的 checkpoint 已提交（`6e6283e feat(filetree): checkpoint filetree double-click open fix (T39 complete)`）。

### 1.5 写域

T39 的改动集（对比 `9957f5f`）= `filetree_integration.{h,cpp}`、`tests/filetree_tests.cpp`（仅注释）、
`tools/rootcheck/seamcheck.cpp`、`tools/rootcheck/CMakeLists.txt` + `evidence/fix-filetree-open/` ——
全部在 `src/workspace/filetree/` 与 `evidence/fix-filetree-open/` 内；`src/workspace/integration.*` 在写域内但未改（允许）。
无测试删除、无门禁脚本改动。

### 1.6 门禁（判据 2）—— 逐项归因

| 门禁 | 观测 | 归因 |
|---|---|---|
| ledger gate `gates`（`bun run verify:gates`，loop 绑定证据 `ev-20261031T104444-100b53`） | **exit 1**：`scripts/gates.sh` 的 `cargo fmt --check` 报 `crates/vd-ffi/src/lsp.rs`、`crates/vd-lsp/src/session.rs`、`crates/vd-lsp/tests/*`、`crates/vd-search/src/lib.rs` 的格式差异 | 全是**并发在飞的 Rust lane**（T6/T8/T21 的未提交工作），**不在 T39 写域**；gate 日志里 `filetree` 出现 0 次 |
| `build/gates/qt6-cxx.sh`（仓库声明的 Qt6 lane gate）run #1 | exit 1：读到 `vedit-app: empty` → `FATAL: vedit-app does not export vd_abi_version` | **并发构建竞态**：另一 lane 正在同一 `target/qt6-build` 重链 `vedit-app`；该次运行后二进制恢复为 64,819,872 B（18:54:48） |
| 同上 run #2 | exit 1：`src/workspace/search/search_selftest.cpp:220 clickHitRow defined but not used [-Werror]` | **T41（搜索跳转返修，in progress）** 正在编辑该文件；30 秒后该函数已被使用 |
| 同上 run #3 | exit 1：`undefined reference to vedit::app::EditorProvider::{openFile,currentFilePath,saveFile}`（`src/app/libvedit_app_shell.a(editor_provider.cpp.o)`） | **T10（编辑内核进产品，in progress）** 的 `EditorProvider` 改到一半；3 次运行里 `filetree` 相关错误 0 条 |
| 本改动自己的门禁 | 见 1.1/1.2：lane ctest 1/1 绿；seamcheck 15/15 绿；修复前证伪 6 红 | ✅ |
| writer 的整树门禁记录（我读、未采信） | `05-gate-qt6.log`：`[qt6] PASS` + `GATE-EXIT=0`（18:30，修复已在树里） | 与我的 lane 级复跑一致 |

**判据 2 的诚实表述**：本改动的门禁（lane build + ctest + 真实 shell 自检）**通过**；
整树 `gates`/Qt6 lane gate 在评审时刻是**红的**，但每一处红都被归因到**并发在飞的其它 lane**（证据见上表与
`04-gates-and-attribution.txt`），**没有一处指向 `src/workspace/filetree`**。整树门禁必须在 T36 人工停点
（并发 lane 落地后）重跑，不能把这次红算在 T39 头上；我**没有**认证整树门禁为绿。

---

## 2. 契约逐条对照

| 契约要求 | 证据 | 结果 |
|---|---|---|
| 双击文件 → 编辑器真的打开（内容断言） | 1.1：`dblclick.file-loads-bytes ok=1 detail=37 bytes on disk`（与磁盘字节比对）、`second-file-loads ok=1` | ✅ |
| 断言**真实产品行为**（active pane 的内容/路径，不只信号） | 1.1 路径属性 `vedit.openFilePath` == 被双击路径；1.2 证伪显示修复前同一断言红 | ✅ |
| ① 双击文件夹**只展开**行为保持 | 1.1/1.2：`folder-expands ok=1`、`folder-emits-no-open ok=1 detail=0`、`folder-keeps-editor ok=1`（修复后） | ✅ |
| ③ 不删/不弱化既有断言、不改判据 | 1.3：名称保留、断言扩展为 15 条；`tests/filetree_tests.cpp` 注释-only（136/136） | ✅ |
| ④ 不改测试迁就实现 | 1.3：唯一测试文件改动是注释；新增断言在允许的 `tools/rootcheck/` 内 | ✅ |
| 具名 open seam / demo 路径（契约指定方向） | `filetree_integration.h` 新增 `openFileInEditor(...)`，实现走 `editorArea→activePane→setPlainText`（与 T22 同路径；调用点稳定，edit-core 后可替换） | ✅ |
| 证据 `evidence/fix-filetree-open/`（失败前/后对照、selftest、两条路径） | 01/02/03/04/06 齐备；我独立复跑并复现了 before（6 红）与 after（15 绿） | ✅（C9 完整性见 F-1） |
| 写域 | 1.5 | ✅ |

---

## 3. 发现（findings）

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low** | C9 证据头：`01-before-red.log`、`02-after-green.log`、`03-two-paths.txt`、`04-lane-tests-full.log`、`04b-lane-tests-after-comment.log`、`05-gate-qt6.log` 的头部只有采集时间/命令，没有 `git rev-parse HEAD` 与 `git status --porcelain`；只有 `00-pins.txt` 带齐三项，`06-source-sha256.txt` 带 date+head | 按 C9 每份证据文件头都带"采集时间 + HEAD + 树状态"（尤其 01/02 生成于 18:28，晚于 00-pins 的 18:25，树状态未随之固定） | `.mpd/cellwork/cell-20261031T085543161Z/requirements.md` **C9 证据规则**（rev 3 第 76 行）；`docs/handover/05-build-and-verify.md` §2.2；实测各文件头 |

**非缺陷说明（已核、不留 finding）**：
- 状态栏断言由旧 harness 的 `message contains "Open "` 改为 `dblclick.file-status-message detail=Opened …`：
  这是"随真实行为改写并保留"（契约 ③ 要求的正是"改成断言真实行为"），且同时新增了内容/路径断言，**不是弱化**。
- writer 报告 §2 声称改了 `tools/rootcheck/CMakeLists.txt`（加 `Qt6::Test`）：**属实**（`git diff 9957f5f 6e6283e` 可见；
  当前 `git status` 干净是因为 checkpoint 提交了它）。
- T39 的改动已被 checkpoint 提交（`6e6283e`），证据 pin 与提交树逐字节一致；这会影响"未提交即未完成"的直觉，但不影响判据。

---

## 4. 我无法验证的部分（诚实声明）

1. **GUI 视觉/真人交互**：本机无显示服务器；seamcheck 用 `QT_QPA_PLATFORM=offscreen` + `QTest::mouseDClick`
   合成双击，属 L2 证据；"用户在有屏机器上双击真的打开"仍需用户在人工停点验证。
2. **整树门禁为绿**：评审时段内整树 `gates` 与 Qt6 lane gate 均红（外部 lane 在飞，见 §1.6）；我**未认证**整树绿，
   必须由 T36 在并发 lane 落地后重跑。
3. **真实 KTextEditor 文档打开**：当前打开落到 shell 的 demo 编辑器（`QPlainTextEdit` + `vedit.openFilePath`）；
   契约允许该 demo 路径（与 T22 相同），edit-core 的真实 provider 属后续 lane —— 本 verdict 只背书"双击→产品里
   真的载入内容 + 路径可断言"这条契约，不背书 KTextEditor 文档模型。
4. **失败分支**（missing/超大/不可读/二进制）与二进制/大文件边界：seamcheck 未覆盖；不在 H1 判据内。
5. **根 ctest（12/12）**：我没能在评审窗口内独立重跑根 ctest（三次 Qt6 gate 都在 build 阶段被其它 lane 打断）；
   仓库自己的 `33-qt6-ctest.log`（18:39）显示 12/12 绿。

---

## 5. 结论

**PASS**。H1 的根因（只接状态栏文案、产品里不打开）被一个具名 seam 修掉：真实 `ApplicationShell` 里双击文件后
编辑器文本 == 磁盘字节、`vedit.openFilePath` == 被双击路径、第二个文件替换内容；双击文件夹仍然只展开、不触发打开、
不动编辑器。自检是**可证伪的**（我独立还原修复前代码，同一套断言 6 红），既有断言没有被删弱（136/136，
测试文件注释-only）。写域干净，source pin 与提交一致。

**给 captain 的两条**：
1. **F-1**（Low）：把 01/02/03/04/04b/05 的证据头补上 HEAD + `git status --porcelain`（可并入 T43 的 C9 批量返修）。
2. **整树门禁**：评审时段的红全部来自并发 lane（`crates/vd-{ffi,lsp,search}` 的 rustfmt、T41 的
   `search_selftest.cpp` 未用函数、T10 的 `EditorProvider` 未定义符号、一次 `vedit-app` 重链竞态）。
   T36 前必须让这些 lane 落地并重跑 `bash build/gates/qt6-cxx.sh` / `bun run verify:gates`，拿到绿再 push。

**引用来源（mpd_verify_record.sources）**：本文件、冻结契约 `.mpd/.../contracts/fix-filetree-open.md`、
`evidence/verdicts/ws-filetree/verdict.md`（H1 原始判据）、`.mpd/.../requirements.md`（C8/C9/C10/AC-0.6）、
`docs/handover/05-build-and-verify.md`（§1 命令、§2 取证规则、§4 门禁语义）、
`docs/handover/02-status-matrix.md`、`evidence/fix-filetree-open/{00,01,02,03,04,05,06,REPORT}`、
`evidence/verdicts/fix-filetree-open/{01..05}`、`git` 提交 `6e6283e`/`9957f5f`。
