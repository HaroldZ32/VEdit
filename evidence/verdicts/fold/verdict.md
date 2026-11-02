# T20 独立评审结论 — T19 `folding-breadcrumbs`（A1/A2/A3 + 面包屑）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z`；verifier ≠ writer `9a870062…`）
- **契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/folding-breadcrumbs.md`（split revision 2）
- **验证环**：`loop-20261031T095153-fc5d81`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（A1/A2/A3 与面包屑均经我独立复跑/读码核对成立；判据未被降级）
- **非阻塞发现**：1 条 Low（证据 pin 与当前树漂移）＋ 外部红（T94 在飞，非 T19）
- **证据**：`evidence/verdicts/fold/01-review-checks.log`

---

## 1. A1 — range 自验（我自己读码 + 跑测试）

- **模型**（`src/syntax/folding/fold_range.{h,cpp}`）：四项检查逐条实现——① 负行号=无 range/invalid；② `start>end` → inverted；
  ③ 任一行 ≥ lineCount → out-of-document；④ `endLine<=startLine` → zero-lines；拒绝原因具名，`Rejection.detail` 带原始 range 与当时选区 ✓。
- **注入器**（`fold_injector.cpp`）：`inject()` **先校验后执行**；被拒 range 只累加 `rejectedCount_`、写诊断，**不进 registry、不进 `fold`、不计注入**；
  `injectUnchecked()` 里 `exec()` 返回值只 push 进 `execResults_`（仅 false 时记日志），**从不用于判定**（注释与代码一致）✓。
- **我实跑**（当前树，fresh /tmp 构建）：
  ```
  A1-CONTROL invalid_range  exec_returned=1 scroll_max_before=939 scroll_max_after=934 hidden_before=0 hidden_after=5
  A1-CONTROL inverted_range exec_returned=1 scroll_max_before=939 scroll_max_after=929
  ```
  → 负向对照复现了 A1 的事实：**非法 range 会回落折叠用户选区**、倒置 range **返回 true 且仍隐藏 10 行** → 证明确实不能用 exec 返回值判定。
- **A1.4 全部要件**在测试里：非空选区（20–25）`a1.selection.non-empty`；四种非法 range 全被拒（`a1.product.rejected-count=4`、injected/inject-count/registry 均 0）；
  注入前后观测**完全相同**（`a1.product.observation-identical`）；选区不变（`a1.product.selection-unchanged`）；四条 `diag-reason-*` + `diag-selection`；
  以及 `a1.control.inverted.exec-returned-true`。

## 2. A2 — 重建时机、计数、性能（我实跑）

```
A2 first-open injected=200 rejected=0 elapsed_ms=87 full_count=1 inject_count=200
A2 key-path chars=200 full_count_before=1 full_count_after=1 key_events=200
A2 cursor-path moves=50 cursor_events=50 full_count=1
A2 toggle-off injected=0 elapsed_ms=8 full_count=2
A2 toggle-back injected=200 elapsed_ms=90 full_count=3
MEASURE fold_inject n=50 p95_us=523.69
```

| A2 判据 | 观测 | 结果 |
|---|---|---|
| 全量重建只允许 3 种触发（FirstOpen/TreeSitterToggled/Reparsed） | `RebuildTrigger` 枚举只有这 3 值；`onKeyPress()/onCursorMoved()/onScroll()` 是显式 no-op（只记 `folding.rebuild.refused … (A2.1)` 日志，不调 rebuild） | ✓ |
| 按键路径 full_count 增量 = 0（200 字符） | 1 → 1 | ✓ |
| 光标 50 次 + 滚动 full_count 增量 = 0 | cursor_events=50，full_count=1 | ✓ |
| 开关切换 full_count 恰 +1 且 ≤150 ms | off: +1（8 ms）；back: +1（90 ms） | ✓ |
| ≥20 条折叠时的一次全量重建 ≤150 ms | 首次 200 条 87 ms | ✓ |
| 单次注入 p95 ≤2 ms | 523.69 µs | ✓ |
| 计数器可观测 `folding.rebuild.full_count` / `folding.inject.count` | 日志行 + `fullCount()/injectCount()`；口径写进 README | ✓ |

## 3. A3 — A1f 观测方法（我实跑 + 读码）

```
A1F state=on  tree_sitter_active=1 expected_hidden=975 expected_scroll_drop=939 observed_scroll_drop=939 hidden_in_viewport=975 ranges=200
A1F state=off tree_sitter_active=0 expected_hidden=0 observed_scroll_drop=0 hidden_in_viewport=0 ranges=0
A1F method1   expected_hidden=39 expected_scroll_drop=19 observed_hidden_in_viewport=39 observed_scroll_drop=19
```

- `observeView()` **只读** `firstDisplayedLine(RealLine/VisibleLine)` 与 `verticalScrollBar()->maximum()`（A3.1 方法 1/2），没有 registry 参与断言。
- 期望值 `975` 来自**样本文本构造**（25 个 module × 每块折叠保留首行、隐藏 39 行），不是 registry，也不是实现自报状态（A3.2）✓；
  测试注释也写明“expected… never from the injector registry”。
- **scroll_max 饱和**是已知物理性质：期望改用 `min(隐藏行数, 基准 scroll_max)`（975→939、39→19），并在 README/报告 §7 写明；
  **精确隐藏行数仍由方法 1 断言**（975=975、39=39），所以 A3.4 的“期望推导 + 独立观察 + 正数控制”三项都在，未降判据。
- on/off 两态各一组快照 ✓（A3.4 的“开关两态”）。

## 4. 面包屑（语义来源）

`src/workspace/breadcrumb/`：`SyntaxBreadcrumbSource` 用 `SyntaxProvider::breadcrumbs(tree, byte_offset)`（`structure.scm`），
同一文本只解析一次（光标移动复用缓存）、文本变化恰好重解析一次；模型有 `setPath`/`changeCount`/`display`/`describe`。
我实跑：`BREADCRUMB-TESTS ok=1 checks=36 failures=0`（PATH/FOLLOW/parse-count 断言全 PASS），lane ctest 4/4。

## 5. 我的实跑汇总（当前树）

| 项 | 命令（隔离 /tmp 构建，不写共享证据） | 观测 |
|---|---|---|
| 折叠 lane 测试 | `cmake -S src/syntax/folding -B /tmp/fold-build && build && vedit-folding-tests` | **FOLDING-TESTS ok=1 checks=75 failures=0**，exit 0；lane ctest 4/4 exit 0 |
| 面包屑 lane 测试 | `cmake -S src/workspace/breadcrumb -B /tmp/bc-build && build && vedit-breadcrumb-tests` | **BREADCRUMB-TESTS ok=1 checks=36 failures=0**，exit 0；lane ctest 4/4 exit 0 |
| 声明 gate `typecheck`（loop 绑定） | `mpd_verify_evidence {gate:typecheck, loop:loop-20261031T095153-fc5d81}` | **exit 1**，见 §6 归因 |

## 6. Findings / 归因

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low**（证据 pin 漂移，不影响判据达成） | `evidence/slice2-fold/05-source-sha256.txt` 的 19 个 pin 里 **3 个**与当前树不符：`src/syntax/folding/CMakeLists.txt`、`README.md`、`tests/folding_tests.cpp`（后者 mtime **14:04**，晚于证据 13:49；证据认证的版本是 54 checks，当前是 75 checks）。我用当前树复跑 75/75 通过，说明**判据仍成立**，但证据 pin 已不覆盖当前交付态 | 提交前把这三份文件重新 pin（刷新 05 + 相关日志），或在报告里注明“证据认证的源码版本 vs 当前树”的差异 | `evidence/slice2-fold/05-source-sha256.txt` vs 当前 sha256（见 `01-review-checks.log` §4） |
| **F-2** | **Low / 外部归因** | 声明 gate `typecheck` 当前 RED：`src/editor/kernel/ktexteditor_provider.cpp:91: error: 'connect' was not declared`——这是 **T94（在飞：把折叠/面包屑接进 vedit-app）** 的 src/editor 编辑，不是 T19 写域；根 ctest 在 14:06 我还跑到 20/20，现因同一在飞改动变红。writer 原始 typecheck 红是 `crates/vd-vcs`（另一 lane，现已绿） | 等 T94 落地后重跑 `typecheck`/根 ctest；T19 自己的两个 lane 构建与测试已是绿的 | gate 证据 `ev-20261101T061831-28ddbd`（exit 1）与 `01-review-checks.log` §5 |
| — | 注（不是缺陷） | 证据日志里含**首轮失败与两次“期望值修正”**（scroll_max 饱和、倒置 range 对照实际有折叠效果）——我核过：修正是让断言符合实测物理行为（饱和用 `min()`；倒置对照改断言“确实有折叠效果”），**没有删除/放宽 A1.4/A2.4/A3.4 的任何必需断言**；A3.4 的独立期望（975/39）与观测（975/39）仍在 | — | `evidence/slice2-fold/02-run-folding.log` 的 RERUN 段 + 当前测试断言 |

**其它说明**：该 lane 目前**未接进 vedit-app**（契约写域不允许，板上 **T94 在飞**专门做集成）——报告 §7 已如实说明；这不是 T19 的判据缺口。

## 7. 未能验证 / 边界

1. **产品级集成**：折叠注入器/面包屑尚未接到 vedit-app 实时视图（T94 在飞）；本评审只认证 lane 级（真实 KTextEditor view + 真实 tree-sitter provider）。
2. **GUI 观感**：无显示服务器；全部 offscreen，未认证“看起来对”。
3. **物理键鼠/真实滚动**：性能与计数用合成事件；真人滚动手感未测。
4. **当前树 gate 红**：由 T94 在飞编辑引起（§6 F-2），未能在本评审窗口内得到绿的整树 `typecheck`/根 ctest。
5. **超大折叠数（1000+）**：报告声明不在本契约判据内，我也未测。

## 8. 结论

**PASS**。A1 的“非法 range 不得折叠用户选区 + exec 返回值不可作判据”由代码（先校验后 exec、拒绝不入 registry/计数）与我的实跑（负向对照：非法 range 折叠了选区、倒置返回 true 仍隐藏 10 行）双重证实；
A2 的“只允许 3 种重建触发、按键/光标/滚动 0 次全量重建、开关切换恰 +1、50 次注入 p95 524µs、200 条重建 87–90ms”全部实跑通过；
A3 的 on/off 两态只用 `firstDisplayedLine`/`scroll_max` 观测，期望 975/39 来自样本文本构造而非 registry，scroll_max 饱和规则有文档与 `min()` 处理，未降判据；
面包屑走语义 query（structure.scm）且解析次数可断言。
F-1（证据 pin 漂移）与 F-2（T94 在飞导致的整树红）都是证据/环境层面的 Low，且已给出归因与修法，不阻塞 T19 放行。
