# app-shell (T13) 结论：Qt6 应用外壳

**结论：交付成立。** QMainWindow + 面板注册表 + 水平/垂直分屏（多编辑器实例、
焦点切换、关闭重排、布局持久化）+ 菜单/快捷键框架 + 主题启动生效 + 无工程空状态
全部实现并实测；`bash scripts/build-qt.sh` 退出码 0，ctest 2/2 通过（含本 lane
自检 73/73），xcb 冒烟 PASS。真实编辑器与面板内容按边界留给各自 lane（见 §6 与
`06-seams.md`）。

- Board task: `T13` · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/app-shell.md`
- 交付源码: `src/app/`（2857 行，10 个文件，见 `08-structure.txt`）
- 证据目录: `evidence/app-shell/`
- 构建镜像: `vedit-qt6-build:local`
  `sha256:de2b7f708fe58e98f4ec791494d48614ec25ffe33efc2974d86bdb5d60063c14`
  （Ubuntu 26.04 / Qt 6.10.2 / KF6 6.24.0 / tree-sitter 0.25.9，见 `00-pins.txt`）

## 1. 构建与门禁（观察到的命令与退出码）

| 命令 | 观察结果 | 证据 |
|---|---|---|
| `bash scripts/build-qt.sh`（`docs/BUILD.md` §12.2 声明的一条命令：pin 镜像 → configure → build → ctest → xcb 冒烟；`build/gates/qt6-cxx.sh` 对 `gates`/`typecheck` 调用的就是它） | **exit 0**，结尾 `[qt6] PASS: configure + build + ctest + xcb/software-rendering smoke inside vedit-qt6-build:local` | `01-build-qt.log` |
| 同上内置 ctest | **2/2 passed**：`vedit-m0-smoke` + **`vedit-app-shell-selftest`** | `01-build-qt.log`、`02-ctest-selftest.log` |
| `ctest -R vedit-app-shell-selftest -V` | **exit 0**，`APP-SHELL-SELFTEST ok=1 checks=73 failures=0` | `02-ctest-selftest.log` |
| `vedit-app --selfcheck`（xcb + llvmpipe，Xvfb） | **exit 0**，`APP-SHELL-SELFTEST ok=1 checks=73 failures=0`，73 个 CHECK 全 PASS | `03-selfcheck-xcb.log` |
| `vedit-app --screenshots evidence/app-shell`（xcb + llvmpipe） | **exit 0**，4 张 1440x900 PNG | `04-screenshots.log`、`05-artifacts.txt` |

注：`bash scripts/build-qt.sh` 自身把规范化日志写进 `evidence/m0-build/build-logs/`
（T10 lane 的目录）；本 lane 的日志副本是 `01-build-qt.log`，两者内容一致。
本任务没有改根 CMakeLists：`src/app/CMakeLists.txt` 存在即被根工程自动纳入
（`-DVEDIT_BUILD_APP_SHELL=ON` 为默认值）。

## 2. 契约 (a)–(e) 逐条对照

### (a) QMainWindow + 面板注册表（按需插入）

- `ApplicationShell : QMainWindow`；`PanelRegistry` 注册 6 个面板 spec
  （file-tree / search / terminal / vcs / settings / outline，含 owner lane 与默认
  dock 区域），**只有被请求时才 `insert()` 为 QDockWidget**；View 菜单与快捷键逐项
  toggle，未提供 factory 时插入带标注的占位内容（空状态，不崩）。
- 实测（`02-ctest-selftest.log` / `03-selfcheck-xcb.log`）：
  `panels.registry.six`、`panels.none-inserted-by-default`、
  每个面板 `panels.show.*` / `panels.toggle-hide.*` / `panels.toggle-show.*`、
  `panels.six-docks` 全 PASS。
- 接缝见 `06-seams.md` §2（`PanelRegistry::setFactory(id, ...)`）。

### (b) 水平与垂直分屏：多编辑器实例、焦点切换、关闭与重排、布局持久化

- `EditorArea` 用二叉分割树（QSplitter 递归）管理多个编辑器 pane；每个 pane 由
  `EditorProvider` 生成一个编辑器 widget（当前为 demo 占位，edit-core 接入后即为
  真实编辑器实例）。
- 操作：`splitActive(Horizontal|Vertical)`、`focusNextPane()`、`closeActivePane()`
  （关闭最后一个 pane 时自动补一个空 pane，保证 ≥1）、`resetLayout()`、
  `setActivePane()`；结构变化时模型是唯一事实源，重建 widget 树（pane 内容不丢）。
- 布局持久化：`EditorArea::saveLayout()/restoreLayout()` 序列化 split 树、
  orientation/ratio、pane id、active pane；会话文件保存/恢复由
  `SessionStore` + `ApplicationShell::saveSession()/restoreSession()` 完成，
  窗口 geometry 与 dock 状态用 `QMainWindow::saveState()`（base64）一并持久化。
- 实测：`split.horizontal.pane-count`、`split.horizontal.root-orientation`、
  `split.vertical.pane-count`、`split.vertical.root-orientation`、
  `panes.three`、`focus.set-second`、`focus.next-wraps`、`close.pane-count`、
  `reset.one-pane`、`close.last-pane.keeps-one`、`close.last-pane.active`、
  `session.save`、`restore.pane-count`、`restore.layout-tree`、
  `restore.active-pane` 全部 PASS。
- 截图：`10-horizontal-split.png`（3 pane 并排，左边 Files、右边 Outline dock）、
  `11-vertical-split.png`（3 pane 上下堆叠）。

### (c) 菜单与快捷键框架（可改键位的存储点在 settings-ui，本任务只留接口）

- `CommandRegistry`：每条命令声明一次（id/text/menu/默认快捷键/order/checkable/
  separator），生成 QAction 与菜单；快捷键从 `IShortcutStore` 读取并可用
  `refreshShortcuts()` 重新应用。菜单：File / View / Help，View 内含分屏、焦点、
  重置、六个面板 toggle 子项。
- 接口：`IShortcutStore`（get/set/clear/save/location）；默认实现
  `JsonShortcutStore`（`<state-dir>/shortcuts.json`），settings-ui 实现/包装后即可提供
  改键 UI 与冲突提示（见 `06-seams.md` §3）。
- 实测：`commands.action.*`（10 个命令）、`commands.file-menu-actions`、
  `commands.view-panel-actions-checkable`、`shortcut.override.applied`、
  `restore.shortcut` 全 PASS。

### (d) 主题加载（KSyntaxHighlighting/KTextEditor 主题或 Qt 调色板，启动即生效）

- `ThemeManager` 从 `KSyntaxHighlighting::Repository` 取主题目录（本镜像 30 个），
  把选中主题映射成 **Qt 应用调色板**（Window/Base/Text/Highlight/ToolTip…）并在启动时
  `QApplication::setPalette()`；主题名持久化到会话，重启后恢复；主题名通过
  `syntaxThemeName()` 交给 editor lane，供其应用到 KTextEditor 同一 schema。
- 非法主题名是**可恢复错误**：拒绝、保留原调色板、记录 diagnostic，不崩。
- 实测：`theme.catalogue`（≥5）、`theme.apply`、`theme.invalid.rejected`、
  `theme.invalid.keeps-palette`、`restore.theme` PASS；截图状态栏显示
  `Theme: Breeze Light`；主题切换在 `12-panels-and-docks.png` 与
  `13-empty-state-no-project.png` 的配色差异中可见（同一会话内两个主题）。

### (e) 无工程时可用、空状态不崩

- 无 `--project` 时标题/状态栏显示 `no project`；面板占位显示“no project open”空状态；
  一个空编辑器 pane 仍可操作。坏会话文件/坏快捷键文件只产生 diagnostic，不阻止启动；
  连续关闭 pane 5 次仍保持 ≥1 pane。
- 实测：`empty.*`、`broken.config.survives`、`broken.config.diagnostics`、
  `stress.*` 全 PASS；截图 `13-empty-state-no-project.png`。

## 3. 目前的状态与已验证的行为（摘要）

- 73/73 自检项 PASS；ctest 集成通过；xcb（真实 X 后端 + llvmpipe）下同样 73/73 通过。
- 分屏：水平 3 pane / 垂直 3 pane 两种状态均有截图与结构断言。
- 持久化：同一 state-dir 新建 shell 后，pane 数、split 树 JSON、active pane、主题、
  面板可见性、快捷键覆盖全部逐项恢复（`restore.*`）。
- 主题：目录来自 KSyntaxHighlighting；非法名被拒绝且调色板不变。
- 依赖：`vedit-app` 只链接 Qt6 Widgets/Gui/Core/Network/DBus + KF6
  SyntaxHighlighting；**不链接 KTextEditor / tree-sitter**（`03-elf.txt` NEEDED 中
  KTextEditor 计数 0），满足“外壳不依赖编辑器 lane”的边界。

## 4. 明确未覆盖 / 边界（如实记录）

- **编辑器内容是占位**：`DemoEditorProvider` 是 QPlainTextEdit 占位实现，仅为让外壳
  可运行、可截图、可测分屏；真实 KTextEditor 实例由 edit-core 通过
  `EditorProvider` 注入，本任务未写 `src/editor/`。
- **面板内容是空占位**：六个面板只有带 lane 标注的占位 widget；内容由
  workspace/tools/vcs/settings/editor 各自 lane 提供（`PanelRegistry::setFactory`）。
- **主题只应用到 Qt 调色板**：KTextEditor 自身的 schema 应用属于 editor lane；
  外壳只提供主题名与持久化。
- dock 状态恢复只在 offscreen/xcb 单进程内验证，未验证跨 KTextEditor/版本升级的
  `restoreState` 兼容性；失败时会记录 diagnostic 并继续启动（实测路径见
  `restoreSession` 的 dockState 分支，但本任务没有构造该失败用例）。
- 未实现真实用户会话（文件打开、项目模型、状态栏编辑器信息），这些属于后续 lane。
- `bash scripts/build-qt.sh` 会写 `evidence/m0-build/build-logs/`（T10 的目录）；
  本 lane 只保留 `01-build-qt.log` 副本，不改 T10 的证据语义。

## 5. 证据清单

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 镜像 id/版本、head、全部命令与退出码 |
| `01-build-qt.log` | `bash scripts/build-qt.sh` 完整输出（configure/build/ctest/xcb 冒烟，exit 0） |
| `02-ctest-selftest.log` | `ctest -V` 的 app-shell 自检（73 CHECK：PASS，Test Passed） |
| `03-selfcheck-xcb.log` | xcb + llvmpipe 下 `--selfcheck` 全量输出（73/73） |
| `03-elf.txt` | `vedit-app` 的 file/NEEDED/最高 GLIBC/ldd/是否链接 KTextEditor/sha256 |
| `04-screenshots.log` + `05-artifacts.txt` | 截图生成日志、sha256 与尺寸 |
| `06-seams.md` | 四个接缝 + 会话格式 + 构建集成（供其他 lane） |
| `08-structure.txt` | 源码文件清单与行数 |
| `10-horizontal-split.png` | 水平分屏（3 pane，Files/Outline dock） |
| `11-vertical-split.png` | 垂直分屏（3 pane 堆叠） |
| `12-panels-and-docks.png` | 六个面板 dock 全部可见（2 pane） |
| `13-empty-state-no-project.png` | 无工程空状态（1 pane，无 dock） |
