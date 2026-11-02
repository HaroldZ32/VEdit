# 独立评审 verdict — T35 term-panel（本地终端面板：pty + VT，可停靠/关闭/重建）

> 口径说明：本文件评审的是老看板 **T35**（`src/terminal/` 终端面板）；本次执行的 board task `T35` 是
> `five-lane-verdicts`（AC-0.6）。两者编号相同，勿混。

- Cell: `cell-20261031T085543161Z` · 评审任务: `T35`（AC-0.6）· 被评 lane: 老看板 `T35`（contract `term-panel.md`）
- 冻结契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/term-panel.md`
- 评审人: **Reviewer**（独立只读；未修改任何源码/测试/门禁）
- 评审对象: 分支 `feat/qt6-ktexteditor-migration`，HEAD `9957f5f`；评审开始时工作树干净（`git status --porcelain` 空）；评审结束时其他 lane 的未提交改动已出现（`Cargo.toml`、`crates/*`、`evidence/*` 等），不在本评审范围
- 日期: 2026-10-31（Asia/Shanghai）
- 环境: 宿主 Linux 7.0.0-38-generic x86_64；pinned 镜像 `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / glibc 2.43 / g++ 15.2.0 / cmake 4.2.3）
- 风险等级: **Medium**
- 结论: **PASS（附带 findings）** — (a)(b)(c) 成立且被我独立复跑（101 checks、exit 0）；根构建已链接进产品（267 个符号）；findings 为非阻塞的健壮性/集成问题。

---

## 1. 总体评估

终端面板是五条 lane 里“最不依赖别人的一套”：libc POSIX pty（`posix_openpt`/`grantpt`/`unlockpt`/`ptsname` + `fork`/`setsid`/`TIOCSCTTY`/`TIOCSWINSZ`）+ 自写 VT 屏幕，零新依赖，许可不变（QTermWidget 是 GPL-2.0，本仓 MIT，拒绝它是正确的取舍）。契约三条我逐条核过：(a) 真 shell 会话/可停靠/关闭/重建有 101 条 CHECK 与两张截图；(b) 退出码与信号终止两处可见（状态行 + 横幅），`exit 7` / `SIGTERM→143` / 交互 `exit 3` 都有断言；(c) 零网络——源码无 socket API、ELF 无 `libQt6Network`、UND 符号无 `socket/bind/listen/connect/accept`（我独立核了静态库与 `vedit-app` 的 NEEDED，符号计数 0）。根构建侧，`abfe963` 之后 `src/terminal` 已在根 lane 清单里，`vedit-app` 真实链接了 `libvedit_terminal.a`（我复核 `nm`：267 个 `vedit::terminal::` 符号），根 ctest 9/9 含 `vedit-terminal-selftest`（我复跑通过），我也独立重跑了 `--selfcheck`（`checks=101 failures=0`）。

非阻塞但有价值的问题集中在“集成与健壮性”，不在验收判据本身：① 会话恢复顺序导致恢复出的 terminal dock 是占位符（`filetree` 已修掉同款，terminal 没修）；② `fork` 的子进程没有关闭继承来的 pty master fd；③ `readAvailable()` 对持续输出不设单次上限，可能饿死事件循环。三条都给了方向，均不需要改测试或降判据。

## 2. Findings（按严重度）

### M1 [Medium — 集成] 会话恢复出来的 terminal dock 是占位符，不是可用终端
- 是什么：`ApplicationShell::ApplicationShell` 构造里先 `restoreSession()` → `applyPanelsFromSession()` → `panels_->insert(id, this)`，此时 terminal 的 factory 仍是空（shell 的 `registerPanels` 只登记了带空 factory 的 PanelSpec），`PanelRegistry` 插入的是 `panel-placeholder-terminal`。`main.cpp` 在 shell 构造**之后**才 `shell.panels()->setFactory("terminal", terminalPanelFactory())`，而 `setFactory` 只影响未来插入。`src/terminal/` 没有任何替换已插入占位内容的代码（`grep -rn placeholder src/terminal` 无命中）；同 shell 的 filetree 已经在 `installFileTree` 里处理了这一点。
- 为什么重要：用户上次开着终端，重启后看到“Content seam: … lane”的占位 dock——终端不可用，且提示文案是给开发者看的。契约 (a) 的“可停靠/关闭/重建”本身由 101 条 CHECK 证明，所以不据此判 FAIL；但这是终端在真实产品里的第一条用户路径上的缺陷。
- 修复方向（描述，不实施）：shell 侧在恢复后重新应用工厂（或把 `insert` 延迟到 `installPanels` 之后）；或让 terminal 像 filetree 一样在 `main.cpp` 里拿到 `dock("terminal")` 后检测 `panel-placeholder-*` 并 `dock->setWidget(terminalPanelFactory()())`。同一修复也覆盖 Search dock（见 T23 verdict M3）。
- 证据：`src/app/app_shell.cpp` 的构造函数 / `restoreSession` / `applyPanelsFromSession`；`src/app/panel_registry.cpp` 的 `PanelRegistry` 占位插入与 `setFactory`；`src/app/main.cpp` 的 `setFactory` 调用顺序；`src/workspace/filetree/filetree_integration.cpp`（反例：已处理）；`evidence/build-registration/README.md` 遗留发现 2。

### M2 [Medium — 正确性] `fork` 的子进程未关闭继承的 pty master fd
- 是什么：`PtySession::start` 先 `posix_openpt` 得到 `master`，再 `fork`；子进程分支里只 `open(slavePath)`、`dup2` 到 0/1/2 并关闭多余 `slave`，**没有 `close(master)`**。因此 shell 及其所有后代都持有一个指向自己终端 master 侧的 fd（`/proc/self/fd` 可见 `/dev/pts/ptmx`）。
- 为什么重要：master 随子进程树存活，pty 对的 master 侧在 `PtySession` 关闭后仍被后代持有；终端会话销毁/重启时资源不会立刻释放，且终端里的任意子进程可以绕过 tty 行规程直接读写自己的 master。用户可见影响小（不是数据损坏），但这是终端实现的经典缺陷。
- 修复方向：子进程分支在 `execve` 前 `close(master)`（只保留 slave 的 0/1/2）；如改用 `posix_spawn` 则用 file-actions 关闭。补一条断言（selftest 里 `ls -l /proc/<child>/fd` 不含 ptmx，或 `sh -c 'ls /proc/$$/fd'`）。
- 证据：`src/terminal/pty_session.cpp` 的 `PtySession::start` 子进程分支（`setsid`/`open(slavePath)`/`dup2`/`execve` 段）；`evidence/term-panel/03b-checks.log` 的 `pty.*` 断言没有覆盖 fd 继承。

### M3 [Medium — 健壮性] `readAvailable()` 单次不设上限，持续输出可饿死事件循环
- 是什么：`PtySession::readAvailable` 在 `count > 0` 时 `continue` 无限循环，直到 `EAGAIN`/`EIO`。子进程持续高速输出（`yes`、`cat /dev/urandom`、大日志）时，非阻塞 fd 往往在每次循环都能读到数据，循环长时间不返回事件循环。
- 为什么重要：终端跑 `yes` 是常见操作；这会导致 UI 冻结/输入延迟，且没有背压或丢弃策略。终端模拟器通常按“每次 notifier 激活最多读 N 字节/轮，然后重新排队”处理。
- 修复方向：给单次激活设字节/轮数上限（例如 256 KB），达到上限就 `QTimer::singleShot(0, …)` 重新调度读取；超限时按策略合并/丢弃并计数（可观测）。
- 证据：`src/terminal/pty_session.cpp` 的 `PtySession::readAvailable`；`src/terminal/terminal_widget.cpp` 的输出消费路径。复跑：在面板里运行 `yes` 或 `cat /dev/zero | tr '\0' x` 观察 UI 响应（有屏环境；L2 未覆盖）。

### M4 [Medium — 证据纪律 C9] 证据没有记录采集时刻的 HEAD 与工作树状态
- 是什么：`evidence/term-panel/` 下没有 `git rev-parse HEAD` / `git status --porcelain`；`00-pins.txt` 只有镜像/工具链，`07-conclusion.md` 记录日期。
- 为什么重要：C9 明确要求；本次我独立重跑了 selftest 与根 ctest，结论不受影响，但证据本身不满足规则。
- 修复方向：证据头统一写 time / HEAD / porcelain。
- 证据：`evidence/term-panel/00-pins.txt`、`07-conclusion.md`；C9 原文见 `docs/handover/appendix/requirements-rev2.md`。

### N5 [Nit] `run-root-rehearsal.sh` 的 lane 清单 needle 已失效
- 是什么：`evidence/term-panel/run-root-rehearsal.sh` 硬编码旧 `VEDIT_LANE_DIRECTORIES` 结尾，根 CMake 加了两条 lane 后 `needle in text == False`，脚本会 FATAL（`evidence/build-registration/README.md` 遗留发现 4 已记录）。
- 为什么提：现在根构建已真实包含 terminal，这个“演练”脚本可以退役；留着会让后来者按文档复跑得到假失败。
- 方向：删除脚本或改成“检查 `src/terminal` 是否在清单里”的正向断言。

### N6 [Nit — 边界已声明] VT 子集不含鼠标上报/DEC 线框/sixel/bidi
- 是什么：`07-conclusion.md` §3 明确列出未实现项；101 条 CHECK 覆盖 40 条 `vt.*`。契约没有要求这些，ncurses 菜单边框类应用可能显示不完美——是“新增”而非回归。
- 方向：保持；若后续要跑 TUI 程序，按需补。

### N7 [Nit — 边界已声明] `sigabbrev_np()` 需要 glibc ≥ 2.32
- 是什么：`PtySession::signalName` 用 `sigabbrev_np`（GNU 扩展，glibc 2.32+）。交付的运行时自带 glibc（`docs/versions-qt.env` 的设计：私有 loader + glibc 2.43，宿主 glibc 2.17 不参与解析），因此当前打包形态下可解析。
- 为什么提：这依赖“永远不会用宿主 glibc 启动 `vedit-app`”这一前提；一旦有人绕过私有 loader，就会在 CentOS 7 上符号解析失败。登记为注意项，不改判据。
- 方向：保持私有 loader 交付；或在 `signalName` 里改用自带信号名表（零 glibc 版本依赖）。

## 3. 我不判的（declined to judge，逐条给理由）

1. **网络命名空间实测** — 容器无 `CAP_SYS_ADMIN` 且不能建 user-ns，`unshare -n` 不可用；报告已如实声明。我复核了源码 grep、NEEDED、UND 符号三条静态证据，成立。
2. **目标机 CentOS 7.9 上的真实运行** — 属 T12/pack-final 的四宿主矩阵；本 lane 证据是镜像内 + offscreen/xcb。
3. **字体渲染与 CJK 宽度** — 镜像里 DejaVu Sans Mono + llvmpipe；终端只依赖 `QFontDatabase::systemFont(FixedFont)`，未在目标机验证。
4. **多终端/并发会话** — 每面板一个 `PtySession`，未测多实例资源与 `SIGCHLD` 竞争（当前用 50 ms 轮询 `waitpid`）。
5. **滚轮横向、copy-on-select** — 报告 §6 声明未做；契约未要求。
6. **`terminate()` 的 500 ms 后 SIGKILL 时序** — 属于关闭路径的取舍，未实测极端慢子进程。
7. **鼠标上报/DEC 线框/sixel/bidi** — 明确未实现且非契约要求（见 N6）。

## 4. 我实际复跑的（命令 + 观察）

```sh
# 1) 终端 selftest（根构建产物，锁定镜像内）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc \
  'QT_QPA_PLATFORM=offscreen target/qt6-build/src/terminal/vedit-terminal-selftest --selfcheck'
# 观察：TERMINAL-SELFTEST ok=1 checks=101 failures=0（exit 0）

# 2) 根 ctest（锁定镜像内）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  ctest --test-dir target/qt6-build --output-on-failure
# 观察：9/9 passed（含 Test #5 vedit-terminal-selftest）

# 3) 链接与零网络（宿主）
nm -C target/qt6-build/src/app/vedit-app | grep -c 'vedit::terminal::'        # 267
nm -C target/qt6-build/src/terminal/libvedit_terminal.a | grep -cE ' (socket|connect|bind|listen|accept|getaddrinfo)$'  # 0
readelf -d target/qt6-build/src/app/vedit-app | grep NEEDED                   # 无 libQt6Network
```

证据面：`03b-checks.log` = 101 条 CHECK 全文；`04-elf-needed.txt` = NEEDED 无 `libQt6Network`；`05-no-network.txt` = 源码/UND 符号/正向对照（`posix_openpt`/`fork`/`waitpid` 等）；`10/11-*.png` = 运行中与 `Exited · code 42` 两张截图；`09-root-build-with-lane.log` = 演练 8/8（历史；现已被真实根构建取代，ctest 9/9）。

## 5. 本次评审的边界

- 没有在有屏环境亲手开终端、跑 `vim`/`ncurses`、按 Ctrl+Shift+C/V（无显示）；交互证据是 `--selfcheck` 的 `QTest` 级断言 + Xvfb 截图，属 L2。
- 没有重跑 `run-evidence.sh` 全流程（会重建 lane 构建并重写 evidence；我改为直接重跑根构建里的 selftest 与 ctest，等价覆盖 (a)(b)(c) 的核心断言）。
- 没有用 sanitizer 复现 fd 继承/读循环问题（代码路径分析 + 根因明确；复跑命令已在 findings 给出）。
- 没有修改任何源码、测试、门禁；本文件是唯一写入。

- 未运行 `bash build/gates/qt6-cxx.sh` 本体：它会重写 tracked 的 `evidence/m0-build/build-logs/*`，超出本次只读写域；改在锁定镜像内直接复跑 ctest / lane 自测（命令与观察见 §4）。

## 6. 结论与一句话理由

**Verdict: PASS（附带 findings）** — 风险 Medium。

理由：契约 (a)(b)(c) 逐条成立且被我独立复跑（101/101、根 ctest 9/9、267 个 terminal 符号进 `vedit-app`、零网络符号），VT 子集与许可取舍都有如实边界；问题集中在会话恢复占位（M1）、master fd 继承（M2）、读循环无上限（M3）三条健壮性/集成缺陷与证据头缺失（M4），都不推翻本 lane 的验收结论，但 M1 建议在打包前修掉，否则用户重启后看到的第一个终端是假的。
