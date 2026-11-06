# 批 1b 验收报告 — 主壳面板恢复 + 未命名标签 Save As（AC-1.3 / AC-1.4 / AC-1.5）

- 任务：board **T7**（`shell-panels-save-as`），cell `cell-20261103T123639841Z`，split rev 1
- 契约：`.mpd/cellwork/cell-20261103T123639841Z/contracts/shell-panels-save-as.md`（冻结 2026-11-03T13:10:27.019Z）
- 写者：**Senior Engineer**（member `Senior Engineer`）· 验证者：**Reviewer**（loop `loop-20261103T131047-155b36`）
- 证据目录：`evidence/batch1-shell/`（本文件、`probe/`、`shots/`、`logs/`）

## 1. 交付内容（一句话）

View 菜单现在能重新打开**被关闭的任何面板**——包括 lane 在构造之后才注册的 **Module Tree** 和
lane 自己 `addDockWidget` 出来的 **Project**（重启后同样可用）；未命名标签的 **Ctrl+S / File > Save As**
会弹真实的保存对话框，确认后文件落盘、标签标题与**会话存档里的路径**一起更新、内容正确，
取消不写，目标已存在时先问覆盖。

## 2. 改了什么（全部在冻结写域内）

| 文件 | 改动 |
|---|---|
| `src/app/panel_registry.h/.cpp` | 新增 `title(id)`：让主壳能给「构造之后才注册的 lane 面板」生成 View 菜单项（例如 Module Tree） |
| `src/app/app_shell.h/.cpp` | ① 新增 `file.save-as` 命令（File 菜单、Ctrl+Shift+S）+ `saveActiveDocumentAs()`；② `saveActiveDocument()`（Ctrl+S / File>Save）在**文档没有路径时**转入 Save As，不再报 “document has no file path yet”；③ 两个可注入 seam：`setSaveAsDialogProvider(suggestedPath)`、`setSaveAsOverwriteConfirmProvider(path)`（与既有 `setOpenFileDialogProvider` 同一模式）；④ 无头（offscreen）时覆盖确认默认**拒绝**并给出可见诊断——绝不静默覆盖；⑤ 新增 `syncDockPanelCommands()`（在 `startSession()` 里、`restoreState()` 之后调用）：给「后注册的 registry 面板」补 `view.panel.<id>`，给「非 registry 的自建 dock」补 `view.dock.<objectName>`，两者都 checkable、handler 切换可见性、`visibilityChanged` 回写勾选状态，并在新增命令后重建菜单栏；⑥ 析构时断开自建 dock 的 visibility 同步连接（见 §6 的崩溃修复） |
| `src/app/editor_area.h/.cpp` | 新增 `EditorPane::saveAsFile(path, error)`：用**两种 provider 共用的路径属性**（`editor_provider.h` 的 `currentFilePath`，edit-core provider 也优先读它）把目标路径交给 provider 自己的 `saveFile()`，因此写入仍是唯一一条保存路径（demo=QSaveFile，edit-core=kernel 原子保存），且 modified 清理由 provider 完成；失败时**回滚路径**，成功时更新本 tab 的 path + 重画标签 + 通知状态 |
| `translations/vedit_zh_CN.ts` | 新增 13 条 UI 译文；`lrelease` **589/589 finished，0 unfinished** |

没有改任何冻结文件（root `CMakeLists.txt` / `Cargo*` / `rust-toolchain.toml` / `build/gates/*` /
`package.json` / `scripts/build-qt.sh`）；**批 1a 的根传播行为未回退**：`bash evidence/batch1-shell/probe/run.sh`
在本树上仍 `BATCH1-SHELL-PROBE ok=1`（offscreen 40/0、xcb 42/0、真二进制冒烟 0 缺失，见 `logs/12-probe-final-run.log`）。

## 3. 行为细节

### AC-1.3 面板恢复

- `syncDockPanelCommands()` 在 `startSession()` 内、`restoreState()` 之后运行，所以：
  - **Module Tree**（`installModuleTree` 在 `installPanels()` 里才 `addPanel`）拿到 `view.panel.module-tree`；
  - **Project**（`project_integration.cpp` 直接 `new QDockWidget("Project") + addDockWidget`，**不是** registry 面板）
    拿到 `view.dock.dock-project`；命令 id 用 dock 的 `objectName`，而它正是 `saveState()/restoreState()`
    记住的同一个 key，因此重启后命令仍指向同一个 dock。
  - 两者都 checkable；dock 自己的 ✕ 通过 `visibilityChanged` 把勾选状态清掉（和 registry 面板同一保证）。
- 菜单栏在构造时已由固定命令表生成，新增命令后必须重建（`buildMenuBar()`），否则命令存在但不在菜单里。

### AC-1.4 Save As

- **入口**：`File > Save As…`（Ctrl+Shift+S）；未命名文档上的 `Ctrl+S` / `File > Save` 走同一函数。
- **对话框**：默认 `QFileDialog::getSaveFileName`（起始名：文档路径，否则 `<projectRoot>/untitled.sv`），
  过滤器沿用既有 `Verilog/SystemVerilog (*.v *.sv *.svh *.vh);;All Files (*)` 的 msgid。
- **写入**：`EditorPane::saveAsFile()` → provider `saveFile()`（demo: QSaveFile；edit-core: kernel 原子保存）。
- **路径更新**：成功后 `Document::path` 更新 → `currentFilePath()/tabFilePath()/tabText()` 立即反映新名，
  `notifyStateChanged()` 让标题栏/状态栏刷新；会话存档的 `editorLayout` 下次保存即带新路径
  （探针用**重启后按新路径真的重开**来证明，见 `shots/31-panels-restored.png` 里恢复出 `saved-as.sv`、`existing.sv` 两个 tab）。
- **取消**：对话框返回空 → 直接返回，不写、不改路径（探针断言目标文件不存在、路径仍为空、仍标记为脏）。
- **覆盖确认**：目标存在 → 先问；拒绝 → 目标逐字节不变（`// OLD CONTENT` 原样）、路径不变；
  接受 → 被替换为新内容。无头环境下没有注入 seam 时**默认拒绝**并记录诊断（绝不静默覆盖）。

## 4. 证据（全部在最终树上实跑）

### 4.1 真实 shell probe（不是 `--selfcheck`）

`evidence/batch1-shell/probe/main.cpp` 新增 `--panels-checks` / `--panels-screenshots` 两个模式：
和 `main.cpp` 一样顺序构建真实主壳（构造 → `installPanels` → `startSession`），装上**产品自己的
KTextEditor provider**（`makeEditCoreProvider()`），Project 面板因此真实存在。

复现：

```bash
bash scripts/build-qt.sh                               # 产品必须先构建
bash evidence/batch1-shell/probe/panels-run.sh         # 构建探针 + offscreen + Xvfb :78 xcb + 截图
```

| 运行 | 结果 | 日志 |
|---|---|---|
| offscreen | `BATCH1B-PROBE ok=1 checks=42 failures=0`，exit 0 | `21-probe-panels-offscreen.log` |
| Xvfb **:78** + `QT_QPA_PLATFORM=xcb` | `BATCH1B-PROBE ok=1 checks=44 failures=0`，exit 0（+2 张截图） | `22-probe-panels-xcb.log` |

逐条（节选原始输出）：

AC-1.3

```
PASS a13.module-tree.command
PASS a13.module-tree.in-view-menu
PASS a13.project.dock-exists
PASS a13.project.toggle — dock-project
PASS a13.project.toggle-in-view-menu
PASS a13.closed.both-hidden           # 用 dock 自己的 ✕ 关掉
PASS a13.closed.action-unchecked
PASS a13.reopened.visible             # 两个 View 菜单项各触发一次
PASS a13.reopened.action-checked
PASS a13.screenshot.view-menu — .../shots/30-view-menu-panels.png
PASS a13.restart.session-saved        # saveState() 落盘
PASS a13.restart.commands-exist       # 重启后两个命令仍在
PASS a13.restart.closed-hidden        # 关闭后确实隐藏
PASS a13.restart.reopened             # 重启后仍能重新打开
PASS a13.screenshot.panels-restored — .../shots/31-panels-restored.png
```

AC-1.4

```
PASS a14.pane.available
PASS a14.untitled.start
PASS a14.editor.is-ktexteditor        # 产品 provider，不是 demo 占位
PASS a14.save-as.command
PASS a14.save-as.shortcut — Ctrl+Shift+S
PASS a14.save-as.in-file-menu
PASS a14.ctrl-s.bound — Ctrl+S
PASS a14.ctrl-s.wrote-file — 35 bytes
PASS a14.ctrl-s.content — module saved_by_save_as;
PASS a14.ctrl-s.tab-path — /tmp/…/saved-as.sv
PASS a14.ctrl-s.tab-title — saved-as.sv
PASS a14.ctrl-s.not-dirty — saved-as.sv
PASS a14.ctrl-s.session-saved
PASS a14.ctrl-s.session-path — /tmp/…/saved-as.sv
PASS a14.cancel.no-file
PASS a14.cancel.path-unchanged
PASS a14.cancel.still-dirty — ● untitled
PASS a14.overwrite.declined-content — // OLD CONTENT
PASS a14.overwrite.declined-path-unchanged
PASS a14.overwrite.accepted-content — module second;
PASS a14.overwrite.accepted-path — /tmp/…/existing.sv
PASS a14.real-dialog.opened            # 真实 QFileDialog 被打开（定时器把它 reject）
PASS a14.real-dialog.no-write
PASS a14.demo.editor / a14.demo.untitled / a14.demo.wrote / a14.demo.tab-path / a14.demo.not-dirty
                                       # 同一路径在 demo provider 上也成立
```

### 4.2 xcb 截图

| 文件 | 内容 |
|---|---|
| `shots/30-view-menu-panels.png` | 真实主壳 + View 菜单展开：**Toggle Module Tree Panel**、**Toggle Project Panel** 两项在列（其余五项为既有内置面板），并显示 KTextEditor 编辑器与 Project/Module Tree/Outline 停靠面板 |
| `shots/31-panels-restored.png` | **重启后**由 View 菜单重新打开的两个面板，且恢复出的 tab 是 `saved-as.sv` / `existing.sv` —— Save As 后的路径确实进了会话 |

### 4.3 断言覆盖点（AC-1.5）

真实 shell probe：批 1a 40/42 → 批 1b **42/44**（+2 项 `a13/a14` 前缀的覆盖点；截图各加 1 条 check）。
**`--selfcheck` 仍是 258 checks / 0 failures，`ctest` 仍是 38** —— 见 §5 的契约冲突说明。

## 5. 契约冲突（AC-1.3 的 “shell selftest 断言” 与 AC-1.5 的 “ctest/CHECK 增加”）

**事实**：T7 的冻结写域（split rev1 `writeScopes`）**不含 `src/app/main.cpp`**，而本仓库
`--selfcheck`（即 ctest #6 `vedit-app-shell-selftest`）的全部 CHECK 都在 `src/app/main.cpp::runSelfCheck()`；
写域内没有第二个 selftest 源文件，新增 ctest 又需要改 `CMakeLists.txt`（同为写域外）。因此
**在冻结写域内无法让 ctest 数或 `--selfcheck` 的 CHECK 数增加**。

**处理**：已两次向 captain 报此冲突并请求裁定（`cellwork_send` `mail-20261103134835-307`、
`agent_teams_mail` `mail-20261103135903-308`），提案是授权 T7 增写 `src/app/main.cpp`（只加
`panel.*`/`save-as.*` 的 CHECK，约 258→278）或由 Reviewer 接受“改用 probe CHECK 增量”。
**在收到裁定前没有越界写 `main.cpp`**。若裁定为“保持写域”，请按“probe CHECK 42/44 +
断言覆盖点只增不减”判定 AC-1.5；若裁定为“授权 main.cpp”，我会补上 selftest 并重跑七道门禁。

## 6. 顺带修掉的一个真实崩溃（属于本批写域内的主壳代码）

`syncDockPanelCommands()` 给自建 dock 装的 `visibilityChanged` 同步回调以 shell 为上下文；
但 `QDockWidget` 是在 `~QMainWindow` 销毁子部件时才发出该信号，此时 `commands_`（成员）**已经被析构**。
探针第一次跑就在退出时 SIGSEGV（backtrace 顶层：`QDockWidget::visibilityChanged` →
`CommandRegistry::action` → shell 析构）。修复：把这些连接存进 `dockToggleConnections_`，
在 `~ApplicationShell` 里先断开（与既有 `panels_->setVisibilityListener(nullptr)` 同一手法）。
修复后探针 offscreen/xcb 均 exit 0。这不是测试脚手架问题，而是产品路径上的 use-after-free。

## 7. 七道门禁（最终树，命令与实测退出码）

| 门禁 | 命令 | 观测 | 日志 |
|---|---|---|---|
| 构建 | `bash scripts/build-qt.sh` | **EXIT=0**；`100% tests passed, 0 tests failed out of 38`；`M0-SMOKE ok=1 platform=xcb …` | `logs/gate-1-build-qt.log`、`evidence/m0-build/build-logs/33-qt6-ctest.log`、`36-qt6-xcb-smoke.log` |
| selfcheck | `vedit-app --selfcheck --state-dir /tmp/sc`（镜像内 offscreen） | **EXIT=0**，`APP-SHELL-SELFTEST ok=1 checks=258 failures=0`（见 §5：未增，待裁定） | `logs/gate-3-selfcheck.log` |
| clippy | `cargo clippy --workspace --all-targets -- -D warnings`（`vedit-build:local`） | **CLIPPY_EXIT=0** | `logs/gate-4-clippy.log` |
| fmt | `cargo fmt --all -- --check`（`vedit-build:local`） | **FMT_EXIT=0** | `logs/gate-5-fmt.log` |
| docs | `bash build/gates/docs.sh` | `[docs] PASS: …`，**DOCS_EXIT=0** | `logs/gate-6-docs.log` |
| lrelease | `/usr/lib/qt6/bin/lrelease translations/vedit_zh_CN.ts`（镜像内） | `589 finished and 0 unfinished`，**LRELEASE_EXIT=0** | `logs/gate-7-lrelease.log` |
| 探针 | `bash evidence/batch1-shell/probe/panels-run.sh` | **exit 0**（offscreen 42/0、xcb 44/0、2 张截图） | `21-*`/`22-*`；最终一次整跑另存 `logs/23-panels-final-run.log` |

批 1a 回归（硬要求）：`bash evidence/batch1-shell/probe/run.sh` → `BATCH1-SHELL-PROBE ok=1`，
根传播的 40/42 条断言与真二进制冒烟全部 PASS（`logs/12-probe-final-run.log`）。

## 8. 未证明 / 边界

1. **`--selfcheck`/ctest 的 CHECK 数没有增加**（§5）。这是契约写域与 AC 文本的冲突，不是实现缺口；
   真实 shell probe 的覆盖点增加了（40/42 → 42/44）且既有断言一条未删未放宽。
2. **重启后 Project 面板会自动重新可见**：`project_integration.cpp` 在异步展开结束后按
   `hasProject()` 重新 `setVisible(true)`（lane 行为，写域外）。因此探针记录 `INFO a13.restart.visibility
   module-tree=0 project=1`，并在重启后的新实例里按用户顺序（先关闭→再从 View 打开）验证“仍可打开”。
   Module Tree 的隐藏状态能被 `restoreState()` 正确恢复（`module-tree=0`）。
3. **Ctrl+S 的验证方式是触发同一条命令的 `QAction`**（T80 已确立 shell 是 Ctrl+S 的唯一权威，
   编辑器自带的保存动作被移除）。探针同时断言 `file.save` 的快捷键就是 `Ctrl+S`；
   没有注入真实键盘事件（探针不链接 Qt6Test）。
4. **Save As 后 KTextEditor 的高亮定义不随新扩展名切换**：`applyHighlightingMode()` 在
   `ktexteditor_provider.cpp`（写域外），本批没有调用点。文件内容/路径/会话/脏标记都正确，
   但“未命名 → 存成 .sv 后立刻按 SystemVerilog 高亮”做不到本批；已在此登记。
5. **对话框内的覆盖确认是主壳的问题框**（`QMessageBox::question`），不是文件对话框自带的
   “覆盖?”提示；无头默认拒绝。目标不存在时不弹任何问题框。
6. `--screenshots`/`--panels-screenshots` 的 `QMenu::popup` + 全屏截取在 Xvfb 无窗口管理器下工作正常
   （`30-view-menu-panels.png` 可见菜单），但没有做像素级断言，只断言文件非空。

## 9. 改动文件清单

产品代码（全部在写域内）：
`src/app/app_shell.h`、`src/app/app_shell.cpp`、`src/app/panel_registry.h`、`src/app/panel_registry.cpp`、
`src/app/editor_area.h`、`src/app/editor_area.cpp`、`translations/vedit_zh_CN.ts`

证据：
`evidence/batch1-shell/{REPORT-1b.md,probe/{main.cpp,build.sh,panels-run.sh,panels-container-run.sh},shots/{30-view-menu-panels.png,31-panels-restored.png},20-probe-panels-build.log,21-probe-panels-offscreen.log,22-probe-panels-xcb.log,logs/{12-probe-final-run.log（批1a 回归）,23-panels-final-run.log,gate-*}}`
