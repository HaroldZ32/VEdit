# 02 · 状态矩阵 —— 什么是真的

> **这张表是整套交接文档的核心。** 上一轮最大的教训是：票面上的 `completed` / `in_progress` 不能当结论用。
> 这里对每条 lane 用**四层判据**分开打分，四层全绿才算完成。
>
> 判据定义：
> - **L1 实现**：树里有能跑的产品代码（不是探针、不是设计稿）
> - **L2 证据**：有能被外人重跑的自测/日志
> - **L3 独立评审**：有**另一个 agent** 看过实现并给了 verdict（自测不算）
> - **L4 进产品**：被根构建编译 **且** 链接进 `vedit-app`

## 总表

| lane | L1 实现 | L2 证据 | L3 独立评审 | L4 进产品 | 结论 |
| --- | --- | --- | --- | --- | --- |
| `src/app` 应用外壳（窗口/菜单/6 dock/分屏/主题/会话） | ✅ | ✅ 73 项自测 + 4 张截图 | ❌ 无 verdict | ✅ | **能跑，但只是空壳**：编辑器是 `QPlainTextEdit` 占位 |
| `src/syntax/provider`（C15 接缝 + C16 仲裁） | ✅ | ✅ ctest | ❌ 无 verdict | ❌ **没链进产品** | 库是好库，产品用不上 |
| `src/syntax/kde`（KSH 私有数据路径） | ✅ | ✅ ctest + 降级证据 | ❌ 无 verdict | ❌ **没链进产品** | 同上 |
| `src/syntax/grammars`（tree-sitter 绑定 + 三组 query） | ✅ | ✅ 三配置各 79/79 | ❌ 无 verdict | ❌ **没链进产品** | 同上；且根构建设 `VEDIT_SYNTAX_GRAMMAR=none` |
| `src/syntax/overlay`（tree-sitter 叠加着色） | ❌ **目录不存在** | ⚠️ 只有 C17 探针证据 | — | ❌ | **方案验证过，代码没写** |
| `src/workspace/filetree` | ✅ | ✅ ctest + 报告 | ❌ 无 verdict | ✅ | 可用 |
| `src/workspace/palette` | ✅ | ✅ 10k 文件基准 | ❌ 无 verdict | ✅ | 可用（全局命令，不是 dock） |
| `src/workspace/search` | ✅ | ✅ ctest + 报告 | ❌ 无 verdict | ✅ | 可用 |
| `src/terminal` | ✅ | ✅ 101 项 CHECK + 截图 | ❌ 无 verdict | ✅（本轮刚接上，**未提交**） | 可用；QTermWidget 的许可取舍见 04 |
| `src/editor` KTextEditor 编辑内核 | ❌ **0/6** | ⚠️ 只有探针（PASS） | — | ❌ | **只有设计稿 + 9 个探针** |
| M0 私有运行时 + AppImage | ✅ | ✅ 四宿主矩阵 | ✅ 有反向对照 | — | **唯一真正端到端可交付的东西** |
| 许可链（R2/B′/F2） | ✅ | ✅ verify.sh 51 passed / 0 failed（含 3 条反向对照） | ✅ | — | 已闭环 |

## 三层现实，请分别记住

### 第一层：真的能交付的

**一个 AppImage**（`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`，65,673,720 B，sha256 `74bd6aae…`）：

- 四台宿主全过：CentOS 7.9（glibc 2.17）/ Ubuntu 24.04 / base-only（宿主 libQt6=libKF6=libX11=libfuse2=0）/ 无 GPU 无 GL
- `libc` 从包内解析（`/app/lib/libc.so.6`），窗口可见性以像素差证死（`compare -metric AE = 307200`，640×480）
- 跑的是 **M0 冒烟程序**，只证明"Qt6/KTextEditor/KSH/tree-sitter 在两条 glibc 线上都能起来"，**不是编辑器**

### 第二层：能构建、但产品没接上的

应用外壳 + 文件树 + 命令面板 + 全局搜索 + 终端都能编译进 `vedit-app`。
但 **`vedit-app` 里的编辑器是 `DemoEditorProvider`（`QPlainTextEdit`）**，syntax 三个库编译了却**没有链接进产品**（`03-architecture.md` §6 G1/G2）。
也就是说：**今天构建出来的 `vedit-app` 没有 KTextEditor、没有语法高亮、没有 tree-sitter**。

### 第三层：看着完成、其实是空的

- `src/editor`：老看板挂着 `in_progress`，实际 **0/6 实现**，`git ls-files src/editor` 原本是 0，
  现在只有 9 个探针 + 1 个 CMake（明写 "Standalone only; NOT part of the product build"）。
- `src/syntax/overlay`：**目录根本不存在**。C17 的探针证明"这条路走得通"，但没有一行产品代码。
- 五条 lane（T17 grammars / T21 filetree / T22 palette / T23 search / T35 terminal）：**都有自测，都没有独立评审**。
  按本项目的定义，这不叫完成。

## 上一轮"票面 vs 事实"的三个实证

| 票面 | 事实 |
| --- | --- |
| T14 `in_progress` | 0/6 实现，只有设计稿（2026-10-31 由 Deep Worker 核查，`git ls-files src/editor` = 0） |
| T17/T21/T22/T23/T35 `completed` | 只有 lane 自己的自测日志，无任何独立 verdict |
| 根构建 9/9 ctest 通过 | 9 个测试**都不覆盖** lane 是否被产品链接；syntax/terminal 曾经"绿但没编" |

## 唯一一条本轮新增的独立评审

`evidence/build-registration/`：Qt Build Engineer 做完根构建登记后，**Reviewer 只读复核 diff + 四份证据 → PASS**
（risk Medium，无 Blocker/High）。它的非阻塞发现值得下一个人逐条看：

1. `VEDIT_SYNTAX_GRAMMAR` 缓存冲突 —— 根构建实际是 `none`，日志里有 `compiled grammar id='none'`
2. 会话恢复顺序 —— `restoreSession` 早于面板注册，恢复出的终端 dock 会是占位符
3. `if(TARGET)` 只护链接，`main.cpp` 仍无条件 include/调用
4. `evidence/term-panel/run-root-rehearsal.sh:34` 的 needle 随列表扩展会失效
5. 6 个 tracked 构建日志被这次绿构建重写
6. 9/9 ctest 不覆盖面板注册本身

## 怎么用这张表

- 想宣称"某功能完成"：必须四层全绿，缺一层就写清楚缺哪层。
- 想排下一轮计划：**优先补 L4 和 L3**，不是补新功能。当前最贵的问题不是功能少，是**产品没接上自己的库**。
