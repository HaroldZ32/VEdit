# 批 1a 验收报告 — 主壳根传播（AC-1.1 / AC-1.2 / AC-1.5）

- 任务：board **T5**（`shell-root-propagation`），cell `cell-20261103T123639841Z`，split rev 1
- 契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/shell-root-propagation.md`（冻结 2026-11-03T13:10:27.019Z）
- 写者：**Senior Engineer**（member `Senior Engineer`）· 验证者：**Reviewer**（loop `loop-20261103T131047-68bfce`）
- 证据目录：`evidence/batch1-shell/`（`probe/`、`shots/`、`logs/`、本文件）

## 1. 交付内容（一句话）

主壳现在有**唯一的工程根**：普通启动（不带 `--project`）时在**任何 lane 注册面板工厂之前**就从
`session.json` 解析出来并交给全部 lane；运行时 `File > Open Folder`（以及 Files 面板自己的
Open Folder）切根后，通过一个广播信号让**已经存在的** Files / Search / VCS / Module Tree /
工程文件集（file set）全部改绑到新根；Search 的查询只可能落在新根上（日志逐条可核）。

## 2. 改了什么

| 文件 | 改动 |
|---|---|
| `src/app/app_shell.h` / `app_shell.cpp` | 新增 `projectRoot()` / `setProjectRoot()` / `setOpenFolderDialogProvider()` 与信号 `projectRootChanged(QString)`；构造函数在 `session_` 建立后按 `--project` → `session.json` 的 `project` 键解析根（`restoreSession` 为真且命令行未给根时），因此**早于 `installPanels()`**；新增 `File > Open Folder…` 命令（id `file.open-folder`，File 菜单 order 6）及其处理函数；`setProjectRoot` 更新状态栏/标题、持久化到下一次会话存档并广播（同一个根不重复广播）；`restoreSession` 的兜底路径同样广播 |
| `src/app/main.cpp` | `ApplicationShell` 构造后把 `shell.projectRoot()` 写回 `options.projectRoot` 再 `installPanels()` —— 每个 lane 用它本来就在读的 `IntegrationOptions` 就拿到恢复出来的根（不必各自再读一次会话）；`--selfcheck` 新增 13 条 `root.*` 断言 |
| `src/workspace/filetree/filetree_integration.cpp` | 工厂内实时读 `shell.projectRoot()`（不再捕获构造期副本）；`directoryOpened` → `shell.setProjectRoot()`；`projectRootChanged` → 已存在的 `FileTreeWidget::openDirectory()`（同根跳过，不会自环） |
| `src/workspace/search/search_integration.cpp` + `search_panel.cpp` | 工厂实时读 shell 根；`projectRootChanged` → 现存面板 `setRootPath()`（会取消在飞扫描、清结果、重画根标签与空态）；**无条件**记录 `search: root=…` 与 `search: query root=…`（AC-1.2 的日志判据，不依赖调试开关） |
| `src/workspace/vcs/vcs_integration.cpp` | 工厂实时读 shell 根；新 `applyRoot()`：`setRoot()`+`refresh()`，并把根写进 `vcs-header` 的 tooltip 与面板属性 `veditProjectRoot`（`vcs_panel.cpp` 不在本任务写域，标题文本保持原样）；`projectRootChanged` → 现存面板全部重读 |
| `src/workspace/module_tree/module_tree_integration.cpp` | holder 新增 `setProjectRoot()`：改根、清旧快照、重置 `project-model.json` 监视戳（文件集轮询下次按新根重读）并重启 hierarchy；holder 暴露 Qt 属性 `projectRoot`（objectName `module-tree-holder`）作为探针读数；工厂/安装用 `shell.projectRoot()` |
| `src/workspace/project/project_integration.cpp` | 首选 `shell.projectRoot()`（保留 lane 状态与 raw session 两处旧兜底）；`projectRootChanged` → `model->openRoot()`（按 model 现有根去重，避免与 Files 的 `directoryOpened` 双开） |
| `translations/vedit_zh_CN.ts` | 新增 3 条 UI 译文：`Open F&older…`、`Open Folder`、`Cannot open %1: not a folder`（576 messages，0 unfinished） |
| `evidence/batch1-shell/probe/{main.cpp,build.sh,container-run.sh,run.sh}` | 真实主壳探针 + 构建/运行脚本（新文件，仅落在既有 evidence 目录） |

冻结文件（root `CMakeLists.txt` / `Cargo*` / `rust-toolchain.toml` / `build/gates/*` / `package.json` /
`scripts/build-qt.sh`）**未改**：`ls -la --time-style=+%m-%d_%H:%M`（2026-11-03 21:37 采）实测 mtime 均早于本任务：

```
-rw-rw-r-- 1 haroldzhao haroldzhao  7580 11-02_22:13 CMakeLists.txt
-rw-rw-r-- 1 haroldzhao haroldzhao 50745 11-02_12:49 Cargo.lock
-rw-rw-r-- 1 haroldzhao haroldzhao  1995 11-02_12:49 Cargo.toml
-rwxrwxr-x 1 haroldzhao haroldzhao  4427 11-02_12:49 build/gates/gates.sh
-rw------- 1 haroldzhao haroldzhao   902 10-30_10:35 package.json
-rw-rw-r-- 1 haroldzhao haroldzhao   298 10-30_11:13 rust-toolchain.toml
-rwxrwxr-x 1 haroldzhao haroldzhao  7170 11-02_12:49 scripts/build-qt.sh
```

## 3. 根按什么路径到达每个面板

| 面板 | 普通启动（AC-1.1） | 运行时切根（AC-1.2） |
|---|---|---|
| Files | 工厂读 `shell.projectRoot()`（已由构造函数恢复） | `projectRootChanged` → `openDirectory(new)`；面板自身 Open Folder 反过来驱动 `setProjectRoot()` |
| Search | 工厂读 shell 根 → 不再显示 “No project root” | `setRootPath(new)`：取消在飞扫描、清旧结果、重画根标签；日志 `search: query root=` 只出现新根 |
| VCS header | `applyRoot()` → `setRoot()`+`refresh()`，header 显示该根的后端/分支/修订 | `applyRoot(new)` + tooltip/属性换新根，header 文本随之变化 |
| Module Tree | holder 以 shell 根构造并立即 elaborating | `holder->setProjectRoot(new)`：清快照 → 重新 elaborating 并显示 `root: <new>` |
| 文件集轮询 | 工程模型 `open(shellRoot)` → `project-model.json` → holder 的 `checkProjectModel()` 轮询消费 | `model->openRoot(new)` 重展开；holder 清监视戳后按新根重读（探针同时读 `ProjectModel::projectRoot()/files()` 与 holder 的 `projectRoot` 属性） |

## 4. 证据（全部在最终树上实跑）

### 4.1 真实主壳探针（不是 `--selfcheck`）

`--selfcheck` 在 `installPanels()` 之前就返回，看不到任何面板，因此**没有**被当作本 AC 的证据。
探针 `evidence/batch1-shell/probe/main.cpp` 链接产品自己的静态库（编译/链接行用
`ninja -t commands` 从 `src/app/CMakeFiles/vedit_app_shell.dir/app_shell.cpp.o` 与
`src/app/vedit-app` 取出，`build.sh` 里 **不重编 src/**），按 `main.cpp` 的真实顺序
（shell 构造 → `installPanels` → `startSession`）复现普通启动：临时 git 仓库 fixture、
预置 `session.json`（含 `project` 与四个面板 id）、**不带 `--project`**。

复现命令：

```bash
bash scripts/build-qt.sh                       # 产品必须先构建（探针链接其静态库）
bash evidence/batch1-shell/probe/run.sh        # 构建探针 + offscreen + Xvfb :78 xcb + 截图 + 真二进制冒烟
```

观测（最终跑，日志 `logs/12-probe-final-run.log`）：

| 运行 | 结果 | 日志 |
|---|---|---|
| offscreen（`QT_QPA_PLATFORM=offscreen`） | `BATCH1-SHELL-PROBE ok=1 checks=40 failures=0`，exit 0 | `10-probe-offscreen.log` |
| Xvfb **:78** + `QT_QPA_PLATFORM=xcb`（软件渲染） | `BATCH1-SHELL-PROBE ok=1 checks=42 failures=0`，exit 0（含 2 张截图） | `11-probe-xcb.log` |
| 真实 `vedit-app` 普通启动冒烟 | exit 0；四条 lane 日志均出现恢复出来的根（`real-binary root-lines-missing=0`） | `13-real-binary-ordinary-start.log` |

探针断言逐条（节选一次实跑的原始输出，`—` 后是实测值；临时 fixture 路径每次运行都不同）：

AC-1.1（普通启动根传播）

```
PASS a11.shell.restored-root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.panels.restored — files=1 search=1 vcs=1 moduleTree=1
PASS a11.files.root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.files.root-row — alpha-project
PASS a11.files.lists-root-content — rtl
PASS a11.files.file-reachable
PASS a11.search.root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.search.not-no-root — Enter a query and press Search.
PASS a11.search.root-label — In /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.search.hit — state=done hits=1 error=
PASS a11.search.hits-inside-root — hits=1
PASS a11.vcs.root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.vcs.backend — Git alpha-project @ de3e418 · 1 changed file(s)
PASS a11.module-tree.poll-root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.module-tree.non-empty — valid=1 rows=2 status=Hierarchy ready…
PASS a11.module-tree.summary-names-root — slang · 2 file(s) · 3 instance(s) … root: /tmp/…/alpha-project
PASS a11.fileset.root — /tmp/batch1-shell-probe-hJOrPF/alpha-project
PASS a11.fileset.non-empty — 2 file(s): …/rtl/second.v,…/rtl/top.sv
PASS a11.fileset.inside-root — …/rtl/second.v,…/rtl/top.sv
```

AC-1.2（运行时切根：直接走 Files 面板的 `openDirectory()`，即其 Open Folder 对话框确认后的同一调用）

```
PASS a12.shell.switched — …/beta-project
PASS a12.files.new-root — …/beta-project
PASS a12.search.new-root — …/beta-project
PASS a12.vcs.new-root — …/beta-project
PASS a12.vcs.header-changed — Git alpha-project @ de3e418 … -> Git beta-project @ 462c06a …
PASS a12.module-tree.poll-root — …/beta-project
PASS a12.module-tree.new-hierarchy — slang · 1 file(s) · 2 instance(s) … root: …/beta-project
PASS a12.fileset.new-root — …/beta-project
PASS a12.fileset.re-expanded — …/beta-project/rtl/other.sv
PASS a12.search.new-root-hit — hits=1
PASS a12.search.new-root-hits-inside — hits=1
PASS a12.search.old-token-not-found — hits=0        # 在 beta 根下搜 alpha 专属 token
PASS a12.search.log-announces-new-root
PASS a12.search.log-queried-new-root
PASS a12.search.log-never-queried-old-root
PASS a12.file-menu-open-folder — shell=…alpha-project search=…alpha-project
PASS a12.file-menu-vcs-followed — /tmp/…/alpha-project
PASS a12.file-menu-module-tree-followed — /tmp/…/alpha-project
```

`a12.search.log-never-queried-old-root` 读的是切根后**采集到的 lane 日志窗口**：其中出现
`search: root='<beta>'` 与 `search: query root='<beta>'`，且**没有任何** `search: query root='<alpha>'`
（探针把日志下标切开后逐行匹配，不是靠人眼看日志）。

### 4.2 xcb 截图（`shots/`，1500×1000 窗口）

| 文件 | 内容 | 实测 |
|---|---|---|
| `10-ordinary-start-root-a.png` | 普通启动：Project“alpha-project · 2 files”、Files 列 `rtl`、Module Tree 显示 `alpha_top / alpha_top.u_child` 且 `root: …/alpha-project`、Source Control `Git alpha-project @ …`、Search 根标签 `In …/alpha-project` 且命中 1、状态栏 `Project: alpha-project` | 148,591 B |
| `20-after-switch-root-b.png` | 切根后：Project“beta-project · 1 file”、Module Tree `beta_top / beta_top.u_child` 且 `root: …/beta-project`、Source Control `Git beta-project @ …`、Search 根标签 `In …/beta-project` 且 alpha token 0 命中、状态栏 `Project: beta-project` | 145,336 B |

### 4.3 断言只增不减（AC-1.5）

- `--selfcheck`：**245 → 258 checks**（+13，全部 `root.*`），0 failures。新增断言：
  `root.restored-before-install`、`root.explicit-wins`、`root.switch-broadcast`、`root.switch-value`、
  `root.switch-label`、`root.switch-idempotent`、`root.open-folder-command`、`root.open-folder-menu`、
  `root.open-folder-switches`、`root.open-folder-broadcast`、`root.open-folder-cancel`、
  `root.switch.persisted`、`root.switch.persisted-value`（见 `logs/gate-3-selfcheck.log`）。
- 既有断言**没有删除或放宽**；`ctest` 仍 **38/38**（本批未新增 ctest 用例，按 AC-1.5 的“或”分支以 selftest CHECK 计数覆盖）。

## 5. 七道门禁（最终树，命令与实测退出码）

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 | `bash scripts/build-qt.sh` | **EXIT=0**；`100% tests passed, 0 tests failed out of 38`；`M0-SMOKE ok=1 platform=xcb …` | `logs/gate-1-build-qt.log` + `evidence/m0-build/build-logs/33-qt6-ctest.log`、`36-qt6-xcb-smoke.log` |
| selfcheck | `vedit-app --selfcheck --state-dir /tmp/sc`（固定镜像内，offscreen） | **EXIT=0**，`APP-SHELL-SELFTEST ok=1 checks=258 failures=0` | `logs/gate-3-selfcheck.log` |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings`（`vedit-build:local`） | **CLIPPY_EXIT=0** | `logs/gate-4-clippy.log` |
| fmt | `cargo fmt --all -- --check`（`vedit-build:local`） | **FMT_EXIT=0**（无输出） | `logs/gate-5-fmt.log` |
| docs | `bash build/gates/docs.sh` | `[docs] PASS: …`，**DOCS_EXIT=0** | `logs/gate-6-docs.log` |
| lrelease | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts`（`vedit-qt6-build:local`） | `Generated 576 translation(s) (576 finished and 0 unfinished)`，**LRELEASE_EXIT=0** | `logs/gate-7-lrelease.log` |
| 探针 | `bash evidence/batch1-shell/probe/run.sh` | **PROBE_RUN_EXIT=0**（offscreen 40/0、xcb 42/0、2 张截图、真二进制冒烟 0 缺失） | `logs/12-probe-final-run.log` |

## 6. 未证明 / 边界（请验证者按此复核，不要按我的推理）

1. **VCS header 的“根”是间接可读的**：`src/workspace/vcs/vcs_panel.cpp` 不在本任务写域，header 文本仍是
   “后端 · 分支 @ 修订 · 变更数”，根以 **tooltip + 面板属性 `veditProjectRoot`** 暴露（探针断言 `a11.vcs.root` /
   `a12.vcs.new-root`）。截图里“换了根”的可见证据是分支名/修订/变更列表同时变化
   （fixture 两个仓库分别叫 `alpha-project`/`beta-project`）。若验收要求 header 正文直接显示根路径，
   需要 vcs 面板 lane 的写者补一行（本批未做，属跨写域事项）。
2. **Module Tree 依赖文档化的运行时工具链**：探针运行脚本按 AGENT.md §4 的第 3 条路径导出
   `VEDIT_SLANG=<checkout>/build/slang/out/slang`，探针首行打印实际解析结果
   （`TOOLCHAIN source=verible:local slang:local missing=[]`）。未测“没有 slang 的机器上 Module Tree 非空”。
3. **“文件集轮询”没有单独的仪表**：产品里它就是 `ProjectModel` 的有效文件集（根+成员，落盘为
   `project-model.json`）与 Module Tree holder 对该文件的轮询。探针读的是 `ProjectModel::projectRoot()/files()`
   与 holder 的 `projectRoot` 属性（`a11.fileset.*` / `a12.fileset.*`），不是内部定时器的 tick 计数。
4. **fixture 是本探针自建的临时 git 仓库**（`QTemporaryDir` 下，分支名=目录名，各留 1 个未提交修改）；
   未引用工作树、未在 VEdit 工作树内创建任何仓库。
5. **探针构建方式的边界**：它链接 `target/qt6-build` 的静态库（不是 AppImage 内的二进制）；
   `build.sh` 里对链接行只做了“换上探针 object / 去掉 `main.cpp.o` / 改输出名”的替换，没有重编 `src/`。
6. **翻译**：3 条新 UI 串已补中文译文（lrelease 576/576 finished、0 unfinished）；`.ts` 的 `location`
   行号是手写的近似值（本仓库构建只跑 lrelease，不跑 lupdate）。
7. **文档面未改**：`docs/` 不在本任务写域，所以 `docs/USER_GUIDE.md` 还没有“File > Open Folder 切根”的
   用户说明；建议由文档/集成任务补一句（不影响本 AC 的产品行为）。
8. **“关闭工程”（空根）没有 UI 入口**：Files 面板没有 Close Folder 动作，`ProjectModel` 也没有 close API，
   因此本批只覆盖“有根 → 另一个根”。若将来加入“关闭文件夹”，Project 面板需要单独的清理路径（本批未做）。
9. `bash scripts/build-qt.sh` 的固有副作用仍会发生（重新生成 `crates/vd-ffi/include/vd_ffi.h`、
   写 `evidence/ffi-abi/logs/*` 与 `evidence/m0-build/build-logs/*`），这是门禁命令本身的行为，不是本任务编辑。

## 7. 改动文件清单（本任务编辑的路径，全部在写域内 + 契约要求的 evidence）

产品代码：
`src/app/app_shell.h`、`src/app/app_shell.cpp`、`src/app/main.cpp`、
`src/workspace/filetree/filetree_integration.cpp`、
`src/workspace/search/search_integration.cpp`、`src/workspace/search/search_panel.cpp`、
`src/workspace/project/project_integration.cpp`、
`src/workspace/module_tree/module_tree_integration.cpp`、
`src/workspace/vcs/vcs_integration.cpp`、
`translations/vedit_zh_CN.ts`

证据：
`evidence/batch1-shell/{REPORT.md,probe/{main.cpp,build.sh,container-run.sh,run.sh},shots/*.png,logs/*}`
