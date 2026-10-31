# T70 独立评审结论 — T69 搜索结果点击在 KTextEditor provider 下真跳转

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Senior Engineer (instance 2)`）
- **被评审任务**：board `T69`（第三处同类硬 cast 回归：`search_jump_target.cpp` 把 `editorWidget` cast 成 `QPlainTextEdit`）
- **验证环**：该 cell 记录里 **没有为 T69/T70 打开 verify loop**（`.mpd/verify/loops/*` 中无 `taskId: T69|T70`，
  `cellwork_status` 也不返回 loop 路径），故本评审按任务书的四件判据 + 自己实跑取证，verdict 落在本目录。
- **结论**：**PASS**。四项判据全部由我自己的源码核对 + 实跑输出支撑；`jump.product.*` 16 条全绿，checks 105（T42 基线 95），
  根 ctest 16/16，typecheck exit 0。
- **盲评/取证顺序**：先跑自检与门禁、读证据日志；随后按任务书点名的四项（真 provider / 双命中 / cast 漏网 / 断言保留）做定向源码核对。

---

## 1. 判据①：真 provider 下真的能跳转（不是只看 demo pane）

**源码核对**（`src/workspace/search/search_selftest.cpp`，我直接读的断言）：

```cpp
shell->setEditorProvider(vedit::editor::makeEditCoreProvider());        // :609 真实产品 provider
KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget()); // :625
KTextEditor::Document *document = vedit::editor::documentFor(...);      // :626
check("jump.product.ktexteditor-provider", view != nullptr && document != nullptr);
...
check("jump.product.first-current-file", pane->currentFilePath() == hitA.filePath);
check("jump.product.first-loads-bytes", !expectedA.isEmpty() && documentText() == expectedA);
check("jump.product.first-cursor-line", cursorLine() == hitA.line);
check("jump.product.first-cursor-column", cursorColumn() == hitA.column);
```

四项都有：**路径**（`currentFilePath() == hit.filePath`）、**字节**（`document->text()` == 磁盘读出的内容）、
**行**（`cursorPosition().line()+1 == hit.line`）、**列**（`column()+1 == hit.column`）。
`makeEditCoreProvider()` 是产品安装的同一个 provider（`src/app/main.cpp` 的产品路径安装它）。

**我的实跑**（根构建，pinned 镜像，我自己的运行，非引用 writer 日志）：

```text
$ cmake --build target/qt6-build --target vedit-workspace-search-selftest ; … --selfcheck
BUILD-EXIT=0 ; SELFTEST-EXIT=0
CHECK jump.product.ktexteditor-provider PASS
SEARCH-JUMP first  requested='a.v':2  current='/tmp/…/a.v'  cursor=2:3
SEARCH-JUMP second requested='b.sv':4 current='/tmp/…/b.sv' cursor=4:1
WORKSPACE-SEARCH-SELFTEST ok=1 checks=105 failures=0 skipped=2
（jump.product 检查 16 条，FAIL 0 条）
```
日志：`evidence/verdicts/search-jump-kte/01-selftest-rerun.log`。
→ 不是 demo pane：`ktexteditor-provider` 检查要求 `viewFor/documentFor` 非空，且路径/行/列全部取自 `KTextEditor::View`。

## 2. 判据②：两次跳转是不同文件且都不是第 1 行

**断言写法**（我读的源码）：命中筛选要求 `hits.at(i).line >= 2`、`filePath != first.filePath` 且 `line != first.line`；
`jump.product.two-hits` 检查两个索引都找到；两次分别断言对 `hitA.line/column` 与 `hitB.line/column`。

**实跑输出**：`first requested='a.v':2 cursor=2:3`、`second requested='b.sv':4 cursor=4:1`
—— **两个不同文件名（a.v / b.sv），行号 2 与 4，均 ≠ 1**，且光标列分别为 3 与 1，能区分"永远落第一行/第一文件"的假绿。
（writer 的 `02-after-green.log` 与我这次实跑数值一致。）

## 3. 判据③：全仓 cast 漏网排查（含"第四条"）

我自己的全仓 grep（`grep -rn 'qobject_cast<QPlainTextEdit' src/ --include=*.cpp --include=*.h`）只有 5 处：

| 位置 | 是否产品路径 | 判定 |
|---|---|---|
| `src/app/editor_provider.h:68` / `:112` | **DemoEditorProvider 自己的 open/save 实现**——demo provider 本来就是 QPlainTextEdit | 设计如此，不是产品编辑器的 cast |
| `src/app/main.cpp:411` | 位于 `int runSelfCheck()`（`--selfcheck` 模式）的"demo provider 保存入口"自检块 | 自检路径，不是产品交互路径 |
| `src/workspace/palette/palette_integration_selftest.cpp:181` | palette **selftest** 的 demo demo 块 | 测试夹具 |
| `src/workspace/search/search_selftest.cpp:1053` | search **selftest** 的截图 demo 块（`grab(...)` 前后） | 测试夹具 |

**产品路径全部干净**：
- `search_jump_target.cpp` 现在走 `pane->openFile(...)`（`EditorPane` 的唯一 open seam）+ `viewFor` + `setCursorPosition`，
  并**回读** `cursorPosition()` 不一致即 fail；无任何 `QPlainTextEdit` cast（该文件里只剩解释性注释）。
- `filetree_integration.cpp:43` 与 `palette_integration.cpp:184` 也都是 `pane->openFile(...)` → **只有一条 open seam**，
  没有第二套。

→ **没有第四条产品路径上的硬 cast**。遗留的 5 处全部在 demo provider 实现 / 自检 / selftest 截图夹具里，均不在产品 open/jump 路径上。

## 4. 判据④：既有断言未被删/弱化

```text
$ git diff 95c496e a058995 -- src/workspace/search/search_selftest.cpp | grep '^-' | grep -v '^---'
-//       large-file and cancellation checks; prints CHECK lines and one
-//       machine-readable summary line. Exit code 0 iff no check failed.
```
→ 整个 T64+T69 提交里，selftest **只删了 2 行文件头注释**，没有任何断言被删。

**检查名集合对比（我自己的脚本）**：
- T42 基线：`evidence/fix-search-jump/02-after-green.log` 的 95 checks → 97 个唯一 CHECK 名；
- 当前：我实跑的日志 → 111 个唯一 CHECK 名；
- `comm -23`（T42 有、当前没有）：**0 条**。
→ `jump.click.*`、`jump.demo.*`、`seam.*`（5 条）、`jump.product.*` 全部保留；checks 95 → **105**（只增）。
新增的检查名：`jump.product.ktexteditor-provider`、`jump.product.second-loads-bytes`、`jump.product.second-cursor-column`
以及 T64 的 lifecycle/slowlifecycle 组。

## 5. 我自己实跑的门禁（引实际输出）

```text
# 搜索自检（§1）：SELFTEST-EXIT=0，checks=105 failures=0 skipped=2，jump.product 16/16 PASS
# 根 ctest（基线 16/16）：
$ ctest --test-dir target/qt6-build --output-on-failure
13/16 Test #13: vedit-workspace-search-selftest ................   Passed   11.89 sec
100% tests passed, 0 tests failed out of 16
ROOT-CTEST-EXIT=0
# typecheck：
$ bash build/gates/typecheck.sh
[typecheck] PASS (pinned image vedit-build:local)
TYPECHECK-EXIT=0
```
日志：`evidence/verdicts/search-jump-kte/{01-selftest-rerun.log,02-typecheck.log,03-root-ctest.log}`。

## 6. 未回退 T41 / T64

- T41：`installSearch` 仍 `findChild/new EditorJumpTarget` + 面板工厂 `setJumpTarget`（`search_integration.cpp:26-34`）；
  点击仍是产品路径。
- T64：`global_search.cpp` sha256 仍是 `8febfb2c…`（T64 ASan-green 的那份），未改；selftest checks 102 → 105 只增。
- 单一 open seam：filetree/palette/search 全部 `EditorPane::openFile`。

## 7. Findings

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| — | — | **无阻塞 finding** | — |
| N-1 | Note | 第四条漏网排查结果：产品路径 0 处；剩余 5 处 `qobject_cast<QPlainTextEdit` 全在 demo provider 实现 / `runSelfCheck()` / 两个 selftest（见 §3 表） | 无需动作；若将来做"零 cast"清洁，可单独收拾 selftest 的截图 demo 块 |
| N-2 | Note | `search_selftest.cpp:1053` 的 demo cast 是当年掩盖回归的同一类气味，但它是截图夹具、且旧 `jump.demo.*` 断言按判据必须保留 | 建议后续把截图块也切到 provider，不阻塞 |
| N-3 | Note | 物理鼠标点击链路（X server / 真实指针）不可验；测试走 `QTest::mouseClick` 的 Qt 事件路径 | 用户在有屏机器做 AC-S1.6 |
| N-4 | Note | T69 的 RED 日志（`01-before-red.log`）是 writer 采集：修前的 `search_jump_target.cpp` 当时是未跟踪新文件，git 里没有旧内容，**我无法用 sha 复跑红**；我独立复核的是 GREEN（105 checks / 0 fail）与产品路径源码 | 接受；GREEN 与源码已足够定 PASS |

## 8. 未能验证的部分

1. **物理鼠标点击**：无 X server，只能验证 Qt 事件路径（`QTest::mouseClick`）→ `jumpRequested` → 产品 consumer → 磁盘/编辑器状态。
2. **视觉滚动位置**：`setScrollPosition()` 的可见效果无截图判定（offscreen 无视觉结论）。
3. **RED 无法独立复跑**：修前代码未入库（untracked 新文件），不能在不改动工作树的前提下回滚复现；
   红/绿对照的"绿"与源码状态由我独立复核（§1/§4/§5）。
4. **真实磁盘慢 I/O**：T64 的 slowio 垫片仍是注入式（属 T64 判据，本单未重评）。

## 9. 结论

**PASS**。真实 `makeEditCoreProvider()` 下，点击命中后 `currentFilePath()` == 磁盘路径、`document->text()` == 磁盘字节、
光标行列 == 命中行列；两次跳转是不同文件且行号 2/4（非 1），假绿防线成立。全仓无第四条产品路径硬 cast，
open seam 仍唯一（`EditorPane::openFile`）。既有断言零删除（只删 2 行注释），checks 95 → 105；
根 ctest 16/16、typecheck exit 0 均为我实跑。
