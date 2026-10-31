# VEdit 交接文档

> 交接时间：**2026-10-31 16:35 (+08:00)** · 分支 `feat/qt6-ktexteditor-migration` · HEAD `4775bee`
> 上一轮编排（cellwork）已强制归档，原因与故障模式见 [`07-cellwork-postmortem.md`](07-cellwork-postmortem.md)。
> **这套文档是给人看的，不是给流程看的**：它优先写清「什么真的是真的」，其次才写「还差什么」。

## 一句话现状

> **一个能在 CentOS 7.9（glibc 2.17）和现代发行版上跑起来的单文件 AppImage 已经交付并被四台不同宿主机验证过；
> 但它跑的是 M0 冒烟程序，不是编辑器产品。产品外壳（窗口/菜单/6 个停靠面板/分屏/主题）已经能构建；
> 而 KTextEditor 编辑内核、tree-sitter 着色、VCS 面板、设置 UI 都还没有接进产品。**

如果你是接手人，先读这三段，再决定从哪份文档进去：

1. [`02-status-matrix.md`](02-status-matrix.md) —— **哪条 lane 是真的、哪条是假的**。这张表是本文档的核心，其余文档都在服务它。
2. [`03-architecture.md`](03-architecture.md) —— 目录、接缝、装配、构建链、打包链，以及 **§6 的 10 条装配缺口**。
3. [`06-open-issues.md`](06-open-issues.md) —— 欠账清单和需要用户拍板的事。

## 文档地图

| 文件 | 里面是什么 | 什么时候读 |
| --- | --- | --- |
| [`01-objective-and-requirements.md`](01-objective-and-requirements.md) | 用户原话目标、已拍板的决策、v1 范围与红线 | 动手前必读，防止走回头路 |
| [`02-status-matrix.md`](02-status-matrix.md) | 逐 lane 的四层判据（实现/证据/独立评审/进产品） | 判断"能不能算完成"时 |
| [`03-architecture.md`](03-architecture.md) | 目录、接缝 C15–C18、装配、构建链、打包链、10 条缺口 | 要改代码/改构建时 |
| [`04-hard-facts.md`](04-hard-facts.md) | 环境、许可、KTextEditor 硬限制、不可信判据 | 设计任何方案之前 |
| [`05-build-and-verify.md`](05-build-and-verify.md) | 所有命令、证据采集规则、门禁的真实语义 | 要构建/取证/验收时 |
| [`06-open-issues.md`](06-open-issues.md) | 欠账 + 待拍板 | 排下一轮计划时 |
| [`07-cellwork-postmortem.md`](07-cellwork-postmortem.md) | 上一轮怎么崩的、下一轮必须加什么约束 | 重开 cellwork 之前**必读** |
| [`08-evidence-index.md`](08-evidence-index.md) | `evidence/` 每个目录证明什么、属于新产品还是旧产品 | 要找证据时 |
| [`appendix/requirements-rev2.md`](appendix/requirements-rev2.md) | 需求权威文本（rev 2，逐条 C1–C18 / A1–A7 / G1–G4） | 有争议时的唯一裁判 |
| [`appendix/amendments-a1-a5.md`](appendix/amendments-a1-a5.md) | 五条硬约束修正（与需求同权） | 做折叠/编辑器时 |
| [`appendix/board-snapshot.md`](appendix/board-snapshot.md) | 上一轮 69 张票的原样快照 | 想知道"某张票当时说了什么" |
| [`appendix/q-vs-lapce-route-compare.md`](appendix/q-vs-lapce-route-compare.md) | Qt 路线 vs fork Lapce 的量化对比（结论：留在 Qt） | 有人想改路线时 |

## 树的现状（交接那一刻）

- `git status` 有**未提交改动**：`CMakeLists.txt`、`src/app/{CMakeLists.txt,main.cpp}`（根构建登记与终端接线）、
  `evidence/m0-build/build-logs/*`（被 `build-qt.sh` 重写的构建日志）。
- 未跟踪：`docs/handover/**`、`evidence/build-registration/**`、`evidence/edit-core/**`、`src/editor/probes/**`、`target/`（已忽略）。
- 体积 **392 M**（收口前 16 G），明细见 `evidence/tree-cleanup/REPORT.md`。
- **唯一交付物**：`packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage`，65,673,720 B，
  sha256 `74bd6aae2af9aefc8a2200d5ff66b1868c3e8281fb77de21f7859c5cafb33faa`。收口后重跑自检仍 PASS。

## 接手的第一步该做什么

1. **把未提交的接线提交掉**（G9）。它在工作树里能通过构建，一次干净 checkout 就会退回"假绿"。
2. **重跑一次 `bash scripts/build-qt.sh`**，把 target 清单和 ctest 结果写进 `evidence/build-registration/`，并带上采集时间与树状态。
3. 然后按 [`03-architecture.md`](03-architecture.md) §6「收口顺序」往下走。

**不要**先做新功能。当前最贵的问题不是缺功能，是**构建链和产品之间断着**：
`syntax` 三个库编译了但没链进 `vedit-app`，`vedit-app` 里用的还是 `QPlainTextEdit` 的 `DemoEditorProvider`，
打包链装的还是 M0 冒烟程序。
