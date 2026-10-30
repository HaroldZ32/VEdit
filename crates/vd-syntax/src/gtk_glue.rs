//! Thin GtkSourceView glue: resolve the mapped language and bind it to a buffer.
//!
//! Everything here is glue over upstream GtkSourceView 4.6.1; VEdit does not own
//! a lexer, a colour table or a renderer. The language definitions
//! (`verilog.lang`, `systemverilog.lang`) and the style schemes are upstream
//! LGPL data assets bundled with the product.
//!
//! ## Search-path priority
//!
//! * **Production path (no variable set):** the manager keeps GtkSourceView's own
//!   default search path, which follows `XDG_DATA_DIRS` — the packaged launcher
//!   points that at `$APPDIR/usr/share`, so `gtksourceview-4/language-specs` is
//!   found without any VEdit-specific variable.
//! * **Explicit override (`VEDIT_GSV_DATADIR`):** *appended after* the XDG-derived
//!   directories rather than replacing them. The value is the GtkSourceView data
//!   directory itself, i.e. the one containing `language-specs/` and `styles/`
//!   (the T5 `AppRun` sets `$APPDIR/usr/share/gtksourceview-4`). It exists for
//!   tests and unusual deployments and is never required, so a packaging
//!   mistake cannot silently remove syntax highlighting.
//!
//! Measured in the pinned CentOS 7 image: `LanguageManager::new()` resolves the
//! default search path to `$XDG_DATA_HOME` plus `$XDG_DATA_DIRS` (it does *not*
//! include the GtkSourceView build prefix), which is exactly why the packaged
//! launcher sets `XDG_DATA_DIRS` to `$APPDIR/usr/share`.
//!
//! These functions only touch `GtkSourceLanguageManager`, `GtkSourceStyleScheme`
//! and `GtkSourceBuffer`. They never create a window or a `gtk::Application`, so
//! the highlighting layer is testable without a real window.

use std::path::{Path, PathBuf};

use sourceview4::prelude::*;

use crate::LanguageId;

/// Environment variable that appends an explicit GtkSourceView data directory.
pub const GSV_DATADIR_ENV: &str = "VEDIT_GSV_DATADIR";

/// The fixed style scheme VEdit ships and screenshots (upstream scheme).
pub const DEFAULT_STYLE_SCHEME: &str = "cobalt";

/// Upstream schemes tried in order; the first one present wins.
///
/// Only upstream schemes are listed — VEdit defines no colours of its own. The
/// fallbacks exist because a trimmed deployment may ship a subset of the styles.
pub const FALLBACK_STYLE_SCHEMES: &[&str] = &["cobalt", "Adwaita", "classic", "kate"];

const LANGUAGE_SPECS_SUBDIR: &str = "language-specs";
const STYLES_SUBDIR: &str = "styles";

/// The explicit GtkSourceView data directory from [`GSV_DATADIR_ENV`], if set.
#[must_use]
pub fn gsv_datadir_override() -> Option<PathBuf> {
    std::env::var_os(GSV_DATADIR_ENV)
        .filter(|value| !value.is_empty())
        .map(PathBuf::from)
}

fn override_search_path(datadir: &Path, subdir: &str) -> Option<String> {
    datadir.join(subdir).to_str().map(str::to_string)
}

/// GtkSourceView's own default search path (XDG-derived, measured in the pinned
/// CentOS 7 image: `$XDG_DATA_HOME` then `$XDG_DATA_DIRS`, each
/// `<dir>/gtksourceview-4/<subdir>`).
fn current_search_path(manager: &sourceview4::LanguageManager) -> Vec<String> {
    manager
        .search_path()
        .iter()
        .map(ToString::to_string)
        .collect()
}

/// Appends `datadir/<subdir>` to the manager's current search path.
fn append_override_path(manager: &sourceview4::LanguageManager, datadir: &Path, subdir: &str) {
    let mut paths = current_search_path(manager);
    paths.extend(override_search_path(datadir, subdir));
    let refs: Vec<&str> = paths.iter().map(String::as_str).collect();
    manager.set_search_path(&refs);
}

/// Builds a language manager honouring the search-path priority documented on
/// this module. `datadir` is the explicit override (the measured value in the
/// pinned build image is `/opt/gsv4/share/gtksourceview-4`); `None` keeps
/// GtkSourceView's own default path.
#[must_use]
pub fn language_manager_with(datadir: Option<&Path>) -> sourceview4::LanguageManager {
    let manager = sourceview4::LanguageManager::new();
    if let Some(datadir) = datadir {
        append_override_path(&manager, datadir, LANGUAGE_SPECS_SUBDIR);
    }
    manager
}

/// Language manager using [`GSV_DATADIR_ENV`] when set, defaults otherwise.
#[must_use]
pub fn language_manager() -> sourceview4::LanguageManager {
    language_manager_with(gsv_datadir_override().as_deref())
}

/// The upstream language for `path`, or `None` when the extension is unknown.
#[must_use]
pub fn language_for_path(
    manager: &sourceview4::LanguageManager,
    path: &Path,
) -> Option<sourceview4::Language> {
    let language_id = crate::language_id_for_path(path)?;
    manager.language(language_id.id())
}

/// Whether the GtkSourceView data directory actually provides `language_id`.
///
/// This is the guard against "the window opens but nothing is highlighted": when
/// the launch environment does not expose `gtksourceview-4/language-specs` (a
/// bare `cargo run`, or a launcher that forgot `XDG_DATA_DIRS`), the language
/// lookup fails silently at the widget level, so the app layer must ask this and
/// warn in the status bar / log instead.
#[must_use]
pub fn is_language_available(manager: &sourceview4::LanguageManager, language_id: &str) -> bool {
    manager.language(language_id).is_some()
}

/// The product languages (Verilog / SystemVerilog) that this GtkSourceView data
/// directory does **not** provide; empty when highlighting is fully available.
#[must_use]
pub fn missing_languages(manager: &sourceview4::LanguageManager) -> Vec<LanguageId> {
    let mut missing: Vec<LanguageId> = crate::SUPPORTED_EXTENSIONS
        .iter()
        .map(|(_, language)| *language)
        .filter(|language| !is_language_available(manager, language.id()))
        .collect();
    missing.sort_unstable();
    missing.dedup();
    missing
}

/// Enables syntax highlighting on `buffer` and binds the language for `path`.
///
/// Returns the GtkSourceView language id actually bound (`verilog` /
/// `systemverilog`), which is the assertable identification result. An unknown
/// extension returns `None` and leaves the buffer unbound: the file is plain
/// text, with no highlighting and no error.
pub fn apply_language_with_manager(
    manager: &sourceview4::LanguageManager,
    buffer: &sourceview4::Buffer,
    path: &Path,
) -> Option<String> {
    let language = language_for_path(manager, path)?;
    let bound = language.id()?.to_string();
    buffer.set_highlight_syntax(true);
    buffer.set_language(Some(&language));
    Some(bound)
}

/// [`apply_language_with_manager`] using the default language manager.
pub fn apply_language(buffer: &sourceview4::Buffer, path: &Path) -> Option<String> {
    let manager = language_manager();
    apply_language_with_manager(&manager, buffer, path)
}

/// Builds a style-scheme manager with the same search-path priority.
#[must_use]
pub fn style_scheme_manager_with(datadir: Option<&Path>) -> sourceview4::StyleSchemeManager {
    let manager = sourceview4::StyleSchemeManager::new();
    if let Some(datadir) = datadir {
        let mut paths: Vec<String> = manager
            .search_path()
            .iter()
            .map(ToString::to_string)
            .collect();
        paths.extend(override_search_path(datadir, STYLES_SUBDIR));
        let refs: Vec<&str> = paths.iter().map(String::as_str).collect();
        manager.set_search_path(&refs);
    }
    manager
}

/// Style-scheme manager using [`GSV_DATADIR_ENV`] when set, defaults otherwise.
#[must_use]
pub fn style_scheme_manager() -> sourceview4::StyleSchemeManager {
    style_scheme_manager_with(gsv_datadir_override().as_deref())
}

/// Applies the first available upstream scheme from [`FALLBACK_STYLE_SCHEMES`]
/// to `buffer`; returns the scheme id actually applied.
pub fn apply_style_scheme_with_manager(
    manager: &sourceview4::StyleSchemeManager,
    buffer: &sourceview4::Buffer,
) -> Option<String> {
    let scheme = FALLBACK_STYLE_SCHEMES
        .iter()
        .find_map(|id| manager.scheme(id))?;
    let applied = scheme.id()?.to_string();
    buffer.set_style_scheme(Some(&scheme));
    Some(applied)
}

/// [`apply_style_scheme_with_manager`] using the default style-scheme manager.
pub fn apply_style_scheme(buffer: &sourceview4::Buffer) -> Option<String> {
    let manager = style_scheme_manager();
    apply_style_scheme_with_manager(&manager, buffer)
}
