# B2 — 搜索面板与 Ctrl+F 对齐 + 全工程搜索/替换（item 5）

契约：`.mpd/plans/ux-batch-2/00-plan.md` §B2 + §1/§2。写作用域：`src/workspace/search/**`
（含其 CMakeLists）、`src/editor/kernel/find_replace.{h,cpp}`。

## 1. 交付内容（路径 + 符号）

| # | 交付 | 位置（路径 + 符号） |
|---|---|---|
| 1 | **两个界面共用的选项模型**（大小写 / 整词 / 正则 / 范围） | 新增 `src/workspace/search/search_query.h`：`vedit::search::QueryOptions`、`SearchScope`、`SearchHandoff`、`SearchHub`（header-only，QtCore-only）。`vedit::editor::FindOptions` 现在是 `QueryOptions` 的别名（`src/editor/kernel/find_replace.h`），因此浮条与面板用的是**同一个类型**，不是两份实现 |
| 2 | 搜索面板：整词/正则开关、范围选择器、替换行（「替换」/「全部替换」） | `src/workspace/search/search_panel.{h,cpp}`：`SearchPanel::queryOptions()/setQueryOptions()`、`setScope()/scope()`、`setWholeWords()/wholeWords()`、`setRegex()/regex()`、`replaceCurrentHit()`、`replaceAllRequested()`、`setReplaceConfirmHandler()`、`setFileRewriter()`、`lastReplaceOutcome()`；控件 objectName `search-whole`、`search-regex`、`search-scope`、`search-replacement`、`search-replace`、`search-replace-all`、`search-replace-status` |
| 3 | 「替换」= 只替换当前命中并前进 | `SearchPanel::replaceCurrentHit()` → `ProjectReplacer::start(单条 hit)` → 完成后 `SearchPanel::onReplaceFinished()` 重新搜索并在 `onSearchFinished()` 里 `activateHit(同一索引)`（即下一条） |
| 4 | 「全部替换」= 先确认（文件数/命中数）再按文件分组、每文件一次原子写、计数、刷新 | `SearchPanel::replaceAllRequested()` → `beginReplaceAllScan()`（`allMatches=true`）→ `confirmAndReplace()` / `confirmReplace()`（默认 `QMessageBox`）→ `ProjectReplacer`（新增 `src/workspace/search/project_replace.{h,cpp}`，`applyReplaceRanges()` 只应用 Rust 报告的精确范围）→ `atomicSaveRewriter()` = 既有 `vedit::editor::atomicSaveText`（失败注入/计数由 `FileRewriter` 注入点支持） |
| 5 | Ctrl+F 的当前文件/全工程开关 + 交接（查询词与选项双向同步） | `src/editor/kernel/find_replace.{h,cpp}`：`FindReplaceBar::scope()/setScope()`、`queryOptions()`、`handoff()`、`requestProjectSearch()`、`publishToHub()/adoptFromHub()`；控件 objectName `vedit-find-scope`；全工程时 `recompute()` 不再做单文件匹配，状态行显示 `Whole project: …`；`search_panel.cpp` 注册 `SearchHub` 的工程搜索处理器，`src/workspace/search/search_integration.cpp` 提供“面板尚未创建时先插 dock 再交接”的兜底处理器 |
| 6 | 未保存缓冲区规则 | 新增 `src/workspace/search/document_sync.{h,cpp}`：`OpenDocumentBridge`（`modifiedPaths()/isOpen()/save()/refresh()`），产品实现枚举真实 KTextEditor view；`SearchPanel::openDocumentsInScope()` + `ensureBuffersSaved()` 决定“先保存或拒绝”。**未修改的打开文档不会被多写一次**（`save()` 对 `isModified()==false` 是 no-op），因此一次替换在文件层面仍只有一次写 |
| 7 | 异步（GUI 线程不遍历目录、不逐行匹配） | 遍历/匹配仍在 Rust `vd_search`（`GlobalSearch` 工作线程轮询 + 取消标记不变）；替换的文件读写/范围应用在新增 `ProjectReplacer` 的工作线程；面板只做计划、确认与刷新 |

辅助改动（同一写作用域内）：

- `src/workspace/search/search_types.h`：`SearchOptions` 增加 `wholeWords/regex/allMatches/fileFilter`；
  `SearchHit` 增加 `endColumn/matchLength`（Rust `VdSearchHit` 的精确范围）；新增
  `ReplaceHit/ReplacePlan/ReplaceOutcome/ReplaceWriteResult`。
- `src/workspace/search/global_search.cpp`：改用 A1 已交付的 `vd_search_tree_query_ex`
  （整词/正则/每行全部命中/扩展名过滤）；`verilogOnly` 用文档化扩展名表 `v/vh/vlg/sv/svh`
  表达（`VdSearchOptions` 没有 verilog 标志）；`fileFilter`（当前文件范围）只过滤上报集合，
  并**按上报集合**重算 `filesMatched/hitCount`。
- `src/workspace/search/CMakeLists.txt`：新增源文件、`VEDIT_SEARCH_HAVE_EDITOR_CORE` 下的
  原子保存 rewriter、以及新 ctest `vedit-search-replace-tests`（仅在有 Rust 搜索服务时注册）。
- `src/workspace/search/README.md`：记录替换/交接/未保存缓冲区规则与两个引擎的差异。

**未改动**（遵守写作用域）：`src/app/**`、`src/workspace/diagnostics/**`、
`src/workspace/module_tree/**`、`crates/**`、`docs/USER_GUIDE.md`、`translations/**`。

## 2. 验收矩阵（全部来自真实运行输出）

| 验收点 | 结果 | 证据 |
|---|---|---|
| (i) 两个界面暴露同一组选项 | PASS | `02-ctest-new.log`：`model.dock.options.round-trip`、`model.both-surfaces.same-options`、`model.bar.options` 等 |
| (ii) 「替换」只改一处并前进 | PASS | `replace-one.*`（含 `replace-one.advances`，替换后索引 1 = a.v:3:22） |
| (iii) 「全部替换」改且只改预期处、计数正确、每文件一次写 | PASS | `replace-all.*`（12 处 / 6 文件；计数写入器断言每文件 1 次、总数 6；磁盘无残留 `foo`） |
| (iv) 正则/整词/大小写 | PASS | `options.*`（大小写 11/10/1、整词 10、正则 `^assign foo` 2、CJK 列 7） |
| (v) Ctrl+F 全工程交接后查询词与选项一致 | PASS | `handoff.*`（`handoff.dock.query`、`handoff.dock.options`、`handoff.dock.project-results`=10、双向 `handoff.sync.dock-to-bar.*`）以及**产品接线** `shell-handoff.*`（经 `installSearch()` 的兜底处理器把 dock 按需创建出来，查询词/选项一致、有工程结果） |
| (vi) 未保存缓冲区：先保存或拒绝 | PASS | `unsaved-buffer.saved-before-replace`、`unsaved-buffer.buffer-reloaded`、`unsaved-buffer.refused*`（注入无法保存的桥 → `replaceAllRequested()==false` 且磁盘未变） |

汇总行（实测）：`SUMMARY checks=81 failures=0 skips=0`，退出码 0。
临时夹具工程的前后 diff 在 `fixture/before-after.diff`（`before/`、`after/` 是完整快照）。

> 开发记录（诚实登记）：`shell-handoff.*` 这组**产品接线**用例在第一次运行时抓到一个真实缺陷——
> 面板在 `SearchHub::requestProjectSearch()` 的处理器回调里注册自己，导致正在执行的
> `std::function` 因 `QList` 重新分配而失效（段错误，退出码 139）。修复方式是调用前先拷贝
> 处理器列表（`src/workspace/search/search_query.h`）。修复后 81/81 通过。

另：既有回归同时重跑并通过：`vedit-edit-core-selftest`、`vedit-editor-find-replace-selftest`、
`vedit-workspace-search-selftest`（见 `03-affected-ctest.log`），全量 ctest 见 `04-full-ctest.log`。

## 3. 给 captain / 用户手册的两条口径（B2 item 6）

### 3.1 RE2 与 QRegularExpression 的差异（必须写进用户手册）

> **工程级搜索/替换**使用 Rust `regex` crate，语义是 RE2 风格：**不支持反向引用
> （`\1`、`(?P=name)`）和 lookaround（`(?=…)`、`(?<=…)`、`(?!…)`、`(?<!…)`）**。
> **Ctrl+F 单文件搜索**使用 `QRegularExpression`（PCRE 风格），上述语法可用。
> 两个界面的大小写 / 整词 / 正则 / 范围四个选项**语义一致**，但**正则方言不同**：
> 在 Ctrl+F 中可用的反向引用/lookaround 放进搜索面板会得到“无效正则”错误
> （面板把 Rust 侧的错误原文显示出来，不会静默无结果）。

实现事实（供核对）：

- 工程级：`crates/vd-search/src/lib.rs::compile_matcher` —— `regex::RegexBuilder`，
  `case_insensitive(!case_sensitive)`，`whole_word` 用 `\b(?:…)\b` 包裹，字面量用
  `regex::escape`；错误经 `VdError` 回到 `GlobalSearch` 的 `SearchSummary::error`。
- 单文件：`src/editor/kernel/find_replace.cpp::findMatches` —— 先
  `QRegularExpression(needle).isValid()` 校验，再走 `KTextEditor::Document::searchText`；
  整词在 C++ 侧用自定义边界判定（对正则模式也生效）。

### 3.2 未保存缓冲区规则（必须写进用户手册）

> 全工程替换前，如果被替换的文件正在编辑器里打开且**有未保存修改**，VEdit 会**先保存**
> 这些文档（以你屏幕上的内容为准），然后才执行替换；只要有一个文档保存失败，
> **整个替换被拒绝**，磁盘上不会发生任何写入。替换完成后，受影响的打开文档会
> 从磁盘重新载入，所以编辑器里看到的就是写进磁盘的内容。
> 范围是“当前文件”时，只检查并保存当前文件；范围是“全工程”时，检查工程根下的所有
> 已打开文档。

## 4. 已知限制 / 未证明项（如实登记）

1. **只验证了 offscreen + 真实 KTextEditor**：没有 xcb 真实鼠标点击“替换”按钮的截图；
   acceptance 要求的 ctest 是 offscreen 的（与契约一致），真机观感仍需用户验收。
2. **跨文件的批次原子性不是事务**：单文件是原子写（`atomicSaveText`），但一次「全部替换」
   中途若某个文件写失败，之前已写的文件保持已写状态；结果里 `ok=false` + 原因 +
   已写文件计数，绝不谎报全部成功。契约只要求“每文件一次原子写”，未要求跨文件事务。
3. **TOCTOU**：命中范围来自统计扫描，替换时文件若被外部改动且范围对不上，结果会拒绝并报
   “the file changed”；若改动恰好保持范围有效（例如同长度覆盖），则无法检测——未做文件哈希复核。
4. **非 UTF-8 文件**：Rust 侧 lossy 解码后仍可能产生命中；替换侧检测到非 UTF-8 会跳过该文件
   并列入 `skippedFiles`，不重写（避免损坏字节）。因此“命中数”与“实际替换数”在这种工程里
   可能不同，结果行会显示跳过数。
5. **翻译**：新增 UI 串（Whole Word/Whole Project/Replace …）走 `tr()`，`translations/**`
   属于 A2 写作用域，本 lane 未改；zh_CN 覆盖由 captain 的翻译合并步骤补齐。
6. **搜索面板保留了既有的 “Verilog” 扩展名过滤开关**：它是**文件集合过滤**，不属于四个
   共用选项；浮条对单文件没有对应概念，因此不共享它（四个共用选项本身在两边完全一致）。
7. **两个界面的初始开关状态是 UI 策略而非语义差异**：面板默认“不区分大小写 + 全工程”，
   浮条默认“区分大小写 + 当前文件”（保持既有行为）；任一界面被用户改动后立即双向同步。

## 5. 复现命令

```bash
# 官方门禁（Rust 层 + configure + build + 全量 ctest + xcb smoke）
bash scripts/build-qt.sh

# 本 lane 的验收 ctest（offscreen；证据写到 build 目录）
ctest --test-dir target/qt6-build -R vedit-search-replace-tests --output-on-failure
# 直接运行并保留 CHECK 行与 before/after 夹具：
target/qt6-build/src/app/workspace-lane-search/vedit-search-replace-tests \
    --selfcheck --evidence evidence/ux-batch-2/b2/fixture
```

注意：容器内运行 ctest 时路径前缀是 `/vedit-build/...`（`scripts/build-qt.sh` 的挂载点），
宿主机上直接 `ctest` 会因测试命令是容器内绝对路径而 “Not Run”。
