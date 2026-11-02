# T94 — 折叠注入器与面包屑接进 `vedit-app`（真 shell + 真 KTextEditor provider）

collected_at: 2026-11-01T14:46:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
（提交由 Lead 统一负责；本任务未 commit、未 push。）

无头声明：本机没有显示服务器。视图观测走 A3.1 允许的公开 API（`firstDisplayedLine(RealLine|VisibleLine)` +
`verticalScrollBar()->maximum()`），offscreen 平台；真实屏幕观感仍属停点验收。

## 0. 一句话

新增 `vedit::editor::FoldBreadcrumbBinding`：每个 KTextEditor 视图一个，把 T19 的折叠注入器与面包屑模型
接到**真产品路径**（provider 创建视图时建立、打开文件时 `FirstOpen` 重建、运行期开关 `TreeSitterToggled`
重灌/释放、光标移动更新面包屑并通过 `EditorProvider` 的新 status 接缝重绘 pane 的面包屑栏）。
`FoldInjector::inject()` 的 A1 四项自验保持在路径上；产品自检断言 **A1 未被绕过**、**按键/编辑不重建**、
**关闭态 hidden_in_viewport=975**、**面包屑 `module:m0 > function:inc`**、**tree-sitter 关闭不发明路径**。

## 1. 接在哪里（生产路径，不是测试钩子）

| 位置 | 内容 |
|---|---|
| `src/editor/kernel/fold_breadcrumb.{h,cpp}`（新） | 绑定对象：`FoldInjector`（每视图一个）+ 关闭态 `KsyntaxFoldSource`（自有 XML，T29）+ tree-sitter `SyntaxFoldSource` + 面包屑 `Model`/`SyntaxBreadcrumbSource`；事件过滤器只数 KeyPress/Wheel（显式 no-op）；文档信号只做防抖面包屑（150 ms），**从不因编辑重建折叠** |
| `src/editor/kernel/ktexteditor_provider.{h,cpp}` | `createEditor()` 里为每个视图建绑定；`openFile()` 在 `setText` 前 `releaseFolds()`（见 §4）、成功后 `onDocumentOpened()`（FirstOpen）；实现 `statusText()` / `setStatusListener()` |
| `src/editor/CMakeLists.txt` | `add_subdirectory` folding + breadcrumb lane（root 构建已有 grammars），链接 `vedit_syntax_folding`、`vedit_workspace_breadcrumb`，定义 `VEDIT_EDIT_CORE_HAVE_FOLDING`；lane 不存在时优雅跳过 |
| `src/app/editor_provider.h`（最小） | 新接缝：`statusText(QWidget*)` / `setStatusListener(QWidget*, std::function<void()>)`，默认空实现（demo provider 不受影响；shell 库不依赖 kernel 头） |
| `src/app/editor_area.{h,cpp}`（最小） | pane 头下方新增面包屑 `QLabel`（objectName `editor-pane-breadcrumb`），空字符串自动隐藏；provider 推送变化时重绘；`breadcrumbText()` 供自检 |
| `src/app/main.cpp` | 产品自检 18 条 `fold.wiring.*`；交互路径注册可勾选命令 `view.treesitter`（运行期开关，关闭时对所有 pane 调 `setTreeSitterEnabled(false)`：unfold 释放折叠 + 面包屑清空） |

## 2. 三种重建触发源（A2.1）

| 触发 | 何时 | 证据 |
|---|---|---|
| `FirstOpen` | `openFile()` 成功后一次 | `fold.wiring.firstopen-full-count PASS full_count=1` |
| `TreeSitterToggled` | `setTreeSitterEnabled()`（绑定额 + 产品命令）：unfold 旧 registry 后按新来源重灌 | `fold.wiring.toggle-on-rebuild PASS full_count=3`（恰 +1） |
| `Reparsed` | `notifyReparsed()` 显式入口（给后续 reparse 管线；本次不擅自挂到 textChanged） | `fold.wiring.reparsed-trigger PASS full_count=6`（恰 +1） |

**按键/光标/滚动/编辑均显式 no-op**（不重建）：`fold.wiring.no-rebuild-on-keys PASS`（50 次 KeyPress，
`full_count` 2→2）、`fold.wiring.no-rebuild-on-edit PASS`（真实 `insertText`，5→5）。

## 3. 判据（真 ApplicationShell + 真 KTextEditor provider，1000 行 .sv）

`APP-SHELL-SELFTEST ok=1 checks=200 failures=0`、`EDIT-CORE-SELFTEST ok=1 checks=67 failures=0`。

```
FOLD-WIRING closed_state first_real=0 last_real=1000 first_visible=0 last_visible=25
             full_count=2 injected_ranges=200 hidden_in_viewport=975 scroll_max=0 tree_sitter=0
CHECK fold.wiring.closed.hidden-975 PASS hidden=975          # T29 判据在接线后仍成立
CHECK fold.wiring.closed.registry-200 PASS registry=200      # XML 规则：25 模块 × 8
CHECK fold.wiring.closed.no-rejects PASS
CHECK fold.wiring.a1-invalid-rejected PASS injected=0 rejected=1
CHECK fold.wiring.breadcrumb.describe PASS module:m0 > function:inc
CHECK fold.wiring.breadcrumb.pane-bar PASS m0 > inc
CHECK fold.wiring.breadcrumb.no-invention PASS describe='' bar=''
```

- **没有绕过 A1**：绑定只经 `FoldInjector::inject()/rebuildFrom()`；自检直接注入 `{5,5}`，返回 0 且 rejected+1
  （T19 的负向对照正是：不校验时 invalid range 会回落折叠用户选区、倒置 range 返回 true 仍隐藏 10 行）。
- **高亮 mode 未回退**：`fold.wiring.highlight-mode PASS SystemVerilog`（T81 语义）；T80 的
  `save.shortcut.unique/authority/multi-pane` 全 PASS。
- **面包屑**：光标进函数体 → `module:m0 > function:inc`，pane 面包屑栏 `m0 > inc`；tree-sitter 关闭 →
  空路径且栏隐藏（**不发明**文本启发式路径）。

## 4. 接线中发现并修复的真崩（search-jump 回归）

第一版接线让 `workspace-search-selftest` **SIGSEGV**：同一 pane 打开第二个文件（a.v → b.sv）时，
KTextEditor 文档仍带着上一个文本的折叠 range，`setText()/setCursorPosition()` 期间崩溃（core dump；
`VEDIT_T94_NO_FOLD=1` 时不崩，逐层探针定位到 fold 路径）。修复：`openFile()` 在替换文本**之前**
调用 `releaseFolds()`（`injector_.rebuild(FirstOpen, {})` 的 unfold 路径，registry 空时跳过），
再 `FirstOpen` 重灌。回归证据：`WORKSPACE-SEARCH-SELFTEST ok=1 checks=105 failures=0 skipped=2`。

## 5. 门禁（自跑输出）

| 门禁 | 结果 |
|---|---|
| `vedit-app --selfcheck` | **`APP-SHELL-SELFTEST ok=1 checks=200 failures=0`**（`01-selfcheck.log`） |
| 根 ctest | **22/22 Passed**（`02-ctest.log`；其中 `vedit-workspace-search-selftest` 105/0、`vedit-folding-tests`/`vedit-breadcrumb-tests` 随 lane 接入） |
| `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`**（最终运行，`03-typecheck.log`）：`[qt6] PASS: ... build (warnings-as-errors)` + Rust clippy 通过。**过程记录**：同一门禁在约 14:36 的一次运行是 `TYPECHECK-EXIT=101`，红在 `crates/vd-ffi/src/vcs.rs:485/490 used expect()`（在飞 VCS lane 的未提交改动，不在 T94 写域）；该 lane 修好后重跑即 0。全程未用 `-Wno-format`/pragma 绕过任何错误。 |

复跑：

```bash
bash scripts/build-qt.sh
QT_QPA_PLATFORM=offscreen target/qt6-build/src/app/vedit-app --selfcheck
QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build --output-on-failure
```

## 6. 边界（如实）

1. `Reparsed` 触发源有显式入口 `notifyReparsed()`，但本任务**没有**把它挂到 `textChanged`：编辑后的
   折叠刷新需要真正的 reparse 管线，把 textChanged 直接当 Reparsed 会退化成“每次输入重建”（A2.1 禁止）。
   当前语义：编辑不重建（自检断言），需要时由管线显式调用。
2. `releaseFolds()` 走 `rebuild(FirstOpen, {})`，会计一次 `full_count`（lane 没有“只 unfold 不计数”的
   公开 API）。首次打开（registry 空）不触发，所以 `firstopen-full-count=1` 语义不变；同一 pane 二次
   打开会 +2（release + FirstOpen）。这是接线在 lane 公开 API 内的最小实现，若评审要求精确计数，
   需要 folding lane 增加一个 release-only 入口（不在 T94 写域）。
3. 无显示服务器：面包屑栏与折叠的**观感**（颜色、折行）不可断言，标 UNVERIFIABLE；无头断言覆盖
   模型/文本/观测数值。
4. 产品开关 `view.treesitter` 只在交互路径 `main()` 注册（自检里用绑定 API 直接驱动同一实现）；
   其菜单勾选状态由 `QAction::setChecked` 同步。

## 7. 改动文件

`src/editor/kernel/fold_breadcrumb.{h,cpp}`（新）、`src/editor/kernel/ktexteditor_provider.{h,cpp}`、
`src/editor/CMakeLists.txt`、`src/app/editor_provider.h`、`src/app/editor_area.{h,cpp}`、`src/app/main.cpp`、
本目录证据。未改 lane 内部逻辑（`src/syntax/folding/**`、`src/workspace/breadcrumb/**` 只调用公开头）。
