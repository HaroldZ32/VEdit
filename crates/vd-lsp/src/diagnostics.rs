//! Diagnostics received from the language server, exposed as a watch snapshot.
//!
//! Rendering belongs to the application layer (T21); this crate only records
//! what arrived, when, and for which document version.

use std::time::Instant;

use lsp_types::{Diagnostic, PublishDiagnosticsParams, Url};
use tokio::sync::watch;

/// The last diagnostics published for one document.
#[derive(Debug, Clone)]
pub struct PublishedDiagnostics {
    /// Document the diagnostics belong to.
    pub uri: Url,
    /// Document version announced by the server, when it sent one.
    pub version: Option<i32>,
    /// The diagnostics, in server order.
    pub diagnostics: Vec<Diagnostic>,
    /// When this crate recorded the notification.
    pub received_at: Instant,
}

/// An immutable view of all diagnostics recorded so far.
#[derive(Debug, Clone, Default)]
pub struct DiagnosticsSnapshot {
    generation: u64,
    entries: Vec<PublishedDiagnostics>,
}

impl DiagnosticsSnapshot {
    /// Monotonic counter, bumped on every received notification.
    pub fn generation(&self) -> u64 {
        self.generation
    }

    /// All documents with diagnostics, in first-publish order.
    pub fn entries(&self) -> &[PublishedDiagnostics] {
        &self.entries
    }

    /// The diagnostics for one document.
    pub fn for_uri(&self, uri: &Url) -> Option<&PublishedDiagnostics> {
        self.entries.iter().find(|entry| &entry.uri == uri)
    }

    /// Total number of diagnostics across every document.
    pub fn diagnostic_count(&self) -> usize {
        self.entries
            .iter()
            .map(|entry| entry.diagnostics.len())
            .sum()
    }
}

/// The lint rule a diagnostic belongs to.
///
/// Verible reports rules inside the message (`… [Style: line-length][line-length]`)
/// rather than in `code`, so this prefers the protocol `code` and otherwise
/// parses the trailing bracketed tokens. The result is suitable for a problem
/// panel's "rule" column.
pub fn diagnostic_rule(diagnostic: &Diagnostic) -> Option<String> {
    match &diagnostic.code {
        Some(lsp_types::NumberOrString::String(code)) if !code.is_empty() => {
            return Some(code.clone());
        }
        Some(lsp_types::NumberOrString::Number(code)) => return Some(code.to_string()),
        _ => {}
    }
    let mut tokens: Vec<&str> = Vec::new();
    let mut rest = diagnostic.message.as_str();
    while let Some(start) = rest.find('[') {
        let Some(end) = rest[start..].find(']') else {
            break;
        };
        tokens.push(&rest[start + 1..start + end]);
        rest = &rest[start + end + 1..];
    }
    tokens.iter().rev().find_map(|token| {
        let trimmed = token.trim();
        let name = trimmed.strip_prefix("Style: ").unwrap_or(trimmed);
        (!name.is_empty() && !name.contains(' ')).then(|| name.to_string())
    })
}

/// Receives `publishDiagnostics` notifications and publishes snapshots.
#[derive(Debug, Clone)]
pub(crate) struct DiagnosticsStore {
    tx: watch::Sender<DiagnosticsSnapshot>,
}

impl DiagnosticsStore {
    /// Create the store and the receiver handed to consumers.
    pub(crate) fn new() -> (Self, watch::Receiver<DiagnosticsSnapshot>) {
        let (tx, rx) = watch::channel(DiagnosticsSnapshot::default());
        (Self { tx }, rx)
    }

    /// Record one notification, replacing the entry for its document.
    pub(crate) fn record(&self, params: PublishDiagnosticsParams) {
        self.tx.send_modify(|snapshot| {
            snapshot.generation += 1;
            snapshot.entries.retain(|entry| entry.uri != params.uri);
            snapshot.entries.push(PublishedDiagnostics {
                uri: params.uri,
                version: params.version,
                diagnostics: params.diagnostics,
                received_at: Instant::now(),
            });
        });
    }
}

#[cfg(test)]
mod tests {
    use super::DiagnosticsStore;
    use lsp_types::{
        Diagnostic, DiagnosticSeverity, Position, PublishDiagnosticsParams, Range, Url,
    };

    fn diagnostic(line: u32, message: &str) -> Diagnostic {
        Diagnostic {
            range: Range {
                start: Position { line, character: 0 },
                end: Position { line, character: 1 },
            },
            severity: Some(DiagnosticSeverity::WARNING),
            code: Some(lsp_types::NumberOrString::String("line-length".to_string())),
            code_description: None,
            source: Some("verible".to_string()),
            message: message.to_string(),
            related_information: None,
            tags: None,
            data: None,
        }
    }

    #[test]
    fn rule_is_taken_from_code_or_from_the_verible_message(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let with_code = Diagnostic {
            code: Some(lsp_types::NumberOrString::String(
                "no-trailing-spaces".to_string(),
            )),
            ..diagnostic(0, "trailing space")
        };
        assert_eq!(
            super::diagnostic_rule(&with_code).as_deref(),
            Some("no-trailing-spaces")
        );
        let verible = diagnostic(
            2,
            "Line length exceeds max: 100; is: 108 [Style: line-length][line-length]",
        );
        assert_eq!(
            super::diagnostic_rule(&verible).as_deref(),
            Some("line-length")
        );
        let plain = Diagnostic {
            code: None,
            ..diagnostic(0, "something without brackets")
        };
        assert!(super::diagnostic_rule(&plain).is_none());
        Ok(())
    }

    #[test]
    fn recording_bumps_the_generation_and_replaces_per_uri(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let (store, rx) = DiagnosticsStore::new();
        let uri = Url::parse("file:///tmp/top.sv")?;
        store.record(PublishDiagnosticsParams {
            uri: uri.clone(),
            version: Some(1),
            diagnostics: vec![diagnostic(0, "first")],
        });
        assert_eq!(rx.borrow().generation(), 1);
        assert_eq!(rx.borrow().diagnostic_count(), 1);
        store.record(PublishDiagnosticsParams {
            uri: uri.clone(),
            version: Some(2),
            diagnostics: vec![diagnostic(0, "a"), diagnostic(1, "b")],
        });
        let snapshot = rx.borrow().clone();
        assert_eq!(snapshot.generation(), 2);
        assert_eq!(snapshot.entries().len(), 1, "one entry per document");
        let entry = snapshot
            .for_uri(&uri)
            .ok_or("missing entry for the document")?;
        assert_eq!(entry.version, Some(2));
        assert_eq!(entry.diagnostics.len(), 2);
        assert_eq!(entry.diagnostics[1].message, "b");
        Ok(())
    }
}
