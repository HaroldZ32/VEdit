# T111 — `src/syntax/kde` CMake 漏接 `vedit_syntax_common`：干净 worktree 验证

collected_at: 2026-11-01T16:32:00+08:00
collected_at_utc: 2026-11-01T08:32:00Z
git_head: 5b76318952ddd860227be043e11203b5c5ec5c02
git_status_porcelain:
 （本报告写入时快照见同目录日志的 C9 头；T111 没有改产品代码）
---

## 0. 结论

- **缺陷确认**：HEAD **`a4b8efd`** 从提交确实无法构建 —— `src/syntax/common/` 在该提交里**缺
  `syntax_directory.h`**，且 `src/syntax/kde/CMakeLists.txt` 只链 `Qt6::Core KF6::SyntaxHighlighting`、
  **没有** `vedit_syntax_common`。干净 worktree 复现：`ksyntax_data.cpp:3: fatal error:
  syntax_directory.h: No such file or directory`（`01-broken-worktree-a4b8efd.log`，BROKEN-BUILD-EXIT=1）。
- **修复提交**：**`5b76318`**（Lead 的 "commit the T110 cmake wiring and equivalence test that were
  left behind"）的内容与 T110 我准备提交的方案一致：`src/editor/kernel/fold_breadcrumb.cpp`（改用共享工具）
  + `src/syntax/kde/CMakeLists.txt`（add_subdirectory common + `PUBLIC ... vedit_syntax_common`）
  + `syntax_directory_equivalence.cpp`。**没有与本地方案冲突的改动**；T111 没有需要覆盖的本地未提交代码。
- 提交地图：`5b76318`（修复）→ `55a874d`（只加了 packaging 的 appimagetool vendoring + evidence，
  `git diff --stat 5b76318 55a874d -- src/syntax src/editor` 为空）→ `eb7bee9`（只加 docs/evidence 日志）。
- **干净 worktree 验证（5b76318）全部通过**：
  - 独立 `vedit_syntax_kde`：configure/build exit 0，ctest 2/2（含 T110 等价性测试）；
  - 独立 folding lane：configure/build exit 0；
  - **`bash scripts/build-qt.sh`：`BUILD-QT-EXIT=0`**，其中产品 ctest **27/27** 通过（`03-clean-worktree-build-qt.log`）。
- T111 本身**没有改任何产品代码/CMake**（HEAD 已经是对的）；本任务的交付是"干净树验证 + 漏接排查 + 证据"。
  若需要我把同一验证再钉在更早的坏提交上，证据 01 就是。

## 1. 为什么主工作树当时是绿的（反思）

T110 期间我的改动（`syntax_directory.h`、kde CMake 的链接）在**主工作树的未提交状态**里；Lead 先提交了
`ksyntax_data.cpp`（引用了还不存在的头）而 CMake/头文件留在工作树 → 我本地能编、`a4b8efd` 编不了。
这是**只在脏树上验证**的纪律错误：以后 CMake/头文件的接线必须用 `git worktree add --detach <HEAD>`
在**提交内容**上验证，而不是在工作树上验证。

## 2. 漏接排查（全部 `syntax_directory.h` 使用方）

| 使用方（`#include "syntax_directory.h"`） | 所属 CMake 目标 | 接线 | 状态 |
|---|---|---|---|
| `src/syntax/common/syntax_directory.cpp` | `vedit_syntax_common` | 自身 include dir | ✅ |
| `src/syntax/kde/ksyntax_data.cpp` | `vedit_syntax_kde` | `PUBLIC ... vedit_syntax_common`（HEAD 5b76318 第 24 行；a4b8efd 第 18 行**缺失**） | ✅（修复已在 HEAD） |
| `src/syntax/kde/tests/syntax_directory_equivalence.cpp` | `vedit-syntax-directory-equivalence` | 链 `vedit_syntax_kde` → common PUBLIC | ✅ |
| `src/syntax/folding/ksyntax_fold_source.cpp` | `vedit_syntax_folding` | `PUBLIC vedit_syntax_common`（folding CMake 第 56 行） | ✅ |
| `src/syntax/folding/tests/syntax_directory_tests.cpp` | `vedit-syntax-directory-tests` | 链 `vedit_syntax_folding` → common PUBLIC | ✅ |
| `src/editor/kernel/fold_breadcrumb.cpp` | `vedit_editor_core` | 经 `vedit_syntax_folding` 的 **PUBLIC** 依赖传递 include dir（editor CMake 第 106 行） | ✅（干净树全量构建证明） |

结论：**5b76318 上已没有第三方漏接**。`common` 的 include dir 由 PUBLIC 链路传播，不是相对路径 hack；
`ksyntax_data.cpp` 的代码逻辑一行未动（T111 只做验证）。

## 3. 证据（均带 C9 头）

| 文件 | 内容 |
|---|---|
| `01-broken-worktree-a4b8efd.log` | 干净 worktree @ a4b8efd：`fatal error: syntax_directory.h`，BUILD-EXIT=1（复现 T31 报告） |
| `02-clean-worktree-standalone.log` | 干净 worktree @ 5b76318：kde 独立构建 exit 0 + ctest 2/2；folding 独立构建 exit 0 |
| `03-clean-worktree-build-qt.log` | 干净 worktree @ 5b76318：**`bash scripts/build-qt.sh` → BUILD-QT-EXIT=0**，产品 ctest **27/27** |
| `04-consumer-audit.log` | 上表的数据来源（include 站点、各 CMake 的 common 链接、两个 HEAD 的对比） |
| `05-main-tree-ctest.log` | 主工作树根 ctest：**27/27**（无回归） |
| `06-main-tree-typecheck.log` | 主工作树 `bash build/gates/typecheck.sh`：**TYPECHECK-EXIT=0** |
| `07-clean-55a874d-build-qt.log` | **干净 worktree @ 55a874d（captain 指定验证点）**：`bash scripts/build-qt.sh` 的完整输出与 exit code（见文件末尾 `CLEAN-55A874D-BUILD-QT-EXIT=…`） |

工作树位置：`target/t111-broken`（a4b8efd）、`target/t111-clean`（5b76318）、`target/t111-verify-55a874d`（55a874d）；验证后移除（captain 给的 `/tmp/...` 路径在跨调用间会被清掉，故放在 workspace 的 `target/` 下，效果相同且容器可挂载）。

## 4. 门禁（区分树状态 —— 这是 T111 的纪律重点）

| 树 | 门禁 | 结果 |
|---|---|---|
| **干净 worktree @5b76318**（`target/t111-clean`） | `bash scripts/build-qt.sh`（含根 ctest） | **BUILD-QT-EXIT=0**，ctest **27/27**（`03-…log`） |
| **干净 worktree @55a874d**（`target/t111-verify-55a874d`，captain 验证点） | `bash scripts/build-qt.sh`（含根 ctest） | **CLEAN-55A874D-BUILD-QT-EXIT=0**，ctest 27/27（`07-…log`） |
| 干净 worktree @a4b8efd（坏提交） | `vedit_syntax_kde` 构建 | **BUILD-EXIT=1**（`fatal error: syntax_directory.h`，`01-…log`） |
| 主工作树（脏树，仅供参考，不作为结论） | 根 ctest / typecheck | 27/27 / `TYPECHECK-EXIT=0` |

**教训（已记入报告）**：以后说“构建通过”必须注明是在**干净 worktree**上；主工作树的绿不算数。
`05/06` 的主工作树结果只用来证明"没有回归"，不当作修复证据。
