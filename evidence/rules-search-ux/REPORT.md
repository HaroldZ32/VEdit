collected_at: 2026-11-01T18:25:30+08:00
git_head: 60c7ae9c95fe769e2bd6c86dd4c56e1514603e44
git_branch: feat/qt6-ktexteditor-migration
git_status:
   M AGENT.md
   M build/evidence/typecheck/00-run.txt
   M build/evidence/typecheck/10-typecheck-image-build.log
   M build/evidence/typecheck/11-typecheck-image-id.txt
   M crates/vd-config/src/project.rs
   M crates/vd-config/src/warnings.rs
   M crates/vd-ffi/include/vd_ffi.h
   M crates/vd-ffi/src/project.rs
   M evidence/ffi-abi/logs/10-rust-image.txt
   M evidence/ffi-abi/logs/19-rust-stage-console.log
   M evidence/ffi-abi/logs/20-cargo-build-release.log
   M evidence/ffi-abi/logs/21-staticlib.txt
   M evidence/ffi-abi/logs/23-cargo-test.log
   M evidence/ffi-abi/logs/26-vd_ffi.h
   M evidence/ffi-abi/logs/27-vd_ffi-comment-free.txt
   M evidence/m0-build/build-logs/20-qt6-image-build.log
   M evidence/m0-build/build-logs/21-qt6-image-id.txt
   M evidence/m0-build/build-logs/31-qt6-configure.log
   M evidence/m0-build/build-logs/32-qt6-build.log
   M evidence/m0-build/build-logs/33-qt6-ctest.log
   M evidence/m0-build/build-logs/34-qt6-elf.txt
   M evidence/m0-build/build-logs/37-qt6-rust-abi.txt
   M packaging/runtime/AppRun
   M packaging/runtime/selfcheck.sh
   M src/workspace/diagnostics/diagnostics_integration.cpp
   M src/workspace/filetree/filetree_integration.cpp
   M src/workspace/palette/README.md
   M src/workspace/palette/palette_integration.cpp
   M src/workspace/search/CMakeLists.txt
   M src/workspace/search/README.md
   M src/workspace/search/global_search.cpp
   M src/workspace/search/global_search.h
   M src/workspace/search/search_integration.cpp
  ?? evidence/locale-and-noise/
  ?? evidence/rules-search-ux/

# T114 报告 — 可选 `.rules.verible_lint` 缺失被当成 Error + 家目录路径泄露

- Cell: `cell-20261031T085543161Z` · Board task: **T114** · 来源：用户在真机 GUI 看到的
  `rules.search_stopped: /home/haroldzhao: no .rules.verible_lint found …`
- 执行: **Senior Engineer (instance 2)** · 日期: 2026-11-01
- 裁定：按 captain 的 (B) —— **从源头不再把“可选规则文件缺失”当 warning**；vd-ffi 投影过滤作纵深防御；
  `$HOME` 边界不动（安全边界，有注释 + AGENT.md 说明）；`lsp.rs:81` 只登记不改。

## 1. 结论（给用户的一句话）

**这条消息不会再出现在 GUI 上。** 普通工程（没有 `.rules.verible_lint`）的 “Diagnostics (N)” 列表里
不会再有任何 `rules.*` 条目，也不会出现家目录路径；只有“规则文件存在但读不了”这一真实问题时，
面板才会显示一条**不带路径**、并说明 **“falling back to built-in rules”（功能仍然可用）** 的提示。

⚠️ **重打包**：修复在 Rust 服务层，静态链接进 `vedit-app` → **现有 AppImage 不包含它，需要重打**。
按 captain 指示我没有触发打包；等实机另外四项观测一起确认后由 Lead 一次性提交/重打
（见 `09-repackaging.md`）。

## 2. 来源与修复

来源链路与 before/after 代码见 **`07-source-fix.md`**。摘要：

| 层 | before | after |
|---|---|---|
| `crates/vd-config/src/project.rs::discover_rules` | 找不到 `.rules.verible_lint` → `Warning{rules.search_stopped, message 含绝对 stop 路径}` | **不存在 → 完全静默**（内置规则生效）；**存在但不可读 → `rules.unreadable`**（`file: None`，消息用相对工程根/文件名的路径，文案含 “falling back to built-in rules”） |
| `crates/vd-config/src/warnings.rs` | — | `RulesSearchStopped` **保留**（机器契约形状不变，正常路径不再产生）；**追加** `RulesUnreadable`（兼容变更） |
| `crates/vd-ffi/src/project.rs` | 原样投影所有 warning | GUI 投影再过滤 `rules.search_stopped`（纵深防御；机器契约不变） |
| `AGENT.md` | — | 新增 “Project discovery: optional files are not errors (T114)”（可选文件语义 + `$HOME` 安全边界） |

**`$HOME` 边界是故意的，不是 bug**：`discover_rules` 的 doc 原文
*“The search stops at `$HOME` when `root` lives under it, otherwise at the filesystem root,
so it can never walk out of the user's tree.”* 未改动，并已写入 `AGENT.md`。

## 3. 控制组证据（全部带 C9 头）

| 对照 | 日志 | 观察 |
|---|---|---|
| **负向**（无 rules） | `02-vd-ffi-tests.log` | `NEGATIVE-CONTROL project_without_rules warnings=[]` —— GUI 投影无任何 `rules.*`，也没有 `/home/` |
| **正向**（有 rules） | 同上 | `POSITIVE-CONTROL project_with_rules warnings=[]` —— 被采纳且无“已找到”噪声 |
| **回归**（真实告警） | 同上 | `REGRESSION-CONTROL warnings=["filelist.unknown_token: /tmp/…/top.f:1: unsupported filelist token …"]` —— 仍到面板 |
| **unreadable 分支** | 同上 | `UNREADABLE-CONTROL warnings=["rules.unreadable: the Verilog lint rules file .rules.verible_lint could not be read; falling back to built-in rules"]` —— 无绝对路径、说明影响面 |
| **权限场景** | `01-vd-config-tests.log` | 容器内 root：`chmod-000 readable_by_this_process=true found=true warnings=[]`（可读分支）；普通账户会走 unreadable 分支——两条都断言，不静默 skip |
| **真实 lint 诊断不丢** | `05-diagnostics-real-lint.log` | `OBSERVED diagnostics=3 … 7/7 file(s) open, 3 diagnostic(s)`，`VER-DIAGNOSTICS-FFI-SELFTEST 23/23` |
| **line-length 仍在** | `03-vd-lsp-line-length.log` | `cargo test -p vd-lsp --test verible_ls` → **8 passed**（默认规则集报 line-length；显式 rules 文件可关掉它） |
| **面板路径未回退** | `04-project-lane.log` | `panel.diagnostics-match PASS`，`PROJECT-MODEL-SELFTEST 36/36` |

## 4. 逐 code 审计（`08-per-code-audit.md`）

**结论**：13 个 `WarningCode` 里只有 `rules.search_stopped` 属于“可选资源缺失”，已从正常路径移除；
其余（`filelist.*`、`config.*`、`xdg.invalid`）都是**用户显式配置/书写的内容有问题**或**声明的东西不可用**，
保持 warning 正确。`vedit.toml` 缺失不产生 warning；`.verible.hld` 无处理路径；
`vd-ffi/src/lsp.rs:81`（显式 rules 路径不存在 → Error）在 `src/` 无调用者、**当前 GUI 不可达** → 只登记不改。

## 5. 门禁

| 门 | 命令 | 结果 |
|---|---|---|
| 根 ctest | `ctest --test-dir target/qt6-build --output-on-failure` | **27/27 passed**（`06-ctest.log`，exit 0） |
| typecheck | `bash build/gates/typecheck.sh` | **PASS**（`10-typecheck.log`，exit 0） |
| Rust 测试 | `cargo test -p vd-config -p vd-ffi` | 全部通过（38 + 14 + 2 + 4 + 16 + 12 + 1；`01`/`02` 日志） |

## 6. 范围 / 诚实声明

- 改动：`crates/vd-config/src/{project.rs,warnings.rs}`（captain 授权）、`crates/vd-ffi/src/project.rs`、
  `AGENT.md`、`evidence/rules-search-ux/**`。
- **未改**：`crates/vd-ffi/src/lsp.rs`、`filelist.*` 语义、`$HOME` 边界、`src/syntax/**`、`src/workspace/**`、
  `src/editor/**`、`packaging/**`。未 commit/push、未打包。
- 无 GUI 像素级验证（headless）：结论基于面板实际消费的 C ABI 投影 + 项目面板 selftest（`panel.diagnostics-match`）。
- 重打包影响：见 `09-repackaging.md`（**需要重打，但按指示等确认**）。
