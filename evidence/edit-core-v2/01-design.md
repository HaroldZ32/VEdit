# Slice 1 实现说明 — KTextEditor 编辑内核进产品（T10 / AC-S1.1）

- Cell: `cell-20261031T085543161Z` · Board task: **T10** · 合同: `.mpd/cellwork/cell-20261031T085543161Z/contracts/edit-core.md`
- 需求：requirements rev2 **AC-S1.1**（`src/app/main.cpp` 调 `setEditorProvider`；打开/原子保存/查找替换/跳行/撤销重做在 selftest 有新增检查并全绿；多光标按 Open Q3 处理；编辑事务不过 C ABI）
- 所有者: **Senior Engineer (instance 2)** · 验证者: Reviewer（独立）
- 基线: `HEAD 95c496e`（分支 `feat/qt6-ktexteditor-migration`）· 环境: `vedit-qt6-build:local`（KTextEditor 6.24.0 / Qt 6.10.2）
- 只做 headless 验证（本机无显示服务器）。

## 1. 交付物

### 1.1 新 lane：`src/editor/kernel/` + `src/editor/CMakeLists.txt`

`vedit_editor_core`（STATIC，链 `KF6::TextEditor`）由 `src/app/CMakeLists.txt` 以
`add_subdirectory(../editor)` 接入 `vedit-app`（root `CMakeLists.txt` 归 build-base lane，本 lane 不改）。
`src/editor/CMakeLists.txt` 同时支持独立 `cmake -S src/editor -B <build>` + ctest。

| 文件 | 职责 |
|---|---|
| `kernel/document_io.{h,cpp}` | `EditorDocument` 包装 `KTextEditor::Document`：`setText/text/lines/lineLength/openFile/filePath/isModified/saveAtomic`；`loadFileIntoDocument()` 是打开的产品路径 |
| `kernel/atomic_save.{h,cpp}` | 同目录临时文件 → 写全 → `fsync(file)` → `rename` → `fsync(dir)`；失败清理临时文件；保留目标文件权限位；`fsync(dir)` 的 `EINVAL/ENOTSUP` 非致命（告警继续）；测试故障注入 `VEDIT_ATOMIC_FAULT=write\|fsync\|rename\|fsync-dir` |
| `kernel/undo_ops.{h,cpp}` | `undo()/redo()` 只能经运行期 metaobject：先按名字断言槽存在，再 `QMetaObject::invokeMethod`；缺失即失败，不静默降级（probe 约束 1） |
| `kernel/find_replace.{h,cpp}` | 迭代 `searchText`（一次只回一个匹配）、零长匹配前进 1 字符、自下而上、**单个 `EditingTransaction`** 完成全部替换（probe 约束 2） |
| `kernel/goto_line.{h,cpp}` | 行号 clamp 到 `[1, lines]`，光标置列 0，回读校验（A1d） |
| `kernel/multi_cursor.{h,cpp}` | 原生 `View::setCursorPositions/cursorPositions` + 文档层 `insertAtCursors()`（单事务、一次 undo 全回滚）——见 `06b-multicursor-conclusion.md` |
| `kernel/ktexteditor_provider.{h,cpp}` | `EditCoreProvider : vedit::app::EditorProvider`：每个 pane 一个 KTextEditor document+view；`openFile/currentFilePath/saveFile` 走内核 |
| `kernel/edit_core_selftest.{h,cpp}` | 自检（53 条独立检查），product `--selfcheck` 复用同一函数 |
| `kernel/cross_boundary.{h,cpp}` | 跨边界调用计数器（见 §3） |
| `kernel/edit_core_selftest_main.cpp` | 独立 selftest 可执行文件（ctest 条目） |

### 1.2 `src/app/` 接线（写域内）

| 文件 | 改动 |
|---|---|
| `editor_provider.h` | 新增**文档接缝** `openFile(editor, path, error)` / `currentFilePath(editor)` / `saveFile(editor, error)`，默认实现（header-inline）驱动 demo `QPlainTextEdit`；因此 lane 内核可独立链接、不需要链接 shell 实现 |
| `editor_area.h/.cpp` | `EditorPane::setProvider()`：保留 pane 与分屏布局，只替换 editor widget；`EditorPane::openFile/currentFilePath/saveFile` 转发到 provider；`EditorArea::setProvider()` 对**已存在**的 pane 全部重建 widget（shell 先建了 demo pane，main 之后才装真 provider） |
| `main.cpp` | 产品路径：`shell.setEditorProvider(vedit::editor::makeEditCoreProvider())`；`--selfcheck` 新增 edit-core 检查（打开/原子保存/查找替换/跳行/撤销重做/多光标/边界）；`ffi` 命名空间每个 C ABI 调用点加 `noteAbiCall()` 计数 |
| `CMakeLists.txt` | `add_subdirectory(../editor)` + 链 `vedit_editor_core` + `VEDIT_HAVE_EDIT_CORE=1` |

`--selfcheck` 的既有 78 条 shell 检查原样保留（共 133 条 = 78 + 55 edit-core），无一条被删/弱化。

## 2. 关键设计决定（都是实测结果逼出来的）

1. **打开文件不用 `KParts::openUrl()`**：探针实现里 `Document::openUrl()` 在 offscreen 下启动 KIO 路径并把自检进程**挂死**（`timeout` 124s，且视图窗口警告先出现）。改为内核自己读文件字节 → 拒绝二进制（含 NUL）→ `Document::setText()`，路径记在文档包装/视图属性 `vedit.openFilePath` 上。打开变得同步、可无头断言；原子保存路径不变。
2. **undo/redo 只能走 metaobject 槽**：`indexOfMethod("undo()")` 断言 + `invokeMethod(document, "undo")`（裸名）。用错签名形式会得到 `No such method ... undo()()`——实测踩过并在日志里留痕。
3. **Provider 在 shell 构造后安装**：`ApplicationShell` 构造时就建了 demo pane；`EditorArea::setProvider()` 改为重建现有 pane 的 editor widget（布局树、比例、活动 pane 不变），在自检里断言 pane widget 变成 KTextEditor view。
4. **头文件内联的默认文档接缝**：让 `src/editor` 独立构建时不必链接 `vedit_app_shell`（否则 provider 对基类虚函数/vtable 的引用在独立 ctest 里是未定义符号）。

## 3. 跨 C ABI 边界（AC-S1.1 后半句）

- 静态：`libvedit_editor_core.a`（产品构建与独立构建两份）**没有任何 `vd_*` 未定义/已定义符号**；`src/editor/kernel/` 里没有 `vd_ffi` 头、没有 `vd_` 记号。
- 运行期：`--selfcheck` 里 `editcore.boundary.zero-calls PASS before=0 after=0`（一整组编辑事务前后计数器零增量），正控制 `editcore.boundary.counter-live PASS before=0 after=1`（一次真实 `vd_abi_version()` 调用使计数器 +1，证明计数器是活的）。
- 计数器递增点：`src/app/main.cpp` 的 `ffi` 命名空间（本产品唯一的 C ABI 调用处），共 15 处（14 个 ABI 调用点 + 1 个正控制）。
- 完整命令与输出：`07-cross-boundary.txt`。

## 4. 与 `evidence/edit-core/DESIGN.md` 的差异

- 不拆 `undo_probe` / `perf_probe` 独立文件：100 步 undo/redo 与多光标断言放进 `edit_core_selftest.cpp`；**perf_probe（10 万行 A1a 基线）不在本任务合同内，未实现**（未覆盖项，不冒充）。
- 多光标落地方式与 DESIGN 不同（位置列表替代 MovingCursor），理由见 `06b-multicursor-conclusion.md` §4。
- 打开路径不用 `openUrl`（见 §2.1）。

## 5. 已知跨 lane 集成问题（不在 T10 写域，需 captain 决定）

产品里 `EditorProvider` 换成 KTextEditor 后，两个既有 lane 的「打开文件」仍按 **demo 时代**的写法直接
`qobject_cast<QPlainTextEdit *>(pane->editorWidget())`：

- `src/workspace/filetree/filetree_integration.cpp:47`（T39 刚修好的 filetree 双击打开）
- `src/workspace/palette/palette_integration.cpp:181`（命令面板打开）

KTextEditor pane 的 widget 是 `KTextEditor::View`，不是 `QPlainTextEdit`，因此这两条打开路径在**产品运行**里会走到
“no editor seam connected yet” 分支。`src/app/editor_area.h` 已提供统一入口
`EditorPane::openFile(path, &error)` / `currentFilePath()` / `saveFile()`（内部走 provider 的文档接缝），
两个 lane 各改 3 行即可；但那两个文件不在 T10 写域，本任务**没有改**，留给 captain 决定（见 REPORT.md §7）。
