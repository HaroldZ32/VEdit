//! The Verible language-server client: process lifecycle, LSP requests and
//! diagnostics plumbing.
//!
//! Everything is async and free of UI types, so the whole lane is testable in a
//! headless container. JSON-RPC framing is provided by `async-lsp`; this module
//! never serializes a frame by hand.

use std::collections::VecDeque;
use std::ops::ControlFlow;
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{anyhow, bail, Context, Result};
use async_lsp::router::Router;
use async_lsp::{MainLoop, ServerSocket};
use lsp_types::notification::{
    DidChangeTextDocument, DidCloseTextDocument, DidOpenTextDocument, DidSaveTextDocument, Exit,
    LogMessage, PublishDiagnostics, ShowMessage,
};
use lsp_types::request::{
    CodeActionRequest, DocumentHighlightRequest, DocumentSymbolRequest, Formatting, GotoDefinition,
    References, Rename, Request, Shutdown,
};
use lsp_types::{
    ClientCapabilities, ClientInfo, CodeActionOrCommand, CodeActionParams,
    DidChangeTextDocumentParams, DidCloseTextDocumentParams, DidOpenTextDocumentParams,
    DidSaveTextDocumentParams, DocumentFormattingParams, DocumentHighlightParams,
    DocumentRangeFormattingParams, DocumentSymbolParams, DocumentSymbolResponse, FormattingOptions,
    GotoDefinitionParams, GotoDefinitionResponse, InitializeParams, InitializeResult,
    InitializedParams, Location, LogMessageParams, Position, PublishDiagnosticsParams,
    ReferenceParams, RenameParams, ServerCapabilities, ShowMessageParams,
    TextDocumentContentChangeEvent, TextDocumentIdentifier, TextDocumentItem,
    TextDocumentPositionParams, TextEdit, Url, VersionedTextDocumentIdentifier, WorkspaceEdit,
};
use serde_json::Value;
use tokio::process::{Child, Command};
use tokio::sync::watch;
use tokio::task::JoinHandle;
use tokio_util::compat::{TokioAsyncReadCompatExt, TokioAsyncWriteCompatExt};

use crate::config::LspConfig;
use crate::diagnostics::{DiagnosticsSnapshot, DiagnosticsStore, PublishedDiagnostics};
use crate::documents::{DocumentStore, OpenDocument};

/// How many times a crashed server may be restarted before the client gives up.
pub const MAX_RESTARTS: u32 = 3;
const STDERR_TAIL: usize = 200;
const SERVER_LOG_TAIL: usize = 200;

/// State shared with the `async-lsp` router (it handles server-to-client traffic).
#[derive(Debug, Clone)]
pub(crate) struct ClientState {
    diagnostics: DiagnosticsStore,
    server_logs: Arc<Mutex<VecDeque<String>>>,
}

impl ClientState {
    fn new() -> (Self, watch::Receiver<DiagnosticsSnapshot>) {
        let (diagnostics, rx) = DiagnosticsStore::new();
        (
            Self {
                diagnostics,
                server_logs: Arc::new(Mutex::new(VecDeque::new())),
            },
            rx,
        )
    }

    fn record_log(&self, message: String) {
        let mut logs = self
            .server_logs
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if logs.len() == SERVER_LOG_TAIL {
            logs.pop_front();
        }
        logs.push_back(message);
    }
}

struct Session {
    server: ServerSocket,
    child: Child,
    mainloop: JoinHandle<Result<(), async_lsp::Error>>,
    pid: u32,
}

/// A Verible language-server session.
///
/// The server is started lazily on the first call that needs it, restarted when
/// it dies (bounded by [`MAX_RESTARTS`]), and re-synchronised with every open
/// document after a restart. `shutdown` sends the LSP shutdown/exit handshake
/// and reaps the child, so no zombie is left behind.
pub struct LspClient {
    config: LspConfig,
    state: ClientState,
    diagnostics_rx: watch::Receiver<DiagnosticsSnapshot>,
    documents: DocumentStore,
    session: Option<Session>,
    capabilities: Option<ServerCapabilities>,
    initialize_params_json: Option<Value>,
    initialize_result_json: Option<Value>,
    restarts: u32,
    stderr_tail: Arc<Mutex<VecDeque<String>>>,
}

impl std::fmt::Debug for LspClient {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LspClient")
            .field("ls_path", &self.config.ls_path)
            .field("workspace_root", &self.config.workspace_root)
            .field("running", &self.is_running())
            .field("pid", &self.pid())
            .field("restarts", &self.restarts)
            .finish_non_exhaustive()
    }
}

impl LspClient {
    /// Create a client. No process is spawned until the first request
    /// (lazy start, as required by the lane contract).
    pub fn new(config: LspConfig) -> Self {
        let (state, diagnostics_rx) = ClientState::new();
        Self {
            config,
            state,
            diagnostics_rx,
            documents: DocumentStore::default(),
            session: None,
            capabilities: None,
            initialize_params_json: None,
            initialize_result_json: None,
            restarts: 0,
            stderr_tail: Arc::new(Mutex::new(VecDeque::new())),
        }
    }

    /// The configuration this client was built with.
    pub fn config(&self) -> &LspConfig {
        &self.config
    }

    /// True once a server process is running.
    pub fn is_running(&self) -> bool {
        self.session
            .as_ref()
            .is_some_and(|session| !session.mainloop.is_finished())
    }

    /// PID of the running server, for lifecycle evidence.
    pub fn pid(&self) -> Option<u32> {
        self.session.as_ref().map(|session| session.pid)
    }

    /// How many times a crashed server has been restarted.
    pub fn restart_count(&self) -> u32 {
        self.restarts
    }

    /// Capabilities advertised by the server (`initialize` result).
    pub fn server_capabilities(&self) -> Option<&ServerCapabilities> {
        self.capabilities.as_ref()
    }

    /// The `initialize` request/result as JSON, for evidence transcripts.
    pub fn initialize_exchange_json(&self) -> (Option<Value>, Option<Value>) {
        (
            self.initialize_params_json.clone(),
            self.initialize_result_json.clone(),
        )
    }

    /// Last lines the server wrote to stderr (bounded).
    pub fn stderr_tail(&self) -> Vec<String> {
        self.stderr_tail
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .iter()
            .cloned()
            .collect()
    }

    /// Last `window/logMessage` / `window/showMessage` lines (bounded).
    pub fn server_logs(&self) -> Vec<String> {
        self.state
            .server_logs
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .iter()
            .cloned()
            .collect()
    }

    /// A receiver that observes diagnostics snapshots (for the UI task).
    pub fn diagnostics(&self) -> watch::Receiver<DiagnosticsSnapshot> {
        self.diagnostics_rx.clone()
    }

    /// Current diagnostics generation; pair with [`Self::wait_for_diagnostics`].
    pub fn diagnostics_generation(&self) -> u64 {
        self.diagnostics_rx.borrow().generation()
    }

    /// Wait until a diagnostics notification newer than `since_generation`
    /// arrives for `uri`.
    ///
    /// # Errors
    /// Times out with the configured budget, or when the channel closes.
    pub async fn wait_for_diagnostics(
        &mut self,
        uri: &Url,
        since_generation: u64,
        budget: Duration,
    ) -> Result<PublishedDiagnostics> {
        let mut rx = self.diagnostics_rx.clone();
        let deadline = tokio::time::sleep(budget);
        tokio::pin!(deadline);
        loop {
            let snapshot = rx.borrow_and_update().clone();
            if snapshot.generation() > since_generation {
                if let Some(entry) = snapshot.for_uri(uri) {
                    return Ok(entry.clone());
                }
            }
            tokio::select! {
                changed = rx.changed() => {
                    if changed.is_err() {
                        bail!("diagnostics channel closed while waiting for {uri}");
                    }
                }
                () = &mut deadline => {
                    bail!("no diagnostics for {uri} within {budget:?}");
                }
            }
        }
    }

    /// Open (or re-open) a document and announce it to the server.
    pub async fn open_document(
        &mut self,
        uri: Url,
        language_id: impl Into<String>,
        text: impl Into<String>,
    ) -> Result<()> {
        let server = self.ensure_session().await?;
        let document = self.documents.open(uri, language_id.into(), text.into())?;
        self.send_did_open(&server, &document)
    }

    /// Apply incremental changes to a tracked document.
    pub async fn change_document(
        &mut self,
        uri: &Url,
        changes: Vec<TextDocumentContentChangeEvent>,
    ) -> Result<()> {
        let server = self.ensure_session().await?;
        let document = self.documents.change(uri, &changes)?;
        server
            .notify::<DidChangeTextDocument>(DidChangeTextDocumentParams {
                text_document: VersionedTextDocumentIdentifier {
                    uri: document.uri,
                    version: document.version,
                },
                content_changes: changes,
            })
            .map_err(|error| anyhow!("didChange failed: {error}"))
    }

    /// Announce a save (`textDocument/didSave`).
    pub async fn save_document(&mut self, uri: &Url) -> Result<()> {
        let server = self.ensure_session().await?;
        server
            .notify::<DidSaveTextDocument>(DidSaveTextDocumentParams {
                text_document: TextDocumentIdentifier { uri: uri.clone() },
                text: None,
            })
            .map_err(|error| anyhow!("didSave failed: {error}"))
    }

    /// Close a tracked document (`textDocument/didClose`).
    pub async fn close_document(&mut self, uri: &Url) -> Result<()> {
        let server = self.ensure_session().await?;
        let closed = self.documents.close(uri)?;
        if closed.is_some() {
            server
                .notify::<DidCloseTextDocument>(DidCloseTextDocumentParams {
                    text_document: TextDocumentIdentifier { uri: uri.clone() },
                })
                .map_err(|error| anyhow!("didClose failed: {error}"))?;
        }
        Ok(())
    }

    /// A tracked document snapshot (text + version).
    pub fn document(&self, uri: &Url) -> Result<Option<OpenDocument>> {
        self.documents.get(uri)
    }

    /// `textDocument/definition`, normalised to concrete locations.
    pub async fn definition(&mut self, uri: &Url, position: Position) -> Result<Vec<Location>> {
        let response = self
            .request::<GotoDefinition>(GotoDefinitionParams {
                text_document_position_params: position_params(uri, position),
                work_done_progress_params: Default::default(),
                partial_result_params: Default::default(),
            })
            .await?;
        Ok(match response {
            Some(GotoDefinitionResponse::Scalar(location)) => vec![location],
            Some(GotoDefinitionResponse::Array(locations)) => locations,
            Some(GotoDefinitionResponse::Link(links)) => links
                .into_iter()
                .map(|link| Location {
                    uri: link.target_uri,
                    range: link.target_selection_range,
                })
                .collect(),
            None => Vec::new(),
        })
    }

    /// `textDocument/references`.
    pub async fn references(
        &mut self,
        uri: &Url,
        position: Position,
        include_declaration: bool,
    ) -> Result<Vec<Location>> {
        let response = self
            .request::<References>(ReferenceParams {
                text_document_position: position_params(uri, position),
                context: lsp_types::ReferenceContext {
                    include_declaration,
                },
                work_done_progress_params: Default::default(),
                partial_result_params: Default::default(),
            })
            .await?;
        Ok(response.unwrap_or_default())
    }

    /// `textDocument/documentSymbol`.
    pub async fn document_symbols(&mut self, uri: &Url) -> Result<Option<DocumentSymbolResponse>> {
        let text_document = TextDocumentIdentifier { uri: uri.clone() };
        self.request::<DocumentSymbolRequest>(DocumentSymbolParams {
            text_document,
            work_done_progress_params: Default::default(),
            partial_result_params: Default::default(),
        })
        .await
    }

    /// `textDocument/rename`.
    pub async fn rename(
        &mut self,
        uri: &Url,
        position: Position,
        new_name: impl Into<String>,
    ) -> Result<Option<WorkspaceEdit>> {
        self.request::<Rename>(RenameParams {
            text_document_position: position_params(uri, position),
            new_name: new_name.into(),
            work_done_progress_params: Default::default(),
        })
        .await
    }

    /// `textDocument/documentHighlight`.
    pub async fn document_highlight(
        &mut self,
        uri: &Url,
        position: Position,
    ) -> Result<Vec<lsp_types::DocumentHighlight>> {
        let response = self
            .request::<DocumentHighlightRequest>(DocumentHighlightParams {
                text_document_position_params: position_params(uri, position),
                work_done_progress_params: Default::default(),
                partial_result_params: Default::default(),
            })
            .await?;
        Ok(response.unwrap_or_default())
    }

    /// `textDocument/formatting`; the caller applies the returned edits to its
    /// buffer (this crate never mutates a document itself).
    pub async fn formatting(
        &mut self,
        uri: &Url,
        tab_size: u32,
        insert_spaces: bool,
    ) -> Result<Vec<TextEdit>> {
        let response = self
            .request::<Formatting>(DocumentFormattingParams {
                text_document: TextDocumentIdentifier { uri: uri.clone() },
                options: formatting_options(tab_size, insert_spaces),
                work_done_progress_params: Default::default(),
            })
            .await?;
        Ok(response.unwrap_or_default())
    }

    /// `textDocument/rangeFormatting`.
    pub async fn range_formatting(
        &mut self,
        uri: &Url,
        range: lsp_types::Range,
        tab_size: u32,
        insert_spaces: bool,
    ) -> Result<Vec<TextEdit>> {
        let range_response = self
            .request::<lsp_types::request::RangeFormatting>(DocumentRangeFormattingParams {
                text_document: TextDocumentIdentifier { uri: uri.clone() },
                range,
                options: formatting_options(tab_size, insert_spaces),
                work_done_progress_params: Default::default(),
            })
            .await?;
        Ok(range_response.unwrap_or_default())
    }

    /// `textDocument/codeAction`.
    pub async fn code_actions(
        &mut self,
        uri: &Url,
        range: lsp_types::Range,
        diagnostics: Vec<lsp_types::Diagnostic>,
    ) -> Result<Vec<CodeActionOrCommand>> {
        let response = self
            .request::<CodeActionRequest>(CodeActionParams {
                text_document: TextDocumentIdentifier { uri: uri.clone() },
                range,
                context: lsp_types::CodeActionContext {
                    diagnostics,
                    only: None,
                    trigger_kind: None,
                },
                work_done_progress_params: Default::default(),
                partial_result_params: Default::default(),
            })
            .await?;
        Ok(response.unwrap_or_default())
    }

    /// Graceful shutdown: `shutdown` request, `exit` notification, then reap.
    ///
    /// # Errors
    /// Only when the session could not be ended at all; a server that stopped
    /// answering is killed and reaped, which is not an error.
    pub async fn shutdown(&mut self) -> Result<()> {
        let Some(mut session) = self.session.take() else {
            return Ok(());
        };
        let request = session.server.request::<Shutdown>(());
        match tokio::time::timeout(self.config.shutdown_timeout, request).await {
            Ok(_) => {}
            Err(_) => self.record_stderr_line("shutdown request timed out; killing the server"),
        }
        match session.server.notify::<Exit>(()) {
            Ok(()) => {}
            Err(error) => self.record_stderr_line(&format!("exit notification failed: {error}")),
        }
        let waited = tokio::time::timeout(self.config.shutdown_timeout, session.child.wait()).await;
        if waited.is_err() {
            match session.child.kill().await {
                Ok(()) => {}
                Err(error) => self.record_stderr_line(&format!("kill failed: {error}")),
            }
            let _ = session.child.wait().await;
        }
        if !session.mainloop.is_finished() {
            session.mainloop.abort();
            let outcome = session.mainloop.await;
            if outcome.is_err() {
                self.record_stderr_line("main loop task was aborted");
            }
        }
        self.capabilities = None;
        if self.restarts > 0 {
            self.record_stderr_line(&format!("session ended after {} restart(s)", self.restarts));
        }
        Ok(())
    }

    /// Ensure a live, initialised session and return its socket.
    async fn ensure_session(&mut self) -> Result<ServerSocket> {
        if self
            .session
            .as_ref()
            .is_some_and(|session| session.mainloop.is_finished())
        {
            self.reap_dead_session();
        }
        if self.session.is_none() {
            self.start_session().await?;
        }
        let session = self
            .session
            .as_ref()
            .ok_or_else(|| anyhow!("language server session is not available"))?;
        Ok(session.server.clone())
    }

    fn reap_dead_session(&mut self) {
        if let Some(session) = self.session.take() {
            self.restarts += 1;
            if !session.mainloop.is_finished() {
                session.mainloop.abort();
            }
            // The child has exited (that is why the main loop finished); wait
            // for it so the process table entry is reaped.
            let child = session.child;
            tokio::spawn(async move {
                let mut child = child;
                let _ = child.wait().await;
            });
            self.record_stderr_line("language server exited; it will be restarted lazily");
        }
    }

    async fn start_session(&mut self) -> Result<()> {
        if self.restarts > MAX_RESTARTS {
            bail!(
                "language server crashed {} times; refusing to restart again",
                self.restarts
            );
        }
        let mut child = Command::new(&self.config.ls_path)
            .args(self.config.command_args())
            .current_dir(self.config.child_working_dir())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()
            .with_context(|| {
                format!(
                    "spawning verible-verilog-ls at {}",
                    self.config.ls_path.display()
                )
            })?;
        let pid = child.id().unwrap_or_default();
        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| anyhow!("language server has no stdout pipe"))?;
        let stdin = child
            .stdin
            .take()
            .ok_or_else(|| anyhow!("language server has no stdin pipe"))?;
        if let Some(stderr) = child.stderr.take() {
            let tail = self.stderr_tail.clone();
            tokio::spawn(async move {
                use tokio::io::AsyncBufReadExt;
                let mut lines = tokio::io::BufReader::new(stderr).lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    let mut tail = tail.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
                    if tail.len() == STDERR_TAIL {
                        tail.pop_front();
                    }
                    tail.push_back(line);
                }
            });
        }

        let state = self.state.clone();
        let (mainloop, server) = MainLoop::new_client(move |_socket| {
            let mut router = Router::new(state);
            router.notification::<PublishDiagnostics>(
                |state: &mut ClientState, params: PublishDiagnosticsParams| {
                    state.diagnostics.record(params);
                    ControlFlow::Continue(())
                },
            );
            router.notification::<LogMessage>(
                |state: &mut ClientState, params: LogMessageParams| {
                    state.record_log(format!("[log] {}", params.message));
                    ControlFlow::Continue(())
                },
            );
            router.notification::<ShowMessage>(
                |state: &mut ClientState, params: ShowMessageParams| {
                    state.record_log(format!("[message] {}", params.message));
                    ControlFlow::Continue(())
                },
            );
            router
        });
        let mainloop = tokio::spawn(async move {
            mainloop
                .run_buffered(stdout.compat(), stdin.compat_write())
                .await
        });
        self.session = Some(Session {
            server: server.clone(),
            child,
            mainloop,
            pid,
        });

        self.initialize(&server).await?;
        Ok(())
    }

    #[allow(deprecated)] // the server (v0.0-4298) still reads the legacy root_uri
    async fn initialize(&mut self, server: &ServerSocket) -> Result<()> {
        let root_uri = Url::from_directory_path(&self.config.workspace_root)
            .map_err(|()| anyhow!("workspace root is not a valid file path"))?;
        let params = InitializeParams {
            process_id: Some(std::process::id()),
            client_info: Some(ClientInfo {
                name: "vedit".to_string(),
                version: Some(crate::CRATE_VERSION.to_string()),
            }),
            root_uri: Some(root_uri.clone()),
            workspace_folders: Some(vec![lsp_types::WorkspaceFolder {
                uri: root_uri,
                name: "workspace".to_string(),
            }]),
            capabilities: client_capabilities(),
            ..InitializeParams::default()
        };
        self.initialize_params_json = serde_json::to_value(&params).ok();
        let response = tokio::time::timeout(
            self.config.init_timeout,
            server.request::<lsp_types::request::Initialize>(params),
        )
        .await
        .map_err(|_| anyhow!("initialize timed out after {:?}", self.config.init_timeout))?
        .map_err(|error| anyhow!("initialize failed: {error}"))?;
        self.initialize_result_json = serde_json::to_value(capabilities_projection(&response)).ok();
        self.capabilities = Some(response.capabilities.clone());
        server
            .notify::<lsp_types::notification::Initialized>(InitializedParams {})
            .map_err(|error| anyhow!("initialized notification failed: {error}"))?;
        self.reopen_documents(server)?;
        Ok(())
    }

    fn reopen_documents(&mut self, server: &ServerSocket) -> Result<()> {
        let documents = self.documents.snapshot()?;
        for document in documents {
            self.send_did_open(server, &document)?;
        }
        Ok(())
    }

    fn send_did_open(&self, server: &ServerSocket, document: &OpenDocument) -> Result<()> {
        server
            .notify::<DidOpenTextDocument>(DidOpenTextDocumentParams {
                text_document: TextDocumentItem {
                    uri: document.uri.clone(),
                    language_id: document.language_id.clone(),
                    version: document.version,
                    text: document.text.clone(),
                },
            })
            .map_err(|error| anyhow!("didOpen failed: {error}"))
    }

    /// Send a request that can be cancelled by the caller.
    ///
    /// Cancellation is a client-side abort with `biased` `select!`: the pending
    /// future is dropped and its response is discarded by `async-lsp`, so a
    /// keystroke never has to wait for a stale navigation or formatting result.
    pub async fn request_cancellable<R: Request>(
        &mut self,
        params: R::Params,
        token: &mut CancelToken,
    ) -> Result<R::Result> {
        let server = self.ensure_session().await?;
        let request = server.request::<R>(params);
        tokio::select! {
            biased;
            () = token.cancelled() => {
                bail!("LSP request {} cancelled before completion", R::METHOD)
            }
            outcome = tokio::time::timeout(self.config.request_timeout, request) => match outcome {
                Ok(Ok(value)) => Ok(value),
                Ok(Err(error)) => Err(anyhow!("LSP request {} failed: {error}", R::METHOD)),
                Err(_) => bail!(
                    "LSP request {} timed out after {:?}",
                    R::METHOD,
                    self.config.request_timeout
                ),
            },
        }
    }

    /// Cancellable `textDocument/definition`.
    pub async fn definition_cancellable(
        &mut self,
        uri: &Url,
        position: Position,
        token: &mut CancelToken,
    ) -> Result<Vec<Location>> {
        let response = self
            .request_cancellable::<GotoDefinition>(
                GotoDefinitionParams {
                    text_document_position_params: position_params(uri, position),
                    work_done_progress_params: Default::default(),
                    partial_result_params: Default::default(),
                },
                token,
            )
            .await?;
        Ok(match response {
            Some(GotoDefinitionResponse::Scalar(location)) => vec![location],
            Some(GotoDefinitionResponse::Array(locations)) => locations,
            Some(GotoDefinitionResponse::Link(links)) => links
                .into_iter()
                .map(|link| Location {
                    uri: link.target_uri,
                    range: link.target_selection_range,
                })
                .collect(),
            None => Vec::new(),
        })
    }

    async fn request<R: Request>(&mut self, params: R::Params) -> Result<R::Result> {
        let server = self.ensure_session().await?;
        let request = server.request::<R>(params);
        match tokio::time::timeout(self.config.request_timeout, request).await {
            Ok(Ok(response)) => Ok(response),
            Ok(Err(error)) => Err(anyhow!("LSP request {} failed: {error}", R::METHOD)),
            Err(_) => bail!(
                "LSP request {} timed out after {:?}",
                R::METHOD,
                self.config.request_timeout
            ),
        }
    }

    fn record_stderr_line(&self, line: &str) {
        let mut tail = self
            .stderr_tail
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if tail.len() == STDERR_TAIL {
            tail.pop_front();
        }
        tail.push_back(line.to_string());
    }
}

impl Drop for LspClient {
    fn drop(&mut self) {
        if let Some(mut session) = self.session.take() {
            if let Ok(()) = session.child.start_kill() {
                // The child was still alive; SIGKILL was delivered.
            }
            session.mainloop.abort();
        }
    }
}

/// Fires cancellation for one in-flight request.
#[derive(Debug, Clone)]
pub struct CancelHandle {
    tx: watch::Sender<bool>,
}

/// Observed by [`LspClient::request_cancellable`].
#[derive(Debug, Clone)]
pub struct CancelToken {
    rx: watch::Receiver<bool>,
}

impl CancelHandle {
    /// Create a handle/token pair.
    pub fn new() -> (Self, CancelToken) {
        let (tx, rx) = watch::channel(false);
        (Self { tx }, CancelToken { rx })
    }

    /// Mark the request as cancelled.
    pub fn cancel(&self) {
        self.tx.send_replace(true);
    }
}

impl CancelToken {
    /// True once [`CancelHandle::cancel`] ran.
    pub fn is_cancelled(&self) -> bool {
        *self.rx.borrow()
    }

    async fn cancelled(&mut self) {
        if self.is_cancelled() {
            return;
        }
        if self.rx.changed().await.is_err() {
            // The handle was dropped; treat that as cancellation.
        }
    }
}

fn position_params(uri: &Url, position: Position) -> TextDocumentPositionParams {
    TextDocumentPositionParams {
        text_document: TextDocumentIdentifier { uri: uri.clone() },
        position,
    }
}

fn formatting_options(tab_size: u32, insert_spaces: bool) -> FormattingOptions {
    FormattingOptions {
        tab_size,
        insert_spaces,
        properties: std::collections::HashMap::new(),
        trim_trailing_whitespace: None,
        insert_final_newline: None,
        trim_final_newlines: None,
    }
}

fn client_capabilities() -> ClientCapabilities {
    use lsp_types::{
        CodeActionClientCapabilities, DocumentSymbolClientCapabilities,
        DynamicRegistrationClientCapabilities, GotoCapability,
        PublishDiagnosticsClientCapabilities, RenameClientCapabilities,
        TextDocumentClientCapabilities, TextDocumentSyncClientCapabilities,
    };

    ClientCapabilities {
        text_document: Some(TextDocumentClientCapabilities {
            synchronization: Some(TextDocumentSyncClientCapabilities {
                dynamic_registration: Some(false),
                will_save: Some(false),
                will_save_wait_until: Some(false),
                did_save: Some(true),
            }),
            definition: Some(GotoCapability {
                dynamic_registration: Some(false),
                link_support: Some(false),
            }),
            references: Some(DynamicRegistrationClientCapabilities {
                dynamic_registration: Some(false),
            }),
            document_symbol: Some(DocumentSymbolClientCapabilities {
                dynamic_registration: Some(false),
                symbol_kind: None,
                hierarchical_document_symbol_support: Some(true),
                tag_support: None,
            }),
            formatting: Some(DynamicRegistrationClientCapabilities {
                dynamic_registration: Some(false),
            }),
            range_formatting: Some(DynamicRegistrationClientCapabilities {
                dynamic_registration: Some(false),
            }),
            rename: Some(RenameClientCapabilities {
                dynamic_registration: Some(false),
                prepare_support: Some(false),
                prepare_support_default_behavior: None,
                honors_change_annotations: None,
            }),
            document_highlight: Some(DynamicRegistrationClientCapabilities {
                dynamic_registration: Some(false),
            }),
            code_action: Some(CodeActionClientCapabilities {
                dynamic_registration: Some(false),
                code_action_literal_support: None,
                is_preferred_support: None,
                disabled_support: None,
                data_support: None,
                resolve_support: None,
                honors_change_annotations: None,
            }),
            publish_diagnostics: Some(PublishDiagnosticsClientCapabilities {
                related_information: Some(false),
                tag_support: None,
                version_support: Some(false),
                code_description_support: Some(false),
                data_support: Some(false),
            }),
            ..TextDocumentClientCapabilities::default()
        }),
        ..ClientCapabilities::default()
    }
}

/// Serialisable projection of the server capabilities, for evidence transcripts.
fn capabilities_projection(result: &InitializeResult) -> Value {
    serde_json::json!({
        "serverInfo": result.server_info,
        "capabilities": result.capabilities,
    })
}
