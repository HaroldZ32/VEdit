# T69 回归修复报告 — 搜索结果点击跳转在 KTextEditor provider 下失效

- Cell: `cell-20261031T085543161Z` · Board task: **T69**（blockedBy T64，T64 已完成）
- 类型: T60 上报的**第三处同类硬 cast**（`search_jump_target.cpp:55-57`）的产品级回归修复
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）· 基线 `HEAD 95c496e`
- 结论: **已收敛并全绿**。`jump.product.*` 共 **16 条，0 红**；search selftest `ok=1 checks=105 failures=0`；
  根 ctest **16/16**（exit 0）；typecheck **PASS**（exit 0）。无断言删除/弱化（102 → 105 checks）。
- 无显示服务器：以下均为 headless 验证（offscreen + pinned 镜像）。

## 0. 关于冻结门禁那次红（已解释）

captain 的冻结门禁里 `vedit-workspace-search-selftest → ok=0 checks=102 failures=7` 全部落在
`jump.product.*`：那次构建窗口 `19:51:37→19:52:17`，而 `search_selftest.cpp` 的 mtime 是 `19:52:24`
（构建时该文件正被我改写）。这是**编译到中间态的假红**，不是最终代码的缺陷。
最终自洽状态下同一套检查 16/16 全绿（§3.2），根 ctest 也恢复 16/16（§3.3）。

## 1. 缺陷

产品 provider 在 T10 后是 KTextEditor（`KTextEditor::View`），而 `EditorJumpTarget::jumpTo()`
在打开文件后仍 `qobject_cast<QPlainTextEdit *>(pane->editorWidget())`，于是：
open 成功、**光标定位失败**，`lastJumpSucceeded_=false`、`lastError_="the active editor pane has no text editor"`，
用户点击搜索结果只打开文件、不跳到命中行/列 —— T41/T42 的成果在真产品里不可用。

## 2. 改动（4 个文件，全部在写域 `src/workspace/search/**`）

| 文件 | 改动 |
|---|---|
| `search_jump_target.cpp` | 打开改走**唯一 shell 级 open seam**：`pane->openFile(request.filePath, &error)`；随后用编辑内核公开 API 定位：`vedit::editor::viewFor(widget)` → `KTextEditor::View::setCursorPosition(Cursor(line-1, column-1))` → `setScrollPosition(cursor)`，并**回读 `cursorPosition()` 校验**；行/列按 `document->lines()` / `lineLength()` clamp；成功后状态栏 `Opened <path> line <n>`；失败保留 `lastError_` + `qWarning` + 状态栏诊断。**不再有任何对 pane widget 的 QPlainTextEdit cast**。 |
| `search_jump_target.h` | 更新文档：打开 seam 改为 `EditorPane::openFile`，定位在 KTextEditor；无 edit-core lane 时报告缺失 provider，不做第二套 fallback。 |
| `search_selftest.cpp` | `testProductJump` 先 `shell->setEditorProvider(makeEditCoreProvider())`（真实产品 provider），断言全部改为 provider 接缝：`pane->currentFilePath()`、`document->text()`、`view->cursorPosition().line()+1` / `.column()+1`；保留原有的检查名与语义，新增 `jump.product.ktexteditor-provider`、`jump.product.second-loads-bytes`、`jump.product.second-cursor-column`。 |
| `CMakeLists.txt` | search lane 链 `vedit_editor_core` + `VEDIT_SEARCH_HAVE_EDITOR_CORE=1`（selftest 同样）；移除对 filetree lane 的链接——打开 seam 统一到 `EditorPane::openFile`（T60 后 filetree 的 `openFileInEditor` 也委托给它），仍是**一条**打开路径，且少一条 lane 耦合。`installSearch` 安装 `EditorJumpTarget` 的 T41 接线未动。 |

**没有**改 `global_search.*`（T64 成果）、`search_panel.*` / `search_integration.cpp`（T41 成果）、
filetree / palette（T60 成果）、`src/app/**`、`src/editor/**`。

## 3. 真跑验证（headless）

### 3.1 修前红（能真红，不是推论）

同一套新断言、旧产品代码（仍 cast QPlainTextEdit）运行：

```
SEARCH-SELFTEST-EXIT=1
search: jump failed: the active editor pane has no text editor
CHECK jump.product.first-cursor-line FAIL
CHECK jump.product.first-cursor-column FAIL
CHECK jump.product.second-cursor-line FAIL
WORKSPACE-SEARCH-SELFTEST ok=0 checks=105 failures=3
```
（`01-before-red.log`；open/加载本身成功，红的正是光标定位三项 —— 与缺陷机理一致。）

### 3.2 修后绿（真实 KTextEditor provider）

```
SEARCH-SELFTEST-EXIT=0
CHECK jump.product.shell-ready PASS
CHECK jump.product.ktexteditor-provider PASS
CHECK jump.product.two-hits PASS
SEARCH-JUMP first  requested='a.v':2  current='.../a.v'  cursor=2:3
CHECK jump.product.first-current-file PASS     # currentFilePath == 磁盘路径
CHECK jump.product.first-loads-bytes PASS      # 文档文本 == 磁盘字节
CHECK jump.product.first-cursor-line PASS      # blockNumber+1 == 命中行
CHECK jump.product.first-cursor-column PASS    # positionInBlock+1 == 命中列
SEARCH-JUMP second requested='b.sv':4 current='.../b.sv' cursor=4:1
CHECK jump.product.second-current-file PASS
CHECK jump.product.second-loads-bytes PASS
CHECK jump.product.second-cursor-line PASS
CHECK jump.product.second-cursor-column PASS
WORKSPACE-SEARCH-SELFTEST ok=1 checks=105 failures=0 skipped=2
```
两次跳转是**两个不同文件、且都非第 1 行**（a.v:2、b.sv:4），防「永远跳第一行」假绿。
`jump.product.*`：**16 条检查，0 失败**（`grep -c '^CHECK jump.product'` = 16，`grep -c ' FAIL'` = 0）。
检查总数 102（T64 后基线）→ **105**（只增不减；既有 `jump.click.*` / `jump.demo.*` / `seam.*` 未删未弱化）。

### 3.3 根 ctest（基线 16/16）

```sh
ctest --test-dir target/qt6-build --output-on-failure
```
- 第一次（与另一 agent 的并发构建重叠）：`14/16 vedit-workspace-project-selftest ... ***Not Run ... [permission denied]`（二进制正在被并发 relink），`94% passed`。
- **立即重跑**：`100% tests passed, 0 tests failed out of 16`，`ROOT-CTEST-EXIT=0`（`03-root-ctest.log`），其中
  `13/16 vedit-workspace-search-selftest ... Passed 11.90 sec`。

### 3.4 `bash build/gates/typecheck.sh`

`[qt6] PASS ... warnings-as-errors`、`[typecheck] PASS (pinned image vedit-build:local)`、`TYPECHECK-EXIT=0`（`04-typecheck.log`）。

## 4. 不可在容器内验证的部分（UNVERIFIABLE-IN-CONTAINER）

- **物理鼠标点击链路**：测试用 `QTest::mouseClick` 打在命中行矩形上（Qt 事件路径 → `jumpRequested` → 产品
  consumer），没有 X server / 真实指针，无法验证物理点击的合成路径；已验的是 Qt 事件路径 + 磁盘/编辑器状态。
- **视觉滚动位置**：`setScrollPosition()` 的可见滚动效果没有截图判定（无显示服务器；offscreen 无视觉结论）。

## 5. 兼容性说明（对 captain 提醒的两条约束）

- **T41 成果未回退**：`installSearch` 仍安装唯一 `EditorJumpTarget`；点击仍然打开命中文件并定位行/列。
- **「单一 open seam」未回退**：`EditorPane::openFile` 就是那条 seam；T60 之后 filetree 的 `openFileInEditor`
  也委托给它，palette 同样。search 不再单独链接 filetree lane，但**没有**新增第二套打开路径。
- **T64 成果未回退**：`global_search.*` 未改；search selftest 基线 102 → 105 只增。

## 6. 证据清单（`evidence/fix-search-jump-provider/`）

`00-pins.txt`、`01-before-red.log`、`02-after-green.log`（105 条完整 CHECK）、`03-root-ctest.log`、`04-typecheck.log`、本报告。

## 7. 说明

未做任何 commit/push（Lead 统一负责）。未触碰写域外文件。
