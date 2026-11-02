# T88 独立评审结论 — T80（Ctrl+S 单一权威）＋ T81（真 KTextEditor 高亮 mode）

> **手工建任务、无冻结契约**：T80/T81/T88 在 cell 里没有 split-key 映射/验证环，判据取自 captain 派单书；
> verdict 落盘于此。评审者 ≠ writer（writer = Senior Engineer）。证据：`evidence/verdicts/ctrl-s-highlight/t88-checks.log`、
> `t88-counter-test.log`。

- **结论**：**PASS**（T80、T81 两条修复均经我独立实跑/反证成立）＋ **2 条 Low finding**（证据标签、残存风险）。
- **回归**（我实跑）：根 ctest **18/18**（exit 0）、`vedit-app --selfcheck` **176 checks / 0 failures**（exit 0）、
  Rust clippy **exit 0** + fmt **exit 0**；C++ `-Werror` 半段用隔离构建覆盖（见 §回归说明）。
- 基线：`HEAD 9a6b828`；共享构建树 `target/qt6-build` 构建于 23:33（晚于两条修复提交 22:55），我复核时未陈旧。

---

## A. T80 — Ctrl+S 双重绑定 → 单一权威

### A1. 根因与枚举（修前 4 条 → 修后恰好 1 条）✅

- **修后**（我实跑 `vedit-app --selfcheck`）：`CTRL-S-BINDING after-provider-install objectName=action-file.save
  class=QAction shortcut=Ctrl+S context=1 associated=[QMenu()] holders=[QMenu()] chain=QMainWindow<`；
  `save.shortcut.unique PASS bindings=1`；分屏后 `save.shortcut.unique.multi-pane PASS bindings=1`。
- **修前**（我做的反证，见 A4）：`bindings=4` = 3 个 KTextEditor `file_save`（`parent=KActionCollection/associated=KateViewInternal`,
  context=3 ApplicationShortcut）+ 1 个 shell `action-file.save`；分屏后 `bindings=5`。
- 枚举实现是运行期真实枚举：`collectShortcutActions()`（`src/app/main.cpp:114`）收集 root actions + 全部子 widget 的
  `actions()` + `findChildren<QAction*>()`；`hasCtrlSShortcut()` 检查 `action->shortcuts()` 是否含 Ctrl+S
  （`:141`）；断言直接用它计数（`:542`/`:644`）。

### A2. 聚焦视图后 Ctrl+S 真的存盘（不是只断言信号）✅

`src/app/main.cpp:570-592`：把文档文本改成 `module saved_by_ctrl_s;`、`setModified(true)`、
`shell->activateWindow()`、**`editPane->editorWidget()->setFocus(Qt::OtherFocusReason)`**、`pump()`，
然后向窗口发真实 `QKeyEvent`(Ctrl+S)；断言
`save.shortcut.disk-bytes` = **磁盘字节逐字节等于新内容**、`save.shortcut.modified-cleared` = **`!document->isModified()`**。
我实跑：`save.shortcut.disk-bytes PASS`、`save.shortcut.modified-cleared PASS`、`save.menu.disk-bytes PASS`。
（T71 漏掉的正是“先聚焦视图”；该用例已补上。）

### A3. 保存链路未被绕过 ✅

`shell save action → saveActiveDocument → app_shell.cpp:489 pane->saveFile() → editor_area.cpp:195 provider_->saveFile()
→ ktexteditor_provider.cpp:134 atomicSaveText(path, document->text()) → document_io.cpp:174 atomicSaveText(...)`；
`atomic_save.cpp:61 atomicSaveText`（tmp→fsync→rename→fsync(dir)）未被改。
自检里 `save.no-temp-file`/`save.demo.action.no-temp-file` PASS → 仍是原子保存、无临时文件残留；没有新增保存逻辑。

### A4. 新断言真能变红 —— 我独立反证 ✅

在隔离 `/tmp` 副本里，**只**把 `clearEditorSaveShortcut()` 改成提前 return（其余 = HEAD 的枚举器/断言/聚焦逻辑），
用与 `build-qt-in-container.sh` 相同的 configure 参数重建（`CONFIGURE-EXIT=0`、`BUILD-EXIT=0`）后跑 selfcheck：

```text
CHECK save.shortcut.unique FAIL bindings=4
QAction::event: Ambiguous shortcut overload: Ctrl+S
CHECK save.shortcut.disk-bytes FAIL
CHECK save.shortcut.modified-cleared FAIL
CHECK save.shortcut.unique.multi-pane FAIL bindings=5
APP-SHELL-SELFTEST ok=0 checks=176 failures=5      COUNTER-EXIT=1
```

→ 断言在真实缺陷下**确实变红**，且与 writer 的 `01-before-red.log`（`bindings=4`、Ambiguous、disk-bytes/modified-cleared FAIL、
分屏 `bindings=5`）**逐项吻合** → **那份修前红日志是真的**（其打印格式是早期版本的 helper，语义一致）。
仓库本体未做任何修改（反证只在容器内 `/tmp/red` 副本）。

### A5. 风险审查（我的判断）

- 修法**只从 action 的 `shortcuts()` 列表里移除 Ctrl+S**（`editor_area.cpp:49-95`），**action 对象与其其它快捷键保留**；
  KTextEditor 自己的菜单项、`KStandardAction::saveAs`（Ctrl+Shift+S）、快捷键配置页都没有被删除/改写。
- **判断：可接受**。补充理由：本产品的打开路径不设文档 URL，KTextEditor 自带 `file_save` 即便被触发也**没有可保存的 URL**，
  所以“shell 的 `file.save` 是唯一权威”本身就是正确语义，清掉编辑器的重复键位不是丢能力，而是消除一个**本来就存不了盘**的歧义入口。
- **残存风险（F-2，Low）**：不变量在 pane 创建 / Show / FocusIn / 事件循环下一拍重新施加；如果 KTextEditor 在**运行期**
  （例如用户改了 KDE 快捷键方案、重新应用 shortcut scheme）再次给 action 加上 Ctrl+S，歧义可能复现——没有测试覆盖这种运行期重灌。
  当前用户场景（打开→改→Ctrl+S）被覆盖；建议后续加一条“运行期再枚举一次”的兜底或说明。

---

## B. T81 — 真 KTextEditor 下没有高亮 → 选择语法模式

### B6. 诊断结论正确（我读代码历史核）✅

- 修复前 `git show 2193f7e^:src/editor/kernel/document_io.cpp`：`loadFileIntoDocument()` 只有 `document->setText(...)`，
  **无 `setUrl`、无 `setHighlightingMode`** → 文档停在 `highlightingMode='None'` 的机制成立。
- 修复后 `document_io.cpp:46-62`：`setText()` 成功后调用 `applyHighlightingMode(...)`，失败 `qWarning`、回落 `qInfo` 带诊断。
- 我实跑（F2 库）：`HIGHLIGHT diagnose.v: url='' mode='Normal' highlightingMode='Verilog'`、
  `diagnose.sv: highlightingMode='SystemVerilog'` ✓。

### B7. 用的是自有定义（author=VEdit）✅

- 我实跑 `LD_LIBRARY_PATH=<F2>/…` + `--diagnose-highlight`：`ldd` 确认 `libKF6SyntaxHighlighting.so.6 =>
  /vedit-build/target/ksh-selfbuild/F2/…`；仓库只有 3 个定义（`None` + `Verilog`(author=VEdit) + `SystemVerilog`(author=VEdit)），
  `highlightingMode='Verilog'/'SystemVerilog'`、`resolved name='...' author='VEdit' valid=1`。
- F2 库无内嵌定义（我实测）：`libKF6SyntaxHighlighting.so.6.24.0` sha256 `aac288a1…`，`DOCTYPE language` 标记 **0**；
  同机发行版库 **63** → 开发机的 451 份 KDE 定义确实只在发行版库上，F2 才是交付口径。02 号证据与我的结果一致。

### B8. 回退行为可诊断（不静默无高亮）✅

`highlight_mode.cpp:92-150` `applyHighlightingMode()`：无 mode / 定义无效 / `setHighlightingMode` 失败 / 文档报告不一致
→ 返回 false + 诊断文本（调用方 `document_io.cpp:59` `qWarning`）；解析到**非 VEdit** 定义（宿主 KDE）→ 仍设 mode 但
`qInfo` 打印 `'Verilog' resolves to author '…' (not VEdit); the delivered runtime narrows …`。
→ 成功/回落/失败三条路都有可见诊断，不是静默无高亮。

### B9. 折叠未被破坏 ✅

我实跑（F2）：`.v → formats=30 begins=2 ends=2`、`.sv → formats=29 begins=1 ends=1`（**begins/ends 非零**）。
两份 XML 的 sha256 与 T45/T66 登记值一致（`c6adc763…` / `2abba334…`），且 T81 的两个提交（`2193f7e`/`a6d5a04`）
**没有**改 `resources/syntax/` → 折叠数据未被回退。

---

## 回归（我实跑，不跑会重写共享树证据的构建）

| 项 | 命令（隔离方式） | 观测 |
|---|---|---|
| 根 ctest | 容器内 `ctest --test-dir target/qt6-build`（构建树=HEAD 9a6b828 于 23:33 构建） | **100% tests passed, 0 failed out of 18**，`CTEST-EXIT=0` |
| `vedit-app --selfcheck` | 容器内，`--state-dir /tmp/... --no-session-restore`（不写共享 state/证据） | `APP-SHELL-SELFTEST ok=1 checks=176 failures=0`、`APP-EXIT=0` |
| typecheck（Rust 半段） | pinned 镜像内 `cargo clippy --workspace --all-targets -- -D warnings` + `cargo fmt --all -- --check` | `CLIPPY-EXIT=0`、`FMT-EXIT=0` |
| typecheck（C++ `-Werror` 半段） | 按纪律**没有**跑会写 `evidence/gate-infra`+`evidence/m0-build` 的 gate 脚本；改用**隔离 /tmp 构建**（与 `build-qt-in-container.sh` 同参数）从 HEAD 源构建，app 目标带 `-Wall -Wextra -Wpedantic -Werror`（`src/app/CMakeLists.txt:39,51,122`） | `CONFIGURE-EXIT=0`、`BUILD-EXIT=0`（160 targets） |

## Findings

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| **F-1** | **Low** | T81 报告把 `01-diagnosis-prefix.log` 标为「修复前（**交付口径，F2 库** + 私有 XDG share）」，但该日志自身是 `editor_repository_definitions=451` 且解析到 KDE 作者（Yevgen Voronenko / Sean O'Boyle）→ 那一次跑的是**发行版库（内嵌 451 份定义）**，不是 F2。修复前的缺陷结论本身仍成立（代码史证明只 `setText`，且缺陷与库无关），修复后 F2 证据（02）也真实；但这份**修前日志的标签是错的** | 把 01 的标签改成“开发机发行版库（内嵌 451 份 KDE 定义）”；或在修前二进制上补采一次 F2 口径 | `evidence/highlight-realkte/01-diagnosis-prefix.log:12-16` vs `REPORT.md:34`；`02-diagnosis-postfix-f2.log:18-24`（F2：3 个定义、author=VEdit） |
| **F-2** | **Low** | T80 的不变量只在 pane 创建/Show/FocusIn/下一拍施加；若 KTextEditor 在运行期重新应用快捷键方案（用户改 KDE 快捷键、重灌 scheme），Ctrl+S 歧义可能复现，且**无测试覆盖**该运行期场景 | 加一条“运行期再枚举”的兜底测试（或至少在报告与代码注释里写明该残存风险）；当前用户路径已覆盖，不阻塞 | `src/app/editor_area.cpp:137-148`、`:235-243`；selfcheck 只在 install/split 时枚举 |

**非 finding 的观察**：writer 的 `01-before-red.log` 的 `CTRL-S-BINDING` 打印格式（`parent=… widgets=1`）与最终 helper
（`associated/holders/chain`）不同——是同一缺陷在 helper 早期版本上的记录；我的反证用**最终提交的代码**复现了同样的红，语义一致。

## 未能验证 / 边界

1. **物理键盘 / 真机观感**：无显示服务器；Ctrl+S 走的是 Qt shortcut map（与真实按键同路径），但真实键盘与“看到颜色”仍需用户在停点 1 验证。
2. **“颜色”是代理指标**：断言的是 `highlightingMode` + 同一 `KSyntaxHighlighting` 引擎的 formats/folding 计数（KTextEditor 6.24 没有公开 view range 枚举 API），不是像素证据。
3. **完整 `typecheck` gate**：按任务的隔离纪律没有跑原脚本（它会重写共享树的 `evidence/gate-infra`/`evidence/m0-build` 日志）；用等价的两半（Rust clippy/fmt + C++ `-Werror` 隔离构建）覆盖。
4. **运行期快捷键重灌**（F-2 场景）未测。
5. **其它语言**：T81 只覆盖 Verilog/SystemVerilog（报告已声明，其它扩展名保持 KTextEditor 默认）。

## 结论

**PASS**：T80 的“恰好 1 条 Ctrl+S（=`action-file.save`）”在修后由我实跑确认，聚焦视图下 Ctrl+S **真的改变磁盘字节并清除 modified**，
保存仍走 `saveFile → provider → atomicSaveText`；我用“关掉修复”的隔离反证让 `save.shortcut.*` 真红（bindings=4/5、Ambiguous、disk-bytes FAIL），
证明新断言不是摆设、修前红日志为真。T81 的诊断与修复都成立：修前只 `setText` 不选 mode（代码史核实），修后在**无内嵌定义的 F2 库**上
mode 解析到 **author=VEdit** 并产生 formats/folding，回落/失败均有诊断，折叠未回退（begins/ends 非零、XML 哈希未变）。
两条 Low（F-1 修前日志标签、F-2 运行期重灌残存风险）建议随下一次证据整理修掉，不阻塞放行。
