# 探针：slang 能不能支撑**跨文件 rename**

问题：当前 Rename 只能用 verible 的 LSP（文件作用域，`NOT-CLAIMED` 跨文件）。
本探针回答：换成 slang 侧做，可行吗、缺什么。

## 结论（一句话）

**可以**，而且不需要新增 C++ 集成：用我们**已经在用**的
`slang --ast-json --ast-json-source-info` 转储，里面每个**引用点**都带
已解析的符号身份（`symbol`）与**起止行列**，每个**声明点**带 `addr` 与自己的位置；
`symbol` 的数字部分就是声明点的 `addr`。所以「跨文件 rename」= 一次转储里
按 `addr` 聚合所有 `symbol == "<addr> <name>"` 的节点 + 那个声明节点，逐个改。

## 实测

```
slang version 12.1.0+0
slang --ast-json ast.json --ast-json-source-info --top top top.sv leaf.sv
-> Build succeeded: 0 errors, 0 warnings      (exit 0)
```

两份 fixture（同目录 `top.sv` / `leaf.sv`）：`top` 把 3 个信号经端口连接接到 `leaf`。

**引用点**（全部 `kind=NamedValue`，带 `symbol` + `source_file_start/line/column_start/end`）：

```
symbol=2365457582792 clk    leaf.sv 2:23-2:26      <- always_ff @(posedge clk)
symbol=2365457583352 q      leaf.sv 2:28-2:29      <- q <= d
symbol=2365457583072 d      leaf.sv 2:33-2:34      <- q <= d
symbol=2365457581216 clk    top.sv  4:20-4:23      <- .clk(clk)
symbol=2365457581352 d      top.sv  4:29-4:30      <- .d(d)
symbol=2365457581488 q      top.sv  4:36-4:37      <- .q(q)
```

**声明点**（`kind=Variable`/`Port`，带 `addr` 与 `source_line/source_column`）：

```
{"name":"clk","kind":"Variable","source_line":2,"source_column":9,"addr":2365457581216,...}   <- top.sv
{"name":"d",  "kind":"Variable","source_line":3,"source_column":15,"addr":2365457581352,...}
{"name":"q",  "kind":"Variable","addr":2365457581488,...}
```

**链接成立**：声明 `clk` 的 `addr=2365457581216`，其三个引用点的 `symbol` 均为
`"2365457581216 clk"`。声明↔引用、跨文件，全部可枚举。

注意：`source_file`/`source_line`/`source_column`（声明）与
`source_file_start`/`source_line_start`/`source_column_start`（引用）是**两套字段名**，
解析时别搞混；`symbol`/`addr` 是**本次进程内的指针**，不能持久化，必须同一次转储内用完。

## 缺什么 / 边界

1. **必须能 elaborate**：需要 `--top` 与完整文件集（缺 `leaf.sv` 时实测得到
   `UninstantiatedDef` 且 `Build failed: 1 error`，没有任何 AST）。项目有语法/展开错误时
   拿不到转储 → 这类情形只能退回 verible 的文件内 rename。
2. **穿过端口边界需要额外决策**：`top` 的 `d` 与 `leaf` 的端口 `d` 是**两个不同符号**
   （addr 不同），只由连接表达式相连。按「只改同名符号」实现时，改 `top.d` 会改到
   `.d(d)` 里的那个 `d`，但**不会改到 `leaf` 的端口声明与内部使用**。要支持「改端口并联动
   子模块」得再走一步：从连接点找到被连的 `Port` 再继续。这是产品语义选择，不是技术障碍。
3. **规模**：转储是整设计的 JSON，大工程会到几十 MB 级；我们已经在 project-lint/模块目录里
   跑同一条命令（30 s 超时），rename 可复用同一路径，但需要缓存/复用最近一次转储。
4. **工作量**：新的 ABI（`vd_rename_query` 一类）+ 结果应用（跨文件多编辑、原子保存）+
   冲突/撤销 + 测试，属**一个独立批次**（天级），不是配置开关。

## 建议的产品切分

- 能 elaborate → 走 slang 的符号级跨文件 rename（本探针已证明可行）；
- 不能 elaborate（半成品代码）→ 退回 verible 的文件内 rename，并在 UI 上说明原因。
两者互补，正好覆盖「打开的文件」与「整个设计」两种场景。
