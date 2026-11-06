# T29 · AC-8.1 — 真·无 `settings.toml` 首启的 Format 能力与文案

Cell `cell-20261102T100600176Z` · board **T29** · member **se-diag (Senior Engineer)**
契约：`.mpd/cellwork/cell-20261102T100600176Z/contracts/diag-format.md`（split rev 2，
frozen 2026-11-02T13:19:19Z）· 写域：`src/workspace/diagnostics/` + `evidence/g6-diag/`
基线：HEAD `4e2d0a5` + 前序 cell 工作树（开工时 236 条脏路径，见
`00-worktree-baseline.txt`）· 本 cell 不管理 git。

## 0. 结论

**缺陷存在，且与 T15 §6.3 的推断一致，但现在是被观测到的**：在真·无 `settings.toml`
首启（state dir 空、构造 shell 前/后都没有任何 settings 文件）时，`TOOLCHAIN` 行报
`verible-format:local`，而 Format 项 `enabled=0`，理由为
`verible-verilog-format not found (bundled, local checkout, PATH and env all empty)`
——**假理由**，且此时用同一份输入跑真实格式化能产出 73 字节输出（红档 `00-red-before-fix.log`，
21 检查 6 失败，exit 1）。

**修复后同一探针同一输入为绿**：Format `enabled=1`，`source='always-verible-format'`，
理由不再声称缺失；触发 `diagnostics.format` 后文档从 59 字节变为 73 字节，且与
`verible-verilog-format` CLI 的参考输出 **逐字节相同（sha256 一致）**；state dir 全程为空、
无任何 `settings.toml`（绿档 `30-green-after-fix.log`，21/21，exit 0）。

**缺失负控**（把同一二进制放到 checkout 之外、PATH 无 verible）：Format `enabled=0`，
理由 `verible-verilog-format: missing (probed …)`，**点名工具并列出真实探测过的 6 个候选路径**，
不出现任何 `verible-format:local` 之类的假来源声明（`40-missing-control.log`，21/21，exit 0）。

## 1. 复现方法（真·无文件首启）

探针：`evidence/g6-diag/probe/g6_diag_probe.cpp`（out-of-tree，按仓库 probe 惯例用
ninja 里 `src/app/vedit-app` 自己的 compile/link 行链接产品静态库）。

关键点：探针**不写任何 settings 文件**，并在构造 shell **之前**断言并打印：

```
PRECONDITION state-dir='/tmp/g6-diag-probe-…/state' entries=<empty> settings-toml=absent
CHECK precondition.state-dir-empty PASS
CHECK precondition.no-settings-toml PASS
```

随后 `installDiagnostics()` 打印产品自己的 `TOOLCHAIN …` 行（探针不伪造该行）：

```
TOOLCHAIN source=verible:local slang:local missing=[] aux source=slang-server:missing \
  verible-format:local verible-lint:local missing=[slang-server] local=[…]
```

运行（主机侧包装，容器内构建+运行，`--user` 跟随仓库 `run_in_qt6_container` 规则）：

```bash
bash evidence/g6-diag/probe/run.sh 30-green-after-fix available   # 期望 0
bash evidence/g6-diag/probe/run.sh 40-missing-control missing     # 负控，期望 0
```

## 2. 根因（代码事实）

1. `src/workspace/diagnostics/diagnostics_integration.cpp:513`
   `BackendWiring` 构造 `state_(lsp, lint)`；`BackendSwitchState` 里的
   `toolchain_` 是**默认构造的空报告**。
2. 唯一填充它的是 `BackendSwitchState::apply()`（`ffi_backends.cpp:802`
   `toolchain_ = toolchain;`），而 `apply()` 只由 `SettingsWatcher` 的回调
   （`applySettings()`，同文件 `:629`）调用。
3. `SettingsWatcher::reload()` 在文件不存在时**直接 return**
   （`diagnostics_integration.cpp:416` 的 `if (!QFileInfo::exists(path_))`），
   所以真·无文件首启永远不会有 `apply()`。
4. 因此 `state_.gate("formatting")`（`ffi_backends.cpp:900`）读到
   `toolchain_.veribleFormat.found()==false`，`decideActionGate()`
   （`ffi_backends.cpp:730-739`）给出 `enabled=false` 与
   “verible-verilog-format not found (bundled, local checkout, PATH and env all empty)”：
   **能力判定来自“settings 是否 apply 过”，而不是来自真实探测**。
5. 同一条链还决定了 `formatDocument()`（`diagnostics_integration.cpp:853`）
   使用 `state_.toolchain().veribleFormat.path`，在种子缺失时为空——即便把门打开也
   无法格式化，所以“只改 enabled 文案”是不够的。
6. T15 §6.3（`evidence/e4-actions/REPORT.md`）的旧探针在构造 shell **之前**写了空
   `settings.toml`（`evidence/e4-actions/probe/editor_actions_probe.cpp:270`），
   因此“无文件”路径从未被观测；本探针不写该文件，缺陷因此可见。

## 3. 修复（最小面，3 处）

| 文件 | 改动 |
| --- | --- |
| `src/workspace/diagnostics/diagnostics_integration.h:94` | 新增 `BackendSwitchState::seedToolchain(const ToolchainReport&)` 声明（带 AC-8.1 说明：CLI 能力是 TOOLCHAIN 事实，不是 settings 事实） |
| `src/workspace/diagnostics/ffi_backends.cpp:792` | 实现 `seedToolchain`（只写 `toolchain_`）；`gate()` 在 Format 缺失时用 `toolchain_.veribleFormat.describe()` 作为 reason——真实工具名 + 真实探测过的候选路径 |
| `src/workspace/diagnostics/diagnostics_integration.cpp:523` | `BackendWiring` 构造体内、`SettingsWatcher` 之前调用 `state_.seedToolchain(toolchain_)`，让门在第一次 `publishGates()` 前就有真实报告 |

语义边界：`apply()` 仍然覆盖 `toolchain_`（含 `tools.*` 覆盖），所以 settings 提供的路径
不会被种子掩盖；种子只解决“settings 尚未 apply”的空窗。未改测试、未改门禁断言、未动编辑器/i18n 面。

## 4. fix-first 先红后绿（同一探针、同一输入）

| 观察项 | 修复前 `00-red-before-fix.log` | 修复后 `30-green-after-fix.log` |
| --- | --- | --- |
| `TOOLCHAIN` 行 | `verible-format:local` | `verible-format:local`（不变） |
| `GATE action=formatting` | `enabled=0 … reason='verible-verilog-format not found (bundled, local checkout, PATH and env all empty)'` | `enabled=1 … reason='formatting always uses verible-verilog-format, independent of the LSP backend'` |
| `diagnostics.format` QAction | `enabled=0`，同假理由 | `enabled=1` |
| 触发动作后文档 | 59B → 59B，`changed=0` | 59B → 73B，`changed=1`；产品自打印 `FORMAT backend=verible formatter=…/verible-verilog-format changed=1 enabled-by='always-verible-format' in-bytes=59 out-bytes=73` |
| 与 CLI 参考输出比对 | `reference=73B`，文档未变 → FAIL | 文档 sha256 `fd85f6a8…eb7018` == CLI 参考 sha256 → PASS |
| 探针结果 | `exit=1 checks=21 failures=6` | `exit=0 checks=21 failures=0` |
| state dir / settings.toml | `<empty>` / absent（前后） | `<empty>` / absent（前后） |

产物对照：`format-before.sv`（59B 未格式化）与 `format-after.sv`（73B 真实格式化结果）。
负控产物在 `evidence/g6-diag/missing/`（59B → 59B，动作被禁用，未格式化）。

## 5. 缺失负控（“确实缺失时才置灰”）

把同一二进制复制到 checkout 之外（`/tmp/g6-missing`）、清空 verible 相关 env 并把
`PATH` 收窄为 `/usr/bin:/bin` 后运行：

```
TOOLCHAIN source=verible:missing slang:missing … verible-format:missing …
GATE action=formatting present=1 enabled=0 known=1 source='always-verible-format' \
  reason='verible-verilog-format: missing (probed /tmp/g6-missing/verible-verilog-format, \
  /tmp/g6-missing/bin/verible-verilog-format, /tmp/g6-missing/../lib/vedit/bin/verible-verilog-format, \
  /tmp/g6-missing/../share/vedit/bin/verible-verilog-format, /usr/bin/verible-verilog-format, \
  /bin/verible-verilog-format)'
CHECK reason.names-the-missing-tool PASS
CHECK reason.names-the-probed-candidates PASS
CHECK reason.no-fake-local-claim PASS
CHECK format.document-unchanged-when-missing PASS
```

即：缺失时置灰、理由点名真实缺失项（工具名 + 实际探测路径），不出现假来源。

## 6. AC-8.1 三句逐条对账

| AC-8.1 原文要求 | 观测证据 | 结论 |
| --- | --- | --- |
| 真·无 `settings.toml` 首启时 Format 状态与 `TOOLCHAIN` 实际能力一致 | 红：TOOLCHAIN `verible-format:local` vs gate `enabled=0` → 探针 `gate.formatting-matches-toolchain` FAIL；绿：同项 PASS；负控 TOOLCHAIN `verible-format:missing` vs `enabled=0` → PASS | **满足** |
| 可用时必须真可用 | 绿档触发真实动作：文档 59→73B，且与 `verible-verilog-format` CLI 参考输出 sha256 一致（`format.matches-the-formatter-cli` PASS） | **满足** |
| 确实缺失时才置灰，文案点名真实缺失项、无假理由 | 绿档理由不含 missing/not found；负控理由 `verible-verilog-format: missing (probed …)` 点名工具与 6 个真实候选，且 `reason.no-fake-local-claim` PASS | **满足** |
| 探针证明构造前 state dir 为空且无 settings 文件（不得写 settings.toml 绕过） | `PRECONDITION … entries=<empty> settings-toml=absent`（构造前）与 `POSTCONDITION … <empty> settings-toml=absent`（运行后），两次检查均 PASS；探针源码不含任何写 settings 的代码 | **满足** |

## 7. 门禁（观测输出 + exit code）

| 命令 | 结果 |
| --- | --- |
| `bash scripts/build-qt.sh`（契约门禁，含 Rust 层、cmake、构建、ctest、xcb 冒烟） | **exit 0**（`50-gate-build-qt-exit.txt`）；日志尾部 `100% tests passed, 0 tests failed out of 38`、`[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke`（`50-gate-build-qt.log:1056,1066`）。注：本轮实测 ctest 为 **38** 项（AC-0.2 记录时 37 项） |
| lane diagnostics 单测 `ctest -R 'diagnostics\|toolchain\|orchestration'` | **exit 0**，`100% tests passed, 0 tests failed out of 6`：`vedit-lint-orchestration-probe`、`vedit-workspace-diagnostics-selftest`、`vedit-toolchain-missing-selftest`、`vedit-diagnostics-ffi-selftest`、`vedit-diagnostics-product-seam`、`vedit-toolchain-ux-selftest` 全 Passed（`51-lane-diagnostics-ctest.log`） |
| `vedit-app --selfcheck --state-dir /tmp/sc-g6`（回归对照，非本任务门禁） | **exit 0**，`APP-SHELL-SELFTEST ok=1 checks=245 failures=0`（`52-selfcheck.log`） |
| 探针红/绿/负控 | `00-red…` exit 1（6 失败，预期的红）；`30-green…` 与 `40-missing…` exit 0（各 21/21） |

另：`53-seam-verbose.log` 是 `vedit-diagnostics-product-seam` 的详细输出——该测试与探针用同一套
产品接线（`ApplicationShell` + `installDiagnostics`），自身不写 `settings.toml`，它在临时 state dir
下真实打开了 LSP 并分析出 1 条诊断（`seam.analysis-after-open PASS`、
`session='backend=verible · buffer changed …'`）。据此**推断**：“无 settings 首启时诊断 lane 本身仍工作，
Format 的门只是读错了报告”，而不是整条 lane 死了；该测试的 state dir 在退出时被删除，未直接留证。

## 8. 未证明 / 边界（请连同结论一起读）

1. **只测了 offscreen**。契约里“如 xcb 可行补截图”是可选补强；AC-8.1 的判据是门状态 + 真实
   格式化文本，本报告用日志与 `format-before/after.sv` 文件给足，未产出 xcb 截图。
2. **探针的“真实能力”对照**用产品自己的 `applicationToolchainContext()`/`resolveToolchain()`
   （与 `installDiagnostics` 同一解析器）来取得 `veribleFormat.found()`，再用**独立**手段
   证明其可用：路径可执行 + 用该路径跑 CLI 得到参考输出并与文档结果逐字节比对。两者都做了；
   但没有用 `strace`/外部观测证明「同一进程内只有这一个解析结果」——属推断。
3. **settings 提供 `tools.verible_format` 的路径**未在本次探针中覆盖（那是 T11 的 settings
   apply 路径，`evidence/w5-backend/` 有对照）；本修复不改变 `apply()` 的覆盖语义，所以未新增
   未测分支，但没有新的针对该分支的证据。
4. **观察到但不在本任务 scope 的相邻问题（未修改）**：缺失负控里 `GATE action=definition
   enabled=1 reason=''`，而 `TOOLCHAIN` 报 `verible:missing`——backend 动作的门取自 backend
   能力表而非缺失的 toolchain（T11 能力表 basis 面）。这是 `definition/references/rename`
   一族的事，不属 AC-8.1 的 Format 判据，按纪律只报告不吸收。
5. `slang-server` 在环境里缺失（`aux … missing=[slang-server]`）是既有环境事实，与 Format 无关。
6. 工作树在开工前已有 236 条脏路径；运行门禁后**没有出现任何新的 scope 外路径**
   （比对见 §10）。门禁本身按仓库设计会重写 `build/evidence/**`、`evidence/m0-build/**`、
   `evidence/ffi-abi/logs/**` 等既有构建日志——它们是前序工作树里本就修改过的路径。

## 9. 证据索引

```
evidence/g6-diag/
  00-worktree-baseline.txt           开工基线（git status --porcelain=v1，236 行，只读）
  00-red-before-fix.log              红：真·无文件首启，Format enabled=0 + 假理由，6 失败，exit 1
  00-red-before-fix-build.log        红档探针构建（含产品 ninja）
  00-red-before-fix-exit.txt         "1"
  30-green-after-fix.log             绿：Format enabled=1，真实格式化 59→73B，与 CLI 一致，21/21，exit 0
  30-green-after-fix-build.log       绿档探针构建（sha256 b6077ebb…906b）
  30-green-after-fix-exit.txt        "0"
  40-missing-control.log             负控：toolchain 缺失 → 置灰 + 真实缺失理由，21/21，exit 0
  40-missing-control-build.log       负控探针构建
  40-missing-control-exit.txt        "0"
  50-gate-build-qt.log / -exit.txt   bash scripts/build-qt.sh（ctest 38/38）、exit 0
  51-lane-diagnostics-ctest.log/-exit.txt  lane 6 个测试全绿、exit 0
  52-selfcheck.log                   vedit-app --selfcheck 245 checks、exit 0
  53-seam-verbose.log                无 settings 首启下 LSP 真实分析（辅助说明）
  format-before.sv / format-after.sv 真实格式化前后文本（59B / 73B）
  missing/format-before.sv / -after.sv  负控前后文本（59B / 59B）
  probe/g6_diag_probe.cpp            探针源码（本报告所有 CHECK 的来源）
  probe/build.sh                     探针构建（仓库 probe 惯例）
  probe/run.sh                       主机侧包装：容器内构建 + 运行 + 落 log/exit
```

复现最短路径：

```bash
bash evidence/g6-diag/probe/run.sh 30-green-after-fix available
bash evidence/g6-diag/probe/run.sh 40-missing-control missing
bash scripts/build-qt.sh
ctest --test-dir target/qt6-build -R 'diagnostics|toolchain|orchestration'
```

## 10. 改动路径（全部在写域内）

```
src/workspace/diagnostics/diagnostics_integration.h      +seedToolchain 声明与说明
src/workspace/diagnostics/diagnostics_integration.cpp    BackendWiring 构造体内 seed（:523）
src/workspace/diagnostics/ffi_backends.cpp               seedToolchain 实现（:792）、gate() 真实缺失理由（:900）
evidence/g6-diag/**                                      本报告 + 探针 + 全部日志/产物
```

门禁运行前后 `git status --porcelain=v1` 的路径集合差分为空（没有新的 scope 外路径）；
scope 外被门禁重写内容的是前序工作树里本就处于 modified 的构建日志。
