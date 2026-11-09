# UI-Mosaic · R3 修复报告（T4）—— A1「最后写入者生效」= settings/archive 按 mtime 仲裁

- 写入方：Deep Worker（看板 `T4`）
- 依据：船长 R3 裁决（按字面补修判据）+ `evidence/ui-mosaic/review/VERDICT.md` F1 背景 + `.mpd/plans/ui-mosaic/REQUIREMENTS.md` A1
- 范围：`src/app/app_shell.cpp`（唯一产品改动）+ `evidence/ui-mosaic/fix-r3/`。**未改** `src/editor/**`（F2 车道已冻结）、既有测试、门禁脚本、翻译（本轮没有新增可见字符串）。
- 结论：四组用例（a/b/c/d1/d2/d3）**11/11 断言通过**；`bash scripts/build-qt.sh` exit 0 + 全量 ctest **62/62**；`lrelease` **1278 finished / 0 unfinished / 0 untranslated**。

## 1. 问题与实现

复验指出的缺陷成立：R1 的启动路径虽然同写 settings.toml 与 session.json，但**启动读取仍是固定优先级**（settings 先到先得），没有任何文件时间比较。于是「settings 写失败 / 外部改档 / 两次写之间崩溃」时，旧 settings 会压住新 archive —— 旧值回弹。

`ApplicationShell::applyStartupThemes`（`src/app/app_shell.cpp`）现在的取值顺序：

| 顺序 | 条件 | 取值 |
| --- | --- | --- |
| 1 | `ShellOptions::uiTheme`（进程内显式 seam，产品无 CLI 填它）有效 | seam 值 |
| 2 | settings 与 archive **都有效且不同** | **mtime 较新者**；mtime 相同 → **settings**；任一 mtime 无效/文件不存在 → **settings** |
| 3 | 仅 settings 有效 | settings |
| 4 | 仅 archive 有效 | archive |
| 5 | 都无有效值 | 契约默认 `yunwu` |

- 比较实现：`QFileInfo(settingsPath).lastModified()` 与 `QFileInfo(session_->path()).lastModified()`；只有两者 `isValid()` 且 `archiveMtime > settingsMtime` 才取 archive（严格大于，相等落到 settings），完全符合「mtime 相同以 settings 为准」。
- 无效值（settings / archive / seam 任一处）**照旧**经 `reportUnknownUiTheme` 出可见诊断（`addDiagnostic` + 12 s 状态栏，`VEDIT_UI_PROBE` 还能从外部读到）——M2 语义未变，且现在即使另一来源胜出、坏值也会被点名（比 R1「找到一个有效值就 break」更不沉默）。
- **切换路径一行未改**（F1 保持）：菜单/工具栏仍经 `persistUiTheme` 同写 settings.toml + session.json；正常切换两边同值，仲裁不参与。
- 语法主题判据**保持 R1 原样**（`archiveIsChoice && !settingsUiThemeValid`）：archive 赢得 uiTheme 时，按胜出的产品主题配对出的语法主题与 archive 里记录的一致（例：archive `shenye` → 配对 `Breeze Dark`）。

### 一次失败的尝试（如实记录）

最初我把语法主题判据改成「archive 赢得 uiTheme 时也用它自己的语法主题」，结果 `vedit-app-shell-selftest` 与 `vedit-app-syntax-startup` 的 `CHECK restore.theme` 打红（失败日志保留在 `logs/build-qt-attempt1-failed.log`）：自检的会话存档里本来就带着显式语法主题，而 settings 没有 `uiTheme`，新判据把这条选择吞掉了。**回退到 R1 判据**后 62/62 全绿（`logs/build-qt.log`）。这条修正只动了语法主题分支，mtime 仲裁本身未变。

## 2. 回归证据（a/b/c/d1/d2/d3，全部 `touch -d` 控制 mtime）

```
$ bash evidence/ui-mosaic/fix-r3/run-mtime-regression.sh
== R3/A1: mtime arbitration between settings.toml and session.json ==
   container names: r3-probe-<case>; QPA=offscreen (no X display is used)
PASS  case-a: setup — settings (1794173400) is older than archive (1794177000)
        settings.toml mtime = 2026-11-09 05:30:00.000000000 +0800
        session.json  mtime = 2026-11-09 06:30:00.000000000 +0800
        observed: theme=shenye  status-theme=Theme: Deep Navy / Breeze Dark style=2 toolbarIconSize=20x20
PASS  case-a: startup took the NEWER archive value (shenye), not the older settings (bohe)
PASS  case-b: setup — settings (1794177000) is newer than archive (1794173400)
        settings.toml mtime = 2026-11-09 06:30:00.000000000 +0800
        session.json  mtime = 2026-11-09 05:30:00.000000000 +0800
        observed: theme=qingkong  status-theme=Theme: Sky Blue / Breeze Light style=2 toolbarIconSize=20x20
PASS  case-b: startup took the NEWER settings value (qingkong), not the older archive (shenye)
PASS  case-c: setup — both files carry the same mtime (1794175200)
        settings.toml mtime = 2026-11-09 06:00:00.000000000 +0800
        session.json  mtime = 2026-11-09 06:00:00.000000000 +0800
        observed: theme=bohe  status-theme=Theme: Mint Green / Breeze Light style=2 toolbarIconSize=20x20
PASS  case-c: equal mtimes fall back to settings (bohe), not the archive (shenye)
        no session.json; settings mtime = 2026-11-09 06:10:00.000000000 +0800
        observed: theme=qingkong  status-theme=Theme: Sky Blue / Breeze Light style=2 toolbarIconSize=20x20
PASS  case-d1: with no archive, settings supplies the theme (qingkong)
        no settings.toml; archive mtime = 2026-11-09 06:10:00.000000000 +0800
        observed: theme=shenye  status-theme=Theme: Deep Navy / Breeze Dark style=2 toolbarIconSize=20x20
PASS  case-d2: with no settings, the archive supplies the theme (shenye)
        settings.toml mtime = 2026-11-09 06:05:00.000000000 +0800
        session.json  mtime = 2026-11-09 06:06:00.000000000 +0800
        observed: theme=yunwu  status-theme=Theme: Cloud White / Breeze Dark style=2 toolbarIconSize=20x20
        [ui-probe] diagnostic=Ignoring unknown interface theme 'no-such-theme' (from settings.toml)
        [ui-probe] diagnostic=Ignoring unknown interface theme 'also-bogus' (from session archive)
PASS  case-d3: two invalid ids fall back to the contract default (yunwu)
PASS  case-d3: the invalid settings id is reported (M2 unchanged)
PASS  case-d3: the invalid archive id is reported too

== summary: pass=11 fail=0 ==
R3-MTIME PASS
```

用例内容（每例独立 state 目录 `state/case-*`，每例独立容器，见 §3）：

| case | settings | archive | 期望 | 实测 |
| --- | --- | --- | --- | --- |
| a | `bohe`，mtime **旧**（epoch 1794173400） | `shenye`，mtime **新**（1794177000） | archive（shenye） | `theme=shenye` ✅ |
| b | `qingkong`，mtime **新**（1794177000） | `shenye`，mtime **旧**（1794173400） | settings（qingkong） | `theme=qingkong` ✅ |
| c | `bohe`，mtime 相同（1794175200） | `shenye`，mtime 相同 | settings（bohe） | `theme=bohe` ✅ |
| d1 | `qingkong` | 无文件 | settings | `theme=qingkong` ✅ |
| d2 | 无文件 | `shenye` | archive | `theme=shenye` ✅ |
| d3 | `no-such-theme` | `also-bogus` | 默认 `yunwu` + 两条诊断 | `theme=yunwu` + 两条诊断 ✅ |

脚本内部先 `stat -c %Y` 断言 mtime 的**大小关系/相等关系**再启动，因此「mtime 没设对」不会伪装成通过；`touch -d` 的时区表达（显示为 +0800 的 8 小时后）不影响比较，因为脚本比较的是 epoch 秒。

原始日志：`logs/case-{a,b,c,d1,d2,d3}.log`（每个都是完整启动输出，`[ui-probe] theme=…` 与诊断都在里面）。

## 3. 证据隔离（按要求）

- state：全部在 `evidence/ui-mosaic/fix-r3/state/`，每次运行先删后建；**从不读写** `evidence/ui-mosaic/review-r1/**`。
- 容器：每例一个 `docker run --rm --name r3-probe-<case>`（`r3-probe-a`…`r3-probe-d3`，运行完即删）；不复用任何既有容器（尤其 `review-r1-x`）。
- 显示：**完全不使用 X**。探针以 `QT_QPA_PLATFORM=offscreen` 运行，因此不存在显示号冲突，也不会触碰 `/tmp/.X11-unix`（宿主 `/tmp` 与容器不共享导致的跨车道 X 串线在此路径上不可能发生）；每例另有独立容器内 `XDG_RUNTIME_DIR=/tmp/xdg-r3-<case>`。
- 镜像/二进制：`vedit-qt6-build:local` + `target/qt6-build/src/app/vedit-app`（本轮构建产物）。

## 4. 构建与门禁

```
$ bash scripts/build-qt.sh                 # logs/build-qt.log
100% tests passed, 0 tests failed out of 62
Total Test time (real) =  86.43 sec
M0-SMOKE ok=1 platform=xcb qt=6.10.2 kf6=6.24.0 ktexteditor=1 … editor=1 document=1
[qt6] PASS: Rust service layer + configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke …
BUILD_EXIT=0

$ bash build/gates/lrelease.sh             # logs/lrelease.log
[lrelease] counts: 1278 translation(s) (1278 finished, 0 unfinished, 0 untranslated)
[lrelease] PASS: 0 unfinished and 0 untranslated in translations/vedit_zh_CN.ts

$ bash build/gates/docs.sh                 # logs/docs.log
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
```

- 本轮**没有新增可见字符串**（只加代码/注释），故译目仍是 R1 的 1278 条，`lrelease` 双 0 保持不变。
- 构建日志里唯一的 `warning:` 是 `ninja: warning: premature end of file; recovering`（ninja 对旧日志的自愈提示，不是编译告警）；`-Wall -Wextra -Wpedantic -Werror` 下 `app_shell.cpp` 重新编译零告警。
- 二进制 `23:22:58` 晚于 `app_shell.cpp` `23:21:25`，回归脚本在构建之后运行，证据对应当前源码。

## 5. 未覆盖 / 诚实边界

- **「mtime 存在但读不到」没有单独构造**：同一用户/同一挂载下很难造出「文件可读、mtime 不可读」的状态。代码路径由 `lastModified().isValid()` 显式判定，且「文件不存在」（d1/d2 的缺档，以及二者都缺的默认路径）已实测走的是同一条回退分支。要更严格需要 root 权限或特殊 FS 故障注入，不在本轮。
- 回归用 `VEDIT_UI_PROBE` 读 `theme=`/`status-theme=`，不点鼠标（无 WM）；mtime 由 `touch -d` 构造，不是真实「崩溃在两次写之间」的复现——但仲裁只看文件 mtime，用例与真实故障后的文件状态等价。
- 应用**退出时**仍可能 `saveSession()` 重写 session.json（把 uiTheme 同步为当前值），所以每例只用**启动后 2500 ms 的探针行**做断言，脚本打印的 mtime 是**启动前**的权威值；同一状态目录不重复用于两例。
- 语法主题与产品主题的配对规则保持 R1（见 §1）；d3 里「默认浅色 chrome + archive 记录的 Breeze Dark 语法主题」是 R1 既有行为（archive 的显式语法主题选择在 settings 无有效 `uiTheme` 时优先），R3 未改。
- 未提交 git；**本轮我的 diff 只有 `src/app/app_shell.cpp`（mtime 23:21）+ `evidence/ui-mosaic/fix-r3/`**。工作区里 `src/editor/kernel/ktexteditor_provider.{cpp,h}` 的改动属于 **F2 车道（T3）**，不是本轮产物，我没有触碰它们（`src/app/app_shell.h` 的改动来自上一轮 R1）。
