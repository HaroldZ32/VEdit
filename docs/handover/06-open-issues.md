# 06 · 欠账清单与待拍板事项

> 排序原则：**先修"产品没接上自己的库"，再补评审，最后才做新功能。**
> 当前最贵的不是功能少，是构建链与产品之间断着。

## A. 装配缺口（G1–G10，详见 `03-architecture.md` §6）

| # | 缺口 | 严重度 | 修它要做什么 |
| --- | --- | --- | --- |
| G1 | `src/editor` 只有探针，产品用 `DemoEditorProvider`（QPlainTextEdit） | **高** | 实现编辑内核 + 在 `main.cpp` 注入 `setEditorProvider` |
| G2 | syntax 三个库编译了但**没链接进 `vedit-app`**（`readelf -d` 无 `KF6TextEditor` / `libtree-sitter`） | **高** | `src/app/CMakeLists.txt` 加链接 + 启动时激活 grammar |
| G3 | 根 configure 的 `VEDIT_SYNTAX_GRAMMAR=none`，`SyntaxService` 默认 inactive | **高** | 改配置为 `verilog`，或显式 `activate("verilog")` |
| G4 | `src/syntax/overlay` **目录不存在** | 中 | 按 C17 探针结论实现；C16 仲裁已在库里有单测 |
| G5 | 6 个 dock 里只有 file-tree / search / terminal 有工厂，vcs / settings / outline 是占位 | 中 | 三条 lane 落地后各加一行 `setFactory` |
| G6 | app-shell 的 selfcheck / 截图在 `installPanels`/factory **之前**就返回，测不到 lane 接线 | 中 | 调整自检时序，让截图发生在面板安装之后 |
| G7 | 打包链装的是 M0 冒烟程序，不是 `vedit-app`（`AppRun` 找 `bin/vd-app`，从未安装过） | **高** | `assemble-appdir.sh:62` 换输入 |
| G8 | filetree 测试在根构建里默认关闭（`VEDIT_BUILD_FILETREE_TESTS=OFF`）；palette 的 bench 不是测试 | 中 | 打开开关；bench 纳入 ctest 或明确标成非测试 |
| G9 | grammars / terminal 的接线**未提交**（HEAD 的 `main.cpp` 没有 terminal factory） | **高** | 提交；否则一次干净 checkout 就退回假绿 |
| G10 | 门禁仍以旧 Rust 产品为主（cargo/slang/verible/gtk；tests ledger 不可达；comments/rows 永远 RED） | 中 | 为 Qt6 产品建自己的门禁 |

## B. 欠账的独立评审（5 条）

这五条都有自测、都**没有 verdict**：

| 交付 | 现有证据 | 缺什么 |
| --- | --- | --- |
| T17 grammar 绑定 + 三组 query | 三配置各 79/79，换装证明（源码 manifest 相同） | 独立评审：许可执行侧、folds.scm 自写是否合规、query 覆盖 |
| T21 文件树 | `evidence/ws-filetree/REPORT.md` | 独立评审 + **dock 占位控件是否真被替换**至今没有明确 verdict |
| T22 命令面板 | `evidence/ws-palette/`，10k 文件基准 | 独立评审：200ms 判据在什么机器上成立 |
| T23 全局搜索 | `evidence/ws-search/REPORT.md` | 独立评审 |
| T35 终端 | 101 项 CHECK + 截图 | 独立评审：VT 覆盖面、退出码/信号、无网络（已自证） |

> 唯一已经有独立评审的是本轮的 `evidence/build-registration/`（Reviewer → PASS）。

## C. 证据债

| # | 债 | 处置建议 |
| --- | --- | --- |
| E1 | `evidence/m0-build/build-logs/*` 被最近一次绿构建重写（6 个文件） | **一并提交**（它们是当前树状态的原始日志），并在文件头补采集时间与树状态 |
| E2 | `evidence/app-shell/08-structure.txt`、`03-elf.txt` 曾比代码老 | 重采；规则写进 05 文档 §2.2 |
| E3 | `cmake/smoke/34-qt6-elf.txt`、`32-qt6-build.log` 是红着结束的，曾被误当编译证据 | 要么重采成绿，要么在文件头标 `INVALIDATED — red run` |
| E4 | `evidence/fim/real-endpoint/harness/target` 已删（117 M），只剩结论 | 结论够用；若要复跑，按 harness/README 重建 |
| E5 | C17 复审（T65）开出去之后**从未返回** | 直接重开一个新的复审，不要等旧的 |

## D. 需要用户拍板

| # | 问题 | 为什么要问 | 我的建议 |
| --- | --- | --- | --- |
| Q1 | `feat/qt6-ktexteditor-migration` 要不要 push 到 origin？ | 目前 7 个提交只在本地；用户明确说过"push个啥" | **不推**，等下一轮 cellwork 重新组织后再说 |
| Q2 | 旧的 Rust/GTK 产品（`crates/`，7 个 crate、65 个 `.rs`）去留？ | 它占着 `Cargo.toml` workspace、门禁、打包链的一大半 | 先留着（有已发布版本），但**门禁要拆开**，别让旧产品的红绿冒充新产品的红绿 |
| Q3 | `src/editor` 走哪条路？ | 现在 0/6 实现，只有探针。探针已把三条最大不确定性打掉 | 按探针结论继续做，路线备忘见 `evidence/edit-core/00-probe.md` §8 |
| Q4 | A1b「≥100 步撤销」的口径 | 已按"逻辑操作"定（多光标=1 条、全部替换=1 条、100 个逻辑操作可逐步 undo/redo） | 已定，写进 `04-hard-facts.md` §4.2，无需再问 |
| Q5 | overlay 是否留在 v1？ | `src/syntax/overlay` 目录都不存在，C17 只到探针 | 建议**降级到 Slice 2/3**，先把"能编辑 + 有 KSH 高亮"做出来 |

## E. 明确的下一步顺序（给下一轮 cellwork 的输入）

1. 提交 G9 的接线（否则一切只存在于工作树）。
2. 修 G2/G3：syntax 三库链接进 `vedit-app` + 激活 grammar → 产品第一次真正有高亮。
3. 做 G1：编辑内核（`src/editor`），注入 `setEditorProvider`。
4. 补五份独立评审（T17/T21/T22/T23/T35）。
5. 修 G7：打包输入换成 `vedit-app`，重跑四宿主矩阵 —— 到这一步才谈得上"产品可以交付"。
6. G4/G5/G6/G8/G10 按需推进。
