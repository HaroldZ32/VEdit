# UI-Mosaic · R1 复验结论（VERDICT-R1）

- 判据：`.mpd/plans/ui-mosaic/REQUIREMENTS.md`（A1–A7）、`.mpd/plans/ui-mosaic/CONTRACT.md`（含 2026-11-08 22:24 的 §2.3 重新冻结与 §3.9 裁决）、上一轮 `evidence/ui-mosaic/review/VERDICT.md`（F1–F8）、本轮修复报告 `evidence/ui-mosaic/fix-r1/REPORT.md`。
- 验证者：reviewer-3（只写 `evidence/ui-mosaic/review-r1/`；产品代码、测试、门禁、翻译、资产一律未改）。
- **复验基线（钉死）**：T1 完成时的树 —— `src/app/app_shell.cpp` sha `5dee40c2…`、`app_shell.h` `42f11b53…`、`product_theme.cpp` `1cf7c0f1…`、`product_theme.h` `cfa9e5ad…`、`main.cpp` `7bf7c33f…`、`settings_panel.cpp` `cd60f42d…`、`translations/vedit_zh_CN.ts` `49d766c1…`、`packaging/runtime/verify-f2-isolation.sh` `783c02de…`、`session_store.h` `f7b855f0…`（未变）；三个入口的 GUI 探针跑在 22:42:43 构建的 `vedit-app` 上（该二进制只含 T1 的 `app_shell.cpp`）。
- **工作区在复验期间被并行车道推进**：T3（F2）于 23:17:46 改 `src/app/app_shell.cpp`；T4（R3，settings/archive mtime 仲裁）随后继续修改（复验结束时 `app_shell.cpp` 已是 `384b8d3b…`）。本结论只对上述 T1 基线成立；**最终集成树必须重跑三个入口探针**。
- 复现：本目录 `logs/` 下的脚本与原始日志；所有命令都在仓库根执行，构建/运行在 pinned 镜像 `vedit-qt6-build:local`。

## 评估段（风险级：Medium）

T1 的 F1 与 M1–M4 全部用我自己的实验证实修好，没有引入新的 Blocker/High。F1 用**真实鼠标/键盘**驱动了三个入口（宿主 `libXtst` → Xvfb 里的 xcb 窗口；弹窗位置由 X 树实时读取，不写死坐标）：视图菜单、工具栏按钮、首选项下拉任一次切换都会把 `uiTheme` 立即写进 `settings.toml`；随后打开首选项不再把主题弹回旧值（同一次切换前后主窗口画布颜色逐像素一致，首选项 `Interface theme` 显示当前值）；退出时存档与设置一致；重启读回最后选择；**连续两次不同入口**（视图→晴空蓝，工具栏→曜石黑）也是最后一次胜出（`uiTheme=yaoshi`、存档 `yaoshi`、重启 `theme=yaoshi`，语法主题正确配对 `Breeze Dark`）。M1 英文界面下状态栏/主题子菜单/首选项下拉全部显示英文名（`Sky Blue`、`Obsidian`…），zh_CN 下仍是中文（`晴空蓝`），`lrelease` **1278 finished / 0 unfinished / 0 untranslated**，`.ts` 无 lupdate 指纹（0 条绝对路径、0 条 unfinished、新增 11 条恰为 6+3+1+1、`product_theme` 6 条无 `<location>`），属纯新增。M2 在「无效 settings + 有效 archive」下回退到 `shenye` 并输出 `Ignoring unknown interface theme 'no-such-theme' (from settings.toml)`，状态栏 12s 可见（已截屏）。M3 用「从真实脚本提取 `check_icon_chain` + 正例 + 逐档删除」证明 8 档全部被断言。M4 单一图标来源成立。门禁：`bash scripts/build-qt.sh` exit 0、**62/62**、xcb smoke `M0-SMOKE ok=1`、0 条编译器告警；`docs.sh` PASS；既有测试/门禁 56 个文件哈希与修复前逐一相同；上一轮 `VERDICT.md` 哈希未变（`dafe47c1…`）；`src/app/CMakeLists.txt`、根 `Cargo.toml`/`Cargo.lock` 的 mtime 都在 22:30 之前（未被本轮改动），无新增依赖。上一轮的 F3 也已关闭：CONTRACT §2.3 在 22:24 已带日期重新冻结（QFrame 包裹作废、改由 cards.qss 承担），与实现一致。剩下的都是 Medium/Nit 级证据与范围问题，故对 R1 波次 **approve**，但最终集成树需重跑一次 F1 探针。

**关键原始输出（节选）**

```
# F1 三入口（真实点击；logs/r1-view2.run.log / r1-toolbar.run.log / r1-prefs.run.log）
view:     settings@after-switch uiTheme="qingkong"; prefs-open color 不变; after-quit settings+archive=qingkong; restart theme=qingkong
          restart status-theme=Theme: Sky Blue / Breeze Light
toolbar:  settings@after-switch uiTheme="qingkong"; after-quit settings+archive=qingkong; restart theme=qingkong
prefs:    settings@after-switch uiTheme="qingkong"; after-quit archive=qingkong; restart theme=qingkong
# F1 连续两次切换（logs/last-writer/last-writer.log）
settings@start bohe -> view: qingkong -> toolbar: yaoshi -> archive@after-quit yaoshi -> restart theme=yaoshi, status-theme=Theme: Obsidian / Breeze Dark
# M2（logs/r1-m2-conflict.log）
theme=shenye syntax=Breeze Dark; diagnostic=Ignoring unknown interface theme 'no-such-theme' (from settings.toml)
# M1（logs/r1-m1.log）
EN: status-theme=Theme: Sky Blue / Breeze Light   ZH: status-theme=主题：晴空蓝 / Breeze Light
# M3（logs/m3/m3-run.log）
positive: CHECK FAIL count = 0; negative drop-{16,24,32,48,64,128,256,512}: RED as expected
# 构建 / lrelease / docs
100% tests passed, 0 tests failed out of 62; BUILD_EXIT=0; M0-SMOKE ok=1
[lrelease] counts: 1278 translation(s) (1278 finished, 0 unfinished, 0 untranslated) -> PASS
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
```

## 发现（按严重度）

### Medium（carry）— A7 交付截图仍是修复前的旧图，展示的正是已被修掉的缺陷

- **What**：`evidence/ui-mosaic/screenshots/after-*.png`（mtime 21:53–21:55）仍显示 `Theme: ▯▯▯ / Breeze Light` —— 那是修复前硬编码中文、抓图环境无 CJK 字体的产物。本轮 M1 只改了字符串（`product_theme.cpp` 的 `makeTheme`/`displayName`），没有重拍这批截图（不在 T1 写入范围）。我独立裁图确认了豆腐块仍在。
- **Why it matters**：A7 要求六套主题的真实截图作为验收证据；现在证据与已修好的 UI 相反，读证据的人会以为 M1 没生效。修复报告里的 M1 证据是日志（`fix-r1/logs/m1-*.log`），不是 A7 截图。
- **Direction**：用 `evidence/ui-mosaic/screenshots/capture-themes.sh` 重拍六张（英文 UI 直接得到 ASCII 主题名；zh_CN 需先装与交付一致的 NotoSansCJK），或在旧图上标注「pre-R1」并由船长裁决是否接受。

### Medium（范围/依赖）— 字面「settings 旧值 + archive 新值 → 最后写入者」不在 T1，仍按冻结优先级解析

- **What**：我的独立实验：`settings.toml uiTheme=bohe`（旧）+ `session.json uiTheme=qingkong`（新，无新切换）启动 → `theme=bohe`（settings 胜，见 `logs/r1-m2-conflict.log`）。T1 的做法是让**每次用户切换同时写两个源**，因此应用自身不再产生冲突（三个入口与连续切换都实测通过）；但仅按 mtime 的「最后写入者」仲裁没有实现。修复方的 `run-f1-regression.sh` case2 同样把这种情况按 settings 胜处理。
- **Why it matters**：任务书的验收句同时要求「启动优先级 settings > archive 保持」与「冲突时最后写入者生效」，二者对**外部制造的**冲突不可同时成立。T1 满足的是用户可见的部分（最后点击胜出）；跨版本升级遗留的旧 settings/新 archive 状态在升级后第一次启动仍会显示旧 settings 值。船长已经开了 T4/R3（按 mtime 仲裁）接手这一点。
- **Direction**：不要在 T1 上补；由 R3 的独立验证覆盖「settings 与 archive 按 mtime 仲裁」并把该规则写进 CONTRACT；本结论不把 T1 判缺陷，只记录最终验收必须包含 R3。

### Nit — 状态栏一次性消息与 `Project:` 标签重叠（既有行为，不是本轮引入）

- **What**：`ApplicationShell::reportUnknownUiTheme` 用 `statusBar()->showMessage(...)` 显示 12s 诊断；实际画面上 `Project: no project`（`buildUi()` 里 `addWidget` 的标签）与消息**叠在同一行**，两者都难读（`logs/m2-status-09s.png`、`m2-status-12s-stat2.png`）。我用既有的「会话无法恢复」启动消息（`startSession()` 的 `Session could not be restored (...)`）复现了同样的重叠（`logs/existing-restore-msg-stat.png`），证明这是状态栏既有用法的问题，M2 只是第一个在启动阶段用它的路径。
- **Why it matters**：M2 的「可见诊断」成立（文字可见、日志可读），但呈现有瑕疵；不阻塞验收。
- **Direction**：确认 `QStatusBar::showMessage` 与 `addWidget` 的隐藏关系（或把 `Project` 换成 permanent 区/并入右侧），让临时消息独占临时区；这会同时改善所有既有的一次性消息。

### Nit — 产品二进制携带测试用环境接缝 `VEDIT_UI_THEME_SWITCH`

- **What**：`ApplicationShell::applyScriptedThemeSwitch`（`app_shell.cpp`，由 `startSession()` 读取 `qEnvironmentVariable("VEDIT_UI_THEME_SWITCH")`）在启动时触发视图菜单里**真实的** `QAction`，会应用并持久化主题；未设置时完全惰性。它只服务于修复方的 `run-f1-regression.sh`（他们的 case1/6 靠它无人值守切换）。
- **Why it matters**：这是生产二进制里的测试钩子，没有文档；误设环境变量会静默改写用户的 `uiTheme` 与存档。不属于用户可见缺陷，但应在发布说明/代码注释里说明，或只在测试构建编译。
- **Direction**：标注为 test-only（注释 + 用户指南「已知环境变量」），或用编译开关隔离；不要让它成为唯一可自动化的切换路径。

### Nit（carry）— `SessionState::uiTheme` 仍未进 `toJson()`

- **What**：`session_store.h` 本轮未改（sha 未变），序列化仍由 `ApplicationShell::saveSession` 手动 `insert("uiTheme")`；此前判 Nit 的风险（第二个 `toJson()` 调用者静默丢键）仍在。
- **Direction**：维持上一轮方向（在 `session_store.cpp` 补序列化，或在该字段处加断言/注释防止第二个消费者）；不阻塞 R1。

## Declined to judge（看过但明确不判）

- **F2（语法主题接不到编辑器渲染层）不在本轮复验范围**（船长另行决策）；T3 车道随后把改动落到 `theme_manager` / `editor_provider` / `ktexteditor_provider`（22:54–22:55），我未复验其像素证据，留给该车道自己的独立 review。
- **A7 截图在无 CJK 字体的抓图环境出现豆腐块本身**：交付 AppDir 自带 NotoSansCJK，不是产品缺陷；这里只判「证据陈旧」。
- **`@textMuted@` / `accentText` 对比度、16–48px light 图标、v1 GTK3 打包线、hover/pressed 真机交互**：与上一轮相同，未重新判定。
- **修复方的两条回归脚本**（`fix-r1/run-f1-regression.sh`、`run-m1-m4.sh`）我只读未原样运行（我的结论来自自己的探针）；其 case2 的 settings/archive 解释与我的观察一致。
- **一次 view 入口探针（22:58）出现「应用提前消失、settings 回到初值」**：同一脚本在 23:19 的受控重跑（以及 toolbar/prefs 与连续切换）均未复现，记录为探针/时序异常，不判产品缺陷。
- **`docs/USER_GUIDE.md` 22:50 的改动**（§8.2 三个入口/写 `uiTheme` 的表述）与修复后行为一致，属船长集成，不在 T1 写入范围。
- **Panel 内嵌 KTextEditor 的 `QFrame` 包裹**：CONTRACT §2.3 已重新冻结（22:24）并由 QSS 承担，上一轮 F3 关闭，不再重判。

## 审阅边界（没读什么 / 跑不了什么）

- **读**：REQUIREMENTS、CONTRACT（含 §2.3 重新冻结与 §3.9）、上一轮 VERDICT、`fix-r1/REPORT.md` 与 `run-f1-regression.sh` 的断言、T1 触及的实现（`ApplicationShell::applyStartupThemes/applyProductUiTheme/persistUiTheme/reportUnknownUiTheme/applyScriptedThemeSwitch`、`ProductTheme::displayName`/`makeTheme`、`main.cpp` 图标启动行、`SettingsPanel::applyThemeFromEntries`、`verify-f2-isolation.sh::check_icon_chain`、`.ts` 的 `product_theme` context）。
- **跑**：真实 XTest 三入口探针与连续两次切换探针（原始日志 + 截图，脚本 `logs/f1-entry-probe.sh`、`logs/last-writer.sh`）；六主题离屏启动；M1 中英；M2 无效 settings + 有效 archive + 状态栏截屏；M3 提取真实函数后的正例/八档负例；`bash scripts/build-qt.sh`（62/62 + xcb smoke，BUILD_EXIT=0，0 条编译器告警，唯一 `warning:` 是 ninja 日志自愈）；`build/gates/lrelease.sh`；`build/gates/docs.sh`；既有测试/门禁 56 文件哈希对比（0 行差异）；上一轮 VERDICT 哈希对比；`CMakeLists.txt`/`Cargo.*` 与 `src/editor/**` 的 mtime 核对。
- **没跑/跑不了**：`assemble-appdir.sh` / `verify-f2-isolation.sh` 的端到端 AppDir/AppImage 链路（本轮没有重新打包产物；M3 只验证断言函数本身）；真实窗口管理器下的任务栏/桌面图标观感（Xvfb 无 WM）；T3（F2）与 T4（R3）的改动（复验期间树在动）；修复方的两条回归脚本原样运行。
- **边界**：本结论钉在上面的 T1 基线；当前树已含 T3 完成改动并正被 R3 修改，**最终集成树必须重跑 `logs/f1-entry-probe.sh` 的三个入口 + 连续切换**，并重新产出 A7 截图。

## 结论

**Verdict：approve（针对 T1 修复波次）。**
一句话理由：F1 与 M1–M4 都用独立实验证实修好、既有测试/门禁与上一轮结论未被改动，没有新的 Blocker/High；剩余 Medium 是 A7 旧截图与已由 R3 接手的字面冲突仲裁，最终集成树需重跑一次 F1 探针。

## 补记（2026-11-08 23:28，最终树）

- 复验期间 T3（F2）与 T4（R3，settings/archive mtime 仲裁）均已 completed；当前二进制 `target/qt6-build/src/app/vedit-app` 为 23:22:58 构建（sha `5c0bacdf…`），含 T1+T3+R3。
- 我在该二进制上独立验证了 R3 的 mtime 仲裁（`logs/r1-final-conflict.log`）：settings 旧(22:27)/archive 新(23:26) → `theme=yaoshi`（archive 胜）；settings 新/archive 旧 → `theme=bohe`（settings 胜）；mtime 相等 → `theme=bohe`（回退 settings）。因此上面「Medium（范围/依赖）」一项在最终树上已关闭；R3 自身的独立 review 仍以 T4 的验收为准。
- 三个入口的 F1 探针仍是在 T1 二进制（22:42:43）上完成的；最终树（T3+T4 都改过 `app_shell.cpp`）的 F1 重跑由**并行复验实例**在同一容器上进行（`logs/r2/r2-final-view/`，23:28），我没有与该实例并发抢占同一容器；最终验收应以那次重跑 + 本结论合并。
- 说明：`review-r1/` 下另有并行 reviewer 实例写入的 `logs/r1-binary`、`logs/r2-binary`、`logs/r2/`、`logs/f1-conflict2*`（23:05–23:28），非本验证者产物，我未改动/未删除。
