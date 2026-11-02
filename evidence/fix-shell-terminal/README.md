# 返修 4 — shell/terminal 四缺陷收口（T47）证据

- Cell: `cell-20261031T085543161Z` · board task `T47` · 契约 `.mpd/cellwork/cell-20261031T085543161Z/contracts/fix-shell-terminal.md`
- Owner: **Deep Worker** · Verifier: **Reviewer**（loop `loop-20261031T101645-2c4ca4`）
- 采集时间: 2026-10-31 19:20–19:50 (Asia/Shanghai)
- 分支 / HEAD: `feat/qt6-ktexteditor-migration` @ `95c496e682e3841add1a081edd78d29c34957aac`
- 工作树状态（采集时刻，`git status --porcelain` 仅列本任务写域）:

```
 M src/app/app_shell.cpp
 M src/app/app_shell.h
 M src/app/main.cpp
 M src/terminal/pty_session.cpp
 M src/terminal/pty_session.h
 M src/terminal/terminal_selftest.cpp
```

  说明：该分支上同时存在其他 lane 的未提交改动（`crates/*`、`src/workspace/search/*`、`src/app/editor_*` 等），
  不在本任务写域内，也不由本证据判断。注意 `src/app/app_shell.{h,cpp}` 与 `src/app/main.cpp` 同时承载
  T71 lane 的 Ctrl+S / File > Save 改动（`saveActiveDocument()`、`file.save` 命令及其 main.cpp 侧检查），
  本任务只对上面 §1 列出的符号负责，不评价 T71 的改动。
- 环境: pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / tree-sitter 0.25.9 /
  `target/qt6-build` 由 `cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo` 配置）
- 判定: **四缺陷均已收口，并有可复跑的失败前/后对照**；仓库门禁 **16/16 通过**
  （最终采集 `logs/30-ctest.log`，`100% tests passed`）。采集过程中 search lane 的 selftest 曾在其修复
  飞行期间为红（与本改动无链接关系，`nm` 计数 0；该 lane 落地后即转绿，见 §6.1）。

## 0. 结论速查

| # | 缺陷（来源 verdict） | 修复位置 | 修前（实测） | 修后（实测） |
|---|---|---|---|---|
| ① | `restoreSession` 早于 `setFactory`，恢复出的 dock 是占位符（term-panel M1） | `src/app/app_shell.{h,cpp}` 的 `ShellOptions::deferSessionRestore` + `ApplicationShell::startSession()`；`src/app/main.cpp` 产品启动顺序 | `CHECK panel-seam.terminal-content FAIL`（dock 仍是 `panel-placeholder-terminal`） | `CHECK panel-seam.terminal-content PASS` / `panel-seam.search-content PASS` / `panel-seam.editor-layout-restored PASS panes=2` |
| ② | 子进程未 `close(master)`，master fd 被 shell 及后代继承（term-panel M2） | `src/terminal/pty_session.cpp` `PtySession::start()`（子进程分支 `close(master)`）+ `posix_openpt(O_CLOEXEC)` | `CHECK pty.master-fd.not-inherited FAIL 4->/dev/pts/ptmx`；后代 `4 -> /dev/pts/ptmx` | shell: `0,1,2 -> /dev/pts/0`（另 `10 -> /dev/tty`），无 ptmx；后代同；两检查 PASS |
| ③ | `readAvailable()` 单次读取无上限，可持续占用事件循环（term-panel M3） | `src/terminal/pty_session.cpp` `readAvailable()` 预算 + `QTimer::singleShot(0, …)` 重新排队；`noteActivation()` 计数；`finalize()` 收尾 drain | `max=163840 limit=65536`（单次激活读了 160 KiB，另一轮 200704 B）；`capped=0` | `max=65536 limit=65536`；`capped=8`；`delivered=4194304 produced=4194304`（不丢字节） |
| ④ | 根构建 `VEDIT_SYNTAX_GRAMMAR=none` 的 provider→cache 优先级（syn-grammars M1） | **已在 T12 修复**（本任务未改该面）；本任务只做两取值复核 | 历史（`5668c43^`）根构建日志: `syn-grammars: compiled grammar id='none'`（provider 先写 cache） | 当前: 默认 → `verilog`；`-D…=none` → `none`；`-D…=verilog` → `verilog`；cache/生成头/manifest/两条 STATUS 行四处一致 |

## 1. 缺陷①：会话恢复早于面板工厂 → 恢复出的 dock 是占位符

根因（代码事实）：`ApplicationShell::ApplicationShell` 在构造里就 `restoreSession()`（`src/app/app_shell.cpp:71` 前的旧代码），
`restoreSession()` → `applyPanelsFromSession()` → `PanelRegistry::insert()`；而 `PanelRegistry::createContent()`
在没有 factory 时插入 `panel-placeholder-<id>`（`src/app/panel_registry.cpp:15-105`）。`PanelRegistry::setFactory()`
只影响**未来**的 insert（`panel_registry.cpp:84-90`），因此 `main.cpp` 在 shell 构造**之后**才
`setFactory("terminal", …)` 时，恢复出的 terminal dock 永远是占位符。`search` 同款；`file-tree`/`outline`
由各自 integration 手工替换占位符（`src/workspace/filetree/filetree_integration.cpp:76-92`、
`src/workspace/diagnostics/diagnostics_integration.cpp:195-206`）所以"看起来修过"。

修复（shell 侧统一，一次性覆盖 terminal + search + 任何从会话恢复的面板）：
- `src/app/app_shell.h:45` 新增 `ShellOptions::deferSessionRestore`；`src/app/app_shell.h:70-77` 新增公有
  `ApplicationShell::startSession()`（幂等，`src/app/app_shell.h:116` 的 `sessionStarted_` 保证不会恢复两次）。
- `src/app/app_shell.cpp:71-74` 构造末尾只在未延迟时调用 `startSession()`；
  `src/app/app_shell.cpp:76-107` 把"恢复会话 / 无会话则默认面板+主题+保证一个 pane"的整段搬进 `startSession()`。
- `src/app/main.cpp:841-855` 产品入口：`options.deferSessionRestore = true` → 构造 shell →
  `setEditorProvider` → `workspace::installPanels()`（注册 file-tree/search/outline 工厂）→
  `panels()->setFactory("terminal", terminalPanelFactory())` → `shell.startSession()` → `shell.show()`。
  工厂全部先就绪，恢复后插入的 dock 天生带真实内容；`setEditorProvider` 也提前到恢复之前，恢复出的
  pane 直接用 KTextEditor（不再是"先造 demo pane 再换"）。

可复跑的失败前/后对照（同一个检查名，同一台机器）：
- 前置复现（修前顺序 = 构造时恢复、之后注册工厂）与修复后（延迟恢复）由
  `repro/run-before-after.sh defect1` 自动切换（把 `main.cpp:305` 的
  `seamRestored.deferSessionRestore = true;` 临时改 `false`，即等价于修前的顺序）。
- 修前（`logs/11-app-before.log`，当时探针已在、修复未在）:

```
CHECK panel-seam.seed-session PASS
CHECK panel-seam.dock-restored PASS
CHECK panel-seam.terminal-content FAIL
APP-SHELL-SELFTEST ok=0 checks=136 failures=1
```

- 修后（`logs/21-app-after.log`）:

```
CHECK panel-seam.seed-session PASS
CHECK panel-seam.deferred-nothing-inserted PASS
CHECK panel-seam.dock-restored PASS
CHECK panel-seam.terminal-content PASS
CHECK panel-seam.search-content PASS
CHECK panel-seam.no-placeholder PASS
CHECK panel-seam.editor-layout-restored PASS panes=2
APP-SHELL-SELFTEST ok=1 checks=161 failures=0
```

  命令（锁定镜像内）:
  `QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck`

自证边界：`panel-seam.terminal-content` 用真实的 `TerminalPanel`（terminal lane 的工厂），
`panel-seam.search-content` 用探针工厂（`QLabel`，objectName `search-panel-probe`）证明"同一机制对第二个面板生效"；
产品里 search 的真实工厂由 `workspace::installPanels()` 在 `startSession()` 之前注册，走同一条路径
（这是代码阅读结论，未在 GUI 里逐个点击核验）。

## 2. 缺陷②：`fork` 的子进程保留 pty master fd

根因（修前代码，`git show HEAD:src/terminal/pty_session.cpp` 的 `PtySession::start`）：`posix_openpt()` 得到
`master` 后 `fork()`；子进程分支只 `open(slavePath)`、`dup2` 到 0/1/2、`close(slave)`，**没有 `close(master)`**，
于是 shell 及其后代都持有一个指向自己终端 master 的 fd（`/proc/<pid>/fd` 显示 `/dev/pts/ptmx`）。

修复（`src/terminal/pty_session.cpp`）:
- `:145` `posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC)` —— 任何 `execve` 都关掉它（跨 exec 的兜底）；
- `:186-189` 子进程分支在 `execve` 前显式 `::close(master);`（契约要求的"子进程侧必须关闭"；也覆盖
  `execve` 失败/提前 `_exit` 的路径）。

自证（`logs/10-term-before.log` → `logs/20-term-after.log`，同一检查；命令
`QT_QPA_PLATFORM=offscreen target/qt6-build/src/terminal/vedit-terminal-selftest --selfcheck`）:

```
# 修前
CHECK pty.master-fd.not-inherited FAIL 4->/dev/pts/ptmx
CHECK pty.master-fd.descendant-not-inherited FAIL total 0  … 1 -> /dev/pts/0  2 -> /dev/pts/0  4 -> /dev/pts/ptmx
TERMINAL-SELFTEST ok=0 checks=112 failures=4
# 修后
CHECK pty.master-fd.not-inherited PASS 0->/dev/pts/0, 1->/dev/pts/0, 10->/dev/tty, 2->/dev/pts/0
CHECK pty.master-fd.descendant-listed PASS total 0  lr-x… 0 -> /dev/null  lrwx… 1 -> /dev/pts/0  lrwx… 2 -> /dev/pts/0
CHECK pty.master-fd.descendant-not-inherited PASS (同上，无 ptmx)
TERMINAL-SELFTEST ok=1 checks=112 failures=0
```

两个检查的分工：`pty.master-fd.not-inherited` 读 shell 进程自己的 `/proc/<pid>/fd`；
`pty.master-fd.descendant-*` 在 shell 里跑 `sleep 20 & ls -l /proc/$!/fd`，直接看**后代**的 fd 表
（这正是 verdict 里"继承进 shell 及后代"一句的可观测面）。

## 3. 缺陷③：`readAvailable()` 单次无上限

根因：修前 `readAvailable()` 在 `read() > 0` 时 `continue`，直到 `EAGAIN`/`EIO`；高速生产者
（`yes`、`cat /dev/zero`）能让循环长时间不返回事件循环。

修复（`src/terminal/pty_session.cpp`）:
- `:285-337` 每次激活有 `PtySession::readChunkLimit()`（`src/terminal/pty_session.h:55`，64 KiB）预算；
  每次 `read()` 只请求"剩余预算"（`:299-305`），因此单次激活**不可能**超过上限；
  预算用尽后 `QTimer::singleShot(0, …)` 重新排队（`:335`），事件循环在两趟之间得到执行；
  余量留在内核 pty 缓冲里（背压，不丢字节）。
- `:339-347` `noteActivation()` 记录 `maxBytesPerActivation()` / `cappedActivations()`（可观测性，
  `pty_session.h:56-57`）。
- `:378-384` `finalize()` 在关闭 master 之前补一次有界 drain：分块读取后子进程可能在 pty 里留下尾部
  输出，不 drain 会丢尾巴（这是"有上限读取"引入的正确性配套，不是新缺陷）。
- `:287-289` `master_ < 0` 直接返回，保证重新排队的 lambda 在会话结束后是空操作。

压测（探针，`terminal_selftest.cpp` 的 `checkPtyReadLimit`）：`/bin/sh -c "head -c 4194304 /dev/zero | tr '\0' x"`，
4 MiB 洪水输出；断言 (a) 全部字节送达、(b) 单次激活 ≤ 上限、(c) 上限确实被触发过。
修前/后（`logs/10-term-before.log` → `logs/20-term-after.log`）:

```
# 修前（预算是声明值 65536，但没有执行）
CHECK pty.read-limit.delivered PASS delivered=4194304 produced=4194304
CHECK pty.read-limit.max-per-activation FAIL max=163840 limit=65536
CHECK pty.read-limit.cap-engaged FAIL capped=0
# 修后
CHECK pty.read-limit.delivered PASS delivered=4194304 produced=4194304
CHECK pty.read-limit.max-per-activation PASS max=65536 limit=65536
CHECK pty.read-limit.cap-engaged PASS capped=8
```

未证明的边界：这是 offscreen、本机 tmpfs 上的压测；"UI 不再冻结"是从"单次激活有界 + 事件循环在两趟之间
得到执行"推出的，未在有屏环境用秒表/帧率测量。`yes` 这种**无限**生产者不能直接跑进断言（会让修前实现
永不返回），所以用有限 4 MiB 洪水代替——修前实测单次激活读到 160–200 KiB 后返回，已足以证明"无上限"。

## 4. 缺陷④：provider→cache 优先级（`VEDIT_SYNTAX_GRAMMAR`）

现实：该缺陷**已在 T12 修复**（同一面：`src/syntax/provider/CMakeLists.txt`），本任务不改该面，只做两取值复核。
- T12 前的代码（`git show 5668c43^:src/syntax/provider/CMakeLists.txt`）在 provider 一侧无条件
  `set(VEDIT_SYNTAX_GRAMMAR "none" CACHE STRING …)`（cache 首写者赢），使 grammars lane 的默认 `verilog`
  被静默丢弃；历史根构建日志 `5668c43^:evidence/m0-build/build-logs/31-qt6-configure.log` 记录:
  `-- syn-grammars: compiled grammar id='none' verilog=1 systemverilog=1`。
- T12 后的代码（当前树）：product 构建不再 seed 该 cache，改为
  `VEDIT_SYNTAX_GRAMMAR_OWNER=src/syntax/grammars`（`src/syntax/provider/CMakeLists.txt:26-42`），
  用 `cmake_language(DEFER …)` 在 configure 末尾读最终值，空值直接 FATAL（`:66-104`、`:114`）；
  同时产品 ctest `vedit-app-syntax-startup` 以 `PASS_REGULAR_EXPRESSION "syntax-product: grammar=verilog"`
  把"id 错配"变成红色门禁（`src/syntax/product/CMakeLists.txt:105-110`）。

两取值验证（`repro/run-before-after.sh defect4`，configure-only，日志在 `logs/grammar/`）:

| 配置 | `CMakeCache.txt` | `grammar_config_generated.h` | `grammars_manifest.txt` | STATUS |
|---|---|---|---|---|
| `-DVEDIT_SYNTAX_GRAMMAR=none` | `VEDIT_SYNTAX_GRAMMAR:STRING=none` | `kCompiledGrammar = "none"` | `VEDIT_SYNTAX_GRAMMAR=none` | `syn-provider: baked grammar id='none'` / `syn-grammars: compiled grammar id='none'` |
| 默认（无 `-D`） | `…:STRING=verilog` | `= "verilog"` | `=verilog` | `baked grammar id='verilog'` / `compiled grammar id='verilog'` |
| `-DVEDIT_SYNTAX_GRAMMAR=verilog` | `…:STRING=verilog` | `= "verilog"` | `=verilog` | 同上 |

三者配置退出码均为 0（`CONFIGURE_EXIT[none]=0` / `[default]=0` / `[verilog]=0`），
说明显式 `none` 与默认 `verilog` 都按期望生效，provider 不再覆盖 grammars lane 的选择。
产品侧的门禁断言在本次 ctest 里也是绿的：`1/16 Test #1: vedit-app-syntax-startup … Passed`。

## 5. 门禁证据（本任务复跑的）

| 命令（pinned 镜像内） | 观察 |
|---|---|
| `cmake --build target/qt6-build --parallel` | `BUILD_EXIT=0`（97/97 目标，含 `vedit-app`） |
| `target/qt6-build/src/terminal/vedit-terminal-selftest --selfcheck` | `TERMINAL-SELFTEST ok=1 checks=112 failures=0`（exit 0） |
| `target/qt6-build/src/app/vedit-app --selfcheck` | `APP-SHELL-SELFTEST ok=1 checks=161 failures=0`（exit 0，含 `EDIT-CORE-SELFTEST ok=1 checks=55`） |
| `ctest --test-dir target/qt6-build --output-on-failure` | `100% tests passed, 0 tests failed out of 16`（exit 0，最终采集；含 `vedit-terminal-selftest`、`vedit-app-shell-selftest`、`vedit-app-syntax-startup`） |

原始日志：`logs/30-ctest.log`（ctest 全文）、`logs/20-term-after.log`、`logs/21-app-after.log`。

## 6. 不在本任务范围的红项 / 发现（只报告，不修）

1. **`vedit-workspace-search-selftest` 曾在其修复飞行期间为红**（采集于 19:41；最终采集 19:48 已由该 lane
   修复为绿，见 §5）。当时观察:
   ```
   SLOWLIFECYCLE delete-returned elapsedMs=10000 released-marker=0 stallMs=11500
   CHECK slowlifecycle.destructor-waits-for-stalled-read FAIL
   CHECK lifecycle.stall-child-pass FAIL
   WORKSPACE-SEARCH-SELFTEST ok=0 checks=102 failures=1 skipped=2
   ```
   与本改动无关的证据：(a) `nm -C …/vedit-workspace-search-selftest | grep -c 'vedit::terminal'` = **0**，
   该二进制不含任何 terminal 符号；(b) 失败面是 search lane 正在飞行中的新东西——`src/workspace/search/`
   下 `CMakeLists.txt/README.md/search_*.{h,cpp}` 均为未提交修改，并新增未跟踪的 `tools/slowio`
   与 `search_jump_target.{h,cpp}`；(c) 失败检查是它们新加的生命周期回归测试，注释自己写着
   "Pre-fix: the worker is still inside read()"，即该测试本来就是为"修前会红"写的（`search_selftest.cpp:880-935`）。
   → 属 search lane 的在飞工作，按当时证据与本任务无关；最终这次采集它已经转绿，无需本任务处理。
2. **`QDockWidget::setWidget()` 的所有权语义**（与 filetree/diagnostics 的占位符替换有关）：实测（Qt 6.10.2，
   `target/t47-scratch/dockwidget_setwidget.cpp`）：`setWidget()` 之后旧 widget **仍存活**
   （`after-setWidget oldAlive=1`、`after-events oldAlive=1`），所以
   `src/workspace/filetree/filetree_integration.cpp:88-89` 与
   `src/workspace/diagnostics/diagnostics_integration.cpp:204-205` 的 `delete placeholder` 是**必要且正确**的
   （不删会泄漏一个占位 QWidget）。延迟恢复后，本任务的产品路径不会再走到那些"替占位符"分支（dock 尚未插入），
   这些集成代码保留亦无冲突。
3. 未做的：没有 GUI 手点时延/帧率；没有在 ASan 下复跑 fd/读循环；没有改 `src/app/CMakeLists.txt`
   （本次修复不需要新增编译单元或链接）。

## 7. 复跑

见 `repro/run-before-after.sh`（需要 docker + `vedit-qt6-build:local` 镜像）：

```sh
bash evidence/fix-shell-terminal/repro/run-before-after.sh after    # 修后：terminal + app 两个 selftest
bash evidence/fix-shell-terminal/repro/run-before-after.sh before   # 修前：②③(py_session.prefix.cpp) + ①(构造期恢复)
bash evidence/fix-shell-terminal/repro/run-before-after.sh defect1  # 缺陷①：修后 / 修前 顺序对照
bash evidence/fix-shell-terminal/repro/run-before-after.sh defect4  # 缺陷④：三种 configure
bash evidence/fix-shell-terminal/repro/run-before-after.sh all      # after + before 全跑
```

脚本会在 `target/t47-repro/` 下写临时日志，并把结果打印到标准输出；缺陷②③的"修前"复现方式是把
`repro/pty_session.prefix.cpp`（= `HEAD` 的 `pty_session.cpp` + 计数探针）临时替换进 `src/terminal/`，
跑完自动还原；缺陷①的"修前"复现方式是把 `src/app/main.cpp` 里 panel-seam 探针的
`deferSessionRestore = true` 临时改 `false`（等价于旧的构造期恢复），跑完自动还原。
