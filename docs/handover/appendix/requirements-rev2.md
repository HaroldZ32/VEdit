# VEdit 重写需求：Qt6 + KTextEditor + KSyntaxHighlighting + tree-sitter，功能对齐 Lapce

cell `cell-20261031T014507403Z` · 需求 **rev 2** · 由 cell-planner 依据用户逐条答复整理

用户原话留档：
- Q8 selected = 「R2. 双引擎：tree-sitter 叠加（最像 Lapce）」
- 许可核对原话 = 「我看了是GPLv3+」
- 工程模型（Q4）selected = M3 显式；打包（Q2）selected = P1 AppImage + Q1「私有运行时，缺啥包啥」

## Goal

一句话：把 VEdit 从 Rust + GTK3 + GtkSourceView 重写为 **Qt6（图形内核）+ KTextEditor（编辑内核）+ KSyntaxHighlighting（底座高亮）+ tree-sitter（结构/高亮 overlay）** 的桌面编辑器，交互与功能对齐 Lapce 0.4.6，保留并强化 Verilog/SystemVerilog 工程能力，最终以**一份自带私有运行时的单文件 AppImage** 交付——同一份产物必须能在 CentOS 7.9（glibc 2.17）与现代 Linux 上直接运行。

分目标：

- **G1 图形内核**：窗口、面板、控件、菜单、对话框全部由 Qt6 提供，不再依赖 GTK。
- **G2 编辑内核**：文本存储、撤销/重做、选区、多光标、折叠由 KTextEditor 提供，替换现有 `sourceview4::Buffer`（`crates/vd-editor/src/document.rs:24-27`、`crates/vd-editor/src/lib.rs:3-5`）。
- **G3 高亮底座**：KSyntaxHighlighting 作为底座高亮引擎（KTextEditor 硬依赖它，必然在底座）；Verilog/SystemVerilog 定义随包分发，语法定义与主题从私有路径加载。
- **G3a 第二结构/高亮引擎（R2）**：tree-sitter 作为**第二高亮与结构引擎**，负责系统级高亮、精确折叠、面包屑/结构导航，向 Lapce 0.4.6 的观感对齐；KSyntaxHighlighting 仍为 KTextEditor 底座与**兜底**（tree-sitter 不可用或被关闭时退回）。
- **G4 Lapce 对齐（v1 范围）**：文件树、命令面板 + 模糊文件跳转、全局搜索、分屏（水平/垂直）、编辑器增强（多光标/折行/代码折叠/面包屑）、终端面板、VCS 面板（Git + SVN）、设置 UI、Verilog 工具链（见 G5）。
- **G5 工程模型**：引入显式「工程（Project）」概念——根目录 + `.f` 文件清单 + 用户手动增删的成员，可持久化，并产出「当前生效文件集」供 SLANG 与 Verible 共用；`Verible LSP + SLANG 深检 + FIM 补全`这三项 Verilog 专用能力保留。
- **G6 VCS 抽象**：同一面板背后两个后端实现——Git 与 SVN，面板代码不感知具体后端。
- **G7 交付形态**：单文件 `.AppImage`，包内自带 `ld-linux` 加载器、新版 glibc、Qt6、KF6、KTextEditor、KSyntaxHighlighting、tree-sitter 运行库、syntax 定义、主题、翻译。
- **G8 既有能力不回退**：原子保存、文件内查找替换、跳行、LSP（诊断/跳转/引用/重命名/格式化/大纲）、SLANG 深检、FIM 补全、配置加载、可改键位。

## Non-goals

v1 明确不做（不是永远不做，见 Open questions）：

1. 模态 Vim 编辑（Lapce 有；v1 不做）。
2. DAP 调试。
3. WASI/WASM 插件系统（Lapce 的 volt 等价物）。
4. 远程开发 / SSH / 独立 proxy 进程。
5. 跨文件替换——Lapce 本身也没有；v1 只有文件内替换 + 全局搜索。
6. minimap、which-key——Lapce 同样没有。
7. 免 FUSE 运行（用户未要求；libfuse2 被登记为运行前提）。
8. 系统级安装包（deb/rpm）——只交付 AppImage。
9. Windows / macOS、非 x86_64。
10. 内置 AI 模型、默认 FIM 端点或密钥（沿用旧非目标）。
11. 仿真 / 综合 / 波形查看 / 调试集成，VHDL / SystemC（沿用旧非目标）。
12. 让 VEdit 变成通用 IDE：v1 仍然是 Verilog/SystemVerilog 专用编辑器。
13. 要求"tree-sitter 与 LSP 语义完全一致"的高亮——R2 的验收只到"无双重着色 + 折叠/面包屑可观测"，不要求语义级正确。

## Users / consumers

**主要用户**：在 CentOS 7.9（glibc 2.17）工作站上做 Verilog/SystemVerilog 开发的工程师。他们通常没有 root，或不愿意为装一个编辑器升级系统；要求「拿到一个文件就能跑」，不安装 Qt/KDE、不改系统库、不联网。

**次要用户**：同一批工程师在现代发行版（Ubuntu 24.04 / Rocky 9 等）上的使用——同一份产物必须直接可用，不接受「老系统一版、新系统一版」。

**边界用户**：

- 离线/内网环境：应用必须能在断网时完成编辑与本地工具链功能（FIM 默认关闭，未配置即零网络）。
- 无独立 GPU 或旧 Mesa 的机器：必须能回退软件渲染。

**消费方（接口要定）**：

- SLANG 深检、Verible LSP、FIM 后端：消费「当前生效文件集」与文件内容。
- `git` / `svn` 可执行文件：由 VCS 面板的后端调用。
- KSyntaxHighlighting 与 tree-sitter：前者消费随包的 `syntax/` 与 `themes/`，后者消费随包的 grammar 动态库与查询文件（queries）。

**明确不被服务的人**：想用 VEdit 做综合/仿真/波形查看的人；Windows/macOS 用户；希望用插件扩展编辑器的人（v1 无插件系统）。

## Constraints

- **C1 目标系统**：CentOS 7.9（glibc 2.17）是硬目标；同一份 AppImage 还必须能在现代发行版直接运行（双宿主、单产物）。
- **C2 私有运行时**：包内自带 `ld-linux` 加载器 + 新版 glibc + Qt6 + KF6 + KTextEditor + KSyntaxHighlighting + tree-sitter + syntax 定义/主题/翻译；由 AppRun 显式经包内 loader 启动。**正因为必须交付到 CentOS 7，才必须自带新版 glibc**——不是因为放弃老系统。不要求用户安装任何依赖，不依赖宿主 glibc 提供新符号。
- **C3 运行前提（显式登记，验收时不得当作意外）**：宿主需可用 libfuse2（AppImage type-2）。用户未要求免 FUSE 运行。
- **C4 渲染**：CentOS 7 的旧 Mesa 下必须可回退到软件渲染（llvmpipe / `QSG_RHI_BACKEND=software` 等），由 M0 验证。
- **C5 版本耦合**：KF6 起步要求 Qt ≥ 6.5 且逐版本抬升；KTextEditor 硬依赖 KSyntaxHighlighting（NEEDED .so）。具体 Qt/KF6 版本在 M0 锁定并写入构建镜像。
- **C6 语法数据路径**：KSyntaxHighlighting 无专用环境变量；用 `XDG_DATA_DIRS` 或 `Repository::addCustomSearchPath()`，必要时以 `-DNO_STANDARD_PATHS` 编译成封闭包。
- **C7 许可（已确认）**：KDE 的 `systemverilog.xml` 上游**确为 `GPLv3+`**（用户亲自核对）；库本身为 MIT。**处置方式待定**（移植自家 LGPL 定义 / 接受 GPLv3+ 随包分发 / v1 只做 `.v`）；在决定前不得默认随包分发 `.sv` 语法定义。仓库现有 `assets/language-specs/{verilog,systemverilog}.lang`（LGPL-2.1-or-later，见 `assets/language-specs/README.md`、`THIRD_PARTY_NOTICES.md:15`）可作替代来源。
- **C8 工程模型（显式，非隐式推断）**：工程 = 根目录 + `.f` 列表 + 手动增删的成员；自动发现（向上找 `.git`/`.svn`/`.f`、根下递归扫 `.v/.sv`）**只作创建时的初值**；工程必须可持久化、跨重启恢复。
- **C9 `.f` 语义**：按标准 Verilog filelist 解析 `+incdir+`、`+define+`、嵌套 `-f`、注释；解析失败的条目必须给出可诊断错误，不得静默丢文件。
- **C10 VCS**：面板与后端解耦，Git/SVN 各一实现；v1 覆盖状态、分支/标签、单文件差异、刷新；写操作（commit/update/revert/switch）不在 v1 硬性范围（见 Open questions）。
- **C11 工具链刷新**：文件集变更后，SLANG 与 Verible 必须重新解析；增量或全量由实现选择，但「变更必然触发重解析，且状态可观测」是硬要求。
- **C12 底座高亮能力边界**：KSyntaxHighlighting 是逐行正则 + 上下文栈，无 AST、无符号表、不跟踪 `` `ifdef ``、无 UVM 支持；这是 R2 引入 tree-sitter 的原因，也是"底座兜底"的能力上限。
- **C13 语义高亮落地点（升级为 R2 阻塞项）**：KSyntaxHighlighting 无注入 API；tree-sitter overlay 与 LSP 语义着色都必须由 KTextEditor 宿主侧的范围/语义属性接口叠加。**在围绕该接口做设计之前，必须先验证其可用性（含性能与增量更新）**，否则 R2 的验收不成立。
- **C14 既有资产**：旧 Rust 代码库与 GTK 实现的处置不在本需求内，由计划阶段决定（见 Open questions）。
- **C15 可替换 grammar 接缝**：tree-sitter grammar 必须以抽象 `SyntaxProvider` 接入——**编译期可选择 grammar、运行期可整体关闭 tree-sitter**；不得把某个具体 grammar 的 API/语义硬编码进架构。许可或质量变化时，可换 MIT/Apache grammar 或自写而不推翻设计。
- **C16 着色冲突策略**：同一语言、同一 range 只有**单一 owner**——底座（KSyntaxHighlighting）或 overlay（tree-sitter）之一，绝不同时上色。策略须在实现前定稿，并给出可观测的「无双重着色」判据（见 A1g）。
- **C17 overlay 落地点**：tree-sitter overlay 必须经 KTextEditor 宿主侧 range / semantic-attribute 接口实现；该接口验证属于 R2 的开工前置条件，不是实施中的意外。
- **C18 tree-sitter 运行库随包**：`libtree-sitter` 与各 grammar 动态库（或静态编译进二进制）都必须打进私有运行时；若静态编译，则 C15 的可替换性必须在编译期配置层保留。

## Acceptance criteria

每条都可测量、可由 verifier 独立检查。

**M0 打包闸门**（必须先过；未过不得进入功能迁移）

- **A0a（M0a，硬闸门）**：在一台干净的 CentOS 7.9（glibc 2.17，未安装任何 Qt/KDE，未改系统库）上直接运行交付的 `.AppImage`：出现 Qt6 主窗口、KTextEditor 初始化成功、可加载随包的 Verilog 与 SystemVerilog syntax 定义及至少一个主题。
- **A0b（M0b，回归闸门）**：同一份产物在 Ubuntu 24.04 或 Rocky 9 上通过与 A0a 完全相同的检查。
- **A0c**：运行期自检（或 `ldd` 等价物）证明进程使用包内 loader 与包内 glibc，未解析到宿主 `/lib64/libc.so.6`；宿主 glibc 版本不影响启动。
- **A0d**：在只含 base 系统的机器/容器上同样通过 A0a，证明未借用宿主 Qt/KF6 库。
- **A0e**：在无 GPU 或旧 Mesa 环境下按回退配置启动成功——窗口可见、可输入，不因 OpenGL/EGL 缺失而崩溃。

**M1 编辑内核**

- **A1a**：打开 ≥10 万行 / ≈1 MB 的 Verilog 文件，滚动与输入无明显卡顿（记录基线：输入到显示 < 50 ms；滚动给出实测值）。
- **A1b**：撤销/重做 100 步内容正确；多光标在 N 处插入同一文本后一次操作全部生效，一次撤销全部回滚。
- **A1c**：折行、代码折叠、面包屑可开关且行为可见（API 断言或截图）。
- **A1d**：文件内查找替换正确；「全部替换」为一步撤销；跳行精确。
- **A1e**：保存保持原子写（临时文件 → fsync → rename）；异常中断不产生半截文件。
- **A1f（R2）**：按文档开启/关闭 tree-sitter 时，**折叠与面包屑行为可观测地变化**（开关状态可断言）；并在 1k 行 SystemVerilog 样本上给出折叠结果一致性检查（tree-sitter 开启时折叠区间集合与预期一致）。
- **A1g（R2 唯一阻塞验收项）**：**无双重着色**——逐 range 断言每个可见 range 只被一个引擎着色（例如对比开启/关闭 overlay 时的属性集合，或对若干已知区间断言单一 owner），并附人工可复核的截图证据。
- **A1h（R2）**：运行期关闭 tree-sitter 后，同一文档回退到 KSyntaxHighlighting 着色且不崩溃——证明 C15 的运行期开关真实存在。

**M2 工作区**

- **A2a**：文件树可打开目录、展开/折叠、双击打开；新建/重命名/删除后磁盘与树一致。
- **A2b**：命令面板 Ctrl+P 对 10,000 个文件的模糊过滤在 200 ms 内返回（基准脚本可测）。
- **A2c**：全局搜索返回命中文件与行号，可点击跳转；无结果时显示明确空状态。

**M3 工程与工具链**

- **A3a**：新建工程——指定根目录后自动发现给出初值（递归 `.v/.sv`、向上查找 `.git/.svn/.f`），用户可增删成员；保存后重启，工程与成员仍在。
- **A3b**：导入含 `+incdir+`、`+define+`、嵌套 `-f`、注释的样例 `.f`，展开正确；UI 显示展开后的文件数与列表；坏条目有错误提示而非静默丢弃。
- **A3c**：同一「生效文件集」同时喂 SLANG 与 Verible——样例工程上，SLANG 的输入文件数与 UI 显示一致；Verible 对集合内文件给出诊断。
- **A3d**：文件集变更（增删成员、改 `.f`）后，SLANG 与 Verible 在约定时限内重新解析，状态（进行中/完成/失败）在 UI 可观测。
- **A3e**：现有能力不回退——LSP 跳转/引用/重命名/格式化/大纲在工程文件集下可用；FIM 默认关闭，未配置时零网络请求。

**M4 VCS**

- **A4a**：同一面板下，Git 仓库与 SVN 工作副本都能显示当前分支/修订、变更文件列表、单文件差异。
- **A4b**：后端通过接口抽象——新增一个 stub 后端不改面板代码即可接入（结构检查 + 测试证明）。
- **A4c**：非仓库目录显示明确空状态，不崩溃。

**M5 设置**

- **A5a**：主题切换即时生效并可持久化；键位可在 UI 修改并持久化；冲突键位有提示。
- **A5b**：配置文件损坏时启动给出可诊断提示并回退默认值。

**跨项**

- **A6**：断网启动并完成上述编辑功能（全离线可用）。
- **A7**：libfuse2 这一运行前提在交付文档与首次启动提示中都写明。

## Open questions

1. **【KSyntaxHighlighting .sv 许可·处置】** 已确认上游 `GPLv3+`（用户核对）；处置未定：接受 GPLv3+ 随包 / 移植自家 LGPL-2.1+ 的 `.lang` 定义 / v1 只做 `.v` 高亮。此项决定 v1 是否随包分发 `.sv` 语法定义。
2. **【tree-sitter grammar 许可·严重】** Lapce vendored 的 Verilog/SystemVerilog grammar 的确切身份、commit 与许可**未核实**（研究席位无 HTTP 抓取）。若为 GPL-3.0：grammar 是编译进二进制的 C 代码，**会迫使整个 AppImage 按 GPL 分发**。缓解路径：C15 的可替换接缝 + 首选 MIT/Apache grammar 或自写。此项必须在 overlay 进交付二进制之前闭合。
3. **【折叠接口】** KTextEditor 的折叠模型能否被外部 range 驱动**未验证**；规划判断**很可能不能**——若不能，R2 的「精确折叠」需要替换其折叠视图而不是叠加，工作量与风险都要上调。
4. **【overlay 接口】** KTextEditor 宿主侧 range/semantic-attribute 接口能否支撑高频增量 overlay（大文件滚动时）需实测；这同时决定 LSP 语义着色能否复用同一机制。
5. **【SVN 写操作】** commit/update/revert/switch 是否进 v1：当前按 Lapce 对齐只做只读展示。
6. **【v2+ 顺序】** 未勾选的模态 Vim、DAP、WASI 插件、远程开发这 4 项的先后顺序，以及是否全部要做。
7. **【旧代码处置】** 旧 Rust 代码库归档 / 并行保留 / 删除；以及是否复用 `vd-lsp`/`vd-deeplint`/`vd-fim`（Rust + C ABI）还是全量 C++ 重写。
8. **【免 FUSE】** 以后是否要支持 `--appimage-extract-and-run` 自动降级（当前只登记 libfuse2 前提）。
9. **【测试矩阵】** M0b 的「现代发行版」具体清单（Ubuntu 24.04 / Rocky 9 / 其他）。
10. **【性能上限】** 是否需要规定更大的文件规模基线（例如 100 MB 单文件）。
