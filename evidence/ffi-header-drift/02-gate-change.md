collected_at: 2026-11-01T19:49:29+08:00
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

# T123 — 防漂移检查（`build/gates/typecheck.sh`）

新增 `check_ffi_header_drift()`，在两处 PASS 之前调用（host clippy 分支 + pinned image 分支）：

1. `git cat-file -e HEAD:crates/vd-ffi/include/vd_ffi.h` —— 头文件**必须被跟踪**，否则 FAIL。
2. `git show HEAD:<header> > $tmp/head.h` —— 取**已提交**版本。
3. `diff -u $tmp/head.h <工作区头文件>` —— 不一致即 **FAIL**（`exit 1`，与其它 typecheck 失败同级；**不是 warn**）。

为什么有效：`crates/vd-ffi/build.rs` 在每次 cargo 构建（含门禁里的 clippy）时用 cbindgen **重新生成**
工作区里的 `vd_ffi.h`。所以到达这一步时，工作区文件就是“由当前源码生成的真值”。
它与 HEAD 里的那份不一致 ⇒ **提交的源码与提交的头文件不同步**（T123 的缺陷形态：T99 新增的 4 个 C ABI 函数
从未入库，干净 clone 无法编译）。

反向控制（在干净 clone `target/verify-abifix` 的临时提交上做，不触碰共享仓库）：
在**提交后的头文件**里追加一行注释（模拟“改了生成物但没重新生成/没同步源码”），再跑门禁 ——
必须 FAIL；见 `05-reverse-control.log`。之后 `git checkout` 恢复。
