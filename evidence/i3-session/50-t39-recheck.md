# T39 复现台 — T19 会话恢复 **A-half** 复验证据（当前树重跑）

Cell `cell-20261102T100600176Z` · task **T15** (`prep-t39`) · member **se-i18n** · verifier **Reviewer**
Frozen contract: `.mpd/cellwork/cell-20261102T100600176Z/contracts/prep-t39.md`
被复验的 T19 契约：`.mpd/cellwork/cell-20261102T050548232Z/contracts/session-restore.md`
（sha256 `c8b81d7c…aa8866`，与旧 FAIL 记录冻结值一致）。
本任务**只写 `evidence/i3-session/`**；未改产品代码、测试或门禁。

## 0. 结论摘要

| 项 | 结果 | 断言 |
|---|---|---|
| A-half（隐藏标签 alpha 的光标/滚动恢复，与 B 同法证明） | **PASS** | offscreen `12:3 / scroll 6`，xcb `12:3 / scroll 5`，均等于关闭前实时捕获值 |
| B-half（活动标签 bravo） | **PASS** | offscreen `45:5 / scroll 30`，xcb `45:5 / scroll 25` |
| fail-first 控制（破坏存档中 A 的位置 → 断言必须变红） | **PASS（红 3 条）** | `13-fail-first-offscreen.log`：`FAIL after.a-caret-line/-column/-scroll-line`，rc=1 |
| 窗口几何 / 分栏 / pane1 两标签与活动标签 / 工程 / 面板 | **PASS** | 见 §4（尺寸精确，位置在文档化的 40 px 容差内） |
| 流水线退出码 | **0** | `RUN_SH_EXIT=0`，`SESSION-PROBE-OK` |

当前树与 17:37 的上一轮证据**在去掉随机临时目录名后逐字节一致**（§5），即结果可复现，非偶发。

## 1. 复验对象（旧 FAIL 的 F1）

旧记录 `.mpd/verify/records/rec-20261102T093018-feec68.json`（verdict **FAIL**，task T19）只有一条 finding：

> **F1 (minor)** AC-10.1.3.2 的 A-half 只在 demo `QPlainTextEdit` provider 上被证明；真实
> KTextEditor 探针只设置/断言了 B；A（pane1 的 alpha）重启后只被证明“存在且有序”，从未被激活读取，
> 所以**隐藏标签的延后应用路径在出厂编辑器上未被证明**。

`evidence/i3-session/REPORT.md` §2/§T51 记录了修复方案（选项 b：扩展 `probe/main.cpp`，不修
`container-run.sh VEDIT_PROBE_KEDITOR` 的第二条构建路径）。F1 的 expected 有四点，逐点核对：

| F1 expected | 当前树实现 | 本次实测 |
|---|---|---|
| 关闭前给 A 一个区分度大的 caret+scroll | `probe/main.cpp` 先激活 A（可见时）设 `12:3` / scroll 6，捕获 `before.a-position` | `PASS before.a-position — caret=12:3 scroll=6`（xcb: scroll=5） |
| 重启后激活 tab 0，读**真实 KTextEditor view** 断言 | `restoredOne->setActiveTab(0)` → `viewFor(...)` → `cursorPosition()`/`firstDisplayedLine(RealLine)` | `after.a-caret-line now=12 want=12`、`-column now=3 want=3`、`-scroll-line now=6 want=6`（xcb 均为 5:5 / 5:5） |
| fail-first 控制在存档 A 位置被破坏时必须变红 | `--corrupt-a-position` 把存档 `documents[0]` 的 caret/scroll 清零后重启 | **3 条 `FAIL after.a-*`，`SESSION-PROBE failures=3`，rc=1** |
| （备选）修 `container-run.sh` 并提交 A+B 日志 | 未采用；改用唯一、已在跑的 lane 探针（选项 b） | `probe/build.sh` 在镜像内成功编译并链接产品静态库 |

> A 与 B 的差别正是“隐藏标签”语义：重启时 `activeTab=1`（B 可见，A 是背景页），A 的 caret/scroll
> 只能在页面首次 Show 时经 `applyPendingPosition` 延后应用。探针**先切回 tab 0 再读真实 view**，
> 所以“已恢复”与“从未看过”被区分开。

## 2. 命令与退出码（固定镜像 `vedit-qt6-build:local`）

```bash
$ bash evidence/i3-session/probe/run.sh
# 镜像内：重建探针链接的两个产品库 -> 编译探针 -> offscreen -> Xvfb+xcb -> fail-first
...
== result: offscreen rc=0 xcb rc=0 fail-first rc=1 (expected non-zero) ==
SESSION-PROBE-OK
$ echo "RUN_SH_EXIT=$?"
RUN_SH_EXIT=0
```

- 探针二进制由 `probe/build.sh` 在容器内用**产品自己的编译/链接行**构建：
  `[probe] built /vedit-build/target/i3-session-probe/session-probe`
  （`12-probe-run.log` 第 1–9 行：`ninja` 重建 `vedit_editor_core` → 编译 `main.cpp` → 链接）。
- 本次新跑产物时间戳 2026-11-02 20:02（CST），probe sha256 `c207da25…45a98b`。
- `ctest --test-dir target/qt6-build -N | grep -i session` → **无匹配**：会话恢复没有 ctest 用例，
  本 lane 的门禁就是 `run.sh` 的退出码 + 断言日志（旧记录同样以探针日志为证据）。
- 断言计数：offscreen `PASS=62 FAIL=0`；xcb `PASS=64 FAIL=0`；fail-first `PASS=60 FAIL=3`。

## 3. A-half 断言原文（当前树，20:02）

**offscreen（`10-probe-offscreen.log`）**
```
PASS before.a-view
PASS before.a-position — caret=12:3 scroll=6
PASS archive.a-caret-and-scroll — expected A caret=12:3 scroll=6
PASS after.a-file — /tmp/vedit-session-probe-asIBMm/project/alpha.sv
PASS after.a-view
PASS after.a-caret-line — now=12 want=12
PASS after.a-caret-column — now=3 want=3
PASS after.a-scroll-line — now=6 want=6
```
**Xvfb + xcb（`11-probe-xcb.log`，真实 shell + 真实 KTextEditor view）**
```
PASS before.a-position — caret=12:3 scroll=5
PASS after.a-file — /tmp/vedit-session-probe-mmTFxC/project/alpha.sv
PASS after.a-caret-line — now=12 want=12
PASS after.a-caret-column — now=3 want=3
PASS after.a-scroll-line — now=5 want=5
```
A 的 scroll 在 offscreen 为 6、xcb 为 5：因为窗口高度不同（775×720 vs 1200×860）可见行数不同，
断言是与**同一轮运行** `before.a-position` 捕获值比较，不是硬编码期望值。

**fail-first（`13-fail-first-offscreen.log`，存档 A 位置被清零后同一探针）**
```
FAIL after.a-caret-line — now=0 want=12
FAIL after.a-caret-column — now=0 want=3
FAIL after.a-scroll-line — now=0 want=6
SESSION-PROBE failures=3 platform=offscreen
```
`run.sh` 内置保护：`corrupt_rc != 0` 且 red ≥ 2 才算控制有效；若控制不再变红，流水线 `FATAL` 退出。

## 4. 其余验收项（同一轮日志）

| 验收 | offscreen | xcb |
|---|---|---|
| 窗口尺寸精确往返 | `775x720 want=775x720` | `1200x860 want=1200x860` |
| 窗口位置（无 WM 的 Xvfb，文档化 40 px 容差） | `0,23 want=0,0` | `1,25 want=0,0` |
| 窗格数 / 分栏 | `after.pane-count panes=2`；`archive.layout-split` | 同左 |
| pane1 两标签 + 活动标签 B | `after.pane1-two-tabs tabs=2`、`after.pane1-file-names alpha.sv | bravo.sv`、`after.pane1-active-tab-is-b — 1` | 同左 |
| pane2 文件 | `after.pane2-file …/charlie.sv` | 同左 |
| provider | `archive.leaf-provider`（ktexteditor） | 同左 |
| 工程根 / 工程文件集 | `after.project-root project`；`after.project-model root=… files=3` | 同左 |
| 面板可见性 | `after.panel-file-tree`、`after.panel-outline` PASS，`after.panel-terminal-hidden` PASS | 同左 |
| 存档样本 | — | `30-session.json`：pane1 `alpha caret{12,3} scroll 5` + `bravo caret{45,5} scroll 25`，`activeTab=1`，pane2 `charlie.sv`，schema version 2 |

截图（`shots/`，20:02 重出；sha256 前 12 位）：
- `19-before-restart-a-active.png` `0b89d85af284` — 关闭前 **A（alpha）可见**，状态栏 `13:4`（0 基 12:3），首行 6（0 基 5），`Panes: 2 · active: pane-1 · tabs: 2`；
- `20-before-restart.png` `fa655341da2c` — A 关闭后切到 B；
- `21-after-restart.png` `aca770a3e2fb` — 重启后 B 激活，`46:6`（0 基 45:5），首行 26（0 基 25）；
- `21b-after-restart-a-active.png` `9ecf9a8a5417` — 重启后 **切回 A**，`13:4`、首行 6（0 基 5），与关闭前一致；`Project: project`、Files/Outline 面板可见；
- `22-unsaved-restart-prompt.png` `efe23ec987cd` — 未保存重启对话框（不属于 A-half，顺带保留）。

## 5. 与旧证据的差异

| 证据 | 时间 | A-half 状态 | 说明 |
|---|---|---|---|
| `.mpd/verify/records/rec-20261102T093018-feec68.json`（F1） | 09:30 | **未证明**：真实探针只断言 B；A 只在 demo provider 日志里 | 旧 FAIL 的对象 |
| `baseline-20261102T1737/`（本次重跑前的上一轮产物） | 17:37 | 已证明（A 断言 + fail-first 已在树中） | 本次运行前原样备份（`cp -p`），供 verifier 对照 |
| 本次重跑（本目录 `1x-*.log`、`shots/`、`30-session.json`） | 20:02 | 已证明，且与 17:37 去随机临时路径后**逐字节一致** | `diff <(norm baseline) <(norm current)` 三份日志均无差异 |

差异细节：
1. 日志中的 `/tmp/vedit-session-probe-XXXXXX` 每轮随机，故文件 sha256 每轮不同；归一化后内容相同。
   旧记录 `sources[].sha256` 引用的是其当轮日志字节，不代表内容回归。
2. 截图为重新渲染（字体/抗锯齿像素级差异导致字节数变化），肉眼核对内容一致（§4）。
3. 存档样本 `30-session.json` 本轮 A scroll=5（xcb 运行写入），与 xcb 日志一致。
4. 旧 FAIL 记录的时间早于修复落树（`probe/main.cpp` mtime 17:34、`run.sh` 17:37 已含 A-half 与
   fail-first）；**本次证据的意义是把该修复在“当前树”上重新跑出来**，而不是新增代码。

## 6. 未能证明 / 边界（交 verifier 判定）

1. **A-half 的证明依赖探针自身的“激活并读取”**（一个 harness 同时负责 setup 和 assert），没有第二个
   独立实现交叉验证；fail-first 控制只证明“存档位置丢失能被发现”，不证明其它损坏形态。
2. 窗口**位置**在无 WM 的 Xvfb 下为 `1,25`（期望 `0,0`），按文档化 40 px 容差通过；尺寸为精确往返。
3. 没有 `ctest -R session` 用例；会话恢复的证据形态是 lane 探针，不是 ctest。
4. 本次任务**未改产品代码**，只重跑并新增 `baseline-20261102T1737/` 与本文档；树内 173 项 dirty
   （含其它 lane 的在途修改）是既有状态，不是本任务产生。
5. 旧 loop `loop-20261102T063252-f1d260` 是否仍在有效期、以及 T39 verdict 的最终判定，属于 verifier
   （见任务契约的 Verification protocol / frozen contract）。

## 7. 复现与产物清单

```bash
# 复现（主机 -> 固定镜像 -> 容器内构建探针 + 三个 pass）
bash evidence/i3-session/probe/run.sh   # 期望末行 SESSION-PROBE-OK 且 exit 0
```

| 产物（20:02 重出） | sha256 |
|---|---|
| `10-probe-offscreen.log` | `4d0e4ffb4c2ccd33b167c67e2624734eb9c4bf90ba7e29e3e177c76b16dd9e72` |
| `11-probe-xcb.log` | `2760209785e2c46c6b864947d7a472b8dd0001d69e59f581e0e47fc3e0a2e689` |
| `12-probe-run.log` | `622b8e0bbfa3de55b885f3018d280f07057babd548c35b742d09216139fce85c` |
| `13-fail-first-offscreen.log` | `c17442f89f120aa43785fc18b909d4b92eec1fe52ec466a58b78fdbc14da907f`（本轮 red=3） |
| `30-session.json` | `a4251fa9edd0bf0734c5352f64261f96c9bd065072e60e7c9f1cc93fa85db2f1` |
| `probe/main.cpp`（构建输入） | `74a0411cbfaae1134df920a3ee3f3c60696fb65735d9d4c877f8d89d96023123` |
| `probe/run.sh`（构建输入） | `401b05d5df2bf4d110d9c40f840bce009662b1b4f761faa0495308a1343855c0` |

树状态：HEAD `4e2d0a5`，`git status --porcelain` 173 项（既有在途修改）；`src/app/editor_area.cpp`
sha256 `063f4ec1…`、`src/app/editor_provider.h` `7c1e9b49…`、
`src/editor/kernel/ktexteditor_provider.cpp` `400e0cb0…`。
