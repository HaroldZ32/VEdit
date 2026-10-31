# Q vs L —— 路线对比（证据版）

> cell `cell-20261031T014507403Z` · 由 cell-planner 出具 · 依据：本轮研究席位通过 GitHub 代码检索实际读到的上游源码 + 本仓库可读文件
> 本文件不改需求。需求仍为 **rev 2（维持 Q）**；若用户看完改判 L，走 rev 3（见 §7）。

## 0. 摘要（给用户，先看这五条）

1. **L（fork Lapce）在法律上是干净的**：Lapce 本体 Apache-2.0、floem（其 UI 框架）MIT（E-外部）。义务只有"给接收者一份许可证副本 + 标注修改过的文件"，没有开源传染。
2. **"Rust GUI 没有中文输入法"这个最常见的反对理由，在代码层不成立**：floem 实现了 preedit 组合与 commit，且经由 winit 的 IME 事件接到了 Lapce 编辑器（E-外部）。但**在 CentOS 7 的 X11 会话里实际能不能弹出拼音候选框，仍未验证**——这是 L 剩下的最大单点风险，一个 30 分钟的实机 spike 就能定论。
3. **L 不解决 CentOS 7 的问题，一点都不解决**：Lapce 根本不发 AppImage，它按 Ubuntu 版本各打一个 `.deb`（bionic/focal/jammy/noble…），glibc 直接继承容器（E-外部）。它最老的 bionic 产物也要求 glibc ≥ 2.27，而 CentOS 7 是 2.17。**私有运行时（自带 loader + 新 glibc + 单文件 AppImage）两条路线都得自己造**，这部分成本与 Q/L 无关。
4. **真正拉开差距的是渲染栈**：floem 用 wgpu，代码里 `force_fallback_adapter: false`、没有软件回退路径，老 GPU 直接 `AdapterNotFoundError`（E-外部）；而 Q 的编辑器面是 Qt Widgets，默认 CPU 光栅绘制。**在老 Mesa 的 CentOS 7 上，Q 是"大概率能跑"，L 是"需要先证明能跑"**。
5. **Q 能留住的现有 Rust 代码更多**：Q 保留 vd-config/vd-lsp/vd-deeplint/vd-fim 共 8,669 行（51.7% src）；L 只能直接留 vd-deeplint/vd-fim 加部分 vd-config ≈ 6,920 行（41%），因为 Lapce 自带 LSP 客户端，vd-lsp 整体冗余（E-仓库）。

**推荐：维持 Q。置信度 70%（中高）。** 若把"用户明确指定 Qt6/KTextEditor/KSyntaxHighlighting"这条要求从矩阵里拿掉，Q 仍是 6.57 vs L 5.57——只是优势从压倒性变成微弱。真正决定 L 能不能翻盘的是 E2（X11 中文 IME 实机）与 E8（wgpu 在老 Mesa 上能否拿到适配器）两个 spike，不是功能清单。

## 1. 证据分级与取证边界

| 标记 | 含义 |
|---|---|
| **E-仓库** | 能在本仓库直接读到（路径:行） |
| **E-外部** | 研究席位实际读到的上游源码内容（附文件路径 + 行号） |
| **U** | 取不到；写"未核实"，并给"若为真/若为假"的双向影响 |

**本轮取证能力**（必须写清楚，否则下面的空白会被误读）：
- 研究席位**没有 HTTP 抓取**，只能用 GitHub 代码检索（grep_app）与搜索标题。KDE 的 invent.kde.org 与 `KDE/kate` 仓库**不在索引里**，因此 E5（KTextEditor 的 LSP 能力）彻底取不到。
- 因此"Lapce 有某个功能"若来自上一轮的功能清单，那是上一轮 Researcher 读 `lapce/lapce` master（`Cargo.toml` version 0.4.6）得到的，标 **E-外部（上一轮）**；本轮新增的结论才标本轮行号。
- **没有任何一条结论来自我的记忆**。下面凡写"未核实"的，就是真的没取到。

## 2. 逐条证据（E1–E8）

### E1 Lapce 本体与 floem 的许可 —— E-外部

**结论**：Lapce 主仓 `License: Apache-2.0`，floem `MIT`。fork + 修改 + 分发合法。
**证据**：`lapce/lapce/LICENSE:1`；`Cargo.toml:24-30` `[workspace.package] license = "Apache-2.0"`；`LICENSE:89-96` 为 Apache §4(a)(b)。`lapce/floem/LICENSE:1-5` "MIT License / Copyright (c) 2023 Floem"；`Cargo.toml:24-26`。
**义务**：Apache 4(a) 给接收者一份许可证副本；4(b) 标注被修改的文件；MIT 保留版权与许可声明。**没有**源码开放义务，也没有 GPL 传染。
**对路线的影响**：L 的"本体许可"不是障碍。注意：**真正会传染的是第三方 grammar 与 KDE 数据文件，不是 Lapce 本体**（见 U1、C7）。

### E2 floem 在 Linux 的中文输入法 —— E-外部（管道已验证）/ U（实机投递）

**结论**：preedit（组合）与 commit（上屏）在代码里是**实现了的**，不是缺失项；输入确实走 winit 的 IME 事件。但**X11/Wayland 具体后端的实机表现未验证**，CentOS 7 上更是未验证。
**证据**：floem `src/app/handle.rs:365-368` `WindowEvent::Ime(ime) => window_handle.ime(ime)`；`src/event/listener.rs:44-52`（ImeCommit / ImeDeleteSurrounding / ImeDisabled / ImeEnabled / ImePreedit）；`src/event/mod.rs:2049-2054`（文档里明确提到拼音候选框与 `set_ime_cursor_area`）；`src/window/handle.rs:1243` 应用 `ime_capabilities()`；Lapce 侧 `lapce-app/src/editor/view.rs:260` 把 `ImePreedit` 接到 `set_preedit(text, cursor, offset)`，`src/text_input.rs:266`。
**对路线的影响**：这条**削弱**了"L 不能写中文注释"的直觉结论——代码路径是通的。但它是 L 的**单点风险**：若在 CentOS 7 的 X11 + fcitx5/ibus 下 preedit 不投递，L 对"Verilog 注释全中文"这个核心场景就是不可用。闭合方式：在目标机器上花 30 分钟实测（见 U4）。

### E3 Lapce 官方 Linux 产物的构建环境与 glibc —— E-外部

**结论**：**Lapce 没有 AppImage**。它按 Ubuntu 代号各打一个 `.deb`（bionic / focal / jammy / noble / oracular / plucky）+ 一个 `.tar.gz`；glibc 来自构建容器，**不随包**；依赖由 `dpkg-shlibdeps` 自动推导。
**证据**：`.github/workflows/release.yml:107-118`（linux job，`runs-on: ubuntu-latest`，`docker buildx bake --pull ubuntu-focal-binary`，产出 `lapce-linux-*.tar.gz`）；`:147+` deb 矩阵；`docker-bake.hcl:128-142`；`extra/linux/docker/ubuntu/Dockerfile:194-201` `dpkg-shlibdeps -O -e usr/bin/lapce` → `dpkg-deb --build`；`Dockerfile:120-126` `xx-cargo build --package lapce-app --profile release-lto`；`CHANGELOG.md:35` "Remove Ubuntu Focal (20.04)"（与 bake 文件里仍构建 focal 存在冲突，说明我读到的是 master，可能晚于实际发布 tag）。
**对路线的影响**：**"借鉴 Lapce 的打包方案"这条捷径不存在**。最老代号 bionic 的 glibc 是 2.27，仍高于 CentOS 7 的 2.17；且没有自带 glibc 的机制。→ **C1/C2 的私有运行时是两条路线共同的前置成本**，不是 Q 独有。

### E4 Lapce 的 tree-sitter grammar —— E-外部（机制）/ U（Verilog 与许可）

**结论**：**grammar 不随二进制发布**。Lapce 运行时从 `lapce/tree-sitter-grammars` 的 release 下载，落到 `<data_local>/grammars`，再以 `tree_sitter_<name>` dlopen。语言表里有 `Verilog` 这个 id，但**那个 release 里到底有没有 verilog/systemverilog 产物、许可是什么，未取到**。
**证据**：`lapce-core/src/language.rs:428` `#[strum(message = "Verilog")] Verilog`、`:1625` 该语言的 SyntaxProperties、`:115-120` grammar/grammar_fn/query、`:1904` `format!("tree_sitter_{}", grammar_fn.replace('-', "_"))`；`lapce-app/src/app/grammars.rs:19-30` 拉 `api.github.com/repos/lapce/tree-sitter-grammars/releases`、`:61` `fetch_grammars`；`lapce-core/src/directory.rs:176` grammars 目录。
**对路线的影响**：
- 对 L：**离线场景是问题**——默认安装若没网就没有 grammar，需要我们自己把 grammar 预置进包内（技术上可做，但要改它的下载路径逻辑）。
- 对 Q：同一件事反而更简单——我们把 grammar 直接编进/放进私有运行时，符合 C18。
- **grammar 的许可对两条路线都是同一颗雷**（见 U1）。

### E5 KTextEditor 是否提供可复用的 LSP 客户端 —— **U（完全取不到）**

**结论**：取不到。唯一线索是搜索结果里一个**没打开**的路径：`invent.kde.org/utilities/kate/-/raw/<sha>/addons/lspclient/lspclientplugin.h` —— 文件名暗示它是 **Kate 的 addon/插件**，而不是 KTextEditor 的公开库 API。但我没有读到 CMakeLists 或头文件安装列表，**不能当证据**。
**证据**：`KDE/kate` 在 grep_app 无索引，5 次不同问法均 0 结果。
**对路线的影响（双向）**：
- 若 Kate 的 LSP 客户端是插件内部实现（**我判断可能性 >80%**）：Q 必须自己提供 LSP 客户端。**但这不是新增成本**——仓库现有 `vd-lsp`（1,749 行，GTK-free，基于 async-lsp）本来就是完整的 LSP 客户端，通过 C ABI 复用即可；这恰好是 rev 2 "Rust 核心保留"的价值所在。
- 若它其实是可链接的公共库：Q 的工作量会显著下降，矩阵里 Q 的"功能对齐速度"可以从 4 分升到 6 分，但不改变推荐方向。
**闭合方式**：读 `ktexteditor/CMakeLists.txt` 的 `install(FILES ...)` 与 `addons/lspclient/` 的 CMake 目标类型（需要能抓网页的席位）。

### E6 原生文件对话框 / 可访问性 / 大文件性能 —— E-外部（PARTIAL）

**结论**：
- 对话框：floem 自己没有实现，直接调 `rfd`；rfd 在 Linux 默认走 **XDG Desktop Portal**（运行时是 GTK 或 KDE 的对话框），zenity 兜底。
- 可访问性：floem 与 lapce 里**没有 accesskit** 的任何匹配 → 无辅助功能支持。
- 大文件性能：没有找到任何文档或 issue（检索工具只索引代码，不索引 issue）。
**证据**：floem `src/platform/file_action.rs:26-30` `rfd::FileDialog::new()`；`Cargo.toml:79` `rfd 0.17.1`；lapce `Cargo.toml:78-81` feature `rfd-async-std`；`PolyMeilex/rfd src/lib.rs:40-58`（默认 xdg-portal，zenity 兜底）；`Cargo.toml:9-16`。
**对路线的影响**：
- **CentOS 7 上这是个新风险**：xdg-desktop-portal 在 CentOS 7 上通常没有；退到 zenity 也要用户机器上装了 zenity。→ L 的"打开文件"对话框在老系统上可能直接不可用，需要额外兜底。
- **可访问性**：L 无、Q 有（Qt6 自带可访问性桥）。若用户单位有无障碍要求，这条是硬差距；目前用户没提，权重低。
- 大文件：两条路线都要自己压测（Q 的 A1a 已写）。

### E7 插件 / DAP / 远程开发：能不能不背 —— E-外部（位置）+ PARTIAL（能否编译掉）

**结论**：三者都在 `lapce-proxy` 这个**独立二进制**里（WASI 宿主用 wasmtime、`DapClient`、远程=ssh + 起 `--proxy`）。但根 crate **无条件依赖 lapce-proxy**，feature 只有 updater/vendored-fonts/portable，**没有受支持的编译裁剪开关**。技术上可以改，但那等于自己维护一份 Lapce 分支的构建系统。
**证据**：`lapce-proxy/src/plugin/mod.rs:1-4`（`pub mod catalog; dap; lsp; psp; wasi;`）；`wasi.rs:27-31`、`:422` `wasmtime::Engine::default()` + `Module::from_file`；`lapce-proxy/Cargo.toml:54-56` wasmtime 14.0.0；`dap.rs:36` `pub struct DapClient`；`lapce-app/src/proxy/remote.rs:78-85,149`；`lapce-app/Cargo.toml:73-77`；根 `Cargo.toml:10-13`。
**对路线的影响**：用户**没勾选**插件/DAP/远程，但在 L 下它们是**编译进来且要跟着升级的**。这不阻塞 v1，但会持续产生维护面与依赖面（wasmtime 是大依赖，也是攻击面）。Q 侧则根本不存在这些代码。

### E8 两方案在 CentOS 7 + 私有运行时下的实际差异 —— E-外部（选择逻辑）/ U（老硬件结果）

**结论**：floem 的 wgpu 初始化是 `WGPU_BACKEND` 环境变量 → 否则 `Backends::all()` → `request_adapter(..., force_fallback_adapter: false)`；**代码里没有软件渲染路径**，老 GPU 直接 `AdapterNotFoundError`。Lapce 自己不加任何覆盖。
**证据**：floem `renderer/src/gpu_resources.rs:55-58` `Backends::from_env().or(backends).unwrap_or(Backends::all())`；`:76-82` `force_fallback_adapter: false` → `GpuResourceError::AdapterNotFoundError`；lapce 全仓无 `Backends::` / `PowerPreference` 匹配。
**对路线的影响**：
- **L**：CentOS 7 的 Mesa 很老，Vulkan 大概率没有；只能寄望 wgpu 的 GL/EGL 后端能枚举到一个可用适配器（llvmpipe 或老 i965）。这需要实机验证（U3）。若不行，要改 floem 的适配器请求逻辑——**等于连 UI 框架一起 fork**。
- **Q**：Qt Widgets 默认走 CPU 光栅绘制，主编辑界面不强制 OpenGL（这是 Qt 的既有设计，本轮未取源码，M0a 实测确认）；即便要 GPU 效果，也有 `QT_QUICK_BACKEND=software` / `QSG_RHI_BACKEND=software` 这类成熟回退。→ **在老系统上，Q 的渲染风险显著低于 L。**
- 两者共同的：私有 loader + glibc 的 AppRun 改造、libfuse2 前提、X11 依赖。这些**完全相同**。

## 3. 决策矩阵

权重是我的建议，供用户改；分数 0–10，越高越好。

| 维度 | 权重 | Q（Qt6+KTextEditor+KSyntaxHighlighting） | L（fork Lapce） | 依据 |
|---|---|---|---|---|
| 满足用户原始三库要求 | 20% | **10** | **0** | 用户原话点名 Qt6/KTextEditor/KSyntaxHighlighting；L 三者全无 |
| CentOS 7.9 + 私有运行时可行性 | 20% | **7** | **4** | E3（两者都要自造私有运行时）、E8（wgpu 无软件回退 vs Qt Widgets 光栅）、E6（rfd 依赖 portal/zenity） |
| 中文 IME / CJK | 15% | **9** | **6** | E2：管道已验证、实机未验证；Qt6 IME 在 X11 上成熟 |
| 功能对齐速度（G4 的 10 组） | 15% | **4** | **9** | E-外部（上一轮）：Lapce 自带文件树/命令面板/分屏/终端/Git 面板/模态 Vim |
| 许可风险 | 10% | **5** | **7** | E1（L 本体干净）；两条路线都要面对 U1 grammar 与 C7 的 GPLv3+ 数据文件 |
| 现有 Rust 资产复用 | 10% | **8** | **4** | §5：Q 留 8,669 行，L 留 ≈6,920 行且 vd-lsp 冗余 |
| 长期维护 / 上游升级 | 5% | **6** | **4** | L 需跟 Lapce 快速演进的分支，且改过构建系统；Q 升级 KDE/Qt 相对可预期 |
| 无障碍 / 生态成熟度 | 5% | **7** | **5** | E6：floem 无 accesskit；Qt 有内置可访问性 |
| **加权总分** | 100% | **7.30** | **4.60** | |

**敏感性**（这条很重要，别只看总分）：
- 若用户放弃"必须 Qt6/KTextEditor/KSyntaxHighlighting"这一条（把第一行权重清零、其余按比例放大），Q ≈ **6.57**、L ≈ **5.57**。Q 仍领先，但领先幅度从 2.7 分掉到 1.0 分——**"用户自己的指定"贡献了大约 2/3 的差距**。
- 若 U3 为真（wgpu 在老 Mesa 能跑，L 的 CentOS 风险降到 7 分）且 U4 为真（X11 中文 IME 实机可用，L 的 IME 升到 8 分），L 的总分升到约 **6.5**，Q 仍是 7.3——**依然推荐 Q，但差距只剩一个身位**。到那时真正该争的是"功能对齐速度值不值 2 分"。
- 若 U1 为真（Verilog grammar 是 GPL-3.0），**两条路线同等受伤**，不改变 Q/L 相对排名。

## 4. 推荐与置信度

**推荐：维持 Q（当前 rev 2 路线）。**

理由按权重排序：
1. 用户自己点名了三件东西，L 三件全不要——这不是技术判断，是需求事实。
2. CentOS 7 + 老 Mesa 上，Q 的渲染路径（Qt Widgets 光栅）比 L 的 wgpu（无软件回退）确定性高一个量级。
3. Q 保留更多现有 Rust 资产（51.7% vs 41%），且 `vd-lsp` 在 Q 下是核心资产、在 L 下是废件。
4. L 省下的（文件树/命令面板/分屏/终端/Git 面板）确实可观，但**换来的是**：放弃 KTextEditor 的编辑内核、自己承担 wgpu/portal/IME 三个老系统风险、以及跟着 Lapce 分支维护。

**置信度：70%（中高）**。拆开说：
- "Q 比 L 更适合这个交付约束"——**80%**。
- "Q 路线本身能做成"——**65%**，被三个未验证项卡着（U1 grammar 许可、KTextEditor overlay 接口、折叠模型能否被外部 range 驱动）。这三个都是可以在 1–2 周内用 spike 结清的，不是结构性风险。
- 若用户明天改主意选 L，我不会认为那是错的——只是要把 E2/E3/E8 三个 spike 放在第一周，并且接受"放弃 KTextEditor 与 KSyntaxHighlighting"这一条需求被删除。

## 5. 如果用户明天说「就 fork Lapce」：19k 行 Rust 的去留

先给总量（E-仓库，来自本仓库清点：65 个 `.rs`，src 16,775 行，tests+examples 2,440 行）：

| crate | src 行数 | 在 L 下的命运 | 说明 |
|---|---:|---|---|
| `vd-config` | 2,911 | **大部分可搬** | 配置模型/provenance/warnings 是纯 Rust、无 GTK。但 Lapce 自带 TOML 配置系统，实际是"把我们的字段映射进它的 schema"，能搬的是解析、校验、诊断这些逻辑 |
| `vd-fim` | 2,189 | **可直接搬** | FIM 补全走 HTTP（ureq + rustls），与 UI 无关；需接到 Lapce 的 inline completion 上 |
| `vd-deeplint` | 1,820 | **可直接搬** | 起 SLANG 子进程、解析诊断，纯后端；与 UI 无关 |
| `vd-lsp` | 1,749 | **基本作废（知识可留）** | Lapce 的 `lapce-proxy` 自带完整 LSP 客户端；再塞一个客户端是架构打架。能留的是"Verible 启动参数/位置映射/诊断归一"这些经验 |
| `vd-app` | 5,032 | **丢弃** | GTK3 界面（`app.rs` 1,941 行）；其中 `commands.rs` 的快捷键表、`cli.rs`+`selftest.rs`（1,257 行）的**行为断言**有价值，但代码不可直接复用 |
| `vd-editor` | 2,100 | **丢弃** | 文本存储就是 `sourceview4::Buffer`，undo 依赖 GTK 原生 undo manager；L 用 `lapce-xi-rope` |
| `vd-syntax` | 974 | **丢弃**（但 `detect.rs` 的路径→语言映射可留几十行） | GtkSourceView 的 language spec 驱动；L 用 tree-sitter |
| **合计** | **16,775** | **可直接搬 ≈ 6,920（41%）；转译/借鉴 ≈ 1,749；丢弃 ≈ 8,106（48%）** | 另有 tests+examples 2,440 行随各自 crate 走 |

**对比 Q（rev 2，Rust 核心保留）**：保留 `vd-config`+`vd-lsp`+`vd-deeplint`+`vd-fim` = **8,669 行（51.7%）**；丢弃 `vd-app`+`vd-editor`+`vd-syntax` = 8,106 行，但那部分本来就要用 C++/Qt 重写（KTextEditor 接管文本与撤销，Qt 接管界面）。

**代码之外，两条路线都能留的**（E-仓库）：
- `scripts/build.sh`、`scripts/versions.env`、`build/gates/*.sh`（11 个门禁）、`docs/BUILD.md`——私有运行时与双宿主验收都要用，**不随路线变**。
- `assets/language-specs/{verilog,systemverilog}.lang`（LGPL-2.1+）：在 L 下**没用**（tree-sitter 不吃 GtkSourceView 格式）；在 Q 下可作为"自写 KSyntaxHighlighting 定义"的移植来源（对应 C7 的处置选项）。
- `docs/USER_GUIDE.md`、`README.md`、`THIRD_PARTY_NOTICES.md`、`AGENT.md`：文档资产，改内容不改结构。
- `evidence/`：历史验收证据，保留。

**一句话成本**：fork Lapce 省掉的是"IDE 外壳"（大约相当于 vd-app 那 5,032 行要做的事，而且它做得比我们从零做更全），付出的是"编辑内核换人 + 三个老系统 spike + 丢掉 1,749 行 LSP 客户端 + 永久跟分支"。

## 6. 未核实项（U）与双向影响

| 编号 | 未核实的事 | 若为真会怎样 | 若为假会怎样 |
|---|---|---|---|
| **U1** | Lapce 用的 Verilog/SystemVerilog tree-sitter grammar 的身份、commit、许可 | 若是 MIT/Apache：两条路线都干净，Q 的 C15 接缝可以直接用现成 grammar | 若是 GPL-3.0：**整个 AppImage 需按 GPL 分发**；必须换 MIT grammar 或自写，C15 的可替换接缝从"保险"变成"必需" |
| **U2** | KTextEditor 是否暴露公共 LSP 客户端 API（Kate 的 lspclient 是插件还是库） | 若是公共库：Q 可以少写一层 LSP 适配，工作量下降 | 若是插件内部（**我判断概率 >80%**）：Q 必须自己提供 LSP 客户端——但仓库已有 `vd-lsp`（1,749 行）可直接复用，成本已被现有资产吸收 |
| **U3** | wgpu 在 CentOS 7 的老 Mesa 上能否枚举到可用适配器（GL/EGL 或 llvmpipe） | 若能：L 的渲染风险降到与 Q 接近，"L 不能跑老系统"的论断不成立 | 若不能（`AdapterNotFoundError` 且无软件路径）：L **无法满足 C1**，除非连 floem 一起 fork 去加软件回退 |
| **U4** | floem 在 CentOS 7 X11 + fcitx5/ibus 下 preedit 能否真正投递 | 若能：L 最大的单点风险消失，中文注释场景可用 | 若不能：L 对"Verilog 注释全中文"这个核心场景不可用——这是否决级 |
| **U5** | Lapce 实际发布的 glibc 基线（bionic 是否仍在发；master 与发布 tag 有冲突） | 若仍在发 bionic：参考价值≈0，因为它要求 2.27 仍高于 2.17 | 若连 bionic 都停了：更印证"L 不解决 CentOS 7"——结论方向不变，只是更确定 |

**这五条里，U1 影响两条路线；U2 只影响 Q（且有现成缓解）；U3/U4 只影响 L，且都是否决级。** 这就是为什么"先做 spike 再决定"的顺序应该是 U3/U4 优先于任何功能开发——如果用户真要走 L。

## 7. 若路线改判 L：rev 3 需要改的需求条款

（本文件不改需求；这里只列清单，供 captain 判断。）

- **删除**：G1（Qt6 图形内核）、G2（KTextEditor 编辑内核）、G3（KSyntaxHighlighting 底座）、G3a（tree-sitter 叠加——在 L 下 tree-sitter 是主力而非叠加）
- **改写**：G4（Lapce 对齐 → 变成"修改 Lapce"）；G7（交付物仍为单文件 AppImage，但**没有任何可借鉴的上游方案**，成本上调）
- **作废**：C5（Qt/KF6 版本耦合）、C6（syntax 数据路径）、C7（KDE `.sv` 许可——不再使用 KDE 数据）、C12/C13/C15/C16/C17（均围绕 KTextEditor/KSyntaxHighlighting overlay）
- **保留**：C1/C2/C3/C4（目标系统与私有运行时）、C8/C9（工程模型与 `.f`）、C10（Git+SVN）、C11（工具链刷新）、C14
- **新增**：floem/wgpu 的软件回退要求、IME 实机验收、grammar 预置进包（不允许运行时下载）、rfd/portal 兜底
- **验收增删**：A0a/A0b 不变（仍是硬闸门）；A1f/A1g/A1h（tree-sitter overlay 相关系列）删除，替换为"floem 渲染在目标机上可用"与"中文 IME 实机通过"；A1c 折叠/面包屑沿用

## 8. 本轮明确没能回答的

1. Kate/KTextEditor 的 LSP 是不是公共库（E5）——KDE 仓库不在检索索引内，需要能抓网页的席位。
2. Lapce release 里到底有没有 verilog grammar、许可是什么（E4）——需要 GitHub releases 页面或 grammar 仓库 LICENSE 原文。
3. 老硬件上 wgpu 的真实结果（E8）与 X11 中文 IME 的真实投递（E2）——这两条**只能在目标机器上跑**，任何二手证据都不能替代。
4. Lapce 大文件性能、issue 里的已知坑（E6）——检索工具只索引代码，不索引 issue/文档站。
