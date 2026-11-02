# T118 — P0 LINT：真机“删一个逗号面板零反应”的定位与修复

- Cell: `cell-20261031T085543161Z` · Board task: **T118** · 日期: 2026-11-01（Asia/Shanghai）
- 基线: `HEAD = 60c7ae9` · 环境: pinned 镜像 `vedit-qt6-build:local`（Qt 6.10.2 / KF6 6.24 / verible 0.0.0-… 本地 recon），headless offscreen
- 结论一句话：**三条假设全部成立**，且它们串联成“点了没反应”——(H2) 打开文件根本不触发分析 → (H1) 即便触发也只读磁盘文本 → (H3) 即便跑完，面板也从不刷新。
- 修复后：**打开文件即自动分析；编辑缓冲区（不保存）约 1 s 内面板更新；状态行写明“分析了哪个文件、几点、用的是缓冲区还是已保存文件、几条诊断”。**

---

## 1. 一张图：触发时机与数据流（修复前 → 修复后）

修复前（用户看到的“什么都没发生”）：

```
 user opens a .sv in the GUI            (double-click AppImage; NO --project)
        │
        │  editor loads the document (syntax colours work)
        ▼
   ┌──────────────────┐        no signal / no call        ┌────────────────────────┐
   │ src/app editor   │ ────────────────────────────────▶ │ diagnostics lane        │
   │ (KTextEditor)    │                                   │  (nothing runs)         │
   └──────────────────┘                                   └────────────────────────┘
        │                                                        ▲
        │  user deletes a comma (BUFFER only)                     │ only if --project:
        ▼                                                        │ ProjectFileSetPoller
   buffer differs from disk ── no trigger ──▶ no LSP didChange   │ (once, at startup)
                                                                  │
   DiagnosticsPanel: refreshFromController() ONLY in its ctor ───┘
   → outline empty, status "No symbols yet — open a file and run Reparse Project."
```

修复后：

```
 user opens/edits a file in the ACTIVE pane
        │
        ▼
 ActiveFileWatcher (QTimer 400 ms, per shell; no project needed)
   ├─ path changed   ──▶ analyze NOW
   └─ text changed   ──▶ debounce 700 ms ──▶ analyze
        │  reads: pane->currentFilePath(), editor buffer text
        │         (KTextEditor::View::document()->text() | QPlainTextEdit::toPlainText())
        │  compares with the file on disk → dirty flag (for the status text)
        ▼
 DiagnosticsController::analyzeFile(file, bufferText, dirty)
   ├─ session over exactly THIS file (workspace root = file dir when no project)
   ▼  when session Ready
 LspSessionBackend::changeBuffer(file, bufferText)  → vd_lsp_session_change (C ABI)
   → vd-lsp Session::change_document → textDocument/didChange (full text, version++)
        │                                          ▲ nothing is read from disk
        ▼                                          │
 verible-verilog-ls publishes textDocument/publishDiagnostics
        │
        ▼
 requestDiagnostics(file) + requestSymbols(file)  (re-asked until the new snapshot lands)
        │
        ▼
 DiagnosticsController::refresh()  ◀── DiagnosticsPanel's QTimer (300 ms, self-owned)
        │
        ▼
 panel status:  "<file> — analyzed HH:MM:SS (editor buffer, not saved yet): N diagnostic(s)"
                (or "(saved file)" when the buffer equals disk)
 outline model: symbols of the current file
```

图层清单：`src/app`（编辑器，未改）→ `src/workspace/diagnostics`（watcher / controller / panel / FFI backend）→ `crates/vd-ffi/src/lsp.rs`（`vd_lsp_session_change`）→ `crates/vd-lsp`（`Session::change_document` → didChange）→ `verible-verilog-ls`（publishDiagnostics）→ 回到面板。

## 2. 用户打开一个 `.sv` 后“应该发生什么”的时序（修复前 vs 修复后）

| 时刻 | 修复前 | 修复后 |
|---|---|---|
| t+0 ms | 文档加载完成、高亮出现 | 文档加载完成、高亮出现 |
| t+≈400 ms | **没有任何动作**（无 project 时连会话都不存在） | `ActiveFileWatcher` 发现新文件 → `analyzeFile(file, buffer, dirty=false)` |
| t+≈0.4–3 s | — | 文件级 LSP 会话启动（no project 时以文件目录为 workspace root）→ `didOpen`（磁盘文本）→ 首批诊断 |
| t+≈0.7–4 s | — | `requestSymbols` → outline 出现当前文件的符号 |
| t+≈0.7–4 s | outline 空；面板写 "open a file and run Reparse Project" | 面板写 "<file> — analyzed HH:MM:SS (saved file): N diagnostic(s)" |
| 用户删一个逗号（t+E） | 0 反应（无触发、无 didChange、面板不刷新） | t+E+0.7 s（防抖）→ `didChange(缓冲区文本)` → t+E+≈1–2 s 面板变为 "(editor buffer, not saved yet): N diagnostic(s)"，语法错误可见 |
| 用户 Ctrl+S | 无动作 | 磁盘变化 → 下一次分析 dirty=false，状态改为 "(saved file)" |
| 用户打开另一个文件 | 无动作 | path 变化 → 立即分析新文件（旧会话被替换） |
| 手动 “Reparse Project” | 工程级一次（结果也到不了面板） | 仍可用：工程文件集全量分析；面板现在会自动刷新 |

## 3. 三条假设的结论与证据

### H1 — lint 跑的是磁盘文本，不是编辑器缓冲区：**成立**
- 代码事实（修复前）：`crates/vd-lsp/src/session.rs` 的 `didOpen` 用 `std::fs::read_to_string(path)` 取文本；C ABI 里 `vd_lsp_session_change` 出现 **0** 次（客户端有 `change_document`，但会话/FFI 从未调用）。
- 证据：`02-h1-disk-vs-buffer.log`。
- 修复：新增 `vd_lsp_session_change(service, session, path, text)` → `Session::change_document` → 全文 `didChange`（version++；不读磁盘）；Qt 后端 `changeBuffer()`；控制器在请求诊断前先推缓冲区。
- 修复后的真实证据（Rust 集成测试，真 verible）：`tests/buffer_change.rs` 2 passed —— 在缓冲区删逗号（不保存）→ 出现语法诊断，磁盘文件字节不变；改回 → 诊断清除。

### H2 — lint 根本没被触发：**成立（真机主因）**
- 真机（双击 AppImage，没有 `--project`）：`ProjectFileSetPoller` 根本不会创建（只在 `projectRoot` 非空时创建），编辑器打开文件也不会通知 diagnostics lane。
- 实测（`01-h2-trigger-probe.log`）：不传 `--project` 跑产品 6 s，`verible-verilog-ls` 子进程数 **0**；传 `--project` 时 **1**。
- 面板的 "No symbols yet — open a file and run Reparse Project." 正是这个状态；而且“Reparse Project”这个动作对“打开的文件”并不存在自动触发（措辞本身也把文件级和工程级混在一起）。
- 修复：新增 `ActiveFileWatcher`（每个 shell 一个，400 ms 轮询活动 pane；打开立即分析、编辑防抖 700 ms），**不再依赖 `--project`**；没有 project root 时用文件所在目录作为 LSP workspace root。空状态文案改为 “open a file; analysis starts automatically.”。

### H3 — 跑了但结果没到面板：**成立**
- 代码事实（修复前）：`DiagnosticsPanel` 在整个文件里只有构造末尾调用一次 `refreshFromController()`；`installDiagnostics` 只在创建面板的工厂里调用一次 `controller->refresh()`。异步的 LSP/slang 结果产生后，**没有任何轮询**把它们送进控件。
- 证据：`03-h3-panel-refresh.log`（`git show HEAD:…` 的旧代码 vs 现在的 `pollTimer_`）。
- 修复：面板自带 300 ms `QTimer`：`controller->refresh()` + `refreshFromController()`；状态行与空状态按“已分析文件/来源/诊断数”显示。

## 4. 修复后的端到端证据（全部 headless，修复后实跑）

| 测试 | 观察 |
|---|---|
| `vedit-diagnostics-product-seam`（真实 `ApplicationShell` + `installDiagnostics`，**无 `--project`**，用文档接缝打开文件，再只改缓冲区） | `edited.sv — analyzed 11:10:50 (saved file): 0 diagnostic(s)` → 删逗号后 `… (editor buffer, not saved yet): 1 diagnostic(s)`；磁盘 76 字节未变；`checks=9 failures=0` |
| `vedit-diagnostics-ffi-selftest`（真实 verible + 真 C ABI） | 缓冲区删逗号 → `syntax error at "b"`（severity 1）；磁盘未被修改（71 vs 70 字节）；改回 → 错误清零；`checks=30 failures=0` |
| `vedit-workspace-diagnostics-selftest`（fakes，面板自带定时器驱动） | `t118.panel-analyzed` / `t118.panel-source-is-buffer` / `t118.panel-counts-diagnostics` / `t118.panel-outline-filled` 全 PASS；`checks=39 failures=0` |
| 应用自身 | `APP-SHELL-SELFTEST ok=1 checks=200 failures=0`；`--hold`（有无 `--project`）均 exit 0（`05-app-selfcheck-and-hold.log`） |

`04-after-fix.log` 是上面三条测试的原始输出。

## 5. 与 T117（段错误）/ 折叠叠不回去 的关系（按额外要求报告，未越界修改）

- 本任务所有改动都在 diagnostics lane / `crates/vd-lsp` / `crates/vd-ffi/src/lsp.rs`，**没有触碰 `src/editor/**`**。
- 我为新加的两个定时器检查了生命周期：`ActiveFileWatcher` 与面板的 `pollTimer_` 都以 QObject 为父对象（随 shell/面板销毁自动停表），对象销毁期间没有事件循环，回调不会在悬空指针上触发；控制器由 shell 的子对象持有、先于 watcher 创建，正常销毁顺序下不会先于 watcher 消失。
- **没有发现 lint 路径与段错误/折叠同源的证据**：本任务不涉及 folding/视图生命周期代码。若 T117 需要，可让 Senior Engineer 2 对照本报告的定时器/父子关系结论。按“发现问题立即停手等裁定”的约定，我没有因为它们而停手——这里没有发现。
- **给 T117 的一条可复现线索（不是 lint 路径）**：根 ctest 复跑时 `vedit-workspace-search-selftest` **SEGFAULT（exit 139，core dumped）**，死在第 74 条 CHECK 左右：`jump.product.first-status PASS` 之后、紧接着 edit-core 的 open 日志（`[edit-core] open: read done … open: setText done …`）之后。即“产品路径打开文件到真实 editor 之后”崩溃；该调试二进制**没有安装 diagnostics 面板**（只装 search），所以与本次 lint 改动无关，而是 search/editor 交互。原始尾部见 `06-root-ctest.log`。

## 6. 回归与门禁

- 根 ctest（`06-root-ctest.log`）：31 个测试中 30 通过；**唯一失败 `vedit-workspace-search-selftest` 与本任务无关**：它的 `jump.product.first-cursor-line/column` 两条断言失败，而该 lane 的 `search_selftest.cpp`(19:52)/`slowio.cpp`(19:37) 在我工作期间被**其他 lane 并发修改**（我的写域不含 search）。本任务新增的 `vedit-diagnostics-product-seam` 通过（28/31 位）。
- `typecheck`：见 `07-typecheck.log`。
- 既有 `line-length` / 语法诊断能力未受影响：`toolchain_selftest --lint-file` 与打包侧的真实 lint 不在本次改动路径上；真实 verible 的缓冲区诊断见 §4。

## 7. 真机上“删一个逗号”应该看到什么（给用户的话）

1. 打开 `.sv` 后 **约 0.5–4 秒内**，右侧 Outline 会自己出现符号，底部状态行变成
   `文件名 — analyzed HH:MM:SS (saved file): N diagnostic(s)`（不需要点任何菜单）。
2. 删掉一个逗号（**不用保存**）：约 **1–2 秒内**状态行变成
   `文件名 — analyzed HH:MM:SS (editor buffer, not saved yet): N diagnostic(s)`，
   其中 N 会比之前大（语法错误），Outlook 面板同时刷新。
3. 如果把逗号加回去：约 1–2 秒内错误消失（`N` 回落）；按 Ctrl+S 后来源标签变回 `(saved file)`。
4. 如果工具链缺失（罕见），状态行会写 “Diagnostics unavailable: … Fix: …”，而不是空白。

## 8. 证据清单（`evidence/lint-trigger/`）

| 文件 | 内容 |
|---|---|
| `00-pins.txt` | 时间、HEAD、镜像、verible sha256、写域与共享工作树状态 |
| `01-h2-trigger-probe.log` | 无 `--project` vs 有 `--project` 时语言服务器子进程数（0 vs 1） |
| `02-h1-disk-vs-buffer.log` | 修复前 `didOpen` 读盘、FFI 无 change API；修复后 Rust 真 verible 测试 2 passed |
| `03-h3-panel-refresh.log` | 修复前面板只在构造时刷新（`git show HEAD:…` 对照），修复后有 300 ms 轮询 |
| `04-after-fix.log` | 产品接缝测试 / FFI 真 verible 测试 / 假后端测试的原始 CHECK 行 |
| `05-app-selfcheck-and-hold.log` | 应用自检 200/0 与两种 `--hold` 的干净退出 |
| `06-root-ctest.log` | 根 ctest 原始输出（含“唯一失败是 search lane 的并发改动”这一事实） |
| `07-typecheck.log` | `bash build/gates/typecheck.sh` 原始输出与退出码 |
