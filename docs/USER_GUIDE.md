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
9. 语言、VHDL 与 Scala（含显示）
10. 已知限制
附录 A 产物形态与安装
附录 B 配置文件与位置
附录 C 排障
附录 D 版本控制面板：能做什么、工具链与安全边界

## 1. 这是什么：界面总览

### 1.1 主窗口的六块区域

从上到下、从左到右，主窗口分成六块：

1. **菜单栏**：固定的五个菜单 `File` / `Edit` / `View` / `Project` / `Help`。
2. **主工具栏**：最常用的命令，每个按钮带一个随主题着色的线性图标和文字，从左到右是 `New`、`Open…`、`Open Folder…`、`Switch Project…`、`Save`、`Save All`、`Undo`、`Redo`，然后是 `Search in Files`、`Lint Project`、`Format Document`。工具栏最右端是**主题按钮**（一个彩色圆点图标），点开就是六套界面配色的切换菜单。
3. **编辑区**：每个标签页是一份打开的文件；可以水平或垂直分成多个分屏，当前分屏右上角有 `active` 标记。
4. **左侧停靠区**：`Files`（文件树）、`Project`（工程，只在有工程时出现）、`Source Control`（版本控制）。
5. **右侧停靠区**：`Module Tree`（模块树）、`Interface`（接口）、`Outline`（上半是大纲符号，下半是诊断列表）。
6. **底部停靠区**：`Search`（工程搜索）、`Terminal`（内置终端）。

底部的**状态栏**：左侧是工程名 `Project: …` 和只停留几秒的一次性提示（例如「已保存」「打开失败」）；右侧依次是未保存标记 `● Unsaved changes`、主题 `Theme: <界面主题> / <语法主题>`、分屏与标签统计 `Panes: … · active: … · tabs: …`，以及诊断徽标 `<请求的 LSP>→<实际> · lint=<lint 引擎> · basis=<…> · reason=<…>`（请求与实际一致时没有箭头；末尾可能带 ` · ⚠ N`，表示设置文件里有 N 条载入警告。字段与原因码见 6.6）。

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

面板的显示与隐藏都在 `View` 菜单里（每一项是勾选式的）：`Toggle Files Panel`、`Toggle Search Panel`、`Toggle Terminal Panel`、`Toggle Source Control Panel`、`Outline`（旧版本这里叫 `Toggle Outline Panel`，旁边还有一条重复的 `Diagnostics` 项；两条现已合并为一条 `Outline`）、以及 lane 注册的 `Toggle Module Tree Panel`、`Toggle Interface Panel`、`Toggle Project Panel`。用面板标题栏的 `×` 关掉后，用同一个菜单项可以再打开。

首次启动时显示 `Files`、`Outline`、`Module Tree`、`Interface`；`Project` 在有工程后自动出现。之后每次启动会恢复你上次的布局。

### 1.3 帮助

按 `F1`，或点 `Help → User Manual`，会打开一个独立的只读窗口，内容就是这份手册。它随程序一起打包，不需要联网。关掉窗口不影响编辑。

### 1.4 常用操作从哪里开始

- 打开文件：`File → Open…`（`Ctrl+O`），或在 `Files` 树里双击。
- 打开工程：`File → Open Folder…`，或工具栏的 `Open Folder…`。
- 找东西：`Ctrl+F` 当前文件查找；`Ctrl+Alt+S` 打开 `Search` 面板搜整个工程。
- 看问题：保存后自动 lint；`View → Outline`（`Ctrl+Alt+O`）打开大纲与诊断列表。
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

**打开 filelist 不等于导入 filelist。** 打开一个 `.f` / `.F` 只会把它当普通文本打开（`.flist` 长拼写同理，只能用对话框的 `All Files (*)` 打开）：

- 如果**当前已经有 filelist**：只打开文本，并在状态栏给一条约 8 秒的提示（说明未被导入、要导入请用 `Project` 面板的 `Import Filelist…`）。工程文件集一个字节都不变。
  该提示是信息性的：`File ▸ Open` 或命令行打开会在它之后显示 `Opened N file(s)` 把它覆盖掉，这是正常现象，**不代表发生了导入**。
- 如果**当前还没有任何 filelist**：弹出一个确认框，两个动作 `Import` 与 `Open Only`，**默认、回车、关闭都是 `Open Only`**。只有点 `Import` 才会把该 filelist 所在目录设为工程根并导入；点 `Open Only` 什么都不导入，也不设置工程根。
  注意：**没有工程时 `Project` 面板的 `Import Filelist…` 是禁用的**，所以首次设置工程根只能走这个确认框。
- 已经有工程后要追加清单：用 `Project` 面板的 `Import Filelist…`；导入只作用于当前工程，**不切换**工程。

导入之后，`Project` 面板会列出它展开出的文件、`-I` 包含目录与 `-D` 宏。**编辑 filelist 后按 `Project` 面板的 `Refresh` 重新展开**；重复导入同一个 filelist 是幂等的，会在面板底部提示 `Already imported: nothing changed (use Refresh to re-read an edited filelist)`。导入的清单随工程持久化，要移除用 `Remove Selected Filelist`。

导入的 `.f` 与 `vedit.toml` 的 `[project] filelists` 就是工程文件集的全部来源；根目录下其它 `.f` 与未被声明的源文件默认不进入工程（见 2.8）。

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
- 界面主题与语法主题。

不恢复：终端历史、搜索面板的临时结果、补全的临时预览。

- 想立刻保存当前会话（不退出）：`File → Save Session Now`。
- 启动时加 `--no-session-restore`：忽略已有会话，从干净状态开始。
- 会话文件损坏时不会阻止启动：状态栏提示「Session could not be restored … — starting clean」，然后按干净状态启动。

启动时，欢迎窗口的渐变条表示 VEdit 正在准备工作台。主窗口显示后即可操作；从命令行打开 `.f` 时，导入确认框会在主窗口出现之后显示。在 `Project` 面板可以查看 filelist 的展开状态。文件集就绪后，VEdit 在后台准备模块目录，并分批显示大型模块树。在目录准备期间可用 `Cancel` 取消。

需要减弱动效时，可在启动前设置环境变量 `VEDIT_REDUCED_MOTION=1`，欢迎窗口将显示静态渐变条。使用 `VEDIT_NO_STARTUP_WELCOME=1` 可直接打开工作台。

### 2.7 路径怎么显示

工程根存在、且文件在根内时，界面显示**相对工程根的路径**；否则显示**绝对路径**。这条规则覆盖编辑器标签的提示、窗口标题、文件树提示、`Project` 面板与搜索结果行。标签页上的文字是**文件名**，把鼠标停在标签上才显示路径。状态栏与一次性提示（如 `Opened …`）仍然显示绝对路径。

### 2.8 工程文件集：只由显式 filelist 决定（`[project] auto_discover`）

工程的**文件集**（`Project` 面板的 `Files`，也是模块树/接口、工程级 lint、搜索与 LSP 的输入）默认**只由显式声明的 filelist 决定**：

- 你导入的 `.f`（见 2.3）；
- 工程根 `vedit.toml` 的 `[project] filelists = [...]` 里写下的 filelist。

默认状态**不会**自动发现工程根目录下的其它 `*.f`，也**不会**递归扫描目录里的源文件。所以一个只列 `rtl/uart.v` 的 `design.f` 就是工程的全部：同目录里同名、甚至同名 module 的 `sim/uart.v` 不会进入工程集，也就不会抢走端口/接口/模块树里显示的定义。

需要恢复旧行为时，在**工程根**的 `vedit.toml` 写：

```toml
[project]
auto_discover = true
```

| 项 | 值 |
|---|---|
| 名字 | `auto_discover` |
| 位置 | 工程根的 `vedit.toml`，`[project]` 表 |
| 默认值 | `false`（只认显式 filelist） |
| 作用 | **同时**控制两件事：工程根本层 `*.f` 的自动发现，以及工程根源文件（`*.v` / `*.sv` / `*.vh` / `*.svh`）的递归扫描 |

- `[project] filelists` 是你写下来的声明，**永远生效**，不受这个开关影响；只有“自动发现”与“递归扫描”受它控制。
- 打开/切换工程、按 `Project` 面板的 `Refresh`、或导入/移除 filelist 时会重新展开，改完 `vedit.toml` 后重新展开即可生效。
- 旧行为（开关打开）的风险正是 2.8 开头那条默认规则要避免的：根目录任意一个 `.f`、任意深层目录里的源文件（包括构建产物目录）都会进入文件集，同名 module 可能来自你从未声明过的文件。

## 3. 编辑基础

### 3.1 标签页与分屏

- `File → New` 新建一个空白文档；`File → Open…` 打开文件；两者都会在当前分屏里增加一个标签。
- 关闭一个文件：点标签右侧的 `×`。有未保存修改时会问「保存 / 丢弃 / 取消」，取消则标签保持打开。
- 分屏：`View → Split Pane Horizontally`（`Ctrl+Alt+H`）左右分，`View → Split Pane Vertically`（`Ctrl+Shift+V`）上下分；`F6` 在分屏间切换焦点；`View → Close Pane`（`Ctrl+W`）关闭当前分屏（其中每个未保存文档都会逐个询问）。
- `View → Reset Layout`（`Ctrl+Alt+R`）回到单分屏；`File → New Empty Layout`（`Ctrl+Alt+N`）重建一个空布局。这两个命令（以及 `View → Mixed Split`）在**有未保存文档时会先询问**：可以保存、丢弃或取消（有多份未保存文档时是「全部保存 / 全部丢弃 / 取消」）；选**取消**则分屏、文档与布局都保持原样。
- 点哪个分屏就激活哪个分屏：鼠标按在非活动分屏的任意位置，或焦点进入它，之后的命令、跳转与重命名都跟随它。
- **标签拖拽**：在同一标签栏内拖动标签只改变顺序；把标签拖出标签栏、落在编辑区的左/右/上/下边缘，会按该方向分屏，文档在源分屏与新分屏之间共享同一缓冲区（未保存内容跟着走，不会从磁盘重读）；拖到另一个分屏的标签栏则把文档移动到那个分屏。拖动时显示落区提示，按 `Esc` 或拖回原处取消。

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

这两个面板的数据来自 `slang` 对整个工程做的一次**模块目录**，在后台异步执行，不会卡住编辑。它们只描述**模块与端口**：没有别的行类型。**工程模式需要显式导入 filelist**：没有 filelist 时模块树显示空态原因、顶层下拉禁用，所有工具按单文件工作。导入 filelist 后会先自动跑一次「只建目录、不跑 lint」的 catalog；在顶部选择顶层模块后再按新顶层重跑一次。**工程超过 5000 个文件时不会自动跑全量目录**：输入顶层模块后回车（只展开该子树），或点 `Refresh` 显式跑全量；运行中可 `Cancel`。超时不会清掉已输入的顶层，也不会自动重试。

### 从信号追踪驱动和负载

导入 `.f` 并等待工程文件集展开。保存相关 HDL 文件后，将光标放在信号名上，从编辑器右键菜单或 `Project` 菜单选择 `Trace Driver` 查看谁写入它，或选择 `Trace Load` 查看谁读取它。只有一个直接结果时会跳到源码；有多个结果时，先按完整实例路径选择。同一模块被重复例化时，可以据此区分两个实例。

`Show Driver Chain` 和 `Show Load Chain` 展开多级分支。按 Enter、双击或点 `Jump to Source` 都可以打开选中节点的位置。追踪按整个信号处理普通赋值、过程赋值与模块端口连接。位选和片选可能归到其基础信号；环形连接会在结果中标出。当前追踪上限为 2,000 个工程文件，结果树最多 32 层、512 行。分析使用磁盘上已保存的设计。

新配置的 Verilog/SystemVerilog 语义后端默认为 `slang-server`。已有的显式后端选择会保留；缺少首选服务时，在状态栏可查看实际启用的后端及原因。信号追踪另调用 slang CLI。

### 5.1 模块树

- 打开：`View → Toggle Module Tree Panel`（右侧）。
- **根节点**：顶部 `Top module` 下拉显式选择一个顶层模块时，树根就是该 top 的子树；`Auto`（空）时根节点 = 从未被任何模块例化的模块（顶层模块），可以有一个或多个。全部模块都被例化过时，面板会明确说「没有顶层模块」。顶层选择按工程根持久化，并影响 lint 的 top scope；无效的 top 会回退 `Auto` 并在汇总行说明。
- 同一个模块被两个父模块例化时，它在**两个父下各出现一行**（靠缩进区分实例路径）。
- 列：`Module` / `Instantiations` / `Definition`。`Instantiations` 是该父下这一模块的例化次数，`Definition` 是定义位置（`文件:行:列`）。
- **双击模块行**打开它的定义文件，并把光标定位到模块声明处。
- 工具栏：`Refresh`（对整个工程重新跑一次模块目录；需要已导入 filelist）、`Filter modules…`（不区分大小写，匹配任一列；匹配行的父行始终保留可见）。
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

### 6.1 两个来源，两个显示面

- **实时 LINT**：当前打开文件的文件级 / LSP 诊断。它只分析当前文件与「当前文件里各模块直接例化的子模块」的定义文件（一级，用于消错），但**只显示当前文件**的诊断；子模块文件里的问题不会出现在列表、波浪线或计数里。
- **工程 LINT**：`Project → Lint Project` **手动**对整个工程跑一趟批量检查，结果显示在底部「工程 LINT」dock（文件树 + 问题列表）。

两个来源**各自显示在自己的位置**：编辑器的波浪线 / 行号栏、右侧 `Outline` 诊断区、诊断计数与状态栏徽标只显示实时 LINT；工程 LINT 的结果只在底部面板。工程结论在下一次手动 pass 或该文件被编辑作废之前保留。

### 6.2 自动分析：只有实时 LINT

- **自动 LINT 只有实时 LINT**：编辑当前文件后按既有去抖分析当前文件 + 它的一级子模块定义文件；显示面仍然只包含当前文件。实时 LSP 会话只覆盖当前文件与一级闭包，不会用整个工程集初始化；工程文件集展开慢时，`Outline` 会显示原因，不会静默空白。
- **全工程 LINT 永不自动**：打开工程、切换工程、导入 filelist、设置顶层模块、保存文件都**不会**启动全工程 pass。工程里没打开过的文件在你手动 `Lint Project` 之前不会有工程结论。
- 工程 LINT 结论**按文件保留**：切换标签页、切走再切回来都不会让别的文件失去结论；编辑某个文件会让它的工程结论失效，直到下一次手动 pass。
- 实时 LINT 的闭包有上限（每个文件最多 64 个定义文件、总数最多 128 个）：超过时状态行给一行说明，不阻塞编辑。
- 手动 `Lint Project` 永远优先；自动实时分析不排队堆积。

### 6.3 显式 Lint Project

菜单与工具栏的 `Lint Project` 是**唯一**的工程检查入口。它覆盖当前展开后的工程文件集（需要**已导入 filelist**；没有 filelist 时会说明需要先导入），跑完后结果显示在底部「工程 LINT」dock：文件树 + 问题列表，双击一条跳到它。**不再弹模态窗口**打断你；状态栏只给一条可点击的摘要提示。工程文件集展开慢时，dock 常驻显示状态；文件集 **300 秒**软超时后继续等待同一任务，不会丢结果或重开第二个展开。

### 6.4 诊断面板与悬浮修复

`View → Outline`（`Ctrl+Alt+O`）打开右侧的 `Outline` 面板，其中：

- 上半部分是**当前文件的符号大纲**，双击一个符号跳到它的位置；
- 下半部分是**实时 LINT 诊断列表**（只含当前文件），按 `Errors` / `Warnings` / `Information` 分组，行格式是 `文件:行:列 — 消息 [规则 id]`；双击一行跳到该位置；
- 全工程 LINT 的结果不在这里，而在底部「工程 LINT」dock（可用 `View` 菜单里的面板开关或 `view.dock.project-lint` 命令显示/隐藏）；
- 中间一行是计数 `Errors: … · Warnings: … · Information: …`，颜色与编辑器的错误/警告颜色一致；
- 顶部状态行说明当前分析的文件是已保存文件还是未保存缓冲区，以及诊断数量；
- 没有结果时给一行空状态与下一步建议，不显示假数据；
- 后端为 Verible、且当前行集出现宏预处理遮挡时，诊断列表下方显示一条常驻提示行，说明它可能掩盖真正的语法错误，并建议切到 slang 后端（见 6.8）。

鼠标停在编辑区的诊断波浪线上，会弹出**严重级 + 规则 id + 消息**。`Fix`（修复）按钮只在**引擎确实能修这条规则**、且存储的 `--autofix` 模式可用时出现：它在临时副本上运行 `verible-verilog-lint` 的自动修复，再对当前文档做**一次可撤销的整文替换**，成功后在状态栏提示「已修复 N 处（一次撤销即可还原）」。它**不会替你保存**文件——保存时才写盘。

- 存储的 `--autofix` 模式（设置页见 8.7）参与决定按钮与后果：`inplace` 在应用前**先问一次**（verible 的这个模式会改文件）；`generate-waiver` **不编辑文档**，而是写一条 waiver 并注册（见 6.7）；`patch-interactive` / `inplace-interactive` 需要终端，产品没有终端、**不显示按钮**，原因写在提示里。
- 引擎修不了（这条规则在这里没有自动修复）时也只给原因，不给按钮。

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

- **工程 LINT 手动触发**后，本轮覆盖的文件（包括工程里没打开过的文件）才有工程结论与状态点；没跑过工程 LINT 时没有工程结论。
- **保存不再触发工程分析**；实时 LINT 只影响当前文件的编辑器与 `Outline` 显示，不刷新文件树状态点。
- 结论**按文件保留**：切换标签页不会让别的文件变回“没有结论”；
- 通过 `.f` 导入、但不在工程目录里的文件，出现在 `Files` 树的 `From filelist (N)` 分组里并带同样的点，也在 `Project` 面板的 `Files` 列表里带同一个标记；
- 有未保存修改的文件：如果它已经有一个**新的**结论，显示红/黄/绿；只有旧结论被编辑作废、又没有新结论时，才显示空心灰环。

把鼠标停在标记上，会用一句话说明当前状态（例如「Not analysed yet (queued, or excluded because the buffer has unsaved changes)」）。

### 6.6 诊断徽标（状态栏，常驻）

状态栏右侧的诊断徽标是**常驻**的（不是只停留 8 秒的一次性提示）。格式固定：

```
<LSP 请求>→<实际生效> · lint=<lint 引擎> · basis=<…> · reason=<…>[ · ⚠ N]
```

- 两个**独立**的轴：LSP 后端（跳转/引用/重命名/大纲）与 CLI lint 引擎（工程 lint）。**请求与实际一致时只显示一个名字**（如 `verible`）；不一致时显示 `请求→实际`（如 `slang→verible`），切换被拒绝后一眼就能看出来。`lint=` 同样有箭头规则。
- `basis=`：`measured`（本机实测的能力表）、`live`（当前会话在 `initialize` 里自报的能力，优先于 measured）、`declared`（没有实测时的声明值）；空值显示 `unknown`，不会悄悄升级成更强的说法。
- `reason=`：`ok` 表示**请求已生效**——**不代表设置文件没有问题**（见下面 `⚠ N`）。否则是稳定的机器 token：`tool-missing`（找不到工具）、`unknown-value`（设置里是未知取值，载入层已回退）、`select-failed`（后端拒绝这次选择）、`no-backend`（本构建没有 LSP 后端）；两个轴同时失败时按 lsp→lint 顺序用 `+` 连接；某个轴没有生效却没给出原因时显示 `not-applied`，绝不显示 `ok`。
- 某个轴没有可用值时显示 `unavailable`（不留空，也不显示一个跑不起来的后端）。
- 末尾的 ` · ⚠ N` 表示设置文件载入时产生了 N 条警告（非法拼写、被拒绝的值等，见 8.9）；完整警告文本在徽标的悬停提示里。有 VHDL 文件在前台时，LSP 轴显示 `vhdl_ls`（缺工具时是 `vhdl_ls→unavailable · reason=tool-missing`），见 9.4。

例：请求 slang、但本机没有 `slang-server` → `slang→verible · lint=slang · basis=measured · reason=tool-missing`。这条文本**重启后仍然保持**（请求与原因来自设置与工具解析结果，不依赖一次性的状态栏消息）。

### 6.7 把一条诊断加入 waiver（双引擎）

waiver = 把某条诊断写进引擎的豁免文件，让它以后不再报。两个引擎两种格式；产品按这条诊断来自哪个引擎决定默认计划：

| 引擎 | 写入格式 | 默认文件 |
|---|---|---|
| verible | 每个诊断一行 `waive --rule=<id> --line=<n> --location=<pattern>` | 工程的 `verible-waivers.txt`（`--waiver_files` 的值） |
| slang | TOML `[[waivers]]`，含 `file` 与 `diagnostic = "<option name>"` | 工程的 `slang-waivers.toml`（`lint.slang.waiver_file` 的值） |

入口：

- 在 `Outline` 面板下半部的**诊断行**上右键 → `Waive with verible-verilog-lint` / `Waive with slang ([[waivers]])`（不可用的项置灰，悬停提示就是原因）；
- 或选中一行后 `Project → Waive Diagnostic…`（`Ctrl+Alt+W`）；没有选中行时用光标所在行的诊断。

行为与边界：

- 每一步都是**异步 Rust job**：先写 waiver 文件，再把它注册进设置（`lint.slang.waiver_file` / `lint.verible.waiver_files`）；两步各有可见结果，失败显示存储层自己的原因。写完的配置会立即被下一轮 lint 使用。
- **重复添加不会重复写**：同一行同一规则第二次执行是 `written=0, deduped=1`，文件里仍然只有一个条目。
- **只写一行，绝不悄悄扩大成整文件豁免**：verible 需要**规则 id**，slang 需要**option name**；缺了就**拒绝**并说明原因（例如错误级 slang 诊断通常没有 option name；verible 的 LSP 行也可能没有规则 id）。计划文本由数据层给出，不解析用户能改的消息文本。
- 文件默认写在**工程根**里；路径可由设置覆盖。`generate-waiver` 自动修复模式（8.7）走的就是这条路径；`inplace`/`patch` 走的是编辑缓冲区那条。
- 证据：`evidence/batch-20261110c/diag-ui/waiver/01-live.log`（真实文件、真实注册、回喂引擎后该诊断被抑制）。

### 6.8 Verible 的宏预处理遮挡（已知上游行为）

针脚版 Verible LSP（v0.0-4298-g085eef05）不能展开 `` `include `` 引入的宏，也不用 `` `+define+ `` 提供的定义。所以当缓冲区里存在它无法展开的宏（`` `include `` 进来的、只由 `` `+define+ `` 定义的、定义出现在使用之后的、位于假 `` `ifdef `` 分支里的，或根本没有定义的）时，只要语法解析失败，它就**只**发一条：

```
preprocessing error at "`MACRO" Error expanding macro identifier, might not be defined before.
```

这条诊断锚在**第一个**无法展开的宏上，和真正出错的位置无关（例如删掉任意一条语句结尾的分号，它可能始终指向文件里第一个宏引用处），并把真正的 `syntax error at ...` 全部顶掉。宏能正常展开时它照常报语法错误；宏无法展开但语法本身没出错时它不发诊断。

因此 `Outline` 的诊断列表下方会显示一条常驻提示行：后端是 Verible、且当前行集里出现这类 preprocessing error 时，它说明这条错误可能掩盖真正的语法错误（例如缺少分号），并建议在 `Preferences → Tools` 把 **LSP backend** 切到 slang。提示只加一行说明，不改诊断行、位置和计数。

切到 **slang**（随包 `slang-server`）后，同一个文件会给出真正的语法诊断（例如 `expected ';'`），位置就在删除点附近；这是本版推荐的读法。

**已知限制**：随包 `slang-server` 0.3.1 忽略命令行 `-I` / `-D`，所以 `Preferences → Tools` 的 Include dirs / Defines 不会到达 slang-server 的 LSP 子进程；工程内同目录的 `` `include `` 不受影响，需要外部 include 目录或 `.f` 里 defines 的工程要等后续批次接线。Verible 侧 `--file_list_path` 里的 `+incdir+` / `+define+` 也改变不了这条预处理错误，本版不改 Verible 二进制/版本。

## 7. 代码导航与多光标

### 7.1 跳转、引用、重命名与格式化

| 操作 | 入口 | 说明 |
|---|---|---|
| 跳转到定义 | `F12`、`Ctrl+点击`、`Project → Go to Definition` | 三者走同一条派发路径 |
| 查找引用 | `Shift+F12`、`Project → Find References` | 结果显示在独立的引用列表里，双击跳转 |
| 重命名符号（当前文件） | `F2`、`Project → Rename Symbol…`、右键 | 由所选 LSP 后端提供，会真正应用编辑；会话是文件作用域的 |
| 工程级重命名 | `Ctrl+Shift+F2`、`Project → Rename Symbol in Project…`、右键 | 用 slang AST 索引整个工程文件集；先预览再应用，见下 |
| 格式化文档 | `Ctrl+Shift+F`、`Project → Format Document`、工具栏 | 按语言分派：Verilog/SystemVerilog 调 `verible-verilog-format`；VHDL 走 `vsg --fix`（含 GHDL 语法门，见 9.4）。结果**一次撤销**可回退 |

- `Ctrl+点击`：先把光标放到点击处，再走与 `F12` 相同的路径。后端不支持定义时，它**按普通点击处理**，并在状态栏说明一次原因，不会静默吞掉点击。
- 重命名（`F2`，LSP）：光标必须落在**可改名的符号**上；不在符号上时**先给出原因**（第几行第几列没有符号），不会先让你输入新名字再失败。后端不提供重命名能力时，菜单与右键项**置灰并说明原因**，请求路径也会拒绝。重命名结果会应用到打开的文档；未打开的文件经原子保存写入磁盘；完成后报告编辑数量，并分别说明哪些文档在编辑器里改了、哪些文件写盘了。
- **两种重命名的范围不同**：`F2` 的 LSP 重命名只对当前语言服务器会话负责（文件作用域，**不承诺跨文件**）；跨整个工程的重命名是下面的 `Ctrl+Shift+F2`。`F2` 已被 LSP 重命名占用，所以工程级重命名用 `Ctrl+Shift+F2`（编辑器右键菜单里两个入口都在）。

#### 信号追踪

导入工程文件清单后，保存当前 Verilog/SystemVerilog 文件并将光标放在信号上。编辑器右键菜单和 `Project` 菜单提供 `Trace Driver`（追踪驱动）和 `Trace Load`（追踪负载）。只有一个直接结果时，编辑器会跳到源位置。有多个结果时，列表按完整实例路径分组，选中一项后按 Enter 或点“跳转到源位置”。

`Show Driver Chain`（查看驱动链）和 `Show Load Chain`（查看负载链）展开可跳转的分支。循环有明确标记，树最多显示 512 行、32 层。追踪使用 slang 编译器和保存后的工程文件，支持常见端口与赋值，按信号而非位级精度追踪。

#### 工程级重命名（`Rename Symbol in Project…`）

入口：`Project → Rename Symbol in Project…`（`Ctrl+Shift+F2`），或编辑器右键菜单里的同名项。它用 **slang 的 AST 索引**定位符号的声明与引用，**先预览、再应用**。

**什么时候可用**：需要 HDL 文件在前台 + `slang` 在位 + 有工程根 + 有工程文件集；缺哪一项，命令就置灰，并把**那一行原因**写在悬停提示里。

**支持**（能唯一确定声明、且引用位置都能被 AST 定位的）：

- 信号 / 网线 / 变量 / 端口。**package 作用域的变量**也算（实测 3 个文件里的 3 处引用一起改）；package 里的 `parameter` / `constant` 属于下面的不支持类别；
- 同一工程里**跨文件**的引用按 AST 身份定位，不是按字符串匹配；
- **带 `-I` 包含目录（或 `-D` 宏）的工程**：支持。重命名会从**项目环境**（`Project` 面板展开出的 `-I` / `-D`，与工程 lint / LSP 子进程拿到的是同一份）取这两个列表并随请求交给 slang，不需要你手工再填一次；属于其它工程的 `-I` / `-D` 不会被串用。若工程环境尚未解析（例如工程还在加载），本次预览会以**引擎原因**失败（如 `not_nameable`：索引集里找不到该声明），重试即可——它不会去猜一个包含目录；
- 注释、字符串、关键字**不会**被当成引用；
- 冒号前的位置以 `文件:行:列` 展示，改前/改后逐条可核对。

**不支持（整体拒绝，绝不退化成文本替换）**：

| 类别 | 原因 |
|---|---|
| `parameter` / `constant`，以及**类型维度**里的使用（如 `[WIDTH-1:0]`） | 类型维度在 slang AST 里**没有引用节点**；改名会留下维度里的旧名字 |
| 实例名（`u_a`） | 实例前缀在层次化引用里没有绑定 |
| 被例化连接引用的端口 | 连接的 `.port` 标签**没有 AST 位置** |
| 经层次化引用到达的符号（`u_a.sig`） | 只绑定叶子名字，不绑定实例路径 |
| 不在工程文件集里的名字 | 索引不到 |

**流程与安全**：

- 输入新名字 → 预览对话框按文件分组列出每条命中的 `文件:行:列` 与改前/改后文本，并给出总数；此时**磁盘还没有任何改动**。
- `Apply` 才写盘，且写入是**原子的**：每个文件先写临时文件再替换；任何一个文件失败，**所有已写文件按校验和回滚**，并点名失败的文件，不会留下半改状态。`Cancel`、关闭对话框、以及 Apply 之前都不写任何文件（预览/取消前后文件校验和不变）。
- 拒绝原因分得清：`ambiguous`（同名多处声明，列出每一处）、`unsupported`（上表四类）、`conflict`（新名字已在某处声明）、`engine`（非法标识符、与原名相同、索引/IO 失败）。
- **边界**：`-I` / `-D` 已随请求传递（见上）；多文件写入是**回滚**而不是文件系统事务（两个文件之间崩溃不会回滚）。回滚这条边界写在「已知限制」第 1 条。

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

- `F12` / `Shift+F12` / `F2` / `Ctrl+Shift+F2` / `Ctrl+Alt+W` / `Ctrl+Shift+F` 是 `Project` 菜单里的命令（默认键位见 8.3），在编辑器里同样生效，所以不在这里重复。
- `Ctrl+F` / `Ctrl+H` / `F3` / `Shift+F3` 由查找浮条自己处理，**不是注册表命令**，不能在 `Preferences → Shortcuts` 里改。
- 撤销 / 重做现在是 `Edit` 菜单命令：`Ctrl+Z` / `Ctrl+Shift+Z`（重做备用 `Ctrl+Y`，见 8.3）。
- 编辑器组件自身还有一些键位（例如 `Ctrl+G` 跳行、`Ctrl+D` 注释、`Ctrl+B` 书签），产品不接管、也不在本表列出。
- 本节与 8.3 记录的是**当前版本的默认值**；要改键请到 `Preferences → Shortcuts`，多光标手势见 7.2。

## 8. 设置（首选项）

### 8.1 打开

`Edit → Preferences…`（`Ctrl+,`）打开首选项对话框。从上到下是：

- 顶部 **Interface theme** 下拉框（六套界面配色，立即生效）；
- 其下的 **Theme** 下拉框（编辑器语法配色，立即生效）；
- 页签：`Shortcuts`、`Tools`、`FIM`、`Appearance`、`Lint`、`Format`、`Environment`；
- 底部的 **Diagnostics** 框：显示设置文件的读取/保存诊断，以及 `Reload from disk`（从磁盘重新读取设置）；
- 最下面一行状态：保存成功或失败的原因。

设置写入用户配置目录（见附录 B）。改动会先**即时预览**；点 `Apply` 或 `OK` 才写进设置文件，点 `Cancel` 放弃本次改动。

设置文件的位置分两种：**正常启动**（不带 `--state-dir`）是 `$XDG_CONFIG_HOME/VEdit/vedit-app/settings.toml`（`XDG_CONFIG_HOME` 未设置时是 `~/.config/VEdit/vedit-app/settings.toml`）；**带 `--state-dir <目录>`** 时是 `<目录>/settings.toml`。两种情况下，保存后的设置文件都会被运行中的程序自动读入（见 8.4），不需要重启。

### 8.2 主题

主题分两层，互不干扰：

- **界面主题**（`Interface theme` 下拉框，在 `Theme` 上方）决定窗口本身的配色：画布、面板卡片、工具栏、标签页、状态栏与强调色。共六套，中文界面显示 `云雾白`、`暖杏黄`、`薄荷绿`、`晴空蓝`、`曜石黑`、`深夜蓝`，英文界面显示 `Cloud White`、`Warm Apricot`、`Mint Green`、`Sky Blue`、`Obsidian`、`Deep Navy`（前四套浅色、后两套深色）。切换入口有三个，效果完全一致：`View → 主题` 子菜单、工具栏最右端的主题按钮、以及首选项的这个下拉框。**任一次切换都会立即生效并写入设置的 `uiTheme` 键**，重启读回最后一次选择。其中「主题」子菜单与工具栏按钮是同一个主题菜单的两处入口，与首选项下拉写的是同一个键。
- **语法主题**（`Theme` 下拉框）只决定**编辑器内部**的配色：背景、文字、选中、禁用态与各类语法记号。它来自 KSyntaxHighlighting 的整套配色（`Breeze Light`、`Solarized Dark`、`Nord`、`Dracula` 等数十种），选中即应用。

状态栏的 `Theme: … / …` 同时显示两者：前面是界面主题，后面是语法主题。切换界面主题**不会**覆盖你手动选过的语法主题；界面主题只在你从未手动选过语法主题时，为浅色/深色配对一个合适的默认语法主题。

主题只改配色，不改文件类型与高亮定义。

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
| `View` | `Outline` | `Ctrl+Alt+O` | 大纲/诊断面板（dock 标题与菜单标签一致；旧版的 `Diagnostics` 重复项已删除） |
| `View` | `Toggle Module Tree Panel` | 无默认键位 | 模块树面板 |
| `View` | `Toggle Interface Panel` | 无默认键位 | 接口面板 |
| `View` | `Toggle Project Panel` | 无默认键位 | 工程面板 |
| `View` | `Use tree-sitter parsing` | 无默认键位 | 切换 tree-sitter 解析 |
| `Project` | `Lint Project` | 无默认键位 | 检查整个工程 |
| `Project` | `Reparse Project` | 无默认键位 | 重新解析工程 |
| `Project` | `Go to Definition` | `F12` | 跳转到定义 |
| `Project` | `Find References` | `Shift+F12` | 查找引用 |
| `Project` | `Rename Symbol...` | `F2` | LSP 重命名（当前文件） |
| `Project` | `Rename Symbol in Project...` | `Ctrl+Shift+F2` | 工程级重命名（slang AST，先预览再应用）；`F2` 已被 LSP 重命名占用，故用此别名 |
| `Project` | `Waive Diagnostic...` | `Ctrl+Alt+W` | 把选中的诊断写进 waiver（双引擎，见 6.7） |
| `Project` | `Run Scala Project` | 无默认键位 | 按工程形态离线运行 Scala（见 9.5） |
| `Project` | `Stop Scala Run` | 无默认键位 | 取消运行并杀掉进程树 |
| `Project` | `Format Document` | `Ctrl+Shift+F` | 格式化当前文档（Verilog→verible；VHDL→VSG。备用默认：`Alt+Shift+F`） |
| `Project` | `Show Hover` | 无默认键位 | 显示悬停信息 |
| `Project` | `Show Backend Capabilities` | 无默认键位 | 显示后端能力 |
| `Project` | `Reload Backend Settings` | 无默认键位 | 重新加载后端设置 |
| `Help` | `User Manual` | `F1` | 打开本手册 |
| `Help` | `About VEdit` | 无默认键位 | 关于 |

几条说明：

- `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+P` 这几个键，编辑器自己也有一份动作。程序会把**编辑器那一份移除**，让按键只落到上面的命令上；否则 Qt 会把两个同键动作判为歧义、两个都不触发。所以它们不是死键：`Ctrl+P` 是“转到文件”，不是打印。
- 旧版本用过的 `Ctrl+Shift+H`（分屏）与 `Ctrl+Alt+V`（版本控制面板）正好被编辑器自己的动作占用，已分别改为 `Ctrl+Alt+H` 与 `Ctrl+Alt+C`，不再出现在上表。
- `Ctrl+Alt+T`（GNOME/Ubuntu：打开终端）与 `Ctrl+Alt+D`（KDE：显示桌面）是桌面全局快捷键，按键会在应用收到之前被桌面消费；因此工具栏的默认取 `Ctrl+Alt+Shift+T`，诊断/大纲面板的 View 菜单项用 `Ctrl+Alt+O`。
- 查找/替换浮条与编辑器内部键位（`Ctrl+G`、`Ctrl+D` 等）不在这个页里，见 7.3。
- 上表是默认值；你改过的键位会覆盖它（`State` 变成 `custom`），保存在 `shortcuts.json`（见附录 B）。备用默认（重做的 `Ctrl+Y`、格式化的 `Alt+Shift+F`）也是默认键位；一旦给该命令设了自己的键，就以你的键为准，备用默认不再附加。

### 8.4 工具

`Tools` 页：

- **Interface language**：`Auto` / `English` / `简体中文`。切换需要重启：选完点旁边的 `Restart now to apply`。详见第 9 节。
- **LSP backend**：提供跳转、引用、重命名、大纲和悬停。Verilog/SystemVerilog 的新配置默认使用 `slang-server`。已有的显式选择继续生效。缺少所选服务器时，状态栏显示实际回退及原因。
- **Lint engine**：独立选择 CLI lint 引擎，默认为 `slang`，显式 `verible` 选择继续生效。全工程 lint 由用户手动启动。
- **工具路径**：`verible-verilog-ls`、`slang`、`slang-server`、`verible-verilog-format`、verible 规则文件。留空表示用随包（或自动发现）的那一份。
- **Include dirs (-I)**、**Defines (-D)**：一行一个，交给工程级 lint 使用。
- 页底说明：这些值存在设置文件里；保存后的改动**立即生效**（运行中的程序约 300 ms 内读入设置文件）：实时 LSP 会话用新选择的后端重启并重新分析当前文件，lint 引擎在下一次 `Lint Project` 时生效，`-I` / `-D` 交给工程级 lint。

### 8.5 FIM（可选的补全服务）

**默认关闭**。关闭时不会发任何网络请求，也不会拖慢启动。打开后：

- `Enable FIM completion`：总开关；
- `Show the suggestion as ghost text in the editor`：以虚影文字显示建议；
- `Provider:`：**服务商预设**。五个选项 `Custom` / `DeepSeek` / `OpenAI` / `Ollama` / `llama.cpp`，选中即自动写入 `Endpoint` 与 `Protocol`：
  - `DeepSeek` → `https://api.deepseek.com/beta/completions`（OpenAI 兼容协议）；
  - `OpenAI` → `https://api.openai.com`（OpenAI 兼容协议）；
  - `Ollama` → `http://127.0.0.1:11434`（OpenAI 兼容协议）；
  - `llama.cpp` → `http://127.0.0.1:8080`（`llama_cpp` 协议）；
  - `Custom` **不动**你已经填好的内容，手工配置的端点不会被预设覆盖。
- `Protocol`：OpenAI 兼容的 `/v1/completions`，或 `llama_cpp` 的 `/infill`；
- `Endpoint`、`Model`、`Timeout`、`Max tokens`：这些值**真正驱动** `Alt+\` 发出的请求（本版本之前，设置页里配的 FIM 并不参与请求；现在生效了）。
- `Model`：**可编辑下拉框**，既能选也能直接手填。最近一次检测到的模型列表会缓存在设置目录的 `fim_models.json`（与 `settings.toml` 同级，**不含任何密钥**）；文件缺失或损坏时下拉框只是空着，设置页照常打开，已保存的模型值也不会丢。
- **API key**：输入框是**只写**的，已保存的密钥不会回显。密钥来源优先是环境变量 `VEDIT_FIM_API_KEY`；否则是与设置文件同目录、权限 `0600` 的密钥文件。保存与删除都有独立按钮，界面只报告来源和权限是否过宽。（完整的三层顺序见本节末「配置优先级」。）
- 密钥**不会**写进普通设置文件，不会出现在日志或界面回显里。

**`Detect`（检测）**：`Detect` 按钮用你**当前填在控件里的值**探测端点——设置页是预览界面，刚改过 `Endpoint` 再点检测，测的就是新地址，而不是上次应用的值。探测**不消耗 token**：它只做端点可达性与鉴权检查并读取模型列表（只发 GET），从不发送补全请求。

- 探测在**非 GUI 线程**执行，期间按钮禁用，窗口不会冻结。单次耗时最多约为所配 `Timeout` 的两倍，没有固定延迟承诺。
- **成功**时页面回填：`Endpoint` ← 归一化后的完整补全地址，`Protocol` ← 探测到的协议，`Model` ← 探测到的模型列表，**你当前选中的模型永远保留并保持选中**（即使它不在返回的列表里，也不会被清掉或换掉）；状态行显示服务商、模型数量与耗时（毫秒）。
- **失败**时**不改动任何字段**，状态行显示原因：检测失败不会改动你已经填好的内容。
- 还有一种失败不是网络或鉴权结论——传入探测的参数无效，或探测返回的响应无法解析：状态行显示对应的失败提示（这属于程序/参数问题，不是你的配置错），详细信息写入日志。
- 端点不是合法的 `http(s)` 地址时，会在**发出任何请求之前**被拒绝，状态行显示「FIM 端点无效」。
- **检测不会保存**：结果只写进控件，点 `Apply` / `OK` 才会存盘，和其它字段一致。
- 检测成功会刷新模型列表缓存（`fim_models.json`）；该缓存不含凭据，随时删除无碍。

**DeepSeek 官方 FIM 直连可用**：端点填上面预设写入的**完整补全地址** `https://api.deepseek.com/beta/completions`，请求就按这个地址原样发出——不再需要自己挂代理改写路径。真机验证走的是真实 API，返回的模型列表为 `deepseek-flash` / `deepseek-v4-pro`。

**配置优先级**：

- 设置文件里**没有**所有权标记 `fim.ui_owned = "true"` 时，整段 FIM 配置跟随 `vedit.toml` 的 `[fim]` 段与 `VEDIT_FIM_*` 环境变量；设置文件里残留的旧 `fim.*` 键（旧版本写过、但没有标记）**不参与**，因此只在 `vedit.toml` 里配过 FIM、从未在设置页改过的人，不会被静默关掉。
- 设置文件里**带**标记 `fim.ui_owned = "true"` 时，设置页只接管**你在界面里明确改过、或检测成功写回**的那些字段；其余字段仍然跟随 `vedit.toml` 与 `VEDIT_FIM_*`（按字段继承：缺哪个键就继承哪个）。点 `Apply` / `OK` 不会把没碰过的字段写成页面默认值。
- API key 的顺序不受上面两条影响：`VEDIT_FIM_API_KEY` > 设置目录下权限 `0600` 的 `fim_api_key` > `vedit.toml` 里的值。
- 没有绑定设置文件时（纯 `vedit.toml` 路径，此时不存在「设置文件同目录」），密钥顺序退化为 `VEDIT_FIM_API_KEY` > `vedit.toml` 里的值。

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
- verible 的 `--autofix` **会生效**：存储的值原样传给 lint job。六个合法值里，`patch`（只打印补丁）、`inplace`（会改文件，应用前先确认一次）与 `generate-waiver`（写 waiver 而不改文档，见 6.7）可驱动；`patch-interactive` / `inplace-interactive` 需要**终端**（verible 会在自己的 stdin 上提问），产品没有终端、无法驱动，页面把这两个值标为**不可用**并给出原因。未知拼写会被拒绝并列出全部合法值。
- 状态行会写清哪些旋钮已应用、哪些不可用（不存在“存了却不发送”的旋钮）。规则文件优先级：页面的 `--rules_config` → `Tools` 页的 verible 规则字段 → 工程里自动发现的 `.rules.verible_lint` → 都没有就不传该参数（用 verible 默认规则）。手填值优先于自动发现。

### 8.8 格式化

`Format` 页有 23 个可配旋钮（缩进空格、列宽、换行、各种对齐、行终止符等），高级参数收在可折叠的**高级选项**里；`Restore defaults` 回到 verible 的默认值。

**默认值不发送任何参数**：只有你改过的选项才会变成格式化器的参数；全默认时只把输入文件交给格式化器，行为就是 verible 自己的默认。格式化结果在编辑器里**一次撤销**即可回退。

本页的旋钮只作用于 **verible** 的格式化路径；VHDL 的 `vsg --fix` 不受这些旋钮影响（见 9.4）。

### 8.9 设置页的诊断

`Diagnostics` 框显示设置存储读写过程中的问题（文件损坏、权限、写入被拒绝等），并给出 `Reload from disk`：从磁盘重新读取设置，适合手工改过设置文件之后使用。

### 8.10 环境变量导入（`Environment`）

第 7 页 `Environment` 有两个来源：**启动自动采集**与**手工捕获**。启动时若还没有自动采集标记（`<状态目录>/environment.auto.json`），产品会在后台识别 `$SHELL`（支持 bash / zsh / sh / ksh / fish / csh / tcsh）并捕获一次，只把 **shell 改变过**的变量写进 **global 层**（不覆盖你已经手工选过的名字）；`VEDIT_NO_ENV_AUTOCAPTURE=1` 可关闭。手工捕获仍然是「点 `Capture from shell`、勾选、`Apply`」的显式流程，只写你勾选的那些。无论哪条路径，**保存后重启 VEdit**，变量会到达：(a) 工程 filelist 的变量展开；(b) lint / catalog / hierarchy 的子进程（slang、verible-verilog-lint 等）；(c) 语言服务器（verible-verilog-ls / slang-server）的 LSP 子进程；(d) 集成终端的 PTY；(e) 诊断 / 大纲 / Scala 的 C++ 工具链解析。它**不修改本程序自己的环境**，也**不会整包导入**。

**仍然不生效的地方**（明确边界）：**版本控制子进程**（git / svn）只继承启动 VEdit 时的进程环境，**收不到**导入层。语言服务器、终端与工具链解析本批已接入导入层（见上一条）。自动采集会把 shell 里改变过的变量（包括名字像密钥的变量）写进 global 层并交给上述子进程；如果不想这样，启动前设 `VEDIT_NO_ENV_AUTOCAPTURE=1`，或只用手工捕获并保持密钥未 reveal。

- 捕获参数：`Shell:`（bash / zsh / fish / csh 等 shell 家族）、可选 `Program:`（自定义程序的绝对路径或 PATH 名）、`RC file:`（先 source 的文件）、`Start a login shell (-l)`、`Start an interactive shell (-i)`。点 `Capture from shell` 只启动**一个**子进程，界面不冻；同一时刻只允许一个捕获，再按会被拒绝并说明。
- 结果只列**新增或变化**的变量（相对本进程环境）；相同值连名字都不出现。**默认全部不勾选**，不会误导入整包。
- 候选清单再叠一层筛选：**只列当前工程 filelist 引用到的名字**（`$VAR` / `${VAR}` / `$(VAR)` 与 `~` 里出现的变量）；工程没有引用到的变量默认**折叠**，用 `Show variables the project does not reference`（默认关）展开。判据是「没被引用就不出现」，**不是**按名字排除 PATH / 密钥之类的黑名单 —— 真被 filelist 引用了，它们照样会出现在这里。
- 每行 `Provided by` 标出该名字**当前由哪一层满足**：`Project`（工程变量表）/ `Imported`（本页导入层）/ `Environment`（进程环境）/ `Missing`（没有层提供 —— 这正是需要导入的）。工程 filelist 不引用任何变量时，清单为空并写明原因。
- 主界面**没有**任何常驻的环境变量列表；这个页面是查看「工程到底用到了哪些环境变量」的唯一入口。
- `Store into:` 默认 `Project (overrides global)`，也可以选 `Global (all projects)`；同名变量**项目层覆盖全局层**，两层保存在**同一个文件**里。导入只把勾选项 upsert 到选中层，**不会覆盖**其它变量或另一层。
- `Project root:`：选项目层时必填（默认当前目录，可用 `Current folder` 一键取值）。
- 写盘时机：捕获与勾选都只是**草稿**，点 `Apply` / `OK` 才写；`Cancel`、关闭对话框都不写。写入是**原子**的（临时文件 + rename）。
- **秘密默认掩码**：名字含 `TOKEN` / `SECRET` / `PASSWORD` / `KEY`（子串、忽略大小写）的行显示为 `<masked>`；**未显式 reveal 时不会被存进文件**（勾选也会被跳过并说明）。已存的值重启后默认仍然掩码；值不会进状态栏/诊断文本，也不会进日志。
- 已存选择表可逐行 `Remove checked`，或手工填 `NAME` / `value` 后 `Add / update`（同层同名是更新；变量名不能为空、不能含 `=`、不能含控制字符）；`Reload from disk` 重新读文件。
- **失败是可见状态，且没有半应用**：rc 文件缺失/不可读 → 给出 shell 的退出码与 stderr 尾（环境仍会读到）；shell 起不来 → `nothing was changed`；超时/取消 → 结果丢弃、草稿保留；内置的 shell 家族（bash / zsh / sh / ksh / fish / csh / tcsh）一律用 `env -0` 的 NUL 形式捕获，值里含换行也会原样保留；只有 `Program:` 指定的**自定义程序**输出换行分隔（不是 NUL）环境时，含换行的值才会被拆开并给警告；名/值以**第一个** `=` 分隔。
- 文件是 `<状态目录>/environment.json`（与 `settings.toml` 同级，见附录 B）；自动采集另留一个**不含变量名/值**的标记 `<状态目录>/environment.auto.json`。坏文件降级为**空选择 + 一条警告**，不崩。导入的变量只在**启动时**读取一次（改完必须重启 VEdit），并应用到 (a) 工程 filelist 的变量展开、(b) lint / catalog / hierarchy 子进程、(c) 语言服务器 LSP 子进程、(d) 集成终端 PTY、(e) C++ 工具链解析；VCS 子进程不在此列。设置里的工具路径与解析顺序不变（`bundled → 本地 checkout → PATH → 进程环境`），导入层作为「进程环境」的一部分参与查找。
- 目标机上没装的 shell 家族会以“起不来”的原因显式报错；测试里这类情况可以显式 SKIP，不会静默通过。
- 证据：`evidence/batch-20261110c/env-ui/`（对话框、掩码、四类失败）、`evidence/batch-20261110c/env-apply/`（启动顺序与子进程环境 dump）。

## 9. 语言、VHDL 与 Scala（含显示）

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

### 9.4 VHDL 工程（`.vhd` / `.vhdl`）

VHDL 走**独立的一条 lane**，不碰 verible / slang：

- 打开 `.vhd` / `.vhdl` 文件时启动真实的 `vhdl_ls` 会话；状态栏出现本 lane 的角标 `vhdl_ls · <状态>`（`starting` / `ready` / `failed` …），诊断徽标的 LSP 轴显示 `vhdl_ls`（工具缺失时是 `vhdl_ls→unavailable · reason=tool-missing`）。VHDL 文件**不会**启动 verible 或 slang（日志里的 `VHDL-ROUTE … verilog-backends=not-contacted`）。
- 会话根：有工程根就用工程根；没有工程时向上（最多 8 层）找最近的含 `vhdl_ls.toml` 的目录，再退回文件所在目录。切换工程会停掉旧会话，下次打开重建。
- **`vhdl_ls.toml`**：工程根已有该文件时**原样使用、绝不改写**；没有时本 lane 会在工程根**生成**一份默认单库配置（`[libraries] proj.files = […]`，库名是普通库 `proj` 而不是 `work`——`work` 是被分析单元的隐式库，写进配置会被服务器拒绝），日志里写明 `source=generated`。生成最多列出 2000 个文件，超出会**报告截断**；配置写不了或内容非法时给一条**明确错误**，服务器继续运行、不崩。
- **多库边界**：默认只生成一个库；跨库 `use liba.pkg.all` 属于 `vhdl_ls 0.88.0` 的多库边界，本版本**不声称全工程 elaborate**；多库工程请自己写 `vhdl_ls.toml`（`[libraries]` 下用普通库名，不要用 `work`）。
- **Lint：GHDL + VSG 合并**。打开 VHDL 文件时异步跑 `ghdl -a`（分析）与 `vsg -js`（风格），合并成**一张行表**进同一个诊断面板：
  - 每行带来源（`ghdl` / `vsg`）。严重度是**固定映射**：GHDL 的 error / warning / note 与 VSG 的 Error / Warning / Info 映射到面板的 error / warning / information；**不认识的等级记为 unknown 并保留**，不会因为不认识就丢诊断。
  - 同一位置同一消息的重复项会被合并并**计数**（日志里的 `deduped`），不是静默丢弃；保留下来的行保留自己的来源。
  - **VSG 不给列号**，它的行落在整行（第 0 列）；GHDL 的行有列号。
  - 干净文件是 **0 行**；工具**拒绝解析**（例如语法错误时 VSG 会这样）会变成一条**说明**，绝不显示成“0 条 = 干净”。
  - 工具缺失时只显示**一条** lane 状态（缺什么、影响什么、三条修复路径），不按文件重复报错。
- **格式化**：`Format Document`（`Ctrl+Shift+F`）对 VHDL 走 `vsg --fix`：先把当前缓冲区写进临时副本 → `ghdl -a` 语法门（有 error 就零改动失败）→ `vsg --fix` → GHDL 复检 → 差异作为**一次可撤销的编辑**应用到缓冲区，**不自动保存**。失败是状态（`start` / `io` / `syntax` / `fix` / `verify` / `timeout`）并带一句原因，不是静默 no-op。
- **本版本未接的**：VHDL 的 hover / 跳转定义**菜单动作**（会话能力已在证据里实测，但没有产品级入口）、`didChange`（会话按磁盘内容打开，编辑器里未保存的修改不会推送）、outline / documentSymbol、VHDL 重命名、debug、全工程 elaborate。
- 工具解析顺序与其它工具一致：**随包 → 本地 → PATH → env**；env 可用 `VEDIT_VHDL_LS` / `VEDIT_GHDL` / `VEDIT_VSG`（或 `_DIR` 形式）。证据：`evidence/batch-20261110c/vhdl/`。

### 9.5 Scala 工程（运行与离线边界）

**运行**：`Project → Run Scala Project`（命令 `scala.run`，**没有默认快捷键**，可在 `Preferences → Shortcuts` 自己绑）按工程形态选型：

- 工程根有 `build.sbt` → `sbt -Dsbt.offline=true run`；
- 有 `project.scala` / `scala-cli.conf` / `.scala` 源文件 → `scala-cli run --offline --server=false --jvm system <工程根>`；
- 两者都没有 → **明确报错**（提示加 `build.sbt` 或 Scala 源文件），不会猜一个语言静默运行。

运行细节：输出进**现有的 Terminal 面板**（带 `[scala]` 前缀），退出码可见；`Project → Stop Scala Run` 取消并**杀掉进程树**；运行是异步 job，界面不冻；不打开 debug 会话。工具解析与其它 lane 相同：**随包 → 本地 → PATH**；`scala-cli` 缺失会点名是哪个工具，AppImage 里这些工具都是随包的。

**离线**（交付物随包 JDK / scala-cli / sbt / Metals / Bloop / coursier 与两套预置缓存）：

- 随包示例 `bin/scala-example <spinalhdl|chisel>` 在**只读挂载 + `--network none`** 下跑通两个示例（SpinalHDL 生成 `Counter.v`、Chisel 生成 `Counter.sv`）；两套 cache 互不污染（各自的清单与污染检查在证据里）。
- 运行期**没有成功的外网访问**：实测外部 TCP 443/80 = **0**。诚实的边界：仍能看到 DNS（UDP 53）尝试，全部 `ENETUNREACH`（scala-cli 的 Scala 提交探测 `api.github.com` 与 JVM 的本机名查询）；**不声称“零系统调用”**——要真正零尝试需要 BSP 层的 `--offline`（该 argv 由 Metals 的 scala-cli 集成生成，不在本仓库控制范围）。
- 需要**未随包预热**的依赖时：离线会**点名缺失的坐标**（不是静默失败）；预热说明见随包的 `usr/lib/scala/PREWARM-SCALA.txt`。

**Metals 的结论（T20，更新口径）**——旧说法“Metals 启动时联网”是**错的**，不得再用：

> 首次启动 `metals --version` 之类的诊断命令会尝试抓取 Maven 索引；离线时会显式报一行 `Could not check supported Scala versions, returning list available at release` 并回退到随包的发布版 Scala 列表（exit 0）。编辑器里的 Metals 会话不发起该请求：编译与诊断在完全断网下可用。

因此本版本对 Scala/Metals 的表述分三档：

- **已验证离线可用**（有断网日志）：Metals LSP `initialize` 完成、Metals ↔ scala-cli BSP 连接、build import、Scala 2.13.18 工程编译、6 次 `publishDiagnostics`、`metals --version` 回退 exit 0、Scala 3 默认工程 `scala-cli compile --offline`、`scala-cli run` 演示；
- **确证离线不可用**：`metals --version` 的**实时 Maven 索引导航**（解析在线 HTML，没有可预载的缓存键；发布版列表回退就是它的随包行为）；**未随包缓存的新依赖解析**（会点名缺失坐标）；
- **未验证**（既不算可用、也不算不可用）：Metals 的 hover / 定义 / 引用 / 重命名（服务器声明了这些能力，本批次未端到端驱动）、SpinalHDL 2.13.12 工程的 Metals BSP 路径、Scala 3 工程的 Metals BSP、Metals + sbt BSP、debug adapter / worksheet / scalafix。

**产品里的 Scala 编辑支持**：本版本提供 `scala.run` 运行 lane；**不声称**编辑器内的 Metals LSP 会话（hover / 定义等）已经接线。证据：`evidence/batch-20261110c/scala/`、`scala-run/`、`scala-payload/`、`scala-metals-launch/`。

## 10. 已知限制

1. **重命名分两种范围**：`F2` 的 LSP 重命名只承诺服务端在**当前文件**里返回的编辑（语言服务器会话是文件作用域的）；`Ctrl+Shift+F2` 的工程级重命名覆盖整个工程文件集，但对 `parameter`/`constant`（含 `type dimensions`）、实例名、被例化连接引用的端口、层次化引用到达的符号**整体拒绝**，并且需要 `slang` 与工程文件集。带 `-I` / `-D` 的工程**已支持**（从项目环境取，见 7.1）；多文件写入是**回滚**而不是文件系统事务（两个文件之间崩溃不会回滚）。
2. **工程级正则 = RE2 风格**：`Search` 面板（整个工程）不支持反向引用与 lookaround；`Ctrl+F` 当前文件是 PCRE 风格。同一个正则在两个范围里结果可能不同（见 4.4）。
3. **替换的残留边界**：写每个文件前都会校验冻结扫描快照、文件戳与命中行身份。命中行超过 400 字符时，搜索只保存了截断预览；对**未保存的缓冲区**，替换前会把缓冲区里的**整行**与磁盘上的整行做完整比对（去 BOM + trim，与写入端同一个解码/切分），不一致或拿不到缓冲区文本就**拒绝**（不保存、不写入）。无法完全排除的残留只有一种：文件被外部程序改写，且**大小与修改时间都不变**——文件戳校验看不出变化。
4. **版本控制只验证了本地仓库**：真实的网络传输（HTTPS / SSH 远端）没有做端到端验证，目前只覆盖本地裸仓库（`file://`）与一个本地 401 认证传输。历史列表有上限（一次一页 ≤ 200 条、blame ≤ 20000 行、文本 ≤ 2 MiB、refs ≤ 2000）。
5. **模块树与接口的规模上限**：层级深度上限 64 层、最多 20000 行；超限或环形例化显示一行标记。本版工程模式需要**显式导入 filelist**：没有 filelist 时模块树/顶层选择禁用，显示空态原因；没有工程或 `slang` 没报出模块时，也只显示一行原因。工程超过 5000 个文件时模块树不会自动跑全量目录，需要输入顶层或显式 `Refresh`；全量目录默认 120 秒业务超时，可 `Cancel`。
6. **`Alt` 手势可能被窗口管理器截获**：KDE / GNOME 默认用 `Alt+拖拽`（以及 `Alt+点击`）移动窗口，事件到不了应用。因此块选择与加光标的**主手势**是 `Shift+Alt+拖拽` 与 `Ctrl+Alt+点击`（窗口管理器不占用它们）；`Alt+...` 只作为次选手势保留。见 7.2。
7. **中文宽度 1.60 倍**：中英文混排的注释列对齐是近似的（见 9.3）。
8. **工程面板不能增删 `-I` / `-D`**：`Project` 面板里的 `Include dirs (-I)` 与 `Defines (-D)` 是从 filelist 展开得到的**只读**结果，没有按工程增删的入口。要调整交给工程级 lint 的 `-I` / `-D`，用 `Edit → Preferences… → Tools` 的全局设置（见 8.4）；要改变展开结果，改 filelist 后按 `Refresh`。
9. **界面语言的 `Auto` 在 AppImage 里通常落到英文**：包内只有 `C.UTF-8`；需要中文请显式选择（见 9.1）。
10. **打包产物不加载用户自定义语法定义**：AppImage 只加载随包白名单里的语法定义；放在用户目录里的自定义定义在 AppImage 里不会生效。
11. **没有真机 GUI 验证**：本版本的界面证据来自离屏 / 软件渲染环境与代码核对，没有在真实桌面上用鼠标逐项验收；实际观感、DPI、字体与中文渲染可能不同，需要在你的桌面上确认。软件渲染截图里缺 CJK 字体时，某些按钮（例如「修复」）会显示成方块——按钮存在且位置正确，不代表产品表现。
12. **FIM 的错误提示只有中文**：FIM 不可用或失败时，状态栏显示的原因来自 Rust 服务，固定为中文短句（如 `FIM 未启用`、`FIM 超时（5000ms）`），不随界面语言切换。
13. **语法定义的「同名遮蔽」**：随包的 5 份定义（Verilog / SystemVerilog / VHDL / Markdown / Tcl）都声明 `version="1"`。在**自己从源码构建、且系统装有 KDE 语法定义**的环境里，KF6 语法仓库同名时取版本高者，KDE 内嵌的 verilog / systemverilog / vhdl / markdown 会胜出——这 4 个名字实际生效的是 KDE 副本，只有 Tcl 用我们的定义。**交付的 AppImage 不受影响**：AppRun 收窄 XDG，私有语法库只含随包白名单的 5 份。把版本号提高是已登记但未做的决策。
14. **VHDL 只覆盖诊断 / 合并 lint / 格式化**：hover、跳转定义、outline、重命名、debug、全工程 elaborate 本版本没有产品入口；会话按磁盘内容打开、未接 `didChange`（未保存的修改不会推送）；默认只生成单库映射，多库工程请自备 `vhdl_ls.toml`。见 9.4。
15. **Scala：运行 lane 之外的编辑器能力未接线**：Metals 随包、离线会话经实测可用，但编辑器内**没有** Metals LSP 会话（hover/定义等未接）；离线仍会尝试 DNS（全部失败，`ENETUNREACH`），**不声称零系统调用**；未预热依赖会点名缺失坐标。见 9.5。
16. **环境变量导入的边界**：手工捕获默认存项目层（项目覆盖全局）；**启动自动采集**写 global 层、只取 shell 改变过的变量，`VEDIT_NO_ENV_AUTOCAPTURE=1` 可关闭。秘密形状名字在手工页默认掩码、未显式 reveal 时不落盘，但**自动采集会把 shell 改变过的密钥形状变量一并写入 global 层**并交给 LSP / 终端 / 工具链子进程（VCS 除外）；不想这样就用上面的开关或只用手工捕获。捕获按 shell 语法“最后赋值胜出”，内置 shell 家族（含 csh / tcsh / fish）都用 `env -0` 的 NUL 输出，值里含换行也能完整捕获（只有自定义程序输出换行分隔时才会被拆开并给警告）；目标机没装的 shell 家族会显式报错。见 8.10。
17. **环境变量候选清单是「打开 Preferences 时的工程快照」**：对话框开着的时候工程又展开了一次，需要重新打开 Preferences 才能看到新的引用集合。工程 filelist 不引用任何变量时清单为空并写明原因，而不是退回「全量列出」。见 8.10。
17. **设置文件里的非法值不是“设置页坏了”**：载入层会把非法值替换成默认值并给一条警告；徽标显示 ` · ⚠ N`，而 `reason=ok` 只表示**请求已生效**，不代表设置文件完美。完整警告文本在徽标悬停提示与 `Preferences → Diagnostics` 框里。见 6.6 / 8.9。

18. **Verible 的宏预处理遮挡与 slang-server 的 argv 限制**：Verible LSP 无法展开 `` `include `` / `` `+define+ `` 的宏，语法错误会被一条锚在第一个宏上的 `preprocessing error` 顶掉（`Outline` 会显示常驻提示，见 6.8）；切到 slang 后端能看到真正的诊断。但随包 `slang-server` 0.3.1 **忽略命令行 `-I` / `-D`**，`Preferences → Tools` 的 Include dirs / Defines 不会到达它的 LSP 子进程——工程内同目录 include 不受影响，外部 include 目录 / `.f` defines 要等后续批次接线。

## 附录 A 产物形态与安装

本节是**部署**说明，不影响日常使用。

### A.1 单文件 AppImage（当前交付物）

VEdit 以**单文件 AppImage** 交付：

```sh
chmod +x VEdit-<版本>-x86_64.AppImage
./VEdit-<版本>-x86_64.AppImage                # 空编辑器
./VEdit-<版本>-x86_64.AppImage path/to/top.sv # 直接打开文件
```

- 包内自带 Qt6 / KF6 / KTextEditor / tree-sitter、CJK 字体、`verible` 与 `slang`，以及 `slang-server`、`git` / `svn`、VHDL 的 `vhdl_ls` / `ghdl` / `vsg`、Scala 的 JDK / scala-cli / sbt / Metals 等随包工具（版本台账见 `docs/BUILD.md` §7.3）；目标机上不需要安装任何依赖。
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

正常启动（不带 `--state-dir`）时，状态目录是 `$XDG_CONFIG_HOME/VEdit/vedit-app/`（`XDG_CONFIG_HOME` 未设置时是 `~/.config/VEdit/vedit-app/`），`settings.toml` 与下表中的其它文件都在这里。带启动参数 `--state-dir <目录>` 时，状态目录换成 `<目录>`，设置文件随之变成 `<目录>/settings.toml`。

| 文件 | 内容 |
|---|---|
| `settings.toml` | 首选项：界面主题（`uiTheme`）、语法主题、界面语言、字体与缩放、工具路径、Lint 与格式化选项 |
| `session.json` | 会话：工程根、分屏布局、打开的文件与光标/滚动位置、面板与窗口状态、界面主题与语法主题 |
| `shortcuts.json` | 你改过的快捷键（只存覆盖项） |
| `project-model.json` | 当前工程的 filelist、变量表与展开结果缓存 |
| `environment.json` | 从 shell 导入的环境变量选择：global 与 project 两层存在同一个文件里，项目层覆盖全局层（见 8.10） |
| `fim_api_key` | 可选的 FIM 密钥文件，权限 `0600`；只由设置页写入 |
| `fim_models.json` | 最近一次 FIM 检测到的模型列表缓存，供 `Model` 下拉框使用；**不含任何凭据**，可随时删除，不会损坏任何东西（缺失或损坏时下拉框只是空着） |

工程内的文件：

- `vedit.toml`：工程配置。`[project] filelists` 声明工程文件集（默认只由显式 filelist 决定，见 2.8；`[project] auto_discover = true` 恢复旧的自动发现 + 递归扫描）；
- `*.f` / `*.F`：filelist（对话框只筛这两种；`.flist` 拼写可用 `All Files (*)` 当普通文本打开，但**不**按 filelist 导入）；
- `.rules.verible_lint`：工程级 verible 规则文件，存在时自动发现；不存在时用内置规则，不报错；
- `vhdl_ls.toml`：VHDL 的库映射。已有则**原样使用**；没有时打开 VHDL 文件会**生成**一份默认单库配置（见 9.4）；
- `verible-waivers.txt` / `slang-waivers.toml`：waiver 的默认落点（verible 用 `--waiver_files`，slang 用 `lint.slang.waiver_file`；路径可由设置覆盖，见 6.7）。

工具定位环境变量（诊断类工具都支持“直接给二进制”与 `_DIR` 给目录两种形式）：`VEDIT_VERIBLE_LS`、`VEDIT_SLANG`、`VEDIT_VHDL_LS`、`VEDIT_GHDL`、`VEDIT_VSG`、`VEDIT_GIT`、`VEDIT_SVN`。

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
| 状态栏诊断徽标显示 `…→… · reason=tool-missing` / `select-failed` / `unknown-value` | 看徽标的悬停提示：它给出人话原因与修复路径；`reason=ok` 只表示请求已生效，设置文件的非法值另以 ` · ⚠ N` 表示（6.6、8.9） |
| VHDL 文件没有诊断 / 面板为空 | 看状态栏 `vhdl_ls · …` 与 lane 的一行状态：`vhdl_ls` / `ghdl` / `vsg` 缺失会点名；工程根没有 `vhdl_ls.toml` 时 lane 会生成默认单库配置，写不了会明确报错（9.4） |
| 打开 VHDL 工程后根目录多出 `vhdl_ls.toml` | 这是 lane 生成的默认单库映射；你可以编辑它，产品不会覆盖已有文件（9.4） |
| `Run Scala Project` 报找不到工具链 | `scala-cli` / `sbt` / `java` 按 **随包 → 本地 → PATH** 解析；优先使用交付的 AppImage（随包 JDK/scala-cli/sbt），或把工具放进 PATH / 产品 `bin/`（9.5） |
| Scala 离线运行报缺少某个依赖坐标 | 该依赖不在随包预热缓存里；按随包 `usr/lib/scala/PREWARM-SCALA.txt` 预热，错误里会点名缺失的坐标（9.5） |
| `Environment` 页捕获失败 | 页面警告会给出 shell 退出码与 stderr 尾；shell 起不来 / 超时 / 取消都是“什么都没改”，重试即可（8.10） |
| 导入的环境变量没生效 | 先**重启程序**（变量只在启动时读取一次）。导入层只作用于**工程 filelist 的变量展开**与 **lint / catalog / hierarchy 子进程**；语言服务器、VCS、终端、工具链解析都**不使用**导入层（8.10）。再确认存到了正确的层（默认项目层，项目根要填对）。 |
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

### D.1.1 并排比较文件与 SVN 修订

从 `File` 菜单选择文件比较命令，或在 `Files` 树右键选中的文件，再选另一份文件。两侧按行对齐，色块标出新增、删除和替换。用 `Previous difference` / `Next difference` 选择差异，再点中间的 `←` 或 `→` 将这一块应用到箭头指向的一侧。`Undo hunk` 可以撤销这一步。目标文件已在编辑器打开时，比较会使用它当前的编辑缓冲区。核对完内容后，点目标一侧的保存按钮写入磁盘。

在 SVN 工作副本的 `Source Control` 中，`Working tree` 可打开工作文件与 `BASE`（本地检出基线）或 `HEAD`（仓库最新版本）的同一比较窗口。在文件历史中，可以比较选中修订和该文件的上一次改动、比较两个修订，或比较选中修订与当前工作文件。两个历史快照都只读；与工作文件比较时，可将选中的历史块取回工作文件再保存。

比较支持 UTF-8 文本，每侧最多 8 MiB、100,000 行。窗口显示末行是否缺少换行，并保留目标侧的 CRLF。文件在比较期间被其他程序修改时，保存会提示重新核对。

### D.2 随包工具链

交付的 AppImage 里**捆绑**了 `git` 与 `subversion` 及其依赖库，干净机器上不需要另装。解析顺序是 **bundled > PATH > env**；想强制使用系统自带的版本，可以设 `VEDIT_GIT` / `VEDIT_SVN`（或对应的 `_DIR`）指向自己的二进制。`svnadmin` 不随包。

双栏文件比较使用 AppImage 内置的 GNU `diff`。从源码构建运行时，请在系统上安装 GNU diffutils，并用 `diff --version` 确认 `PATH` 上的版本。

### D.3 能做什么

- 读：状态（后端 / 分支或修订 / 变更文件）、单文件 diff、refs、提交历史、blame、比较；
- Git 写：stage / unstage / discard、commit（含 amend）、fetch / pull / push（`file://` 或本地裸仓库）、分支与标签的创建/切换/删除/重命名、merge / rebase / cherry-pick / revert / reset、stash、worktree、submodule；
- SVN 检出：在 `File` 菜单选择检出命令，输入仓库地址并选择本地目的目录；完成后可直接打开工作副本。
- SVN 日常操作：在 `Working tree` 查看状态、变更和所属 changelist。选中一项或多项后可添加、移除、重命名、忽略、还原，或只提交所选路径。也可按 changelist 提交、导出选中项／整组／全部变更的补丁。
- SVN 分支和维护：从 `SVN Actions` 创建或切换分支、合并、解决冲突、清理工作副本，或刷新远端变更并更新选中文件。合并产生的文本冲突可先打开文件核对，再将选中项标记为已解决。
- SVN 历史：在 `History` 页查看仓库或文件历史，按修订号或文字搜索。可以保存仓库地址，查看该地址下的历史和只读快照。比较所选版本、前一文件修订、`BASE`、`HEAD` 或当前工作副本的方法见 D.1.1。
- 多工作副本：在 `Source Control` 的工作副本选择器中切换当前根。状态、历史、比较和刷新会跟随选择；关闭某份工作副本会停止监控，之后可重新打开。在父工程中操作嵌套工作副本时，先在这里选中它。自动发现包括工程内已检出的目录型 `svn:externals`。较深的目录可启用更大范围扫描，定时远端刷新需手动开启。

所有操作都是**异步**的：即使 `git` / `svn` 卡住，界面也不会冻结。

### D.4 安全边界

- **读路径拒绝一切写子命令**：读操作在**启动进程之前**就拒绝 commit / push / checkout / reset / revert / switch / update 等；写只能走**逐操作白名单**，未列出的操作、非仓库、后端不匹配、越界路径一律默认拒绝。
- **危险操作要确认两次**：`reset --hard`、`clean`、`discard`、删除分支/标签、删远端、`worktree remove` 都是「确认 → 再确认」，任一步取消都**不写任何东西**。
- **凭据不落盘**：没有 helper/SSH 时弹出的凭据框只把密码放进**子进程的环境变量**，不进命令行参数、不写文件、不进日志；取消即中止，并显示 git 的真实原因。
