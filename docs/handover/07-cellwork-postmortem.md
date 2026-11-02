# 07 · 上一轮 cellwork 复盘 —— 它到底哪里错了

> 用途：**下一轮重开 cellwork 之前必读**。这份不是抱怨，是故障清单 + 必须加的约束。
> 只写可观察的事实；能定位到文件/票号的都写出来，方便复核。原始快照见
> [`appendix/board-snapshot.md`](appendix/board-snapshot.md)（69 张票、13 个成员）。

## 1. 结论先说

**问题不在"任务太多"，在"完成的定义被跳过了"。**
上一轮把需求切成了 68 张票、拉了 13 个成员并行，交付速度不慢；但 **"什么叫做完"这件事从头到尾没有被强制**。
于是出现了三种同时存在的状态：

- 票面 `completed` 但只有作者自测（5 条 lane）
- 票面 `in_progress` 但一行产品代码都没有（`src/editor`，0/6）
- 票面之外：三条 lane 的代码在树里，**从来没被根构建编译过**（最贵的那个）

## 2. 十种可观察的故障模式

| # | 现象 | 证据 | 代价 |
| --- | --- | --- | --- |
| F1 | **幽灵票**：T2 无 owner、永远 pending、无法关闭、也不阻塞任何东西 | `board-snapshot.md` T2 行 | 看板永远不"干净"，`status` 无法作为进度判据 |
| F2 | **fenced 席位囤票**：Senior Engineer 进入 `checkpoint` 模式后被 fence，名下 **10 张**（T19/T24/T25/T27/T29/T30/T33/T34/T36/T37）无人能领 | 强制归档前 `cellwork_supervise` 输出 | 10 张票静默停摆，且没有任何自动回收机制 |
| F3 | **票面状态与事实脱钩**：T14 `in_progress` = 0/6；T17/T21/T22/T23/T35 `completed` 但无 verdict | `02-status-matrix.md` | 所有基于看板的排期都是错的 |
| F4 | **按需求条款切分**：68 张票横跨 13 个成员，每张票都对得上一条 C/A 条款，但没有一张对得上"用户能做什么" | `appendix/requirements-rev2.md` 的条款编号 vs 看板 | 交付了一堆"完成"的部件，拼不成产品 |
| F5 | **假绿**：`VEDIT_LANE_DIRECTORIES` 只列了 provider/kde，`src/terminal`、`src/syntax/grammars` 在树里但不在构建里；更早还发生过 provider **编译 0 个文件**而门禁全绿 | `evidence/t63-root-cmake-lanes/`、`evidence/build-registration/00-counterexample.txt` | 一整条 lane 的"通过"是假的 |
| F6 | **成员创建失败**：`Project Engineer` 两次 raise 失败；团队上限 13，失败会移除 roster 行但不占位 | journal | 容量账算不准，派工时才发现 owner 不存在 |
| F7 | **证据被删**：`evidence/m0-build/` 被成员删除，备份放 `/tmp`（按命令隔离）→ 不可恢复，只能重建 20 个文件 | `evidence/m0-build/REBUILD-INCIDENT.md` | 交付证据丢失一次，重建带 "rebuild after evidence-loss incident" 标记 |
| F8 | **数字漂移**：某报告称 +380 µs/+755 µs（+26%），日志实为 +464.58/+425.39 µs（+33.5%/+30.7%），低估 1.2–1.7× → 盲审 FAIL | `evidence/r2-c17/` v1/v2 对照 | 一次 FAIL + 一次返工（T58/T59/T65） |
| F9 | **评审开出后不返回**：T65（C17 复审）至今 `in_progress`，从未产生 verdict | `board-snapshot.md` T65 行 | 一个 FAIL 的修复无法关案 |
| F10 | **跨 lane 无主**：根 `CMakeLists.txt` 是所有人的前置，却不属于任何 lane；`src/app` 的面板注册同理 | `03-architecture.md` §6 G9 | 每条 lane 都对，拼起来装配缺失 |

## 3. 根因（三层）

1. **完成的定义是"票完成"，不是"可观测事实成立"。** 契约里缺少"外人能重跑的一条断言"，
   于是"我测过了"就成了完成判据。
2. **切片维度选错。** 按需求条款切，天然产生"部件全对、整机不动"的局面；
   装配工作（根构建、面板注册、链接、打包输入）不属于任何一条条款，于是谁都不做。
3. **没有"装配"这一类工作。** 看板只有 `work` / `review` / `requirement` / `repair` / `integration`，
   但没有一条规则要求"lane 落地时必须同时改根构建"，也没人拥有根构建。

## 4. 下一轮 cellwork 必须加的硬约束

> 这些是**给下一轮编排的输入**，建议直接写进契约模板与验收流程，而不是靠人记。

### C-1 契约只写可观测判据
每张 work 票的验收必须包含至少一条「外人能照着命令重跑、输出可判真假」的断言。
写不出来的票**不允许进图**（在 split 阶段就拒）。

### C-2 「构建通过」必须附 target 清单 + 一次证伪
贴 `exit 0` 不算。要有：实际编译到的 target 与磁盘产物、真实链接行（`ninja -t commands` / `nm` / `readelf`），
以及"放一行 `#error` 必须变红"的证伪记录。

### C-3 lane 的 done-definition 必须包含"进根构建 + 进产品"
一条 lane 不许以"我自己目录里 ctest 通过"结案。必须证明：
根 `CMakeLists.txt` 列了它 → 配置期日志能看到 `building lane directory <x>` → 产品 ELF 里能找到它的符号。

### C-4 评审票与工作票同时创建，verdict 缺失即未完成
不许"先做完再想起来要评审"。评审票在 split 阶段就与 work 票成对生成；
没有 verdict 的交付在看板上自动标成"未完成"，不允许 `completed`。

### C-5 owner 必须是活实例；每人同时最多 2 张在飞
角色名不是 owner。派工前先确认实例存在（F6）。
fenced / checkpoint 席位名下的票要**有回收规则**（例如超过 N 分钟无心跳自动 reassign），否则就是 F2。

### C-6 禁止幽灵票
每张票创建时必须有 owner 和依赖答案（`blocked_by` 或 `independent`）；
没有 owner 的票在创建时就应当被拒，而不是留成一个永远 pending 的 T2。

### C-7 证据只落工作区内
`/tmp` 按命令隔离，**不是备份**。任何"备份一下"的动作必须落在 workspace 内且被 .gitignore 之外可见（F7）。

### C-8 报告里的数字必须由脚本产出
不许手抄。所有性能/尺寸数字要么由脚本写进证据文件、正文只引用文件名与行号，要么不算（F8）。

### C-9 里程碑在冻结的树上重采证据
每个里程碑结束：固定 HEAD → 重跑 → 证据带采集时间 + `git rev-parse HEAD` + `git status` 干净度。
避免"证据比代码老"和"拿红日志当绿证据"。

### C-10 切片维度：按用户能做的事切
一片结束必须是"用户能亲手做成一件事"，不是"条款 N 全部实现"。
参考 [`../plans/v2-slices.md`](../plans/v2-slices.md)。

### C-11 装配是一等公民
根构建、面板注册、链接、打包输入这四件事要有**明确 owner**（或作为每条 lane 的收尾项），
不能默认"总有人会做"（F10）。

### C-12 证据丢失视为事故
删任何证据目录前先 `git ls-files` 确认是否已入库；未入库的证据目录不许 `rm -rf`。

## 5. 我自己犯的错（同样要写进来）

| # | 错误 | 纠正 |
| --- | --- | --- |
| M1 | 把用户「做私有运行时打包」误读成「放弃 CentOS 7」，还写进了需求 | 用户当场叫停：「停，谁跟你说放弃CentOS7了」。已作废该条、给 planner 发更正、需求升到 rev 2、写记忆更正 |
| M2 | 相信了成员报告里的性能数字，没有先核对日志 | 盲审 FAIL，返工两轮（T58/T59/T65）。此后规定数字必须由脚本产出（C-8） |
| M3 | 收口时 `rm -rf build/gui-spike`，连带删掉 69 个**已入库**文件 | `git checkout` 全量恢复。此后规定删前先 `git ls-files`（C-12） |
| M4 | 让 5 条 lane 带着"只有自测"的状态走了很久没有开评审 | 本轮已把它们全部记为未完成（`02-status-matrix.md`） |
| M5 | 把"用户确认的切片方向"搁置太久，直到工作树炸了才动手 | 本轮已重排为三片计划 |

## 6. 一句话给下一轮

> **不要再用"票完成"当进度。用"用户能不能亲手做成这件事"当进度，用"外人能不能重跑这条断言"当完成。**
