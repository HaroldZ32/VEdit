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
- 船长最终自查（提交后、最终二进制）：全量 ctest 连跑两次均 **62/62**；更早一次为 61/62，唯一失败是 **既有 flake** `vedit-rename-e2e (SIGPIPE)`——单独复跑两次均 Passed（1.19s / 1.18s），且历史同款见 `evidence/ux-batch-2/u8-final/ctest-integration.log:48`，与本批改动无关（该用例不在改动集内）。
- 图标链路：船长独立复跑 `icon-assets/icon-chain-sim.sh` → 正向 10/10 通过；负向删除 `64x64` 时**正确报红**（`missing or empty` 并被 `check_icon_chain` 捕获），汇总 `ICON-CHAIN SIM PASS`。

### AppImage 端到端打包（2026-11-09 补充，关闭此前的 A4 端到端缺口）

`bash packaging/runtime/build.sh` → **exit 0，`BUILD: F2 runtime OK`，`steps_failed=0`**：

- AppDir 359 文件；AppImage `138,758,648` 字节，sha256 `aef125095dac5985cb4ac0e8610c4c848a6377948ac78d467d064dc09d6f78c8`（与 `out/SHA256SUMS` 一致）。
- 包内 `F2-ISOLATION PASS`：八档 hicolor（16/24/32/48/64/128/256/512）、`.DirIcon`、根 `io.github.vedit.VEdit.png`、`vedit.desktop` 的 `Icon=`/`StartupWMClass=` **全部 PASS**，含反向控制（宿主泄漏 406 定义、shadow 用例）。
- CentOS 7 机制探针 PASS：包内 `git 2.53.0` / `svn 1.14.5` 来自 bundled 工具链；`APP-SHELL-SELFTEST ok=1 checks=321 failures=0`。
- 包内 UI 实拍：`screenshots/appimage-verify.png`（AppImage 以 `APPIMAGE_EXTRACT_AND_RUN=1` 在 Xvfb 内运行）——工具栏图标、右上角主题按钮、卡片外观、状态栏 `Theme: Cloud White / Breeze Light` 均在交付物中生效。

### 视觉修复（用户实机反馈后，V1→V7）

用户反馈："没有框边阴影、卡片颜色与背景颜色相同、圆角按钮全部缺框线勾勒"。量化定位与修复：

| 现象 | 根因（实测） | 修复 |
|---|---|---|
| 卡片与背景同色 | 面板主体像素 = `#F4F4F2`（画布色），不是卡片色；未显式上色的子控件继承调色板 Window=canvas | QSS 在卡片子树铺 `surface`（渲染等价，A 判据六套 Δ=0） |
| 无描边 | 沿卡边扫 40px 无任何色差；`border` 派生与 surface 只差 5 | `border`/`borderStrong` 改为 surface→text 混合 12%/22%（Δ 24–26） |
| 无阴影 | 无 effect | `QGraphicsDropShadowEffect`（blur 12/14、offset 0,1、浅色 α22/深色 α220）+ 纵向分隔条 8px→16px 让出画布带 |
| 按钮无轮廓 | 只有字形像素 | `QPushButton`/`QToolButton` 用 `borderStrong` 描边，保留圆角 |
| 深色两卡无阴影 | dock 在主题应用**之前**创建，`owner->palette()` 走浅色 α22 分支，在 `#131315` 上 Δ≤1.6 | 阴影改为按 `QApplication::palette()` 计算，并在 `ApplicationPaletteChange`/`PaletteChange` 时重算 |

最终验收（船长自测，六套主题，`screenshots/after-*.png`）：外圈 8px 卡片像素 **0**；纵向连续画布 **10px**、横向 **16–18px**；卡片下/左/右阴影 **3–5px**；描边 Δ **24–26**；按钮轮廓 Δ **48–55**（均圆角）。顶部边阴影被子控件重绘覆盖属 Qt 绘制顺序，已按 `CONTRACT.md §5` 豁免并留痕。
构建 `bash scripts/build-qt.sh` exit 0，全量 ctest **62/62**。`vedit-app-ui-theme-selftest` 的 `dock-separator-gap` 期望值随冻结常量 8→16 同步（仅此一处，见 §5「间隙语义」）。

### 重新打包（视觉修复后）

`bash packaging/runtime/build.sh` → `steps_failed=0`，`BUILD: F2 runtime OK`，`F2-ISOLATION PASS`，CentOS 7 探针 PASS。

- AppImage `138,795,512` 字节，sha256 `3cfd24f06dea154990d6ed08da7932231760923737e8286085f451766844dadb`（与 `out/SHA256SUMS` 一致）。
- 包内实跑截图 `screenshots/appimage-verify.png`：卡片白底 + 可见描边 + 阴影 + 画布间隙 + 按钮轮廓，状态栏 `Theme: Cloud White / Breeze Light` —— 证明修复已进入交付物。
- 包内 `F1/T2` 复测（同一口径）：外圈 8px 卡片像素 **0**、纵向连续画布 **10px**。

### 视觉修复二轮（V8/V9，用户第二轮反馈）

用户反馈："工程窗口阴影除标题外无渲染；文件、大纲窗口左下右下角变成直角；暗色模式别阴影直接描边；文件树重命名输入框文字只显示一半"。

| 反馈 | 根因 | 修复 | 实测 |
|---|---|---|---|
| 工程窗口只有标题有阴影 | `Project` dock 在 `project_integration.cpp` 里**直接 `addDockWidget`**，绕过 `PanelRegistry`，没有 `veditDockCard` 属性/边距/阴影 | 抽出 `PanelRegistry::applyCardChrome(QDockWidget*)`，`insert()` 与 `project_integration.cpp` 共用 | 左栏上半区 surface 占比 **0.645 → 0.913**；卡片体 `#FFFFFF`、左边 `#E4E5E5` 描边 |
| 左下/右下是直角 | 圆角只画在描边上，**子控件（树/表格）方形背景盖住弧线** | 卡片容器加 `padding: 2px` + 内层视图背景透明，让容器圆角底透出 | 左下角 12×12 非 surface 像素 **0 → 65**；右下 24；六套一致 |
| 暗色阴影太黑 | 深色也上了 α220 黑阴影 | 深色主题**不加阴影**（`setGraphicsEffect(nullptr)`），只用描边；深浅判定改读 `QApplication::palette()` | 深色外侧阴影 **0–1px**（无阴影带），描边 Δ24 且比表面亮 |
| 重命名输入框文字被裁一半 | 全局 `QLineEdit` 的 padding 命中文件树内联编辑器（行高 ~20px） | 追加 `QTreeView/QTableView/QListView/QAbstractItemView QLineEdit { padding: 0 4px; margin:0; min-height:0; border-radius:3px }` | 构建 62/62；实机待用户确认 |

最终六套量化（`screenshots/after-*.png`）：下/左/右阴影 4–5px（深色 0–1px 属"无阴影"）；纵向画布 10px（深色 16px）；横向 18–21px；角点弧线六套一致。
项目态证据：`screenshots/project-yunwu.png`（`--project tests/fixtures/verilog-project`）。

### 第三次打包（V8/V9 之后）

- AppImage `138,799,608` 字节，sha256 `eb2d8e1eef5ca197cb440ac090f3e6f5f27da4ec2c7f6cd3594058e19f6a8327`（与 `out/SHA256SUMS` 一致）。
- 包内实跑（深色 `yaoshi` + `--project tests/fixtures/verilog-project`）截图 `screenshots/appimage-project-yaoshi.png`：
  左栏上半区 surface 占比 **0.931**（Project 面板已是卡片）；卡片右缘外侧亮度恒为 canvas 19.2（**深色无阴影带**）；纵向连续画布 **16px**。

### 选中行近黑带修复（V10，用户第三轮反馈："浅色模式下点右键出来一坨黑"）

根因：把**强调色当大面积填充**。云雾白 `accent = #2A2A28`（近黑，本意只用于小元素），却被用作树/表的**选中行背景**，实测 x=200 纵向 `y=683..704` 是一条 22px 的 `#2A2A28` 带（ΔL\* 83）。

修复：`ProductTheme` 新增 `selection` = `surface` 向 `accent` 混合（浅色 14% / 深色 28%），新增 `@selection@` 令牌；`cards.qss` 把 `QTreeView/QListView/QTableView`（`selection-background-color` 与 `::item:selected`）、`QMenu::item:selected`、`QComboBox` 弹层选中行改为 `@selection@` 底 + `@text@` 字。按钮 / 下划线 / 文本选择等**小面积** accent 用法未动。

实测（XTest 真鼠标点选）：yunwu `#E1E1E1` vs `#FFFFFF`（ΔL\* 10.5、文字对比度 12.08）；yaoshi `#514222` vs `#1D1D20`（ΔL\* 17.9、对比度 7.95）；浅色右键菜单白底可读、hover `#E1E1E1`。
已知：其余四套浅色主题按同一 14% 公式派生，ΔL\* 约 5.7–6.3（可见但更淡）；若需要更强，调 `kSelectionBlendPercentLight`。

### 第四次打包（V10 选中色）与一次打包偏差（如实记录）

- **偏差原因**：`packaging/runtime/build.sh` 第 5 步 `appimagetool` 需要从 GitHub 下载 `runtime-x86_64`，本次连续两次返回 **HTTP 504**（宿主与容器内均复现，网络侧故障，非代码问题）。AppDir 组装与隔离检查（25/25 PASS）都正常。
- **处置**：从本地已发布的 AppImage 中提取 type2 runtime（**长度 944632 字节**，即 `hsqs` 的第二个命中；第一次命中 194183 是 runtime 内部的误报，按它截取会得到截断 runtime、打出来的包 `--appimage-extract` 段错误），用 `packaging/appimage/vendor/appimagetool-x86_64 --runtime-file` 手工打包。
- **本次产物**：`138,795,512` 字节，sha256 `b4c725f193b79b01cbce48b282d20ddd74f18da947f15d06bfc4c7f321a1b124`；`--appimage-version` 正常返回 type2 runtime 版本；包内实跑（Xvfb+xcb）截图 `screenshots/appimage-selection-yunwu.png`，选中行为 `#E1E1E1`（修复前 `#2A2A28` 近黑带）。
- **未跑**：脚本第 6/7 步（包内 selfcheck 与 AppImage 形态的 F2 隔离）——`verify-f2-isolation.sh` 不接受直接传 AppImage 路径（输出 `unknown: <path>`），本次未追到其正确调用形式。AppDir 内容与上一次全绿构建一致（仅二进制与 cards.qss 更新），图标链等检查基于 AppDir 路径，未受影响。

### 弹层加固（V11，用户反馈"文件对话框里点右键出一坨黑"）

用户实机：**只有 QFileDialog 里的右键菜单**整块纯黑（浅灰字几乎不可见），应用自身菜单正常；主题暖杏黄；X11 带合成器。
无头 Xvfb（无合成器）下**无法复现**（改动前后菜单都是白底），故按症状做**防御性加固**——纯黑 + 1px 浅色圆角边 + 浅字是"弹层被置为半透明但背景未被填充"的典型形态，而 QSS 给弹层设 `border-radius` 会让 Qt 启用半透明窗口。

`cards.qss` 末尾 +64 行：
- `QMenu`：**方角**（`border-radius: 0px`）+ 显式 `background-color: @surface@` + `color: @text@` + 1px `@border@`；`QMenu::item`/`::item:selected`/`::separator`/`::icon` 配套；
- `QToolTip`、`QComboBox QAbstractItemView` 同样显式底色、不做半透明；
- `QFileDialog` 内部 `QTreeView/QListView/QComboBox/QLineEdit/QLabel` 底色保底（`QLineEdit` 只补底色，不动 padding）。

验证：构建 exit 0 + ctest 62/62；`QT_STYLE_OVERRIDE=fusion|windows` 两风格下，文件对话框右键菜单与 View 菜单**纯黑像素均为 0**、主导色 = surface；包内（暖杏黄 + windows 风格）截图 `screenshots/pkg-ctxmenu-xinghuang.png` 菜单 `#FFFDF6` 可读、方角、无纯黑。
**未证明**：实机合成器路径、QToolTip 与下拉列表实拍——无头环境缺这些条件，需用户实机复核。

### 原生对话框排查与菜单诊断（V12）

用户反馈：新包（`5f433086`）里文件的对话框右键菜单**照样纯黑**，包层 QSS 加固无效。

排查发现：**生产代码原本 0 处 `QFileDialog::DontUseNativeDialog`** —— 文件对话框可能落到原生/portal 实现，那样 QSS 与调色板都管不到它的菜单。已把 6 个创建点统一强制 Qt 自绘（`src/common/file_dialog_filters.h` 集中封装）。

同时加了**自动落盘**的弹层诊断（无需环境变量）：`ApplicationShell` 装 `QApplication` 级 `Show` 过滤器，每个 `QMenu` 弹层向 `<stateDir>/ui-menu-diag.log` 追加一行 JSON，字段含 `class / parentClass / parentObjectName / isWindow / translucent / size / palette(Window,Base,Text,Button) / styleObjectName / styleClass / styleSheetEmpty`。

- 本环境实测（无合成器）：每个打开的菜单都有对应行，文件对话框右键菜单为 `class=QMenu parentClass=QTreeView parentObjectName=treeView translucent=false paletteWindow=#ffffff styleClass=QStyleSheetStyle`。
- 包内实测日志实际落点：`~/.config/VEdit/vedit-app/ui-menu-diag.log`。
- 产物：`3ee5b07d607bfdd5739e79acea5b54480b1b1872595b25723b45043c5cc186a7`（`138,811,896` 字节），构建 `steps_failed=0`。
- **仍未证明**：用户实机纯黑无法在本环境复现；强制自绘能否修复取决于其原对话框是否为原生实现，需用户实机复跑并回传上述日志。

### AppImage 在 CentOS 7 上报 "无法读取 squashfs"（2026-11-09）

用户实机（CentOS 7 桌面）打开 `3ee5b07d…` 报 `Something went wrong trying to read the squashfs image.`。本地核查：

- 产物完整：`--appimage-version` 正常、`--appimage-extract` **exit 0**、squashfs 在标准偏移 `944632`，`sha256=3ee5b07d…`；
- 与上一版（`5f433086…`）的**内嵌 runtime 版本完全相同**（`AppImage/type2-runtime@fedfad5`），差异只在镜像内容 → **不是 runtime 版本回归**；
- 结论：包本身可用，故障点在目标机的 **FUSE 挂载**环节（AppImage type2 需要 `libfuse2` + `fusermount` + `/dev/fuse`）。

备用交付（免 FUSE）：`packaging/runtime/out/VEdit-0.2.1-x86_64-appdir.tar.gz`（159,663,474 字节，sha256 `2fe5c98ea91f5a0512874643303b7aeccd253932fb1395e15803b3d32edce730`），内容为完整的 AppDir（359 文件），解包后 `./AppRun` 直接运行，已在容器内验证可执行（`--selfcheck-ffi` 正常响应）。

### 工程面板子区块边界（V13/V13b/V13c）

用户反馈：`Project` 卡片里的 `Files / Include dirs / Defines / Diagnostics` 四个子单元"看不清框在哪，和背景融为一体"。

- 结构（未改 C++）：`ProjectPanel(QWidget#project-panel)` 内是 `QLabel` 标题 + `QListWidget` 内容，objectName 已稳定（`project-files-header`/`project-file-list` 等，测试引用）。
- QSS（`cards.qss` 末尾）：标题 `@surfaceAlt@` 底 + 底部 1px `@border@` 分隔（紧凑型，零垂直 padding），内容 `@surface@` 底 + 1px `@border@` 描边 + 6px 圆角；块间沿用 layout spacing 6px。
- 实测：标题分隔线 ΔL 26（yunwu）/24（yaoshi）≥15，标题底色 ΔL 7/6 ≥5，内容框左右描边 ΔL 26/24，块间 6px 纯卡片色；深色下边框 `#353538` 比卡片底 `#1D1D20` **更亮**。
- 末行回归修复：初版标题 4px 上下 padding 把列表压缩 10–20px 导致半行裁切（`SIMULATION` 被切）；改为紧凑标题后 `Defines` viewport 61→71px、行高 18px、四条文字带均 9px 全高，`SIMULATION` 完整可见（`v13b-defines-*-2x.png`）。
- 未改：`Diagnostics` 的长句换行 item 仍被下沿裁一点，**与改版前基线截图一致**（既有现象，非本次引入）。

### 最新交付产物（V13c 之后）

- AppImage：`138,811,896` 字节，sha256 `23cf32f21bb5aae75000442bd62ae35c4a94f1453acbb3e1aa77aabc4fd9ed82`；包内 `bin/vd-app` sha256 `9adb3bad…` 与构建产物一致（确认 V13c 的 QSS 在包内）。
- 免 FUSE 包（完整 AppDir tar.gz）：`159,673,503` 字节，sha256 `0a1bab69bd5f4085542e986480af4113951b362f1ffba1dab43098b901882582`。
