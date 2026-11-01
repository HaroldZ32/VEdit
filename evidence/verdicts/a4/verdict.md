# T30 独立评审结论 — T29 `a4-fold-residual`：1k 行 `.sv` 关闭态折叠（期望值来自自有 XML 的 beginRegion）

- **评审者**：Reviewer 4（cell `cell-20261031T085543161Z`；verifier ≠ writer `Senior Engineer 3` / Deep Worker lane）
- **被评审任务**：board `T29`（Slice 3，AC-S3.5 折叠部分）；冻结契约 `.mpd/cellwork/cell-20261031T085543161Z/contracts/a4-fold-residual.md`
- **验证环**：`loop-20261031T095153-63590f`（`mpd_verify_seat` 已绑定）
- **结论**：**PASS**（bound seat）。AC-S3.5 的判据（1k 行 + tree-sitter 关闭态 + A3.1 观测之一 + ≥1 折叠 + 期望值来自自有 XML）
  经我**自己复跑 + 源码审计**全部成立；F2 交付库口径也由我 `ldd` 证明并复跑一致。
  两点必须记：**当前共享树**的 typecheck/根 ctest 是红的，但原因是 **T94 在飞的 `src/app/main.cpp` printf 格式错**（与 T29 文件无关）；
  **T66**（T45 的 F2 证据重录）仍未关门。（**状态更新 2026-11-01，Reviewer 3，依据 T100 审计：该条已关闭——T66 已交付并经审计
  （`evidence/fold-evidence-final/03-T45-upgrade-audit.md`），`fix-xml-folding` verdict 已同步升级为无保留 PASS；见 F-3。下文原历史表述保留。**）

---

## 1. 我实际跑的命令与观测

### 1.1 lane 折叠测试（我自己重跑，容器 `vedit-qt6-build:local`，offscreen）

```text
$ QT_QPA_PLATFORM=offscreen target/a4-fold-build/vedit-folding-tests          （宿主/发行版库）
HOST-EXIT=0
A4 closed_state tree_sitter_active=0 trigger=first-open injected=200 rejected=0
   expected_hidden=975 observed_hidden=975 scroll_drop=939 expected_scroll_drop=939
FOLDING-TESTS ok=1 checks=75 failures=0 skipped=0

$ ldd target/a4-fold-build/vedit-folding-tests | grep -i syntaxhighlight
libKF6SyntaxHighlighting.so.6 => /usr/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6

$ LD_LIBRARY_PATH=<F2>/install/lib/x86_64-linux-gnu ldd …/vedit-folding-tests | grep -i syntaxhighlight
libKF6SyntaxHighlighting.so.6 => …/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/libKF6SyntaxHighlighting.so.6
$ LD_LIBRARY_PATH=<F2> QT_QPA_PLATFORM=offscreen targets/a4-fold-build/vedit-folding-tests
F2-EXIT=0  ；同样的 A4 closed_state 行 ；FOLDING-TESTS ok=1 checks=75 failures=0
$ ctest --test-dir target/a4-fold-build -R vedit-folding-tests --output-on-failure
1/1 Test #1 Passed ；100% tests passed
```

→ **F2 交付库口径成立**：`ldd` 证明该次运行的 `libKF6SyntaxHighlighting` 确实来自
`target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu/`（sha256 `aac288a1…`），且输出与发行版库逐行一致。
（writer 的 `01-lane-tests.log` run B 只写了 F2 的 sha 与"无内嵌定义"标记，**没有 `ldd` 行**——正是 T46 抓过的
"标签 ≠ 链接事实"陷阱；这次由我用 `ldd` 补上。）

### 1.2 自有 XML 的折叠规则（T45 修复后的形态）

```text
$ grep … resources/syntax/vedit-systemverilog.xml
<list name="veditFoldBegin">  ← 13 项（module interface program class package generate function task case casex casez randcase begin）
<list name="veditFoldEnd">    ← 10 项（endmodule endinterface endprogram endclass endpackage endgenerate endfunction endtask endcase end）
<keyword attribute="Keyword" context="#stay" String="veditFoldBegin" beginRegion="veditFold"/>
<keyword attribute="Keyword" context="#stay" String="veditFoldEnd"   endRegion="veditFold"/>
<language name="SystemVerilog" … section="Hardware" … author="VEdit" license="LGPL-2.1-or-later">
```

### 1.3 期望与观测的对账

- 样本：测试内生成 `25 × 40 = 1000` 行 SystemVerilog，每模块 8 条折叠（module/function/always_ff begin/if begin/else begin/always_comb begin/generate/for begin），
  **200 条**；测试用**独立写死的构造期望**（不是从注入器读）逐条比较：`a4.ranges.match-construction PASS`。
- 期望来源：`A4 expected_source=…/resources/syntax/vedit-systemverilog.xml region=veditFold ranges=200 unbalanced=0`；
  `a4.source.identity-is-ours`（name=SystemVerilog / section=Hardware / **author=VEdit**）、`region-veditfold`、
  `begin-list-from-xml` / `end-list-from-xml` 逐项断言 = XML 内容。**没有从应用 registry 取值**。
- 关闭态：`SyntaxService::setTreeSitterEnabled(false)` + `a4.tree-sitter-switch-off` + 来源 `treeSitterActive()==false`；
  该来源根本不经过 tree-sitter/编辑器 repository（见 §2）。
- A3.1 观测：`observeView()` 直接用真实 `KTextEditor::View` 的
  `firstDisplayedLine(RealLine/VisibleLine)`、`lastDisplayedLine(...)`、`verticalScrollBar()->maximum()` 计算
  `hidden_in_viewport = (real span) − (visible span)`；折叠后 975、`scroll_max` 939→0（落差 939 = min(975,939)）。
- A1 未被绕过：`a4.ranges.all-valid`（200 条全部通过四项自验）、`a4.invalid.still-rejected`（`{5,5}` 仍被拒）。

## 2. 源码审计（关键：期望确实来自自有 XML，不是 registry）

- `src/syntax/folding/ksyntax_fold_source.cpp`：`QFile` + `QXmlStreamReader` 读 XML；采集 `<language>` 的
  name/section/author、`<list>` 项、`<keyword String=… beginRegion=…>` / `endRegion=…` 映射；
  偏好 `veditFold` 区域；**没有** `KTextEditor::Repository`/`definitionForName`/应用 registry 的读取。
- `ranges(text)`：逐行扫描、关键字分词、**平衡栈**配对，`//` 行注释与 `"…"` 字符串被跳过；输出整行 `FoldRange`，并统计不平衡数。
- `treeSitterActive()` 恒 `false`：关闭态折叠不依赖 tree-sitter。

## 3. 门禁

| 命令 | 观测 | 归属 |
|---|---|---|
| lane ctest（`target/a4-fold-build`） | **1/1 Passed**（我自己重跑） | ✅ T29 |
| lane 测试二进制 | **checks=75 failures=0**，exit 0（宿主库与真 F2 两次，我自己重跑） | ✅ T29 |
| `bash build/gates/typecheck.sh`（采集时 14:03） | `TYPECHECK-EXIT=0`（`evidence/a4-fold/04-typecheck.log`，含 pinned image 横幅） | ✅ 采集时 |
| 根 ctest（采集时 14:03） | `19/19 Passed`（`03-root-ctest.log`） | ✅ 采集时 |
| **当前共享树** typecheck | **RED**（`ev-20261101T062206-e7d94b`）：`src/app/main.cpp:794-800` `printf` 格式与参数类型不匹配（`-Werror=format=`），属于 **T94 在飞的 FOLD-WIRING 集成**（该文件不在 T29 写域） | ⚠ 外部 |
| **当前共享树** 根 ctest | **RED**：`#10 vedit-app-shell-selftest (Failed)`、`#12 vedit-folding-tests (Not Run)`（根构建的 folding 测试二进制未产出）——与 T94 的在飞重建/改动一致 | ⚠ 外部 |
| docs 门禁（我产出） | `ev-20261101T062438-de190f`（exit 0） | ✅ |

## 4. 前置与证据链

- **T45（fix-xml-folding）**：`evidence/verdicts/fix-xml-folding/verdict.md` = **PASS（有条件）**（**2026-11-01 已升级为无保留 PASS**）：XML 新写法在**真 F2** 上
  由该评审独立测得 Verilog 7/7、SV 11/11、before 0/0；条件是旧的 `syn-kde-data/10/11` 探针日志其实是 DISTRO 测量、
  待 **T66** 重录。→ XML 修复本身成立（前置满足），T66 仍未关门（见 F-3；**已于 2026-11-01 关闭**）。
- 本任务证据直接读自有 XML 并断言 author/section，**不依赖** T66 要重录的那两份旧日志，故 T29 的结论不被 T66 阻塞。

## 5. Findings / Notes

| ID | 严重度 | 内容 | 处理建议 |
|---|---|---|---|
| **F-1** | **Low** | `evidence/a4-fold/01-lane-tests.log` 的 run B 只写了 F2 的 sha 与"无内嵌定义"标记，**没有 `ldd` 证明实际链接的是 F2**（T46 抓过的"标签 ≠ 链接事实"模式）。我已用 `ldd` 亲自证明并复跑一致（§1.1） | 把 `ldd` 行补进归档日志 |
| **F-2** | **Medium（外部，非 T29）** | 当前共享树 typecheck + 根 ctest 红：`src/app/main.cpp:794-800` 的 `FOLD-WIRING` `printf` 格式错（`-Werror`），以及根构建里 `vedit-folding-tests` 二进制未产出（Not Run）——都是 **T94** 在飞的集成改动，不在 T29 写域 | T94 修 printf 并重建后，树会恢复绿；不该记到 T29 |
| **F-3** | Note | **T66**（T45 的 F2 探针证据重录）仍 open，T45 是"有条件 PASS" | captain 推进 T66，别让条件 PASS 沉底。**→ 已关闭（2026-11-01，Reviewer 3，依据 T100 审计）**：T66 已交付并经审计（`evidence/fold-evidence-final/03-T45-upgrade-audit.md`：`-F2` 重录 + ldd 防呆 + 负控 exit=1），`fix-xml-folding` verdict 已同步升为无保留 PASS |
| **F-4** | Low | 关闭态来源跳过 `//` 与 `"…"`，但**不处理 `/* … */` 块注释**：块注释里的 `begin`/`end` 会被计入折叠。A4 样本没有块注释，未被此边界覆盖 | 建议加一条块注释样本断言（或声明为已知子集） |
| **F-5** | Note | 该来源尚未接进 `vedit-app`（T94 负责集成）；契约只要求 lane 级证据，故不阻塞 | 由 T94 收口 |

## 6. 未能验证的部分

1. **视觉屏幕观感**：本机无显示器；`offscreen` + KTextEditor 公开 API 的观测是 A3.1 允许的代理指标，真实屏幕折叠观感仍属停点验收。
2. **当前根树的全绿**：被 T94 在飞改动打断（F-2），非 T29 可修。
3. 我没有独立重新生成 1k 样本（用测试自身生成 + 独立写死的 8 条期望做对账；样本构造我在源码里逐行核对过）。

## 7. 结论

**PASS**。AC-S3.5 的折叠判据由我自己的复跑成立：1000 行 `.sv`、tree-sitter 关闭态、A3.1 的 `firstDisplayedLine`+`scroll_max` 观测、
**975 行隐藏 / 939 滚动落差**、200 条区间与独立构造期望逐条相等；期望值确实来自自有 `vedit-systemverilog.xml` 的
`veditFold` beginRegion/endRegion（author=VEdit 断言，源码直读 XML、不经 registry）；F2 交付库口径也由 `ldd` 证明。
当前根树红是 T94 在飞（F-2）；F-1/F-4 是证据与边界的小改进；T66 需要收尾（F-3）→ **已于 2026-11-01 关闭**（T66 已交付并经 T100 审计，T45 升为无保留 PASS；见 F-3 与 `evidence/fold-evidence-final/03-T45-upgrade-audit.md`）。
