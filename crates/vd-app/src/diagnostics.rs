//! The unified problem model for the panel and the gutter.
//!
//! Verible publishes LSP diagnostics (UTF-16 positions) and SLANG reports
//! `vd-deeplint` diagnostics (already converted byte → UTF-16 by that lane); both
//! are normalised here into one [`ViewDiagnostic`] so the panel and the gutter
//! have a single renderer. No lane logic is duplicated: the rule name comes from
//! `vd_lsp::diagnostic_rule`, positions are clamped through the position adapter.

use std::collections::BTreeMap;

use vd_editor::{Document, Range};

use crate::position_adapter;

/// Which lane produced a diagnostic (shown in the panel).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiagnosticSource {
    /// Verible's `publishDiagnostics` (online lint / LSP).
    Verible,
    /// SLANG deep lint (`vd-deeplint`).
    Slang,
}

impl DiagnosticSource {
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Verible => "verible",
            Self::Slang => "slang",
        }
    }
}

/// Severity with a stable label and a themed icon name for gutter marks.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Severity {
    Info,
    Note,
    Warning,
    Error,
}

impl Severity {
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Note => "note",
            Self::Info => "info",
        }
    }

    #[must_use]
    pub fn icon_name(self) -> &'static str {
        match self {
            Self::Error => "dialog-error",
            Self::Warning => "dialog-warning",
            Self::Note | Self::Info => "dialog-information",
        }
    }

    fn from_lsp(severity: Option<lsp_types::DiagnosticSeverity>) -> Self {
        match severity {
            Some(lsp_types::DiagnosticSeverity::ERROR) => Self::Error,
            Some(lsp_types::DiagnosticSeverity::WARNING) => Self::Warning,
            Some(lsp_types::DiagnosticSeverity::INFORMATION) => Self::Info,
            Some(lsp_types::DiagnosticSeverity::HINT) | None | Some(_) => Self::Note,
        }
    }

    fn from_slang(severity: &vd_deeplint::Severity) -> Self {
        match severity {
            vd_deeplint::Severity::Error => Self::Error,
            vd_deeplint::Severity::Warning => Self::Warning,
            vd_deeplint::Severity::Note => Self::Note,
            vd_deeplint::Severity::Ignored | vd_deeplint::Severity::Unknown(_) => Self::Info,
        }
    }
}

/// One diagnostic as the panel and gutter render it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ViewDiagnostic {
    pub source: DiagnosticSource,
    pub severity: Severity,
    pub message: String,
    pub code: Option<String>,
    /// UTF-16 editor range; `None` for a diagnostic with no position
    /// (`TooManyErrors`), which the panel still lists.
    pub range: Option<Range>,
}

impl ViewDiagnostic {
    /// Panel line: `error  [slang:UndeclaredIdentifier] message`.
    #[must_use]
    pub fn panel_text(&self) -> String {
        let code = self
            .code
            .as_deref()
            .map_or_else(String::new, |code| format!("[{code}] "));
        format!("{}  {}{}", self.severity.as_str(), code, self.message)
    }
}

/// The diagnostics of one tab, split by producer.
#[derive(Debug, Clone, Default)]
pub struct DiagnosticsStore {
    verible: Vec<ViewDiagnostic>,
    slang: Vec<ViewDiagnostic>,
}

impl DiagnosticsStore {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn set_verible(&mut self, diagnostics: Vec<ViewDiagnostic>) {
        self.verible = diagnostics;
    }

    pub fn set_slang(&mut self, diagnostics: Vec<ViewDiagnostic>) {
        self.slang = diagnostics;
    }

    pub fn all(&self) -> impl Iterator<Item = &ViewDiagnostic> {
        self.verible.iter().chain(self.slang.iter())
    }

    #[must_use]
    pub fn counts(&self) -> (usize, usize) {
        (self.verible.len(), self.slang.len())
    }

    /// The worst severity per line, for the gutter marks.
    #[must_use]
    pub fn worst_severity_per_line(&self) -> BTreeMap<u32, Severity> {
        let mut worst: BTreeMap<u32, Severity> = BTreeMap::new();
        for diagnostic in self.all() {
            let Some(range) = diagnostic.range else {
                continue;
            };
            for line in range.start.line..=range.end.line {
                worst
                    .entry(line)
                    .and_modify(|severity| *severity = (*severity).max(diagnostic.severity))
                    .or_insert(diagnostic.severity);
            }
        }
        worst
    }
}

/// Normalise Verible's LSP diagnostics into the view model.
///
/// The rule name comes from `vd_lsp::diagnostic_rule` (verible puts it in the
/// message, not in `code`); ranges are clamped to the current document.
#[must_use]
pub fn from_lsp(document: &Document, diagnostics: &[lsp_types::Diagnostic]) -> Vec<ViewDiagnostic> {
    diagnostics
        .iter()
        .map(|diagnostic| ViewDiagnostic {
            source: DiagnosticSource::Verible,
            severity: Severity::from_lsp(diagnostic.severity),
            message: diagnostic.message.clone(),
            code: vd_lsp::diagnostic_rule(diagnostic),
            range: Some(position_adapter::range_from_lsp(document, diagnostic.range)),
        })
        .collect()
}

/// Normalise `vd-deeplint` diagnostics into the view model.
#[must_use]
pub fn from_slang(report: &vd_deeplint::LintReport, document: &Document) -> Vec<ViewDiagnostic> {
    report
        .diagnostics
        .iter()
        .map(|diagnostic| {
            let range = diagnostic.editor_range.map(|range| {
                let start = position_adapter::clamp_to_document(
                    document,
                    vd_editor::Position::new(range.start.line, range.start.character),
                );
                let end = position_adapter::clamp_to_document(
                    document,
                    vd_editor::Position::new(range.end.line, range.end.character),
                );
                Range::new(start, end)
            });
            ViewDiagnostic {
                source: DiagnosticSource::Slang,
                severity: Severity::from_slang(&diagnostic.severity),
                message: diagnostic.message.clone(),
                code: Some(diagnostic.code.clone()),
                range,
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use vd_editor::Position;

    fn document() -> Document {
        Document::from_text("module m;\n  // 你好😀\nendmodule\n")
    }

    #[test]
    fn lsp_diagnostics_are_clamped_and_keep_the_derived_rule() {
        let document = document();
        let diagnostics = vec![lsp_types::Diagnostic {
            range: lsp_types::Range::new(
                lsp_types::Position::new(1, 5),
                lsp_types::Position::new(1, 999),
            ),
            severity: Some(lsp_types::DiagnosticSeverity::WARNING),
            code: None,
            code_description: None,
            source: Some("verible-verilog-ls".to_string()),
            message: "line too long [Style: line-length][line-length]".to_string(),
            related_information: None,
            tags: None,
            data: None,
        }];
        let view = from_lsp(&document, &diagnostics);
        assert_eq!(view.len(), 1);
        assert_eq!(view[0].severity, Severity::Warning);
        assert_eq!(view[0].code.as_deref(), Some("line-length"));
        assert_eq!(
            view[0].range.map(|range| range.end),
            Some(Position::new(1, 9))
        );
    }

    #[test]
    fn slang_diagnostics_keep_a_positionless_entry() {
        let document = document();
        let report = vd_deeplint::LintReport {
            diagnostics: vec![vd_deeplint::Diagnostic {
                severity: vd_deeplint::Severity::Error,
                message: "too many errors".to_string(),
                code: "TooManyErrors".to_string(),
                location: None,
                ranges: Vec::new(),
                symbol_path: None,
                raw: serde_json::json!({"code": "TooManyErrors"}),
                editor_range: None,
                conversion_note: Some("no location".to_string()),
                parse_note: None,
            }],
            total_diagnostics: 1,
            truncated: false,
            binary: vd_deeplint::BinaryResolution {
                chosen: None,
                attempts: Vec::new(),
            },
            failure: None,
            exit_code: Some(1),
            stdout: String::new(),
            stderr: String::new(),
            elapsed: std::time::Duration::ZERO,
            child_pid: None,
        };
        let view = from_slang(&report, &document);
        assert_eq!(view.len(), 1);
        assert_eq!(view[0].severity, Severity::Error);
        assert!(view[0].range.is_none());
        assert_eq!(
            view[0].panel_text(),
            "error  [TooManyErrors] too many errors"
        );
    }

    #[test]
    fn worst_severity_per_line_prefers_errors() {
        let mut store = DiagnosticsStore::new();
        store.set_verible(vec![ViewDiagnostic {
            source: DiagnosticSource::Verible,
            severity: Severity::Warning,
            message: "w".to_string(),
            code: None,
            range: Some(Range::new(Position::new(1, 0), Position::new(1, 2))),
        }]);
        store.set_slang(vec![ViewDiagnostic {
            source: DiagnosticSource::Slang,
            severity: Severity::Error,
            message: "e".to_string(),
            code: None,
            range: Some(Range::new(Position::new(1, 0), Position::new(1, 2))),
        }]);
        let worst = store.worst_severity_per_line();
        assert_eq!(worst.get(&1), Some(&Severity::Error));
        assert_eq!(store.counts(), (1, 1));
    }
}
