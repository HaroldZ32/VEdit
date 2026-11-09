# VERDICT-V1 — V1/V3 视觉修复独立复核（Reviewer）

评估：V1+V3 把“卡片是白的、背景也是白的”这个主缺陷修掉了——六套主题的卡片内部都精确等于
surface（Δ0），卡片描边是真描边（与 surface 差 24–27、与 canvas 差 15–35，且与派生令牌逐位
相同），工具栏 11 个按钮都有可见轮廓（亮度差 44–48）且仍是圆角，深色两套描边比 surface 更亮
（L 53–54 vs 29–30），窗口左右外沿留出了 14–16px 的 canvas，横向卡片间隙有 12–14px 连续画布。
但**冻结判据并没有全过**：纵向“两张相邻卡片之间”在四套浅色主题里只有 **2px** 连续 canvas
（要求 ≥6），F1 按冻结口径（外圈 8px 全是 canvas）在六套主题上**全部不成立**（顶部 5px 有菜单
字形、底部 1px 有窗口缩放手柄），C 只在卡片**底边**成立且恰好 4px（第 4 个像素 Δ=3 正好卡线），
F3 在曜石黑的第一条纵向间隙只有 2 个色阶。写入方报告里的 “A–F 六套全过” 只在它自己改弱的
F1 口径（“第一个卡片像素”）和“只测底边”的 C 口径下成立，不是冻结判据的结论。

风险级：**Medium-High**（用户三条实机诉求基本达成；剩余问题集中在纵向留白与判据/报告口径）。

---

## 0. 我做了什么（不采信写入方结论）

* 自建容器 `vra-shot`/`vra-ui2`、显示 `:93`、socket `.mpd/x11-vra`，每次抓图在同一次 bash 调用
  内重建 `/tmp/.X11-unix` 软链；六套主题各抓一张 1680×1050 全屏图（`shots/v3-<theme>.png`）。
* **自己写探针** `analyze.py`：主题 canvas/surface 从 `src/app/product_theme.cpp` 的 `makeTheme()`
  独立解析，border/borderStrong 用同一混合公式（surface→text 12%/22%）复算，逐像素判 A–F；
  没有读取或运行写入方的 `probe.py` 判定。输出 `probe-review.txt` / `probe-review.json`。
* 交互图：`input_driver.py` 用 XTest 先点击激活窗口再用键盘驱动——`yunwu` 与 `shenye` 各截
  菜单（File）、终端面板（Ctrl+Alt+J）、首选项对话框（Ctrl+,）三张实机图（`shots/*-ui2-*.png`）。
* 自跑 `bash scripts/build-qt.sh`（`build-qt.log`）与全量 ctest 三次（`ctest-full-rerun{1,2}.log`），
  另对失败用例单独复跑 5 次。
* 复跑：`python3 evidence/ui-mosaic/review-visual/analyze.py`。

### 冻结判据的量化结果（六套主题，窗口 1280×820）

| 主题 | A 三处卡片内部 | B 描边 Δsurface/Δcanvas | C 底边带 | D 按钮 | E 深色更亮 | F1 冻结口径 | F2 横向 | F2 纵向（卡对卡） | F3 横向/纵向 |
|---|---|---|---|---|---|---|---|---|---|
| yunwu | Δ0 ×3 | 27 / 16 | 4px | 11/11 | — | **FAIL**（上5 下1） | 12px OK | **2px FAIL** | OK / OK |
| xinghuang | Δ0 ×3 | 27 / 21 | 4px | 11/11 | — | **FAIL** | 12px OK | **2px FAIL** | OK / OK |
| bohe | Δ0 ×3 | 27 / 15 | 4px | 11/11 | — | **FAIL** | 12px OK | **2px FAIL** | OK / OK |
| qingkong | Δ0 ×3 | 27 / 18 | 4px | 11/11 | — | **FAIL** | 12px OK | **2px FAIL** | OK / OK |
| yaoshi | Δ0 ×3 | 24 / 35 | 4px | 11/11 | 是 | **FAIL** | 12–14px OK | 8px OK | OK / **2 色阶 FAIL** |
| shenye | Δ0 ×3 | 25 / 35 | 5px | 11/11 | 是 | **FAIL** | 10–13px OK | 8px OK | OK / OK |

C 的逐边实测（同一张图，同一张卡）：

| 主题 | 左 | 底 | 上 | 编辑器卡顶 | 编辑器卡底 | 右 | 右卡底 |
|---|---|---|---|---|---|---|---|
| yunwu / xinghuang / bohe / qingkong | 3px | **4px** | **0px** | 2px | **4px** | 3px | **4px** |
| yaoshi | 3px | **4px** | **0px** | 2px | **4px** | **0px** | **4px** |
| shenye | 4px | **5px** | **0px** | 3px | **5px** | **0px** | **5px** |

F1 的另一种口径（写入方用的“第一个**卡片**像素”）：左 16 / 右 16 / 上 30 / 下 11,7,13,7,2,7。
冻结口径的“第一个**非 canvas** 像素”：左/右无（≥14px），**上 5px**（菜单项字形，
如 (39,5)=(241,214,149)），**下 1px**（状态栏缩放手柄，(1252,818)）。

---

## 1. 分级发现

### Blocker

无。

### High

**H1 — 纵向相邻卡片之间没有 ≥6px 连续 canvas：四套浅色主题只有 2px，报告测错了卡片对。**
* 位置：`resources/themes/cards.qss`（`QDockWidget` 卡片 + `QMainWindow::separator` 的留白机制）
  与 `src/app/panel_registry.cpp::makeCardShadow` 的叠加；表现为右栏三张 dock（Module Tree /
  Interface / Outline）之间的 8px 缝。
* 证据（`shots/v3-yunwu.png`，x=1100 列）：卡底描边 y=360 → 232,235,238,240,**241,241**,240,239
  → 卡顶描边 y=369。**连续 canvas（Δ≤3）只有 y=365..366 两像素**；y=361..364 与 367..368 是
  V3 阴影。第二条缝 y=491..500 同样 2px。四套浅色主题完全一致；深色两套 8px（因为黑阴影在
  `#131315` 上不可见，见 H1 的 note）。
* 为什么重要：这是冻结 F2 的**唯一**纵向“两张相邻卡片”对（右栏三卡；项目态左栏还有 6 张卡，
  实测同款 2px/0px）。报告 §V3.3 的“纵向 17px / 4 色阶”测的是**工具栏↔卡片**，不是两卡之间；
  §V3.5 更写成“当前布局没有纵向相邻的主卡片”——**这句与画面不符**，右栏就是三张纵向相邻卡。
  一个“A–F 全过”的结论因此建立在不成立的纵向项上。
* 修复方向：让同一 dock area 内的堆叠卡之间出现真实的 6px 以上画布带（dock 间距/画布色分隔），
  或者把阴影再收窄到两侧各 ≤1px 使中段 canvas 达到 6px；同时把 F2 的纵向测量改到真正的
  相邻卡片对（x=1100 列或项目态左栏），而不是工具栏。

**H2 — F1 按冻结口径六套主题全部不成立；报告把一个被改弱的判据当作 PASS。**
* 位置：`resources/themes/cards.qss`（窗口外沿无任何留白机制，留白只来自
  `panel_registry.cpp::insert()` 的 `window_->setContentsMargins(16, 0, 16, 0)`）与状态栏/菜单栏布局。
* 证据：窗口最外 8px 环内，六套主题都有非 canvas 像素——顶部 y=5（菜单项字形/图标），
  底部 y=1（状态栏右端缩放手柄，(1252,818)=(242,245,250)），底部还有 1,157–1,188 个状态栏
  文字像素；左侧/右侧干净（0 个）。冻结口径写的是“第一处**非 canvas** 像素应出现在 ≥8px 处”，
  实测上 5px、下 1px → **FAIL**。
* 为什么重要：写入方的 F1 实现（`probe.py` 第 339 行起）把判据改成“第一个**卡片像素**”，并在
  §V3.5 披露了这一点；但 §V3.3 的表格与“A–F 全过”没有把“冻结口径实际是 FAIL”写出来。
  这不是卡边贴死窗口（那个问题确实修好了），而是判据与报告的缺口。
* 修复方向：要么由船长把 F1 明确改成“不得出现卡片像素（文字/手柄除外）”，并把该例外写进判据；
  要么给菜单栏/状态栏留出 8px 内边距。**不要**在冻结口径下继续声称 F1 PASS。

### Medium

**M1 — C 只在卡片**底边**成立，且刚好 4px（第 4 个像素 Δ=3 正好卡线）。**
* 位置：`panel_registry.cpp::makeCardShadow`（blur 10/12、offset (0,1)、浅色 α22 / 深色 α220）。
* 证据：见上表——底边 4px（浅色/yaoshi）、5px（shenye），但左 3–4px、右 0–3px、上 0px，
  编辑器卡顶 2–3px。底边第 4 个像素的 Δ 恰好等于 3（阈值本身），没有任何余量；浅色主题
  右沿 3px、深色主题右沿 0px（(1264,300)=(18,18,20) 与 canvas (19,19,21) 只差 1）。
* 为什么重要：船长特别问过“4px 是不是刚好卡线”——是。判据只在“阴影朝下”的语义下成立；
  若按“卡片四边外侧都要有 ≥4px 阴影带”理解，则只有底边过。更高 DPI 下 blur/α 会按
  devicePixelRatio 缩放，逻辑像素结果相同，但抽检阈值仍在边际，换缩放因子或 Qt 版本时
  可能掉到 3px。
* 修复方向：把阴影带做宽 1–2px（blur 12–14 或 α 略升），或把 C 明确为“卡片下方的投影带”。

**M2 — F3 在曜石黑的第一条纵向间隙只有 2 个色阶（无阴影，纯 canvas）。**
* 位置：同 H1 的纵向缝；深色画布 `#131315` 上的 α220 黑阴影几乎不可见。
* 证据：x=1100、y=361..368 的像素只有 (18,18,20) 与 (19,19,21) 两种；F3 要求 ≥3 个色阶。
* 判断：这与 F3 的**意图**（排除“实心灰带”）相反——这里是**没有**阴影的纯 canvas，不是实心
  色块；浅色主题的同一缝隙反而是“阴影几乎填满”（6 个色阶）。所以按字面 FAIL、按意图 PASS。
* 修复方向：F3 只针对浅色主题的间隙，或明确“纯 canvas 段不算实心填充”。

**M3 — 门禁不是确定性的 62/62：我的一次全量 ctest 是 61/62。**
* 位置：`vedit-module-tree-catalog-test` 的 `acceptance.root-change.interface-empty-state`
  （`src/workspace/module_tree/module_tree_catalog_test.cpp`）；我自跑 `scripts/build-qt.sh`
  时该断言失败（界面面板仍显示 “No module catalog yet…”，期望含 “No project open”），
  构建脚本因此 exit 8。
* 证据：`build-qt.log`（`43 - vedit-module-tree-catalog-test (Failed)`）；随后两次全量 ctest
  62/62（`ctest-full-rerun1.log`、`ctest-full-rerun2.log`），该用例单独复跑 5/5 通过。
* 判断：是 flake，不是 V1/V3 造成的（这条断言与颜色/布局无关），但“build-qt.sh exit 0 +
  62/62”在最终树上**不可稳定复现**，写入方的“62/62”只是其中一种结果。
* 修复方向：写入方或门禁 owner 定位该用例的时序依赖；报告应注明一次失败与复跑结果。

### Nit

* **N1 — 过期注释没有真正修好。** `src/app/panel_registry.cpp::makeCardShadow` 上方注释仍写
  “深色 150/255、浅色 22/255”，而代码是 `light ? 22 : 220`（150 vs 220）。报告 §V3.2 说“顺手把
  注释改成真实参数”，实际只把 120 改成 150，仍与代码不符（这条线索最初由上一轮复核提出）。
* **N2 — bohe 的 B 判据 Δcanvas 恰好 15（阈值本身），同样没有余量。**
* **N3 — 深色两套右沿阴影为 0px**（浅色 3px）：阴影在深色下只朝下/左可见，右沿几乎无。
  不影响“有阴影”的观感，但报告未披露这种不对称。

---

## 2. 副作用核查（对话框 / 菜单 / 终端 / 树行 / 滚动条）

用 XTest 真机驱动（先点击激活窗口再发键），浅色 `yunwu` 与深色 `shenye` 各一组：

* **首选项对话框（Ctrl+,）**：底色精确等于 surface（Δ0，两种主题），面板/表格/按钮自洽，
  没有露出画布补丁；文字可读。`shenye` 下没有发白或发灰的区块。
* **菜单（Alt+F / 点击 File）**：弹出层底色 = surface（87% 面积），条目文字与选中态可读；
  菜单不是卡片色误刷。
* **终端面板（Ctrl+Alt+J）**：终端保留自己的深底（yunwu (30,30,30)、shenye (12,18,28)），
  浅色主题下文字对比度 16.7:1，没有被 `QDockWidget QWidget` 的 surface 规则刷白——这条
  最容易出问题的副作用**没有发生**。
* **文件树行 / 滚动条**：项目态（`--project tests/fixtures/verilog-project`）截图里树行文字、
  目录图标、列表滚动条滑块与底色都可辨；未见不可读。
* **窗口其它部位**：菜单栏/工具栏/状态栏仍是 canvas（F1 左/右 0 个非画布像素；上/下的非画布
  像素是字形与缩放手柄，不是面板），没有出现“整窗被刷成卡片色”。

结论：V1 §4.1 那种“面板主体被刷成画布色”的同类副作用没有复发。

---

## 3. Declined to judge（明确不看/不判的项）

* 阴影强度/α 的审美取舍（α22/220、blur 10/12）——口味问题，不是缺陷。
* 16px 外沿 vs 任务书 8px 的视觉等价性——船长已给“机制自由”，只报像素。
* `productPalette()` 的 `QPalette::Window == canvas` 偏离（V1 §4.1）——超出本任务写入范围，
  且有既有断言，未判。
* `QGraphicsDropShadowEffect` 的长时性能——未压测，写入方已披露，我不判。
* 编辑器正文视口不是 surface（跟随 Breeze Light/Dark 语法主题）——按设计，不判。
* 真实 WM/合成器、2x DPI、CentOS 7 实机上的阴影表现——未测。
* 深色主题纵向间隙“没有阴影”本身（M2 只判 F3 字面）。
* 其余四套主题的弹出层/终端（只抽了浅/深各一套代表）。

---

## 4. 审阅边界

* 未读：完整 QSS/调色板契约、其它 lane 的实现；只读了改动涉及的 5 个源文件、REPORT、相关测试
  断言与 `probe.py` 的 F1 实现。
* 未跑：高 DPI/缩放（QT_SCALE_FACTOR）、长时滚动性能、真实合成器、AppImage 重打包、CentOS 7
  实机。截图环境为 Xvfb 1680×1050×24 + xcb + 软件渲染、无 WM/合成器，窗口 1280×820、1x。
* 门禁：自跑 build-qt.sh 1 次（ctest 61/62，exit 8）、全量 ctest 2 次复跑（62/62）、失败用例
  单跑 5 次（5/5）。flake 根因未定位。
* 只写了 `evidence/ui-mosaic/review-visual/`；重跑 build-qt.sh 会按其脚本固有行为覆盖
  `evidence/m0-build/build-logs/`、`evidence/ffi-abi/logs/`，那不是我的结论产物。

---

## 5. Verdict

**request changes。**

一句话理由：用户三条实机诉求（卡片可区分、可见描边+阴影、按钮轮廓）的像素证据都已成立，
但冻结判据里 **F2 纵向（四套浅色 2px < 6px）** 与 **F1 冻结口径（六套全部在 8px 内有非 canvas
像素）** 在最终树上不成立，C 也只在底边、恰好卡线，而报告以改弱后的口径宣称 “A–F 六套全过”——
需要写入方补纵向卡片留白并按冻结口径重写结论；若船长裁定接受“F1 只看卡片像素、C 只看底边”，
则本批可降为 approve，但该裁定必须写进判据而不是留在实现里。

---

### 附：证据清单

| 文件 | 内容 |
|---|---|
| `shots/v3-<theme>.png` ×6 | 我用 `vra-shot` 容器、显示 `:93` 重拍的六套主题 |
| `analyze.py` / `probe-review.txt` / `probe-review.json` | 自写探针与六套主题逐项结果 |
| `shots/<theme>-ui2-{menu-click,terminal,dialog}.png` | 菜单/终端/首选项真机交互图（yunwu、shenye） |
| `build-qt.log` | 我自跑的 build-qt.sh（ctest 61/62，exit 8） |
| `ctest-full-rerun1.log` / `ctest-full-rerun2.log` | 两次 62/62 全量复跑 |
| `capture-vra.sh` / `capture-ui-vra2.sh` / `input_driver.py` | 可重跑的抓图与输入驱动 |
