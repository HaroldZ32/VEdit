# 契约补充 AMENDMENTS（rev 1）— 折叠链路的 4 条硬约束 + 1 条引用修正

> **效力声明**：本文件由 cell `cell-20261031T014507403Z` 的 captain 授权、由 cell-splitter 执笔，
> **与 split revision 2 的冻结契约具有同等效力**。它**只增加约束，不放松任何既有条款**；
> 与冻结契约正文冲突时，**以更严格的一条为准**。本文件**不修改**冻结契约正文，也不升
> `requirements.md` 的 revision（不制造 STALE）。
>
> **权威来源**：`evidence/r2-fold/decision.md`（sha256 `6ff64838…0ac`，KTextEditor 6.24.0 实测）
> 与 T42 评审记录 `rec-20261031T030614-c4da7a`（verdict PASS，findings M1/M2/M3）。
> 探针命令与输出：`evidence/r2-fold/02-run.log`、`00-pins.txt`、`09-result.txt`。
>
> **被补充的契约（基线快照，sha256 为写入本文件时磁盘上的冻结版本）**：

| 契约 | Board task | Loop | sha256 |
|---|---|---|---|
| `contracts/r2-fold-probe.md` | `T9` | `loop-20261031T023040-16299d` | `ab92d4a1e5571edeb1ea919c69082ea787d798fc119e196c425cc3ac537f061f` |
| `contracts/edit-folds.md` | `T19` | `loop-20261031T023040-a905c1` | `3de2c41fbecd5238b1221f45342f16347696850edda11a9b850eea5fdc1e5408` |
| `contracts/acc-editor.md` | `T36` | `loop-20261031T023040-6a8a19` | `fcaacc7602d3a761df44a7a927f6f3c249994acfac549b7e4aeca9999cde2bb0` |
| `contracts/syn-kde-data.md` | `T16` | `loop-20261031T023040-bfbfb8` | `31377818e7e43f7185415f855665e08a30672172640ad1992e1d38841591d75f` |

> **给验证者的硬要求**：评审上述任务时，本文件与对应冻结契约**必须同时**出现在
> `mpd_verify_record.sources` 中；缺本文件视为证据不全，不得判 PASS。
>
> **术语（下文一律按此义使用）**
> - **注入** = 通过 `KTextEditor::Editor::commands()` 的 `fold` / `tfold` / `unfold` 命令影响折叠。
> - **全量重建** = 对我们自己登记的全部 N 条折叠重新执行一遍 `unfold` + `fold`。
> - **registry** = 应用侧（我们自己维护）的折叠登记表；它**不是** KTextEditor 的折叠模型。
> - **判据** = 可执行、可复跑、有实测输出的断言；人工描述不算判据。

---

## A1 — 非法 range 会折叠用户选区（修 `r2-fold-probe` / `edit-folds`）

**事实（KDE v6.24.0 `src/utils/katecmds.cpp:258/261` 原文）**：`fold` / `tfold` 的 range 参数在
`range.isValid()` 为假时**回落到 `v->selectionRange()`**——即非法 range 不是 no-op，而是
**折叠用户当前选区**。`evidence/r2-fold/decision.md` §3.3 记录的 `empty_range_ok=0` 只在
“当时选区为空”这一前提下成立，**不可推广**；同一次测试里 `inverted_range_ok=1`（倒置 range
返回成功但无可见效果）进一步证明**命令返回值不能作为“折叠是否发生”的校验手段**。

**条款（A1.1）**：任何调用 `Command::exec(view, "fold"|"tfold", …)` 的代码路径，
**必须在调用前自行校验 range**，四项全真才允许调用：

1. `start` 与 `end` 均非空/非无效（不得是默认构造的 `Range`）；
2. `start <= end`（起止有序；倒置一律拒绝）；
3. `start.line()` 与 `end.line()` 均在 `[0, document->lines()-1]` 内（越界拒绝）；
4. range 至少覆盖一行（不得为零行 range）。

**条款（A1.2）**：**禁止**依据 `Command::exec` 的返回值判定“已折叠/未折叠”；
校验失败时**不得**写入 registry，并须留下可诊断日志（被拒原因 + 原始 range + 当时选区）。

**条款（A1.3，归口 `edit-folds`）**：`edit-folds` 必须交付一条**回归用例**，证明
「传入非法 range 时，用户当前选区不会被折叠」。用例至少覆盖三种非法输入：空 range、倒置 range、
越界 range；并且**测量时必须设置非空选区**（否则测不出回落行为）。

**验收判据（A1.4）**：

- 用例准备：选中第 20–25 行形成非空选区，视口记录 `firstDisplayedLine(RealLine)` 与
  `firstDisplayedLine(VisibleLine)` 差值、`verticalScrollBar()->maximum()`（下文记 `scroll_max`）；
- 分别注入空/倒置/越界 range，每次注入后断言：上述观测值与注入前**完全相同**（无隐藏行增加），
  选区文本仍全部可见；
- 同时记录三次 `Command::exec` 的返回值（倒置应为 `true`），以证明 A1.2 的必要性；
- 输出落 `evidence/edit-folds/`，含原始日志与断言计数。

> 归口说明：`r2-fold-probe`（T9）已评审关闭，**不重开**；其 `decision.md` §3.3 的
> `empty_range_ok=0` 结论由本 A1 取代/限定，溯源时以本条款为准。

---

## A2 — 折叠重建的时机与性能上限（修 `edit-folds` / `acc-editor`）

**事实（实测，`evidence/r2-fold/02-run.log`）**：单次注入 `MEASURE fold_command_exec
p50=680.50us / p95=1081.92us`；50 条折叠 ≈ 34 ms（p50）/ 54 ms（p95）。
`decision.md` §3.4 已写“不能每帧对全文档重建”，但 `edit-folds.md` 与 `acc-editor.md`
**没有任何节流/上限判据**——实现者可以对每次按键做全量重建而验收照样绿。本条款补上。

**条款（A2.1，允许重建的时机）**：折叠的**全量重建只允许**发生在：
(a) tree-sitter 运行期开关切换；(b) 语法/折叠来源重新解析完成之后；(c) 文件打开时的初次注入。
其余任何事件（按键、光标移动、滚动、每帧渲染、选区变化）**禁止**触发全量重建。

**条款（A2.2，数值上限，含来源）**：

| 指标 | 上限 | 依据 |
|---|---|---|
| 单次注入 | **≤ 2 ms**（p95） | 实测 p95 1.08 ms，留 ≈2× 余量（†） |
| 50 条折叠的一次全量重建 | **≤ 150 ms** | 实测 p50 34 ms / p95 54 ms，取 ≈3× p95 留量（†） |
| 按键路径的全量重建次数 | **必须为 0** | M2 评审：可观测计数断言 |

（†）来源 `evidence/r2-fold/decision.md` §3.4 + `02-run.log` 的 `MEASURE` 行；
测量环境 `vprobe-qt6kf6:1`（Qt 6.10.2 / KTextEditor 6.24.0）。若实现环境不同，
**必须在证据中同时记录实测绝对值与「环境差异说明」**，且「按键路径 0 次全量重建」这一条
与环境无关、不得豁免。

**条款（A2.3，可观测计数，归口 `edit-folds`）**：注入器必须暴露可读取的计数器
（日志行或状态接口均可，名称为 `folding.rebuild.full_count` 与 `folding.inject.count`），
供验收侧断言；计数器口径写进 `evidence/edit-folds/` 的说明。

**验收判据（A2.4，归口 `acc-editor`）**：
(a) 文档内存在 ≥20 条折叠时，连续输入 200 个字符，断言 `full_count` 增量为 **0**；
(b) 触发一次 tree-sitter 开关切换，断言 `full_count` 增量 **恰为 1**，且该次重建耗时 **≤150 ms**；
(c) 记录单次注入 p95，断言 **≤2 ms**；
(d) 证据含计数器前后值、耗时数字与命令，不得只给结论。

---

## A3 — A1f 的观察方法（修 `acc-editor`）

**事实**：A1f 要求断言“折叠区间集合”，但 `decision.md` §2.1/§7 已证明该集合**无法经
KTextEditor 公共 API 查询**（无折叠模型、无文档级枚举）。因此拿应用侧 registry 的期望集合
自证 = 同义反复，不构成验收。

**条款（A3.1，观察方法写死为三者之一）**：

1. `View::firstDisplayedLine(KTextEditor::View::RealLine)` 与
   `…(KTextEditor::View::VisibleLine)` 在折叠前后的**差值**；
2. `verticalScrollBar()->maximum()`（`scroll_max`）在折叠前后的**差值**；
3. **截图 diff**（同一视口、同一滚动位置，折叠前后各一张，附 sha256）。

**条款（A3.2，期望值来源）**：期望的隐藏行数可来自 **tree-sitter query 输出**（`folds.scm`
对样本文本的离线计算结果）或**独立计算的折叠区间**；**禁止**来自应用 registry、也禁止来自
实现方自己的运行时状态。“期望”与“观察”必须是两条互相独立的路径。

**条款（A3.3，禁止项）**：禁止以 registry 集合作为断言来源；禁止只断言“有变化”而不给出
正数控制（例如必须同时给出“隐藏行数 = 7 行”的期望与观察值）；禁止用“实现自报的状态字段”
代替 A3.1 三种观测之一。

**验收判据（A3.4）**：`acc-editor` 的 A1f 证据必须对**开关两态各给出一组**记录：
`sample`（1k 行 SystemVerilog 样本）、`expected_hidden_lines`（含推导：query 命中区间长度之和）、
`observed_visible_delta` / `observed_scroll_max_delta`、以及截图路径。
缺少“期望推导”或“独立观察”任一，A1f 判 FAIL。

---

## A4 — `.sv` 在“关闭态”的折叠来源（修 `edit-folds` / `acc-editor` / `syn-kde-data`）

**事实**：`requirements.md` G3a 期望 tree-sitter 关闭时退回 KSyntaxHighlighting 底座折叠；
但 r2-license 已定 **KDE 的 `systemverilog.xml`（GPLv3+）不得随包**（C7，处置未定即不得随包）。
于是 `.sv` 在“关闭态”**没有任何折叠来源**——除非我们自有那份定义自带折叠区。

**条款（A4.1，采纳 (a)）**：**采用 (a)**：我们自有的、许可干净（LGPL-2.1-or-later 或自写）的
`systemverilog.xml`（供 KSyntaxHighlighting 使用）**必须包含 `<folding begin="…" end="…"/>`
区**，使 **tree-sitter 关闭态下 `.sv` 仍有折叠**。折叠区至少覆盖包含式结构：
`module/interface/program/class/function/task/generate/case/begin-end`（可进一步细分，但不得少）。

**条款（A4.2，对 `syn-kde-data` / T16 的硬要求）**：T16 的交付物中，
`.sv` 的 KSyntaxHighlighting 定义**以我们自有的 XML 形式随包**，且该 XML：

- 含 A4.1 要求的 `<folding>` 区（这是 `<folding>` **XML** 定义，与 tree-sitter 的
  `folds.scm` 是两条独立来源，二者都要有，互不替代）；
- 来源许可干净（GtkSourceView 4.6.1 的 `verilog.lang`/`systemverilog.lang` 为 LGPL-2.1-or-later，
  sha256 与出处见 `assets/language-specs/README.md`；从 `.lang` 移植为 KSyntaxHighlighting XML
  属于**移植**，不是复制，provenance 必须写清）；
- **不得**随包 KDE 的 GPLv3+ `systemverilog.xml`（C7 不变）；
- provenance（上游、版本、许可、sha256）落 `evidence/syn-kde-data/`，供 `pack-final` 汇总进
  `THIRD_PARTY_NOTICES.md`。

**条款（A4.3，对 `edit-folds` / `acc-editor` 的联动）**：关闭态 `.sv` 的折叠来源固定为
A4.2 的自有 XML；`acc-editor` 的 A1f 对 `.sv` 的期望按 A3.2 从该 XML 的 folding 区独立推导，
**不得**假定“关闭态无折叠”，也不得跳过 `.sv` 的关闭态用例。

**条款（A4.4，回退路径）**：仅当**用户明确否决 (a)**（理由应是 T16 移植工作量）时，才切换到
(b)：声明 `.sv` 在关闭态无折叠，并由 captain 发布 `AMENDMENTS rev 2` 同步修正 A1f 的期望与
`edit-folds` 的退化路径。**在 rev 2 发布之前，(a) 是唯一有效条款**；实现方不得自行选择。

**验收判据（A4.5）**：tree-sitter 关闭、打开 1k 行 `.sv` 样本，可观测到 ≥1 条折叠
（用 A3.1 的观测方法），且证据中给出取自自有 XML `<folding>` 区的期望值；
同时 `syn-kde-data` 的证据里列出随包定义清单，证明其中**没有** KDE GPLv3+ 的 `.sv` 文件。

---

## A5 — 引用修正（评审记录路径笔误）

T42 评审记录 `rec-20261031T030614-c4da7a` 的 `sources` 第一项写作 `..mpd/cellwork/…`，
**正确路径**为：

```
.mpd/cellwork/cell-20261031T014507403Z/contracts/r2-fold-probe.md
```

（sha256 `ab92d4a1e5571edeb1ea919c69082ea787d798fc119e196c425cc3ac537f061f`，与记录
`basis.frozenContract.sha256` 一致，因此该笔误不影响判定，仅作溯源更正。）

---

## 派活时必转的 3 句话（captain 直接复制给下游）

1. **A1**：折叠注入前必须自验 range（非空、`start<=end`、行号在文档内、至少一行）；命令返回
   `true` 不代表折叠成功——非法 range 会回落到用户选区，**禁止用返回值当校验**。
2. **A2**：折叠只允许在「tree-sitter 开关切换 / 重解析完成 / 打开文件」时全量重建；按键与每帧
   禁止重建；按键路径 `full_count` 必须为 0，50 条重建 ≤150 ms，单次注入 p95 ≤2 ms。
3. **A3/A4**：A1f 只能用 `firstDisplayedLine(RealLine|VisibleLine)`、`scroll_max` 差值或截图 diff
   观测，**禁止**用自家 registry 自证；`.sv` 关闭态折叠由我们自有 LGPL XML 的 `<folding>` 区提供，
   KDE GPLv3+ 定义不随包。

---

## 待用户拍板（captain 转达）

- **A4 的 (a) vs (b)**：本文件已把 **(a)** 写死为有效条款——它给 `syn-kde-data`（T16）**新增了
  实际工作量**：把 GtkSourceView 4.6.1 的 LGPL `.lang` 定义**移植**成 KSyntaxHighlighting XML，
  并额外产出手写 `<folding>` 区（含 module/generate/case 等包含式结构）。
  若用户认为 v1 不值得这份工作量，选择 (b)（`.sv` 关闭态无折叠 + 修正 A1f 期望），
  则需 captain 发布 `AMENDMENTS rev 2`，本文件 A4.1/A4.2 随之失效。
- 其余条款（A1/A2/A3/A5）为实现侧约束，**无需用户决策**。
