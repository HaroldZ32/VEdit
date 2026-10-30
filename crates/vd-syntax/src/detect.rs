//! Pure path → language mapping (no GTK dependency).
//!
//! This is the authoritative Verilog/SystemVerilog detection table for VEdit. It
//! is deliberately GTK-free so the mapping can be unit-tested in a headless
//! container without a display, and so the app wiring (T21) can query it without
//! building a `GtkSourceLanguageManager` first.
//!
//! `GtkSourceLanguageManager::guess_language` is *not* used for the decision:
//! the product's mapping must be explicit and assertable, per the T11 contract.

use std::path::Path;

/// A language VEdit has first-class support for.
///
/// The variants map 1:1 onto the upstream GtkSourceView language ids shipped by
/// the pinned `gtksourceview-4.6.1` (`verilog`, `systemverilog`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum LanguageId {
    /// IEEE 1364 Verilog (`.v`, `.vh`).
    Verilog,
    /// IEEE 1800 SystemVerilog (`.sv`, `.svh`, `.svi`).
    SystemVerilog,
}

impl LanguageId {
    /// GtkSourceView language id (`verilog` / `systemverilog`).
    #[must_use]
    pub const fn id(self) -> &'static str {
        match self {
            Self::Verilog => "verilog",
            Self::SystemVerilog => "systemverilog",
        }
    }

    /// Human-readable name, for the status bar and the "supported types" list.
    #[must_use]
    pub const fn display_name(self) -> &'static str {
        match self {
            Self::Verilog => "Verilog",
            Self::SystemVerilog => "SystemVerilog",
        }
    }

    /// Maps a file extension (with or without a leading dot) to a language.
    ///
    /// Case-insensitive: `SV`, `Sv` and `.sv` all resolve to
    /// [`LanguageId::SystemVerilog`]. Returns `None` for anything else, which the
    /// caller renders as plain text (no syntax highlighting, no error).
    #[must_use]
    pub fn from_extension(extension: &str) -> Option<Self> {
        let ext = extension.strip_prefix('.').unwrap_or(extension);
        SUPPORTED_EXTENSIONS
            .iter()
            .find(|(known, _)| known.eq_ignore_ascii_case(ext))
            .map(|(_, language)| *language)
    }
}

/// The complete extension → language table, as data.
///
/// Exposed so the verifier and the app wiring can assert the mapping directly
/// instead of probing individual paths.
pub const SUPPORTED_EXTENSIONS: &[(&str, LanguageId)] = &[
    ("v", LanguageId::Verilog),
    ("vh", LanguageId::Verilog),
    ("sv", LanguageId::SystemVerilog),
    ("svh", LanguageId::SystemVerilog),
    ("svi", LanguageId::SystemVerilog),
];

/// The extension → language table (see [`SUPPORTED_EXTENSIONS`]).
///
/// Order is the declared order of [`SUPPORTED_EXTENSIONS`] and is part of the
/// contract: Verilog extensions first (`v`, `vh`), then SystemVerilog (`sv`,
/// `svh`, `svi`). Callers that need alphabetical order sort it themselves; the
/// verifier asserts this exact order.
#[must_use]
pub fn supported_extensions() -> &'static [(&'static str, LanguageId)] {
    SUPPORTED_EXTENSIONS
}

/// Resolves the language for `path` from its extension.
///
/// Returns `None` for unknown or missing extensions: the editor then loads the
/// file as plain text — no syntax highlighting, no error, no crash.
#[must_use]
pub fn language_id_for_path(path: &Path) -> Option<LanguageId> {
    path.extension()
        .and_then(|extension| extension.to_str())
        .and_then(LanguageId::from_extension)
}

/// Whether `path` has first-class Verilog/SystemVerilog syntax support.
#[must_use]
pub fn is_supported(path: &Path) -> bool {
    language_id_for_path(path).is_some()
}

#[cfg(test)]
mod tests {
    use super::{language_id_for_path, supported_extensions, LanguageId, SUPPORTED_EXTENSIONS};
    use std::path::Path;

    #[test]
    fn verilog_extensions_map_to_verilog() {
        assert_eq!(
            language_id_for_path(Path::new("counter.v")),
            Some(LanguageId::Verilog)
        );
        assert_eq!(
            language_id_for_path(Path::new("/tmp/proj/defs.vh")),
            Some(LanguageId::Verilog)
        );
    }

    #[test]
    fn systemverilog_extensions_map_to_systemverilog() {
        for name in ["top.sv", "pkg.svh", "iface.svi", "/a/b/c/dut.SV"] {
            assert_eq!(
                language_id_for_path(Path::new(name)),
                Some(LanguageId::SystemVerilog),
                "{name}"
            );
        }
    }

    #[test]
    fn extension_matching_is_case_insensitive() {
        assert_eq!(
            language_id_for_path(Path::new("TOP.SV")),
            Some(LanguageId::SystemVerilog)
        );
        assert_eq!(
            language_id_for_path(Path::new("DEFS.VH")),
            Some(LanguageId::Verilog)
        );
        assert_eq!(
            LanguageId::from_extension(".svi"),
            Some(LanguageId::SystemVerilog)
        );
    }

    #[test]
    fn unknown_extensions_are_plain_text() {
        for name in ["notes.txt", "design.vhd", "noext", "file.", "a.sv.bak"] {
            assert_eq!(language_id_for_path(Path::new(name)), None, "{name}");
        }
        assert!(!super::is_supported(Path::new("design.vhd")));
        assert!(super::is_supported(Path::new("design.sv")));
    }

    #[test]
    fn language_ids_are_the_upstream_gtksourceview_ids() {
        assert_eq!(LanguageId::Verilog.id(), "verilog");
        assert_eq!(LanguageId::SystemVerilog.id(), "systemverilog");
        assert_eq!(LanguageId::Verilog.display_name(), "Verilog");
        assert_eq!(LanguageId::SystemVerilog.display_name(), "SystemVerilog");
    }

    #[test]
    fn supported_table_is_exactly_the_contract_table() {
        // Declared order, not alphabetical: verilog extensions first.
        assert_eq!(
            supported_extensions(),
            [
                ("v", LanguageId::Verilog),
                ("vh", LanguageId::Verilog),
                ("sv", LanguageId::SystemVerilog),
                ("svh", LanguageId::SystemVerilog),
                ("svi", LanguageId::SystemVerilog),
            ]
        );
        assert_eq!(SUPPORTED_EXTENSIONS.len(), 5);
        // Every table row resolves through the path-based entry point.
        for (extension, language) in SUPPORTED_EXTENSIONS {
            let path = format!("probe.{extension}");
            assert_eq!(language_id_for_path(Path::new(&path)), Some(*language));
        }
    }
}
