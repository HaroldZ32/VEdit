# T39 返修报告 — filetree 双击真正打开文件（H1 / High）

- Cell: `cell-20261031T085543161Z` · Board task: **T39** · 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-filetree-open.md`
- 修复对象: `evidence/verdicts/ws-filetree/verdict.md` §2 **H1**（A2a 的“双击打开文件”只在信号边界成立，产品里不打开文件）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）
- 代码基线: `HEAD = 9957f5fe4d63c17c681e8753288262fa2c00955e`（分支 `feat/qt6-ktexteditor-migration`）；被测源码的 sha256 见 `06-source-sha256.txt`
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24.0 / cmake / g++ 15.x / glibc 2.43，容器内实测见 `02-after-green.log` 头部）
- 结论: **H1 已修复并通过 headless 验证**。本机没有显示服务器，全部结论来自构建、单元测试、仓库 gate、以及**真实 `ApplicationShell`** 内的编辑器状态断言；没有“看起来正常”这类判断。

---

## 1. 根因（复现，不是猜测）

修复前 `src/workspace/filetree/filetree_integration.cpp` 的 `installFileTree()` 只把
`FileTreeWidget::openFileRequested` 连接到 `shell.statusBar()->showMessage("Open <path>")`，
没有任何编辑器载入动作。把 HEAD 版本放回工作树、用同一套真实 shell 断言跑基线（`01-before-red.log`）：

```
SEAM status-after-file-dblclick="Open /vedit-build/target/ws-filetree-tmp/project/rtl/top.v"
SEAM CHECK dblclick.file-loads-bytes ok=0 detail=37 bytes on disk
SEAM CHECK dblclick.file-records-path ok=0
SEAM CHECK dblclick.second-file-loads ok=0
SEAM CHECK dblclick.folder-keeps-editor ok=0
FILETREE-SEAMCHECK ok=0 checks=15 failures=6
```

即：双击后编辑器仍是 demo 欢迎文本，`vedit.openFilePath` 属性为空——与 verdict 的描述一致。

## 2. 改动（全部在写域内）

| 文件 | 改动 |
|---|---|
| `src/workspace/filetree/filetree_integration.h` | 新增**具名 open seam** `bool openFileInEditor(ApplicationShell &, const QString &path, QString *error = nullptr)` 的声明与文档（edit-core 落地后替换实现、调用点不变）。 |
| `src/workspace/filetree/filetree_integration.cpp` | 实现 `openFileInEditor()`：与 `PaletteIntegration::openInEditor` 相同的 demo 路径（`editorArea()->activePane()->editorWidget()` → `QPlainTextEdit` → `setPlainText(文件 UTF-8 字节)` + 属性 `vedit.openFilePath` + 状态栏 `Opened <path>`；失败分支与 palette 文案一致）。`installFileTree()` 的 panel factory 在**创建面板的地方**连接 `openFileRequested → openFileInEditor`，所以占位替换得到的面板和以后重新插入的面板都带同一条桥。 |
| `src/workspace/filetree/tools/rootcheck/seamcheck.cpp` | 自检改为断言**真实产品行为**：真实鼠标双击文件行 → 断言 active pane 编辑器文本 == 磁盘字节、`vedit.openFilePath` == 该路径、第二个文件会替换内容；双击文件夹行 → 断言只展开、`openFileRequested` 计数为 0、编辑器仍保留上一个文件。原状态栏断言**保留**（文案随行为改为 `Opened`），并新增 `SEAM CHECK <name> ok=<0|1>` 机器可读输出与 checks/failures 汇总。 |
| `src/workspace/filetree/tools/rootcheck/CMakeLists.txt` | 链接 `Qt6::Test`（真实鼠标事件 + `QSignalSpy`）。 |
| `src/workspace/filetree/tests/filetree_tests.cpp` | **仅注释**：说明本套件跑的是无 shell 的独立面板，面板级契约是“上报路径”，产品级行为在 `tools/rootcheck/seamcheck.cpp` 断言。既有 111 条断言一条未删、未弱化（`04-lane-tests-full.log`：`FILETREE-TESTS checks=111 failures=0`）。 |

`src/workspace/integration.cpp/.h` 在写域内但**未改动**（具名 seam 放在 lane 自己的 shell adapter 头里，避免 filetree 库反向依赖 `vedit_workspace_integration` 造成静态库环）。

## 3. 修复设计要点

- **双击文件**：`QTreeView::doubleClicked → FileTreeWidget::onActivated → emit openFileRequested → openFileInEditor()` → 载入字节 + 记录路径 + 状态栏。走的是 palette 已在产品里证明可行的同一条 demo 编辑器路径；edit-core 提供真实 provider 后，此处只替换 widget 类型/实现，`openFileInEditor` 的调用点不变。
- **双击文件夹**：`FileTreeWidget` 在 `isDirectory` 时仍然只 `ensureLoaded + expand`，不发 `openFileRequested`；bridge 不改变这条路径（before/after 两次运行都断言通过）。
- **面板重建**：桥接放在 factory 内，而不是只挂在“替换占位控件”那一次。

## 4. 我实际运行的命令与观察（headless）

> 所有 GUI 进程都在 `QT_QPA_PLATFORM=offscreen` 下运行；全部在 pinned 镜像内执行。

### 4.1 修复前/后对照（同一 harness、同一 fixture）

```sh
# before: filetree_integration.{h,cpp} 恢复为 HEAD 9957f5f；after: 带修复
bash -lc 'source scripts/lib-qt.sh; load_qt_versions; run_in_qt6_container \
  bash evidence/fix-filetree-open/run-seamcheck.sh before|after'
```

- before: `FILETREE-SEAMCHECK ok=0 checks=15 failures=6`，`SEAMCHECK before exit=1`（`01-before-red.log`，host exit 1）
- after: `FILETREE-SEAMCHECK ok=1 checks=15 failures=0`，`SEAMCHECK after exit=0`（`02-after-green.log`，host exit 0）

after 的关键行（原文）：

```
SEAM CHECK editor.initially-no-file ok=1
SEAM status-after-file-dblclick="Opened /vedit-build/target/ws-filetree-tmp/project/rtl/top.v"
SEAM CHECK dblclick.file-loads-bytes ok=1 detail=37 bytes on disk
SEAM CHECK dblclick.file-records-path ok=1 detail=/vedit-build/target/ws-filetree-tmp/project/rtl/top.v
SEAM CHECK dblclick.second-file-loads ok=1
SEAM CHECK dblclick.second-file-records-path ok=1 detail=/vedit-build/target/ws-filetree-tmp/project/rtl/alu.sv
SEAM CHECK dblclick.folder-expands ok=1
SEAM CHECK dblclick.folder-emits-no-open ok=1 detail=0
SEAM CHECK dblclick.folder-keeps-editor ok=1
FILETREE-SEAMCHECK ok=1 checks=15 failures=0
```

两条路径的逐行对照另见 `03-two-paths.txt`。

### 4.2 lane 独立构建 + 验收套件（回归）

```sh
bash -lc 'source scripts/lib-qt.sh; load_qt_versions; run_in_qt6_container \
  bash -lc "cmake -S src/workspace/filetree -B target/filetree-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo; \
            cmake --build target/filetree-build --parallel \$(nproc); ctest --test-dir target/filetree-build --output-on-failure"'
```

观察（`01-before-red.log` / `02-after-green.log` 第 2b 段）：`100% tests passed, 0 tests failed out of 1`（ctest exit 0）。
完整 CHECK 列表重跑：`FILETREE-TESTS checks=111 failures=0`（`04-lane-tests-full.log`、`04b-lane-tests-after-comment.log`），与修复前一致。

### 4.3 仓库声明的 gate（Qt6 产品线）

```sh
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh
```

观察（`05-gate-qt6.log`）：配置/构建/ctest/xcb smoke 全绿，
`100% tests passed, 0 tests failed out of 10`，`M0-SMOKE ok=1 platform=xcb ...`，
`[qt6] PASS: ...`，`GATE-EXIT=0`。日志中 `[qt6-image] VEDIT_SKIP_IMAGE_BUILD=1: reusing ...` 说明镜像未重建。

## 5. 硬约束逐条对照

1. **双击文件夹只展开**：保持不变。before/after 两次运行 `dblclick.folder-expands ok=1`、`dblclick.folder-emits-no-open ok=1`、`dblclick.folder-keeps-editor ok=1`（后者修复前为 0 只因编辑器本来就没打开过文件）。
2. **自检断言真实产品行为**：`seamcheck.cpp` 断言 active pane 编辑器的文本内容与磁盘字节相等、`vedit.openFilePath` 属性等于被双击路径；不再只断言信号/状态栏。
3. **不删/不弱化既有断言**：`tree.dblclick.file-opens`（信号层）仍保留；seamcheck 里原状态栏断言保留（文案由 `Open` 变为行为对应的 `Opened`，且同时由编辑器断言兜底）；面板级 111 条断言未动。
4. **不改测试迁就实现**：`filetree_tests.cpp` 只加了一段注释；无任何断言、判据被修改。

## 6. 未验证 / 局限（如实说明）

- **没有 GUI 可视化判断**：本机无显示服务器。`screenshots/before-shell.png`、`screenshots/after-shell.png` 是 offscreen grab，只证明渲染成功，**不作为“看起来正确”的证据**。
- **只覆盖 demo 编辑器 seam**：`openFileInEditor` 目前载入的是 shell 的 demo `QPlainTextEdit`；KTextEditor/edit-core 的真实文档加载属于后续 lane 的实现替换，本任务只保证“双击 → 产品里真的打开 + 路径可断言”这条契约与具名 seam。
- **失败分支未逐一实测**：`openFileInEditor` 对 missing/too large/unreadable/binary 的分支与 palette 文案一致，但本任务没有为这些分支单独造 fixture 断言（不在 H1 判据内）。
- **二进制/大文件边界**未在 seamcheck 中触发。
- **根 ctest 仍不含 filetree 套件**（verdict M2，另行返修任务，本任务不处理）。

## 7. 说明 / 阻塞

- **无阻塞**。
- **未做任何 git 提交/建分支**：这是多人共享工作树（其他 lane 有大量未提交改动，`git status` 见 `00-pins.txt`），VCS 集成属于 captain/Lead 的两个人工停点；本任务只改文件并留证据。
- 仓库 gate 运行时会刷新它自己的证据日志（`evidence/m0-build/build-logs/`、`evidence/ffi-abi/logs/`），这些路径在 T39 开始前就已处于 modified/untracked 状态；本任务自身的 diff 只有第 2 节列出的 5 个文件 + `evidence/fix-filetree-open/`。

## 8. 证据清单（`evidence/fix-filetree-open/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 采集时间、HEAD、工作树 porcelain、镜像 id |
| `01-before-red.log` | 修复前（HEAD 9957f5f）同一 harness 的 RED 输出 + host exit 1 |
| `02-after-green.log` | 修复后同一条命令的 GREEN 输出 + host exit 0 |
| `03-two-paths.txt` | 双击文件 vs 双击文件夹两条路径的逐行断言（且含修复前对照） |
| `04-lane-tests-full.log` | lane 验收套件完整 CHECK 列表（111 checks / 0 failures） |
| `04b-lane-tests-after-comment.log` | 注释改动后重建重跑，同样 111/0 |
| `05-gate-qt6.log` | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh` 全量输出，`GATE-EXIT=0` |
| `06-source-sha256.txt` | 被测源码 sha256（固定本轮证据对应的代码版本） |
| `10-rootcheck-configure-{before,after}.log`、`11-rootcheck-build-{before,after}.log` | root-context harness 的 configure/build 原始日志 |
| `screenshots/{before,after}-shell.png` | offscreen 抓图（仅证明渲染成功，不做视觉判断） |
| `run-seamcheck.sh` | 容器内可复跑脚本（before/after 两参数） |
