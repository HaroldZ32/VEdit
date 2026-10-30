//! Verilog/SystemVerilog language detection and GtkSourceView binding (T11).
//!
//! VEdit owns **no** highlighting engine: the tokens are produced by the
//! GtkSourceView 4.6.1 language definitions (`verilog.lang`,
//! `systemverilog.lang`) and coloured by an upstream style scheme. This crate is
//! the glue layer only:
//!
//! * [`LanguageId`] / [`language_id_for_path`] — the explicit, assertable
//!   extension → language table (pure logic, no GTK dependency).
//! * [`supported_extensions`] — the same table as data, for the app wiring and
//!   for the verifier to assert directly.
//! * [`apply_language`] — binds the mapped language to a `GtkSourceBuffer`.
//! * [`apply_style_scheme`] — applies the fixed upstream scheme
//!   ([`DEFAULT_STYLE_SCHEME`], `cobalt`).
//!
//! ## AC-05 evidence route
//!
//! `systemverilog.lang` describes SystemVerilog-specific constructs and pulls
//! the Verilog contexts in through `<context ref="verilog:verilog"/>`, so a
//! string search inside `systemverilog.lang` for `module` proves nothing. The
//! contract's judgement is therefore **rendered style**: the `highlight-probe`
//! binary sets the `systemverilog` language on a buffer and asserts the
//! foreground colour / weight of every AC-05 token category, including the
//! Verilog-inherited `module`. See `evidence/highlight/`.
//!
//! ## Boundaries
//!
//! This crate touches `GtkSourceLanguageManager`, `GtkSourceStyleSchemeManager`
//! and `GtkSourceBuffer` only. It never creates a window or a `gtk::Application`
//! (that belongs to `vd-app`), so highlighting is testable without a window.
#![forbid(unsafe_code)]

mod detect;
mod gtk_glue;

pub use detect::{
    is_supported, language_id_for_path, supported_extensions, LanguageId, SUPPORTED_EXTENSIONS,
};
pub use gtk_glue::{
    apply_language, apply_language_with_manager, apply_style_scheme,
    apply_style_scheme_with_manager, gsv_datadir_override, is_language_available,
    language_for_path, language_manager, language_manager_with, missing_languages,
    style_scheme_manager, style_scheme_manager_with, DEFAULT_STYLE_SCHEME, FALLBACK_STYLE_SCHEMES,
    GSV_DATADIR_ENV,
};

/// Version of this crate, from its manifest.
pub const CRATE_VERSION: &str = env!("CARGO_PKG_VERSION");
