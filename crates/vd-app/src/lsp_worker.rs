//! Background Verible LSP session.
//!
//! `vd-lsp` owns the protocol, lifecycle and restarts. This module runs one
//! client on a tokio runtime thread, turns its diagnostics watch into events and
//! forwards the UI's requests (open/change/format/definition/references/symbols/
//! rename preview) to it. No protocol logic lives here.

use std::path::PathBuf;
use std::sync::mpsc::{Receiver, Sender};
use std::thread;

use lsp_types::{DocumentSymbolResponse, Location, Url, WorkspaceEdit};
use tokio::sync::mpsc as tokio_mpsc;

use vd_lsp::{LspClient, LspConfig};

use crate::config_glue::Startup;

/// Events from the LSP worker to the UI thread.
#[derive(Debug)]
pub enum LspEvent {
    Status {
        message: String,
        running: bool,
        restarts: u32,
    },
    Diagnostics {
        uri: String,
        diagnostics: Vec<lsp_types::Diagnostic>,
        generation: u64,
    },
    Opened {
        ok: bool,
        error: Option<String>,
    },
    Formatted {
        ok: bool,
        edits: Vec<lsp_types::TextEdit>,
        error: Option<String>,
    },
    Definitions {
        locations: Vec<Location>,
        error: Option<String>,
    },
    References {
        locations: Vec<Location>,
        error: Option<String>,
    },
    Symbols {
        symbols: Vec<(String, Option<lsp_types::Range>)>,
        error: Option<String>,
    },
    Renamed {
        preview: String,
        error: Option<String>,
    },
}

enum LspCommand {
    Open {
        uri: Url,
        language_id: String,
        text: String,
    },
    Format {
        uri: Url,
        tab_size: u32,
        insert_spaces: bool,
    },
    Definition {
        uri: Url,
        position: lsp_types::Position,
    },
    References {
        uri: Url,
        position: lsp_types::Position,
    },
    Symbols {
        uri: Url,
    },
    Rename {
        uri: Url,
        position: lsp_types::Position,
        new_name: String,
    },
    Shutdown,
}

/// Handle used by the UI thread.
pub struct LspBridge {
    tx: tokio_mpsc::UnboundedSender<LspCommand>,
    pub events: Receiver<LspEvent>,
}

impl LspBridge {
    /// Start the worker. Returns `None` when no Verible language server path was
    /// resolved (the caller shows the tool attempts instead).
    #[must_use]
    pub fn start(startup: &Startup) -> Option<Self> {
        let ls_path = startup.tools.ls.clone()?;
        let workspace_root = startup.workspace_root.clone();
        let rules_path = startup.rules_path.clone();
        let (command_tx, command_rx) = tokio_mpsc::unbounded_channel();
        let (event_tx, events) = std::sync::mpsc::channel();
        thread::Builder::new()
            .name("vedit-lsp".to_string())
            .spawn(move || run_worker(ls_path, workspace_root, rules_path, command_rx, event_tx))
            .ok()?;
        Some(Self {
            tx: command_tx,
            events,
        })
    }

    pub fn open(&self, uri: Url, language_id: String, text: String) {
        let _ = self.tx.send(LspCommand::Open {
            uri,
            language_id,
            text,
        });
    }

    pub fn format(&self, uri: Url, tab_size: u32, insert_spaces: bool) {
        let _ = self.tx.send(LspCommand::Format {
            uri,
            tab_size,
            insert_spaces,
        });
    }

    pub fn definition(&self, uri: Url, position: lsp_types::Position) {
        let _ = self.tx.send(LspCommand::Definition { uri, position });
    }

    pub fn references(&self, uri: Url, position: lsp_types::Position) {
        let _ = self.tx.send(LspCommand::References { uri, position });
    }

    pub fn symbols(&self, uri: Url) {
        let _ = self.tx.send(LspCommand::Symbols { uri });
    }

    pub fn rename(&self, uri: Url, position: lsp_types::Position, new_name: String) {
        let _ = self.tx.send(LspCommand::Rename {
            uri,
            position,
            new_name,
        });
    }

    pub fn shutdown(&self) {
        let _ = self.tx.send(LspCommand::Shutdown);
    }
}

fn run_worker(
    ls_path: PathBuf,
    workspace_root: PathBuf,
    rules_path: Option<PathBuf>,
    mut command_rx: tokio_mpsc::UnboundedReceiver<LspCommand>,
    events: Sender<LspEvent>,
) {
    let runtime = match tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
    {
        Ok(runtime) => runtime,
        Err(error) => {
            let _ = events.send(LspEvent::Status {
                message: format!("LSP runtime could not start: {error}"),
                running: false,
                restarts: 0,
            });
            return;
        }
    };
    runtime.block_on(async move {
        let mut config = LspConfig::new(ls_path, workspace_root);
        if let Some(rules) = rules_path {
            config = config.with_rules_config(rules);
        }
        let mut client = LspClient::new(config);
        let mut watch = client.diagnostics();
        let (diagnostics_tx, mut diagnostics_rx) = tokio_mpsc::unbounded_channel();
        tokio::spawn(async move {
            loop {
                if watch.changed().await.is_err() {
                    break;
                }
                let snapshot = watch.borrow().clone();
                if diagnostics_tx.send(snapshot).is_err() {
                    break;
                }
            }
        });

        loop {
            tokio::select! {
                command = command_rx.recv() => {
                    let Some(command) = command else { break; };
                    match command {
                        LspCommand::Open { uri, language_id, text } => {
                            match client.open_document(uri, language_id, text).await {
                                Ok(()) => {
                                    let _ = events.send(LspEvent::Opened { ok: true, error: None });
                                    let _ = events.send(LspEvent::Status {
                                        message: format!(
                                            "LSP running (pid {}), restarts {}",
                                            client.pid().map_or_else(|| "?".to_string(), |pid| pid.to_string()),
                                            client.restart_count()
                                        ),
                                        running: client.is_running(),
                                        restarts: client.restart_count(),
                                    });
                                }
                                Err(error) => {
                                    let _ = events.send(LspEvent::Opened { ok: false, error: Some(format!("{error:#}")) });
                                }
                            }
                        }
                        LspCommand::Format { uri, tab_size, insert_spaces } => {
                            match client.formatting(&uri, tab_size, insert_spaces).await {
                                Ok(edits) => { let _ = events.send(LspEvent::Formatted { ok: true, edits, error: None }); }
                                Err(error) => { let _ = events.send(LspEvent::Formatted { ok: false, edits: Vec::new(), error: Some(format!("{error:#}")) }); }
                            }
                        }
                        LspCommand::Definition { uri, position } => {
                            match client.definition(&uri, position).await {
                                Ok(locations) => { let _ = events.send(LspEvent::Definitions { locations, error: None }); }
                                Err(error) => { let _ = events.send(LspEvent::Definitions { locations: Vec::new(), error: Some(format!("{error:#}")) }); }
                            }
                        }
                        LspCommand::References { uri, position } => {
                            match client.references(&uri, position, true).await {
                                Ok(locations) => { let _ = events.send(LspEvent::References { locations, error: None }); }
                                Err(error) => { let _ = events.send(LspEvent::References { locations: Vec::new(), error: Some(format!("{error:#}")) }); }
                            }
                        }
                        LspCommand::Symbols { uri } => {
                            match client.document_symbols(&uri).await {
                                Ok(response) => {
                                    let symbols = symbols_of(response);
                                    let _ = events.send(LspEvent::Symbols { symbols, error: None });
                                }
                                Err(error) => { let _ = events.send(LspEvent::Symbols { symbols: Vec::new(), error: Some(format!("{error:#}")) }); }
                            }
                        }
                        LspCommand::Rename { uri, position, new_name } => {
                            match client.rename(&uri, position, new_name).await {
                                Ok(edit) => { let _ = events.send(LspEvent::Renamed { preview: rename_preview(edit.as_ref()), error: None }); }
                                Err(error) => { let _ = events.send(LspEvent::Renamed { preview: String::new(), error: Some(format!("{error:#}")) }); }
                            }
                        }
                        LspCommand::Shutdown => {
                            let _ = client.shutdown().await;
                            let _ = events.send(LspEvent::Status { message: "LSP stopped".to_string(), running: false, restarts: client.restart_count() });
                            break;
                        }
                    }
                }
                Some(snapshot) = diagnostics_rx.recv() => {
                    let generation = snapshot.generation();
                    for entry in snapshot.entries() {
                        let _ = events.send(LspEvent::Diagnostics {
                            uri: entry.uri.to_string(),
                            diagnostics: entry.diagnostics.clone(),
                            generation,
                        });
                    }
                }
            }
        }
    });
}

fn symbols_of(response: Option<DocumentSymbolResponse>) -> Vec<(String, Option<lsp_types::Range>)> {
    match response {
        Some(DocumentSymbolResponse::Flat(symbols)) => symbols
            .into_iter()
            .map(|symbol| (symbol.name, Some(symbol.location.range)))
            .collect(),
        Some(DocumentSymbolResponse::Nested(symbols)) => symbols
            .into_iter()
            .map(|symbol| (symbol.name, Some(symbol.selection_range)))
            .collect(),
        None => Vec::new(),
    }
}

fn rename_preview(edit: Option<&WorkspaceEdit>) -> String {
    let Some(edit) = edit else {
        return "no rename edits were returned".to_string();
    };
    let mut lines = Vec::new();
    let mut files = 0usize;
    let mut edits = 0usize;
    if let Some(changes) = &edit.changes {
        for (uri, text_edits) in changes {
            files += 1;
            edits += text_edits.len();
            lines.push(format!("{}: {} edit(s)", uri, text_edits.len()));
            for text_edit in text_edits.iter().take(3) {
                lines.push(format!(
                    "  line {}:{} → {:?}",
                    text_edit.range.start.line + 1,
                    text_edit.range.start.character + 1,
                    text_edit.new_text
                ));
            }
        }
    }
    match &edit.document_changes {
        Some(lsp_types::DocumentChanges::Edits(edits)) => {
            lines.push(format!("documentChanges: {} file edit(s)", edits.len()));
        }
        Some(lsp_types::DocumentChanges::Operations(operations)) => {
            lines.push(format!(
                "documentChanges: {} operation(s)",
                operations.len()
            ));
        }
        None => {}
    }
    lines.insert(0, format!("{files} file(s), {edits} edit(s)"));
    lines.join("\n")
}
