# ws-palette（T22）结论：Ctrl+P 命令面板 + 10k 文件模糊跳转（A2b）

**结论：交付成立，A2b 达标。** 面板在真实 `vedit-app` 里由 `Ctrl+P` 打开，输入按
大小写不敏感的模糊子序列过滤，回车打开；**10,000 个真实磁盘文件**上，过滤
p95 **1.305 ms**、面板打字路径 p95 **1.404 ms**，相对 200 ms 预算余量 **≈142×**。
全量 `bash scripts/build-qt.sh` **exit 0**，ctest **7/7**，palette 两条自检
**44/44 + 23/23**，`vedit-app` 仍然不链接 KTextEditor / tree-sitter。

- Board task：`T22` · 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-palette.md`
- 验收：**A2b**（`requirements.md` rev 2 第 110 行）
- 验证 loop：`loop-20261031T023040-6a6ed0`（verifier：Reviewer）
- 交付源码：`src/workspace/palette/`（core + 面板 + 适配层 + 自检 + 基准）
- 证据目录：`evidence/ws-palette/`
- 环境：pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0
  / tree-sitter 0.25.9 / cmake 4.2.3 / g++ 15.2.0），见 `00-pins.txt`

## 1. 构建与测试（观察到的命令、输出、退出码）

| 命令 | 观察结果 | 证据 |
|---|---|---|
| `bash scripts/build-qt.sh` | **exit 0**，结尾 `[qt6] PASS: configure + build + ctest + xcb/software-rendering smoke inside vedit-qt6-build:local`；configure 里三条 lane 全部 wired（见下） | `30-build-qt.log` |
| 同上内置 ctest | **7/7 passed**（含 `vedit-workspace-palette-selftest`、`vedit-workspace-palette-integration-selftest`），总耗时 0.68 s | `30-build-qt.log`、`evidence/m0-build/build-logs/33-qt6-ctest.log` |
| `ctest -V -R vedit-workspace-palette` | **exit 0**；`PALETTE-SELFTEST ok=1 checks=44 failures=0`；`INTEGRATION-SELFTEST ok=1 checks=23 failures=0`；4+3 张截图 OK | `32-ctest-palette.log` |
| `bash src/workspace/palette/tools/bench.sh 10000 200` | **exit 0**；`BENCH VERDICT PASS p95_ms=1.404 budget_ms=200.000 files=10000 source=disk` | `31-bench.log`、`bench.json` |
| `readelf -d target/qt6-build/src/app/vedit-app` | NEEDED 里 **KTextEditor=0、tree-sitter=0**（保持 T13 的保证；只多了 Qt Widgets/Gui/Core 与既有 KSyntaxHighlighting） | `33-elf.txt` |

configure 观察（`30-build-qt.log`）：

```
-- VEdit: workspace lane 'filetree' included
-- VEdit: workspace lane 'palette' included
-- VEdit: workspace lane 'search' included
-- VEdit: workspace lane 'filetree' wired (vedit_file_tree)
-- VEdit: workspace lane 'palette' wired (vedit_workspace_palette)
-- VEdit: workspace lane 'search' wired (vedit_workspace_search)
-- VEdit: workspace integration enabled (src/workspace/integration.cpp)
```

## 2. A2b 实测（10,000 个真实文件，200 样本/查询）

候选集来自**真实磁盘** `target/ws-palette-bench-fixture`（10,000 个文件，由基准
自己创建一次并复用），经 `WorkspaceFileIndex` 扫描后过滤。测量在 pinned 镜像内、
`QT_QPA_PLATFORM=offscreen`、`RelWithDebInfo`。

| 指标 | median | p95 | max |
|---|---|---|---|
| 核心过滤 `FuzzyMatcher::filter`（10k 候选、10 条查询 ×200 次） | **0.739 ms** | **1.305 ms** | 2.291 ms |
| 面板打字路径（过滤 + 100 行结果列表重建） | **0.838 ms** | **1.404 ms** | 1.705 ms |
| 冷扫描 10k 文件（仅一次） | — | — | **22 ms** |
| 第二次 `scan()`（缓存，不扫盘） | — | — | **0 ms**（`scan_count=1`） |

- 预算 **200 ms**；取两条路径较大的 p95 = **1.404 ms**，余量 **≈142×**。
- 查询集合：`m, ma, mai, main, cmd, pal, src, widget, file_index, zzzz`（含无命中）。
- 复跑：`bash src/workspace/palette/tools/bench.sh`（宿主一条命令，pinned 镜像内
  配置/构建/建 fixture/跑基准/写 JSON）。机器可读结果：`bench.json`。

## 3. 契约 (a)–(d) 逐条

### (a) Ctrl+P 打开、输入模糊过滤、回车打开

- 命令 `file.goto-file` 走 shell 的 `CommandRegistry`（默认 `Ctrl+P`，可被
  `IShortcutStore` 重绑），并插入 File 菜单；`shell.addAction()` 让快捷键在窗口内
  生效。实现：`src/workspace/palette/palette_integration.cpp`。
- 集成自检（真实 `ApplicationShell`，`32-ctest-palette.log`）：
  `integration.command-registered`、`integration.shortcut-ctrl-p`、
  `integration.in-file-menu`、`integration.ctrl-p-opens`、`integration.filters`、
  `integration.enter-opens`、`integration.rebinding` / `integration.rebinding-restored`
  全 PASS（23/23）。
- 截图：`screenshots/21-integration-palette.png`（真实 shell 内的面板）、
  `screenshots/22-integration-shell.png`（状态栏显示已打开文件）。
- 键盘：Enter 打开、Esc 关闭、↑/↓/PageUp/PageDown 选择（自检
  `palette.enter-opens`、`palette.escape-closes`、`palette.arrow-selects`、
  `palette.arrow-enter-opens`、`palette.typing-filters`）。

### (b) 10,000 文件的模糊过滤在 200 ms 内

见 §2：p95 **1.305 ms**（核心）/ **1.404 ms**（面板路径），脚本可复跑。

### (c) 无命中时明确空状态

- 空状态文案固定为 `No matching files`（`CommandPalette::noResultsText()`）；
  无工程时显示 `No project open — …`；工程无文件时显示 `No files indexed …`。
- 自检：`palette.empty-state`、`palette.10k-empty-state`、`palette.no-project-empty-state`；
  集成自检：`integration.empty-state`；截图 `screenshots/11-palette-empty-state.png`、
  `screenshots/20-integration-empty.png`、`screenshots/12-palette-no-project.png`。

### (d) 与文件树共享的文件索引接口、不重复扫描磁盘

- `src/workspace/palette/file_index.h` 的 `WorkspaceFileIndex`：
  - `scan()` 只走一次磁盘并缓存（第二次返回缓存，`scanCount()` 不变）；
  - `setFiles(paths)` 接收文件树**自己扫描后发布的快照**，palette 不再扫盘；
  - `addFile/removeFile` 是文件树 create/rename/delete 的增量更新路径；
  - `scanCount()` / `revision()` 是可断言的记账。
- 三处断言：自检 `index.no-rescan`、`index.publish-no-scan`、
  `index.incremental-no-scan`、`palette.10k-never-scans-disk`；集成自检
  `integration.single-scan`（两次打开面板 + 两次回车后 `scanCount() == 1`）；
  基准 `BENCH SCAN ... scan_count=1`。
- 共享方向：`PaletteIntegration::fileIndex()` 暴露索引对象供 ws-filetree 发布/更新
  （接口约定见 `SEAM.md`）。

## 4. 截图（`screenshots/`）

| 文件 | 内容 |
|---|---|
| `10-palette-filter.png` | 10k 合成索引 + 查询 `pal`，命中字符高亮、状态栏 `100 of 10000 files` |
| `11-palette-empty-state.png` | 无命中：`No matching files` + `0 of 10000 files` |
| `12-palette-no-project.png` | 无工程空状态 |
| `13-palette-project.png` | 小工程索引 + 查询 `main` |
| `20-integration-empty.png` | 真实 shell 内无命中空状态 |
| `21-integration-palette.png` | 真实 shell 内查询 `ma` → `src/main.cpp` |
| `22-integration-shell.png` | 真实 shell 窗口（状态栏 `Opened …/src/util/helper.cpp`，demo 编辑器已载入文件） |

## 5. 接缝（captain 授权的通用改动，不在冻结契约写域内）

- `src/app/main.cpp` 只加 2 行（include + `installPanels(...)` 一行调用）；
  `src/app/CMakeLists.txt` 末尾加 workspace lane 段（`if(EXISTS)` 守卫）；
  新增 `src/workspace/integration.{h,cpp}` 统一入口。
- 完整 diff/说明：`36-app-seam-diff.md`；给 A/C 的接入契约：`SEAM.md`。
- 保护 T13 约束：`33-elf.txt` 证明 `vedit-app` 的 NEEDED 里 KTextEditor /
  tree-sitter 计数为 0。

## 6. 残余风险与如实说明

1. **打开文件的落点**：edit-core 尚未落地，回车后适配层默认把文件载入 shell 的
   demo 编辑器（`QPlainTextEdit`；>4 MB 或含 NUL 字节拒绝并只给状态栏提示）。
   edit-core 上线后用 `PaletteIntegration::setFileOpenHandler()` 注入真实打开逻辑，
   app 层不需要反向依赖编辑器。**“回车打开”当前证明到“路径交付 + demo 编辑器
   内容更新”，不是真实编辑器。**
2. **与文件树的共享目前是接口级**：索引 API、发布/增量路径和 `scanCount` 断言
   都在；ws-filetree 的 `installFileTree` 刚落地（根构建已 wired），但**“文件树
   把快照发布进 palette 索引”这条联合路径尚无联合用例**。在它调用
   `fileIndex()->setFiles(...)` 之前，palette 首次打开会自己扫一次盘（`scanCount=1`），
   之后复用。
3. **写域**：`src/app/main.cpp`、`src/app/CMakeLists.txt`、`src/workspace/integration.*`
   在冻结契约的 allowed write scopes 之外，是 captain 2026-10-31 明确授权的通用接缝
   改动；palette 其余代码全部在 `src/workspace/palette/`。请 verifier 以 captain
   授权/`SEAM.md` 为准，不要按冻结契约第 1 条直接判越界。
4. **`.v` 扩展名陷阱**：本面板不做语言/扩展名过滤，不受 “`.v` 解析成 V(vlang)”
   影响（`evidence/m0-build/REPORT.md` §4.9）。
5. **测量环境**：数字来自本机 rootless docker + pinned 镜像、offscreen、宿主负载
   正常；单次 200 样本。p95 余量 ≈142×，远超预算，但极端负载下 max 仍会抖动
   （观察到 max 最高 3.1 ms）。基准本身带 `--assert-p95-ms 200`，超过即 exit 3。
6. **offscreen 平台噪声**：selftest 输出里的 `This plugin does not support raise()`
   / `grabbing the keyboard` 是 offscreen 平台对 `Qt::Popup` 的提示，不是失败；
   真实窗口路径由 `build-qt.sh` 的 xcb/llvmpipe 冒烟覆盖（但该冒烟不点击面板）。
7. **门禁红点修复**：`palette_selftest.cpp` 顶部曾用行尾反斜杠续写注释，被
   `-Werror=comment` 判红；已改成单行注释（真实修复，未放宽 `-Werror`、未加
   NOLINT）。

## 7. 证据索引

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像/版本/sha256（源码与关键文件） |
| `30-build-qt.log` | `bash scripts/build-qt.sh` 全量日志（exit 0） |
| `31-bench.log` / `bench.json` | 10k 基准原始输出与机器可读结果 |
| `32-ctest-palette.log` | 两条 palette ctest 的 verbose 输出（44/44、23/23） |
| `33-elf.txt` | `vedit-app` NEEDED 与禁用依赖计数=0 |
| `36-app-seam-diff.md` | app 侧接缝的 unified diff + 新文件 |
| `SEAM.md` | A/C 两条 lane 的接入契约 |
| `screenshots/` | 7 张截图（见 §4） |
