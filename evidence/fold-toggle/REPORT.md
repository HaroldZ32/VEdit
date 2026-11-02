# T120 — 折叠"能开不能叠回" / 折叠后崩溃：调查结论（代码已回退，绿色优先）

collected_at: 2026-11-01T19:32:00+08:00
git_head: 见同目录日志的 C9 头
## 已确认的事实

1. **复现器已建立并验证**（`fold_interaction_tests.cpp`，未提交、已随回退删除）：真 KTextEditor view +
   真 provider，产品路径注入 200 folds 后用 **KTextEditor 自己的 `fold`/`unfold` 命令**模拟用户。
   offscreen 下 A–J 序列（unfold→refold、重复 unfold/refold、重叠 fold、隐藏行上 unfold 等）
   **都没有崩溃**；`setCursorPosition` 到被折叠隐藏的行在 offscreen 也不崩。
2. **真实崩溃条件（T117 已用栈确认，本机复刻其逻辑）**：折叠后把光标设到被隐藏的行 →
   KTextEditor 内部空指针（真机 xcb 下崩，offscreen 不崩，所以我们的无头测试从未覆盖）。
3. **我们自己的 release 路径确实会制造"未完全展开"**（新发现，决定性）：
   `FoldInjector::unfoldRegistered()` 旧实现对每个 registry Range 先 `setCursorPosition(startLine)` 再
   `unfold`；在 5 行小文件上实测：`unfold target=0 ret=0` → **一条 fold 没展开**，随后
   `releaseFolds` 仍把 registry 清空，第二次打开文件在"文档仍带折叠"的状态下 `setText()` → 崩溃/状态错乱。
   这就是 search-jump（第二次打开）selftest 在我改动后段错误的原因，也解释了"用户折叠后再次操作会死"。
4. **为什么 27/27 ctest 没抓到**：折叠证据只断言注入条数/隐藏行数；没有一条测试
   "注入 → 执行用户折叠/展开 → 再次 rebuild/release"的顺序。T117 的 blind-spots 同样指出这一点。

## 我尝试但**已回退**的修复

- `unfoldRegistered()` 改为**外层优先**排序 + 只对"未被仍折叠区间覆盖"的 startLine 设光标 +
  恢复光标；对不可恢复情形拒绝把光标设到隐藏行。
- 结果：在 1000 行样本上 release 从 hidden=975 完整回到 0，且反向控制（旧的 inner-first 顺序）
  模拟出 175 次"目标行已隐藏"的移动 → 证明守卫真的有意义（`hidden_moves 0 vs 175`）。
- **但这个修复与 T116 的窗口注入/延后打开改动在 search-jump 路径上互相干扰，最终 root ctest 变红**；
  按 captain "绿树优先"的要求，全部产品代码已回退（当前树 30/30 绿）。

## 下次接手需要的顺序（建议）

1. 只做 `FoldInjector::unfoldRegistered()` guard（外层优先 + 可见性守卫 + 恢复光标），不掺 T116 改动；
   在干净 worktree 跑 30/30 + search-jump。
2. 独立分支做 T116 的窗口注入/大文件模式。
3. 补真交互断言：
   ```
   注入 N 条嵌套 fold → setCursorPosition 到隐藏行（断言不崩）
   展开 → 折叠 → 再展开（每步断言 hidden/scroll 真变化）
   用户折叠后再 rebuild/release（断言不崩、状态正确）
   ```
4. 反向控制：用旧 inner-first 顺序的模拟计数（本报告已验证 175 次隐藏目标）作为 red 侧。

## 对用户的一句话（真机上应看到什么）

点模块折叠标记 → 该模块体应收起为一行、行号/滚动区域随之变化；再点一次应重新展开。
当前构建在这条路径上会崩/被错误地再次展开，**需要先落地上述 guard + 真交互断言后再给用户复验**。

---

## 更新（真交互断言已交付）：命令级别折叠/展开双向可用

新增 `src/syntax/folding/tests/fold_toggle_tests.cpp`（CMake 目标 `vedit-fold-toggle-tests`，加入 ctest），
用**KTextEditor 自己的 `fold`/`unfold` 命令**模拟用户，每一步都断言隐藏行数真的变化：

| 步骤 | 观测 | 断言 |
|---|---|---|
| 产品注入 200 区间 | hidden=975 | `injected.hidden-positive` PASS |
| 用户 unfold 模块 0 | hidden 975 → 950 | `unfold.really-unfolded` PASS |
| **用户 fold back 模块 0** | hidden 950 → **975** | `fold-back.really-folded` **PASS** |
| 再 unfold 一次 | 975 → 950 | `unfold-again.really-unfolded` PASS |
| 用户在这段可见区新建 fold | 950 → 954 | `user-created-fold.really-folded` PASS |
| 对已折叠区间再 fold | 954 → 954 | `already-folded.no-op` PASS（KTextEditor 语义） |
| 用户动作后 rebuild | 200 注入、hidden=975、不崩 | PASS |
| rebuild 后再次 unfold | 975 → 954 | `after-rebuild.unfold-really-unfolded` PASS |

`FOLD-TOGGLE ok=1 checks=9 failures=0`（`01-toggle-test.log`，C9 头）。

## 现象分类结论

- (a) 标记不显示 / (b) 点了没反应 / (c) 回弹 / (d) 计数错乱：**在命令层都无法复现**——
  注入器路径的双向折叠是好的（975→950→975）。
- 因此真机"叠不回去"落在 **GUI 折叠标记的鼠标路径**（KatePart 的 gutter marker / 命令注入创建的
  fold 在展开后 marker 状态），而不是 `FoldInjector` 的注入/释放 API。无头环境无法驱动 gutter 点击，
  需要真机日志确认：已在 `FoldBreadcrumbBinding` 增加 **`VEDIT_FOLD_TRACE=1`** 的可选日志
  （rebuild 触发、窗口注入），用户点击折叠标记时若日志里**没有** rebuild，即可证明不是我们的重建逻辑。
- 另：根 ctest 本轮被**其它 lane 的未提交编译错误**阻塞（`src/workspace/search/search_selftest.cpp`
  `check()` 参数个数不匹配），只构建了本任务目标 `vedit-fold-toggle-tests` / `vedit-edit-core-selftest`
  （BUILD-EXIT=0），未宣称整树绿。

## 真机上应看到什么

点模块折叠标记 → 模块体收成一行、行号/滚动范围变化；再点一次应重新展开（命令层已验证两个方向都成立）。
若真机仍不能叠回，请用 `VEDIT_FOLD_TRACE=1 vedit-app` 复现并回传日志：日志没有 rebuild 行 ⇒ 缺陷在
gutter 标记/KatePart，不在本任务的折叠注入路径。
