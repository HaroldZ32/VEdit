# T14 设计说明 — KTextEditor 编辑内核（先设计后动手）

- cell `cell-20261031T014507403Z` · task **T14 / edit-core** · owner: Deep Worker
- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/edit-core.md`
- 写域：`src/editor/`、`evidence/edit-core/`
- 本文件先于实现写定：**API 事实来自镜像里安装的头文件**，不是记忆。

## 0. 已验证的 API 事实（KF6 KTextEditor 6.24.0，头文件原文）

| 能力 | API | 依据 |
|---|---|---|
| **撤销分组（关键）** | `KTextEditor::Document::EditingTransaction`（RAII；构造即 start，析构即 finish） | `document.h:130-131`「Text manipulation actions can be grouped together to one undo/redo action」、`:610-650` 的 `EditingTransaction` 文档块（示例：`EditingTransaction t(document); document->insertText(...)`） |
| 搜索 | `QList<Range> Document::searchText(Range, const QString&, SearchOptions)` | `document.h:1075` |
| 替换 | `bool Document::replaceText(Range, const QString&, bool block=false)` | `document.h:979/993` |
| 移动光标 | `KTextEditor::MovingCursor`（`setPosition`、`position`） | `movingcursor.h:122/206` |
| 移动区间 | `KTextEditor::MovingRange`（`setRange`） | `movingrange.h:230/245/262/413` |
| 多光标 | **没有原生多光标 API**（6.24 头文件无 `addCursor`/`MultiCursor`） | 在 `KTextEditor/` 头目录 grep 无命中；Kate 的多光标是编辑器自己的实现 |

**结论（A1b 的设计点）**：多光标必须由应用用 N 个 `MovingCursor` + **单个 `EditingTransaction`** 实现。
「一次撤销全部回滚」的保证来自事务边界，不是来自逐个插入后手工合并 undo 栈。

## 1. 组件划分（`src/editor/`）

| 文件 | 职责 | 契约条款 |
|---|---|---|
| `document_io.{h,cpp}` | 文档模型与打开/保存：`EditorDocument` 包装 `KTextEditor::Document`；`open(QUrl)`、`save()`（走原子写）、`text()/setText()`（测试路径） | (a) |
| `atomic_save.{h,cpp}` | 原子保存：同目录临时文件 → 写入 → `flush` → `fsync(file)` → `rename` → `fsync(dir)`；失败清理临时文件；**故障注入**（环境变量 `VEDIT_ATOMIC_FAULT=write\|fsync\|fsync-dir\|rename` 与 `VEDIT_ATOMIC_KILL_AT=`） | (b) A1e |
| `find_replace.{h,cpp}` | 文件内查找/替换；`replaceAll` 在**一个 `EditingTransaction`** 内完成；返回替换次数 | (c) A1d |
| `goto_line.{h,cpp}` | 跳行：行号 clamp 到 `[1, lines]`、列 0、`setCursorPosition`，并把结果回读校验 | (d) A1d |
| `multi_cursor.{h,cpp}` | N 个 `MovingCursor`；`insertAtCursors(text)` 在**单个事务**内自下而上插入；`undoAll()` 一次撤销 | (e) A1b |
| `undo_probe.{h,cpp}` | 应用 K 次编辑，撤销/重做 ≥100 步并逐步比对文本 | (e) |
| `perf_probe.{h,cpp}` | ≥10 万行文档：加载耗时、`searchText` 全量耗时、事务内 N 次插入耗时；输出 `PERF key=value` 行作为 **A1a 基线入口** | (f) |
| `edit_core_selftest.cpp` | 自检驱动（`main` + CHECK 宏，不依赖 QtTest）：A1b/A1d/A1e 全部断言 + 退出码 | 全部 |
| `CMakeLists.txt` | 独立可构建：`vedit_editor_core`(STATIC) + `vedit-edit-core-selftest` + `add_test`；同时可作为 `add_subdirectory` 被 root 收录（root 的 lane 列表由 build-base lane 维护，本 lane 不编辑 root） | — |
| `README.md` | 集成片段（root 收录方式）、测试命令、A1a 基线用法 | — |

## 2. 各条款的实现要点与判据

### (a) 文档模型 / 打开 / 保存
- 用 `KTextEditor::Editor::instance()->createDocument(nullptr)`；打开用 `openUrl(url)`（产品路径）与 `setText()`（测试路径，避免文件系统噪声）；
- 保存走 `AtomicSave` 写文件（不依赖 KTextEditor 的 save，便于注入故障）；保存成功后清除 modified 标记（`setModified(false)`）。

### (b) 原子保存（A1e）
- 顺序：`open(tmp, O_WRONLY|O_CREAT|O_EXCL, 0600)` → 写全量 → `fsync(fd)` → `close` → `rename(tmp, target)` → `open(dir)` → `fsync(dirfd)`；
- 临时文件名 `.<target>.vedit-tmp-<pid>-<counter>`，与目标**同目录**（跨设备 rename 不原子）；
- **故障注入矩阵**（测试逐个执行）：
  1. `write` 阶段失败 → 目标文件字节不变；临时文件被清理；
  2. `fsync(file)` 失败 → 不 rename；目标不变；
  3. `rename` 前 `SIGKILL`（子进程） → 目标不变；只可能留下临时文件（下一次保存前清理）；
  4. `fsync(dir)` 失败 → rename 已发生（目标已是新内容），记录为「文件系统级妥协」，测试断言**不是半截文件**（新内容完整）。
- 判据：任何注入点之后，`target` 要么是旧内容的完整字节、要么是新内容的完整字节，**绝无混合**；`sha256` 与长度双重校验。

### (c) 查找替换（A1d）
- 查找：`searchText`（区分大小写/正则用 `SearchOptions`）；
- `replaceAll`：先收集所有 ranges，再在**一个 `EditingTransaction`** 内**自下而上**替换（避免偏移失效），返回 `replaced` 计数；
- 判据：替换后文本等于预期；`undoCount`/一次 `undo()` 后文本**完全回到**替换前；再 `redo()` 回到替换后。

### (d) 跳行（A1d）
- `gotoLine(n)`：`n` clamp；`setCursorPosition({n-1, 0})`；回读 `cursorPosition()` 断言；
- 边界：`n=0` → 1、`n>lines` → lines、空文档 → `{0,0}`；
- 另外提供 `ensureVisible` 回调钩子（view 层实现，core 不依赖 view）。

### (e) 撤销/重做 ≥100 步 + 多光标（A1b）
- 100+ 次编辑：每次 `insertText` 一个唯一标记；逐步 `undo()` 100 次并断言文本逐步回退，再 `redo()` 100 次断言恢复；
- 多光标：N=3/10/50 三档，位置分散；`insertAtCursors` 单事务；断言：插入后每处都出现文本；**一次 `undo()` 后全文回到插入前**（不是部分回滚）；再 `redo()` 全部恢复；
- 判据用「全文 sha256 + 每个位置的内容」双校验，避免只查一个位置。

### (f) ≥10 万行 + A1a 入口
- 生成 120,000 行（`module m<i>; ... endmodule`），加载/搜索/事务插入耗时；
- 输出机器可读：`PERF lines=120000 open_ms=… search_ms=… insert50_ms=…`；
- 入口：`vedit-edit-core-selftest --perf` 与 `--perf-lines=N`；acc-editor 可直接消费这些 key=value 产出 A1a 基线。

## 3. 测试与构建策略（不改 root CMake）

- `src/editor/CMakeLists.txt` 自带 `project(...)` 守卫：既能在本目录独立 `cmake -S src/editor -B <build>`，也能被 root 的 lane 列表 `add_subdirectory(src/editor)` 收录；
- 本 lane 的构建/测试在固定镜像 `vedit-qt6-build:local` 内执行：`cmake -S src/editor -B <work> && cmake --build <work> && ctest --test-dir <work> --output-on-failure`；
- 证据：`evidence/edit-core/00-pins.txt`（镜像/版本/命令/退出码）、`01-build.log`、`02-selftest.log`（全部 CHECK 原样）、`03-perf.log`、`04-atomic-faults.log`、`09-result.txt`（failures=0）。

## 4. 设计风险与取舍

| 风险 | 取舍 |
|---|---|
| `EditingTransaction` 在无 view 的 headless 环境下是否仍合并 undo | 先用探针验证（K 次插入 + 1 次 undo → 文本全回）；若不合并，退回「应用层 undo 栈」（自己记录操作并批量回滚），并把结论写进证据 |
| `Document::replaceText` 的 `block` 参数与事务叠加 | 统一用 `EditingTransaction`，不混用 `block=true` |
| 原子写在 NFS/overlay 上的 `rename` 语义 | 只承诺 POSIX 语义（同目录 rename 原子）；证据注明文件系统类型 |
| 10 万行 + 语法高亮的内存/耗时 | perf 只测编辑核心（文档/搜索/插入），不启高亮；基线由 acc-editor 结合 UI 侧产出 |

## 5. 完成定义（DoD）

- 上述条款全部有**可执行断言**，`ctest`/`--selfcheck` 退出码 0；
- A1e 的故障注入**逐个注入点**都有日志；A1b 的多光标**一次撤销全回滚**有全文 sha256 证据；
- 证据目录完整、命令可复跑；未改 `src/app/`、未改 root `CMakeLists.txt`。
