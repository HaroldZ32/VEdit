# 看板快照（cellwork 崩掉那一刻的原样）

> 这份快照是**只读存档**，不是待办清单。它的用途只有一个：
> 下一次重开 cellwork 时，把"哪些票其实早就完成了、哪些票是幽灵、哪些票挂着
> in_progress 其实一行代码都没有"这三件事对齐，不要再从零重切一遍。

- 团队：`team-20261031014509049-t5fa-002`  plan：`plan-20261031014509037-t5fa-001`  phase：`active`
- 成员 13 人 · 任务 69 张 · 批准于 2026-10-31T01:45:09.260Z
- 状态分布：完成 **35** · 待办 29 · 进行中 5

## 成员

| # | 成员 | 角色 | 终止时状态 |
|---|---|---|---|
| M1 | cell-planner | Planner | inactive |
| M2 | cell-splitter | Lead | inactive |
| M3 | Deep Worker | Deep Worker | running |
| M4 | Senior Engineer | Senior Engineer | inactive |
| M5 | Junior Engineer | Junior Engineer | inactive |
| M6 | Reviewer | Reviewer | inactive |
| M7 | Architect | Architect | running |
| M8 | Qt Build Engineer | — | inactive |
| M9 | Probe Engineer | — | inactive |
| M10 | Workspace Engineer A | — | running |
| M11 | Workspace Engineer B | — | running |
| M12 | Workspace Engineer C | — | inactive |
| M13 | Overlay Engineer | — | running |

## 进行中（5 张 —— 注意：票面状态不可信，见 02-status-matrix.md）

| 票 | 归属 | 类别 | 阻塞于 | 标题 |
|---|---|---|---|---|
| T14 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | T13 | KTextEditor 编辑内核：打开/原子保存/查找替换/跳行/撤销重做/多光标（G2, A1b/A1d/A1e） |
| T18 | Overlay Engineer | work | T15, T7 | tree-sitter overlay 着色（宿主侧 range 接口）+ 无双重着色 A1g |
| T21 | Workspace Engineer A | work | T13 | 文件树面板：打开目录、展开/折叠、双击打开、新建/重命名/删除与磁盘一致（A2a） |
| T22 | Workspace Engineer B | work | T13 | 命令面板 Ctrl+P + 模糊文件跳转，10k 文件 200ms 内（A2b） |
| T65 | Architect | review | — | 复审：C17 overlay v2（官方镜像复跑）——T40 FAIL 的修复是否真闭合 + 能否关案 |

## 待办（29 张）

| 票 | 归属 | 类别 | 阻塞于 | 标题 |
|---|---|---|---|---|
| T2 | — | work | T1 | Measured task split from the agreed requirements |
| T19 | Senior Engineer | work | T14, T15, T9 | 折叠与面包屑：按 r2-fold-probe 结论落地（叠加或替换折叠视图），随 tree-sitter 开关可观测变化（A1c/A1f） |
| T20 | Junior Engineer | work | T18 | 运行期 tree-sitter 开关与干净回退 KSyntaxHighlighting（A1h/C15） |
| T24 | Senior Engineer | work | T13 | 工程模型：根目录+成员+持久化+自动发现初值（C8/A3a） |
| T25 | Senior Engineer | work | T13 | .f 文件清单解析：+incdir+/+define+/嵌套 -f/注释，坏条目可诊断（C9/A3b） |
| T26 | Deep Worker | work | T24 | Verible LSP 接入工程文件集：诊断/跳转/引用/重命名/格式化/大纲（A3c/A3e） |
| T27 | Senior Engineer | work | T24 | SLANG 深检消费同一生效文件集（A3c） |
| T28 | Junior Engineer | work | T24 | FIM 补全：默认关闭、未配置即零网络（A3e/A6） |
| T29 | Senior Engineer | work | T25, T24, T26, T27 | 工具链刷新编排：文件集变更→SLANG/Verible 重解析，状态可观测（C11/A3d） |
| T30 | Senior Engineer | work | T13 | VCS 后端接口抽象 + stub 后端接入证明（C10/A4b） |
| T31 | Junior Engineer | work | T30 | Git 后端：当前分支/状态、变更列表、单文件差异、刷新 |
| T32 | Junior Engineer | work | T30 | SVN 后端：当前修订/状态、变更列表、单文件差异、刷新 |
| T33 | Senior Engineer | work | T30 | VCS 面板（后端无感）+ 非仓库空状态（A4a/A4c） |
| T34 | Senior Engineer | work | T13 | 设置 UI：主题切换持久化、键位编辑与冲突提示、坏配置诊断回退（A5a/A5b） |
| T36 | Senior Engineer | work | T14, T19, T20 | 编辑器验收自动化：A1a–A1h 可测判据与性能基线 |
| T37 | Senior Engineer | work | T29, T26, T27, T28, T33, T34, T35, T24, T25, T21, T22, T23, T31, T32 | 工作区/工程/工具链/VCS/设置验收自动化：A2–A5 + A6 离线 |
| T38 | Deep Worker | integration | T12, T36, T37 | 交付集成：单文件 AppImage 终版 + A0 回归 + A6/A7 文档与首启提示 |
| T45 | Reviewer | review | T14, T19 | 评审：编辑内核 + 折叠面包屑（A1b–A1f 实现侧） |
| T46 | Architect | review | T15, T16, T17, T18, T20 | 评审：SyntaxProvider/grammar/overlay/回退 与 C16/A1g 判据 |
| T47 | Reviewer | review | T21, T22, T23 | 评审：文件树/命令面板/全局搜索（A2a–A2c） |
| T48 | Reviewer | review | T24, T25 | 评审：工程模型与 .f 解析（C8/C9/A3a/A3b） |
| T49 | Reviewer | review | T26, T27, T28, T29 | 评审：Verible/SLANG/FIM/刷新编排（A3c/A3d/A3e/A6） |
| T50 | Reviewer | review | T30, T31, T32, T33 | 评审：VCS 抽象与 Git/SVN 后端与面板（A4a–A4c） |
| T51 | Reviewer | review | T34, T35 | 评审：设置 UI 与终端面板（A5a/A5b + G4 终端） |
| T52 | Reviewer | review | T36, T37 | 评审：验收自动化判据（A1a–A1h, A2–A5, A6） |
| T53 | Reviewer | review | T38 | 评审：交付集成（单文件 AppImage、A0 回归、A6/A7、许可一致性） |
| T66 | Qt Build Engineer | repair | — | 修 T43 发现：F1 重采构建日志 + F2 零字节日志 + F3 数字 + F4 事故披露，并写入门禁证据时间规则 |
| T68 | Probe Engineer | repair | — | 修 T44 M1：外壳证据新鲜度（08/03 重采 + 镜像出处 + 自查清单） |
| T69 | Junior Engineer | work | — | 工作树盘点：本仓 git 实况（分支/远端/提交数/未跟踪/构建产物/体积/证据与源码边界）——只写 evidence/tree-cleanup/，不删不改 |

## 已完成（35 张）

| 票 | 归属 | 类别 | 阻塞于 | 标题 |
|---|---|---|---|---|
| T1 | d3cff8ca-2d9e-4b06-ad6c-cfe48240d072 | requirement | — | Requirements interview and agreed requirements document |
| T3 | d3cff8ca-2d9e-4b06-ad6c-cfe48240d072 | requirement | — | requirements rev2：写入 R2 双引擎（tree-sitter 叠加）并 update |
| T4 | 54698d43-18a2-4ff4-9a68-18596893c80d | work | — | rev2 拆分：按 requirements rev 2 提交任务图（cellwork_split submit） |
| T5 | d3cff8ca-2d9e-4b06-ad6c-cfe48240d072 | work | — | 证据版对比：Q（Qt+KTextEditor）vs L（fork Lapce），含 Floem IME / 许可 / 打包 / 复用成本 |
| T6 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | — | R2 取证：tree-sitter Verilog/SV grammar 身份+commit+许可（含 C7 .sv 处置） |
| T7 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | — | R2 前置：KTextEditor 宿主侧 range/semantic-attribute overlay 接口可行性（C17） |
| T8 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | — | R2 前置：SyntaxProvider 接缝 + 编译期 grammar 选择 + 运行期开关，用第二个 grammar 证明可换（C15/C18） |
| T9 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | — | R2 前置：KTextEditor 折叠接口能否被外部 range 驱动（Open Q3） |
| T10 | Qt Build Engineer | work | — | M0 构建底座：Qt6/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter 版本锁定 + CMake 骨架 + 闸门改造 |
| T11 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | T10 | M0 私有运行时：AppImage 自带 loader/glibc/Qt6/KF6/KTextEditor/KSyntaxHighlighting/tree-sitter（C2/C6/C18） |
| T12 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | T11 | M0 双宿主闸门执行：CentOS 7.9 / Ubuntu 24.04 或 Rocky 9 / base-only / 无 GPU（A0a/A0b/A0d/A0e） |
| T13 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | T10 | Qt6 应用外壳：主窗口/菜单/停靠布局/水平+垂直分屏/主题加载（G1/G4） |
| T15 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | T10, T8 | SyntaxProvider 生产化 + C16 单一 owner 仲裁（C15/C16/C18） |
| T16 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | work | T10, T6 | KSyntaxHighlighting 底座数据：私有路径加载、verilog 随包、.sv 按许可决定（C6/C7） |
| T17 | Probe Engineer | work | T15, T6 | Verilog/SystemVerilog tree-sitter grammar 绑定 + queries（highlights/folds/structure），许可执行侧 |
| T23 | Workspace Engineer C | work | T13 | 全局搜索：命中文件+行号、点击跳转、空状态（A2c） |
| T35 | 5529019a-2bd3-42d2-8404-39a72fa3c21b | work | T13 | 终端面板：本地 shell 会话、可停靠、可关闭 |
| T39 | 3c04672b-fae2-4516-87ec-8f26152ca92e | review | T6 | 评审：R2 许可取证报告（grammar 身份/commit/许可 + C7 .sv 口径） |
| T40 | 255031d6-8861-4bc1-b61e-3b2cc8ff4eb0 | review | T7 | 评审：C17 overlay 接口可行性 probe 的结论与测量 |
| T41 | 255031d6-8861-4bc1-b61e-3b2cc8ff4eb0 | review | T8 | 评审：C15 接缝可替换性 probe（第二 grammar 换装证明） |
| T42 | 3c04672b-fae2-4516-87ec-8f26152ca92e | review | T9 | 评审：KTextEditor 折叠接口 probe 结论（叠加 vs 替换折叠视图） |
| T43 | 3c04672b-fae2-4516-87ec-8f26152ca92e | review | T10, T11, T12 | 评审：M0 构建底座 + 私有运行时 + 双宿主闸门（A0a–A0e） |
| T44 | 3c04672b-fae2-4516-87ec-8f26152ca92e | review | T13 | 评审：Qt6 应用外壳与分屏（G1/G4 骨架） |
| T54 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | — | ADR 收口：D2 裁决=B（移植 LGPL 定义）并写执行口径 |
| T55 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | repair | T54 | 修 T6 评审发现：M1 证据覆盖 + N1-N4（docs-only） |
| T56 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | T55 | 许可风险：KSyntaxHighlighting 把 451 定义（含 GPLv3+ 的 sv）编进 .so，方案 B 前提受质疑 |
| T57 | 54698d43-18a2-4ff4-9a68-18596893c80d | work | — | 契约补充：折叠 A1/A2/A3/A4 四条硬约束（AMENDMENTS.md） |
| T58 | 46eaff82-654d-4e64-bb9c-2b2d526251ca | repair | — | 修 T40 评审 FAIL：F1 数字对不上日志 + F3/F4/F5 + 版本依赖声明（docs-only） |
| T59 | Probe Engineer | work | T58, T10 | C17 探针在官方锁定镜像上复跑（v2，含 v1/v2 对照表） |
| T60 | 71ef3b4d-33b2-40f3-b573-1dbea782dfe5 | work | — | B′：自建 QRC_SYNTAX=OFF 的 KSyntaxHighlighting（库内 0 定义）+ CLI 0.27.1 锁定 |
| T61 | 71ef3b4d-33b2-40f3-b573-1dbea782dfe5 | work | — | 定案：KSH 白名单注入形态（F1 const_cast vs F2 XDG_DATA_DIRS），含宿主定义隔离实验 |
| T62 | 71ef3b4d-33b2-40f3-b573-1dbea782dfe5 | repair | T61 | 补件：重建被误删的 evidence/m0-build/（优先 08-locked-package-versions.txt 与 01-image-facts.txt） |
| T63 | 71ef3b4d-33b2-40f3-b573-1dbea782dfe5 | work | — | 根 CMake 通用纳入：src/syntax/{provider,kde} 等领域目录（含反例验证覆盖真生效） |
| T64 | Deep Worker | work | T12 | T11 收包改为 F2：AppRun 双 XDG 变量 + 禁 syntax-bundled + 运行时白名单守卫（C1–C8） |
| T67 | e0eab12c-1004-4ff9-9b5f-275c0aa27d03 | work | — | M0 结案：delivery 版 AppImage 跑双宿主矩阵（含宿主定义隔离断言） |
