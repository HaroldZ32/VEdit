# A4 残项（T29 / AC-S3.5 折叠）：1k 行 `.sv` + tree-sitter 关闭态 ≥1 折叠（期望值来自自有 XML 的 beginRegion）

collected_at: 2026-11-01T14:10:00+08:00
git_head: 95c496e682e3841add1a081edd78d29c34957aac
contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/a4-fold-residual.md`

无头声明：本机没有显示服务器；视图观测用 KTextEditor 的公开 API（`firstDisplayedLine(RealLine|VisibleLine)`、
`verticalScrollBar()->maximum()`）+ offscreen 平台完成，属 AMENDMENTS A3.1 允许的观测方式。

## 0. 一句话

新增**关闭态折叠来源** `KsyntaxFoldSource`：直接读取我们自己的
`resources/syntax/vedit-systemverilog.xml` 里的 `beginRegion="veditFold"` / `endRegion="veditFold"`
关键字表并做平衡栈匹配（不读编辑器 repository、不读应用 registry）。在 1000 行 `.sv` 样本上、
`tree-sitter` 运行期开关**关闭**时，真实 `KTextEditor::View` 观测到 **975 行被隐藏**（25 个模块 × 39 行），
`scroll_max` 落差 **939 = min(975, 939)**，与样本文本构造独立算出的期望一致；A1 的四项自验仍在路径上
（非法区间仍被拒绝，`rejected=0` 只针对 200 条合法 XML 区间）。

## 1. 样本与关闭态

- 样本由 lane 测试生成：`kModuleCount=25 × kModuleLines=40` = **1000 行** SystemVerilog
  （`module/…/endmodule`，内含 `function`、两个 `always_* begin…end`、`generate` 与 `for…begin`）。
- 关闭态：`vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(false)`，测试断言
  `a4.tree-sitter-switch-off PASS` 且该来源 `treeSitterActive() == false`。
  本来源**不依赖** tree-sitter（XML 规则直读），所以关闭态折叠与库内嵌定义无关。

## 2. 期望值从哪来（不得来自 registry）

1. **规则来源**：`resources/syntax/vedit-systemverilog.xml`
   - `<language name="SystemVerilog" section="Hardware" author="VEdit">`（断言 ours；不是 KDE 定义）；
   - 折叠规则（T45 的 KDE keyword-list 写法）：
     `veditFoldBegin`（13 项：module, interface, program, class, package, generate, function,
     task, case, casex, casez, randcase, begin）→ `beginRegion="veditFold"`；
     `veditFoldEnd`（10 项：endmodule, endinterface, endprogram, endclass, endpackage,
     endgenerate, endfunction, endtask, endcase, end）→ `endRegion="veditFold"`。
   - 测试断言：`regionName()=="veditFold"`、两张表逐项相等、`definitionName/Section/Author` 与 XML 一致。
2. **构造期望（A3.2：独立于 registry）**：40 行模块的固定偏移 8 条区间，测试里写死：

   | XML 关键字 | 区间（模块内偏移） |
   |---|---|
   | module/endmodule | (0, 39) |
   | function/endfunction | (6, 8) |
   | always_ff `begin`/`end` | (9, 15) |
   | if `begin`/`end`（第 12 行 `end`） | (10, 12) |
   | else `begin`/`end` | (12, 14) |
   | always_comb `begin`/`end` | (16, 18) |
   | generate/endgenerate | (19, 23) |
   | for `begin`/`end` | (20, 22) |

   25 个模块 × 8 = **200 条**；实际 `A4 ranges total=200 first-module: (6,8) (10,12) (12,14)
   (9,15) (16,18) (20,22) (19,23) (0,39)` —— 与表一致（顺序按结束事件，比较时排序）。
3. 期望值**不**取自注入器 registry：`hiddenInViewport` 的期望 975 = 25×39 来自样本构造与 XML 的
   module 规则；registry 只在注入器内部用于 rebuild 的 unfold 一遍（A3.3）。

## 3. A3.1 观测与实得（真 `KTextEditor::View`）

```
SNAPSHOT a4.baseline first_real=0 last_real=61  first_visible=0 last_visible=61  hidden_in_viewport=0    scroll_max=939
SNAPSHOT a4.folded   first_real=0 last_real=1000 first_visible=0 last_visible=25 hidden_in_viewport=975  scroll_max=0
A4 closed_state tree_sitter_active=0 trigger=first-open injected=200 rejected=0
   expected_hidden=975 observed_hidden=975 scroll_drop=939 expected_scroll_drop=939
```

- `hidden_in_viewport`：视图里“真实行跨度 − 可见行跨度” = 1000−25 个可见行跨度 = **975**，与期望相等；
- `scroll_max`：939 → 0，落差 **939 = min(975, 939)**（`scroll_max` 在内容行数−视口行数处饱和，同 T19 说明）；
- `full_count=1`（只允许的 FirstOpen 触发重建一次），A2 语义未被接线破坏；
- A1 未被绕过：`inject({5,5})` 仍被拒（`a4.invalid.still-rejected PASS`，`rejected +1`）；
  200 条合法 XML 区间全部通过四项自验（`a4.ranges.all-valid PASS`）。
- 断言全绿：`FOLDING-TESTS ok=1 checks=75 failures=0 skipped=0`。

## 4. 库无关性（F2 交付口径）

同一测试在**交付自建 F2 库**（`target/ksh-selfbuild/F2/.../libKF6SyntaxHighlighting.so.6.24.0`，
sha256 `aac288a16e508d529e9ae275daf4e3766fe4309a68c2b93ed16083d252785fa6`，
`strings | grep -c 'DOCTYPE language'` = **0**，无内嵌 KDE 定义）与宿主发行版库上**输出逐行相同**
（`01-lane-tests.log` 的 run A / run B），因为来源直接读 XML 文件、从不经 repository 解析定义。
这也顺带满足 A4 的“探针断言 author/section”要求（断言内容见 §2）。

## 5. 门禁（自跑输出）

| 门禁 | 结果 |
|---|---|
| lane ctest `vedit-folding-tests` | `1/1 Passed`（`02-ctest.log`，`LANE-CTEST-EXIT=0`） |
| 根 ctest | **19/19 Passed**（`03-root-ctest.log`；一次重跑前的 `BAD_COMMAND` 对是并发把 `typecheck` 与 `ctest` 同时指向同一构建目录造成的竞态，单独重跑即 19/19，已如实记录） |
| `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`**（`04-typecheck.log`） |

复跑：

```bash
cmake -S src/syntax/folding -B target/a4-fold-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/a4-fold-build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir target/a4-fold-build -R vedit-folding-tests --output-on-failure
# F2 交付库口径：
F2=$PWD/target/ksh-selfbuild/F2/install/lib/x86_64-linux-gnu
LD_LIBRARY_PATH=$F2 QT_QPA_PLATFORM=offscreen target/a4-fold-build/vedit-folding-tests
```

## 6. 改动（均在写域内）

| 文件 | 改动 |
|---|---|
| `src/syntax/folding/ksyntax_fold_source.h/.cpp`（新） | 关闭态来源：QXmlStreamReader 读自有 XML（language 属性 + list + keyword beginRegion/endRegion），平衡栈匹配 `//` 注释与双引号字符串之外的关键字，输出整行 `FoldRange`；`treeSitterActive()` 恒 false；暴露 identity/region/关键字表供断言 |
| `src/syntax/folding/CMakeLists.txt` | 链接新源文件；给测试定义 `VEDIT_A4_SYSTEMVERILOG_XML`（指向 `resources/syntax/vedit-systemverilog.xml`） |
| `src/syntax/folding/tests/folding_tests.cpp` | 新增 `testA4ClosedStateFolds()`：identity/规则表断言、1000 行样本、200 条构造期望比对、关闭态真实视图观测（975/939）、`full_count`、A1 非法区间仍被拒 |
| `src/syntax/folding/README.md` | 文件表 + A4 段：关闭态来源与“仍未链入 vedit-app（T94 集成工作）”的边界 |

未改 `src/workspace/breadcrumb/**`、未改 `resources/syntax/**`、未改其它 lane 在飞文件；未 commit/push。

## 7. 边界（如实声明）

1. **未接进 `vedit-app`**：本任务只交付 lane 级验收（真实视图 + 真实 provider/真实 XML 规则）。
   把关闭态来源接到实时视图（打开首建、tree-sitter 开关重灌、按键/光标 no-op）以及面包屑接线，
   属于 `T94`（此前已被本成员 claim、尚未开工；请统筹改派/释放）。这次不把差距拖到停点：
   T94 的验收断言设计已在 lane 层被本测试覆盖到“注入器 + 真实视图”这一半，剩余是“何时调用”的集成。
2. 本来源的文本子集限于 XML `Normal` 上下文的关键字折叠：忽略 `//` 行注释与 `"…"` 字符串；
   XML 里以 `Detect2Chars` 定义的 `Comment` 区域不是关键字表，不在本来源折叠（A4 判据针对
   module/function/… 关键字折叠，样本即此子集）。
3. 期望区间是**写死的构造值**：若 `vedit-systemverilog.xml` 的折叠关键字表变化，测试会红——
   这是刻意的“规则锚点”，不是脆弱测试。
4. 无显示服务器：`offscreen` 下的视图观测是 A3.1 的代理指标，真实屏幕观感仍属停点验收。
