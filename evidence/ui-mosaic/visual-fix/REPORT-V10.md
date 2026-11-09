# V10 — 浅色主题选中行近黑带修复

- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`
- 分支 / 基线：`feat/ui-mosaic` / `69ed535`
- 缺陷：云雾白（yunwu）下图中的选中项整行被 `@accent@`（`#2A2A28`）填满，
  在白色卡片上形成 22px 近黑带；深色曜石黑（yaoshi）同样把高亮强调色
  `#D5A027` 铺满整行，刺眼。
- 修复：选中项底色不再复用 accent，改为 "surface 与 accent 的淡混合"
  （浅色 14%，深色 28%），选中文字一律用 `@text@`。

## 1. 实现

### 1.1 新增字段

`src/app/product_theme.h` — `struct ProductTheme` 新增：

```cpp
QColor  selection;    // 选中项整行底色：surface 与 accent 的淡混合，不是 accent 本身
```

### 1.2 派生规则

`src/app/product_theme.cpp`：

```cpp
constexpr int kSelectionBlendPercentLight = 14;
constexpr int kSelectionBlendPercentDark = 28;
...
theme.selection = blendToward(
    theme.surface, theme.accent,
    dark ? kSelectionBlendPercentDark : kSelectionBlendPercentLight);
```

`renderStyleSheet()` 的令牌表新增 `{"@selection@", theme.selection.name()}`；
残留 `@` 检查保持原样（现有 62 条 ctest 里 `product.stylesheet.render`
对六套主题断言"非空、无残留令牌、含 accent"）。

六套主题的派生值（14% / 28% 规则，只有 yunwu/yaoshi 做了像素取证）：

| theme | surface | accent | selection | ΔL* vs surface |
|---|---|---|---|---|
| yunwu | `#FFFFFF` | `#2A2A28` | `#E1E1E1` | 10.5 |
| xinghuang | `#FFFDF6` | `#C87A2E` | `#F7EBDA` | 5.7 |
| bohe | `#FFFFFF` | `#3E9A63` | `#E4F1E9` | 6.0 |
| qingkong | `#FFFFFF` | `#4A7EF0` | `#E6EDFD` | 6.3 |
| yaoshi | `#1D1D20` | `#D5A027` | `#514222` | 17.9 |
| shenye | `#161F2E` | `#518BE3` | `#273D61` | 14.1 |

### 1.3 样式表覆盖的选择器

`resources/themes/cards.qss`：

- `QTreeView` / `QListView` / `QTableView` 基类的
  `selection-background-color` / `selection-color` → `@selection@` / `@text@`
  （item:selected 之外仍会用到基类选中色）。
- `QTreeView::item:selected` / `QListView::item:selected` /
  `QTableView::item:selected` → `background: @selection@; color: @text@;`
- `QMenu::item:selected` → `background: @selection@; color: @text@;`
- `QComboBox QAbstractItemView` 的
  `selection-background-color` / `selection-color` → `@selection@` / `@text@`
  （下拉列表的选中行同属"大面积填充"）。
- 卡片注释同步为 `selection / surfaceAlt`。

模板里没有 `:active` / `:!active` 变体，无需改动。

**有意未改**（小面积 accent 用法，按任务约束保留）：

- `QToolButton:pressed`、`QMenuBar::item:pressed`、`QPushButton:pressed/:checked`、
  `QCheckBox::indicator:checked` 的 accent 底（按钮/勾选小块）。
- `QLineEdit` / `QPlainTextEdit` / `QTextEdit` 的 `selection-background-color`
  仍是 accent：这是"选中文字"的标准反色语义，不是整行选中项；编辑器文字选择
  在 accent 底上仍用 accentText，未纳入本次"黑带"缺陷。
- 焦点描边、下划线、accent 文字等用法未动。

## 2. 真机取证（XTest 真实鼠标）

管线：`evidence/ui-mosaic/visual-fix/capture-v10.sh`
- 容器 `v10-shot`，Xvfb `:96`，`QT_QPA_PLATFORM=xcb`，1680x1050；
- 启动 `target/qt6-build/src/app/vedit-app --project tests/fixtures/verilog-project`；
- `driver_v10.py` 用 XTest 在 Files 文件树里**左键点 `expected` 行**（真实坐标
  125,712），移开指针后截图；再右键打开同一行的菜单，移动指针到首个菜单项截图。

测量：`measure-v10.py`（选中带按"该主题 selection 色的连续横向 run"自动定位，
不写死行号；亮度用 CIE L*，对比度用 WCAG）。

### 2.1 选中行

| 主题 | surface（实测） | 选中行（实测） | 行高 | L* surface → selection | **ΔL*** | 文字对比度 | 测量文本像素 |
|---|---|---|---|---|---|---|---|
| yunwu | `#FFFFFF` | `#E1E1E1` | 22px | 100.0 → 89.5 | **10.5** | **12.08**（规则色 `#1F2328`）/ 11.5（实测像素） | `#242631` |
| yaoshi | `#1D1D20` | `#514222` | 22px | 10.9 → 28.8 | **17.9** | **7.95**（规则色 `#E6E8EC`）/ 7.68（实测像素） | `#E2E5E2` |

两档都落在要求的 8–40 区间内；深色选中行与 accent（`#D5A027`，L*=69.1）
相差 40.3 个 L* 单位，明显不刺眼（不再是 accent 铺满整行）。

原始数据：`measure-v10-yunwu.json`、`measure-v10-yaoshi.json`。

### 2.2 修复前对照

`evidence/ui-mosaic/screenshots/filedialog-ctx-yunwu.png`，x=200 纵向扫描：

- `y=683..704` 是 22px 的 `#2A2A28`（L*=17.0）；
- 相对白色卡片 ΔL* = **83.0**，远高于 40 的上限 —— 即用户看到的"一坨黑"。
- 修复后同位置（yunwu 选中行 `#E1E1E1`）ΔL* = **10.5**。

### 2.3 右键菜单（浅色可读性）

- yunwu：菜单底色实测 `#FFFFFF`，首个高亮项 `#E1E1E1`
  （ΔL* 相对底面 10.5，文字对比度 12.08），不是黑条。
- yaoshi：菜单底色 `#1D1D20`，高亮项 `#514222`（ΔL* 17.9，对比度 7.95）。
- 截图：`shots-v10/yunwu-ctxmenu.png`、`yunwu-ctxmenu-hover.png`、
  `yaoshi-ctxmenu.png`、`yaoshi-ctxmenu-hover.png`。

## 3. 构建与测试

命令：`bash scripts/build-qt.sh`（后台运行，日志 `build-v10.log`）

```
100% tests passed, 0 tests failed out of 62

Total Test time (real) =  89.53 sec
...
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 ...
[qt6] PASS: Rust service layer + configure + build + ctest ... + xcb/software-rendering smoke
BUILD_EXIT=0
```

- 全量 ctest：**62/62**，exit 0。

## 4. 证据文件

```
evidence/ui-mosaic/visual-fix/
  REPORT-V10.md
  build-v10.log
  capture-v10.sh / capture-v10.log
  driver_v10.py
  measure-v10.py
  measure-v10-yunwu.json / measure-v10-yaoshi.json
  shots-v10/
    yunwu-tree-selected.png / yunwu-ctxmenu.png / yunwu-ctxmenu-hover.png
    yaoshi-tree-selected.png / yaoshi-ctxmenu.png / yaoshi-ctxmenu-hover.png
    app-yunwu.log / app-yaoshi.log
```

## 5. 已知边界

- 其余四套浅色主题按同一条 14% 规则派生（见 §1.2 表），本次只对验收指定的
  yunwu/yaoshi 做了像素级门禁；未测量的四个值来自同一实现公式。
- 文件打开对话框里的文件树与主窗口共用全局 QSS 选择器，因此同一修复生效；
  本次验收截图取自主窗口 Files 树（验收步骤指定的取证面）。
- 未改 `src/editor/**`、未新增依赖、未跑 lupdate、未改测试。
