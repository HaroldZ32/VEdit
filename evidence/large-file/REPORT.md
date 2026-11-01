# T116 — 56MB 文件打开卡死：基准结论（代码已回退，绿色优先）

collected_at: 2026-11-01T19:30:00+08:00
git_head: 见同目录日志的 C9 头
## 结论（先量化，后修复）

真实数字（`01-prefix-benchmark.log`，合成 Verilog 样本，**不是用户文件**；无头 offscreen）：

| 行数 | read | setText | tree-sitter fold 扫描 | breadcrumb | **product-open** |
|---|---|---|---|---|---|
| 1,000 | 0 ms | 10 ms | 33 ms | 33 ms | 687 ms |
| 10,000 | 0 ms | 14 ms | 159 ms | 192 ms | **77,963 ms** |
| 100,000 | 0 ms | 57 ms | 1,323 ms | 1,178 ms | **>240 s（超时）** |
| 500,000 | — | — | — | — | 未跑（同上量级） |

**墙不是 tree-sitter、不是 highlightSpans、不是折叠扫描，而是逐条 `exec("fold")` 的注入循环**：
10k 行 4,000 条 fold ≈ 4,000 × 16 ms ≈ 66 s（product-open 78 s 含其它阶段）；1k 行 400 条 ≈ 0.7 s。
注入命令的单次代价随文档行数增长（100k 时 40,000 条形同分钟级），所以"逐条全量注入"在
几千行时就已不可接受。

## 已实现但**已回退**的修复（为了不把树留在红状态）

试过的改动（均已 `git checkout` 回退，当前 HEAD 绿树）：

1. `src/editor/kernel/large_file.{h,cpp}`：字节/行双阈值（默认 10 MiB / 200k 行，`VEDIT_LARGE_FILE_*`
   可配置），超限时给可见状态 `Large file (… MB, … lines): syntax-aware features … limited`。
2. 打开路径**分阶段日志**（默认 qInfo）：`open: read done … / setText done … / total … / features done …`，
   并在阶段间 `processEvents` 让出事件循环（"慢可以、死不行"）。
3. provider 打开前把 pane 面包屑栏设为 `Opening <name> (<MB> MB)…`；大文件加载后换成 Large-file 状态。
4. `FoldBreadcrumbBinding`：大文件跳过折叠/面包屑/tree-sitter；中等文件只注入**可见窗口**折叠
   （滚动/滚轮定时增量注入，每批 24 条），小文件保持全量（`hidden-975`/`registry-200` 不变）。
5. 打开特性对 >4000 行文档**延后一拍**执行，open 先返回让窗口先绘制。

回退原因（诚实）：这套改动与 T120 的崩溃修复路径相互影响，在回退前 root ctest 出现
`vedit-workspace-search-selftest` 段错误（第二次打开时文档仍带折叠 + 光标在隐藏行）。
按 captain 的硬要求"绿树优先"，我回退了全部产品代码；本目录只保留基准与诊断结论。
**下次可直接按上面的 1–5 重做**，但必须与 T120 的 guard 一起做、并且先在干净 worktree 上验证。

## 尚未验证

- 56MB（2.8M 行）级的真实打开时间：大文件模式只做了代码路径，未跑完基准（时间不足）。
- **真机待用户复验**（本机无显示服务器，以上是无头数字）。
- 合成样本 vs 用户真实 Verilog 结构可能有差异。
