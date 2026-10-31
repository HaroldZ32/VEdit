# src/workspace/diagnostics — Verible LSP + SLANG diagnostics lane (T8)

One **effective file set** (the project model's `.f` expansion) feeds two
backends; the lane reports an observable reparse state while it does so.

- **LSP**: `crates/vd-ffi/src/lsp.rs` session exports (`vd_lsp_session_*`) over
  the Rust client `crates/vd-lsp` (session facade added in
  `crates/vd-lsp/src/session.rs`): initialize, `didOpen` for every file in the
  set, diagnostics wait, definition / references / documentSymbol / rename /
  formatting.
- **SLANG**: `crates/vd-ffi/src/deeplint.rs` (`vd_deeplint_lint`/`poll`), one
  run per file of the same set.
- **Controller**: `diagnostics_controller.*` — `setFileSet()` → `Reparsing`,
  `refresh()` → `Ready` / `Degraded` / `Failed`, with the A3c report
  (`fileSetReport()`) and the A3d snapshot (`session()`, `generation()`).
- **Outline**: `diagnostics_outline_model.*` + `diagnostics_panel.*`; the shell's
  `outline` panel factory is replaced by `diagnostics_integration.cpp`.

## Build / test (headless)

```sh
# fakes only (no Rust FFI, no server needed)
cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja
cmake --build target/diagnostics-build
ctest --test-dir target/diagnostics-build --output-on-failure

# with the Rust C ABI and a real verible-verilog-ls
cmake -S src/workspace/diagnostics -B target/diagnostics-build -G Ninja \
      -DVEDIT_RUST_FFI_LIB=target/release/libvd_ffi.a \
      -DVEDIT_RUST_FFI_INCLUDE=crates/vd-ffi/include
cmake --build target/diagnostics-build
ctest --test-dir target/diagnostics-build --output-on-failure
```

The ctest suite contains:

| test | proves |
|---|---|
| `vedit-workspace-diagnostics-selftest` | A3c same-set fan-out (fake backends), A3d Reparsing→Ready, the six entry points, degraded/failed paths — no display, no server |
| `vedit-diagnostics-ffi-selftest` | the same properties through the real C ABI with a real `verible-verilog-ls`; **prints `SKIP:` and exits 0** when the binary or fixture is absent |

`SKIP` is explicit and is reported as such in the evidence: it never counts as a
passing interaction.

## What this lane does NOT prove (no display, honest limits)

- The underlines, the dock rendering and the editor jump itself are user-visible
  behaviour; this lane proves the **entry points and their typed results** in
  headless runs only.
- `slang` is not installed on the build host used for the first evidence run, so
  the SLANG leg is proven as a *typed degradation* (`slangFailures` in the A3c
  report, message in `lastError()`), not as successful diagnostics. A host with
  slang runs the same test with a healthy leg.
- Wiring this lane into `vedit-app` needs the lane list in
  `src/app/CMakeLists.txt` and a call in `src/workspace/integration.cpp`; both
  are outside this lane's write scope and are reported to the captain.

## Integration contract

`installDiagnostics(ApplicationShell &, const IntegrationOptions &)`:

1. creates the controller with the real FFI backends when linked;
2. registers the "outline" panel factory (document symbols);
3. registers `project.reparse` (re-run Verible + SLANG);
4. starts the project-model job and feeds `setFileSet()` when it finishes
   (timer-driven, so startup is not blocked);
5. exposes the controller as the shell property `vedit.diagnosticsController`
   (`QObject *` holder) so the editor lane can call definition/references/rename/
   format with the caret position.

## Toolchain discovery and missing-tool visibility (T82)

The external tools are resolved with the delivery order **bundled > PATH > env**:

- `bundled`: next to the product binary (`bin/` beside `vd-app`, plus the legacy
  `lib/vedit/bin/` layout). The delivered AppDir installs
  `bin/verible-verilog-{ls,lint,format}` and `bin/slang` (sha256 pinned).
- `PATH`: the user's shell PATH.
- `env`: `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` (explicit override, last).

The chosen source (`bundled` / `PATH` / `env:<name>`) and a `missing: [...]` list are
printed by `vedit-app --selfcheck` (`TOOLCHAIN ...`), appended to the shell diagnostic
list (status bar) and shown in the outline/diagnostics panel when a tool is absent —
including what is missing, what it disables and how to fix it.

```sh
# bundled / missing proofs on an assembled payload
vedit-toolchain-selftest --appdir <AppDir> --expect bundled \
                         --lint-file crates/vd-lsp/fixtures/project/bad_line_length.sv
vedit-toolchain-selftest --appdir <AppDir-with-tools-moved> --expect missing
```

The `missing` case is also a ctest entry (`vedit-toolchain-missing-selftest`) so a clean
build always asserts the explicit-failure path.
