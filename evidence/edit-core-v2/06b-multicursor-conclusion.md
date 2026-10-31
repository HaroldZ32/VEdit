# 多光标结论（Open Q3 / C12）

- Cell: `cell-20261031T085543161Z` · Board task: **T10** · 日期: 2026-10-31
- 问题（requirements rev2 Open Q3）：「KTextEditor 6.24 是否原生支持多光标？不支持则 Qt 侧实现并记录结论。」
- 探针环境：pinned 镜像 `vedit-qt6-build:local`（KTextEditor **6.24.0** / Qt 6.10.2），原始输出见 `06a-multicursor-api.log`
- 结论：**KTextEditor 6.24 原生支持「多个光标位置」（View API），但不是「多光标同时编辑」的文档级 API。**

## 1. 原生支持的部分（有符号与运行期证据）

- 头文件 `KTextEditor/View`（`/usr/include/KF6/KTextEditor/ktexteditor/view.h`）：
  - `void setCursorPositions(const QList<KTextEditor::Cursor> &positions);`（注释明写 “This allows to create multiple cursors in this view”，**since 5.95**）
  - `QList<KTextEditor::Cursor> cursorPositions() const;`（注释明写 “The returned vector contains the primary cursor as first element”）
- 运行期库 `/usr/lib/x86_64-linux-gnu/libKF6TextEditor.so.6` 导出：
  - `T _ZN11KTextEditor4View18setCursorPositionsERK5QListINS_6CursorEE`
  - `T _ZNK11KTextEditor4View15cursorPositionsEv`
- 头文件里**没有** `addCursor` / `MultiCursor` / `secondaryCursor` 之类的独立光标对象 API（grep 计数 0）——多光标是 View 上的一组位置。

## 2. 原生 API 的实测行为（可复跑断言）

`03-app-selfcheck.log` / `05-standalone-ctest.log` 中的检查（真实 KTextEditor view，offscreen）：

```
CHECK editcore.multicursor.native-api PASS            # setCursorPositions(3 个) -> cursorPositions() 回读 3 个
CHECK editcore.multicursor.native-read-back PASS      # 第一个为主光标，位置逐个一致
CHECK editcore.multicursor.native-insert-call PASS
CHECK editcore.multicursor.native-insert-primary PASS 1
EDITCORE-MULTICURSOR native-cursors=3 native-inserted-x=1 text="Xaaa..."
```

即：**光标集合**是原生的（3 个位置全部被接受并回读）；但 `View::insertText()` 这个便利函数只编辑主光标（3 光标下只插入 1 个字符）。无头环境下真实按键注入（`QTest::keyClicks`）在 T14 探针里已被证明无法向视图写入字符（`chars=0`，见 `evidence/edit-core/00-probe.md` §4），因此「按键驱动的多光标输入」不能在无头环境断言。

## 3. 采用的实现（两条路径，都有可执行断言）

1. **交互层**：直接使用原生 `View::setCursorPositions()` / `cursorPositions()`（`src/editor/kernel/multi_cursor.cpp` 的 `setViewCursors()` / `viewCursors()`），也就是 Kate 自己的多光标位置机制；按键行为由 KTextEditor 视图负责。
2. **编辑语义层（可无头断言）**：`insertAtCursors(document, positions, text)` 在**单个 `KTextEditor::Document::EditingTransaction`** 内自下而上插入（高位先插，避免位移失效）。判据是 A1b 要求的口径：N 个光标一次编辑 = **一次 undo 全部回滚**，一次 redo 全部恢复：
   ```
   CHECK editcore.multicursor.batch-insert PASS
   CHECK editcore.multicursor.batch-text PASS XaXaXa
   CHECK editcore.multicursor.batch-one-undo PASS   # 一次 documentUndo() 回 "aaa"
   CHECK editcore.multicursor.batch-redo PASS       # 一次 documentRedo() 回 "XaXaXa"
   ```

## 4. 与 `evidence/edit-core/DESIGN.md` 的差异（有意为之）

- DESIGN 写的实现是「N 个 `MovingCursor` + 单事务」；落地用**位置列表 + 单事务**。
  原因：`Document::newMovingCursor()` 的所有权语义在 6.24 头文件里没有声明（谁 delete 未定义），
  而位置列表自下而上插入能给出同样的「一次 undo 全回滚」保证，且没有生命周期风险。判据不变。
- DESIGN 的 `perf_probe`（10 万行 A1a 基线）不在本任务（T10/AC-S1.1）范围内，未实现；登记为未覆盖项。

## 5. 一句话回答 Open Q3

**原生位置级多光标：有（View::setCursorPositions/cursorPositions，5.95+，实测 6.24 可用）；
原生「一次编辑作用于全部光标」的文档 API：没有。**
因此编辑内核在文档层用单事务批量插入保证 A1b 语义，交互层复用原生 view 光标集合。无需 Qt 侧自造光标对象，也没有和用户另议的阻塞。
