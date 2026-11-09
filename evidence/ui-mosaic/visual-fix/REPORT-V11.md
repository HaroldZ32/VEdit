# V11 — 弹层加固（文件对话框右键菜单整块纯黑）

- 工作区：`/home/haroldzhao/MyProj/DshProj/VEdit`
- 分支 / 基线：`feat/ui-mosaic` / `bdbb9b0`
- 缺陷（用户实机反馈，含截图）：文件对话框里右键菜单整块纯黑，纯黑底 + 1px
  浅色圆角边 + 浅灰字。这是**弹层背景没有被真正画出来**的形态。
- 根因模型：Qt 在弹层声明 `border-radius` 时会启用半透明窗口
  （`WA_TranslucentBackground`）；背景填充一旦在某个平台 / 合成器 / 系统调色板
  组合下丢失，露出的就是黑色底。
- 改动范围：**只改 `resources/themes/cards.qss`**（末尾追加一组覆盖规则）。
  未改 C++、测试、`src/editor/**`，未新增依赖，未跑 lupdate。

## 1. 追加的规则

`resources/themes/cards.qss` 末尾（靠后规则覆盖前面的圆角弹层）：

| 选择器 | 规则 |
|---|---|
| `QMenu` | `background-color: @surface@; color: @text@; border: 1px solid @border@; border-radius: 0px;` |
| `QMenu::item` | `padding: 4px 18px; color: @text@; background: transparent;` |
| `QMenu::item:selected` | `background-color: @selection@; color: @text@;` |
| `QMenu::separator` | `height: 1px; background: @border@; margin: 4px 8px;` |
| `QMenu::icon` | `padding-left: 4px;` |
| `QToolTip` | `background-color: @surface@; color: @text@; border: 1px solid @border@;` |
| `QComboBox QAbstractItemView` | `background-color: @surface@; color: @text@; selection-background-color: @selection@; selection-color: @text@; border: 1px solid @border@;` |
| `QFileDialog QTreeView, QListView, QComboBox, QLineEdit, QLabel` | `background-color: @surface@; color: @text@;` |

要点：

- 菜单**方角**（`border-radius: 0px`）——方角弹层不会走半透明陷阱。
- 显式写 `background-color`（不是只写 `background`），避免背景填充被跳过。
- `QFileDialog QLineEdit` **只补底色、不复位 padding**，保持全局输入框观感。
- 未触碰已验收内容：dock 卡片、`@gap@`、`separator` 高度 16、按钮 / 描边 /
  阴影、`@selection@` 的定义。

## 2. 真机取证（Xvfb + XTest 真实鼠标）

管线：`capture-v11.sh` + `driver_v11.py`，容器 `v11-shot`，Xvfb `:97`
（1680x1050x24），`QT_QPA_PLATFORM=xcb`，主题 `yunwu`（surface `#FFFFFF`）。
每种风格都重新启动一次产品：

```
DISPLAY=:97 QT_QPA_PLATFORM=xcb QT_STYLE_OVERRIDE=<fusion|windows> \
  ./target/qt6-build/src/app/vedit-app --project tests/fixtures/verilog-project
```

步骤：XTest 左键点工具栏 `Open Folder…`（243,44）→ 在文件列表内右键
（700,420）→ 截图；关菜单 / 关对话框后点应用自己的 `View` 菜单（125,10）→
截图。两种风格下菜单都**正常弹出**，无“右键无响应”。

截图：

| 风格 | 右键菜单 | View 菜单 |
|---|---|---|
| fusion | `v11-menu-fusion.png` | `v11-view-fusion.png` |
| windows | `v11-menu-windows.png` | `v11-view-windows.png` |

（另附 `v11-dialog-<style>.png` 作为右键前对照，`v11-closed-<style>.png` 证明
对话框已关闭，`v11-crop-*.png` 是测量区域裁图。）

测量：`measure-v11.py`。弹层区域由前后两帧的**变化像素连通域**定位（右键菜单
取包含点击点 700,420 的连通域；View 菜单取整块变化包围盒），不写死菜单坐标。
判定：区域内纯黑像素（`R+G+B < 30`）必须为 0；主导色必须是 surface ±2。

| 风格 | 弹层 | 包围盒 | 面积 | 纯黑像素 | 主导色 | 占比 | Δ vs surface |
|---|---|---|---|---|---|---|---|
| fusion | 文件对话框右键菜单 | 700,420–870,474 | 9180 | **0** | `#FFFFFF` | 82.4% | **0** |
| fusion | View 菜单 | 119,0–472,514 | 181442 | **0** | `#FFFFFF` | 82.6% | **0** |
| windows | 文件对话框右键菜单 | 700,420–869,474 | 9126 | **0** | `#FFFFFF` | 82.3% | **0** |
| windows | View 菜单 | 101,0–454,514 | 181442 | **0** | `#FFFFFF` | 82.6% | **0** |

原始数据：`v11-measure.json`；抓图日志：`v11-capture.log`；应用日志：
`v11-app-fusion.log`、`v11-app-windows.log`（无样式表解析告警、无未替换令牌）。

方角已生效：两张菜单裁图的四个角像素都是 `#E4E5E5`（1px `@border@` 边），
没有圆角切角 / 透明角。

## 3. 构建与测试

命令：`bash scripts/build-qt.sh`（后台运行，exit 0）

```
62/62 Test #62: vedit-ui-scale-restart .........................   Passed    0.35 sec

100% tests passed, 0 tests failed out of 62

Total Test time (real) =  88.69 sec
...
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke inside vedit-qt6-build:local
```

构建日志：`evidence/m0-build/build-logs/32-qt6-build.log`、
`evidence/m0-build/build-logs/33-qt6-ctest.log`。

## 4. 未能复现的说明与未验证项

- **未能复现**：无头 Xvfb 没有合成器，fusion 与 windows 两种风格下
  （改动前后的产品都能正常画出弹层背景）**均未出现纯黑底**。因此这是
  **防御性加固**：本次取证能证明“当前构建中弹层是方角、显式 surface 填充、
  弹层区域内纯黑像素为 0”，**不能证明**用户实机那条“半透明窗口 + 背景丢失”
  的合成器路径已被直接复现与修复。
- **未验证**：`QToolTip` 与 `QComboBox` 下拉未在 GUI 中实拍（规则随样式表
  加载，语法层面由 62 条 ctest 的“无残留令牌”断言覆盖）；`QFileDialog` 内部
  控件未逐类取样；深色主题 / HiDPI 未拍；真实硬件合成器未验证。

## 5. 证据清单与边界

- 产品改动：`resources/themes/cards.qss`（+64 行，仅末尾追加）。
- 取证工具：`capture-v11.sh`、`driver_v11.py`、`measure-v11.py`。
- 截图哈希：
  - `v11-menu-fusion.png` `483e9e0e7bfe9a575fbbcc1c660bbe78c0e1a9e1d0d5c96f4fb9e25b61203f72`
  - `v11-menu-windows.png` `7d9a1a499c73c8d6ed99740b3afe9cda948fd3fe86fc31d229fe184b7c057d25`
  - `v11-view-fusion.png` `800c991c4a9b89e1aeba5aba3d070a18a3837d7a4d812a805f3c8c66926be3b3`
  - `v11-view-windows.png` `bc086810c6379910631a03a3d04c6430f65fdb03c04dda6337edbd0d97b747b1`
- 构建命令会重写 `evidence/m0-build/**` 与 `evidence/ffi-abi/**` 的日志（这些
  文件在本次任务开始前已是修改态），不是本任务的产品改动。
