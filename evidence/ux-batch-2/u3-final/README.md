# U3 evidence — user manual + documentation corrections (shared task T3)

- **Task**: T3 = handover `docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U3,
  contract `.mpd/plans/ux-batch-2/00-plan.md` (and its §0/§0.1 user rulings).
- **Author**: Senior Engineer (`senior-engineer-5`), 2026-11-06.
- **Write scope touched (exactly U3's list)**:
  `docs/USER_GUIDE.md`, `docs/handover/**` (3 files corrected),
  `src/syntax/kde/README.md`, `evidence/ux-batch-2/b1/README.md`,
  plus this evidence directory.

> Claim rule used here: a statement is in the manual only if this directory can
> point at the code or the evidence that proves it. Facts that could not be
> proven (or that the contract marks as pending) were either left out or written
> as an explicit limitation.

> Line citations were observed on 2026-11-06 while other lanes were still editing
> the tree; if a line number has drifted, the named symbol/function is authoritative.

---

## 1. What changed

| File | Change |
|---|---|
| `docs/USER_GUIDE.md` | **+TOC** (sections 1–16) and one intro sentence; **new §11 模块树与接口面板**; **§12 搜索与替换**; **§13 诊断：自动 lint、悬浮修复与文件状态点**; **§14 代码导航与多光标**; **§15 设置：字体、缩放、Format 与 Lint**; **§16 已知限制** (the four §8 bounds); §9 “Verible lint 规则” row made precise (discovery exists, the lint job does not pass it yet). §1–§10 otherwise keep their numbers and text (minimal diff; the only other edit is the intro sentence). |
| `docs/handover/04-hard-facts.md` | §3 delivery bullet corrected: 5 shipped definitions (`Markdown,SystemVerilog,Tcl,VHDL,Verilog`), `KSH-ENV defs=5`, `KSH-GUARD PASS` under C / zh_CN / en_US; the old 2-definition probe value is marked void. |
| `src/syntax/kde/README.md` | “two definitions” → **five** (data-files table now lists Verilog/SystemVerilog/VHDL/Markdown/Tcl with folding facts), shipping path corrected to `<AppDir>/usr/share/org.kde.syntax-highlighting/syntax/` (DATA ROOT form), extension map updated (`.vhd/.md/.tcl/.sdc/.xdc` included), sha256 embargo explicitly scoped to the two KDE files. |
| `evidence/ux-batch-2/b1/README.md` | “Every string is wrapped” corrected: it was **not** true when the lane landed; true only after `b1-repair` §M2 + the batch translation step. |
| `docs/handover/appendix/requirements-rev2.md` | non-goal #5 (cross-file replace) annotated: delivered by UX batch 2 item 5. |
| `docs/handover/CELL-20261102-STATUS.md` | top-of-file correction note + inline markers: the T17 module tree is the OLD instance-hierarchy lane, replaced by the pure module hierarchy; the “跨文件替换 is a non-goal” bullet corrected. |
| `docs/handover/CELL-20261103-STATUS.md` | top-of-file correction note + inline markers: T37 module-tree §C/D is the old lane; the “verible hover 置灰” conclusion is refuted by the batch's F1 measurement. |

---

## 2. Claim-by-claim verification (manual → source)

### §11 模块树与接口面板

| Claim | Verified at |
|---|---|
| Two dock panels in the right dock area; data from one whole-project slang module catalog, async | `evidence/ux-batch-2/b1/README.md` §1 (`module_tree_integration.cpp`: two docks, right area; `catalog_client`: `vd_catalog_start`/`poll`, 50 ms QTimer) |
| Roots = modules never instantiated; “defined but never instantiated” appears as a root | plan §B1 (`root = ABI is_top`); `b1/README.md` acceptance ① `roots=[orphan, top_a, top_b]` |
| A module instantiated by two parents appears under BOTH | `b1/README.md` acceptance ② (`top_a.u_shared`, `top_b.u_shared`); `src/workspace/module_tree/module_hierarchy.cpp` (`buildModuleTree`) |
| Columns 模块 / 例化次数 / 定义位置 | `src/workspace/module_tree/module_tree_panel.cpp:105-107` (`Module`, `Instantiations`, `Definition`) |
| Double-click a module row opens its declaration | `b1/README.md` acceptance ③ (`cursor=1:8`, declaration text); `module_tree_panel.cpp` `itemDoubleClicked` |
| No network/driver/load rows exist | `b1/README.md` acceptance ④ (`only-module-rows`, row labels) |
| Refresh + filter (case-insensitive, any of 3 columns, parents kept) | `module_tree_panel.cpp:88-100` (Refresh, “Filter modules…”), `:322-343` (`applyFilter`) |
| Depth limit 64 / row cap 20000 / cycle markers | `b1/README.md` §1 (`kModuleTreeMaxDepth`, `kModuleTreeMaxRows`, guard markers) |
| Summary line: tool, module count, top count, ms, peak KiB | `src/workspace/module_tree/module_catalog.cpp:62-72` (`summaryLine`) |
| Empty state is one reason line | `translations/vedit_zh_CN.ts` “未找到模块：slang 未报告此工程的任何模块定义。”; `b1/README.md` §1 |
| Interface columns 名称/方向/类型/位宽/位置 | `src/workspace/module_tree/interface_panel.cpp:85-89` |
| Direction In/Out/InOut, ref→Ref | plan §1 F2; handover §5.F2 |
| Width from the type string: `logic[3:0]`→4, unpacked→1, unparsable→empty+tooltip | plan §B1; `b1/README.md` acceptance ⑤ (`y / Out / logic[3:0] / 4`, `location-column top_a.sv:1:27`) |
| Focus rule: tree selection, else caret module (same file, def line ≤ caret line) | plan §B1; `b1/README.md` §1 (“focus (a) tree selection / (b) caret module”) |
| Double-click port → declaration | `b1/README.md` acceptance ⑥ |
| Ctrl+double-click port → the instantiation statement; chooser lists 父模块 · 实例路径 · 文件:行 | `interface_panel.cpp:298-314` (`%1 · %2 · %3` from `parentModule`, `instance.path`, `describePosition`); `activationTooltip()` `:56-62`; `b1/README.md` acceptance ⑥ |
| Why: the catalog has no per-connection position | `activationTooltip()`; `b1-repair/REPORT.md` §M6 |
| Empty state text | `interface_panel.cpp:115-119`; translations |

### §12 搜索与替换

| Claim | Verified at |
|---|---|
| Two entries (Ctrl+F bar, Search panel) share 4 options | `src/workspace/search/search_query.h:5-16`; `src/editor/kernel/find_replace.cpp:386-401`; `src/workspace/search/search_panel.cpp:100-130` |
| Search panel opens from View menu (`Ctrl+Alt+S`) or the toolbar search button | `src/app/app_shell.cpp:589-609` (`search.focus`, panel toggle `Ctrl+Alt+S`); `src/app/ux_selftest.cpp:312-322` |
| Search panel extra “Verilog” filter (`.v/.vh/.sv/.svh`) | `search_panel.cpp:133-139` |
| Async, cancellable; results File/Line/Match | `search_panel.cpp:140-145` (Search/Cancel), `:193` (tree headers); plan §B2 (“一律异步”) |
| Result limit 2000 hits / 15 s budget | `src/workspace/search/search_panel.h:307,309` |
| 「替换」= current match then next | `search_panel.cpp:163-165` (“Replace the current match and advance to the next one”) |
| 「全部替换」asks confirmation with file/hit counts first | plan §B2; `search_panel.cpp:1522-1600` (`confirmAndReplace`); translations “Replace "…"? … occurrence(s) in … file(s)” |
| Per-file single atomic write | plan §B2 (`atomicSaveText`); `b2-repair-r3/README.md` §1 |
| Save-before-replace; refusal if the save fails | `b2-repair-r3/README.md` §1/§6; translations “已先保存 %1 个受影响的未保存编辑器文档” / “全部替换被拒绝：有未保存的编辑器文档无法保存” |
| Refusal list (written during scan, hit line changed, query/options changed, limits, no bridge) | `search_panel.cpp` refusal strings; translations (`Replace All refused: …`); `b2-repair-r3/README.md` §1 |
| RE2 vs PCRE difference | `search_query.h:22-27` (“the PROJECT-wide engine is the Rust `regex` crate (RE2-style: no backreferences, no lookaround), while the in-file search uses QRegularExpression (PCRE-like)”); plan §2 |
| Ctrl+F whole-project hands query+options to the panel; replace only in the panel | `find_replace.cpp:397-401, 999, 1146-1147`; `:914` (“Project replace runs from the Search panel”); plan §B2 |

### §13 诊断

| Claim | Verified at |
|---|---|
| Upper = project lint, lower = per-file/LSP; only upper shown when present; edit after pass drops upper; one merged view for all surfaces | plan §0.1 **R2**; `src/workspace/diagnostics/diagnostics_integration.cpp:136` (squiggles from the ONE merged view), `:345-347` (`markFileEdited` drops the upper verdict at once) |
| Auto project lint after every successful save | `diagnostics_integration.cpp:835-836` (`setOnDocumentSaved` → `saveTrigger_->noteSaved`); plan §B3 |
| 700 ms debounce / single-flight / never blocks the save | `src/workspace/diagnostics/project_lint_trigger.cpp:13, 44-56, 80-90`; plan §0.1 R3, §B3 |
| Automatic run is never modal; explicit run is | `src/workspace/diagnostics/project_lint_notice.cpp:106` (`Mode::Explicit` only → `showDialog()` at `:139`); `:95-97` (clickable status-bar link for automatic runs) |
| ELAB modal lists errors, double-click jumps | `project_lint_notice.cpp:140-190` (`Project lint errors`, `itemDoubleClicked` → `jumpTo`) |
| Hover shows severity + rule id + message | `src/workspace/diagnostics/diagnostic_hover.cpp:51-59`; `diagnostic_hover.h:18-23` |
| 「修复」only when the engine can really fix it; temp copy + one undo | plan §B3; `diagnostic_hover.h:9-23`; `src/workspace/diagnostics/autofix.cpp`; `b3-repair-r2/REPORT.md`; translations “已用 verible-verilog-lint 修复 %1（一次撤销即可还原）” |
| No fix → no button, reason in tooltip | `diagnostic_hover.h:9-23`; `autofix.cpp` reason strings |
| Ordinary symbols keep LSP hover; verible advertises `hoverProvider:false` but answers | handover §5.F1; plan §F1 |
| File dot red=error / yellow=warning only / green=clean / no dot=no verdict; between icon and name | `src/workspace/filetree/file_status_delegate.cpp:20-31` (`dotColor`), `:41-75` (paint between icon and name); plan §B3 item 10 |
| Rename: pre-check before the dialog; grey + reason when the backend lacks it; applies to open docs and writes unopened files; reports counts | plan §B5; `b3-repair-r2/REPORT.md`; translations “重命名需要光标位于符号上（第 %1 行第 %2 列没有符号）”, “重命名已应用：… 磁盘上写入 %4 个文件” |
| Cross-file rename NOT claimed | handover §8.2 |

### §14 导航 / 多光标

| Claim | Verified at |
|---|---|
| Ctrl+click = go-to-definition via the F12 action (same dispatch) | `evidence/ux-batch-2/b4/README.md` §1 (`dispatchDefinitionAt` → shell action `action-diagnostics.definition`); acceptance ② |
| Backend without definition → plain click + one status-bar reason | `b4/README.md` acceptance ③ |
| Alt+click adds/removes a cursor; Alt+drag keeps block select; other gestures unchanged | `b4/README.md` acceptance ①④ |
| Clicking a non-active split pane activates it (inner widget included) | `evidence/ux-batch-2/pane-activation/README.md` §What changed; ctest `vedit-editor-pane-activation-tests` |

### §15 设置

| Claim | Verified at |
|---|---|
| `Edit → 首选项…` / `Ctrl+,`; tabs 快捷键/工具/FIM/界面/Lint/格式化 | `src/workspace/settings/settings_dialog.cpp:18`; `src/workspace/settings/settings_panel.cpp:325-333` |
| Font size applies immediately; “跟随系统” stores nothing | `evidence/ux-batch-2/a4/ASSERTIONS.md` ⑤ (`QApplication::font().pointSize()`, `font.system-not-stored`) |
| Scale 100/125/150/200, next start (`QT_SCALE_FACTOR`) | `a4/ASSERTIONS.md` ⑥ (`scale.env-*`, product `--ui-scale-echo`) |
| Format: 23 knobs, advanced section, restore defaults | `a4/ASSERTIONS.md` ② (`settings.a4.format.knobs 23/23`); translations “高级选项”/“恢复默认” |
| Format defaults send NO flag | `a4/ASSERTIONS.md` ② (`default-argv` empty; real process receives only `-`) |
| One Lint page, two independent stores | plan §0.1 **R1**; `src/workspace/settings/lint_options.h:1-40`; `a4/ASSERTIONS.md` ③ |
| Unsupported knobs greyed + reason in tooltip | `a4/ASSERTIONS.md` ④; `src/workspace/settings/lint_options.cpp:99-105` |
| Transport-pending knobs say “stored but not sent yet” | `src/workspace/settings/lint_options.h:21/66/129`; `settings_panel.cpp:2197`; translations line 6245 |
| `--rules_config` is the one lint knob that reaches the backend | `lint_options.cpp:96-99`; `src/workspace/diagnostics/ffi_backends.cpp:605-610` |
| Discovered `.rules.verible_lint` is NOT passed as the default yet | `ffi_backends.cpp:598-602` (`QString discoveredRulesPath;` stays empty; the comment says the C ABI does not expose it yet); `lint_options.h:122-126` |

### §16 已知限制 (handover §8, verbatim in substance)

| Bound | Handover source |
|---|---|
| No real-machine GUI verification; offscreen/Xvfb only; Xvfb CJK glyphs are tofu | §8.1 + §5.F9 |
| Cross-file Rename not claimed (verible answers for the open file only) | §8.2 |
| Project-wide regex is RE2-style (no backreferences/lookaround), Ctrl+F is PCRE | §8.3 + plan §2 |
| Narrow search-replace residual bounds (identical size+mtime external rewrite; identical text line inserted outside the scan window; non-product bridge without `bufferText()`) | §8.4 |

### Syntax highlighting table (§2, already present; checked, not rewritten)

| Claim | Verified at |
|---|---|
| Complete extension table incl. `.sdc/.xdc → Tcl` | `docs/USER_GUIDE.md` §2 table (6 rows, 15 suffixes); `src/editor/kernel/highlight_mode.cpp:69-71` (`kExtensionTable`, including `{"sdc","Tcl"}, {"xdc","Tcl"}`); `resources/syntax/README.md`; A3 drift guard `evidence/ux-batch-2/a3-repair/06-r4d-extension-drift.log` (15 contract suffixes) |

---

## 3. Corrections landed (with greps)

```
$ grep -rn 'builtin_valid_defs=2' docs/
docs/handover/CELL-20261105-UX-BATCH2-HANDOVER.md:97:  … 纠错：`… builtin_valid_defs=2` → 5 …
  ⓘ the ONLY remaining hit is the frozen handover's own correction instruction
    (it quotes the stale value as the thing to fix). 04-hard-facts.md no longer
    contains it.

$ grep -rn 'Every string is wrapped' evidence/ux-batch-2/b1/README.md
(no output — GONE; the paragraph now records that the claim was false before b1-repair)

$ grep -n 'Verilog /$\|SystemVerilog definitions\|two definitions\|two data files' src/syntax/kde/README.md
(no output — GONE; the file now lists five definitions and the corrected ship path)
```

Handover narrative corrections (every occurrence now carries a dated correction;
the historical text is kept, not silently rewritten):

```
$ grep -rn '跨文件替换' docs/handover/ | grep -v CELL-20261105
appendix/requirements-rev2.md:34  “跨文件替换……”  + line 35 correction: delivered by item 5
CELL-20261102-STATUS.md:10/186      top-of-file correction + corrected non-goal bullet

$ grep -rn '信号 source/target\|§C/D\|2/5→5/5' docs/handover/CELL-2026110{2,3}-STATUS.md
CELL-20261102-STATUS.md:8   top note: T17 = OLD lane, replaced by the pure module hierarchy
CELL-20261102-STATUS.md:39  T17 row marked “旧 lane，已被 UX 批次 2 item 1 的纯模块层级树取代”
CELL-20261102-STATUS.md:97  §C/D re-run marked “该 lane 已被删除，此句作废”
CELL-20261103-STATUS.md:10  top note: T37 §C/D = OLD lane
CELL-20261103-STATUS.md:20/45  T37 rows marked “旧 lane，见文首更正”

$ grep -n 'hover 按后端能力\|置灰并给真实原因' docs/handover/CELL-20261103-STATUS.md
:64 original claim, followed on :65-66 by the correction (verible hover actually
    answers; the batch removed the wrong grey-out, handover §5.F1)
```

---

## 4. Gates / checks run

```
$ bash build/gates/docs.sh
[docs] PASS: README.md docs/BUILD.md docs/USER_GUIDE.md THIRD_PARTY_NOTICES.md
exit=0
```

Structural checks on the manual (TOC ⇄ headings; required topics present):

```
$ grep -n '^## \(1[0-6]\|[1-9]\)\.' docs/USER_GUIDE.md
## 1 … ## 16   (16 top-level sections; the 目录 table lists exactly these)
$ for pat in '从未被例化的模块' '两个父下各出现一行' '双击模块行' '网络 / 驱动 / 负载' \
  '名称 / 方向 / 类型 / 位宽 / 位置' 'Ctrl+双击端口行' '例化语句' '全部替换' 'RE2' 'PCRE' \
  '先保存后替换' '保存成功后自动触发' '700 ms' '单飞' '绝不弹模态框' 'ELAB' '修复' \
  'Ctrl+左键' 'Alt+左键' 'Alt+拖拽' '点哪个分屏就激活哪个分屏' '立即作用于整个界面' \
  '下次启动' '默认值不发送任何 flag' '一个页面，两套配置' '置灰' '传输待实现' \
  '没有真机 GUI 验收' '跨文件 Rename 不声明支持' '工程级正则 = RE2 风格' '最窄残留边界'; do
  grep -q -- "$pat" docs/USER_GUIDE.md && echo "PRESENT $pat" || echo "MISSING $pat"; done
(all PRESENT)
```

`git diff --stat` for the corrected tracked files:

```
docs/USER_GUIDE.md                          | 419 ++++++++++++++++++++++++++--
docs/handover/04-hard-facts.md              |   9 +-
docs/handover/appendix/requirements-rev2.md |   1 +
src/syntax/kde/README.md                    |  60 ++--
4 files changed, 451 insertions(+), 38 deletions(-)
```

(`CELL-20261102/3-STATUS.md` and `evidence/ux-batch-2/b1/README.md` are untracked
in this working tree, so they do not appear in `git diff`; both were verified with
the greps above.)

---

## 5. What I could NOT prove / did not run

1. **No product rebuild** (`scripts/build-qt.sh`), no `--selfcheck`, no ctest:
   this is a docs-only change. The manual is embedded into the binary through
   `resources/help/user_manual.qrc`, so a build is required for `Help → User
   Manual` to show the new text — that belongs to the captain's final gate/pack
   step, not to a docs edit.
2. **No Markdown-rendering run** in `QTextEdit::setMarkdown` (no Qt runtime on the
   host). The added syntax is the same GFM the file already uses (tables, bold,
   inline code, lists), and the help-viewer ctest only asserts the H1 title
   (`src/app/ux_selftest.cpp:337`), which is unchanged.
3. **No zh_CN `.qm` load in a running app.** UI labels quoted in the manual were
   taken from `translations/vedit_zh_CN.ts` (A2 evidence: 1018 finished /
   0 unfinished); I did not re-run `lrelease` or the language-switch test.
4. **The four §8 limitations are disclosed, not re-verified** — they are the
   batch's own declared bounds (the first one is exactly “no real-machine GUI
   verification”).
5. **Time-sensitive claim**: “discovered `.rules.verible_lint` is not passed as
   the default yet” is true for THIS tree (`ffi_backends.cpp` passes an empty
   `discoveredRulesPath`). If U6 wires `VdProjectInfo.rules_path`, §15.3 and the
   §9 row need a one-line update; the fix site is named in both places.

---

## 6. Reproduction

```sh
cd /home/haroldzhao/MyProj/DshProj/VEdit
bash build/gates/docs.sh                                   # docs gate
grep -n '^## ' docs/USER_GUIDE.md                          # TOC/headings
grep -rn 'builtin_valid_defs=2' docs/                      # only the handover instruction
grep -rn 'Every string is wrapped' evidence/ux-batch-2/b1/README.md
grep -n 'two definitions' src/syntax/kde/README.md
grep -rn '跨文件替换' docs/handover/ | grep -v CELL-20261105
git diff -- docs/USER_GUIDE.md docs/handover/04-hard-facts.md \
  docs/handover/appendix/requirements-rev2.md src/syntax/kde/README.md
```
