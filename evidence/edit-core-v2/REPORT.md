# T10 交付报告 — Slice 1：KTextEditor 编辑内核进产品

- Cell: `cell-20261031T085543161Z` · Board task: **T10** · 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/edit-core.md`
- 需求: **AC-S1.1**（edit-core.md 交付说明）+ C1/C3/G3；验证者: Reviewer（独立）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）
- 基线: `HEAD 95c496e`（`feat/qt6-ktexteditor-migration`）· 镜像: `vedit-qt6-build:local`（KTextEditor 6.24.0 / Qt 6.10.2 / glibc 2.43 / g++ 15.2）
- 结论: **AC-S1.1 的交付面全部落地并通过 headless 验证**；多光标按 Open Q3 给出有据结论；编辑事务不过 C ABI 有静态 + 运行期双重证据。**有 1 个写域外的跨 lane 集成问题需要 captain 决定（§7）**。
- 本机无显示服务器：全部结论来自构建、ctest、`--selfcheck`、Xvfb/xcb 烟测与静态符号检查；没有「看起来正确」类判断。

## 1. 交付内容（全部在写域内）

新 lane `src/editor/kernel/`（KTextEditor 编辑内核，静态库 `vedit_editor_core`）+ `src/editor/CMakeLists.txt`；
`src/app/` 接线（provider、pane、main、CMake）。文件级细节见 `01-design.md`，要点：

1. **文档缓冲**：`EditorDocument` 包装 `KTextEditor::Document`。打开 = 内核读字节 → 拒二进制 → `setText()` → 记录路径（不用 `KParts::openUrl()`：它在 offscreen 下挂死自检，见 §2.1）。
2. **撤销/重做**：`undo_ops` 经运行期 metaobject 按名字断言并调用 `undo()/redo()`（6.24 公开头文件没有声明，probe 约束 1）；100 个逻辑操作逐步 undo/redo 逐步比对通过。
3. **查找替换**：迭代 `searchText`（一次只回一个匹配）+ 零长前进 + 自下而上 + 单个 `EditingTransaction`：3/3 替换文本精确、一次 undo 回原文、redo 回替换后。
4. **跳行**：clamp 到 `[1, lines]`，列 0，回读校验（0 → 1、越界 → 末行、列 0）。
5. **原子保存**：临时文件 → 写全 → `fsync(file)` → `rename` → `fsync(dir)`；写/fsync/rename 故障注入后目标字节不变且临时文件被清理；`fsync(dir)` 的 dir-fsync 非致命路径有断言。
6. **多光标**：原生 `View::setCursorPositions/cursorPositions`（6.24 实测可用）+ 文档层单事务批量插入（一次 undo 全回滚）。结论见 `06b-multicursor-conclusion.md`。
7. **产品接线**：`src/app/main.cpp` 调 `shell.setEditorProvider(vedit::editor::makeEditCoreProvider())`；`EditorArea::setProvider()` 会把已存在的 demo pane 重建成 KTextEditor pane（布局不变）；`--selfcheck` 里断言 pane widget 是 KTextEditor view、pane 级 open/save 成功。
8. **`--selfcheck` 新增 edit-core 检查**：`CHECK editcore.*`（55 条，产品构建）覆盖打开/原子保存/查找替换/跳行/撤销重做/多光标/边界；既有 78 条 shell 检查一条未删。

## 2. 关键实测发现（不是推测）

1. **`Document::openUrl()` 在 headless 下会把进程挂死**：第一次 selftest 运行 `timeout 60` 退出码 124，卡在 `openUrl`（日志停在 `propagateSizeHints` 警告）。改为内核读字节 + `setText()` 后同一检查退出 0。这也是产品打开路径的最终实现。
2. **`QMetaObject::invokeMethod` 要裸名**：传 `"undo()"` 得到 `No such method KTextEditor::DocumentPrivate::undo()()`；`indexOfMethod` 要 `"undo()"`、`invokeMethod` 要 `"undo"`。日志留痕后修正，`editcore.replace.one-undo` 从 FAIL 变 PASS。
3. **原生多光标是「位置集合」而非「编辑 API」**：`setCursorPositions(3)` 回读 3 个；但 `View::insertText()` 只改主光标（`native-inserted-x=1`）。因此编辑语义由文档层单事务批量插入保证。
4. **`Document` 与 `View` 的生命周期**：view 必须先于 document 删除（首版先删 document 导致 selftest 段错误 139）；provider 里 document 挂到 view 下。

## 3. 我实际运行的命令与观察（headless）

### 3.1 独立 lane 构建 + ctest（`src/editor` 自包含）

```sh
bash -lc 'source scripts/lib-qt.sh; load_qt_versions; run_in_qt6_container bash -lc \
  "cmake -S src/editor -B target/edit-core-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
   cmake --build target/edit-core-build --parallel \$(nproc) && \
   ctest --test-dir target/edit-core-build --output-on-failure"'
```
观察：`cmake`-build exit 0；`1/1 Test #1: vedit-edit-core-selftest ... Passed 0.13 sec`；`100% tests passed, 0 tests failed out of 1`（`05-standalone-ctest.log`）。
同一二进制完整 CHECK 列表：`EDIT-CORE-SELFTEST ok=1 checks=53 failures=0`（独立构建无 C ABI hooks，边界两项打印 SKIP，故 53 = 55 − 2）。

### 3.2 仓库声明的 gate（Qt6 产品线）

```sh
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh
```
观察：`-- VEdit: edit-core lane wired (vedit_editor_core); KTextEditor is the product editor`；
`100% tests passed, 0 tests failed out of 16`（含 `10/16 Test #10: vedit-edit-core-selftest ... Passed`）；
`M0-SMOKE ok=1 platform=xcb ...`；`[qt6] PASS: ...`；`GATE-EXIT=0`（`02-gate-build.log`、`03-gate-ctest.log`）。构建是 `-Wall -Wextra -Wpedantic -Werror`。

### 3.3 产品 `--selfcheck`（含 edit-core 新增检查）

```sh
target/qt6-build/src/app/vedit-app --selfcheck --state-dir <tmp>   # offscreen
```
观察：`EDIT-CORE-SELFTEST ok=1 checks=55 failures=0`；`APP-SHELL-SELFTEST ok=1 checks=133 failures=0`；退出码 0（`04-app-selfcheck.log`，133 = 78 shell + 55 edit-core，`FAIL` 行计数 0）。
关键行：`CHECK editor.provider.id PASS`、`CHECK editor.pane.ktexteditor-widget PASS`、`CHECK editor.pane.open PASS`、`CHECK editor.pane.save PASS`、`CHECK editcore.undo.100-operations PASS 100`、`CHECK editcore.boundary.zero-calls PASS before=0 after=0`、`CHECK editcore.boundary.counter-live PASS before=0 after=1`。

## 4. 跨 C ABI 边界（AC-S1.1 后半句）

- 静态：两份 `libvedit_editor_core.a`（产品构建 + 独立构建）`nm -u | grep -c 'vd_'` = **0**，`nm --defined-only` 也没有 `vd_*`；`src/editor/kernel/` 无 `vd_ffi` 头、无 `vd_` 记号。
- 运行期：一整组编辑事务前后跨边界计数器 `before=0 after=0`；正控制一次真实 `vd_abi_version()` 调用使计数器 `0 → 1`（证明计数是活的）。
- 计数器递增点全部在产品唯一的 C ABI 使用处 `src/app/main.cpp` 的 `ffi` 命名空间（14 个调用点 + 1 个正控制）。
- 完整命令与输出：`07-cross-boundary.txt`。

## 5. 多光标结论（Open Q3，原文见 `06b-multicursor-conclusion.md`）

**原生位置级多光标：有**（`KTextEditor::View::setCursorPositions/cursorPositions`，since 5.95；6.24 头文件 + 运行期符号 + 实测 3 光标回读 3 个）。
**原生「一次编辑作用于全部光标」的文档 API：没有**（`View::insertText()` 只改主光标，实测 x=1）。
因此交互层用原生 view 光标集合，编辑语义层用单事务批量插入保证 A1b「一次 undo 全回滚」。无需与用户另议。

## 6. 未验证 / 局限（如实说明）

- **无 GUI/键盘级验证**：本机无显示服务器。KTextEditor 的**真实按键 -> 多光标键入**路径没有在无头环境实测（T14 探针已证明 offscreen 下 `QTest::keyClicks` 写不进字符，见 `evidence/edit-core/00-probe.md` §4）；已用原生 view API 断言 + 文档层编辑断言两条可复跑路径覆盖，并在 `06b` 中明确标注。
- **没有 xcb 下的 vedit-app 截图/窗口验证**：gate 的 xcb 烟测是 M0 smoke target，不是 vedit-app；`--selfcheck` 在 offscreen 下创建真实 KTextEditor view 并打开/保存文件，但没有“窗口可见/渲染正确”的判断（不能断言）。
- **性能基线（DESIGN 的 `perf_probe`，10 万行 A1a）不在本任务合同内**，未实现。
- **Ctrl+S / 菜单命令未接**：本任务交付内核 + provider + 自检；快捷键/命令注册属后续 lane（AC-S1.6 用户验收前需要）。
- **`fsync(dir)` 的真实 EINVAL 场景**未造出（本文件系统返回 0）；非致命分支用故障注入 `VEDIT_ATOMIC_FAULT=fsync-dir` 覆盖。

## 7. 跨 lane 集成问题（写域外，需 captain 决定）

产品 editor provider 换成 KTextEditor 后，两处既有「打开文件」仍按 demo 时代的写法直接
`qobject_cast<QPlainTextEdit *>(pane->editorWidget())`：

- `src/workspace/filetree/filetree_integration.cpp:47`（T39 刚修好的 filetree 双击打开）
- `src/workspace/palette/palette_integration.cpp:181`（命令面板打开）

KTextEditor pane 的 widget 是 `KTextEditor::View`，因此这两条路径在产品运行里会落到
“no editor seam connected yet” 分支（filetree 双击不再打开文件、palette 打开失效）。
`src/app/editor_area.h` 已提供统一入口 `EditorPane::openFile(path, &error)` / `currentFilePath()` / `saveFile()`
（内部走 provider 文档接缝），两个 lane 各改 3 行即可；这两个文件**不在 T10 写域**，我没有改。
请求：captain 决定是扩写域让我修，还是另开返修任务（建议后者也顺带给 palette 加同一条断言）。

## 8. 说明

- **未做任何 git 提交/建分支/推送**：多人共享工作树（其他 lane 有未提交改动），VCS 集成属于 captain/Lead 的人工停点。
- 仓库 gate 会刷新它自己的证据日志（`evidence/m0-build/build-logs/`、`evidence/ffi-abi/logs/`），这些路径在任务开始前就已是 modified/untracked；本任务自身改动仅为 §1 的文件 + `evidence/edit-core-v2/`。
- 证据清单：`00-pins.txt`（HEAD/镜像/改动路径）、`01-design.md`、`02-gate-build.log`、`03-gate-ctest.log`、`04-app-selfcheck.log`、`05-standalone-ctest.log`、`06a-multicursor-api.log`、`06b-multicursor-conclusion.md`、`07-cross-boundary.txt`、本报告。
