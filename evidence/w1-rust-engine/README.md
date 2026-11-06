# Rust engine layer — dual backend, engine selection, batch lint, ABI (T138 / board T3)

Task: `cell-20261102T050548232Z` / `T3` — contract
`.mpd/cellwork/cell-20261102T050548232Z/contracts/rust-backend-services.md`.
Scope written: `crates/vd-lsp/`, `crates/vd-deeplint/`,
`crates/vd-ffi/src/{lsp,deeplint,capi}.rs`, `crates/vd-ffi/include/vd_ffi.h`,
`evidence/w1-rust-engine/`.

## What was delivered, mapped to the acceptance criteria

| AC | Delivered | Evidence |
| --- | --- | --- |
| AC-1.1 (read side of the key) | `vd_lsp_session_start_v2(..., backend, include_dirs, defines)`; `LspBackend::{Verible,Slang}` (0/1); `vd_lsp_session_status` now reports `backend` + `backend_name`. Old `vd_lsp_session_start` is unchanged and delegates with backend 0. | `01-cargo-test.txt`; unit test `start_v2_rejects_an_unknown_backend_and_starts_a_known_one` (`crates/vd-ffi/src/lsp.rs`) |
| AC-1.6 (engines are separate) | `vd_deeplint_lint_v2(..., engine, ...)`; every report records `engine` + `tool`; LSP diagnostics origin 1, CLI origin 2. | `01-cargo-test.txt`; `crates/vd-deeplint/src/engine.rs` |
| AC-2.5 (`-I`/`-D` really reach the tools) | slang: real argv `-I<dir>` / `-D<name>=<value>`; verible LSP: a generated `.f` (`+incdir+` / `+define+`) passed as `--file_list_path`; verible lint: the tool accepts neither (measured) and the engine's argv dump says so. | `04-argv-dump.txt`, `05-...` (below), `03-tool-versions.txt` |
| AC-2.6 (single-file first) | the single-file job path is untouched (`vd_deeplint_lint` / `_v2` are ordinary jobs); the batch is a separate registry. | `01-cargo-test.txt` (existing `abi.rs` suite still green) |
| AC-2.7 (batch off the GUI thread, progress) | `vd_deeplint_batch_start/_progress/_poll/_cancel/_release`; ids start at `1<<32` so they never collide with service job ids. | `01-cargo-test.txt` (`a_batch_reports_progress_and_delivers_its_result_once`), `06-cancel-observation.txt` |
| AC-2.8 (cancel really stops) | the batch's cancel flag IS the per-file runner's cancel flag → the running child is killed; no later file starts. | `06-cancel-observation.txt` |
| AC-2.10 (tool + stage on failure) | `LintReport::{tool, stage}`; the ABI error text is `"<tool> failed at <stage>: <reason>"`; `VdLintProgress.tool`/`.stage`. | `01-cargo-test.txt` (`a_missing_binary_fails_the_pass_with_its_stage_and_tool`) |
| AC-2.13 (dedup key + source) | `vd_deeplint::diagnostic_key` (path + 0-based line + shared severity class + whitespace-folded message, `0x1F`-joined); `VdDiagnostic.{file,key,origin}`, `VdLspDiagnostic.origin`, `vd_diagnostic_key(origin, ...)` for LSP rows (the LSP response carries no path, so the caller supplies the file it requested). | `01-cargo-test.txt` (`the_dedup_key_is_identical_for_an_lsp_and_a_cli_row`, `the_same_problem_gets_the_same_key_from_both_paths`) |
| Capability query (reasons) | `vd_lsp_backend_capabilities` (static) and `vd_lsp_session_capabilities` (live); every row has a reason; `basis` = `measured` / `live` / `declared`. | `05-lsp-capabilities.json`, `01-cargo-test.txt` |
| Append-only C ABI | no existing declaration was removed (`git diff` of the header has zero deleted `vd_*` declarations); the header is valid C++17 and the new structs zero-initialise. | `09-header-cxx-check.txt` |

## Evidence files

* `01-cargo-test.txt` — `cargo test -p vd-lsp -p vd-deeplint -p vd-ffi`, exit 0.
* `02-cargo-clippy.txt` — same three crates, `--all-targets -- -D warnings`, exit 0.
* `03-tool-versions.txt` — measured versions and the flag surface that decides the
  argv: slang 12.1.0 has `-I`/`-D`; `verible-verilog-lint` rejects
  `--file_list_path` and has no include/define flag; `verible-verilog-ls` has
  `--file_list_path` + `--rules_config`.
* `04-argv-dump.txt` — the argv a real run produced (fake-tool dump) and the real
  slang run whose outcome changes with the settings: **2 diagnostics without
  `-I/-D`, 0 with** them.
* `05-lsp-capabilities.json` — the real `verible-verilog-ls v0.0-4298-g085eef05`
  `initialize` result; the static table is pinned to exactly this.
* `06-cancel-observation.txt` — `phase=cancelled done=0/5 diagnostics_after_cancel=0
  invocations_at_cancel=1 invocations_500ms_later=1`.
* `07-fmt-drift.txt` — the changed files are formatted; the one file the workspace
  fmt check still flags (`crates/vd-lsp/tests/buffer_change.rs`) is unmodified by
  this task (pre-existing drift at HEAD).
* `08-workspace-clippy.txt` — `--workspace` cannot run on this host (GTK system
  libraries absent; AGENT.md §4 runs those gates in the pinned image); every
  non-GTK crate is clean when checked one at a time.
* `09-header-cxx-check.txt` — the generated header compiles as C++17.
* `10-verible-filelist-route.txt` — the verible-LS `-I`/`-D` route, end to end: the
  session argv really carries `--file_list_path=/tmp/vedit-filelist-…f`, the
  generated file contains `+incdir+/proj/inc` / `+define+WIDTH=8`, the same text is
  available as a preview before a session exists, and releasing the session deletes
  the file.

## Bounds that are NOT proven here

1. **slang-server is not shipped by this task** (T4 built and measured it:
   `evidence/w2-spike/`). The capability rows are now `basis=measured` against
   `slang-server v0.3.1+2ad996f`; `capabilities_from_server` remains the live
   refinement for a different server version. The binary is not in the delivery
   package yet (spike artifact: sha256 `f691600f…`, ~11.7 MB).
2. **`verible-verilog-lint` cannot receive `-I`/`-D`** (measured: `--helpfull` has
   neither, and `--file_list_path` is `Unknown command line flag`). The engine says
   so in its argv dump instead of dropping them silently. The Verible *language
   server* does receive them, through the generated file list.
3. **Verible CJK columns** are assumed to be byte offsets like slang's
   `--diag-column-unit byte`; that equality is measured on ASCII only.
4. Verible lint findings are reported as `Warning` with the rule name as the code —
   verible has no separate severity levels.
5. The batch pass is **sequential** on purpose (one heavy tool at a time); progress
   is therefore per file.
6. The Qt lane was not compiled here (no Qt/KF6 toolchain on this host). The header
   change is append-only, and the C++ call sites read fields by name from a
   `{}`-initialised struct.
7. Other agents were editing the same tree during this task (`crates/vd-settings`,
   `crates/vd-ffi/src/settings.rs`, `src/**`, `crates/vd-lsp/src/documents.rs`).
   None of those paths are part of this task's diff.

## A transient failure that was observed and explained

One full run in the middle of this task failed at `Doc-tests vd_deeplint` with
`error[E0463]: can't find crate for 'serde'`. Re-running the same command (and
`cargo test -p vd-deeplint --doc` alone) passed immediately, and the final run in
`01-cargo-test.txt` is green with `cargo-test-exit=0`. The cause is the shared
`target/` directory: other agents were building the same workspace concurrently,
so a dependency artifact was being rewritten while rustdoc linked against it.
This is a workspace-level concurrency artifact, not a defect of the change — the
final recorded run is the one to judge.

## Repair round after the T3 review (A-6 + measured capabilities)

1. **`crates/vd-app/**` constructor fix** (scope extended by **ADDENDUM A-6**, three
   files only: `slang_worker.rs`, `selftest.rs`, `diagnostics.rs`): the three
   `LintReport` literals now fill `engine`/`tool`/`stage`. No `Default`, no other
   edit. Gate shape: `cargo clippy --workspace --all-targets -- -D warnings`
   inside the GTK build image (CentOS 7) → **exit 0**; `cargo fmt --all -- --check`
   → exit 0 at the time of that run.
2. **verible-LS `-I`/`-D` route** is now pinned by a test and a dump
   (`10-verible-filelist-route.txt`): the generated `.f` carries `+incdir+` /
   `+define+`, the session argv carries `--file_list_path=<that file>`, and the
   file is removed with the session. The content was correct — no defect found.
3. **slang capability table → measured** (`crates/vd-lsp/src/backend.rs`,
   `basis = measured`, source = `evidence/w2-spike/04-built-initialize-handshake.txt`)
   with a `note` field that states the asymmetry; `VdLspCapabilities.note` exposes it
   through the C ABI. Neither backend claims `signatureHelp` (editor-local
   synthesis is an editor-layer decision, not a backend capability).

Gate status after the repair: `cargo test -p vd-lsp -p vd-deeplint -p vd-ffi` exit 0
(`01-cargo-test.txt`), the same three crates' clippy exit 0 (`02-cargo-clippy.txt`),
every file this task touched is rustfmt-clean. The only workspace fmt drift left is
`crates/vd-config/src/project.rs` and `crates/vd-ffi/src/project.rs`, which belong to
the in-flight T6 (project model) and are not part of this task's diff.
