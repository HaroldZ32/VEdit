//! The cross-engine diagnostic identity (T138, AC-2.13).
//!
//! The same problem can arrive twice — once from the language server for an open
//! file ([`DiagnosticSource::Lsp`]) and once from a project-wide CLI pass
//! ([`DiagnosticSource::Cli`]). The display layer shows one row per problem and
//! marks where it came from, so both paths must derive **the same key** for the
//! same problem:
//!
//! ```text
//! key = <absolute file path> 0x1F <0-based start line> 0x1F <severity class> 0x1F <normalized message>
//! ```
//!
//! * the path is the file the problem is in, as the caller named it;
//! * the line is the **0-based editor line** of the range start, so the two
//!   engines (which report 1-based lines) agree after conversion;
//! * the severity class is the shared error/warning distinction, because LSP
//!   codes diagnostics `1..=4` and this crate codes them `0..=4`;
//! * the message is trimmed and every run of whitespace is folded to one space,
//!   because the two tools format the same sentence differently.
//!
//! The unit separator (`0x1F`) cannot appear in a path, a message or a number, so
//! two different tuples can never produce the same key.

use crate::diagnostic::Severity;

/// Where a displayed diagnostic came from (AC-2.13's source column).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiagnosticSource {
    /// Not known (a synthetic row, a test fixture).
    Unknown,
    /// The language server's push diagnostics for an open document.
    Lsp,
    /// A CLI lint run (`slang` or `verible-verilog-lint`).
    Cli,
    /// Two rows with the same key were merged (`LSP` and `CLI`).
    Both,
}

impl DiagnosticSource {
    /// Stable code for the C ABI: 0 unknown, 1 LSP, 2 CLI, 3 both.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Unknown => 0,
            Self::Lsp => 1,
            Self::Cli => 2,
            Self::Both => 3,
        }
    }

    /// Parse a C ABI source code.
    #[must_use]
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Unknown),
            1 => Some(Self::Lsp),
            2 => Some(Self::Cli),
            3 => Some(Self::Both),
            _ => None,
        }
    }

    /// The label the source column shows.
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Unknown => "",
            Self::Lsp => "LSP",
            Self::Cli => "CLI",
            Self::Both => "LSP+CLI",
        }
    }

    /// Merge two sources of one problem: any pair of distinct LSP/CLI rows
    /// becomes [`Self::Both`].
    #[must_use]
    pub const fn merge(self, other: Self) -> Self {
        match (self, other) {
            (Self::Both, _) | (_, Self::Both) => Self::Both,
            (Self::Lsp, Self::Cli) | (Self::Cli, Self::Lsp) => Self::Both,
            (Self::Unknown, other) => other,
            (this, Self::Unknown) => this,
            (this, _) => this,
        }
    }
}

/// The shared error/warning distinction the dedup key uses.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum SeverityClass {
    /// An error (LSP `1`, this crate's `Severity::Error`).
    Error,
    /// A warning (LSP `2`, this crate's `Severity::Warning`).
    Warning,
    /// Information / hint / note (LSP `3`/`4`, this crate's note/ignored).
    Info,
    /// A severity this build does not classify.
    Unknown,
}

impl SeverityClass {
    /// Stable code for the C ABI and the key.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Error => 0,
            Self::Warning => 1,
            Self::Info => 2,
            Self::Unknown => 3,
        }
    }

    /// The label a severity column shows.
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Info => "info",
            Self::Unknown => "unknown",
        }
    }
}

/// Classify this crate's [`Severity`].
#[must_use]
pub const fn class_of_deeplint(severity: &Severity) -> SeverityClass {
    match severity {
        Severity::Error => SeverityClass::Error,
        Severity::Warning => SeverityClass::Warning,
        Severity::Note => SeverityClass::Info,
        Severity::Ignored => SeverityClass::Info,
        Severity::Unknown(_) => SeverityClass::Unknown,
    }
}

/// Classify an LSP `DiagnosticSeverity` code (1 error, 2 warning, 3 info,
/// 4 hint; anything else — including `None` encoded as `0` — is unknown).
#[must_use]
pub const fn class_of_lsp(code: i32) -> SeverityClass {
    match code {
        1 => SeverityClass::Error,
        2 => SeverityClass::Warning,
        3 | 4 => SeverityClass::Info,
        _ => SeverityClass::Unknown,
    }
}

/// Classify a CLI severity code as this crate's C ABI encodes it (0 error,
/// 1 warning, 2 note, 3 ignored, 4 unknown).
#[must_use]
pub const fn class_of_cli_code(code: i32) -> SeverityClass {
    match code {
        0 => SeverityClass::Error,
        1 => SeverityClass::Warning,
        2 | 3 => SeverityClass::Info,
        _ => SeverityClass::Unknown,
    }
}

/// Trim the message and fold every run of whitespace into one space.
#[must_use]
pub fn normalize_message(text: &str) -> String {
    let mut normalized = String::with_capacity(text.len());
    let mut pending_space = false;
    for character in text.trim().chars() {
        if character.is_whitespace() {
            pending_space = !normalized.is_empty();
            continue;
        }
        if pending_space {
            normalized.push(' ');
            pending_space = false;
        }
        normalized.push(character);
    }
    normalized
}

/// The dedup key: path, 0-based start line, severity class, normalised message.
#[must_use]
pub fn diagnostic_key(path: &str, line: u32, class: SeverityClass, message: &str) -> String {
    let separator = '\u{1f}';
    format!(
        "{path}{separator}{line}{separator}{}{separator}{}",
        class.code(),
        normalize_message(message)
    )
}

#[cfg(test)]
mod tests {
    use super::{
        class_of_cli_code, class_of_deeplint, class_of_lsp, diagnostic_key, normalize_message,
        DiagnosticSource, SeverityClass,
    };
    use crate::Severity;

    #[test]
    fn whitespace_is_trimmed_and_folded() {
        assert_eq!(normalize_message("  a \t b\n\n c  "), "a b c");
        assert_eq!(normalize_message(""), "");
        assert_eq!(normalize_message("   "), "");
    }

    #[test]
    fn the_same_problem_gets_the_same_key_from_both_paths() {
        let from_lsp = diagnostic_key(
            "/p/top.sv",
            7,
            class_of_lsp(1),
            "  Undeclared identifier   'x'",
        );
        let from_cli = diagnostic_key(
            "/p/top.sv",
            7,
            class_of_deeplint(&Severity::Error),
            "Undeclared identifier 'x' ",
        );
        assert_eq!(from_lsp, from_cli);
    }

    #[test]
    fn different_line_severity_or_message_changes_the_key() {
        let base = diagnostic_key("/p/a.sv", 1, SeverityClass::Error, "boom");
        assert_ne!(
            base,
            diagnostic_key("/p/a.sv", 2, SeverityClass::Error, "boom")
        );
        assert_ne!(
            base,
            diagnostic_key("/p/a.sv", 1, SeverityClass::Warning, "boom")
        );
        assert_ne!(
            base,
            diagnostic_key("/p/a.sv", 1, SeverityClass::Error, "boom!")
        );
        assert_ne!(
            base,
            diagnostic_key("/p/b.sv", 1, SeverityClass::Error, "boom")
        );
    }

    #[test]
    fn severity_codes_map_onto_the_shared_class() {
        assert_eq!(class_of_lsp(1), SeverityClass::Error);
        assert_eq!(class_of_lsp(2), SeverityClass::Warning);
        assert_eq!(class_of_lsp(3), SeverityClass::Info);
        assert_eq!(class_of_lsp(4), SeverityClass::Info);
        assert_eq!(class_of_lsp(0), SeverityClass::Unknown);
        assert_eq!(class_of_lsp(99), SeverityClass::Unknown);
        assert_eq!(class_of_deeplint(&Severity::Error), SeverityClass::Error);
        assert_eq!(
            class_of_deeplint(&Severity::Warning),
            SeverityClass::Warning
        );
        assert_eq!(class_of_deeplint(&Severity::Note), SeverityClass::Info);
        assert_eq!(
            class_of_deeplint(&Severity::Unknown("custom".to_string())),
            SeverityClass::Unknown
        );
        assert_eq!(class_of_cli_code(0), SeverityClass::Error);
        assert_eq!(class_of_cli_code(1), SeverityClass::Warning);
        assert_eq!(class_of_cli_code(2), SeverityClass::Info);
        assert_eq!(class_of_cli_code(4), SeverityClass::Unknown);
    }

    #[test]
    fn sources_round_trip_and_merge() {
        for source in [
            DiagnosticSource::Unknown,
            DiagnosticSource::Lsp,
            DiagnosticSource::Cli,
            DiagnosticSource::Both,
        ] {
            assert_eq!(DiagnosticSource::from_code(source.code()), Some(source));
        }
        assert_eq!(
            DiagnosticSource::Lsp.merge(DiagnosticSource::Cli),
            DiagnosticSource::Both
        );
        assert_eq!(
            DiagnosticSource::Cli.merge(DiagnosticSource::Cli),
            DiagnosticSource::Cli
        );
        assert_eq!(
            DiagnosticSource::Unknown
                .merge(DiagnosticSource::Lsp)
                .label(),
            "LSP"
        );
    }
}
