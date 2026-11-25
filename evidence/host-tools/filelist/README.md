# HDL filelist authority regression

Criterion 4 of `host-svn-shell-20261125.md`. The Qt harness uses the real editor, project import and catalog, diagnostics controller, and application jump target. It sends a Ctrl+left mouse press and release to the KTextEditor view and reads the canonical path of the resulting active editor tab. The fixture has a top in `rtl`, a child imported through a nested `-F` list, and a same-name decoy beside the top. The decoy is also opened in an editor tab. The child accepts `correct` and the decoy accepts `decoy`. An unsaved edit introduces an intentional bad port and proves that fresh semantic lint ran.

The shipped slang-server 0.3.1+ provides both `textDocument/definition` and the realtime `publishDiagnostics` rows. The separate slang 12.1.0+0 CLI produces the module catalog. The Verible run uses v0.0-4298-g085eef05. Before the fix, the compile inputs already excluded the decoy. Slang-server's independent workspace index nevertheless made its shallow analysis and definition lookup choose the nearest declaration, `rtl/child.sv`. `red.log` captures the navigation and port errors against baseline `f90e30d` before the implementation changed.

The fix gives project LSP sessions an index derived from the expanded filelist and the catalog's shadowed file exclusions. Slang-server uses a private workspace configuration with exact canonicalizing index patterns. Verible receives an explicit generated `--file_list_path`. Realtime compilation retains its open-document and one-level closure scope. Loose documents retain normal server discovery. The project configuration uses the imported include directories and defines. The private Slang index excludes independent `.slang/server.json` workspace and user indexing configuration in project mode. Existing filelist syntax is retained, including the nested `-F` and flattened source and include entries exercised here.

## Reproduction and gates

The builds used `vedit-build:local` with the pinned Rust 1.99.0 toolchain and `vedit-qt6-build:local` with Qt 6.10.2 and KF6 6.24.0. The worktree is `/src` in both containers. The local runtime's tools are mounted read-only as `/hdl-tools` and the built `slang` directory as `/src/build/slang/out`. The test environment supplies `VEDIT_SLANG=/hdl-tools/bin/slang`, `VEDIT_SLANG_SERVER=/hdl-tools/bin/slang-server`, `VEDIT_VERIBLE_LS=/hdl-tools/bin/verible-verilog-ls`, `VEDIT_VERIBLE_BIN_DIR=/hdl-tools/bin`, and `VEDIT_NO_ENV_AUTOCAPTURE=1`.

```sh
cargo build --locked --release -p vd-ffi
cmake -S /src -B /src/target/filelist-qt -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DVEDIT_RUST_FFI_LIB=/src/target/filelist-qt/rust/release/libvd_ffi.a \
  -DVEDIT_RUST_FFI_INCLUDE=/src/crates/vd-ffi/include
ctest --test-dir /src/target/filelist-qt -V -R '^vedit-filelist-authority-e2e$'
ctest --test-dir /src/target/filelist-qt -V -R 'vedit-(filelist-authority|module-closure|realtime-depth-switch|split-pane-mouse)-e2e|vedit-workspace-diagnostics-selftest|vedit-module-tree-(selftest|lane-b-test|catalog-test)'
ctest --test-dir /src/target/filelist-qt -V -R 'vedit-filelist-authority(-verible)?-e2e'
QT_QPA_PLATFORM=xcb xvfb-run -a target/filelist-qt/src/app/workspace-lane-diagnostics/vedit-filelist-authority-e2e
QT_QPA_PLATFORM=offscreen target/filelist-qt/src/app/vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
cargo test --locked -p vd-lsp -p vd-ffi
cargo clippy --locked -p vd-lsp -p vd-ffi --all-targets -- -D warnings
```

`CARGO_TARGET_DIR=/src/target/filelist-qt/rust` isolates the Rust archive. Before the fix the new ctest returned 8 with `LINT pass=0` and `CTRL pass=0`. The final combined Qt run passed all nine tests (`ctest-final.log`). The real Verible run also passed when an on-disk `verible.filelist` named the decoy. X11 under Xvfb returned 0, and the application ABI selfcheck reported `checks=20 failures=0`. Rust tests and Clippy exited 0. The runtime logs show the canonical imported child, navigation into the `+incdir+` header, the new child after refreshing the nested list, and ordinary definition navigation after switching to a workspace with no filelist.
