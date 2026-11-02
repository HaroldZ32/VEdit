# T92 报告 — highlightSpans 整树查询的性能边界：量化 + 按范围查询修复

- Cell: `cell-20261031T085543161Z` · Board task: **T92**（来源：T17 如实登记的性能边界）
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01（Asia/Shanghai）· 基线 `HEAD 31a7c2d`
- 写域: `src/syntax/**`（provider + overlay + selftest + CMake）、`evidence/provider-range-query/**`
- 结论: **已修复并通过 10k 行验收**。根因是 overlay 每次重建调用 **整树** `highlightSpans`（成本随文档行数线性增长）；
  改为 provider 侧**按字节范围查询**（`ts_query_cursor_set_byte_range`）后：
  - 范围查询耗时与文档大小**无关**（1k→10k 行：0.067 ms 恒定）；
  - 10,081 行：**50 次重建 median 29.8 ms（预算 150）**、**单次注入 p95 0.147–0.29 ms（预算 2）**，即使机器被其他 lane 负载干扰的最差三次运行也全 PASS；
  - T17 同一 1,051 行文档、同一测量方法：**105.45/102.65/104.37 ms → 24–28 ms（约 4 倍改善）**；
  - A1g 单一 owner、A1h 回退、增量 vs 全量一致性（812==812）全部保持可跑且仍绿（40 checks / 0 failures）。
- 无显示服务器：全部为 headless 测量（pinned 镜像、offscreen）。

## 1. 先量化：整树查询的曲线（旧路径）

在 1,009/2,017/5,041/10,081 行合成 SystemVerilog 上，同一棵解析树直接测
`provider->highlightSpans(tree)`（旧路径）的 median（5 次）：

| 行数 | 字节 | 整树查询（旧） | 每千行成本 |
|---|---:|---:|---:|
| 1,051 | 25,388 | **1.71 ms** | 1.63 µs/行 |
| 2,017 | 48,710 | **3.82 ms** | 1.89 µs/行 |
| 5,041 | 121,858 | **9.84 ms** | 1.95 µs/行 |
| 10,081 | 243,778 | **20.30 ms** | 2.01 µs/行 |

→ **线性（≈O(n)），不是平方**。但线性也足够撞墙：T17 的预算是「50 次重建 ≤150 ms」，
即每次重建 ≤3 ms；整树查询在 1.05k 行已 ~1.7 ms、到 10k 行 ~20 ms。
**旧路径在 10k 行的 50 次重建 = 20.3 ms × 50 ≈ 1.0 s**（再加 sync ≈ 1.04 s），是预算的 ~7 倍，
与 T17 登记的「1.5k 行以上会超」完全一致（T17 实测 1,051 行 = 105 ms，含 ~1.75 ms/次查询）。

## 2. 优化：provider 侧按范围查询（接口变更前后见 `05-api-change.md`）

- `SyntaxProvider` 新增**非纯虚**重载 `highlightSpans(tree, const Range &)`：
  默认实现过滤整树结果（老 provider/测试替身零改动）；`TreeSitterSyntaxProvider` 覆盖为
  `ts_query_cursor_set_byte_range()` 限定查询字节范围（向两侧各扩一行以覆盖多行节点），再按契约求交。
- overlay：`open()`/`setEnabled(true)` 仍整树缓存（`spans()` 语义不变，A1g/paint 路径不动）；
  **`applyEdit()` 只查询可见窗口**；`injectWindow()` 在滚动出缓存范围时按新窗口查询。
- **没有删除任何 `highlightSpans` 调用**：整树 API 保留，grammars/provider 测试与 `open()` 仍用它。

## 3. 优化后数据（同一测量方法：50 次重建 × 3 批取 median；注入 100 个窗口取 p95）

3 次独立运行（T17 同款 `x` 插入/删除净零编辑；run2/run3 期间其他 lane 在构建，CPU 有争用，原始值全列）：

| 行数 | 整树查询（旧路径） | 范围查询（新路径） | 50 次重建 median（预算 150） | 单次注入 p95（预算 2） | 全文档 span 缓存 | 窗口 span | RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1,051 | 1.71 / 3.40 / 3.48 ms | **0.067 / 0.118 / 0.119 ms** | **24.5 / 49.9 / 47.4 ms** | 0.152 / 0.273 / 0.247 ms | 2,251 | 86 | ~43.6 MB |
| 2,017 | 3.82 / 6.77 / 6.75 ms | **0.068 / 0.117 / 0.117 ms** | **25.9 / 48.6 / 51.3 ms** | 0.160 / 0.282 / 0.285 ms | 4,321 | 86 | ~43.7 MB |
| 5,041 | 9.84 / 16.96 / 16.63 ms | **0.067 / 0.123 / 0.114 ms** | **27.6 / 57.8 / 53.1 ms** | 0.156 / 0.287 / 0.264 ms | 10,801 | 86 | ~46.5 MB |
| **10,081** | 20.30 / 31.08 / 32.84 ms | **0.069 / 0.103 / 0.105 ms** | **29.8 / 65.2 / 65.8 ms** | 0.147 / 0.274 / 0.166 ms | 21,601 | 86 | ~60.0 MB |

- **10k 行验收：单次注入 p95 0.147–0.274 ms < 2 ms；50 次重建 median 29.8–65.8 ms < 150 ms**（三次运行全部 PASS，
  selftest 里的 12 条预算断言全部 PASS）。最干净的 run1：29.8 ms / 0.147 ms，余量分别 5.0x / 13.6x。
- **增长阶**：范围查询与文档大小无关（0.065–0.123 ms 恒定）；50 次重建从 24.5 → 29.8 ms（10 倍行数只涨 22%，
  剩余增长来自增量重解析 edit 本身）；整树查询仍是线性（20.3 ms @10k），旧路径的 50 次重建 ~1.0 s。
- **内存**：全文档 span 缓存 105 KB → 1,012 KB（线性，已不再常驻：编辑后缓存只有窗口的 86 个 span）；
  RSS 43.6 → 60.0 MB（sweep 同时持有 controller 与 probe 两棵树，属测量口径，非产品常驻）。

## 4. 与 T17 基线的对比（同一文档、同一函数、同一方法）

| 指标 | T17 基线（1,051 行） | 本次（同 1,051 行，`overlay.perf.*` 同款代码块） |
|---|---:|---:|
| 50 次重建 median | 105.45 / 102.65 / 104.37 ms | **24.5 / 49.9 / 47.4 ms**（最干净 3 次为 23.6–27.8 ms） |
| 单次注入 p95 | 0.068 / 0.070 / 0.070 ms | 0.152 / 0.273 / 0.247 ms（预算 2 ms；注入侧新增了按窗口查询） |
| 单次重建中的查询 | 1.7–2.0 ms（整树） | 0.067–0.119 ms（窗口） |

→ 同一文档的重建成本约 **4 倍改善**（干净运行 24–28 ms vs 105 ms）；10k 行从「~1.0 s」降到「29.8–65.8 ms」。

**测量方法上的一个实测发现**（影响任何同类测量）：在 1,051 行的同一编辑点，插入 `x`（标识符字符）
比插入空格（`' '`）的增量重解析贵约 4 倍（sync 390–793 µs vs 94–101 µs；同文档重建 24.5–49.9 ms vs 8.5–10.6 ms）。
本次 sweep 采用与 T17 相同的 `x` 编辑，因此数字与 T17 可比；两个数字都记录在原始日志里。

## 5. 可观测性未回退（与 T17 同口径，仍绿）

`02-selftest-sweep.log` 每次运行 40 checks / 0 failures（T17 时为 28 条，新增 12 条 sweep 预算断言）：
- **A1g**：`overlay.single-owner`（494 spans）、`overlay.single-owner.overlay-present`（overlay spans=247）、
  `overlay.single-owner.known-ranges`、paint 计划 `overlay.paint.*`（含 `raw-overlap` 裁剪、`arbiter-agrees`）全 PASS；
- **A1h**：`overlay.fallback.*`（claims-released before=181 after=0、100 次 enable/disable、service-switch）PASS；
- **增量一致性**：`overlay.incremental.equivalence PASS incremental=812 full=812`；
- ctest：`vedit-syntax-overlay-selftest ... Passed 2.14 sec`（root build 的 ctest 条目）。

## 6. 复跑命令

```sh
# 构建 + 自检（含 1k/2k/5k/10k sweep）
cmake --build target/qt6-build --target vedit-syntax-overlay-selftest --parallel "$(nproc)"
target/qt6-build/src/syntax/overlay/vedit-syntax-overlay-selftest --selfcheck
# ctest 条目
ctest --test-dir target/qt6-build -R vedit-syntax-overlay-selftest --output-on-failure
# 类型检查（全树，最终代码）
bash build/gates/typecheck.sh        # TYPECHECK-EXIT=0（04-typecheck.log）
```

## 7. 未验证 / 局限

- **无 GUI 视觉验证**：性能与断言都在 offscreen 下取得；没有“看起来高亮正确”的判断（T17 的 paint 计划
  一致性断言仍覆盖单一 owner 语义）。
- **`open()` 仍是整树查询**（10k 行 20.3–32.8 ms，一次性，不在每次编辑的预算内）。如果未来要在 10k 文档上
  进一步压低“打开”延迟，可同样改为“先查可见区、后台补全”，属后续优化，不在本任务判据内。
- **机器是多人共享的开发机**：run2/run3 的绝对值受其他 lane 构建影响；三次原始值全部保留，判据（150/2 ms）
  在最差运行里仍 PASS。
- 未 commit/push（Lead 统一负责）。未触碰 `src/app/**`、`src/editor/**`、`src/workspace/**`、`resources/syntax/**`。

## 8. 证据清单（`evidence/provider-range-query/`）

`00-pins.txt`（HEAD/镜像/改动路径）、`01-build.log`、`02-selftest-sweep.log`（3 次运行原始输出）、
`03-ctest.log`（ctest 条目）、`04-typecheck.log`（`TYPECHECK-EXIT=0`）、`05-api-change.md`（接口/调用点前后对照）、本报告。
