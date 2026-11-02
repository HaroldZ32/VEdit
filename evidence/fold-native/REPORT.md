# T128 — 折叠改 A：删除注入层，KDE 原生折叠

collected_at: 2026-11-02T10:48:00+08:00
git_head: 见同目录日志 C9 头
## 做了什么（删除清单）

- 删除 `src/syntax/folding/fold_injector.{h,cpp}`（`exec("fold")` 注入、registry、
  `unfoldRegistered()`、光标移动；A1-A3 实现）
- 删除 `src/syntax/folding/tests/folding_tests.cpp`（依赖注入器的旧验收；hidden-975/registry-200 口径退役）
- `FoldBreadcrumbBinding` 重写为**只做面包屑**（保留 `onDocumentOpened`/`setTreeSitterEnabled`/`notifyReparsed`/
  `breadcrumb*`/listener），删除 `releaseFolds`/`injector()`/折叠 source 字段
- provider：删除 open 前的 `releaseFolds()`；createEditor 时显式 `setConfigValue("folding-bar", true)`，
  保证 gutter 标记默认可用；打开不再自动折叠
- app selfcheck 的 `fold.wiring.*`（依赖注入器）改为 `fold.native.*`：无自动折叠、面包屑不变、
  二次打开与光标移动不崩
- AGENT.md：新增 §10 折叠策略（KDE 原生）+ 删除清单/保留清单

## 新验收（真交互，替代旧口径）

| 项 | 结果 |
|---|---|
| ctest `vedit-fold-gutter-click`（xvfb+xcb，T127 探针） | 双向点击：hidden 0→38→0，scroll_max 939→901→939；负控制 changed=0；折叠时移动光标到隐藏行不崩 |
| ctest `vedit-native-fold-tests` | 10k 行 open **116 ms（≤2 s）**；打开无自动折叠；native fold 生效；同 pane 二次打开 + 光标扫描不崩 |
| 主树 ctest | **31/31 Passed** |
| `vedit-app --selfcheck` | **194/0**（fold.native.* 全绿，面包屑 `module:m0 > function:inc` 保留） |
| typecheck | **TYPECHECK-EXIT=0** |

## 保留但无产品消费方（建议下一步删除）

`fold_range.*`、`fold_source.*`、`syntax_fold_source.*`、`ksyntax_fold_source.*` 与
`vedit_syntax_folding` 目标：删除注入后产品已不再使用，只有测试/探针链接。建议在后续清理任务
中连同 editor CMake 的接线一并删除。

## 诚实缺口

- **干净 worktree 未完成认证**：本次复用主树的 `libvd_ffi.a` 做 clean-worktree build，在
  无关 lane（diagnostics selftest）出现链接错误，未定位/未修复（不是本任务文件）；干净树的
  完整 `scripts/build-qt.sh`（含 Rust）仍待执行。
- 反向控制（“把注入层加回去 → 二次打开重新崩”）没有以可提交测试实现：注入层已删除，
  重加需恢复旧代码；T121/T122 的崩溃证据（`evidence/fold-unfold-guard/`）作为历史依据引用，
  本轮未重跑该实验。
