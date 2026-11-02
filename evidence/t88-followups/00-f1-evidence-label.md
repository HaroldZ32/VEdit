# T90/F-1 — 证据标签更正 + 真 F2 修前证据补采（选择“后者更好”）

collected_at: 2026-11-01T10:44:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac

## 事实（评审 F-1 的指控成立）

`evidence/highlight-realkte/01-diagnosis-prefix.log` 在本目录 `REPORT.md` 里被标成
「修复前（**交付口径，F2 库**）」。但该日志本体是：

```
HIGHLIGHT editor_repository_definitions=451
HIGHLIGHT definitionForName(Verilog): name='Verilog' author='Yevgen Voronenko …'  # KDE 作者
```

—— 那一次用的是**开发机发行版的 KSyntaxHighlighting**（内嵌 451 份 KDE 定义），不是 F2 库。

- 修前缺陷的**结论仍然成立**，且**与库无关**：T81 修前的 `loadFileIntoDocument()` 只有
  `setText()`，没有 `setHighlightingMode`（`grep -c applyHighlightingMode` 在修前的
  `document_io.cpp` 上为 0）。
- 修后的 F2 证据（`02-diagnosis-postfix-f2.log`）是真的。

## 选择：补采真 F2 口径的修前证据（而不是只改标签）

两类改动，二者都做了，但**旧日志的字节未动**：

1. **更正标签**：`evidence/highlight-realkte/REPORT.md` 文首加了“更正（T90/F-1）”块；把
   `01-…` 的小节标题与证据表描述改为「开发机发行版库，内嵌 451 份定义；库无关的机制证据」。
2. **补采 `01b-diagnosis-prefix-f2.log`**：
   - 修前代码来源：`git show a6d5a04^:src/editor/kernel/document_io.cpp`（引入修复的提交
     `a6d5a04` 的父提交），在 `target/t90-prefix-src/editor` 的**临时副本**中构建
     （共享工作树未改动；副本只把编译期 `VEDIT_SYNTAX_RESOURCES_DIR` 调整为绝对
     `/vedit-build/resources`，以便从临时位置填充私有 XDG share）。
   - 运行口径：`LD_LIBRARY_PATH=target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu`
     （F2 库，`strings | grep -c 'DOCTYPE language'` = 0，**无内嵌 KDE 定义**）+
     selftest main 把 `XDG_DATA_DIRS`/`XDG_DATA_HOME` 收窄到只含两份自有 XML 的私有 share
     （与 AppRun 的行为一致）。
   - 观测：仓库里是 `None + Verilog(author=VEdit) + SystemVerilog(author=VEdit)`（3 个定义），
     `definitionForFileName(top.v)` 解析到我们自己的 `Verilog`，但文档
     `highlightingMode='None'`、formats/folding 均为 0 → `HIGHLIGHT-DIAGNOSIS checks=15 failures=8`。
   - 与修后的 `02-diagnosis-postfix-f2.log`（mode 选中、formats=30/29、folding 2/1、15/0）
     构成**同一交付口径**的红→绿对照。

## 旧日志未改动的证明

`evidence/t88-followups/01-f1-label-correction.log` 记录了：

- `sha256sum evidence/highlight-realkte/01-diagnosis-prefix.log`
  = `2041cab48ea46bbb5c028e5c4117fce662a580b7c670a62d430b7905606c3107`（与 T90 开始前记录的值相同）；
- `git status --porcelain` / `git diff --stat` 对该路径**无输出**（内容未被改动）；
- 新增 `01b-diagnosis-prefix-f2.log` 的 F2 sha256（`aac288a1…`）、定义列表与 `failures=8` 摘录；
- `REPORT.md` 更正块与两侧标签的 grep 证据。

## 备注

- 本记录只涉及证据与标签；产品代码未因 F-1 改动。
- 临时构建产物在 `target/t90-prefix-src/`、`target/t90-prefix-build/`（`target/` 已被 .gitignore），
  便于评审复跑；不进入提交。
