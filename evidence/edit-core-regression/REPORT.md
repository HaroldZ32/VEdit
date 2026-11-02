# T60 回归修复报告 — KTextEditor 化后 filetree / palette 的打开路径

- Cell: `cell-20261031T085543161Z` · Board task: **T60**
- 类型: T10（编辑内核换 KTextEditor）引入的**产品级回归**；契约 = captain 任务书（本任务无独立 contract 文件）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）· 基线 `HEAD 95c496e`
- 结论: **两处打开路径已改走 `EditorPane::openFile/currentFilePath()`，并在真实 KTextEditor provider 下真跑验证通过**；根 ctest 16/16、typecheck PASS。**另发现第三处同类硬 cast（search lane），按任务要求只报告、未动**（§7）。
- 无显示服务器：以下全部为 headless 验证（offscreen + pinned 镜像 `vedit-qt6-build:local`）。

## 1. 问题

产品 provider 在 T10 后是 KTextEditor（`KTextEditor::View`），而两条打开路径仍硬 cast
`qobject_cast<QPlainTextEdit *>(pane->editorWidget())`，在产品运行中会落到
“no editor seam connected yet”，即：**T39 刚修好的 filetree 双击打开、以及 palette 打开文件在真实产品里失效**。

## 2. 改动（全部在写域内，6 个文件）

| 文件 | 改动 |
|---|---|
| `src/workspace/filetree/filetree_integration.cpp` | `openFileInEditor()` 不再 cast widget：取 `editorArea()->activePane()` → `pane->openFile(path, &error)`；成功状态栏 `Opened <path>`，失败把 provider 的诊断回显。demo 时代的大小/二进制检查下沉到 provider 文档接缝（KTextEditor 路径没有 demo 大小限制）。 |
| `src/workspace/filetree/tools/rootcheck/seamcheck.cpp` | 修复的**真跑验证**：先 `shell.setEditorProvider(makeEditCoreProvider())`，编辑器内容从 `editor::documentFor(widget)->text()` 读、路径从 `pane->currentFilePath()` 读；新增 `editor.provider`、`editor.ktexteditor-widget` 两条断言；`--project` 参数统一转绝对路径。原 15 条断言一条未删（现共 17 条）。 |
| `src/workspace/filetree/tools/rootcheck/CMakeLists.txt` | 链接 `vedit_editor_core` + `VEDIT_ROOTCHECK_HAVE_EDIT_CORE=1`；同时补齐 mini-project 的两个依赖：`include(cmake/VEditRustFfi.cmake)`（project lane 需要 `vd_ffi`）与 `find_package(KF6TextEditor)`（edit-core lane 需要）。 |
| `src/workspace/palette/palette_integration.cpp` | `openInEditor()` 同样改走 `pane->openFile(path, &error)`，保留 `setFileOpenHandler` 自定义 handler 优先的语义与状态栏文案；删掉本地 cast 与 demo 限制常量。 |
| `src/workspace/palette/palette_integration_selftest.cpp` | **新增真实 provider 断言**（`VEDIT_PALETTE_HAVE_EDIT_CORE`）：切到 KTextEditor provider 后触发 palette 打开，断言 `currentFilePath()` == 磁盘路径、文档文本 == 磁盘字节、`openedPath()` 记录正确；原 demo 断言全部保留。 |
| `src/workspace/palette/CMakeLists.txt` | 上述 selftest 链接 `vedit_editor_core` + 定义 `VEDIT_PALETTE_HAVE_EDIT_CORE`（仅在 edit-core lane 存在时）。 |

没有动 `src/editor/**`、`src/app/**` 主逻辑，也没有动其他 lane 的在飞文件。

## 3. 我实际运行的命令与观察（headless，全部真跑）

### 3.1 filetree：真实 KTextEditor provider 下的双击

```sh
# 容器内，offscreen；fixture 由 lane 自己的 demo --prepare 生成（7 个工程文件）
cmake -S src/workspace/filetree/tools/rootcheck -B target/filetree-rootcheck -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/filetree-rootcheck --target vedit-filetree-seamcheck
target/filetree-rootcheck/vedit-filetree-seamcheck \
    --project target/ws-filetree-tmp/project --state <tmp> --screenshot <png>
```
观察（`02-filetree-seamcheck.log`，`SEAMCHECK-EXIT=0`）：

```
SEAM CHECK editor.provider ok=1 detail=ktexteditor
SEAM CHECK editor.ktexteditor-widget ok=1
SEAM editor-initial-bytes=0
SEAM CHECK editor.initially-no-file ok=1
SEAM status-after-file-dblclick="Opened /vedit-build/target/ws-filetree-tmp/project/rtl/top.v"
SEAM CHECK dblclick.file-loads-bytes ok=1 detail=37 bytes on disk     # 编辑器文本 == 磁盘字节
SEAM CHECK dblclick.file-records-path ok=1 detail=.../rtl/top.v        # currentFilePath() == 磁盘绝对路径
SEAM CHECK dblclick.second-file-loads ok=1
SEAM CHECK dblclick.second-file-records-path ok=1 detail=.../rtl/alu.sv
SEAM CHECK dblclick.folder-expands ok=1
SEAM CHECK dblclick.folder-emits-no-open ok=1 detail=0                 # 双击文件夹不发打开请求
SEAM CHECK dblclick.folder-keeps-editor ok=1
FILETREE-SEAMCHECK ok=1 checks=17 failures=0
```
双击是**真实鼠标事件**（`QTest::mouseClick` + `mouseDClick` 打在 QTreeView viewport 的行矩形上），不是直接调 `activateIndex()`。

### 3.2 palette：真实 KTextEditor provider 下的打开

```sh
target/qt6-build/src/app/workspace-lane-palette/vedit-workspace-palette-integration-selftest \
    --screenshots <dir>          # offscreen
```
观察（`01-palette-selftest.log`，退出 0）：

```
CHECK integration.ktexteditor-widget PASS
CHECK integration.ktexteditor-opened-path PASS
CHECK integration.ktexteditor-document PASS
CHECK integration.ktexteditor-content PASS
KTEXTEDITOR-OPEN path=/tmp/.../project/src/util/helper.cpp bytes=10
CHECK integration.ktexteditor-opened-path-recorded PASS
INTEGRATION-SELFTEST ok=1 checks=28 failures=0
```
打开动作仍是产品路径：Ctrl+P 命令 → 输入过滤 → Enter 发送到 palette 的 queryEdit，然后断言编辑器内容与路径。

### 3.3 根 ctest（当前基线 16/16）

```sh
ctest --test-dir target/qt6-build --output-on-failure
```
观察：`100% tests passed, 0 tests failed out of 16`，`ROOT-CTEST-EXIT=0`，其中
`12/16 Test #12: vedit-workspace-palette-integration-selftest ... Passed`（`03-root-ctest.log`）。

### 3.4 `bash build/gates/typecheck.sh`

观察：`[qt6] PASS: ... build (warnings-as-errors)`、`[typecheck] PASS (pinned image vedit-build:local)`、
`TYPECHECK-EXIT=0`（`04-typecheck.log`）。

## 4. 既有断言的处理（不删、不弱化）

- filetree：T39 的 15 条断言全部保留（同一套 check 名称），只把「编辑器内容/路径」的读取方式从
  `QPlainTextEdit` 改为 provider 接缝（`documentFor(...)->text()` / `currentFilePath()`）；新增 2 条
  provider 断言，共 17 条。
- palette：原 demo provider 的断言全部保留，另加 5 条真实 provider 断言（widget / path / document /
  content / openedPath），共 28 条。
- 两份 harness 都在 **KTextEditor provider 已安装**的前提下运行，不是「demo pane 绿了就算修好」。

## 5. 未验证 / 局限

- 无 GUI/视觉验证：KTextEditor 视图在 offscreen 下创建、载入与保存都有断言，但没有“窗口看起来正确”的判断。
- 修复前的红灯未重新录制：缺陷事实由 T10 的代码分析 + 任务书前提给定；本任务录制的是修复后的真跑通过。
- filetree 的 per-file 大小/二进制提示现在由 provider 统一给出（KTextEditor 路径不设 demo 的 4 MB 上限），
  没有断言依赖旧文案；若产品需要该上限，应在 provider 层定义（属 T10 写域）。

## 6. 证据清单（`evidence/edit-core-regression/`）

`00-pins.txt`（HEAD/镜像/改动路径）、`01-palette-selftest.log`、`02-filetree-seamcheck.log`、
`03-root-ctest.log`、`04-typecheck.log`、`screenshots/filetree-ktexteditor-shell.png`（offscreen 抓图，仅证明渲染成功）。

## 7. 第三处同类硬 cast（按任务要求：只报告，未动）

`src/workspace/search/search_jump_target.cpp:55-57` 仍在做
`qobject_cast<QPlainTextEdit *>(pane->editorWidget())`（全局搜索的“跳转到命中文件”目标）。
它与本次修复的两处是同一类回归：产品 provider 为 KTextEditor 后会落到 demo 分支。
该文件不在 T60 写域（`src/workspace/search/**`），我**没有改**。
建议：由 captain 开一个同样的返修（或扩写域），把 jump target 改为 `EditorPane::openFile/currentFilePath`，
并给 search lane 补一条真实 provider 断言（方法与 palette 相同）。

## 8. 说明

- 未做任何 commit/push（Lead 统一负责）。
- 未触碰 `src/editor/**`、`src/app/**` 主逻辑、其他 lane 在飞文件；`git status` 中 T60 自身改动仅第 2 节的 6 个文件 + 本证据目录（另有他人未跟踪的 `src/workspace/filetree/tools/generate-testdata.sh`，非本次产物）。
