# evidence/ui-mosaic — 卡片式 UI 重做 + 全工程程序图标

需求与契约：`.mpd/plans/ui-mosaic/REQUIREMENTS.md`、`.mpd/plans/ui-mosaic/CONTRACT.md`。
参考素材（输入，非交付物）：`.mpd/recon/ui-ref/1..7.png`（Mosaic 六套配色）、`.mpd/recon/icon-ref/1..3.png`（VEdit 图标三态）。

## 目录约定

| 路径 | 内容 | 产出方 |
| --- | --- | --- |
| `screenshots/` | 改造前后真实运行截图（Xvfb + xcb 抓屏） | 船长 |
| `icon-assets/REPORT.md` | 图标资产流水线、多尺寸清单、打包改动 | icon-assets |
| `theme-kernel/REPORT.md` | 产品主题表、QSS、ThemeManager、单测 | theme-kernel |
| `chrome-ui/REPORT.md` | 主题菜单/工具栏/卡片外观/首选项/语言包 | chrome-ui |
| `review/VERDICT.md` | 独立验证结论（A1–A7） | reviewer |

## 基线（改造前）

- `screenshots/before.png` — 1280×820 窗口，纯文字工具栏、QDockWidget 直边三栏（Files / 编辑器 / Module Tree+Interface+Outline），状态栏显示 `Theme: Breeze Light`。
- 基线构建：`bash scripts/build-qt.sh --no-test` → exit 0（2026-11-08）。
- 抓屏方法：容器内 `Xvfb :99` 跑 `vedit-app`（`QT_QPA_PLATFORM=xcb`），宿主 Pillow 通过工作区共享的 X socket 抓屏；容器内没有 import/scrot/xwd。

## 改造成果

### 图标（T1，已完成）

- `scripts/build-app-icons.py`：四角泛洪透明化 + 不透明 bbox 裁切 + 2% 边距 → 14 个 PNG，幂等（两次运行 sha256 全等）。
- `resources/icons/src/` 存原图逐字节副本；`resources/icons/app/` 三个 512 主图；`resources/icons/hicolor/{16..512}/apps/` 八档；`resources/icons/actions/` 15 个 `currentColor` 线性图标。
- 浅色变体与外部浅灰只差 4–8 灰度，独立泛洪会吃掉图案，故采用**共享轮廓**（三变体同一几何、浅色像素原样保留）——见 `icon-assets/REPORT.md §3`。
- 打包：`vedit.desktop` 增 `StartupWMClass`；`assemble-appdir.sh` 安装 hicolor 八档 + 根图标/`.DirIcon` 改用新 256 light，并新增图标链交付不变量（9 个负例实测全部失败）；`verify-f2-isolation.sh` 增 `check_icon_chain`。
- 船长集成：旧 GTK3 打包线 `packaging/appimage/assets/io.github.vedit.VEdit.png` 同步替换为新图标（256×256 light，sha256 `3e7efb5b…`），保证两条打包线同源。

### 主题与界面（T2 / T3 / T5 / T6 / T7 / T8，已完成）

- 六套产品主题：云雾白 / 暖杏黄 / 薄荷绿 / 晴空蓝 / 曜石黑 / 深夜蓝；chrome 由 `product_theme` + `cards.qss`（令牌模板）驱动，编辑器语法配色仍由 KSyntaxHighlighting 决定，深色两套配对 `Breeze Dark`。
- `screenshots/palette-swatch.png` — 六套色板设计自检（与参考图观感一致）。
- `screenshots/capture-themes.sh` — 抓图 harness（Xvfb + xcb + 宿主 Pillow；主题经 `settings.toml` 的 `uiTheme` 选择）。
- `screenshots/after-{yunwu,xinghuang,bohe,qingkong,yaoshi,shenye}.png` — 六套主题的真实运行截图；画布角像素实测与契约钉死值逐一吻合。
- `chrome-ui/screenshots/syntax-{yaoshi,shenye}.png` — 深色主题 + 真实 SystemVerilog 文件的可读性证据。
- `chrome-ui/screenshots/crop-kte-statusbar.png` — KTextEditor 状态行修复后的像素级确认。

#### 修复过程中发现的两个"测试抓不到"的缺陷（可复用教训）

1. **`QSvgRenderer` 不接受 `#AARRGGBB`**：`QColor::HexArgb` 产出 `#ff1f2328` 时整条 path 渲染为空 → 图标全部**全透明**，而 `!pixmap.isNull()` 这类断言永远绿。着色必须用 `HexRgb`，断言必须检查「至少一个不透明像素」。证据 `theme-kernel/themed-icons-probe.log`。
2. **全局 QSS 按钮规则会污染第三方嵌入控件**：`QPushButton { padding: 6px 14px }` 命中 KTextEditor 内部 18px 高的状态按钮，`18-2×6-2×1=4px` 内容高把 13px 字体压成横切。修法是在第三方类名容器（`KateStatusBar` 等）内复位，而不是只收窄自家控件。证据 `theme-kernel/qss-pollution-before-after.log`。

### 修复波次（独立验证打回后）

第一轮独立验证给出 `request changes`（`review/VERDICT.md`），两条 High 与若干 Medium 已修：

| 发现 | 修法 | 证据 |
|---|---|---|
| **F1** 主题持久化在用过首选项后失效 | 三个入口同写同一个 `uiTheme` 设置键（经 `ApplicationShell::setUiThemePersister` 接缝，由 `main.cpp` 装真 `SettingsClient`，同一键同一原子写）+ 同步会话存档；`VEDIT_UI_PROBE`/`VEDIT_UI_THEME_SWITCH` 测试缝（默认关）驱动真实 QAction 做过回归 | `fix-r1/REPORT.md`，14 项 + 30 项断言 |
| **A1 补充**「最后写入者生效」未实现（固定优先级、无 mtime 仲裁） | 按文件 mtime 仲裁，mtime 相同以 settings 为准 | `fix-r3/REPORT.md` |
| **F2** 语法主题切换零视觉效果 | 经 `EditorProvider::applySyntaxTheme` seam 接到 `KTextEditor::View::setConfigValue("theme", …)`（Researcher 证明这是唯一公开路径；`Repository::setDefaultTheme` 在 6.24 不存在，且 KTextEditor 用 `KateHlManager` 自己的仓库） | `fix-f2/REPORT.md`：变化 **226,018 px**、bbox `(587,113,1178,817)`、**编辑器文本区占 99.4%**、正文行带 113–769 与状态栏行带 804–816 不重叠、背景亮度 240.7→47.5；新建 view 自动继承；无效名回退全局主题 |
| **M1** 主题名硬编码中文 | 改为可翻译，英文源文本 `Cloud White / Warm Apricot / Mint Green / Sky Blue / Obsidian / Deep Navy`，中文译文手补 | `fix-r1/REPORT.md`（en/zh 双语断言 30 项） |
| **M2** 无效 `uiTheme` 吞掉有效 archive 主题 | 无效视为未设置，继续 archive > 默认 回退，并给可见诊断 | 同上 |
| **M3** 图标链只复检 4/8 档 | `check_icon_chain` 断言全部 8 档 hicolor（正/负控制） | 同上 |
| **M4** `main.cpp` 硬编码图标路径 | 改调 `themed_icons::appIcon(false)`，消除双来源 | 同上 |

已知环境限制：抓图镜像内无 CJK 字体，中文界面下状态栏主题名会显示为方框（AppDir 才装 NotoSansCJK）。**交付用的六张截图已用最终二进制（含 R1/R3/F2）重拍**，英文界面下状态栏实际显示 `Theme: Cloud White / Breeze Light`、`Theme: Obsidian / Breeze Dark`，不再是方框；中文文案正确性由断言证明，不靠截图。既有 flake `vedit-rename-e2e (SIGPIPE)` 与历史同款（`evidence/ux-batch-2/u8-final/ctest-integration.log:48`），单独复跑通过、全量复跑 62/62。

### 复验与证据备注

- 最终判定：`review-r1/VERDICT-R1.reviewer3.md`（**canonical 副本**，verdict = approve，风险 Medium，无 Blocker/High）。`review-r1/VERDICT-R1.md` 可能在复验期间由**并行实例**写入，两者内容重叠；以 `.reviewer3.md` 为准。
- 复验期间观察到同一 `reviewer-3` 成员的两个并行实例同时写 `review-r1/`（22:57–23:28），因此该目录存在重复产物；受污染时段的运行已在判定中作废并重跑。
- `review-r1/logs/{final-binary,r1-binary,r2-binary}` 是 3 份 131MB 的产品二进制副本，**已删除**以免污染仓库；其 sha256 记录在 `review-r1/logs/BINARIES-REMOVED.sha256`。

### 门禁

- `build/gates/lrelease.sh` → **PASS**，`1267 finished, 0 unfinished, 0 untranslated`
- `build/gates/docs.sh` → **PASS**
- `bash scripts/build-qt.sh` → **exit 0**，`100% tests passed, 0 tests failed out of 62`，xcb smoke `M0-SMOKE ok=1`
- 图标链路：船长独立复跑 `icon-assets/icon-chain-sim.sh` → 正向 10/10 通过；负向删除 `64x64` 时**正确报红**（`missing or empty` 并被 `check_icon_chain` 捕获），汇总 `ICON-CHAIN SIM PASS`。
