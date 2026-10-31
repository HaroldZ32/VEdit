# term-panel (T35) — 结论：Lapce 对齐的本地终端面板

**结论：交付成立。** 面板在同一进程内跑一个**本地** shell 会话（真 pty、真 tty），
可停靠/关闭/重建，退出码与进程结束状态**两处可见**（状态栏 + 横幅），且**不含任何网络
通路**（源码无 socket API、ELF 无 `libQt6Network`、UND 符号无 `socket/bind/listen/connect/accept`）。
全部证据可由 `bash evidence/term-panel/run-evidence.sh` 一条命令复跑。

- Board task: `T35` · 契约: `.mpd/cellwork/cell-20261031T014507403Z/contracts/term-panel.md`
- 交付源码: `src/terminal/`（12 个文件，3644 行，`08-structure.txt` 有 sha256）
- 证据目录: `evidence/term-panel/` · 复跑脚本: `evidence/term-panel/run-evidence.sh`
- 构建镜像: `vedit-qt6-build:local`（Ubuntu 26.04.1 / Qt 6.10.2 / KF6 6.24.0 / g++ 15.2.0，`00-pins.txt`）
- 写入范围: 只有 `src/terminal/` 与 `evidence/term-panel/`；`scripts/`、`src/app/`、根
  `CMakeLists.txt` **一行未改**（见 §6 的交接）。

## 1. 观察到的命令与退出码

| 命令 | 观察结果 | 证据 |
|---|---|---|
| `cmake -S src/terminal -B target/term-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo` | **exit 0**，`-- Build files have been written to: /vedit-build/target/term-build` | `01-configure.log` 末行 `configure exit: 0` |
| `cmake --build target/term-build --parallel "$(nproc)"` | **exit 0**，14 个编译/链接步骤，`-Wall -Wextra -Wpedantic -Werror` 全绿（含 `seam_check.cpp` 的编译期接缝检查） | `02-build.log` 末行 `build exit: 0` |
| `ctest --test-dir target/term-build --output-on-failure` | **exit 0**，`1/1 Test #1: vedit-terminal-selftest ... Passed 1.07 sec`，`100% tests passed, 0 tests failed out of 1` | `03-ctest.log` 末行 `ctest exit: 0` |
| `vedit-terminal-selftest --selfcheck`（offscreen） | `TERMINAL-SELFTEST ok=1 checks=101 failures=0` | `03b-checks.log`（101 行 CHECK 全文） |
| 同上，`QT_QPA_PLATFORM=xcb` + Xvfb + llvmpipe 软件渲染 | `TERMINAL-SELFTEST ok=1 checks=101 failures=0`（真窗口） | `06-xcb-and-screenshots.log` |
| `vedit-terminal-selftest --screenshots evidence/term-panel` | `exit 0`，两张 1100x640 PNG | `10-terminal-running.png`、`11-terminal-exited-code42.png` |
| 根工程 + 本 lane（演练，见 §5） | **configure exit 0 / build exit 0 / ctest exit 0**，`8/8` 通过，含 `4/8 vedit-terminal-selftest Passed 1.07 sec` | `09-root-build-with-lane.log` |

## 2. 契约 (a)(b)(c) 逐条对照

### (a) 本地 shell 会话（QTermWidget 或等价），可停靠/关闭/重建

**技术路线**：固定镜像里既没有 `qtermwidget` 也没有 `libvterm`（`00-pins.txt` 实测：
`dpkg -l` 无、`apt-cache policy` 在离线镜像里无候选），而加依赖必须改 `scripts/` 并重建
镜像 —— 超出本 lane 边界。且 **QTermWidget 是 GPL-2.0+，VEdit 是 MIT**，链进去等于改变整包
许可（与 C7「不随包 GPL 代码」的口径冲突）。因此按契约允许的「**等价**」实现：
libc 的 POSIX pty（`posix_openpt`/`grantpt`/`unlockpt`/`ptsname` + `fork`/`setsid`/
`TIOCSCTTY`/`TIOCSWINSZ`，`04-elf-needed.txt` 有 NEEDED 与 sha256）＋ Qt6 Widgets 自绘
VT 屏幕（`vt_screen.*` 不依赖 Qt，可离线单测）。**零新依赖、零 scripts 改动、许可不变。**

| 必须是 | 实现 | 实测断言 |
|---|---|---|
| 真 shell 会话 | `PtySession`：子进程 exec `$SHELL`→`/bin/bash`→`/bin/sh`，pseudo terminal 做主从 | `pty.start`、`pty.pid`、`pty.interactive.output`（写入 `echo VEDIT_SESSION_READY\r` 后真的读到回显与结果） |
| 交互输入 | 键盘 → pty 字节（Enter/Backspace/Tab/箭头（含 application-cursor-keys 变体）/Home/End/Ins/Del/PgUp/PgDn/F1–F12/Ctrl 字母/Alt 前缀） | `widget.key-events-to-pty`（真实 QKeyEvent 打进 shell，屏幕出现 `echo`） |
| pty 尺寸跟随控件 | `TIOCSWINSZ`，每次 resize 同步 | `pty.resize.tty-size`（`stty size` 输出 `12 40`）、`panel.dock.pty-size-follows-widget`（在真实 dock 里 `stty size` == 屏幕 rows/cols） |
| 可停靠 | 面板是普通 `QWidget`，由 shell 的 `QDockWidget` 承载 | `panel.dock.content`、`panel.dock.area`（`BottomDockWidgetArea`）、`panel.dock.float`（浮动/回停）、`panel.dock.closed`、`panel.dock.reopened` |
| 可关闭 | Close 按钮 → `closeRequested()` + 关闭所属 dock | `panel.close-button.signal`、`panel.close-button.hides-dock` |
| 可重建 | Restart 按钮 / 会话结束后按 Enter → 销毁旧 pty、清屏、开新会话 | `panel.restart.start/new-pid/banner-cleared/screen-cleared`、`panel.restart-button.new-pid`、`panel.enter-restarts` |

### (b) 退出码与进程结束状态可见

`PtySession` 用 `waitpid` 收尸并区分两种结束：`exited with code N` 与
`killed by signal N (SIGNAME)`（`exitCode()` 在信号情况下为 `128+N`，与 shell 惯例一致）。
面板把它显示在**两处**：底部状态行与屏幕上方横幅。

| 断言 | 观察 |
|---|---|
| `pty.exit-code` / `pty.exit-known` / `pty.exit-description` | `sh -c 'printf hello; exit 7'` → `exited with code 7`，输出 `hello` 收到 |
| `pty.signal.termination-signal` / `pty.signal.exit-code-128` / `pty.signal.description` | `kill -TERM $$` → signal 15、`exitCode()==143`、描述含 `SIGTERM` |
| `pty.interactive.exit` | 交互会话 `exit 3` → 退出码 3 |
| `panel.exit.state-code` | 状态行含 `42`（实测渲染为 `Exited · code 42`，见截图） |
| `panel.exit.banner-visible` / `banner-hint` | 横幅 `Shell exited with code 42. Press Enter or Restart to start a new session.` |
| `panel.exit.output-kept` | 退出后屏幕内容保留（不因进程结束被清掉） |

截图 `11-terminal-exited-code42.png` 同时可见：横幅（深红条）+ 状态行 `Exited · code 42`
+ 之前会话的输出。

### (c) 不支持远程（非目标 4），不引入网络监听

| 检查 | 观察 | 证据 |
|---|---|---|
| 源码 | `grep -rnE "QTcp\|QUdp\|QSslSocket\|QNetwork\|QHostAddress\|QAbstractSocket\|inet_\|getaddrinfo\|:listen\(\|:bind\(\|:accept\("` 无命中 | `05-no-network.txt` |
| ELF NEEDED | 只有 `libQt6Widgets/Gui/Core`、`libstdc++`、`libgcc_s`、`libc` —— **无 `libQt6Network`** | `04-elf-needed.txt` |
| UND 符号 | 未导入 `socket/bind/listen/connect/accept/getaddrinfo/gethostbyname`；**正向对照**：确实导入了 `posix_openpt/grantpt/unlockpt/ptsname/fork/waitpid/ioctl/execve/setsid` | `05-no-network.txt` |
| 网络命名空间实测 | 本容器不允许 `unshare -n`/`-Urn`（无 CAP_SYS_ADMIN、无 user-ns 创建），**未能在此环境实测**；上面三条静态证据成立 | `05-no-network.txt` 末段如实记录 |

## 3. 模拟器覆盖范围（VT 子集）

`vt_screen` 解析并断言过的：可打印 UTF-8、CR/LF/BS/HT、CSI
`A B C D E F G H f d J K L M P X S T @ m n c h l r s u b`、SGR
`0–9/21–29/30–37/39/40–47/49/90–97/100–107`、`38;5;n`、`38;2;r;g;b`、私有模式
`1/6/7/25/47/1047/1049/2004`、OSC 0/2 标题、DSR(6)/DA 回写、alt screen、带回环上限的
scrollback（`vt.scrollback.limit`）、选区取文本（`vt.selection.*`）、resize
（`vt.resize.*`）。共 40 条 `vt.*` 断言。

**未实现（如实记录）**：鼠标上报（1000–1006）、DEC 线框字符集（`ESC ( 0` 画线）、sixel、
bidi、连字。shell 与验收用例都不需要它们；若后续需要（例如 `ncurses` 菜单边框），
是**新增**而不是回归。

## 4. 契约之外的判据（我加的，便于复跑）

- `08-structure.txt`：12 个交付文件的**行数 + sha256**，以及两张截图的 sha256。
- `03b-checks.log`：101 条 CHECK 的完整输出（40 `vt.*` / 21 `pty.*` / 39 `panel.*` / 1 `widget.*`）。
- `seam_check.cpp`：以 OBJECT 库形式**编译**（不链接），把
  `panels.setFactory("terminal", terminalPanelFactory())` 这个真实调用交给编译器做类型检查；
  `PanelSpec::factory` 若改签名，本 lane 自己的构建就会红。

## 5. 根工程纳入演练（本 lane 不改根 CMakeLists）

根 `CMakeLists.txt` 现由 T63 用显式清单 `VEDIT_LANE_DIRECTORIES` 纳入 lane，注释明确要求
「lane 落地时在同一个 PR 里把它加进清单」。那一行在**我的写范围之外**，所以：

- 演练脚本（可复跑）：`bash evidence/term-panel/run-root-rehearsal.sh`；它把树复刻到
  `target/term-rehearsal/`，**只**在复刻件的清单里加一行
  `src/terminal`，然后在同一固定镜像里 configure/build/ctest；
- 结果：`-- VEdit: building lane directory src/terminal`、**build exit 0**（90 个目标，
  含 `src/app/vedit-app` 与本 lane 的 selftest）、**ctest 8/8 通过**，其中
  `4/8 vedit-terminal-selftest ... Passed 1.07 sec`（`09-root-build-with-lane.log`）；
- 仓库内**没有**任何一行改动被留在这条路径上（`git status` 只显示 `src/terminal/` 与
  `evidence/term-panel/`）。

**交接给 assembly（已在 cell 里 mail captain）**：

1. 根 `CMakeLists.txt` 的 `VEDIT_LANE_DIRECTORIES` 追加
   `src/terminal          # term-panel lane (T35)`（owner: m0-build）；
2. `src/app` 里接上面板工厂（owner: app-shell），一行：
   `shell->panels()->setFactory(QStringLiteral("terminal"), vedit::terminal::terminalPanelFactory());`
   并让 `vedit_app_shell`（或 `vedit-app`）链接 `vedit_terminal`。

这两条落地后，`bash scripts/build-qt.sh` 的 ctest 列表里就会出现
`vedit-terminal-selftest`，即仓库自己声明的 C++ 门禁覆盖本 lane。

## 6. 边界与未验证项

- **没有**运行 `bash scripts/build-qt.sh` 本身：它把日志写进 `evidence/m0-build/build-logs/`，
  那是 T10 lane 的目录，**不在我的写范围**。等 §5 的两行落地后由 gates 覆盖；本 lane 用
  同一镜像、同一 `-Werror` 编译选项、同一 ctest 直接调用作为等价门禁（上表）。
- **未验证**：`vedit-app` 里真实显示本面板（需要 §5 的 `setFactory` 那一行，属 app-shell）；
  目标机 CentOS 7.9 上的运行（属 T12/pack-final）；目标机字体渲染（镜像里是 DejaVu Sans
  Mono + llvmpipe；面板对字体只依赖 `QFontDatabase::systemFont(FixedFont)`）。
- 面板**不**做鼠标选择之外的花样（无滚轮横向、无 copy-on-select）；滚轮/Shift+PgUp/PgDn 翻
  scrollback、Ctrl+Shift+C/V 复制粘贴已实现。
