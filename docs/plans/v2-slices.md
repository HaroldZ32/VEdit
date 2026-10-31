# VEdit v2 切片计划（2026-10-31 重排）

> 起因：上一轮按「需求条款」切任务，切出 68 张票、13 个成员、三条 lane 交付了却
> 从未被根构建编译。用户判定这条路不对，要求收口重排。本文件是重排后的唯一执行依据。

## 0. 重排前提（这一轮已经做完的事）

| 事项 | 结果 |
| --- | --- |
| 工作树体积 | 16 G → **392 M**（删的全部是可重建构建产物） |
| 未跟踪文件 | 1017 个、238 M → **全部入库**，`git status` 干净 |
| 入库方式 | 分支 `feat/qt6-ktexteditor-migration`，5 个提交（build / feat / docs / test / chore） |
| 交付物 | `packaging/runtime/out/VEdit-0.1.1-x86_64.AppImage` 保留，65,673,720 B，sha256 `74bd6aae…` **校验一致** |
| 误删纠正 | 删 `build/gui-spike` 时连带删掉 69 个已入库文件，已 `git checkout` 全量恢复（现 800 K） |
| 权限残留 | `target/` 下 151 M 由容器以 `nobody` 身份写死，宿主无 sudo，改用容器 root 删除 |
| 编排模式 | cellwork 已停（强制归档），改为 **captain 直管 + 可续子代理** |

## 1. 现状（按证据说话，不按票面状态说话）

### 1.1 真闭环（有实现 + 有可复查证据）

- **M0 构建底座与私有运行时**：Qt 6.10.2 / KF 6.24.0 锁定（22 个 deb + sha256 + 指纹
  `c97a2566…`），AppImage 在 **4/4** 宿主上通过（CentOS 7.9 glibc 2.17 / Ubuntu 24.04 /
  base-only（宿主 libQt6=libKF6=libX11=libfuse2=0）/ 无 GPU 无 GL），`libc=/app/lib/libc.so.6`，
  窗口可见性以像素差证死（`compare -metric AE = 307200`）。
- **许可链**：tree-sitter-verilog `4457145e…`（MIT）；KDE `systemverilog.xml`（GPL-3.0-or-later）
  与 `verilog.xml`（无许可）按 sha256 拉黑；自建 `QRC_SYNTAX=OFF` 的 KSyntaxHighlighting（库内 0 定义）；
  自家 LGPL-2.1+ 移植定义带 `<folding>` 随包。`verify.sh` **51 passed / 0 failed**，含 3 条反向对照。
- **C15 / C17 / C18 接缝取证**：四配置换装证明 `SyntaxProvider` 可替换；持有 C17 overlay 的
  v1+v2 两份实测（v2 在官方锁定镜像上复跑）。
- **已交付的面板**：应用外壳（73 项自测 + 4 张截图）、KSyntaxHighlighting 数据层、
  tree-sitter grammar 绑定（三配置各 **79/79**）、文件树、命令面板、全局搜索、终端
  （自写 pty + VT，零新依赖，因为 QTermWidget 是 GPL-2.0 而本仓是 MIT）。

### 1.2 假绿（代码在树里，从没被编译过）——最高优先级

`CMakeLists.txt` 的 `VEDIT_LANE_DIRECTORIES` 只登记了 `src/syntax/provider` 和 `src/syntax/kde`。
以下三条 lane **存在但不在构建里**：

- `src/terminal`（终端整条 lane）
- `src/syntax/grammars`（tree-sitter grammar，T17 的 79/79 是它自己的 CMake 跑的，不是根构建）
- `src/syntax/overlay`（tree-sitter 着色叠加）

外加 `src/app` 还差终端面板的 `setFactory("terminal", …)` 与 `vedit_terminal` 链接。
**这正是项目已经栽过一次的同一类事故**（T63 之前 `src/syntax/provider` 编译 0 个文件而闸门全绿）。
不修这一条，后面每一条“通过”都不可信。

### 1.3 有实现、没判据（需要独立评审，不能算完成）

文件树 T21、命令面板 T22、全局搜索 T23、终端 T35、grammar T17 —— 都有自测日志，
但**没有一份独立评审 verdict**。另外 C17 复审（T65）开出去之后从未返回。

### 1.4 未开工

编辑内核收口（T14，设计稿在 `evidence/edit-core/DESIGN.md`）、工程模型、`.f` 清单解析、
Verible LSP、SLANG 深检、FIM 补全、折叠与面包屑、overlay 运行期开关、VCS 抽象 + Git + SVN + 面板、
设置 UI、最终打包、验收自动化。

## 2. 三片（按用户能看见的东西切，不按条款切）

### Slice 1 ——「能用了」

**做完之后用户能亲手做**：在一个真实 Verilog 工程目录上打开 VEdit → 文件树里双击 `.sv` →
语法高亮正确 → 改几行 → Ctrl+S 原子保存（不丢权限位、不写半截文件）→ 工程按 `.f` 清单认出全部源文件
→ Verible 诊断显示在编辑器里 → 跳转到定义能跳。

| 内容 | 前置 |
| --- | --- |
| 根构建登记 + 终端接入（1.2 的假绿） | 无，立即可做 |
| 编辑内核收口：打开/原子保存/查找替换/跳行/撤销重做/多光标 | 设计已完成 |
| 工程模型：根目录 + 成员 + 持久化（**显式 M3**，不做自动发现当默认） | 外壳 |
| `.f` 清单解析：`+incdir+` / `+define+` / 嵌套 `-f` / 注释，坏条目可诊断 | 工程模型 |
| Verible LSP：诊断 / 跳转 / 引用 / 重命名 / 格式化 / 大纲 | 工程模型 + `.f` |
| 折叠与面包屑（含「选区被误折叠」的 A1 修正） | 编辑内核 + C17 结论 |
| 运行期 tree-sitter 开关与干净回退 | overlay 落地 |

**验收动作**（用户 5 分钟内可跑）：拿一个带 `.f` 的真实工程，`bash scripts/build-qt.sh` 出二进制 →
打开 → 双击打开 3 个文件 → 保存 → 看诊断出现。必须显示：文件树有内容、高亮有颜色、诊断有行号。

### Slice 2 ——「像 Lapce 了」

**做完之后用户能亲手做**：一份文件里 tree-sitter 的语义着色与 KSyntaxHighlighting 同时存在但**不双重着色**、
关掉 tree-sitter 后颜色干净回退、折叠与面包屑跟着语义走、Ctrl+P 模糊跳转 10k 文件 200ms 内、
全局搜索能点进命中行、VCS 面板能看分支与逐文件 diff。

| 内容 | 前置 |
| --- | --- |
| tree-sitter overlay 着色收口（C17 v2 结论落地） | C17 复审返回 |
| 折叠 + 面包屑走语义 | 编辑内核 |
| 文件树 / 命令面板 / 全局搜索 收口 + 独立评审 | 已有实现 |
| VCS 抽象 + Git 后端 + SVN 后端 + 面板（后端无感、非仓库空状态） | 外壳 |

### Slice 3 ——「完整交付」

**做完之后用户能亲手做**：下载**一个** AppImage，在 CentOS 7.9 和最新 Ubuntu 上都能双击打开，
设置里换主题重启后还在、键位冲突有提示、坏配置会自诊断回退。

| 内容 | 前置 |
| --- | --- |
| 设置 UI：主题持久化、键位编辑 + 冲突提示、坏配置回退 | Slice 2 |
| FIM 补全：默认关闭，未配置即零网络 | 工程模型 |
| 最终打包：单文件 AppImage + 四宿主回归 | Slice 1–2 全绿 |
| 验收自动化：A1a–A1h / A2–A5 / A6 的可执行判据 | 各片 |

## 3. 现在的关键路径（按顺序，不并行开工）

1. **根构建登记**：`src/terminal`、`src/syntax/grammars`、`src/syntax/overlay` 进 `VEDIT_LANE_DIRECTORIES`；
   `src/app` 加终端的 `setFactory` 与链接；**用反例证明生效**（放 `#error` 必须构建失败），
   再跑真构建并把 target 清单写进证据。**这一条不完成，后面所有“通过”都不算数。**
2. **五份独立评审**：T17 / T21 / T22 / T23 / T35 —— 判据是契约 + 证据，不是实现。
3. **编辑内核收口**（Slice 1 主干），随后工程模型 → `.f` → Verible。
4. Slice 2、Slice 3 按上面表内的前置串起来。

## 4. 这次明确不做

- 不做自动发现作为默认工程模型（用户选了 **M3 显式**）。
- 不引入 QTermWidget（GPL-2.0，会改变整包许可）。
- 不用 binary sha256 当产物身份判据（同源码不同构建树哈希不同）。
- 不用 docker image id 当身份判据（同一 Dockerfile 每次 id 不同）。
- 不重写已发布历史（AGENT.md §6）。

## 5. 作业规则（从这一轮的事故里提炼，全部约束我自己的行为）

1. **交付一件东西，必须同时交一条能被外人重跑的判据**；只有自测日志的不算完成。
2. **凡是“构建通过”，必须贴出实际编译到的 target 清单**，并证伪一次（放 `#error` 让它红）。
3. **删任何东西之前先问“这是不是唯一副本”**；`/tmp` 是隔离的，备份必须落在工作区内。
4. **用户原话逐字转发，不要折回预设选项**（上一轮把「私有运行时」误读成「放弃 CentOS 7」就是这套错）。
5. **契约只写可观测判据**，写不出判据的票不许进图。
6. 一批工作结束就在冻结的树上重采一次证据，别让证据比代码老。
