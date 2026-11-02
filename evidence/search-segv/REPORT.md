# T121 报告 — search selftest SIGSEGV：已确认由 T120 在飞改动引入，回退后消失；栈/时间线/盲区/防复发

- Cell: `cell-20261031T085543161Z` · Board task: **T121** · 日期: 2026-11-01
- 结论一句话：**还在崩 → 已消失**。`vedit-workspace-search-selftest` 在 19:19 **3/3 必崩**
  （exit 139），19:20 SE3 回退其 `unfoldRegistered()` 在飞改动后 **5/5 + 3/3 绿**；
  根因与 SE3 报告一致：**unfold 未生效（`unfold target=0 ret=0`）却清空 registry → 同一 view 第二次 open
  时文档仍带折叠 → `setText()` → katepart 绘制里 `Kate::TextFolding::foldingRangesStartingOnLine()` 空指针**。
  栈与时间线见 `02-stack.txt` / `03-attribution.md`。

## 1. 现场与栈（`01-repro-run.log` / `02-stack.txt`）

```
#2 Kate::TextFolding::foldingRangesStartingOnLine(int)   libKF6TextEditor + 0xc5bc8
#4 QWidget::event                                          paint
#9 QWidgetRepaintManager::paintAndFlush()
#19 QCoreApplication::processEvents()
#21 vedit-workspace-search-selftest + 0xede80 → loadFileIntoDocument  document_io.cpp:92
signal=11 (SIGSEGV)  si_code=1 SEGV_MAPERR  fault_address=0x0
```
崩溃发生在**第二次 open**（同一 pane）之后、`setText` 让出事件循环的那次重绘里；
最后一条 CHECK 是 `jump.product.first-status`，随后 `open: read done … setText done lines=6`。

## 2. 时间线 / 归因（`03-attribution.md`）

| 时间 | 事件 |
|---|---|
| 14:04 | T94 折叠接线进产品 open 路径（此后 open 才注入 fold） |
| 14:27–14:50 | 已有**同栈** search-selftest core（间歇） |
| 15:40 | 我原任务里连跑 10/10 全绿（绘制时机没踩到） |
| 18:56 | 大文件 lane 在 `document_io.cpp` 加 `yieldToEventLoop()`（未提交）→ 每次 open 都派发一次绘制 |
| 19:19 | **3/3 必崩**（T121 现场） |
| 19:20 | SE3 回退其 `unfoldRegistered()` 改动 → **5/5 绿**，随后 ctest search 通过 |

→ **是 T120 在飞改动引入的（SE3 本人确认）**，大文件 lane 的让出把它从“间歇”放大成“必现”。
不是 search lane 的逻辑错，也没有改 search 的搜索/跳转代码。

## 3. 我加的防复发断言（`07-prevention.md` / `10-search-selftest.log`）

`src/workspace/search/search_selftest.cpp` 新增 `jump.product.second-open-paint-stable`：
第二次 open 之后主动泵 5 轮 `processEvents` + `sendPostedEvents(Paint)`，断言文档/光标/当前文件仍一致。
当前 search selftest **106 checks / 0 failures**（3/3 绿）。折叠 lane 侧的根治建议（registry 清空前验证 unfold
真的生效）与 SE3 的反向控制数字（旧 inner-first 顺序 175 次“目标行已隐藏” vs 外层优先+守卫 0 次）
一并写进 `07-prevention.md`。

## 4. 盲区（`08-blind-spots.md`）

折叠测试不开第二个文件；search 测试虽做第二次 open，但从不显式泵 paint、也不断言折叠树状态；
各 lane 单独绿 → 加总绿；offscreen 重绘少 → 间歇；真机 xcb 重绘多 → 必现；崩溃无栈时无法归因（T117 的取证
这次发挥了作用）。建议把“打开→注入折叠→第二次打开→泵 paint”作为固定 ctest 序列（本任务已加断言）。

## 5. 门禁（最终树）

- 根 ctest：**31/31 passed，exit 0**（`06-post-revert-ctest.log`）。
- `bash build/gates/typecheck.sh`：**PASS，exit_code 0**（`09-typecheck.log`）。
- search selftest：**106 checks / 0 failures**（`10-search-selftest.log`）。
- 证据全部带 C9 头（collected_at + HEAD + branch + porcelain）。

## 6. 范围

改动：`src/workspace/search/search_selftest.cpp`（+1 条 paint 稳定性断言）、`evidence/search-segv/**`。
未改 `src/editor/kernel/**`、`src/syntax/folding/**`；未 commit/push、未打包。
