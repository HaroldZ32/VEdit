# RM-8 — 内置用户手册重写为 Qt6 用户手册

**任务**：`docs/USER_GUIDE.md`（同时是内置帮助，经 `resources/help/user_manual.qrc` 嵌入、由 `src/app/help_viewer.cpp` 渲染）
被真实用户评价为「上古时期的内容」「还是 GTK3 时期的」。本行把它重写为一本**面向 Qt6 产品使用者**的中文手册。

**写范围**：`docs/USER_GUIDE.md`、`evidence/ux-batch-2/rm8-manual/`。未改 qrc、help_viewer 或任何源码。

## 交付

- `docs/USER_GUIDE.md`：604 行 / 42,837 字节，10 个用户章节 + 4 个附录。
  - 开头即用户材料：这是什么 → 界面总览（每块区域、每个面板、状态栏、帮助入口）→ 打开/切换/恢复工程 → 编辑 → 搜索 → 模块树/接口 → 诊断 → 导航/多光标 → 设置 → 语言/显示 → 已知限制。
  - 安装、配置、排障、VCS 安全边界全部移到附录，且附录 A 明确标注为部署说明。
  - 菜单/按钮名使用代码里的实际英文标签（中文界面会显示译名），关键句给出行为与失败时的用户可见反馈。
- 旧内容处理：GTK3 时代的安装三种产物、`vd-app`、GtkSourceView、GTK 输入法模块、WenQuanYi 字体、
  `cargo`/旧构建步骤、旧面板名（网络/驱动/负载）、旧快捷键表全部删除；不保留任何「旧版 GTK3 产物」章节
  （附录 A.3 只说明旧封装不在本文范围，不描述旧产品）。
- 键位表按**当前注册表**填写，并在 §7.3 末尾注明「按当前版本记录，后续版本调整默认键位/手势时同步更新」，
  对应后续 RM-6（Alt 手势）与 RM-5b（默认键位）落地后刷新。

## 验证（命令与观察输出）

### 1) 文档门禁 — PASS

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

完整输出：`logs/01-docs-gate.log`。

### 2) 陈旧词汇扫描 — 0 命中（全文，不限于用户章节）

```
$ grep -n -E "<stale vocabulary>" docs/USER_GUIDE.md
terms: GTK3|GtkSourceView|gtk3|gtksourceview|vd-app|GTK_IM_MODULE|VEDIT_GSV|WenQuanYi|im-xim|ibus-gtk3|cargo build|vedit\.sh|--selftest all|网络 ?/ ?驱动 ?/ ?负载
hits=0
```

完整输出：`logs/02-stale-vocabulary.log`。

### 3) 结构与规模

`logs/03-structure-headings.log`：`grep -n '^#'` 的完整标题列表 + `wc -l -c` + `git diff --stat`。
摘要：

- 604 行；`git diff --stat` = `1 file changed, 517 insertions(+), 550 deletions(-)`（旧 637 行 → 新 604 行，整篇重写）；
- 标题：`#` 一级 1 个（`# VEdit 用户手册`）；`##` 二级 15 个（目录 + 10 章 + 4 附录）；`###` 三级 48 个；
- 用户章节从第 24 行开始（`## 1. 这是什么：界面总览`），目录在第 7–22 行；
- 9 张表格、4 个代码块（均为 `sh`）。

### 4) 帮助查看器的渲染安全性

`logs/04-markdown-shape.log`：

```
$ grep -n -E "<[a-zA-Z/][^>]*>|\[\^[^]]*\]|^\s*\[[^]]*\]:"
forbidden_hits=1 (1 = none)

tables=9 fences=8
$ grep -n USER_GUIDE resources/help/user_manual.qrc
14:        <file alias="USER_GUIDE.md">../../docs/USER_GUIDE.md</file>
```

- 无 HTML 块、无脚注、无链接引用定义；目录是纯文本列表，不承诺可点击锚点（`QTextEdit::setMarkdown`
  没有 `QTextBrowser` 的链接点击面，见 `src/app/help_viewer.cpp`：`QTextEdit` + `setMarkdown`）。
- 表格保留（旧手册已有表格并能在帮助窗口渲染，见 `target/qt6-build/src/app/ux-screenshots/11-user-manual.png`）；
- 嵌入路径未变，`resources/help/user_manual.qrc:14` 仍然指向 `../../docs/USER_GUIDE.md`，下一次构建即携带新文本。

## 断言表（claim → 依据）

行号在 2026-11-06、分支 `feat/ux-batch-2` 上取样。**注意**：`src/app/app_shell.cpp` 与
`src/workspace/diagnostics/diagnostics_integration.cpp` 正被同批的其他 RM 行修改，行号可能漂移；
每条断言以其引用的符号/原文为准，不依赖行号本身。

| 手册断言 | 依据（路径:行，符号/原文） |
|---|---|
| 菜单固定 File/Edit/View/Project/Help | `src/app/app_shell.cpp:511-519`，`setMenuLabel` ×5 + 注释「the menu bar is FIXED in this order」 |
| 主工具栏顺序 New/Open/Open Folder/Switch Project/Save/Save All/Undo/Redo \| Search in Files/Lint Project/Format Document | `src/app/app_shell.cpp:853-857`，`kToolBarOrder` |
| 面板名与默认停靠区：Files/Project/Source Control/Search/Terminal/Outline | `src/app/app_shell.cpp:484-491`（`addPanel`）；`src/workspace/project/project_integration.cpp:82-101`（Project dock，标题 `Project`） |
| Module Tree / Interface 两个右侧面板 | `src/workspace/module_tree/module_tree_integration.cpp:453-478`（`addPanel`，标题 `Module Tree` / `Interface`） |
| Outline 面板 = 大纲 + 诊断列表 | `src/workspace/diagnostics/diagnostics_panel.cpp:79-120`（标题 `Diagnostics`、`view_` 大纲树、`issues_` 分组列表）、`:390-442`（计数与分组） |
| View 菜单的面板开关（含模块树/接口/工程；后注册面板也会补进菜单） | `src/app/app_shell.cpp:610-634`（`panelCommands[]`）、`:1284-1350`（`syncDockPanelCommands`，含 `view.dock.*` 分支） |
| 首启显示 Files、Outline；模块树+接口在安装时显示 | `src/app/app_shell.cpp:215-219`；`src/workspace/module_tree/module_tree_integration.cpp:498-501` |
| 状态栏左=工程名+一次性提示，右=未保存/主题/分屏统计/诊断徽标 | `src/app/app_shell.cpp:455-459`（`addPermanentWidget` vs `addWidget`）、`:1402-1422`；徽标 `src/workspace/diagnostics/diagnostics_integration.cpp:883-887, 2247-2249`（`LSP backend=… · lint=…`） |
| F1 / Help → User Manual 打开独立只读窗口 | `src/app/app_shell.cpp:646, 742, 910-917`；`src/app/help_viewer.cpp:24-46`（`QTextEdit` 只读 + `setMarkdown`） |
| Diagnostics 打开同一个右侧面板（Ctrl+Alt+D） | `src/workspace/diagnostics/diagnostics_integration.cpp:2651-2659`（`view.panel.diagnostics` → `showPanel("outline")`） |
| Open… 的三组过滤器 | `src/app/app_shell.cpp:47-52`（`openFileDialogFilter`） |
| 选中的目录就是工程根（不向上查找） | `src/app/app_shell.cpp:385-403`（`openProjectFolderDialog` → `setProjectRoot(info.absoluteFilePath())`） |
| Open Folder 与 Switch Project 走同一条路径 | `src/app/app_shell.cpp:552-556, 672-675`（两个 handler 都调用 `openProjectFolderDialog`） |
| `--project` 启动参数 | `src/app/main.cpp:1830-1834` |
| 切根时六个面板一起跟随 | `src/workspace/filetree/filetree_integration.cpp:128`；`search/search_integration.cpp:59`；`vcs/vcs_integration.cpp:89`；`module_tree/module_tree_integration.cpp:484`；`diagnostics/diagnostics_integration.cpp:2551`；`project/project_integration.cpp:133` |
| 切根不关闭已打开文档 | `src/app/app_shell.cpp:352-375`（`setProjectRoot` 只改 root/标题/广播，不触碰 `editorArea_` 的文档） |
| 打开 `.f`：无工程时以其目录为根并导入；有工程时只导入 | `src/workspace/project/project_integration.cpp:114-127`（`planOpenedFilelist` 分支与注释） |
| 重复导入提示 `Already imported: nothing changed (…Refresh…)` | `src/workspace/project/project_panel.cpp:218-226` |
| Project 面板按钮/列表 | `src/workspace/project/project_panel.cpp:66-88`（Refresh/Import）、`:147`（Edit Variables…）、`:92-170`（Filelists/Files/-I/-D/Variables/Diagnostics） |
| 变量对话框只填「已引用但未解析」的名字，不能增删 | `src/workspace/project/project_variables_dialog.cpp:12-50`（行由 `names_` 生成，无增删控件）；持久化 `src/workspace/project/project_state.cpp:87`（`project-model.json`） |
| `.f` 变量语法 `$NAME`/`${NAME}`/`$(NAME)`/`$$`/`~/`，工程表优先于环境 | `crates/vd-config/src/filelist.rs:6-10, 405, 421-427`；环境优先级同文件注释 |
| 嵌套 `-f` 深度上限 16 | `crates/vd-config/src/filelist.rs:32`（`MAX_DEPTH: u32 = 16`） |
| 会话恢复字段 / Save Session Now / --no-session-restore / 损坏提示 | `src/app/session_store.h:1-50`；`src/app/app_shell.cpp:682-686, 198-216`；`src/app/main.cpp:1849-1851` |
| 路径显示规则（根内相对、根外绝对；标签文字=文件名、提示=路径） | `src/common/path_display.h`；`src/app/editor_area.cpp:604` |
| 标签 `×` 关闭；未保存弹 Save/Discard/Cancel | `src/app/editor_area.cpp:426-436`；`src/app/app_shell.cpp:1456-1480` |
| 分屏 Ctrl+Shift+H/V、F6、Ctrl+W；关闭分屏逐个询问 | `src/app/app_shell.cpp:583-595, 577-578`；`src/app/app_shell.cpp:1485-1516`（`confirmPaneClose`） |
| Reset Layout / New Empty Layout 会重建布局并关闭其中的文档 | `src/app/editor_area.cpp:1749-1756`（`resetLayout` → `clearTree` → 新建 pane）、`:306-321`（pane 析构删除 documents）；`src/app/app_shell.cpp:676-679, 714-717` |
| Undo/Redo 命令无自带键位，驱动活动分屏 | `src/app/app_shell.cpp:522-531`（注释：编辑器自己的 Ctrl+Z/Ctrl+Y 不被 shell 抢）+ `:729-733` |
| 保存/另存/全部保存行为与原子写 | `src/app/app_shell.cpp:563-572, 922-971`；`src/editor/kernel/atomic_save.cpp:94-131`（临时文件 + rename 为提交点） |
| 查找浮条键位 Ctrl+F/H、F3/Shift+F3、Esc | `src/editor/kernel/find_replace.cpp:544-551`；关闭键在其事件过滤器 |
| 查找选项与范围、全工程交给 Search 面板 | `src/editor/kernel/find_replace.cpp:380-401, 914, 999` |
| Search 面板控件、三列结果、2000 命中/15 秒 | `src/workspace/search/search_panel.cpp:92-169, 193, 447, 467` |
| Replace All 确认文案与「先保存再替换」 | `src/workspace/search/search_panel.cpp:1646-1690` |
| 替换写前拒绝的五类原因 | `src/workspace/search/search_panel.cpp:775-813, 1134-1175, 1201-1222` |
| 工程级 RE2（无反向引用/lookaround），当前文件 PCRE | `src/workspace/search/search_panel.cpp:116-117`（正则按钮 tooltip） |
| 模块树列/按钮/过滤（匹配任一列、父行保留） | `src/workspace/module_tree/module_tree_panel.cpp:88-110, 322-343` |
| 模块树顶层/空态/汇总 | 同文件 `:145-190`（`setCatalog` 分支与状态行） |
| 模块树上限 64 层 / 20000 行 | `src/workspace/module_tree/module_hierarchy.h:29-32` |
| 接口列、方向原样、宽度推导、焦点空态 | `src/workspace/module_tree/interface_panel.cpp:84-90, 183, 228-236` |
| 诊断上下位规则（上位覆盖下位，编辑后回落） | `src/workspace/diagnostics/diagnostics_controller.h:133-158`（live upper-layer API 与注释） |
| 保存后自动工程 lint：700 ms 去抖、单飞、静默 | `src/workspace/diagnostics/project_lint_trigger.cpp:13, 44-96`（注释「Single-flight: a running pass is cancelled, never queued behind」） |
| 模态框只给显式 Lint Project；自动跑只给可点击状态提示 | `src/workspace/diagnostics/project_lint_notice.h:1-20`；`project_lint_notice.cpp:95, 144` |
| 悬浮修复只在可修时给 Fix，一次撤销 | `src/workspace/diagnostics/diagnostic_hover.cpp:112-140`；`diagnostics_integration.cpp:1524`（一次撤销提示原文） |
| 文件树状态点红/黄/绿/无点 | `src/workspace/filetree/file_status_delegate.cpp:18-28, 76-92`（dot 位于 icon 与名字之间） |
| F12/Shift+F12/F2/Ctrl+Shift+F | `src/editor/kernel/lsp_actions.cpp:363-390`（`editorActionShortcut`） |
| F8/Shift+F8 诊断导航 | `src/editor/kernel/lsp_actions.cpp:865-874`（事件过滤） |
| 多光标 Alt+点击 / Alt+拖拽、并说明 WM 抢占 | `src/editor/kernel/multi_cursor.cpp:604-712`（`handleMouse` 注释：Alt+CLICK 加光标、Alt+DRAG 块选） |
| 编辑器键位 Ctrl+/、Alt+↑/↓、Shift+Alt+↓ 等 | `src/editor/kernel/multi_cursor.cpp:872-905`（`commandForKey`）；多光标编辑用 `KTextEditor::Document::EditingTransaction`（一次撤销） |
| 设置页签 Shortcuts/Tools/FIM/Appearance/Lint/Format | `src/workspace/settings/settings_panel.cpp:325-333` |
| 首选项是延迟保存：预览，Apply/OK 落盘，Cancel 放弃 | `src/workspace/settings/settings_dialog.cpp:38-44`（`setDeferredPersistence(true)` + OK/Cancel/Apply） |
| 主题即时预览、Apply/OK 后保存 | `src/workspace/settings/settings_panel.cpp:814-832`（`applyTheme` → `persist()`）；`:881-900`（deferred 分支的 Preview 文案） |
| 快捷键表三列、default/override、冲突红字提示 | `src/workspace/settings/settings_panel.cpp:491-520, 1543-1600` |
| Tools 页（语言/LSP/Lint/工具路径/-I/-D） | `src/workspace/settings/settings_panel.cpp:538-616` |
| FIM 页字段与密钥来源 | `src/workspace/settings/settings_panel.cpp:635-713, 1402-1502` |
| 外观页：字号立即、缩放 100/125/150/200 下次启动 | `src/workspace/settings/settings_panel.cpp:1767-1810`；`src/workspace/settings/ui_scale.h:31` |
| Lint 页两套配置、置灰原因、`--autofix` 只保存 | `src/workspace/settings/settings_panel.cpp:1814-1905, 2185-2220`；规则文件优先级 `src/workspace/settings/lint_options.h:199-208` |
| Format 页 23 个旋钮 + Restore defaults + 默认不发参数 | `src/workspace/settings/format_options.h:65`（`kFormatKnobs`，实测 23 条）；`settings_panel.cpp:1975-1980` |
| 界面语言 Auto/English/简体中文，重启生效；Auto 在 AppImage 落英文 | `settings_panel.cpp:538-552`；`src/app/i18n.cpp`（`languageTag` 用 `QLocale::system()`）；`packaging/runtime/AppRun:89-133`（仅 C.utf8，回退导出 LANG/LC_ALL） |
| 中文翻译随包 `bin/translations/` | `packaging/runtime/assemble-appdir.sh:104-106`；`src/app/i18n.cpp`（`translationCandidates` 先找 `<bin>/translations`） |
| 字体：Noto Sans CJK 随包；拉丁 DejaVu；CJK 1.60× | `packaging/runtime/README.md`（CJK font fallback 节）；`AGENT.md` T119 |
| AppImage 未随包 Qt 输入法平台插件 | `packaging/runtime/out/AppDir/plugins/` 仅 `platforms/`、`xcbglintegrations/`、`kf6/parts/`；`AppRun:64`（`QT_PLUGIN_PATH` 收窄到 AppDir） |
| AppImage 产物/入口、FUSE 备选、源码构建、容器矩阵 | `packaging/runtime/README.md`（产物表、FUSE 限制、交付形态）；`packaging/runtime/assemble-appdir.sh:27, 93-95`；`AppRun:174`；`docs/BUILD.md`（构建命令） |
| 状态目录与五个状态文件 | `src/app/app_shell.cpp:63-67`（AppConfigLocation + `session.json`）；`src/workspace/settings/settings_integration.cpp:20-25`（`settings.toml`）；`src/workspace/project/project_state.cpp:87`（`project-model.json`）；`src/app/app_shell.cpp:90`（`shortcuts.json`）；FIM 密钥目录逻辑 `settings_panel.cpp:1402-1502` |
| 启动参数（文件、--project、--state-dir、--theme、--no-session-restore） | `src/app/main.cpp:1826-1851` |
| VCS 两页签/三栏/按钮/历史控件 | `src/workspace/vcs/vcs_integration.cpp:60-80`；`vcs_panel.cpp:704-800`；`vcs_history.cpp:150-216` |
| VCS 随包 git/svn、bundled > PATH > env | `AGENT.md` §VCS 工具链；`vcs_integration.cpp:80-83`（`reportToolchainOnce`） |
| VCS 上限 200/20000/2 MiB/2000 | `src/workspace/vcs/vcs_source.h:234`（`kMaxHistoryPage`）；`crates/vd-vcs/src/history.rs:32-37`（`MAX_PAGE`/`MAX_BLAME_LINES`/`MAX_TEXT_BYTES`）；`crates/vd-vcs/src/lib.rs:477`（`MAX_REFS`） |
| VCS 安全边界（读拒写、白名单、两次确认、凭据只进环境） | `AGENT.md` §VCS 安全边界；`crates/vd-vcs/tests/readonly.rs`（正反用例）；`src/workspace/vcs/vcs_capabilities.cpp`（不支持操作的真实原因） |

## 未能证明 / 本次未做

1. **没有真机 GUI 点击验收**：本行只改文档，没有在真实桌面上逐面板点一遍；所有 UI 断言来自**代码与随包翻译目录**，
   不是一次现场操作。可用的界面截图（`target/qt6-build/src/app/ux-screenshots/`）是 shell demo 面板，不能当作产品面板证据。
2. **没有重建 AppImage**（任务只要求 docs 门禁）：qrc 嵌入路径未变，下一次构建会带上新文本；captain 已确认当前
   AppImage 内嵌的是改动前的**当前**文本，所以这是内容替换、不是打包问题。
3. **输入法在 AppImage 内的实际行为未实测**：只确认了「未随包 `platforminputcontexts` + `QT_PLUGIN_PATH` 收窄」这一事实，
   手册按这个边界写成「取决于系统输入法与 Qt xcb 的配合」，没有声称中文输入一定可用（见发现 3）。

## 发现（超出本行写范围，未修）

1. **`README.md:176` 对本文的描述已过期**：仍写「安装三种产物、编辑/诊断/格式化/深度 lint/FIM、中文输入、排障」。
   README 不在本行写范围，需要一行更新或另行派活。
2. **`View → Reset Layout` / `File → New Empty Layout` 会无提示销毁未保存文档（数据丢失形状）**：
   `src/app/editor_area.cpp:1749-1756` 的 `resetLayout()` 先 `clearTree()`（删除所有 pane 及其中的 KTextEditor 文档，
   `src/editor/kernel/ktexteditor_provider.cpp:103-105`：「the document dies with its view」），
   两个命令的 handler（`app_shell.cpp:676-679, 714-717`）都没有经过 `confirmPaneClose`/未保存确认。
   手册已在 §3.1 写成「使用前请先保存」的警告，但建议修复（重置前走同一套未保存确认）。
3. **AppImage 不随包 Qt 输入法平台插件**：`AppDir/plugins/` 只有 `platforms/` 与 `xcbglintegrations/`，而 `AppRun:64`
   把 `QT_PLUGIN_PATH` 收窄到 AppDir，宿主 ibus/fcitx 的 Qt 插件不会被加载。中文输入能否工作取决于 XIM 路径，
   未实测。手册 §9.2 已如实披露；是否随包插件或验证 XIM 需要产品决定。
4. **（轻微）首选项里主题的状态文字与实际保存时机不一致**：`settings_panel.cpp:826-830` 在 deferred 模式下仍写
   「applied and stored by the Rust service」，而 `persist()` 的 deferred 分支（`:881-900`）只标记预览、等 Apply/OK。
   手册按真实行为写（选中即时预览，Apply/OK 保存），代码文案可另行修。
