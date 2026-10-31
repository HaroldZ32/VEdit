# T48 独立评审结论 — T47 `fix-shell-terminal`：shell/terminal 四缺陷收口

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Deep Worker`）
- **被评审任务**：board `T47`；冻结契约 `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-shell-terminal.md`
- **验证环**：`loop-20261031T101645-2c4ca4`（`mpd_verify_seat` 已绑定）
- **结论**：**PASS**（bound seat）。四缺陷逐条成立、均有失败前/后对照；无阻塞 finding。
- **取证顺序**：先读契约/两个来源 verdict/证据日志与复跑脚本，再自跑修复后自检与三配置 configure；
  之后按判据做定向源码核对（①延迟恢复、②子进程 close(master)、③读上限、④provider→cache）。

---

## 1. 我实际跑的命令与观测

### 1.1 修复后自检（terminal lane + app shell）

```text
$ bash evidence/fix-shell-terminal/repro/run-before-after.sh after
AFTER-EXIT=0
CHECK pty.master-fd.not-inherited PASS 0->/dev/pts/0, 1->/dev/pts/0, 10->/dev/tty, 2->/dev/pts/0
CHECK pty.read-limit.delivered PASS delivered=4194304 produced=4194304
CHECK pty.read-limit.max-per-activation PASS max=65536 limit=65536
CHECK pty.read-limit.cap-engaged PASS capped=11
TERMINAL-SELFTEST ok=1 checks=112 failures=0
CHECK panel-seam.deferred-nothing-inserted PASS
CHECK panel-seam.dock-restored PASS
CHECK panel-seam.terminal-content PASS
CHECK panel-seam.search-content PASS
CHECK panel-seam.no-placeholder PASS
CHECK panel-seam.editor-layout-restored PASS panes=2
EDIT-CORE-SELFTEST ok=1 checks=55 failures=0
APP-SHELL-SELFTEST ok=1 checks=161 failures=0
```
日志：`evidence/verdicts/fix-shell-terminal/01-after-rerun.log`（我自己重跑，非引用 writer 日志）。

### 1.2 缺陷④：三种 configure（我自己重跑）

```text
$ bash evidence/fix-shell-terminal/repro/run-before-after.sh defect4
DEFECT4-EXIT=0
--- none   : CONFIGURE_EXIT=0  cache=none     header="none"     manifest=none
             syn-grammars id='none'    / syn-provider baked id='none' (owner: src/syntax/grammars)
--- default: CONFIGURE_EXIT=0  cache=verilog  header="verilog"  manifest=verilog
             syn-grammars id='verilog' / syn-provider baked id='verilog'
--- verilog: CONFIGURE_EXIT=0  cache=verilog  header="verilog"  manifest=verilog
             syn-grammars id='verilog' / syn-provider baked id='verilog'
```
日志：`evidence/verdicts/fix-shell-terminal/02-defect4-rerun.log`。
→ 显式 `none` 与默认/显式 `verilog` **都按期望生效**，provider 不再覆盖 grammars lane 的选择。

### 1.3 仓库门禁

```text
$ ctest --test-dir target/qt6-build --output-on-failure
 1/16 vedit-app-syntax-startup ... Passed 0.29 sec
 7/16 vedit-terminal-selftest  ... Passed 2.03 sec
 9/16 vedit-app-shell-selftest ... Passed 0.33 sec
100% tests passed, 0 tests failed out of 16        ROOT-CTEST-EXIT=0

$ mpd_verify_evidence {kind:"gate", gate:"typecheck"}
gate typecheck exit 0 -> .mpd/verify/evidence/ev-20261031T141616-c1b5fc.log
（另手跑 bash build/gates/typecheck.sh → TYPECHECK-EXIT=0，日志 04-typecheck.log）
```

---

## 2. 四缺陷逐条核对

| # | 来源 verdict | 修前（证据） | 修后（我的实测） | 判定 |
|---|---|---|---|---|
| ① 会话恢复早于 setFactory → 恢复出的 dock 是占位符 | term-panel **M1** | `11-app-before.log`：`CHECK panel-seam.terminal-content FAIL`（`APP-SHELL-SELFTEST ok=0 checks=136 failures=1`） | 我的 `after` 跑：`terminal-content PASS`、`search-content PASS`、`no-placeholder PASS`、`editor-layout-restored PASS panes=2`（161 checks / 0 fail） | ✅ |
| ② 子进程未 `close(master)` → master fd 继承进 shell 与后代 | term-panel **M2** | `10-term-before.log`：shell `4->/dev/pts/ptmx`、后代 `4->/dev/pts/ptmx`，两项 FAIL | 我的 `after` 跑：shell fd 表 `0/1/2->/dev/pts/0, 10->/dev/tty`（无 ptmx）；后代无 ptmx；两项 PASS | ✅ |
| ③ `readAvailable` 单次无上限 → 可饿死事件循环 | term-panel **M3** | `10-term-before.log`：`max=163840 limit=65536`、`capped=0`（预算声明但未执行） | 我的 `after` 跑：`max=65536 limit=65536`、`capped=11`、`delivered=produced=4194304`（不丢字节） | ✅ |
| ④ 根构建 grammar provider→cache 优先级 | syn-grammars **M1** | 历史根构建 `syn-grammars: compiled grammar id='none'`（provider 先写 cache） | 我的 defect4 三配置：`none`/`default`/`verilog` 全部正确（见 §1.2） | ✅ 已在 T12 修复（契约允许跳过），本任务只复核 |

**修复手段审计（不是掩盖式）**：
- ① `ShellOptions::deferSessionRestore`（`app_shell.h:45`）+ 幂等 `startSession()`（`app_shell.cpp:76-81`，`sessionStarted_`），
  构造函数只在未延迟时调用；产品入口 `main.cpp:841-855`：`deferSessionRestore=true` → 构造 → **工厂注册** → `startSession()` → `show()`。
- ② `posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC)`（`pty_session.cpp:145`）+ **子进程分支在 execve 前显式 `::close(master)`**（`:189`，
  我读了 `:176-196` 的上下文：dup2 后 close(slave)，再 close(master)，然后 chdir/execve）。
- ③ `readChunkLimit()=64 KiB`（`pty_session.h:55`）、每次 `read()` 只请求"剩余预算"（`:299`）、
  预算用尽 `QTimer::singleShot(0, …)` 重新排队（`:335`，让事件循环插进来）、`finalize()` 补一次有界 drain（`:361-382`）、`cappedActivations_` 可观测。
- ④ `src/syntax/provider/CMakeLists.txt` 不再无条件 seed cache，改为 `VEDIT_SYNTAX_GRAMMAR_OWNER=src/syntax/grammars` +
  `cmake_language(DEFER …)` 在 configure 末尾读最终值、空值 FATAL；该机制在 `5668c43`（syntax lane / T12 的 checkpoint）引入，与本报告"已在 T12 修复"一致。

**修前复现的真实性**：`repro/pty_session.prefix.cpp` 我做了 diff——它 == `git show 22e5ef8^:src/terminal/pty_session.cpp`
+ 仅探针计数（`activationBytes`/`noteActivation`/max/capped），本身不含 `O_CLOEXEC`、不含 `readChunkLimit`、
子进程分支无 `::close(master)`（全部 3 处 `close(master)` 都是错误路径）。所以 `before` 的 RED 跑的是真修前代码。

---

## 3. 写域与归属

- `22e5ef8 feat(terminal): …` 只动 `src/terminal/{pty_session.cpp,pty_session.h,terminal_selftest.cpp}` → 均在允许写域 `src/terminal/`。
- `e7445f6 feat(app): wire the shell for session restore and save bindings` 是 Lead 的 checkpoint，**同时**包含 T47 的
  `app_shell.{h,cpp}`/`main.cpp` 恢复顺序与 **T71 lane 的 save bindings**（`editor_area.*`/`editor_provider.*` 等）。
  T47 证据在采集时刻的 `git status` 只列了 `app_shell.cpp/h`、`main.cpp`、`terminal/pty_session.*`、`terminal_selftest.cpp`
  ——全部在契约写域内；`editor_area.*`/`editor_provider.*` 属 T71，不是 T47 的改动。README §0 也明确"只对本任务符号负责"。
- ④ 的面未改（契约允许），`src/syntax/provider|grammars` 当前状态由 T12 的机制保证。
- 未改测试/门禁迁就实现：terminal/app selftest 只新增检查（112 / 161），根 ctest 16/16 绿。

---

## 4. Findings

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| — | — | **无阻塞 finding** | — |
| N-1 | Note | 我没有复跑脚本的 `before`/`defect1` 模式——它们会临时替换 `src/terminal/pty_session.cpp` 与 `src/app/main.cpp`
（trap 自动还原），这越过我的只读/写域纪律。我改为：核对 prefix 变体确实等于修前代码 + 自跑 after 绿 + 读 writer 的 before 日志。 | 可接受；若需要更强，可让 writer 或 captain 跑一次 before 并留档 |
| N-2 | Note | ③ 的"UI 不再冻结"是"单次激活有界 + 两趟之间事件循环得到执行"的推论，**未在有屏环境测帧率/秒表**。压测证明的是上限生效与不丢字节。 | 无（契约判据正是上限与不饿死；视觉不在无屏环境认证） |
| N-3 | Note | `yes` 这类**无限**生产者没有直接进断言（会让修前实现永不返回），用 4 MiB 有限洪水代替——报告已如实说明。 | 无 |
| N-4 | Note | `e7445f6` 把 T47 与 T71 的改动混在一个 checkpoint 提交里（Lead 的提交策略），评审时须按符号区分。 | 提交纪律问题，非 T47 缺陷 |
| N-5 | Note | ④ 的修复属于 T12（syntax lane），T47 只做复核；契约明确允许"已在 T12 修复则跳过并写明"。 | 无 |

## 5. 未能验证的部分

1. **GUI 行为/冻结观感**：本机无显示服务器；只验证无头 selftest 与 ctest，不认证任何视觉/交互表现。
2. **`before` 模式我没有亲自复跑**（会临时改 `src/`）：以"prefix == 修前代码"的 diff 证据 + writer 原始 before 日志为准。
3. **真实慢盘/无限输出终端在 UI 里的表现**：压测是 offscreen、本机 tmpfs、有限 4 MiB 洪水。
4. 未在 ASan 下复跑 fd/读循环（writer 声明未做；契约未要求）。

## 6. 结论

**PASS**。四缺陷全部有"修前红 / 修后绿"的可复跑对照，且修法都是生命周期/边界手段而非掩盖：
会话恢复改为工厂就绪后延迟启动（①），子进程 execve 前显式关闭 master + O_CLOEXEC（②），
单次激活读预算 64 KiB + 重新排队 + 收尾有界 drain 且不丢字节（③），provider 不再抢写 cache、`none` 与 `verilog`
两取值都按预期生效（④，已在 T12 修复、本任务只复核）。根 ctest 16/16、typecheck exit 0 均为我实跑。
