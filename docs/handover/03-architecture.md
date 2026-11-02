# VEdit 架构与装配交接（Qt6 / KTextEditor 迁移）

> **读者**：零上下文的接手人。本文只描述**当前工作树里实际存在**的东西；理想架构与未落地的设计一律收进 §6。
>
> **树状态**：分支 `feat/qt6-ktexteditor-migration`，HEAD `4775bee`。`git status` 显示 `CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/app/main.cpp` 已改未提交，`src/editor/` 与 `evidence/edit-core/` 未跟踪；`build-qt.sh` 还会重写 `evidence/m0-build/build-logs/`，所以那批日志也处于已修改状态。**这些改动没提交**是 §6 的头号风险。
>
> **票号口径**：本文所有 `T<n>` 指 Qt6 新看板 `docs/handover/appendix/board-snapshot.md`（下称「新看板」）。旧 Rust/GTK 看板有同名票（例如旧 T15/T21 与新 T15/T21 完全无关），本文不引用旧看板票号。
>
> **证据口径**：`evidence/` 下多数是已入库证据（未跟踪的例外会单独标注）；`target/` 是 `.gitignore` 掉的构建树（`.gitignore:10`）。文中标「本机观察」的数字来自 2026-10-31T16:21 对当前树的一次构建（`target/build-qt-green.log`，未入库），其余来自源码与已入库证据。

## 0. 三行结论

- 能跑的是**应用外壳**：主窗口、菜单、6 个 dock、分屏、会话、主题；`vedit-app` 确实被构建（`src/app/CMakeLists.txt:42`）。
- **编辑器和 tree-sitter 着色没有进产品**：`vedit-app` 用 `DemoEditorProvider`（QPlainTextEdit），syntax 三个 lane 只编译成静态库并跑 ctest（§6 G1/G2）。
- **打包链装的不是 `vedit-app`**：`packaging/runtime/` 把 M0 smoke 装成 `bin/vedit-m0-smoke`（§6 G7）。

## 1. 目录结构总表

「参与根构建」分两层写：**编译**（静态库是否被根 CMake 编出来）与**进产品**（是否链接进 `vedit-app`）。两者的差别见 §6。

| 目录 | 负责什么 | 对外入口 | 票（新看板） | 参与根构建（本机观察） |
|---|---|---|---|---|
| `src/app` | 主窗口、菜单、停靠布局、水平/垂直分屏、会话持久化、主题、`PanelRegistry`、`CommandRegistry` | `int main()` `src/app/main.cpp:350`；`ApplicationShell` `src/app/app_shell.cpp:31`；`PanelRegistry` `src/app/panel_registry.h:34` | T13（board-snapshot:88） | 编译：是（根 `CMakeLists.txt:80-85`）；进产品：是（`src/app/CMakeLists.txt:42-43`） |
| `src/syntax/provider` | 语言无关 `SyntaxProvider` 接口、C16 `OwnerArbiter`、C15 `SyntaxService` 运行期开关、tree-sitter 后端 | `class SyntaxProvider` `src/syntax/provider/syntax_provider.h:85`；`GrammarRegistry` `grammar_binding.h:42`；`SyntaxService` `syntax_service.h:24` | T15（board-snapshot:89） | 编译：是（根 `CMakeLists.txt:62`；target `src/syntax/provider/CMakeLists.txt:82`；ctest `:150`）；进产品：**否** |
| `src/syntax/kde` | KSyntaxHighlighting 私有数据路径加载、`.v/.sv` 显式映射、禁用 KDE 两份定义 | `class KsyntaxData` `src/syntax/kde/ksyntax_data.h:60`；`load()` `:69` | T16（board-snapshot:90） | 编译：是（根 `CMakeLists.txt:63`；target `src/syntax/kde/CMakeLists.txt:16`；ctest `:33`）；进产品：**否** |
| `src/syntax/grammars` | tree-sitter-verilog 绑定、`highlights/folds/structure` 三组 query、编译期 grammar 注册 | `registerBuiltinGrammars()` `src/syntax/grammars/bindings.h:21`；`makeVerilogBinding()` `:24` | T17（board-snapshot:91） | 编译：是（工作树根 `CMakeLists.txt:64`，**未提交**；target `src/syntax/grammars/CMakeLists.txt:174`；ctest `:196`）；进产品：**否** |
| `src/syntax/overlay` | 计划中的 tree-sitter overlay 着色（C17/C16 落点） | **无**：目录不存在 | T18（board-snapshot:34，进行中） | **否**：根 `CMakeLists.txt:66` 列了它，但 `if(EXISTS)` 在 configure 时打印 `src/syntax/overlay not present yet; skipped`（`target/build-qt-green.log:117`） |
| `src/workspace/filetree` | 文件树面板：目录展开、双击打开、新建/重命名/删除、`file-tree` 面板工厂 | `installFileTree()` `src/workspace/filetree/filetree_integration.h:30`；工厂注册 `filetree_integration.cpp:26` | T21（board-snapshot:35） | 编译：是（`src/app/CMakeLists.txt:67-74`；target `src/workspace/filetree/CMakeLists.txt:45`）；进产品：是（`src/app/CMakeLists.txt:78,102`） |
| `src/workspace/palette` | Ctrl+P 命令面板、文件索引、模糊匹配 | `installPalette()` `src/workspace/palette/palette_integration.cpp:213`；命令注册 `:101-109` | T22（board-snapshot:36） | 编译：是（target `src/workspace/palette/CMakeLists.txt:30`）；进产品：是（`src/app/CMakeLists.txt:79,102`）；**它是全局命令，不是 dock** |
| `src/workspace/search` | 全局搜索面板与 `search` 面板工厂 | `installSearch()` `src/workspace/search/search_integration.h:20`；工厂注册 `search_integration.cpp:17` | T23（board-snapshot:92） | 编译：是（target `src/workspace/search/CMakeLists.txt:64`；ctest `:92`）；进产品：是（`src/app/CMakeLists.txt:80,102`） |
| `src/terminal` | 本地 shell 会话、自写 pty + VT 模拟器、`terminal` 面板工厂 | `terminalPanelFactory()` `src/terminal/terminal_panel.h:80`；target `src/terminal/CMakeLists.txt:33`；ctest `:62` | T35（board-snapshot:93） | 编译：是（工作树根 `CMakeLists.txt:65`，**未提交**）；进产品：是（`src/app/CMakeLists.txt:47-49` 未提交；`main.cpp:396-397` 未提交） |
| `src/editor` | 计划中的 KTextEditor 编辑内核（打开/原子保存/查找替换/跳行/撤销重做/多光标） | **无产品入口**；只有 `src/editor/probes/*.cpp` 探针 | T14（board-snapshot:33，进行中） | **否**：`src/editor/probes/CMakeLists.txt:1` 明写 “Standalone only; NOT part of the product build”；根 `CMakeLists.txt:61-67` 的 lane 列表无该条目 |

补充两点：

- 旧 Rust/GTK 产品仍在 `crates/`（`Cargo.toml:6-15` 的 workspace members，未删）；本文的 `src/*` lane 均指新 Qt6 产品，旧产品的门禁仍会在 `build/gates/gates.sh:43-52` 里跑。
- `src/workspace/integration.{h,cpp}` 是**胶水**，不是 lane：`installPanels()` 声明在 `src/workspace/integration.h:32`，实现用 `#if defined(VEDIT_WORKSPACE_HAVE_*)` 只调用已链接的 lane（`src/workspace/integration.cpp:17-31`）。
- 命名漂移：`filetree_integration.h:9` 的注释写 target `vedit_workspace_filetree`，实际 target 是 `vedit_file_tree`（`src/workspace/filetree/CMakeLists.txt:45`，映射在 `src/app/CMakeLists.txt:78`）。接手时以 CMake 为准。

## 2. 接缝清单（C15/C16/C17/C18 + Open Q3）

C13 说明了为什么需要这些接缝：语义高亮必须走 KTextEditor 宿主侧 range/属性接口（`docs/handover/appendix/requirements-rev2.md:77`）。C15–C18 的定义在同一文件 `:79-82`。**注意：接缝的设计证据在 `evidence/` 与 `probes/`，生产实现大多还没接进产品**。

| 编号 | 一句话 | 树内有生产实现？ | 进 `vedit-app`？ | 主要证据 |
|---|---|---|---|---|
| C15 | 抽象 `SyntaxProvider` + 编译期选 grammar + 运行期整体关 tree-sitter | 有（provider + grammars 静态库） | 否 | `evidence/r2-c15/结论.md:3-7`；`evidence/syn-provider/07-conclusion.md:37-95` |
| C16 | 同一语言/range 单一 owner（Base 或 Overlay） | 有（`OwnerArbiter` 库） | 否（只有单测调用） | `src/syntax/provider/POLICY.md:1-20`；`evidence/syn-provider/07-conclusion.md:72-84` |
| C17 | overlay 必须经 KTextEditor 宿主侧 moving-range/属性接口 | **否**（只有探针） | 否 | `evidence/r2-c17/结论.md:66-79`；`evidence/r2-c17/v2/结论-v2.md:22-51` |
| C18 | `libtree-sitter` 与 grammar 随私有运行时（动态或静态） | 有（构建 + 打包脚本） | 仅间接（打包的是 smoke） | `src/syntax/provider/CMakeLists.txt:91-103`；`packaging/runtime/assemble-appdir.sh:75-96` |
| Open Q3 | 折叠由外部 range 经命令注入，不做替换折叠视图 | **否**（只有决策文档） | 否 | `evidence/r2-fold/decision.md:3-12,180-189` |

### C15 — 可替换 grammar 接缝

- **真实含义**：grammar 必须以抽象 `SyntaxProvider` 接入；编译期可选 grammar、运行期可整体关闭 tree-sitter，不得把具体 grammar 的 API/语义写死（`requirements-rev2.md:79`）。
- **真实签名**：`SyntaxProvider` 有 `id/displayName/version/license/capabilities`、`parse(std::string_view)`、`queryIds()/queryRanges(tree, query_id)`、`highlightSpans(tree)`、`foldRanges(tree)`、`breadcrumbs(tree, byte_offset)`、`applyEdit(const Edit&)`、`text()`（`src/syntax/provider/syntax_provider.h:85-122`）。grammar 侧是 `GrammarBinding`（`language` 为不透明函数指针，`grammar_binding.h:27-38`）与 `GrammarRegistry::registerBinding`（`:47`）。
- **谁实现**：T15 实现接口与 generic tree-sitter 后端；T17 的 `src/syntax/grammars` 通过 `VEDIT_SYNTAX_REGISTER_GRAMMAR` 注册具体 grammar（`grammar_binding.h:74-78`、`bindings.cpp`）。
- **谁调用**：目前只有测试。`SyntaxService` 的调用点在 `src/syntax/provider/tests/test_syntax_provider.cpp:56,141,229` 与 `src/syntax/grammars/tests/test_syn_grammars.cpp:266,482`；产品侧没有引用：`src/app/CMakeLists.txt:30-35` 的链接列表与 `target/qt6-build/build.ninja:1185` 的 `vedit-app` 链接行都不含 syntax 库，且 `src/app`/`src/workspace`/`src/terminal` 内没有 `syntax_service.h` 的 include（本轮 grep，无行号可引）。
- **变了会影响谁**：编辑器（provider 的生命周期/每文档实例）、overlay（消费 spans）、折叠（foldRanges 只是数据出口）、设置 UI（运行期开关）、打包（grammar 的许可与体积）。C15 证据见 `evidence/r2-c15/结论.md:3-7,111-136`。

### C16 — 单一 owner 仲裁

- **真实含义**：同一语言、同一 byte range 最多一个着色 owner；两个 overlay 重叠一律拒绝并报告现有 owner；底座关闭时区间变 `None`（`requirements-rev2.md:80`；策略全文 `src/syntax/provider/POLICY.md:1-20`）。
- **真实签名**：`OwnerArbiter::setBaseEnabled(language, enabled)`、`claim(language, overlay_id, start, end)`、`release(...)`、`releaseAll(language, overlay_id)`、`ownerSpans(language, start, end)`、`ownerAt(language, offset)`、`assertSingleOwner(language, start, end, detail)`（`src/syntax/provider/owner_arbiter.h:47-70`）；`OwnerKind {None, Base, Overlay}`（`:23`），`ClaimResult {Granted, Conflict, InvalidRange}`（`:32`），`ClaimOutcome` 携带 `message`/`conflicting`（`:34-38`）。
- **谁实现**：T15（`owner_arbiter.cpp`）。**谁调用**：只有 `src/syntax/provider/tests/test_syntax_provider.cpp:59-61` 的单测；没有任何产品绘制路径调用它。
- **变了会影响谁**：overlay 的 claim/release 策略、底座重绘、A1g「无双重着色」判据、编辑器的 base-colour pass。C17 v2 明确指出仲裁点必须在**应用侧**（`evidence/r2-c17/v2/结论-v2.md:43-46`）。

### C17 — KTextEditor 宿主侧 overlay 接口

- **真实含义**：overlay 必须经 KTextEditor 宿主侧 range/semantic-attribute 接口落地，且该接口的可用性是 R2 开工前置（`requirements-rev2.md:81`；C13 见 `:77`）。
- **真实签名**（证据记录的是镜像内安装头文件的行号，不是本仓头文件）：`KTextEditor::Document::newMovingRange(Range, InsertBehaviors, EmptyBehavior)`（`evidence/r2-c17/结论.md:68`）、`MovingRange::setAttribute(Attribute::Ptr)`（`:69`）、`attribute()`（`:70`）、`setZDepth(qreal)/zDepth()`（`:71`）、`setAttributeOnlyForViews(bool)`（`:72`）、`Attribute` 继承 `QTextCharFormat`（`:73`）、`Attribute::setDefaultStyle(KSyntaxHighlighting::Theme::TextStyle)`（`:74`）。
- **硬约束**：每行属性 range 上限 **1024**（1100 个/行时整行属性被静默丢弃）；没有文档级 range 枚举、没有逐点属性查询、没有公共语义高亮 API；`setDefaultStyle` 在默认主题下只可查询不参与渲染，必须由应用侧解析主题色（`evidence/r2-c17/v2/结论-v2.md:40-51`）。
- **谁实现**：**产品里没有**。只有独立探针 `probes/c17-overlay/src/main.cpp`（有自己的 CMake，根 `CMakeLists.txt:61-67` 不包含 `probes/`）；T18 仍在进行中（`board-snapshot:34`）。**谁调用**：无。
- **状态**：C17 已在官方镜像复跑并「有条件关闭」（`evidence/r2-c17/v2/结论-v2.md:1,22-35`）；结论的版本矩阵是 Qt 6.10.2 / KTextEditor 6.24.0（`evidence/r2-c17/结论.md:24-25`），升级需重测。
- **变了会影响谁**：编辑器的渲染层、主题解析、折叠、A1g/A1h 判据；若接口变化，退化路径是 D1 完全回退 KSyntaxHighlighting（`evidence/r2-c17/结论.md:226-234`）。

### C18 — tree-sitter 运行库随包

- **真实含义**：`libtree-sitter` 与 grammar（动态库或静态编译进二进制）都必须进私有运行时；静态编译时 C15 的可替换性必须保留在编译期配置层（`requirements-rev2.md:82`）。
- **真实签名/开关**：`VEDIT_SYNTAX_TREESITTER=ON|OFF`、`VEDIT_SYNTAX_TS_LINK=shared|static`、`VEDIT_SYNTAX_GRAMMAR=<id>`（`src/syntax/provider/CMakeLists.txt:24-27`）；static 分支直接找 `libtree-sitter.a` 链入（`:91-103`）。运行库版本锁在 `scripts/versions-qt.env:52-55`（0.25.9）。
- **谁实现**：T15 负责链接方式；T11/T64 负责把运行库放进 AppDir：`assemble-appdir.sh:75-96` 用 `ldd` 闭包复制依赖，`AppRun:65` 用包内 loader 启动。
- **变了会影响谁**：AppImage 体积、grammar ABI（换运行时需重测）、许可清单。许可侧：默认 grammar 是 MIT 的 `tree-sitter-verilog@4457145e…`，`.sv` 底座用 LGPL-2.1-or-later 移植定义，KDE 的 `systemverilog.xml`/`verilog.xml` 按 sha256 禁止随包（`docs/decisions/r2-license.md:37-57`；C15/C18 的保留理由在 `:79-98`）。

### Open Q3 — 折叠（没有 C 编号，但同样关键）

- **真实含义/结论**：公共 KTextEditor API 没有折叠模型；但 `KTextEditor::Editor::commands()` 暴露 `fold`/`tfold`/`unfold`，`Command::exec(view, cmd, msg, range)` 接受调用方给出的 Range，所以「精确折叠」可以**叠加注入**，不能查询/按 range 删除/每帧重建（`evidence/r2-fold/decision.md:3-12`）。
- **真实签名**：`KTextEditor::Editor::commands()`、`KTextEditor::Command::exec(view, cmd, msg, range)`，命令名 `fold`、`tfold`、`unfold`（`evidence/r2-fold/decision.md:8-9`）。
- **谁实现/谁调用**：无。T19 待办（`board-snapshot:44`），树内没有 fold 注入代码；outline 面板也没有工厂（§3）。
- **边界与影响**：单次注入约 0.7 ms（`decision.md:128-133`），50 条 ≈ 35 ms，只能开关级重建；`unfold` 只作用于光标所在行，无法按 range 删除任意折叠（`:137-139`）；替换折叠视图量级 L（`:172-178`）。影响 A1c/A1f、面包屑、outline。

## 3. 应用装配

### 3.1 主窗口怎么起（按执行顺序）

1. `main()`（`src/app/main.cpp:350`）创建 `QApplication`、设置应用名/版本/组织（`:352-355`）。
2. 解析命令行：`--state-dir`、`--project`、`--theme`、`--selfcheck`、`--screenshots`、`--no-session-restore`、`--hold`（`:357-383`）。
3. `--selfcheck` 走 `runSelfCheck()`（`:385-387`）；`--screenshots` 走 `captureScreenshots()`（`:388-390`）；两者都在 `installPanels()` 之前返回（§6 G6）。
4. 交互模式构造 `ApplicationShell shell(options)`（`src/app/main.cpp:392`）。构造函数（`src/app/app_shell.cpp:31-92`）依次：解析 stateDir（`:39-42`）→ `SessionStore`/`ShortcutStore`（`:43-50`）→ `PanelRegistry`/`CommandRegistry`/`ThemeManager`（`:52-55`）→ `EditorArea` + `DemoEditorProvider`（`:34,57-59`）→ `buildUi()`/`registerPanels()`/`registerCommands()`/`buildMenuBar()`（`:66-69`）→ 恢复会话或建默认布局（`:71-89`）。
5. `installPanels(shell, {projectRoot, stateDir})`（`src/app/main.cpp:393`）→ `src/workspace/integration.cpp:17-31`：按编译宏调用 `installPalette` / `installFileTree` / `installSearch`。
6. `shell.panels()->setFactory("terminal", terminalPanelFactory())`（`src/app/main.cpp:396-397`）把终端 lane 的工厂装进 `terminal` dock。
7. `shell.show()` + `app.exec()`（`:398-404`）。

### 3.2 面板注册

`ApplicationShell::registerPanels()`（`src/app/app_shell.cpp:156-179`）只注册 **spec**（id/title/owner/area），不创建内容；`PanelRegistry::createContent()` 在没有 factory 时返回占位控件（`src/app/panel_registry.cpp:97-105`；占位实现 `:15-44`）。`setFactory()` 只影响**之后**的插入（`panel_registry.cpp:84-90`）。

| 面板 id | shell 里登记的 owner | 工厂现状 | 内容现状 |
|---|---|---|---|
| `file-tree` | `workspace`（`app_shell.cpp:174`） | `installFileTree()` → `setFactory`（`filetree_integration.cpp:26`） | 真实文件树 |
| `search` | `workspace`（`:175`） | `installSearch()` → `setFactory`（`search_integration.cpp:17`） | 真实搜索面板 |
| `terminal` | `tools`（`:176`） | `main.cpp:396-397` | 真实终端 |
| `vcs` | `vcs`（`:177`） | **无** | 占位（`panel_registry.cpp:15-44`） |
| `settings` | `settings`（`:178`） | **无** | 占位 |
| `outline` | `editor/syntax`（`:179`） | **无** | 占位 |

### 3.3 `src/app` 怎么把 lane 装起来

**没有 `src/app/integration.h`**。`main.cpp:15` 的 `#include "integration.h"` 实际解析到 `src/workspace/integration.h`：`vedit_workspace_integration` 把 `src/workspace` 作为 PUBLIC include（`src/app/CMakeLists.txt:85-87`），而 `vedit-app` 链接了它（`:108`）；`terminal_panel.h` 同理来自 `vedit_terminal` 的 PUBLIC include（`src/terminal/CMakeLists.txt:34`）与 `:47-49` 的链接。

- 工作区 lane 的约定：`src/workspace/<lane>/<lane>_integration.cpp` 存在且 target 存在，才链接并定义 `VEDIT_WORKSPACE_HAVE_<LANE>`（`src/app/CMakeLists.txt:100-107`）；编入与否由 `src/workspace/integration.cpp:5-13` 的宏控制。
- 面板之外：Ctrl+P 是 `CommandRegistry` 的全局命令 `file.goto-file`（`palette_integration.cpp:101-109`），不是 `PanelRegistry` 的 dock；它把文件打开到 shell 的 demo editor（`palette_integration.h:10-13`）。
- 编辑器 seam：`EditorProvider` 定义在 `src/app/editor_provider.h:18-31`（`id`/`displayName`/`createEditor(parent)`），占位实现 `DemoEditorProvider`（`:35-40`，QPlainTextEdit）；注入点 `ApplicationShell::setEditorProvider()`（`src/app/app_shell.cpp:126-133`）**全树没有调用者**（本轮 `grep -rn setEditorProvider src` 只命中定义/声明：`app_shell.cpp:126`、`app_shell.h:54`）。shell 故意不链接 KTextEditor（`src/app/CMakeLists.txt:11-14`）。

## 4. 构建链路

### 4.1 根 CMake 的 lane 清单（显式清单，NO GLOB）

`set(VEDIT_LANE_DIRECTORIES ...)`（`CMakeLists.txt:61-67`）当前列了 5 个目录：`src/syntax/provider`、`src/syntax/kde`、`src/syntax/grammars`、`src/terminal`、`src/syntax/overlay`；后三个是**工作树未提交**的新增（HEAD 版本只有前两个，见 `git diff CMakeLists.txt`）。

| 条目 | 目录在树里？ | 有 CMakeLists？ | configure 结果（本机观察） |
|---|---|---|---|
| `src/syntax/provider` | 是 | 是 | 编译（`target/build-qt-green.log:112`） |
| `src/syntax/kde` | 是 | 是 | 编译（`:113`） |
| `src/syntax/grammars` | 是 | 是 | 编译（`:115`） |
| `src/terminal` | 是 | 是 | 编译（`:116`） |
| `src/syntax/overlay` | **否** | — | 跳过（`:117`） |

**为什么不用 `file(GLOB)`**：构建不能依赖「磁盘上恰好有什么文件」，否则一次 `git clean`/分支切换会静默改变构建内容；lane 落地时必须在同一个 PR 里把目录加进清单（`CMakeLists.txt:52-60`）。T63 用反例证明过「列表里的 lane 坏了门禁一定红」：在隔离副本里让 provider 编译 `#error`，`build-qt.sh` exit 1（`evidence/t63-root-cmake-lanes/README.md:9-16,35-37,68-87`，原始日志 `30-counterexample-red.log`）。注意该反例只覆盖 provider，不覆盖当前新增的 terminal/grammars。

### 4.2 `if(EXISTS)` 机制

- 根 lane 循环：`if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${lane_dir}/CMakeLists.txt")` 才 `add_subdirectory`，否则打印跳过（`CMakeLists.txt:68-75`）。
- `src/app`：根在 `CMakeLists.txt:80-85` 里用同一个 `if(EXISTS)` 自动纳入，所以 app lane 不需要改根构建。
- `src/app` 自己也有两级可选纳入：workspace 三 lane 按 `src/workspace/${lane}/CMakeLists.txt` 存在性加入（`src/app/CMakeLists.txt:67-74`）；workspace integration 只有 `src/workspace/integration.cpp` 存在才建库（`:82-109`）。
- 目标存在才链接：`if(TARGET vedit_terminal)`（`src/app/CMakeLists.txt:47-49`）；`if(TARGET ${lane_target} AND EXISTS ..._integration.cpp)`（`:100-107`）。lane 内部也用同一模式：filetree/palette/search 只有 `TARGET vedit_app_shell` 时才编译 app 适配器（`src/workspace/filetree/CMakeLists.txt:56-63`、`src/workspace/palette/CMakeLists.txt:56-74`、`src/workspace/search/CMakeLists.txt:30-60`）。

### 4.3 `scripts/build-qt.sh` → 容器镜像 → cmake/ninja

1. `scripts/build-qt.sh` source `scripts/lib-qt.sh` 并加载 pin（`build-qt.sh:21-23`），证据目录 `evidence/m0-build/build-logs`（`:37`）。
2. 构建/复用固定镜像 `vedit-qt6-build:local`（`build-qt.sh:43`；镜像实现 `scripts/lib-qt.sh` 的 `build_qt6_image`），记录 image id（`build-qt.sh:44`）。
3. 用 `run_in_qt6_container` 在镜像内跑 `scripts/build-qt-in-container.sh --tests`（`build-qt.sh:47`）。
4. 容器内 `cmake -S /vedit-build -B target/qt6-build -G Ninja`（`build-qt-in-container.sh:43`），然后 `cmake --build`（`:48`）。
5. ELF 事实（NEEDED、最高 GLIBC、ldd、sha256）写 `34-qt6-elf.txt`（`:56-74`）；`ctest --output-on-failure`（`:76`）；Xvfb + xcb + llvmpipe 窗口冒烟（`:78-104`）。
6. 镜像本身是 `scripts/docker/Dockerfile.build` 的 `qt6-build` stage（`Dockerfile.build:38`）；base 按 digest 固定（`:36-37`），每个 `.deb` 按 `qt6-locked-packages.txt` 的 sha256 校验后安装（`:47-56`）。版本矩阵在 `scripts/versions-qt.env:39-55`，CMake 侧用 `EXACT` 请求（`cmake/VEditVersions.cmake`、根 `CMakeLists.txt:31-42`）。

### 4.4 `build/gates/*.sh` 各管什么

`package.json:7-17` 把 MPD gate id 映射到脚本；但**这套门禁的主语仍是旧 Rust 产品**，Qt lane 是折进去的（`docs/BUILD.md:278-287`）。下表中的脚本路径均相对 `build/gates/`。

| 脚本 | 管什么 | 现状/边界 |
|---|---|---|
| `gates.sh` | 旧 slang 产物 + `GLIBC_2.17` 上界（`:4,30-31`）+ `cargo`（`:43-52`）+ Qt lane（`:56-63`） | 唯一能跑重活的 ledger gate；Qt lane 失败会红 |
| `qt6-cxx.sh` | 调 `scripts/build-qt.sh`（完整或 `--no-test`） | 被 `gates`/`typecheck`/`tests` 复用（脚本头 `:1-12`） |
| `typecheck.sh` | C++ `-Werror` 配置+编译（`:11-18`）+ `cargo clippy`（`:27-32`） | 没有独立 C++ 静态分析器 |
| `tests.sh` | Rust `cargo test` + Qt ctest | **MPD ledger 不可达**（`:4-7`），只供人工 |
| `manifest.sh` | Rust crate manifest 一致性 | 与 Qt 产品无关 |
| `docs.sh` | README/BUILD/USER_GUIDE/THIRD_PARTY_NOTICES 存在且 ≥10 非空行（`:7,17-24`） | 旧产品文档要求 |
| `vendor.sh` | 旧产品第三方 `slang/verible/gtk` 出现在 notices（`:15`） | 不检查 Qt/KF6/tree-sitter 组件 |
| `dist.sh` | 找 `*.AppImage` 并检查可执行/ELF/`--appimage-version`（`:7-34`） | 不验证里面是不是 `vedit-app` |
| `pack.sh` | 找 `*.tar.gz` 并检查 launcher（`:8-32`） | 旧 tarball 产物 |
| `comments.sh` / `rows.sh` | 无此门禁，永远 exit 1（各自 `:1-8`） | 诚实红，不是通过 |

**本机观察**：2026-10-31T16:21 对当前树跑出的 `target/build-qt-green.log` 里 configure 打印了 provider/kde/grammars/terminal 与三个 workspace lane（`:112-127`），`ctest` 9/9 通过（`:254-272`）。这份日志未入库，且不代表 §6 的缺口已闭合。

## 5. 打包链路

`packaging/runtime/` 是当前 Qt6 私有运行时的打包实现；`packaging/appimage/` 与 `packaging/tarball/` 是旧 Rust 产品的打包/测试工具，不要混用。下表中前六个脚本均位于 `packaging/runtime/`。

| 脚本 | 职责 | 关键落点 |
|---|---|---|
| `packaging/runtime/build.sh` | 一键编排 1/7–7/7：装配 → 许可闸门 → AppDir 自检 → F2 隔离 → AppImage → AppImage 自检/隔离 → CentOS 7 机制探针 | 步骤行号 `:32,45,51,57,63,69,76,85,107`；`CENTOS_IMAGE` 固定 digest `:24` |
| `assemble-appdir.sh` | 组 AppDir：程序、Qt 插件、`ldd` 库闭包、loader+glibc、白名单 syntax XML、字体/图标、manifest/notices；末尾断言无 `syntax-bundled/` | `:61-64` 程序；`:75-96` 库闭包；`:98-106` loader/glibc；`:108-122` 白名单；`:150-157` 硬条件；`:159-174` manifest |
| `build-appimage.sh` | 用固定 sha256 的 appimagetool 把 AppDir 打成单文件；拒绝用 probe AppDir 出交付包 | probe 拒绝 `:31-36`；工具校验 `:38-44` |
| `selfcheck.sh` | 用干净环境经 `AppRun --selfcheck` 启动，断言 `M0-SMOKE ok=1`、`libc=<包内前缀>`、打印定义集合 | `:44-56` |
| `verify-f2-isolation.sh` | 造 402 个宿主 XML + 1 个用户 XML，验证正控 `defs=2`、反向控制 `defs=403`（证明测试能抓到泄漏） | 夹具 `:49-54`；正控 `:71-85`；反控 `:87-95` |
| `verify-not-shipped.sh` | 交付闸门：按 sha256 拦 KDE `systemverilog.xml`/`verilog.xml`（`:27-28,46-51`）、禁 `syntax-bundled/`（`:60-64`）、可见定义集合必须等于白名单（`:81-83`）、库内不得内嵌 GPL 定义（`:89-110`） | 失败即 `DELIVERY_BLOCKED=yes`（`:132`） |

### AppRun 里两个 XDG 变量为什么必须一起设

`AppRun:31-32` 同时设：

```sh
export XDG_DATA_DIRS="$APPDIR/usr/share"
export XDG_DATA_HOME="$APPDIR/usr/share"
```

KSyntaxHighlighting 通过 `QStandardPaths` 找定义（F2 形态保留 `NO_STANDARD_PATHS=OFF`，见 `evidence/m0-ksh-selfbuild/REPORT.md:170-173`；C6 允许 `XDG_DATA_DIRS` 或 `addCustomSearchPath`，`requirements-rev2.md:69`），**两个变量分别覆盖两类目录**：`XDG_DATA_DIRS` 管系统级（宿主 `/usr/share`），`XDG_DATA_HOME` 管用户级（默认 `$HOME/.local/share`；这条分工写在 `evidence/m0-ksh-selfbuild/FORM-DECISION.md:25-26` 的 C3/C4 条件里）。只设前者时，用户目录仍可见：实验 `f2-xdgdirs-only` 得到 `defs=2`，其中一个是 `UserLeakProbe`；两个都设才得到 `defs=1`，只剩 `Verilog`（`evidence/m0-ksh-selfbuild/FORM-DECISION.md:45-47`）。反向控制把 `/usr/share` 加回 `XDG_DATA_DIRS` 立刻回到 `defs=402`（`:49`）；T64 在 AppDir/AppImage 上重跑得到正控 `defs=2`、反控 `defs=403`（`evidence/m0-runtime/F2.md`，报告“正控/反向控制”一节）。

同一脚本还有两个易踩点：`VEDIT_SYNTAX_DIR` 也指向包内（`AppRun:33`）；`LD_LIBRARY_PATH` **只在 exec 前导出**（`AppRun:65`），提前导出会让宿主的 `cat/sed/grep` 用包内 glibc 而失败，导致白名单守卫读到空集合、假 PASS（`AppRun:20-21`）。

## 6. 装配缺口（最重要）

先给结论：**根构建现在确实编译了 provider/kde/grammars/terminal，但产品二进制只链接了 app shell + terminal + 三个 workspace lane；编辑器、syntax provider、kde 数据、grammar、overlay 都不在 `vedit-app` 里。** 严格说，树里没有「有成型产品代码却完全没被编译」的 lane（overlay 目录不存在、editor 只有探针）；更危险的是「编译了、测试了，但没链接进产品」。下面每条都是「绿色但没证明产品可用」。

| # | 假绿点 | 树内证据 | 实际后果 |
|---|---|---|---|
| G1 | **`src/editor` 没有编辑内核** | 只有探针：`src/editor/probes/CMakeLists.txt:1` 明写 standalone only；没有 `src/editor/CMakeLists.txt`，没有 `document_io/atomic_save/find_replace/...` 实现；`evidence/edit-core/DESIGN.md` 只是设计稿（且未跟踪） | `vedit-app` 用 `DemoEditorProvider`（QPlainTextEdit，`app_shell.cpp:34`、`editor_provider.h:35-40`）；打开/保存/查找/撤销/多光标全部不存在；`setEditorProvider()` 无调用者（`app_shell.cpp:126-133`），产品 ELF 没有 `libKF6TextEditor` NEEDED（本机 `readelf -d target/qt6-build/src/app/vedit-app`） |
| G2 | **syntax 三个 lane 编译了但没链接进产品** | `src/app/CMakeLists.txt:30-35,43,47-49,108` 只链 app_shell/terminal/workspace；本机 `target/qt6-build/build.ninja:1185` 的 `vedit-app` 链接行没有 `libvedit_syntax_provider.a`/`libvedit_syntax_kde.a`/`libvedit_syntax_grammars.a` | 产品里没有 `SyntaxProvider`、没有 `KsyntaxData`、没有 tree-sitter；`ctest` 里的 provider/grammar/kde 测试只证明库自己可用，不证明产品可用 |
| G3 | **根 configure 的编译期 grammar 是 `none`** | `src/syntax/provider/CMakeLists.txt:25` 先写 cache `VEDIT_SYNTAX_GRAMMAR=none`；`src/syntax/grammars/CMakeLists.txt:36` 的 `set(... CACHE ...)` 不会覆盖已有 cache；生成头 `target/qt6-build/src/syntax/provider/generated/grammar_config_generated.h:5` 为 `kCompiledGrammar="none"`；`SyntaxService` 构造因此不设 `active_`（`syntax_service.cpp:15-21`），`available()==false`（`:62-70`） | 即使按 G2 把库链进去，provider 默认也不工作；必须在启动时 `activate("verilog")` 或把 configure 改成 `-DVEDIT_SYNTAX_GRAMMAR=verilog` |
| G4 | **overlay lane 不存在，C16/C17 没落点** | 根 `CMakeLists.txt:66` 列了 `src/syntax/overlay`，目录不存在，configure 跳过（`target/build-qt-green.log:117`）；`src/` 内无 `MovingRange`/`OverlayRegistry` 产品代码；`OwnerArbiter` 只有单测调用（`test_syntax_provider.cpp:59-61`） | 没有 tree-sitter 着色；C16 的单一 owner 策略没有执行者；C17 的 1024/行、应用侧主题解析等结论只停在证据里 |
| G5 | **6 个 dock 只有 3 个有内容** | 6 个 spec 在 `app_shell.cpp:174-179`；只有 file-tree/search/terminal 注册 factory；vcs/settings/outline 永远走占位（`panel_registry.cpp:97-105`、`:15-44`） | 侧栏/底部有界面但点开是空状态；VCS、设置、outline 三条 lane 在树里不存在（本轮 `ls src` 无 `vcs/`、`settings/`、`tools/`） |
| G6 | **app-shell 自测不测 lane 接线** | `--selfcheck` 只查注册表/布局（`main.cpp:110-131`）并在 `:385-387` 返回；`installPanels`/`setFactory` 在 `:392-397` 才执行；截图模式同样不装 lane（`:301-332`） | `vedit-app-shell-selftest` 通过 ≠ 面板有内容；它测的是 shell 骨架 |
| G7 | **AppImage 装的是 M0 smoke，不是 `vedit-app`** | `assemble-appdir.sh:27,62` 只把 `cmake/smoke/vedit-m0-smoke` 装成 `bin/vedit-m0-smoke`；`AppRun:67-68` 先找 `bin/vd-app`（从未安装，本轮 `grep -rn vd-app packaging/runtime` 只命中 `AppRun:67` 这一处引用，无安装步骤），再回退 smoke；`build.sh:19` 的 preflight 也只引用 smoke | 打包链、AppRun、F2 隔离、CentOS 探针全绿，但跑的是冒烟程序；`vedit-app` 的终端/工作区/主题没有被交付验证过 |
| G8 | **filetree 不在根 ctest，palette bench 不是测试** | `src/workspace/filetree/CMakeLists.txt:67-69` 让测试默认跟随 standalone，根构建 standalone=OFF，所以 filetree 的 demo/ctest 不构建；本机 cache 也是 `VEDIT_BUILD_FILETREE_TESTS:BOOL=OFF`（`target/qt6-build/CMakeCache.txt:463`）；本机 9 个 ctest（`target/build-qt-green.log:254-272`）没有 `vedit-filetree-tests`；palette bench 只 `add_executable`（`src/workspace/palette/CMakeLists.txt:43-45`） | 「ctest 全绿」不覆盖文件树行为；文件树也没有独立评审 verdict（`docs/plans/v2-slices.md:48-51`） |
| G9 | **关键接线未提交** | `CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/app/main.cpp` 已改未提交；`src/editor/`、`evidence/edit-core/` 未跟踪；`git diff CMakeLists.txt` 显示 grammars/terminal/overlay 三条 lane 是新增，`git diff src/app/main.cpp` 显示 terminal factory 是新增；HEAD `4775bee` 的 `src/app/main.cpp:392` 只有 `installPanels`（以 `git show HEAD:src/app/main.cpp` 为准），没有 terminal factory | 一次干净 checkout 就退回「lane 在树里但根构建不编译 terminal/grammars」的旧假绿状态；本机 9/9 日志也是未跟踪产物，不能当交付证据 |
| G10 | **门禁语义仍以旧 Rust 产品为主** | `gates.sh:43-52` 硬要求 `cargo`；`vendor.sh:15` 只查 `slang/verible/gtk`；`dist.sh:7`、`pack.sh:8` 找旧产物；`tests` 在 ledger 不可达（`tests.sh:4-7`）；`comments/rows` 永远 RED | 「gates 全绿」不能读成 Qt 产品完成；T63 反例也只覆盖 provider，不覆盖当前新增 lane |

### `src/app` 还差哪一行接线

1. **先提交**现有 3 个修改文件；否则下面都只存在于工作树。
2. 在 `main.cpp:392` 之后加**编辑内核注入**（一行，类需 T14 先实现）：`shell.setEditorProvider(std::make_unique<vedit::editor::KTextEditorProvider>())`。目前 `setEditorProvider()` 只有定义、没有调用（`app_shell.cpp:126-133`）。
3. 在 `src/app/CMakeLists.txt:43` 之后把 `vedit_syntax_provider`/`vedit_syntax_kde`/`vedit_syntax_grammars` 链进 `vedit-app`，并在启动时 `SyntaxService::instance().activate("verilog")`（或把根 configure 的 `VEDIT_SYNTAX_GRAMMAR` 改为 `verilog`）。
4. 为 `vcs`/`settings`/`outline` 各加一条 `shell.panels()->setFactory(...)`（各自 lane 落地后；目前三条 lane 都不存在）。
5. 打包侧把 `assemble-appdir.sh:62` 的 smoke 换成 `target/qt6-build/src/app/vedit-app`，装成 `bin/vd-app`（`AppRun:67` 已经在找这个名字）。

### `src/editor` 当前状态

- 目录里只有 `probes/`（9 个探针 + CMake）与未跟踪的证据，没有产品源文件；探针 CMake 明确「Standalone only; NOT part of the product build」（`src/editor/probes/CMakeLists.txt:1`）。
- 设计已完成：`evidence/edit-core/DESIGN.md` 定义了 `document_io/atomic_save/find_replace/goto_line/multi_cursor/undo_probe/perf_probe` 的职责与判据，但**这些文件都不存在**。
- T14 在新看板仍是进行中（`board-snapshot:33`）；`src/app` 的 `EditorProvider` seam 已就绪（`editor_provider.h:18-31`），只差实现与那一行注入。

### 收口顺序（建议）

1. 提交 G9 的接线，并重跑 `bash scripts/build-qt.sh` 把 target 清单写进证据。
2. 按 G2/G3 链接 syntax 三库并激活 grammar（先不做 overlay）。
3. 实现 `src/editor`（T14），在 `main.cpp` 注入 provider。
4. 落地 `src/syntax/overlay`（T18）与 fold 注入（T19），再接 C16 仲裁。
5. 最后把打包输入从 smoke 换成 `vedit-app`（G7），再跑双宿主矩阵。

### 本文的核实方式与边界

- **已核实**：上述每个 file:line 都有本轮读取的源码/证据/构建产物依据；构建参与结论同时对照了 `target/qt6-build/build.ninja:1185`、`readelf -d target/qt6-build/src/app/vedit-app` 与 `target/build-qt-green.log:112-127,254-272`（本机 2026-10-31T16:21 的未入库构建）。
- **未核实**：没有在本轮重新执行 `bash scripts/build-qt.sh`（它会重写已入库的 `evidence/m0-build/build-logs/`）；没有在 AppImage 里实际点开面板；C17/C15/C18 的结论转述自已入库证据，未重跑探针。C17 的所有接口行号是**镜像内头文件**行号，不是本仓文件行号。
