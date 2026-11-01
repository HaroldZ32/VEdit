# 折叠注入 lane —— Slice 2，任务 T19（AC-S2.2 / AMENDMENTS A1–A3）

应用侧的折叠注入器：把 **syntax stack 的 tree-sitter fold ranges** 通过 KTextEditor
唯一的公开折叠入口（`KTextEditor::Editor` 的 `fold` / `unfold` 命令）注入真实编辑视图。

KTextEditor 6.24.0 没有公开的折叠模型（`evidence/r2-fold/decision.md` §2.1：公共头文件里
零折叠类；折叠实现是 KatePart 私有的 `Kate::TextFolding`），公共 API 只能配置折叠栏。
`Command::exec(view, "fold", msg, range)` 接受调用方给出的 range，因此“精确折叠”只能以
**注入**方式叠加，不能查询/按 range 删除。本 lane 就是这一层注入 + 校验 + 计数 + 观测。

## 文件

| 文件 | 职责 |
|---|---|
| `fold_range.h/.cpp` | 折叠区间模型（整行、0-based）与 **A1.1 四项自验**：非空/非默认、`start<=end`、两行都在文档内、至少隐藏一行；拒绝原因可诊断 |
| `fold_injector.h/.cpp` | 注入器：校验后才 `exec("fold")`；被拒 range 不写 registry、不计数，并记录“原因+原始 range+当时选区”；`folding.rebuild.full_count` / `folding.inject.count`；只允许三种触发做全量重建；按键/光标/滚动路径显式不重建 |
| `fold_source.h/.cpp` | 折叠来源接口 + 固定列表来源（单元测试与 A1f 的“关闭态”来源） |
| `syntax_fold_source.h/.cpp` | tree-sitter 来源：`SyntaxProvider::foldRanges()`（`resources/queries/<grammar>/folds.scm`）；运行期开关关闭（或 provider 不可用）时产 0 条 —— A1f 的 on/off 两态 |
| `ksyntax_fold_source.h/.cpp` | **关闭态来源（A4 残项，T29）**：直接读取自有 `resources/syntax/vedit-systemverilog.xml` 的 `beginRegion="veditFold"` / `endRegion="veditFold"` 关键字表（不查编辑器 repository，因此不受 KDE 内嵌 451 份定义影响），按关键字栈产整行折叠区间；`treeSitterActive()` 恒为 false |
| `tests/folding_tests.cpp` | 验收测试（真实 `KTextEditor::View` + 真实 tree-sitter provider） |

## A1 / A2 / A3 落点

- **A1（range 自验）**：`FoldInjector::inject()` 先 `validateFoldRanges()`，被拒的 range
  永远不会到达 `Command::exec`。`exec()` 的返回值**只记录、不判定**（A1.2）——
  测试里的负向对照实测：`Range::invalid()` 会回落到用户选区并折叠它；倒置 range 返回
  `true` 且仍然隐藏了两行之间的内容。所以“命令说成功”不能当作折叠发生的证据。
- **A2（重建时机/计数/性能）**：`RebuildTrigger` 只有 `FirstOpen` / `TreeSitterToggled` /
  `Reparsed`；`rebuild()` 才 +1 `full_count`。`onKeyPress()` / `onCursorMoved()` /
  `onScroll()` 只记事件数，**永不**重建。实测（本机 pinned 镜像，见 `evidence/slice2-fold/`）：
  200 条折叠首次/重建 87–93 ms（判据 ≤150 ms，判据是 50 条）；单次注入 p95 ≈ 0.52–0.63 ms
  （判据 ≤2 ms）；200 次按键 + 50 次光标移动后 `full_count` 增量 0。
- **A3（观测方法）**：`observeView()` 只用 `firstDisplayedLine(RealLine|VisibleLine)` 与
  `verticalScrollBar()->maximum()`（`hiddenInViewport` 由二者差值给出）；测试的“期望值”来自
  样本文本的构造（25×40 行、每模块折叠隐藏 39 行），**不来自 registry**。
  `scroll_max` 是“内容行数 − 视口行数”，所以其落差在基准上限处饱和：期望落差 =
  `min(隐藏行数, 基准 scroll_max)`；精确隐藏行数由 `firstDisplayedLine` 差值给出。

## 构建与测试（独立 lane）

```bash
cmake -S src/syntax/folding -B target/syn-folding -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/syn-folding --parallel
ctest --test-dir target/syn-folding -R vedit-folding-tests --output-on-failure
```

`QT_QPA_PLATFORM=offscreen` 即可（ctest 已设置）；不需要显示服务器。

## 接缝与边界

- 注入器**不拥有** `KTextEditor::View`：调用方（编辑内核 `src/editor`）拥有视图并在
  “首次打开 / tree-sitter 开关切换 / 重解析完成”时调用 `rebuildFrom(source, trigger)`，
  在按键/光标路径调用 `onKeyPress()` / `onCursorMoved()`。头文件即接缝。
- 本 lane 尚未链入 `vedit-app`：把注入器接到活动编辑器视图属于 `src/editor` 的集成工作，
  不在冻结契约 `folding-breadcrumbs` 的写域内（写域只有 `src/syntax/folding/`、
  `src/workspace/breadcrumb/`、`evidence/slice2-fold/`）。验收证据因此是 **lane 级**：
  真实 KTextEditor 视图 + 真实 tree-sitter provider，而不是整机点击。
- A4 残项（`.sv` 关闭态由自有 XML 提供 ≥1 折叠）已由 `a4-fold-residual`（T29）补上：
  `ksyntax_fold_source` 读取我们自己的 XML 规则，1k 行样本在 tree-sitter 关闭态下由真实视图观测到
  975 条隐藏行（期望值来自样本文本构造，不来自 registry）；证据在 `evidence/a4-fold/`。
- 本 lane 仍**未链入 `vedit-app`**：实时视图接线（打开时首建、tree-sitter 开关重灌、光标路径 no-op）
  与面包屑接线是 `T94` 的集成工作，不在 T29 的写域内。
