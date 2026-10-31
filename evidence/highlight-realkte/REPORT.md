# T81 — 真机缺陷：真 KTextEditor 下没有语法高亮（诊断 + 修复 + 无头断言）

collected_at: 2026-10-31T22:55:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
（提交由 Lead 统一负责；本任务未 commit、未 push。）

无头声明：本机没有显示服务器。这里的“有颜色”代理指标是：**文档真的选中的 mode** +
**与 KTextEditor::View 同一高亮引擎（KSyntaxHighlighting）对同一文本产生的 format ranges /
folding regions 数**。KTextEditor 没有公开 API 枚举 view 内部的渲染 range，因此这是可达的
最强代理；真实观感仍需用户在停点 1 用眼睛确认。

---

## 1. 结论（机制级）

**根因：不是“没有定义”，而是“文档从不选择 mode”。**

- 产品打开文件的路径（`loadFileIntoDocument` → `KTextEditor::Document::setText()`）**既没有
  设置文档 URL，也没有调用 `setHighlightingMode`**。KTextEditor 只在有 URL 时才按扩展名自动
  匹配定义；在这条同步打开路径上（`document_io.h` 明确说明不走 `KParts::openUrl()`，因为 KIO
  任务会在无头环境下挂起），文档就停在默认的 **`highlightingMode='None'`**，view 没有任何东西
  可上色。
- 交付运行时里定义其实是**在的**：用交付口径（自建 F2 KSyntaxHighlighting 库 + XDG 收窄到
  AppDir 的 `org.kde.syntax-highlighting/syntax/`，即 AppRun 的行为）实测，编辑器的仓库里有
  `Verilog`（author=`VEdit`）与 `SystemVerilog`（author=`VEdit`），`definitionForFileName("top.v")`
  也解析到我们自己的 `Verilog`。**仓库没问题，文档没选 mode。**
- 为什么 T12 的“3/4 format ranges”没抓到：那是在 **demo 文档 + VEdit 自己的
  `KSyntaxHighlighting::Repository`** 上量的，不是 KTextEditor view 用的仓库/文档；两条管线从
  未在同一条路径上被验证过。
- 另一个把问题藏起来的事实：**本机发行版的 `libKF6SyntaxHighlighting` 内嵌了 451 份 KDE 定义**
  （`strings … | grep -c 'DOCTYPE language'` = 63），所以在这台开发机上仅靠 `XDG_DATA_DIRS`
  收窄并不能让编辑器只看到我们的定义；必须用交付用的 **自建 F2 库**（无内嵌定义）才复现真机。

### 修复前（交付口径，F2 库 + 私有 XDG share）— `01-diagnosis-prefix.log`

```
HIGHLIGHT editor_repository_definitions=3
HIGHLIGHT definitionForName(Verilog): name='Verilog' author='VEdit' … valid=1
HIGHLIGHT diagnose.v: url='' mode='Normal' highlightingMode='None' availableModes=3
HIGHLIGHT diagnose.v: resolved name='None' … valid=0 -> formats=0 begins=0 ends=0
CHECK highlight.document-mode-is-expected FAIL  diagnose.v: got 'None', expected 'Verilog'
```
诊断第一版里我有一条 `!mode.isEmpty()` 的弱断言（`'None'` 也非空，且 invalid definition 也会
产生默认 format）——它当时错误地显示 PASS；这正是任务提醒的“又一个看起来对的证据”。现在这两条
已被收紧为 `mode == expected` 与 **definition 必须 valid 且 formats>0**。

### 修复后（同一交付口径）— `02-diagnosis-postfix-f2.log`

```
HIGHLIGHT defineForName(Verilog): name='Verilog' author='VEdit' … valid=1
HIGHLIGHT diagnose.v: url='' mode='Normal' highlightingMode='Verilog' availableModes=3
HIGHLIGHT diagnose.v: resolved name='Verilog' author='VEdit' -> formats=30 begins=2 ends=2
CHECK highlight.document-mode-is-expected PASS
CHECK highlight.definition-authored-by-vedit PASS
CHECK highlight.format-ranges-positive PASS  diagnose.v: formats=30
CHECK highlight.folding-regions-positive PASS  diagnose.v: begins=2 ends=2
HIGHLIGHT diagnose.sv: highlightingMode='SystemVerilog' -> formats=29 begins=1 ends=1
HIGHLIGHT-DIAGNOSIS checks=15 failures=0
```

## 2. 改了什么（写域内）

| 文件 | 改动 |
|---|---|
| `src/editor/kernel/highlight_mode.{h,cpp}`（新） | 打开文档时的 mode 选择：先让**编辑器仓库自己的** `definitionForFileName()` 决定（若解析到 author=`VEdit` 的定义就用它），否则用 VEdit 扩展名表（`.v/.vh/.vlg`→`Verilog`，`.sv/.svh`→`SystemVerilog`，镜像两份 XML 的 `extensions=`）；`setHighlightingMode(name)` 并校验文档真的报告该 mode；失败/回落到宿主定义时给出诊断文本。另含 `measureHighlighting()`（format ranges + folding regions 计数，供诊断与自测共用） |
| `src/editor/kernel/document_io.cpp` | `loadFileIntoDocument()` 在 `setText()` 成功后调用 `applyHighlightingMode()`；失败/回落写 `qWarning`/`qInfo("[edit-core] syntax highlighting: …")`，**不**使打开失败（打不开比没颜色更糟） |
| `src/editor/kernel/highlight_diagnosis.cpp`（新） | `--diagnose-highlight`：打印 XDG 路径、编辑器仓库定义（name/author/version）、文档 URL/mode/`highlightingMode`/availableModes、解析到的定义与 formats/folding 计数，并断言（mode 正确 + author=VEdit + ranges>0 + folding>0） |
| `src/editor/kernel/edit_core_selftest_main.cpp` | 新增 `--diagnose-highlight`：在 **QApplication 之前**建私有 share（把两份 XML 拷到 `<share>/org.kde.syntax-highlighting/syntax/`）并把 `XDG_DATA_DIRS`/`XDG_DATA_HOME` 指向它（复刻 AppRun 的收窄） |
| `src/editor/kernel/edit_core_selftest.{h,cpp}` | `runSelfChecks` 新增 10 条 `editcore.highlight.*`（.v/.sv 各：mode / definition / format-ranges / folding-regions + fixture/open）；这些断言与宿主是否装 KDE 无关（开发机上解析到 KDE 定义也算“有颜色”，交付口径由 02 号证据严格断言 author=VEdit） |
| `src/editor/CMakeLists.txt` | 新源文件、显式 `KF6::SyntaxHighlighting` 依赖、selftest 的 `VEDIT_SYNTAX_RESOURCES_DIR` 定义 |

**没有改** `resources/syntax/*.xml`（两份 XML 的 sha256 前后一致：`c6adc763…` / `2abba334…`），
因此 **T45/T66 的折叠规则、已登记哈希与 `THIRD_PARTY_NOTICES` 都不需要更新**；也没有改
T12 的 grammar/三库链接（见 §4）。

### 为什么选这个方案（而不是改 XML / 改名 / 改 version）

- 交付运行时（AppRun）已经把 `XDG_DATA_DIRS` **和** `XDG_DATA_HOME` 收窄到 AppDir，仓库里只有
  我们的 `Verilog`/`SystemVerilog`（02 号证据的定义列表就是 `None + 我们两份`）。缺的只是
  “选 mode”，所以最小且正确的修复是显式选 mode。
- 改 XML 的名字/版本号会让已登记的 sha256、T45/T66 证据和许可登记全部连带变更，且会把
  “宿主 KDE 定义版本更高”这类环境差异带进交付；在本问题里没有必要。
- 不改 `src/app/**`（T80 在飞）。

## 3. 可复跑的“修复前/后”对照

```sh
# 修复前（本仓库历史状态）：mode 从不被选择 -> highlightingMode='None'、ranges/folding=0
#   evidence/highlight-realkte/01-diagnosis-prefix.log

# 修复后，交付口径：自建 F2 库（无内嵌 KDE 定义）+ 私有 XDG share（= AppRun 收窄）
F2=$PWD/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu
LD_LIBRARY_PATH=$F2 QT_QPA_PLATFORM=offscreen \
  target/qt6-build/src/app/editor-lane/vedit-edit-core-selftest --diagnose-highlight
#   -> highlightingMode='Verilog'/'SystemVerilog'，formats=30/29，folding begins=2/1，failures=0
```

## 4. 验收证据（本任务改动后实测）

| 项 | 观测 |
|---|---|
| `--diagnose-highlight`（交付口径） | `HIGHLIGHT-DIAGNOSIS checks=15 failures=0`（`02-*.log`） |
| 独立 kernel selftest | `EDIT-CORE-SELFTEST ok=1 checks=65 failures=0`（`03-*.log`；含 10 条新增 `editcore.highlight.*` 全 PASS） |
| `vedit-app --selfcheck` | `APP-EXIT=0`，`EDIT-CORE-SELFTEST ok=1 checks=67 failures=0`，`APP-SHELL-SELFTEST ok=1 checks=176 failures=0`（`04-*.log`；产品进程内同样走 provider 的打开路径） |
| 根 ctest | **17/17 通过**（`05-*.log`；含 `#4 vedit-syntax-kde-data`、`#6 vedit-syntax-product-tests`） |
| `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`**（`07-*.log`） |
| T12 不回退 | `syntax-product: grammar=verilog … registered=[systemverilog,verilog] active='verilog'`；`readelf -d vedit-app` 含 `libKF6TextEditor.so.6`、`libKF6SyntaxHighlighting.so.6`、`libtree-sitter.so.0.25`（`06-*.log`） |
| T45/T66 不回退 | `resources/syntax/*.xml` sha256 未变；`vedit-syntax-kde-data` 与 `vedit-syntax-product-tests` 通过（折叠 begins/ends 断言在其中） |

## 5. 没有验证 / 边界（如实）

1. **真实观感**：无显示服务器，无法断言“用户看到颜色”；断言的是 mode 与同一引擎的
   ranges/folding。停点 1 仍需用户在有屏机器上打开 `.v/.sv` 确认颜色。
2. **KTextEditor view 内部 range**：KTextEditor 6.24 未公开枚举 view 渲染 range 的 API；证据用
   `KTextEditor::Document::highlightingMode()` + 编辑器仓库解析到的 Definition + 同一
   `KSyntaxHighlighting` 引擎计数（与 T12 同一量测方法）。
3. **开发机 vs 交付运行时**：发行版 KSH 库内嵌 451 份 KDE 定义，开发机上仓库会解析到 KDE 的
   `Verilog`/`SystemVerilog`（仍是“有颜色”，但 author 不是 VEdit）；交付口径用自建 F2 库 +
   AppRun 收窄复现（02 号证据）。产品在该情形会打印一行 `[edit-core] syntax highlighting:
   'Verilog' resolves to author '…' (not VEdit); the delivered runtime narrows …`，不静默。
4. **其它语言**：只覆盖 VEdit 交付的 Verilog/SystemVerilog；其它扩展名不设 mode（保持
   KTextEditor 默认行为），`applyHighlightingMode` 返回 false 但打开仍成功。

## 6. 证据文件（`evidence/highlight-realkte/`，均带 C9 头）

| 文件 | 内容 |
|---|---|
| `01-diagnosis-prefix.log` | 修复前、交付口径：仓库有我们的定义（author=VEdit），但文档 `highlightingMode='None'`、无 ranges/folding（并记录第一版弱断言的教训） |
| `02-diagnosis-postfix-f2.log` | 修复后、交付口径：mode 选中、author=VEdit、formats=30/29、folding 2/1、checks=15 failures=0；附 F2 库路径/sha256 与“无内嵌定义”证明 |
| `03-editor-selftest.log` | 独立 kernel selftest（宿主库）65 checks / 0 failures |
| `04-app-selfcheck.log` | 产品 `--selfcheck`（EDIT-CORE 67/0、APP-SHELL 176/0） |
| `05-ctest.log` | 根 ctest 17/17 + T12 两个语法测试 |
| `06-t12-invariants.log` | grammar/registered/三库 NEEDED/符号计数 |
| `07-typecheck.log` | `TYPECHECK-EXIT=0` 全日志 |
