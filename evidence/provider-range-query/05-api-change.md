# T92 接口变更前后对照（SyntaxProvider 按范围查询）

## 变更前（T17 基线）

`src/syntax/provider/syntax_provider.h`：

```cpp
// 唯一的整树查询：调用方拿到全文档的 span 列表
virtual std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) = 0;
```

- overlay 每次重建（`OverlayController::applyEdit` → `refreshSpans()`）调用它，拿全文档 spans 再从中挑可见窗口；
- tree-sitter 实现 `TreeSitterSyntaxProvider::highlightSpans(tree)` 用 `ts_query_cursor_exec(整棵树)` +
  全量收集 + 排序，成本随文档行数增长（T17 实测 1,051 行时 `query_us≈1.7 ms/次重建`，占预算 90%）。

## 变更后（T92）

同一个头文件**新增一个非纯虚重载**（原来那个整树方法**保持签名与语义不变**）：

```cpp
// 整树查询（不变）
virtual std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree) = 0;

// 新增：只返回与 range 相交的 spans（默认实现=过滤整树结果，老 provider 零改动）
virtual std::vector<HighlightSpan> highlightSpans(const ParseTreePtr &tree,
                                                  const Range &range);
```

实现分两层：

1. **默认实现** `src/syntax/provider/syntax_provider.cpp`（新文件）：调用整树方法再按
   `span.end_byte > range.start_byte && span.start_byte < range.end_byte` 过滤。
   任何只实现了整树方法的 provider / 测试替身**不需要改动**。
2. **tree-sitter 覆盖** `TreeSitterSyntaxProvider::highlightSpans(tree, range)`：
   - 把字节范围向两侧各扩一行（多行高亮节点可能从窗口前一行开始），
   - `ts_query_cursor_set_byte_range(cursor, scan_start, scan_end)` 后再 `exec`，
     tree-sitter 直接剪枝，成本只与范围相关；
   - 结果再按 `range` 精确求交（契约仍是“相交”），排序与整树方法一致。

`Range` 复用了已有的 `vedit::syntax::Range`（`start_byte/end_byte` 有效；行号字段在范围查询里不使用）。

## 调用点清单（改前 → 改后）

| 调用点 | 改前 | 改后 |
|---|---|---|
| `src/syntax/overlay/overlay.cpp` `open()` / `setEnabled(true)` | `highlightSpans(tree)`（全文档，保持 `spans()` 的整树语义） | `highlightSpans(tree, {0, size})`（**仍走整树**，行为等价） |
| `src/syntax/overlay/overlay.cpp` `applyEdit()` | `refreshSpans()` → 整树 | `refreshSpans(window_start, window_end)` → **按窗口范围查询** |
| `src/syntax/overlay/overlay.cpp` `injectWindow()` | 只用缓存过滤 | 若窗口超出缓存范围（滚动），**按新窗口范围查询** |
| `src/syntax/grammars/tests/test_syn_grammars.cpp:295,384,408` | `highlightSpans(tree)` | 不变（整树 API 保留） |
| `src/syntax/provider/tests/test_syntax_provider.cpp:168,247` | `highlightSpans(tree)` / 禁用 provider | 不变 |
| `src/syntax/overlay/overlay_selftest.cpp`（T92 新增 sweep） | — | 两种：整树（旧路径成本）与 `(tree, window)`（新路径成本），同一棵树上对比 |
| `src/syntax/provider/disabled_provider.cpp` | 只实现整树方法（返回空） | 不变（默认实现过滤空结果） |

**没有删除任何 `highlightSpans` 调用**：整树方法仍是 `open()` 的缓存来源、仍是 provider/grammars 测试的
断言对象，T17 的 `buildPaintPlan(base, controller.spans(), ...)` 路径不受影响（`spans()` 语义：
open 后=全文档，编辑/滚动后=最近查询的窗口，见 `overlay.h` 的注释）。

## 影响面

- 行为：overlay 的 claim 集合不变（A1g 单 owner 检查、A1h 回退、增量全量一致性 812==812 全部仍绿）。
- 内存：`spans` 缓存从“全文档”降为“最近查询的窗口”（10k 行时 21,601 个 span → 窗口 86 个），
  全文档 span 缓存内存（`span_cache_kb`）不再常驻；`open()` 仍会短暂构建全文档缓存。
- 新增 TU：`src/syntax/provider/syntax_provider.cpp`（默认实现），已加入
  `src/syntax/provider/CMakeLists.txt` 的 `VEDIT_SYNTAX_PROVIDER_SOURCES`。
- 未改 `src/app/**`、`src/editor/**`、`src/workspace/**`、`resources/syntax/**`。
