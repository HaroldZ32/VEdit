# UI-Mosaic V8 视觉修复报告

分支：`feat/ui-mosaic`，基线 HEAD `89f392f`。改动文件只有四个：
`resources/themes/cards.qss`、`src/app/panel_registry.h`、`src/app/panel_registry.cpp`、
`src/workspace/project/project_integration.cpp`。

## 1. 三处改法

### 1.1 深色主题只用描边，不加阴影

`src/app/panel_registry.cpp` · `applyCardShadow()`：

- 深浅判定改为应用调色板画布亮度：`0.299R + 0.587G + 0.114B < 128` 为深色
  （仍读 `QApplication::palette().color(QPalette::Window)`，不读 `owner->palette()`）。
- 深色分支直接 `owner->setGraphicsEffect(nullptr)` 并 return：既不创建新
  `QGraphicsDropShadowEffect`，也把已有的（含主题热切换前创建的）一并销毁。
- 浅色分支保持原参数不变：`blurRadius=12`、`offset(0,1)`、`color(0,0,0,22)`。
- `CardShadowWatcher` 的 `ApplicationPaletteChange` / `PaletteChange` 重算逻辑保留，
  重算仍走同一个 `applyCardShadow()`，所以深浅切换会加回/移除阴影。

### 1.2 Project 面板走同一套卡片外观

- `src/app/panel_registry.h`：新增 `public static void PanelRegistry::applyCardChrome(QDockWidget *dock);`
- `src/app/panel_registry.cpp`：把原先内联在 `insert()` 里的四件事（`veditDockCard`
  属性、`WA_StyledBackground`、内容边距 `(1,0,1,1)`、阴影 + `CardShadowWatcher`）
  抽进 `applyCardChrome()`；`insert()` 改为 `dock->setWidget(content); applyCardChrome(dock);`，
  行为与原来一致（属性设置时机从 `setWidget` 前挪到后，首次 polish 前属性已就位）。
- `src/workspace/project/project_integration.cpp`：`#include "panel_registry.h"`，
  在 `dock->setWidget(panel)` 之后、`addDockWidget` 之前调用
  `vedit::app::PanelRegistry::applyCardChrome(dock);`。没有复制第二份实现。

### 1.3 Files / Outline 卡片下方两角恢复圆角

`resources/themes/cards.qss`：给卡片体规则加 `padding: 2px;`，保留原有
background / border / border-radius：

- `QDockWidget > QWidget`（任务指定的规则）；
- `QWidget[veditDockCard="true"]`（`panel_registry.cpp` 给真实面板根控件打的属性，
  文件内既有探针注释说明 `QDockWidget > QWidget` 匹配不到真实面板根控件，所以真实
  卡片体的 2px 内缩必须落在这一条上；两条都加，行为不冲突）。

子控件（QTreeView / QTableView）的方形背景因此内缩 2px，父层
`border-bottom-*-radius` 的弧线露出来。未改控件结构、未包 QFrame。

## 2. 构建与测试

```bash
bash scripts/build-qt.sh
```

观测：后台任务结束 `EXIT:0`。ctest 汇总（`evidence/m0-build/build-logs/33-qt6-ctest.log`）：

```text
100% tests passed, 0 tests failed out of 62

Total Test time (real) =  89.54 sec
```

既有 flake（`vedit-rename-e2e` SIGPIPE / `vedit-module-tree-catalog-test`）本次未出现。
未改任何测试；`ui_theme_selftest.cpp` 的 `dock-separator-gap=16` 未动。

## 3. 截图与像素量化

命令：`bash evidence/ui-mosaic/screenshots/capture-themes.sh yunwu yaoshi`
（1680×1050，Xvfb + xcb；应用窗口 1276×816，Files 卡片 x=[17,585]、底边 y=797）。
量化脚本：`evidence/ui-mosaic/visual-fix/measure-v8.py`（10 行），原始输出
`measure-v8.txt`。

```text
after-yaoshi.png ... card x=[17,585] bottom=796 stroke_delta=24 shadow_down=0 left=0 right=0 corner_canvas=left:7 right:7
after-yunwu.png  ... card x=[17,585] bottom=796 stroke_delta=27 shadow_down=5 left=4 right=4 corner_canvas=left:0 right:0
```

### 3.1 深色 yaoshi

| 判据 | 要求 | 实测 |
|---|---|---|
| 卡片外侧与 canvas 差 ≥3 的连续像素（下 / 左 / 右） | 0–1px | **0 / 0 / 0 px** |
| 描边像素与 surface 亮度差 | ≥15 | **24**（描边 `#353538` vs surface `#1D1D20`） |
| Files 卡左下 / 右下角 12×12 内画布色像素 | ≥3 | **7 / 7** |

左下角 ASCII（`#`=canvas，`.`=surface，`o`=描边/其它；x 从 14 到 29）：

```text
786  # # o . . . . . . o o o . . . o
787  # # o . . . . . . o o o . . . o
788  # # o . . . . . . o o o . . . o
789  # # o . . . . . . o o o o o o o
790  # # # . . . . . . . o o o o o o
791  # # # . . . . . . . . . . . . .
792  # # # . . . . . . . . . . . . .
793  # # # . . . . . . . . . . . . .
794  # # # . . . . . . . . . . . . .
795  # # # . . . . . . . . . . . . .
796  # # # . . . . . . . . . . . . .
797  # # # # # # # # # # o o o o o o
```

底边 y=797 在 x=17..23 是画布而不是描边，正是 12px 圆角让出的弧形缺口（直角时这
7 个像素会是描边色）；右下角对称，同样 7 个画布像素。

### 3.2 浅色 yunwu

| 判据 | 要求 | 实测 |
|---|---|---|
| 卡片下 / 左 / 右阴影带（与 canvas 差 ≥3） | ≥3px | **5 / 4 / 4 px**（未回归） |
| 描边像素与 surface 亮度差 | ≥15 | **27** |

圆角同位置（12×12）严格"画布色"计数为 **0 / 0**：浅色仍保留阴影，弧形缺口正好
落在 α22 投影里，像素是阴影过渡色而非纯 canvas。几何圆角本身存在——同一条底边
y=797 的描边从 x=24 才开始，x=17..23 是阴影而非描边（ASCII 见
`corner-ascii-yunwu.txt`，7 个 `o`），与深色 7px 缺口一一对应。若把"卡片外
（非 surface）"计入，浅色同样是 7px 缺口；按任务给的严格画布色判据则只有深色主题
满足 ≥3。

### 3.3 Project 面板

`tests/fixtures/verilog-project` 可以打开（`--project`），截图：
`evidence/ui-mosaic/screenshots/project-yaoshi.png`、`project-yunwu.png`。
Project dock 出现在左上，标题条 + surface 卡片 + 1px 描边与 Files / Module Tree
一致；深色下其右边框外侧紧邻就是 canvas（x=586 描边 `#353538`，x=587 起
`#131315`），没有阴影带，与其它面板一致。

## 4. 未验证 / 说明

- 浅色主题的"角点 12×12 纯画布像素 ≥3"未达成（0），原因见 §3.2：阴影被要求保留，
  弧形缺口被阴影覆盖；几何圆角以"描边起点 x=24"和深色 7px 缺口为证。未为此改
  阴影参数或测试判据。
- 未改 `src/editor/**`、未新增依赖、未跑 lupdate、未写探针实验脚本（除 10 行
  `measure-v8.py`）。
- `QDockWidget > QWidget` 上的 `padding` 按任务要求保留，但真实面板根控件由
  `QWidget[veditDockCard="true"]` 命中；两处同值，故无行为分叉。
