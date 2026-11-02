collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
 M CMakeLists.txt
 M Cargo.lock
 M Cargo.toml
 M THIRD_PARTY_NOTICES.md
 M docs/decisions/r2-license.md
 M docs/handover/README.md
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/syn-grammars/00-pins.txt
 M evidence/syn-grammars/01-build.log
 M evidence/syn-grammars/02-tests.log
 M evidence/syn-grammars/03-manifest.txt
 M evidence/syn-grammars/04-manifest-none.txt
 M evidence/syn-grammars/04-manifest-static.txt
 M evidence/syn-grammars/04-manifest-systemverilog.txt
 M evidence/syn-grammars/04-manifest-verilog.txt
 M evidence/syn-grammars/04-swap-proof.txt
 M evidence/syn-grammars/05-layering.txt
 M evidence/syn-grammars/06-licensing.txt
 M evidence/syn-grammars/07-conclusion.md
 M evidence/syn-grammars/09-result.txt
 M evidence/term-panel/00-pins.txt
 M evidence/term-panel/01-configure.log
 M evidence/term-panel/02-build.log
 M evidence/term-panel/03-ctest.log
 M evidence/term-panel/03b-checks.log
 M evidence/term-panel/04-elf-needed.txt
 M evidence/term-panel/05-no-network.txt
 M evidence/term-panel/06-xcb-and-screenshots.log
 M evidence/term-panel/07-conclusion.md
 M evidence/term-panel/08-structure.txt
 M evidence/term-panel/09-root-build-with-lane.log
 M evidence/ws-filetree/00-pins.txt
 M evidence/ws-filetree/00-steps.txt
 M evidence/ws-filetree/01-configure.log
 M evidence/ws-filetree/02-build.log
 M evidence/ws-filetree/03-ctest.log
 M evidence/ws-filetree/03a-tests-full.log
 M evidence/ws-filetree/04-fixture.log
 M evidence/ws-filetree/05-screenshots.log
 M evidence/ws-filetree/06-permissions-nonroot.log
 M evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log
 M evidence/ws-filetree/08-real-shell-seamcheck.log
 M evidence/ws-filetree/09-real-app-install.log
 M evidence/ws-filetree/10-rootcheck-configure.log
 M evidence/ws-filetree/11-rootcheck-build.log
 M evidence/ws-filetree/12-gate-build-qt-final.log
 M evidence/ws-filetree/12a-gate-build-qt-first.log
 M evidence/ws-filetree/13-gate-qt6-cxx-final.log
 M evidence/ws-filetree/14-real-shell-check-run.log
 M evidence/ws-filetree/REPORT.md
 M evidence/ws-palette/00-pins.txt
 M evidence/ws-palette/30-build-qt.log
 M evidence/ws-palette/31-bench.log
 M evidence/ws-palette/32-ctest-palette.log
 M evidence/ws-palette/33-elf.txt
 M evidence/ws-palette/34-artifacts.txt
 M evidence/ws-palette/36-app-seam-diff.md
 M evidence/ws-palette/37-gate-qt6-cxx.log
 M evidence/ws-palette/REPORT.md
 M evidence/ws-palette/SEAM.md
 M evidence/ws-search/04-selfcheck-root.log
 M evidence/ws-search/05-selfcheck-nonroot.log
 M evidence/ws-search/06-screenshots.log
 M evidence/ws-search/07-screenshots-nonroot.log
 M evidence/ws-search/08-build-qt.log
 M evidence/ws-search/09-structure.txt
 M evidence/ws-search/10-ctest-root-search.log
 M evidence/ws-search/13-integration-proof.log
 M evidence/ws-search/14-standalone.log
 M evidence/ws-search/REPORT.md
 M scripts/build-qt-in-container.sh
 M scripts/build-qt.sh
 M src/app/CMakeLists.txt
 M src/app/main.cpp
?? cmake/VEditRustFfi.cmake
?? crates/vd-ffi/
?? crates/vd-search/
?? crates/vd-settings/
?? crates/vd-vcs/
?? docs/handover/appendix/amendments-rev2.md
?? evidence/docs-amendments/
?? evidence/ffi-abi/
?? evidence/fixture/
?? evidence/m0-build/build-logs/37-qt6-rust-abi.txt
?? evidence/verdicts/
?? scripts/verify-fixture.sh
?? tests/fixtures/
backfill: 补记 —— C9 头为事后补齐；git_head / git_status_porcelain 为补记时刻快照，非原始采集时刻；原始采集时刻不可回溯，文件 mtime 2026-10-31T04:18:29Z 仅供参考
---
# T21 文件树面板 — 交付与证据报告

契约：`contracts/ws-filetree.md`（split revision 2，冻结 2026-10-31T02:25:28Z）
需求：`requirements.md` rev 2 · C8（工程模型）/ A2a（文件树）
写域（AC1）：`src/workspace/filetree/` + `evidence/ws-filetree/` —— 本报告所列改动**全部**落在这两处。
构建/门禁环境：pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24.0 / cmake 4.2.3 / g++ 15.2.0 / glibc 2.43，见 `00-pins.txt`）。

**结论**：A2a 的 (a)(b)(c)(d) 全部实现并有可复跑实测；`bash scripts/build-qt.sh` exit **0**、`bash build/gates/qt6-cxx.sh` exit **0**；面板已按 `evidence/ws-palette/SEAM.md` 接入真实 `vedit-app`（真实二进制运行日志 + 真实外壳截图）。残余风险见 §8，其中最重要的是「双击打开」只到「面板发出路径 + shell 状态栏桥」，真正落到编辑器要等 edit-core 的 `EditorProvider` 接缝。

---

## 1. 契约与需求对照

| 契约条目 | 要求 | 落地 |
|---|---|---|
| 目标 (a) | 打开目录、展开/折叠、双击打开文件 | `FileTreeWidget::openDirectory()` / `QTreeView` 原生展开折叠 + 懒加载 / `openFileRequested(path)` 信号（`doubleClicked` 与 `activateIndex()` 两条路径） |
| 目标 (b) | 新建/重命名/删除后磁盘与树一致，失败可诊断 | 操作走 `fileops::*`（磁盘为先），成功后**重读该目录**；失败返回一行诊断 + 状态栏红字 + `operationFailed()`，磁盘与树都不动 |
| 目标 (c) | ≥10k 文件不阻塞 UI（懒加载） | 目录展开时才 readdir；行以 250 条/批经 `canFetchMore/fetchMore` 交付；不递归 |
| 目标 (d) | 空目录/权限拒绝的明确空状态 | `RootState` 六态 + 居中空状态页/提示条（含完整路径与原因） |
| 边界 | 只写 `src/workspace/filetree/`；不改 `src/app/` 布局；不实现命令面板/搜索 | 遵守；`installFileTree()` 只注册工厂 + 替换已插入 dock 的占位内容，不插入 dock、不改布局、不 `showPanel()` |
| 证据 | 测试 + 截图 + `evidence/ws-filetree/` | `03a-tests-full.log`（111 checks）、`06-permissions-nonroot.log`（17 checks）、`screenshots/*.png` |

需求 A2a 原文：「文件树可打开目录、展开/折叠、双击打开；新建/重命名/删除后磁盘与树一致。」

---

## 2. 交付物（全部在写域内）

```
src/workspace/filetree/
  CMakeLists.txt            双模式：独立 configure / 被根构建 add_subdirectory
  file_kind.h/.cpp          扩展名 → 类型（.v=Verilog 的陷阱隔离，见 §5）
  file_ops.h/.cpp           新建/重命名/删除的磁盘操作 + 诊断（不覆盖、不截断）
  file_tree_model.h/.cpp    懒加载 QAbstractItemModel（分批、目录重读、六态根状态、计数器）
  file_tree_widget.h/.cpp   面板：工具栏 / 树 / 空状态 / 状态栏 / 右键菜单 / 双击
  filetree_panel.h/.cpp     面板工厂 makeFileTreePanel(projectRoot)
  filetree_integration.h/.cpp  SEAM.md 约定的 installFileTree()（真实 shell 适配器）
  demo/filetree_demo.cpp    截图/场景驱动 + fixture 生成（--prepare / --screenshots）
  tests/filetree_tests.cpp  验收测试（111 checks + 非 root 权限套件 17 checks）
  tools/in-container.sh     在 pinned 镜像里：独立 configure→build→ctest→权限套件→截图
  tools/run-in-container.sh 上面的宿主入口
  tools/real-shell-check.sh 真实 shell 接缝检查（真实 vedit-app 运行 + 断言 + 截图）
  tools/run-real-shell-check.sh  上面的宿主入口
  tools/rootcheck/          根上下文小工程：add_subdirectory(src/app) + seamcheck
evidence/ws-filetree/       本目录（全部证据）
```

`vedit_file_tree` 是 SEAM.md 映射表里的目标名（`src/app/CMakeLists.txt:72`），未改名。

---

## 3. 命令与退出码（实测）

| # | 命令（在 pinned 镜像内） | 日志 | 退出码 |
|---|---|---|---|
| 01 | `cmake -S src/workspace/filetree -B target/filetree-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo` | `01-configure.log` | **0** |
| 02 | `cmake --build target/filetree-build --parallel 16`（`-Wall -Wextra -Wpedantic -Werror`） | `02-build.log` | **0** |
| 03 | `ctest --test-dir target/filetree-build --output-on-failure` | `03-ctest.log` | **0**（1/1 passed，1.13 s） |
| 03a | `target/filetree-build/vedit-filetree-tests`（完整 CHECK 列表） | `03a-tests-full.log` | **0**（111 checks / 0 failures） |
| 06 | `setpriv --reuid=1000 ... VEDIT_FILETREE_SUITE=permissions target/filetree-build/vedit-filetree-tests` | `06-permissions-nonroot.log` | **0**（17 checks / 0 failures） |
| 04 | `vedit-filetree-demo --prepare target/ws-filetree-tmp --big-count 10000` | `04-fixture.log` | **0** |
| 05 | `setpriv --reuid=1000 ... vedit-filetree-demo --screenshots evidence/ws-filetree/screenshots ...` | `05-screenshots.log` | **0**（8 张 PNG，全部 ok=1） |
| 09 | `readelf -d target/qt6-build/src/app/vedit-app \| grep -E 'KTextEditor\|tree-sitter' \| wc -l` + `vedit-app --project … --hold 1200` | `09-real-app-install.log` | **0**（count=0；安装日志一行，见 §6） |
| 10/11 | `cmake -S src/workspace/filetree/tools/rootcheck -B target/filetree-rootcheck` + `cmake --build` | `10-…log` / `11-…log` | **0** / **0** |
| 12 | `target/filetree-rootcheck/vedit-filetree-seamcheck --project … --screenshot …` | `08-real-shell-seamcheck.log` | **0**（`FILETREE-SEAMCHECK ok=1`） |
| 13 | `bash scripts/build-qt.sh`（宿主 → pinned 镜像内 configure+build+ctest+xcb 冒烟） | `12-gate-build-qt-final.log` | **0**（`BUILD-QT-EXIT=0`，7/7 tests） |
| 14 | `bash build/gates/qt6-cxx.sh`（契约声明的仓库门禁，内部就是上一条） | `13-gate-qt6-cxx-final.log` | **0**（`GATE-EXIT=0`，7/7 tests） |

门禁日志关键行（原文）：

```
-- VEdit: workspace lane 'filetree' included
-- VEdit: workspace lane 'filetree' wired (vedit_file_tree)
-- VEdit file tree: vedit_app_shell present -> installFileTree() is active
100% tests passed, 0 tests failed out of 7
[qt6] PASS: configure + build + ctest + xcb/software-rendering smoke inside vedit-qt6-build:local
```

> 说明 1：`02-build.log` 的 `-Werror` 就是 `build/gates/typecheck.sh` 走的 C++ 静态检查（docs/BUILD.md §12.5）。
> 说明 2：跑 `build/gates/qt6-cxx.sh` / `scripts/build-qt.sh` 会**重写** `evidence/m0-build/build-logs/*`（T10 的证据目录，不属于本票写域）。运行前的副本已备份在工作区内：`target/filetree-backup/m0-build-logs-pre-t21/`（gitignored，未用 `/tmp`）。这是本次唯一的写域外副作用，主动披露。

---

## 4. A2a 逐条证据

### (a) 打开目录 / 展开折叠 / 双击打开

- 打开：`CHECK tree.root-open ok=1`、`tree.root.rows ok=1`（根下 4 项，目录在前）、截图 `10-file-tree-open.png`。
- 展开/折叠：`CHECK tree.collapse ok=1` / `tree.expand ok=1`；`tree.lazy.nested-not-read ok=1`（展开根之前子目录**没有**被读取：`scannedDirectories()==1`）。
- 双击：`CHECK tree.dblclick.folder-expands ok=1`、`tree.dblclick.folder-does-not-open ok=1`、`tree.dblclick.file-opens ok=1`、`tree.dblclick.file-path ok=1`（用 `QTest::mouseDClick` 打在 viewport 的 `visualRect` 上，真实双击事件；先有一次 `mouseClick`，因为 `QAbstractItemView::mouseDoubleClickEvent` 依赖上一步 press 的 `pressedIndex`，这是 Qt 的真实语义）。
- 程序化路径：`CHECK tree.activate.file-opens ok=1`（demo 与 shell 适配器用同一入口）。

### (b) 新建 / 重命名 / 删除：磁盘与树一致

操作后**从磁盘重读该目录**，模型不是真相来源。逐条：

| 场景 | checks | 结果 |
|---|---|---|
| 新建文件 / 新建目录 | `ops.create-file*`, `ops.create-folder*` | ok，且 `diskAndTreeAgree()` ok |
| 重名拒绝 | `ops.create-dup-refused/error/no-disk-change/tree-count` | 拒绝 + 诊断 + 磁盘/树不变 |
| 非法名（空、`a/b`、`..`） | `ops.invalid.*` | 拒绝 + 各自原因，磁盘不变 |
| 重命名 | `ops.rename*` | 旧名消失/新名出现（磁盘+树），一致 |
| 重命名撞已有名 | `ops.rename-dup-refused/error/no-change/consistent` | 拒绝，无变化 |
| 删除文件 / 删除目录（递归） | `ops.delete-file*`, `ops.delete-folder*`（含 `rows-shrunk 3 -> 1`） | 磁盘消失 + 树同步 + 已物化行数下降 |
| 删除当前根 | `ops.delete-root-refused/error` | 拒绝并提示（避免面板丢失根） |
| 磁盘被外部改动 | `refresh.hidden-until-refresh` / `refresh.appears` / `refresh.consistent` | 刷新前不可见、刷新后出现 → 磁盘为先 |

演示日志（`05-screenshots.log`，同一目录的磁盘列表与树列表并排打印，逐字）：

```
DISK-CREATE …/rtl entries=5 [alu.sv,defs.vh,new_module.v,sub,top.v]
TREE-CREATE …/rtl entries=5 [alu.sv,defs.vh,new_module.v,sub,top.v]
OP create ok=1 status="Created file 'new_module.v' (Verilog)"
OP rename old_gone=1 new_present=1 status="Renamed 'new_module.v' to 'renamed_module.v'"
OP delete gone_from_disk=1 status="Deleted 'renamed_module.v'"
```

截图：`11-created-file.png`（新文件已选中、状态栏 `Created file 'new_module.v' (Verilog)`）、`12-renamed-file.png`、`13-deleted-file.png`。

### (c) 大目录 ≥10k 不阻塞 UI（懒加载）

fixture：`target/ws-filetree-tmp/big/` 下 **10000** 个 `module_NNNNN.v`（`04-fixture.log`）。

```
LAZY open_ms=64 rows_loaded=1250 can_fetch_more=1 scanned_dirs=1
LAZY fully_loaded rows=10000 total_ms=193 scanned_dirs=1
```

- 打开 64 ms（其中包含一次 readdir + 10000 次 stat 的代价），**没有**物化 10000 行：首批 250 行，视口再拉 5 批到 1250 行。
- `scanned_dirs=1` 证明**没有递归**读子目录；子目录节点只有展开时才读（`lazy.child-not-read ok=1`）。
- 单批 `fetchMore` 实测 `<1 ms`（日志 `MEASURE lazy.one-batch 0ms`，表里按毫秒整数）；全量 drain 193 ms（含 10000 行 insert）。
- 断言：`lazy.open-fast`（<500 ms，实测 64 ms）、`lazy.not-all-rows-materialised`、`lazy.can-fetch-more`、`lazy.only-root-scanned`、`lazy.batch-fast`、`lazy.total-rows`（10002 = 10000 + 2 子目录，测试 fixture）。
- 截图：`16-large-folder-lazy.png`（滚动条存在、只加载了可见范围）。

### (d) 空目录 / 权限拒绝的明确空状态

| 状态 | 实测文本 | checks |
|---|---|---|
| 未打开 | `No folder open.\nUse "Open..." to browse a project.` | `state.no-root*` |
| 空目录 | `This folder is empty.` | `state.empty-folder.text` + 截图 `14-empty-folder.png` |
| 权限拒绝 | `Cannot read folder 'denied': permission denied.`（状态栏保留完整路径） | `state.permission-denied*`、`perm.root-*` + 截图 `15-permission-denied.png` |
| 目录不存在 | `The folder does not exist: 'does-not-exist'` | `state.missing*` + 截图 `17-missing-folder.png` |
| 不是目录 | `'<name>' is not a folder.` | `state.not-a-folder*` |

居中空状态只放短句（dock 很窄），**完整绝对路径在状态栏**（可选复制）与 label tooltip 里；两者都来自 `FileTreeModel::rootError()`。

嵌套不可读目录不会被伪装成「空目录」：`perm.child-signalled`（`loadFailed` 一次）、`perm.child-tooltip`（tooltip 含 `permission denied`）、`perm.child-no-rows`。

**权限用例的诚实边界**：容器内默认以 root（rootless docker 的容器 root 映射到宿主 uid）运行，root 会绕过 DAC，所以 root 跑主套件时这两条打印 `SKIPPED running-as-root`（`03a-tests-full.log` 可查），**不计为通过**；真正的权限验证由 `06-permissions-nonroot.log` 以 uid 1000 单独跑（17 checks / 0 failures），截图 `15-…png` 也是 uid 1000 驱动拍下的。

---

## 5. `.v` 扩展名陷阱（与 KSyntaxHighlighting 解耦）

镜像里 `KSyntaxHighlighting::Repository::definitionForFileName("x.v")` 返回 **V（vlang）**（`docs/BUILD.md §12.6` / `evidence/m0-build/12-ksyntax-definitions-probe.txt`）。文件树**不**调用 KSyntaxHighlighting，而是用自有表：

| 扩展名 | 类型 |
|---|---|
| `.v` `.vh` | Verilog |
| `.sv` `.svh` | SystemVerilog |
| `.f` `.filelist` | File list（C9） |
| 其他 | File |

证据：`CHECK kind.v-is-verilog` / `kind.v-label=Verilog` / **`kind.v-id-not-vlang`**（显式断言不是 `v`）/ `kind.vh-*` / `kind.sv-*` / `kind.uppercase-suffix` / `kind.f-*`；UI 上选中 `top.v` 时状态栏显示 `top.v - Verilog`（`CHECK tree.status.kind`，截图 `10-file-tree-open.png` 末行）。demo 日志：`KIND top.v=verilog(Verilog) alu.sv=systemverilog(SystemVerilog) filelist.f=filelist(File list)`。

---

## 6. 接缝（`evidence/ws-palette/SEAM.md`）落实

| 约定 | 落实 |
|---|---|
| 目录 `src/workspace/filetree/` | ✔ |
| 库目标 | `vedit_file_tree`（SEAM 映射表指定，未改名） |
| 入口 | `src/workspace/filetree/filetree_integration.h` 声明 `void installFileTree(vedit::app::ApplicationShell&, const IntegrationOptions&)`，在 `vedit::workspace` 命名空间 |
| 只在根构建编译实现 | `CMakeLists.txt` 用 `if(TARGET vedit_app_shell)` 守卫；独立 configure 时不编译该 TU，也不链接 `src/app` |
| 只注册工厂、不插 dock、不 `showPanel()` | ✔（`installFileTree` 里没有任何 `showPanel()` 调用） |
| lane 库不链接 KTextEditor / tree-sitter | ✔ `NEEDED-KTEXTEDITOR-OR-TREESITTER count=0`（`09-real-app-install.log`） |
| 幂等 | 第二次 `installPanels()` 打印 `placeholderReplaced=0`，不重复建面板（`08-real-shell-seamcheck.log`） |

真实 `vedit-app`（`target/qt6-build/src/app/vedit-app`，由 `bash scripts/build-qt.sh` 产出）运行 1.2 s 的原始日志：

```
filetree: installFileTree projectRoot='target/ws-filetree-tmp/project' dockInserted=1 placeholderReplaced=1
```

真实外壳（同一 shell 代码、同一 `installPanels()` 入口）的断言（`08-real-shell-seamcheck.log`）：

```
SEAM dock-inserted=1 content-before-install=panel-placeholder-file-tree
SEAM panel-installed=1 dock-content-is-panel=1 find-child=1 root=/vedit-build/target/ws-filetree-tmp/project
SEAM file-index-valid=1
SEAM status-after-activate="Open /vedit-build/target/ws-filetree-tmp/project/rtl/top.v"
SEAM rows=5 scanned-dirs=2
SCREENSHOT ok=1 /vedit-build/evidence/ws-filetree/screenshots/20-real-shell-file-tree.png
FILETREE-SEAMCHECK ok=1
```

截图 `20-real-shell-file-tree.png`：真实 `ApplicationShell`（菜单 File/View/Help、分区编辑器、主题 Breeze Light）左侧 `Files` dock 就是本面板，状态栏同时显示 `Open …/rtl/top.v`（双击桥）。

为什么需要「替换已插入 dock 的占位内容」：`ApplicationShell` 构造时 `showDefaultPanels` 已经 `showPanel("file-tree")` 插入了带占位标签的 dock（`src/app/app_shell.cpp:82`），而 `setFactory()` 只影响**未来**的 `insert()`。因此 `installFileTree()` 在 `setFactory()` 之后检测 `panel-placeholder-*` 内容并 `dock->setWidget(新面板)`（`QDockWidget::setWidget` 不删除旧 widget，故显式 delete）。这不改布局、不插 dock，但它是「面板在真实 app 可见」的必要一步，特此显式说明供评审判断。

---

## 7. 错误路径与诊断

- 操作失败：`FileOpResult{ok=false, error, detail}` → 状态栏红字 + tooltip + `operationFailed(message)` + `diagnostics()`；磁盘不变、树不刷新（不会出现幽灵行）。
- 读目录失败：`FileTreeModel::loadFailed(path, error)` + 节点 `error` + tooltip；根级失败同时切到 `PermissionDenied` 空状态。
- `qWarning("filetree: …")` 会把根级/嵌套读取失败写进 stderr（`06-permissions-nonroot.log` 开头两行就是实例）。
- 状态栏文本可选中复制（`Qt::TextSelectableByMouse`）。

---

## 8. 已知限制 / 残余风险（如实）

1. **「双击打开」只到面板边界**：面板发出 `openFileRequested(path)` 并由接缝接到 shell 状态栏（临时桥）。真正的「在编辑器里打开」需要 edit-core 的 `EditorProvider` 提供打开文件的入口；T13 的 seam 只有 `createEditor()`，没有 open(path)。→ 面板侧已完成并有测试；端到端的编辑器打开属于 edit-core，跨 lane 待接。
2. **权限用例依赖非 root 身份**：容器默认 root，主套件里两条权限用例如实 `SKIPPED`；真实验证在 `06-permissions-nonroot.log`（setpriv uid 1000）。若评审环境只能以 root 跑，请以该日志为准，不要把 SKIPPED 当通过。
3. **真实 app 的窗口截图**：镜像里没有 `xwd/import/scrot/ffmpeg`，`vedit-app` 的 `--screenshots` 模式又是在自己的 shell 里构造（不调用 `installPanels`），因此真实二进制只提供了**运行日志**（`09-…log`，含 `placeholderReplaced=1`）；可视化截图来自 `rootcheck`（同一 `vedit_app_shell` 静态库 + 同一 `installPanels` 入口）。两者合起来构成「真实 app 里面板可见/可操作」的证据链。
4. **排序/过滤取舍**：目录在前、名称大小写不敏感排序；点开头（隐藏）文件默认不显示；符号链接按链接本身展示，展开时跟随目标（不递归扫描，无环风险）。
5. **大目录的同步代价**：单个目录的一次 readdir + 每项 stat（10k → 实测 62 ms）发生在首次展开，仍在 UI 线程。对极慢的网络盘/超大目录（如 100 万项）没有后台线程化——本票的 10k 指标已达标，更大规模属后续优化（requirements 未要求）。
6. **不做 Git 提交**：按 captain 的单一提交者约定，本票只落盘不 commit；写域内文件均为未跟踪新文件。
7. `evidence/m0-build/build-logs/*` 被门禁重写（见 §3 说明），运行前副本在 `target/filetree-backup/m0-build-logs-pre-t21/`。

---

## 9. 复跑指引

```sh
# 0) 一次性：pinned 镜像（T10）
bash scripts/build-qt.sh                 # 同时就是产物构建 + 门禁；exit 0

# 1) 本 lane 的独立证据（独立 configure / -Werror build / ctest / 非 root 权限套件 / 截图）
bash src/workspace/filetree/tools/run-in-container.sh      # → evidence/ws-filetree/01…06 + screenshots/1x
# 2) 真实 app 接缝证据（需要第 0 步的 target/qt6-build）
bash src/workspace/filetree/tools/run-real-shell-check.sh   # → evidence/ws-filetree/08…11 + screenshots/20
# 3) 仓库门禁（契约声明的命令）
bash build/gates/qt6-cxx.sh               # exit 0
```

---

## 10. 证据索引

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 本次运行的容器/工具链/源码版本 |
| `00-steps.txt` | 每条命令 + 观测退出码 |
| `01-configure.log` / `02-build.log` | 独立的 configure 与 `-Werror` 构建 |
| `03-ctest.log` / `03a-tests-full.log` | ctest 结果 / 111 条 CHECK 明细（含 2 条 root-SKIPPED） |
| `04-fixture.log` | 10000 文件 fixture 生成 |
| `05-screenshots.log` | 交互驱动：打开/新建/重命名/删除/空目录/权限/大目录/缺失，磁盘与树列表并排 |
| `06-permissions-nonroot.log` | uid 1000 的权限套件（17 checks） |
| `07a-gate-qt6-cxx-pre-seam.log` | 接缝落地**前**的门禁运行（lane 已纳入但未 wired） |
| `08-real-shell-seamcheck.log` | 真实 shell 接缝断言 |
| `09-real-app-install.log` | 真实 `vedit-app` 运行 + NEEDED=0 |
| `10-rootcheck-configure.log` / `11-rootcheck-build.log` | 根上下文工程配置/构建 |
| `12-gate-build-qt-final.log` | `bash scripts/build-qt.sh` 最终运行（exit 0，7/7 tests，lane wired） |
| `12a-gate-build-qt-first.log` | 接缝落地后的第一次完整构建（qInfo 之前，历史留档） |
| `13-gate-qt6-cxx-final.log` | `bash build/gates/qt6-cxx.sh` 最终运行（exit 0） |
| `14-real-shell-check-run.log` | `run-real-shell-check.sh` 整包输出（09–12 步，exit 0） |
| `screenshots/10…17` | 面板交互（打开/新建/重命名/删除/空目录/权限拒绝/大目录/缺失） |
| `screenshots/20-real-shell-file-tree.png` | 真实 shell 中左侧 Files dock = 本面板 |

---

## 11. 截图 sha256（`screenshots/`）

| 文件 | sha256 |
|---|---|
| `10-file-tree-open.png` | `a1c0810605f85481d39fa9da2f9ef795acd5a816ffa26f718cf6610b8661b6dd` |
| `11-created-file.png` | `06aed5dde15731855af3049adf614ee9cc0b93e92f462f0260958841ca8ea4a8` |
| `12-renamed-file.png` | `259021b4426454ae77049daba4619f621d08f7b4d520471195745c49754ef13b` |
| `13-deleted-file.png` | `c7501ef24b13362360055ea76555f913068fe56fdd53ee996cb47638eaede62e` |
| `14-empty-folder.png` | `9894236bcc9ea0a5d6dcbc1955dc3b7cd407ca2bfe0ea06c9b5d59e3d62337c4` |
| `15-permission-denied.png` | `f03a1cadd5296ba44312f038f02396c5e33e784fc82a24123fef186b8de02362` |
| `16-large-folder-lazy.png` | `263c351688354b643dc2e0173f4206a50f7281b72aa23f4ce2449acebe7486a8` |
| `17-missing-folder.png` | `9ceae4fba2556d31e31245adf6444dacaf743991ee984fa8bea654b63b16de82` |
| `20-real-shell-file-tree.png` | `046e4b03fb99e2695c6bb899d4db01897a914602acf66eb640fa373e129badb6` |
