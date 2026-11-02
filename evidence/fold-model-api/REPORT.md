# T125 调研：KTextEditor 折叠模型 API 是否存在

collected_at: 2026-11-02T10:25:00+08:00
git_head: 见 `01-api-research.log` 的 C9 头
## 结论（一句话）

**不存在公开的折叠模型 API。** KF6 **6.24.0**（pinned）的 `KTextEditor` 公共头文件里
**没有** `Document::foldingModel()`、**没有** `FoldingModel` 类、**没有** `setFoldingRanges()` /
`addFoldingRegion()` / `removeFoldingRegion()`，也没有 `FoldingRange`/`FoldingRegion` 类型头文件。
→ 任务里的“优先方案”（`FoldingModel::setFoldingRanges` 一次批量下推）**在本版本无法实现**。

## 逐条回答

1. **`KTextEditor::Document::foldingModel()` 是否存在？**
   不存在。`grep -rn "foldingModel\|FoldingModel" /usr/include/KF6/KTextEditor/` 无任何匹配
   （Grep exit 1，见日志）。`document.h` 的公开模型只有 `annotationModel()`（`AnnotationModel`），
   没有任何折叠模型访问器。

2. **是否提供 `setFoldingRanges(...)` / `addFoldingRegion` / `removeFoldingRegion`？**
   没有。`grep -rn "setFoldingRanges\|addFoldingRegion\|removeFoldingRegion\|FoldingRange"`
   在 `/usr/include/KF6/KTextEditor/` 中无匹配。

3. **`FoldingRange`/`FoldingRegion` 字段？**
   KTextEditor 没有这个公共类型。`KSyntaxHighlighting/FoldingRegion` 只是**高亮器输入**
   （`Type::Start/End`、`FoldingRegion()`、`operator==`、`isValid()`），不是编辑器折叠模型；
   它由 `AbstractHighlighter::applyFolding()` 消费，不能用来给 KTextEditor 下推折叠区间。

4. **能否“批量原子、不动光标、无需 registry”？**
   不能。公共 API 关于折叠只有：
   - `View` 的折叠栏配置键（`folding-bar`、`folding-preview`、`folding-marker-color`，见
     `view.h:160/164/202`）；
   - `fold` / `tfold` / `unfold` **命令**（`KTextEditor::Command`）——也就是我们当前使用的
     `exec("fold")` 路径；
   - 只读观测：`firstDisplayedLine(RealLine/VisibleLine)`、`verticalScrollBar()`。
   库内部确有折叠实现（`KateBuffer::computeFoldings`、`KTextEditor::ViewPrivate::unfoldLine/
   clearFoldingState/applyFoldingState/toggleFoldingOfLine` 等 50 个符号），但全是**私有符号**，
   没有公共头文件与稳定 ABI，不能作为产品依赖。

## 备选方向（需要 captain 定，均未动手）

**A. 改用 KSH 原生折叠（推荐，若可接受产品口径变化）**
我们的 `resources/syntax/vedit-systemverilog.xml` 本来就带 `beginRegion/endRegion`；
KatePart 会通过 `KateBuffer::computeFoldings` 从高亮器计算折叠**标记**，用户可直接在 gutter
折叠/展开。那样可以删掉全部命令注入与 registry，三类崩溃与 66 秒一起消失。
代价：**打开时不再自动折叠**（`hidden-975` 变成“标记存在、用户按需折叠”的口径），
且需要先在真机/可驱动 gutter 的环境验证标记确实显示（无头无法点 gutter）。
需要 captain 确认是否接受这个验收口径变化。

**B. 保留自动折叠，但换掉危险路径（不改产品口径）**
- 只在可见窗口做命令注入（10k open 从 4000 次降到几十次，可 ≤2 s）；
- **不再调用 `unfoldRegistered()`**：同 pane 二次打开时，换成“重建 view/document”而不是
  “在带折叠的 document 上 setText + unfold”（这是 T117/T121 两次崩溃的共同触发点）；
- 需要改 kernel 的 open 路径（view 重建），比 A 大，但保留 hidden-975。
风险：仍是命令注入，与 KatePart 内部状态交互；需要真实二次打开回归。

**C. 依赖私有符号（KateBuffer/ViewPrivate）**：不建议——无公共头、无 ABI 保证。

## 本轮没有动代码

按 captain 要求“API 不存在时每个备选都要先报、不要自己选”，这里只交调研证据；
`evidence/fold-model-api/01-api-research.log` 是完整 grep/符号/版本输出（C9 头）。

---

## 决定性前提实验（T125 第二步）：NO injection, KTextEditor 自己的 tfold/fold

探针：`src/syntax/folding/tests/fold_native_probe.cpp`（临时工具目标，未进 ctest），
输出 `02-native-probe.log`。

**结果（一行）**：`tfold`/`fold` 在 module 行（cursor line 0）**都返回 0 且视图完全不变**
（first/last/hidden/scroll_max 全等；C++ 对照同样返回 0）。

**同时确认的两件事**：
1. KSH 本身**会**产出折叠区间：用 KDE 的 SystemVerilog 定义离线跑 `AbstractHighlighter::applyFolding`
   → `begin=75 end=75`（我们的 25-module 样本）。所以“没有折叠区间”不是原因。
2. **KTextEditor 实际加载的不是我们的 XML**：`KSyntaxHighlighting::Repository` 对
   `SystemVerilog` 解析到 **KDE 自带定义（author Sean O'Boyle）**，即使把我们的 XML 复制进私有
   XDG share 或 `addCustomSearchPath(resources/syntax)` 也一样（`Editor::repository()` 是 const，
   没有公共 API 往编辑器仓库注入自定义路径）。我们的 XML 只被我们自己的 `KsyntaxFoldSource` 使用。

**判定**：按 captain 的标准 = **“不能折”** → 前提不成立。
**但方法学 caveat（必须说清）**：`Command::exec(view, ..., Range::invalid())` 可能**不是**交互式
gutter 点击走的路径；KatePart 的收起动作可能只在 UI 动作里带 range。因此这条实验能证明
“命令 API 的 no-range 不能折”，**不能 100% 排除 GUI 标记存在**。要彻底定 A，需要在 xcb 下
真实点击 gutter 的测试（本机镜像有 Xvfb；需要额外时间与 captain 批准）。
