# T68 独立评审结论 — T67（diagnostics 测试期望值修正 + 证据补正）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z`；verifier ≠ writer `e1acec86…`）
- **被评审任务**：board `T67`（kind=work，owner=Senior Engineer，status=completed）
- **契约**：本任务在 cell 里**没有** split-key 映射/验证环（`cellwork_status` 与 `.mpd/verify/loops` 均无 T67/T68 的 loop），
  故按 T68 的任务书 + T67 的 board subject 评审；没有 `mpd_verify_record` 可写，verdict 落盘于本文件。
- **结论**：**FAIL** — 核心问题（断言是修正不是放宽）与第二命题（两种环境都绿）都**成立**；
  但第三命题里的「“不可证”的表述是否已改正」**没有真正完成**：`09-report.md` §4 仍写
  「SLANG success is **NOT proven**」，与同一批证据 `16-f1-slang-success-path.log` 直接矛盾；
  并且被改成「SLANG success is **SLANG success IS** now proven … (no binary;」这种**拼接残句**。
  C9 补记也缺任务书要求的 `backfill:` 标记。
- **判据未达成的只有证据文本**；代码/测试本身无需再改（两种环境已真绿、断言仍可真红）。

---

## 1. 第一命题：这是修正错误期望，不是放宽测试凑绿 → **成立**

### 1.1 断言改成了什么（读当前源码）

`src/workspace/diagnostics/diagnostics_ffi_selftest.cpp`（`degrade` 块）现在是：

```cpp
// The expected state follows DiagnosticsController::finaliseState(): lspReady &&
// slangComplete -> Ready; exactly one of the two legs -> Degraded; neither -> Failed.
//   * SLANG usable and successful -> Degraded (one leg good, one bad);
//   * SLANG unusable            -> Failed (both legs bad).
// Both are the contract's "degraded, diagnosable, never a crash"; the previous
// Failed-only assertion was coupled to the SLANG-less host...
check("degrade.missing-verible-state",
      missingState == BackendState::Degraded || missingState == BackendState::Failed,
      stateName(missingState));
check("degrade.missing-verible-not-ready", missingState != BackendState::Ready,
      stateName(missingState));
std::printf("OBSERVED missing-verible-state=%s\n", ...);
check("degrade.missing-verible-message", !missing.lastError().isEmpty(), ...);
```

- 显式接受 `{Degraded, Failed}`（两个值都来自 `BackendState`），**没有**改成一个恒真断言；
- **新增** `degrade.missing-verible-not-ready`（`!= Ready`）——比原来更严；
- 原来的 `degrade.missing-verible-message`（非空、可诊断）**保留**；
- 没有删除任何既有断言（旧检查从 22 条变 23 条，只增不减）。

### 1.2 关键反驳测试：它**仍然能真红**（我自己造的负例）

我在容器内把 lane 的 `src/` 复制到 `/tmp/neg`，只改**副本**里`degrade` 块喂进去的场景
（把 "missing verible" 的硬编码 `/nonexistent/verible-verilog-ls` 换成真实可用的 verible，
即制造"状态应为 Ready、message 应为空"的错误场景），然后带 `VEDIT_SLANG` 运行：

```text
=== [C] NEGATIVE COUNTER-TEST (patched copy: wrong scenario fed to the degrade block) ===
patched-line-count: 1
neg-build-exit=0
CHECK FAIL degrade.missing-verible-state  ready
CHECK FAIL degrade.missing-verible-not-ready  ready
CHECK FAIL degrade.missing-verible-message
VER-DIAGNOSTICS-FFI-SELFTEST checks=23 failures=3
C-EXIT=1
```

→ 喂错误的 state/message 时它**照样判红**（3 条失败、exit 1）。测试没有被废掉。
（仓库本体未做任何修改；`/tmp/neg` 是容器内一次性副本。）

### 1.3 `Degraded` 是语义正确值的依据（读代码核对）

`src/workspace/diagnostics/diagnostics_controller.cpp` `finaliseState()`（~234-252）：

```cpp
const bool lspReady   = !lspFailed && lspStarted_ && report_.lspReceivedWholeSet;
const bool slangComplete = report_.slangAvailable && report_.slangRequested > 0
    && report_.slangCompleted >= report_.slangRequested && report_.slangFailures == 0;
if (!lspReady && !slangComplete)      state_ = Failed;
else if (!lspReady || !slangComplete) state_ = Degraded;
else                                  state_ = Ready;
```

verible 缺失 ⇒ LSP 腿**永远**不可能 ready；SLANG 可用且成功 ⇒ `slangComplete=true`
⇒ 恰好一条腿健康 ⇒ **`Degraded`**。所以：
**要修的是测试的期望值，不是产品**——测试改对了，产品代码未被改动（`diagnostics_controller.cpp`
mtime 18:54，早于 T67 的 19:28 改动；T67 只动 `diagnostics_ffi_selftest.cpp`）。

---

## 2. 第二命题：两种 SLANG 环境都真绿（我自己两种都跑）

在 `vedit-qt6-build:local` 重新构建 lane（configure/build exit 0）后：

```text
=== [A] SLANG UNAVAILABLE (default env) ===
CHECK PASS degrade.missing-verible-state  failed
CHECK PASS degrade.missing-verible-not-ready  failed
OBSERVED missing-verible-state=failed
CHECK PASS degrade.missing-verible-message  cannot open … spawning verible-verilog-ls at /nonexistent/…
VER-DIAGNOSTICS-FFI-SELFTEST checks=23 failures=0
A-EXIT=0

=== [B] SLANG AVAILABLE (VEDIT_SLANG=build/slang/out/slang) ===
CHECK PASS degrade.missing-verible-state  degraded
CHECK PASS degrade.missing-verible-not-ready  degraded
OBSERVED missing-verible-state=degraded
CHECK PASS degrade.missing-verible-message  cannot open … spawning verible-verilog-ls at /nonexistent/…
VER-DIAGNOSTICS-FFI-SELFTEST checks=23 failures=0
B-EXIT=0
```

→ 两种环境各 **23/23、exit 0**，且 message 均可诊断。与 writer 的 `15-f2-after-fix-both-envs.log` 一致。

---

## 3. 第三命题：证据与回归

| 子项 | 我的观测 | 结果 |
|---|---|---|
| `evidence/lsp-v2/` 补上 SLANG 成功路径且可复跑 | `16-f1-slang-success-path.log`：`a3c.slang-got-every-file requested=7 completed=7 failures=0`、`a3c.lsp-opened-whole-set opened=7/7`、`OBSERVED diagnostics=21`；我按同命令（`VEDIT_SLANG=build/slang/out/slang` + FFI selftest）复跑得到**同样结果** | ✅ |
| “不可证”的表述是否已改正 | **没有真正改正**：`09-report.md` §4 第 1 条仍写「**SLANG success is NOT proven.** `slang` is absent on this host (`command -v slang` empty)」，与 `16-f1` 直接矛盾；文末「Unchanged honest SKIPs」被改成残句「SLANG success is **SLANG success IS** now proven … (no binary; the typed degradation is proven)」 | ❌（F-1） |
| `01-08,10-13` 的 C9 头补 `git status --porcelain` | 已补：每份文件头部有 `# C9 header (added by T67 …)` + `collected_at_utc=11:28:59Z` + `git_rev_parse_HEAD=95c496e` + 完整 porcelain 列表；**原始** `collected_at`/`git_head`（10:57–11:11Z, 1f47e56）原样保留 | ✅ |
| 补记是否编造时间 | 否：原始采集时间未被改写，补记有独立的 `collected_at_utc`（11:28:59Z）与 HEAD（95c496e）。**但**没有任务书要求的字面 `backfill:` 标记（只有 "added by T67"），且措辞 "git_status_porcelain (at collection time, verbatim; …)" 对"哪个 collection time"有歧义 | ⚠️（F-2，Low） |
| 根 ctest（基线 16/16） | **我自己在 pinned 镜像内跑两次**：第一次 `100% tests passed, 0 failed out of 16`（CTEST-EXIT=0，含 #15 diagnostics-selftest、#16 diagnostics-ffi-selftest）；几分钟后第二次 `15/16`，唯一失败是 **#13 vedit-workspace-search-selftest**（T41/T64 search lane 正在改，非本任务写域）。host 侧直接跑会全 "Not Run"（build dir 里 add_test 的路径是容器前缀 `/vedit-build`），这是环境事实不是回归 | ✅（带外部归因） |
| `bash build/gates/typecheck.sh` 自己实跑 | `TYPECHECK-EXIT=0`（C++ -Werror + pinned 镜像内 Rust clippy；rustc/cargo 1.99.0；`[typecheck] PASS (pinned image vedit-build:local)`） | ✅ |
| T59 接线未回退 | `src/app/CMakeLists.txt:99` `VEDIT_WORKSPACE_LANES … project diagnostics`；`src/workspace/integration.cpp:41 installDiagnostics(shell, options);`；`diagnostics_integration.h:23` 有声明 | ✅ |

---

## 4. Findings

| ID | 严重度 | 症状 | 期望（修法） | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Medium** | `evidence/lsp-v2/09-report.md` §4.1 仍写「**SLANG success is NOT proven.**」并解释 "slang is absent on this host"，与同目录新证据 `16-f1-slang-success-path.log`（SLANG 腿 7/7 成功）**自相矛盾**；文末「Unchanged honest SKIPs」是**拼接残句**（"SLANG success is SLANG success IS now proven … (no binary;"），同一段里既说已证明又说无 binary | 把 §4.1 改写成一致的两句：默认环境（无 `VEDIT_SLANG`）SLANG 腿是 typed degradation；把 `VEDIT_SLANG` 指向仓库自带 `build/slang/out/slang` 时 7/7 成功（见 16-f1）。删除/改写「Unchanged honest SKIPs」残句；不得留 "NOT proven" 与 "now proven" 并存 | `evidence/lsp-v2/09-report.md:77-83`、`:162-165`；`evidence/lsp-v2/16-f1-slang-success-path.log`；我的复跑 `evidence/verdicts/lsp/t67-01-both-envs-and-negative.log` [B] 段 |
| **F-2** | **Low** | C9 补记头缺任务书要求的字面 `backfill:` 标记；"git_status_porcelain (at collection time, verbatim; …)" 中的 "collection time" 指 11:28:59Z（补记时刻）而非原始 10:57–11:11Z 的采集时刻，措辞有歧义 | 补记头写 `backfill: git_status_porcelain captured 2026-10-31T11:28:59Z (original run 10:57:06Z, HEAD 1f47e56)` 之类，明确"补记"与"原采集"两个时刻 | `evidence/lsp-v2/01..13` 头部第 1-6 行 |
| **F-3** | **Low（非本任务缺陷，归因）** | 根 ctest 当前不是稳定 16/16：#13 `vedit-workspace-search-selftest` 因 search lane（T41/T64）并行编辑而红；host 侧 ctest 全 "Not Run"（build dir 的 add_test 路径是容器前缀 `/vedit-build`） | 停点前在无并行编辑的树上重跑 `ctest --test-dir target/qt6-build`（容器内）确认 16/16；记录"host 跑 ctest 会 Not Run，必须在 pinned 镜像内跑"这一事实 | 我的两次 in-image ctest：一次 16/16 exit 0，一次 15/16 仅 #13 失败 |

**明确结论（任务书要求）**：**断言不是被废掉**——负例（喂 `Ready`+空 message）会让它 `checks=23 failures=3`、exit 1；正例两种环境都 23/23。修正方向正确（改测试、不改产品）。

---

## 5. 我未能验证 / 边界

1. **没有 split-key 映射 / 没有验证环**：T67/T68 在 cell 里没有 contract 文件、没有 `mpd_verify_*` loop，因此没有 `mpd_verify_record`；本 verdict 只能落盘 + 消息回传。
2. **GUI 可见行为**：无显示器，outline dock/诊断下划线等仍不可认证（与 T9 相同边界）。
3. **并行树的全局稳定性**：根 ctest 在我两次运行之间从 16/16 变 15/16（#13 search lane 在飞）；无法把"整树永远 16/16"作为结论，只能证明 **diagnostics 两条 ctest 稳定通过**、其余失败可归因到别的 lane。
4. **T67 到底改了哪些字节**：`src/workspace/diagnostics/` 与 `evidence/lsp-v2/` 都是 **untracked**，没有 git 基线可比；断言改动是我按 T9 时读到的旧断言（`== Failed`）与当前源码对比得出的，证据改动按 mtime（19:28）+ 内容比对判断。

---

## 6. 结论

- **第一命题（修正 vs 放宽）**：**是修正**，且**仍能真红**（负例 3 红；`!= Ready` 是新增的更严断言）。
- **第二命题（两环境真绿）**：**是**（A 23/23、B 23/23，均 exit 0；message 均可诊断）。
- **第三命题（证据与回归）**：SLANG 成功证据 ✅、C9 porcelain ✅（未编造原始时间）、root ctest ✅（带外部归因）、typecheck exit 0 ✅、T59 接线未回退 ✅；
  但 **“不可证”的表述未改正（§4 仍 NOT proven）+ 残句** → **Medium 发现**；`backfill:` 标记缺失 → Low。
- **判定：FAIL**（唯一原因是证据文本与自身证据矛盾/残句；代码与测试无需再改。修好 `09-report.md` 与补记头即可复审转 PASS）。
