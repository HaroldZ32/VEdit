## 🛑 人工停点 1 — 待用户在有屏机器上验收（未合并 / 未打 tag / 未发 Release）

本 PR 交付 **v2 迁移的 Slice 1**。此刻分支处于**冻结状态**：工作树 clean，门禁全绿，等待用户 `git pull` 后亲手验收。
**未合并、未打 tag、未发 Release**；交付出口就是这个 PR。

### 门禁（冻结树 `09907ae`，真实输出）
| 门禁 | 命令 | 结果 |
|---|---|---|
| typecheck | `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`** → `[typecheck] PASS (pinned image vedit-build:local)`（真的在 CentOS 7 / rustc 1.99.0 容器里跑，容器横幅可查） |
| 构建 + 测试 + 冒烟 | `bash scripts/build-qt.sh` | **`BUILD-QT-EXIT=0`**，ctest **`100% tests passed, 0 tests failed out of 16`**，xcb/软件渲染 `M0-SMOKE ok=1` |

> 诚实记录：本停点第一次跑门禁时是红的（`BUILD-QT-EXIT=8`，ctest 15/16，唯一失败 `vedit-workspace-search-selftest` 7 条 `jump.product.*`）。根因是**构建竞态**：构建窗口 `19:51:37→19:52:17`，而 T69 的 `search_selftest.cpp` mtime 是 `19:52:24` —— 编译进来的是一份中间态。等 T69 收敛后重跑即 16/16。两次记录都在 `evidence/slice1-stop/{12-build-qt.log,13-ctest-red.txt,22-build-qt-final.log,23-gates-summary.txt}`。

### Slice 1 交付内容（按 lane）
| lane | 交付物 | 独立评审 |
|---|---|---|
| **T10 编辑内核** | `src/editor/kernel/**` + `src/editor/CMakeLists.txt`，ctest `vedit-edit-core-selftest` | `evidence/verdicts/edit-core/` |
| **T47 终端 + 会话恢复** | `src/terminal/**`，ctest `vedit-terminal-selftest` | `evidence/verdicts/fix-shell-terminal/` |
| **T71 保存（Ctrl+S / 菜单）** | `src/app/app_shell.{h,cpp}`、`main.cpp`（与 T47 双 lane 共写，见豁免表） | `evidence/verdicts/save-binding/` |
| **T6 工程模型 + `.f`** | `crates/vd-config/**`、`src/workspace/project/**` | `evidence/verdicts/project-model/` |
| **T8 Verible LSP** | `crates/vd-lsp/**`、`src/workspace/lsp` 接缝 | `evidence/verdicts/lsp/{t67-verdict.md,t73-verdict.md}` |
| **T12 语法高亮** | `src/syntax/**`、`resources/syntax/vedit-*.xml` | `evidence/verdicts/syntax/` |
| **T69 全局搜索 + 跳转** | `src/workspace/search/**` | `evidence/verdicts/search-uaf/`、`evidence/verdicts/fix-search-jump/` |
| **T59 诊断** | `src/workspace/diagnostics/**` | `evidence/verdicts/`（diagnostics） |
| **T39/T40 filetree** | `src/workspace/filetree/**` | `evidence/verdicts/fix-filetree-open/`、`fix-filetree-provider/` |
| **T54/T55 门禁基础设施** | `build/gates/{typecheck,gates}.sh` | `evidence/verdicts/gate-infra/verdict.md` |
| **T57 仓库卫生** | `.gitignore`、`src/workspace/filetree/tools/generate-testdata.sh` | `evidence/verdicts/repo-hygiene/verdict.md` |

### 证据索引（都在本 PR 里）
- `evidence/slice1-stop/` —— 停点 1 专属：冻结树 C9 头、两次门禁原始输出、ctest 红/绿对照、未验证项
- `evidence/verdicts/**` —— 每个 lane 的独立评审 verdict
- `evidence/captain-exemptions.md` —— **7 条写域豁免登记** + `src/app/{app_shell.h,app_shell.cpp,main.cpp}` 的 T47+T71 双 lane 共写提示，verifier 判读前请先读这张表
- `evidence/{ffi-abi,highlight-v2,docs-amendments,fix-filetree-open}/` 里的 `*-recollect-*` / `*-backfill-*` —— C9 补记。**原始 `collected_at` / `git_rev_parse_HEAD` 一格未改**，补记是独立文件
- `evidence/ffi-abi/**`、`evidence/m0-build/build-logs/**` —— Slice 0 与构建的原始日志（会被后续构建重写，本 PR 里是冻结时刻的内容）

### 用户验收步骤（只有他能做）
```bash
git pull                      # 在本分支
# 构建（见 AGENT.md §4 / docs/BUILD.md）
bash scripts/build-qt.sh
# 打开一个真实工程目录
vedit-app --project <真实工程目录>
```
然后：**双击打开 3 个文件** → **改一行** → **Ctrl+S** → **看诊断是否出现**。
执行服务器**没有显示服务器**，以上没有任何一步能被 agent 代劳；本 PR 里的所有"绿"都只覆盖**无头可证**的部分。

### 明确的边界
- **GUI 一律未验收**：窗口交互、高亮观感、折叠/面包屑、面板手感。
- `M0-SMOKE ok=1` 是 **xcb + 软件渲染**的无头冒烟，不等于人眼验收。
- 未跑全量 MPD 门禁（`rows`/`comments` 是仓库声明式 N/A→RED，与本 PR 无关）。

### 本轮提交（`95c496e..09907ae5e69fcff6a646823619ce80b506d7549b`，共 13 个）

```
5f22bd3 feat(editor): land the KTextEditor editing kernel lane
22e5ef8 feat(terminal): land the pty terminal lane and its selftest
e7445f6 feat(app): wire the shell for session restore and save bindings
a058995 feat(workspace): land the project, diagnostics, search and filetree lanes
1ed2f9f feat(crates): land the rust service crates and the cbindgen header
0584d1e feat(syntax): land the vedit verilog/systemverilog grammar data
8fa1819 build(packaging): land the appdir, tarball and clean-room packaging scripts
5098bbf build(gates): land the gate rewrite and the container build drivers
00128af docs: land the user guide split, license decision and amendments
c86ce6c docs(evidence): land the independent verdict set
cfe708b docs(evidence): record captain exemptions and the two reconstructed PASS verdicts
e8c67a2 docs(evidence): land the remaining lane evidence
09907ae docs(evidence): backfill C9 headers on the frozen tree and record the gate results
```

---

## 🔁 GO-3 更新 —— 用户报的三条真机缺陷已修完（待用户复验）

| 缺陷 | 根因 | 修法 | 红→绿对照 |
|---|---|---|---|
| **T80 Ctrl+S 无反应 / 歧义** | `action-file.save`（QMainWindow, context=1）与 KTextEditor 视图自带的 `file_save`（parentless KActionCollection, context=3）同绑 Ctrl+S | 装 editor widget 时清掉编辑器自身（含子树）的 Ctrl+S，`action-file.save` 保留为唯一权威；保存链路不变 | 修前 `--selfcheck` 真跑出 `QAction::event: Ambiguous shortcut overload: Ctrl+S`、聚焦后 Ctrl+S 磁盘不变；修后 **bindings 4→1**，新增 `save.shortcut.{unique,authority,unique.multi-pane}` 三条断言 |
| **T81 真 KTextEditor 下无高亮** | 文档从不选择语法模式（打开只 `setText()`，不设 URL、不调 `setHighlightingMode`） | 新增 `src/editor/kernel/highlight_mode.{h,cpp}`，打开时选择模式 | 修后 `highlightingMode=Verilog/SystemVerilog`、author=`VEdit`、**formats=30/29**、folding begins=2/1；`resources/syntax/*.xml` 未动（sha256 不变，T45/T66 无需更新） |
| **T82 工具链不随包 / 缺工具不可见** | assemble-appdir 只装 vd-app，不装 verible/slang | `--toolchain-verible/--toolchain-slang/--require-toolchain`，装 `bin/verible-verilog-{ls,lint,format}` + `bin/slang`，逐个 sha256 对 pin 校验，写 `usr/share/vedit/toolchain.txt`；发现顺序 bundled > PATH > env 且可观测 | 干净容器两路验收：bundled 时真出 `line-length` 诊断；移走后 `Diagnostics unavailable … Impact … Fix …`，不崩溃不伪造 |

### 门禁（GO-3 冻结树，真实输出）
```
TYPECHECK-EXIT=0        [typecheck] PASS (pinned image vedit-build:local)
BUILD-QT-EXIT=0         ctest: 100% tests passed, 0 tests failed out of 17
```
一次通过，未出现 relink 竞态；原始日志 `evidence/slice1-stop/{31,32}-*.log`、汇总 `33-go3-gates.txt`。

### 多 lane 共写的文件（判读提示）
| 路径 | 承载 |
|---|---|
| `src/app/main.cpp` | **T80**（绑定枚举 helper + 3 条新断言）+ **T82**（runSelfCheck 打印 TOOLCHAIN） |
| `src/app/app_shell.{h,cpp}` | **T71**（保存绑定/菜单）+ **T82**（`addShellDiagnostic`） |

### 本轮提交
```
2193f7e fix(editor): select the syntax mode so KTextEditor actually highlights
2454362 feat(diagnostics): discover bundled toolchain and surface a missing one explicitly
169fb74 fix(app): make Ctrl+S single-authority and print the toolchain in the selfcheck
c42d745 feat(packaging): bundle verible and slang into the appdir with pinned sha256
144fa55 docs(legal): register Verible (Apache-2.0) and slang (MIT) in the notices
ad05938 docs: name the v2 deliverable build entry in AGENT.md
a6d5a04 test(editor): extend the edit-core selftest for the highlighting assertions
9a11c91 docs: sync the build and usage documentation
07f3b63 docs(evidence): land the T80/T81/T82 lane evidence
5709520 docs(evidence): record the GO-3 frozen gate results
```

### 未验证项（如实）
- **GUI 一律未验收**：高亮观感、Ctrl+S 的实际效果、lint 诊断在界面上的呈现，只能由用户真机确认。门禁只覆盖无头可证部分（构建 / ctest / xcb 软件渲染冒烟）。
- `build/glibc217-spike/run-spike.sh` 是 T76 探针产物（untracked），本轮未纳入提交。

---

## 🔁 GO-4 更新 —— T84（打包形态语法定义丢失）与 T85（工具链缺失刷屏）已修

| 缺陷 | 根因 | 修法 | 红→绿对照 |
|---|---|---|---|
| **T84：打包形态下语法定义丢失** | `VEDIT_SYNTAX_DIR` 指向 `…/syntax` **子目录**，而产品契约要求的是**数据根** | `packaging/runtime/AppRun` 与 centos7 验证脚本改为数据根；`src/syntax/kde/ksyntax_data.cpp` **容忍两种取值**（子目录归一化到父目录），防下一个打包者再踩 | tarball 在干净 centos:7 上 `highlight.verilog-definition` **FAIL → PASS**；`SUMMARY PASS=15 FAIL=1 SKIP=2`（唯一 FAIL 是预期的 `kernel.is-3.10`） |
| **T85：缺工具时刷屏** | 一条 **1289 字符**的超长消息（20 条候选探测路径塞一行），且 `qWarning` + `addShellDiagnostic` **各输出一次** | 改为**一条 446 字符单行**（缺什么 / 影响什么 / 三条修复路径）；候选清单移入 `probeDetail()`，仅 `VEDIT_TOOLCHAIN_TRACE=1` 时输出；面板 1 条状态、outline 0 行 + 一行空状态 | 新增 `vedit-toolchain-ux-selftest`（42 checks / 0 failures）；新增发现链 **bundled → local → PATH → env**（local 从二进制向上找 checkout 根，探测 `build/slang/out`、`.mpd/recon/verible/bin`、`target/verible-pin/extract/verible-*/bin`），裸构建现在直接 `source=verible:local slang:local missing=[]`，真 Verible 会话产出 **5 个 outline 符号** |

### 门禁（GO-4 冻结树，真实输出）
```
TYPECHECK-EXIT=0        [typecheck] PASS (pinned image vedit-build:local)
BUILD-QT-EXIT=0         ctest: 100% tests passed, 0 tests failed out of 18
```
原始日志 `evidence/slice1-stop/{41-typecheck-go4.log,42-build-qt-go4.log}`。

### 本轮提交
```
0ed4915 fix(syntax): accept both the syntax dir and its parent for KDE data
2e12136 fix(packaging): point AppRun at the KDE data root, not the syntax subdir
2ce8bbb build(centos7): land the clean-room verify scripts
fd78844 fix(diagnostics): one concise status line and a local-checkout discovery source
1bda664 docs: document the runtime toolchain discovery order
c755d9c docs(evidence): point the T83 snapshot at its T84 successor
73f6c8c docs(evidence): land the T84/T85 evidence and the GO-4 gate logs
1c03f80 docs(evidence): refresh the operational build logs
8856ed8 docs(evidence): land the T83 clean-room console, container report and tarball checksums
```

### 不入库的构建产物（显式跳过）
| 路径 | 大小 | 原因 |
|---|---|---|
| `build/centos7-verify/dist/*.tar.gz` | 108 MB | `.gitignore` 的 `*.tar.gz` 已覆盖；**只提交其 `.sha256`** |
| `build/centos7-verify/AppDir/` | 287 MB | 打包产物（staged AppDir） |
| `build/centos7-verify/stage/` | 287 MB | 打包暂存目录 |
| `build/centos7-verify/samples/` | 几 KB | 由 `build-tarball.sh` 生成 |
| `build/glibc217-spike/` | 12 KB | T76 探针产物 |

> ⚠️ 仓库卫生建议（供后续任务）：`build/centos7-verify/{AppDir,stage,samples}/` **没有被 `.gitignore` 覆盖**，只有 `*.tar.gz` 被忽略。本 PR 用显式路径避开了它们，但 `git add -A` 的使用者会一次性提交约 574 MB 二进制。建议补一条 `build/centos7-verify/{AppDir,stage,samples}/` 忽略规则。

### 历史证据的处理（不改成假绿）
`evidence/centos7-verify-package/README.md` 是 T83 当时的历史快照，其「`highlight.verilog-definition` FAIL」的结论**保持原文不改**；顶部新增一段「更新：已被 T84 取代，见 `evidence/appdir-syntax-dir/`」的指引。

### 未验证项（如实）
- **GUI 一律未验收**：高亮是否在界面上正确呈现、工具链状态行在面板里的实际观感，只能由用户真机确认。
- T84 的红→绿是在**干净 centos:7 容器**里跑的，容器共用宿主内核（7.0.0），`kernel.is-3.10` 仍是预期 FAIL —— 真机内核未验。
