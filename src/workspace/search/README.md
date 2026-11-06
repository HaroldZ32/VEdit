# ws-search — 全局搜索（任务 T23 / 验收 A2c）

Lapce 对齐的全局搜索：**命中文件 + 行号 + 片段**、**点击跳转**、**可辨空状态**、
**可取消且不冻结 UI**。ux-batch-2 B2 推翻了“跨文件替换是非目标”的旧限制：本 lane 现在提供
**全工程替换**（「替换」/「全部替换」两个按钮）与 Ctrl+F 浮条的**当前文件/全工程**范围开关，
两个界面共用同一套选项模型（`search_query.h`）。

- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-search.md`
- 需求：`requirements.md` rev 2 · A2c
- 接缝：`evidence/app-shell/06-seams.md` §2（PanelRegistry）+
  `src/workspace/integration.h`（B 的通用 lane 入口，约定见 `evidence/ws-palette/SEAM.md`）

## 目录与职责

| 文件 | 职责 |
|---|---|
| `search_types.h/.cpp` | 选项、命中、摘要、`JumpRequest`（UI 位置值）、Verilog 扩展名表；**不含**跳转接口（A-1/T20 统一到 `src/workspace/jump`） |
| `global_search.h/.cpp` | 后端（T21）：**只消费 Rust 搜索服务** `vd_search_tree_query`/`vd_search_tree_poll`；无 Qt 目录遍历、无 Qt 行匹配。工作线程负责轮询与取消，命中按 200 条分批回主线程 |
| `search_query.h` | **两个界面共用的选项模型**（大小写 / 整词 / 正则 / 范围）与 `SearchHub` 双向同步接缝（header-only，编辑器内核可反向包含，不产生链接依赖） |
| `document_sync.h/.cpp` | 未保存缓冲区桥：列出被修改的打开文档、经原子保存路径落盘、替换后重载；没有编辑器内核时返回 nullptr（不静默跳过规则） |
| `project_replace.h/.cpp` | 全工程替换引擎：**只应用 Rust 匹配器报告的精确字符范围**（不重新匹配）；按文件分组、每文件一次原子写；工作线程 + 取消；非 UTF-8 文件只报告不改写 |
| `search_replace_selftest.cpp` | ux-batch-2 B2 验收 ctest（`vedit-search-replace-tests`）：选项集合一致、替换一条并前进、全部替换计数与“每文件一次写”、正则/整词/大小写、Ctrl+F 交接、未保存缓冲区 |
| `search_panel.h/.cpp` | 面板：查询框、结果树（文件分组：文件 / 行号 / 片段）、选项（大小写/整词/正则/范围）、替换行（替换/全部替换）、空状态、点击跳转 |
| `search_integration.h/.cpp` | lane 入口 `installSearch(shell, options)`：注册 `PanelRegistry` 工厂，并把产品跳转目标 `EditorJumpTarget` 注册进 `jump::Registry`（全产品唯一 target） |
| `search_jump_target.h/.cpp` | **全产品跳转目标**（`jump::IJumpTarget`）：经唯一 open seam `EditorPane::openFile()` 在活动窗格打开文件，再把光标定位到 1-based 行/列；搜索/模块树/诊断/文件树/面包屑共用 |
| `search_selftest.cpp` | 自检（root 构建 95 项 CHECK；非 root 另含不可读 root 用例）与截图（装进真实 `ApplicationShell` 的 search dock） |
| `CMakeLists.txt` | target `vedit_workspace_search`；独立配置 = 核心 lane（无 shell） |

## 数据流

```
SearchPanel::startSearch()
  → GlobalSearch::start(options)            立即返回（QThread::create）
      worker: vd_search_tree_query(root, query, options) → 轮询 vd_search_tree_poll
              Rust 侧完成 递归遍历 + 跳过/计数 + 逐行匹配 + 限额/时间预算
      worker: 结果按 200 条/批 invokeMethod 回主线程；取消 = 停止轮询（服务 job 自行结束）
  ← 主线程: hitsReady(batch)                逐批追加结果树
  ← 主线程: finished(summary)               文案 + 空状态 + searchFinished
```

替换（ux-batch-2 B2）：

```
SearchPanel::replaceAllRequested()
  → GlobalSearch::start(allMatches=true)    第一次统计扫描（只用来找出本次会触及的文件；
                                              truncated / timedOut / 扫描窗口内有文件被写入 → 拒绝）
  → 未保存缓冲区：只保存“命中文件 ∩ 已修改打开文档”，失败 → 拒绝（已保存的保持已保存）
  → GlobalSearch::start(allMatches=true)    保存后重扫一次，确认框用的是真实计数
  → 确认框（默认 QMessageBox，列出文件数、命中数与被保存的文档）
  → ProjectReplacer::start(hits, replacement, snapshot)  worker: 逐文件比对扫描窗口标记 +
                                                       扫描时戳 + 每命中行的内容指纹 + 用冻结查询
                                                       重验区间 → 每文件一次 atomicSaveText
  ← 主线程: replaceFinished(outcome)        计数写进状态行；只重载“内容未变”的打开文档；
                                             用普通查询刷新结果
```

`vd-search` 拥有遍历与匹配（`crates/vd-search/src/lib.rs::search_tree`），`vd-ffi`
只做 C 投影；Qt lane 没有第二套实现。构建未链接 Rust 层时 `GlobalSearch` 返回
类型化 Failed（`the Rust search service is not linked in this build`），不会退化成
Qt 自带搜索。

- **不冻结 UI**：job 启动与轮询都在 `QThread` 上；`start()` 在 96 MiB 文件样本上实测
  < 100 ms 返回（`CHECK large.start.non-blocking`），工作线程身份由 `ranOnWorkerThread()` 断言。
- **生命周期（verdict M2 + T21）**：取消令牌按值传给 worker；析构时 `cancel()` 后等待
  worker 退出，再确定性地回收 `QThread`。Rust job 由服务内部的 `Arc` 持有，服务句柄
  可以在 job 未结束时销毁（`vd-ffi` service.rs 的生命周期契约），因此 worker 不会读到
  已释放的对象；慢盘读只会拖慢服务 job，不会造成 Qt 侧 use-after-free
  （回归证据 `evidence/search-uaf/`，当前实现仍由 `slowlifecycle` 自检覆盖）。
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
| 同一行多个命中 | 浏览结果该行只出 1 条（首次出现的列号，1-based）；「全部替换」的统计扫描用 `allMatches=true`，每个出现各一条，因此计数是**真实替换数** |
| 文件不是合法 UTF-8 | 替换**不改写**该文件，只在结果里报告（Rust 侧是 lossy 解码，按它重写会损坏字节） |
| 统计扫描达到 `maxResults` | 「全部替换」**拒绝**并提示先用更窄的查询（绝不只替换一部分） |

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
vedit::workspace::jump::jumpTo(filePath, line, column);   // 全产品唯一跳转接缝
```

- 跳转接口属于 `src/workspace/jump`（AC-7.7），search lane **不再自带** `IJumpTarget`（A-1/T20）。
  **产品路径**：`installSearch()` 把 `EditorJumpTarget`（`search_jump_target.h`）注册进
  `jump::Registry`；它经**唯一** open seam `EditorPane::openFile()`（T21/T39）在活动窗格打开
  命中文件，再把 KTextEditor 光标放到 1-based `line`/`column`（越界按文档夹取）。
  模块树/诊断/文件树/面包屑也走同一个 target，没有第二条打开路径。
- 自检同时覆盖两层：`CHECK jump.click.*`（QTest 真鼠标点击）→ `CHECK jump.demo.opened/line`
  （面板 + 夹具目标），以及 `CHECK jump.product.*`（真实 `ApplicationShell` 里点击命中，
  断言文档文本 == 磁盘字节、光标行号/列号 == 命中行/列、`vedit.openFilePath` == 命中路径、
  状态栏含路径）。截图 `03-search-jump.png` 为 offscreen 抓图，不作为“看起来正确”的证据。
- 旧的非目标“不做跨文件替换”已被 ux-batch-2 B2 推翻，见下一节。

## 全工程替换（ux-batch-2 B2）

两个按钮，不是预览对话框（用户决策 6）：

| 按钮 | 行为 |
|---|---|
| 「替换」`replaceCurrentHit()` | 替换**结果树中当前选中的那一条**命中（键盘导航也算，不要求点击；写入该文件一次），随后用同一查询刷新结果并**前进到下一条**（刷新后位于同一索引的命中） |
| 「全部替换」`replaceAllRequested()` | ①把查询+选项+范围+文件过滤**冻结成操作快照**，用 `allMatches=true` 跑第一次统计扫描；②命中文件里被修改的打开文档、以及“磁盘副本无命中但缓冲区可能含命中”的修改文档先保存，然后用**同一份冻结请求**重扫（绝不重新解析 CurrentFile 而悄悄放宽范围）；③弹确认框列出**文件数/命中数**与被保存的文档；④按文件分组、每文件一次 `atomicSaveText`；⑤报告计数并刷新结果 |

关键不变量：

- **写侧只认冻结快照（b2-repair-r2）**：统计扫描结束时把“查询文本 + 大小写/整词/正则 +
  `scanId`（匹配器身份）+ 每个命中文件的大小/修改时间”冻结成 `ReplaceSnapshot`，命中本身也带
  `scanId`。worker 的 `RangeMatcher` 只由这份快照构造，**从不读取**查询框或选项按钮；查询/选项
  在扫描、保存、确认或写入期间被改动 → 整个操作拒绝并提示重新搜索，空查询同样拒绝
  （不存在“不校验就照写”的路径）。扫描与写入期间查询框和全部选项控件被禁用。
- **身份覆盖“扫描窗口”，而不只是扫描结束那一刻（b2-repair-r3）**：扫描的**开始时刻**也是冻结身份
  的一部分。每个命中文件在冻结时记录“是否在扫描开始之后被写过”（`ReplaceFileStamp::
  writtenDuringScan`）；在扫描进行中被保存过的文件，其命中描述的是保存前内容、戳却来自保存后的
  文件，两者会同时“通过”，因此：
  - 「全部替换」在两次统计扫描的 `onSearchFinished` 里先检查该标记，命中即**整个操作拒绝**
    （发生在保存未保存缓冲区和确认框**之前**，一个字节都不写）；
  - 单条「替换」对被标记的文件拒绝该行（同一工程里其他文件的结果仍可替换）；
  - worker 写入前再检查一次（第二道防线）。
- **每个命中带内容指纹（b2-repair-r3）**：扫描器返回的 `preview`（Rust 侧 trim + 截断到 400 字符）
  就是匹配器实际读到的那一行，`GlobalSearch` 对它计算 `matchedLineFingerprint()`
  （SHA-256，去掉 UTF-8 BOM、同样 trim/截断）并随命中带到写侧。`applyReplaceRanges()` 在应用区间
  前用当前文件的同一行重算该指纹，不等就**拒绝该文件**并报出文件名与行号。这直接覆盖
  “同一 token 换到另一个位置”的情形：行号/列号相同但该行已不是扫描时读到的文本，绝不会被当成原处替换。
  `ReplaceHit` 里指纹为空表示“没有逐命中证据”（测试/手工构造区间），此时仍执行冻结查询的区间校验。
- **结果会失效**：查询/选项一改就清空结果行；单条「替换」前再次比较实时控件与冻结快照，
  并检查该行的 `scanId`。文档在扫描后变化（哪怕同一行/列仍是同样文本）由每文件
  size/mtime 戳 + 每命中行内容指纹共同拒绝，避免 D5/A3/C4 的“换了一处仍被当成原处替换”。
- **单条「替换」的未保存缓冲区：先校验、再保存、再刷新基线（b2-repair-r3）**：被修改的打开文档
  在保存前先做两件事——①磁盘上的大小/修改时间必须仍等于冻结戳（否则说明扫描之后被外部改过，
  保存会覆盖它）；②**缓冲区里命中所在那一行**的内容指纹必须仍等于扫描时的指纹（否则用户自己的
  编辑已经把目标移走，此时**不保存**、不替换，并说明“文档未保存、未替换”）。两项都通过才保存，
  保存后用新的 size/mtime 重写该文件的冻结戳，因此“先保存再因为自己的保存而拒绝”的旧缺陷不复存在；
  写侧的行指纹仍然兜底。
- **Ctrl+F 交接不打断进行中的替换（b2-repair-r3）**：`applyHandoff()` / `startSearch()` /
  `beginScan()` 在 `operationBusy()`（统计扫描、保存、确认、写入任一阶段）为真时**拒绝**并给出状态行，
  hub 订阅在同样状态下忽略外部快照；因此交接既不会取消进行中的「全部替换」，也不会把
  `operationActive_` 留成无主状态——操作自己到达终态后控件恢复可用。
- **不产生第二套搜索结果**：替换只应用 Rust 匹配器返回的 `column`/`matchLength`（字符列，
  非字节、非 UTF-16 单元）。`applyReplaceRanges()` 在写之前用**同一个冻结查询**（字面量，
  或 `\b(?:…)\b` 整词包装的同一正则）在该区间重新验证：文本不再匹配（文件在扫描后变了）
  就**拒绝该文件/整批**并报出文件名与行号，绝不把旧区间套到新字符上。验证只用于拒绝，
  不产生任何结果。
- **每文件一次写**：`ProjectReplacer` 在 worker 线程按文件分组，一个文件只调用一次写函数
  （测试用计数写入器断言 6 个文件各 1 次、总数 6）。
- **未保存缓冲区规则**：文件在编辑器里打开且被修改时，替换前**先保存**（内存内容为准）；
  “磁盘副本没有命中、但缓冲区可能有命中”的修改文档也会保存并重扫（字面量用 ASCII 前缀过滤，
  正则/非 ASCII 保守地保存后重扫），因此只在缓冲区里的命中不会被静默跳过。保存失败则
  **拒绝整个替换**，且**已经保存的文档保持已保存**（保存是逐个进行的，不会回滚），
  替换本身一个字节都不写；没有编辑器桥时同样拒绝，不静默跳过该规则。
- **取消/拒绝文案如实反映已保存状态**：保存过受影响文档后取消或超时，状态行写
  “N 个文档已保存、未替换任何内容”，不再说“什么都没写”。
- **重载不覆盖新输入**：保存时记下缓冲区内容的 token；替换完成后，只有**仍然持有该内容**
  的打开文档才会从磁盘重载。替换期间用户又敲了字的缓冲区保持原样、不被覆盖，
  并在状态行明确列出 `N file(s) changed during replace — not reloaded: …`
  （`ReplaceOutcome::notReloadedFiles` 同样可观察）。
- **拒绝而不是部分替换**：统计扫描被截断（`maxResults`）、**在时间预算内没有扫完
  （`timedOut`）**、取消、或某个文件的命中范围与当前内容不符时，`ReplaceOutcome.ok=false`
  并带原因；非 UTF-8 文件只进 `skippedFiles`，绝不按 lossy 文本重写。
- **UTF-8 BOM**：匹配器按字符计列且保留行首 U+FEFF，替换路径用
  `Stateless | ConvertInitialBom` 解码（同样保留 BOM，写回时字节级保留），
  第 1 行的列不会偏移一个字符。
- **异步**：目录遍历与逐行匹配始终在 `vd-search` 的工作线程；替换的读写也在
  `ProjectReplacer` 的 worker 线程。GUI 线程只做计数、确认框和刷新。

## Ctrl+F 交接（当前文件 / 全工程）

`src/editor/kernel/find_replace.{h,cpp}` 的 `FindReplaceBar` 现在有与搜索面板**同一套**
选项（`vedit::search::QueryOptions`，`FindOptions` 就是它的别名）和同一组范围值：

- `CurrentFile`：浮条自己在本文件内搜索/替换（原有行为）。
- 面板的 `CurrentFile` 范围搜索的是**当前活动编辑器文档**：`installSearch()` 给面板装一个
  provider，在每次搜索/替换时经 `EditorArea::activePane()->currentFilePath()` 解析，
  因此切换标签页或分屏后范围立即跟随；它不再依赖浮条最近一次打开或结果点击留下的路径
  （没有 provider 的独立 lane 构建才回退到 `SearchHub::currentFilePath()`）。
- `WholeProject`：浮条**不再假装在单文件里搜全工程**——它把查询词与选项通过
  `SearchHub` 发布并调用搜索面板的工程搜索处理器；面板用**同样的查询与选项**跑工程搜索
  并把结果展示在自己里面。任一界面的查询词/选项改变都会同步到另一界面（双向，
  带重入保护）。
- 若面板尚未创建：`installSearch()` 注册的兜底处理器会先经 shell 的 PanelRegistry
  把 search dock 插进来再交接；没有任何处理器时浮条显式提示
  `Whole project: open the Search panel to search the project`，不静默吞掉。

## 外壳接入（B 的通用接缝）

`src/app/CMakeLists.txt` 以 `workspace/<lane>` 方式纳入本目录；lane 入口：

```cpp
// src/workspace/integration.cpp（B 维护）
installSearch(shell, options);   // 本 lane 提供：注册工厂 + 安装跳转目标
```

`installSearch` 内部（A-1/T20：跳转统一走 `vedit::workspace::jump` 注册表，
search lane 不再有自己的 `IJumpTarget`/每面板 target）：

```cpp
auto *jumpTarget = shell.findChild<search::EditorJumpTarget *>();  // 每个 shell 一个
if (jumpTarget == nullptr) jumpTarget = new search::EditorJumpTarget(&shell);
vedit::workspace::jump::setTarget(jumpTarget);   // 全产品的唯一 target
shell.panels()->setFactory("search", [root] {
    return search::createSearchPanel(root);       // 面板点击 → jump::jumpTo()
});
```

`EditorJumpTarget`（`search_jump_target.{h,cpp}`）是**整个产品**的跳转目标：搜索结果、
模块树、诊断行、文件树、命令面板、面包屑都通过同一个 `jump::jumpTo()` 到达它，
它再用唯一的开文件接缝 `EditorPane::openFile()` 在当前活动窗格里打开并定位。
因此 `vedit-app` 的 Search 面板（View 菜单 / `Ctrl+Alt+S`）就是真实实现，
没有第二套注册/开文件路径，也不改外壳布局（面板仍然“按需插入”）。

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
4. **两个界面的正则引擎不同（必须如实告知用户）**：工程级搜索/替换走 Rust `regex` crate
   （RE2 风格：**不支持反向引用与 lookaround**），Ctrl+F 的单文件搜索走 `QRegularExpression`
   （PCRE 风格）。四个选项的**语义**一致（大小写/整词/正则/范围），但**方言**不同：
   在 Ctrl+F 里能用的反向引用/lookaround 放到搜索面板会报“无效正则”。
   替换前的区间重验也用 `QRegularExpression`，但它**只验证匹配器已报告的那一段**
   （同一查询、同样的 `\b(?:…)\b` 整词包装；RE2 模式是 PCRE 的子集），不产生搜索结果。
   这条差异由 captain 合并进 `docs/USER_GUIDE.md`（本 lane 不直接改用户手册）。
5. **截图里 CJK 文件名显示为方框**：构建镜像没有安装 CJK 字体（`fc-list` 无 CJK），
   数据正确性由 `CHECK panel.hit.unicode-*`（文件、列号）断言；交付 AppImage 会带字体，
   属于打包 lane 的范围。
6. **权限用例在 root 下 SKIP**：镜像内默认以 root 运行，`chmod 000` 不生效；
   `05-selfcheck-nonroot.log` 用 `setpriv --reuid=1000` 跑同一套用例（0 SKIP，83 checks）。
7. **搜索期间的面板刷新是逐批的**：不额外做进度节流 UI（小工程 0 ms 完成，大工程靠批次到达）。
8. **文档身份的粒度与残余（b2-repair-r3 起登记，b2-repair-r4 补上整行校验）**：
   - **扫描窗口**：以扫描开始时刻（毫秒）为界，命中文件的 mtime **严格晚于**它（且不晚于当前时钟）
     才判定“扫描中被写”。与扫描开始落在同一毫秒的写入不会被这一层标记，但该命中行的内容指纹仍会拒绝
     （行内容变了）；若行内容、行号都没变、只有文件其它位置变了，替换的仍是同一坐标上的同一文本。
     mtime 在未来的文件不会被永久误判为“扫描中写入”，其后的变化由每文件 size/mtime 戳把关。
   - **扫描之后**：每文件“大小 + 毫秒级 mtime”+ 每命中行内容指纹。外部写入若**保持同样大小、
     不推进 mtime**，且命中行文本完全相同，则与扫描时状态不可区分——这是本 lane 能给出的最窄残余。
   - **逐命中指纹只覆盖预览**：指纹算的是匹配器返回的 preview，即该行**去掉首尾空白后的前 400 个字符**
     （`TREE_SNIPPET_CHARS` 上限，`matchedLineFingerprint`）。因此指纹本身分不清“命中行”与“前 400 个
     字符相同、之后不同”的另一行。**b2-repair-r4**：单条「替换」对**未保存缓冲区**不再只比指纹——
     保存前先读取扫描时文件在命中行号上的**整行**（`readDocumentLine`，解码规则与写入端完全一致：
     UTF-8 + `ConvertInitialBom`），要求缓冲区该行与它**整行相等**（`normalizedLineIdentity`，只归一化
     BOM/首尾空白）；不等就**拒绝**并说明原因，**不保存、不写**。于是：
     - 在命中行上方插入一行“前 400 个字符相同、之后不同”的文本行 → **拒绝**（不会写进插入的那行）；
     - 缓冲区把命中行 400 字符之后的尾巴改掉 → **拒绝**（扫描读到的那行已不存在）；
     - 插入一行与命中行**整行完全相同**的副本 → 仍接受，但两行文本相同，替换到任一行的结果一样，
       不会写错字符。
     写侧（`ProjectReplacer::applyReplaceRanges`）仍以 400 字符预览指纹作为最后一道防线；对未保存缓冲区
     它已被上面的整行校验兜住，剩余暴露只剩上一条“扫描之后”（外部写入保持大小与 mtime 不变）。
   - **替代桥**：单条「替换」的保存前校验依赖 `OpenDocumentBridge::bufferText()`；产品桥
     （KTextEditor）总是提供缓冲区文本。替代桥若返回空串：**短行**退化为“先保存、由写侧行指纹拒绝”；
     **超过 400 字符（预览被截断）的行**则直接**拒绝**（不保存、不写），因为整行身份无法证明。
   - **浮条文案**：Ctrl+F 交接被拒绝时，浮条自己的 transient 文案仍是
     “Whole project: searching in the Search panel”（该文案在
     `src/editor/kernel/find_replace.cpp`，不在本 lane 写作用域）；搜索面板自身显示真实的拒绝原因。
   - 正则/非 ASCII 的缓冲区预过滤是保守策略：宁可多保存一些修改文档，也不漏掉只在缓冲区里的命中。
