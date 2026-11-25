# HDL filelist authority repair

The integrated code revision is `8643809224affc9fc2c32be1fd73315b57075ab4`. The repair preserves the pinned slang-server's workspace, user and local compiler settings while the imported project supplies the symbol index. Slang reports actual parsed include ownership during the same catalog invocation. Definition locations use the included source, while realtime compilation keeps the owning translation unit in the bounded input set.

- `red.log` records the two review failures against `ba73342`. With configured `../shared` outside the filelist, lint reported a missing include and Ctrl could not find its signal. With a module in `child.svh`, the module tree omitted the definition. The CTest command exited 8.
- `command-file-red.log` records the remaining cwd case before the final repair. Workspace flags use `-f cfg/compiler.f`. With an unrebased nested command file, lint reported missing `flagfile.svh` and `scoped.svh` and an undeclared `flag_sig`. CTest exited 8.
- `command-file-green.log` records CTest exit 0 with the final configured fixture. The real Ctrl gesture opened the imported child and `vendor/include/flagfile.svh`. The nested `-f` retains project-relative `-I`, the nested `-F` uses its own containing directory, and nested command-file macros remain active. Workspace flags override an intentionally missing user command file without losing the user `incdirs`. The open, same-name decoy is listed as a source operand in the flag file and is excluded from the effective inputs. Realtime lint still reports the intentional bad-port error.
- `qt-x11-abi.log` records the Qt suite, X11 runs and product ABI smoke. A module included from `top.sv` resolves to the header with only `top.sv` compiled. An included module in a different compilation unit resolves to the same header with `top.sv` plus `wrapper.sv` compiled. Both keep the decoy open and out of the compile set. The original nested filelist refresh, Verible override and loose-file navigation tests still run.
- `session-reuse.log` records the existing repeated-tab-switch regression. The controller started and stopped no additional language-server sessions on switches within the existing realtime union. Slang configuration reads, index serialization and writes now run on the Rust session worker. A new Slang session still builds its own index when the session scope requires reopening. Verible's filelist write stays synchronous when starting a new session. This run did not benchmark large-workspace startup.
- `ready-transition.log` records an extra X11 stress observation. A Ctrl+click immediately after reopening `top.sv` from an excluded header was dispatched while the watcher was changing sessions. The existing status showed `no navigation request in flight` and the editor stayed on `top.sv`. The regression now waits for analysis to adopt each active document and for the corresponding session to become ready before each navigation request. The application still requires the active document's session to be ready before navigation can complete.

The scoped Slang configuration recognizes explicit command-file paths, nested `-f` and `-F`, compiler path options, and the pinned driver's quoted and separated macro forms exercised by the fixture. The filelist parser's existing supported grammar is unchanged. The workspace and user configuration files remain untouched. Paths shown are disposable fixture paths.

## Commands

Run in the `vedit-build:local` and `vedit-qt6-build:local` images as appropriate with the checkout mounted at `/src`, `CARGO_TARGET_DIR=/src/target/filelist-qt/rust`, the local tools supplied at `/hdl-tools/bin`, and `VEDIT_NO_ENV_AUTOCAPTURE=1`. The Rust tests also set `VEDIT_VERIBLE_BIN_DIR=/hdl-tools/bin`. CMake uses the resulting `libvd_ffi.a` and generated `vd_ffi.h`.

```sh
cargo build --locked --release -p vd-ffi
cargo test --locked -p vd-lsp -p vd-ffi -p vd-deeplint
cargo clippy --locked -p vd-lsp -p vd-ffi -p vd-deeplint --all-targets -- -D warnings
ctest --test-dir /src/target/filelist-qt -V -R 'vedit-(filelist-authority(-[a-z]+)?|module-closure|realtime-depth-switch|split-pane-mouse)-e2e|vedit-workspace-diagnostics-selftest|vedit-module-tree-(selftest|lane-b-test|catalog-test)'
QT_QPA_PLATFORM=xcb xvfb-run -a target/filelist-qt/src/app/workspace-lane-diagnostics/vedit-filelist-authority-e2e --configured
QT_QPA_PLATFORM=offscreen target/filelist-qt/src/app/vedit-app --selfcheck-ffi crates/vd-ffi/tests/fixtures/project
```

The X11 harness also runs with no arguments and with `--included`, `--include-wrapper`, and `--verible`. `artifacts.sha256` identifies the isolated binaries used for this scope.
