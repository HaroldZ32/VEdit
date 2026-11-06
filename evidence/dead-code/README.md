# T29 — 批7b 乙：删除 `vedit_syntax_folding` 死代码（AC-7.7）

Cell `cell-20261103T123639841Z` · task `T29` · owner Junior Engineer · verifier Reviewer.
写域（captain 2026-11-04 书面裁定）：`src/syntax/folding/`、`src/editor/CMakeLists.txt`、
`resources/queries/`、`translations/vedit_zh_CN.ts`、`src/editor/kernel/fold_gutter_click_probe.cpp`、
`evidence/dead-code/**`。

## 1. 结论（一句话）

`vedit_syntax_folding` 库、它的 CMake 接线与 2 个测试可执行已删除；
**第 3 个（真实 gutter 点击）没有被删，而是迁到 editor lane 并重新注册，check id 不变**；
`resources/queries/**` 与面包屑接线**保留**并写明理由；`.ts` 只删 folding 独有的 7 条；
`grep -r vedit_syntax_folding`（源码/CMake/脚本）= **0**；构建 + ctest 全绿。
产品覆盖点**没有减少**：唯一的断言点减少是 10 个重复的 `syntax_directory` 断言，其覆盖在
`src/syntax/kde/tests/syntax_directory_equivalence.cpp`（9 个断言点）与 ctest id
`vedit-syntax-directory-equivalence` 上仍然存在。

## 2. 删除清单（13 个文件，全部为 tracked，可用 `git show HEAD:<path>` 复现）

| 文件 | 行数 |
|---|---|
| `src/syntax/folding/CMakeLists.txt` | 103 |
| `src/syntax/folding/README.md` | 73 |
| `src/syntax/folding/fold_range.{h,cpp}` | 72 / 84 |
| `src/syntax/folding/fold_source.{h,cpp}` | 56 / 30 |
| `src/syntax/folding/ksyntax_fold_source.{h,cpp}` | 85 / 211 |
| `src/syntax/folding/syntax_fold_source.{h,cpp}` | 42 / 79 |
| `src/syntax/folding/tests/syntax_directory_tests.cpp` | 106 |
| `src/syntax/folding/tests/fold_native_probe.cpp` | 299 |
| `src/syntax/folding/tests/fold_native_gui_probe.cpp` | 269 → **迁移**，见 §4 |

构建图里消失的 target：

| target | 类型 | 处置 |
|---|---|---|
| `vedit_syntax_folding` | STATIC 库 | 删除 |
| `vedit-syntax-directory-tests` | 可执行 + ctest id | 删除（覆盖重复，见 §5） |
| `vedit-fold-native-probe` | 可执行（无 add_test，T125 一次性实验器） | 删除 |
| `vedit-fold-native-gui-probe` | 可执行 | **迁移**为 `fold-gutter-click-probe`（`src/editor/kernel/fold_gutter_click_probe.cpp`） |
| `vedit-fold-gutter-click` | ctest id | **保留，名称不变**，改在 `src/editor/CMakeLists.txt` 注册 |

## 3. 为什么这个 lane 是死的（有据可查，不是推断）

- 产品侧**没有任何文件** include 它：`grep -rn 'fold_range.h|fold_source.h|syntax_fold_source.h|
  ksyntax_fold_source.h|fold_injector' src/ | grep -v '^src/syntax/folding/'` → **0 行**。
- 它的唯一消费者曾经是 `FoldInjector`（T128 已删）与折叠扫描探针；AGENT.md §10 原始记录：
  「仍保留但**已无产品消费方**……下一步可整体删除（删 `vedit_syntax_folding` 及其 CMake 接线）」。
- 折叠的产品行为由 KDE 自己按 KSyntaxHighlighting 定义计算（AGENT.md §9/§10），VEdit 不再注入
  `exec("fold")`；因此删掉 lane 不改变任何用户可见行为。
- `src/editor/CMakeLists.txt` 里 lane 的**唯一**入口是 `add_subdirectory(.../syntax/folding)` +
  `target_link_libraries(vedit_editor_core PUBLIC vedit_syntax_folding)`，两者本次都已移除。

## 4. 保留 / 迁移，以及理由

### 4.1 真实 gutter 点击验收（`vedit-fold-gutter-click`）——迁移，不删

- 事实：`src/syntax/folding/tests/fold_native_gui_probe.cpp` 只 include `KTextEditor/*` 与 Qt
  （`QApplication`/`QImage`/`QMouseEvent`…），**从不调用** `vedit_syntax_folding` 的任何符号；
  旧的 `target_link_libraries(... vedit_syntax_folding Qt6::Gui)` 只是把它挂在 lane 旁边。
- 迁移：源码 → `src/editor/kernel/fold_gutter_click_probe.cpp`（仅改头注释，说明迁移与依赖边界，
  逻辑零改动）；target → `fold-gutter-click-probe`（link `Qt6::Widgets KF6::TextEditor`）；
  ctest id 仍是 **`vedit-fold-gutter-click`**（xvfb-run + `QT_QPA_PLATFORM=xcb`，无 xvfb 时 SKIP 77），
  AGENT.md §10 记录的那条判据仍可被 `ctest -R vedit-fold-gutter-click` 找到。
- 为什么不能删：`vedit-native-fold-tests`（`src/editor/kernel/native_fold_tests.cpp`）断言的是
  10k 行打开 ≤2 s / 二次打开不崩 / 折叠时移动光标不崩 / 打开不自动折叠，**没有**真实点 marker 的
  双向覆盖；删掉就是产品覆盖点减少，与契约「产品覆盖点不减少」冲突（captain 裁定：迁移，不删）。

### 4.2 `resources/queries/**`——全部保留（6 个 .scm）

- `highlights.scm` / `folds.scm` / `structure.scm`（verilog + systemverilog）被
  `src/syntax/grammars/CMakeLists.txt:58-163` **嵌入进构建产物**（bindings 的 sha256 与内容都来自这些
  文件）；`structure.scm` 还被面包屑产品路径消费（`src/workspace/breadcrumb/syntax_breadcrumb_source.*`）。
- 删任何一个都需要改 `src/syntax/grammars/CMakeLists.txt`（T29 写域之外）→ 按契约「确无消费方才删，
  否则书面保留理由」，**全部保留**。

### 4.3 面包屑接线与 `VEDIT_EDIT_CORE_HAVE_FOLDING` 宏——保留

- `src/editor/CMakeLists.txt` 里原来一个 `if()` 同时接 folding 与 breadcrumb 两个 lane；本次只摘掉
  folding 的 `add_subdirectory` 与库链接，**保留** breadcrumb 的 `add_subdirectory`、
  `kernel/fold_breadcrumb.cpp` 与 `VEDIT_EDIT_CORE_HAVE_FOLDING=1`。
- 宏名**故意不改**：`src/editor/kernel/ktexteditor_provider.cpp` 用它守护面包屑绑定，改宏名要动
  `src/editor/kernel/`（写域外）；CMake 注释已写明该宏现在只表示「面包屑已接线」。

### 4.4 `.ts`——只删 folding 独有 7 条

- 位于 `src/syntax/folding/` 的 message 共 9 条；其中 2 条（`no provider for grammar '%1'`、
  `parse returned no tree`）**同时**有 `src/workspace/breadcrumb/syntax_breadcrumb_source.cpp` 的
  location → **保留**（面包屑仍在用），只清掉它们身上那条已失效的 folding location。
- 删除 7 条（均无任何 src/ 调用点残留，逐条核对过）：
  `cannot open %1: %2`、`cannot parse %1: %2`、`fold lists of region %1 are empty`、
  `source is not loaded: %1`、`tree-sitter switch is off`、`grammar '%1' is not registered`、
  `provider '%1' parsed %2 fold ranges`。
  （注意 `tree-sitter switch is off` 与 `src/syntax/overlay/overlay.cpp:62` 那条**更长的**不同串
  `SyntaxService tree-sitter switch is off (A1h): …` 不是同一条，后者未动。）
- `.ts` 计数：943 → **936 `<message>`**（922 finished + 14 vanished），lrelease
  `Generated 922 translation(s) (922 finished and 0 unfinished)`（`04-lrelease-after.log`）。

## 5. 覆盖点去向（删除的测试逐条登记）

| 删除的测试 | 覆盖点 | 去向 |
|---|---|---|
| `vedit-syntax-directory-tests`（106 行 / **9 个 check 调用点 = 8 个不同 id**） | `VEDIT_SYNTAX_DIR` 归一化（T84/T109 数据根） | **不是完整重复**（T49 更正 T29 时的过度声明）：等价测试只覆盖其中 **4 个调用点**。另 **2 个仍在用的** id（`syntax-dir.missing-path-is-empty`、`syntax-dir.empty-string-is-empty`，`resolveSyntaxDefinitionDirectory` 是活代码 —— `src/syntax/kde/ksyntax_data.cpp:108` 在调）已由 T49 **原样补回** `src/syntax/kde/tests/syntax_directory_equivalence.cpp`；剩下 **3 个**（`syntax-dir.file-verilog`、`syntax-dir.file-systemverilog`、`syntax-dir.application-fallback`）断言的是两个**已无任何调用点**的 helper —— T49 把这两个 helper 一并删除，因此它们**没有覆盖点**（死代码断言）。详见 §9 与 `16-check-ids-after-repair.txt`。 |
| `vedit-fold-native-probe`（299 行 / 0 个断言点） | T125 一次性实验器（打印 `scroll_max`/`hidden` 观察，无 add_test） | 实验结论已固化进 `vedit-fold-gutter-click`（真实点击双向）与 `vedit-native-fold-tests`；实验器本身无产品覆盖点 |
| `vedit-fold-native-gui-probe`（269 行 / 0 个断言点，靠 `ok=` 结果码） | T128 真实 gutter 点击（xcb+Xvfb） | **未删除**：迁到 `src/editor/kernel/fold_gutter_click_probe.cpp`，ctest id 不变 |

## 6. 数字（本轮实测，日志见本目录）

| 项 | before | after |
|---|---|---|
| `grep -r vedit_syntax_folding`（src + CMake + scripts） | 11 行 / 3 个 CMake 文件 | **0**（`00-before-grep.txt`、`06-after-grep.txt`） |
| ctest 用例数 | 38 | **37**（`02-before-ctest.txt`、`08-after-ctest.txt`） |
| `vedit-fold-gutter-click` | 存在 | **存在（同名）**，见 `08-after-ctest.txt` |
| AC-4.7 口径断言点（`check(/QVERIFY(/QVERIFY2(/QCOMPARE(/expect(`，`src/`） | 2447 | **2439**（T29 删掉 `syntax_directory_tests.cpp` 的 10 个调用点；T49 把其中 2 个仍在用的原样补回，净 -8） |
| `.ts` message | 943 = **929 finished + 14 vanished**（T27 终态） | **936 = 922 finished + 14 vanished**（删 7 条，全部是 finished，vanished 不变） |
| lrelease | `929 finished / 0 unfinished` | **`922 finished / 0 unfinished`** |

`673` 这个「lane selftest 断言基线」在本 revision **无法复现**：按 `evidence/i18n/14-selftest-coverage.md`
记录的口径（`check(/QVERIFY(/QVERIFY2(/QCOMPARE(/expect(` 调用点，扫 `src/`）当前实测是 **2447**。
如实报告这一差异，不猜一个接近 673 的扫描范围来"凑数"。

## 7. 收口门禁

见 `09-gates.md`（逐条命令 + 观测 exit code + 日志文件）。

## 8. 遗留（不在 T29 写域，交给相应任务）

1. **过期注释里的路径**（不是 target 名）：`src/workspace/breadcrumb/CMakeLists.txt:8`、
   `src/workspace/breadcrumb/breadcrumb_model.h:16`、`src/workspace/breadcrumb/README.md:27`、
   `src/syntax/common/CMakeLists.txt:5` 仍在注释里提到 `src/syntax/folding`。它们在 T29 写域外，
   未改；属于文案清理（文档/收口任务）。
2. **AGENT.md §10 末句**仍写「下一步可整体删除（删 `vedit_syntax_folding` 及其 CMake 接线）」——
   该动作本轮已完成，但 AGENT.md 不在 T29 写域，**未改**，请文档任务收口。
3. 本 lane 的 README/`README.md` 已随目录删除，其历史内容可从 `git show HEAD:src/syntax/folding/README.md`
   取回。

## 9. T49 修复记录（review rec-20261104T055834-63ba3a / rep-20261104T055834-f7d666）

评审判 FAIL 的两条，逐条闭合：

### F1（blocking）删除的测试不是「完整重复」——已按实测更正并补回

- 事实：`vedit-syntax-directory-tests` 有 9 个 check 调用点（8 个不同 id），
  `syntax_directory_equivalence.cpp` 只覆盖同类语义的 4 个。
- **补回 2 条活代码断言**（id 与语义保持原样）：`syntax-dir.missing-path-is-empty`、
  `syntax-dir.empty-string-is-empty` → 现在在 `src/syntax/kde/tests/syntax_directory_equivalence.cpp`，
  跟着 ctest id `vedit-syntax-directory-equivalence` 一起跑，实测 PASS。
- **删掉 2 个死 helper**（`syntaxDefinitionFileName`、`applicationSyntaxDefinitionDirectory`：
  全仓 `grep` 只剩自身声明/定义，零调用点）→ `src/syntax/common/syntax_directory.{h,cpp}`，
  随之 `#include <QCoreApplication>` 也去掉；`grep -rn` 两个名字 = **0**。
- 因此原测试里的 `syntax-dir.file-verilog`、`syntax-dir.file-systemverilog`、
  `syntax-dir.application-fallback` 三条断言**没有覆盖点**：它们测的东西已不存在
  （记录在案，不是"覆盖被悄悄丢掉"）。

### F2 证据数字错误——已更正

- `.ts` before 写成「943（922 finished + 21 vanished）」→ 更正为 **943 = 929 finished + 14 vanished**；
  after **936 = 922 finished + 14 vanished**（943-936 = 删掉的 7 条都是 finished，929→922；vanished 不变）。
- §5 的「genuine duplicate / 9 assertion points」过度声明 → 更正为上面 F1 的实测映射。
- `09-gates.md` 里重复的同一句过度声明 → 同步更正。

### 复验（日志见本目录）
- `15-build-after-repair.log`：构建 + ctest（37 项，全绿）
- `16-check-ids-after-repair.txt`：等价测试的 CHECK id 清单（含补回的 2 条）+ 死 helper grep=0
- `17-gutter-and-lane-tests.log`：`ctest -R` 目标用例（equivalence / gutter-click / native-fold）
- `18-gates-after-repair.md`：七道门禁逐条 exit code + lrelease/对账交叉检查
