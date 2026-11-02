collected_at: 2026-11-01T19:55:20+08:00
git_head: 60c7ae9c95fe769e2bd6c86dd4c56e1514603e44
git_branch: feat/qt6-ktexteditor-migration
git_status:
   M AGENT.md
   M THIRD_PARTY_NOTICES.md
   M build/evidence/typecheck/00-run.txt
   M build/evidence/typecheck/10-typecheck-image-build.log
   M build/evidence/typecheck/11-typecheck-image-id.txt
   M build/gates/typecheck.sh
   M crates/vd-config/src/project.rs
   M crates/vd-config/src/warnings.rs
   M crates/vd-ffi/include/vd_ffi.h
   M crates/vd-ffi/src/lsp.rs
   M crates/vd-ffi/src/project.rs
   M crates/vd-lsp/src/client.rs
   M crates/vd-lsp/src/documents.rs
   M crates/vd-lsp/src/session.rs
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
   M packaging/runtime/README.md
   M packaging/runtime/assemble-appdir.sh
   M packaging/runtime/selfcheck.sh
   M scripts/versions.env
   M src/app/CMakeLists.txt
   M src/app/app_shell.cpp
   M src/app/editor_area.cpp
   M src/app/main.cpp
   M src/syntax/product/syntax_product.cpp
   M src/workspace/diagnostics/CMakeLists.txt
   M src/workspace/diagnostics/diagnostics_controller.cpp
   M src/workspace/diagnostics/diagnostics_controller.h
   M src/workspace/diagnostics/diagnostics_ffi_selftest.cpp
   M src/workspace/diagnostics/diagnostics_integration.cpp
   M src/workspace/diagnostics/diagnostics_panel.cpp
   M src/workspace/diagnostics/diagnostics_panel.h
   M src/workspace/diagnostics/diagnostics_selftest.cpp
   M src/workspace/diagnostics/ffi_backends.cpp
   M src/workspace/diagnostics/lsp_backend.h
   M src/workspace/filetree/filetree_integration.cpp
   M src/workspace/palette/README.md
   M src/workspace/palette/palette_integration.cpp
   M src/workspace/search/CMakeLists.txt
   M src/workspace/search/README.md
   M src/workspace/search/global_search.cpp
   M src/workspace/search/global_search.h
   M src/workspace/search/search_integration.cpp
   M src/workspace/search/search_selftest.cpp
   M src/workspace/settings/settings_integration.cpp
   M src/workspace/vcs/vcs_integration.cpp
  ?? crates/vd-lsp/tests/buffer_change.rs
  ?? evidence/crash-segv/
  ?? evidence/ffi-header-drift/
  ?? evidence/fold-toggle/
  ?? evidence/fold-unfold-guard/
  ?? evidence/fonts-cjk/
  ?? evidence/large-file/
  ?? evidence/lint-trigger/
  ?? evidence/locale-and-noise/
  ?? evidence/rules-search-ux/
  ?? evidence/search-segv/
  ?? packaging/runtime/assets/fonts/
  ?? packaging/runtime/assets/licenses/
  ?? src/app/crash_report_check.sh
  ?? src/app/crash_reporter.cpp
  ?? src/app/crash_reporter.h
  ?? src/app/crash_reporter_selftest.cpp
  ?? src/workspace/diagnostics/selftests/

# T123 报告 — HEAD 不可构建：`vd_ffi.h` 生成物未入库（T99 的 4 个 C ABI 函数从未提交）

- Cell: `cell-20261031T085543161Z` · Board task: **T123**（P0 交付阻断）· 日期: 2026-11-01
- 结论一句话：**新 HEAD（HEAD + 重新生成的 `vd_ffi.h` + 防漂移门禁）可以从干净 clone 编过** ——
  已在 `git clone --no-hardlinks` 的干净副本里验证：`BUILD-QT-EXIT=0`、`typecheck EXIT=0`、ctest **27/27**。
  共享仓库的 HEAD 仍是旧头文件（我**没有 commit**），需要 Lead 把重新生成的头文件 + 门禁一起提交（见 §5）。

## 1. 缺陷确认

- HEAD `60c7ae9` 的 `crates/vd-ffi/include/vd_ffi.h` **缺少** T99 新增的：
  `vd_vcs_info_job` / `vd_vcs_info_poll` / `vd_vcs_diff_job` / `vd_vcs_diff_poll`（含注释共 11 行新增，`01-header-drift-at-head.diff`）。
- HEAD 的 `crates/vd-ffi/src/vcs.rs` **已包含**这四个函数（18 处匹配）→ Rust 侧入了库，**只有生成的头文件没入库**。
- 后果：`vcs_source.cpp` 调用 `vd_vcs_info_job`，干净 clone 无法配置/编译（T122 在干净 worktree 复现）。
- 用 HEAD 的源码重新生成的头文件：`04-regenerated-header.h`（sha256 `82f8f5c5…`），与工作区当前版本（由脏源码生成）不同，
  **交给 Lead 提交的是「与将要提交的源码一致」的那份；门禁会强制这一点**。

## 2. 防漂移检查（`build/gates/typecheck.sh`，不降级）

`check_ffi_header_drift()`（host 与 pinned 两条 PASS 路径之前都调用）：

1. `git cat-file -e HEAD:<header>` —— 必须被跟踪，否则 FAIL；
2. `git show HEAD:<header>` 取已提交版本；
3. `diff` 与工作区（构建时被 build.rs/cbindgen 重新生成）→ 任何差异 **exit 1**，与其它 typecheck 失败同等级。

**关键细节**：门禁在 Rust 阶段前 `touch crates/vd-ffi/build.rs`，强制 cargo 重跑 build script，
让 cbindgen **一定**重写头文件 —— 否则 cargo 缓存会让“手改已提交头文件”逃过比较（我第一版就是这样漏掉的，反向控制抓出来了）。

## 3. 干净 clone 验证（硬要求，已做）

```
git clone --no-hardlinks <repo> target/verify-abifix      # HEAD 60c7ae9
# 放入重新生成的头文件 + 新门禁，在「clone 本地」提交（不触碰共享仓库）：
#   clone-local commits: fcfd1fe (fix) / bf9949f (gate forces regeneration)
VEDIT_SKIP_IMAGE_BUILD=1 bash scripts/build-qt.sh --no-test   → BUILD-QT-EXIT=0   (06)
bash build/gates/typecheck.sh                                  → FINAL-TYPECHECK-EXIT=0 (07)
ctest (inside pinned image)                                    → 100% 27/27 passed (09)
```

## 4. 反向控制（能真红）

在 clone 里把**已提交**的头文件追加一行注释（模拟手改生成物）并提交 → 门禁：
`[typecheck] FAIL: crates/vd-ffi/include/vd_ffi.h drifted from its source …`，**exit 1**（`08-reverse-control.log`）。
随后 `git reset --hard` 回到干净提交，门禁 PASS（`07`）。

## 5. 需要 Lead 做的（一步）

在共享仓库提交时：
1. **包含** `crates/vd-ffi/include/vd_ffi.h` 的当前工作区版本（构建已用当前源码重新生成；若不放心，
   在提交前跑一次 `cargo build -p vd-ffi --lib` 再 `git add` 它）。
2. **包含** `build/gates/typecheck.sh`（新的 `check_ffi_header_drift`）。
3. 提交后跑 `bash build/gates/typecheck.sh`：应当是 PASS（我在 clone 的等价提交上验证过）；
   若报 `drifted`，说明提交的源码与头文件仍不同步 —— 按提示重新生成并纳入同一次提交。
- 注意：**在 Lead 提交之前，共享工作区的 `typecheck` 会因为 HEAD 的头文件仍旧而 RED** —— 这是新门禁的正确行为
  （它正是要拦住 T123 这种“代码入库、生成物没入库”的提交）。

## 6. 同类审计（生成物 + 入库 + 被构建消费）

见 `03-audit-generated-artifacts.md`：**只有 `vd_ffi.h` 一个**。
其它候选均不是“生成物被消费”：`scripts/versions-qt.env` 是手写 pin（`cmake/VEditVersions.cmake` 直接 FATAL 校验）；
`resources/syntax/*.xml`、`packaging/**` 清单是手写输入；`target/`、`build/artifacts/`、`**/CMakeFiles/` 等输出已在 `.gitignore`；
`evidence/**` 不被构建图引用。

## 7. 证据（全部 C9 头：collected_at + HEAD + branch + porcelain）

`01-header-drift-at-head.diff`、`02-gate-change.md`、`03-audit-generated-artifacts.md`、`04-regenerated-header.h`、
`05-clone-commits.txt`、`06-clone-build.log`（BUILD-QT-EXIT=0）、`07-clone-typecheck.log`（EXIT=0）、
`08-reverse-control.log`（EXIT=1 + 失败文案）、`09-clone-ctest.log`（27/27）、本报告。
未 commit/push、未打包。
