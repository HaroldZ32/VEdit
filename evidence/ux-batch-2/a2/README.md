# UX batch 2 — lane A2：顶栏 + 菜单重排 + i18n 补全 + 内置用户手册

写者：Senior Engineer 2（共享任务 T2）。契约：`.mpd/plans/ux-batch-2/00-plan.md` §3.A2 +
captain 的 4 条裁决（main.cpp 3 行授权 / 整树链接归 A4 / project_model 根因修复 / F1 终端透传）。

一句话结论：**五项验收全部可机检通过**（A2 自测 + 产品 `--selfcheck` 312/312 + 受影响 ctest 7/7），
`.ts` 重生成后 **1018 finished / 0 unfinished**（基线 936），F1 的“终端内进 PTY / 终端外开手册”
两个方向都用真实按键实测过。

---

## 0. 验收对照（计划里的 ①–⑤）

| # | 验收点 | 判据（check id） | 结果 | 证据 |
|---|---|---|---|---|
| ① | 菜单顺序 File, Edit, View, Project, Help；无 Settings 菜单 | `a2.menu.order`、`a2.menu.no-settings` | PASS | `logs/31-ux-selftest.log`，截图 `screenshots/10-main-toolbar-menu.png` |
| ② | Preferences… 在 Edit 菜单底部 | `a2.menu.preferences-in-edit`、`a2.menu.preferences-last-in-edit` | PASS | 同上 |
| ③ | 工具栏每个动作存在且连到真实命令；撤销/重做真实改变文档 | `a2.toolbar.*`（16 项，含 hide-and-show）+ `a2.undo.real-*` / `a2.redo.real-*`（真实 KTextEditor 文档） | PASS | `logs/31-ux-selftest.log` |
| ④ | 中文 .qm 加载后 6 个字符串为中文 | `a2.i18n.menu-0..4`、`a2.i18n.preferences`、`a2.i18n.panel-toggle` | PASS | 同上 + 截图 `screenshots/12-main-menu-zh.png` |
| ⑤ | 帮助阅读器能从 qrc 打开且含用户手册标题 | `a2.help.*`（6 项） | PASS | `logs/31-ux-selftest.log`，截图 `screenshots/11-user-manual.png` |

产品级自检（同一棵树的真实产品二进制）：`vedit-app --selfcheck` → **312 PASS / 0 FAIL，exit 0**
（`logs/41-selfcheck-green.log`）。受影响测试 `ctest -R ...` → **7/7 passed**（`logs/30-ctest-affected.log`）；
**全量 ctest 42/42 passed**（`logs/32-ctest-all.log`，同一棵树的最终二进制）。

---

## 1. 改动文件与语义

| 文件 | 改动 |
|---|---|
| `src/app/app_shell.cpp` / `.h` | 菜单固定顺序（`buildMenuBar()` 按 `kMenuOrder` 排序，不再依赖注册顺序）；新增 Edit 菜单（Undo/Redo 在顶部、Preferences 在底部）；File 新增 New / Save All；View 新增 Toolbar 开关与 Search in Files；Help 新增 User Manual（F1）；新增主工具栏 `buildToolBar()`（与菜单共用同一批 QAction）；`syncEditorCommandStates()` 撤销/重做可用性同步；`focusSearchPanel()`、`openUserManual()`、`saveAllDocuments()`；panel toggle 标题全部 `tr()` |
| `src/app/editor_area.cpp` / `.h` | `EditorPane::newDocument()`（复用未改动的空 tab）；`triggerEditorAction()/editorActionEnabled()`（按 objectName 找编辑器自己的动作，KTextEditor 的 `edit_undo`/`edit_redo`，纯文本 provider 回退到 `undo()`/`redo()` 槽）；**修复 provider 切换时的悬垂指针**（`editor_ = nullptr`，见 §6） |
| `src/app/command_registry.*` | **未改**（工具栏复用同一注册表 QAction；排序在 shell 侧完成） |
| `src/app/help_viewer.*`（新） | `HelpViewer`：qrc 内嵌 `docs/USER_GUIDE.md`，`QTextEdit::setMarkdown` 渲染，F1 打开 |
| `resources/help/user_manual.qrc`（新） | 把 `docs/USER_GUIDE.md` 以别名 `:/help/USER_GUIDE.md` 嵌入 `vedit_app_shell`；`Q_INIT_RESOURCE` 在全局作用域函数里（Qt 不允许在 namespace 内使用该宏） |
| `src/app/ux_selftest.cpp`（新） | A2 验收自测（offscreen 断言 + `--screenshots` 模式） |
| `src/app/CMakeLists.txt` | 加 `help_viewer.cpp` + qrc、`CMAKE_AUTORCC`、`vedit-app-ux-selftest` / `vedit-app-ux-screenshots` 两个 ctest |
| `src/editor/kernel/lsp_actions.cpp` | **仅 7 个标签文本**包 `QCoreApplication::translate("VEdit", …)` |
| `src/workspace/diagnostics/diagnostics_integration.cpp` | **仅命令文本字符串**包 `translate()`（id/快捷键/处理器一行未动） |
| `src/workspace/project/project_model.cpp` | **captain 授权扩域**：三条 `translate("VEdit", a == 1 ? "…" : "…")` 改成先选再传字面量（渲染文本一字不变），修复 lupdate 静态提取不到的根因 |
| `src/terminal/terminal_widget.*` | **captain 授权扩域**：`event()` 里对无修饰键 F1 接受 `ShortcutOverride`，终端有焦点时 F1 进 PTY |
| `src/app/main.cpp` | **captain 授权：只改 3 行断言**（见 §5） |
| `translations/vedit_zh_CN.ts` | `lupdate -locations absolute src` 重生成 + 补齐中文（含 A2 新串与 A4 页面新串） |

**未改**：`src/workspace/settings/**`、`crates/**`、命令 id、既有快捷键语义（只新增 F1 / 无其它新绑定）。

---

## 2. 菜单与工具栏（实现要点）

- **顺序**：`ApplicationShell::buildMenuBar()` 用固定列表 `file, edit, view, project, help` 排序，
  未登记的菜单 id 排在其后；空菜单不显示。因此 Project（由 diagnostics lane 在 shell 构造之后
  注册）也落在第 4 位，与注册顺序无关。
- **Preferences**：命令 id `settings.preferences` 不变、Ctrl+, 不变，菜单从 `settings` 改为 `edit`，
  `order=100`（Edit 最后一项）；`settings` 菜单不再存在（`menus()` 里也没有该 id）。
- **工具栏**：`QToolBar` objectName `vedit-main-toolbar`，`movable/floatable`，View → Toolbar 可开关；
  按用户给定顺序：New, Open, Open Folder, Save, Save All, Undo, Redo ｜分隔｜ Search in Files,
  Lint Project, Format Document。每个按钮就是 `CommandRegistry` 里的**同一个 QAction 指针**
  （`a2.toolbar.shares-registry-actions` 断言指针相等），所以按钮与菜单项不可能分叉。
  Lint/Format 由 diagnostics lane 注册；缺失时跳过而不是伪造。
- **撤销/重做不是死按钮**：命令处理器触发当前编辑器**自己的**动作：
  KTextEditor 的 `edit_undo`/`edit_redo`（在内部 KateViewInternal 的 QAction 表里，按 objectName 找，
  shell 不 include KTextEditor），纯文本 provider 回退到 `undo()`/`redo()` 槽。
  可用性来自该动作的 `isEnabled()`（并监听其 `changed` 信号 → 重同步），无活动视图时置灰。
  真实 KTextEditor 文档的“打字→撤销→重做”由 `a2.undo.real-*` / `a2.redo.real-*` 断言。
- **搜索**：`search.focus` 命令显示 search dock 并按 panel 的稳定 objectName `search-query`
  聚焦/全选输入框（`a2.toolbar.search-focuses-query`）。
- **New / Save All**：New 打开一个新的空文档 tab（未改动的空 tab 复用，不堆叠）；
  Save All 保存所有“有文件且脏”的文档，未命名脏文档明确跳过并在状态栏报告（不静默丢弃）。

---

## 3. i18n：数字与根因修复

命令（在 `vedit-qt6-build:local` 内、workspace 挂到 `/vedit-build`）：

```sh
/usr/lib/qt6/bin/lupdate -locations absolute src -ts translations/vedit_zh_CN.ts
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm
```

| 时点 | messages | active(有译文) | vanished | unfinished | lrelease |
|---|---|---|---|---|---|
| 基线（本 lane 接手时） | 942 | 936 | 6 | 0 | 936 finished |
| 首次重生成（未修根因） | 944 | 930 | 12 | 2 | 930 finished —— **少了 6 条** |
| **最终（根因修复后）** | **1025** | **1018** | 7 | **0** | **1018 finished / 0 unfinished** |

**那 6 条为什么一度丢了（根因修复前后对比）**：
`src/workspace/project/project_model.cpp` 原来把三元表达式作为 `translate()` 的第二参数：

```cpp
// 修复前：lupdate 静态提取不到 → 6 条 " · %1 filelist(s)/variable(s)/line(s) skipped"
//         被标 vanished，中文从 .qm 里消失（运行时仍会查表，属于真回归）
text += QCoreApplication::translate(
            "VEdit", lists == 1 ? " · %1 filelist" : " · %1 filelists").arg(lists);
```

```cpp
// 修复后：先选分支、再传字面量（渲染文本完全不变）；lupdate 原生提取
text += (lists == 1 ? QCoreApplication::translate("VEdit", " · %1 filelist")
                    : QCoreApplication::translate("VEdit", " · %1 filelists")).arg(lists);
```

验证（`translations/vedit_zh_CN.ts` 解析）：修复后这 6 条都是 **type 为普通（非 vanished）**
且带原生 `<location filename="../src/workspace/project/project_model.cpp" line="379..391"/>`。
→ 不再需要（也不再使用）任何“手工恢复 vanished”的脚本逻辑；`tools/fill_translations.py` 现在
只负责把 `type="unfinished"` 的条目补中文。

补充：
- `lupdate -locations absolute` 的 location 为绝对路径（容器内 `/vedit-build/src/...`），符合任务要求。
- 新增中文覆盖：A2 菜单/工具栏/帮助串 + 右键 7 标签 + diagnostics 命令文本 + panel toggles，
  以及同一扫描里出现的 A4 设置页新串（.ts 是共享产物，一并补齐，0 unfinished）。
- `&Settings` 变为 vanished（菜单标题已改为 `&Edit`），这是预期：它不再被任何源引用。

---

## 4. F1 与终端：双向实测（captain 裁决 4）

实现照仓库既有 `ShortcutOverride` 先例（`src/editor/kernel/find_replace.cpp`）：
`TerminalWidget::event()` 对 **无修饰键 F1** 接受 `ShortcutOverride` → 窗口级 F1 不触发，
按键以普通 KeyPress 到达 `keyPressEvent()` → `functionKeySequence(F1)` = VT100 `OP` 写进 PTY。
只认 F1；F6/F8 等既有窗口快捷键语义不变。

实测在 `vedit-app-ux-selftest` 里，**真实按键 + 真实 PTY**（`stty raw -echo; exec cat -v`，
raw 模式是必需的：canonical 模式下 pty 会缓存到换行，`cat` 根本看不到 F1 字节）：

| check | 断言 | 结果 |
|---|---|---|
| `a2.f1.terminal-claims-shortcut-override` | terminal 接受 F1 的 ShortcutOverride（窗口动作不会触发） | PASS |
| `a2.f1.terminal-f1-reaches-pty` | 真实 F1 后屏幕出现 `^[OP`（= ESC O P 真进了子进程，被 `cat -v` 渲染） | PASS |
| `a2.f1.terminal-does-not-open-manual` | 终端内 F1 不打开手册 | PASS |
| `a2.f1.manual-opens-outside-terminal` | 编辑器聚焦时同一个 F1 **确实打开**帮助阅读器 | PASS |

即：**终端有焦点 → F1 进 PTY；否则 → F1 打开用户手册**，两个方向都有断言，不是推断。

---

## 5. main.cpp 两处断言的红 → 绿（captain 授权：只改 3 行）

`src/app/main.cpp` 的 `--selfcheck` 原本断言旧的 Settings 菜单结构，与 A2 契约（Settings 菜单消失、
Preferences 移到 Edit 底部）直接冲突。改动**仅这三行**：

```diff
-    check("menus.settings", shell->commands()->menus().contains(QStringLiteral("settings")));
+    check("menus.edit", shell->commands()->menus().contains(QStringLiteral("edit")));
...
     check("settings.preferences.menu",
-          preferences != nullptr && shell->commands()->menuLabel(QStringLiteral("settings"))
-              == QCoreApplication::translate("vedit::app::ApplicationShell", "&Settings"));
+          preferences != nullptr && shell->commands()->menuLabel(QStringLiteral("edit"))
+              == QCoreApplication::translate("vedit::app::ApplicationShell", "&Edit"));
```

**红**（`logs/40-selfcheck-red.log`，改前三行断言时同一棵树的 `vedit-app --selfcheck`）：

```
CHECK menus.file PASS
CHECK menus.view PASS
CHECK menus.settings FAIL
CHECK menus.help PASS
CHECK settings.preferences.exists PASS
CHECK settings.preferences.menu FAIL
CHECK settings.preferences.shortcut PASS Ctrl+,
```

**绿**（`logs/41-selfcheck-green.log`，改后同命令）：`312 PASS / 0 FAIL`，exit 0，其中
`CHECK menus.edit PASS`、`CHECK settings.preferences.menu PASS`。

> 说明：`git diff` 显示该文件相对 HEAD 还有大量改动（+429 行）——那是 A4 的写域（`QT_SCALE_FACTOR` / 设置页）
> 加上本批次之前就存在的未提交工作，**不是本 lane 的**；本 lane 只落了上面 3 行。旁边的旧注释（“so today it still reads "&Settings"”）
> 按 captain “其它任何行都不许碰”保留未动，已作为 finding 上报。

---

## 6. 途中发现并修复的缺陷（A2 自身引入，已修）

在把 `syncEditorCommandStates()` 接到 `EditorPane` 的“状态变化”回调后，产品 `--selfcheck` 出现
SIGSEGV。崩溃报告（`/tmp/.local/state/vedit/crash-*.log`）栈：

```
QWidget::actions() ← EditorPane::editorActionEnabled() ← EditorArea::activeEditorActionEnabled()
← ApplicationShell::syncEditorCommandStates() ← pane stateChanged ← KTextEditor
Document::modifiedChanged ← ... ← EditorPane::setProvider()
```

根因：`EditorPane::setProvider()` 在循环里 **delete 旧 editor 后再加载新文档**，加载会触发
`modifiedChanged` → shell 同步 → 读 `editorWidget()`，而 `editor_` 还指着已删除的旧 widget。
修复：`setProvider()` 进入替换循环前把 `editor_` 置空（瞬间状态读作“无活动视图”，
末尾 `syncCurrentEditor()` 恢复），保证 `editor_` 永远指向存活 widget 或为空。
修复后产品 `--selfcheck` 312/312 通过。

---

## 7. 命令、结果与复现

```sh
# 构建（pinned image，workspace 挂到 /vedit-build；注意 rootless docker 不能加 --user）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e HOME=/tmp \
  vedit-qt6-build:local bash -lc 'cmake --build target/qt6-build --parallel $(nproc)'

# A2 自测 + 产品自检
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app-ux-selftest
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck --state-dir /tmp/s

# 受影响 ctest
cd target/qt6-build && ctest -R "vedit-app-ux|vedit-app-shell-selftest|vedit-app-i18n|vedit-terminal-selftest|vedit-edit-core-selftest|vedit-workspace-project-selftest"

# 截图（真实 xcb + Xvfb；容器内先装随包 CJK 字体，否则中文是豆腐块）
cp packaging/runtime/assets/fonts/NotoSansCJK-Regular.ttc /usr/share/fonts/truetype/ && fc-cache -f
xvfb-run -a -s "-screen 0 1600x1000x24" \
  target/qt6-build/src/app/vedit-app-ux-selftest --screenshots evidence/ux-batch-2/a2/screenshots

# 翻译
/usr/lib/qt6/bin/lupdate -locations absolute src -ts translations/vedit_zh_CN.ts
python3 evidence/ux-batch-2/a2/tools/fill_translations.py translations/vedit_zh_CN.ts
/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts -qm /tmp/v.qm
```

观察到的结果：

- `A2-UX-SELFTEST failures=0`（53 个 CHECK 全 PASS，见 `logs/31-ux-selftest.log`）
- `APP-SHELL-SELFTEST ok=1 checks=312 failures=0`（`logs/41-selfcheck-green.log`）
- `ctest`：`100% tests passed, 0 tests failed out of 7`（`logs/30-ctest-affected.log`）
- 全量 `ctest`：`100% tests passed, 0 tests failed out of 42`（`logs/32-ctest-all.log`）
- `lrelease`：`Generated 1018 translation(s) (1018 finished and 0 unfinished)`（`logs/11-lrelease-final.log`）
- 截图：`SCREENSHOT OK …` ×3（`logs/20-screenshots.log`），1600×950

---

## 8. 未验证 / 已知局限（诚实声明）

1. **打包形态未重验**：手册走 qrc（编进二进制），因此**不需要**改 `packaging/runtime/assemble-appdir.sh`
   ——这也避免了与 A3 的写域冲突；但本轮没有重打 AppImage/AppDir，`/help/USER_GUIDE.md` 在打包产物里的
   可见性只由“资源在二进制内 + 该二进制已跑通”间接证明。
2. **ctest 自动截图无 CJK 字体**：`vedit-app-ux-screenshots` 在构建镜像里跑，镜像只有 DejaVu，中文渲染成
   豆腐块（检查本身 PASS，只抓图）。`evidence/…/screenshots/` 里的图是**手动装入随包 NotoSansCJK 后**
   重抓的真实观感；两条路径都在 `logs/20-screenshots.log` 里。
3. **整套 7 gate 未跑**：只跑了与 A2 相关的 build / ctest 子集 / 产品 selfcheck / lrelease；
   `clippy`/`fmt` 属 Rust lane，`docs`/`manifest`/`pack` 未动。captain 已说明另行做产品级收口。
4. **F1 的边界**：只对“无修饰键 F1”做透传；`Shift+F1`/`Ctrl+F1` 仍会走窗口快捷键路径（未改）。
   终端透传用 `ShortcutOverride`，若未来有 lane 在 editor 之上再加 F1 级拦截，需要复核这条。
5. **.ts 是共享产物**：本次重生成把 A4 设置页的最新串一并补齐（0 unfinished）；若 A4 之后继续改文案，
   需要再跑一次 `lupdate + fill_translations.py`，否则新串会是 unfinished。
6. **工作树带有本批次之前的未提交改动**：`git diff --stat` 反映的是工作树 vs HEAD，不能当作本 lane 的
   单 lane diff；`translations/`、`lsp_actions.cpp`、`help_viewer.*` 等仍是 untracked。本轮没有提交，
   交付时按仓库流程随分支提交。
7. **模拟 lane 的测试替身**：ux-selftest 里 Project 菜单/Build/Format 由测试自己注册同 id 命令来模拟
   diagnostics lane（真实产品里有该 lane）；真实产品的菜单顺序/产品 selfcheck 已覆盖同一路径。

---

## 9. 证据文件索引

```
evidence/ux-batch-2/a2/
├── README.md                      本文件
├── logs/
│   ├── 10-lrelease.ts.log         首次（未修根因）lrelease 计数
│   ├── 11-lrelease-final.log      最终 lrelease：1018 finished / 0 unfinished
│   ├── 20-screenshots.log         xcb+Xvfb 抓图日志
│   ├── 30-ctest-affected.log      受影响 ctest 7/7
│   ├── 32-ctest-all.log           全量 ctest 42/42
│   ├── 31-ux-selftest.log         A2 全部 53 个 CHECK
│   ├── 40-selfcheck-red.log       改 main.cpp 断言前：2 个 FAIL（红）
│   ├── 41-selfcheck-green.log     改后：312 PASS / 0 FAIL（绿）
│   └── 50-tree-facts.log          文件清单 / main.cpp 3 行 / sha256
├── screenshots/
│   ├── 10-main-toolbar-menu.png   英文界面：菜单顺序 + 主工具栏
│   ├── 11-user-manual.png         F1 帮助阅读器渲染 docs/USER_GUIDE.md（中文正常）
│   └── 12-main-menu-zh.png        中文界面：5 菜单 + 工具栏全中文
└── tools/
    └── fill_translations.py       重生成后补齐 unfinished 的脚本（含翻译表）
```
