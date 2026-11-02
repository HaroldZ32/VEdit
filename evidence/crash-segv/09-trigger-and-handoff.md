# T117 — 折叠崩溃：复现、栈、触发条件（交给 T120 / Senior Engineer 3）

## 复现（已实际运行，18:54）

探针（我临时加的 `src/app/fold_crash_probe.cpp`，取证后已删除，避免与 SE3 的
`src/syntax/folding/tests/fold_interaction_tests.cpp` 重复）：
真 `KTextEditorProvider` + 真 view + 1800 行 SV（60 modules × 30 行）→ 按产品路径
`FoldBreadcrumbBinding::onDocumentOpened()`（注入 120 条 fold）→ **`view->setCursorPosition(Cursor(120,0))`** → SIGSEGV。

命令（当时的构建）：
```
XDG_STATE_HOME=/tmp/probe-state QT_QPA_PLATFORM=offscreen \
  target/qt6-build/src/app/vedit-fold-crash-probe
```
结果：`exit=139 (SIGSEGV)`，崩溃报告见 `04-crash-report.log`。

## 栈（符号化，来自新装的崩溃报告器）

```
signal=11 (SIGSEGV)  si_code=1 (SEGV_MAPERR)  fault_address=0x0
last_events: probe start / fixture / view+binding / opened / folds-injected
backtrace:
  vedit-fold-crash-probe(vedit_crash_handler+0x8fb)
  libc.so.6(+0x45cb0)
  libKF6TextEditor.so.6(+0x27c00c)          <-- 空指针解引用发生在 KTextEditor 内部
  vedit-fold-crash-probe(+0x17fd86)          <-- main, fold_crash_probe.cpp:112
  vedit-fold-crash-probe(main+0x652)
```
`addr2line`：`+0x17fd86` = `fold_crash_probe.cpp:112 (discriminator 2)`，即
**`view->setCursorPosition(KTextEditor::Cursor(120, 0))`** 这一行；KTextEditor 内部
`+0x27c00c` 的帧在释放 `QArrayDataPointer<char16_t>` 附近、解引用空指针。

## 触发条件（当前证据）

- **必须已注入折叠**（崩溃前最后一步是 120 条 fold 注入；`FimBinding`/VCS/settings 全未参与）。
- **把光标放到被折叠隐藏的行**（第 120 行落在一个已折叠的 module/always_comb 区间内）。
  这与 captain 的假设一致：折叠后行号/可见性变化，`setCursorPosition` 到隐藏行会让
  katepart 的折叠/光标路径空指针崩溃。
- **产品里谁会这么做**（高危路径，供 T120 排查）：
  1. `FoldInjector::unfoldRegistered()` —— 对 registry 里每个 Range **先 `setCursorPosition(startLine,0)` 再 `unfold`**；
     嵌套 fold（module 内嵌 always_comb/begin-end）时，**内层 fold 的 startLine 可能已被外层折叠隐藏** → 正是这个崩溃。
     用户「往回叠」时触发 rebuild（Reparsed/Toggled）就会走这条路径。
  2. 搜索跳转 / goto-line / 面包屑恢复光标到被隐藏的行。
  3. 用户手动 fold 后任何 `notifyReparsed()`（我们的 rebuild）。
- 这与 T116 的 4000×16 ms 线索同源：**全量注入 4000 条嵌套 fold 后，几乎任何“重建/跳转”都会命中隐藏行**。
  可见窗口注入（captain 给的主线方案）会同时降低 66 s 墙和这条崩溃的概率。

## 建议修复方向（由 T120/SE3 定夺，我不改 `src/syntax/folding/**`）

1. **unfold 前不要盲设光标**：`unfoldRegistered()` 按“外层先、内层后”排序（按 start 升序/end 降序），
   每展开一个就把下一个内层 fold 变为可见；或先用 `view->cursorPosition()` 检查目标行**当前是否可见**
   （`KTextEditor::View` 的可见行集合 / `document->hiddenLines`?），不可见则先展开其父。
2. 展开改为“从最外层开始”的循环，直到所有 registry 行可见，再逐个 unfold。
3. 注入策略改为**可见窗口优先**（captain 的主线）：大幅减少同屏 fold 数量，直接绕开大面积嵌套。
4. 回归用例（加到 SE3 的 harness）：注入嵌套 fold 后，对**隐藏行**执行 `setCursorPosition` 必须不崩；
   以及 `notifyReparsed()` 在嵌套 fold 存在时必须不崩。**当前 27/27 ctest 从未覆盖“注入后的光标移动/重建”。**
