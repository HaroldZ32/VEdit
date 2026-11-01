# T103 — T100 审计核验 + 两份 verdict 更新记录

> 执行：**Reviewer**（本 cell Reviewer 3）· 2026-11-01（Asia/Shanghai）· 评审任务 **T103**（手工任务，无冻结契约）
> 写域：`evidence/verdicts/{fix-xml-folding,a4}/verdict.md`、`evidence/fold-evidence-final/**`（本文件为追加）
> 未改：`resources/syntax/**`、`src/**`、`evidence/syn-kde-data/**` 的测量日志、其它 verdict。

## 1. 对 T100 审计（`evidence/fold-evidence-final/03-T45-upgrade-audit.md`）的逐条核验

| # | 审计声称 | 我的核验（实际读到的内容） | 结论 |
|---|---|---|---|
| 1 | 旧 `10-folding-before.log` / `11-folding-after.log` 都带 `# INVALID (T66)` 横幅 | 两文件首行均为 `# INVALID (T66): this log measured the DISTRO library (F2_DIR path bug) and the SV sample branch never ran; superseded by …-F2.log` | ✅ 属实 |
| 2 | 新 `10-folding-before-F2.log` 完整：before Verilog 0/0、SV **26 行** 0/0，`PROBE-SUMMARY mode=before expected=zero all-checks=PASS`、`RUNNER-EXIT=0` | 实读：`SAMPLE language=verilog text-lines=18 … begins=0 ends=0`；`SAMPLE language=systemverilog text-lines=26 … begins=0 ends=0`；`PROBE-SUMMARY mode=before expected=zero all-checks=PASS`；`RUNNER-EXIT=0` | ✅ 属实 |
| 3 | 新 `11-folding-after-F2.log` 完整：after Verilog 18 行 7/7、SV 26 行 11/11，`PROBE-SUMMARY mode=after expected=fold all-checks=PASS`、`RUNNER-EXIT=0` | 实读：`SAMPLE language=verilog text-lines=18 … begins=7 ends=7`；`SAMPLE language=systemverilog text-lines=26 … begins=11 ends=11`；`PROBE-SUMMARY mode=after expected=fold all-checks=PASS`；`RUNNER-EXIT=0` | ✅ 属实 |
| 4 | F2 日志是链接事实不是标签：`# ldd guard` + **每个结果块前** `LDD library=F2 resolves=…F2…` | before 日志：结果块前 2 处 `LDD library=F2 resolves=/vedit-build/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`（verilog / systemverilog 各一）；after 日志同样 2 处；两份都有 `# ldd guard: libKF6SyntaxHighlighting resolves inside the F2 tree: …` | ✅ 属实 |
| 5 | 负控 `14-ldd-guard-negative-control.log`：旧 flags 解析到 DISTRO，guard `exit=1` | 实读：`old_flags_ldd_resolves=/usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6`；`GUARD-ON-OLD-BINARY=REJECTED exit=1 (as required: a distro-linked binary is never labelled F2)`；`NEGATIVE-CONTROL-EXIT=1` | ✅ 属实 |
| 6 | 发现并修正 1 处过时引用（`FOLDING-fix-T45.md` 的 0/0 依据已改指 `-F2` 日志） | 实读 `FOLDING-fix-T45.md`：第 23 行 `… (10-folding-before-F2.log, re-recorded by T66 on the real F2 library; the first 10-folding-before.log measured DISTRO and is superseded)`；表格 52–55 行全部引用 `-F2` 日志；60/74–75/123–124 行把旧日志标为 invalid/superseded | ✅ 属实 |
| 7 | `docs/`、`AGENT.md`、requirements 不再把旧日志当有效引用 | 我 grep `docs/ AGENT.md .mpd/cellwork/.../requirements.md` → **0 命中** | ✅ 属实 |
| 8 | 遗留：两个 verdict 仍写「有条件 PASS」/「T66 仍 open」，需 owner 更新 | 我实读两份 verdict，确认旧表述仍在（这正是本任务要更新的） | ✅ 属实（已由本任务更新） |

**核验结论**：T100 审计的 1–8 全部属实，T45 的升级条件（X=`10-folding-before-F2.log`、Y=`11-folding-after-F2.log`、Z=`14-ldd-guard-negative-control.log`，加上旧日志 INVALID 横幅与修正后的引用）全部满足 → 可以执行 verdict 升级。

## 2. 本次对两份 verdict 的改动（只加「结论 + 依据」，历史原样保留）

### 2.1 `evidence/verdicts/fix-xml-folding/verdict.md`（T46/T45）

- 文首新增 **「升级记录」** blockquote：写明「PASS（有条件，等 T66 重录）」→「**PASS（无保留）**」，列出升级依据
  （`10-folding-before-F2.log` / `11-folding-after-F2.log` / `14-ldd-guard-negative-control.log` 的具体观测），
  并注明执行者（Reviewer 3）、时间（2026-11-01）、依据（T100 审计）与核验记录路径（本文件）。
- 旧 header「结论: **PASS（有条件）** …」**原文保留**，仅在句末追加「（已于 2026-11-01 升级为无保留 PASS，见文首「升级记录」；原措辞保留）」。
- §6「**Verdict: PASS（有条件）** — 风险 High」**原文保留** + 同样的升级标注；§6 理由段落后追加一段
  「**更新（2026-11-01，Reviewer 3，依据 T100 审计）**：条件已由 T66 交付满足 → 升级为无保留 PASS」。
- **未删**：文首「依据更正（2026-10-31 19:25）」的自我更正 blockquote、§1.3「我第一版为什么没抓到」、§5 边界与第一版 record 说明，全部原样保留。

### 2.2 `evidence/verdicts/a4/verdict.md`（T30/T29）

- 文首 header 的「T66 仍未关门」**原文保留** + 追加「**状态更新 2026-11-01 … 该条已关闭**」。
- §4「前置与证据链」里「T45 = PASS（有条件）」**原文保留** + 标注「2026-11-01 已升级为无保留 PASS」，以及「T66 仍未关门（见 F-3；**已于 2026-11-01 关闭**）」。
- Findings 表 **F-3**：原「T66 仍 open，T45 是有条件 PASS」**原文保留** + 追加
  「**→ 已关闭（2026-11-01，Reviewer 3，依据 T100 审计）**：T66 已交付并经审计（`evidence/fold-evidence-final/03-T45-upgrade-audit.md`：`-F2` 重录 + ldd 防呆 + 负控 exit=1），`fix-xml-folding` verdict 已同步升为无保留 PASS」。
- §7 结论末句「T66 需要收尾（F-3）」**原文保留** + 追加「→ 已于 2026-11-01 关闭」。

### 2.3 未触碰

- `evidence/syn-kde-data/**` 的任何测量日志（只读核验，未改一个字节）。
- 其它 verdict（`fold/`、`fold-wiring/`、`lsp/`、`user-guide/`、`vcs/`、`a4/` 之外的任何 verdict 文件）。
- `resources/syntax/**`、`src/**`、测试/门禁脚本。

## 3. 验收自检（grep）

```text
$ grep -c '升级记录' evidence/verdicts/fix-xml-folding/verdict.md                 → 1（+ 2 处升级标注）
$ grep -n 'PASS（有条件）' evidence/verdicts/fix-xml-folding/verdict.md           → 旧结论仍在（2 处，均带“已升级”标注）
$ grep -n '无保留 PASS' evidence/verdicts/fix-xml-folding/verdict.md              → 升级说明在
$ grep -n 'F-3.*已关闭' evidence/verdicts/a4/verdict.md                            → F-3 关闭标注在
$ grep -n 'T66 仍未关门' evidence/verdicts/a4/verdict.md                           → 原文保留 + 状态更新标注
$ grep -n 'T66 需要收尾' evidence/verdicts/a4/verdict.md                           → 原文保留 + “已于 2026-11-01 关闭”
```

## 4. 边界

- 本任务只做「按审计更新 verdict」：**没有**重新跑折叠探针、没有重新仲裁 T45/T66 的技术结论；升级依据是 T100 审计给出的
  三份日志 + 我对其内容的逐条核验（§1）。
- 我**没有**运行任何构建/门禁（本任务不要求；改动只在 verdict 文本）。
- 未 commit/push（按任务要求由 Lead 统一提交）。

## 3b. 验收自检（实际运行，2026-11-01 15:17 CST）

```text
$ grep -c "升级记录" evidence/verdicts/fix-xml-folding/verdict.md
3
$ grep -n "PASS（有条件）" evidence/verdicts/fix-xml-folding/verdict.md   # 旧结论仍在 + 已升级标注
16:> **原「PASS（有条件）」结论、以及最初「无 ldd 依据」的自我更正记录，原样保留在下方，未被删除或改写**（只在相关位置加了升级标注）。
31:- 结论: **PASS（有条件）** — XML 修复经我独立在**真 F2** 上重测成立（Verilog 7/7、SV **11/11**、before 0/0）；但**证据之记录的 10/11 日志不是 F2 测量**、SV 采样分支从未生效；在 **T66** 修好 `run.sh`、加 `ldd` 防呆并重录 10/11 之前，本票只能读作“有条件 PASS / 依据待重录确认”。（**已于 2026-11-01 升级为无保留 PASS，见文首「升级记录」；原措辞保留**）
102:**Verdict: PASS（有条件）**（**已于 2026-11-01 升级为无保留 PASS，见文首「升级记录」；原措辞保留**） — 风险 High。
$ grep -n "依据更正\|我第一版为什么没抓到" evidence/verdicts/fix-xml-folding/verdict.md   # 历史自我更正保留
18:> **依据更正（2026-10-31 19:25，captain 指出后由本评审独立复核）**：本 verdict 的第一版把
46:3. **我第一版为什么没抓到**：我只核了“F2 那个文件里没有内嵌定义”（`strings | grep -c '<language'` = 0），这**不能证明探针链接的是它**；我也复核了探针拷贝只差 `name=` 一行、author/section 断言有效——这些都对，但都绕过了“实际链接的是哪个库”。正确判据是 `ldd`，我第一版没有跑。这一点我承认。
$ grep -n "F-3\|已关闭\|T66 仍未关门\|T66 需要收尾" evidence/verdicts/a4/verdict.md
9:  **T66**（T45 的 F2 证据重录）仍未关门。（**状态更新 2026-11-01，Reviewer 3，依据 T100 审计：该条已关闭——T66 已交付并经审计
10:  （`evidence/fold-evidence-final/03-T45-upgrade-audit.md`），`fix-xml-folding` verdict 已同步升级为无保留 PASS；见 F-3。下文原历史表述保留。**）
90:  待 **T66** 重录。→ XML 修复本身成立（前置满足），T66 仍未关门（见 F-3；**已于 2026-11-01 关闭**）。
99:| **F-3** | Note | **T66**（T45 的 F2 探针证据重录）仍 open，T45 是"有条件 PASS" | captain 推进 T66，别让条件 PASS 沉底。**→ 已关闭（2026-11-01，Reviewer 3，依据 T100 审计）**：T66 已交付并经审计（`evidence/fold-evidence-final/03-T45-upgrade-audit.md`：`-F2` 重录 + ldd 防呆 + 负控 exit=1），`fix-xml-folding` verdict 已同步升为无保留 PASS |
114:当前根树红是 T94 在飞（F-2）；F-1/F-4 是证据与边界的小改进；T66 需要收尾（F-3）→ **已于 2026-11-01 关闭**（T66 已交付并经 T100 审计，T45 升为无保留 PASS；见 F-3 与 `evidence/fold-evidence-final/03-T45-upgrade-audit.md`）。
$ git status --porcelain -- evidence/verdicts/fix-xml-folding evidence/verdicts/a4 evidence/fold-evidence-final
 M evidence/verdicts/fix-xml-folding/verdict.md
?? evidence/fold-evidence-final/
?? evidence/verdicts/a4/
```
