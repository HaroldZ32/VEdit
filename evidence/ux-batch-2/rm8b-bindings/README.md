# RM-8b / T33 — 手册的手势与快捷键表定稿（静态默认版）+ §2/§3.1/§6 追补

**任务**：T18（RM-8b）定稿两张表；T33 是设计变更后的收尾——运行时“rehome”被删除，
`view.toolbar` 与 `view.panel.diagnostics` 改为**静态默认**，手册里不能再出现“启动时搬运”的规则。

**最终形态（当前树）**：

- 45 条注册表命令 = **34 条主键位 + 2 条备用默认 + 11 条无默认键位**；
- 产品级自检 `commands.no-desktop-global-shortcuts` 对**所有车道**的每条绑定（含备用默认）断言：
  不得是桌面全局键；该自检在 `evidence/ux-batch-2/rm5c-defaults/product-selfcheck.log:376` 为 PASS；
- **没有任何运行时重定位**：`grep -rn rehomeDesktopGlobalDefaults src` 无命中；
- `view.toolbar` = `Ctrl+Alt+Shift+T`（源码静态字面量，`app_shell.cpp:660-661`）；
  `view.panel.diagnostics` = `Ctrl+Alt+Shift+D`（源码静态默认，`diagnostics_integration.cpp:3013`）；
- 备用默认：`edit.redo` 加 `Ctrl+Y`（`app_shell.cpp:581`）、`diagnostics.format` 加 `Alt+Shift+F`
  （`app_shell.cpp:274`；源码字面量 `Shift+Alt+F`，快捷键页/快照打印为 `Alt+Shift+F`）；
- 新命令：`View → Next Tab`（`Ctrl+Tab`）/ `View → Previous Tab`（`Ctrl+Shift+Tab`）
  （`app_shell.cpp:674-677`）。

**写范围**：`docs/USER_GUIDE.md`（两张表及周边文字；为一致性也更新了 §1.4/§2.2/§2.3/§3.1/§3.2/§3.3/
§6.2/§6.4/§6.5）、`evidence/ux-batch-2/rm8b-bindings/`。未改源码、qrc、help_viewer、translations。

## 1. 本轮的修改

| 章节 | 改动 |
|---|---|
| §8.3 说明 | **删除**“启动时把未改过的默认换成 `Ctrl+Alt+Shift+…`”的规则；改为一句静态理由：桌面占用了 `Ctrl+Alt+T` / `Ctrl+Alt+D`，所以工具栏与诊断面板的**默认**分别是 `Ctrl+Alt+Shift+T` / `Ctrl+Alt+Shift+D` |
| §8.3 表 | 45 行；`View → Diagnostics` = `Ctrl+Alt+Shift+D`（值，不是规则）；`Toolbar` = `Ctrl+Alt+Shift+T`；`Next Tab` / `Previous Tab` 作为**存在的命令**列出（`Ctrl+Tab` / `Ctrl+Shift+Tab`）；`Redo` 备注备用 `Ctrl+Y`、`Format Document` 备注备用 `Alt+Shift+F` |
| §7.2 手势 | 主 `Shift+Alt+拖拽` / `Ctrl+Alt+点击`；次 `Alt+拖拽` / `Alt+点击`；补“匹配规则对 `Shift+Alt` / `Ctrl+Alt` / 纯 `Alt` 一致”、`Meta+Alt` 排除、WM 抢 Alt 的原因 |
| §7.3 编辑器键位 | 去掉属于注册表命令的 `F12/Shift+F12/F2/Ctrl+Shift+F` 与撤销/重做；查找浮条键位标注“不是注册表命令”；注明 `Ctrl+G/Ctrl+D/Ctrl+B` 仍是编辑器组件自身键位 |
| §3.1 | `Ctrl+Shift+H` → `Ctrl+Alt+H`；Reset/New Empty/Mixed Split 的“先保存”警告改为“会先询问未保存文档，取消则原样保留” |
| §3.2 / §3.3 | 撤销/重做为 `Ctrl+Z` / `Ctrl+Shift+Z` 命令（重做备用 `Ctrl+Y`）；Save All 补 `Ctrl+Alt+Shift+S` |
| §2.2 / §2.3 | 打开/切换工程后自动全工程分析；`.f` 外部文件出现在 `From filelist (N)` 分组与 Project 面板 Files 列表并带状态点 |
| §6.2 / §6.5 | 覆盖策略（开工程全量、保存增量、结论按文件保留、去抖/单飞、显式 Lint 优先）；五态状态点（红/黄/绿/空心灰环/无标记）与覆盖范围 |
| §1.4 / §6.4 | `View → Diagnostics` 的值改为 `Ctrl+Alt+Shift+D` |

## 2. 定稿的手势表（与手册 §7.2 一致）

| 手势 | 作用 |
|---|---|
| `Shift+Alt+拖拽` | 块选择（矩形选区，每行一个光标）——**主手势** |
| `Ctrl+Alt+点击` | 加一个光标；点到已有光标上则移除该光标——**主手势** |
| `Alt+拖拽` / `Alt+点击` | 同样的块选择 / 加光标；窗口管理器不占用 `Alt` 时可用（次选手势） |
| 普通点击 / 拖拽 | 编辑器的普通光标与普通选区 |

依据：`src/editor/kernel/multi_cursor.cpp:593-605` 的 `isMultiCursorGesture()` 接受 `Alt` +
companions ∈ {无, Shift, Control, Shift+Control}，**显式排除 Meta**；`:637-651` 在 Ctrl 分支之前检查
（所以 `Ctrl+Alt+点击` 不会走到跳定义）；`:736-738` 点击（无拖拽）→ `addCursor()`，`:732-735`
拖拽 → `finishBlock()`。日志：`logs/04-gesture-code.log`。

## 3. 定稿的默认键位表（与手册 §8.3 一致，45 条）

“无默认键位”是注册表里的真实状态（`(unbound)`），不是遗漏。

| 菜单 | 命令 | 默认键位 | 注册点（当前树，行号可能随后续改动漂移） |
|---|---|---|---|
| File | New | `Ctrl+N` | `app_shell.cpp:591` |
| File | Open… | `Ctrl+O` | `app_shell.cpp:593` |
| File | Open Folder… | `Ctrl+Alt+Shift+O` | `app_shell.cpp:602` |
| File | Switch Project… | `Ctrl+Alt+Shift+P` | `app_shell.cpp:610` |
| File | Go to File… | `Ctrl+P` | `palette_integration.cpp:98,101` |
| File | New Empty Layout | `Ctrl+Alt+N` | `app_shell.cpp:612` |
| File | Save | `Ctrl+S` | `app_shell.cpp:618` |
| File | Save As… | `Ctrl+Shift+S` | `app_shell.cpp:622` |
| File | Save All | `Ctrl+Alt+Shift+S` | `app_shell.cpp:628` |
| File | Save Session Now | 无默认键位 | `app_shell.cpp:630`（`QKeySequence()`） |
| File | Quit | `Ctrl+Q` | `app_shell.cpp:636` |
| Edit | Undo | `Ctrl+Z` | `app_shell.cpp:574` |
| Edit | Redo | `Ctrl+Shift+Z`（备用 `Ctrl+Y`） | `app_shell.cpp:576` + `:581` |
| Edit | Preferences… | `Ctrl+,` | `app_shell.cpp:585` |
| View | Split Pane Horizontally | `Ctrl+Alt+H` | `app_shell.cpp:644` |
| View | Split Pane Vertically | `Ctrl+Shift+V` | `app_shell.cpp:646` |
| View | Focus Next Pane | `F6` | `app_shell.cpp:648` |
| View | Next Tab | `Ctrl+Tab` | `app_shell.cpp:674` |
| View | Previous Tab | `Ctrl+Shift+Tab` | `app_shell.cpp:676` |
| View | Close Pane | `Ctrl+W` | `app_shell.cpp:634` |
| View | Reset Layout | `Ctrl+Alt+R` | `app_shell.cpp:652` |
| View | Mixed Split (demo) | `Ctrl+Alt+M` | `app_shell.cpp:650` |
| View | Toolbar | `Ctrl+Alt+Shift+T` | `app_shell.cpp:660-661`（静态字面量 `Ctrl+Shift+Alt+T`） |
| View | Search in Files | `Ctrl+Alt+Shift+F` | `app_shell.cpp:669` |
| View | Toggle Files Panel | `Ctrl+Alt+F` | `app_shell.cpp:686` + `:697` |
| View | Toggle Search Panel | `Ctrl+Alt+S` | `app_shell.cpp:687` + `:697` |
| View | Toggle Terminal Panel | `Ctrl+Alt+J` | `app_shell.cpp:688` + `:697` |
| View | Toggle Source Control Panel | `Ctrl+Alt+C` | `app_shell.cpp:691` + `:697` |
| View | Toggle Outline Panel | `Ctrl+Alt+O` | `app_shell.cpp:692` + `:697` |
| View | Diagnostics | `Ctrl+Alt+Shift+D` | `diagnostics_integration.cpp:3013`（静态默认） |
| View | Toggle Module Tree Panel | 无默认键位 | `app_shell.cpp:1428`（spec 无 defaultShortcut） |
| View | Toggle Interface Panel | 无默认键位 | 同上 |
| View | Toggle Project Panel | 无默认键位 | `app_shell.cpp:1473`（`view.dock.*`，无 defaultShortcut） |
| View | Use tree-sitter parsing | 无默认键位 | `main.cpp:2035`（spec 无 defaultShortcut） |
| Project | Lint Project | 无默认键位 | `diagnostics_integration.cpp:2959`（`QKeySequence()`） |
| Project | Reparse Project | 无默认键位 | `diagnostics_integration.cpp:3021` |
| Project | Go to Definition | `F12` | `diagnostics_integration.cpp:2933` |
| Project | Find References | `Shift+F12` | `diagnostics_integration.cpp:2936` |
| Project | Rename Symbol... | `F2` | `diagnostics_integration.cpp:2940` |
| Project | Format Document | `Ctrl+Shift+F`（备用 `Alt+Shift+F`） | `diagnostics_integration.cpp:2944` + `app_shell.cpp:274` |
| Project | Show Hover | 无默认键位 | `diagnostics_integration.cpp:2956` |
| Project | Show Backend Capabilities | 无默认键位 | `diagnostics_integration.cpp:2962` |
| Project | Reload Backend Settings | 无默认键位 | `diagnostics_integration.cpp:2965` |
| Help | User Manual | `F1` | `app_shell.cpp:723` |
| Help | About VEdit | 无默认键位 | `app_shell.cpp:725` |

注：`QKeySequence` 把修饰键顺序规范化为 `Ctrl+Alt+Shift+…` / `Alt+Shift+…`，所以表里用程序/快捷键页
实际打印的写法（源码字面量可能是 `Ctrl+Shift+Alt+…` / `Shift+Alt+…`，同一个序列）。

## 4. 逐行是怎么验证的

### 4.1 四重检查（`verify_table.py`，`logs/02-table-vs-registry.log`）

```
$ python3 evidence/ux-batch-2/rm8b-bindings/verify_table.py
manual table rows: 45
OK: 45 command ids enumerated from the sources == reference map
desktop-global sequences known: 17
OK: no primary default is a desktop-global sequence
product selfcheck snapshot: 36 bindings, 34 commands
OK: every product COMMAND-BINDING matches the manual (36 bindings, 34 commands)
OK: product selfcheck commands.no-desktop-global-shortcuts PASS
RM-5b product dump (historical): 32 commands
delta: view.toolbar: dump='Ctrl+Alt+T' -> now='Ctrl+Alt+Shift+T'  (src/app/app_shell.cpp:660-661 static Ctrl+Shift+Alt+T literal)
delta: view.panel.diagnostics: dump='Ctrl+Alt+D' -> now='Ctrl+Alt+Shift+D'  (diagnostics_integration.cpp:3013 static default (no relocation))
delta: view.tab.next: dump=None -> now='Ctrl+Tab'  (src/app/app_shell.cpp view.tab.next)
delta: view.tab.previous: dump=None -> now='Ctrl+Shift+Tab'  (src/app/app_shell.cpp view.tab.previous)
OK: the remaining 28 dump rows match the manual
RM-5c green-run bindings: 30
OK: 30 green-run rows agree with the manual table
OK: no runtime relocation machinery (rehomeDesktopGlobalDefaults absent)
OK: product gate commands.no-desktop-global-shortcuts present
unbound rows (11): diagnostics.capabilities, diagnostics.hover, diagnostics.reloadSettings, file.save-session, help.about, project.lint, project.reparse, view.dock.dock-project, view.panel.interface-panel, view.panel.module-tree, view.treesitter
bound primaries: 34 (+ extras: 2)
SUMMARY rows=45 failures=0
```

四个来源互相独立：

1. **源码命令全集**：从 `app_shell.cpp` / `diagnostics_integration.cpp` / `palette_integration.cpp` 抽 id，
   加 5 个运行期生成的 id，与 45 行一一对应（多/少都失败）。
2. **当前全产品快照**：`evidence/ux-batch-2/rm5c-defaults/product-selfcheck.log` 的
   `COMMAND-BINDING <portable sequence>\t<id>` —— 36 条绑定 / 34 个命令，**与手册逐条相等**（含两个备用默认，
   备用项按页面打印的 `Alt+Shift+F` 规范化）；同一日志 `:376` 的
   `CHECK commands.no-desktop-global-shortcuts PASS` 是产品级门禁。
3. **历史快照**：`evidence/ux-batch-2/rm5b-shortcuts/product-bindings.txt`（RM-5b 的 32 行）早于本轮设计；
   28 行与手册一致，4 行是脚本里显式登记的 post-dump delta（先断言旧值确实在快照里）。
4. **静态性检查**：源码里不存在 `rehomeDesktopGlobalDefaults`（运行时重定位已删除），且
   `commands.no-desktop-global-shortcuts` 门禁存在（`src/app/main.cpp:502`）。

完整产品绑定快照见 `logs/08-product-binding-snapshot.log`。

### 4.2 关于两个“桌面全局键”默认与两个备用默认

- `Ctrl+Alt+T`（GNOME/Ubuntu：打开终端）与 `Ctrl+Alt+D`（KDE：显示桌面）会被桌面在应用收到之前消费。
  当前产品**静态地**避开它们：`view.toolbar` 的源码默认就是 `Ctrl+Alt+Shift+T`（`app_shell.cpp:660-661`），
  `view.panel.diagnostics` 的源码默认就是 `Ctrl+Alt+Shift+D`（`diagnostics_integration.cpp:3013`）。
  **没有任何启动时改写**；产品自检 `commands.no-desktop-global-shortcuts` 覆盖所有车道的每条绑定。
- `edit.redo` 的主键位是 `Ctrl+Shift+Z`，额外默认 `Ctrl+Y`（`app_shell.cpp:581`）；
  `diagnostics.format` 的主键位是 `Ctrl+Shift+F`，额外默认 `Shift+Alt+F`（源码）/
  `Alt+Shift+F`（页面与快照打印，`app_shell.cpp:274`）。产品快照里这两条备用默认都在，
  快捷键页的 `Effective shortcut` 列用 ` / ` 连接显示（`visible.redo-shows-both` =
  `Ctrl+Shift+Z / Ctrl+Y`）。
- 两个备用默认在 KTextEditor 动作表里都 FREE（见 4.3）；备用默认不会被 `syncOwnedShortcuts()`
  从编辑器动作里移除（它只收集主键位），所以这是它们能成立的真正原因，也是唯一需要留意的边界。

### 4.3 KTextEditor 抢占检查（`verify_kte.py`，`logs/03-kte-collision-matrix.log`）

```
$ python3 evidence/ux-batch-2/rm8b-bindings/verify_kte.py
KTextEditor actions: 114
desktop-global sequences: 17
...
collisions: 5 -> Ctrl+P, Ctrl+S, Ctrl+Shift+S, Ctrl+Shift+Z, Ctrl+Z
all collisions are in the taken-over set: yes
sequences the OLD manual used, owned by the editor, now avoided:
  Ctrl+Alt+V       -> switch_next_input_mode (Switch to Next Input Mode)
  Ctrl+Shift+H     -> edit_find_selected_backwards (Find Selected Backwards)
SUMMARY checked=36 failures=0
```

- 5 个冲突序列由 shell 的**所有权机制**接管：`ApplicationShell::syncOwnedShortcuts()`（`app_shell.cpp`）、
  `EditorArea::setOwnedShortcuts()`（`editor_area.cpp:1430`）、`clearOwnedShortcuts()`
  （`editor_area.cpp:119`）；`CommandRegistry::refreshShortcuts()` 结束回调 applier（`app_shell.cpp`），
  改键立即生效。它们不是死键：`Ctrl+P` 是“转到文件”，不是打印。
- 其余 29 个主键位 + 2 个备用默认在编辑器动作集合里 **FREE**。
- 手册 §8.3 的“几条说明”写了这 5 个接管项、旧版被占用的 `Ctrl+Shift+H` / `Ctrl+Alt+V` 为什么换掉，
  以及两个桌面全局键为什么取 Shift 版本。

### 4.4 编辑器表（§7.3）与手势表

- 查找浮条：`src/editor/kernel/find_replace.cpp:544-551` 用 `ShortcutOverride` 认领
  `Ctrl+F` / `Ctrl+H` / `F3` / `Shift+F3`，不是注册表命令。
- 编辑器增强：`src/editor/kernel/multi_cursor.cpp:1001`（`clearCompetingActions`）认领
  `Ctrl+/`、`Alt+↑/↓`、`Shift+Alt+↓`、`Ctrl+Shift+K/U/L`、`Ctrl+=`/`Ctrl+-`、`Ctrl+Shift+W`；
  行为由 `multi_cursor_selftest.cpp` 的真实按键用例覆盖。
- F8 / Shift+F8：`src/editor/kernel/lsp_actions.cpp:865-874` 的事件过滤。
- 手势：见 §2。

## 5. 追加要求（RM-9 状态点/覆盖策略 + RM-12 未保存确认）的代码依据

`logs/05-dots-coverage-rm12.log`，要点：

- **五态定义**：`src/common/file_status.h:29-95` —— 0 Unknown（无点）、1 Error（红）、2 Warning（黄）、
  3 Clean（绿）、4 NotAnalyzed（空心灰环，tooltip 原文 “Not analysed yet (queued, or excluded because
  the buffer has unsaved changes)”）；`fileStatusColor()` / `fileStatusIsFilledDot()` / `fileStatusHint()`
  是唯一来源，文件树与 Project 面板共用。
- **空心环绘制**：`file_status_delegate.cpp:37-41,111`；Project 面板 `project_panel.cpp:72-130` 同一套状态、
  同一个 ring 分支。
- **覆盖策略**：`diagnostics_integration.cpp:1900`（`startFullProjectAnalysis()`：工程打开/切换时对
  “目录扫描 ∪ `.f`”整个集合跑一次；不打断正在跑的一轮）；`:996`（保存走
  `setIncrementalRequestBuilder`，只重跑刚保存的文件）；`project_lint_trigger.cpp:70`（增量 + 单飞 +
  700 ms 去抖）；`diagnostics_controller.cpp:1218-1247`（`statusValueForFile` 的优先级：上位 →
  新的下位 → 排除 → 未分析 → 不在集合）与 `:1300-1345`（`fileStatuses()` 给分析集里没有结论的文件补
  `NotAnalyzed`）。
- **`.f` 外部文件**：`file_tree_model.cpp:722` 生成 “From filelist (%1)” 分组；`project_panel.cpp`
  Files 列表的委托（同 72-130）画同一标记。
- **RM-12 未保存确认**：`app_shell.cpp:754`（`file.new-layout`）、`:783`（`file.quit`）、
  `:817`（`view.layout.mixed`）、`:826`（`view.layout.reset`）都先调 `confirmUnsavedDocuments()`
  （`app_shell.cpp:1613` 起：多份 dirty 文档一次询问，Save All / Discard All / Cancel；单份走
  `promptUnsaved()` 的 Save / Discard / Cancel）。取消则不清空 layout、不关文档。

## 6. 门禁输出

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

完整输出：`logs/01-docs-gate.log`。结构：`logs/06-structure.log`（行数、标题清单、diff stat）。

## 7. 未能证明 / 本轮没做

1. **没有自己重跑产品自检 / 键路径测试**：两个可执行文件在本环境会卡住（Qt 平台插件/会话，offscreen
   超时）。本轮的整机证据取自本批已完成的两次运行：`product-selfcheck.log`（全车道 36 条绑定、
   `commands.no-desktop-global-shortcuts` PASS）与 `green-run.log`（123 checks 0 failures，含
   `real.ctrl-y-redoes` 等真实按键断言）。没有一次新按键覆盖全部 36 个键位。
2. **没有真机键盘逐条验收**：表格依据是注册表/代码/产品快照，不是物理键盘。
3. **没有改动** `translations/vedit_zh_CN.ts`，也没有运行 `lupdate`（i18n 车道自己收 `active` 徽标行）。

## 8. 证据文件

- `logs/01-docs-gate.log` — 文档门禁 PASS
- `logs/02-table-vs-registry.log` — 45 行 vs 源码全集 / 全产品快照 36 绑定 / RM-5b 历史快照 32 条
- `logs/03-kte-collision-matrix.log` — 114 条编辑器动作 vs 36 个键位
- `logs/04-gesture-code.log` — 手势匹配器与 press/release 代码
- `logs/05-dots-coverage-rm12.log` — 状态点定义、覆盖策略、未保存确认的代码锚点
- `logs/06-structure.log` — 行数、标题、diff stat
- `logs/07-static-defaults.log` — 静态默认、备用默认、标签命令、无 rehome、产品门禁
- `logs/08-product-binding-snapshot.log` — 全产品 `COMMAND-BINDING` 快照（36 条）
- `verify_table.py` / `verify_kte.py` — 可复跑校验脚本（自带参考映射与断言）

## 9. T34 更新（评审 High H1 + M1 三条限制 + M2/N1）

T34 与 T33 在同一个文件上向前推进：§8 的静态默认定稿已在 §1–§8 里；本节记录本轮对
§2.5 / §10 的手册修正。**手册现在是 669 行 / 53,370 字节**（N1：旧的 604 行/42,837 字节快照不再被引用）。

### 9.1 H1 — 变量对话框：改为 RM-3 之后的真实契约（§2.5 + §10 #8）

手册 §2.5 末段从“只能填已有缺失变量、不能增删”重写为：

> `Variables` 对话框（标题 `Filelist variables`）可以**增删改**工程变量表：`Add variable` 新增一行，
> 名字与值都可编辑；每行右侧的 `Delete` 删除该行。打开时会列出已有变量和当前仍缺失的变量——即使工程
> 还一个变量都没有，`Edit Variables…` 也能打开它（新增就是用途之一）。名字按
> `[A-Za-z_][A-Za-z0-9_]*` 校验：空名字、含空格或 `=` / `$` 等字符、以数字开头、或重名都会在该行
> 下面给出原因，`OK` 不会关闭对话框。删除一个仍被 filelist 引用的变量只会去掉它的值：工程会把它重新
> 列为缺失，并在下次展开时再问（不会静默消失）。保存是**整表替换**，并按**工程根分别记忆**：
> A→B→A 切换回来时恢复 A 自己的表，切到 B 看不到 A 的变量（存在 `project-model.json`，重启仍在）。
>
> `Project` 面板里的 `Include dirs (-I)` 与 `Defines (-D)` 列表是从展开结果来的只读视图，
> **没有按工程增删的入口**（见第 10 节）。

每一句的代码依据：

| 手册句子 | 代码依据 |
|---|---|
| `Add variable` 新增一行，名字与值都可编辑 | `project_variables_dialog.cpp:52-60`（按钮 → `appendRow("", "", false)`）；`:98-126`（同一行的 `nameEdit` + `valueEdit` + `Delete`） |
| 每行 `Delete` 删除该行 | `:125`（`Delete` 按钮）→ `:159` `removeRowAt()` |
| 打开时列出已有 + 仍缺失；没有变量也能打开 | `:80-96`（`requested` = 传入名字 ∪ 已知名字，已知的总是显示）；`project_panel.cpp:441-458`（空表也打开） |
| 名字校验 `[A-Za-z_][A-Za-z0-9_]*`；空/非法/重名行内报错，OK 不关闭 | `:286-310` `validateRows()`（三条原因）；`:328-349` `isValidName()`；`:65-71`（`accepted` 只有验证通过才 `accept()`） |
| 删除仍被引用的变量 → 重新列为缺失并再问 | `:42-46`（对话框规则文本）；`:159-175`（删除提示 “the project still needs it and will ask again”）；`project_model.cpp` 的 `missingVariables()` 重新报告 |
| 整表替换（不是追加） | `project_panel.cpp:485-506` `applyVariableTable()`（REPLACE） |
| 按工程根分别记忆；A→B→A；B 看不到 A | `project_model.cpp:246-263` `setVariables()`（写 `variablesByRoot`）；`:108-116`（`open()` 保留 map 并恢复**新**根的条目）；`project_state.cpp:49-90`（`version` 3 + `variablesByRoot` 序列化） |
| 存在 `project-model.json`，重启仍在 | `project_state.cpp:87`；e2e 的 restart 用例 |
| 回归 `vedit-project-variables-e2e` 36/0 | `src/workspace/project/CMakeLists.txt:137,147`；`evidence/ux-batch-2/rm3-variables/05-e2e-final.log` → `SUMMARY checks=36 failures=0`（负控 `04` = 11 failures） |
| 仍存在的限制：面板 `-I`/`-D` 只读 | `project_panel.cpp:177-288`：面板只有 `Refresh` / `Import Filelist…` / `Remove Selected Filelist` / `Edit Variables…` 四个按钮；`-I`/`-D` 只通过 `addItems()` 填充（`:678-684`），没有增删入口。全局设置页 `Tools` 才有 `Include dirs (-I)` / `Defines (-D)`（`settings_panel.cpp` 的 `buildToolsPage`） |

§10 第 8 条已由“变量对话框不能增删”替换为这条真实的 `-I` / `-D` 限制。

### 9.2 M1 — §10 新增三条限制（#11–#13）

| 手册条目 | 批内依据 |
|---|---|
| **11 没有真机 GUI 验证**（界面证据来自离屏/软件渲染与代码核对；观感、DPI、字体、CJK 渲染由用户验收；软件渲染缺 CJK 字体时按钮可能显示成方块） | `evidence/final-acceptance-3/AC-MATRIX.md:37,113`（“没有真机 GUI 验证：全部 offscreen 或 Xvfb；观感、DPI、字体、CJK 渲染由用户验收”）；`evidence/ux-batch-2/rm9-dots/README.md` 同类披露 |
| **12 FIM 的错误提示只有中文**（`reason()` 固定中文短句，不随界面语言） | `crates/vd-fim/src/error.rs:66-84`（`Disabled → "FIM 未启用"`、`Timeout → "FIM 超时（…ms）"`、`Transport → "FIM 连接被拒绝"` 等）；`evidence/fim/README.md:54`（“reason() strings are already Chinese … paste them into the status bar unchanged”） |
| **13 语法定义「同名遮蔽」**（5 份定义 `version="1"`；带 KDE 定义的环境里 4/5 名字实际用 KDE 内嵌副本，只有 Tcl 是我们的；交付 AppImage 不受影响） | `evidence/final-acceptance-3/AC-MATRIX.md:116`；`evidence/ux-batch-2/u4b-final/README.md:185-205`（探针输出 4 个 KDE 文件路径 + Tcl 才解析到我们的 `vedit-tcl.xml`）；`src/syntax/kde/README.md:84-96`；`resources/syntax/*.xml` 均为 `version="1"`；交付形态 `packaging/runtime/README.md` 的 F2 契约（私有 KSH：库内 0 定义 + 白名单 5 份 + AppRun 收窄 XDG） |

### 9.3 D13 — 替换边界措辞（AC 矩阵把它并入 T34；本轮一并修正）

`evidence/final-acceptance-3/AC-MATRIX.md:179` 写着 `USER_GUIDE` 三条替换边界措辞被 B2b 推翻、
“并入 T34”。本卡没有附“精确措辞”，因此按 B2b 的证据改正 §10 第 3 条：

- 写每个文件前校验冻结扫描快照 + 文件戳 + 命中行身份；
- 命中行 >400 字符（预览截断）时，**未保存缓冲区**在保存前做**整行**比对（去 BOM + trim，与写入端同一
  解码/切分），不一致或拿不到缓冲区文本就**拒绝**（不保存、不写入）；
- 残留只剩一种：文件被外部程序改写且**大小与修改时间都不变**（文件戳看不出）。
- 删掉了被推翻的“整行一致 = 无害”说法。

依据：`evidence/ux-batch-2/b2-repair-r4/README.md` §1–§2（P5 复现、whole-line 身份、
`normalizedLineIdentity()` / `readDocumentLine()`）；`src/workspace/search/search_panel.cpp` 的
`replaceCurrentHit` 与 `project_replace.cpp` 的写入前校验。若队长手里有另一份“精确措辞”，替换这一段即可；
当前文本已不再包含被推翻的断言。

### 9.4 本轮验证输出

```
$ grep -n '不能新增\|也不能删除\|只能为已被 filelist 引用' docs/USER_GUIDE.md
(no output)                     # banned_hits=1 (grep exit code, i.e. 0 matches)

$ grep -n '变量对话框不能增删' docs/USER_GUIDE.md
(no output)                     # old_limit_hits=1 (0 matches)

$ wc -l -c docs/USER_GUIDE.md
  669 53370 docs/USER_GUIDE.md
```

完整日志：`logs/09-t34-variables-greps.log`。

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

T33 的两份校验脚本本轮重跑（同 §4）：

```
$ python3 evidence/ux-batch-2/rm8b-bindings/verify_table.py
... (full output in logs/02) ...
bound primaries: 34 (+ extras: 2)
SUMMARY rows=45 failures=0

$ python3 evidence/ux-batch-2/rm8b-bindings/verify_kte.py
... (full output in logs/03) ...
SUMMARY checked=36 failures=0
```

### 9.5 本轮没做 / 边界

- **没有在真机 GUI 上操作变量对话框**（这也正是新增限制 #11 的内容）：§2.5 的每句来自代码 + `vedit-project-variables-e2e`
  的 36/0 批内日志，没有重新构建/运行该 e2e。
- 没有改动 `translations/vedit_zh_CN.ts`（i18n 车道自己收 `active` 徽标行）。

## 10. T35 更新 — docs/BUILD.md 的打包入口写错了

### 10.1 发现

`docs/BUILD.md:101` 在「打包三种产物」里把 `bash packaging/appimage/run-evidence.sh` 写成 AppImage 的
构建入口。该管线是**旧 Rust/GTK `vd-app`** 的打包/验收管线（它自己的 README 首段就这么写），
不是 Qt 交付物；实测其 AppImage 也不带翻译。同一节还写着“两者都…”并把 tar.gz/.run 并列，
读者会把它当成发布路径。

### 10.2 修复（`docs/BUILD.md` §7，重新编号为 7.1 / 7.2）

- **§7.1 Qt 交付物（唯一 AppImage）**：`bash packaging/runtime/build.sh` →
  `packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` + `packaging/runtime/out/SHA256SUMS`；
  F2 私有运行时，含 `bin/translations/`。开头是一句加粗的“交付物 = …”单句。
- **§7.2 旧版 GTK 产物（不是 Qt 交付物）**：`packaging/appimage/run-evidence.sh` 与
  `packaging/tarball/build.sh` 被明确标成旧 Rust/GTK 管线（按需求非目标 5 保留），并给出三条
  可复核差异（无 `bin/translations/`、体积不同、旧干净容器仍从宿主 glibc 家族解析 8 个库）。
  原文中“两者都只消费 pinned 输入 / GLIBC 断言”保留为**旧管线自身**的断言。

### 10.3 两套 README 的自述（判断依据）

- `packaging/runtime/README.md:19`：`packaging/runtime/out/VEdit-<ver>-x86_64.AppImage` —
  “**唯一交付物**：单文件 AppImage … `out/SHA256SUMS` 与之配套”；`:11` 是那条一键命令。
- `packaging/appimage/README.md:1-5`：“**Legacy GTK product packaging.** This directory still
  packages the legacy Rust/GTK `vd-app` … The **Qt delivery** … is produced by
  `packaging/runtime/build.sh`; **do not present this pipeline as the final Qt AppImage**.”

### 10.4 依赖的实测数字（`logs/10-t35-packaging-audit.log`）

| 事实 | 数值 |
|---|---|
| 两个产物体积（同一天构建） | `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` = **137,079,288** 字节；`packaging/appimage/out/VEdit-0.1.1-x86_64.AppImage` = **47,745,528** 字节 |
| 翻译（`unsquashfs -o <offset> -l` 匹配 `zh_CN\|translations`） | Qt 交付物 = 3 条（`bin/translations/`、`qtbase_zh_CN.qm`、`vedit_zh_CN.qm`）；旧 AppImage = **0** 条 |
| 包内 `vedit_zh_CN.qm` 与当前目录表 | 提取出的 sha256 `276ccfa2c608…` == 对 `translations/vedit_zh_CN.ts` 现场 `lrelease` 的 sha256（1244 translations / 0 unfinished）→ **IDENTICAL** |
| 旧管线干净容器 | `evidence/appimage/clean-room.log:43`：`deps resolved from payload: 67; from host glibc family: 8` |

### 10.5 文档 sweep（`logs/11-t35-doc-sweep.log`）

修改前（`grep -rn "appimage/run-evidence\|packaging/appimage" docs/ README.md`）：

- `docs/BUILD.md:101` — **错误**：把旧管线写成 AppImage 构建入口（已修）。
- `docs/handover/03-architecture.md:176` — 已经正确（`packaging/runtime/` 是当前实现，
  `packaging/appimage/` 与 `packaging/tarball/` 是旧产品工具，不要混用）。
- `docs/RELEASING.md:105` — 发布检查单仍写 `bash packaging/appimage/run-evidence.sh` 与
  `bash packaging/tarball/build.sh` 的干净无网验收。**未改**：超出本行 WRITE SCOPE；
  它整份检查单（`scripts/build.sh`、三产物、tag）是旧 GTK 线的流程，建议后续行统一改成
  `bash scripts/build-qt.sh` + `bash packaging/runtime/build.sh`，或至少加一句“本节是旧 GTK 线，
  Qt 交付物见 docs/BUILD.md §7.1”。
- `README.md` — 没有出现 `appimage/run-evidence` 或 `packaging/appimage`，因此按卡片“只有写错管线才改”
  未动；但它的正文写的是**旧 Rust+GTK3 产品**（“三种产物”、≈48 MB AppImage、tar.gz/.run），
  与当前 Qt 交付物不一致，属于更大的文档陈旧问题，作为发现上报。

修改后：`docs/BUILD.md` 只剩 §7.2 的旧管线行（已明确标为“旧 Rust/GTK 产物，不是 Qt 交付物”）；
`handover` 不变；`RELEASING.md` 作为 out-of-scope 发现保留。

### 10.6 验证

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

### 10.7 边界

- 没有运行任一条打包管线；上述事实来自**已存在的产物**（18:07 / 18:09 构建）、两套 README 的自述、
  `unsquashfs` 列表与旧管线的 clean-room 日志。
- 没有改动 `RELEASING.md` / `README.md`（超出 WRITE SCOPE），已作为发现列出。
