# Independent verification of the final Qt6 build

The realtime Slang scenarios passed on production and regression commit `bd22d7f8892f8e042f1b638ebe138e421f621404`, branch `fix/realtime-slang-context`. The full CTest run with every HDL tool exposed reported 95 passes, two explicit skips and two failures. Both failures also reproduced on the project baseline with Slang and passed on the final revision with the Verible fallback. Runtime commands used the isolated `realtime-slang-context` worktree mounted at `/src` and the [pinned containers and HDL tools](../toolchain/README.md).

| Verified artifact | SHA-256 |
| --- | --- |
| `target/qa/qt/src/app/vedit-app` | `e50752632d8b6332a5910a91e724f7dc8ddce8a3463f6bc9a45ef5f134f4b748` |
| `vedit-realtime-context-e2e` | `28d4df3fc3e45d56a1198753935e4c065bb198ea0571774e8be5e716c3cc563f` |
| `vedit-realtime-depth-switch-e2e` | `27583ffe28b7875de2751efff402d855386d91120da77f6230bc2fd95ac5d57c` |
| `target/qa/rust/release/libvd_ffi.a` | `9d56010528b1bcb2c5979e7bbb142e785fafc6c734a4d78b1215bd32330a5579` |

## Product observations

- The real Qt regression passed both offscreen and under X11. It resolved the guarded child through its filelist owner and cleared the stale port error after an unsaved correction. The saved and reopened file resolved its direct children. A macro-generated instance remained in scope after the edit removed a separate literal instance. The true missing-module diagnostic stayed visible. [Runtime rows](final-runtime.log) · [X11 run](final-x11.log)
- The real application accepted filelist import and displayed the module tree before the sidebar edits. Under X11, adding width `10` cleared the out-of-range diagnostic, and replacing it with `4` showed `logic[3:0]`. Deleting the override restored the filelist width `2` or the native Slang width `5`, according to the fixture. The `genuinely_missing` module remained visible after each edit. The Qt regression also verified replacement with a bare macro name and that native config and nested command files stayed byte-identical. [X11 observations](final-x11.log) · [Baseline reproduction](../post-import/README.md)
- In a separate X11 run, clicking Project Lint before adding the macro produced a one-error manual result. After the sidebar edit, the rendered Outline cleared its old undefined-branch row and retained the true missing module. The prior manual result remained separately visible. The captured observation preceded the GUI launcher’s teardown timeout. [X11 observations](final-x11.log)
- One-level depth used two requested files of three in the open-session union. Ten file switches measured 13 ms p95, with starts 1→1 and stops 0→0. The module closure, filelist authority including Verible and the nested command-file case, editor-open benchmark, layout, and project selftests passed. The direct pinned Slang build-context run passed 11 checks and the application FFI ABI smoke passed 20. [Runtime rows](final-runtime.log)

## Gate results

The pinned Rust release archive and all Qt targets built successfully. The Rust build used Rust 1.99.0. The Qt build used the documented Release Ninja configuration and linked that archive. The source files and test inputs matched the tested commit after the runs.

```sh
cargo build --locked --release -p vd-ffi
cmake -S /src -B /src/target/qa/qt -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DVEDIT_RUST_FFI_LIB=/src/target/qa/rust/release/libvd_ffi.a \
  -DVEDIT_RUST_FFI_INCLUDE=/src/crates/vd-ffi/include
cmake --build target/qa/qt --parallel 8
ctest --test-dir target/qa/qt --parallel 2 --output-on-failure
QT_QPA_PLATFORM=offscreen target/qa/qt/src/app/vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
cargo test --locked -p vd-config -p vd-deeplint -p vd-lsp -p vd-ffi
cargo fmt --all -- --check
cargo clippy --locked -p vd-config -p vd-deeplint -p vd-lsp -p vd-ffi --all-targets -- -D warnings
```

The full CTest's two failures were `vedit-diagnostics-product-seam` (the expected text `syntax error` vs. actual Slang diagnostics `expected ';'` and `expected a declaration name`) and `vedit-rename-e2e` (the Slang server returned JSON-RPC error `-32603` for the cross-file rename). Both failures were reproduced with the same pinned Slang server and the matching Verible and Slang tool paths on a separately built archive of baseline `b614a4af1d57c6e4ce292a25e539722320189a4b`. The final revision passed both in a targeted repeat with the Verible fallback. The explicit skips were the SVN workbench and workspaces checks because the Qt container lacks their SVN tools. The registered build-context test skips internally on the development checkout's missing AppDir path. The direct invocation used the pinned real server and passed all 11 checks. The 99-entry CTest run exited 8. [Runtime rows](final-runtime.log)

The Rust tests exited 0 with 569 passed and four opt-in generated stress fixtures ignored. Formatting and focused Clippy exited 0. The Rust builder's CentOS 7 glibc is 2.17, so for the Rust LSP tests only, a launcher used the pinned Qt image loader and libraries to invoke the exact staged slang-server binary. This kept the real server integration checks active under the pinned compiler. [Runtime rows](final-runtime.log)

The MPD evidence probe resolved the top-level checkout. These independent results are tied to the isolated worktree and binary hashes instead. The MPD verdict remains unrecorded. The checked artifact here is the Qt development build with release-pinned HDL tools.

Raw local gate logs and binary hashes are in `target/qa/t13/`. The earlier integration snapshot is recorded in [T4 CTest excerpts](ctest-t4.log) and [T4 X11 observations](../post-import/x11-green-t4.log).
