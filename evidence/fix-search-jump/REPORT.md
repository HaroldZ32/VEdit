# T41 返修报告 — 搜索结果点击真正跳转（定位到命中行）（H1 / High）

- Cell: `cell-20261031T085543161Z` · Board task: **T41** · 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-search-jump.md`
- 修复对象: `evidence/verdicts/ws-search/verdict.md` §2 **H1**（A2c 的“点击结果跳转到对应文件与行”在产品里没有消费者）
- 执行: **Deep Worker (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）
- 代码基线: RED 采集 `HEAD = 8fedbdd`；GREEN/回归采集 `HEAD = 1f47e56`（分支 `feat/qt6-ktexteditor-migration`，多人共享工作树，期间有其他 lane 的提交）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（`sha256:26287a352e136a5ade5484e8a073f8062a2a9ab46526fd564f54718b91c12af6`，Qt 6.10.2）
- 结论: **H1 已修复，headless 验证通过**（RED → GREEN 同一 harness 对照）。点击命中后，**产品里真的打开该文件，并把光标放到命中行/列**；断言读的是 shell 编辑器的真实状态（文档文本、光标行号/列号、`vedit.openFilePath`、状态栏），不是信号。本机没有显示服务器，全部结论来自构建、自检、ctest 与真实 `ApplicationShell` 内的状态断言；没有“看起来正确”这类判断。

---

## 1. 根因（复现，不是猜测）

修复前 `installSearch()` 只 `setFactory`，全仓没有 `setJumpTarget` 调用方：`SearchPanel::activateHit` 里
`emit jumpRequested(request)` 之后 `jumpTarget_ == nullptr`，于是点击只发信号、**文件不打开、行不定位**。
自检里的 `DemoJumpTarget` 是测试夹具，不是产品路径。

先用同一 harness 把**产品级断言**写进自检（此时实现未改），得到 RED（`01-before-red.log`，原文）：

```
CHECK jump.product.shell-ready PASS
CHECK jump.product.target-installed FAIL
CHECK jump.product.initially-no-file PASS
CHECK jump.product.two-hits PASS
CHECK jump.product.click-first PASS
SEARCH-JUMP first requested='a.v':2 current='' cursorLine=1
CHECK jump.product.first-current-file FAIL
CHECK jump.product.first-loads-bytes FAIL
CHECK jump.product.first-cursor-line FAIL
CHECK jump.product.first-cursor-column FAIL
CHECK jump.product.first-status FAIL
CHECK jump.product.click-second PASS
SEARCH-JUMP second requested='b.sv':4 current='' cursorLine=1
CHECK jump.product.second-current-file FAIL
CHECK jump.product.second-cursor-line FAIL
WORKSPACE-SEARCH-SELFTEST ok=0 checks=95 failures=8 skipped=2
SELFTEST-EXIT=1
```

即：真实鼠标点击被记录（`click-*` PASS），但产品没有消费者——`current=''`（没有打开任何文件）、`cursorLine=1`（编辑器仍在初始位置）。与 verdict H1 描述一致。

## 2. 改动（全部在写域内）

| 文件 | 改动 |
|---|---|
| `src/workspace/search/search_jump_target.h` | **新增** 产品跳转目标 `EditorJumpTarget`（`QObject` + `IJumpTarget`）：`jumpTo()` 打开命中文件并定位光标；文档说明它是 `jumpRequested` 的产品消费者、edit-core 落地后替换实现而非调用点。 |
| `src/workspace/search/search_jump_target.cpp` | **新增** 实现：复用 filetree lane 的**唯一 open seam** `vedit::workspace::openFileInEditor(shell, path, &error)`（读文件/大小/二进制/不可读等判定都在那里），随后取 `editorArea()->activePane()->editorWidget()` 的 `QPlainTextEdit`，把光标放到 `line`/`column`（越界夹到文档块内），`centerCursor()`，`qInfo` 记录实际光标行；失败写 `lastError()` + `qWarning`。非 shell 构建（无 open seam）时**报告缺失接缝而不是自造第二条打开逻辑**。 |
| `src/workspace/search/search_integration.cpp` | `installSearch()` 现在**安装**这个消费者：每个 shell 一个 `EditorJumpTarget`（`QObject` 父对象是 shell，故其寿命长于工厂创建的每个面板实例；重复调用复用同一个），工厂在**创建面板的地方**`setJumpTarget()`，所以占位替换得到的面板与以后重新插入的面板都带同一条跳转路径。 |
| `src/workspace/search/CMakeLists.txt` | `search_jump_target.cpp` 仅在 shell 构建加入 sources；`if(TARGET vedit_file_tree)` 时链接 `vedit_file_tree` 并定义 `VEDIT_SEARCH_HAVE_FILETREE_OPEN=1`（lane 可选，不反向依赖 `vedit_workspace_integration`，避免静态库环）。 |
| `src/workspace/search/search_selftest.cpp` | **只加不改**：新增 `testProductJump()`（真实 shell + `installSearch` 装的产品目标 + 真鼠标点击）与 `clickHitRow()` 小工具，断言真实产品状态；`main()` 里加一行调用。`git diff --numstat` = **139 insertions / 0 deletions**，无任何既有断言被删除或弱化。 |
| `src/workspace/search/search_panel.h`、`search_types.h` | 只更新“跳转是接缝、由自检提供 demo target”的过时注释为当前事实（产品由 `installSearch` 装 `EditorJumpTarget`）。 |
| `src/workspace/search/README.md` | 文档同步：文件职责表加 `search_jump_target.h/.cpp`，`installSearch` 片段改为“注册工厂 + 安装跳转目标”，自检描述补 `jump.product.*` 产品级断言（不再把 demo 夹具描述成唯一证据）。 |

**不在我的改动里**：`src/workspace/integration.cpp` 的 `installProject` 三行 diff 是 **project lane** 在共享工作树里的在途改动（写域相同但不是本任务所作，本任务未改该文件）；`src/app/*`、`src/editor/*`、`crates/*` 的改动/新增均属其他 lane。

## 3. 设计要点

- **一条打开路径**：跳转不自己读文件，而是调用 T21/T39 建立的具名 open seam `openFileInEditor()`（文件树双击、命令面板同一条 demo 编辑器路径）。搜索新增的只有“打开后把光标放到命中行/列”这一层。
- **谁消费**：`SearchPanel` 保持“发信号 + 转发给已装目标”的契约不变；`installSearch()` 成为产品安装点，于是 `src/app/main.cpp` 无需知道搜索 lane 的细节（沿用通用 lane 接缝）。
- **寿命**：目标以 shell 为 `QObject` 父对象；面板由工厂按需创建/销毁，目标始终存活且被共享；`installSearch` 重复调用 `findChild` 复用。
- **edit-core 落地后的替换点**：只替换 `EditorJumpTarget::jumpTo` 里“打开 + 定位”的实现（或它调用的 seam 后端），`installSearch` / `SearchPanel` / 自检的调用点不变。
- **失败语义**：文件缺失/不可读/二进制/过大（`openFileInEditor` 的既有判定）、无编辑器 seam、无 filetree lane，都在状态栏 + `lastError()` 可见，不抛异常。

## 4. 我实际运行的命令与观察（headless，全部在 pinned 镜像内）

### 4.1 RED → GREEN（同一 harness）

```sh
# 容器内（target/qt6-build 为仓库权威构建树）
$ cmake --build target/qt6-build --target vedit-workspace-search-selftest
$ QT_QPA_PLATFORM=offscreen vedit-workspace-search-selftest --selfcheck
```

- RED（`01-before-red.log`）：`WORKSPACE-SEARCH-SELFTEST ok=0 checks=95 failures=8 skipped=2`，`SELFTEST-EXIT=1`
- GREEN（`02-after-green.log`，关键行原文）：

```
search: jump opened '/tmp/vedit-workspace-search-selftest-SiCTSc/a.v' line=2 column=3 (cursor line=2)
CHECK jump.product.click-first PASS
SEARCH-JUMP first requested='a.v':2 current='/tmp/vedit-workspace-search-selftest-SiCTSc/a.v' cursorLine=2
CHECK jump.product.first-current-file PASS
CHECK jump.product.first-loads-bytes PASS
CHECK jump.product.first-cursor-line PASS
CHECK jump.product.first-cursor-column PASS
CHECK jump.product.first-status PASS
search: jump opened '/tmp/vedit-workspace-search-selftest-SiCTSc/b.sv' line=4 column=1 (cursor line=4)
SEARCH-JUMP second requested='b.sv':4 current='/tmp/vedit-workspace-search-selftest-SiCTSc/b.sv' cursorLine=4
CHECK jump.product.second-current-file PASS
CHECK jump.product.second-cursor-line PASS
WORKSPACE-SEARCH-SELFTEST ok=1 checks=95 failures=0 skipped=2
SELFTEST-EXIT=0
```

第二次 GREEN（`02-after-green.log` 的 RERUN 段，`HEAD=1f47e56`，断言未变）：同样 `ok=1 checks=95 failures=0 skipped=2`、`SELFTEST-EXIT=0`。

断言读的都是**产品状态**：`editor->toPlainText() == 磁盘字节`、`textCursor().blockNumber()+1 == hit.line`、`positionInBlock()+1 == hit.column`、`property("vedit.openFilePath") == hit.filePath`、状态栏含被打开路径；两次跳转选的是**两个不同文件、都不在第 1 行**的命中（a.v:2 / b.sv:4），所以“文件打开默认落在第 1 行”的假绿不可能通过。

### 4.2 lane 独立构建 + 验收套件（回归）

```sh
$ cmake -S src/workspace/search -B target/search-standalone -G Ninja && cmake --build ... && ctest ...
```

- 首次 RED：`-Werror=unused-function`（新增的 `clickHitRow` 在无 shell 构建里没人用）——**这是我的缺陷**，已把该 helper 放进 `#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL`，未放宽任何编译选项。
- 修复后（`03-lane-standalone.log`）：`BUILD-EXIT=0`，`1/1 Test #1: vedit-workspace-search-selftest ... Passed 0.33 sec`，`100% tests passed, 0 tests failed out of 1`，`CTEST-EXIT=0`。

### 4.3 相邻 lane 不回归

`cmake --build ... --target vedit-workspace-search-selftest vedit-workspace-palette-selftest vedit-workspace-palette-integration-selftest` + `ctest -R "workspace-search|workspace-palette"`（`04-neighbour-lanes.log`）：`BUILD-EXIT=0`，`100% tests passed, 0 tests failed out of 3`，`CTEST-EXIT=0`。

（`vedit-filetree-tests` 不属于根构建：`VEDIT_BUILD_FILETREE_TESTS` 默认只在独立 lane 构建里 ON，故未纳入；本任务未改 filetree 源码。）

### 4.4 无 filetree lane 的分支也要能编译

`#if !defined(VEDIT_SEARCH_HAVE_FILETREE_OPEN)` 分支用同一编译命令去掉该宏后 `-fsyntax-only` 复核（`06-nofiletree-branch.log`）：`SYNTAX-EXIT=0`。

### 4.5 仓库声明的 Qt6 gate（AGENT.md §4）

```sh
$ VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh
```

观察（`07-gate-qt6.log`，原文摘录）：

- Rust 服务层全绿：`test result: ok. 3/12/6/6/6 ...; 0 failed` 等 9 段
- configure 成功：`VEdit: workspace lane 'search' included`、`VEdit: workspace lane 'search' wired (vedit_workspace_search)`（本 lane 正常接入）
- 构建在**别人的在途改动**处失败：

```
[63/66] Building CXX object src/app/CMakeFiles/vedit-app.dir/main.cpp.o
FAILED: [code=1] src/app/CMakeFiles/vedit-app.dir/main.cpp.o
/vedit-build/src/app/main.cpp:323:14: error: too many arguments to function ‘void {anonymous}::check(const char*, bool)’
ninja: build stopped: subcommand failed.
GATE-EXIT=1
```

`src/app/main.cpp` 属 edit-core lane 的未提交在途改动（`git status`：`M src/app/CMakeLists.txt`、`M src/app/editor_area.*`、`M src/app/editor_provider.*`、`?? src/editor/`），本轮采集时该 lane 正在同一构建树里并行落地；同一错误在 `05-app-link-blocked.log` 的两次独立探针里可复现。**gate 的失败点不在本任务写域内，也不在 search lane 的编译单元**——search 库与其自检在本次 gate 构建中编译/链接成功（`[53] Linking CXX static library ...libvedit_workspace_search.a`）。因此 `ctest`（gate 的后半段）本轮未被执行；我用 4.1–4.3 的等价覆盖代替，并在 `07-gate-qt6.log` 保留 gate 原始输出与 `GATE-EXIT=1`。

## 5. 硬约束逐条对照

1. **自检断言真实产品状态**：`jump.product.*` 断言文档文本 == 磁盘字节、光标行号/列号 == 命中行/列、`vedit.openFilePath` == 命中路径、状态栏含路径；**没有**把信号当结论。
2. **不删/不弱化既有断言**：`search_selftest.cpp` 的 diff 是 `139 insertions / 0 deletions`；原有 `jump.click.*`、`jump.demo.*`、`seam.*` 等断言逐条保留并通过（`02-after-green.log` 全文可见）。CHECK 总数 82 → 95（+13，全为新增）。
3. **不改测试迁就实现**：唯一的测试侧修正是把**我自己新增**的 helper 放进 shell 宏（修 `-Werror=unused-function`），未改任何既有断言/判据/门禁脚本。
4. **写域**：改/增文件全部在 `src/workspace/search/` 与 `evidence/fix-search-jump/`；`src/workspace/integration.cpp` 在写域内但**本任务未改**（其 3 行 diff 属 project lane 在途改动）。

## 6. 未验证 / 局限（如实说明）

- **没有 GUI 可视化判断**：本机无显示服务器；所有结论是 headless（构建 / 自检 / ctest / 真实 shell 的状态断言）。
- **只覆盖 demo 编辑器 seam**：当前产品编辑器仍是 shell 的 demo `QPlainTextEdit`；跳转“打开 + 定位”走的就是 edit-core 将来要替换的同一个 `openFileInEditor` 调用点。KTextEditor/edit-core 的真实文档定位未验证（该 lane 尚未落地）。
- **产品二进制本轮未能链接**：`vedit-app` 因 edit-core lane 的在途 `src/app/main.cpp` 编译错误无法构建（§4.5，外部原因）。因此“`vedit-app` 里点一条命中”的整机冒烟本轮**没有**跑；产品路径由真实 `ApplicationShell` + `installSearch` 的自检覆盖。
- **gate 的 ctest 段未执行**：原因同上；等价覆盖见 §4.1–4.3，待 edit-core 落地后应由验证者重跑整条 gate。
- **失败分支未逐一实测**：目标对 missing/不可读/二进制/超大文件、无编辑器 seam、无 filetree lane 都有显式失败路径，但本任务只为成功路径造了 fixture 断言（H1 判据只要求点击真正跳转）。
- **M2（`GlobalSearch` 析构期 use-after-free 风险）未处理**：verdict 标注为“建议同批修”，不在本冻结契约的 Deliverable 内；如需处理应另开返修任务。
- **非 root 检查**：本轮 `skipped=2`（不可读 root 相关），未在非 root 下重跑。

## 7. 证据清单（`evidence/fix-search-jump/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 采集时间、HEAD、镜像 id、宿主、共享工作树 `git status --porcelain` |
| `01-before-red.log` | 新断言 + 未修复实现：8 条 `jump.product.*` FAIL，`checks=95 failures=8`，exit 1 |
| `02-after-green.log` | 修复后同一 harness：`checks=95 failures=0 skipped=2`，exit 0；含 RERUN（`HEAD=1f47e56`） |
| `03-lane-standalone.log` | lane 独立构建（无 shell）+ ctest；含 `-Werror` 首轮 RED 与修正后 GREEN |
| `04-neighbour-lanes.log` | search + palette 测试目标重建、`ctest -R` 3/3 passed |
| `05-app-link-blocked.log` | `vedit-app` 构建失败两次探针 + 工作树状态（证明失败点在 edit-core 在途改动） |
| `06-nofiletree-branch.log` | `VEDIT_SEARCH_HAVE_FILETREE_OPEN` 未定义分支的 `-fsyntax-only` 复核 |
| `07-gate-qt6.log` | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh` 全量输出与 `GATE-EXIT=1`（失败原因原文） |

## 8. 说明 / 阻塞

- **无本任务阻塞**：H1 的可交付行为已在真实 `ApplicationShell` 中以产品状态断言验证。
- **外部阻塞（供验证者/`captain` 知悉）**：共享工作树里 edit-core lane 的未提交改动导致 `vedit-app` 无法编译（`src/app/main.cpp:323`），整条 `scripts/build-qt.sh` gate 因此 `GATE-EXIT=1`。该失败与本任务写域无关，且在 gate 日志中定位到具体文件行。
- **未做 git 提交**：多人共享工作树、VCS 由 `captain`/Lead 统一处理（沿用 T39 的约定）；本任务只改文件并留证据。
