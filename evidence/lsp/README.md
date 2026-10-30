# T15 — Verible LSP client lane: evidence

Owner: Deep Worker · board **T15** (split key **T7**) · scope `crates/vd-lsp/**` ·
evidence `evidence/lsp/`.

## What the lane delivers

One bundled `verible-verilog-ls` process is the only backend, spawned lazily and
spoken to over stdio LSP. All JSON-RPC framing comes from `async-lsp 0.2.4`
(`MainLoop::new_client` + `Router`); this crate never serializes or parses a
frame. The transport is `tokio::process` pipes bridged with
`tokio-util`'s `compat()` — the two lines the captain approved.

Public surface consumed by the application lane (T21):

```rust
let mut client = LspClient::new(LspConfig::new(ls_path, workspace_root)
    .with_rules_config(rules_path));                 // from T13's loaded.project.rules_path
client.open_document(uri, "systemverilog", text).await?;
let generation = client.diagnostics_generation();
let diagnostics = client.diagnostics();               // watch::Receiver<DiagnosticsSnapshot> for the UI
client.change_document(&uri, changes).await?;
let edits = client.formatting(&uri, 2, true).await?;  // apply via vd_lsp::apply_text_edits
let locations = client.definition(&uri, position).await?;
client.shutdown().await?;                             // shutdown/exit + reap
```

Modules: `client` (lifecycle, typed requests, cancellation), `config`
(argv/timeouts), `diagnostics` (watch snapshot + rule extraction), `documents`
(versions + restart re-sync), `edits` (TextEdit application), `position`
(UTF-16 ⇄ bytes).

## Capability boundary (measured, not assumed)

`initialize` result (`probe-transcript.txt`, capabilities JSON):

- advertised and used: `definitionProvider`, `referencesProvider`,
  `documentSymbolProvider`, `renameProvider`, `documentHighlightProvider`,
  `documentFormattingProvider`, `documentRangeFormattingProvider`,
  `codeActionProvider`, `diagnosticProvider`, `textDocumentSync.change = 2`
  (incremental, `openClose = true`);
- **absent / disabled**: `hoverProvider: false`, no `completionProvider`, no
  `signatureHelpProvider`. These are requirements Non-goals: the client does not
  implement them and does not fake them. Completion's only source is FIM (T19).

## Verible flags (measured from the bundled binary)

`verible-verilog-ls v0.0-4298-g085eef05` exposes, among others:
`--rules_config=PATH` (setting it disables search), `--rules_config_search`,
`--file_list_path=NAME` (default `verible.filelist`),
`--push_diagnostic_notifications` (default true). It has **no include-dir or
define flags** — those belong to slang (T17); this lane passes only rules and an
optional filelist.

### Rules policy (decided by the captain, implemented as such)

- T13 resolves `loaded.project.rules_path`; if present it is passed explicitly as
  `--rules_config=<absolute path>` — deterministic, and identical to what
  `--print-config` shows.
- If no rules file was resolved, **no rules flag at all**: the client never adds
  `--rules_config_search`, so the rule file in force can never differ from the
  one reported by the configuration layer.

Both argv cases are printed in `probe-transcript.txt`, and the printed argv is
the argv of the client that actually ran:

```
rules_file=<none>
server_argv=[]

rules_file=/vedit-build/crates/vd-lsp/fixtures/project_with_rules/.rules.verible_lint
server_argv=["--rules_config=/vedit-build/crates/vd-lsp/fixtures/project_with_rules/.rules.verible_lint"]
```

The contrast is behavioural, not only textual: the no-rules run reports
`derived_rule=Some("line-length")`, while the explicit-rules run suppresses that
rule (the same file, the same violation).

## Lifecycle (AC-09)

- **Lazy**: `LspClient::new` spawns nothing; the first call that needs the server
  starts it (`lazy_start_running=false` in the transcript).
- **Handshake**: `initialize` (bounded by `init_timeout`, default 10 s) then
  `initialized`; the server's working directory is the project root.
- **Crash restart**: the main-loop task finishing marks the session dead; the
  next call reaps the old child and starts a new one (bounded by
  `MAX_RESTARTS = 3`), re-sends `initialize`, and re-opens every tracked document
  with its current version and text.
- **No zombie**: `shutdown` sends `shutdown`/`exit`, waits for the child (bounded
  by `shutdown_timeout`), kills and re-waits if the wait timed out, then aborts
  the main-loop task. `Drop` is a last-resort guard (`start_kill` + abort).
  Evidence: after each probe run `ps -eo pid,ppid,stat,comm | grep -i verible`
  prints `none`, and the crash-restart test asserts `/proc/<pid>` disappears for
  both the killed and the restarted process.

## Timeouts and cancellation

- Timeouts: `init_timeout` 10 s, `request_timeout` 15 s, `shutdown_timeout` 5 s
  (all configurable per client). `initialize` timeout and a real request timeout
  are exercised by tests (`initialize_timeout_is_enforced_and_the_child_is_reaped`,
  `request_timeout_and_cancellation_are_enforced`).
- Cancellation: every request is a future owned by the caller — dropping it
  abandons the response. `request_cancellable` additionally takes
  `(CancelHandle, CancelToken)` and uses a `biased` `select!`, so a keystroke can
  abort an in-flight navigation/formatting request instead of waiting for a stale
  answer. Cancellation is client-side: the server may still finish the work, but
  its response is discarded. Document versions (`documents.rs`) make stale
  diagnostics attributable.
- The UI is never blocked: the whole API is async; the application drives it on a
  tokio runtime and renders from `watch::Receiver<DiagnosticsSnapshot>`.

## Formatting (AC-07)

`textDocument/formatting` returns `TextEdit`s; the LSP layer never mutates a
document. `apply_text_edits` sorts edits, rejects overlaps and applies them using
UTF-16 positions. The integration test compares the applied result **byte for
byte** with the bundled `verible-verilog-format` on the same file:
`byte_equal_to_bundled_formatter=true` in the transcript.

## Diagnostics (AC-06)

`publishDiagnostics` is recorded into an immutable snapshot with a monotonic
`generation` and `received_at`; `wait_for_diagnostics` returns as soon as a newer
notification for the URI arrives. Measured latency from `didOpen` to the
notification in this container: **1 ms** (budget 3 s; transcript). Positions are
LSP-native 0-based UTF-16; `position.rs` is the shared conversion used by the
editor lane as well.

Verible puts the lint rule inside the message (`… [Style: line-length][line-length]`)
and leaves `code` empty, so `diagnostic_rule()` prefers `code` and otherwise
parses the trailing bracketed token — the problem panel gets a rule name for
AC-06 without re-parsing engine-specific text downstream.

## Tests (all green)

```
cargo test -p vd-lsp
  16 passed (unit: positions incl. emoji/CRLF, edits incl. overlap rejection,
             documents, diagnostics, config argv, rule extraction)
   8 passed (integration, real verible-verilog-ls):
     capabilities_are_the_measured_ten_and_start_is_lazy
     diagnostics_arrive_within_three_seconds_and_clear_after_a_fix
     definition_references_symbols_rename_and_highlight
     formatting_edits_reproduce_the_bundled_formatter_byte_for_byte
     rules_config_is_passed_explicitly_and_changes_diagnostics
     lazy_start_crash_restart_and_shutdown_leave_no_zombie
     request_timeout_and_cancellation_are_enforced
     initialize_timeout_is_enforced_and_the_child_is_reaped
```

`scoped-gates.log`: `cargo fmt -p vd-lsp -- --check` EXIT 0,
`cargo clippy -p vd-lsp --all-targets -- -D warnings` EXIT 0,
`cargo test -p vd-lsp` EXIT 0.

## Evidence files

| file | content |
|---|---|
| `probe-transcript.txt` | argv (with/without rules) used by the client, initialize request/result JSON, diagnostics with latency plus **derived rule / raw code / message**, definition location, formatting byte-equality, shutdown/pid state |
| `scoped-gates.log` | fmt/clippy/test exit codes |
| `timeouts-cancellation-lifecycle.txt` | timeout, cancellation and crash-restart test output |
| `zombie-proof.txt` | `ps` after the probe: no verible process and no verible zombie |
| `run-evidence.sh` | regenerates all of the above inside the pinned image |

## Limitations / notes for T16

- The transcript shows the JSON-RPC **bodies**; framing bytes are produced by
  `async-lsp`, not by this crate (per the lane contract).
- `request_cancellable` is a client-side abort; `async-lsp` exposes no outbound
  `$/cancelRequest` id control, so the server-side work is not interrupted.
- Dynamic registration is not implemented (no client handlers for
  `client/registerCapability`); the server advertises static capabilities.
- Include dirs/defines are not passed to the LSP (no such flags); they are for
  slang (T17). The optional `--file_list_path` is supported by `LspConfig`.
- Repository-wide `bash scripts/gates.sh` was red in `crates/vd-editor` (T9, in
  progress) at the time of this task; the scoped gates above cover this crate.

## Reproduce

```bash
# inside the pinned image (scripts/lib.sh shows the exact docker invocation)
cargo fmt -p vd-lsp -- --check
cargo clippy -p vd-lsp --all-targets -- -D warnings
cargo test -p vd-lsp
cargo run -q -p vd-lsp --example lsp-probe -- crates/vd-lsp/fixtures/project

# regenerate evidence
docker run --rm -e HOME=/vedit-build/target/home \
  -e CARGO_HOME=/vedit-build/target/cargo-home -e RUSTUP_HOME=/opt/rust \
  -e RUSTUP_TOOLCHAIN=1.99.0 -v "$PWD:/vedit-build" -w /vedit-build \
  vedit-build:local bash /vedit-build/evidence/lsp/run-evidence.sh
```
