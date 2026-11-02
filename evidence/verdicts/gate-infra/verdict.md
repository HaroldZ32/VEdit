# 独立评审 verdict — T55 gate-infra（门禁基础设施：typecheck 的执行站点 + gates 的 slang 缺件路径）

- Cell: `cell-20261031T085543161Z` · 被评 work: `T54`（gate-infra）· 评审任务: `T55`
- 冻结契约: 本任务由 captain 手工建立，**无独立的冻结契约文件**；被评对象为 `T54` 的交付物 `evidence/gate-infra/**` 与 `build/gates/{typecheck,gates}.sh`
- 评审人: **Reviewer**（盲审，依据 cell mail 记录）
- 落盘整理: **Lead**（T16）—— 依据 captain 的 cell mail 记录整理成文件，**不是** Reviewer 本人重新签署
- 落盘复核: Lead 于 2026-10-31T11:53Z 对下列每条论断逐一回对了**一手证据**（见 `复核` 列），并**修正了一处与证据不符的转述**（见 §2）
- 日期: 2026-10-31（Asia/Shanghai）
- 风险等级: **Low**
- 结论: **PASS**

---

## 1. 评审结论（captain 邮件记录）

T54 修复的是「门禁在错误的地方运行」：`build/gates/typecheck.sh` 的 Rust 半段原本在**宿主**跑 `cargo`，而宿主 pinned 1.99.0 rustup toolchain 没有 cargo —— 门禁测的是宿主而不是产品。修的**不是检查强度，而是执行站点**。

## 2. 论断逐条复核（一手证据）

| # | 论断 | 复核结果 | 证据 |
|---|---|---|---|
| 1 | **门禁未被降级**：无 `\|\| true`、无 `cargo check` 回退、无 `set +e`、无 pass-on-failure | ✅ 成立。`typecheck.sh` 内这些词只出现在**注释**里（第 32–33 行说明「没有」），代码中不存在 | `build/gates/typecheck.sh:1-95` |
| 2 | **Rust 半段真在 pinned 镜像跑** | ✅ 成立。本次 T16 实跑：容器横幅 `[typecheck] container facts: CentOS Linux release 7.9.2009 (Core) \| rustc 1.99.0 (b940084d7 2026-09-28) \| cargo 1.99.0 (5f94df478 2026-08-27)`，随后 `[typecheck] PASS (pinned image vedit-build:local)`，`TYPECHECK-EXIT=0` | `evidence/slice1-stop/11-typecheck.log` |
| 3 | **证伪注入成立**：注入 `useless use of format!` ⇒ 门禁 RED | ✅ 成立。`error: useless use of \`format!\`` → `could not compile \`vd-search\` (lib) due to 1 previous error` → `TYPECHECK-EXIT=101` | `evidence/gate-infra/02-falsification-injected.log:547,557,580` |
| 4 | **证伪已恢复** | ✅ 成立。恢复后日志中 `useless use of format!` 消失（`grep` 无命中） | `evidence/gate-infra/03-falsification-restored.log` |
| 5 | `gates` 的 slang 缺件路径**不跳过、不冒充通过** | ✅ 成立。三个场景日志齐备：documented build 产出 artifact、缺件时给可执行错误、artifact 在场时打印版本 | `evidence/gate-infra/{20-slang-build-attempt,21-gates-slang-missing-path,22-gates-with-artifact}.log` |
| 6 | 其它门禁未被本任务改变 | ✅ 成立（记录在案）：`docs` PASS、`manifest` PASS、`rows`/`comments` 为仓库声明式 N/A→RED，未变 | `evidence/gate-infra/30-other-gates.log` |

## 3. ⚠️ 对 captain 邮件转述的一处更正（不改结论）

captain 的邮件把传导写成「`exit 7` → 7 传导成立」。**证据不支持这个数字**：

- 证伪注入后的真实退出码是 **`TYPECHECK-EXIT=101`**（clippy/rustc 编译失败的退出码），不是 7；
- `build/gates/_common.sh` 的 `no_gate()` 是 `exit 1`，不是 7；
- 全树 `grep -rn 'exit 7' build/gates/*.sh scripts/*.sh` **无命中**。

**结论不变**（PASS）：退出码确实**如实传导**（注入即红、真红即非零），只是值为 101/1 而非 7。本文件按证据记录，不照抄邮件里的数字 —— 这正是「verdict 必须建立在证据上」的意义。

## 4. 边界（如实）

- 本 verdict 由 Lead 依据 captain 的 cell mail 记录 + 一手证据复核整理落盘；**Reviewer 本人没有二次签署**。若流程要求 Reviewer 亲签，本条应视为「待亲签」。
- 复核只覆盖上表六条论断，未重新做一次完整的盲审。
