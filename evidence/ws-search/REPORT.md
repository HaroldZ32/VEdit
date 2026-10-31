collected_at: unknown (header added retroactively at 2026-10-31T10:19:00Z)
git_head: 9957f5fe4d63c17c681e8753288262fa2c00955e
git_status_porcelain:
 M CMakeLists.txt
 M Cargo.lock
 M Cargo.toml
 M THIRD_PARTY_NOTICES.md
 M docs/decisions/r2-license.md
 M docs/handover/README.md
 M evidence/m0-build/build-logs/21-qt6-image-id.txt
 M evidence/m0-build/build-logs/31-qt6-configure.log
 M evidence/m0-build/build-logs/32-qt6-build.log
 M evidence/m0-build/build-logs/33-qt6-ctest.log
 M evidence/m0-build/build-logs/34-qt6-elf.txt
 M evidence/syn-grammars/00-pins.txt
 M evidence/syn-grammars/01-build.log
 M evidence/syn-grammars/02-tests.log
 M evidence/syn-grammars/03-manifest.txt
 M evidence/syn-grammars/04-manifest-none.txt
 M evidence/syn-grammars/04-manifest-static.txt
 M evidence/syn-grammars/04-manifest-systemverilog.txt
 M evidence/syn-grammars/04-manifest-verilog.txt
 M evidence/syn-grammars/04-swap-proof.txt
 M evidence/syn-grammars/05-layering.txt
 M evidence/syn-grammars/06-licensing.txt
 M evidence/syn-grammars/07-conclusion.md
 M evidence/syn-grammars/09-result.txt
 M evidence/term-panel/00-pins.txt
 M evidence/term-panel/01-configure.log
 M evidence/term-panel/02-build.log
 M evidence/term-panel/03-ctest.log
 M evidence/term-panel/03b-checks.log
 M evidence/term-panel/04-elf-needed.txt
 M evidence/term-panel/05-no-network.txt
 M evidence/term-panel/06-xcb-and-screenshots.log
 M evidence/term-panel/07-conclusion.md
 M evidence/term-panel/08-structure.txt
 M evidence/term-panel/09-root-build-with-lane.log
 M evidence/ws-filetree/00-pins.txt
 M evidence/ws-filetree/00-steps.txt
 M evidence/ws-filetree/01-configure.log
 M evidence/ws-filetree/02-build.log
 M evidence/ws-filetree/03-ctest.log
 M evidence/ws-filetree/03a-tests-full.log
 M evidence/ws-filetree/04-fixture.log
 M evidence/ws-filetree/05-screenshots.log
 M evidence/ws-filetree/06-permissions-nonroot.log
 M evidence/ws-filetree/07a-gate-qt6-cxx-pre-seam.log
 M evidence/ws-filetree/08-real-shell-seamcheck.log
 M evidence/ws-filetree/09-real-app-install.log
 M evidence/ws-filetree/10-rootcheck-configure.log
 M evidence/ws-filetree/11-rootcheck-build.log
 M evidence/ws-filetree/12-gate-build-qt-final.log
 M evidence/ws-filetree/12a-gate-build-qt-first.log
 M evidence/ws-filetree/13-gate-qt6-cxx-final.log
 M evidence/ws-filetree/14-real-shell-check-run.log
 M evidence/ws-filetree/REPORT.md
 M evidence/ws-palette/00-pins.txt
 M evidence/ws-palette/30-build-qt.log
 M evidence/ws-palette/31-bench.log
 M evidence/ws-palette/32-ctest-palette.log
 M evidence/ws-palette/33-elf.txt
 M evidence/ws-palette/34-artifacts.txt
 M evidence/ws-palette/36-app-seam-diff.md
 M evidence/ws-palette/37-gate-qt6-cxx.log
 M evidence/ws-palette/REPORT.md
 M evidence/ws-palette/SEAM.md
 M evidence/ws-search/04-selfcheck-root.log
 M evidence/ws-search/05-selfcheck-nonroot.log
 M evidence/ws-search/06-screenshots.log
 M evidence/ws-search/07-screenshots-nonroot.log
 M evidence/ws-search/08-build-qt.log
 M evidence/ws-search/09-structure.txt
 M evidence/ws-search/10-ctest-root-search.log
 M evidence/ws-search/13-integration-proof.log
 M evidence/ws-search/14-standalone.log
 M evidence/ws-search/REPORT.md
 M scripts/build-qt-in-container.sh
 M scripts/build-qt.sh
 M src/app/CMakeLists.txt
 M src/app/main.cpp
?? cmake/VEditRustFfi.cmake
?? crates/vd-ffi/
?? crates/vd-search/
?? crates/vd-settings/
?? crates/vd-vcs/
?? docs/handover/appendix/amendments-rev2.md
?? evidence/docs-amendments/
?? evidence/ffi-abi/
?? evidence/fixture/
?? evidence/m0-build/build-logs/37-qt6-rust-abi.txt
?? evidence/verdicts/
?? scripts/verify-fixture.sh
?? tests/fixtures/
backfill: 补记 —— C9 头为事后补齐；git_head / git_status_porcelain 为补记时刻快照，非原始采集时刻；原始采集时刻不可回溯，文件 mtime 2026-10-31T04:13:38Z 仅供参考
---
# ws-search 证据报告（任务 T23 / 验收 A2c）

**结论：交付成立。** 全仓门禁 `bash scripts/build-qt.sh` **exit 0**（7/7 ctest，
含本 lane 的 `vedit-workspace-search-selftest`；xcb + llvmpipe 冒烟 PASS），
`src/workspace/search/` 已按 `evidence/ws-palette/SEAM.md` 接入真实 `vedit-app`
（配置日志 `workspace lane 'search' wired (vedit_workspace_search)`）。

- 契约：`.mpd/cellwork/cell-20261031T014507403Z/contracts/ws-search.md`
- 需求：`requirements.md` rev 2 · A2c
- 写域：`src/workspace/search/` + `evidence/ws-search/`（未改 `src/app/`、根 CMake、测试或门禁）
- 设计说明：`src/workspace/search/README.md`

## 1. 门禁（命令 / 观察 / 退出码）

| # | 命令 | 观察结果 | 退出码 | 日志 |
|---|---|---|---|---|
| 1 | `bash scripts/build-qt.sh`（repo 声明的 C++ 门禁；pinned 镜像 → configure → build → ctest → xcb 冒烟） | `7/7` ctest 通过；`Test #7: vedit-workspace-search-selftest ... Passed 0.31 sec`；`[qt6] PASS: configure + build + ctest + xcb/software-rendering smoke`；`M0-SMOKE ok=1`；配置日志 `workspace lane 'search' wired (vedit_workspace_search)`、`workspace integration enabled` | **0** | `08-build-qt.log` |
| 2 | `ctest --test-dir target/qt6-build -R vedit-workspace-search-selftest -V`（root 构建） | `100% tests passed, 0 tests failed out of 1`；逐条 CHECK 输出，末行 `ok=1 checks=82 failures=0 skipped=2` | **0** | `10-ctest-root-search.log` |
| 3 | `vedit-workspace-search-selftest --selfcheck`（root 构建，offscreen，root 用户） | `WORKSPACE-SEARCH-SELFTEST ok=1 checks=82 failures=0 skipped=2` | **0** | `04-selfcheck-root.log` |
| 4 | 同上，`setpriv --reuid=1000`（非 root，权限用例生效） | `ok=1 checks=88 failures=0 skipped=0` | **0** | `05-selfcheck-nonroot.log` |
| 5 | `--screenshots evidence/ws-search`（xcb + llvmpipe + Xvfb） | `SCREENSHOTS ok=1 failures=0`，4 张 1280x860 PNG | **0** | `06-screenshots.log` |
| 6 | 同上，非 root（出 `05-search-unreadable-root.png`） | `SCREENSHOTS ok=1 failures=0`，5 张 PNG | **0** | `07-screenshots-nonroot.log` |
| 7 | 独立配置/构建/ctest（核心 lane，无 shell） | `100% tests passed, 0 tests failed out of 1` | **0** | `14-standalone.log` |
| 8 | `readelf -d vedit-app \| grep -E 'KTextEditor\|tree-sitter' \| wc -l` | `0`（SEAM §5.3：app 不得多链编辑器/语法引擎） | **0** | `13-integration-proof.log` |
| 9 | `strings vedit-app \| grep -F 'No project root'` | 命中 2 行：面板真的被链进 `vedit-app` | **0** | `13-integration-proof.log` |

注：门禁脚本自身会写 `evidence/m0-build/build-logs/`（T10/T13 既有行为），
本 lane 没有直接写入该目录。

## 2. 契约 A2c 逐条对照

### (a) 指定根目录按文本搜索，返回文件 + 行号 + 命中片段

- 结果模型 `SearchHit{filePath, relativePath, line(1-based), column(1-based), text}`；
  面板按文件分组：文件行（相对路径 + 命中数）→ 子行（行号 + 片段）。
- 实测（`04-selfcheck-root.log`）：`panel.hits.count=7`、`panel.tree.files=6`、
  `panel.tree.one-row-per-hit=7`、`panel.hit.line-number`、`panel.hit.column`
  （`a.v` 第 2 行第 3 列）、`panel.hit.snippet`、`panel.hit.unicode-file` /
  `panel.hit.unicode-column`（`sub/中文.v` 第 1 行第 7 列，UTF-8 路径与列号）。
- 同一行多次出现只出 1 条：`panel.hits.one-per-line`（`double.txt`）。
- 剪枝：`.git/`、`build/` 不返回（`panel.tree.no-git`、`panel.tree.no-build`）。
- 过滤：大小写（`panel.case-sensitive.*`）、仅 Verilog/SystemVerilog
  （`panel.verilog-only.*`）、Unicode 查询（`panel.unicode.query`）。
- 截图 `01-search-results.png`：真实 `ApplicationShell` 的 Search dock 内
  `7 hits in 6 of 6 files — 0 ms (1 too large skipped, 1 binary skipped)`。

### (b) 点击结果跳转到对应文件与行

- 点击/回车 → `activateHit(index)` → `jumpRequested(JumpRequest)` + 可选 `IJumpTarget`。
- 实测（QTest 真鼠标点击，不是直接调函数）：`jump.row-visible` →
  `jump.click.recorded` → `jump.click.file=/b.sv`、`jump.click.line=4`、
  `jump.click.column=1`；点文件行不跳转：`jump.file-row.no-jump`。
- 消费端路径：`jump.demo.opened`（目标读过文件并显示）、`jump.demo.line`
  （光标落到命中行）——即 edit-core 将来要实现的行为，本 lane 用 demo 目标证明整条链。
- 截图 `03-search-jump.png`：编辑器 pane 已打开 `b.sv` 且命中行被选中。

### (c) 无结果时显示明确空状态（可辨，不是一句 "No results"）

| 场景 | 文案（实现于 `search_panel.cpp`） | 自检 |
|---|---|---|
| 无工程根 | `No project root. Open a project to search its files.` | `empty.no-project.*` |
| 查询为空 | `Enter a query and press Search.` | `empty.query.*` |
| 完成但无命中 | `No results. Nothing matched "<q>" in <root>.` | `empty.no-results.*` |
| 根目录不可读 | `Search failed: search root is not readable: <root>` | `empty.unreadable-root.*` |
| 时间预算内无命中 | `No results within the <N> ms time budget.` | 文案在实现中；状态由 `large.timeout.flag/state` 断言 |
| 取消且尚无命中 | `Search cancelled before any hit.` | 文案在实现中；状态由 `large.cancel.state` 断言 |

截图：`02-search-empty-state.png`（无命中，结果树隐藏、空状态可见）、
`04-search-no-project.png`（无工程）、`05-search-unreadable-root.png`（不可读根）。

### (d) 搜索可取消、大工程不冻结 UI

- 扫描在 `QThread` 上：`large.worker-thread`（`ranOnWorkerThread()` 断言，不是注释）。
- 不阻塞：96 MiB 文件上 `start()` 返回 `< 100 ms`（`large.start.non-blocking`）；
  2000 文件 / 2000 命中目录树上同样 `< 100 ms`（`many.start.non-blocking`），
  完成后 `many.hits=2000`、`many.files-scanned=2000`、`many.elapsed-bounded`。
- 取消：`large.cancel.returns-fast`（`cancel()` < 100 ms）、
  `large.cancel.finishes`、`large.cancel.state=Cancelled`、`large.cancel.bounded`。
- 时间预算：`maxDurationMs` 用尽即停，`large.timeout.flag`（`timedOut=true`）、
  `large.timeout.state=Done`、`large.timeout.bounded`（远小于总扫描时间）。

## 3. 两条质量红线（captain 指定）

### 3.1 大文件 / 二进制：有可观测的超时与跳过行为

| 行为 | 实现 | 自检 |
|---|---|---|
| > `maxFileBytes`（默认 8 MiB）不读进内存 | 先看 `QFileInfo::size()`，跳过并计数 | `large.skipped-by-default`（96 MiB 文件）、`panel.summary.skipped-too-large` |
| 二进制（含 NUL 字节）跳过 | 读入后检测 `\0`（仅在限额内的文件） | `panel.summary.skipped-binary`、`panel.tree.no-binary` |
| 不可读文件/目录跳过并计数 | 不用 `QDir::Readable` 过滤列表，显式计数 | `entries.unreadable.counted=2`（非 root 运行） |
| 结果上限 | `maxResults`（默认 2000）→ `truncated=true` | `limit.results=3`、`limit.truncated-flag`、`limit.summary-text` |
| 时间预算 | `maxDurationMs`（默认 15 s）→ `timedOut=true` | `large.timeout.*` |
| 可观测性 | 面板摘要行追加 `(1 too large skipped, 1 binary skipped)` 等 | `panel.summary.text.observable`、`entries.unreadable.text` |

面板摘要示例（截图 01）：`7 hits in 6 of 6 files — 0 ms (1 too large skipped, 1 binary skipped)`。

### 3.2 空状态是真实现

见 §2(c)：无工程 / 空查询 / 无命中 / 根不可读 四种状态各有独立文案与自检，
结果树与空状态互斥（`empty.no-results.tree-hidden`）。

## 4. 外壳接缝（按 B 的 SEAM 落地，未改 `src/app/`）

- `src/workspace/search/search_integration.{h,cpp}`：
  `void vedit::workspace::installSearch(ApplicationShell&, const IntegrationOptions&)`，
  只做 `shell.panels()->setFactory("search", [root]{ return createSearchPanel(root); })`；
  **不插入 dock、不改布局**，面板保持"按需插入"。
- 幂等：重复 install 只是重设同一个工厂（SEAM §2.4 要求）。
- 实测（`04-selfcheck-root.log`）：`seam.dock-created`、`seam.not-placeholder`、
  `seam.factory-installed`、`seam.root-applied`、`seam.search-in-shell`（7 命中）——
  用的是真实 `installSearch()`（即 `installPanels()` 调用的同一个函数）。
- 端到端接线证据：配置日志 `workspace lane 'search' wired (vedit_workspace_search)`；
  `strings vedit-app` 含面板文案；`readelf` 中 KTextEditor/tree-sitter NEEDED 计数 **0**。
- `src/app/main.cpp:392` 的 `installPanels(shell, {...})` 是 B 的接缝改动（我只读未改）。

## 5. 写域合规

`git status --short -- src/workspace/search evidence/ws-search` 只列出这两个目录（均为新增）；
没有任何契约外的源文件被本 lane 修改。冻结契约的 allowed write scopes 即这两条路径。

## 6. 红点修复记录（门禁曾因本 lane 变红）

- 现象：`src/workspace/search/search_integration.cpp:18: 'createSearchPanel' is not a
  member of 'vedit::workspace::search'`。
- 根因：`search_panel.h` 中我把工厂声明误放进了 `SearchPanel` 类体内（`signals:` 之后、
  `private:` 之前），于是它是成员函数；`search_integration.cpp` 却按自由函数调用。
- 修复：声明移到命名空间作用域（类外），定义本来就是自由函数；未注释代码、未放宽
  `-Werror`、未改测试。
- 修复后全仓门禁 `exit 0`（`08-build-qt.log`）。

## 7. 残余风险 / 明确未覆盖

1. **跳转消费端仍是接缝**：`src/editor`（edit-core）尚未落地，`EditorArea` 没有
   open-file API；本 lane 用 `jumpRequested` + `IJumpTarget` 定义契约，自检用 demo
   目标证明"点击→打开文件→定位行"。真实 KTextEditor 接入后由 editor lane 实现目标。
2. **工程根在 install 时捕获**：当前没有工程模型/切换工程信号，运行期换工程需
   `SearchPanel::setRootPath()`（已公开）；等工程模型落地后应接到 root 变更信号。
3. **不解析 `.gitignore`**：只按固定目录名剪枝（`.git build target node_modules` 等）。
4. **单行只报一次、无正则/整词**：A2c 只要求文本搜索；跨文件替换是需求非目标 5。
5. **并发搜索**：同一时刻一个扫描；新搜索会取消并等待旧扫描（`start()` 返回 false
   仅在旧扫描 5 s 内未停时，极端情况）。
6. **截图 CJK 字体**：构建镜像无 CJK 字体，`sub/中文.v` 文件名在 PNG 中显示为方框；
   数据正确性由 `panel.hit.unicode-*`（路径/列号）断言。字体随包属打包 lane。
7. **权限用例在 root 下 SKIP**：镜像默认 root；`05-selfcheck-nonroot.log`
   （`setpriv --reuid=1000`）覆盖同一套用例，88 checks / 0 skip。
8. **10k 文件基准属 A2b（palette）**；本 lane 用 2000 文件目录树证明大工程路径
   （start <100 ms、2000 命中、elapsed 有界），未重复 palettes 的 10k 基准。

## 8. 证据清单（`evidence/ws-search/`）

| 文件 | 内容 |
|---|---|
| `01-search-results.png` | 真实 shell Search dock：7 命中 / 6 文件 / 行号 / 片段 / 跳过计数 |
| `02-search-empty-state.png` | 无命中空状态（结果树隐藏） |
| `03-search-jump.png` | 点击跳转后编辑器已打开 `b.sv` 且命中行选中 |
| `04-search-no-project.png` | 无工程根空状态 |
| `05-search-unreadable-root.png` | 根目录不可读（失败态，与"无结果"可辨） |
| `04-selfcheck-root.log` | root 构建自检：82 checks / 0 fail / 2 skip |
| `05-selfcheck-nonroot.log` | 非 root 自检：88 checks / 0 fail / 0 skip（权限用例生效） |
| `06-screenshots.log` / `07-screenshots-nonroot.log` | 截图命令输出与退出码 |
| `08-build-qt.log` | 全仓门禁 `bash scripts/build-qt.sh`：7/7 ctest、xcb 冒烟、exit 0 |
| `10-ctest-root-search.log` | root 构建里本 lane 的 ctest -V 输出 |
| `13-integration-proof.log` | `readelf` KTextEditor/tree-sitter=0；`strings vedit-app` 含面板文案 |
| `14-standalone.log` | 独立配置/构建/ctest（核心 lane）exit 0 |
| `09-structure.txt` | 文件清单、行数、无 KSyntaxHighlighting 调用点 |

## 9. 隐藏陷阱对照

- **`.v` → "V"（vlang）**（`evidence/m0-build/REPORT.md` §4.9）：本 lane 的文件类型
  判断完全不用 KSyntaxHighlighting，`isVerilogFileName()` 是显式扩展名表
  （`.v/.vh/.sv/.svh`，大小写不敏感）；`09-structure.txt` 记录 lane 内无
  `definitionForFileName` 调用点（唯一出现是一行说明注释）。库只链接 Qt6 Widgets。
- **`vedit-app` 不链接 KTextEditor/tree-sitter**：`13-integration-proof.log` 计数 0。
