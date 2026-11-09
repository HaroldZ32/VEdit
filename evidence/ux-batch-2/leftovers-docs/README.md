# leftovers-docs — T44：*.flist 遗留、help_viewer 注释、handover 的 BUILD.md 行号

**任务**：T44（`fix/leftovers`）。三处文档与已交付代码不一致；本卡修 1 与 3，2 按要求报告
（代码侧已在工作区被另一个 lane 修好，见 §2）。

**写范围**：`docs/`、`evidence/ux-batch-2/leftovers-docs/`。未改 `src/`、未改代码逻辑。

## 1. 手册的 `.flist` glob（已修）

**旧文**：

- `docs/USER_GUIDE.md:73`：`Filelists (*.f *.F *.flist)`（三组过滤器的第二组）；
- `:96`：`打开一个 `.f` / `.F` / `.flist`：`；
- `:612`：``- `*.f` / `*.F` / `*.flist`：filelist；``。

**现在是**：

- `:73`：`... Filelists (*.f *.F)`、`All Files (*)`。过滤器只列 `*.f` / `*.F`；`.flist` 长拼写不在
  过滤器里，但仍可通过 `All Files (*)` 选中并打开，导入行为相同。
- `:96`：`打开一个 `.f` / `.F`（`.flist` 长拼写用对话框的 `All Files (*)` 选中）：`
- `:612`：``- `*.f` / `*.F`：filelist（对话框默认只筛这两种；`.flist` 拼写通过 `All Files (*)` 打开后
  仍按 filelist 导入）；``

**代码依据**：

- `src/common/file_dialog_filters.h:36-50`：`filelistExtensions()` = `{"f"}`；`filelistGlobs()` =
  `{"*.f", "*.F"}`（大写拼写单列，因为 QFileSystemModel 在 Linux 上按大小写匹配 glob）；
- `src/app/app_shell.cpp:48`：RM-16 注释，`openFileDialogFilter()` 用共享表，旧字面量曾 advertise
  `.flist`；
- 产品测试：`src/app/main.cpp:807` `open.filter.no-guessed-flist` 断言过滤器不含 `*.flist`；
  `src/app/file_dialog_filters_selftest.cpp:202-206` 同样断言。
- `.flist` 的 nuance：打开路径（`src/workspace/project/project_filelist.cpp:18-22`）仍接受
  `f` / `flist`（大小写不敏感），`project_selftest.cpp:555-556` 的 `filelist.plan.flist` 断言
  `.flist` 会按 filelist 导入。所以“过滤器不再列出、但用 `All Files (*)` 选中后导入行为相同”是
  交付代码的事实，不是猜测。

**grep 证明**（`logs/03-greps-after.log`）：

```
$ grep -n '\*.flist' docs/USER_GUIDE.md
exit=1    # 0 命中：旧 glob 已消失（`.flist` 只作为 All Files 的说明文字保留）
```

## 2. help_viewer.h 的旧标题（报告，未由本 lane 修改）

**位置**：`src/app/help_viewer.h:30`（工作区当前内容）。

**旧文**（HEAD 版本 / `git diff` 的 `-` 行）：

```cpp
// heading ("VEdit 用户指南"); and the window title.
```

**新文**（工作区当前 / `git diff` 的 `+` 行）：

```cpp
// heading ("VEdit 用户手册"); and the window title.
```

**事实核对**：

- `docs/USER_GUIDE.md:1` = `# VEdit 用户手册`；
- `src/app/ux_selftest.cpp:188` 的 `kManualTitle = "VEdit 用户手册"`，`:347`/`:362` 用它断言
  `manualTitle()` 与渲染视图；
- `grep -rn '用户指南' docs/` → **0 命中**（docs 里已没有旧标题）。

**状态**：我没有编辑该文件。工作区里的 `src/app/help_viewer.h` 已有这项修改（不是我做的；
`git diff` 显示 `用户指南 → 用户手册`，文件 mtime `2026-11-07 00:12:21`）。因此这条不需要代码 owner
再动手，但按要求登记 old→new 原文（`logs/04-help-viewer-report.log` 有 diff）。

## 3. handover 的 BUILD.md 行号引用（已修）

`docs/handover/03-architecture.md:157` 原文：

> `package.json:7-17` ... Qt lane 是折进去的（`docs/BUILD.md:278-287`）。

现在：

> （`docs/BUILD.md §12.5`「门禁」）

| 旧引用 | 新引用 | 理由 |
|---|---|---|
| `docs/BUILD.md:278-287` | `docs/BUILD.md §12.5`（当前第 386–396 行） | T35/T39/T42 改写了 BUILD.md，旧行号失效；改用**章节号**，下次内容移动也不会再错 |

**handover 全目录 sweep**：`grep -rn 'BUILD\.md:[0-9]' docs/` 修复前 = 1 条（就是上面这条），
修复后 = **0 条**（`logs/03`）。其余 `docs/BUILD.md §13` / 无行号的引用仍然有效。

## 4. 门禁

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
docs-gate exit=0
```

## 5. 超范围观察（未改，供代码 owner 判断）

`src/common/file_dialog_filters.h` 的注释说“`.flist` 是 UI 猜测、没有 producer/consumer 认它”，
但打开路径的 `planOpenedFilelist()`（`src/workspace/project/project_filelist.cpp:18-22`）与
`project_selftest.cpp:555-556` 的 `filelist.plan.flist` 仍然接受 `.flist`。两者对“谁认 `.flist`”的
说法不一致：过滤器/发现谓词只认 `f`，打开规划器仍认 `f|flist`。手册现在按“过滤器只列 `*.f *.F`、
`.flist` 走 All Files 打开后按 filelist 导入”写，与**打开路径**一致；如果产品决定彻底不认 `.flist`，
应改的是打开规划器与它的 selftest，不是手册。

## 6. 证据文件

- `logs/01-docs-gate.log` — 文档门禁 PASS
- `logs/02-greps-before.log` — 修复前三处 grep
- `logs/03-greps-after.log` — 修复后 grep（旧 glob 0 命中、docs 无“用户指南”、无 `BUILD.md:<line>`）
- `logs/04-help-viewer-report.log` — H1、help_viewer diff/当前行、ux_selftest 常量
