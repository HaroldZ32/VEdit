# VEdit 用户手册

VEdit 是面向 Verilog / SystemVerilog 工程的桌面编辑器。打开一个工程文件夹后：左侧看文件与工程结构，中间写代码（支持多标签与分屏），右侧看模块层级与端口，底部做工程级搜索或开一个终端；保存后自动跑工程 lint，问题可以双击跳到源码。

本手册从**打开程序之后**讲起：界面每一块是做什么的、怎么打开和切换工程、怎么编辑与搜索、诊断怎么读、键位怎么改；最后是已知边界和安装、配置附录。文中菜单与按钮名使用程序里实际显示的英文标签，后面用中文说明它的作用。

## 目录

1. 这是什么：界面总览
2. 打开、切换与恢复工程
3. 编辑基础
4. 工程级搜索与替换
5. 模块树与接口面板
6. 诊断：Lint、悬浮修复与文件状态点
7. 代码导航与多光标
8. 设置（首选项）
9. 语言与显示
10. 已知限制
附录 A 产物形态与安装
附录 B 配置文件与位置
附录 C 排障
附录 D 版本控制面板：能做什么、工具链与安全边界

## 1. 这是什么：界面总览

### 1.1 主窗口的六块区域

从上到下、从左到右，主窗口分成六块：

1. **菜单栏**：固定的五个菜单 `File` / `Edit` / `View` / `Project` / `Help`。
2. **主工具栏**：最常用的命令，从左到右是 `New`、`Open…`、`Open Folder…`、`Switch Project…`、`Save`、`Save All`、`Undo`、`Redo`，然后是 `Search in Files`、`Lint Project`、`Format Document`。
3. **编辑区**：每个标签页是一份打开的文件；可以水平或垂直分成多个分屏，当前分屏右上角有 `active` 标记。
4. **左侧停靠区**：`Files`（文件树）、`Project`（工程，只在有工程时出现）、`Source Control`（版本控制）。
5. **右侧停靠区**：`Module Tree`（模块树）、`Interface`（接口）、`Outline`（上半是大纲符号，下半是诊断列表）。
6. **底部停靠区**：`Search`（工程搜索）、`Terminal`（内置终端）。

底部的**状态栏**：左侧是工程名 `Project: …` 和只停留几秒的一次性提示（例如「已保存」「打开失败」）；右侧依次是未保存标记 `● Unsaved changes`、主题 `Theme: …`、分屏与标签统计 `Panes: … · active: … · tabs: …`，以及诊断徽标 `LSP backend=… · lint=…`。

### 1.2 每个面板是干什么的

| 面板 | 默认位置 | 作用 |
|---|---|---|
| `Files` | 左侧 | 工程文件夹树。双击打开文件；工具栏可以新建文件/文件夹、重命名、删除、刷新；图标与文件名之间有诊断状态点 |
| `Project` | 左侧（打开工程后出现） | filelist、展开出的文件、`-I` 包含目录、`-D` 宏、工程变量与解析诊断 |
| `Source Control` | 左侧 | Git / Subversion 的工作区与历史（详见附录 D） |
| `Search` | 底部 | 当前文件或整个工程的搜索与替换 |
| `Terminal` | 底部 | 内置终端，运行系统 shell |
| `Module Tree` | 右侧 | 工程模块层级，根节点是从未被例化的顶层模块 |
| `Interface` | 右侧 | 焦点模块的端口表 |
| `Outline` | 右侧 | 上半是当前文件的符号大纲；下半是诊断列表 |

面板的显示与隐藏都在 `View` 菜单里（每一项是勾选式的）：`Toggle Files Panel`、`Toggle Search Panel`、`Toggle Terminal Panel`、`Toggle Source Control Panel`、`Toggle Outline Panel`、`Toggle Module Tree Panel`、`Toggle Interface Panel`、`Toggle Project Panel`，以及 `Diagnostics`。用面板标题栏的 `×` 关掉后，用同一个菜单项可以再打开。

首次启动时显示 `Files`、`Outline`、`Module Tree`、`Interface`；`Project` 在有工程后自动出现。之后每次启动会恢复你上次的布局。

### 1.3 帮助

按 `F1`，或点 `Help → User Manual`，会打开一个独立的只读窗口，内容就是这份手册。它随程序一起打包，不需要联网。关掉窗口不影响编辑。

### 1.4 常用操作从哪里开始

- 打开文件：`File → Open…`（`Ctrl+O`），或在 `Files` 树里双击。
- 打开工程：`File → Open Folder…`，或工具栏的 `Open Folder…`。
- 找东西：`Ctrl+F` 当前文件查找；`Ctrl+Alt+S` 打开 `Search` 面板搜整个工程。
- 看问题：保存后自动 lint；`View → Diagnostics`（`Ctrl+Alt+Shift+D`）打开诊断列表。
- 改设置：`Edit → Preferences…`（`Ctrl+,`）。
- 跳转：`F12` 定义，`Shift+F12` 引用，`F2` 重命名。

## 2. 打开、切换与恢复工程

### 2.1 打开文件

- `File → Open…`（`Ctrl+O`）打开一个文件。文件对话框的过滤器分三组：`Verilog/SystemVerilog (*.v *.sv *.svh *.vh)`、`Filelists (*.f *.F)`、`All Files (*)`。过滤器只列 `*.f` / `*.F`；`.flist` 长拼写不在过滤器里，可通过 `All Files (*)` 当作普通文本文件打开，但**不会**按 filelist 导入。
- 也可以在启动时把文件路径作为参数：`./VEdit-<版本>-x86_64.AppImage path/to/top.sv`；给多个文件时，每个文件各占一个分屏。
- 从 `Files` 树双击文件，或从 `Project` 面板双击文件，也会打开它。

打开普通源文件**不会**自动切换工程，只是把文件加进当前窗口。

### 2.2 打开工程与切换工程

- `File → Open Folder…`：选一个目录作为工程打开。
- `File → Switch Project…`（工具栏也有）：运行中换一个工程，走的是同一条路径。
- 启动参数 `--project <目录>`：启动时直接以该目录作为工程。

**选中的目录本身就是工程根**，程序不会向上层目录继续查找。

切换工程时：

- 文件树、搜索、版本控制、模块树、接口、诊断这些**工程范围**的面板会一起切到新工程；
- 已经打开的标签页**不会关闭**，未保存的内容也不会丢；
- 新工程会随会话一起保存，正常退出后再启动会自动恢复；
- 打开或切换工程后会**自动分析整个工程**（见 6.2），文件树与 `Project` 面板的状态点覆盖工程里的文件，而不只是打开过的文件。

### 2.3 打开 filelist（`.f`）会发生什么

打开一个 `.f` / `.F`（`.flist` 长拼写只能用对话框的 `All Files (*)` 当普通文本打开，不会按 filelist 导入）：

- 如果**当前没有工程**：该 filelist 所在目录成为工程根，并把它导入工程；
- 如果**已经有工程**：只把它导入当前工程，**不切换**工程；
- filelist 本身仍然作为普通文本在编辑器里打开，方便查看和编辑。

导入之后，`Project` 面板会列出它展开出的文件、`-I` 包含目录与 `-D` 宏。**编辑 filelist 后按 `Project` 面板的 `Refresh` 重新展开**；重复打开同一个 filelist 是幂等的，会在面板底部提示 `Already imported: nothing changed (use Refresh to re-read an edited filelist)`。

通过 `.f` 导入、但不在工程目录内的文件会出现在 `Files` 树的 `From filelist (N)` 分组里（带诊断状态点），也会出现在 `Project` 面板的 `Files` 列表里（列表左侧有同样的标记）。

### 2.4 工程面板

`Project` 面板从上到下是：

- 摘要行与按钮：`Refresh`、`Import Filelist…`、`Remove Selected Filelist`、`Edit Variables…`；
- `Filelists`：已导入的 filelist，双击打开；
- `Files`：展开出的源文件，双击打开；
- `Include dirs (-I)` 与 `Defines (-D)`：展开得到的包含目录与宏；
- `Variables`：工程变量表；
- `Diagnostics`：filelist 解析诊断（缺失的 `+incdir+`、未知指令、嵌套问题等）。这些都显示在面板里，不弹模态框。

没有工程时，面板显示一行提示；这不是错误。

### 2.5 filelist 里的变量与转义

`.f` 的**每一行**（文件路径、`+incdir+`、`+define+`、`-f` 的参数都算）先做变量展开，再按指令解释：

| 写法 | 含义 | 例子 |
|---|---|---|
| `$NAME` | 变量；名字以字母或 `_` 开头，后面是字母、数字或 `_` | `$RTL/a.sv` |
| `${NAME}` | 同上，用花括号划界（后面紧跟字母或数字时必须用） | `${RTL}_tb.sv` |
| `$(NAME)` | 同上，用圆括号划界（Makefile 风格） | `$(RTL)/a.sv` |
| `$$` | 一个字面 `$`（唯一的转义写法，不展开） | `out$$.sv` → `out$.sv` |
| `~/…` | 当前用户的 `HOME`，只在行首或分隔符之后、且紧跟 `/` 时展开 | `~/gen/a.sv` |

不构成上面任何一种引用时，`$` 就是字面 `$`（shell 规则）。只有 `${` / `$(` 开了头却没闭合、或名字为空，才算语法错误：该行报 `文件:行号` 并跳过。

**优先级：工程变量表 > 系统环境变量。**工程变量表在 `Project` 面板的 `Edit Variables…` 里填写，保存后写进工程模型文件，重启仍在。`HOME` 始终来自环境。

**没有变量或路径会被悄悄丢掉**，每条都有带 filelist 路径与行号的诊断：

- 变量在工程表和环境中都找不到：该行跳过并给出诊断；所有缺失的名字会收集成一批，用一个对话框一次填完。取消对话框不会丢掉已导入的结果，这些行留在诊断里。
- 展开后的路径不存在：条目**保留**（生成文件稍后出现时仍然生效），同时给一条诊断，写清实际查找的路径。
- 嵌套 `-f` 的路径按包含它的那个 `.f` 所在目录解析；环形引用会点名整条链；嵌套深度上限 16 层。
- 不认识的 `+…` / `-…` 指令报一条诊断后跳过，不会让工程打不开。

`Variables` 对话框（标题 `Filelist variables`）可以**增删改**工程变量表：`Add variable` 新增一行，名字与值都可编辑；每行右侧的 `Delete` 删除该行。打开时会列出已有变量和当前仍缺失的变量——即使工程还一个变量都没有，`Edit Variables…` 也能打开它（新增就是用途之一）。名字按 `[A-Za-z_][A-Za-z0-9_]*` 校验：**空名字、含空格或 `=` / `$` 等字符、以数字开头、或重名**都会在该行下面给出原因，`OK` 不会关闭对话框。删除一个仍被 filelist 引用的变量只会去掉它的值：工程会把它重新列为缺失，并在下次展开时再问（不会静默消失）。保存是**整表替换**，并按**工程根分别记忆**：A→B→A 切换回来时恢复 A 自己的表，切到 B 看不到 A 的变量（存在 `project-model.json`，重启仍在）。

`Project` 面板里的 `Include dirs (-I)` 与 `Defines (-D)` 列表是从展开结果来的只读视图，**没有按工程增删的入口**（见第 10 节）。

### 2.6 恢复上次会话

正常退出时，程序把下面这些写进会话文件，下次启动恢复：

- 工程根；
- 分屏布局、活动分屏、每个分屏里打开的文件、光标位置与滚动位置；
- 打开过的面板与停靠布局、窗口大小；
- 主题。

不恢复：终端历史、搜索面板的临时结果、补全的临时预览。

- 想立刻保存当前会话（不退出）：`File → Save Session Now`。
- 启动时加 `--no-session-restore`：忽略已有会话，从干净状态开始。
- 会话文件损坏时不会阻止启动：状态栏提示「Session could not be restored … — starting clean」，然后按干净状态启动。

### 2.7 路径怎么显示

工程根存在、且文件在根内时，界面显示**相对工程根的路径**；否则显示**绝对路径**。这条规则覆盖编辑器标签的提示、窗口标题、文件树提示、`Project` 面板与搜索结果行。标签页上的文字是**文件名**，把鼠标停在标签上才显示路径。状态栏与一次性提示（如 `Opened …`）仍然显示绝对路径。

## 3. 编辑基础

### 3.1 标签页与分屏

- `File → New` 新建一个空白文档；`File → Open…` 打开文件；两者都会在当前分屏里增加一个标签。
- 关闭一个文件：点标签右侧的 `×`。有未保存修改时会问「保存 / 丢弃 / 取消」，取消则标签保持打开。
- 分屏：`View → Split Pane Horizontally`（`Ctrl+Alt+H`）左右分，`View → Split Pane Vertically`（`Ctrl+Shift+V`）上下分；`F6` 在分屏间切换焦点；`View → Close Pane`（`Ctrl+W`）关闭当前分屏（其中每个未保存文档都会逐个询问）。
- `View → Reset Layout`（`Ctrl+Alt+R`）回到单分屏；`File → New Empty Layout`（`Ctrl+Alt+N`）重建一个空布局。这两个命令（以及 `View → Mixed Split`）在**有未保存文档时会先询问**：可以保存、丢弃或取消（有多份未保存文档时是「全部保存 / 全部丢弃 / 取消」）；选**取消**则分屏、文档与布局都保持原样。
- 点哪个分屏就激活哪个分屏：鼠标按在非活动分屏的任意位置，或焦点进入它，之后的命令、跳转与重命名都跟随它。

### 3.2 撤销与重做

`Edit → Undo` / `Edit → Redo`，工具栏上的 `Undo` / `Redo` 是同一个命令。它们作用于**当前活动的分屏**，默认键位是 `Ctrl+Z` / `Ctrl+Shift+Z`（重做另有备用默认 `Ctrl+Y`，见 8.3）。编辑器自带的那一份键位已被移除，所以同一个按键只会触发这一个命令，不会两个动作抢键。

工具栏的 `Undo` / `Redo` 只在该分屏确实可撤销/可重做时才可点。

### 3.3 保存

- `File → Save`（`Ctrl+S`）：保存当前文件。没有路径的文档（新建后从未保存过）会转到「另存为」。
- `File → Save As…`（`Ctrl+Shift+S`）：换一个路径保存。
- `File → Save All`（`Ctrl+Alt+Shift+S`）：保存所有有路径且被修改过的文档。没有路径的文档不会被悄悄丢掉，状态栏会报告「已保存 N 个，另有 M 个无路径文档需要另存」。
- 保存是**原子**的：先写临时文件再重命名，中途失败不会留下半截文件。
- 有未保存修改时，状态栏显示 `● Unsaved changes`，窗口标题带 `*`。

关闭标签、关闭分屏、退出程序（`File → Quit` / `Ctrl+Q`），以及重置/新建布局，都会先问未保存的文档；取消就什么都不做。

### 3.4 当前文件里查找与替换

`Ctrl+F` 打开查找浮条，`Ctrl+H` 打开带替换框的浮条。

- 选项：区分大小写、全词匹配、正则表达式、只在选区内。
- 范围选择器有 `Current File`（当前文件）和 `Whole Project`（整个工程）。选整个工程时，浮条把查询词和选项**交给** `Search` 面板执行，自己不再假装能在单文件里搜全工程；替换也一样，整个工程的替换只在 `Search` 面板里做。
- 结果：`F3` 下一个，`Shift+F3` 上一个，`Esc` 关闭浮条。
- 替换：`Replace` 替换当前匹配并跳到下一个；`Replace All` 一次替换当前文件里的全部匹配，且可以**一次撤销**整体回退。
- 浮条下方会显示状态（例如「无匹配」「已替换 1 处」「整个工程 → Search 面板」），不弹模态框。

当前文件的查找使用 Qt 的正则引擎（PCRE 风格）；整个工程的查找使用另一种引擎，差异见 4.4。

## 4. 工程级搜索与替换

### 4.1 打开与选项

三种打开方式，指向同一个面板：

- `View → Toggle Search Panel`（`Ctrl+Alt+S`）；
- 工具栏的 `Search in Files`（会打开面板并把光标放进输入框）；
- 在 `Ctrl+F` 浮条里把范围选成 `Whole Project`，浮条会把查询交过来。

面板里的控件：查询输入框、区分大小写 / 全词 / 正则三个开关、`Verilog` 过滤器（只搜 `.v` / `.vh` / `.sv` / `.svh`）、范围下拉框（`Current File` / `Whole Project`）、`Search` 与 `Cancel`、替换输入框与 `Replace` / `Replace All` 两个按钮。

### 4.2 搜索

- 输入查询后按 `Search`；搜索是**异步**的，随时可以 `Cancel`。
- 结果按 `File` / `Line` / `Match` 三列列出，双击一行跳到命中处。
- 结果有上限：默认 **2000 条命中**与 **15 秒时间预算**。达到任一条会在结果里注明「结果不完整」，这时应缩小查询。
- 空状态会说明原因（没有工程、还没输入查询、没有当前文件等），并给下一步提示。

### 4.3 替换

- `Replace`：只替换**当前选中的那一条**，然后跳到下一条。
- `Replace All`：先弹确认框，写明「把 X 替换为 Y？」以及「将替换 N 个文件中的 M 处」；确认后按文件分组写入，**每个文件一次原子写**，完成后报告替换计数并刷新结果。
- **先保存后替换**：命中的文件如果正在编辑器里且有未保存修改，替换前会先保存（确认框会列出被保存的文件名）；保存失败则整次替换被拒绝，不写任何东西。
- **会在写之前拒绝、并要求重新搜索的情况**（每条都显示原因）：
  - 文件在搜索或计数扫描期间被外部写入；
  - 命中行在替换前的内容与搜索时读到的不一致（逐命中行校验）；未保存的缓冲区还会先做整行比对；
  - 查询词或选项在扫描期间被改动；
  - 结果达到上限或时间预算，命中列表不完整；
  - 编辑器接口不可用，无法确认未保存缓冲区的状态。
- 被拒绝时不会写任何文件，也不会先保存；状态文字会说明原因。

### 4.4 正则：当前文件与整个工程不一样

- **当前文件**（`Ctrl+F` 浮条）：PCRE 风格（Qt 正则引擎）。反向引用 `\1`、lookaround `(?=…)` / `(?<=…)` 等可用。
- **整个工程**（`Search` 面板）：**RE2 风格**。**不支持**反向引用和 lookaround；字符类、量词、分组、`^` / `$`、`\b` 等常见语法照常。

同一个正则在两个范围里结果可能不同；工程级搜索建议先用简单模式验证，再加复杂断言。

## 5. 模块树与接口面板

这两个面板的数据来自 `slang` 对整个工程做的一次**模块目录**，在后台异步执行，不会卡住编辑。它们只描述**模块与端口**：没有别的行类型。

### 5.1 模块树

- 打开：`View → Toggle Module Tree Panel`（右侧）。
- **根节点 = 从未被任何模块例化的模块**（顶层模块），可以有一个或多个。全部模块都被例化过时，面板会明确说「没有顶层模块」。
- 同一个模块被两个父模块例化时，它在**两个父下各出现一行**（靠缩进区分实例路径）。
- 列：`Module` / `Instantiations` / `Definition`。`Instantiations` 是该父下这一模块的例化次数，`Definition` 是定义位置（`文件:行:列`）。
- **双击模块行**打开它的定义文件，并把光标定位到模块声明处。
- 工具栏：`Refresh`（对整个工程重新跑一次模块目录）、`Filter modules…`（不区分大小写，匹配任一列；匹配行的父行始终保留可见）。
- 底部汇总行给出 `slang`、模块数、顶层数、本次目录耗时与峰值内存；有告警时在同一行末尾追加警告。
- 空状态只显示一行原因（没有工程、还没跑过目录、`slang` 没报出模块、没有顶层模块等），不会把每个文件渲染成错误。
- 保护：层级深度上限 64 层、最多 20000 行；超限或环形例化会显示一行标记，不会崩。

### 5.2 接口面板

- 列：`Name` / `Direction` / `Type` / `Width` / `Location`。
  - `Direction`：`In` / `Out` / `InOut`；`ref` 端口显示 `Ref`。
  - `Type`：声明里的原样字符串（如 `logic[3:0]`）。
  - `Width`：从类型字符串推导——`logic[3:0]` 是 4，没有打包维度是 1；无法解析时留空，提示里说明原因。
  - `Location`：端口声明处的 `文件:行:列`。
- 焦点规则：模块树里选中了模块就用它；否则用编辑器光标所在的模块（同文件内「定义行 ≤ 光标行」的最近一个模块）。切换编辑器、切换分屏、改变选中、重新展开都会刷新。
- **双击端口行**跳到端口声明处。
- **Ctrl+双击端口行**跳到该模块被例化的那条语句（父模块里的 `u_xxx` 那一行）；有多个父模块时先弹选择列表，每项标出「父模块 · 实例路径 · 文件:行」。
- 诚实说明：模块目录里**没有**「端口连接位置」的信息，所以跳的是**例化语句**，不是具体连线。
- 空状态：没有焦点模块时显示「没有焦点模块——请在模块树中选择一个模块，或将光标置于模块定义内。」

## 6. 诊断：Lint、悬浮修复与文件状态点

### 6.1 两个来源，一次只显示一个

- **上位 = 工程 lint**：对整个工程跑一趟批量检查，覆盖范围内的所有文件。
- **下位 = 文件级 / LSP 诊断**：语言服务器推来的逐文件诊断，或单文件检查。

同一个文件在某一轮里有上位结果时，编辑区**只显示上位结果**（不叠加、不去重合并出双份）。文件在上位检查**之后被编辑**，该文件的上位结论立即作废，回落到下位，直到下一次工程 lint。这条规则对编辑区波浪线、行号栏、诊断面板、悬浮框、文件树状态点以及所有计数都生效——只有一个来源。

### 6.2 自动分析：打开工程全量，保存增量

- **打开或切换工程后自动跑一次全工程分析**：范围是整个展开后的文件集（目录扫描 + 导入的 `.f`），跳过有未保存修改的缓冲区（磁盘上的内容已过期）。工程里**没打开过的文件也会得到结论**，不需要手动点 `Lint Project`。
- **保存后只重新分析刚保存的那个文件**（另存为同理；连续保存或“全部保存”会在去抖窗口内合并成一次，重跑最后保存的那个文件）：其他文件的结论保留，不会因为一次保存把整个工程重跑一遍。先落盘再分析，不阻塞、不推迟保存本身。
- **结论按文件保留**：切换标签页、切走再切回来都不会让别的文件失去结论；只有**编辑**某个文件才会让它的上位结论失效，回落到下位（见 6.1）。
- **保存触发的分析去抖 700 毫秒**，并且是单飞的：连续快速保存合并成一次，上一轮还在跑就取消并重启最新一轮，不排队堆积。
- 打开工程时的全量分析**不会打断**已经跑着的一轮（包括你手动触发的 `Lint Project`），它会在那一轮结束后开始。手动 `Lint Project` 永远优先：会取消正在跑的分析并立刻开始。
- 自动分析的结论只进编辑区显示、文件树与 `Project` 面板的状态点、诊断面板与状态栏；**绝不弹模态框**。工程级错误只给一条**可点击的状态栏提示**：`⚠ N project error(s) — click to jump`，点一次跳一个。

### 6.3 显式 Lint Project

菜单与工具栏的 `Lint Project` 是**显式**触发。工程级检查失败时，显式触发会弹一个标题为 `Project lint errors` 的窗口，列出错误，**双击一条跳到它**；普通结果则显示在诊断面板里。

### 6.4 诊断面板与悬浮修复

`View → Diagnostics`（`Ctrl+Alt+Shift+D`）打开右侧的 `Outline` 面板，其中：

- 上半部分是**当前文件的符号大纲**，双击一个符号跳到它的位置；
- 下半部分是**诊断列表**，按 `Errors` / `Warnings` / `Information` 分组，行格式是 `文件:行:列 — 消息 [规则 id]`；双击一行跳到该位置；
- 中间一行是计数 `Errors: … · Warnings: … · Information: …`，颜色与编辑器的错误/警告颜色一致；
- 顶部状态行说明当前分析的文件是已保存文件还是未保存缓冲区，以及诊断数量；
- 没有结果时给一行空状态与下一步建议，不显示假数据。

鼠标停在编辑区的诊断波浪线上，会弹出**严重级 + 规则 id + 消息**。只有引擎确实能修这条规则时才出现 `Fix`（修复）按钮：它在临时副本上运行 `verible-verilog-lint` 的自动修复，再对当前文档做**一次可撤销的整文替换**，成功后在状态栏提示「已修复 N 处（一次撤销即可还原）」。不能修时不给按钮，原因写在提示里。

在编辑器里按 `F8` / `Shift+F8` 跳到当前文件的下一个 / 上一个诊断（编辑器有焦点时生效）。

### 6.5 文件状态点

`Files` 树在图标与文件名之间画一个状态点；`Project` 面板的 `Files` 列表在最左侧画同一个标记。两处用的是同一套状态定义，颜色与含义一致：

| 状态 | 含义 |
|---|---|
| 实心红 | 该文件有 **error** |
| 实心黄 | 只有 **warning**（没有 error） |
| 实心绿 | **已分析、没有问题** |
| 空心灰环 | **还没有可信结论**：在分析集里但还没轮到（排队中 / 正在分析），或因为有未保存修改被排除、而旧结论已经失效 |
| 无标记 | 不在任何分析集里：目录、非 HDL 文件，或还没有打开工程 |

覆盖范围：

- **打开/切换工程**后自动跑一次全工程分析（见 6.2），所以工程里**没打开过的文件也有点**；
- **保存**只重新分析刚保存的文件，其他文件的点保持上一轮的结论；
- 结论**按文件保留**：切换标签页不会让别的文件变回“没有结论”；
- 通过 `.f` 导入、但不在工程目录里的文件，出现在 `Files` 树的 `From filelist (N)` 分组里并带同样的点，也在 `Project` 面板的 `Files` 列表里带同一个标记；
- 有未保存修改的文件：如果它已经有一个**新的**结论，显示红/黄/绿；只有旧结论被编辑作废、又没有新结论时，才显示空心灰环。

把鼠标停在标记上，会用一句话说明当前状态（例如「Not analysed yet (queued, or excluded because the buffer has unsaved changes)」）。

## 7. 代码导航与多光标

### 7.1 跳转、引用、重命名与格式化

| 操作 | 入口 | 说明 |
|---|---|---|
| 跳转到定义 | `F12`、`Ctrl+点击`、`Project → Go to Definition` | 三者走同一条派发路径 |
| 查找引用 | `Shift+F12`、`Project → Find References` | 结果显示在独立的引用列表里，双击跳转 |
| 重命名符号 | `F2`、`Project → Rename Symbol…` | 会真正应用编辑，不是预览 |
| 格式化文档 | `Ctrl+Shift+F`、`Project → Format Document`、工具栏 | 调外部 `verible-verilog-format`，结果**一次撤销**可回退 |

- `Ctrl+点击`：先把光标放到点击处，再走与 `F12` 相同的路径。后端不支持定义时，它**按普通点击处理**，并在状态栏说明一次原因，不会静默吞掉点击。
- 重命名：光标必须落在**可改名的符号**上；不在符号上时**先给出原因**（第几行第几列没有符号），不会先让你输入新名字再失败。后端不提供重命名能力时，菜单与右键项**置灰并说明原因**，请求路径也会拒绝。重命名结果会应用到打开的文档；未打开的文件经原子保存写入磁盘；完成后报告编辑数量，并分别说明哪些文档在编辑器里改了、哪些文件写盘了。
- **不承诺跨文件重命名**：语言服务器的会话是文件作用域的，只对当前打开文件作答（见第 10 节）。

### 7.2 多光标与块选择

| 手势 | 作用 |
|---|---|
| `Shift+Alt+拖拽` | 块选择（矩形选区，每行一个光标）——**主手势** |
| `Ctrl+Alt+点击` | 加一个光标；点到已有光标上则移除该光标——**主手势** |
| `Alt+拖拽` / `Alt+点击` | 同样的块选择 / 加光标；窗口管理器不占用 `Alt` 时可用（次选手势） |
| 普通点击 / 拖拽 | 编辑器的普通光标与普通选区 |

匹配规则对 `Shift+Alt`、`Ctrl+Alt` 与纯 `Alt` 一致：**带这些修饰键的点击 = 加/减光标，拖拽 = 块选**。所以 `Shift+Alt+点击`、`Ctrl+Alt+拖拽` 也成立；推荐用上面两个主手势，因为窗口管理器不会占用它们。（`Meta+Alt` 是例外：Linux 上它是窗口管理器的缩放手势，程序刻意不处理。）

`Ctrl+点击`（不带 `Alt`）仍然是跳转到定义；`Ctrl+Alt+点击` 先按多光标处理，不会误触跳转。

多光标下输入、删除、回车都会作用到每个光标，**一次撤销**整体回退。

为什么主手势是 `Shift+Alt` / `Ctrl+Alt`：KDE、GNOME 等桌面默认把 `Alt+拖拽`（以及 `Alt+点击`）当作窗口管理器的“移动窗口”手势，事件根本到不了应用；`Shift+Alt` / `Ctrl+Alt` 不会被它占用。VS Code 在 Linux 上把列选择改成 `Shift+Alt+拖拽` 也是同一个原因。

### 7.3 编辑器里的手势与快捷键

| 快捷键 | 作用 |
|---|---|
| `Ctrl+F` / `Ctrl+H` | 查找 / 替换浮条（由查找浮条处理，不是注册表命令） |
| `F3` / `Shift+F3` | 下一个 / 上一个匹配（同上） |
| `F8` / `Shift+F8` | 下一个 / 上一个诊断（编辑器内处理） |
| `Ctrl+/` | 切换行注释 |
| `Alt+↑` / `Alt+↓` | 上移 / 下移当前行 |
| `Shift+Alt+↓` | 复制当前行 |
| `Ctrl+Shift+K` | 删除当前行 |
| `Ctrl+Shift+U` / `Ctrl+Shift+L` | 转大写 / 转小写 |
| `Ctrl+=` / `Ctrl+-` | 放大 / 缩小编辑器 |
| `Ctrl+Shift+W` | 自动换行开关 |
| `Alt+\` | 请求一次 FIM 补全（`Tab` 接受，`Esc` 取消） |

说明：

- `F12` / `Shift+F12` / `F2` / `Ctrl+Shift+F` 是 `Project` 菜单里的命令（默认键位见 8.3），在编辑器里同样生效，所以不在这里重复。
- `Ctrl+F` / `Ctrl+H` / `F3` / `Shift+F3` 由查找浮条自己处理，**不是注册表命令**，不能在 `Preferences → Shortcuts` 里改。
- 撤销 / 重做现在是 `Edit` 菜单命令：`Ctrl+Z` / `Ctrl+Shift+Z`（重做备用 `Ctrl+Y`，见 8.3）。
- 编辑器组件自身还有一些键位（例如 `Ctrl+G` 跳行、`Ctrl+D` 注释、`Ctrl+B` 书签），产品不接管、也不在本表列出。
- 本节与 8.3 记录的是**当前版本的默认值**；要改键请到 `Preferences → Shortcuts`，多光标手势见 7.2。

## 8. 设置（首选项）

### 8.1 打开

`Edit → Preferences…`（`Ctrl+,`）打开首选项对话框。从上到下是：

- 顶部 **Theme** 下拉框（立即生效）；
- 页签：`Shortcuts`、`Tools`、`FIM`、`Appearance`、`Lint`、`Format`；
- 底部的 **Diagnostics** 框：显示设置文件的读取/保存诊断，以及 `Reload from disk`（从磁盘重新读取设置）；
- 最下面一行状态：保存成功或失败的原因。

设置写入用户配置目录（见附录 B）。改动会先**即时预览**；点 `Apply` 或 `OK` 才写进设置文件，点 `Cancel` 放弃本次改动。

### 8.2 主题

`Theme` 下拉框列出所有可用配色，选中即应用到整个界面；点 `Apply` 或 `OK` 后写进设置。状态栏的 `Theme: …` 显示当前主题。主题只改配色，不改文件类型与高亮定义。

### 8.3 快捷键

`Shortcuts` 页列出注册表里的**每一条命令**，四列：

- `Command`：命令文本与内部 id；
- `Effective shortcut`：当前实际生效的键位；一个命令有多个键位时用 ` / ` 连接（例如 `Ctrl+Shift+Z / Ctrl+Y`）；没有绑定时显示 `(unbound)`；
- `Default`：注册表里的默认**主键位**；额外的备用默认仍会出现在 `Effective shortcut` 里；没有默认时显示 `(none)`；
- `State`：`default`（仍是默认值）、`custom`（你改过）、`unbound`（没有绑定）。

操作方式：

- 选中一行，在 `New shortcut:` 里按下组合键，点 `Assign to selected command`。**立即生效，不需要重启**。
- `Reset to Default` 把选中命令恢复默认；`Reset All to Defaults` 一次全部恢复。
- 如果组合键已经被占用，页面会**拒绝**并点名占用者，例如 `Shortcut Ctrl+S is already used by Save (file.save) — nothing was changed.`；被编辑器自己的动作占用时也会点名（`the editor's own action`）。不会写入一个让两个动作同时失效的键。
- 已存在的历史冲突（手工改过快捷键文件等）会在下方以红色 `Conflicting shortcuts: …` 列出；没有冲突时显示 `No conflicting shortcuts.`

默认键位（当前注册表；`无默认键位` 表示该命令本来就没有默认键，不是遗漏）：

| 菜单 | 命令 | 默认键位 | 说明 |
|---|---|---|---|
| `File` | `New` | `Ctrl+N` | 新建空白文档 |
| `File` | `Open…` | `Ctrl+O` | 打开文件 |
| `File` | `Open Folder…` | `Ctrl+Alt+Shift+O` | 打开工程文件夹 |
| `File` | `Switch Project…` | `Ctrl+Alt+Shift+P` | 运行中切换工程 |
| `File` | `Go to File…` | `Ctrl+P` | 按文件名快速跳转 |
| `File` | `New Empty Layout` | `Ctrl+Alt+N` | 重建空布局（有未保存文档时会先询问） |
| `File` | `Save` | `Ctrl+S` | 保存当前文件 |
| `File` | `Save As…` | `Ctrl+Shift+S` | 另存为 |
| `File` | `Save All` | `Ctrl+Alt+Shift+S` | 保存全部 |
| `File` | `Save Session Now` | 无默认键位 | 立即保存会话 |
| `File` | `Quit` | `Ctrl+Q` | 退出（有未保存文档时会先询问） |
| `Edit` | `Undo` | `Ctrl+Z` | 撤销（当前分屏） |
| `Edit` | `Redo` | `Ctrl+Shift+Z` | 重做（当前分屏；备用默认：`Ctrl+Y`） |
| `Edit` | `Preferences…` | `Ctrl+,` | 首选项 |
| `View` | `Split Pane Horizontally` | `Ctrl+Alt+H` | 左右分屏 |
| `View` | `Split Pane Vertically` | `Ctrl+Shift+V` | 上下分屏 |
| `View` | `Focus Next Pane` | `F6` | 切换分屏焦点 |
| `View` | `Next Tab` | `Ctrl+Tab` | 当前分屏的下一个标签页 |
| `View` | `Previous Tab` | `Ctrl+Shift+Tab` | 当前分屏的上一个标签页 |
| `View` | `Close Pane` | `Ctrl+W` | 关闭当前分屏 |
| `View` | `Reset Layout` | `Ctrl+Alt+R` | 回到单分屏（有未保存文档时会先询问） |
| `View` | `Mixed Split (demo)` | `Ctrl+Alt+M` | 混合分屏（演示；有未保存文档时会先询问） |
| `View` | `Toolbar` | `Ctrl+Alt+Shift+T` | 显示/隐藏主工具栏 |
| `View` | `Search in Files` | `Ctrl+Alt+Shift+F` | 打开搜索面板 |
| `View` | `Toggle Files Panel` | `Ctrl+Alt+F` | 文件面板 |
| `View` | `Toggle Search Panel` | `Ctrl+Alt+S` | 搜索面板 |
| `View` | `Toggle Terminal Panel` | `Ctrl+Alt+J` | 终端面板 |
| `View` | `Toggle Source Control Panel` | `Ctrl+Alt+C` | 版本控制面板 |
| `View` | `Toggle Outline Panel` | `Ctrl+Alt+O` | 大纲/诊断面板 |
| `View` | `Diagnostics` | `Ctrl+Alt+Shift+D` | 诊断列表 |
| `View` | `Toggle Module Tree Panel` | 无默认键位 | 模块树面板 |
| `View` | `Toggle Interface Panel` | 无默认键位 | 接口面板 |
| `View` | `Toggle Project Panel` | 无默认键位 | 工程面板 |
| `View` | `Use tree-sitter parsing` | 无默认键位 | 切换 tree-sitter 解析 |
| `Project` | `Lint Project` | 无默认键位 | 检查整个工程 |
| `Project` | `Reparse Project` | 无默认键位 | 重新解析工程 |
| `Project` | `Go to Definition` | `F12` | 跳转到定义 |
| `Project` | `Find References` | `Shift+F12` | 查找引用 |
| `Project` | `Rename Symbol...` | `F2` | 重命名符号 |
| `Project` | `Format Document` | `Ctrl+Shift+F` | 格式化当前文档（备用默认：`Alt+Shift+F`） |
| `Project` | `Show Hover` | 无默认键位 | 显示悬停信息 |
| `Project` | `Show Backend Capabilities` | 无默认键位 | 显示后端能力 |
| `Project` | `Reload Backend Settings` | 无默认键位 | 重新加载后端设置 |
| `Help` | `User Manual` | `F1` | 打开本手册 |
| `Help` | `About VEdit` | 无默认键位 | 关于 |

几条说明：

- `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+P` 这几个键，编辑器自己也有一份动作。程序会把**编辑器那一份移除**，让按键只落到上面的命令上；否则 Qt 会把两个同键动作判为歧义、两个都不触发。所以它们不是死键：`Ctrl+P` 是“转到文件”，不是打印。
- 旧版本用过的 `Ctrl+Shift+H`（分屏）与 `Ctrl+Alt+V`（版本控制面板）正好被编辑器自己的动作占用，已分别改为 `Ctrl+Alt+H` 与 `Ctrl+Alt+C`，不再出现在上表。
- `Ctrl+Alt+T`（GNOME/Ubuntu：打开终端）与 `Ctrl+Alt+D`（KDE：显示桌面）是桌面全局快捷键，按键会在应用收到之前被桌面消费；因此工具栏与诊断面板的默认分别取 `Ctrl+Alt+Shift+T` 与 `Ctrl+Alt+Shift+D`。
- 查找/替换浮条与编辑器内部键位（`Ctrl+G`、`Ctrl+D` 等）不在这个页里，见 7.3。
- 上表是默认值；你改过的键位会覆盖它（`State` 变成 `custom`），保存在 `shortcuts.json`（见附录 B）。备用默认（重做的 `Ctrl+Y`、格式化的 `Alt+Shift+F`）也是默认键位；一旦给该命令设了自己的键，就以你的键为准，备用默认不再附加。

### 8.4 工具

`Tools` 页：

- **Interface language**：`Auto` / `English` / `简体中文`。切换需要重启：选完点旁边的 `Restart now to apply`。详见第 9 节。
- **LSP backend**：提供跳转、引用、重命名、大纲、悬停的语言服务器。
- **Lint engine**：工程 lint 用哪个引擎（verible / slang）。它与 LSP 后端相互独立。
- **工具路径**：`verible-verilog-ls`、`slang`、`slang-server`、`verible-verilog-format`、verible 规则文件。留空表示用随包（或自动发现）的那一份。
- **Include dirs (-I)**、**Defines (-D)**：一行一个，交给工程级 lint 使用。
- 页底说明：这些值存在设置文件里；后端与 lint 引擎的选择在**下一次解析**时生效，`-I` / `-D` 交给工程级 lint。

### 8.5 FIM（可选的补全服务）

**默认关闭**。关闭时不会发任何网络请求，也不会拖慢启动。打开后：

- `Enable FIM completion`：总开关；
- `Show the suggestion as ghost text in the editor`：以虚影文字显示建议；
- `Protocol`：OpenAI 兼容的 `/v1/completions`，或 `llama_cpp` 的 `/infill`；
- `Endpoint`、`Model`、`Timeout`、`Max tokens`；
- **API key**：输入框是**只写**的，已保存的密钥不会回显。密钥来源优先是环境变量 `VEDIT_FIM_API_KEY`；否则是与设置文件同目录、权限 `0600` 的密钥文件。保存与删除都有独立按钮，界面只报告来源和权限是否过宽。
- 密钥**不会**写进普通设置文件，不会出现在日志或界面回显里。

使用时在编辑器里按 `Alt+\` 请求补全；`Tab` 接受，`Esc` 取消，继续输入会让建议作废。失败只在状态栏显示一句短提示，不阻塞编辑。

### 8.6 外观

`Appearance` 页：

- **Font size**：界面字号，**立即生效**；选 `Follow the system` 表示不保存任何值。编辑器自身的字体由编辑器组件管理，不在这里改。
- **Interface scale**：`100 %` / `125 %` / `150 %` / `200 %`。Qt 只在程序启动时读一次缩放，所以**下次启动**才生效，页面会提示重启；选 `100 %` 表示不覆盖系统缩放。

### 8.7 Lint

`Lint` 页只有**一套控件**，显示当前 Lint 引擎（`Tools → Lint engine`）的那一组选项；两个引擎各自保存自己的设置，**切换引擎不丢另一套**。

- 当前引擎**不支持**的旋钮会**置灰**，并在提示里说明原因；不会静默发一个无效参数。
- 页面上有两行状态，分别说明 verible / slang 两组当前是「生效」还是「不是当前引擎（已禁用，值仍保留）」。
- verible 的 `--ruleset`、`--rules`、`--rules_config`、`--waiver_files`，以及 slang 的 `-W…`、`--error-limit` 都会进入工程 lint 的调用参数；默认值不发送任何参数。
- 唯一例外是 verible 的 `--autofix`：值会保存，但**只保存不生效**（当前版本没有把它传给 lint 引擎的通道）。页面顶部的状态行会明确写出这一点，以及「已保存但尚未生效」的旋钮列表。
- 规则文件优先级：页面的 `--rules_config` → `Tools` 页的 verible 规则字段 → 工程里自动发现的 `.rules.verible_lint` → 都没有就不传该参数（用 verible 默认规则）。手填值优先于自动发现。

### 8.8 格式化

`Format` 页有 23 个可配旋钮（缩进空格、列宽、换行、各种对齐、行终止符等），高级参数收在可折叠的**高级选项**里；`Restore defaults` 回到 verible 的默认值。

**默认值不发送任何参数**：只有你改过的选项才会变成格式化器的参数；全默认时只把输入文件交给格式化器，行为就是 verible 自己的默认。格式化结果在编辑器里**一次撤销**即可回退。

### 8.9 设置页的诊断

`Diagnostics` 框显示设置存储读写过程中的问题（文件损坏、权限、写入被拒绝等），并给出 `Reload from disk`：从磁盘重新读取设置，适合手工改过设置文件之后使用。

## 9. 语言与显示

### 9.1 界面语言

- `Edit → Preferences… → Tools → Interface language`：`Auto`（跟随系统）、`English`、`简体中文`。
- 切换后需要重启才能生效，页面提供 `Restart now to apply`。
- `Auto` 跟随系统 locale。**AppImage 只带 `C.UTF-8` 一个 locale**，所以打包运行时 `Auto` 通常解析为英文；要看中文，请显式选 `简体中文` 再重启。
- 中文翻译文件随包（`bin/translations/`），不需要另外安装语言包。

### 9.2 中文输入

VEdit 不自带输入法，中文输入依赖系统安装并正在运行的输入法（ibus / fcitx 等）。AppImage 没有随包携带 Qt 的输入法平台插件（`platforminputcontexts`），能否候选与上屏取决于系统输入法与 Qt xcb 平台插件的配合；如果输入法在 AppImage 里不可用，可以改用源码构建（使用宿主的 Qt 插件），或先确认系统输入法在其他程序里工作正常。输入法本身不可用是系统环境问题，不是编辑器功能。

### 9.3 字体与中文宽度

- AppImage 自带 `Noto Sans CJK`，中文注释与界面文字不会显示成方块。
- 编辑器对拉丁字符使用 `DejaVu Sans Mono`，对中文字符回退到 `Noto Sans Mono CJK`。
- 两种字体的字宽不同，所以**一个中文字符的宽度约为一个拉丁字符的 1.60 倍**，而不是排版上的 2.0 倍。纯中文内容仍然对齐；中英文混排的注释列对齐是近似的，这是有意的取舍（优先保证纯 ASCII 代码的宽度稳定）。

## 10. 已知限制

1. **跨文件重命名不承诺**：语言服务器的会话是文件作用域的，只对当前打开文件作答。重命名只承诺服务端在当前文件里返回的编辑（以及因此被写入的文件），不要指望它改遍整个工程。
2. **工程级正则 = RE2 风格**：`Search` 面板（整个工程）不支持反向引用与 lookaround；`Ctrl+F` 当前文件是 PCRE 风格。同一个正则在两个范围里结果可能不同（见 4.4）。
3. **替换的残留边界**：写每个文件前都会校验冻结扫描快照、文件戳与命中行身份。命中行超过 400 字符时，搜索只保存了截断预览；对**未保存的缓冲区**，替换前会把缓冲区里的**整行**与磁盘上的整行做完整比对（去 BOM + trim，与写入端同一个解码/切分），不一致或拿不到缓冲区文本就**拒绝**（不保存、不写入）。无法完全排除的残留只有一种：文件被外部程序改写，且**大小与修改时间都不变**——文件戳校验看不出变化。
4. **版本控制只验证了本地仓库**：真实的网络传输（HTTPS / SSH 远端）没有做端到端验证，目前只覆盖本地裸仓库（`file://`）与一个本地 401 认证传输。历史列表有上限（一次一页 ≤ 200 条、blame ≤ 20000 行、文本 ≤ 2 MiB、refs ≤ 2000）。
5. **模块树与接口的规模上限**：层级深度上限 64 层、最多 20000 行；超限或环形例化显示一行标记。没有工程或 `slang` 没报出模块时，只显示一行原因。
6. **`Alt` 手势可能被窗口管理器截获**：KDE / GNOME 默认用 `Alt+拖拽`（以及 `Alt+点击`）移动窗口，事件到不了应用。因此块选择与加光标的**主手势**是 `Shift+Alt+拖拽` 与 `Ctrl+Alt+点击`（窗口管理器不占用它们）；`Alt+...` 只作为次选手势保留。见 7.2。
7. **中文宽度 1.60 倍**：中英文混排的注释列对齐是近似的（见 9.3）。
8. **工程面板不能增删 `-I` / `-D`**：`Project` 面板里的 `Include dirs (-I)` 与 `Defines (-D)` 是从 filelist 展开得到的**只读**结果，没有按工程增删的入口。要调整交给工程级 lint 的 `-I` / `-D`，用 `Edit → Preferences… → Tools` 的全局设置（见 8.4）；要改变展开结果，改 filelist 后按 `Refresh`。
9. **界面语言的 `Auto` 在 AppImage 里通常落到英文**：包内只有 `C.UTF-8`；需要中文请显式选择（见 9.1）。
10. **打包产物不加载用户自定义语法定义**：AppImage 只加载随包白名单里的语法定义；放在用户目录里的自定义定义在 AppImage 里不会生效。
11. **没有真机 GUI 验证**：本版本的界面证据来自离屏 / 软件渲染环境与代码核对，没有在真实桌面上用鼠标逐项验收；实际观感、DPI、字体与中文渲染可能不同，需要在你的桌面上确认。软件渲染截图里缺 CJK 字体时，某些按钮（例如「修复」）会显示成方块——按钮存在且位置正确，不代表产品表现。
12. **FIM 的错误提示只有中文**：FIM 不可用或失败时，状态栏显示的原因来自 Rust 服务，固定为中文短句（如 `FIM 未启用`、`FIM 超时（5000ms）`），不随界面语言切换。
13. **语法定义的「同名遮蔽」**：随包的 5 份定义（Verilog / SystemVerilog / VHDL / Markdown / Tcl）都声明 `version="1"`。在**自己从源码构建、且系统装有 KDE 语法定义**的环境里，KF6 语法仓库同名时取版本高者，KDE 内嵌的 verilog / systemverilog / vhdl / markdown 会胜出——这 4 个名字实际生效的是 KDE 副本，只有 Tcl 用我们的定义。**交付的 AppImage 不受影响**：AppRun 收窄 XDG，私有语法库只含随包白名单的 5 份。把版本号提高是已登记但未做的决策。

## 附录 A 产物形态与安装

本节是**部署**说明，不影响日常使用。

### A.1 单文件 AppImage（当前交付物）

VEdit 以**单文件 AppImage** 交付：

```sh
chmod +x VEdit-<版本>-x86_64.AppImage
./VEdit-<版本>-x86_64.AppImage                # 空编辑器
./VEdit-<版本>-x86_64.AppImage path/to/top.sv # 直接打开文件
```

- 包内自带 Qt6 / KF6 / KTextEditor / tree-sitter、CJK 字体、`verible` 与 `slang`，目标机上不需要安装任何依赖。
- 目标平台是 CentOS 7.9 x86_64（glibc 2.17）及更新的主流发行版；打包流程已在 CentOS 7、Ubuntu 24.04、Rocky Linux 9、Ubuntu 26.04 的容器环境验证启动与基本功能。
- 直接运行需要宿主提供 FUSE（`libfuse2`）。没有 FUSE 时用：

```sh
./VEdit-<版本>-x86_64.AppImage --appimage-extract-and-run
APPIMAGE_EXTRACT_AND_RUN=1 ./VEdit-<版本>-x86_64.AppImage
```

### A.2 从源码构建（开发用）

```sh
bash scripts/build-qt.sh              # 构建 + 测试
bash scripts/build-qt.sh --no-test    # 只构建
```

产物是 `target/qt6-build/src/app/vedit-app`。在另一台机器直接运行它需要 KF6/Qt6 运行库、`libtree-sitter` 0.25 与 Qt 平台插件，版本与包名见 `docs/BUILD.md`。

### A.3 不在本文范围的产物

如果你手上是更早打包的 `tar.gz` 目录包或自解压 `.run` 单文件，那套产物对应的是**旧界面**，本文描述的面板、设置页与快捷键都不适用于它；日常使用请改用 A.1 的 AppImage。

## 附录 B 配置文件与位置

默认状态目录是 `~/.config/VEdit/vedit-app/`（可用启动参数 `--state-dir <目录>` 改）。

| 文件 | 内容 |
|---|---|
| `settings.toml` | 首选项：主题、界面语言、字体与缩放、工具路径、Lint 与格式化选项 |
| `session.json` | 会话：工程根、分屏布局、打开的文件与光标/滚动位置、面板与窗口状态、主题 |
| `shortcuts.json` | 你改过的快捷键（只存覆盖项） |
| `project-model.json` | 当前工程的 filelist、变量表与展开结果缓存 |
| `fim_api_key` | 可选的 FIM 密钥文件，权限 `0600`；只由设置页写入 |

工程内的文件：

- `*.f` / `*.F`：filelist（对话框只筛这两种；`.flist` 拼写可用 `All Files (*)` 当普通文本打开，但**不**按 filelist 导入）；
- `.rules.verible_lint`：工程级 verible 规则文件，存在时自动发现；不存在时用内置规则，不报错。

常用启动参数（把 `vedit-app` 换成你的启动方式）：

```sh
vedit-app path/to/file.sv         # 打开文件（多个文件各占一个分屏）
vedit-app --project <目录>        # 以该目录作为工程根
vedit-app --state-dir <目录>      # 换一个状态目录
vedit-app --theme <主题名>        # 指定启动主题
vedit-app --no-session-restore    # 忽略上次会话，干净启动
```

## 附录 C 排障

| 现象 | 原因 / 处理 |
|---|---|
| 界面是英文，设了中文也不变 | 切换语言后需要重启；用 `Tools → Interface language → Restart now to apply`。`Auto` 在 AppImage 里通常解析为英文（9.1） |
| 中文显示成方块 | 用包内 AppImage 启动；自带字体没有生效时，先确认没有绕过 AppRun 自行启动解包目录 |
| 输入法没有候选框 | 系统输入法（ibus / fcitx）没有安装或没有运行；AppImage 也没有随包输入法插件（见 9.2） |
| AppImage 起不来，提示找不到 FUSE | 装 `libfuse2`，或用 `--appimage-extract-and-run` / `APPIMAGE_EXTRACT_AND_RUN=1`（A.1） |
| 状态栏或面板提示找不到 `verible` / `slang` | AppImage 自带这两个工具；从源码运行时用 `bash scripts/fetch-verible.sh` 与 `bash build/slang/build.sh` 取得，或用环境变量 `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` 指向实际路径 |
| `Outline` 面板一直为空 | 看面板顶部状态行：没有打开文件、后端缺失、工程目录未打开都会给出原因 |
| 模块树为空 | 没有工程时显示原因；有工程时点 `Refresh`；`slang` 报错时状态行会写出错误 |
| 保存后没有自动 lint | 自动 lint 需要工程文件集（filelist 或工程目录）；没有工程时只做文件级诊断 |
| `Lint Project` 没反应 | 看状态栏的一次性提示（「没有工程」「工程 lint 没有启动：原因」） |
| 快捷键按了没反应 | 到 `Preferences → Shortcuts` 看下方是否有红色冲突提示；编辑器内部手势（7.3）不在这个表里，不能在那里改 |
| 会话没有恢复 | 检查是否用了 `--no-session-restore`；会话文件损坏时状态栏会说明并干净启动（2.6） |
| 终端面板显示 `Not started` | 点面板工具栏的 `Restart` 启动 shell；shell 来自系统的 `SHELL` 环境变量 |

## 附录 D 版本控制面板：能做什么、工具链与安全边界

### D.1 位置与两个页签

`Source Control` 面板在左侧，顶部有两个页签：

- **Working tree**：三栏布局——左栏 `Branches and tags`，中栏 `Changes` 与 `Staged`，右栏 `Diff`。底部是操作栏：`Refresh`、`Stage`、`Unstage`、`Discard…`、`Commit…`、`Remove…`、`Fetch`、`Pull`、`Push`、`Branch…`、`Tag…`、`Remote…`、`Workspace…`。合并/变基等操作进行中时会出现冲突提示条与 `Continue` / `Abort`。
- **History**：提交列表与提交详情；可以按提交说明搜索、翻页（`Previous` / `Next`），也可以对某个文件看 `File history`、`Blame`，或用 `Compare selected` 比较两次提交。

面板底部有一行**能力说明**：当前后端不支持的操作会置灰，并把**真实原因**写在提示里（不是「未实现」）。

### D.2 随包工具链

交付的 AppImage 里**捆绑**了 `git` 与 `subversion` 及其依赖库，干净机器上不需要另装。解析顺序是 **bundled > PATH > env**；想强制使用系统自带的版本，可以设 `VEDIT_GIT` / `VEDIT_SVN`（或对应的 `_DIR`）指向自己的二进制。`svnadmin` 不随包。

### D.3 能做什么

- 读：状态（后端 / 分支或修订 / 变更文件）、单文件 diff、refs、提交历史、blame、比较；
- Git 写：stage / unstage / discard、commit（含 amend）、fetch / pull / push（`file://` 或本地裸仓库）、分支与标签的创建/切换/删除/重命名、merge / rebase / cherry-pick / revert / reset、stash、worktree、submodule；
- SVN：status / info / diff / commit / update / revert / add / remove；能力表里不支持的操作会在面板置灰并给出真实原因。

所有操作都是**异步**的：即使 `git` / `svn` 卡住，界面也不会冻结。

### D.4 安全边界

- **读路径拒绝一切写子命令**：读操作在**启动进程之前**就拒绝 commit / push / checkout / reset / revert / switch / update 等；写只能走**逐操作白名单**，未列出的操作、非仓库、后端不匹配、越界路径一律默认拒绝。
- **危险操作要确认两次**：`reset --hard`、`clean`、`discard`、删除分支/标签、删远端、`worktree remove` 都是「确认 → 再确认」，任一步取消都**不写任何东西**。
- **凭据不落盘**：没有 helper/SSH 时弹出的凭据框只把密码放进**子进程的环境变量**，不进命令行参数、不写文件、不进日志；取消即中止，并显示 git 的真实原因。
