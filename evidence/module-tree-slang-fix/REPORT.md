# Module Tree: packaged-AppDir `slang binary not found` — fix report

Task: fix `elaboration failed: slang failed: slang binary not found (tried 4
location(s); set VEDIT_SLANG)` in the Module Tree when `<AppDir>/bin/slang`
exists and runs. Scope: the three complementary fixes, their real tests, the
repo gates, and this evidence directory.

**Result: all three fixes land, the repo gates are green (ctest 37/37), and the
packaged layout is proven end to end through the real AppDir loader.** The
reproduction (RED) and the fixed run (GREEN) are both recorded below.

---

## 1. What changed (7 source files, no frozen file touched)

### Fix 1 — the Module Tree now uses the diagnostics lane's resolved toolchain

* `src/workspace/module_tree/module_tree_integration.cpp:8-30` — new
  `moduleTreeSlangBinary(shell)`:
  * `VEDIT_SLANG` first (the lane's historical explicit override; the Rust
    resolver also treats the option it is handed as highest precedence);
  * otherwise the path the diagnostics lane resolved and published on the shell
    (`vedit.diagnostics.toolchainSlang` — literally
    `ToolchainReport::slang.path`, the same report `diagnostics_integration.cpp:996`
    uses for its project pass);
  * otherwise empty, which lets the Rust service resolve it itself.
* `module_tree_integration.cpp:218` — the holder calls
  `client_->setSlangBinary(moduleTreeSlangBinary(shell_))` in `refresh()`, i.e.
  at elaboration start, not in the constructor (the holder is constructed before
  `installDiagnostics()` runs — this is exactly why the old env-only constructor
  path could not see the resolver's report).
* `module_tree_integration.h:14-27` — the selector is declared next to
  `installModuleTree()` so the headless self-test can exercise the real seam.
* `src/workspace/diagnostics/diagnostics_integration.cpp:1427` — the diagnostics
  lane publishes `toolchain.slang.path` on the shell at install time;
  `:753` republishes `state_.toolchain().slang.path` after a successful settings
  apply, so a `tools.slang` override stays in step. The shell-property seam is
  the existing one (`vedit.diagnostics.gates` / `.capabilities` / `.session` /
  `vedit.diagnosticsController`).

Why not read `state_` directly: the report lives in the anonymous-namespace
`ControllerHolder`/`BackendWiring` of the diagnostics lane, is a local of
`installDiagnostics()`, and the module-tree lane is installed **before** the
diagnostics lane (`src/workspace/integration.cpp` calls `installModuleTree` at
line 61 and `installDiagnostics` at line 64; lane build order is
`... project module_tree diagnostics ...`). A typed accessor would require
reordering lanes and a new lane-to-lane link; the property is the smallest
existing signal that carries the same value.

### Fix 2 — the Rust resolvers probe `<exe_dir>/bin/<tool>`

* `crates/vd-deeplint/src/binary.rs`
  * new candidate `exe_dir/bin/slang` (`ExeRelativeRootBin`, probe at :171) and
    `exe_dir/bin/verible-verilog-lint` (`ExeRelativeRootBinVerible`, :234), with
    their labels in `AttemptSource::as_str()` (:53, :58);
  * inserted after the existing `exe_dir/../bin/<tool>` base and before
    `exe_dir/<tool>`; **every existing base and its relative order is kept**,
    and `PATH` is still never consulted (module doc + `probe()` unchanged);
  * why: under AppRun the process is the bundled loader
    (`<AppDir>/ld-linux-x86-64.so.2`), so `executable_dir()` is the AppDir ROOT;
    the old bases looked at `<AppDir>/../bin/slang` and missed the real
    `<AppDir>/bin/slang`.
* `crates/vd-config/src/tools.rs:212-224` — `bundled_candidates()` gains
  `exe_dir/bin/<binary>` for all three Verible tools, same reasoning, all
  existing candidates kept.
* No PATH addition anywhere.

### Fix 3 — AppRun names the packaged tools

* `packaging/runtime/AppRun:27-43` — exports `VEDIT_SLANG="$APPDIR/bin/slang"`
  (mirrors `packaging/tarball/launcher/vedit.sh`), and exports
  `VEDIT_VERIBLE_LS` / `_LINT` / `_FORMAT` only when the caller did not set them
  and the packaged file exists. The comment states why it is there (AppRun is
  the launch contract and enters through the loader) **and** that the
  product-side resolvers must work without it.

### Tests added (all real behaviour, no table-only assertions)

* `binary.rs:409` — `resolve_binary(None, None, <tmp>/AppDir)` where
  `<tmp>/AppDir/bin/slang` exists and `<tmp>/bin/slang` does not → picks
  `<tmp>/AppDir/bin/slang` and records `ExeRelativeRootBin`. Negative control is
  asserted explicitly.
* `binary.rs:435` — the same for `resolve_verible_lint`.
* `tools.rs:331` — `resolve_verible` finds the AppDir-root `bin/` payload.
* Existing attempt-list tests were **updated, not weakened**: the slang
  "nothing found" test now asserts 5 attempts (was 4) and additionally requires
  the new base to be listed; the Verible failure test asserts 4 bundled
  candidates (was 3). The original meaning is kept: env unset first, all
  path-bearing attempts `NotFound`, and the description names each base.
* `module_tree_selftest.cpp:519` — `checkToolchainWiring` builds a real
  executable at `<tmp>/AppDir/bin/slang`, sets the exact shell property the
  diagnostics lane publishes, and asserts the lane selector returns it; adds
  negative control (no source → empty) and the `VEDIT_SLANG` override check.
* `module_tree_selftest.cpp:689` — `checkPackagedLayoutResolution` runs the REAL
  hierarchy service with **no explicit slang path and no `VEDIT_SLANG`**, with
  the tool at `<exe_dir>/bin/slang` and the legacy/tarball bases absent
  (creating the file when the probe root is writable, using an existing one when
  run through the real AppDir loader). It fails with the original user-visible
  error before the Rust fix (RED) and passes afterwards (GREEN).

---

## 2. Verification (commands + observed output)

All commands were run from the repository root. The Rust commands ran in the
repo's pinned image `vedit-build:local` with `RUSTUP_TOOLCHAIN=1.99.0`
(the host rustup shim for 1.99.0 is broken: `the 'cargo' binary ... is not
applicable to the '1.99.0-x86_64-unknown-linux-gnu' toolchain`; the pinned image
is the toolchain of record, AGENT.md §4).

| # | Command | Observed result | Log |
|---|---|---|---|
| 1 | `cargo test -p vd-deeplint -p vd-config` (pinned image) | **EXIT=0**; `binary::tests::appdir_root_layout_is_found_relative_to_the_executable ... ok`, `binary::tests::appdir_root_layout_finds_verible_lint ... ok`, `tools::tests::bundled_payload_is_found_in_the_appdir_root_bin ... ok`; all suites green | `10-cargo-test.log` |
| 2 | `cargo fmt --all -- --check` (pinned image) | **EXIT=0** | `11-cargo-fmt.log` |
| 3 | `cargo clippy -p vd-deeplint -p vd-config --all-targets -- -D warnings` (pinned image) | **EXIT=0**, `Finished dev profile` | `12-cargo-clippy.log` |
| 4 | `bash scripts/build-qt.sh` (frozen gate, run from repo root) | **EXIT=0**; `100% tests passed, 0 tests failed out of 37`; `29/37 Test #29: vedit-module-tree-selftest ... Passed`; Rust layer + Qt build + ctest + xcb/software smoke all PASS | `30-build-qt.log` |
| 5 | `ctest --test-dir target/qt6-build -R vedit-module-tree-selftest --output-on-failure` **before** the Rust static library was rebuilt | **RED**: `FAIL packaged.answered — elaboration failed: slang failed: slang binary not found (tried 4 location(s); set VEDIT_SLANG)`; `60 checks, 1 failure(s)`; ctest `0% tests passed` | `21-selftest-before-rust-rebuild.log` |
| 6 | same ctest **after** `build-qt.sh` rebuilt the Rust layer + product | **GREEN**: `ok packaged.answered`, `ok packaged.instances — instances=3`, `61 checks, 0 failure(s)` | `22-selftest-after-rebuild.log` |
| 7 | lane self-test executed **through the real AppDir loader** (`<AppDir>/ld-linux-x86-64.so.2 --library-path <AppDir>/lib ...`, `VEDIT_SLANG` unset, bundled `$APPDIR/bin/slang`): the AppDir-root layout, exactly as AppRun launches the product | `ok packaged.appdir-slang — pre-existing .../AppDir/bin/slang`; `ok packaged.answered`; `ok packaged.instances — instances=3`; `61 checks, 0 failure(s)`; **EXIT=0** | `23-appdir-loader-e2e.log` |
| 8 | `sh -n packaging/runtime/AppRun` + fake-AppDir probe (a /tmp AppDir whose "loader" prints the environment) | syntax OK; `VEDIT_SLANG=<AppDir>/bin/slang`; verible LS/LINT/FORMAT set when the files exist; a caller-provided `VEDIT_VERIBLE_LS=/caller/verible-ls` is preserved | `13-apprun-env.log` |
| 9 | product wiring presence | the shared key is in the product binary and in the diagnostics producer TU; `T vedit::workspace::moduleTreeSlangBinary(QObject const*)` is in the module-tree TU; `target/release/libvd_ffi.a` rebuilt by the gate (16:20:20) | `40-binary-wiring-check.log` |
| 10 | rebuilt product offscreen in the Qt6 image with the project fixture (`--project probes/slang-spike/fixture --hold 3000`) | **EXIT=0**; `module-tree: installed (root='.../probes/slang-spike/fixture' ...)`; `TOOLCHAIN source=verible:local slang:local missing=[]` | `25-product-offscreen-run.log` |

The RED/GREEN pair is the same binary target and the same check; the only
difference is whether the rebuilt Rust static library contains the new base.
That isolates the resolver fix as the cause of the change.

---

## 3. What was verified, and what was not

**Verified:**
* The exact user-visible failure text is reproduced by a lane check before the
  fix, and disappears after it (`tried 4 location(s)` → success).
* The packaged AppDir layout (`exe_dir` = AppDir ROOT, tool at
  `<AppDir>/bin/slang`, no env) resolves and a REAL hierarchy elaboration
  returns 3 instances — run through the actual AppDir loader.
* The Module Tree selector returns the diagnostics-resolved path when published,
  keeps `VEDIT_SLANG` as the explicit override, and returns empty otherwise.
* `bash scripts/build-qt.sh` stays green at **37/37**, including the module-tree
  self-test and the xcb smoke.
* `cargo test`, clippy and rustfmt for both touched crates.
* AppRun exports the expected environment, preserves caller-provided Verible
  values, and passes `sh -n`.

**Not proven / limits (honest):**
* The delivered `VEdit-0.1.1-x86_64.AppImage` artifact itself was **not**
  rebuilt and re-run as a GUI in this environment. The packaged-layout proof is
  the rebuilt lane self-test executed through the real AppDir loader
  (`23-appdir-loader-e2e.log`), plus the AppRun environment probe
  (`13-apprun-env.log`). A packaging-pipeline rebuild is still required to ship
  the fixed AppImage.
* No GUI-level assertion of the Module Tree *dock text* was made. The C++
  selector is covered by the self-test; the holder's call site is covered by
  compilation and by the real-service run of the same client. The panel status
  string itself was not captured from the product window.
* A full product GUI launch **through the AppDir loader** (offscreen, with a
  project fixture) was attempted but did not terminate within the probe timeout
  (`24-product-appdir-run.log`, `PIPELINE_EXIT=124`), so no claim is made from
  it; the packaged layout is proven by the lane self-test run through that same
  loader (`23-appdir-loader-e2e.log`) and the product itself is smoke-run in the
  Qt6 image (`25-product-offscreen-run.log`).
* `VEDIT_SLANG` remains ahead of the resolved toolchain (documented in the
  header and in `moduleTreeSlangBinary`). This is the lane's pre-existing
  precedence. Note the diagnostics Qt resolver itself orders bundled > local >
  PATH > env; the module tree deliberately treats an explicit env value as the
  override, matching the Rust resolver's explicit-option-first contract.
* The historical `crates/vd-deeplint/evidence/T17-REPORT.md` still lists the old
  three package-relative bases. It is a historical evidence file and was left
  untouched by design (AGENT.md: do not overwrite an older batch's evidence).
* The workspace was already dirty before this task; this report lists only the
  seven files this task touched. In particular the root `CMakeLists.txt` shows
  as modified in `git status`, but its mtime is 2026-11-02 (before this task) and
  its diff is not part of this change; no frozen file was edited here.

---

## 4. Environment facts

* Host `cargo` for the pinned 1.99.0 toolchain is unusable (rustup shim reports
  the cargo component as not applicable), so all Rust checks ran in
  `vedit-build:local` with `RUSTUP_TOOLCHAIN=1.99.0`, the same image
  `scripts/build-qt.sh` uses.
* The real AppDir used for the loader E2E is the existing build artifact
  `packaging/runtime/out/AppDir` (Ubuntu glibc 2.43 loader, bundled
  `bin/slang` 12.1.0). No repo fixture was created for E2E; the temporary
  AppDir used by the AppRun probe lived in `/tmp` and was removed.

## 5. Evidence inventory

| File | Contents |
|---|---|
| `10-cargo-test.log` | pinned-image `cargo test -p vd-deeplint -p vd-config` |
| `11-cargo-fmt.log`, `12-cargo-clippy.log` | fmt / clippy for the touched crates |
| `13-apprun-env.log` | `sh -n` + fake-AppDir environment probe |
| `20-cpp-compile.log` | focused C++ compile of the touched targets in the Qt6 image |
| `21-selftest-before-rust-rebuild.log` | RED: exact original failure text |
| `22-selftest-after-rebuild.log` | GREEN: packaged probe passes, 61/61 checks |
| `23-appdir-loader-inputs.log`, `23-appdir-loader-e2e.log` | real AppDir loader E2E |
| `30-build-qt.log` | frozen gate: Rust layer + Qt build + ctest 37/37 + xcb smoke |
| `40-binary-wiring-check.log` | producer/consumer compiled into the product |
| `25-product-offscreen-run.log` | rebuilt product smoke run (module-tree install + TOOLCHAIN line) |
| `24-product-appdir-run.log` | the timed-out AppDir-loader product attempt (negative note only) |
