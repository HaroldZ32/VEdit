# `src/editor/probes` — T14 探针（可重跑）

三个**规范探针**，用来钉住 KTextEditor 6.24 的真实语义。它们是独立 CMake 工程，
**不进入产品构建**（根 `CMakeLists.txt` 不收录本目录）。

| 探针 | 回答的问题 | 源码 |
|---|---|---|
| `tx_probe` | `EditingTransaction` 是否把 N 次编辑合并成 1 条 undo（有/无 view）；undo/redo 槽是否存在 | `tx_probe.cpp` |
| `replace_probe2` | `searchText` 的迭代语义 + 自下而上批量替换：文本、匹配数、**一次 undo 回原文** | `replace_probe2.cpp` |
| `fsync_probe` | `tmp→write→fsync(file)→rename→fsync(dir)` 在本文件系统上的返回码 | `fsync_probe.cpp` |

## 一条命令重跑（在固定镜像内）

```bash
docker run --rm -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
  -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local bash -lc '
  B=evidence/edit-core/.work/probe-build
  cmake -S src/editor/probes -B "$B" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build "$B" -j4 >/dev/null &&
  "$B/tx_probe"      | grep "^TX " &&
  "$B/replace_probe2"| grep "^REPLACE2 " &&
  "$B/fsync_probe" "$B" | grep "^FSYNC "'
```

- 构建目录放在**工作区内**（`evidence/edit-core/.work/`，已 gitignore）。**不要**用 `/tmp`：
  本 harness 的 `/tmp` 按命令隔离，跨命令会丢。
- 镜像里需要 `libkf6texteditor-*` 与 katepart 插件（`vedit-qt6-build:local` 已装）；否则
  `KTextEditor::Editor::instance()` 为 null，探针打印 `editor=null FAIL` 并返回 2。

## 期望输出（关键行）

```
TX noview metaClass=KTextEditor::DocumentPrivate undoIndex=70 redoIndex=71
TX noview case=tx3_insert ... verdict=PASS
TX noview case=tx100_insert len=100 after_undo_len=0 empty=1 verdict=PASS
TX view   （与 noview 同结论）
REPLACE2 case=adjacent           found=4 replaced=4 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=zero_length_anchor found=3 replaced=3 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=longer             found=2 replaced=2 text_ok=1 count_ok=1 one_undo_ok=1 PASS
REPLACE2 case=shorter            found=2 replaced=2 text_ok=1 count_ok=1 one_undo_ok=1 PASS
FSYNC step=fsync_dir rc=0 errno=0 (ok)
FSYNC verdict=PASS
```

- `plain3_insert` / `plain100_insert` 在新版里**预期不是「一次退一步」**：KTextEditor 会合并连续键入
  （详见 `evidence/edit-core/00-probe.md` §5/§7）。这两行不是失败，是**语义证据**。
- 判定以 `verdict=PASS` 的行为准；`FAIL` 行数应为 0（`TX` 的 `plain*` 行除外，见上）。
- 完整历史日志：`evidence/edit-core/00-probe-*.log`；结论与硬约束：`evidence/edit-core/00-probe.md`。

## 只用于调查、不属于规范三件的探针

`tx_steps_probe{,2,3,4,5}.cpp` 是追查「undo 粒度」的中间实验（含一次 key-event 尝试，
offscreen 下未能向视图写入字符）——保留以便复现推理链，但不作为验收依据。
