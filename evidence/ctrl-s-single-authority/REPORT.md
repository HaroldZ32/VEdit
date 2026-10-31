# T80 修复报告 — Ctrl+S "Ambiguous shortcut overload"（停点 1 真机缺陷）

- Cell: `cell-20261031T085543161Z` · Board task: **T80**
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-10-31（Asia/Shanghai）· 基线 `HEAD 95c496e`
- 结论: **已修复并 headless 验证通过**。根因证实为「我们的 `file.save` 与 KTextEditor 视图自带的
  `KStandardAction::save` 同时绑定 Ctrl+S」，Qt 记录 `Ambiguous shortcut overload: Ctrl+S` 并**两个都不触发**。
  修法取任务书 (a)：shell 的 `file.save` 是唯一权威入口，编辑器 widget 自带的 Ctrl+S 绑定在进入 pane 时被清空
  （action 本体保留，仍可从编辑器自己的菜单触发）；保存链路仍是 T71 的 `EditorPane::saveFile → provider →
  atomicSaveText`，未新增保存逻辑。
- 无显示服务器：全部验证为 headless（offscreen + pinned 镜像）；真机物理键盘仍需用户在停点 1 复验。

## 1. 复现与取证（先证实）

修前在 `--selfcheck` 中做两件事并真跑：

1. **枚举**当前 shell 内所有带 Ctrl+S 的 action（`objectName / class / parent / shortcut / shortcutContext /
   关联对象 / holders`），输出样例（`01-before-red.log`）：

```
CTRL-S-BINDING objectName=file_save class=QAction shortcut=Ctrl+S context=3 associated=[KateViewInternal()] holders=[KateViewInternal()] chain=KActionCollection<
CTRL-S-BINDING objectName=file_save ...（每个 KTextEditor 视图一条，当时 3 个 pane）
CTRL-S-BINDING objectName=action-file.save class=QAction shortcut=Ctrl+S context=1 associated=[QMenu()] holders=[QMenu()] chain=QMainWindow<
CHECK save.shortcut.unique FAIL bindings=4
```

2. **在真实用户路径下触发**：先 `shell->activateWindow()` + `editorWidget()->setFocus()`，再向窗口发送真实
   Ctrl+S `QKeyEvent`。修前日志里出现 Qt 自己打的：
   ```
   QAction::event: Ambiguous shortcut overload: Ctrl+S
   CHECK save.shortcut.disk-bytes FAIL
   CHECK save.shortcut.modified-cleared FAIL
   ```
   即：**Ctrl+S 被 Qt 判为歧义，磁盘没有变化、document 仍是 modified** —— 与用户真机报告一致，缺陷在无头环境
   可复现（这也回答了「T71 模拟为什么没抓到」：T71 的键盘用例没有先把焦点放到编辑器视图上，焦点不在视图时
   KTextEditor 的 action 不参与匹配，于是只跑了我们的 action。本次已把这条用例改成「先聚焦真实 view」）。

## 2. 根因细节

- 我们的 `file.save`：`objectName=action-file.save`，parent 是 `QMainWindow`，挂在 File 菜单，`context=1`
  （WidgetWithChildrenShortcut）。
- KTextEditor 视图自带的标准保存 action：`objectName=file_save`，parent 是 **无 QObject 父对象的
  `KActionCollection`**，实际注册在视图内部子 widget **`KateViewInternal`** 上（`associated/holders` 都是它），
  `context=3`（ApplicationShortcut），快捷键来自 `KStandardAction::save` = Ctrl+S。
- 两者在同一窗口上下文同时可用 → Qt `QShortcutMap` 判歧义 → 两个 action 都不触发。

注意：该 action **既不在视图自己的 `actions()` 里，也不是视图的 QAction 子对象**（parentless collection），
所以「从 editor widget 自身清 action」不够，必须遍历编辑器 widget 的**整个子 widget 树**取 `widget->actions()`。

## 3. 修法（选 (a)，理由）

`src/app/editor_area.cpp`：

- 新增 `clearEditorSaveShortcut(QWidget *editor)`：遍历 `editor->actions()`、编辑器**全部子 widget 的
  `actions()`**、以及 `editor->findChildren<QAction*>()`，把快捷键列表里的 `Ctrl+S` 移除
  （`action->setShortcuts(kept)`；action 本体保留、其它快捷键保留）。
- 在 `EditorPane::replaceEditorWidget()` 中调用：**每个 pane 的每次创建/替换**（首屏 demo → KTextEditor、
  provider 切换、分屏新增 pane）都走这条路径。
- 由于 KTextEditor 的 action 是**懒填充**且 collection 没有 QObject 父对象，另外在 `EditorPane::eventFilter`
  的 `Show` / `FocusIn` 以及事件循环下一拍（`QTimer::singleShot(0)`，`QPointer` 保护）再应用一次，保证
  「用户按下 Ctrl+S 之前」不变量一定成立。

`src/app/main.cpp`：

- 新增 `collectShortcutActions() / hasCtrlSShortcut() / ctrlSActions() / printCtrlSBindings()`：
  枚举 + 打印 + 断言用（Qt 视角：窗口内所有 widget 的 `actions()` + QAction 子对象）。
- T71 的键盘用例改为**先聚焦编辑器视图**再发 Ctrl+S（真实用户路径；这就是让缺陷暴露的改动）。
- **新增 3 条断言**（防同类回归，只增不减）：
  - `save.shortcut.unique`：整个 shell 内 Ctrl+S 绑定数 **恰好 1**；
  - `save.shortcut.authority`：那唯一一条必须是 shell 的 `file.save` action；
  - `save.shortcut.unique.multi-pane`：**分屏新增一个 pane 后**仍恰好 1 条（验证修复覆盖每个 pane）。

没有改 `src/editor/**`（只通过公开接口观察行为）、`src/workspace/**`、也没有另写保存逻辑；
保存失败仍走 `saveActiveDocument()` 的 `Save failed` 状态栏 + 诊断（T71 的 `save.failure.*` 全绿）。

## 4. 验证（全部真跑，引用输出）

| 验证 | 命令 | 观察 |
|---|---|---|
| 修前红（真实缺陷复现） | `vedit-app --selfcheck`（offscreen） | exit **1**；`save.shortcut.unique FAIL bindings=4`；`Ambiguous shortcut overload: Ctrl+S`；`save.shortcut.disk-bytes FAIL`、`save.shortcut.modified-cleared FAIL`（`01-before-red.log`） |
| 修后绿 | 同一条命令 | exit **0**；`CTRL-S-BINDING` 只剩 `action-file.save`（context=1，chain=QMainWindow），**bindings=1**；`save.shortcut.unique PASS`、`save.shortcut.authority PASS`、`save.shortcut.unique.multi-pane PASS`；聚焦视图后 Ctrl+S `disk-bytes PASS`、`modified-cleared PASS`；`APP-SHELL-SELFTEST ok=1 checks=176 failures=0`（基线 161，只增不减）（`03-after-final.log`） |
| 根 ctest | `ctest --test-dir target/qt6-build --output-on-failure` | `100% tests passed, 0 tests failed out of 17`，exit **0**（`03-after-final.log`；当前树里另一个 lane 新增了第 17 个测试，基线 16/16） |
| typecheck | `bash build/gates/typecheck.sh` | `[qt6] PASS ... warnings-as-errors`、`[typecheck] PASS (pinned image vedit-build:local)`，exit **0**（`04-typecheck.log`） |
| 全量 gate（附加） | `VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh` | `100% tests passed ... out of 17`、`M0-SMOKE ok=1 platform=xcb`、`GATE-EXIT=0` |

**新增断言真能红的证明**：修前日志 `01-before-red.log` 就是该断言在**真实缺陷代码**上的 red（`bindings=4`），
不需要再做「临时恢复双重绑定」的合成实验，因此也**没有留下任何临时改动**：最终 `git diff` 只包含本任务的修复
（`src/app/editor_area.cpp` + `src/app/main.cpp` 两文件，均为 T80 写域）。

## 5. T71 断言保持

T71 的 21 条 `save.*` 断言全部保留并全绿（`save.command.registered`、`save.shortcut.ctrl-s`、
`save.menu.file-item`、`save.demo.*`、`save.ktexteditor.document`、`save.shortcut.window/disk-bytes/
modified-cleared/path-unchanged`、`save.menu.*`、`save.failure.*`、`save.recovery.*`）；
没有任何断言被删除或弱化；键盘用例只**加强**为「先聚焦真实编辑器视图」。

## 6. 未验证 / 局限（UNVERIFIABLE-IN-CONTAINER）

- **物理键盘**：测试通过 `QKeyEvent` 送进窗口，走的是 Qt 的 shortcut map（与真实按键同一条路径），但没有真实
  键盘/显示服务器；用户需在停点 1 真机复验 Ctrl+S。
- 没有视觉/交互录屏证据（无显示器）。
- 真机上的键盘布局/输入法层不在本环境范围内。

## 7. 证据与改动清单

证据 `evidence/ctrl-s-single-authority/`：`01-before-red.log`（修前红 + 绑定枚举）、`02-after-green.log`
（修后中间态绿）、`03-after-final.log`（最终 selfcheck + 根 ctest）、`04-typecheck.log`。

改动文件（写域 `src/app/**`）：
- `src/app/editor_area.cpp`：新增 Ctrl+S 去重 helper，在 pane 的 editor widget 安装路径上应用（+ Show/FocusIn/
  下一拍兜底）。约 +80 行（含注释）。
- `src/app/main.cpp`：绑定枚举/打印 helper + 3 条新断言 + T71 键盘用例先聚焦视图。

未 commit/push（Lead 统一负责）。
