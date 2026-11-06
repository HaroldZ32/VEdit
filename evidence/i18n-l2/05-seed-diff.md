# T38 — 与 seed（`target/tmp/t22-precheck/g7-seed.md`）的差异对账

Seed：`target/tmp/t22-precheck/g7-seed.md`，349 行，sha256
`ed76171a96351dc197a83bd416f24eaf76908c68b7deda458a60b2e9bc16b162`（与 captain 记录一致）。
**用途**：只作交叉核对；本轮自己重新枚举，未把 seed 当答案（seed §345 自述：全程只读、未编译、
未逐条佐证）。

| 项 | seed | 本轮实测 | 差异与理由 |
|---|---|---|---|
| A 类 | 102（5 上下文） | **102**（QObject 3、FindReplaceBar 23、SearchPanel 27、SettingsPanel 37、FileTreeWidget 12） | 一致；lrelease 的 101 已解释为「1 条带文本 + 101 条无文本」 |
| B 类 | **77 call-site**（14 文件） | **151 call-site**（78 + 73，共约 22 文件） | seed 是启发式只读清单：漏掉面板本地文本（表头/空态/右键菜单/片段名/工具提示等）。第一批 77→78 的差在交接 `se-shell-1.md` 已逐条解释（HTML 片段合并、`All files (*)` 拼写、`spec.title` 非字面量等）；第二批 73 条是 seed 之外的新发现（`03-b-reconciliation.md` §2） |
| 466 语料 | 历史参考 | 未作为基线 | 466 来自不可重现的启发式清单；本任务用 `.ts ∪ --any-literal` |
| 抽取锚点 | — | 894 次/780 串是 T21 前快照 | 当前树无法复现（默认模式 0 候选）；本轮实测 `candidates=1456 unique=1268`，如实记录 |
| C 类 | vendor 路径 | 已 vendor + 装载 + 打包 | sha256 与 captain 记录一致 |
| 「可见」依据 | 调用点 | 调用点 + UI sink 窗口 + 探针文本断言 | 本轮的可见性判据更强（`03-b-reconciliation.md` §1） |
