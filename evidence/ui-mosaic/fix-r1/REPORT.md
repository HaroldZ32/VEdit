# UI-Mosaic · R1 修复波次报告（F1 + M1–M4）

- 写入方：Deep Worker（`T1`）
- 依据：`evidence/ui-mosaic/review/VERDICT.md`（F1 + M1–M4）、`.mpd/plans/ui-mosaic/REQUIREMENTS.md`（A1/A6）、`.mpd/plans/ui-mosaic/CONTRACT.md`（§1.5/§1.6/§2.1/§3.1/§3.9）
- 范围：本轮只修 F1 与 M1–M4；**未碰 F2**（语法主题渲染接线），`src/editor/**` 与本轮无关，一行未改。既有测试与门禁未改（只按 M3 要求**加强**了既有 `verify-f2-isolation.sh` 的断言）。
- 复现：两条证据脚本 + 三条门禁，全部命令与原始日志在 `evidence/ui-mosaic/fix-r1/` 下。

## 0. 结论速览

| 项 | 状态 | 运行证据 |
| --- | --- | --- |
| F1 三入口「切换→重启→读回」，无旧值回弹 | **PASS** | `run-f1-regression.sh` → `pass=14 fail=0`（`logs/run-f1-regression.log`） |
| M1 产品主题名可翻译（英文源 + 手工 zh_CN） | **PASS** | `run-m1-m4.sh`（`logs/m1-english.log` / `logs/m1-chinese.log`） |
| M2 无效 settings `uiTheme` 视为未设置 → archive > 默认 + 可见诊断 | **PASS** | 同上脚本 case4/case5 |
| M3 8 档 hicolor 断言 | **PASS** | `logs/m3-positive.log` / `logs/m3-negative.log` |
| M4 `main.cpp` 单一图标来源 | **PASS** | `run-m1-m4.sh` M4 段 |
| Qt6 构建 + 全量 ctest | **PASS** | `logs/build-qt-clean.log`：`100% tests passed, 0 tests failed out of 62` |
| `build/gates/lrelease.sh` 双 0 | **PASS** | `logs/lrelease.log`：`1278 finished, 0 unfinished, 0 untranslated` |
| `build/gates/docs.sh` | **PASS** | 见 §5 |

## 1. 改动清单（路径 + 符号）

| 文件 | 符号 / 位置 | 改动 |
| --- | --- | --- |
| `src/app/app_shell.h` | `ApplicationShell::setUiThemePersister` / `using UiThemePersister`；私有 `persistUiTheme`、`reportUnknownUiTheme`、`applyScriptedThemeSwitch`、`uiThemePersister_`、`persistingUiTheme_` | 新增持久化接缝与三个私有实现声明 |
| `src/app/app_shell.cpp` | `setUiThemePersister` | 接缝实现（与 `setPreferencesOpener` 同一模式） |
| 同上 | `applyStartupThemes` | 优先级改为「逐项校验的 seam > settings > archive > 默认」：未知 id 视为未设置并继续回退（M2），同时 `reportUnknownUiTheme` 出可见诊断；`settingsUiThemeValid` 取代「非空即算配置」的旧判据 |
| 同上 | `applyProductUiTheme` | 记录**实际生效**的 id；用户切换（`reportFailure == true`）时调用 `persistUiTheme(appliedId)`，启动应用不写入 |
| 同上 | `persistUiTheme`（新） | (1) 经 `uiThemePersister_` 写 settings.toml（产品里就是 `SettingsClient`）；(2) 立即把 `uiTheme` 写回已存在的 session.json；两者失败都出可见诊断，不静默 |
| 同上 | `reportUnknownUiTheme`（新） | `addDiagnostic` + 12 s 状态栏提示 |
| 同上 | `applyScriptedThemeSwitch`（新）+ `startSession()` 尾部 | `VEDIT_UI_THEME_SWITCH=<id>` 时**触发视图菜单里真实的** `QAction`（按 `data()` 匹配），用于无人值守回归；未设置时完全惰性 |
| 同上 | `logUiProbe` | 额外打印 `[ui-probe] diagnostic=<line>`（只读，不改状态），让 M2 的「可见诊断」可被外部观测 |
| `src/app/main.cpp` | 图标启动行 | `QIcon(":/icons/app/vedit-light.png")` → `vedit::app::themed_icons::appIcon(false)`（M4）；新增 `#include "themed_icons.h"` |
| 同上 | `ApplicationShell shell(options);` 之后（`VEDIT_HAVE_APPEARANCE_SETTINGS` 内） | 安装 `SettingsClient` 持久化 lambda：load → `setValue("uiTheme")` → save → `flush()`，与首选项页面**同一个类、同一个键、同一个原子写** |
| `src/app/product_theme.h` | `ProductTheme::displayName` 注释 / 文件头 | 说明 displayName 是可翻译 UI 文本、id 才是持久化值（M1） |
| `src/app/product_theme.cpp` | `makeTheme` + 6 条主题数据 | 源文本改 `Cloud White / Warm Apricot / Mint Green / Sky Blue / Obsidian / Deep Navy`，构造时 `QCoreApplication::translate("product_theme", ...)`；id 与色值一字未动（M1） |
| `src/workspace/settings/settings_panel.cpp` | `applyThemeFromEntries` | 读到的 id 无对应主题时视为未设置并出诊断（与启动路径同一规则），不再对无效 id 做一次失败 apply（M2 一致性） |
| `translations/vedit_zh_CN.ts` | 新 context `product_theme`（6 条）；`vedit::app::ApplicationShell` +3；`VEdit` +1；`vedit::app::SettingsPanel` +1 | 手工补译文，**未跑 lupdate**；总数 1267 → 1278，全部 finished |
| `packaging/runtime/verify-f2-isolation.sh` | `check_icon_chain` 的 required 列表 | 4 档 → **8 档**（16/24/32/48/64/128/256/512），与 `assemble-appdir.sh` 安装清单一致（M3） |

新增证据文件：`evidence/ui-mosaic/fix-r1/{run-f1-regression.sh, run-m1-m4.sh, REPORT.md, logs/, state/, state-m1/, m3-appdir/}`。
**未改**：`src/editor/**`、任何既有测试、`src/app/CMakeLists.txt`、`resources/**`、其余门禁脚本。

## 2. F1（High，必修）—— 三入口一致 + 最后写入者生效

### 2.1 根因与修法

VERDICT 的根因是**两个存储源不同步**：菜单/工具栏只应用、不写 settings.toml；退出时只把主题写进 session.json；启动又优先 settings.toml；首选项 `reload()` 还会重放 settings 旧值。修法是把「用户切换」变成**一次写入两个源**，并让首选项读的仍是同一个 store：

1. 菜单/工具栏（`createThemeMenu` 的两个菜单共用同一 lambda）→ `applyProductUiTheme(id, true)` → `persistUiTheme(id)`；
2. `persistUiTheme` 先经 `main.cpp` 安装的 `SettingsClient` 接缝写 `uiTheme`（**与首选项页面同一个类、同一个键、同一种原子写**），再把同一个 id 立刻写回已存在的 session.json；
3. 启动仍是 settings > archive > 默认，但因为每次切换都刷新 settings，最后写入者（用户最后一次点击）必然胜出；
4. 首选项 `reload()` 从磁盘读到的就是刚写入的新值，`currentUiTheme() != storedUi` 守卫使旧值无从上屏 —— 不再回弹。

为什么需要接缝而不是直接在 shell 里调用 `SettingsClient`：CMake 里 `vedit_workspace_settings` **PUBLIC 链接 `vedit_app_shell`**（依赖方向是 settings → shell），app 层 include settings 头会成环。接缝由入口 `main.cpp` 安装（它已经链接 settings 与 FFI），没有第二份写实现。

### 2.2 可执行回归（真实二进制，离屏，`VEDIT_UI_PROBE=1`）

```
bash evidence/ui-mosaic/fix-r1/run-f1-regression.sh
```

脚本用 `docker run … vedit-qt6-build:local` 跑**真实** `target/qt6-build/src/app/vedit-app --state-dir /state --hold 3500`；切换动作由 `VEDIT_UI_THEME_SWITCH=<id>` 触发**视图菜单里真实的 QAction**（`action->trigger()`），不是私有捷径。原始日志 `logs/case*.log`。

回归断言以**可执行脚本**交付：本轮写入范围不含新测试源文件，且硬约束禁止改既有测试，所以断言放在 `run-f1-regression.sh`（可直接重跑，失败退出码非 0），不改任何 ctest 用例。汇总：

```
PASS  case1: the live switch applied qingkong (status-theme=主题：晴空蓝 / Breeze Light ...)
PASS  case1: settings.toml now carries uiTheme = "qingkong" (the Preferences key)
PASS  case1: session archive uiTheme was synced to qingkong
PASS  case1: restart read the switch back (theme=qingkong)
PASS  case1: status-theme names the stored Sky Blue theme (zh_CN UI)
PASS  case2: with no switch, settings.toml still wins over the archive (bohe)
PASS  case2: the second start also reads bohe (settings stays authoritative)
PASS  case3: an absent settings.toml falls back to the archived shenye
PASS  case4: invalid settings id fell through to the archive (shenye), not yunwu
PASS  case4: the fallback is a VISIBLE diagnostic (not silent)
PASS  case5: invalid settings id with no archive lands on the contract default (yunwu)
PASS  case5: the default fallback is still diagnosed
PASS  case6: the menu switch wrote the store's exact spelling (uiTheme = "xinghuang")
PASS  case6: settings.toml is still valid TOML with the OTHER key preserved
== summary: pass=14 fail=0 ==
F1-REGRESSION PASS
```

关键「切换→重启→读回」逐字证据（case1：settings=bohe、archive=shenye，用户切到 qingkong）：

```
# 切换（带 VEDIT_UI_THEME_SWITCH=qingkong）
[ui-probe] theme=qingkong syntax=Breeze Light status-theme=主题：晴空蓝 / Breeze Light style=2 toolbarIconSize=20x20
# 之后磁盘上的两个源（logs/… 与 state/case1-switch/）
"ui.language" = "zh_CN"
uiTheme = "qingkong"          <- settings.toml（键仍按 Rust store 的排序列出，说明写的就是 SettingsClient）
archive.uiTheme = qingkong    <- session.json
# 不带切换的第二次启动（重启）
[ui-probe] theme=qingkong syntax=Breeze Light status-theme=主题：晴空蓝 / Breeze Light style=2 toolbarIconSize=20x20
```

`case2`（不切换）证明优先级没被改坏：settings=bohe 仍压过 archive=shenye，连续两次启动都是 `theme=bohe`。`case3` 证明 archive 仍是第二来源。`case6` 用 `tomllib` 断言写出的文件是**合法 TOML 且其它键（`ui.language`）原样保留**——这正是首选项页面随后读写同一个 store 的前提。

### 2.3 声明的边界（未proven）

- **首选项下拉没有在无头环境里被鼠标点过**：它的写入路径未改（`SettingsClient::setValue(&entries_, "uiTheme", id); persist();`），与菜单路径是同一个类、同一个键、同一次原子写；`case6` 证明菜单路径写出的行就是该 store 自己的序列化结果（`uiTheme = "id"`，其余键保留），所以两条入口在磁盘上不可区分。可交互复核（真实点击下拉）留给有 WM 的环境。
- 工具栏按钮的菜单是 `createThemeMenu()` 造出的第二组 QAction，连接同一个 lambda；回归驱动的是视图菜单那一组。
- 回归用环境变量重放，不是真实鼠标事件（无 WM）；`VEDIT_UI_THEME_SWITCH` 默认不设置，产品行为不受影响。
- 若 `VEDIT_HAVE_APPEARANCE_SETTINGS` 未编译（没有 settings 车道的精简构建），shell 没有 store：切换会给出「已应用但未保存」的可见诊断，并仍同步 archive —— 该配置下不存在首选项入口，故不构成三入口不一致。

## 3. M1（Medium）—— 产品主题名可翻译

`product_theme.cpp` 的 `displayName` 不再写死中文：数据里存英文源文本，构造时按固定 context `product_theme` 取译文；`id` 与冻结色值不变。

```
$ bash evidence/ui-mosaic/fix-r1/run-m1-m4.sh
en  status-theme=Theme: Sky Blue / Breeze Light style=2 toolbarIconSize=20x20
zh  status-theme=主题：晴空蓝 / Breeze Light style=2 toolbarIconSize=20x20
PASS  M1: English UI shows the English source name 'Sky Blue'
PASS  M1: zh_CN UI shows the hand-written translation 晴空蓝
PASS  M1: the persisted id is still qingkong (display text does not change it)
ids in product_theme.cpp: yunwu,xinghuang,bohe,qingkong,yaoshi,shenye
PASS  M1: product_theme.cpp carries the English source "Cloud White" … （6 条全 PASS）
PASS  M1: displayName is built through QCoreApplication::translate("product_theme")
PASS  M1: vedit_zh_CN.ts maps Cloud White -> 云雾白 … （6 条全 PASS）
```

译文（手工补进 `translations/vedit_zh_CN.ts`，**未跑 lupdate**）：

| source (en) | translation (zh_CN) |
| --- | --- |
| Cloud White | 云雾白 |
| Warm Apricot | 暖杏黄 |
| Mint Green | 薄荷绿 |
| Sky Blue | 晴空蓝 |
| Obsidian | 曜石黑 |
| Deep Navy | 深夜蓝 |

## 4. M2（Medium）—— 无效 settings `uiTheme` 不再吞掉有效 archive

`applyStartupThemes` 逐项校验三个来源（seam / settings / archive）：未知 id → `reportUnknownUiTheme`（`addDiagnostic` + 状态栏 12 s）→ **继续下一来源**；只有全部为空/无效才落默认 `yunwu`。首选项的 `applyThemeFromEntries` 用同一规则。

```
# case4：settings=no-such-theme，archive=shenye
[ui-probe] theme=shenye syntax=Breeze Dark status-theme=主题：深夜蓝 / Breeze Dark …
[ui-probe] diagnostic=忽略未知的界面主题“no-such-theme”（来自 settings.toml）
# case5：settings=no-such-theme，无 archive
[ui-probe] theme=yunwu syntax=Breeze Light status-theme=Theme: Cloud White / Breeze Light …
[ui-probe] diagnostic=Ignoring unknown interface theme 'no-such-theme' (from settings.toml)
```

同一 `settings.toml` 在两次运行里只差了语言键，诊断因此一条中文一条英文——同时反证 M1 的翻译链路与 M2 的「不静默」。

## 5. M3 / M4 与门禁

### M3 — `verify-f2-isolation.sh` 补齐 8 档

`check_icon_chain` 的 required 列表现在是 8 档（16/24/32/48/64/128/256/512），与 `assemble-appdir.sh` 的安装清单一致；用**抽出的函数**做真 fixture 正/负控制：

```
hicolor tiers asserted by the extracted function: 8
PASS  M3: 16x16 is asserted … 512x512 is asserted（8 条）
PASS  M3: verify-f2-isolation.sh parses (bash -n)
PASS  M3: positive control — an AppDir with all eight tiers passes (14 assertions)
PASS  M3: negative control — the missing 48x48 tier is reported as a FAIL
```

负控制的原始日志（`logs/m3-negative.log`）里是被删掉的 `usr/share/icons/hicolor/48x48/apps/io.github.vedit.VEdit.png` —— 换回旧脚本这一档不会被断言。

### M4 — `main.cpp` 单一图标来源

```
PASS  M4: main.cpp no longer hard-codes any :/icons/app/ resource path
1856:    // UI-Mosaic R1/M4: ONE source for the launcher icon. `themed_icons::appIcon(false)`
1859:    QApplication::setWindowIcon(vedit::app::themed_icons::appIcon(false));
PASS  M4: main.cpp calls themed_icons::appIcon(false) (the single source)
PASS  M4: main.cpp includes themed_icons.h
```

`appIcon(false)` 返回的就是 `:/icons/app/vedit-light.png`，行为等价；该图标「非空且有不透明像素」由既有 `vedit-app-ui-theme-selftest` 承担（全量 ctest 里通过）。

### 门禁原始输出

```
$ bash scripts/build-qt.sh          # logs/build-qt-clean.log
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  86.38 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 … window=1 editor=1 document=1
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke …
BUILD_EXIT=0

$ bash build/gates/lrelease.sh     # logs/lrelease.log
Generated 1278 translation(s) (1278 finished and 0 unfinished)
[lrelease] counts: 1278 translation(s) (1278 finished, 0 unfinished, 0 untranslated)
[lrelease] PASS: 0 unfinished and 0 untranslated in translations/vedit_zh_CN.ts

$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
```

构建在 pinned 镜像 `vedit-qt6-build:local` 内完成，`-Wall -Wextra -Wpedantic -Werror`；`logs/build-qt-clean.log` 是删掉 `.ninja_log` 后的**全量编译**日志（`409` 个构建步，`app_shell.cpp`、`product_theme.cpp`、`settings_panel.cpp`、`main.cpp` 都在其中重新编译）。日志里全是 **0 条编译告警**；唯一的 `warning:` 行是构建开始处 ninja 对截断日志的自愈提示（`ninja: warning: premature end of file; recovering`），不是编译器输出。

## 6. 复现步骤

```bash
# 1) 两条证据脚本（会在 pinned 镜像里跑真实二进制）
bash evidence/ui-mosaic/fix-r1/run-f1-regression.sh   # 14/14，F1 + M2
bash evidence/ui-mosaic/fix-r1/run-m1-m4.sh           # 30/30，M1 + M3 + M4

# 2) 门禁
bash scripts/build-qt.sh                              # 62/62 + xcb smoke
bash build/gates/lrelease.sh                          # 1278 finished / 0 / 0
bash build/gates/docs.sh
```

## 7. 未做 / 已知限制

- **F2 未碰**（VERDICT F2、语法主题渲染接线）：按本轮任务书由船长另行决策；`src/editor/**` 零改动。
- `docs/USER_GUIDE.md` §8.2 说的「三个入口……写进 `uiTheme`」现在成立；M1 之后英文界面显示英文名，指南里仍列中文名（文档不在本轮写入范围）。
- 未提交 git（按仓库纪律由船长集成）；本轮 diff 只覆盖上面 §1 的文件 + `evidence/ui-mosaic/fix-r1/`。
  - 构建脚本会重写 `evidence/m0-build/build-logs/2x-qt6-*` 与 `evidence/ffi-abi/logs/*`（脚本自身行为，非人工改动）。
