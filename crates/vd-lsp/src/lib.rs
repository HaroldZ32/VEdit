//! Verible language-server client for VEdit (T15).
//!
//! # Scope
//!
//! One bundled `verible-verilog-ls` process is the **only** backend. The client
//! speaks stdio LSP through `async-lsp` (no hand-written JSON-RPC framing) and
//! exposes exactly the capabilities the server advertises:
//! definition, references, documentSymbol, rename, documentHighlight,
//! documentFormatting, documentRangeFormatting, codeAction, diagnostics and
//! incremental text synchronisation. Hover, completion and signature help are
//! **not** part of this lane and are not implemented.
//!
//! # Lifecycle
//!
//! [`LspClient::new`] spawns nothing (lazy start). The first call that needs the
//! server spawns it, performs the `initialize`/`initialized` handshake and
//! re-opens tracked documents. A crashed server is detected through the main
//! loop task and restarted lazily (bounded by [`MAX_RESTARTS`]), with every open
//! document re-synchronised. [`LspClient::shutdown`] performs the
//! `shutdown`/`exit` handshake and reaps the child, so it leaves no zombie.
//!
//! # Formatting
//!
//! The server returns `TextEdit`s; this crate never mutates a document. The
//! application applies them through [`apply_text_edits`], which is what makes
//! AC-07 (byte-identical to `verible-verilog-format`) testable.
//!
//! # Rules
//!
//! When the project has a `.rules.verible_lint`, the caller passes it explicitly
//! ([`LspConfig::with_rules_config`]) and the client starts the server with
//! `--rules_config=<path>`. It deliberately does not enable
//! `--rules_config_search`, so the rules file shown by `--print-config` is the
//! one actually in force.
//!
//! # Cancellation
//!
//! Every request is a future owned by the caller; dropping it abandons the
//! response. [`LspClient::request_cancellable`] additionally takes a
//! [`CancelHandle`]/[`CancelToken`] pair so a keystroke can abort an in-flight
//! navigation or formatting request instead of waiting for a stale result.
//!
//! # Positions
//!
//! LSP positions are UTF-16 code units; [`position_to_byte`] and
//! [`byte_to_position`] convert explicitly and reject positions inside a
//! surrogate pair.

#![forbid(unsafe_code)]

mod client;
mod config;
mod diagnostics;
mod documents;
mod edits;
mod position;
mod session;

pub use client::{CancelHandle, CancelToken, LspClient, MAX_RESTARTS};
pub use config::LspConfig;
pub use diagnostics::{diagnostic_rule, DiagnosticsSnapshot, PublishedDiagnostics};
pub use documents::OpenDocument;
pub use edits::{apply_content_changes, apply_text_edits};
pub use position::{byte_to_position, line_start_byte, position_to_byte};
// The C ABI layer (vd-ffi) converts these types without depending on
// lsp-types directly; the aliases keep that dependency at one crate.
pub use lsp_types::Location as SessionLocation;
pub use lsp_types::Range as SessionRange;

pub use session::{
    range_parts, severity_code, PollOutcome, SessionDiagnostic, SessionHandle, SessionPhase,
    SessionRequest, SessionResponse, SessionStatus, SkippedFile, SymbolNode, WorkspaceTextEdit,
    DEFAULT_DIAGNOSTICS_BUDGET, DEFAULT_STOP_BUDGET, DEFAULT_TAB_SIZE,
};

/// Version of this crate, from its manifest.
pub const CRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
