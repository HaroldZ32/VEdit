//! The language registry: what this build can analyse, expressed POSITIVELY.
//!
//! One table answers, for a path, **which language it is** (by extension) and
//! **which capabilities this build provides for that language**. Every consumer
//! that decides whether a file is a project source, enters the lint pass, feeds
//! the module catalog or is handed to an LSP session derives its answer from
//! here — never from an extension list of its own.
//!
//! A filelist (`.f`) is a project MANIFEST, not a language: it is READ as
//! configuration input (its `+incdir+`, `+define+` and nested `-f` honoured) and
//! has no entry here. So no consumer ever asks "should I skip it?" — it is
//! simply not a candidate, and a manifest can never be parsed as HDL, linted, or
//! catalogued as a source.
//!
//! Adding a language is DATA: one [`LanguageSpec`] row with its extensions and
//! the capabilities landed for it. The consumers do not change. The C++
//! consumers mirror this table in `src/workspace/project/language_registry.h`
//! (the ABI does not publish this table yet; both sides pin the same extension
//! contract in their own tests until it does).

use std::path::Path;

use serde::Serialize;

/// What a consumer can ask about a file.
///
/// The names are the consumers that exist in this build:
/// * [`Capability::Source`] — membership of the project file set (the project
///   scan and the filelist expansion);
/// * [`Capability::Diagnostics`] — the file-scope LSP analysis of an open
///   document;
/// * [`Capability::Lint`] — the CLI lint pass;
/// * [`Capability::Catalog`] — the module-catalog input.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Capability {
    /// The file is a source of the project (scan / filelist expansion).
    Source,
    /// A language server can produce diagnostics for it.
    Diagnostics,
    /// A CLI lint engine can lint it.
    Lint,
    /// The module catalog can elaborate it.
    Catalog,
}

/// One language this build knows, and what it can do with it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct LanguageSpec {
    /// Stable id (`systemverilog`); what an analyzer is registered for.
    pub id: &'static str,
    /// User-facing name for a status line ("no analyzer for ...").
    pub display_name: &'static str,
    /// Extensions (without the dot), matched case-insensitively.
    pub extensions: &'static [&'static str],
    /// What this build can do with the language. A capability nobody landed is
    /// simply absent; the consumer then reports "no analyzer for this file
    /// type" instead of handing the file to a tool that cannot read it.
    pub capabilities: &'static [Capability],
}

/// The languages this build ships with.
pub const LANGUAGES: &[LanguageSpec] = &[LanguageSpec {
    id: "systemverilog",
    display_name: "Verilog / SystemVerilog",
    extensions: &["sv", "svh", "v", "vh"],
    capabilities: &[
        Capability::Source,
        Capability::Diagnostics,
        Capability::Lint,
        Capability::Catalog,
    ],
}];

/// The language of `path`, by extension, case-insensitively.
#[must_use]
pub fn language_for_path(path: &Path) -> Option<&'static LanguageSpec> {
    // ADDENDUM A-17: a filelist-expanded path may carry a LITERAL trailing `$`
    // (`out_tail.sv$`). That character belongs to the file NAME, not to the
    // extension, so the lookup ignores a trailing run of `$` - it is not a
    // special case for Verilog, it is how a literal dollar is spelled.
    let extension = path.extension().and_then(std::ffi::OsStr::to_str)?;
    let extension = extension.trim_end_matches('$');
    LANGUAGES.iter().find(|spec| {
        spec.extensions
            .iter()
            .any(|known| extension.eq_ignore_ascii_case(known))
    })
}

/// Does this build provide `capability` for `path`?
#[must_use]
pub fn supports(path: &Path, capability: Capability) -> bool {
    language_for_path(path).is_some_and(|spec| spec.capabilities.contains(&capability))
}

/// Every extension of a registered language that has `capability` (the scan's
/// projection of the table; never a hand-written list).
#[must_use]
pub fn extensions_with(capability: Capability) -> Vec<&'static str> {
    let mut extensions: Vec<&'static str> = LANGUAGES
        .iter()
        .filter(|spec| spec.capabilities.contains(&capability))
        .flat_map(|spec| spec.extensions.iter().copied())
        .collect();
    extensions.sort_unstable();
    extensions.dedup();
    extensions
}

#[cfg(test)]
mod tests {
    use super::{extensions_with, language_for_path, supports, Capability, LANGUAGES};
    use std::path::Path;

    #[test]
    fn the_builtin_set_is_the_documented_hdl_set() {
        assert_eq!(
            extensions_with(Capability::Source),
            vec!["sv", "svh", "v", "vh"],
            "the HDL source contract; both crates' consumers must agree"
        );
        assert_eq!(LANGUAGES.len(), 1);
        assert_eq!(LANGUAGES[0].id, "systemverilog");
    }

    #[test]
    fn matching_is_case_insensitive_and_needs_a_registered_extension() {
        assert!(supports(Path::new("/p/rtl/top.SV"), Capability::Source));
        assert!(supports(
            Path::new("/p/rtl/top.svh"),
            Capability::Diagnostics
        ));
        assert!(supports(Path::new("/p/rtl/top.vh"), Capability::Lint));
        assert!(!supports(Path::new("/p/rtl/top.vhdl"), Capability::Source));
    }

    /// ADDENDUM A-17: a literal trailing `$` in an expanded filelist path is
    /// part of the name; `out_tail.sv$` is still Verilog, `notes.md$` is still
    /// not a source.
    #[test]
    fn a_literal_trailing_dollar_does_not_change_the_language() {
        assert!(supports(Path::new("/p/out_tail.sv$"), Capability::Source));
        assert!(supports(Path::new("/p/out_dollar$.sv"), Capability::Source));
        assert!(!supports(Path::new("/p/notes.md$"), Capability::Source));
        assert!(!supports(Path::new("/p/run.tcl$"), Capability::Source));
    }

    /// A manifest is not a language and never a source: this is what makes the
    /// `.f`-parsed-as-HDL defect unrepresentable rather than "filtered out".
    #[test]
    fn a_manifest_and_non_source_files_have_no_language() {
        for path in [
            "/p/top.f",
            "/p/sources.lst",
            "/p/constraints.sdc",
            "/p/pins.xdc",
            "/p/run.tcl",
            "/p/README.md",
            "/p/notes.txt",
            "/p/config.json",
            "/p/vedit.toml",
            "/p/ci.yml",
            "/p/Makefile",
        ] {
            assert!(
                language_for_path(Path::new(path)).is_none(),
                "{path} must not be a registered language"
            );
            for capability in [
                Capability::Source,
                Capability::Diagnostics,
                Capability::Lint,
                Capability::Catalog,
            ] {
                assert!(
                    !supports(Path::new(path), capability),
                    "{path} {capability:?}"
                );
            }
        }
    }
}
