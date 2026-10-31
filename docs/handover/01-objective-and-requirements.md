# 01 · 目标与已拍板的决策

> 这一份的作用是**防止走回头路**。每一条都是用户明确说过或明确选过的，不是推测。
> 权威文本仍是 [`appendix/requirements-rev2.md`](appendix/requirements-rev2.md)（rev 2）；本文是它的"人话版 + 决策记录"。

## 1. 用户原话（逐字，勿改写）

> 把图形内核库换成QT6，用KTextEditor作为基础编辑库，使用KSyntaxHighlighting作为语法高亮引擎，其他的你照着lapce做（github上有），功能尽量一模一样

补充原话，按时间顺序：

| 原话 | 它约束了什么 |
| --- | --- |
| 「静态库给我打包进去，用现代的东西免不了的」 | 私有运行时打包是**必须**，不是可选 |
| 「我的意思是做私有运行时打包，直接把新版本glibc这类的东西打包进去，缺啥包啥，否则设计就总会被这个糟心的系统版本困死」 | 打包要自带 loader/glibc/依赖库 |
| **「停，谁跟你说放弃CentOS7了」** | **CentOS 7.9 必须支持，且同一个产物也要能在新系统上跑**。这条曾被误读成"放弃 CentOS 7"，是上一轮最严重的一次误解 |
| 「其他就是导入额外的.f一起lint的能力，文件夹内的.v和.sv自动加入链接，GIT面板一方面，我还要在这个面板基础上支持SVN」 | `.f` 清单解析、目录内 `.v/.sv` 自动纳入、VCS 面板要 Git **和** SVN |
| 「清理当前工作树，重新整理并继续计划」 | 本套文档的起因 |
| 「现在这套是之前cellwork出bug整出来的烂摊子，我之后会重新开cellwork解决掉这些」 | 本轮**只整理交接，不再开新工**；下一轮用新 cellwork |

## 2. 已拍板的决策（不要再讨论，除非用户主动改）

| # | 决策 | 选择 | 理由/出处 |
| --- | --- | --- | --- |
| D1 | 技术路线 | **留在 Qt6 + KTextEditor**，不 fork Lapce | 量化对比见 [`appendix/q-vs-lapce-route-compare.md`](appendix/q-vs-lapce-route-compare.md)（Q 7.30 vs L 4.60） |
| D2 | `.sv` 语法定义 | **B′**：自建 `QRC_SYNTAX=OFF` 的 KSyntaxHighlighting（库内 0 条定义）+ 自家 LGPL-2.1+ 移植定义 | KDE 的 `systemverilog.xml` 是 GPL-3.0-or-later、`verilog.xml` 无许可。见 `docs/decisions/r2-license.md` |
| D3 | 语义高亮的底盘 | **R2 双引擎**：KSyntaxHighlighting 打底 + tree-sitter 叠加 | 需求 C13/C15/C16/C17 |
| D4 | 工程模型 | **M3 显式**：用户显式指定根目录与成员，自动发现只作初值 | 用户明确选择；避免大仓里瞎猜 |
| D5 | 折叠区域来源 | **A4(a)**：自家 XML 必须自带 `<folding>` 区域 | 不依赖 KDE 定义 |
| D6 | 打包形态 | **P1 AppImage 自包含 + 私有运行时** | 见 §1 原话 |
| D7 | 终端实现 | **自写 pty + VT**，不链 QTermWidget | QTermWidget 是 GPL-2.0+，本仓 MIT，链进去会改变整包许可 |
| D8 | v1 范围 | 文件树、命令面板、全局搜索、分屏、编辑器增强、终端、Git 面板、设置 UI、Verilog 工具链（Verible + SLANG + FIM） | 用户逐项确认 |

## 3. v1 明确不做

- 模态 Vim 键位
- DAP 调试
- WASI 插件
- 远程开发
- 自动发现作为工程模型的**默认**（M3 显式才是默认）

## 4. 三条红线（违反任何一条，整个方案就作废）

1. **CentOS 7.9 / glibc 2.17 必须能跑，且与新版系统共用同一个产物。** 这条曾被误解一次，代价是一整轮返工。
2. **许可必须干净。** 本产品 MIT；不能把 GPL 代码链进来（KDE 的 SystemVerilog 定义、QTermWidget 都属于这一类）。
3. **tree-sitter 只作为叠加，且必须能在运行期整体关掉、干净回退到 KSyntaxHighlighting。** 关掉之后不能剩下任何语义着色的残留。

## 5. 下一轮 cellwork 开工前要复述给用户的

- v1 范围还是不是上面那八项？有没有要砍的？
- CentOS 7 与新版共用同一产物这条还成立吧？
- `src/editor`（编辑内核）是继续做还是换路线？它现在是 0 实现（见 `02-status-matrix.md`）。
