# T20 主壳集成：七项（search/jump/面包屑/双窗格/Qt ABI guard）

Cell `cell-20261102T100600176Z` · 任务 **T13 / shell-t20** · 成员 **se-shell**（Senior Engineer）
契约：`.mpd/cellwork/cell-20261102T100600176Z/contracts/shell-t20.md`
边界：只改本任务 scope 内路径（见 §7）；不改测试断言、不改门禁。
技能：programming、debugging（已加载）。

**结论（本任务范围）**：七项全部落地并有证据；门禁 `bash scripts/build-qt.sh` **exit 0**，
**ctest 37/37**（search selftest 由 4 红转 0 红，断言条数 106 不变）；`bash build/gates/typecheck.sh`
的**两项实质检查绿**（Qt6 `-Werror` build、`cargo clippy --all-targets -D warnings`），
但该脚本整体 exit 1 —— 红在 `check_ffi_header_drift`（**仓库状态检查**：committed vs worktree，
由**其他 lane 在飞的 C ABI 改动**造成，非本任务代码信号；T22 既定口径见交接 §2）。

---

## 1. 树状态与新鲜度

- HEAD `4e2d0a57ca50db9568db5a5b040cc66f04c9a05f`；记录时 worktree dirty=173（含其他 lane 在飞改动）。
- 固定镜像：`vedit-qt6-build:local`；门禁构建实测 `image_id=sha256:acd1bc9d09036ef5ff3790c721bb0fd895bd09f1a9e1c2047dc2e462f9178daa`
  （typecheck 的 Qt6 lane 重建了镜像：`0110012f…`；Rust 层镜像 `2401e5bd…`。镜像配方由仓库 Dockerfile 固定，
  但**其他任务会重建**它，所以 runner 应按各日志里的 image_id 记录，不要假定 id 稳定）。
- 本任务源码 mtime 全部在 19:35–19:49；产品二进制 `vedit-app` 19:50:47、search selftest 19:50:41、
  探针 19:47:13（均晚于源码）。完整 mtime/sha 见 `09-metadata-20261102.txt`。

## 2. 七项：逐项落地与证据

| # | 交付 | 落地位置（本任务改动） | 证据 |
|---|---|---|---|
| ① | 删除 search 本地 `IJumpTarget`，统一注册到 `jump::Registry`（A-1） | `search_types.h`（删本地接口，保留 UI 值类型 `JumpRequest`）、`search_panel.{h,cpp}`（`activateHit` 直接 `jump::jumpTo`）、`search_jump_target.{h,cpp}`（实现 `jump::IJumpTarget`，析构 `clearTargetIf`）、`search_integration.cpp`（`jump::setTarget`）、`search/CMakeLists.txt`（链接 jump lane） | `03-probe-run-*.log`: `t20.jump-target-registered`；三个生产者（search/面包屑/诊断）都经同一 registry 落点；search selftest 的 `jump.*` 断言改走 registry 后 106/0 |
| ② | 修 `search_selftest.cpp` 缓存（view/document 按 pane/tab 解析），`jump.product.second-*` 4 红转绿（A-2） | `search_selftest.cpp`：`refreshTab()` 在每次点击后从 `pane->editorWidget()` 重取 view/document；断言不变 | 前：`00-search-baseline-*.log` = **106 checks / 4 failures**（`second-loads-bytes`、`second-cursor-line`、`second-cursor-column`、`second-open-paint-stable`）；后：`01-search-after-jump-unify-*.log` = **106 checks / 0 failures**；ctest **37/37** |
| ③ | `global_search.cpp` 无 FFI 构建的 `-Werror=unused-parameter`（A-3） | `global_search.cpp`：`runScan` 的 `cancelTag` 标 `[[maybe_unused]]`（无 FFI 时扫描在轮询前就失败，token 不消费） | `08-search-standalone-*.log`：独立构建（**无 FFI、无 shell**）`CONFIGURE_EXIT=0`、`BUILD_EXIT=0`（修复前该构建在 `global_search.cpp:113` 红，见 `evidence/review-jump/06-search-build.log`） |
| ④ | 面包屑挂载：pane 用 `vedit::breadcrumb::Bar` + `pathEntries()`（A-3 的 `fold_breadcrumb.{h,cpp}` + 授权 CMake hunk） | `fold_breadcrumb.h/.cpp`：新增 `pathEntries()` + `publishEntries()`（把同一路径写成 view 的 `veditBreadcrumbEntries` 属性，shell 不碰 KTextEditor 头）；`editor_area.{h,cpp}`：pane 挂 `Bar`（无 lane 时保留旧 QLabel 回退，`if(EXISTS)` 式保护），`breadcrumbText()` 由段标签拼回 `"a > b"`；`src/app/CMakeLists.txt` 授权 hunk：`if(TARGET vedit_workspace_breadcrumb)` 链入 + `VEDIT_APP_HAVE_BREADCRUMB` | 真实 shell 探针 `t20.bar.mounted / .segments / .module-segment / .function-segment / .file`，真实鼠标点段 `t20.bar.click-delivered` + 落点行正确；截图 `shots/03a-breadcrumb-segments.png`（`alpha › inc`）、`shots/03-breadcrumb-click.png`。回归：`--selfcheck` 的 `fold.native.breadcrumb.pane-bar PASS m0 > inc`、`no-invention PASS`（245/0，未削弱） |
| ⑤ | 双窗格各 2 标签；跳转/面包屑/诊断分别落在正确窗格与文件（probe + 截图） | 探针 `probe/t20_shell_probe.cpp`（真实 `ApplicationShell` + `installPanels` + KTextEditor provider；真实 QTest 鼠标事件） | `03-probe-run-*.log` = **50 checks / 0 failures, exit 0**：先断言 `t20.two-panes-two-tabs`（panes=2, 2+2 tabs）；search 命中 alpha.sv（已开在**左**窗格）→ 在**活动右**窗格打开（tabs=3、左窗格不变、光标=命中行 11）；面包屑点段 → 右窗格 alpha.sv 第 3 行（module 行）；诊断（真实 slang 报错 undeclared `missing_signal`，工具行=源码行 8）→ 双击行时**左**窗格为活动 → 落在左窗格 beta.sv 第 8 行，右窗格保持 alpha.sv。截图 `shots/01..04` |
| ⑥ | Qt ABI guard 作为 compile-only CMake target（A-11） | 根 `CMakeLists.txt`：`vedit_qt_abi_guard` OBJECT（default build 编译、不链接执行），`if(EXISTS)` 护住 `crates/vd-ffi/tests/qt_abi_guard.cpp` 与**树里的生成头** `crates/vd-ffi/include/vd_ffi.h`，`OBJECT_DEPENDS` 让头变了必重编；缺文件时 `message(WARNING)` 响亮说明原因+命令 | `05-build-qt-*.log`：configure 行 `VEdit: Qt/ABI compile-only guard enabled (in-tree header: …/crates/vd-ffi/include/vd_ffi.h)`，编译行 `[47/288] Building CXX object CMakeFiles/vedit_qt_abi_guard.dir/crates/vd-ffi/tests/qt_abi_guard.cpp.o`；`06-qt-abi-guard-*.log`：目标编译 OK + **negative control**（同 TU + 带 `signals` 字段的头副本）**exit=1**，错误正是 `qtmetamacros.h: … define signals` 的宏碰撞；Rust 单测仍在门禁里跑（`Running tests/qt_abi_guard.rs`） |
| ⑦ | 不吸收 T19 的标签会话持久化 | 本任务的写域与 diff 未触碰 `session_store.*` / `EditorPane` 的会话恢复逻辑；`editor_area` 的改动只涉及面包屑 widget 与 `updateBreadcrumb()` | `--selfcheck` 245/0（含 T19/T12 会话与 teardown 断言）；改动文件清单见 §7 |

## 3. 命令与退出码（全部在 `vedit-qt6-build:local`；offscreen，除门禁自带的 xcb smoke）

| # | 命令 | 观察结果 / exit | 日志 |
|---|---|---|---|
| 1 | `bash scripts/build-qt.sh` | `[qt6] PASS: … configure + build + ctest (incl. the C ABI smoke) + xcb/software-rendering smoke`；`100% tests passed, 0 tests failed out of 37`；**BUILD_QT_EXIT=0** | `05-build-qt-20261102.log` |
| 2 | `bash build/gates/typecheck.sh` | 两项实质检查绿：Qt6 `[qt6] PASS … warnings-as-errors`；clippy `Finished dev profile … 1.65s`（无 error）。脚本整体 **TYPECHECK_EXIT=1**，唯一红 = `[typecheck] FAIL: crates/vd-ffi/include/vd_ffi.h drifted from its source`（committed vs worktree 的**仓库状态**检查；diff 里是 `vd_project_expand_v2`/项目变量表等**其他 lane** 的在飞 ABI 改动，本任务未改任何 Rust/ABI 源） | `07-typecheck-20261102.log` |
| 3 | search selftest（改前基线） | `WORKSPACE-SEARCH-SELFTEST ok=0 checks=106 failures=4 skipped=2` | `00-search-baseline-20261102.log` |
| 4 | search selftest（改后） | `WORKSPACE-SEARCH-SELFTEST ok=1 checks=106 failures=0 skipped=2`，exit 0 | `01-search-after-jump-unify-20261102.log` |
| 5 | `vedit-app --selfcheck`（回归） | `APP-SHELL-SELFTEST ok=1 checks=245 failures=0`，exit 0；`fold.native.breadcrumb.pane-bar PASS m0 > inc` | `02-app-selfcheck-20261102.log` |
| 6 | 探针构建（`probe/build-t20.sh`） | `PROBE_BUILD_OK`，exit 0；二进制 `target/t20-probe/t20-shell-probe` | `04-probe-build-20261102.log` |
| 7 | 探针运行（`probe/run-t20.sh`） | `50 checks, 0 failure(s)`，**PROBE_EXIT=0** | `03-probe-run-20261102.log` |
| 8 | search 独立构建（无 FFI/shell） | `CONFIGURE_EXIT=0`、`BUILD_EXIT=0` | `08-search-standalone-20261102.log` |
| 9 | ABI guard 目标 + negative control | 目标编译 OK；negative control `exit=1`（`signals` 宏碰撞）；Rust 单测在门禁 Rust 阶段运行 | `06-qt-abi-guard-20261102.log`、`05-…log` |

## 4. 探针断言摘要（50 项全绿，节选关键项）

```
t20.jump-target-registered — jump::hasTarget()
t20.two-panes-two-tabs … panes=2（左 alpha+beta，右 gamma+delta，各 2 标签）
t20.search-jump-right-file — …/alpha.sv（alpha 原本只在左窗格）
t20.search-jump-right-tab — tabs=3（活动右窗格新开标签，左窗格不变）
t20.search-jump-cursor — cursor=11 expected=11
t20.bar.segments — segments=2；t20.bar.module-segment — alpha；t20.bar.function-segment — inc
t20.bar.click-delivered — 2（Delivered）；t20.bar.jump-line — cursor=3 expected=3
t20.diagnostics.produced — entries=1（真实 slang：undeclared identifier）
t20.diagnostics.tool-position — tool=8 source=8（工具行号 == 源码 `missing_signal` 行）
t20.diagnostics.left-file — …/beta.sv；t20.diagnostics.right-unchanged — …/alpha.sv
t20.diagnostics.cursor — cursor=8 expected=8
```

截图（1500×950，真实 shell 抓图）：`shots/01-dual-pane-2-tabs.png`、
`02-search-jump-active-pane.png`、`03a-breadcrumb-segments.png`、`03-breadcrumb-click.png`、
`04-diagnostics-jump-active-pane.png`。截图不是“看起来正确”的证据，断言以日志为准。

## 5. 诚实限制（本证据不证明什么）

1. **平台**：探针与 selftest 均为 `QT_QPA_PLATFORM=offscreen`；鼠标事件是 QTest 投递到真实控件的
   真实事件序列，但未在 xcb 下复跑（门禁的 xcb smoke 只证明窗口/编辑器可起）。
2. **面包屑语义**依赖 tree-sitter 语法开关与语法 lane；本树默认开启（`--selfcheck` 的 pane-bar 断
   言与探针一致）。语法 provider 缺失时 Bar 保持隐藏（`no-invention` 断言），不发明文本路径。
3. **诊断行的内容**来自真实 slang（`VEDIT_SLANG` 指向树内 `build/slang/out/slang`）；控制器是
   shell 自己 `installDiagnostics` 造的那个。**发现（不在本任务 scope，未修）**：CLI 批处理结果
   进入 `controller.cliDiagnostics_`，而 `DiagnosticsPanel` 只读 `controller.diagnostics()`，
   `displayDiagnostics()`（合并 CLI 结果）**没有面板消费方** —— 即批量 lint 结果可能不进
   Diagnostics 分组。这是 diagnostics lane 的既有缺口（T8/T30 归属），本任务只记录。
4. **真实编辑器 target** 现在是产品里唯一的 `EditorJumpTarget`（搜索 lane 文件内）——这是 A-1 授权
   的位置；它按“活动窗格”开文件，因此“正确窗格”= 当时活动的窗格（探针显式切换活动窗格验证）。
5. **树是脏的**（dirty=173，含其他 lane 在飞改动）；若之后产品代码继续变化，需按 §6 重建探针再复核。
6. `--selfcheck` 只作**回归**证据（245/0）；契约要求的挂载证据全部来自真实 shell 探针 + 截图。

## 6. 复现命令

```bash
cd /home/haroldzhao/MyProj/DshProj/VEdit
# ① 门禁（Rust 层 + Qt6 构建 + ctest + xcb smoke）
bash scripts/build-qt.sh
# ② 两项实质检查（Qt6 -Werror build + clippy）；注意 check_ffi_header_drift 是仓库状态检查
bash build/gates/typecheck.sh
# ③ search selftest（真实 KTextEditor 产品 provider + 真实 Rust 搜索服务）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build -e QT_QPA_PLATFORM=offscreen \
  -e XDG_RUNTIME_DIR=/vedit-build/target/qt6-build/xdg-runtime-search vedit-qt6-build:local \
  bash -lc './target/qt6-build/src/app/workspace-lane-search/vedit-workspace-search-selftest --selfcheck'
# ④ 真实 shell 探针（双窗格 × 2 标签 + 三个跳转生产者 + 截图）
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
  bash -lc 'bash evidence/t20-shell/probe/build-t20.sh'
bash evidence/t20-shell/probe/run-t20.sh
# ⑤ 独立 search lane 构建（无 FFI）—— ③ 的 -Werror 修复回归
docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local bash -lc \
  'cmake -S src/workspace/search -B target/t20-search-standalone -G Ninja && cmake --build target/t20-search-standalone'
```

期望：① ctest `100% tests passed, 0 tests failed out of 37`、exit 0；② Qt6 PASS + clippy 无 error
（header drift 单独判定，见 §3）；③ `ok=1 checks=106 failures=0`；④ `50 checks, 0 failure(s)`；
⑤ 配置/构建 exit 0。

## 7. 改动文件（全部在契约 scope 内）

`CMakeLists.txt`（A-11 compile-only target）、`src/app/CMakeLists.txt`（面包屑 lane 授权 hunk）、
`src/app/editor_area.{h,cpp}`（Bar 挂载 + `breadcrumbText`）、
`src/editor/kernel/fold_breadcrumb.{h,cpp}`（`pathEntries()` + 属性桥）、
`src/workspace/search/{search_types.h, search_panel.h, search_panel.cpp, search_jump_target.h,
search_jump_target.cpp, search_integration.cpp, search_selftest.cpp, global_search.cpp, CMakeLists.txt,
README.md}`、`evidence/t20-shell/**`。

`crates/vd-ffi/tests/qt_abi_guard.{cpp,rs}` 是 A-11 既有实现（未跟踪、本任务**未修改**），本任务只把
`.cpp` 作为 compile-only target 编进默认构建。未改任何测试断言/门禁；T19 会话持久化未动。

## 8. 证据索引（`evidence/t20-shell/`）

| 文件 | 内容 |
|---|---|
| `00-search-baseline-20261102.log` | 改前：106 checks / 4 failures |
| `01-search-after-jump-unify-20261102.log` | 改后：106 checks / 0 failures |
| `02-app-selfcheck-20261102.log` | `--selfcheck` 245/0（含面包屑 pane-bar 回归） |
| `03-probe-run-20261102.log` | 真实 shell 探针：50 checks / 0 failures |
| `04-probe-build-20261102.log` / `probe/build-t20.sh` / `probe/run-t20.sh` | 探针构建/运行 |
| `05-build-qt-20261102.log` | 门禁：ctest 37/37 + xcb smoke，exit 0 |
| `06-qt-abi-guard-20261102.log` | A-11 目标编译 + negative control |
| `07-typecheck-20261102.log` | 两项实质检查绿；header drift 红（状态检查） |
| `08-search-standalone-20261102.log` | 独立 lane 构建（A-3 修复证明） |
| `09-metadata-20261102.txt` | 树状态、image id、源码/产物 mtime 与 sha |
| `fixture/{alpha,beta,gamma,delta}.sv` | 探针 fixture（beta 带真实 slang 错误） |
| `probe/t20_shell_probe.cpp` | 真实 shell 探针源码 |
| `shots/01..04 + 03a` | 双窗格/跳转/面包屑/诊断 截图 |
