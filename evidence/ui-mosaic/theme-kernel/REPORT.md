# UI-Mosaic T1 主题内核 — 证据报告

- 车道：theme-kernel（看板任务 **T2**，契约写入方 A）
- 契约：`.mpd/plans/ui-mosaic/CONTRACT.md` §0 / §1（冻结，未改动）
- 需求：`.mpd/plans/ui-mosaic/REQUIREMENTS.md`（A1/A2/A3/A6/A7 相关部分）
- 角色：Senior Engineer（workmate `senior-engineer-9`）
- 结论：**交付完成，构建与全量 ctest 通过**（`bash scripts/build-qt.sh` exit 0，62/62 tests passed，0 failed）。真实窗口视觉与打包闭包不在本车道证明范围，见 §6。

---

## 1. 交付物（仅本车道写入范围）

| 文件 | 说明 |
| --- | --- |
| `src/app/product_theme.h` / `.cpp`（64 / 203 行） | 契约 §1.2 接口 + §1.3 六套取值 + 派生色规则 + `productPalette` + `renderStyleSheet` |
| `resources/themes/cards.qss`（481 行） | 令牌模板（`@canvas@ … @gap@` + 额外 `@textDisabled@`），覆盖 §1.4 全部选择器与状态 |
| `resources/themes/theme.qss.qrc`（14 行） | prefix `/themes`，嵌入 `cards.qss` |
| `resources/app_icon.qrc`（33 行） | prefix `/icons`，逐条列出 3 个 PNG + 15 个 SVG（icon-assets 车道产出，本车道不创建） |
| `src/app/themed_icons.h` / `.cpp`（24 / 161 行） | `appIcon` / `appIconMono` / `actionIcon` / `actionNames`；`currentColor` 全量替换 + QSvgRenderer 渲染 + 缓存 + 缺失降级 |
| `src/app/ui_theme_selftest.cpp`（约 350 行） | 手写 `main()` + 失败计数器，**30 条断言**（§1.7 六条 + A3 对比度 + §1.5 行为 + A2 卡片像素 + §8 的 4 条 dock 卡片判据 + §9 的不透明像素/重着色判据） |
| `src/app/theme_manager.h` / `.cpp`（+12 / +63 行） | `applyUiTheme` / `currentUiTheme` / `uiThemeIds`；既有 `applyTheme` 语义受保护（见 §3.5） |
| `src/app/CMakeLists.txt`（+37 行） | 源文件登记、`Qt6::Svg`、两个 qrc、`vedit-app-ui-theme-selftest` 的 `add_executable`/`add_test` |
| `translations/vedit_zh_CN.ts`（+10 行） | 2 条新可见字符串手工补译文，**未跑 lupdate** |

新增中文注释按契约 §0.6 写「为什么」，未逐行翻译代码。

---

## 2. 契约点的实现方式（供 Reviewer 对照）

1. **接口签名**逐字照 §1.2 / §1.6；`productThemes()` 顺序 = `yunwu,xinghuang,bohe,qingkong,yaoshi,shenye`。
2. **六套取值**逐字照 §1.3 采样表（未自行调色）。派生规则（写在 `product_theme.cpp` 注释里）：
   - `text = dark ? #E6E8EC : #1F2328`（唯一不混色的取值）；
   - `textMuted = blendToward(text, surface, 55%)`；
   - `surfaceAlt = blendToward(surface, canvas, 60%)`，`border = blendToward(surface, canvas, 88%)`。
   六套主题里 canvas 都比 surface 暗，所以 surfaceAlt/border 都是比卡片更深的柔和层次。
3. **`productPalette` 的 `QPalette::Window` 固定取 `canvas`**（契约允许 surface 或 canvas，实现择一固定）；`WindowText/Text` 亮度差在六套上均 > 200（断言下限 90）。
4. **Disabled 组**按 RM-7 的既有规则派生：文字向它实际被绘制的底色混合 **40%**。选 40% 而不是既有 55% 是因为实测 55% 在云雾白上只有约 2.7:1，达不到 A3 的 3:1；40% 在六套上均 ≥ 4:1，并由自测逐主题断言（正文 ≥ 4.5:1、禁用态 ≥ 3:1）。
5. **`applyUiTheme` 副作用顺序**：`setPalette` → `setStyleSheet` → `setWindowIcon`，与 §1.5 一致；空 id 落到缺省 `yunwu`，未知 id 返回 false + 错误串且不改状态。
6. **`applyTheme`（语法主题）**：`uiThemeActive_` 为 true 后不再调用 `QApplication::setPalette`（只更新 `palette_`/`current_`）；未应用产品主题时行为与改动前逐字一致。理由：§1.5 要求「不得清掉产品主题的 palette/QSS」，而语法主题的既有语义是重刷应用调色板——两者只能在「产品主题已接管」这一新状态下取交集。这是本车道对既有语义唯一的收窄，**请 Reviewer 重点确认**。
7. **qrc 的登记方式**：两个 qrc 挂在**可执行文件**（`vedit-app` 与 `vedit-app-ui-theme-selftest`）的源列表里，而不是 `vedit_app_shell` 静态库。原因：静态库里的 rcc 对象只有在有人引用 `qInitResources_*` 时才会被链接进可执行文件，而 `main.cpp` 归 icon-assets 车道且对本任务冻结，不能加 `Q_INIT_RESOURCE`；挂在可执行文件上时 Qt 的资源初始化器在 `main()` 之前自动执行，零侵入。`theme.qss.qrc` 这个带点的资源名也不依赖 CMake 的符号清洗规则（日志显示生成的是 `qrc_theme.qss.cpp`，可用）。
8. **`@textDisabled@` 是额外令牌**（契约 §1.4 原文「至少包含」）：QSS 的普通 `color` 规则会连禁用态一起上色，所以每个带文字的控件都需要一个达到 3:1 的显式禁用色；`textMuted` 在浅色主题上约 2.9:1 不够，故按 Disabled 组同一条 40% 规则单独派生。
9. **`themed_icons` 着色**：`QByteArray::replace("currentColor", color.name(QColor::HexArgb))` 是**全量替换**（`theme.svg` 的 `fill` 与 `stroke` 各出现一次），按 `qGuiApp->devicePixelRatio()` 出图，缓存键含 (name, colour, DPR)；资源缺失返回空 `QIcon`，不崩溃。
10. **QtSvg**：`themed_icons.cpp` 需要 `QSvgRenderer`，在 `src/app/CMakeLists.txt` 里 `find_package(Qt6 REQUIRED COMPONENTS Svg)` 并链接 `Qt6::Svg`。它是 pinned 镜像自带的 Qt6 模块（不是第三方依赖），`ldd` 已确认 `vedit-app` 动态链接 `libQt6Svg.so.6`。

---

## 3. 运行过的命令与观测结果

所有命令在仓库根目录 `/home/haroldzhao/MyProj/DshProj/VEdit` 执行；容器为 pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24.0）。原始日志都在 `evidence/ui-mosaic/theme-kernel/`。

### 3.1 语法预检（先做，避免白等长构建）

命令（容器内）：`g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -fPIC <Qt flags> -I<KF6 include> -Isrc/app <4 个新/改 TU>`

观测（`syntax-check.log`）：

```
== src/app/product_theme.cpp
   OK
== src/app/themed_icons.cpp
   OK
== src/app/theme_manager.cpp
   OK
== src/app/ui_theme_selftest.cpp
   OK
ALL-SYNTAX-OK
```

### 3.2 中文语言包门禁（`lrelease`，容器内）

```
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/vedit_zh_CN.qm
    Generated 1258 translation(s) (1258 finished and 0 unfinished)
    37a4d86c4c4543eb64a1cd5f356cd83174c6d45f8371b6257694018cc2b0b59f  /tmp/vedit_zh_CN.qm
```

- **0 unfinished**；输出中没有 `Ignored ... untranslated` 行，即 **0 untranslated**（`build/gates/lrelease.sh` 的判据）。
- 新增两条消息均在 `VEdit` context：`unknown UI theme '%1'`、`cannot apply a UI theme without a QApplication`。

### 3.3 完整构建 + 全量 ctest（完成判据）

命令：`bash scripts/build-qt.sh`（后台执行，退出码 **0**；完整日志 `build.log`）

关键观测：

```
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke)
        + xcb/software-rendering smoke inside vedit-qt6-build:local
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  84.60 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 ... window=1 editor=1 document=1 ...
```

- 新文件确实进入编译：`Building CXX object .../product_theme.cpp.o`、`themed_icons.cpp.o`；`qrc_theme.qss.cpp.o` 与 `qrc_app_icon.cpp.o` 同时编进 `vedit-app` 与 `vedit-app-ui-theme-selftest`。
- 新 ctest 条目：`16/62 Test #16: vedit-app-ui-theme-selftest ... Passed 0.03 sec`。
- **既有条目无回归**：本轮只新增 1 个 ctest 条目（源码里 `add_test` 由 64 → 65，实际生效 61 → 62），全量 100% 通过、0 failed。
- 编译告警：构建日志中没有 `warning:`/`error:`。唯一的 `ninja: warning: premature end of file; recovering` 在**改动前的基线构建日志**里同样存在（`evidence/m0-build/build-logs/32-qt6-build.log:1`），不是本车道引入。

### 3.4 新自测的直接运行 + 定向 ctest

```
ok   product.themes.count / .order / .lookup / .unknown-null / .darkness ...
ok   product.stylesheet.render
ok   product.palette.contrast
ok   icons.app.dark / .light / .mono / .actions.list / .actions.render
ok   manager.default-ui-theme / .ui-theme-ids / .unknown-rejected / .unknown-keeps-state
ok   manager.apply / .current / .palette-applied / .stylesheet-applied / .window-icon-applied
ok   manager.syntax-keeps-product-theme / .empty-id-default / .stylesheet-card-renders
PASS: 0 failure(s)
```

定向 ctest（正确挂载 `/vedit-build` 后）：

```
1/1 Test #16: vedit-app-ui-theme-selftest ......   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1
```

日志：`selftest-ui-theme.log`、`ctest-ui-theme.log`。

### 3.5 契约 §1.7 断言映射

| §1.7 | 自测断言 |
| --- | --- |
| 1. 6 条 + 固定顺序 | `product.themes.count`、`product.themes.order` |
| 2. id/displayName/syntaxTheme 非空 | `product.themes.lookup`（+ 未知 id：`product.themes.unknown-null`） |
| 3. 深色判定 | `product.themes.darkness`（+ 未知 id false） |
| 4. 样式表非空/无 `@`/含 accent | `product.stylesheet.render`（另查 `QFrame#veditCard` 与 `12px`） |
| 5. Window=surface\|canvas，亮度差 ≥90 | `product.palette.contrast`（固定取 canvas；另加 A3 正文 ≥4.5:1 / 禁用 ≥3:1） |
| 6. 深浅窗口图标非空 | `icons.app.dark`、`icons.app.light`（另加 mono） |

额外断言（不替代、只加强）：15 个动作图标全部可渲染；`ThemeManager` 的缺省/未知/应用/语法主题保护/空 id；**A2 卡片像素**——应用样式表后创建 `QFrame#veditCard`，`grab()` 中心像素必须等于该主题 `surface`。

### 3.6 QSS 真实渲染探针（独立于自测的旁证）

`card_probe.cpp`（证据工具，不属产品代码）在 offscreen 下对六套主题各贴一次样式表并 grab 一张卡片：

```
yunwu      surface=#ffffff  rendered=#ffffffff OK
xinghuang  surface=#fffdf6  rendered=#fffffdf6 OK
bohe       surface=#ffffff  rendered=#ffffffff OK
qingkong   surface=#ffffff  rendered=#ffffffff OK
yaoshi     surface=#1d1d20  rendered=#ff1d1d20 OK
shenye     surface=#161f2e  rendered=#ff161f2e OK
CARD-PROBE PASS
```

日志：`card-probe.log`。它证明整份 `cards.qss` 没有解析失败、`QFrame#veditCard` 规则真的生效。

### 3.7 动态依赖

```
ldd target/qt6-build/src/app/vedit-app | grep -i svg
  libQt6Svg.so.6 => /usr/lib/x86_64-linux-gnu/libQt6Svg.so.6
```

`packaging/runtime/assemble-appdir.sh` 用 `ldd` 闭包收集库，因此 AppDir 应自动带上 `libQt6Svg.so.6`——**但打包链路本轮没有运行**（见 §6）。

---

## 4. 变更范围核对

`git status` 里本车道只有：`src/app/CMakeLists.txt`、`src/app/theme_manager.{h,cpp}`、`translations/vedit_zh_CN.ts`（修改），以及 `src/app/product_theme.*`、`src/app/themed_icons.*`、`src/app/ui_theme_selftest.cpp`、`resources/themes/`、`resources/app_icon.qrc`（新增）。

未触碰：`src/app/main.cpp`（icon-assets 车道）、`resources/icons/**`、其他车道的 `.cpp`、任何测试/门禁脚本。**没有运行 `lupdate`**。

---

## 5. 复现命令

```bash
# 语法预检（可选，容器内）
docker run --rm -v "$PWD":/src -w /src vedit-qt6-build:local bash -lc '
  QTFLAGS=$(pkg-config --cflags Qt6Widgets Qt6Svg Qt6Gui); KS=/usr/include/KF6/KSyntaxHighlighting
  for f in src/app/product_theme.cpp src/app/themed_icons.cpp src/app/theme_manager.cpp \
           src/app/ui_theme_selftest.cpp; do
    g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -fPIC $QTFLAGS -I"$KS" -Isrc/app "$f"; done'

# 语言包
docker run --rm -v "$PWD":/src -w /src vedit-qt6-build:local \
  /usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/vedit_zh_CN.qm

# 主验证（20–40 分钟，含 Rust 层 / configure / build / 全量 ctest / xcb smoke）
bash scripts/build-qt.sh

# 定向重跑（注意构建树里的测试命令记录的是 /vedit-build 绝对路径）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build -R '^vedit-app-ui-theme-selftest$' -V
```

---

## 6. 未证明 / 已知边界（请 Reviewer 据此判定）

1. **真实窗口视觉未验证**：本车道只证明 QSS 被解析、卡片底色像素正确、调色板数值达标；`chrome-ui` 完成接线（视图菜单主题入口、卡片 QFrame、工具栏图标）之前的六套主题观感 / 深色可读性截图 **不在本车道**，A2/A3 的最终目检需要在该车道合并后做（xvfb+xcb 截图）。
2. **DPR ≠ 1 的图标清晰度未实测**：代码按 `qGuiApp->devicePixelRatio()` 出图并缓存，但 offscreen 自测 DPR=1；高 DPI 屏幕上的观感未验证。
3. **打包链路未运行**：`libQt6Svg.so.6` 已进入 `vedit-app` 的动态依赖，`assemble-appdir.sh` 用 ldd 闭包应自动收录，但 AppDir/AppImage 构建与 `verify-f2-isolation.sh` 本轮未执行（不属本车道完成判据）。
4. **契约 §1.5 的语义解释**：`applyTheme` 在产品主题激活后不再重刷应用调色板（见 §2.6）。如果 Reviewer 认为「既有 applyTheme 语义不得改变」应当逐字理解为「任何情况下都重刷调色板」，则与本条 §1.5「不得清掉产品主题 palette」冲突，需要船长裁决；当前实现按后者执行，并让既有路径（未应用产品主题时）逐字不变。
5. **资源缺失的降级路径**：`renderStyleSheet` 在 qrc 缺失时返回空串（只应用调色板）；`themed_icons` 返回空 QIcon。生产可执行文件与自测都带 qrc，所以该路径只在未来某个只链接 `vedit_app_shell` 的二进制里出现——这是有意的优雅降级，不是静默吞错（自测会因空串/空图标失败）。`card_probe.cpp` 是证据工具，未登记进 CMake。
6. **QSS 细节**：`QTabBar::tab` 按 §2.3 用 8px 圆角（§1.4 的「胶囊形」在 8px 圆角 + 6/14px 内边距下是克制的胶囊观感）；滚动条箭头被压成 0 高度、`QToolButton::menu-indicator` 隐藏。这些都需要真实窗口目检确认没有平台样式差异。
7. **`ninja: warning: premature end of file; recovering`**：基线构建日志同款（`evidence/m0-build/build-logs/32-qt6-build.log:1`），非本车道引入，构建恢复后成功。

---

## 7. 证据文件

| 路径 | 内容 |
| --- | --- |
| `evidence/ui-mosaic/theme-kernel/build.log` | 最终 `bash scripts/build-qt.sh` 全量日志（62/62 ctest、xcb smoke、exit 0） |
| `evidence/ui-mosaic/theme-kernel/build-run1.log` | 首次构建日志（加卡片像素断言之前，62/62 通过） |
| `evidence/ui-mosaic/theme-kernel/selftest-ui-theme.log` | T1 时期的自测直接运行（25 ok / 0 failure）+ 定向 ctest |
| `evidence/ui-mosaic/theme-kernel/ctest-ui-theme.log` | `ctest -R '^vedit-app-ui-theme-selftest$' -V`（正确挂载下 1/1 Passed） |
| `evidence/ui-mosaic/theme-kernel/syntax-check.log` | `-Wall -Wextra -Wpedantic -Werror` 语法预检 |
| `evidence/ui-mosaic/theme-kernel/lrelease.log` | 1258 finished / 0 unfinished / 0 untranslated |
| `evidence/ui-mosaic/theme-kernel/card-probe.log` | 六套主题卡片像素探针 |
| `evidence/ui-mosaic/theme-kernel/card_probe.cpp` | 探针源码（证据工具，不参与产品构建） |

---

## 8. T5 增量：dock 卡片化（QSS-only；产品构建待船长放行）

背景：界面接线车道实测发现「不许改测试」，因此 dock 内容不能被 QFrame 包裹（会打红
`vedit-app-shell-selftest` panel-seam、`ux_selftest` a2.f1.terminal-panel、
`vedit-terminal-selftest` dock.content、filetree 占位面板替换逻辑共 4 条既有断言）。
A2「面板是圆角卡片」只能由 QSS 画在 `QDockWidget` 上。

### 8.1 本轮改动（仅两个文件）

- `resources/themes/cards.qss`（+79 行，现 560 行）
- `src/app/ui_theme_selftest.cpp`（+4 条 dock 卡片像素断言）

未碰任何其他文件；**没有运行产品构建**（构建目录归 T3），探针只在容器 `/tmp` 里用
pinned 镜像的 Qt 编译运行，未写 `target/qt6-build`。

### 8.2 新增/修改的选择器与理由

| 选择器 | 规则 | 理由 |
| --- | --- | --- |
| `QMainWindow::separator` | `width:@gap@; height:@gap@; background:@canvas@` | dock 之间的真实留白 = 卡片间隙；分隔条是 QMainWindow 自带的，不用改控件树 |
| `QMainWindow::separator:hover` | 同 `@canvas@` | 拖动时不做刺眼高亮 |
| `QDockWidget` | `background:@canvas@; border:none` | 实测 dock 自身 border/radius 不绘制；铺画布色让卡片圆角缺口透出画布 |
| `QDockWidget::title` | `background:@surface@; padding:6px @gap@; font-weight:bold; border:1px solid @border@; border-bottom:none; border-top-left/right-radius:@radius@` | 卡片头上半圈：surface 底 + 上/左/右 1px 描边 + 上方 12px 圆角 |
| `QDockWidget > QWidget` | `background:@surface@; border:1px solid @border@; border-top:none; border-bottom-left/right-radius:@radius@` | 卡片体下半圈：内容层默认画 palette Window（画布色）会盖住卡片底；只作用于 dock 直接子控件，不是全局 QWidget |
| `QDockWidget QTabWidget` | `background:transparent` | VCS 面板 tab 之外的空隙不出现画布色 |
| `QTableView` | 并入既有 `QTreeView, QListView` 规则（含 `::item` hover/selected、`:disabled`） | 诊断/设置面板的表格视图与树视图同一套底色/选中色 |
| `QPlainTextEdit, QTextEdit` | surface 底 + 1px 描边 + 8px 圆角 + accent 选中色 | 面板里的差异视图/多行输入不再在卡片中间留画布补丁 |

**没有**任何无差别的全局 `QWidget { ... }` 规则。

### 8.3 实测结论（改变实现方案的依据）

探针环境：pinned 镜像 `vedit-qt6-build:local`，Qt 6.10.2 / Fusion / `QT_QPA_PLATFORM=offscreen`，
`app.setStyleSheet(renderStyleSheet(yunwu))` 后构造真实 `QDockWidget`/`QMainWindow` 取像素。

1. **`QDockWidget` 自身的 `border` / `border-radius` 不被绘制**：只写
   `border: 2px solid red` 时边缘仍是内容底色；`background: green; border: red;
   border-radius: 12px` 时边缘仍是 green，没有红边、圆角也不裁剪。→ 原方案「dock 本体
   `border + border-radius`」不可行，改为 §8.2 的拼接方案。
2. **`QDockWidget::title` 子控件的 background / border / border-radius 全部生效**：
   蓝底 + 3px 绿边 + 左上 12px 圆角时，左上角透出 dock 底色、边线沿 title 四周连续。
3. **直接内容控件可自由绘制** background / border / border-radius（`QDockWidget > QWidget`
   命中后边缘即为 border 色）。
4. 因此卡片轮廓 = title 上半圈 + 内容层下半圈；dock 本体铺 canvas，让 12px 圆角缺口透出
   画布，形成真正的圆角卡片剪影。

三种主题的实测像素（`qss-dock-probe.log`）：

| 主题 | 左上角(0,0) | title 上边框 | 内容层左边框 | 内容中心 | 期望 |
| --- | --- | --- | --- | --- | --- |
| yunwu | `#f4f4f2` (canvas) | `#f5f5f4` (border) | `#f5f5f4` (border) | `#ffffff` (surface) | 全部一致 |
| yaoshi | `#131315` (canvas) | `#141416` (border) | `#141416` (border) | `#1d1d20` (surface) | 全部一致 |
| shenye | `#0f1622` (canvas) | `#101723` (border) | `#101723` (border) | `#161f2e` (surface) | 全部一致 |

`QMainWindow::separator` 实测：dock 宽 240，central widget x=248，**gap = 8px**，间隙像素为
canvas 色（`qss-dock-probe.log`）。

渲染截图（offscreen，仅证明 QSS 生效，不是最终目检）：

- `qss-dock-probe-yunwu.png` / `qss-dock-probe-yaoshi.png` / `qss-dock-probe-shenye.png`
- 注：截图中中文标题显示为方框，是 pinned 镜像没有 CJK 字体这一**既有**环境限制
  （`src/workspace/search/README.md:277` 已记录），与 QSS 无关。

### 8.4 自测新增断言（已在 T3 合并构建中执行并通过）

`ui_theme_selftest.cpp` 在原 25 条断言之后新增 4 条，全部是真实 QMainWindow + QDockWidget
的像素判据（`QT_QPA_PLATFORM=offscreen` 下 grab）：

- `manager.dock-separator-gap`：`central->x() - dock->width() == 8`
- `manager.dock-card-title-border`：dock 抓图 `(midX, 0)` == `border`
- `manager.dock-card-body`：`(0, midY)` == `border` 且 `(midX, midY)` == `surface`
- `manager.dock-card-round-corner`：`(0,0)` == `canvas`

语法预检：`-Wall -Wextra -Wpedantic -Werror` 通过（§3.1 同款命令）。

**执行结果**（T3 车道 21:10 的合并构建产物，本车道只做只读验证，没有重新编译）：

```
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
    ctest --test-dir target/qt6-build -R '^vedit-app-ui-theme-selftest$' --output-on-failure
Test project /vedit-build/target/qt6-build
    Start 16: vedit-app-ui-theme-selftest
1/1 Test #16: vedit-app-ui-theme-selftest ......   Passed    0.04 sec

100% tests passed, 0 tests failed out of 1
```

同一二进制直接运行（29 条 `ok`，含 4 条新 dock 断言，`0 failure(s)`）：

```
ok   manager.stylesheet-card-renders
ok   manager.dock-separator-gap
ok   manager.dock-card-title-border
ok   manager.dock-card-body
ok   manager.dock-card-round-corner
PASS: 0 failure(s)
```

日志：`selftest-ui-theme-t3build.log`、`ctest-ui-theme-t3build.log`。

### 8.5 构建验证来源与边界（如实声明）

- **本车道没有独立运行 `bash scripts/build-qt.sh`**。按船长指令只跑只读的定向 ctest；
  编译来源是 T3 车道 2026-11-08 21:10 的全量合并构建（其 `32-qt6-build.log` 第 156 行
  编译了 `ui_theme_selftest.cpp.o`，第 345 行链接了自测可执行文件；`33-qt6-ctest.log`
  第 33 行是 `16/62 vedit-app-ui-theme-selftest Passed`，总汇总 `62/62 passed, 0 failed`）。
- **深色可读性与六套主题的最终目检不在本车道结论内**：由船长截图 + 独立验证车道负责；
  本车道只提供 QSS 像素探针（§8.3）与 offscreen 渲染图（会有平台/字体差异，见下）。
- **集成反馈备案（不改代码，等 T3 定位）**：T3 的真实截图里工具栏一个图标都没显示、
  右上角主题按钮不可见。本车道当前 `cards.qss` 的 `QToolButton` 规则只有
  `background/color/border/border-radius/padding`，**没有** `icon-size`、固定
  `width/height` 或 `image` 覆盖；`QToolButton::menu-indicator { image: none; }` 只隐藏
  菜单指示器。根因由 T3 诊断；若结论是 QSS 吃掉图标，船长会另开任务，本车道再改。
- 截图里的中文标题方框是 pinned 镜像**没有 CJK 字体**的既有环境限制
  （`src/workspace/search/README.md:277`），与 QSS 无关。

### 8.6 新增证据文件

| 路径 | 内容 |
| --- | --- |
| `evidence/ui-mosaic/theme-kernel/qss-dock-probe.log` | 三主题 dock 卡片像素探针 + 分隔条 8px 实测 |
| `evidence/ui-mosaic/theme-kernel/qss-dock-probe-{yunwu,yaoshi,shenye}.png` | offscreen 渲染截图（卡片/间隙/胶囊 tab） |
| `evidence/ui-mosaic/theme-kernel/qss-pollution-probe.log` | T8 隐患自查：dock 卡片判据 / 标题栏按钮 / tab 滚动钮 / KTE 裁剪对照 |
| `evidence/ui-mosaic/theme-kernel/qss-pollution-before-after.log` | T8 before/after：KTE 18px 按钮 darkTextPixels 25→210 等 |
| `evidence/ui-mosaic/theme-kernel/qss-t8-recheck.log` | 删除死选择器后的复测（dock 判据不变、KTE 对照仍 25→210） |
| `evidence/ui-mosaic/theme-kernel/ctest-ui-theme-t6build.log` | T6 合并构建产物定向 ctest：1/1 Passed，0 failed |
| `evidence/ui-mosaic/theme-kernel/selftest-ui-theme-t6build.log` | T6 合并构建产物直接运行：30 ok / 0 failure |
| `evidence/ui-mosaic/theme-kernel/selftest-ui-theme-t3build.log` | T3 合并构建产物直接运行：29 ok / 0 failure（含 4 条 dock 断言） |
| `evidence/ui-mosaic/theme-kernel/ctest-ui-theme-t3build.log` | 定向 ctest（只读，未编译）：1/1 Passed，0 failed |

---

## 9. T7：SVG 着色 HexArgb → HexRgb（图标全透明根因修复）

### 9.1 根因（chrome-ui 在真实 vedit-app 进程内取证，本车道复现）

`themed_icons::renderSvg()` 按旧契约把 `currentColor` 替换成
`color.name(QColor::HexArgb)` = `#ff1f2328`，而 **`QSvgRenderer` 不接受 Qt 扩展色序
`#AARRGGBB`**：整条 path 渲染为空，图标变成全透明 pixmap。`!pixmap.isNull()` 抓不到这种
失败（pixmap 存在但 0 个不透明像素），所以旧断言一直是绿的。

本车道用 `new.svg` 复现（`themed-icons-probe.log`）：

```
HexArgb(#ff1f2328) opaquePixels=0   (expected 0)
HexRgb (#1f2328)   opaquePixels=195 (expected >0)
```

### 9.2 改动（仅两个文件，其余未动）

1. `src/app/themed_icons.cpp`
   - `renderSvg()` 改用 `color.name(QColor::HexRgb)`（线稿不承载 alpha；调用方 alpha
     被有意丢弃）；
   - 渲染缓存键同步改成 `HexRgb`（否则只差 alpha 的两次调用会命中互相矛盾缓存）；
   - 注释改写为血泪版「为什么不能用 HexArgb」，并指向契约 §1.6 的更正。
2. `src/app/ui_theme_selftest.cpp`
   - 新增 `opaquePixelCount()` / `differingPixelCount()` 两个纯 helper；
   - `icons.app.{dark,light,mono}` 从「非 null」加强为「64px 下至少 1 个不透明像素」；
   - 15 个 action 逐个断言「24px 下至少 1 个不透明像素」（断言名 `icons.actions.opaque`，
     失败时点名具体图标）；
   - 新增 `icons.actions.recolour`：`theme` 图标用 `#1f2328` 与 `#f4f4f2` 两种差异明显的
     颜色渲染，差异像素数必须 > 0（防止颜色替换再次静默失效）。

### 9.3 修复后的实测（`themed-icons-probe.log`，容器 /tmp 探针，未动构建目录）

15 个 action 全部 `iconNull=0` 且不透明像素 117–240：

```
new 195 / open 172 / save 240 / save-as 212 / undo 117 / redo 117 / find 139 /
replace 132 / sidebar 216 / terminal 227 / build 208 / vcs 156 / settings 177 /
theme 108 / help 210
appIcon(dark) 3708 / appIcon(light) 3708 / appIconMono 3708
recolour: differingPixels=108
ICONS-PROBE PASS
```

语法预检：`-Wall -Wextra -Wpedantic -Werror` 对两个改动的 TU 通过。

### 9.4 验证边界（如实声明）

- **本车道没有独立运行 `bash scripts/build-qt.sh`**。T7 的自证来自容器 `/tmp` 探针（同一份
  `themed_icons.cpp` + `resources/app_icon.qrc`）与 `-Wall -Wextra -Wpedantic -Werror`
  语法预检，不是产品构建产物。
- **最终合并构建由 chrome-ui（T6）统一执行**（构建目录独占权在 T6，避免冲突毁证据）；
  其后以 T6 的构建日志 + 定向 `ctest -R '^vedit-app-ui-theme-selftest$'` 复核，确认全量
  **62/62** 且新断言 `icons.actions.opaque` / `icons.actions.recolour` 首次在真实产品构建中
  通过。
- 若定向复核失败，按缺陷回退给本车道修复；复核通过后的输出再补进本节。

---

## 10. T8：全局按钮 padding 污染 KTextEditor 状态栏（根因收敛）

### 10.1 缺陷与根因

chrome-ui 在真实 `vedit-app` 进程内取证（`evidence/ui-mosaic/chrome-ui/logs/40-runtime-yunwu.log`）：
编辑区卡片几何完全正确（`card bottom=789`、状态栏 `top=798`，8px 间隙在；KTextEditor 的
`KateStatusBar 346x18` 完全在卡片内）。被裁的是 KTextEditor **内部 18px 状态按钮**
（`UTF-8 / Soft Tabs / LF / Normal`）：全局 `QPushButton { padding: 6px 14px; }` 命中后，
内容区高度 `18 - 2×6 - 2×1 = 4px`，13px 字体被压成横切。

本车道用同名容器类复刻（`KateStatusBar` 子类 + 60×18 的 QPushButton、13px 字体）：

| 样式 | darkTextPixels（文字完整度） |
| --- | --- |
| 旧（只有全局 padding 6px 14px） | **25**（只剩中间 4px 一横） |
| 补丁后（全局 + `KateStatusBar QPushButton` 复位） | **210**（完整 “UTF-8”） |

### 10.2 改动（仅 `resources/themes/cards.qss`，+93 行 → 653 行）

`ui_theme_selftest.cpp` 本轮**未改**：T5 的 4 条 dock 像素断言在补丁后复测不变（见 §10.4），
而 KTextEditor 内部类无法在 shell 自测里实例化（见 §10.5 边界）。

1. **KTextEditor 嵌入控件复位组**（类名逐个取自 `libKF6TextEditor.so` 自身的 moc 类表）：
   `KateStatusBar / KateViewBar / KateViewBarWidget / KateSearchBar / KateCommandLineBar`
   下的 `QPushButton`、`QToolButton`：
   `padding: 0 6px; border: none; background: transparent; border-radius: 6px; color:@text@;`
   另加 `:hover`（surfaceAlt）与 `:pressed`（surface）。
   初稿曾写 `KateView` 作兜底，核对库内 moc 类表后确认**不存在名为 `KateView` 的 QObject
   类**（只有 KateViewBar/KateViewInternal/KateViewAccessible 等），已删除该死规则；
   这些选择器比单个 `QPushButton` 更具体（0,0,2 对 0,0,1），稳定覆盖全局规则。
2. **KTextEditor 搜索/命令行条的 QLineEdit**：`padding: 2px 6px;`（全局 5px 会在 18~24px
   的嵌入条里把输入框撑高；不裁剪但会破坏条高度）。
3. **QTabBar 小按钮**：`QTabBar QToolButton { padding: 0; border: none; background: transparent; }`
   （滚动按钮）与 `QTabBar::close-button`（关闭按钮，QAbstractButton 子类只走 subcontrol）。
4. **QDockWidget 标题栏按钮顺序硬化**：把 `QDockWidget::close-button/float-button` 规则移到
   `QDockWidget > QWidget` 之后，并补 `QDockWidget > QAbstractButton` 复位——两者特异度相同，
   后写才生效。

### 10.3 同类隐患自查结论

| 检查项 | 结论 |
| --- | --- |
| KTextEditor `KateStatusBar` 18px 按钮 | **实测被裁（25→210）**，已覆盖 |
| KTextEditor 各 bar 的按钮（ViewBar/Widget/SearchBar/CommandLineBar） | 类名来自库自身 moc 类表，按同一规则组覆盖；无法在 shell 自测里实例化（防御性）。库内无 `KateView` QObject 类，故未写该兜底 |
| KTextEditor bar 内 QLineEdit | 防御性 padding 复位（全局 5px 会撑高嵌入条） |
| QTabBar 左右滚动按钮（QToolButton） | 实测补丁前后均 `16x30`（QTabBar 自己定几何，全局 padding 未撑大）；规则保留作防御 |
| QDockWidget 标题栏按钮 | 实测标题栏右侧按钮带内 border 色像素 **前后都是 0**（未被卡片体规则污染）；顺序硬化保留作防御 |
| 标签页关闭按钮（QAbstractButton 子类） | 不受 `QToolButton`/`QPushButton` 选择器影响；已补 `QTabBar::close-button` 样式 |
| 全局 `QPushButton`/`QToolButton` 是否带 `font-size`/`min-width` | **都没有**（自查项）——不存在字号/min-width 污染，唯一污染源是 padding |
| `QComboBox`/`QCheckBox`/`QTreeView` 等全局规则 | 只出现在应用面板与 KTE 配置对话框内部，不嵌入 18px 工具条；判断无裁剪风险，未改 |
| 全局 `QWidget` 规则 | 仍然没有（沿用 T5 结论） |

**为什么保留全局 `QPushButton`/`QToolButton` 而不整体收窄**：面板按钮由多条车道用不同类/
objectName 生成（`SearchPanel`、`ProjectPanel`、`VcsPanel`、设置页各页…），把全局规则收窄
到“今天已知的应用类”会漏掉未来面板；在**已知会嵌入第三方控件的容器**内复位盒模型，才是
可维护的根因收法（第三方类表是封闭的，Qt/KTE 的类名稳定）。

### 10.4 补丁后回归复测（容器 /tmp 探针，未动构建目录）

T5 的 4 条 dock 像素判据复测仍成立：

```
dock card: gap=8 corner=#f4f4f2(canvas) titleBorder=#f5f5f4(border)
           bodyLeft=#f5f5f4(border) bodyMid=#ffffff(surface)
```

QSS 应用过程无 “Could not parse stylesheet” 告警。证据：
`qss-pollution-probe.log`、`qss-pollution-before-after.log`；
删除死选择器后复测：`qss-t8-recheck.log`（dock 判据不变、KTE 对照仍 25→210）。

### 10.5 最终合并验证（T6 合并构建 + 定向复核，已完成）

T6 的最终合并构建 **exit 0**（二进制 mtime `2026-11-08 21:52:04`），包含本节的 QSS 补丁
（`cards.qss` 最后修改 mtime `21:49`，构建日志第 973/1079 行重新编译了
`qrc_theme.qss.cpp.o`）。本车道只做只读复核，没有重新构建：

```
$ docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
    ctest --test-dir target/qt6-build -R '^vedit-app-ui-theme-selftest$' --output-on-failure
Test project /vedit-build/target/qt6-build
    Start 16: vedit-app-ui-theme-selftest
1/1 Test #16: vedit-app-ui-theme-selftest ......   Passed    0.04 sec

100% tests passed, 0 tests failed out of 1
```

同一二进制直接运行：**30 条 `ok` / 0 failure**（T5 的 4 条 dock 判据、T7 的
`icons.actions.opaque` / `icons.actions.recolour` 全部在真实产品构建中通过）。

T6 全量日志 `evidence/ui-mosaic/chrome-ui/logs/14-t6-post-t8-build-qt.log`：

- 第 973 行 `Building CXX object .../qrc_theme.qss.cpp.o`（本节的 QSS 确实进了产品构建）；
- 第 1301 行 `16/62 Test #16: vedit-app-ui-theme-selftest ... Passed 0.04 sec`；
- 第 1395 行 `100% tests passed, 0 tests failed out of 62`。

**集成方目检确认（船长提供，引用）**：T6 抓的
`evidence/ui-mosaic/screenshots/after-yunwu.png` 中 KTextEditor 状态行
`1:1  INSERT  Soft Tabs: 4  UTF-8  Normal` 文字完整、不再横向裁切，证明 `KateStatusBar`
复位生效；六套主题截图的 sha256 全部不同于修复前基线。

日志：`ctest-ui-theme-t6build.log`、`selftest-ui-theme-t6build.log`。

### 10.6 覆盖边界（如实声明）

- 自测里**没有** KTextEditor 状态栏的像素断言：实例化 KTE 内部类需要把 KTextEditor 链接进
  只依赖 shell 的自测，超出该测试的依赖面。该缺陷的最终确认由 T6 的真实运行截图承担
  （已确认，见 §10.5），自测守的是 QSS 规则存在性之外的卡片/图标/对比度判据。
- KTE 若在未来版本新增内部容器类，需要在本节的选择器组里补一行（QSS 没有通配类选择器）；
  这是该收法的已知维护成本。
- 上一轮的数字（25→210 等）来自容器 `/tmp` 探针；本轮 T6 的 62/62 与 30 条断言来自真实
  产品构建产物。
