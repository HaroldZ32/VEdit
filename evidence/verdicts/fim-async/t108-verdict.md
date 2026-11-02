# T108 独立评审结论 — T106（FIM 请求异步化：job + poll + QTimer，超时=取消+提示）

> **手工建任务、无冻结契约**：T108（及 T106）在 cell 里没有 split-key 映射/验证环，判据取自 captain 派单书。
> 评审者 ≠ writer（writer = Senior Engineer 2）。
> **我评的代码**：T106 的改动在评审过程中落成提交 **`e6f55e5 feat(fim): make inline completion asynchronous`**；
> 我实际读/跑的都是这份已提交内容（`src/editor/fim/fim_backend.cpp` sha256 `13ae42de…`、
> `src/editor/kernel/fim_wiring.cpp` `f7511e94…`、`fim_wiring_selftest.cpp` `4bfd0087…`；mtime 15:44–15:47，均早于我的运行）。
> 当前 HEAD `206033c`（其上是证据/文档提交）。证据：`evidence/verdicts/fim-async/01-review-checks.log`。

- **结论**：**T106 可以放行（PASS）** — 异步是真的（代码 + 实证 + 我做的同步反证）、超时无悬挂且可重新发起、T104 语义未回退、已知限制写清楚。
- 非阻塞发现：2 条 Low（超时后“再发起”无测试断言；tick 指标单用不足以抓同步实现）+ 1 条环境说明（首跑 ctest 偶发 BAD_COMMAND 竞态）。

---

## 命题一：GUI 线程真的不再等待 ✅

### 1.1 代码级（逐行核）
- `RustBackend::start()`（`fim_backend.cpp`）：创建 service → **只调一次 `vd_fim_complete`** → 存 token → 返回；**无任何等待**。
- `RustBackend::poll()`：查 token → **只调一次 `vd_fim_poll`** → 映射状态 → `vd_fim_result_free` → 终态释放 token；**无等待/阻塞原语**。
- `cancel()`/`releaseToken()`：`vd_service_destroy(service)`（真取消 Rust job）。
- 阻塞原语只出现在**便利调用**里：`RustBackend::complete()`（`QThread::msleep(5)` 轮询，15 s 预算）与
  `InlineCompletion::request()`（同类有界轮询）；产品接线 `FimBinding` **只走 job + QTimer**，不调它们。
- `fim_wiring.cpp`：`QTimer(15 ms)` 轮询；`requestCompletion()` 只是 `requestJob()` 后启动 pollTimer 并立即返回；
  超时分支 `cancelRequest + activeJob_=0 + pollTimer stop + hideGhost + removeAcceptShortcuts + 诊断状态`。

### 1.2 实证（我实跑 `vedit-fim-wiring-selftest`，50 checks / 0 failures，exit 0）
```
CHECK fim.async.start-returns-fast PASS start took 0 ms (the endpoint sleeps 400 ms)
CHECK fim.async.in-flight PASS
CHECK fim.async.event-loop-ticks PASS ticks=12
CHECK fim.async.still-pending PASS in_flight=1
CHECK fim.async.polls-ran PASS polls=16
CHECK fim.async.suggestion-arrives-late PASS / ghost-after-arrival PASS
CHECK fim.async.typing-cancels-inflight PASS / cancelled-never-arrives PASS
```
→ 慢端点（响应前 sleep 400 ms）下 `Alt+\` 立即返回（0 ms）、等待期间事件循环在跳（ticks=12）、15 ms QTimer 真的在轮询（16 次）、建议晚到但仍显示。

### 1.3 灵敏度对照：**我做了同步反证**（这是本票最重要的一步）✅
- 先读对照本身：`fim.async.metric-detects-block` 的实现是「schedule QTimer(20 ms) → **主线程 `sleep_for(300 ms)`** → processEvents →
  断言 timer 在 **≥250 ms** 才触发」。我的运行：`20 ms timer fired at 300 ms after scheduling` → 该指标**确实会因线程被阻塞而报晚**，不是恒 PASS。
- 更关键的反证：复制当前树到容器 `/tmp/rev2`，**只**把 `FimBinding::requestCompletion()` 改回同步形态
  （调 `completion_->request(...)`，即 pre-T106 的阻塞轮询），其余不变，重建 wiring selftest 后运行：
```
CHECK fim.async.start-returns-fast FAIL start took 411 ms (the endpoint sleeps 400 ms)
CHECK fim.async.still-pending FAIL in_flight=0
CHECK fim.async.polls-ran FAIL polls=0
FIM-WIRING-SELFTEST ok=0 checks=52 failures=15
```
→ **整套断言真能抓住同步实现**（15 条失败，含“start 耗时 411 ms”“没有在飞 job”“0 次轮询”）。
  ⚠ 细则：`fim.async.event-loop-ticks` 在这份同步反证里**仍 PASS**（ticks=12）——因为它的计数窗口在阻塞调用返回之后才开始；
  抓住同步实现的是 `start-returns-fast / still-pending / polls-ran` 这一组。见 Low-2。

## 命题二：超时语义与“无悬挂”✅

- 我实跑（黑洞端点 + 300 ms 预算）：`fim.async.timeout-fires PASS after 301 ms with a 300 ms budget`；
  `timeout-status PASS FIM 超时（300 ms 无结果，已取消）`；`timeout-no-suggestion PASS`；`timeout-document-unchanged PASS`。
- **无悬挂（我自己补的独立断言，在 /tmp 副本的 selftest 末尾追加，不改仓库测试）**：
  - 超时后 `Tab` 已摘除：`t108.after-timeout.tab-removed PASS`（超时分支确实 `removeAcceptShortcuts()`）；
  - 在飞 job 已取消：`inFlightJobs()==0` + `releaseToken → vd_service_destroy`（代码级）；
  - **下一次 `Alt+\` 能重新发起**：`t108.reissue-after-timeout PASS in_flight=1`（新 job 被接受）。
- 结论：超时留下的是“一句可诊断状态 + 干净状态机”，没有悬挂 job、没有残留 Tab、可立即重试。

## 命题三：T104 语义未回退 ✅（我实跑）
- wiring selftest：`default-off.zero-connections PASS connections=0`、`default-off.tab-free`、`default-off.no-suggestion/no-ghost/document-untouched`、
  `accept.text` + `accept.exactly-once` + `accept.shortcuts-removed`、`cancel.document-unchanged`、
  `invalidate.typing-drops-it` + `tab-disarmed` + `accept-inserts-nothing`、`binding-owned-by-view` 全 PASS。
- lane selftest（51/0）：`start-token`、`poll-pending`、`poll-ready`、`accept-once`、`stale-poll-after-cancel`、
  **`single-inflight`（同一时刻仅一个 job）**、**`new-cancels-old`**、`cancel-releases` 全 PASS。

## 命题四：已知限制写清楚了 ✅
- `src/editor/fim/README.md` §**Ghost-text limitation**：`The overlay previews only the **first line** of a suggestion (a multi-line one shows ↵); Tab inserts the complete text. KTextEditor 6.24 has no public ghost-text API…` —— 用户能读懂“按 Tab 才插入完整多行文本”。
- ghost tooltip（`fim_wiring.cpp:55-56`）：`FIM ghost text: previews the first line only; Tab inserts the full suggestion.` ✓

## 回归（我实跑）
| 项 | 观测 |
|---|---|
| 根 ctest | 首次 `CTEST-EXIT=8`，唯一失败 `16 - vedit-fim-selftest (BAD_COMMAND)`——**共享树并发竞态**（二进制被另一进程重建/占用）；**立即重跑 26/26 passed、exit 0**，无失败。属环境竞态，不记 T106。 |
| `bash build/gates/typecheck.sh` | **exit 0**（`[qt6] PASS … warnings-as-errors` + pinned 镜像 Rust clippy PASS） |
| C9（`evidence/fim-async/0*.log`） | 4 份日志**每份**都有 `collected_at` + `git_head` + `git_branch` + `git_status:`（porcelain 若干行）；`run-evidence.sh` 的 `emit_header()` 统一生成，已逐份核对 ✓ |

## Findings（非阻塞）

| ID | 严重度 | 内容 | 建议 |
|---|---|---|---|
| **Low-1** | Low（测试覆盖缺口） | 交付的 wiring selftest 在超时断言后就结束了，**没有**“超时后再次发起”的断言（我用自己的 /tmp 副本补测才证明可行，PASS）。行为正确，但覆盖缺失 | 把「超时后 Tab 已摘除 + 新请求可发起」加进 selftest（我在 /tmp 里的两句断言可直接搬） |
| **Low-2** | Low（指标说明） | `fim.async.event-loop-ticks` 单独**抓不到**我的同步反证（窗口开在阻塞返回之后，仍 ticks=12）；真正抓住同步实现的是 `start-returns-fast / still-pending / polls-ran` 组。报告的表格读起来像每个指标都独立充分 | 把 tick 计数窗口移到 `activate()` 之前，或在报告/注释里写明“这组三件套共同构成守卫” |
| — | 环境说明 | captain 期望根 ctest 25/25；当前树是 **26 个测试**（多出的是其它 lane 的新测试），我重跑 26/26 全绿；证据日志采集时确实是 25/25 | 无需处理，仅记录漂移 |
| — | 注 | 已知 flake `vedit-folding-tests` 在我这次运行里**没有**复现（26/26 全绿） | 仅记录 |

## 未能验证 / 边界
1. **真实 GUI 观感**（ghost 叠层外观、光标跟随手感）：无显示服务器，全部 offscreen；不得作视觉认证。
2. **真实网络端点的超时/慢响应**：测试用本机回环的合成端点（sleep/黑洞），不是真实 FIM 服务。
3. **多视图并发**：同一时刻仅一个 job 是在单绑定/单视图语义下验证的；多 pane 并发请求未测。
4. **550 ms 以上的长尾**：慢端点只测了 400 ms；更长延迟（数秒）下的 UI 交互未覆盖。

## 结论

**T106 可以放行（PASS）**。核心 C2 命题由三层证据支撑：代码级（start 只调一次 job、poll 只调一次且无阻塞原语、msleep 只在便利调用里）、
实证（慢端点 0 ms 返回 + ticks/polls + 建议晚到仍显示）、以及**我做的同步反证**（把 requestCompletion 改回同步 → 15 条失败，含 start 411 ms、
无在飞 job、0 次轮询），证明这套断言能抓住同步实现；超时语义 301 ms + 诊断状态 + 无建议 + 文档不变，且我用补充断言证明无悬挂（Tab 摘除、job 取消、可立即重发）；
T104 的默认关闭/0 连接/Tab 受控/accept 恰一次/Esc 不改文档/输入作废/单 job 全部仍绿；ghost 只预览首行的限制在 README 与 tooltip 都写清楚了。
两条 Low（超时后再发起的测试覆盖、tick 指标的单用局限）建议顺手补上，不阻塞放行。
