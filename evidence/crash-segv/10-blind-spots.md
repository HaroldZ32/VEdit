# T117 — 为什么 27/27 ctest 没抓到这次崩溃

用户是在**注入大量折叠后、手动往回叠**时崩的。现有测试没有一个序列覆盖这条路径：

1. **折叠测试止步于“注入了多少条”**：`vedit-folding-tests` / kernel 折叠测试断言
   `injected`、`rejected`、`a1.*` 自验与计数，**从不移动光标、不执行 fold/unfold 命令、不重建**。
   SE3 的 `fold_interaction_tests` 是 T120 才开始补的（A–D 序列目前都不崩，说明触发条件更窄：
   **光标落在隐藏行**，而不是命令本身）。
2. **注入规模不对**：ctest 用 1000 行 / 200 folds；产品 10k 行 / 4000 folds。
   嵌套层的极端情形（多层 module/always_comb）在测试里很少。
3. **没有“注入后交互”**：没有 `setCursorPosition` → `notifyReparsed` → `releaseFolds` → 再注入的序列；
   而这是崩溃路径（`unfoldRegistered()` 对嵌套 start 行设光标）。
4. **headless 不重绘**：多数 lane 测试只 `processEvents` 少量次数，不触发完整布局/重绘。
   本次 coredump 里还发现过一个 search-selftest 在 `QWidgetRepaintManager::paintAndFlush` 内崩的
   **历史栈**（10 月 31 日–11 月 1 日 14:27-14:50，现已不复现：10/10 次重跑全绿），说明绘制路径
   本来就少测。
5. **一进程一文档**：每条 ctest 一个短进程；真实用户是“长会话 + 反复开关文件 + 折叠 + 跳转”。
   T94 的 `releaseFolds` 就是“同 pane 二次打开”的崩溃，说明这类会话序列是系统性盲区。
6. **无 ASan/崩溃取证**：ctest 崩溃只会显示 “Failed”，没有栈。**本次交付的崩溃报告器就是补这一环**：
   下次任何测试/真机崩溃都会留下带符号的 `crash-*.log`，这类问题不再“无从查起”。

**结论（给 captain）**：盲区 = **“大量注入折叠后的用户交互/重建序列”** 完全没测；模型层计数全绿掩盖了交互层。
建议 T120 的交互 harness 直接进 ctest（在真 view + 嵌套注入下做 光标移动/重建/unfold），
并把崩溃报告器作为所有 lane selftest 的公共前置（本轮已可在 `src/app` 侧复用）。
