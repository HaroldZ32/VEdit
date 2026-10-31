# T85 — 缺工具链时 diagnostics/outline 的“刷屏”→ 面板级单条状态 + 裸构建可用

collected_at: 2026-10-31T23:32:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
（提交由 Lead 统一负责；本任务未 commit、未 push。）

无头声明：本机没有显示服务器。下列结论全部是无头可证的（文本计数、行数、字节数、条目级别、
ctest、selftest）；面板在真实屏幕上的**换行/观感**按任务要求标 **UNVERIFIABLE**。

---

## 1. 复现与诊断（先查清再改）

**场景**：裸构建产物 `target/qt6-build/src/app/vedit-app`，打开 `tests/fixtures/verilog-project`
（7 个文件），PATH 里没有 `verible-verilog-ls` / `slang`。

**修复前实测（`01-before-product-run.log`，旧二进制）**：

```
PATH-TOOLS: none
total output lines: 9
occurrences of the user-facing message: 2        <-- 同一条内容出现两次
longest line length (bytes): 1289                <-- 单行 1289 字节的“墙”
[app-shell] Diagnostics unavailable: verible-verilog-ls not found
  (bundled, PATH, env — all empty; probed /vedit-build/target/qt6-build/src/app/verible-verilog-ls,
   /vedit-build/…/bin/verible-verilog-ls, …)        <-- 20 条候选路径全塞在一条消息里
```

**诊断结论（逐条回答任务里的三个事实）**：

1. **来源**：不是“每打开一个文件插一条”。条目来自 **`installDiagnostics()` 的一次启动**——
   `ToolchainReport::userMessage()` 把**每个候选探测路径（两个工具共 20 条）**拼进同一条消息；
   这段文本在 UI 里被 `QLabel` 自动换行成一屏，看起来就是“一堆错误”。
2. **重复两次**：集成代码里既调了 `shell.addShellDiagnostic(userMessage())`（shell 内部
   `qWarning` 一次），又显式 `qWarning("diagnostics: %s", userMessage())` —— 所以同一条出现 **2 次**
   （见上：`occurrences: 2`）。
3. **不是条目级 Error**：`controller.diagnostics()` 本来就没有为缺失工具伪造任何 `DiagnosticEntry`
   （severity 1 = error 的条目数为 0）；问题只在**文案长度/重复**与**面板把整段文本当状态显示**。
   顺序也是对的（bundled → PATH → env），但**裸构建的本地产物（`build/slang/out/slang`、
   `scripts/fetch-verible.sh` 的输出）完全不在发现链里**，所以开发机明明有工具也报“missing”。

## 2. 修复前 / 修复后（同一场景，可复跑）

| 指标 | 修复前（旧二进制） | 修复后（新二进制） |
|---|---|---|
| 用户可见消息出现次数 | **2** | **1** |
| 单条消息字节数 | **1289**（含 20 条候选路径） | **459**（`03-after-missing-state.log` 原文） |
| 消息里的候选路径墙 | 有（`probed ...`） | 无（默认运行 `probed` 出现 0 次；`VEDIT_TOOLCHAIN_TRACE=1` 才输出到 qDebug） |
| 面板状态行数 | 1 行但折成一屏 | **1 行**（446 字符，单行断言 PASS） |
| outline 行数 | 0 | **0** + 一行空状态原因 |
| 伪造的 error 级条目 | 0 | **0**（断言 `panel.no-error-entries`） |
| 裸构建（仓库内）工具链 | **missing**（明明有 `build/slang/out/slang` 也不用） | **`source=verible:local slang:local missing=[]`**（`local=[build/slang/out,.mpd/recon/verible/bin]`） |

修复后的单条消息（原文，459 字节）：

```
Diagnostics unavailable: verible-verilog-ls, slang not found (bundled, local checkout, PATH and env all empty).
Impact: Verible diagnostics, navigation and the outline are unavailable; SLANG deep lint is unavailable.
Fix: bundle `verible-verilog-ls`/`slang` in the product's bin/ next to vd-app,
run `bash scripts/fetch-verible.sh` and `bash build/slang/build.sh` in a source checkout,
install them on PATH, or set VEDIT_VERIBLE_LS / VEDIT_SLANG.
```

## 3. 改了什么（写域内）

| 文件 | 改动 |
|---|---|
| `toolchain.{h,cpp}` | 发现链新增 **local checkout** 层：从产品二进制向上找到 repo 根（标记 `scripts/fetch-verible.sh`），探测 `<repo>/build/slang/out`、`<repo>/.mpd/recon/verible/bin`、`<repo>/target/verible-pin/extract/verible-*/bin`；顺序 **bundled → local → PATH → env**，新来源标签 `local`；`userMessage()` 改为**一条简洁消息**（不再包含探测路径），候选清单移到新增的 `probeDetail()`（仅日志/selfcheck）；`toolchainStatusLine()` 增加 `local=[…]`，`--selfcheck` 可直接看到本地产物是否被发现 |
| `diagnostics_panel.{h,cpp}` | 状态行保持“一条”；新增 **一行空状态**（outline 无符号时说明原因，缺工具链则写缺哪个/一行修复提示），并提供 `outlineRowCount()`/`emptyStateText()`/`emptyStateVisible()`/`controller()` 供无头断言；绝不把缺失工具链变成 N 条错误行 |
| `diagnostics_integration.cpp` | 删除重复的显式 `qWarning`（现在只有 shell 的 1 条）；`TOOLCHAIN` 行带上 `local=[…]`；候选路径细节改为 **opt-in** `VEDIT_TOOLCHAIN_TRACE=1` 时的 `qDebug`（默认运行零噪音） |
| `toolchain_selftest.cpp` | 新增：简洁消息（单行/长度/无路径墙/三条修复路径）、`probeDetail` 分离、四层顺序（env < PATH < local < bundled）的确定性测试 |
| `toolchain_ux_selftest.cpp`（新）+ `CMakeLists.txt` | 新的 ctest `vedit-toolchain-ux-selftest`：缺失场景**面板级**断言（1 条状态行、0 outline 行、1 行空状态、0 伪造条目、0 error 级条目、重复 reparse 不增殖）；可用场景断言 `local` 发现、无缺失提示、以及**真实 Verible 会话产出真 outline 符号**（FFI 构建） |
| `AGENT.md`、`docs/BUILD.md`（§10.1） | 写清三条路：**随包（bin/）/ 本地取（`bash scripts/fetch-verible.sh` + `bash build/slang/build.sh`）/ env（`VEDIT_VERIBLE_LS`、`VEDIT_SLANG`，含 `_DIR` 形式）**，并修正 BUILD.md 排障表里“不查 PATH”的过时说法 |

没有改 `src/app/**`（最小化原则：只需 lane 内改动即可满足）；没碰 `src/editor/**`、`resources/syntax/**`、`packaging/**`。

## 4. 验收证据

| 项 | 观测 |
|---|---|
| 缺失场景（产品级） | `03-after-missing-state.log`：消息 **1 次**、459 字节、默认运行无 `probed` 墙；`TOOLCHAIN source=verible:missing slang:missing missing=[verible, slang] local=[]` |
| 缺失场景（面板级，替代 GUI） | UX selftest：`panel.status-single-line/concise/mentions-both-tools/no-probe-wall`、`panel.outline-zero-rows`、`panel.empty-state-visible/single-line/mentions-missing`、`panel.no-fabricated-diagnostics`、`panel.no-error-entries`、`repeat.*` 全 PASS |
| 缺失场景（shell 级 diagnostics 列表） | UX selftest：`shell.diagnostics-single-entry PASS 1`（重复调用被去重）、`shell.diagnostics-concise PASS 446`、`shell.diagnostics-no-probe-wall`、`shell.diagnostics-single-line` |
| 可用场景（裸构建） | `02-after-product-run.log`：`TOOLCHAIN source=verible:local slang:local missing=[] local=[…]`，`Diagnostics unavailable` 出现 **0 次**，最长行 247 字节；UX selftest：`available.real-symbols-non-empty PASS 5`、`available.real-panel-has-symbols PASS 5`、`available.panel-no-unavailable-text PASS` |
| 工具链发现自测 | `06-selftests.log`：`VER-TOOLCHAIN-SELFTEST expect=missing checks=20 failures=0`；`TOOLCHAIN-UX-SELFTEST ok=1 checks=42 failures=0 skipped=0` |
| 根 ctest | **18/18 通过**（含新增 `#18 vedit-toolchain-ux-selftest`）——`04-ctest.log` |
| `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`**——`05-typecheck.log` |

## 5. 没有验证 / 边界

1. **GUI 观感**：无显示服务器。面板文本的**换行折行、颜色、滚动**无法断言，标 **UNVERIFIABLE**；
   有头验收由停点 1 的用户完成。
2. **用户真实工程**：证据用仓库 fixture（7 文件）；用户工程只影响消息触发次数（一次启动一次），
   不改变断言形状。
3. **bundled 分支**：由 T82 的 `toolchain_selftest --expect bundled` + AppDir 证据覆盖；本任务新增的
   UX 测试在 ctest 里只跑 missing/local 场景（打包 AppDir 不在根构建里）。
4. 开发机的 `slang` 通过 local 层被发现；`available.real-session-ready-not-failed` 的 detail 里仍能看到
   SLANG 后端在测试构造时未传路径（UX 测试只构造了 FFI slang 默认后端），这是**测试构造**而非产品路径：
   产品集成里 `createFfiSlangBackend(toolchain.slang.path)` 传的是解析结果。

## 6. 证据文件（`evidence/toolchain-ux/`，均带 C9 头）

| 文件 | 内容 |
|---|---|
| `01-before-product-run.log` | 修复前：2 次重复消息、1289 字节单行、20 条候选路径墙 |
| `02-after-product-run.log` | 修复后裸构建（仓库内）：`source=…:local`、0 条缺失消息 |
| `03-after-missing-state.log` | 修复后缺失状态（二进制移出 checkout）：1 次消息、459 字节、无路径墙；opt-in trace 仍可取候选清单 |
| `04-ctest.log` | 根 ctest 18/18 |
| `05-typecheck.log` | `TYPECHECK-EXIT=0` |
| `06-selftests.log` | toolchain 20/0 与 UX 42/0 的逐条输出（含 panel/shell 级计数） |
