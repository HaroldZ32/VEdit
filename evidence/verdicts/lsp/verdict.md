# T9 独立评审结论 — T8 `verible-lsp`（Verible LSP Rust 服务 + 诊断/跳转/大纲接入）

- **评审者**：Reviewer（cell `cell-20261031T085543161Z` 的 Reviewer 实例；verifier ≠ writer `d842e2f9…`）
- **被评审任务**：board `T8`（kind=work，owner=Senior Engineer；lane 已由 `13462f3` checkpoint 提交，Qt 侧与证据仍未提交）
- **冻结契约**：`.mpd/cellwork/cell-20261031T085543161Z/contracts/verible-lsp.md`（split revision 2）
- **验证环**：`loop-20261031T095153-27eeb3`（`mpd_verify_seat` 已绑定本会话）
- **结论**：**PASS**（bound seat；契约判据 1/2/4 满足，判据 3 的"claims"里有两处 Low 级不准确，见 F-1/F-2）
- **非阻塞发现**：3 条 Low（F-1 SLANG 成功路径被低估、F-2 降级断言环境耦合、F-3 C9 头缺 porcelain）
- **证据目录**：`evidence/verdicts/lsp/`（本文件 + 01/02/03 三份可复跑证据）

---

## 1. 我实际跑的证据（命令 + 观测输出）

### 1.1 Rust 服务（pinned 镜像 `vedit-build:local`，CentOS 7 / cargo 1.99.0）

```text
$ run_in_build_container cargo test -p vd-lsp --lib --test session_degradation --test session_file_set -- --nocapture
running 21 tests ...                                  test result: ok. 21 passed
running 1 test  (session_degradation) ...            test result: ok. 1 passed
running 2 tests (session_file_set) ...               test result: ok. 2 passed
OBSERVED set=full expected=7 opened=7 diagnostics=3
OBSERVED definition locations=1
OBSERVED reference locations=1
OBSERVED rename edits=2
OBSERVED formatting edits=1
OBSERVED set=reduced expected=3 opened=3 diagnostics=0
TESTS-EXIT=0
$ cargo fmt --all -- --check              -> FMT-EXIT=0
$ cargo clippy --workspace --all-targets -- -D warnings -> CLIPPY-EXIT=0
```

→ 六类能力（诊断/跳转/引用/重命名/格式化/大纲）在 **7 文件工程集**上全部有真实答案；
文件集替换后 `opened` 从 7 变 3（A3d 的可观测计数）。输出里**没有 SKIP 行**（verible 确实参与）。

### 1.2 Qt 侧 lane（`vedit-qt6-build:local`，headless/offscreen）

```text
$ cmake -S src/workspace/diagnostics -B /tmp/diag -DVEDIT_RUST_FFI_LIB=target/release/libvd_ffi.a ... && build
CONFIGURE-EXIT=0   BUILD-EXIT=0
$ ctest --test-dir /tmp/diag --output-on-failure
100% tests passed, 0 tests failed out of 2          CTEST-EXIT=0
$ vedit-workspace-diagnostics-selftest --selfcheck
VE-DIAGNOSTICS-SELFTEST checks=24 failures=0       FAKE-EXIT=0
$ vedit-diagnostics-ffi-selftest tests/fixtures/verilog-project .mpd/recon/verible/bin/verible-verilog-ls
CHECK PASS a3c.lsp-opened-whole-set   opened=7/7
CHECK PASS a3c.slang-got-every-file   requested=7 completed=7 failures=7
CHECK PASS degrade.missing-verible-failed/message  (typed message names the missing binary)
VER-DIAGNOSTICS-FFI-SELFTEST checks=22 failures=0  FFI-EXIT=0
```

### 1.3 我额外补的 A3c「双腿都成功」证据（writer 没做）

仓库已有自建 slang 二进制 `build/slang/out/slang`（mtime 18:48，**早于** writer 采集证据的 18:55）。
我用 `VEDIT_SLANG` 指向它重跑真实 FFI selftest：

```text
CHECK PASS a3c.slang-got-every-file   requested=7 completed=7 failures=0   ← SLANG 腿对同一 7 文件全部成功
OBSERVED diagnostics=21 lsp_message='ready: 7/7 file(s) open, 3 diagnostic(s), 0 skipped'
CHECK FAIL degrade.missing-verible-failed   degraded
VER-DIAGNOSTICS-FFI-SELFTEST checks=22 failures=1
```

→ **A3c 成立且比我预期的更强**：同一 7 文件集，LSP 7/7 打开、SLANG 7/7 成功。
→ 同时暴露 F-2：`degrade.missing-verible-failed` 断言只在"slang 也不可用"时成立；slang 可用时控制器正确地
给出 `Degraded`（一条腿好、一条腿坏），该断言失败。

### 1.4 门禁（契约判据 2）

```text
$ mpd_verify_evidence {kind:gate, gate:typecheck, loop_id:loop-20261031T095153-27eeb3}
gate typecheck exit 0 -> .mpd/verify/evidence/ev-20261031T111640-18b39a.log   （C++ -Werror 构建 + Rust clippy）
```

补充核对（独立于 writer 报告）：根产品 `vedit-app`（71.6 MB，19:17）导出 **174** 个
`vedit::workspace::diagnostics` 符号；根 ctest **16/16** 含 `#15 vedit-workspace-diagnostics-selftest`、
`#16 vedit-diagnostics-ffi-selftest`；`src/app/CMakeLists.txt:99` 的 lane 列表含 `diagnostics`。
这些属于 **T59 产品接线（另一张票，写域在 T8 之外）**，不构成 T8 的判据，只作为"产品里真的接上了"的交叉核对。

### 1.5 C2（不阻塞 UI 线程、无每按键跨界调用）静态审计

- `src/workspace/diagnostics/` 全目录**没有**任何 `textChanged`/`contentsChanged`/`cursorPositionChanged`/
  `keyPressed` 等编辑器信号连接（`grep` 零命中）→ 无按键路径跨界调用。
- Rust `SessionHandle::request()`（`crates/vd-lsp/src/session.rs:426-448`）是 `mpsc` channel `send` 到
  worker 线程并立即返回 job id；Qt 侧 `requestXxx()` 返回 job id，由 `waitForPolled()`/定时器轮询结果
  （selftest 的 `queued`/`answered` 两段）。
- 产品触发点：`diagnostics_integration.cpp` 的 `project.reparse` 命令 + 定时器驱动的工程模型 job，非按键直调。

---

## 2. 契约逐条对照

| 契约要求 | 证据 | 结果 |
|---|---|---|
| 诊断/跳转/引用/重命名/格式化/大纲在工程文件集下可用（AC-S1.4） | 1.1 六类均有真实答案；1.2 六类入口 + 大纲 5 条 | ✅ |
| 同一生效文件集同时喂 SLANG 与 Verible（A3c） | 1.2/1.3：`a3c.lsp-opened-whole-set opened=7/7` 与 `a3c.slang-got-every-file requested=7 completed=7`；fake selftest 的 same-set 两条 | ✅（我补上了双腿成功的证据） |
| 文件集变更后重解析状态可观测（A3d） | 1.1 `replacing_the_file_set…`（3/3,0 diagnostics）；1.2 `a3d.reparsing-observable`→终态、`a3d.generation-advanced` | ✅ |
| C2：C ABI + opaque handle + 不阻塞 UI + 不做每按键跨界调用 | 1.5 静态审计 + 1.1/1.2 的 job/poll 异步形态 | ✅（静态） |
| verible 缺失降级可诊断、不崩溃 | 1.1 `session_degradation` 1/1；1.2 `degrade.missing-verible-failed/message`（消息点名缺失路径）；1.3 同（状态 Degraded，消息仍在） | ✅ |
| 证据 `evidence/lsp-v2/`（版本/命令/输出/降级路径） | 14 份文件，00-context 有完整 C9 头（时间/HEAD/porcelain/镜像/版本）；C9 缺 porcelain 见 F-3 | ✅（有 1 条 Low） |
| 写域 `crates/vd-lsp/`、`crates/vd-deeplint/`、`crates/vd-ffi/src/lsp.rs`、`src/workspace/diagnostics/`、`evidence/lsp-v2/` | `git status`：`M vd-lsp/src/session.rs`、`M vd-lsp/tests/*`、`M vd-ffi/src/lsp.rs`、`?? src/workspace/diagnostics/`、`?? evidence/lsp-v2/`；`13462f3` 提交了 vd-lsp 四个文件。写域外文件（`src/app/CMakeLists.txt`、`src/workspace/integration.cpp`、`vd_ffi.h` 生成物）在 writer 的 10-scope-check 里被标 OTHER-LANE，与 T59 归因一致 | ✅ |
| GTK 隔离（vd-lsp 去 GTK） | 1.3 审计：`cargo tree -p vd-lsp / -p vd-ffi / -p vd-deeplint` 中 gtk/sourceview 命中均为 **0** | ✅ |

---

## 3. 发现（findings）

| ID | 严重度 | 症状 | 期望 / 说明 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low** | writer 报告 §5/§6 称 "SLANG success is not provable on this host (no binary)"；但 `build/slang/out/slang` 在证据采集（18:55）**之前**就已存在（mtime 18:48），只是不在 deeplint 的 4 个默认搜索位置。我设 `VEDIT_SLANG` 后真实 FFI selftest 的 SLANG 腿对同一 7 文件 **全部成功**（`requested=7 completed=7 failures=0`） | 证据应补一次 SLANG 成功路径（或至少不要断言"不可能"）；A3c 的判据本身**已由我独立证明成立** | `evidence/lsp-v2/00-context.txt:5-6`（"slang: ABSENT on the host"）vs `ls -la build/slang/out/slang`（18:48）；`evidence/verdicts/lsp/02-qt-lane-and-slang-success.log` 末段 |
| **F-2** | **Low** | `diagnostics_ffi_selftest.cpp:234` 的 `degrade.missing-verible-failed` 硬断言 `BackendState::Failed`。该状态只在"LSP 坏 + SLANG 也不可用"时出现（`diagnostics_controller.cpp:234-252` 的状态聚合）；当 SLANG 可用且成功时，缺 verible 的正确状态是 `Degraded`（一条腿好、一条腿坏），断言因此失败（22 checks/1 failure） | 契约原文是"verible 缺失时**降级可诊断、不得崩溃**"——`Degraded` 正是"降级"；断言应接受 `Degraded` 或 `Failed`（并可保留 message 断言）。这是测试环境耦合，不是产品缺陷 | `src/workspace/diagnostics/diagnostics_ffi_selftest.cpp:234-237`；`diagnostics_controller.cpp:234-252`；01.3 运行输出 |
| **F-3** | **Low** | C9 证据头：`evidence/lsp-v2/` 的 12 份日志/报告（01-08, 10-13）只有 `collected_at` + `git_head`，没有 `git status --porcelain`；只有 `00-context.txt` 带完整三项 | 按 `requirements.md` C9 每份证据文件头都带"采集时间 + HEAD + 树状态" | `evidence/lsp-v2/01..13` 头部实测（`03-audits-and-gate.txt` §6） |

**Observation（非 finding）**：
- writer 报告 §7 的 T59 产品接线（`src/app/CMakeLists.txt:99`、174 符号、16/16 ctest）**在本轮评审时刻确实成立**
  （我独立核过），但它属于 T59 的写域，不计入 T8 的判据；T8 自身在 Qt 侧只交付 lane 与 selftest。
- `crates/vd-ffi/include/vd_ffi.h` 在写域外但由 `build.rs`（cbindgen）生成，非手改；writer 的 10-scope-check 已注明。

---

## 4. 我无法验证的部分（诚实声明）

1. **GUI/视觉与真人交互**：本机无显示服务器。outline dock、诊断面板的实际外观与交互未认证；所有结论来自
   headless selftest / 单元测试（L2 级）。
2. **SLANG 诊断质量**：只核到"7/7 文件成功、诊断条数（21）"，未核 SLANG 报出的诊断内容是否正确/有意义
   （需要真实工程与人工判读）。
3. **C2 的运行时度量**：我做的是静态审计（无按键信号连接、请求走 channel/job + 轮询）；没有用事件循环
   profiler 证明"UI 线程零阻塞"。
4. **大文件/异常工程**：file set 只有 7 个 fixture 文件；10k 级文件集的会话性能未测（不在本契约）。
5. **真实 verible 版本矩阵**：只用 `.mpd/recon/verible/bin/verible-verilog-ls`（v0.0-4298-g085eef05）一个版本。

---

## 5. 结论

**PASS**。AC-S1.4 六类能力在 7 文件工程集上真实可用（Rust 21+1+2 测试、Qt 24/24 与真实 verible 的 22/22）；
A3c 我用 `VEDIT_SLANG` 补证了"同一文件集双腿都成功"（7/7 + 7/7）；A3d 的文件集替换与代数推进可观测；
C2 的异步/轮询形态与"无按键跨界调用"静态成立；verible 缺失两条路径都是 typed degradation、无崩溃；
GTK 隔离 0 命中；仓库声明的 `typecheck` 门禁 exit 0（`ev-20261031T111640-18b39a`）。

三条 Low 都是**证据/测试健壮性**问题，不是判据未达成：F-1 补一次 SLANG 成功证据即可；F-2 把降级断言放宽为
`Degraded|Failed`（并保留 message 断言）；F-3 按 C9 补 porcelain 头（可并入批量 C9 返修）。
