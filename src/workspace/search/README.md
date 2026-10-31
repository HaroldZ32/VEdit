# ws-search — 全局搜索（任务 T23 / 验收 A2c）

Lapce 对齐的全局搜索：**命中文件 + 行号 + 片段**、**点击跳转**、**可辨空状态**、
**可取消且不冻结 UI**。跨文件替换是需求 rev 2 的非目标 5，本 lane 不实现。

- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-search.md`
- 需求：`requirements.md` rev 2 · A2c
- 接缝：`evidence/app-shell/06-seams.md` §2（PanelRegistry）+
  `src/workspace/integration.h`（B 的通用 lane 入口，约定见 `evidence/ws-palette/SEAM.md`）

## 目录与职责

| 文件 | 职责 |
|---|---|
| `search_types.h/.cpp` | 选项、命中、摘要、`JumpRequest`、`IJumpTarget`、Verilog 扩展名表 |
| `global_search.h/.cpp` | 后端：工作线程目录遍历 + 逐行字面匹配 + 批量回传 + 取消/限额/时间预算 |
| `search_panel.h/.cpp` | 面板：查询框、结果树（文件分组：文件 / 行号 / 片段）、空状态、点击跳转 |
| `search_integration.h/.cpp` | lane 入口 `installSearch(shell, options)`：注册 `PanelRegistry` 工厂，并安装产品跳转目标 `EditorJumpTarget` |
| `search_jump_target.h/.cpp` | **产品跳转消费者**：复用 filetree lane 的 open seam `openFileInEditor()` 打开命中文件，再把光标定位到命中行/列（edit-core 落地后替换其实现） |
| `search_selftest.cpp` | 自检（root 构建 95 项 CHECK；非 root 另含不可读 root 用例）与截图（装进真实 `ApplicationShell` 的 search dock） |
| `CMakeLists.txt` | target `vedit_workspace_search`；独立配置 = 核心 lane（无 shell） |

## 数据流

```
SearchPanel::startSearch()
  → GlobalSearch::start(options)            立即返回（QThread::create）
      worker: 递归遍历 → 跳过/计数 → 逐行匹配 → 每 64 条或 40 ms 批量 invokeMethod 回主线程
  ← 主线程: hitsReady(batch)                逐批追加结果树
  ← 主线程: finished(summary)               文案 + 空状态 + searchFinished
```

- **不冻结 UI**：扫描在 `QThread` 上；`start()` 在 96 MiB 文件样本上实测 < 100 ms 返回
  （`CHECK large.start.non-blocking`），工作线程身份由 `ranOnWorkerThread()` 断言。
- **生命周期（verdict M2，T64 修复）**：每次扫描一个 `std::shared_ptr<std::atomic_bool>`
  取消令牌，worker **按值**持有它、从不读 `GlobalSearch` 的成员；析构时 `cancel()` 后
  **无上限等待** worker 真正退出（旧实现只等 10 s，而扫描预算是 15 s，慢盘 `read()` 仍在跑时
  对象就被释放 → heap-use-after-free，证据 `evidence/search-uaf/`），然后确定性地回收
  `QThread`（断连 `finished→deleteLater` + `removePostedEvents` + `delete`）。
  代价：卡死的文件系统会拖慢销毁；这是“宁可不释放，也不在 worker 脚下释放”的取舍。
- **可取消**：`cancel()` 只置原子标志；扫描每 256 行 / 每个目录条目检查一次；
  旧搜索的结果由 generation 过滤，不会串到新搜索里。
- **限额可观测**：`SearchSummary` 带 `skippedBinary / skippedTooLarge /
  skippedUnreadable / truncated / timedOut / elapsedMs`，面板摘要行显示
  `(1 too large skipped, 1 binary skipped)` 这类附注。

## 边界（写进实现而不是注释里）

| 情况 | 行为 |
|---|---|
| 文件大小 > `maxFileBytes`（默认 8 MiB） | **不读进内存**，`skippedTooLarge++`；100 MiB 文件不会拖死搜索 |
| 内容含 NUL 字节 | 判为二进制，`skippedBinary++` |
| 文件/目录不可读 | `skippedUnreadable++`，继续搜其余部分 |
| 达到 `maxResults`（默认 2000） | 停止，`truncated=true`，摘要写 `result limit N reached` |
| 超过 `maxDurationMs`（默认 15 s） | 停止，`timedOut=true`，摘要写 `time budget N ms reached` |
| 符号链接 | 不跟随（防环、防逃出根目录） |
| 默认剪枝目录 | `.git .svn .hg build target node_modules .cache .idea .vscode CMakeFiles .ccache .venv` |
| 隐藏文件 | 不匹配（`QDir` 默认不含 `QDir::Hidden`） |
| 同一行多个命中 | 该行只出 1 条（首次出现的列号，1-based） |

## 空状态（可辨，不是一句 "No results"）

| 场景 | 文案 |
|---|---|
| 无工程根 | `No project root. Open a project to search its files.` |
| 查询为空 | `Enter a query and press Search.` |
| 搜索完成但无命中 | `No results. Nothing matched "<q>" in <root>.` |
| 根目录不可读（失败） | `Search failed: search root is not readable: <root>` |
| 时间预算内无命中 | `No results within the <N> ms time budget.` |
| 取消且尚无命中 | `Search cancelled before any hit.` |

对应自检：`empty.no-project.* / empty.query.* / empty.no-results.* /
empty.unreadable-root.* / large.timeout.*`。

## 点击跳转（接缝）

`SearchPanel` 结果子行点击/回车 → `activateHit(index)`：

```cpp
emit jumpRequested(JumpRequest{filePath, line, column});
if (jumpTarget_) jumpTarget_->jumpTo(request);
```

- `IJumpTarget`（`search_types.h`）是编辑器接缝；**产品路径**由 `installSearch()` 安装的
  `EditorJumpTarget` 消费（`search_jump_target.h`）：它调用 filetree lane 的
  `vedit::workspace::openFileInEditor()`（T21/T39 的**唯一** open seam，不另造打开逻辑），
  再把活动 pane 编辑器的光标放到命中 `line`/`column`。edit-core 落地后只替换该实现。
- 自检同时覆盖两层：`CHECK jump.click.*`（QTest 真鼠标点击）→ `CHECK jump.demo.opened/line`
  （面板 + 夹具目标），以及 `CHECK jump.product.*`（真实 `ApplicationShell` 里点击命中，
  断言文档文本 == 磁盘字节、光标行号/列号 == 命中行/列、`vedit.openFilePath` == 命中路径、
  状态栏含路径）。截图 `03-search-jump.png` 为 offscreen 抓图，不作为“看起来正确”的证据。
- 不做“跨文件替换”（非目标 5）。

## 外壳接入（B 的通用接缝）

`src/app/CMakeLists.txt` 以 `workspace/<lane>` 方式纳入本目录；lane 入口：

```cpp
// src/workspace/integration.cpp（B 维护）
installSearch(shell, options);   // 本 lane 提供：注册工厂 + 安装跳转目标
```

`installSearch` 内部：

```cpp
auto *jumpTarget = shell.findChild<search::EditorJumpTarget *>();  // 每个 shell 一个
if (jumpTarget == nullptr) jumpTarget = new search::EditorJumpTarget(&shell);
shell.panels()->setFactory("search", [root, jumpTarget] {
    QWidget *panel = search::createSearchPanel(root);
    if (auto *searchPanel = qobject_cast<search::SearchPanel *>(panel))
        searchPanel->setJumpTarget(jumpTarget);   // 点击命中 → 打开 + 定位
    return panel;
});
```

因此 `vedit-app` 的 Search 面板（View 菜单 / `Ctrl+Alt+S`）就是真实实现，
没有第二套注册路径，也不改外壳布局（面板仍然“按需插入”）。

## 构建与自检

全仓（权威路径，lane 由 `src/app` 自动纳入）：

```bash
bash scripts/build-qt.sh        # 或 build/gates/qt6-cxx.sh
```

独立配置（只构建核心 lane，不链接 shell；快速回路）：

```bash
cmake -S src/workspace/search -B target/search-build -G Ninja
cmake --build target/search-build
ctest --test-dir target/search-build --output-on-failure
```

自检与截图：

```bash
vedit-workspace-search-selftest --selfcheck
vedit-workspace-search-selftest --screenshots evidence/ws-search
```

## 已知陷阱与限制（如实登记）

1. **`.v` 会被 KSyntaxHighlighting 解析成 "V"（vlang）**（`evidence/m0-build/REPORT.md` §4.9）。
   本 lane 的文件类型判断**完全不用** KSyntaxHighlighting：`isVerilogFileName()` 是显式扩展名表
   （`.v .vh .sv .svh`，大小写不敏感），库只链接 Qt6 Widgets。
2. **不解析 `.gitignore`**：只按固定目录名剪枝（见上表）；Lapce 的 ignore 语义不在 A2c 范围。
3. **单行只报一次**：同一行多次出现不重复计数（结果条数 = 命中行数）。
4. **无正则/整词匹配**：A2c 只要求文本搜索；需要时再加，不改结果契约。
5. **截图里 CJK 文件名显示为方框**：构建镜像没有安装 CJK 字体（`fc-list` 无 CJK），
   数据正确性由 `CHECK panel.hit.unicode-*`（文件、列号）断言；交付 AppImage 会带字体，
   属于打包 lane 的范围。
6. **权限用例在 root 下 SKIP**：镜像内默认以 root 运行，`chmod 000` 不生效；
   `05-selfcheck-nonroot.log` 用 `setpriv --reuid=1000` 跑同一套用例（0 SKIP，83 checks）。
7. **搜索期间的面板刷新是逐批的**：不额外做进度节流 UI（小工程 0 ms 完成，大工程靠批次到达）。
