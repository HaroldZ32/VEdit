# Slice 2 — tree-sitter/KSH overlay 单一 owner + 回退 + 增量性能（T18）

- cell: `cell-20261031T085543161Z` · board task **T17**（契约：`.mpd/cellwork/cell-20261031T085543161Z/contracts/ts-overlay.md`）
- **采集窗口（T93 返修的最终一轮重采）**：2026-11-01 **14:08:16 – 14:09:38** (Asia/Shanghai)（`01-build.log` 头 14:08:16 → `04-ctest.log` 落盘 14:09:38；逐份日志的 `collected_at` 为准）。
  **证据绑定**：本轮四份日志都记录被测 lane 的 `lane_sources` sha256（`src/syntax/overlay` 在 git 里仍 untracked，哈希是唯一绑定）。**T93 期间该 lane 的 `overlay_selftest.cpp` 被 lane 侧并发修改多次**（14:04:36 / 14:09:21 / 14:10:04 / 14:11:15…，且本路径的二进制在 14:12:49 又被重建），所以本文的数字与 40 项检查绑定到 **`overlay_selftest.cpp = 8cb45b65…`**（01/02 日志头）与当次构建的二进制；之后源码若再变，请按 §4 复跑。完整说明见 `07-binding.txt`。
- `git rev-parse HEAD`：`31a7c2d10111a9ab6078a433b1aa8289b31197a6`（每份日志头部都有采集时的 HEAD 与主题）
- `git status --porcelain`：**每份日志头部都有采集时刻的完整快照**（这是共享工作树，快照里同时出现其他 lane 的在飞文件；本任务写域只有 `evidence/slice2-overlay/`）
- 机器：AMD Ryzen 7 7840H w/ Radeon 780M Graphics · nproc=16 · kernel 7.0.0-38-generic · mem 30,496,356 kB · 采集时 loadavg 见各日志头
- 环境：pinned 镜像 `vedit-qt6-build:local`（`sha256:28a15c62…f1f77`；Qt 6.10.2 / KF6 6.24 / tree-sitter 0.25.9）；根构建 `target/qt6-build`

## 0. T93 返修说明（只改证据，功能代码未动）

T18 评审（F1–F5）指出本目录的证据缺陷，本次返修逐条处理如下（**没有改 `src/syntax/overlay/**` 一行**）：

| finding | 处理 |
|---|---|
| **F1（HIGH）时间伪造** | 原 README 的采集窗口（两个时间点）**没有任何日志支持，其中后一个时间点在写入时刻（约 13:38）之后——即所谓的“未来时间”**（原始值见评审 F1；本目录不再重复该无效数字）。本 README 与四份日志现已**整体重采**，窗口以日志头 `collected_at` 为准（最终一轮 14:08:16–14:09:38；此前 13:54 与 14:02 两轮都因 lane 源码继续改动而作废，见页首说明与 `07-binding.txt`）。原始采集（13:38:01–13:38:58）的时间戳与错误窗口的对照见 `05-time-audit.txt`。**没有用一个“看起来合理”的数字去补**，而是重跑并逐份写头。 |
| **F2（HIGH）C9 头缺失** | `01-build.log` / `02-selftest.log` / `03-perf-runs.log` / `04-ctest.log` 四份现在都有完整头：`collected_at` + `git_head` + `git_status_porcelain`（另加机器、镜像、命令）。另加 `lane_sources` 的 sha256（lane 在 git 里 untracked，哈希绑定被测代码）。这四份是**重采**，不是事后补记；`05-time-audit.txt` 明确区分“原始采集”与“本次重采”。`03/04` 末尾另有一条 `backfill:` 备注：其 `lane_sources` 快照在写入时已比被测二进制新（lane 被并发编辑），被测二进制的来源以 01/02 头 + `07-binding.txt` 为准。 |
| **F3（MEDIUM）性能缺机器信息** | 重跑 3 次并把 **CPU 型号 / nproc / kernel / mem / loadavg** 写进 `03-perf-runs.log` 的头（`02`/`04` 同样带）。 |
| **F4（LOW）未声明的合成 base 替身** | §6 第 6 条**明确声明** A1g 的 paint-plan 证据用的是合成 base 替身 `baseLineSpans()`，并改写结论措辞（见下）。 |
| **F5（LOW）ctest 声明过度** | `04-ctest.log` 现在记录**全量根 ctest**（`ctest --test-dir target/qt6-build`，20/20 PASS）**加上**本 lane 入口（1/1 PASS）；README 的措辞同步改为“全量根 ctest 20/20 绿 + 本 lane ctest 绿”。 |

## 结论

**AC-S2.1 的三条（A1g / A1h / C12+Open Q2）在本 lane 内达成，40 项校验 0 失败；全量根 ctest 20/20 绿、本 lane ctest 绿。**
证据分两类：**agent 可做**（本目录全部实测）与**仅用户可做**（带窗截图，见 §5）。

| 项 | 结论 | 证据 |
|---|---|---|
| A1g 无双重着色 | `OwnerArbiter::assertSingleOwner` 全文档 + 4 个已知区间全 PASS；喷涂计划是单引擎分区；`raw-overlap=663 B` 证明“底座与 overlay 本来重叠、被裁掉”这条路径真的被走到。**注意：这里的 base 是测试内的合成替身（§6.6），不是真 KSyntaxHighlighting span**；结论限于“裁剪路径与 arbiter 一致性在合成 base 上被验证”。 | `02-selftest.log` 的 `overlay.single-owner*` / `overlay.paint.*` |
| A1h 运行期关闭回退且不崩溃 | `setEnabled(false)` 立即释放全部 claim（`before=181 after=0`），所有权回到 base；100 次 enable/disable（中间还注入）不崩溃；`SyntaxService` 全局开关关掉时 `NoProvider`、无 claim、恢复后回到 `Active` | `overlay.fallback.*` / `overlay.service-switch.*` |
| C12 增量 + Open Q2 预算 | 1,051 行 / 25,388 B 合成 SystemVerilog：**50 次重建 median 23.986–24.172 ms（预算 150 ms）**；**单次注入 p95 0.145–0.147 ms（预算 2 ms）**；另有 1,009/2,017/5,041/10,081 行 4 档扫描（重建 8.66/9.51/12.31/35.88 ms，range-query median 0.065 ms）全部在预算内；滚动 50 个窗口后 claim 从不越出当前窗口；增量结果与“新控制器全量重建”逐区间一致（812=812） | `03-perf-runs.log`（3 次独立运行，含机器信息）、`02-selftest.log` 的 `overlay.perf*` / `overlay.perf-sweep.*`、`overlay.incremental.equivalence`、`overlay.scroll.window-isolated` |

## 1. 设计（单 owner 是怎么保证的）

- **所有权只有一个来源**：`src/syntax/provider` 的 `OwnerArbiter`（C16 策略，`POLICY.md` 冻结）。
  overlay 的每次着色都是 `claim(language, "tree-sitter", start, end)`；底座（KSyntaxHighlighting）
  是所有未被 claim 字节的隐式 owner。因此"双重着色"在数据结构上不可能出现，而不是靠调用方自觉。
- **喷涂计划同源**：`paint.cpp` 的 `buildPaintPlan()` 把 base spans 与 overlay spans 切成单引擎分区；
  overlay 覆盖的字节把 base span 裁掉（overlay 赢），并且把两个原始输入的重叠字节数
  （`raw_overlap_bytes`）记下来 —— 这就是"裁剪真的发生了"的判据（本次 663 B）。
  另有一条交叉校验：`overlay.paint.arbiter-agrees` 逐个 segment 比对"计划里的引擎"与
  "arbiter 里的 owner kind"，两者必须一致。
- **回退是同步的**：`setEnabled(false)` 先 `releaseAll()` 再改状态；`injectWindow` 在 disabled 下是显式
  no-op（不静默 claim）。`SyntaxService::setTreeSitterEnabled(false)` 让 `createProvider()` 返回 null，
  控制器进入 `NoProvider`，`ownerSpans` 全为 base。
- **增量**：overlay 只拥有"当前可见窗口"——`injectWindow(start,end)` 先释放全部旧 claim 再 claim 窗口内
  span（滚动是替换而不是累积，单帧工作量有界）。一次编辑 = 同步 provider（不能增量同步则整体重解析，
  永不崩）+ 刷新 span 缓存 + 只重注入窗口；`overlay.incremental.equivalence` 证明"编辑 20 次后的增量结果
  == 同一文本上新控制器的全量结果"（812 == 812 个区间）。

## 2. 性能（Open Q2 复测，3 次独立运行，最终一轮重采）

文档：合成 SystemVerilog，**1,051 行 / 25,388 B**（C12/A1f 的 "1k 行" 量级）。
机器（同 `03-perf-runs.log` 头）：AMD Ryzen 7 7840H（16 逻辑核）/ kernel 7.0.0-38-generic /
mem 30,496,356 kB / 采集时 loadavg 见日志头；运行在 pinned 镜像 `vedit-qt6-build:local` 内。

| 指标 | 预算 | run1 (14:08:56) | run2 (14:08:58) | run3 (14:09:01) |
|---|---|---|---|---|
| 50 次重建（median of 3 batches） | ≤ 150 ms | **23.986** | **24.172** | **24.041** |
| 单次注入 p95 | ≤ 2 ms | **0.147** | **0.145** | **0.145** |
| 单次注入 max | — | 0.179 | 0.187 | 0.231 |

（完整 `PERF` 行：`sync_us` 385–404、`query_us` 74–77；`02-selftest.log` 同一构建的单次完整自检
为 `rebuild50_ms=23.717 / p95=0.146`。）

**规模扫描**（`02-selftest.log` 的 `overlay.perf-sweep.*`，4 档文档，每档 3 项断言）：

| 文档行数 | 50 次重建 median | 单次注入 p95 | range-query median |
|---|---|---|---|
| 1,009 | 8.662 ms | 0.145 ms | 0.065 ms |
| 2,017 | 9.510 ms | 0.171 ms | 0.065 ms |
| 5,041 | 12.310 ms | 0.150 ms | 0.065 ms |
| 10,081 | 35.884 ms | 0.282 ms | 0.065 ms |

即：在这台记录机器上，**1k–10k 行范围内 50 次重建与单次注入都在预算内**，range-query 中位数稳定在
0.065 ms。单次重建的构成（`02-selftest.log` 的 PERF 行，微秒）：`text≈0` + `sync_us≈385–404` +
`query_us≈74–77`。注意：这是 headless 的**调用耗时**，不是帧率手感（§5/§6）。

## 3. 校验清单（40 项，0 失败）

`02-selftest.log` 原文（最终一轮重采），分组如下：

- **A1g（10 条 line）**：`overlay.inject.claims`、`overlay.single-owner`、`overlay.single-owner.overlay-present`、
  `overlay.single-owner.known-ranges`、`overlay.paint.segments/contiguous/partition-covers-the-range/
  raw-overlap-exercised/claims-never-base/arbiter-agrees`。
- **A1h（9 条）**：`overlay.fallback.state/claims-released/base-owns-everything/inject-is-noop/
  toggle-stress/reenabled`、`overlay.service-switch.no-provider/no-claims/restored`。
- **护栏（3 条）**：`overlay.guards.edit-out-of-range/double-close/no-provider-open`。
- **C12 + 性能（5 条）**：`overlay.incremental.equivalence`、`overlay.scroll.window-isolated`、
  `overlay.scroll.windows-nonempty`、`overlay.perf.rebuild-50`、`overlay.perf.injection-p95`。
- **规模扫描（12 条）**：`overlay.perf-sweep.{1009,2017,5041,10081}.{rebuild-50,injection-p95,range-query-bounded}`。
- **前置（1 条）**：`overlay.provider.grammar-activated`（真实 grammar 注册 + `SyntaxService` 激活）。

（10+9+3+5+12+1 = 40 条 CHECK；与 `SYN-OVERLAY-SELFTEST ok=1 checks=40 failures=0` 一致。）

## 4. 复跑

```sh
# 构建（根构建会自动纳入本 lane：VEDIT_LANE_DIRECTORIES 里已有 src/syntax/overlay）
bash scripts/build-qt.sh            # 或已有构建目录：cmake --build target/qt6-build --target vedit-syntax-overlay-selftest
cmake --build target/qt6-build --target vedit-syntax-overlay-selftest
# 校验（含性能）
QT_QPA_PLATFORM=offscreen target/qt6-build/src/syntax/overlay/vedit-syntax-overlay-selftest --selfcheck
ctest --test-dir target/qt6-build -R vedit-syntax-overlay-selftest
ctest --test-dir target/qt6-build          # 全量（04-ctest.log 记录的就是这一条 + lane 入口）
```

原始日志（全部为本次重采，头部 C9 完整）：`01-build.log`（构建，exit 0）、`02-selftest.log`（40 项全量，
exit 0）、`03-perf-runs.log`（3 次独立性能，含机器）、`04-ctest.log`（**全量 20/20 PASS** + lane 1/1 PASS）。
时间自审：`05-time-audit.txt`。

## 5. agent 可做 / 仅用户可做（契约要求标注）

| 判据 | 谁做 | 本次状态 |
|---|---|---|
| A1g 逐 range 单一 owner（arbiter + 喷涂计划交叉校验） | **agent 可做** | ✅ 已做（`overlay.single-owner*`、`overlay.paint.*`；base 为合成替身，见 §6.6） |
| A1g **人工可复核的截图**（开/关 overlay 两张图） | **仅用户可做** | ⏳ 未做：本环境无带窗会话（只有 offscreen）；需在有 DISPLAY 的机器上按 `src/editor` 现状截图 |
| A1h 运行期开关回退不崩（headless 判定） | **agent 可做** | ✅ 已做（含 100 次切换压力 + 服务级开关） |
| A1h 真实窗口里"关掉 tree-sitter 后肉眼颜色变化" | **仅用户可做** | ⏳ 未做（同上，需要窗口） |
| C12 增量 + Open Q2 预算 | **agent 可做** | ✅ 已做（3 次独立运行，含构成拆分与机器信息） |
| 真实编辑器里滚动大文件的帧率手感 | **仅用户可做** | ⏳ 未做（headless 只能测调用耗时，不能测帧） |

## 6. 边界（如实登记）

1. **性能文档是合成的**（25 KB、1,051 行、结构重复），不是真实工程的 1k 行 SV；真实工程里节点分布更不均衡，
   `highlightSpans` 的耗时可能更高。预算余量（150−24≈126 ms）与 1,009–10,081 行的扫描一起，是这种偏差的余量；
   但扫描文档也是合成的，**不能替代真实工程测量**。
2. **规模结论只到 10,081 行**：`overlay.perf-sweep.*` 的 4 档全在预算内（§2），但再往上没有测；
   且所有数字都是 headless 调用耗时（provider 同步 + span 查询 + 注入），不含绘制/合成/帧调度。
3. **"重建"定义**：本证据记的是 `applyEdit`（文本编辑 + provider 增量同步 + span 刷新 + 释放旧 claim），
   不含注入（注入单独计 p95）。这与契约的"50 条重建 / 单次注入"两个指标一一对应。
4. **无带窗证据**：截图与真实窗口行为是用户可做项（§5），本任务没有伪装成已做。
5. 未覆盖：编辑器画布（`src/editor`）如何消费 `buildPaintPlan()` —— 该集成不在本任务写域，本 lane 只提供
   单一 owner 的数据面与判据。
6. **A1g 的 base 是合成替身（T93/F4 明确声明）**：`overlay_selftest.cpp` 的 `baseLineSpans()`
   （`:120–136`，在 `:201` 用于 paint-plan 校验）是**每个非空行一个 span 的合成 base**，
   **不是** KSyntaxHighlighting 真引擎产出的 span。因此：
   - `raw-overlap=663` 是"合成 base span 与 overlay claim 的重叠"，它证明的是**裁剪路径被走到**
     与 **paint 计划与 arbiter 一致**；
   - 它**不**证明真 KSH span 下的重叠量、也不证明编辑器画布真的按这份计划上色——
     真 KSH span 的产出与 `src/editor` 的画布消费都在本 lane 写域之外（§5/§6.5）；
   - 结论措辞以上述为限，不把合成替身的结果说成真 base 的证明。
