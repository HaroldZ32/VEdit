# T71 — Ctrl+S / File > Save 接到 EditorPane::saveFile（停点 1 前置）

collected_at: 2026-10-31T19:43:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
（提交由 Lead 统一负责；本任务未 commit、未 push。）

无头声明：本机没有显示服务器。下面的“快捷键”路径是**Qt 快捷键系统**上的真事件
（`QKeyEvent` 送入 shell 的 window，经 Qt 的 shortcut map 触发 QAction），而不是直接调用
handler；但**真实 X11/Wayland 键盘 → Qt 的整条 OS 路径**只能在用户有屏机器上验收，见 §5。

---

## 1. 结果

| 项 | 结果 |
|---|---|
| Ctrl+S | 绑定 `file.save`（`QKeySequence("Ctrl+S")`，全树唯一一处），经 `saveActiveDocument()` → `activePane()->saveFile()` → `EditorProvider::saveFile` |
| File → Save | 同一个 `QAction` 出现在 `menuBar()` 的 File 菜单里（`buildMenuBar()` 从 `file` 菜单命令生成），不走第二条实现 |
| 无头自检 | 新增 **21** 条 `save.*` 断言，全部 PASS；`APP-SHELL-SELFTEST ok=1 checks=160 failures=0` |
| 目标 ctest | `ctest -R vedit-app-shell-selftest` → `100% tests passed, 0 tests failed out of 1`（`TARGET-CTEST-EXIT=0`） |
| 门禁 | `bash build/gates/typecheck.sh` → `TYPECHECK-EXIT=0`（C++ -Werror 半段 + Rust clippy 半段都跑过） |
| 原子写 | 复用 T10 的 `atomicSaveText`，未重写；失败注入 `VEDIT_ATOMIC_FAULT=write` 的诊断原文是 `atomic save: injected write failure`，证明保存没有绕过 tmp→fsync→rename→fsync(dir) |
| 副作用 | Ctrl+S 只保存，不触发 quit/布局等任何其它动作（触发前后进程继续跑完剩余断言，`APP-EXIT=0`） |

`--selfcheck` 计数变化：**136（旧二进制基线，且当时有 1 个既存失败）→ 160，failures=0**。
其中 21 条是本任务新增的 `save.*`；另外 +3 条来自其它 lane 的代码（基线二进制是更早构建的，
本次重建把它们一并编进来了），并非本任务添加。

## 2. 改了什么

| 文件 | 改动 |
|---|---|
| `src/app/app_shell.h` | 新增公有 `bool saveActiveDocument();`（两条用户路径共用的唯一入口），带失败语义说明 |
| `src/app/app_shell.cpp` | 新增命令 `file.save`（文本 `&Save`、菜单 `file`、默认 `Ctrl+S`、order 15）、handler，以及 `saveActiveDocument()` 实现：取 `editorArea_->activePane()` → `pane->saveFile(&error)`；失败时**状态栏 `Save failed: <原因>`（8s）+ `diagnostics()` + `qWarning("[app-shell] …")`**，成功时 `Saved <path>`（4s）；失败不会清 modified 状态、不会改磁盘 |
| `src/app/main.cpp` | `--selfcheck` 新增断言（见 §3）；仅在 `main.cpp` 增加 `<KTextEditor/Document>` 以在自检里改动文档文本，shell 库本身仍不含 KTextEditor 头 |
| `evidence/save-binding/**` | 本证据 |

没有改 `src/editor/**`：入口走既有 `EditorProvider::saveFile` seam，KTextEditor provider 的
`saveFile` 本来就调用 `atomicSaveText`，因此不需要动内核代码（避免重写原子写）。

## 3. 无头断言（21 条，全部 PASS）

设计：先断言入口的命令/快捷键/菜单三项，再用 **demo provider**（此时 shell 仍是 demo）证明入口
与 provider 无关；随后装上 `makeEditCoreProvider()`（KTextEditor），在真实 provider 下把
**两条用户路径**都跑通；最后注入失败证明“失败可见”，再恢复并证明同一入口能再次保存成功。

```
save.command.registered PASS
save.shortcut.ctrl-s PASS
save.menu.file-item PASS                      # saveAction 真的在 menuBar 的 File 菜单里
save.demo.open PASS
save.demo.action.disk-bytes PASS              # demo provider 也经入口保存
save.demo.action.path-unchanged PASS
save.demo.action.no-temp-file PASS
save.ktexteditor.document PASS
save.shortcut.window PASS
save.shortcut.disk-bytes PASS                 # 真 QKeyEvent(Ctrl+S) → shortcut map → 磁盘字节变化
save.shortcut.modified-cleared PASS
save.shortcut.path-unchanged PASS
save.menu.disk-bytes PASS                     # 触发 File 菜单里的那个 QAction → 磁盘字节变化
save.menu.path-unchanged PASS
save.no-temp-file PASS                        # 无 *.vedit-tmp-* 残留
save.failure.disk-unchanged PASS              # 注入失败：磁盘不变
save.failure.status-bar PASS                  # 状态栏含 "Save failed"
save.failure.diagnostic PASS                  # diagnostics() 含 "Save failed"
save.failure.still-modified PASS              # 失败后文档仍是 modified
save.recovery.disk-bytes PASS                 # 故障移除后同一入口保存成功
save.recovery.not-modified PASS
```

关键实现细节（可复跑核对）：
* 快捷键：`shell->activateWindow();` 后把 `QKeyEvent(KeyPress, Qt::Key_S, Qt::ControlModifier)`（及
  KeyRelease）`QApplication::sendEvent(shell->windowHandle(), …)`，走 Qt 的 shortcut map
  （`WindowShortcut` 上下文），不是直接点 handler。
* 菜单：从 `shell->menuBar()->actions()` 里找到持有 `saveAction` 的 `QMenu`，然后触发**那个 QAction**。
* 这些断言都在**同一进程的 KTextEditor provider**上执行；断言对象是磁盘字节、
  `currentFilePath()` 不变与临时文件不存在，而不是“函数返回 true”。

## 4. 运行记录（原样摘录）

```
$ cmake --build target/qt6-build            # 全量
BUILD-ALL-EXIT=0

$ vedit-app --selfcheck --state-dir /tmp/t71-evidence-state   (QT_QPA_PLATFORM=offscreen)
APP-EXIT=0
APP-SHELL-SELFTEST ok=1 checks=160 failures=0 state=/tmp/vedit-app-YRKEcH/state

$ ctest -R vedit-app-shell-selftest
1/1 Test #9: vedit-app-shell-selftest ... Passed
100% tests passed, 0 tests failed out of 1     TARGET-CTEST-EXIT=0

$ bash build/gates/typecheck.sh
[typecheck] PASS (pinned image vedit-build:local)
TYPECHECK-EXIT=0
```

根 ctest 全量（16 项）：**15/16 通过**。唯一失败 `#13 vedit-workspace-search-selftest`
（`lifecycle.worker-running`）属 **搜索 lane（T41/T21）在飞**，不在本任务写域、本任务未碰
`src/workspace/search/**`；另有一次运行中 `#7 vedit-terminal-selftest` 也红过（terminal lane
在飞，下一次运行又通过）。本任务目标 `#9 vedit-app-shell-selftest` 在所有运行中都通过。
需要说明：captain 提到的“基线 16/16”是更早构建的树；当前共享工作树因其它 lane 的在飞改动
并非 16/16，这是我不能替它们修的部分。

## 5. 没有验证 / 边界（如实）

1. **真实键盘路径**：无显示服务器，无法验证 OS 键盘事件进入 Qt 的整条链路；无头验证到的是
   “Qt 快捷键系统 → QAction → 保存”这一段，以及真正的绑定值。停点 1 需用户在有屏机器上按
   Ctrl+S 确认。
2. **鼠标点菜单**：自检触发的是 File 菜单里真实的 `QAction`（菜单由 `menuBar()` 构建），但
   “鼠标点击 → QMenu → QAction”的 GUI 部分同样只能由用户验收。
3. **多 pane**：断言使用 active pane；切换 pane 后保存另一个 pane 的文档没有单独断言（入口
   只取 `activePane()`，逻辑单一；如需要可另开断言）。
4. 其它 lane 的 ctest 红（搜索/偶尔 terminal）见 §4，与本任务无关且未修改。

## 6. 证据文件（`evidence/save-binding/`，均带 C9 头）

| 文件 | 内容 |
|---|---|
| `01-design-notes.md` | 两条路径的调用链、失败可见性、为什么无头断言是“真”的、为什么不动内核 |
| `02-selfcheck-save.log` | 21 条 `save.*` CHECK + `APP-SHELL-SELFTEST ok=1 checks=160 failures=0` |
| `03-root-ctest.log` | 目标 ctest 通过 + 全量 ctest 结果（含外部 lane 失败归因） |
| `05-typecheck.log` | `TYPECHECK-EXIT=0` 全日志 |
| `06-changed-files.txt` | `git status`/`git diff --stat`、命令声明行、Ctrl+S 唯一绑定、仅 main.cpp 含 KTextEditor 头 |

复跑：
```sh
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'cmake --build target/qt6-build && QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build -R vedit-app-shell-selftest --output-on-failure'
bash build/gates/typecheck.sh
```
