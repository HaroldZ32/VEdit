# T8 — Verible LSP (Rust service) + diagnostics lane: delivery report

- Cell: `cell-20261031T085543161Z` · board task **T8** · loop `loop-20261031T095153-27eeb3`
- Contract: `.mpd/cellwork/cell-20261031T085543161Z/contracts/verible-lsp.md`
- Evidence directory: `evidence/lsp-v2/` (every file carries a C9 header: collection
  time, `git rev-parse HEAD`, and the exact command; the raw outputs are unedited).
- Headless only: no display server. GUI-visible effects are **not** claimed here.

## 1. What was delivered

### Rust

| Path | What |
|---|---|
| `crates/vd-lsp/src/session.rs` | `SessionHandle` worker-thread facade: opens the effective file set on one `LspClient`, waits for the first diagnostics under one budget, serves polled requests (definition / references / documentSymbol / rename / formatting / diagnostics), exposes phase + counts + generation + skipped files. |
| `crates/vd-lsp/src/lib.rs` | exports the session API and `SessionLocation` / `SessionRange` aliases (so `vd-ffi` does not depend on `lsp-types`). |
| `crates/vd-lsp/tests/session_file_set.rs` | real `verible-verilog-ls` over the fixture's `expected/files.txt` (7 files): ready state, whole-set open, all six request kinds, file-set replacement, clean stop (no zombie). Explicit `SKIP:` when the binary is absent. |
| `crates/vd-lsp/tests/session_degradation.rs` | missing server binary → `Failed` with a message naming the path/OS error; requests answer with typed failures; clean stop. |
| `crates/vd-ffi/src/lsp.rs` | C ABI session service: `vd_lsp_session_start/status/stop/free`, `vd_lsp_session_request`, `vd_lsp_request_poll`, `vd_lsp_status_free`, `vd_lsp_response_free`, PODs for status/locations/symbols/edits/diagnostics. The Slice-0 `vd_lsp_start/poll/info_free` handshake is untouched. Caller passes the **same** `VdStringList` the project model produced (A3c). |

### Qt (headless-testable lane)

| Path | What |
|---|---|
| `src/workspace/diagnostics/diagnostics_controller.*` | `setFileSet()` → `Reparsing`; `refresh()` → `Ready` / `Degraded` / `Failed`; A3c report (`fileSetReport()`) and A3d snapshot (`session()`, `generation()`); five navigation entry points polled without blocking. |
| `src/workspace/diagnostics/ffi_backends.cpp` | real backends over the C ABI (LSP session + SLANG per-file lint); fakes stay injectable. |
| `src/workspace/diagnostics/diagnostics_outline_model.*`, `diagnostics_panel.*` | outline table model + dock widget; feeds the shell's `outline` panel id. |
| `src/workspace/diagnostics/diagnostics_integration.*` | `installDiagnostics()`: outline factory, `project.reparse` command, timer-driven project-model job that feeds the effective file set to `setFileSet()`, controller exposed as `vedit.diagnosticsController`. |
| `src/workspace/diagnostics/CMakeLists.txt`, `README.md` | standalone + root-build wiring; build/test commands and the honest limits. |
| `diagnostics_selftest.cpp` | fakes: A3c same-set fan-out, A3d transitions, six entries, degraded/failed paths — 24/24 CHECK PASS. |
| `diagnostics_ffi_selftest.cpp` | real Verible through the real C ABI — 22/22 CHECK PASS. |

## 2. Commands and observed results

| # | Command (where) | Observed |
|---|---|---|
| 1 | `cargo test -p vd-lsp --lib --test session_degradation --test session_file_set -- --nocapture` (vedit-build:local, cargo 1.99.0) | `test result: ok` ×3 (21 + 1 + 2). `OBSERVED set=full expected=7 opened=7 diagnostics=3`, `definition locations=1`, `reference locations=1`, `rename edits=2`, `formatting edits=1`; `set=reduced expected=3 opened=3`; `EXIT=0` |
| 2 | `cargo test -p vd-ffi` (same image) | T4 ABI surface still green: 3 + 12 tests, `EXIT=0` |
| 3 | `cargo fmt --all -- --check` | `FMT-ALL-EXIT=0` (also per-file `rustfmt --check` on the five T8 files: `RUSTFMT-FILES-EXIT=0`) |
| 4 | `cargo clippy --workspace --all-targets -- -D warnings` | `CLIPPY-EXIT=0` |
| 5 | `bash build/gates/typecheck.sh` | **`TYPECHECK-EXIT=1` — RED, not from this lane**: link failure of `src/app/editor-lane/vedit-edit-core-selftest` (`undefined reference to vedit::app::EditorProvider::{openFile,saveFile,currentFilePath}` in `src/editor/kernel/ktexteditor_provider.cpp`), i.e. T10's in-flight editor lane. This lane's Rust halves (fmt/clippy) are green; the gate's Qt half does not compile this lane because product wiring is not applied (see §3). |
| 6 | `cargo tree -p vd-ffi` | `ISOLATION=PASS`: no `gtk` / `sourceview4` / `vd-editor` / `vd-syntax`; `vd-config`, `vd-lsp`, `vd-deeplint`, `vd-fim` present (AC-0.3 / AC-S0.5) |
| 7 | `cmake -S src/workspace/diagnostics ... -DVEDIT_RUST_FFI_LIB=… && cmake --build` (vedit-qt6-build:local) | `CONFIGURE-EXIT=0`, `BUILD-EXIT=0` |
| 8 | `ctest --test-dir target/diagnostics-build --output-on-failure` | 2/2 passed, `CTEST-EXIT=0` |
| 9 | `vedit-workspace-diagnostics-selftest --selfcheck` (fakes, no FFI, no display) | `checks=24 failures=0`, `EXIT=0` |
| 10 | `vedit-diagnostics-ffi-selftest <fixture> <verible-ls>` (real server via C ABI) | `checks=22 failures=0`, `EXIT=0`; `a3d.terminal-state-within-60s degraded (lsp 7/7 file(s), diagnostics 3)`; `a3c.lsp-opened-whole-set opened=7/7`; `entry.symbols-contains-top 5 outline entries`; `definition_locations=1`; missing-verible path → `failed` with `cannot open … spawning verible-verilog-ls at /nonexistent/…` |
| 11 | shell-only sources compiled with `-DVEDIT_DIAGNOSTICS_HAVE_APP_SHELL=1 -Werror` | `COMPILE-EXIT=0` for `diagnostics_integration.cpp` and `diagnostics_panel.cpp` |

Full logs: `00-context.txt`, `01-rust-session-tests.log`, `02-rust-fmt.log`,
`03-clippy.log`, `04-typecheck.log`, `05-qt-lane-build.log`, `06-qt-lane-selftest.log`,
`07-qt-ffi-selftest.log`, `08-gtk-isolation.log`, `08-cargo-tree-vd-ffi.txt`,
`10-scope-check.txt`.

## 3. A3c / A3d — what is proven, how

**A3c (one effective set feeds SLANG and Verible).** The effective set is the
project model's `.f` expansion (`tests/fixtures/verilog-project/expected/files.txt`,
verified by `scripts/verify-fixture.sh`). `DiagnosticsController::setFileSet()` hands
that exact `QStringList` to the LSP backend and starts one SLANG lint per file;
`fileSetReport()` records the counts. Evidence:

- fake backends: `a3c.lsp-received-same-set`, `a3c.slang-received-same-set`,
  `a3c.report-marks-same-set` (24/24 selftest);
- real Verible: `a3c.lsp-opened-whole-set opened=7/7`, `a3c.report-carries-the-set`,
  `a3c.slang-got-every-file requested=7 completed=7 failures=7` (the SLANG leg is a
  **typed degradation** on this host — see §4).

**A3d (reparse observable after a file-set change).** `setFileSet()` advances
`generation()`, sets `Reparsing`, and `refresh()` moves to `Ready`/`Degraded`/`Failed`
with `filesExpected/filesOpened/diagnostics/generation`. Evidence:
`a3d.reparsing-observable`, `a3d.terminal-state-within-60s`,
`a3d.generation-advanced`, `a3d.new-counts-follow-set`, `a3d.state-ready-after-change`,
plus the real C-ABI run above.

## 4. Not verified / explicit SKIPs (no pretending)

1. **SLANG has two proven paths, selected by `VEDIT_SLANG`.**
   * **Default environment (no `VEDIT_SLANG`):** the leg degrades diagnostically — the
     session reports `slangFailures=7` with the typed message
     `slang: slang binary not found (tried 4 location(s); set VEDIT_SLANG)` while the LSP
     side stays usable (`lsp 7/7`); nothing crashes and nothing is faked.
   * **With `VEDIT_SLANG=build/slang/out/slang`:** the success path is proven in
     `16-f1-slang-success-path.log` — `a3c.slang-got-every-file requested=7 completed=7
     failures=0`, `a3d.terminal-state-within-60s ready (lsp 7/7 file(s), diagnostics 21)`,
     `VER-DIAGNOSTICS-FFI-SELFTEST checks=23 failures=0`.
2. **`verible-verilog-syntax`** is absent, but this lane does not use it: only
   `verible-verilog-ls` is required and it is present
   (`v0.0-4298-g085eef05`, runs in both images). No SKIP needed for the LS path.
3. **GUI-visible behaviour** (diagnostic underlines, jump UI, outline dock) cannot be
   seen without a display and is **not** claimed. The lane proves the entry points,
   their typed results, and the outline model contents headlessly.
4. **Product wiring is DONE (T59).** `src/app/CMakeLists.txt` lists `diagnostics` in
   `VEDIT_WORKSPACE_LANES` and `src/workspace/integration.cpp` calls
   `installDiagnostics()` under `VEDIT_WORKSPACE_HAVE_DIAGNOSTICS` (guarded include +
   call, same style as the existing project block). `vedit-app` now links the lane:
   `nm -C vedit-app | grep -c 'vedit::workspace::diagnostics'` = **174** (43 `T`
   symbols), `ctest -N` = **16** tests (was 14), full root `ctest` = **16/16 passed**.
   See `11-product-wiring.log` and `12-ctest-before-after.txt`.
5. **Generated shared header — retained by captain's decision (T59).**
   `crates/vd-ffi/include/vd_ffi.h` is a **cbindgen-generated artifact, rewritten by
   `build.rs`**, never hand-edited; any `cargo build -p vd-ffi` reproduces the same
   +398 lines (session PODs/exports). It ships with T8 so the generated header and the
   Rust ABI declarations cannot diverge.
   **「`vd_ffi.h` 为 cbindgen 生成物，本次由 `build.rs` 自动重写，非手改」**（船长 T59 裁定：保留，不 revert）。
6. **`bash build/gates/typecheck.sh` = PASS after T59** (`TYPECHECK-EXIT=0`,
   `[typecheck] PASS (pinned image vedit-build:local)`), see
   `13-typecheck-after-wiring.log`. Earlier in the T8 run the gate was RED because of
   T10's in-flight `src/app/main.cpp` / `EditorProvider` link errors — that was not
   this lane; no test or gate was changed. A full ctest run taken while lane binaries
   were being relinked by a concurrent build reported 3 transient `BAD_COMMAND`
   entries; the immediate rerun passed 16/16 (recorded in `11-product-wiring.log`).

## 5. Shared-file change applied (T59, captain-authorized)

```diff
--- a/src/app/CMakeLists.txt
-set(VEDIT_WORKSPACE_LANES filetree palette search)
+set(VEDIT_WORKSPACE_LANES filetree palette search diagnostics)
--- a/src/workspace/integration.cpp
+#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
+#include "diagnostics/diagnostics_integration.h"
+#endif
@@ inside installPanels()
+#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
+    installDiagnostics(shell, options);
+#endif
```

No target mapping is needed: the lane target is `vedit_workspace_diagnostics`
(the default `<lane>` target name) and the entry file is
`diagnostics_integration.cpp`.

Root configure now prints `workspace lane 'diagnostics' included` and
`workspace lane 'diagnostics' wired (vedit_workspace_diagnostics)`; the link graph
(`ninja -t query src/app/vedit-app`) lists `libvedit_workspace_diagnostics.a`.

## 6. Evidence index (C9 headers present in each)

| File | Content |
|---|---|
| `00-context.txt` | time, HEAD, `git status --porcelain`, toolchain/image facts |
| `01-rust-session-tests.log` | vd-lsp session tests (`--nocapture`, OBSERVED lines) + vd-ffi ABI tests |
| `02-rust-fmt.log` | per-file rustfmt check + workspace `cargo fmt --all -- --check` |
| `03-clippy.log` | `cargo clippy --workspace --all-targets -- -D warnings` |
| `04-typecheck.log` | `bash build/gates/typecheck.sh` (RED from T10; see §4.6) |
| `05-qt-lane-build.log` | Qt lane configure/build/ctest + shell-branch `-Werror` compiles |
| `06-qt-lane-selftest.log` | fake-backend selftest raw output (24/24) |
| `07-qt-ffi-selftest.log` | real Verible via C ABI (22/22) + degradation |
| `08-gtk-isolation.log`, `08-cargo-tree-vd-ffi.txt` | dependency-tree isolation |
| `10-scope-check.txt` | changed-path classification against the T8 write scopes |
| `11-product-wiring.log` | T59: root build of the wired targets, symbols, link inputs, ctest, configure messages, full ctest + typecheck |
| `12-ctest-before-after.txt` | T59: ctest 14→16 and product symbol 0→174 before/after |
| `13-typecheck-after-wiring.log` | T59: `bash build/gates/typecheck.sh` → `TYPECHECK-EXIT=0` |

## 7. T59 — product wiring evidence (before → after)

| Check | Before wiring | After wiring |
|---|---|---|
| `ctest -N` (root build) | 14 tests; no diagnostics entry | **16 tests**; `#15 vedit-workspace-diagnostics-selftest`, `#16 vedit-diagnostics-ffi-selftest` |
| `nm -C vedit-app \| grep -c 'vedit::workspace::diagnostics'` | **0** (pre-wiring binary) | **174** (43 `T` symbols, e.g. `DiagnosticsController::setFileSet`, `…::requestDefinition`, `createOutlinePanel`, `installDiagnostics` call site) |
| Root configure | lane not listed | `workspace lane 'diagnostics' included` + `wired (vedit_workspace_diagnostics)` |
| Link graph | lane absent from `vedit-app` inputs | `libvedit_workspace_diagnostics.a` in `ninja -t query src/app/vedit-app` |
| UTF-16 strings in the product | no diagnostics objectNames | `diagnosticsOutline`, `diagnosticsStatus` present (plus shell-owned `dock-`, `outline`) |
| Full root ctest | — | **16/16 passed** |
| `bash build/gates/typecheck.sh` | earlier RED from T10 (unrelated) | **`TYPECHECK-EXIT=0`** |

Unchanged honest limits: SLANG **success is proven** with
`VEDIT_SLANG=build/slang/out/slang` (`16-f1-slang-success-path.log`:
`requested=7 completed=7 failures=0`, `lsp 7/7`, `checks=23 failures=0`), while the default
environment (no `VEDIT_SLANG`) exercises the typed degradation without a crash. GUI-visible
behaviour is not claimed (no display), and `verible-verilog-syntax` is absent but unused by
this lane.
