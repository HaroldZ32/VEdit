# UI-Mosaic T3（chrome-ui / 界面接线）证据报告

- 看板任务：T3（blocked_by T2/theme-kernel，T2 已完成并冻结）
- 写入范围：`src/app/app_shell.h`、`src/app/app_shell.cpp`、`src/app/session_store.h`、
  `src/app/editor_area.cpp`、`src/workspace/settings/settings_panel.cpp`、
  `translations/vedit_zh_CN.ts`（`settings_integration.cpp` 无需改动，见 §8）
- 冻结依据：`.mpd/plans/ui-mosaic/CONTRACT.md` §0/§2、`REQUIREMENTS.md` A1–A7
- 基线（开工前）：`bash scripts/build-qt.sh` exit 0，全量 ctest 62/62，`lrelease` 双 0

---

## 1. 交付清单（路径 + 符号）

### 1.1 `src/app/app_shell.h`

| 符号 | 说明 |
| --- | --- |
| `ShellOptions::uiTheme` | 产品主题 id 的显式接缝（`--theme-ui` 将来填入）；空 = 不强制 |
| `ApplicationShell::applyProductUiTheme(QString, bool)` | 唯一的 UI 主题应用点（切换 + 启动） |
| `ApplicationShell::createThemeMenu(QWidget*, QString)` | 生成同一份 6 项互斥菜单（视图子菜单 / 工具栏按钮） |
| `ApplicationShell::syncThemeMenuChecks()` | 两份菜单的勾选状态跟随 `ThemeManager::currentUiTheme()` |
| `ApplicationShell::refreshToolBarIcons()` | 按当前应用调色板重着色工具栏图标 |
| `ApplicationShell::changeEvent(QEvent*)` | 处理 `QEvent::ApplicationPaletteChange`（Qt 6 推荐方式），刷新图标/勾选 |
| 成员 `themeToolBar_` / `themeButton_` / `viewThemeMenu_` / `themeButtonMenu_` | 主题入口对象 |

### 1.2 `src/app/app_shell.cpp`

| 位置 | 内容 |
| --- | --- |
| 匿名命名空间（:66） | `toolbarIconName()` 命令 id→SVG 名；`paletteProductTheme()` 用调色板 `Window == canvas` 反查产品主题；`readSettingsString()` 单键读取 `settings.toml`（与 `i18n.cpp` 的语言读取同一规则） |
| 构造函数（:146/:241/:246） | `setObjectName("veditMainWindow")`；central widget 为画布容器（外边距 8px = cards.qss `@gap@`），内含 `QFrame#veditCard` 包裹 `EditorArea` |
| `buildMenuBar()`（:1042） | 在「视图」菜单**末尾追加子菜单**「主题(&T)」（objectName `menu-theme`），不新增顶级菜单 |
| `buildToolBar()`（:1053 起） | `Qt::ToolButtonTextBesideIcon`；每个 action 设 `themed_icons::actionIcon(<name>, theme.text)` 且 `setIconVisibleInMenu(false)`；主工具栏 actions() 顺序**完全不变** |
| 主题工具栏（:1112–1137） | 独立 `QToolBar`（objectName `vedit-theme-toolbar`，TopToolBarArea，不可拖动）+ Expanding spacer + `QToolButton`（objectName `theme-button`，`InstantPopup`，图标 `actionIcon("theme", accent)`，菜单 `menu-theme-button`） |
| `createThemeMenu()`（:1140 起） | 6 个 `QActionGroup` 互斥项，顺序 = `productThemes()`，文本 = 契约冻结的中文名，data = 主题 id |
| `applyProductUiTheme()`（:1376） | 调 `themes_->applyUiTheme(id, &err)`；失败：`addDiagnostic` + 状态栏 8s（用户操作时）+ 回退默认；成功后刷新图标、菜单勾选、状态栏 |
| `applyThemeForStartup()`（:1360 起） | 语法主题之后应用 UI 主题：`options_.uiTheme` > `settings.toml uiTheme` > 默认 yunwu |
| `restoreSession()`（:1420 起） | 原始 JSON 一次读入（`uiTheme` 键单独取），语法主题之后按 `options_.uiTheme` > `settings.toml uiTheme` > 会话存档 `uiTheme` > 默认应用 |
| `saveSession()`（:1496 起） | `state.uiTheme = themes_->currentUiTheme()`；`state.toJson()` 后追加 `"uiTheme"` 键再原子写出 |
| `updateStatusBar()`（:1836） | 复用既有 `status-theme`：`tr("Theme: %1 / %2")`（zh = `主题：<产品主题名> / <语法主题名>`），并同步两份菜单勾选 |

### 1.3 `src/app/session_store.h`

- `SessionState::uiTheme` 字段（空 = 未选择）+ 归档格式注释补充 `uiTheme` 键。
- 按船长裁决，`session_store.cpp` **未改**；读写在该键在 `ApplicationShell::restoreSession()/saveSession()` 内完成（契约 §2.1 的“写入会话存档”行为已满足）。

### 1.4 `src/app/editor_area.cpp`

- `currentProductTheme()`（:55）：用应用调色板 `QPalette::Window == canvas` 反查产品主题（无需第二个注册点）。
- `EditorPane::applyTabStyle()`（:661 起）：胶囊标签页，`border-radius: 8px`；未选中 = 透明 + `textMuted`；选中 = `surface` 底 + `accent` 文字 + 1px `border`；hover = `surfaceAlt`；active badge 用 `accent`。不再用调色板混色。
- `EditorArea` 容器 objectName = `veditCard`（:1359）。

### 1.5 `src/workspace/settings/settings_panel.cpp`

- `kUiThemeKey = "uiTheme"`（:56）。
- `buildUi()`（:324）：`settings-ui-theme` 下拉列 6 套产品主题（displayName 显示 / id 作 itemData），放在既有语法主题下拉**上方**；激活即 `themes_->applyUiTheme(id, &err)` → 成功 `SettingsClient::setValue(entries_, "uiTheme", id)` + `persist()`，失败 `addDiagnostic` + 状态标签，不静默。
- `applyThemeFromEntries()`（:822）：reload 时按存储的 `uiTheme` 恢复（空则不覆盖会话/启动选择）。
- `refreshThemeCombo()`（:1704）：产品主题下拉跟随实时 `currentUiTheme()`（菜单/工具栏切换后打开首选项也一致）。

### 1.6 `translations/vedit_zh_CN.ts`

手工新增 9 条 `<message>`（未跑 lupdate）：

- context `vedit::app::ApplicationShell`：`&Theme`→`主题(&T)`、`Theme`→`主题`、`Theme: %1 / %2`→`主题：%1 / %2`、`Theme '%1' could not be applied: %2`→`主题“%1”无法应用：%2`
- context `vedit::workspace::settings::SettingsPanel`：`Interface theme:`→`界面主题：`、``Applied immediately and stored as `uiTheme` by the Rust service.``、`Interface theme '%1' could not be applied.`、`Interface theme '%1' could not be applied: %2`、`Interface theme '%1' applied and stored by the Rust service.`

---

## 2. 工具栏命令 → SVG 映射（船长要求的逐条清单）

| 工具栏命令 id | SVG 名 | 备注 |
| --- | --- | --- |
| `file.new` | `new` | |
| `file.open` | `open` | |
| `file.open-folder` | `open` | 共用“打开”图形 |
| `file.switch-project` | `open` | 无专用“切换工程”图形，用最接近的打开图形 |
| `file.save` | `save` | |
| `file.save-all` | `save` | 共用“保存”图形 |
| `edit.undo` | `undo` | |
| `edit.redo` | `redo` | |
| `search.focus` | `find` | |
| `project.lint` | `build` | |
| `diagnostics.format` | **`settings`（契约 §3.3 兜底）** | **唯一用 settings 兜底的 id**：本轮未随包 formatter 图形 |
| 主题按钮（非命令） | `theme` | 强调色 `accent` |

结论：**没有配到专用图形而以 `settings` 兜底的命令只有 `diagnostics.format` 一条**；`file.switch-project` 用的是 `open`（不是 settings 兜底）。

---

## 3. 启动优先级（实测，见 §6 probe）

`ShellOptions::uiTheme`（`--theme-ui` 接缝） > `settings.toml` `uiTheme` > 会话存档 `uiTheme` > 默认 `yunwu`。

probe 结果：

```
PASS startup.archive-ui-theme :: yaoshi
PASS startup.settings-beats-archive :: bohe
PASS startup.seam-beats-settings :: qingkong
```

---

## 4. 命令与观测输出（原始）

### 4.1 `bash build/gates/lrelease.sh`（exit 0）

日志：`evidence/ui-mosaic/chrome-ui/logs/20-lrelease.log`

```
Generated 1267 translation(s) (1267 finished and 0 unfinished)
[lrelease] counts: 1267 translation(s) (1267 finished, 0 unfinished, 0 untranslated)
[lrelease] qm: 134401 bytes sha256=4635eb9987ac5d25e008c21336a1fe426a32b56de11b643528d7f6f26e09e00e
[lrelease] PASS: 0 unfinished and 0 untranslated in /home/haroldzhao/MyProj/DshProj/VEdit/translations/vedit_zh_CN.ts
LRELEASE_EXIT=0
```

### 4.2 `bash scripts/build-qt.sh`（第一次全量，exit 0）

日志：`evidence/ui-mosaic/chrome-ui/logs/10-build-qt.log`

```
100% tests passed, 0 tests failed out of 62

Total Test time (real) =  85.33 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0 tree_sitter=0.25.9 window=1 editor=1 document=1
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
BUILD_QT_EXIT=0
```

`vedit-app-ux-selftest` / `vedit-app-shell-selftest` / `vedit-app-ui-theme-selftest` 均通过（`vedit-app-ui-theme-selftest` 见 §4.3 与日志第 16 项）。

### 4.3 theme-kernel 的 QSS 增量落盘后的最终构建（exit 0）

日志：`evidence/ui-mosaic/chrome-ui/logs/12-final-post-t5-build-qt.log`

T5 的 `resources/themes/cards.qss`（`QMainWindow::separator`、`QDockWidget` 卡片本体、
`QDockWidget > QWidget` 兜底）与 `src/app/ui_theme_selftest.cpp`（dock 像素断言）已进入本次构建
（`[344/400] Linking CXX executable src/app/vedit-app-ui-theme-selftest`）：

```
16/62 Test #16: vedit-app-ui-theme-selftest ....................   Passed    0.04 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  85.68 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0 tree_sitter=0.25.9 window=1 editor=1 document=1
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
POST_T5_BUILD_QT_EXIT=0
```

**没有出现 T5 的 dock 断言红**，无需回传失败输出。

### 4.4 一次性接线 probe（`target/`，不入交付）

日志：`evidence/ui-mosaic/chrome-ui/logs/30-chrome-probes.log`

`target/theme_probe.cpp`（用 `vedit-app-ux-selftest` 的编译/链接参数构建，含 app_icon/theme.qss 资源）：

```
PASS window.objectName :: veditMainWindow
PASS menubar.at-most-five :: menu-file,menu-edit,menu-view,menu-help
PASS view.theme-submenu
PASS view.theme-six-items :: 云雾白,暖杏黄,薄荷绿,晴空蓝,曜石黑,深夜蓝
PASS view.theme-checked-default :: yunwu
PASS view.theme-names :: 云雾白,暖杏黄,薄荷绿,晴空蓝,曜石黑,深夜蓝
PASS toolbar.text-beside-icon
PASS toolbar.every-action-has-icon :: 9/9
PASS toolbar.icons-hidden-in-menu
PASS toolbar.theme-toolbar
PASS toolbar.theme-button
PASS toolbar.theme-button-menu
PASS toolbar.theme-button-icon
PASS status.two-theme-names :: Theme: 云雾白 / Breeze Light
PASS editor-area.veditCard :: veditCard
PASS central.gap-8px
PASS central.editor-card-frame
PASS switch.current-id :: yaoshi
PASS switch.stylesheet-accent
PASS switch.status-updated :: Theme: 曜石黑 / Breeze Light
PASS switch.menu-check-follows
PASS switch.tab-style-follows-accent
PASS session.save
PASS startup.archive-ui-theme :: yaoshi
PASS startup.settings-beats-archive :: bohe
PASS startup.seam-beats-settings :: qingkong
RESULT checks=26 failures=0
THEME_PROBE_EXIT=0
```

说明：裸 shell 未安装 diagnostics lane，故工具栏只有 9 个 action（probe 断言 9/9 全带图标）；产品里 `project.lint` / `diagnostics.format` 由 diagnostics lane 注册后同样带图标（映射表见 §2）。

T6 修复后该 probe 扩展到 28 项（新增 `switch.paired-syntax-theme`、`startup.settings-paired-syntax`），
**28/28 PASS**；真实产品进程的图标不透明像素与主题按钮几何见 §8.1/§8.2。

`target/settings_probe.cpp`（用 `vedit-workspace-settings-selftest` 的参数构建）：

```
PASS prefs.ui-theme-combo
PASS prefs.ui-theme-six-items :: 6
PASS prefs.ui-theme-id-order :: yunwu,xinghuang,bohe,qingkong,yaoshi,shenye
PASS prefs.ui-theme-above-syntax
PASS prefs.ui-theme-applies :: yaoshi
PASS prefs.ui-theme-persists :: uiTheme = "yaoshi"
PASS prefs.ui-theme-status :: Stored by the Rust service in /tmp/settings_probe-*/settings.toml
RESULT checks=7 failures=0
SETTINGS_PROBE_EXIT=0
```

### 4.5 快速迭代构建/ctest（构建期间）

`target/quickbuild.log`、`target/quickctest.log`：同一镜像内 `cmake --build` + 全量 `ctest`，`100% tests passed, 0 tests failed out of 62`。

---

## 5. 覆盖的需求

| 需求 | 落点 | 证据 |
| --- | --- | --- |
| A1 主题可切换 + 持久化 | 视图子菜单 / 工具栏按钮 / 首选项下拉；`uiTheme` 写 settings.toml + 会话存档 | §4.4 theme probe、settings probe、§3 |
| A2 卡片视觉（EditorArea 侧） | `QFrame#veditCard` 外层 + 8px 画布 + 胶囊标签页 | probe `central.gap-8px` / `central.editor-card-frame` / `switch.tab-style-follows-accent` |
| A5 工具栏图标 + 菜单纯文字 | TextBesideIcon + 每 action 图标 + `setIconVisibleInMenu(false)` | probe `toolbar.*`、ux_selftest 62/62 |
| 翻译完整 | 9 条手工 `<message>` | §4.1 双 0 |
| 不新增顶级菜单 | 主题入口是 View 子菜单 | probe `menubar.at-most-five`（5 菜单断言由 ux_selftest 全绿覆盖） |
| 零告警 | `-Wall -Wextra -Wpedantic -Werror` | `10-build-qt.log` / `12-final-post-t5-build-qt.log` 均构建通过 |

---

## 6. 偏差与未覆盖项（诚实边界）

1. **dock 卡片 `QFrame#veditCard` 包裹：未做（船长已批准）**。`QDockWidget::setWidget(card)` 会改变 `dock->widget()`，实测打红：
   `vedit-app-shell-selftest`（`panel-seam.terminal-content` / `search-content` / `no-placeholder`）、
   `vedit-app-ux-selftest`（`a2.f1.terminal-panel`）、`vedit-terminal-selftest`（`panel.dock.content`），
   并使 `filetree_integration` 的占位面板替换失效（文件树永不安装）。契约 §0.5“既有 ctest 全绿”优先，
   所以 `panel_registry.cpp` **未改**，dock 卡片化改由 QSS 承担。
   **待 theme-kernel 的 QSS 增量（已由船长派 T5）**，需要：
   - `QMainWindow::separator { width: 8px; height: 8px; background: @canvas@; }`（dock 之间的真实留白）；
   - `QDockWidget` 本体：`background: @surface@; border: 1px solid @border@; border-radius: @radius@;`；
   - `QDockWidget::title` 作为卡片头（透明 + 加粗 + 上下留白）；
   - 内容控件背景兜底：`QDockWidget > QWidget { background: transparent; }`（仅限 dock 子控件，避免污染对话框/菜单）。
   当前 `cards.qss` 的 `QDockWidget { background: @canvas@; border: none; }` 仍会呈现“直边拼贴”，
   T5 落地后需用真实截图复判 A2（船长会做）。
2. **`--theme-ui` CLI 未接入**：契约 §2.1 明确“本轮不新增 CLI”，`main.cpp` 属 icon-assets 车道且被禁止改动。
   已交付 `ShellOptions::uiTheme` 接缝并 probe 证明它优先级最高；产品入口尚未解析该参数（未声称可用）。
3. **`settings_integration.cpp` 无改动**：Preferences 对话框直接把 `shell.themes()` 传给 `SettingsPanel`，
   新下拉的应用/持久化都在 `settings_panel.cpp` 内闭环，无需额外接线。
4. **`session_store.cpp` 未改**（船长批准）：`uiTheme` 键由 `app_shell.cpp` 读写；`SessionState::toJson/fromJson`
   不认识该键，头文件已注明。
5. **无真实截图**：probe 全部跑在 `QT_QPA_PLATFORM=offscreen`；A2 的像素级观感（圆角/描边/留白）需在 T5 的 QSS
   落地后由船长的集成截图判定。本轮不声称截图通过。
6. **probe 是自建一次性程序**（`target/theme_probe.cpp`、`target/run_theme_probe.sh` 等），不在交付源码树内，
   也不进入任何 CMake target；其结论只作为“接线存在且行为正确”的证据，不替代 ctest。

---

## 7. 复现

```bash
bash build/gates/lrelease.sh                     # 0 unfinished / 0 untranslated
bash scripts/build-qt.sh                         # 全量构建 + 62 ctest + xcb smoke
# 可选：接线 probe（一次性，非交付）
source scripts/lib.sh && source scripts/lib-qt.sh && load_versions && load_qt_versions
run_in_qt6_container bash /vedit-build/target/run_theme_probe.sh
run_in_qt6_container bash /vedit-build/target/run_settings_probe.sh
```

最终构建汇总行（T5 QSS 已包含）：

```
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  85.68 sec
POST_T5_BUILD_QT_EXIT=0
```

---

## 8. T6 修复记录（船长集成复核的 4 个缺陷）

### 8.1 缺陷 1：工具栏图标全透明（A5）

- **运行期取证**（`VEDIT_UI_PROBE=1`，真实 `vedit-app`，日志
  `logs/40-runtime-yunwu.log`）：11 个 action 全部 `iconNull=0` 但 `opaquePixels=0`，
  `themeButton iconNull=0` —— 图标不是 null，是**全透明**，所以
  `QIcon::isNull()` 断言抓不到。
- **根因（theme-kernel 车道，已由 T7 修复）**：`themed_icons.cpp::renderSvg()` 按契约 §1.6
  把 `currentColor` 换成 `color.name(QColor::HexArgb)` = `#ff1f2328`；**QSvgRenderer 不接受
  `#AARRGGBB`**，整条 path 渲染为空。镜像内独立实验 `target/svg_probe.cpp`：
  ```
  original(currentColor, unhandled): 195
  hexArgb #ff1f2328: 0
  hexRgb  #1f2328: 195
  rgba(   ): 0
  ```
- **修法**：T7 把着色改为 `HexRgb`，并把断言加强为「24px 下至少一个不透明像素」。
- **本轮复验**：修复进入构建后同一探针输出 `opaquePixels=117..240`（11/11 非零），
  截图可见图标（`screenshots/after-yunwu.png` / `after-yaoshi.png`）。

### 8.2 缺陷 2：右上角主题按钮不可见（A1）

- **运行期取证**：`mainToolBar_ = 1250x53 @ x=0`（窗口 1280），`vedit-theme-toolbar` 只分到
  `30x53 @ x=1250`；`themeButton` 的 `sizeHint=45x37` 但被 QToolBarLayout 撑到 `100x30`，
  于是被工具栏判定为溢出项并隐藏（`visible=0`）。
- **修法（`src/app/app_shell.cpp`）**：
  1. `mainToolBar_->setIconSize(QSize(20, 20))`（11 个按钮省约 44px）；
  2. `themeToolBar_->setMinimumWidth(56)`（保证按钮宽度不被主工具栏吃掉）；
  3. `themeButton_->setFixedSize(36, 36)` + `setIconSize(22,22)`（固定尺寸阻止 widget action 被拉伸）。
- **复验**：`themeButton global=1236,30 36x36 visible=1`，`themeImage opaquePixels=108`，
  截图中右侧金色「圆点 + 半环」主题按钮可见（`after-yaoshi.png`）。

### 8.3 缺陷 3：KTextEditor 状态行文字被裁（**已由 T8 修复并复验通过**）

- **运行期几何（原样保留）**：`card bottom=789`，应用状态栏 `top=798`（8px 画布间隙存在）；
  `KateViewBar/KateStatusBar = 346x18 @ y=771`（完全在卡片内，没有被卡片或状态栏压住）。
- **根因**：`cards.qss:283-289` 的全局 `QPushButton { padding: 6px 14px; }` 命中了
  KTextEditor 内部 **18px 高的状态按钮**（`QPushButton 18x18 ~ 58x18`）：
  `18 - 2×6 - 2×1(border) = 4px` 内容高度，13px 字体被压成"横向切一半"。
  pill 边框本身是完整的 18px，所以这不是几何遮挡，而是**样式表污染**。
- **归属**：`resources/themes/cards.qss` 属 theme-kernel，本车道未改 QSS；已上报船长并由 T8 修复
  （`KateStatusBar QPushButton / QToolButton` 等更具体选择器，同时自查了同类全局样式污染）。
- **复验（T8 落地后的最终构建）**：`after-yunwu.png` 中
  `1:1  INSERT  Soft Tabs: 4  UTF-8  Normal` 文字完整、不再横向裁切；
  放大图 `screenshots/crop-kte-statusbar.png`（原图 y=745..800，2× 最近邻）。
  产品探针仍是同一条几何：`KateStatusBar 346x18 @ y=771`、`card bottom=789`、`status top=798`，
  说明修复只改了按钮内边距，没有动几何。

### 8.4 缺陷 4：深色界面下语法主题错配（A3）

- **规则（REQUIREMENTS §6.3）**：显式语法主题（`--theme` CLI 或首选项写入的 `theme` 键）
  永远优先；从未显式选择时，产品主题切换/启动使用该产品主题的配对 `syntaxTheme`
  （`yunwu/xinghuang/bohe/qingkong → Breeze Light（xinghuang → Solarized Light）`、
  `yaoshi/shenye → Breeze Dark`）。
- **实现**：`src/app/app_shell.cpp` 新增 `applyStartupThemes(archiveSyntax, archiveUi)` 与
  `applyPairedSyntaxTheme()`；`restoreSession()` 与无存档启动走**同一个**决策函数。
  会话存档里"非默认"的语法主题仍被保留（保住既有 `vedit-app-shell-selftest` 的
  `restore.theme` 断言）；`settings.toml` 的 `uiTheme` 属于外部主题决策，会触发重新配对。
  首选项切换产品主题时同样按上述规则配对（`settings_panel.cpp`）。
- **实测**（真实产品进程）：
  ```
  yunwu  : theme=yunwu  syntax=Breeze Light status-theme=Theme: 云雾白 / Breeze Light
  yaoshi : theme=yaoshi syntax=Breeze Dark  status-theme=Theme: 曜石黑 / Breeze Dark
  ```
- **探针断言**（`target/theme_probe.cpp`，28/28 PASS）：
  `switch.paired-syntax-theme :: Breeze Dark`、
  `startup.settings-paired-syntax :: Breeze Light`、
  `switch.status-updated :: Theme: 曜石黑 / Breeze Dark`。
- **备选论证（若认为产品主题不该管语法主题）**：本轮**不成立**——`CONTRACT.md §1.3` 固定了每套
  产品主题的 `syntaxTheme`，且深色界面（`#131315`/`#0F1622`）配 `Breeze Light` 时，编辑器底
  仍是 Breeze Light 的浅底（KTextEditor 背景由语法主题决定），会出现"深色 chrome 包一块浅色
  编辑器"；改配 `Breeze Dark` 后编辑器底变成深色，与产品主题一致。可读性截图见 §8.5。

### 8.5 真实文件的语法配色可读性（yaoshi / shenye）

- 测试文件：`target/readability.sv`（module、端口、`//` 注释、字符串、数字、`assign`）。
- 截图：`evidence/ui-mosaic/chrome-ui/screenshots/syntax-yaoshi.png`
  （sha256 `261323232cc9c09d50857e6a04c6fa6d1721d2fb8d61f4ad28b87645ea10eb72`）、
  `syntax-shenye.png`
  （sha256 `f7a3e0607b9cd585203ce51f933fe46626f5e3ac0e429bce9ebc3fc15285b568`）。
- 抓图命令：`bash target/readability-shot.sh yaoshi` / `... shenye`（一次性脚本，位于 `target/`）。
- 目检结论（yaoshi 已实测，见 `screenshots/readability-yaoshi.png`）：编辑器底为深色
  （`#1D1D20` 级），`module/parameter/input/output/assign/string/endmodule` 为浅蓝/白，
  类型 `logic/int` 为青色，数字 `8` / `8'hA5` / `1'b1` 为橙色，字符串为橙红色，
  `//` 与 `/* */` 注释为中灰；全部为浅色记号压在深底上，**没有出现浅底配色的浅色记号**。
  状态栏第二个字段为 `Breeze Dark`。shenye 同规则（同一份 Breeze Dark 语法主题）。

### 8.6 运行期探针 `VEDIT_UI_PROBE`

- 位置：`ApplicationShell::logUiProbe()`（`src/app/app_shell.cpp`），由 `buildToolBar()` 在
  `VEDIT_UI_PROBE=1` 时以 2.5s 延迟调用；**只在环境变量开启时打印，不改任何状态**。
- 输出：每个工具栏 action 的 `iconNull/opaquePixels`、主题按钮的全局几何/可见性/不透明像素、
  两条工具栏的几何、central/card/editor/status 的几何，以及编辑器底部
  `KateViewBar / KateStatusBar / 内部按钮`的几何。
- 用法：`VEDIT_UI_PROBE=1 vedit-app`（容器内 `DISPLAY=:99 QT_QPA_PLATFORM=xcb VEDIT_UI_PROBE=1 ...`）。
- 保留理由：`QIcon::isNull()` 对"渲染为空的全透明图标"无感；这类缺陷只有数像素/量几何能观测到。


---

## 9. T6 最终验证（T8 落地后的全量构建）

- 构建：`bash scripts/build-qt.sh` → exit 0（日志 `logs/14-t6-post-t8-build-qt.log`）：
  ```
  100% tests passed, 0 tests failed out of 62
  Total Test time (real) =  86.19 sec
  M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0 tree_sitter=0.25.9 window=1 editor=1 document=1
  [qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke
  POST_T8_BUILD_EXIT=0
  ```
- 二进制：
  ```
  -rwxr-xr-x 1 haroldzhao haroldzhao 136526272 2026-11-08 21:52:04.412840500 +0800 target/qt6-build/src/app/vedit-app
  sha256=7c3dd1c4829699c981677017996252fc8b5e071d21943e589d98f55066a162ff
  ```
- 六套主题抓图命令（原文）：
  `bash evidence/ui-mosaic/screenshots/capture-themes.sh yunwu yaoshi shenye bohe qingkong xinghuang`
  产物 sha256（全部不同于旧基线 `6ae59c43ea3b…`）：
  ```
  557c8ed9a6bd9e338d922f92464213980ff57571129a549eeb4fd431cf431fa0  after-yunwu.png
  fcaa26647084cf62ca24659769a734310c7ccad27ad5d6c59c971011a0fb191e  after-yaoshi.png
  0e0fdcc79009dbee8bea243c26bd9a2a0b08b8434244f7aac8999c7db11f6f71  after-shenye.png
  9ac56cbb49e37e57a5282c0aa4ea64ad74ff759a383459bffef81c219bc1e589  after-bohe.png
  5c03c39e41a909fcbe6b6dcbb709694ee8ce9459e0851abe4e24d7a6299eb3a2  after-qingkong.png
  dbb056e65bbff303bb2407b09329bfd3187884e7468b38a4f6838f74136369c2  after-xinghuang.png
  ```
- 翻译门禁：`bash build/gates/lrelease.sh` → exit 0（日志 `logs/21-t6-lrelease.log`）：
  ```
  Generated 1267 translation(s) (1267 finished and 0 unfinished)
  [lrelease] counts: 1267 translation(s) (1267 finished, 0 unfinished, 0 untranslated)
  [lrelease] PASS: 0 unfinished and 0 untranslated
  ```
- 接线探针（一次性，`target/`）：theme probe 28/28 PASS、settings probe 7/7 PASS
  （日志 `logs/30-chrome-probes.log`；本轮修复后的运行期日志 `logs/40-runtime-*.log`、`logs/41-readability-*.log`）。
