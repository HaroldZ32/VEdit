# VERDICT-V2 — V 系列视觉修复与交付 AppImage 独立复核（Reviewer）

评估：V 系列在**无项目态的六套主题**上确实把用户三条实机诉求做出来了——四个面板卡体的主导色
精确等于 `surface`（Δ0、占比 1.0），卡片描边与 `surface` 的亮度差 24.0–26.3（与 `canvas` 10.6–34.1），
左/右/底阴影 4–6px，11 个工具栏按钮全部有 `borderStrong` 轮廓（浅色 Δ48.0–48.2、深色 44.4–44.7）且
`corner_cut=4`，右栏 Module Tree↔Interface↔Outline 的纵向连续 canvas 为 10px（浅色/曜石黑）/8px
（深夜蓝），横向 18px/16px，六套的 F1/F2/F3 按我的口径都过；交付 AppImage（sha256
`3cfd24f0…dadb`）的包内二进制也带 `borderStrong`/`veditDockCard`，包内实跑同样通过这些测量。
**但项目态左栏的 `Project` 面板完全绕过了卡片管线**：卡体 25.1% 是 canvas（A 的 surface 占比仅
0.645，低于 0.9），左右/底/上没有任何描边或阴影（B/C 全败），Project↔Files 的 6px 缝也不是两张
“卡片”之间的缝；交付 AppImage 同样继承这个缺口（项目态 0.671/0.254）。此外 C 的“四边”按冻结
文本没有全过（left/r1 上边 0px、曜石黑编辑器上边 2px<3），而报告把这些都当作“上边整体豁免”并
宣称 A–F 全过；船长自测的“纵向 10px”“按钮 Δ48–55”也各有不成立的子项。因此**不能 approve**。

风险级：**High**（核心六主题/AppImage 的视觉修复成立，但项目态一个主面板不满足需求 1/2，且冻结
C 的顶部口径需要船长裁定或修复）。

---

## 0. 我做了什么（不采信船长自测）

* 冻结判据以 `.mpd/plans/ui-mosaic/CONTRACT.md` **§5** 为准（A–F、C 的上边豁免、横向 8px/纵向 16px
  间隙语义）；测试与文档先于 diff 读。
* **自写探针** `evidence/ui-mosaic/review-visual-v2/analyze_v2.py`：从 `src/app/product_theme.cpp`
  的 `makeTheme()` 独立解析六套主题，按 `product_theme.cpp` 的同一混合公式复算 border/borderStrong；
  不读取、不运行 `visual-fix/probe.py`，也不复用 `review-visual/analyze.py`。输出 `probe-v2.json` /
  `probe-v2.txt`，并对船长提交的 `screenshots/after-*.png` 再跑一次（`probe-committed.txt`，结论一致）。
* 自建容器 `rv2-shot`、显示 `:97`、socket `.mpd/x11-rv2`，六套主题各抓 1680×1050（窗口 1280×820、
  无 WM/合成器、1x、xcb）；另抓项目态（yunwu/yaoshi）与交互态（yunwu/shenye：菜单、终端、首选项、
  工具栏/按钮 hover/pressed）。脚本 `capture_v2.sh` / `driver_v2.py`；证据在 `shots/`。
* 交付物：`APPIMAGE_EXTRACT_AND_RUN=1` 在容器 `rv2-appimage`/`rv2-appimage-proj`、显示 `:98`/`:96`
  跑 sha256 `3cfd24f0…dadb` 的 AppImage（yunwu/shenye 默认态 + yunwu 项目态）；`--appimage-extract`
  核对包内 `bin/vd-app` sha256 与 `strings`；脚本 `appimage_v2.sh` / `appimage_project.sh`。
* 自跑 `bash scripts/build-qt.sh`（后台）：exit 0，ctest **62/62**（90.06s），xcb smoke `M0-SMOKE ok=1`；
  日志 `build-qt-v2.log`。`git diff dc2b190~1..dc2b190 --stat` 与测试/门禁 diff 逐项核对。
* 六套主题的量化结果（源构建，1280×820 窗口；A 为四个面板卡体，编辑器视口单列）：

| 主题 | A（panel） | B Δsurface/Δcanvas | C 左/右/上/底 | C 编辑器上 | D 按钮数/通过/Δ | E 深色更亮 | F2 横/横/纵/纵 | F3 最低 |
|---|---|---|---|---|---|---|---|---|
| yunwu | Δ0, share 1.0 | 26.3 / 15.1 | 4/4/**0**/5 | 3 | 11/11/48.2 | — | 18/18/10/10 | 7 |
| xinghuang | Δ0, 1.0 | 26.2 / 15.5 | 4/4/**0**/5 | 3 | 11/11/48.0 | — | 18/18/10/10 | 7 |
| bohe | Δ0, 1.0 | 26.3 / 12.5 | 4/4/**0**/5 | 3 | 11/11/48.2 | — | 18/18/10/10 | 7 |
| qingkong | Δ0, 1.0 | 26.3 / 10.6 | 4/4/**0**/5 | 3 | 11/11/48.2 | — | 18/18/10/10 | 7 |
| yaoshi | Δ0, 1.0 | 24.0 / 34.1 | 4/4/**0**/5 | **2 FAIL** | 11/11/44.7 | 29.3→53.3 | 18/18/10/10 | 6 |
| shenye | Δ0, 1.0 | 24.2 / 32.9 | 5/5/**0**/6 | 4 | 11/11/44.4 | 30.0→54.2 | 16/16/**8/8** | 9 |

（C 的 r2/r3 上边在浅色四套是 3px、曜石黑 2px、深夜蓝 4px；表中“左/右/上/底”取左卡，r1 上边同为 0。
A 的编辑器视口不纳入 A：xinghuang 是 Solarized Light `(253,246,227)` Δsurface19、深色是 Breeze Dark
`(35,38,41)` Δsurface9/13，按 REQUIREMENTS A8 由语法主题决定，见 declined。）

### 与船长自测的逐条对照

| 船长自测 | 我的复测 | 结论 |
|---|---|---|
| 外圈 8px 卡片像素 = 0 | 六套 card_pixels **0**；环内非 canvas 是菜单字形（如 (39,5)）与状态栏文字/右下 handle（如 (1252,818)） | 成立（例外必须列出；见下） |
| 纵向连续画布 = 10px | 浅色/曜石黑 **10px**；深夜蓝 **8px**（源构建与 AppImage 一致） | 不成立于深夜蓝；仍 ≥6 |
| 横向 = 16–18px | 浅色 18px；深夜蓝 16px | 成立 |
| 下/左/右阴影 = 3–5px | 左/右 4–5px，底 5–6px；上边 left/r1 0px | 大体成立；深夜蓝底 6px、上边 0px |
| 描边 Δ = 24–26 | 24.0–26.3（Δsurface） | 成立 |
| 按钮轮廓 Δ = 48–55，均圆角 | 浅色 48.0–48.2；**深色 44.4–44.7**；corner_cut 4/4 | 深色不在 48–55；仍 ≥20、均圆角 |
| 构建 exit 0，ctest 62/62 | 我自跑 exit 0，62/62，90.06s | 成立 |
| 六套 A–F 全过 | 无项目态成立（C 上边按广义豁免）；**项目态 Project 面板 A/B/C 失败**；C strict 上边/yaoshi 编辑器顶 2px 未过 | **不成立**（见 H1/M1） |
| AppImage 含这些修复 | 默认态 A–F 通过；包内二进制含 `borderStrong`/`veditDockCard`；项目态 Project 缺陷同样在包内 | 含修复；也含 Project 缺口 |

F1 的例外（契约要求列出坐标+颜色）：顶部 y=5 的菜单字形，yunwu (39,5)=(241,214,149)、yaoshi
(39,5)=(21,50,120)、shenye (39,5)=(18,52,127)，六套共 132 个非 canvas 像素；底部状态栏文字/y=818
handle，yunwu (1252,818)=(248,248,247)、yaoshi (1252,818)=(102,102,104)、shenye (1252,818)=(100,104,112)，
六套 1154–1217 个非 canvas 像素。深色主题里状态栏抗锯齿可恰好等于 surface（yaoshi (1047,812)=
(30,30,32)、shenye (1046,812)=(22,32,44)），我按“状态栏例外 + 连续 ≥8px 才算法卡片像素”判定；
若按逐像素严格匹配，它们会被误报为卡片像素——这是 F1 报告必须解释的假阳性。

---

## 1. 分级发现

### Blocker

无。

### High

**H1（High）— 项目态左栏 `Project` 面板完全绕过卡片管线：A/B/C 失败，交付 AppImage 同样。**

* **What**：打开项目后，左栏顶部的 `Project` dock 在
  `src/workspace/project/project_integration.cpp::installProject` 里直接 `new QDockWidget` +
  `shell.addDockWidget`，没有经过 `src/app/panel_registry.cpp::PanelRegistry::insert`；因此没有
  `veditDockCard` 属性、没有 `WA_StyledBackground`、没有 `setContentsMargins(1,0,1,1)`、没有
  `applyCardShadow()`/`CardShadowWatcher`。标题栏（`QDockWidget::title`）有上/左/右 1px 描边，
  卡片体没有。
* **实测**（`shots/project-yunwu.png`、`shots/project-yaoshi.png`；窗口 1280×820）：
  * A：Project 面板体（x 17..585、y 102..567）主导色是 surface，但占比 **0.645**（yunwu/yaoshi），
    canvas 占 **0.251**，低于 A 的 ≥0.9；AppImage 项目态 **0.671 / 0.254**。
  * B：y=300/400/500 处 x=16/586 全是 canvas，没有描边。
  * C：左 x=15、右 x=587、下 y=568..573、上 y=73 全是 canvas，四边阴影 **0px**。
  * F2：Project↔Files 的缝只有 **6px** 连续 canvas（568..573，刚好卡线），且 Project 侧没有底边/
    阴影，所以不是“两张卡片之间”的边；F3 该缝只有 1 个色阶。
  * 交付 AppImage 项目态 `shots/appimage-project-yunwu.png`：同样 0.671/0.254，x=16/575 无侧边
    描边，缝 568..573=6px；`appimage-project.log` 确认项目已加载（`PROJECT-ROOT-FILES … files=11`）。
* **Why**：只要用户打开项目（主要工作态），需求 1（卡片与背景可区分）和需求 2（卡片有框边阴影）
  在这个左栏主面板上就不成立；CONTRACT §5 F2 明确把“项目态左栏的各卡”列为测量对象，而六张
  `after-*.png` 与 `appimage-verify.png` 都是无项目态，因此自测没有覆盖这个缺口。
* **修复方向（不自行改）**：让 Project dock 走 `PanelRegistry::insert`（注册 project spec/factory），
  或逐项复制同一套卡片处理（property/WA_StyledBackground/1px margins/applyCardShadow/watcher）；
  补项目态六主题截图与 A/B/C/F2 复测，并重打 AppImage。

### Medium

**M1（Medium）— C 的“四边”按冻结口径没有全过：left/r1 上边 0px，yaoshi 编辑器上边 2px。**

* **What**：六套主题里，左卡与右栏第一张卡（r1）的 `top` 阴影都是 **0px**；`r2/r3` 的 top 是
  3px（浅色四套）/2px（曜石黑）/4px（深夜蓝），编辑器卡 top 是 3px（浅色）/2px（曜石黑）/4px（深夜蓝）。
  CONTRACT §5 C 的字面豁免只写“纵向堆叠卡片的上边”（r2/r3）；左卡在无项目态不是纵向堆叠；
  曜石黑的编辑器卡不是纵向堆叠，2px<3 是明确的阈值失败。
* **Where**：`resources/themes/cards.qss`（QDockWidget/title）、`src/app/panel_registry.cpp::
  applyCardShadow()`、`src/app/editor_area.cpp::EditorArea::EditorArea` 的投影；`CONTRACT.md §5` C。
* **Why**：冻结判据要求四条边都测；报告（`evidence/ui-mosaic/README.md`“最终验收”段）把“上边”
  整体豁免并宣称 A–F 全过；§5 补充规则 2 要求口径调整时同时给出原口径与调整后口径，这里没有。
* **修复方向**：由船长把“工具栏/上方兄弟控件覆盖的所有 top 边”写进 §5（并逐主题列出实测值），
  或把 left/r1/editor 的 top 阴影做到 ≥3px；报告同时给出 strict/adjusted 两行。

**M2（Medium）— 项目态证据缺失，F2 的 project 项没有被写入方验证。**

* **What**：`evidence/ui-mosaic/screenshots/` 只有 `after-<theme>.png`（无项目态）；`appimage-verify.png`
  也是无项目态；没有 `project-*` 截图，也没有项目态 A/B/C/F2 数字。CONTRACT §5 F2 明确要求“项目态
  左栏的各卡”，因此“六套 A–F 全过”的证据不覆盖它；H1 的 Project 面板缺陷正是由此漏掉。
* **为什么重要**：这是一个主工作态的卡片验收缺口，不是审美问题；交付物证据也没有包内项目态。
* **修复方向**：补项目态六主题（至少 yunwu/yaoshi）截图、左栏卡对测量与 AppImage 项目态截图。

### Nit

**N1（Nit）— `QPushButton:pressed` 的 QSS 声明与渲染不一致（潜在轮廓丢失）。**

* **What**：`resources/themes/cards.qss` 的 `QPushButton:pressed`/`:checked`/`:focus` 都写
  `border-color: @accent@`，与 pressed 填充同色；若该声明生效，pressed 轮廓 Δ=0。实测 Qt 6.10.2 下
  Refresh 按钮 pressed 的左/右/上/下边仍是 `borderStrong`（yunwu 边 (206,207,208) vs 填充 (42,42,40)，
  dLum 153；shenye 边 (68,75,88) vs 填充 (81,139,227)，dLum 47.9），hover/pressed 轮廓当前保持；
  这是与 QSS 文本矛盾的潜在脆弱点。
* **修复方向**：pressed/checked 保持 `borderStrong` 或另一对比色，并加一条像素/样式自测。

**N2（Nit）— 船长自测数字有 3 处不成立（不影响阈值，但影响验收表可信度）。**

* “纵向连续画布 = 10px”：深夜蓝实测 **8px**（源构建与 AppImage 都是 8），浅色/曜石黑 10px；仍 ≥6。
* “按钮轮廓 Δ = 48–55”：深色两套实测 **44.4–44.7**（浅色 48.0–48.2）；仍 ≥20。
* “下/左/右阴影 = 3–5px”：深夜蓝底边 **6px**；左/右 4–5；上边 left/r1 0px（见 M1）。
* F1 的“外圈 8px 卡片像素=0”在深色主题下只有把状态栏字形当作例外才成立：yaoshi (1047,812)=
  (30,30,32)、shenye (1046,812)=(22,32,44) 与 surface 同色，属状态栏文字/抗锯齿；契约允许但必须
  列出坐标+颜色（本报告已列）。

---

## 2. 交付 AppImage 核查

* 路径 `packaging/runtime/out/VEdit-0.2.1-x86_64.AppImage`，138,795,512 字节；sha256
  **3cfd24f06dea154990d6ed08da7932231760923737e8286085f451766844dadb**，与
  `packaging/runtime/out/SHA256SUMS` 一致（与任务给的值一致）。
* `--appimage-extract` 后 `squashfs-root/bin/vd-app` sha256
  **2cb89d256ed859bba33cf6b490264fcff27c83ab658d806b0d0696ece6dfefe3**；`strings` 含
  `borderStrong`（2 次）与 `veditDockCard`。包内二进制与 `out/AppDir/bin/vd-app` 同 hash。
* 包内实跑（yunwu/shenye）：A 四个面板 Δ0/share1.0；B 26.3/15.1 与 24.2/32.9；C 左/右/底 4–6px、
  上边 left/r1 0px；D 11/11、浅色 48.2/深色 44.4；F1 0 卡片像素（例外为菜单/状态栏/句柄）；F2
  18/18/10/10（yunwu）与 16/16/8/8（shenye）；F3 7–11。状态栏显示 `Theme: Cloud White / Breeze
  Light`（shenye 为深色）。**结论：交付包里确实含 V 系列的卡片/描边/阴影/按钮轮廓修复。**
* 包内**项目态** `appimage-project-yunwu.png`：Project 面板 A/B/C 的失败与源构建一致（0.671/0.254、
  无侧边描边、缝 6px），说明 H1 同样进入交付物。
* AppImage 的左右卡 x 坐标与源构建略有差异（左卡 16..575、编辑器 600..909，源构建 16..586、
  611..906；字体/度量差异），我的包内测量用包内几何，不是回归。
* AppImage 未见自跑 `--selfcheck`/CentOS 7 探针；本报告只做视觉包内取证（prior evidence 声称的
  selfcheck/隔离不在本次复测范围）。

---

## 3. 副作用检查（用户重点 e）

* **对话框（Ctrl+,）**：`QDialog` 背景为 surface（yunwu 白、shenye 深蓝），文字与底部 OK/Cancel/Apply
  按钮可读；按钮有 `borderStrong` 轮廓。未见白底白字/黑底黑字。
* **菜单（File）**：surface 背景 + 描边，选中项 accent；菜单项文字可读。
* **终端（Ctrl+Alt+J）**：终端控件自身仍保留深底（用户看到的提示行 `(30,30,30)`、文字可读），
  **但**在这个 1680×1050/Xvfb 无 WM 的抓图里，终端控件只占面板底部约 14px，上面的大块是
  `TerminalPanel` 的 surface 底色（yunwu 白、shenye 深蓝）；这与 **dc2b190 之前**的
  `review-visual/shots/yunwu-ui2-terminal.png` 像素一致（同样的白区/14px 深带），不是 V 系列引入，
  但终端布局本身值得另开 lane 核查；我没有把它判为本次回归。
* **文件树/树行/滚动条**：项目态截图（`shots/project-yunwu.png`）中树行、目录图标、状态点、滚动条
  可辨；未发现因卡片底色铺满而不可读或错色。
* **Project 面板**：不是“底色铺满”的副作用，而是完全绕过卡片管线（H1）。

---

## 4. Declined to judge（明确不看/不判的项）

* 编辑器正文视口不是 surface（xinghuang Solarized Light、深色 Breeze Dark）——REQUIREMENTS A8 由
  KSyntaxHighlighting 决定，按设计，不判；只报数值（见上表注）。
* 深色主题描边/按钮强度的审美取舍（Δ24/44、无“刺眼”实测）——口味问题，不判。
* 若船长裁定“所有在工具栏/上方兄弟控件之下的 top 边都豁免”，left/r1/编辑器 top 的 0/2px 可降为
  已接受豁免——但这需要写进 §5，当前字面口径下见 M1。
* 横向卡片间距 24px vs 契约“横向 separator @gap@=8px”的语义——F2 的连续 canvas 18/16px 已过，
  separator 宽度没有直接断言，不判。
* 终端面板大块 surface/14px 终端控件的布局——dc2b190 之前已存在（V1 截图同像素），不是本次
  变更引入；仅记录。
* `QPushButton:pressed` 的 QSS `border-color: accent` 与当前渲染矛盾——当前渲染轮廓保持，见 N1，
  不按用户可见失败判。
* `ProductTheme` 新增 `borderStrong` 字段（CONTRACT §1.2 签名“固定”）——additive、无调用方破坏，
  与既有 `@textDisabled@` 额外令牌先例同类，不判。
* 对话框/菜单/树/滚动条的其它主题、所有键盘 focus/选中/滚动条 hover 状态——只抽了 yunwu/shenye，
  未测全六套；未判。
* F3 在 Project↔Files 缝只有 1 个色阶——该缝没有阴影（C 已失败），F3“仅对有阴影的间隙要求”，
  不单独判，归入 H1。
* 高 DPI/2x、真实 WM/合成器、CentOS 7 实机、长时阴影性能、AppImage 六套主题全部实跑——未测。
* 窗口最外 8px 的菜单字形/状态栏文字/右下 handle——契约允许的例外，已列坐标+颜色，不判。

---

## 5. 审阅边界

* **读**：`CONTRACT.md` §5、`REQUIREMENTS.md`、`cards.qss`、`product_theme.{h,cpp}`、
  `panel_registry.cpp`、`editor_area.cpp`、`ui_theme_selftest.cpp` 的 diff、
  `project_integration.cpp` 的 Project dock、`evidence/ui-mosaic/README.md`、`REPORT-V7.md`、
  以及上一轮 `review-visual/VERDICT-V1.md`。**未读**：全部 96 个 evidence 文件、其它 lane 的完整实现、
  CMake/gates 全文、KTextEditor 内部、AppImage 完整 squashfs 内容。
* **跑**：`bash scripts/build-qt.sh`（后台，exit 0，62/62，90.06s）；六套主题抓图（`rv2-shot`:97）；
  项目态 yunwu/yaoshi；交互 yunwu/shenye；AppImage 默认态 yunwu/shenye + 项目态 yunwu（`rv2-appimage`
  :98、`rv2-appimage-proj` :96）；`--appimage-extract`/sha256/strings；自写探针。
* **未跑**：高 DPI/`QT_SCALE_FACTOR`、真实 WM/合成器、CentOS 7 上的 AppImage 实机、AppImage 六套主题
  全跑、长时滚动/阴影性能、键盘 focus/树选中/滚动条 hover、全六套菜单/对话框/终端。
* 环境：Xvfb 1680×1050×24、xcb、软件渲染、无 WM/合成器、1x；源构建容器 `vedit-qt6-build:local`。
* 我只写了 `evidence/ui-mosaic/review-visual-v2/`；**我本人未改任何源码**。`build-qt.sh` 按其脚本
  固有行为覆盖了 `evidence/ffi-abi/logs/`、`evidence/m0-build/build-logs/` 等 tracked evidence
  （工作树因此有 modified 日志；不是我的结论产物）。**未重打 AppImage**；交付包保持船长 11:07 的
  3cfd24f。
* **并发事实（重要）**：复验快结束时（12:11–12:14），一个并行写入方在工作树里修改了
  `resources/themes/cards.qss`、`src/app/panel_registry.{h,cpp}`、
  `src/workspace/project/project_integration.cpp`（新增 `PanelRegistry::applyCardChrome` 并在
  `installProject` 里调用，方向正是 H1），并把 `target/qt6-build/src/app/vedit-app` 重建到 12:14。
  这些改动是 **uncommitted**，不在 `dc2b190`/`89f392f`，也不在我复验的 `target` 二进制/交付
  AppImage（3cfd24f）里；我的 build 日志是 11:19、六套截图 11:24、AppImage 实跑 11:38–11:50，均早
  于这些改动，因此本报告结论对应 **dc2b190 + 3cfd24f**。我没有评审这套未提交 V8 改动；若它就是
  H1 的修复，需要重新构建、重打 AppImage，并另做一次项目态 A/B/C 复验（并检查 `applyCardChrome`
  是否也覆盖了 Project 内容根的 `setContentsMargins(1,0,1,1)` 与 `WA_StyledBackground`）。
* 工作区事实：HEAD 是 **89f392f**（dc2b190 之后的 docs/evidence 提交），任务写的是 dc2b190；
  `git diff dc2b190~1..dc2b190 --stat` 为 **96 files changed, 16342 insertions(+), 16 deletions(-)**，
  其中源码 **6 个文件**（`cards.qss`、`editor_area.cpp`、`panel_registry.cpp`、`product_theme.{h,cpp}`、
  `ui_theme_selftest.cpp`），其余为 evidence/截图/日志；测试只改 `dock-separator-gap` 8→16 这一处，
  `build/`、`scripts/`、`tests/` 无改动。复验期间工作区出现 untracked 的
  `evidence/ui-mosaic/visual-fix/*v8*` 文件；不在 dc2b190/HEAD 内，未纳入本次判断。

---

## 6. Verdict

**request changes。**

一句话理由：无项目态的六套主题与交付 AppImage 中三条用户诉求的像素证据都成立，但项目态左栏
`Project` dock 未走卡片管线（A/B/C 全败，AppImage 同样），且 C 的顶部/曜石黑编辑器顶边按冻结口径
仍不达标、自测数字也有夸大——需要写入方修复并补项目态证据、重打 AppImage 后复验。

---

### 附：证据清单

| 文件 | 内容 |
|---|---|
| `analyze_v2.py` / `probe-v2.json` / `probe-v2.txt` | 自写 A–F 探针与六套源构建结果 |
| `probe-committed.txt` | 对船长 `screenshots/after-*.png` 的独立复测（与我的重拍结论一致） |
| `capture_v2.sh` / `driver_v2.py` / `shots/src-*.png` | 我用 `rv2-shot`:97 重拍的六套、项目态、交互态 |
| `shots/project-yunwu.png` / `project-yaoshi.png` | H1 的源构建项目态证据 |
| `project-probe.txt` / `appimage-project-probe.txt` | Project 面板 A/B/C/F2 的量化 |
| `appimage_v2.sh` / `appimage_project.sh` / `shots/appimage-*.png` | 交付 AppImage 默认/深色/项目态包内实拍 |
| `appimage-probe.txt` / `appimage-probe.json` | 包内 A–F 量化（yunwu/shenye） |
| `button-states-probe.txt` | hover/pressed 工具栏与 Refresh 按钮的轮廓像素 |
| `surfaces-probe.txt` | 终端/对话框/菜单底色 |
| `build-qt-v2.log` | 我自跑 build-qt.sh 的 62/62 与 exit 0 |
| `crops/` | 右栏/工具栏/项目态/终端等放大裁剪 |
