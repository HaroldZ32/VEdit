# T121 — 防复发断言 + 相关线索

## 结论（已确认）

`vedit-workspace-search-selftest` 的 SIGSEGV **由 T120（SENIOR ENGINEER 3）在飞改动引入，回退后消失**：

- 引入机制（SE3 报告）：`unfoldRegistered()` 在 **`unfold target=0 ret=0`（一条 fold 没展开）** 的情况下
  仍清空 registry → 同一 view 第二次 open 时文档仍带折叠 → `setText()` → 崩溃。
- 我的栈（`02-stack.txt`）与该机制一致：第二次 open 的 `setText` 之后（大文件 lane 的
  `yieldToEventLoop()` 让出时）katepart 绘制调用
  `Kate::TextFolding::foldingRangesStartingOnLine()` → 空指针（`si_code=1 SEGV_MAPERR, fault=0x0`）。
- 我的时间线（`03-attribution.md`）：14:27–14:50 已有**同栈** core（间歇），15:40 连跑 10/10 绿，
  18:56 大文件 lane 加让出后 19:19 变成 3/3 必崩；19:20 SE3 回退 → **5/5 绿 + ctest 里 search 测试通过**。

## 我在本任务加的反向控制（防复发断言）

`src/workspace/search/search_selftest.cpp` 的 `jump.product.second-open-paint-stable`：
在**同一 pane 第二次 open 之后**主动泵 5 轮 `processEvents` + `sendPostedEvents(Paint)`，
然后断言文档文本/光标/当前文件仍然一致。

- 没有这条断言时：崩溃发生在 paint 里 → 测试进程直接 SIGSEGV（红，但只有退出码，没有位置）；
- 有这条断言后：这条序列被显式覆盖，任何“第二次 open 留下不一致折叠树”的回归都会在这里红/崩。
- 当前：**106 checks / 0 failures**，3/3 连跑绿（`08-search-selftest.log`）。

### 还建议（属于折叠 lane，T120 所有者）

1. `unfoldRegistered()`：**registry 清空前必须验证目标行真的已展开**（`exec` 返回 false 或
   `still folded` 时不要清空/或重试）；这正是本次 bug 的触发点。
2. SE3 的反向控制数字（请记入 T120 证据）：**旧的 inner-first 顺序在 1000 行样本上模拟出 175 次
   “目标行已隐藏”**；外层优先 + 可见性守卫 + 光标恢复后为 **0**。→ 说明“看不见的 offscreen 测试”
   抓不到这类问题，必须靠守卫 + 真序列断言。

## 为什么 27/27（后来的 30/30）没抓到

1. **交叉序列缺口**：折叠 lane 的测试不打开第二个文件；search lane 的测试不关心折叠。
   两边各自绿 → 加总绿。崩溃需要“同一 view、已注入折叠、第二次 open、然后一次绘制”。
2. **paint 是关键路径但测试很少泵 paint**：大多数 lane 测试只 `processEvents` 少量次数；
   崩溃发生在真实重绘里，之前没有测试显式 `sendPostedEvents(Paint)`。
3. **时序敏感**：offscreen 下崩溃是间歇的（14:27–14:50 有、15:40 没有、18:56 后必现）；
   真机 xcb 的重绘时机不同，更容易踩到 —— 所以“本地全绿”不能证明真机安全。
4. **没有崩溃取证时**：ctest 只报 “Failed”，这次靠 T117 的崩溃报告器/core 才拿到栈。
