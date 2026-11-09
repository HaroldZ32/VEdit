# V1 视觉修复报告 —— 卡片可辨识 / 可见描边 / 卡片阴影 / 按钮轮廓

任务：T1（V1 视觉修复）。写入范围：`src/app/product_theme.{h,cpp}`、
`resources/themes/cards.qss`、`src/app/panel_registry.cpp`、`src/app/editor_area.cpp`、
本目录。

结论先行：**六套主题的 A–E 像素判据全部通过**（`probe.py --all` → `PASS: 0 failing
check(s) over 6 image(s)`），`bash scripts/build-qt.sh` **exit 0**，**ctest 62/62** 全过，
既有测试与门禁**一行未改**。
有一处对任务书“改调色板基线”的**有意偏离**，理由与替代方案见 §4.1——它不是省事，
而是被一条既有断言挡住（改 `productPalette()` 会直接打红 ctest）。

---

## 1. 根因（用像素复核，不重猜）

修复前的对照图 `evidence/ui-mosaic/screenshots/appimage-verify.png` 用同一个探针跑
（日志：`probe-before-yunwu.txt`）：

| 判据 | 修复前 | 说明 |
|---|---|---|
| A 左面板内部 | **FAIL** dominant=`#F4F4F2`（canvas），与 surface 差 13，占比 100% | 面板主区就是画布色 |
| B 跨边缘描边 | **FAIL** 边缘像素 `#F5F5F4`，与 surface 差 11、与 canvas 差 2 | 没有任何描边像素 |
| C 阴影带 | **FAIL** 0px | 卡片外没有阴影 |
| D 按钮轮廓 | **FAIL** 找到 0 个按钮填充/轮廓 | 工具栏只有字形像素 |

根因链（每一条都在本轮实测过）：

1. **QSS 的卡片体选择器一条都不命中。** 任务书里“`QDockWidget > QWidget` 给内容层
   上色/描边”的假设在 Qt 6.10 上不成立：给该选择器涂纯红后整张真图**红色 0 像素**；
   换成无 `>` 的 `QDockWidget QWidget` 涂纯绿则命中 **544,904 像素**。也就是说
   `dock->widget()` 并不是 QDockWidget 的直接子控件——真实面板控件在更深一层。
   于是卡片体从来没有 surface 底和描边，露出来的是 dock 自己的画布色。
2. **描边色永远不可能可见。** 旧派生是 `border = blend(surface, canvas, 88%)`，
   云雾白下 = `#FAFAF9`，与 `#FFFFFF` 只差 **5**；而 canvas 与 surface 本身只差
   **11**，无论取什么比例都到不了 15。
3. **按钮没有轮廓。** `QToolButton` 的 `border` 是 `transparent`；`QPushButton` 用的
   又是上面那个不可见的 `@border@`。

## 2. 改法

### 2.1 `src/app/product_theme.{h,cpp}` —— 描边改成能被看见的派生

`border` / `borderStrong` 不再从 canvas 混，改为 **surface 向 `text` 混合 12% / 22%**。
这样不依赖“canvas 与 surface 有多接近”，且方向自动正确：浅色主题描边比 surface 暗，
深色主题比 surface **亮**（E 的要求）。实测派生值：

| 主题 | dark | canvas | surface | border | Δ(surface) | borderStrong | Δ(surface) |
|---|---|---|---|---|---|---|---|
| yunwu | no | `#F4F4F2` | `#FFFFFF` | `#E4E5E5` | 27 | `#CECFD0` | 49 |
| xinghuang | no | `#F9F2E1` | `#FFFDF6` | `#E4E3DD` | 27 | `#CECDC9` | 49 |
| bohe | no | `#EDF4EE` | `#FFFFFF` | `#E4E5E5` | 27 | `#CECFD0` | 49 |
| qingkong | no | `#EBF0F7` | `#FFFFFF` | `#E4E5E5` | 27 | `#CECFD0` | 49 |
| yaoshi | **yes** | `#131315` | `#1D1D20` | `#353538`（更亮） | 24 | `#494A4D` | 45 |
| shenye | **yes** | `#0F1622` | `#161F2E` | `#2F3745`（更亮） | 25 | `#444B58` | 46 |

新增令牌 `@borderStrong@`（`renderStyleSheet()` 里登记，替换后无残留 `@...@`），
结构体新增 `ProductTheme::borderStrong`。

### 2.2 `resources/themes/cards.qss` —— 卡片体基线、描边、按钮

* `QDockWidget QWidget { background: @surface@; }`：卡片体基线。等效于“调色板
  Window 基线本来是 surface”想要的结果——dock 内部默认就是卡片色，
  画布只留给 `QMainWindow` / `separator` / 工具栏 / 菜单栏 / 状态栏
  （它们是显式 canvas 规则或根本不在 dock 里）。子控件自己的显式规则
  （滚动条、tab、表格项、hover）特异度更高，不被覆盖。
* `QWidget[veditDockCard="true"]`：真实面板卡片的**左右下三边 1px 描边 + 下方圆角**
  （上边由 `QDockWidget::title` 的上半圈承担，拼成一张连续卡片）。属性由
  `panel_registry.cpp` 打在内容根控件上。
* `QDialog { background: @surface@; }`（原来给的是 canvas）：对话框是卡片。
* `QToolButton`：默认 `surface` 底 + `@borderStrong@` 1px 描边 + 圆角；hover 变
  `surfaceAlt`；pressed/checked 保持原有 accent 反馈。
* `QPushButton`：`@borderStrong@` 描边；disabled 用弱一档的 `@border@`。
* 既有 `QFrame#veditCard`（编辑器卡片）规则不变，只跟着新的 `@border@` 变可见。

### 2.3 `src/app/panel_registry.cpp` —— 卡片抽屉与阴影

* 内容根控件：`setProperty("veditDockCard", true)` + `WA_StyledBackground` +
  **`setContentsMargins(1, 0, 1, 1)`**。最后一条不是装饰：Qt 不会为 QSS 描边自动
  收缩布局，不透明子控件会把父控件的 1px 描边整条盖住（本轮实测：只加描边规则时
  边缘仍然是纯白）。内缩 1px 后，描边才真正露出来，同时形成“外 1px 描边 + 内 1px
  台阶”的两层边缘。
* 每个 dock 装 `QGraphicsDropShadowEffect`：`blurRadius 20`、`offset (0,1)`、
  纯黑、**浅色 α=44 / 深色 α=150**（按画布亮度分档；30/255 的黑在 `#131315` 上
  根本看不见）。投影落在 dock 之间的 8px 画布间隙里。

### 2.4 `src/app/editor_area.cpp` —— 编辑器卡片同一投影

`QFrame#veditCard` 由外壳在 `EditorArea` 构造之后创建并把它重挂进去，所以在构造
末尾用 `QTimer::singleShot(0, ...)` 等父链定型后再给该 `QFrame` 装同一个投影
（父控件不是卡片时——例如只单独构造编辑区的测试——什么都不做）。

## 3. 验收（像素量化，全部来自真实运行截图）

命令：`python3 evidence/ui-mosaic/visual-fix/probe.py --all --json probe.json`
（截图：`capture-themes.sh` → `after-<theme>.png`，同一构建、同一次会话循环）。

| 主题 | A 三处卡片内部 | B 描边（Δsurface / Δcanvas / 与派生色差） | C 阴影带 | D 按钮（通过/找到） | 结果 |
|---|---|---|---|---|---|
| yunwu | `#FFFFFF` ×3，Δ0 | `#E4E5E5`，27 / 16 / 0 | 10px | 6/6 | **PASS** |
| xinghuang | `#FFFDF6` ×3，Δ0 | `#E4E3DD`，27 / 21 / 0 | 10px | 6/6 | **PASS** |
| bohe | `#FFFFFF` ×3，Δ0 | `#E4E5E5`，27 / 15 / 0 | 10px | 6/6 | **PASS** |
| qingkong | `#FFFFFF` ×3，Δ0 | `#E4E5E5`，27 / 18 / 0 | 10px | 6/6 | **PASS** |
| yaoshi | `#1D1D20` ×3，Δ0 | `#353538`，24 / 35 / 0（**比 surface 亮**） | 5px | 6/6 | **PASS** |
| shenye | `#161F2E` ×3，Δ0 | `#2F3745`，25 / 35 / 0（**比 surface 亮**） | 7px | 6/6 | **PASS** |

* **A**：左面板体、右面板体、编辑器卡片 chrome 三处采样区的主色都**精确等于**
  该主题的 surface（Δ=0），占比 0.94–1.00。
* **B**：垂直跨过编辑器卡片上边缘，紧贴填充的像素与 surface 差 24–27（要求 ≥15）、
  与 canvas 差 15–35（要求 ≥10），且颜色与 `product_theme.cpp` 的派生描边色**逐位
  相同**（匹配差 0，要求 ≤8）。深色两套的描边比 surface 更亮（E）。
* **C**：卡片下边缘之外连续阴影带 5–10px（要求 ≥4px，与 canvas 差 ≥3；实测首像素
  差 7–23）。
* **D**：工具栏里 6 个按钮全部满足“轮廓 vs 填充差 45–49（要求 ≥20）”且圆角
  （填充矩形角沿对角线 ≥2 个非填充像素，实测 2）。
* **E**：六套主题全过；深色主题描边比 surface 亮。

D 的样例（yunwu）：`box=[18,32,78,31] outline={'left':49,'right':49,'top':49,'bottom':49} corner_gap=2`。

### 修复前/后对照图

* 修复前：`evidence/ui-mosaic/screenshots/appimage-verify.png`（交付时的实机验证图，
  本次未改动；探针判定 5 项 FAIL，见 `probe-before-yunwu.txt`）
* 修复后：`after-yunwu.png`、`after-xinghuang.png`、`after-bohe.png`、
  `after-qingkong.png`、`after-yaoshi.png`、`after-shenye.png`（本目录）
* 整窗裁切对照：`before-window.png` / `after-window.png`

## 4. 偏离、取舍与仍不确定的项

### 4.1 调色板基线：**没有**把 `QPalette::Window` 改成 surface（重要）

任务书第 1 条要求 `productPalette()` 的 `QPalette::Window` 改为 `surface`。本轮
**没有照做**，原因是一条既有断言会直接打红：

`src/app/ui_theme_selftest.cpp:305-308`
```cpp
const bool applied = manager.applyUiTheme(QStringLiteral("shenye"), &applyError);
check("manager.palette-applied",
      shenye != nullptr && app.palette().color(QPalette::Window) == shenye->canvas);
```
它把“实现选了 canvas”写死成断言。任务书 F 同时要求 **ctest 62/62** 且**不得修改
任何既有测试或门禁**，而 `ui_theme_selftest.cpp` 也不在本任务的写入范围里。
两者无法同时满足，所以按“不破既有断言 + 不动写入范围”的硬约束选择了等价的可视化
改法：**卡片体基线改由 QSS 在 dock 子树铺 surface**（§2.2 第一条）。这个方案对
渲染结果是等价的（A 判据六套主题 Δ=0），代价是**根因里的“调色板默认角色”本身没变**。

需要船长裁决的部分：如果必须让 `QPalette::Window` 本体就是 surface，则同一文件的
`manager.palette-applied` 要放宽成契约允许的 `window == canvas || window == surface`
（`ui_theme_selftest.cpp` 里 `product.palette.contrast` 的注释原文就写着“契约允许
Window 取 surface 或 canvas，实现固定选了 canvas；两者之一都算通过”）。
在拿到这个批准之前，本轮不做“改测试让它变绿”的动作。

**仍然存在的副作用**：任何**不在 dock 子树里、又没被 QSS 显式上色**的独立
`QWidget`（理论上包含某些自绘对话框内容）仍会拿到 canvas。已知的对话框/菜单/
下拉/提示都由 QSS 显式铺了 surface（`QDialog` 这一轮从 canvas 改成了 surface）。

### 4.2 投影没有做长时性能压测

`QGraphicsDropShadowEffect` 每次重绘会把子树渲染进 pixmap。本轮验证到的是：
六套主题实机运行正常、截图正常、全套 ctest（含 xcb 真实窗口冒烟与 10k 行打开
时限用例）通过；**没有**做“长时间滚动大文件 / 连续切主题”的 CPU 或帧率测量。
如果后续观察到编辑器滚动变慢，按任务书允许的退路改成“外层 1px 描边 + 内侧 1px
台阶”（本轮已经因为 §2.3 的 1px 内缩而部分存在），删掉
`panel_registry.cpp::makeCardShadow()` 与 `editor_area.cpp` 的 singleShot 即可。

### 4.3 深色主题的投影更浓

α=150（深色）vs α=44（浅色）。这不是为了通过 C，而是因为 α≈30 的黑在
`#131315` 画布上确实看不见；如果评审认为深色投影过重，可单独下调，C 仍有 5–7px
余量（要求 ≥4px）。

### 4.4 `setContentsMargins(1, 0, 1, 1)` 的 1px 内缩

所有 dock 内容根控件都会内缩 1px。它换来了真正可见的描边，但这是一个**布局层的
副作用**；如果某条 lane 的像素级几何断言对 1px 敏感，需要在那里重新钉基线。

### 4.5 编辑器正文不是 surface（探针的采样点选择）

编辑器**文本视口**的底色由配对的 KSyntaxHighlighting 主题决定
（Solarized Light `#FDF6E3`、Breeze Dark `#232629`），那是编辑器内容而不是产品
卡片底色。所以 A 的“编辑器卡片内部”采样点取的是卡片 chrome（标签条空白区），
而不是文本视口——否则会把“编辑器跟随语法主题”误判成卡片缺陷。这一点写在
`probe.py` 的 `CARD_ROIS` 注释里。

### 4.6 对话框 / 菜单 / 弹层只做了规则层核对，没有截到真图

`QDialog` 这一轮从 canvas 改成 surface；`QMenu`、`QToolTip`、`QComboBox` 的下拉
视图原本就是 surface，未改。这四处是**读 QSS 规则确认**的，没有像主窗口那样抓到
真实运行截图（本轮截的是主窗口六套主题；对话框需要额外打开才能截）。如果评审要求
逐张弹层截图，需要另加一个自动化步骤（例如用 QTest 打开 About/Settings 再抓屏）。

### 4.7 修复前对照图的右面板

`appimage-verify.png` 的右面板内容与本次截图不同（探针在该图右面板只得到 61% 的
surface 占比），所以修复前判定的决定性证据是左面板=canvas、无边线、无阴影、
无按钮四项；右面板那一项只是同一张旧图的区域内容差异，不代表修复前的卡片状态。

## 5. 构建与测试（gate 跑在最终源码上）

命令：`bash scripts/build-qt.sh`（Rust 服务层 → 固定镜像内 configure/build →
ctest → ELF/ABI 事实 → Xvfb+xcb 软件渲染窗口冒烟），完整日志：
`evidence/m0-build/build-logs/32-qt6-build.log`、`33-qt6-ctest.log`、
`34-qt6-elf.txt`、`37-qt6-rust-abi.txt`。

结果（修复后的最终源码，`exit=0`）：

```
[qt6] ctest (offscreen: QMainWindow + KTextEditor document + KSyntaxHighlighting + tree-sitter)
...
62/62 Test #62: vedit-ui-scale-restart .........................   Passed    0.35 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  88.44 sec
[qt6] xcb + software-rendering window smoke under Xvfb
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ksynhl=6.24.0
         tree_sitter=0.25.9 window=1 editor=1 document=1 ...
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke)
       + xcb/software-rendering smoke inside vedit-qt6-build:local
```

* ctest：**100% tests passed, 0 tests failed out of 62**（与任务书要求的 62/62 一致）。
* 既有测试/门禁：**零改动**。`git status` 里只有 §2 的 5 个源文件与本 evidence
  目录；`tests/`、`src/**/tests/`、`*-selftest.cpp`、`package.json`、`scripts/`
  全部未动（唯一相关的是 `src/app/ui_theme_selftest.cpp`——如 §4.1 所述，
  本轮**没有**去改它，而是绕开了会打红它的做法）。
* lrelease：本轮**没有新增任何可见字符串**（只改了 QSS 颜色/几何、C++ 常量与注释），
  `translations/vedit_zh_CN.ts` 零改动（`git status translations/` 为空），
  所以 `release_translations` 目标不重跑、lrelease 没有新的 warning/error；
  构建日志里也没有 lrelease 报错。

## 6. 证据清单

| 文件 | 内容 |
|---|---|
| `probe.py` | 可重跑的像素验收探针（A–E），颜色从 `product_theme.cpp` 解析 |
| `probe.json` | 六套主题的完整判定结果（含每个采样区的原始数值） |
| `probe-before-yunwu.txt` | 同一探针跑修复前 `appimage-verify.png` 的输出（5 项 FAIL） |
| `after-<theme>.png` ×6 | 修复后的真实运行截图（`capture-themes.sh`，1680×1050 全屏） |
| `probe-after-all.txt` | `probe.py --all` 的完整终端输出（六套主题 A–E） |
| `before-window.png` / `after-window.png` | 1280×820 整窗裁切对照 |

复跑方式：

```bash
# 1) 抓图（需要 docker 镜像 vedit-qt6-build:local 与 X socket 目录 .mpd/x11）
bash evidence/ui-mosaic/screenshots/capture-themes.sh
for t in yunwu xinghuang bohe qingkong yaoshi shenye; do
  cp evidence/ui-mosaic/screenshots/after-$t.png evidence/ui-mosaic/visual-fix/after-$t.png
done
git checkout -- evidence/ui-mosaic/screenshots/   # 不覆盖上一批的既有证据

# 2) 像素验收
python3 evidence/ui-mosaic/visual-fix/probe.py --all --json probe.json

# 3) 构建 + 测试门禁
bash scripts/build-qt.sh
```

---

# V3（二轮）：恢复画布留白、间隙回到 canvas、阴影收柔

任务：T3。写入范围与 V1 相同（`product_theme.{h,cpp}`、`cards.qss`、
`panel_registry.cpp`、`editor_area.cpp`、本目录）。**本轮没有改任何测试/门禁**，
`src/app/ui_theme_selftest.cpp` 的 `Window==canvas` 断言原样保留。

## V3.1 根因（三条，都是像素实测）

1. **卡片贴死窗口四边。** `QMainWindow` 的 dock area 没有任何 margin/padding，
   dock 直接落在窗口 x=0 / x=1279 上（V1 截图 y=300 行：`x=0` 就是描边
   `#E4E5E5`，`x=1279` 也是）。`QMainWindow::separator` 只负责 **dock 之间**的
   分隔，窗口外沿没有任何机制——所以“窗口被刷成白色”的观感其实是“卡片贴边 +
   卡片是白的”。
2. **间隙被 V1 的投影填满。** V1 用 blur 20 / α44，两侧投影在 8~17px 的间隙里
   叠起来，实测间隙中段只剩 `Δ3`、十几像素同色，看起来就是一条实心灰带
   （而且正好接近描边色 `#E4E5E5`）。
3. **阴影不收窄就会和 F 判据打架**：留白要画布、C 判据又要阴影带，两者只能在
   同一条边上共存于“影子只贴边 4~6px”的前提下。

## V3.2 改法

1. `src/app/panel_registry.cpp::insert()`：`window_->setContentsMargins(16, 0, 16, 0)`。
   * **为什么是 contentsMargins 而不是 QSS**：QSS 的 `padding`/`border` 只影响
     绘制盒，不会移动 `QMainWindowLayout`（实测布局不动）；`setContentsMargins`
     会让整个 layout 的几何缩进——实测卡片从 x=0 移到 x=16。
   * **为什么是 16 而不是 8**：C 判据要求卡片外沿有 4~6px 的阴影带，若留白只有
     8px，第一条非画布像素会落在 x≈5（<8），F1 必红。16px = 8px 设计留白
     （`@gap@`）+ 8px 让阴影带走完，窗口最外圈 8px 仍是纯画布（实测左 16 / 右 15）。
   * **为什么不加 separator**：`QMainWindow::separator` 只存在于 dock 之间，
     窗口外沿没有 separator 的对应物（而且既有 selftest 断言 separator == 8px，
     不能改宽）。
2. `makeCardShadow()`（panel_registry）与 `editor_area.cpp` 的卡片投影：
   * 浅色：`blur 10 / offset (0,1) / α22`（V1 是 blur 20 / α44）；
   * 深色：`blur 12 / offset (0,1) / α220`（深色画布 `#131315` 上 Δ≥3 的阴影带
     需要更浓才够 4px；α150 + blur10 实测只有 3px）。
   * 顺手把注释里过期的“深色 120/255、浅色 32/255”改成真实参数（复核方指出的
     陈旧注释）。
3. `probe.py` 新增 **F1/F2/F3**（实现见文件头 docstring）：
   * F1：四条窗口边上“第一个卡片像素（surface/描边色）”的距离必须 ≥8px；
     附报“第一个非画布像素”的距离（左下角有状态栏文字，它不是卡片，所以判据
     按卡片像素而不是任意非画布像素，这一点在 docstring 里写清）。
   * F2：横向扫描行 + 纵向扫描列各找“两侧都被非画布像素夹住”的最长画布段
     （Δ≤3），两者都要 ≥6px。
   * F3：在 F2 找到的那段间隙里采样不同色阶，要求 ≥3（排除实心填充）。

## V3.3 A–F 量化结果（六套主题，`probe.py --all --prefix v3-after-`）

`PASS: 0 failing check(s) over 6 image(s)`（完整输出 `probe-v3-all.txt`、`probe-v3.json`）

| 主题 | A 卡片内部 | B 描边 Δsurface/Δcanvas | C 阴影带 | D 按钮 | F1 卡边距 左/右/上/下 | F2 横向间隙 / 色阶 | F2 纵向间隙 / 色阶 |
|---|---|---|---|---|---|---|---|
| yunwu | Δ0 ×3 | 27 / 16 | 4px | 6/6 | 16 / 15 / 32 / 20 | 12px / 4 | 17px / 4 |
| xinghuang | Δ0 ×3 | 27 / 21 | 4px | 6/6 | 16 / 15 / 32 / 21 | 12px / 4 | 17px / 4 |
| bohe | Δ0 ×3 | 27 / 15 | 4px | 6/6 | 16 / 15 / 32 / 20 | 12px / 4 | 17px / 4 |
| qingkong | Δ0 ×3 | 27 / 18 | 4px | 6/6 | 16 / 15 / 32 / 21 | 12px / 4 | 17px / 4 |
| yaoshi（深） | Δ0 ×3 | 24 / 35（更亮） | 4px | 6/6 | 16 / 15 / 32 / 21 | 14px / 4 | 17px / 4 |
| shenye（深） | Δ0 ×3 | 25 / 35（更亮） | 5px | 6/6 | 16 / 15 / 32 / 21 | 13px / 6 | 16px / 6 |

补充实测（yunwu，`v3-after-yunwu.png`，y=300 行）：

* 窗口左沿 `x=0..9` 全是 `#F4F4F2`，阴影从 x=10 起，描边在 x=16，卡片从 x=17 起；
* 横向间隙 `x=586..603`（18px）：描边 → 6px 渐变 → **12px 连续 canvas**（`#F4F4F2`）
  → 6px 渐变 → 描边；
* 卡片底边（x=300）：描边在 y=797，阴影带 y=798..801，y=805 起回到 canvas。

## V3.4 前后对照

| | V1（`after-yunwu.png`） | V3（`v3-after-yunwu.png`） |
|---|---|---|
| 窗口左沿 | `x=0` 就是卡片描边，无留白 | `x=0..9` 画布，卡片描边在 `x=16` |
| 卡片间隙 | 18px 全是阴影渐变，中段只有 Δ3 | 中段 12px 纯 canvas（Δ0），两侧各 6px 渐变 |
| 阴影 | blur 20 / α44（把间隙填成灰带） | blur 10 / α22（贴着卡片 4~6px） |
| F1/F2/F3 | 未过（F1 左/右=0，F2 1px 单色） | 全过 |

三张关键区间截图（4× 最近邻放大，yunwu）：

* `v3-region-outer-edge.png`：窗口左沿 → 画布 → 阴影 → 1px 描边 → 卡片；
* `v3-region-gap-horizontal.png`：两张卡片之间的横向间隙（中间是连续画布）；
* `v3-region-gap-vertical.png`：卡片底边 → 阴影 → 状态栏画布。

## V3.5 仍不确定 / 取舍

* **C 判据只剩 4px 余量**：浅色四套与 yaoshi 的阴影带正好 4px（要求 ≥4），
  shenye 5px。再收阴影就会打红 C——如果后续还要“更柔”，`probe.py` 的 C 判据
  与设计需要一起重新定标。
* **F1 的 8px 口径按“卡片像素”实现**：窗口最外圈 8px 里可能有菜单文字/状态栏
  文字（例如左下 `Project: no project` 从 y=812 起，距下沿 5px），它们不是卡片。
  判据因此取“第一个卡片像素 ≥8px”，并附报“第一个非画布像素”（左 14 / 右 13 /
  上 31 / 下 5）。若复核方要求“任意非画布像素也不行”，状态栏/菜单栏必须整体
  内缩，那是另一个布局改动。
* **纵向间隙不是“两卡之间”**：当前布局没有纵向相邻的主卡片，F2 的纵向扫描找到
  的是“工具栏 ↔ 卡片顶边”和“卡片底边 ↔ 状态栏”这两条画布带（16~17px）；
  报告如实列出，没有把它说成两卡之间的间隙。
* **16px 外沿比任务书说的 8px 宽**：多出的 8px 是给阴影带的（见 V3.2），
  视觉上与卡片之间 17~18px 的间隙一致；若要严格 8px，需要同时把 C 判据改小。
* 对话框/弹层仍未截真图（与 V1 的 §4.6 同一条限制）。

## V3.6 构建与测试（gate 跑在最终源码上）

命令：`bash scripts/build-qt.sh`（完整日志 `gate-v3.log`），**exit=0**：

```
62/62 Test #62: vedit-ui-scale-restart .........................   Passed    0.35 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  89.22 sec
M0-SMOKE ok=1 platform=xcb ... window=1 editor=1 document=1 definitions=451 themes=30
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke)
       + xcb/software-rendering smoke inside vedit-qt6-build:local
```

* ctest **62/62**；既有测试/门禁零改动（`git status` 只有 V1/V3 的 5 个源文件 +
  本证据目录；`ui_theme_selftest.cpp` 的 `Window==canvas` 断言原样）。
* 六套 V3 截图取自同一个最终二进制（先构建、后抓图；抓图之后源码未再改动）。

## V3.7 V3 证据清单

| 文件 | 内容 |
|---|---|
| `v3-after-<theme>.png` ×6 | V3 六套主题真实运行截图 |
| `probe-v3-all.txt` / `probe-v3.json` | `probe.py --all --prefix v3-after-` 的 A–F 完整结果 |
| `v3-region-outer-edge.png` | 关键区间①：窗口左沿 → 画布 → 阴影 → 描边 → 卡片（4×） |
| `v3-region-gap-horizontal.png` | 关键区间②：两张卡片之间的横向间隙（中间连续画布，4×） |
| `v3-region-gap-vertical.png` | 关键区间③：卡片底边 → 阴影 → 状态栏画布（4×） |
| `v3-window-yunwu.png` | V3 整窗（云雾白） |
| `gate-v3.log` | V3 的 `build-qt.sh` 完整输出 |
