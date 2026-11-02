# T19 交付报告 — Slice 2：折叠 / 面包屑（AC-S2.2；AMENDMENTS A1/A2/A3）

- Cell: `cell-20261031T085543161Z` · Board task: **T19** · 冻结契约: `.mpd/cellwork/cell-20261031T085543161Z/contracts/folding-breadcrumbs.md`
- 执行: **Deep Worker (instance 2)** · 日期: 2026-11-01（Asia/Shanghai）
- 代码基线: `HEAD = 31a7c2d`（分支 `feat/qt6-ktexteditor-migration`，Slice 1 停点已关闭）；被测源码 sha256 见 `05-source-sha256.txt`
- 环境: pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / KTextEditor 6.24.0 / g++ 15；`QT_QPA_PLATFORM=offscreen`，无显示服务器）
- 结论: **A1/A2/A3 的三组判据在真实 `KTextEditor::View` + 真实 tree-sitter provider 上全部通过**；`full_count` 在按键/光标路径增量为 0、每次允许的重建恰 +1；A1f 的 on/off 只用 `firstDisplayedLine` 与 `scroll_max` 观测，期望值来自样本文本构造（不来自 registry）。两个 lane 的验收测试：折叠 **54 checks / 0 fail**，面包屑 **36 checks / 0 fail**。仓库 gate：根 ctest **19/19 PASS**、Qt6 C++ gate **PASS**；`typecheck` 的 **Rust clippy 步骤当前 RED，原因是另一条 lane 正在落地的 `crates/vd-vcs`（未提交）**，与本任务写域无关（证据见 §5）。

---

## 1. 交付物（全部在写域内）

### `src/syntax/folding/` — 折叠注入 lane

| 文件 | 职责 |
|---|---|
| `fold_range.h/.cpp` | 整行折叠区间模型 + **A1.1 四项自验**：① 非空/非默认（负行号 = 无 range）② `start<=end` ③ 两行都在 `[0, lines-1]` ④ 至少隐藏一行；返回具名拒绝原因（`invalid/inverted/out-of-document/zero-lines`） |
| `fold_injector.h/.cpp` | 注入器：**先校验再 `exec("fold")`**；被拒 range 不写 registry、不计注入数，诊断行含「原因 + 原始 range + 当时选区」（A1.2）；`exec()` 返回值只记录不判定；计数器 `folding.rebuild.full_count` / `folding.inject.count`；全量重建只接受 `FirstOpen` / `TreeSitterToggled` / `Reparsed` 三种触发，`onKeyPress()` / `onCursorMoved()` / `onScroll()` 是显式 no-op（A2.1）；`observeView()` 只读 `firstDisplayedLine(RealLine/VisibleLine)` 与 `verticalScrollBar()->maximum()`（A3.1） |
| `fold_source.h/.cpp` | 折叠来源接口 + 固定列表来源 |
| `syntax_fold_source.h/.cpp` | tree-sitter 来源：`SyntaxService` → `SyntaxProvider::foldRanges()`（`resources/queries/<grammar>/folds.scm`）；运行期开关关闭或 provider 不可用时产 **0 条**（A1f 的 off 态） |
| `tests/folding_tests.cpp` | 验收测试（A1 负向对照、A1.3 回归、A2.4 计数与耗时、A1f/A3.4 两态观测） |

### `src/workspace/breadcrumb/` — 面包屑 lane

| 文件 | 职责 |
|---|---|
| `breadcrumb_model.h/.cpp` | 语义路径模型：`setPath()`（外→内）、变化检测（`changeCount`）、`display()`/`describe()` |
| `breadcrumb_source.h` / `syntax_breadcrumb_source.h/.cpp` | 语义来源：`SyntaxProvider::breadcrumbs(tree, byte_offset)`（`resources/queries/<grammar>/structure.scm`）；**同一文本只解析一次**（光标移动复用缓存），文本变化恰好重解析一次 |
| `tests/breadcrumb_tests.cpp` | 模型 + 真实结构 query 的验收测试 |

两个 lane 各自带 `CMakeLists.txt` 与 README，可独立配置/构建/ctest。

## 2. A1 — range 自验（判据逐条）

模型级（`testRangeValidation`）：合法 range 通过；空/默认、倒置、越界（start 与 end 各一例）、零行 range 各自返回对应拒绝原因；批量校验保留顺序并把选区写进诊断（`selection=[20,25]`）。

产品路径（`testInvalidRangesDoNotFoldSelection`）：真实视图里**选中第 20–25 行形成非空选区**，注入空/倒置/越界/零行四种非法 range 后，同一 harness 的实测：

```
SNAPSHOT a1.baseline      first_real=0 last_real=61 first_visible=0 last_visible=61 hidden_in_viewport=0 scroll_max=939 cursor_line=0
SNAPSHOT a1.after-invalid first_real=0 last_real=61 first_visible=0 last_visible=61 hidden_in_viewport=0 scroll_max=939 cursor_line=0
CHECK a1.product.injected-none PASS
CHECK a1.product.inject-count PASS actual=0 expected=0
CHECK a1.product.registry-empty PASS actual=0 expected=0
CHECK a1.product.rejected-count PASS actual=4 expected=4
CHECK a1.product.observation-identical PASS
CHECK a1.product.selection-unchanged PASS
CHECK a1.product.diag-reason-invalid PASS / diag-reason-inverted PASS / diag-reason-out-of-document PASS / diag-reason-zero-lines PASS
```

负向对照（证明 A1.2 的必要性，不是“只断言有变化”）：

```
A1-CONTROL invalid_range exec_returned=1 scroll_max_before=939 scroll_max_after=934 hidden_before=0 hidden_after=5
CHECK a1.control.invalid-range-folds-selection PASS
A1-CONTROL inverted_range exec_returned=1 scroll_max_before=939 scroll_max_after=929
CHECK a1.control.inverted.exec-returned-true PASS
CHECK a1.control.inverted.exec-has-effect PASS
```

即：`Range::invalid()` 会**回落折叠用户选区**（5 行被隐藏）；倒置 range **返回 true 且仍然隐藏了 10 行**。所以 `Command::exec` 的返回值不能作为“折叠是否发生”的判据——本 lane 的判定只看校验结果与登记的注入，返回值仅入诊断日志。

## 3. A2 — 重建时机、计数与性能（判据逐条）

`testRebuildPolicyAndCounters`（1000 行 SystemVerilog 样本，tree-sitter provider 产出 200 条 fold range）：

| 判据（AMENDMENTS A2.2/A2.4） | 实测 | 结果 |
|---|---|---|
| 首次打开：`full_count` = 1 | 1 | PASS |
| 文档内 ≥20 条折叠 | 200 条（inject_count=200） | PASS |
| 全量重建 ≤150 ms（判据给的是 50 条） | 首次 **87 ms**；off→on 重建 **91 / 93 ms**（含 200 次 unfold + 200 次 fold） | PASS |
| 连续输入 200 字符后 `full_count` 增量 = 0 | `full_count 1 → 1`，`key_events=200`，registry 不变 | PASS |
| 光标移动 50 次 + 滚动：`full_count` 增量 = 0 | `cursor_events=50`，`full_count=1` | PASS |
| tree-sitter 开关切换：`full_count` 增量**恰为 1**，且 ≤150 ms | `1 → 2`，elapsed **9 ms**（off）；再切回 `2 → 3`，**91 ms** | PASS |
| 单次注入 p95 ≤2 ms | 50 次注入 `p95 = 524 µs`（另一轮 628 / 528 µs） | PASS |

计数口径（写进 README 与诊断行）：`folding.inject.count` = 通过校验并发出 `fold` 命令的次数（**不是** `exec` 返回 true 的次数）；`folding.rebuild.full_count` = 执行过“先展开 registered + 再注入新集合”的次数。

## 4. A1f / A3 — 两态观测（只用允许的三种方法）

`testTreeSitterToggleObservable`：1000 行样本（25 个 module × 40 行）。**期望值来自样本构造**（每个 module 折叠保留首行、隐藏其余 39 行 ⇒ 共 975 行），不取自 registry。

```
SNAPSHOT a1f.baseline first_real=0 last_real=61  first_visible=0 last_visible=61  hidden_in_viewport=0   scroll_max=939
SNAPSHOT a1f.on       first_real=0 last_real=1000 first_visible=0 last_visible=25 hidden_in_viewport=975 scroll_max=0
A1F state=on  tree_sitter_active=1 expected_hidden=975 expected_scroll_drop=939 observed_scroll_drop=939 hidden_in_viewport=975 ranges=200
CHECK a1f.on.scroll-drop-equals-expected PASS actual=939 expected=939
CHECK a1f.on.first-displayed-hidden-equals-expected PASS actual=975 expected=975
A1F state=off tree_sitter_active=0 expected_hidden=0 observed_scroll_drop=0 hidden_in_viewport=0 ranges=0
CHECK a1f.off.scroll-drop-zero PASS / a1f.off.hidden-in-viewport-zero PASS / a1f.off.injected-none PASS
```

方法 1 的正数控制（单个 40 行 module，隐藏 39 行）：

```
SNAPSHOT a1f.small-baseline first_real=0 last_real=61 first_visible=0 last_visible=61 hidden_in_viewport=0  scroll_max=19
SNAPSHOT a1f.small-folded   first_real=0 last_real=80 first_visible=0 last_visible=41 hidden_in_viewport=39 scroll_max=0
CHECK a1f.method1.hidden-in-viewport PASS actual=39 expected=39
```

一个必须说明的观测口径：`verticalScrollBar()->maximum()` 是“内容行数 − 视口行数”，所以它只能**饱和地**反映隐藏行数。期望落差按 `min(隐藏行数, 基准 scroll_max)` 计算（首次 939 = min(975,939)；小样本 19 = min(39,19)），精确隐藏行数由 `firstDisplayedLine(RealLine/VisibleLine)` 差值给出（975 / 39，与构造期望逐数相等）。

## 5. 面包屑（“随语义走”）

`testSemanticPath`（真实 `systemverilog` 结构 query，样本文本的行号由测试构造写死）：

```
PATH line=0 depth=1 display='m0' describe='module:m0'
CHECK path.module-header.depth PASS / kind PASS / name PASS
CHECK path.function-body.depth PASS actual=2 ; kinds=[module,function] ; names=[m0,inc] ; start-line=3
CHECK path.always-body.depth PASS actual=1 （always 不是结构节点，路径停在 module）
CHECK path.second-module.name PASS （m1）
CHECK path.outside.depth PASS actual=0 （endmodule 之后的注释行没有语义容器）
CHECK source.parse-count-after-moves PASS actual=1 （5 次光标位置的查询只解析一次）
CHECK source.parse-count-after-reparse PASS actual=2 （文本变化恰好重解析一次）
CHECK path.third-module.name PASS （新追加的 m2 立刻可见）
FOLLOW header='m0' function='m0 > inc' changes=3 updates=4
CHECK source.disabled.semantic-inactive PASS / path.disabled.depth PASS actual=0 / parse-count PASS actual=0
```

- 路径元素来自 `structure.scm` 的 `@module/@function/...` + `@name`，不是文本猜测；
- tree-sitter 关闭时**不发明路径**（空路径、0 次解析）；
- 面包屑 lane **不链接** `src/syntax/folding`，也没有任何折叠 include：更新面包屑不可能触发 A2.1 的折叠全量重建。折叠 lane 侧另有 50 次光标移动 `full_count` 增量 0 的断言。

## 6. 我实际运行过的命令与观察（headless）

```sh
# 折叠 lane（真实 KTextEditor::View + 真实 tree-sitter provider）
cmake -S src/syntax/folding -B target/syn-folding -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # exit 0
cmake --build target/syn-folding --parallel                                                    # exit 0
QT_QPA_PLATFORM=offscreen ./target/syn-folding/vedit-folding-tests
#   FOLDING-TESTS ok=1 checks=54 failures=0 skipped=0
ctest --test-dir target/syn-folding -R vedit-folding-tests --output-on-failure
#   1/1 Test #1: vedit-folding-tests ... Passed 0.66 sec ; 100% tests passed, 0 tests failed out of 1

# 面包屑 lane（QCoreApplication，无需显示）
cmake -S src/workspace/breadcrumb -B target/ws-breadcrumb -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo # exit 0
cmake --build target/ws-breadcrumb --parallel                                                  # exit 0
./target/ws-breadcrumb/vedit-breadcrumb-tests     # BREADCRUMB-TESTS ok=1 checks=36 failures=0
ctest --test-dir target/ws-breadcrumb -R breadcrumb --output-on-failure
#   1/1 passed（整棵 lane 构建含 grammars/provider 时 4/4）

# 仓库声明的 gate（本任务未改根构建，两种 lane 独立构建）
ctest --test-dir target/qt6-build --output-on-failure
#   100% tests passed, 0 tests failed out of 19 ; CTEST-EXIT=0
VEDIT_SKIP_IMAGE_BUILD=1 bash build/gates/qt6-cxx.sh --no-test
#   [qt6] PASS ... ; QT6-CXX-EXIT=0
VEDIT_SKIP_IMAGE_BUILD=1 bash build/gates/typecheck.sh
#   Qt6 段 PASS；Rust clippy 段失败：`crates/vd-vcs`（未提交，另一条 lane 在飞）
#   error: could not compile `vd-vcs` (lib) due to 7 previous errors ; TYPECHECK-EXIT=101
```

**关于 typecheck 的 RED（如实说明）**：失败点是 Rust clippy 对 `crates/vd-vcs` 的 7 条 `expect_used` 等告警，
工作树里 `crates/vd-vcs/src/lib.rs`、`crates/vd-vcs/src/runner.rs`、`crates/vd-vcs/src/service.rs`、
`crates/vd-search/src/lib.rs`、`crates/vd-ffi/src/{search,vcs}.rs` 都是**其他 lane 的未提交改动**
（`04-root-gates.log` 记录了 `git status`）；本任务的写域（`src/syntax/folding/`、
`src/workspace/breadcrumb/`、`evidence/slice2-fold/`）里没有任何 Rust 文件。Qt6 C++ 门与根 ctest 均 PASS。
第一次 typecheck 运行还遇到过一个短暂的“vedit-app 为空文件”竞态（共享工作树里另一进程同时构建），
重跑后该段 PASS——同样记录在 `04-root-gates.log`。

## 7. 未验证 / 边界（如实说明）

- **未接入 `vedit-app`**：把注入器接到编辑内核的实时视图、把面包屑接到当前光标/重绘，属于
  `src/editor` 的集成工作；冻结契约的写域只有两个 lane 目录 + `evidence/slice2-fold/`，
  所以本任务交付的是 **lane 级**证据（真实 KTextEditor 视图 + 真实 tree-sitter provider），
  不是整机点击/截图。集成任务可复用 `FoldInjector` / `SyntaxBreadcrumbSource` 的公开头文件。
- **A4 残项不冒充**：`.sv` 关闭态由自有 LGPL XML 提供 ≥1 折叠（A4.5）归 `a4-fold-residual`（T29）；
  本 lane 的 A1f off 态只声明“tree-sitter 来源关闭 ⇒ 本来源 0 条、观测回到 0”。
- **无 GUI 视觉结论**：全部在 `QT_QPA_PLATFORM=offscreen` 下运行；没有任何“看起来对”的判断。
- **观测方法的已知口径**：`scroll_max` 会饱和（见 §4），因此精确隐藏行数用方法 1（`firstDisplayedLine`），
  方法 2 用 `min(隐藏行数, 基准 scroll_max)` 做期望；这写进了 README 与测试断言。
- **性能数字的环境**：pinned 镜像 `vedit-qt6-build:local`，实测绝对值 87–93 ms（200 条重建）与
  p95 ≈ 0.52–0.63 ms；判据来自 `evidence/r2-fold/decision.md` §3.4（50 条 34/54 ms、单次 p95 1.08 ms），
  本机重建含 200 条折叠仍 ≤150 ms、单次注入仍 ≤2 ms。
- **未做**：多视图/多文档共享同一 document 的折叠命令路由、折叠态与搜索/替换的交互、
  超大折叠数（1000+）的内存与总耗时——都不在本契约判据内，未验证。

## 8. 证据清单（`evidence/slice2-fold/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 采集时间、HEAD、镜像 id、写域 `git status`、其余 lane 的改动清单 |
| `01-build-folding.log` | 折叠 lane 的 configure/build 原始输出 |
| `02-run-folding.log` | 折叠验收测试全程输出（CHECK/SNAPSHOT/A1-CONTROL/A2/A1F/MEASURE）与最终 `ok=1 checks=54 failures=0` |
| `03-run-breadcrumb.log` | 面包屑测试输出（PATH/FOLLOW/CHECK）与 `ok=1 checks=36 failures=0`、ctest |
| `04-root-gates.log` | 根 ctest（19/19 PASS）、`qt6-cxx.sh --no-test`（PASS, exit 0）、typecheck（Qt6 PASS / Rust clippy 因其他 lane 未提交的 `crates/vd-vcs` 而 RED, exit 101）及竞态说明 |
| `05-source-sha256.txt` | 两个 lane 全部源文件的 sha256 |

## 9. 提交

未 commit/push（按任务要求由 Lead 统一提交）。本任务新增：`src/syntax/folding/`（9 个文件）、
`src/workspace/breadcrumb/`（8 个文件）、`evidence/slice2-fold/`（6 个文件）；未改任何既有文件。
