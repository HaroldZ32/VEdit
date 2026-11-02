# T101 独立评审结论 — T94（折叠注入器 + 面包屑接进 `vedit-app`）

> **手工建任务、无冻结契约**：T101（及 T94）在 cell 里没有 split-key 映射/验证环，判据取自 captain 派单书；
> verdict 落盘于此。评审者 ≠ writer（writer = Senior Engineer 3）。
> **我评的代码状态**：`HEAD 48569602d0c86fb402dc88155593d1dc06445c90` + T94 的**未提交**改动
> （`src/editor/kernel/fold_breadcrumb.{h,cpp}` 新增、`ktexteditor_provider.{h,cpp}`、`src/editor/CMakeLists.txt`、
> `src/app/{editor_area.*, editor_provider.h, main.cpp}`）。证据：`evidence/verdicts/fold-wiring/t101-checks.log`。

- **结论**：**PASS**（四个核心命题全部经我独立复跑/反证成立）
- **非阻塞发现**：4 条 Low（均证据/语义说明，不阻塞）

---

## 核心命题一：接线没有绕过“先验后折叠”纪律 ✅

- **代码审计**：全仓（非测试）只有 `src/syntax/folding/fold_injector.cpp` 调 `exec("fold")`（`fold`/`unfold` 各一处）；
  `src/editor/kernel/fold_breadcrumb.cpp` 里**直接 KTextEditor 命令调用 = 0**，只经 `FoldInjector::inject()/rebuild()/rebuildFrom()`；
  绑定使用的重建触发恰好只有 `FirstOpen / TreeSitterToggled / Reparsed` 三种（`grep RebuildTrigger::` 结果就是这三个）。
- **我实跑**（产品二进制 `--selfcheck`，真实 ApplicationShell + 真实 KTextEditor provider）：
  ```
  CHECK fold.wiring.a1-invalid-rejected PASS injected=0 rejected=1
  ```
  自检经**绑定内部的同一个 injector** 注入 `FoldRange{5,5}`（`binding->injector()->inject(...)`，`main.cpp:821-824`）：
  返回 0、`rejectedCount` +1 → A1 的四项自验仍在路径上，非法 range 不会被交给 `fold`（T19/T20 已证明否则会折叠用户选区）。

## 核心命题二：段错误是**真崩**，不是防御性动作 ✅（我做了反向验证）

- 代码路径：`ktexteditor_provider.cpp:111-127`——`openFile()` 在 `loadFileIntoDocument()`（`setText`）**之前**调
  `binding->releaseFolds()`（unfold 全部已登记折叠），失败则 `loadFileIntoDocument` 直接返回；成功后 `onDocumentOpened()`（FirstOpen 重灌）。
- **反向验证（我自己做的）**：把**当前**源码树复制到容器内 `/tmp/rev`，**只**注释掉 `binding->releaseFolds();` 这一行
  （其余完全不变），同参数重建 `vedit-workspace-search-selftest`（writer 记录的崩溃复现器），运行：
  ```
  CONFIGURE-EXIT=0   BUILD-EXIT=0
  SEARCH-SELFTEST-EXIT=139   Segmentation fault (core dumped)
  ```
  崩溃点正是“同一 pane 打开第二个文件（a.v → b.sv）”之后（日志最后一行 `search: jump opened '.../b.sv'` 后即 core）。
  对照组：**修好的共享树**里同一个 selftest 在根 ctest 中通过（22/22）。→ **这是一次真实的段错误修复**（去掉这一行就必崩），
  不是“加了防呆没坏处”。仓库本体未改（反证只在容器内 `/tmp/rev` 副本）。

## 核心命题三：计数与性能语义 ✅（我实跑 + 读真实信号路径）

```
FOLD-WIRING closed_state … last_visible=25 full_count=2 injected_ranges=200 hidden_in_viewport=975 tree_sitter=0
CHECK fold.wiring.closed.hidden-975 PASS hidden=975
CHECK fold.wiring.closed.registry-200 PASS registry=200
CHECK fold.wiring.closed.no-rejects PASS
FOLD-WIRING key_path full_count=2 key_events=50
CHECK fold.wiring.no-rebuild-on-keys PASS
CHECK fold.wiring.toggle-on-rebuild PASS full_count=3
CHECK fold.wiring.no-rebuild-on-edit PASS full_count=5
CHECK fold.wiring.reparsed-trigger PASS full_count=6
FOLD-WIRING breadcrumb describe='module:m0 > function:inc' bar='m0 > inc'
CHECK fold.wiring.breadcrumb.describe PASS / pane-bar PASS / no-invention PASS describe='' bar=''
```

- `hidden_in_viewport=975`、`registry=200`、`rejected=0` → T29 判据在接线后仍成立 ✓。
- **50 次按键 `full_count 2→2`** ✓；**真实 `insertText` 编辑 `5→5`**（不重建）✓。
- **真实 `contentsChanged` 路径确认**（不是只看 selfcheck 输出）：`fold_breadcrumb.cpp:67-75` 把
  `KTextEditor::Document::textChanged` **只**接到面包屑 150 ms 防抖，注释明确“NEVER rebuild folds here: typing is not an allowed
  rebuild trigger”；折叠重建的唯一入口是 `rebuildFor(trigger)`，而 trigger 只有三种允许值。自检的 `insertText` 驱动的就是这条真实信号。
- `TreeSitterToggled` 恰 +1（2→3）✓；`Reparsed` 恰 +1（5→6）✓。
- 面包屑：函数体内 `module:m0 > function:inc`、pane 栏 `m0 > inc` ✓；tree-sitter 关闭 → `describe='' bar=''`（**不发明**）✓。

## 核心命题四：T81 / T80 未回退 ✅（我实跑）

- T80：`save.shortcut.unique PASS bindings=1`、`save.shortcut.authority PASS`、`save.shortcut.unique.multi-pane PASS bindings=1`。
- T81 语义：`fold.wiring.highlight-mode PASS SystemVerilog`；`editcore.highlight.verilog.mode PASS mode='Verilog'`、
  `editcore.highlight.systemverilog.mode PASS mode='SystemVerilog'`、两条 `definition` / `format-ranges` / `folding-regions` 均 PASS。
  （开发机日志里 `resolves to author 'Yevgen Voronenko…'` 是**预期**：交付口径的 F2 库 author=VEdit 由 T88 在 F2 上验证；本条只要求 mode 被选中。）

## 回归（我实跑，全部达期望）

| 项 | 观测 |
|---|---|
| `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=0`**（`[qt6] PASS … warnings-as-errors` + pinned 镜像内 Rust clippy PASS） |
| 根 ctest（容器内，构建树=当前 T94 源，二进制 14:45 晚于源 14:37/14:41） | **100% tests passed, 0 failed out of 22**，exit 0 |
| `vedit-app --selfcheck` | **`APP-SHELL-SELFTEST ok=1 checks=200 failures=0`**、`EDIT-CORE-SELFTEST ok=1 checks=67 failures=0`，exit 0 |

## Findings（均 Low，非阻塞）

| ID | 严重度 | 症状 | 期望 | 证明来源 |
|---|---|---|---|---|
| F-1 | Low | `evidence/fold-breadcrumb-wiring/REPORT.md` 头部 `git_head: 95c496e…` 是**过期值**（实际树是 `4856960…`；同目录 `01/04` 日志里的 C9 头是正确的 4856960）。另外证据目录**没有** wired 源码的 sha256 pin（不像 T19 的 `05-source-sha256.txt`；`01-selfcheck.log` 有 binary sha256 可替代一部分） | 报告头改成实际 HEAD，并补一份 wired 源码 sha256（或声明以 binary sha256 + C9 日志为准） | `evidence/fold-breadcrumb-wiring/REPORT.md:3` vs `01-selfcheck.log:4` / `04-search-regression.log:4` |
| F-2 | Low（writer 已自述） | `releaseFolds()` 走 `rebuild(FirstOpen, {})`，会计一次 `full_count`；同一 pane 第二次打开文件 = release(+1) + FirstOpen(+1) = **+2**（首次打开 registry 空不触发，故 `firstopen-full-count=1` 语义不变） | 若要求精确计数，folding lane 需要增加“只 release 不计数”的公开入口（不在 T94 写域）；当前是实现层语义，已写进报告 §6.2 | `src/editor/kernel/fold_breadcrumb.cpp:148-160`；`REPORT.md` §6.2 |
| F-3 | Low（设计边界） | `Reparsed` 触发有显式入口 `notifyReparsed()`，但**未**挂到 `textChanged`（挂上去就会退化成每次输入重建，违反 A2.1） | 由未来的 reparse 管线显式调用；当前语义“编辑不重建”已被自检断言 | `src/editor/kernel/fold_breadcrumb.cpp:176`；`REPORT.md` §6.1 |
| F-4 | Low（边界） | 产品命令 `view.treesitter` 只在交互 `main()` 路径注册；无头自检用绑定 API 直接驱动同一实现 | 可接受；若要求“命令本身”也被自检覆盖，需在自检里注册同一 action | `REPORT.md` §6.4 |

## 未能验证 / 边界

1. **真实屏显观感**（折叠标记、面包屑栏颜色/折行）：无显示服务器，全部 offscreen；GUI 不得认证。
2. **物理键盘/鼠标滚轮手感**：自检用合成事件（KeyPress/insertText），真人交互未测。
3. **`Reparsed` 的产品级触发**：尚无 reparse 管线调用 `notifyReparsed()`（设计如此，F-3）。
4. **多视图共享同一 document 的折叠路由、1000+ 折叠规模**：不在本票判据内，未测。

## 结论

**PASS**。T94 把 T19 的折叠注入器与面包屑接到真实产品路径（`ApplicationShell` + 真实 `KTextEditor` provider），
并且**没有绕过**“先验后折叠”的纪律（`inject({5,5})` → 0 且 rejected+1；全仓只有 `fold_injector` 调 `exec`，接线层 0 处直接命令调用）；
它修掉的是一处**真实段错误**——我只回退 `releaseFolds()` 一处就在隔离副本里稳定复现 SIGSEGV（exit 139），修复后同套件在根 ctest 22/22 中通过；
计数/性能语义（975 hidden、registry 200、按键 2→2、编辑不重建、两种允许触发各 +1）与面包屑“随语义走、关闭不发明”均经我实跑确认；
T80/T81 未回退；`typecheck` exit 0、根 ctest 22/22、selfcheck 200/200。四条 Low 都是证据/语义说明层面，建议随手补齐（尤其 F-1 的报告头 HEAD）。
