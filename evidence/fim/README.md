# T19 — optional FIM completion (evidence)

Cell `cell-20261030T020634155Z`, board task **T19** (split key T9), frozen contract
`.mpd/cellwork/cell-20261030T020634155Z/contracts/T9.md`, owner Junior Engineer, verifier
Reviewer (board T20).

Deliverable: a data/control layer for optional fill-in-the-middle completion — **off by
default, zero network, zero threads** — with two protocol adapters over one transport seam.
UI wiring (ghost text, Tab/Esc/Alt+\\) is T21's job; this crate only produces and cancels
results.

## Changed paths (`crates/vd-fim/**`)

| path | role |
|---|---|
| `src/settings.rs` | `FimSettings` (projection of `vd_config::FimConfig`), `Protocol`, timeout/token clamps, `unavailable()` |
| `src/secret.rs` | redacted `ApiKey` (no `Display`, `Debug`/`Serialize` = `<redacted>`) |
| `src/error.rs` | `FimError` with a short status-bar `reason()` |
| `src/protocol.rs` | pure request-body builders and response parsers for both protocols |
| `src/transport.rs` | `Transport` trait + `HttpTransport` (ureq/rustls) + error mapping + response cap |
| `src/backends.rs` | `CompletionBackend`, `OpenAiBackend`, `LlamaCppBackend`, `backend_from_settings` |
| `src/service.rs` | `FimService`: one worker, one-slot mailbox, generation cancellation, `worker_spawns()` |
| `src/testing.rs` | `RecordingTransport` test double (counts calls, records requests, optional delay) |
| `src/bin/fim-probe.rs` | evidence producer (`01-probe.log`) |
| `tests/default_off.rs` | public-API integration test of the default-off path |

## Interface for T21 (the app wiring)

```rust
// config → settings (the only mapping T21 has to write)
let settings = FimSettings {
    enabled: fim.enabled,
    protocol: match fim.protocol {
        vd_config::FimProtocol::OpenAi => Protocol::OpenAi,
        vd_config::FimProtocol::LlamaCpp => Protocol::LlamaCpp,
    },
    endpoint: fim.endpoint.clone(),
    model: fim.model.clone(),
    timeout_ms: fim.timeout_ms,
    max_tokens: DEFAULT_MAX_TOKENS,
    api_key: fim.api_key.as_ref().map(|k| ApiKey::new(k.expose_secret())),
};
let service = FimService::from_settings(settings);

// during startup (once): warn instead of silently doing nothing
if !service.enabled() {
    if let Some(reason) = service.unavailable_reason() { status_bar.show(reason); }
}

// Alt+\ → service.start(prefix, suffix);  Esc / further typing → service.cancel()
// per frame → if let Some(result) = service.poll() { … }   // text or FimError::reason()
```

`reason()` strings are already Chinese, short (≤ 80 chars) and secret-free — paste them into
the status bar unchanged.

## The seven hard requirements

| # | requirement | where it is proven |
|---|---|---|
| 1 | disabled ⇒ **zero network, zero threads** | `01-probe.log` lines `DEFAULTS`/`DISABLED` (`worker=false`, `start=None`) + `CHECK default_zero_transport_calls PASS`; unit `disabled_service_is_inert_and_never_touches_a_transport`; integration `default_settings_send_nothing_and_start_no_worker`; no transport is even constructed (`FimService::build` returns before `HttpTransport::new()`, and `with_transport` never stores the injected one) |
| 2 | enabled but endpoint missing ⇒ report, no request, no panic | `HALF_CONFIGURED … reason=Some("FIM 未配置端点")`, `CHECK half_configured_zero_calls PASS`; integration `openai_without_a_model_is_reported_without_a_request` |
| 3 | thread count bounded (no thread per keystroke) | `BURST threads before=5 after=5 transport_calls=1`, `CHECK burst_thread_count_bounded PASS`; unit `rapid_typing_does_not_spawn_more_workers_or_queue_requests` (200 starts, 5 ms mock, asserts `worker_spawns()` unchanged and < 40 calls) |
| 4 | error text has no sensitive material | `FAILURE … reason="FIM HTTP 401"`, `CHECK failure_reason_has_no_secret PASS`; unit `reasons_never_contain_a_secret_or_a_url`, `a_bad_uri_never_echoes_the_url`, `io_errors_are_classified_without_their_message` |
| 5 | response size cap | `protocol::MAX_RESPONSE_BYTES = 64 KiB`; `HttpTransport` reads at most `cap + 1` and returns `ResponseTooLarge` (unit `the_response_cap_is_enforced_at_the_byte`) |
| 6 | both protocol bodies verbatim | `OPENAI_BODY {"max_tokens":64,"model":"qwen2.5-coder","prompt":"assign x","suffix":";\nendmodule"}` and `LLAMA_CPP_BODY {"input_prefix":"always_ff @(","input_suffix":") begin","n_predict":32}` in `01-probe.log`; exact-equality tests `openai_body_has_exactly_the_contract_fields` / `llama_cpp_body_has_exactly_the_contract_fields` also assert there are **no extra fields** |
| 7 | `Failed(reason)` ready for the status bar | `reason()` is a fixed table, ≤ 80 chars (`every_reason_is_short_and_not_empty`), same strings shown by the probe |

## Protocol responses

```
OPENAI_RESPONSE    {"choices":[{"text":" <= 1;"}]}   -> Ok(" <= 1;")
LLAMA_CPP_RESPONSE {"content":" <= '0;"}             -> Ok(" <= '0;")
```

Trailing line breaks are stripped, leading/inner whitespace is preserved (FIM text is
inserted verbatim); a body without the expected field is `InvalidResponse`, never a panic.

## Cancellation, stated honestly

`ureq` has no cancellable handle, so an in-flight request is **not aborted on the wire**.
`cancel()` advances a generation counter, empties the one-slot mailbox and clears pending
results; a late completion whose generation is stale is dropped (`CANCELLED poll=None`,
`CHECK cancel_drops_result PASS`). The configured timeout bounds the socket. This satisfies
AC-14's "typing again cancels the in-flight request" in the sense that matters — **a stale
result never overwrites a newer one** — and the limit is documented here and in `src/service.rs`.

## Security

* The key exists only as `ApiKey`: no `Display`, `Debug`/`Serialize` = `<redacted>`, plaintext
  only via `expose_secret()` at the socket call (`src/secret.rs`, 5 tests).
* `vd-fim` deliberately duplicates `vd_config::ApiKey` instead of depending on `vd-config`
  (no manifest change); both sides lock their behaviour with their own tests.
* Errors carry only a status code, a timeout value or a fixed category string.

## Gates

`02-scope-gates.log` (pinned CentOS 7 image): `cargo fmt -p vd-fim -- --check` exit 0;
`cargo clippy -p vd-fim --all-targets -- -D warnings` exit 0; `cargo test -p vd-fim` exit 0 —
**40 unit + 4 integration tests**. Three consecutive full runs were green (the counter
assertions are serialised with a mutex because `worker_spawns()` is process-wide).

The workspace-wide gate is not this lane's judgement (all lanes share one working tree); it is
re-run at T21/T27/T28.

## Reproduce

Inside the pinned `vedit-build:local` image, from the repository root:

```sh
cargo run -q -p vd-fim --bin fim-probe     # 01-probe.log, exit 0, no socket, no display
sh evidence/fim/run-gates.sh               # 02-scope-gates.log
```

## Bounds (not verified here)

* No real HTTP server is contacted: the transport seam is exercised with
  `RecordingTransport`; the wire path (`ureq`/rustls) is covered by the dependency-policy gate
  (no `openssl`/`native-tls` in `Cargo.lock`) and by T27's e2e, not by this lane.
* The UI half (ghost text, Tab accept, Esc cancel, Alt+\\) belongs to T21; this crate stops at
  "text or reason out of `poll()`".
