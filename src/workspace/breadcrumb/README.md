# 面包屑 lane —— Slice 2，任务 T19（AC-S2.2：“面包屑随语义走”）

面包屑 = 光标所在位置的**语义容器路径**（module › function › …），由解析器的
`structure` query 给出，不是文本启发式，也不是折叠 registry。

## 文件

| 文件 | 职责 |
|---|---|
| `breadcrumb_model.h/.cpp` | 路径模型：`setPath()` 接收语义路径、检测是否变化（视图据此决定是否重绘）、`display()`/`describe()` 渲染 |
| `breadcrumb_source.h` | 语义来源接口 `pathAt(text, line)` + `semanticActive()` |
| `syntax_breadcrumb_source.h/.cpp` | provider 来源：`SyntaxProvider::breadcrumbs(tree, byte_offset)`（`resources/queries/<grammar>/structure.scm`）；**同一文本只解析一次**（光标移动复用缓存），每次文本变化恰好重解析一次 |
| `tests/breadcrumb_tests.cpp` | 验收测试：模型 + 真实 tree-sitter 结构 query |

## 语义从哪来（不是“看起来像”）

- 结构 query 捕获 `@module` / `@function` / `@task` / `@interface` / `@class` /
  `@package` / `@program`，`@name` 是该声明的标识符文本；provider 的 `breadcrumbs()`
  返回“包含该 offset 的所有结构节点，外→内”。
- 因此光标在 `module m0` 的函数体里得到 `module:m0 > function:inc`；在 `always_comb`
  里只有 `module:m0`（always 不是结构节点）；在最后一个 `endmodule` 之后的注释行得到空路径
  （面包屑栏隐藏）。
- tree-sitter 运行期开关关闭时 `semanticActive()` 为 false、路径为空：**不退回文本猜测**。

## 与折叠的关系

本 lane **不依赖** `src/syntax/folding`（`CMakeLists.txt` 里没有该链接，也没有任何 include）。
光标移动/语义更新只会更新面包屑，不可能触发 AMENDMENTS A2.1 所说的折叠全量重建。

## 构建与测试（独立 lane）

```bash
cmake -S src/workspace/breadcrumb -B target/ws-breadcrumb -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/ws-breadcrumb --parallel
ctest --test-dir target/ws-breadcrumb -R breadcrumb --output-on-failure
```

不需要显示服务器（`QCoreApplication` + tree-sitter）。

## 边界

本 lane 尚未链入 `vedit-app`：把模型接到活动编辑器的光标/重绘属于 `src/editor` 的集成工作，
不在冻结契约 `folding-breadcrumbs` 的写域内。验收证据是 lane 级：真实 provider 的结构 query
与写定的期望路径（模块/函数行号由样本文本构造给出）。
