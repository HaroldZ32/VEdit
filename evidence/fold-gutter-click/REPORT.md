# T127 — xcb 真点击 gutter：KDE 原生折叠标记存在且双向可用

collected_at: 2026-11-02T10:32:00+08:00
git_head: 见 `01-xcb-click.log` 的 C9 头
## 结论（一句话）

**有标记 + 两个方向都点得动** → 走 **A**：删掉我们整套命令注入层，改用 KTextEditor/KSH 原生折叠。
“叠不回去”的真因是**我们的注入与原生折叠互相打架**，不是 KatePart 没有折叠。

## 实验条件（严格按 T127）

- **Xvfb + xcb 真图形路径**（`xvfb-run -a -s "-screen 0 1280x1024x24"`，`QT_QPA_PLATFORM=xcb`），不是 offscreen。
- **一行 `FoldInjector` 都没加载**；只创建 `KTextEditor::Document/View`，设置 `setHighlightingMode("SystemVerilog")`
  并打开 `folding-bar`/`folding-preview` 配置。
- 25 个 module 的合成 SystemVerilog 文本；真实 `QMouseEvent`（press+release）发给 `QApplication::widgetAt(global)` 命中的 widget。
- 截图：`gutter.png`（1000×900）；局部放大 `gutter-crop.png`；像素图见下。

## 结果

```
baseline            first=0 last=61 hidden=0  scroll_max=939
marker_found        x=44 (相对 view 左边缘；view 全局原点 + 44)
after_first_click   first=0 last=61 hidden=38 scroll_max=901   changed=1   ← 点一下：收起来了
after_second_click  first=0 last=61 hidden=0  scroll_max=939                ← 再点：展开回来
negative_control    line 1（非 region 起始行，同一列）changed=0            ← 反向控制：不会误折
verdict             marker_created_by_kde=1 fold_direction=1 unfold_direction=1
```

- 折叠标记位于左侧 gutter（文本区 `text_x=60`，marker 命中点 x=44；像素探测在 x≈30–34 处有标记字形，行高 14px）。
- 双向点验：`hidden 0→38→0`，`scroll_max 939→901→939`，**两个方向都真的发生了**。
- 非 region 行同列点击不改变视图（负控制通过）。

## 判定

| 条件 | 结果 |
|---|---|
| 有标记 | ✅（KDE 定义产出折叠区间，gutter 有字形，点击生效） |
| 点一下收起 | ✅ `hidden 0→38`、`scroll_max 939→901` |
| 再点展开 | ✅ `hidden 38→0`、`scroll_max →939` |
| 非 region 行负控制 | ✅ 无变化 |

→ **走 A**：删除 `FoldInjector` 命令注入 / registry / `unfoldRegistered()` / 光标移动逻辑；
验收从“注入 200 区间、hidden-975”改为**真交互判据**（点 marker 两个方向都改变视图）。
注意：native folding 由 **KDE 自带的 SystemVerilog 定义**提供（见下条事实），不是我们的 XML。

## 必须记住的事实（也写入了 AGENT.md）

> **KTextEditor 的 Repository 不认我们随包的两份 XML**（`SystemVerilog` 解析到 KDE 自带定义，
> author `Sean O'Boyle (seanoboyle@intelligentdv.com)`）；`addCustomSearchPath` 与私有 XDG 目录
> 都改不了这一点（`KTextEditor::Editor::repository()` 是 const，没有公共注入路径）。
> **我们的 XML 只被自己的 `KsyntaxFoldSource` 使用** —— 这解释了“高亮是好的”（用的是 KDE 定义），
> 而我们自己的 XML 只影响我们自己的折叠扫描。

## 证据文件

- `01-xcb-click.log`（C9 头，探针完整输出）
- `gutter.png`（原始截图）、`gutter-crop.png`（左边缘放大）
- 探针源码：`src/syntax/folding/tests/fold_native_gui_probe.cpp`（测试工具目标，未进 ctest）
