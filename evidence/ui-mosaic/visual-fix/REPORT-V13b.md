# V13b — 修 V13 引入的“列表行被裁半截”，保留区块边界

- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`
- 分支 / 基线：`feat/ui-mosaic` / `c804f55`（V13 改动仍未提交）
- 缺陷：V13 给 Project 面板四个子区块标题加了 `padding: 4px 8px` + 1px 描边，
  固定高度 dock 里四个 `QListWidget` 各被压掉一截，末行只露半行
  （`v13-project-yunwu.png` 里 `SIMULATION` 只剩 3px 高的字顶）。

## 1. 改动（只改一条规则）

`resources/themes/cards.qss`，V13 新增的 Project 子区块标题规则（第 810 行）：

```diff
     border-radius: 6px;
-    padding: 4px 8px;
+    padding: 1px 8px;
     color: @text@;
     font-weight: 600;
```

- 只动这一条 `padding` 的上下分量（4px → 1px），左右 8px 不变；
  `background-color` / `border` / `border-radius` / `color` / `font-weight` 全部原样。
- 列表规则（V13 的第二条，内容描边盒）**未改**；未改行高、dock 尺寸、列表
  `min-height`、卡片/按钮/阴影/选中色/`@gap@`/separator，未改 C++、测试、依赖，
  未跑 lupdate。`git diff --stat -- resources src tests scripts` 只有
  `resources/themes/cards.qss`（54 行新增 = V13 原块 + 本行的值变化）。

标题总高从 **24px → 18px**（填充块 22px → 16px，上下描边各 1px 不变），
四个标题共归还 24px，布局把可用高度分给列表。

## 2. 验收 1：构建 + 全量 ctest

命令（后台）：`bash scripts/build-qt.sh`，日志
`evidence/ui-mosaic/visual-fix/v13b-build.log`。

```
62/62 Test #62: vedit-ui-scale-restart .........................   Passed    0.34 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  88.94 sec
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
EXIT=0
```

`grep -c Passed` = 62，`EXIT=0`。

## 3. 验收 2：新截图（Xvfb + `--project tests/fixtures/verilog-project`）

管线：`capture-v13b.sh` + `driver_v13b.py`，容器 `v13b-shot`，Xvfb `:99`
（1680x1050x24），`QT_QPA_PLATFORM=xcb`、`QT_STYLE_OVERRIDE=fusion`，每个主题
独立 `--state-dir`（`settings.toml` 写 `uiTheme`），裁同一个 Project dock
矩形 `(0, 60, 620, 640)`（与 V13 同口径）。日志 `v13b-capture.log`、
`v13b-app-<theme>.log`（无 QSS 解析告警、无未替换令牌）。

| 主题 | 截图 | sha256 |
|---|---|---|
| yunwu | `v13b-project-yunwu.png` | `285c66aaab92da8e76fdd1fe39841f9c9700596f1cbac8b532ca937642c333dd` |
| yaoshi | `v13b-project-yaoshi.png` | `a741c47dfeaa08e5e64f0e07db7d41f21db389b93ef7747907195d5c2a5c5dc5` |

## 4. 验收 3：末行不再被裁（可执行判据 + 2x 目检）

测量：`measure-v13b.py`（复用 `measure-v13.py` 的标题填充/描边/内容框检测，
再在内容框内按“与 surface 的亮度差 ≥20”逐行找文字带）。窗口内文字带等距
（`project_panel.cpp` 的 `setUniformItemSizes(true)`），带间距即行高。

| 主题 | 列表 | viewport 高 | 行高 | 余数 | 裁剪量 = 行高−余数 | 文字带 | 末带高度 / 中位高 | 末带底 vs viewport 底 |
|---|---|---|---|---|---|---|---|---|
| yunwu | Defines (-D) | **71** | **18** | **17** | **1px** | 4 条（322–330 / 340–348 / 358–366 / 376–384） | 9 / 9 | 384 ≤ 388 |
| yaoshi | Defines (-D) | **71** | **18** | **17** | **1px** | 同上 | 9 / 9 | 384 ≤ 388 |

- `viewport_height % row_height = 71 % 18 = 17`，最后一行（第 4 项）只差
  **1px < 2px** 就满高；4 条文字带高度完全相同（9px），最后一条
  `SIMULATION` 的整字形都在 viewport 内（376–384，viewport 到 388）。
- 对比 V13（同一检测器、同一屏）：Defines viewport 只有 **61px** →
  `61 % 18 = 7`，裁剪 **11px**，第 4 条文字带只剩 **3px** 高（384–386）——
  正是截图里“SIMULATION 被切掉一截”的形态。本次修复后该带恢复 9px 全高。
- 其它列表：Files 54px = 3×18 余 0，3 项完整；Include dirs 71px 余 17
  （`…/rtl/include`、`…/tb/include` 四行文字全在）；Diagnostics 54px 与其
  V13 前相同（见 §6）。
- 2x 目检图（NEAREST 放大，供人眼复核）：
  `v13b-defines-yunwu-2x.png`、`v13b-defines-yaoshi-2x.png`。
  **目检结论：`WIDTH=8`、`DEPTH=16`、`DEBUG`、`SIMULATION` 四行全部完整可见，
  `SIMULATION` 一行完整、字底未被裁。**

原始数据：`v13b-measure-yunwu.json`、`v13b-measure-yaoshi.json`、
`v13b-measure.txt`。

## 5. 验收 4：V13 的区块边界仍在

同一检测器（`@surfaceAlt@` 填充块、上下 1px `@border@` 描边）：

| 主题 | 子区块 | 标题上/下描边 y | 描边 ΔL（上/下） | 标题底色 ΔL | 内容框描边 ΔL | 相邻区块纯 surface 间隔 |
|---|---|---|---|---|---|---|
| yunwu | Files | 104 / 121 | 26 / 26 | 7 | 26 | 6 |
| yunwu | Include dirs | 190 / 207 | 26 / 26 | 7 | 26 | 6 |
| yunwu | Defines | 293 / 310 | 26 / 26 | 7 | 26 | 6 |
| yunwu | Diagnostics | 430 / 447 | 26 / 26 | 7 | 26 | — |
| yaoshi | Files | 104 / 121 | 24 / 24 | 6 | 24 | 6 |
| yaoshi | Include dirs | 190 / 207 | 24 / 24 | 6 | 24 | 6 |
| yaoshi | Defines | 293 / 310 | 24 / 24 | 6 | 24 | 6 |
| yaoshi | Diagnostics | 430 / 447 | 24 / 24 | 6 | 24 | — |

四个标题仍有描边（ΔL 26 / 24，要求 ≥15）与底色（ΔL 7 / 6，要求 ≥5），
内容区 1px 描边盒仍在，区块间 6px 纯 surface 间隔不变。边界没有丢。

## 6. 观察但未改动 / 未验证

- **Diagnostics 列表仍有文字被下边缘裁掉**：它的条目是长句换行（多行 item），
  viewport 54px 与 V13、以及 V13 之前的基线截图
  `v13-before-project-yunwu.png` 完全相同（第 3 条同样只露一半）。这是既有
  现象，不是本次 padding 修复引入的；按任务约束未改行高 / min-height / dock。
- 只拍 `yunwu` / `yaoshi`；另外四套主题未单独拍（同一组令牌）。
- 只在 Xvfb+xcb、fusion、100% 缩放下验证；HiDPI、其它 Qt style、真实硬件
  合成器未验证。
- Filelists / Variables 两块标题也命中同一规则，但 fixture 中为空且隐藏，
  未截图验证。
