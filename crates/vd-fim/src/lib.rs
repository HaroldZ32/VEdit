//! Optional fill-in-the-middle (FIM) completion — data and control interface.
//!
//! # Default off, zero network, zero threads
//!
//! FIM is a purely optional add-on. With the product default settings (disabled)
//! the service is constructed in the unavailable state: no transport is built, no
//! worker thread exists, [`FimService::start`] returns `None` and nothing on the
//! editing path changes. There is no default endpoint, model or key anywhere in
//! this crate.
//!
//! # What the layer provides
//!
//! * [`FimSettings`] — the transport-level projection of `vd_config::FimConfig`
//!   (the app layer maps one to the other; `vd-fim` does not depend on `vd-config`).
//! * [`CompletionBackend`] with two adapters — OpenAI-compatible
//!   `POST /v1/completions` (`prompt` + `suffix`) and llama.cpp `POST /infill`
//!   (`input_prefix` + `input_suffix`), over one [`Transport`] seam.
//! * [`FimService`] — one long-lived worker thread, a one-slot mailbox, and
//!   generation-based cancellation, so rapid typing never spawns a thread per
//!   keystroke and a stale completion never overwrites a newer one.
//! * [`FimError`] — typed failures whose [`FimError::reason`] is a short
//!   status-bar sentence with no endpoint, header or key in it.
//!
//! # Security
//!
//! The key lives in [`ApiKey`]: no `Display`, redacted `Debug` and `Serialize`,
//! plaintext only through `expose_secret()`, which the transport calls once per
//! request. Nothing in an error message, a log line or the status bar contains it.
//!
//! # Scope
//!
//! The editor UI (ghost text, Tab to accept, Esc to cancel, Alt+\ to trigger) is
//! wired by the app layer; this crate only produces and cancels results.
#![forbid(unsafe_code)]

pub mod backends;
pub mod error;
pub mod protocol;
pub mod secret;
pub mod service;
pub mod settings;
pub mod testing;
pub mod transport;

pub use backends::{
    backend_from_settings, BackendSpec, CompletionBackend, CompletionRequest, LlamaCppBackend,
    OpenAiBackend,
};
pub use error::{FimError, TransportCategory, MAX_REASON_CHARS};
pub use protocol::{
    llama_cpp_request_body, openai_request_body, parse_llama_cpp_response, parse_openai_response,
    MAX_RESPONSE_BYTES,
};
pub use secret::ApiKey;
pub use service::{worker_spawns, FimService, Generation};
pub use settings::{
    FimSettings, Protocol, DEFAULT_MAX_TOKENS, DEFAULT_TIMEOUT_MS, MAX_MAX_TOKENS, MAX_TIMEOUT_MS,
    MIN_TIMEOUT_MS,
};
pub use testing::RecordingTransport;
pub use transport::{HttpTransport, Transport};

/// Version of this crate, from its manifest.
pub const CRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
