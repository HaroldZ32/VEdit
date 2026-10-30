//! Open-document tracking: versions, tracked text, and restart re-sync data.

use std::collections::HashMap;
use std::sync::{Mutex, MutexGuard};

use anyhow::{bail, Result};
use lsp_types::{TextDocumentContentChangeEvent, Url};

use crate::edits::apply_content_changes;

/// A document this client has opened on the server.
#[derive(Debug, Clone)]
pub struct OpenDocument {
    /// Document URI.
    pub uri: Url,
    /// Language id sent in `didOpen`.
    pub language_id: String,
    /// Version of the tracked text.
    pub version: i32,
    /// Current text, needed to re-open the document after a server restart.
    pub text: String,
}

/// Tracks every open document. Version numbers are per document and start at 1.
#[derive(Debug, Default)]
pub(crate) struct DocumentStore {
    documents: Mutex<HashMap<Url, OpenDocument>>,
}

impl DocumentStore {
    pub(crate) fn open(&self, uri: Url, language_id: String, text: String) -> Result<OpenDocument> {
        let mut documents = self.lock()?;
        if documents.contains_key(&uri) {
            bail!("document is already open: {uri}");
        }
        let document = OpenDocument {
            uri: uri.clone(),
            language_id,
            version: 1,
            text,
        };
        documents.insert(uri, document.clone());
        Ok(document)
    }

    pub(crate) fn change(
        &self,
        uri: &Url,
        changes: &[TextDocumentContentChangeEvent],
    ) -> Result<OpenDocument> {
        let mut documents = self.lock()?;
        let Some(document) = documents.get_mut(uri) else {
            bail!("didChange for a document that is not open: {uri}");
        };
        document.text = apply_content_changes(&document.text, changes)?;
        document.version += 1;
        Ok(document.clone())
    }

    pub(crate) fn close(&self, uri: &Url) -> Result<Option<OpenDocument>> {
        Ok(self.lock()?.remove(uri))
    }

    pub(crate) fn get(&self, uri: &Url) -> Result<Option<OpenDocument>> {
        Ok(self.lock()?.get(uri).cloned())
    }

    /// Every open document, sorted by URI, for restart re-synchronisation.
    pub(crate) fn snapshot(&self) -> Result<Vec<OpenDocument>> {
        let documents = self.lock()?;
        let mut open: Vec<OpenDocument> = documents.values().cloned().collect();
        open.sort_by(|left, right| left.uri.as_str().cmp(right.uri.as_str()));
        Ok(open)
    }

    fn lock(&self) -> Result<MutexGuard<'_, HashMap<Url, OpenDocument>>> {
        self.documents
            .lock()
            .map_err(|_| anyhow::anyhow!("document store lock poisoned"))
    }
}

#[cfg(test)]
mod tests {
    use super::DocumentStore;
    use lsp_types::{Position, Range, TextDocumentContentChangeEvent, Url};

    #[test]
    fn versions_and_text_track_changes() -> Result<(), Box<dyn std::error::Error>> {
        let store = DocumentStore::default();
        let uri = Url::parse("file:///tmp/top.sv")?;
        let opened = store.open(uri.clone(), "systemverilog".to_string(), "abc".to_string())?;
        assert_eq!(opened.version, 1);
        let changed = store.change(
            &uri,
            &[TextDocumentContentChangeEvent {
                range: Some(Range {
                    start: Position {
                        line: 0,
                        character: 3,
                    },
                    end: Position {
                        line: 0,
                        character: 3,
                    },
                }),
                range_length: None,
                text: "d".to_string(),
            }],
        )?;
        assert_eq!(changed.version, 2);
        assert_eq!(changed.text, "abcd");
        assert_eq!(store.snapshot()?.len(), 1);
        assert!(store
            .open(uri.clone(), "systemverilog".to_string(), "x".to_string())
            .is_err());
        assert!(store.close(&uri)?.is_some());
        assert!(store.get(&uri)?.is_none());
        Ok(())
    }

    #[test]
    fn change_on_an_unopened_document_is_an_error() {
        let store = DocumentStore::default();
        let Ok(uri) = Url::parse("file:///tmp/missing.sv") else {
            return;
        };
        assert!(store.change(&uri, &[]).is_err());
    }
}
