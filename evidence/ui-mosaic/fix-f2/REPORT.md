# UI-Mosaic F2 — 语法主题真正接到 KTextEditor 渲染层

任务：修掉独立验证的 **High** 缺陷 —— 产品主题接管应用调色板后 `ThemeManager::applyTheme()`
不再 `setPalette`，而全仓没有任何代码把语法主题交给编辑器，于是语法主题切换只改状态栏文字、
零视觉效果（违背 `REQUIREMENTS.md §2`「编辑器语法配色仍由它决定」与 `docs/USER_GUIDE.md §8.2`）。

**结论：已修复。** 语法主题现在经 `KTextEditor::View::setConfigValue("theme", <name>)` 真正进入
KTextEditor 渲染层：同一 `yunwu` 产品主题下，`Breeze Light` vs `Breeze Dark` 的真实 Xvfb+xcb
截图逐像素 diff 有 **226,018** 个变化像素（>8），其中 **224,674（99.4%）落在编辑器文本区**，
文件树/工具栏/产品主题 chrome 为 **0** 变化。

环境：Qt6 6.10.2 / KF6 6.24.0 / KTextEditor 6.24.0，镜像 `vedit-qt6-build:local`
（`scripts/versions-qt.env` 的 pin），证据时间 2026-11-08。

---

## 1. 根因与正确入口

- 根因：`ThemeManager::applyTheme()` 在产品主题生效后只记录 `current_`，不产生任何渲染效果；
  shell 也从未把 `syntaxThemeName()` 交给编辑器 lane。语法主题因此没有消费者。
- 正确入口（研究已确认，本次落地验证）：`KTextEditor::View::setConfigValue("theme", name)`。
  KTextEditor 的 per-view 主题是公开 API；`view->theme()` 返回实际生效的
  `KSyntaxHighlighting::Theme`。
- 关键事实（实测）：**KTextEditor 用的是它自己的 `KSyntaxHighlighting::Repository`**
  （`Editor::instance()->repository()`，与 `vedit::app::ThemeManager` 自建的 repository 不是同一对象，
  T127），所以主题名必须用这份仓库校验；本机 30/30 个 catalogue 名字在这份仓库里全部有效
  （probe `themes.kteditor-repository-parity`）。

## 2. 改动（写入范围内，7 个文件）

| 文件 | 改动 |
| --- | --- |
| `src/app/editor_provider.h` | 新增 seam `virtual void applySyntaxTheme(const QString &themeName);`，默认空实现（header-only inline）。shell 仍然不 include 任何 KTextEditor 头。 |
| `src/app/theme_manager.h/.cpp` | 新增 `setSyntaxThemeApplier(std::function<void(const QString&)>)` 与成员 `syntaxThemeApplier_`；`applyTheme()` **成功后**回调生效主题名（失败不回调）。 |
| `src/app/app_shell.cpp` | `setEditorProvider()` 里把 `themes_` 的 applier 接到 `provider_->applySyntaxTheme`，并**立即**用 `themes_->syntaxThemeName()` 应用一次当前主题（覆盖「先 applyTheme、后装 provider」的启动顺序）。 |
| `src/editor/kernel/ktexteditor_provider.h/.cpp` | 记住 `syntaxTheme_`；`applySyntaxTheme()` 遍历 `Editor::instance()->documents()` 的每个 `views()` 全部设置；`createEditor()` 在 `document->createView(parent)` 后立即给新 view 补设（per-view 语义）。 |
| `src/app/app_shell.h` | 无需改动（`themes_`/`provider_` 已存在）。 |

没有新增第三方依赖、没有改既有测试/门禁、没有跑 lupdate、没有新增可见字符串。
全部 TU 以工程原 flags（含 `-Wall -Wextra -Wpedantic -Werror`）`-fsyntax-only` 通过
（`syntax-check.sh`）。

## 3. 语义决策（复验关注点）

### 3.1 per-view：新 View / 分屏必须补调
KTextEditor 的主题是 **每个 View** 一份，不是 document 级。因此：
- `applySyntaxTheme()` 对**已打开**的每个 document 的每个 view 设置；
- `createEditor()` 对新 view 立即设置 `syntaxTheme_`（分屏、换 provider、新标签都走这一条路）。
probe 断言：切换后新建的第二个 view `configValue("theme") == "Breeze Light"`（`view.second.uses-current-theme`）。

### 3.2 主题名校验与回退（明确选择）
校验：用 **KTextEditor 自己的** repository 判断
`Editor::instance()->repository().theme(name).isValid()`。

回退顺序（本次明确选择，报告依据）：
1. 无效名字 → **KTextEditor 当前全局主题** `Editor::instance()->theme()`；
2. 若连全局主题也无效 → 该 repository 的 `defaultTheme(Repository::LightTheme)`。

为什么首选「当前全局主题」而不是直接 `defaultTheme(Light/Dark)`：
- 无法从「无效名字」推断用户想要的明/暗意图，直接强制 Light 会凭空把（系统默认深色的）编辑器刷亮；
- 全局主题是该 KTextEditor 进程里**真实生效**的默认值，是唯一有依据的确定性回退；
- 该路径在产品中不可达：catalogue 的 30/30 个名字在 KTextEditor 仓库里全部有效（probe 已证明），
  `Light/Dark` 只作为「连全局主题都无效」的最后兜底；
- 无论走到哪一层，view 都会得到一个**有效**主题，不会静默保留 half-applied 状态。

### 3.3 背景/选区是否跟随：跟随，不额外补设
本次像素验证的答案是**跟随**，任务里预留的 `background-color` / `selection-color` 显式补设
**没有加**（避免与 KTextEditor 的主题机制重复维护一份颜色状态）：
- probe：view 的 grab 里主题自身 `BackgroundColor` 命中率 86.4%（Breeze Dark）/ 82.1%（Breeze Light），
  远高于 20% 阈值；每次切换后整张 view（595,200 = 960×620 全部像素）都不同；
- 真实截图：编辑器文本区平均亮度 **240.7 → 47.5**。
如果将来 KTextEditor 版本改变这一行为，probe 的 `view.background.*` 会先红。

### 3.4 状态栏一致性
状态栏文字读取的是 `ThemeManager::currentTheme()`（`app_shell.cpp` 的 `updateStatusBar()`），
而它正是传给 applier 的**同一个值**（`theme_manager.cpp` `applyTheme()` 末尾）——状态栏读的是
我们自己的 ThemeManager 状态，不是 `KTextEditor::Editor::theme()`，所以 per-view 设置不会让它漂移：
- probe 断言 `status.label-matches-render`：`manager.currentTheme() == view->theme().name()`
  （Breeze Dark 与 Breeze Light 各一次，均 PASS）；
- probe 打印 `INFO editor-global-theme=Breeze Light`：per-view 设置**不改变** `Editor::theme()`
  这个全局值，所以**不能**拿它当状态栏来源（我们没用它）。

## 4. 验证结果

### 4.1 构建 + 全量 ctest（`bash scripts/build-qt.sh`）
- 首次全量运行：构建成功，ctest `98% tests passed, 1 tests failed out of 62` ——
  **`vedit-rename-e2e` (SIGPIPE)**。判定为**既有 flake，非本改动**：
  - 同一失败类别在历史证据里出现过：`evidence/ux-batch-2/u8-final/ctest-integration.log:48`
    `49 - vedit-rename-e2e (SIGPIPE)`；
  - 单独复跑：`1/1 Test #51 vedit-rename-e2e ... Passed 1.16 sec`；
  - `AGENT.md` 已记录该类测试「spawn 真实 LSP 子进程 + 静态链接 Rust」的 SIGPIPE 潜在问题；
  - 本改动不触碰 diagnostics/LSP/rename 代码路径（diff 仅 7 个 theme/editor seam 文件）。
  证据：`build.log`、`ctest-rerun.log`。
- 全量复跑：**62/62 passed, 0 failed**（`ctest-rerun.log`，`CTEST_EXIT=0`）。
- 再跑一次 `bash scripts/build-qt.sh`（干净单次）：见 `build-run2.log`（`BUILD_EXIT=0`，62/62），
  证明脚本本身 exit 0。
- 既有测试一律未修改（`git diff` 只包含上表 7 个文件 + 本证据目录）。

### 4.2 lrelease 门禁（`bash build/gates/lrelease.sh`）
```
Generated 1278 translation(s) (1278 finished and 0 unfinished)
[lrelease] counts: 1278 translation(s) (1278 finished, 0 unfinished, 0 untranslated)
[lrelease] PASS: 0 unfinished and 0 untranslated
```
`LRELEASE_EXIT=0`；与 F1 收尾基线一致（1278/0/0），本改动没有新增可见字符串。
证据：`lrelease.log`。

### 4.3 可重复断言（`run-probe.py` + `theme_seam_probe.cpp`，链接真实库）
证据工具不登记 CMake（不得改既有测试/门禁），用 CMake 记录的同一份编译 flags 与链接库清单
（参考目标 `vedit-editor-pane-activation-tests`）编译链接 `libvedit_app_shell.a` +
`libvedit_editor_core.a`，因此断言跑的是**真实现**。`F2-PROBE PASS failures=0`，关键行：

```
CHECK theme.apply                           PASS
CHECK theme.callback.name                   PASS (Breeze Dark)
CHECK theme.invalid.refused                 PASS (unknown theme '__vedit-f2-no-such-theme__' ...)
CHECK theme.invalid.silent                  PASS (calls=1)
CHECK theme.no-applier.noop                 PASS
CHECK themes.kteditor-repository-parity     PASS (30/30 names valid)
CHECK fixture.syntax-definition-found       PASS (V)
CHECK chain.apply                           PASS
CHECK view.theme.config-value               PASS (Breeze Dark)
CHECK view.theme.object                     PASS (Breeze Dark)
CHECK status.label-matches-render           PASS (label=Breeze Dark render=Breeze Dark)
CHECK view.theme.switched                   PASS (Breeze Light)
CHECK view.theme.object.switched            PASS (Breeze Light)
CHECK status.label-matches-render.switched  PASS (label=Breeze Light render=Breeze Light)
CHECK view.render.differs                   PASS (differing=595200)
CHECK view.background.dark-theme            PASS (hits=514236/595200 (86.4%))
CHECK view.background.light-theme           PASS (hits=488813/595200 (82.1%))
CHECK view.second.uses-current-theme        PASS (Breeze Light)
INFO  editor-global-theme=Breeze Light (per-view theme settings do not change it)
CHECK view.invalid-name.falls-back          PASS (render=Breeze Light expected=Breeze Light)
F2-PROBE PASS failures=0
```
完整日志：`probe.log`。

### 4.4 真实像素证据（`capture-pixels.sh`，Xvfb + xcb + 真二进制）
两次真实运行**完全相同**的产品主题 `yunwu`、同一个多语法 Verilog fixture
（`fixture/syntax-mosaic.v`：注释/字符串/数字/关键字/类型/宏/系统任务/函数/运算符），
唯一差异是语法主题：默认配对 `Breeze Light` vs `--theme "Breeze Dark"`。

隔离与可归因（本车道专用，未与任何复核车道共用资源）：容器 `f2-pixels`、显示 `:97`、
X socket 目录 `.mpd/x11-97`、应用状态目录 `evidence/ui-mosaic/fix-f2/state/`（`--state-dir`，
两轮都只有 `uiTheme = "yunwu"`）、输出只写 `evidence/ui-mosaic/fix-f2/**`。

- 截图：`screens/yunwu-breeze-light.png`、`screens/yunwu-breeze-dark.png`（1680×1050 全屏抓图，
  窗口 1280×820 固定在 (0,0)，两次几何一致）。
- 整屏逐像素 diff（阈值 >8）：**changed pixels = 226,018**；
  **diff bbox = (587, 113, 1178, 817)**；changed row bands = **[(113, 769), (804, 816)]**；
  changed column bands = [(587, 930), (1163, 1177)]。
- 分区统计（同一 diff）：
  | 区域 | 变化像素 |
  | --- | --- |
  | 编辑器文本区 (x 587–930, y 113–769) | **224,674** |
  | 状态栏主题名文字 (y 804–816) | 1,234 |
  | 诊断面板时间戳 (x 1163–1177, y 530–538，两次启动时间不同，与主题无关) | 110 |
  | 左侧文件树面板 (x 0–300, y 80–780) | **0** |
  | 工具栏 (x 400–1200, y 0–80) | **0** |
- 编辑器文本区平均亮度：**light=240.7 → dark=47.5**（背景真正换了，不只是文字颜色）。
- **直接结论：diff 的正文行带 (113..769) 与状态栏行带 (804..816) 不重叠；99.4% 的变化像素
  (224,674/226,018) 落在编辑器文本区，产品主题 chrome 0 变化** —— 即 F2 要求的
  「diff 必须落在编辑器文本区域，不是只有状态栏」成立。
- 人眼复核用裁剪图：`screens/editor-light.png`、`screens/editor-dark.png`。
- 原始日志与分区分析：`pixels.log`、`pixel-analysis.log`。

## 5. 复现命令

```bash
# 1) 全量构建 + ctest（62/62）
bash scripts/build-qt.sh
# 2) lrelease 门禁（0 unfinished / 0 untranslated）
bash build/gates/lrelease.sh
# 3) 可重复断言（真库 + 真 KTextEditor 渲染）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
    python3 evidence/ui-mosaic/fix-f2/run-probe.py
# 4) 真实像素证据（Xvfb+xcb，两次抓图 + 逐像素 diff）
bash evidence/ui-mosaic/fix-f2/capture-pixels.sh
```

## 6. 已知边界（如实声明）

- 像素抓图在**无窗口管理器**的 Xvfb 上运行：窗口位置 (0,0)、尺寸 1280×820 由应用自身决定，
  两次运行几何一致；有 WM 的桌面上抓图数字会随窗口位置变化，但断言只依赖编辑器内部区域。
- `applySyntaxTheme` 保持 `void` seam（按任务约定）：若将来引入「只有我们仓库可见、KTextEditor
  仓库不认」的自定义主题，编辑器会按 §3.2 回退，而状态栏仍显示请求名。当前不存在这种主题
  （probe 证明 catalogue 30/30 名字双方都有效），因此未扩展 seam；真引入时需把「实际生效名」
  回传给 shell（或让状态栏消费该回传值）。
- 未订阅 KTextEditor 的 `repositoryReloaded()`：仓库在运行时重载时，view 的 theme 名仍在
  KTextEditor 仓库内有效（30/30），因此无需额外处理；无已知用户可见影响。
- 未修改既有测试、未新增 ctest 目标（证据断言放在 `evidence/` 下的独立探针，链接真实库）。
