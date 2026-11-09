# UI-Mosaic · T4 独立验证结论（VERDICT）

- 判据：`.mpd/plans/ui-mosaic/REQUIREMENTS.md`（A1–A7 + §2 范围决策）、`.mpd/plans/ui-mosaic/CONTRACT.md`（冻结版，含 2026-11-08 21:26 的 HexRgb 修正）
- 验证者：reviewer-3（只写 `evidence/ui-mosaic/review/**`；本目录外的产品代码、测试、门禁、翻译、资产一律未改）
- 复现：本目录 `logs/` 下的脚本与原始日志；所有命令都在仓库根执行，构建/运行在 pinned 镜像 `vedit-qt6-build:local`

## 评估段（风险级：High）

T1–T8 的交付在"可构建、可启动、视觉成形、图标全链路、门禁全绿"这几条上基本成立：我独立重跑 `bash scripts/build-qt.sh` 得到 exit 0、全量 ctest 62/62（含 `vedit-app-ux-selftest`、`vedit-app-shell-selftest`、`vedit-app-ui-theme-selftest`、`vedit-fold-gutter-click`）与 xcb 冒烟 `M0-SMOKE ok=1`；`build/gates/lrelease.sh` = 1267 finished / 0 unfinished / 0 untranslated；`build/gates/docs.sh` PASS；六套主题逐一起真实二进制离屏启动，产品主题与配对语法主题全部正确；图标 14 个 PNG 的 sha256 与脚本日志逐条一致、角 alpha=0/中心不透明、qrc 与磁盘 15 个 SVG 一一对应、打包不变量 5 个负例全部报红。但有两处用户可见的高风险缺陷：**A1 的"重启后保持"在首选项写过 `uiTheme` 之后失效**（视图菜单/工具栏切换只落会话存档，而启动优先旧的 `settings.toml`，且首选项重载会把主题弹回旧值）；**语法主题切换在产品主题接管后变成无效果控件**（真实截图逐像素比较只变了状态栏文字，编辑器/面板 0 像素变化，而需求与新版用户指南都声称编辑器配色由它决定）。因此不能 approve；其余为契约/证据一致性的 Medium/Nit。

**Gate 原始输出（节选）**

```
[lrelease] counts: 1267 translation(s) (1267 finished, 0 unfinished, 0 untranslated)
[lrelease] PASS: 0 unfinished and 0 untranslated
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  85.35 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ... window=1 editor=1
[qt6] PASS: Rust service layer + configure + build + ctest ... inside vedit-qt6-build:local
BUILD_EXIT=0
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
```

六主题离屏启动（`--hold` + `VEDIT_UI_PROBE=1`，主题经 `settings.toml` 选择；`--theme-ui` 按契约 §2.1 不存在）：

```
[ui-probe] theme=yunwu     syntax=Breeze Light    status-theme=Theme: 云雾白 / Breeze Light  style=2 toolbarIconSize=20x20
[ui-probe] theme=xinghuang syntax=Solarized Light status-theme=Theme: 暖杏黄 / Solarized Light style=2 toolbarIconSize=20x20
[ui-probe] theme=bohe      syntax=Breeze Light    status-theme=Theme: 薄荷绿 / Breeze Light  style=2 toolbarIconSize=20x20
[ui-probe] theme=qingkong  syntax=Breeze Light    status-theme=Theme: 晴空蓝 / Breeze Light  style=2 toolbarIconSize=20x20
[ui-probe] theme=yaoshi    syntax=Breeze Dark     status-theme=Theme: 曜石黑 / Breeze Dark   style=2 toolbarIconSize=20x20
[ui-probe] theme=shenye    syntax=Breeze Dark     status-theme=Theme: 深夜蓝 / Breeze Dark   style=2 toolbarIconSize=20x20
```

## 发现（按严重度）

### F1 — High — A1 的"重启后保持"在首选项写过 uiTheme 之后失效

- **What**：`src/app/app_shell.cpp` 的 `ApplicationShell::applyProductUiTheme`（视图「主题」子菜单与工具栏主题按钮的唯一应用点）只调用 `ThemeManager::applyUiTheme`，**不写 `settings.toml`**；主题的持久化只发生在 `ApplicationShell::saveSession`（退出时写入 `session.json`）。而 `ApplicationShell::applyStartupThemes` 的优先级是 `settings.toml uiTheme` > 存档 `uiTheme` > 默认；`src/workspace/settings/settings_panel.cpp` 的 `SettingsPanel::applyThemeFromEntries`（`reload()` 时调用）还会在打开首选项时重新应用 `settings.toml` 里的旧值。
- **Why it matters**：用户在首选项里选过一次界面主题（写 `settings.toml`），之后从视图菜单/工具栏切换到别的主题，重启会被旧 `settings.toml` 值覆盖；打开首选项时当前主题还会回弹到旧值。这正是 A1 的验收句"切换后……重启后保持（持久化 `uiTheme`）"。`docs/USER_GUIDE.md` §8.2 声称三个入口"效果一致……并写进设置的 `uiTheme` 键"，与实现不符。
- **Evidence**（`logs/03-precedence.log`、`logs/05-precedence-2.log`，真实二进制）：
  - `settings.uiTheme=bohe` + 存档 `uiTheme=shenye` → 启动 `theme=bohe`（settings 胜出），退出后存档被改写成 `bohe`；
  - 无 `settings.toml`、存档 `shenye` → `theme=shenye`（说明存档本身是有效的持久化）；
  - 代码侧：`grep` 全仓，`uiTheme` 的 settings 写入只有 `settings_panel.cpp` 的 `SettingsClient::setValue(&entries_, kUiThemeKey, id)`，`app_shell.cpp` 无任何 settings 写入。
- **Direction**：让菜单/工具栏切换把 id 持久化到同一个 `uiTheme` settings 键（复用 settings client 或在 shell 暴露一次性写回调），或在首选项重载时以当前运行主题为准；两种改法都要与契约冻结的 settings > archive 优先级自洽。

### F2 — High — 语法主题切换在产品主题接管后没有任何视觉效果

- **What**：`src/app/theme_manager.cpp` 的 `ThemeManager::applyTheme` 在 `uiThemeActive_` 为 true 时不再 `QApplication::setPalette`（这是 CONTRACT §1.5 的要求），但全仓没有任何代码把 `ThemeManager::currentTheme()` / `syntaxThemeName()` 交给编辑器或 KTextEditor（`src/editor/**` 无 `ThemeManager` 消费者，唯一使用点是状态栏文案、存档与 selftest）。
- **Why it matters**：REQUIREMENTS §2 的范围决策写明"现有 30 个 KSyntaxHighlighting 语法主题继续存在（**编辑器语法配色仍由它决定**）"；本批新增的 `docs/USER_GUIDE.md` §8.2 又说语法主题"只决定编辑器内部的配色：背景、文字、选中、禁用态与各类语法记号"。实测两句都不成立：首选项里的 Theme 下拉现在只改状态栏文字，相对改造前（`applyTheme` 会重刷 palette）是用户可见回归。
- **Evidence**（`logs/06-syntax-theme-effect.log`、`logs/screens/`）：同一产品主题 `yunwu`，两次真实 Xvfb+xcb 抓图——默认配对 `Breeze Light` vs `--theme "Breeze Dark"`；整帧逐像素 diff 的 bbox = `(613, 804, 771, 817)`，changed pixels (>8) = **1090**，且只落在状态栏标签那一行（changed row bands `[(804, 816)]`）；工具栏、菜单、左右面板、编辑区 **0 像素变化**。`yunwu-plain.png` 与交付的 `evidence/ui-mosaic/screenshots/after-yunwu.png` 字节相同（sha256 `557c8ed9…`），说明抓图链路可信；语法主题名确实生效在 `ThemeManager` 状态里（状态栏显示 `Breeze Dark`），只是没有渲染消费者。
- **Direction**：把选中的语法主题真正接到 KTextEditor 渲染层（编辑器 lane 的 schema/theme seam），并用"打开一个真实文件后编辑器背景/记号颜色改变"的像素/颜色断言锁住；或回船长重新冻结，明确 descope 并同步删除/改写用户指南与控制项。**本项需要契约层决策，不是写入方私自能收口的。**

### F3 — Medium — CONTRACT §2.3 的 `panel_registry.cpp` QFrame 未实现（船长裁决用 QSS 承担，但契约未重新冻结）

- **What**：`src/app/panel_registry.cpp` 未被修改；CONTRACT §2.3 仍要求"dock 内容外层套 `QFrame`，`objectName = "veditCard"`"。T3 报告 §9 记录"船长已批准"改由 `cards.qss` 在 `QDockWidget`/`QDockWidget::title` 上画卡片，理由是 `src/app/ux_selftest.cpp` 的既有断言通过 `dock->widget()` 直接取面板内容，包 QFrame 会打红——即冻结契约 §0.5（既有测试必须继续通过）与 §2.3 自相矛盾。
- **Why it matters**：A2 的用户可见效果由 QSS + `ui_theme_selftest` 的 4 条 dock 像素断言承担，我的抓图也确认卡片/圆角/间隙成立，所以**不是用户缺陷**；但冻结文档与实现不一致（HexRgb 那次已按"修正"重新冻结，这次没有），后续维护/复验会按 §2.3 去找一个不存在的 QFrame。
- **Direction**：在 `CONTRACT.md` 记录 §2.3 的裁决修正（与 HexRgb 同样标注日期与理由），让冻结文档与实现一致；不要通过改既有测试来迁就实现。

### F4 — Medium — A7 截图里主题中文名是豆腐块；英文 UI 下主题名硬编码中文

- **What**：交付截图 `evidence/ui-mosaic/screenshots/after-*.png` 与 `crop-statusbar.png` 的状态栏显示 `Theme: ▯▯▯ / Breeze Light`；抓图镜像里 `fc-list | grep -ci cjk` = **0**（AppDir 才安装 `NotoSansCJK-Regular.ttc`）。六个 `displayName` 是 `src/app/product_theme.cpp` 里的硬编码中文（CONTRACT §2.1 要求"勾选项显示中文名"），不经过 `tr()`，因此英文 UI 下也显示中文。
- **Why it matters**：A7 的截图本是 A3"所有自绘文本可读"的视觉证据，却展示了不可读的主题名；在"英文 UI + 无 CJK 字体"的机器上（交付 AppDir 自带字体，故交付路径本身没问题），唯一的主题名不可读。属于证据质量 + i18n 一致性缺陷。
- **Direction**：抓图环境安装与交付一致的 NotoSansCJK（或设 `LANG=zh_CN`）后重拍 A7 截图；并考虑把六个名字做成英文源串 + zh_CN 译文，使中英 UI 都正确（这需要把契约的"中文名"改成"中文译文"，属契约层小修正）。

### F5 — Nit — `main.cpp` 未使用契约指定的 `themed_icons::appIcon(false)`

- **What**：`src/app/main.cpp` 现在是 `QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app/vedit-light.png")))`，而 CONTRACT §3.2 写的是 `themed_icons::appIcon(false)`。
- **Why it matters**：功能等价（同一资源、同一 512 PNG，启动后立刻被 `applyUiTheme` 覆盖），但同一图标有了第二个来源，将来改 `appIcon()` 的降级/DPR 行为时 `main.cpp` 会漏。
- **Direction**：调 helper（`main.cpp` 加一行 include），或把契约这句话改成"直接读资源"并说明理由。

### F6 — Nit — 无效的 settings `uiTheme` 会丢掉有效的存档主题

- **What**：`settings.toml` 里 `uiTheme="no-such-theme"` + 有效存档 `shenye` → 启动落到 `yunwu`（`logs/05-precedence-2.log` 实测），`applyProductUiTheme` 失败回退到默认而不尝试存档。
- **Why it matters**：手改坏的/旧版本残留的值会让用户上一次的会话主题失效；启动路径 `reportFailure=false`，只有 `addDiagnostic`，用户看不到原因。
- **Direction**：失败回退顺序改为 archive → default，或至少在诊断里说明"回退到默认"。

### F7 — Nit — `verify-f2-isolation.sh` 只复检 4/8 个 hicolor 尺寸

- **What**：`packaging/runtime/verify-f2-isolation.sh` 的 `check_icon_chain` 检查 16/64/256/512 四档；`assemble-appdir.sh` 安装并校验 16/24/32/48/64/128/256/512 全部八档。
- **Why it matters**：交付树里 24/32/48/128 缺失不会被 shipped-tree 检查发现（build-time 会拦住，属防御纵深缺口）。
- **Direction**：两处共用同一个尺寸列表（或循环 8 档）。

### F8 — Nit — `SessionState::uiTheme` 不参与 `SessionState::toJson()`

- **What**：字段已按契约加进 `src/app/session_store.h`，但序列化由 `ApplicationShell::saveSession` 在 `state.toJson()` 之后手动 `insert("uiTheme")`（`session_store.cpp` 未改是船长裁决）。
- **Why it matters**：当前只有一条保存路径，行为正确；风险是未来出现第二个 `toJson()` 调用者时静默丢键。
- **Direction**：在 `session_store.cpp` 补序列化（需扩写入范围）或在该字段/函数处加注释与断言，防止第二个消费者。

## Declined to judge（看过但明确不判）

- **`@textMuted@` 在四套浅色主题上的对比度**：我独立算得 2.74–2.75:1（用于状态栏与次要文本）。A3 只给出"正文 ≥ 4.5、禁用态 ≥ 3"两个下限，且 CONTRACT §1.3 钉死 55% 派生规则；不算缺陷，仅记录供后续 a11y 收紧。
- **`accentText` 落在 `accent` 上的对比度**：xinghuang 3.34、bohe 3.50、qingkong 3.79（白字在强调色上）。颜色是契约 §1.3 的采样钉死值，不属于本轮实现自由度，不判缺陷。
- **运行期 `No such signal ...::directoryOpened(QString)` 告警**：改造前的 `evidence/batch1-shell/` 日志里已存在，非本批引入。
- **`docs/USER_GUIDE.md` 与 `packaging/appimage/assets/io.github.vedit.VEdit.png` 的改动在三个车道写入范围之外**：`evidence/ui-mosaic/README.md` 标注为船长集成，内容与功能一致，不按车道缺陷记；请船长确认提交归属。
- **16–48px light 图标在白底上对比度低**：`icon-assets/REPORT.md` §9 已声明，CONTRACT §3.1 指定 hicolor 用 light 变体，按契约执行。
- **v1 GTK3 打包线（`packaging/appimage/`）的图标链**：不在 CONTRACT §3 范围，未判。
- **hover/pressed 的真实鼠标交互**：QSS 规则存在（`QToolButton:hover/pressed`、`QPushButton:hover/pressed`、`QTabBar::tab:hover` 等），无 WM 环境下未做真实指针测试。
- **30 个 KSyntaxHighlighting 主题目录本身**：只验证"选中后有/无效果"，未审主题内容。

## 审阅边界（没读什么 / 跑不了什么）

- **读**：REQUIREMENTS、CONTRACT、三份车道 REPORT 与 `evidence/ui-mosaic/README.md`，以及实现文件 `product_theme.*`、`themed_icons.*`、`theme_manager.*`、`app_shell.*`（相关函数）、`editor_area.cpp`（标签样式）、`settings_panel.cpp`（主题/持久化路径）、`resources/themes/cards.qss`、`scripts/build-app-icons.py`、`packaging/runtime/*` 的 diff、`main.cpp` 的 diff。注：CONTRACT §4 的 DoD 还写着"Reviewer 不读实现"，与本次 T4 任务书（要求逐条核对实现）冲突——我按任务书执行，§4 这句话建议船长一并修正。
- **跑**：独立全量 `bash scripts/build-qt.sh`（62/62 + xcb 冒烟）、`build/gates/lrelease.sh`、`build/gates/docs.sh`、六主题离屏启动、启动优先级 4 组真实运行、`assemble-appdir.sh` 的图标不变量块（从真实脚本逐字抽出）正例 + 5 个负例、14 个 PNG 的 sha256 与 5 个尺寸的 alpha 复核、Xvfb+xcb 真实抓图与逐像素 diff。
- **跑不了/没跑**：完整 `assemble-appdir.sh` 与 `verify-f2-isolation.sh`（需要完整 AppDir 载荷与探针容器，本批未产出新 AppImage/AppDir，故 A4 的"AppDir 根图标/.DirIcon/hicolor"以脚本 + 逐字不变量负例证明，未以真实打包产物证明）；真实桌面/窗口管理器下的任务栏、桌面条目分组与窗口图标观感（Xvfb 无 WM）；菜单/工具栏的鼠标交互切换（无 GUI 自动化）；六套主题的独立全量抓图（我独立抓了 yunwu 与 yunwu+Breeze Dark 两张，交付的六张只直接看了 yunwu/yaoshi 两张，其余以启动探针与 `palette-swatch.png` 佐证）。
- **`--theme-ui`**：契约 §2.1 明确"本轮不新增 CLI 选项"，二进制没有该参数；我按真实启动路径用 `settings.toml` 的 `uiTheme` 选择主题，并用 `ShellOptions::uiTheme` 接缝的行为由代码审阅确认。
- **窗口图标随深浅切换**：代码路径（`applyUiTheme` → `setWindowIcon(appIcon(dark))`）与单测（三个图标非空、不透明像素 > 0）成立；无 WM 环境无法观察任务栏实际图标。

## 结论

**Verdict：request changes。**
一句话理由：A1 的"重启后保持"在首选项写过 `uiTheme` 后被旧 settings 值覆盖、语法主题切换在真实运行里只剩状态栏文字变化（与需求 §2 及新版用户指南直接矛盾），这两项必须先修/先回船长重新冻结，其余 A2–A7 的构造与门禁证据是绿的。
