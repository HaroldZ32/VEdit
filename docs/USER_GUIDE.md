# VEdit 用户指南

面向在 **CentOS 7.9 x86_64**（无 root、无网络）上写 Verilog / SystemVerilog 的工程师。
本文按"安装 → 日常编辑 → IDE 能力 → 可选项 → 排障"组织。

## 1. 安装（三种产物，选一种即可）

三种产物都**自带** GTK3 运行时、GtkSourceView 数据、verible、slang、字体与输入法模块，
不需要在目标机上安装任何软件包。

### 1.1 AppImage（最省事，需要 FUSE）

```sh
chmod +x VEdit-0.1.1-x86_64.AppImage
./VEdit-0.1.1-x86_64.AppImage
```

CentOS 7 常常没有 `fusermount3`，直接运行会以 `No suitable fusermount binary found` 退出 127。
两种兜底方式：

```sh
./VEdit-0.1.1-x86_64.AppImage --appimage-extract-and-run
APPIMAGE_EXTRACT_AND_RUN=1 ./VEdit-0.1.1-x86_64.AppImage
```

### 1.2 目录包 tar.gz（不需要 FUSE，最稳）

```sh
tar xzf vedit-0.1.1-linux-x86_64.tar.gz
./VEdit/vedit.sh                 # 空编辑器
./VEdit/vedit.sh path/to/top.sv  # 直接打开文件
```

包可以解压到任意位置（家目录、`/opt`、共享盘），启动脚本按**自身所在目录**解析所有路径。

### 1.3 单文件自解压 `.run`（不需要 FUSE，单文件）

```sh
sh VEdit-0.1.1-x86_64.run path/to/top.sv
# 或
chmod +x VEdit-0.1.1-x86_64.run && ./VEdit-0.1.1-x86_64.run
```

行为说明：

- 先尝试 `${TMPDIR:-/tmp}`，并**实际试执行**一个探针脚本；若该文件系统是 `noexec`，
  自动回退到 `${XDG_CACHE_HOME:-$HOME/.cache}/vedit/`，再试一次；
- 两者都不可执行时，以退出码 3 明确报错，并提示可用的替代方式（tar.gz / AppImage extract-and-run）；
- 退出时清理解包目录；调试时设 `VEDIT_KEEP_TMP=1` 可保留并打印路径；
- 参数原样传给编辑器（`--selftest all`、`--version` 等）。

## 2. 第一次使用

1. 启动后 `Ctrl+O` 打开一个 `.sv`/`.v` 文件，或直接把文件路径作为参数；
2. 底部状态栏依次显示：光标位置、文件路径、语言（`systemverilog`/`verilog`）、
   verible 诊断数、slang 诊断数、FIM 状态、LSP 进程状态；
3. 打开工程（Qt6）：启动时用 `--project <dir>` 指定工程目录（或恢复上次会话）。工程根
   **从该目录向上逐级查找**：在每一级依次看 `vedit.toml`（优先）→ `.git` → 该目录是否
   **直接包含 `*.f`**，命中的第一级即工程根（来源依次报告为 `vedit_toml` / `git_dir` /
   `filelist`，都没命中的退化为起始目录 `fallback`）。
   所以一个只带 `project.f` 的目录本身也算工程根，即使它位于更大的 git 仓库内部：以该目录为
   起点时根就是它自己（`filelist`），而以它所在的外层仓库为起点时根仍是外层（`git_dir`），
   嵌套的 `.f` 不会把根改到子目录；同一目录同时有 `.git` 与 `*.f` 时根不变，来源按更强的
   `.git` 报 `git_dir`。扩展名大小写不敏感：`.F` 与 `.f` 同样算根
   （`has_f_extension` 用 `eq_ignore_ascii_case("f")`）。在此之上的 `.rules.verible_lint` 与
   `*.f` filelist 会被自动发现；
4. 语法高亮是自动的（按扩展名），主题默认 `cobalt`，可在 `[ui] theme` 改。

工程根的**起点**在两个前端不同：旧 GTK3 前端（`crates/vd-app`）以**进程启动目录**
（`std::env::current_dir()`）为起点；Qt6 产品的工程根由 **`--project <dir>`**
（`src/app/main.cpp:804/824`）或**恢复的会话**（`src/app/app_shell.cpp:390`）指定，
**不是由打开文件推导**：`--project` 写入 `ShellOptions.projectRoot`
（`src/workspace/integration.h:26`），`src/workspace/project/project_integration.cpp:56,69`
直接 `model->open(root)`，全仓没有 file-open → project-root 的连线。
`crates/vd-config` 的 `Loader::new(start_dir)` 契约规定**调用方应传被打开文件所在目录**
（`crates/vd-config/src/lib.rs:93-103`），而 Qt6 当前 shell 传的是 `--project` 的值；
GTK3 前端当前传的是进程启动目录（`crates/vd-app/src/config_glue.rs:63`，与契约不一致）。
因此 Qt6 里用 `--project` 指定工程目录（根发现从该目录向上）；旧 GTK3 前端要先 `cd` 到期望的
工程目录再启动。

编辑能力：多标签（`Ctrl+N` 新建、`Ctrl+W` 关闭、未保存会确认）、原生撤销/重做、
查找/替换/下一个（`Ctrl+F`/`Ctrl+H`/`F3`）、跳行（`Ctrl+G`）、CRLF 保留、原子保存
（写临时文件再 rename，避免半截文件）。

## 3. Verible 在线能力（默认快捷键见下表）

打开工程后，`verible-verilog-ls` 由编辑器自动拉起（状态栏显示 `LSP running`）：

| 能力 | 操作 | 说明 |
|---|---|---|
| 实时诊断 | 自动 | 问题面板 + 行号栏标记（error/warning/note），点击行跳到对应位置 |
| 一键格式化 | `Ctrl+Shift+F` | 通过 LSP 的 documentFormatting，**一步撤销**即可回退 |
| 跳转定义 | `F12` | 跨文件 |
| 查找引用 | `Shift+F12` | 结果列表 |
| 符号大纲 | `Ctrl+Shift+O` | 当前文件的符号 |
| 重命名预览 | `F2` | **只预览** LSP 给出的编辑，不写文件 |

LSP 异常退出时会提示并可重启后端；LSP 请求不阻塞 UI。

## 4. SLANG 深度 lint

- 默认 `Ctrl+Shift+L`；执行 `slang --lint-only -Weverything --diag-json <file>`；
- 结果显示在问题面板（severity / message / code / 文件:行:列，行列按 1 基显示）；
- 触发期间 UI 可交互；重复触发会取消上一次的结果；
- slang 缺失/损坏/返回非法 JSON/非 0 退出码时：状态栏给错误提示，编辑器继续可用、不崩溃；
- `slang` 的路径解析顺序：`VEDIT_SLANG` → 包内相对路径（`bin/slang`、`../lib/vedit/bin/slang`…），
  **不查 PATH**。

## 5. 可选 FIM 补全

**默认关闭**：未配置时不会发出任何网络请求、不弹错误框、不拖慢启动；编辑/保存/跳转全链路正常。

配置（用户配置 `~/.config/vedit/vedit.toml` 或工程 `vedit.toml`）：

```toml
[fim]
enabled = true
protocol = "open_ai"     # OpenAI 兼容 /v1/completions（prompt + suffix）
                         # 或 "llama_cpp"（/infill，input_prefix + input_suffix）
endpoint = "http://127.0.0.1:8080"
model = "qwen2.5-coder"  # llama_cpp 不需要
timeout_ms = 5000
# api_key 可写在这里，或用环境变量（推荐后者）
```

等价的环变量：`VEDIT_FIM_ENABLED`、`VEDIT_FIM_PROTOCOL`、`VEDIT_FIM_ENDPOINT`、
`VEDIT_FIM_MODEL`、`VEDIT_FIM_TIMEOUT_MS`、`VEDIT_FIM_API_KEY`（环境变量优先级最高）。

使用：`Alt+\` 请求补全 → ghost text 显示 → `Tab` 接受 / `Esc` 取消 / 继续输入自动作废。
失败（超时、401/500、连不上）只在**状态栏**显示一句短提示（如 `FIM HTTP 401`），不阻塞编辑。

### 安全边界（请务必读）

- **密钥只从配置文件或环境变量读取**，没有内置默认端点/模型/密钥；
- 密钥在类型层面被 `ApiKey` 包裹：`Debug`/`Serialize` 恒为 `<redacted>`，**没有 `Display`**，
  明文只能在发请求的一处读出；日志、错误、状态栏都不含密钥；
- 请求错误只暴露状态码/超时值/固定类别，**不含请求头、key、带 query 的完整 URL**；
- 响应体积上限 64 KiB，超出即失败；请求由 `timeout_ms` 兜底；
- **取消是结果级**：网络 I/O 不会中途 abort，迟到的响应会被丢弃（陈旧结果永远不会覆盖新结果）；
- 默认关闭时**连传输层都不构造、线程也不创建**，这是一条结构性保证（有自动化测试与证据）。

## 6. 中文输入与中文显示

- **显示**：包内捆绑 WenQuanYi Zen Hei（`share/fonts/wqy-zenhei/`），中文注释不会显示成方块；
  emoji 不捆绑（体积/许可）。
- **输入**：包内捆绑 GTK3 输入法模块 `im-xim.so`（`gtk3-immodule-xim`）与
  `im-ibus.so`（`ibus-gtk3`），启动脚本设置 `GTK_IM_MODULE_FILE`。
  **系统输入法本身（ibus / fcitx）需要目标机自行安装并运行**——这是外部依赖，不是 VEdit 提供的。
  在 XIM 场景可先试 `GTK_IM_MODULE=xim ./VEdit/vedit.sh`。

## 7. 缩略配置示例

```toml
[ui]
language = "zh-CN"
theme = "cobalt"

[editor]
tab_width = 4
insert_spaces = true
show_line_numbers = true
highlight_current_line = false

[project]
filelists = ["rtl/files.f"]
include_dirs = ["rtl/include"]
defines = ["SYNTHESIS"]

[keys]
slang = "Ctrl+Alt+L"
accept-fim = "Tab"
```

配置的完整字段与优先级见仓库 `README.md` 的"配置"一节。

## 8. 排障

| 现象 | 原因 / 处理 |
|---|---|
| AppImage 报 `No suitable fusermount binary found`（exit 127） | 没有 FUSE：用 `--appimage-extract-and-run`，或改用 `.run` / tar.gz |
| `.run` 报 "no extraction directory allows executing files" | `/tmp` 与 `$HOME/.cache` 都是 `noexec`：设 `TMPDIR` 指向可执行目录，或改用 tar.gz |
| 打开文件但**没有语法高亮** | GtkSourceView 数据目录不在搜索路径。用包内启动方式运行；tar.gz / `.run` 可用 `VEDIT_PRINT_ENV=1 ./VEdit/vedit.sh` 打印启动环境自查（AppImage 的 AppRun 无此开关） |
| 状态栏提示 verible/slang 找不到 | 用包内启动脚本；或设 `VEDIT_VERIBLE_DIR` / `VEDIT_SLANG` 指向实际路径（**不会查 PATH**） |
| 状态栏一直 `FIM 未启用` | 这是默认行为；要启用见第 5 节 |
| 中文显示成方块 | 用了非包内启动方式导致字体目录未生效；用包内 `vedit.sh`/`.run`/AppImage |
| 输入法无候选框 | 系统未装/未运行 ibus/fcitx；VEdit 只提供 GTK 侧模块 |
| 启动日志 `Locale not supported by C library` | 目标机缺 `en_US.UTF-8` locale；不影响显示，可 `localedef` 生成 |
| 无头容器里 `--screenshot` 得到空白图 | 已知问题（无窗口管理器时 GTK 绘制时机）；用 X server 捕获代替 |
| 想确认安装完整性 | `sh VEdit-0.1.1-x86_64.run --selftest all`（退出码 0 即 6 套件 62 项全过） |

## 9. 文件与位置

| 内容 | 位置 |
|---|---|
| 用户配置 | `$XDG_CONFIG_HOME/vedit/vedit.toml`（默认 `~/.config/vedit/vedit.toml`） |
| 工程配置 | `<工程根>/vedit.toml`（工程根 = 从打开位置向上第一个命中 `vedit.toml` / `.git` / 直接含 `*.f` 的目录；同一级优先 `vedit.toml` > `.git` > `*.f`） |
| Verible lint 规则 | `<工程根>/.rules.verible_lint`（无则用 Verible 默认） |
| filelist | `<工程根>/*.f`，以及 `[project] filelists` 里列出的 |
| GtkSourceView 数据 | 包内 `share/gtksourceview-4/`（启动脚本导出 `VEDIT_GSV_DATADIR`） |
| 自检 fixtures | 包内 `share/vedit/testdata/`（`VEDIT_SELFTEST_FIXTURES`） |
