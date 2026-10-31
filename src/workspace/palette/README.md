# ws-palette — Ctrl+P 命令面板 + 模糊文件跳转（T22 / A2b）

Lapce 对齐的文件跳转面板：`Ctrl+P` 打开浮层，输入按**大小写不敏感的模糊子序列**
过滤，回车打开；10,000 个文件的过滤实测 p95 **1.4 ms**（预算 200 ms）。
面板不自己扫盘：它过滤共享的 `WorkspaceFileIndex`，索引由文件树或面板懒加载
**各只建一次**。

## 文件

| 文件 | 作用 |
|---|---|
| `fuzzy_matcher.{h,cpp}` | 无分配的模糊子序列匹配 + 打分（basename 权重、边界/连续加分、gap 惩罚），结果确定性排序 |
| `file_index.{h,cpp}` | **共享**工作区文件索引：一次 `scan()` 缓存、`setFiles()` 接收文件树快照、`addFile/removeFile` 增量更新；`scanCount()` 是可断言的“扫盘次数” |
| `command_palette.{h,cpp}` | `QDialog(Qt::Popup)` 浮层：查询框 + 结果列表 + 空状态 + 高亮委托；Enter/Esc/↑/↓/PageUp/PageDown |
| `palette_integration.{h,cpp}` | app 适配层（唯一 include `src/app/` 的文件）：注册 `file.goto-file`（默认 `Ctrl+P`）到 `CommandRegistry`、插进 File 菜单、`shell.addAction()` 使快捷键生效、回车回调 |
| `palette_selftest.cpp` | 44 项离屏自检（fuzzy/索引/面板/10k 打字）+ 截图 |
| `palette_integration_selftest.cpp` | 23 项真实 `ApplicationShell` 集成自检（Ctrl+P 命令、菜单、键位重绑、回车打开、只扫一次盘） |
| `palette_bench.cpp` | 10k 基准（磁盘 fixture + 内存候选两用），输出 JSON |
| `synthetic_paths.h` | 自检与基准共用的确定性 1 万条路径 |
| `tools/bench.sh` | 宿主一条命令：pinned 镜像内配置/构建/跑基准，写 `evidence/ws-palette/` |

## 共享文件索引（契约 d：与文件树共享、不重复扫盘）

```cpp
WorkspaceFileIndex index(projectRoot);
index.setFiles(treeSnapshot);     // 文件树扫描后“发布”快照（palette 不再扫盘）
index.addFile(path);              // 文件树 create/rename 增量更新
index.removeFile(path);           // 文件树 delete 增量更新
index.scan(&error);               // 或者由本索引做唯一一次扫描，两条 lane 共享对象
```

`PaletteIntegration::fileIndex()` 暴露这个对象，文件树 lane 可以直接发布/更新。
面板侧保证：**打开面板、打字过滤只读缓存**，`scanCount()` 不会增加（自检
`palette.10k-never-scans-disk`、集成自检 `integration.single-scan`、基准
`BENCH SCAN ... scan_count=1` 三处断言）。

## 接入真实 app（接缝）

`src/app/main.cpp` 已有一行统一入口（captain 授权的通用接缝）：

```cpp
ApplicationShell shell(options);
vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
```

完整约定（目标名、`install<Lane>()` 签名、CMake 自动接线、A/C 两条 lane 的
具体动作）见 `evidence/ws-palette/SEAM.md`。

命令走 shell 的 `CommandRegistry`，因此 `IShortcutStore` 重绑键位后
`refreshShortcuts()` 立即生效（集成自检 `integration.rebinding`）。

## 性能（A2b，pinned 镜像内实测）

真实磁盘 10,000 个文件（`target/ws-palette-bench-fixture`，200 样本/查询）：

| 指标 | median | p95 | max |
|---|---|---|---|
| 核心过滤（`FuzzyMatcher::filter`，10k 候选） | 0.772 ms | **1.338 ms** | 3.070 ms |
| 面板打字路径（过滤 + 100 行列表重建） | 0.829 ms | **1.403 ms** | 1.827 ms |
| 索引冷扫描（一次，10k 文件） | — | — | 18 ms |
| 缓存扫描（二次调用） | — | — | 0 ms（`scan_count=1`） |

预算 200 ms，p95 余量约 **142×**。复跑：`bash src/workspace/palette/tools/bench.sh`。

## 构建 / 测试

```sh
# 根构建（pinned 镜像；palette 作为 workspace lane 自动纳入，含两个 ctest）
bash scripts/build-qt.sh

# 只跑本 lane 的两个 ctest（需先构建）
bash build/gates/qt6-cxx.sh --no-test
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build -V -R vedit-workspace-palette

# 独立配置（不带 app 适配层，只核心库 + 自检 + 基准）
cmake -S src/workspace/palette -B target/palette-standalone -G Ninja
cmake --build target/palette-standalone
QT_QPA_PLATFORM=offscreen target/palette-standalone/vedit-workspace-palette-selftest
```

## 已知边界（如实记录）

1. **打开文件的落点**：edit-core 未落地，回车后回调默认把文件载入 shell 的 demo
   编辑器（`QPlainTextEdit`，≤4 MB、含 NUL 视为二进制并拒绝）。edit-core 上线后
   用 `PaletteIntegration::setFileOpenHandler()` 注入真实打开逻辑，app 层不需要
   反向依赖编辑器。
2. **与文件树的共享是接口级**：索引 API 与计数器已就绪；ws-filetree 尚未调用
   `fileIndex()` 发布快照（其 lane 仍在飞）。在它发布前，palette 首次打开会自己
   扫一次盘（`scanCount=1`），之后复用。
3. **`.v` 扩展名陷阱**（`evidence/m0-build/REPORT.md` §4.9）：本面板**不做**
   语言/扩展名过滤，因此不受 “`.v` 被识别成 V(vlang)” 影响。
4. **契约写域**：`src/app/main.cpp`、`src/app/CMakeLists.txt`、
   `src/workspace/integration.{h,cpp}` 在冻结契约的写域之外，是 captain 明确授权
   的通用接缝改动（见 `SEAM.md` 与提交说明）；palette 自身代码全部在
   `src/workspace/palette/`。
