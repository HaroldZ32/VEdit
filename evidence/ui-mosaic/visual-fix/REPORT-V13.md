# V13 — Project 面板子区块边界

- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`
- 分支 / 基线：`feat/ui-mosaic` / `c804f55`
- 缺陷（用户实机反馈）：Project 卡片里的 Files / Include dirs / Defines /
  Diagnostics 等子单元没有边框、没有标题底色，整列文字与卡片白底融为一体。
- 改动范围：**只改 `resources/themes/cards.qss`**（末尾追加 40 行）。
  未改 C++、测试、`src/editor/**`，未新增依赖，未跑 lupdate，未动 dock 卡片、
  `@gap@`、separator、按钮、阴影、`@selection@`。

## 1. 控件结构清单（`src/workspace/project/project_panel.cpp`）

所有子区块的 objectName **原本就已存在且被测试引用**（例如
`rm9_status_dots_e2e.cpp` 查 `project-file-list`），因此本次**没有改 C++、
没有重命名**，QSS 直接按现有 id 选择。

层级：`QDockWidget#dock-project` → 内容根 `ProjectPanel`（`QWidget`，
`objectName=project-panel`，`veditDockCard=true`，`QVBoxLayout` margins 8、
spacing 6）→ 下列控件：

| 子区块 | 标题控件 | objectName | 内容控件 | objectName |
|---|---|---|---|---|
| Project 状态行 | `QLabel` | `project-summary` | — | — |
| 工具按钮 | — | — | `QPushButton` ×2 | `project-refresh`、`project-import-filelist` |
| 空态提示 | `QLabel` | `project-empty-hint` | — | — |
| Filelists（无 `.f` 时隐藏） | `QLabel` | `project-filelists-header` | `QListWidget` | `project-filelist-list` |
| **Files** | `QLabel` | `project-files-header` | `QListWidget` | `project-file-list` |
| **Include dirs (-I)** | `QLabel` | `project-include-header` | `QListWidget` | `project-include-dirs` |
| **Defines (-D)** | `QLabel` | `project-defines-header` | `QListWidget` | `project-defines` |
| Variables（无变量时隐藏） | `QLabel` | `project-variables-header` | `QListWidget` | `project-variables` |
| 编辑变量按钮 | — | — | `QPushButton` | `project-edit-variables` |
| **Diagnostics** | `QLabel` | `project-diagnostics-header` | `QListWidget` | `project-diagnostics` |
| 卡片状态行 | `QLabel` | `project-status` | — | — |

Fixture 中 Filelists / Variables 为空被 `setVisible(false)`，实际可见的正是验收
要求的四个子区块（Files、Include dirs、Defines、Diagnostics）。

## 2. 追加的 QSS

`resources/themes/cards.qss` 末尾，复用现有令牌（未新增颜色常量）：

```css
QWidget#project-panel QLabel#project-filelists-header,
QWidget#project-panel QLabel#project-files-header,
QWidget#project-panel QLabel#project-include-header,
QWidget#project-panel QLabel#project-defines-header,
QWidget#project-panel QLabel#project-variables-header,
QWidget#project-panel QLabel#project-diagnostics-header {
    background-color: @surfaceAlt@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 4px 8px;
    color: @text@;
    font-weight: 600;
}

QWidget#project-panel QListWidget#project-filelist-list,
QWidget#project-panel QListWidget#project-file-list,
QWidget#project-panel QListWidget#project-include-dirs,
QWidget#project-panel QListWidget#project-defines,
QWidget#project-panel QListWidget#project-variables,
QWidget#project-panel QListWidget#project-diagnostics {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 6px;
}
```

为什么带 `#project-panel` + 控件 id：卡片作用域已有
`QWidget[veditDockCard="true"] QListView { background: transparent; }`，不带 id
的 `QListView` 规则特异性不够，会把列表底复位透明；带两个 id 的选择器稳定
胜出，同时不把规则泄漏到文件树 / 模块树 / 其它面板。

间距：`ProjectPanel` 的 `QVBoxLayout` 原本就是 `spacing(6)`，QSS **没有**再加
`margin`，避免把 6px 叠成 12px。测量到的相邻子区块纯 surface 间隔正是 6px。

## 3. 真机取证

管线：`capture-v13.sh` + `driver_v13.py`，容器 `v13-shot`，Xvfb `:99`
（1680x1050x24），`QT_QPA_PLATFORM=xcb`、`QT_STYLE_OVERRIDE=fusion`、每个主题
独立 `--state-dir`（`settings.toml` 写 `uiTheme`），
`--project tests/fixtures/verilog-project`。截图裁出左上 Project dock：
crop `(0, 60, 620, 640)`（下表 y 都是 crop 坐标，全屏 y = crop y + 60）。

| 主题 | 截图 | sha256 |
|---|---|---|
| yunwu | `v13-project-yunwu.png` | `99f8622c77997c63f0ab98d7b703d5110d427d2d52b7899cef98312e79bc5e9d` |
| yaoshi | `v13-project-yaoshi.png` | `3bf0be3bcfb2214a79dcbf395ad25db66983c3359bded5d0f8bb61ef62ac448` |

完整画面：`v13-full-<theme>.png`；应用日志：`v13-app-<theme>.log`（无 QSS
解析告警 / 未替换令牌）；抓图日志：`v13-capture.log`。

测量：`measure-v13.py`，PIL 像素级判据，不依赖 OCR：

- 标题：先找 `surfaceAlt` 的 4-连通填充块（宽 ≥300、高 15..60），填充块上下
  各 1px 就是标题描边；再检查这一行是横跨标题宽度的连续描边（run）。
- 内容：标题之间找 `@border@` 的列/行——长竖线给出左右边框 x，横跨宽度的行
  给出上下边框 y；左右边框取连续竖线长度（run）证明不是孤立像素。
- 间隔：内容下边框到下一标题上边框之间，逐行检查整段宽度是否都是
  `@surface@`，统计连续纯 surface 行数。

### 3.1 亮色 yunwu（surface `#FFFFFF`，surfaceAlt `#F8F8F7`，border `#E4E5E5`）

| 子区块 | 标题上/下边界 y | 标题边界 ΔL | 标题底色 ΔL | 内容左/右边框 x | 内容上/下边框 y | 竖线 run | 内容边框 ΔL |
|---|---|---|---|---|---|---|---|
| Files | 104 / 127 | 26 / 26 | 7 | 25 / 577 | 134 / 189 | 44 / 44 | 26 / 26 |
| Include dirs | 196 / 219 | 26 / 26 | 7 | 25 / 577 | 226 / 288 | 51 / 51 | 26 / 26 |
| Defines | 295 / 318 | 26 / 26 | 7 | 25 / 577 | 325 / 387 | 51 / 51 | 26 / 26 |
| Diagnostics | 428 / 451 | 26 / 26 | 7 | 25 / 577 | 458 / 513 | 44 / 44 | 26 / 26 |

标题描边横跨 541px 连续（标题全宽 553px，差值为 6px 圆角）；标题底色与卡片
底色差 7（≥5），描边差 26（≥15）。

### 3.2 深色 yaoshi（surface `#1D1D20`，surfaceAlt `#171719`，border `#353538`）

| 子区块 | 标题上/下边界 y | 标题边界 ΔL | 标题底色 ΔL | 内容左/右边框 x | 内容上/下边框 y | 竖线 run | 内容边框 ΔL | 边框比底色亮 |
|---|---|---|---|---|---|---|---|---|
| Files | 104 / 127 | 24 / 24 | 6 | 25 / 577 | 134 / 189 | 45 / 45 | 24 / 24 | 是 |
| Include dirs | 196 / 219 | 24 / 24 | 6 | 25 / 577 | 226 / 288 | 52 / 52 | 24 / 24 | 是 |
| Defines | 295 / 318 | 24 / 24 | 6 | 25 / 577 | 325 / 387 | 52 / 52 | 24 / 24 | 是 |
| Diagnostics | 428 / 451 | 24 / 24 | 6 | 25 / 577 | 458 / 513 | 45 / 45 | 24 / 24 | 是 |

深色下 `border` 由 `surface` 向 `text` 混合 12% 得到，方向自动变亮：
`#353538` 的亮度高于 `#1D1D20`，实测 ΔL=24，边界线不是“看不见的暗线”。

### 3.3 四个子区块之间的纯 surface 间隔

| 相邻区块 | 间隔带（crop y） | 连续纯 surface 行 |
|---|---|---|
| Files → Include dirs | 190–195 | **6** |
| Include dirs → Defines | 289–294 | **6** |
| Defines → Diagnostics | 388–427（中间有 Edit Variables 按钮） | **6**（388–393） + 按钮 + **6**（422–427） |

两个主题完全一致。Defines 与 Diagnostics 之间还夹着 `Edit Variables…` 按钮
（源码顺序），因此整段 40px 不是纯色带；按钮上下各有 6px 纯 surface，四个
子区块没有连成一坨。

### 3.4 与修复前对照（同口径）

对照图：`evidence/ui-mosaic/screenshots/ctxmenu-files-yunwu.png`，裁同一
crop `(0,60,620,640)`，存为 `v13-before-project-yunwu.png`，跑同一个检测器：

| 指标 | 修复前 | 修复后（yunwu） |
|---|---|---|
| surfaceAlt 标题填充块数 | **0** | **4** |
| 内容区横跨面板宽度的描边行数（y 90–520） | **0** | **16**（4 标题 ×2 + 4 内容 ×2） |
| 标题边界 / 内容左右边框 | 检测不到 | 见 3.1 |

原始数据：`v13-measure-before.json`、`v13-measure-yunwu.json`、
`v13-measure-yaoshi.json`、`v13-measure.txt`。

## 4. 构建与测试

命令：`bash scripts/build-qt.sh`（后台运行，exit 0），日志
`evidence/ui-mosaic/visual-fix/v13-build.log`。

```
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  88.64 sec
...
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
```

`grep -c Passed` = 62；既有 flake（`vedit-rename-e2e`、`vedit-module-tree-catalog-test`）
本次未红。

## 5. 观察但未改动 / 未验证

- **观察（本次改动的副作用，未修）**：标题增加 4px 上下 padding 后，Project
  dock 在 fixture 的 795px 高窗口里总高度不变，四个列表各被压缩约 10–20px，
  最后一行出现半行裁切（Files/Include/Defines/Diagnostics 均可滚动）。这是固定
  高度 dock 的布局再分配，不是边界缺失；按任务要求没有改 dock 尺寸或列表
  最小高度。
- 只测了 `yunwu` / `yaoshi` 两套主题；另外四套（xinghuang、bohe、qingkong、
  shenye）未拍，规则使用同一组令牌，未单独取证。
- 只在 Xvfb+xcb、fusion 风格、100% 缩放下验证；真实硬件合成器、HiDPI、其它
  Qt style 未验证。
- Filelists / Variables 两块的标题与列表也加了同样的规则，但 fixture 中为空
  且隐藏，未截图验证。
