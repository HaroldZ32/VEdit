//! Worker-thread session facade over [`LspClient`].
//!
//! The UI never awaits an LSP round trip: [`SessionHandle::start`] spawns one
//! worker thread that owns the client and a tokio runtime, opens the effective
//! file set, waits (bounded) for the first diagnostics, and then serves requests
//! one at a time. Every entry on this type is non-blocking; results are read
//! through [`SessionHandle::status`] and [`SessionHandle::poll`].
//!
//! # Why a session and not more `async fn`
//!
//! The C ABI (C2) cannot carry a Rust future. A session gives the Qt side one
//! stable handle for the whole file set: it is the object that receives the
//! **same** `Vec<PathBuf>` the project model produced, and it reports
//! `files_expected` / `files_opened` / `skipped` so a file that disappeared
//! between expansion and `didOpen` is diagnosable instead of silently absent.
//!
//! # Lifecycle
//!
//! `Starting` -> `Ready` | `Failed`; `stop` -> `Stopped`. A session whose server
//! binary is missing becomes `Failed` with a message naming the path; it never
//! panics and never leaves a pending request unanswerable.
//!
//! # Threading
//!
//! * the worker thread owns the [`LspClient`] (constructed on that thread);
//! * `status`/`request`/`poll` are `&self` and lock only a short mutex;
//! * `stop` is a channel send; [`SessionHandle::wait_for_stop`] is the only
//!   blocking call and exists for tests and shutdown paths.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use anyhow::{anyhow, Result};
use lsp_types::{
    Diagnostic, DocumentSymbol, DocumentSymbolResponse, Location, Position, Range, TextEdit, Url,
    WorkspaceEdit,
};
use tokio::sync::mpsc;

use crate::client::LspClient;
use crate::config::LspConfig;
use crate::diagnostics::PublishedDiagnostics;

/// Default budget for the first diagnostics of one opened document.
pub const DEFAULT_DIAGNOSTICS_BUDGET: Duration = Duration::from_secs(3);
/// Default budget for a cooperative stop (shutdown handshake + reaping).
pub const DEFAULT_STOP_BUDGET: Duration = Duration::from_secs(5);
/// Tab size used by the formatting entry point (Verible's default is 2 spaces).
pub const DEFAULT_TAB_SIZE: u32 = 2;

/// Lifecycle state of one LSP session.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionPhase {
    /// The worker is opening documents.
    Starting,
    /// The file set is open; requests are served.
    Ready,
    /// The session could not be established; the message says why.
    Failed,
    /// The worker has ended (stop requested or the process exited).
    Stopped,
}

impl SessionPhase {
    /// Stable code for the C ABI: 0 starting, 1 ready, 2 failed, 3 stopped.
    pub const fn code(self) -> i32 {
        match self {
            Self::Starting => 0,
            Self::Ready => 1,
            Self::Failed => 2,
            Self::Stopped => 3,
        }
    }
}

/// One file from the effective file set that could not be opened.
#[derive(Debug, Clone)]
pub struct SkippedFile {
    /// The path as it came from the project model.
    pub path: PathBuf,
    /// Why it was skipped (missing file, non-file path, invalid URI).
    pub reason: String,
}

/// Observable state of the session (A3d: what the UI shows).
#[derive(Debug, Clone)]
pub struct SessionStatus {
    /// Lifecycle phase.
    pub phase: SessionPhase,
    /// How many files the caller handed in (the project model's effective set).
    pub files_expected: usize,
    /// How many of those were opened on the server.
    pub files_opened: usize,
    /// Files in the set that could not be opened, with the reason.
    pub skipped: Vec<SkippedFile>,
    /// Diagnostics currently recorded across the open documents.
    pub diagnostics: usize,
    /// Diagnostics generation counter of the client.
    pub generation: u64,
    /// PID of the server process, when one is running.
    pub server_pid: Option<u32>,
    /// How many times a crashed server was restarted.
    pub restarts: u32,
    /// Human-readable state line for the UI.
    pub message: String,
}

impl SessionStatus {
    fn new(files_expected: usize) -> Self {
        Self {
            phase: SessionPhase::Starting,
            files_expected,
            files_opened: 0,
            skipped: Vec::new(),
            diagnostics: 0,
            generation: 0,
            server_pid: None,
            restarts: 0,
            message: format!("opening {files_expected} file(s)"),
        }
    }
}

/// One request the session can serve.
#[derive(Debug, Clone)]
pub enum SessionRequest {
    /// `textDocument/definition` at a zero-based position.
    Definition {
        /// Source file.
        file: PathBuf,
        /// Zero-based line.
        line: u32,
        /// Zero-based UTF-16 character.
        character: u32,
    },
    /// `textDocument/references` at a zero-based position.
    References {
        /// Source file.
        file: PathBuf,
        /// Zero-based line.
        line: u32,
        /// Zero-based UTF-16 character.
        character: u32,
    },
    /// `textDocument/documentSymbol` (the outline).
    DocumentSymbols {
        /// Source file.
        file: PathBuf,
    },
    /// `textDocument/rename` at a zero-based position.
    Rename {
        /// Source file.
        file: PathBuf,
        /// Zero-based line.
        line: u32,
        /// Zero-based UTF-16 character.
        character: u32,
        /// New identifier.
        new_name: String,
    },
    /// `textDocument/formatting` (the caller applies the edits).
    Formatting {
        /// Source file.
        file: PathBuf,
    },
    /// Diagnostics currently recorded for one file.
    Diagnostics {
        /// Source file.
        file: PathBuf,
    },
}

/// One outline entry (flat; nested symbols are flattened in document order).
#[derive(Debug, Clone)]
pub struct SymbolNode {
    /// Symbol name.
    pub name: String,
    /// Detail/type string when the server supplied one.
    pub detail: Option<String>,
    /// `lsp_types::SymbolKind` as a number.
    pub kind: i32,
    /// Selection range (zero-based).
    pub range: Range,
}

/// One text edit together with the document it belongs to.
#[derive(Debug, Clone)]
pub struct WorkspaceTextEdit {
    /// Target document.
    pub uri: Url,
    /// The edit.
    pub edit: TextEdit,
}

/// One diagnostic normalised for the problem model.
#[derive(Debug, Clone)]
pub struct SessionDiagnostic {
    /// Range (zero-based).
    pub range: Range,
    /// 1 error, 2 warning, 3 information, 4 hint; `None` means "server default".
    pub severity: Option<i32>,
    /// Rule/code when the server supplied one.
    pub code: Option<String>,
    /// Producing tool.
    pub source: Option<String>,
    /// Message text.
    pub message: String,
}

/// Result payload of one finished request.
#[derive(Debug, Clone)]
pub enum SessionResponse {
    /// Definition/reference locations.
    Locations(Vec<Location>),
    /// Outline entries.
    Symbols(Vec<SymbolNode>),
    /// Edits to apply (formatting or rename).
    Edits(Vec<WorkspaceTextEdit>),
    /// Diagnostics for one file.
    Diagnostics(Vec<SessionDiagnostic>),
}

/// Result of one [`SessionHandle::poll`] attempt.
#[derive(Debug, Clone)]
pub enum PollOutcome {
    /// The request is still running.
    Pending,
    /// The request finished.
    Ready(SessionResponse),
    /// The request failed; the message says why.
    Failed(String),
    /// The id is unknown or was already consumed.
    Unknown,
}

enum RequestState {
    Pending,
    Ready(SessionResponse),
    Failed(String),
}

enum Command {
    Request { id: u64, request: SessionRequest },
    Stop,
}

struct Inner {
    status: Mutex<SessionStatus>,
    requests: Mutex<HashMap<u64, RequestState>>,
    commands: mpsc::UnboundedSender<Command>,
    next_request: AtomicU64,
    stop_sent: AtomicBool,
}

impl Inner {
    fn lock_status(&self) -> MutexGuard<'_, SessionStatus> {
        self.status
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn lock_requests(&self) -> MutexGuard<'_, HashMap<u64, RequestState>> {
        self.requests
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn set_ready(
        &self,
        opened: usize,
        skipped: Vec<SkippedFile>,
        diagnostics: usize,
        generation: u64,
        pid: Option<u32>,
        restarts: u32,
    ) {
        let mut status = self.lock_status();
        status.phase = SessionPhase::Ready;
        status.files_opened = opened;
        status.skipped = skipped;
        status.diagnostics = diagnostics;
        status.generation = generation;
        status.server_pid = pid;
        status.restarts = restarts;
        status.message = format!(
            "ready: {opened}/{} file(s) open, {diagnostics} diagnostic(s), {} skipped",
            status.files_expected,
            status.skipped.len()
        );
    }

    fn fail(&self, message: String) {
        let mut status = self.lock_status();
        status.phase = SessionPhase::Failed;
        status.message = message.clone();
        let mut requests = self.lock_requests();
        for state in requests.values_mut() {
            if matches!(state, RequestState::Pending) {
                *state = RequestState::Failed(message.clone());
            }
        }
    }

    fn stopped(&self) {
        let mut status = self.lock_status();
        if status.phase != SessionPhase::Failed {
            status.phase = SessionPhase::Stopped;
            status.message = "stopped".to_string();
        }
        let mut requests = self.lock_requests();
        for state in requests.values_mut() {
            if matches!(state, RequestState::Pending) {
                *state = RequestState::Failed("the session was stopped".to_string());
            }
        }
    }

    fn finish_request(&self, id: u64, outcome: Result<SessionResponse>) {
        let mut requests = self.lock_requests();
        let state = match outcome {
            Ok(response) => RequestState::Ready(response),
            Err(error) => RequestState::Failed(error.to_string()),
        };
        requests.insert(id, state);
    }

    fn refresh_from(&self, generation: u64, diagnostics: usize) {
        let mut status = self.lock_status();
        status.generation = generation;
        status.diagnostics = diagnostics;
    }
}

/// A running LSP session over one effective file set.
pub struct SessionHandle {
    inner: Arc<Inner>,
    worker: Mutex<Option<JoinHandle<()>>>,
    stop_budget: Duration,
}

impl SessionHandle {
    /// Start a session over `files` (the project model's effective file set).
    ///
    /// No server process is started here: the worker thread does that on its
    /// first `didOpen`. `files` is used verbatim — duplicates are not removed
    /// here because the caller already flattened the set.
    ///
    /// # Errors
    ///
    /// Only when the worker thread cannot be spawned; everything else (missing
    /// binary, unreadable file, server crash) is reported through the status.
    pub fn start(config: LspConfig, files: Vec<PathBuf>) -> Result<Self> {
        Self::with_budgets(
            config,
            files,
            DEFAULT_DIAGNOSTICS_BUDGET,
            DEFAULT_STOP_BUDGET,
        )
    }

    /// Start a session with explicit diagnostic and shutdown budgets.
    ///
    /// # Errors
    ///
    /// See [`Self::start`].
    pub fn with_budgets(
        config: LspConfig,
        files: Vec<PathBuf>,
        diagnostics_budget: Duration,
        stop_budget: Duration,
    ) -> Result<Self> {
        let (sender, receiver) = mpsc::unbounded_channel();
        let inner = Arc::new(Inner {
            status: Mutex::new(SessionStatus::new(files.len())),
            requests: Mutex::new(HashMap::new()),
            commands: sender,
            next_request: AtomicU64::new(1),
            stop_sent: AtomicBool::new(false),
        });
        let worker_inner = Arc::clone(&inner);
        let worker = std::thread::Builder::new()
            .name("vd-lsp-session".to_string())
            .spawn(move || {
                match tokio::runtime::Builder::new_multi_thread()
                    .enable_all()
                    .build()
                {
                    Ok(runtime) => {
                        runtime.block_on(session_loop(
                            worker_inner,
                            config,
                            files,
                            diagnostics_budget,
                            receiver,
                        ));
                    }
                    Err(error) => {
                        worker_inner.fail(format!(
                            "cannot start the LSP runtime for the session: {error}"
                        ));
                    }
                }
            })
            .map_err(|error| anyhow!("cannot spawn the LSP session worker: {error}"))?;
        Ok(Self {
            inner,
            worker: Mutex::new(Some(worker)),
            stop_budget,
        })
    }

    /// A snapshot of the observable session state (never blocks on the server).
    pub fn status(&self) -> SessionStatus {
        self.inner.lock_status().clone()
    }

    /// Queue one request; returns its id for [`Self::poll`].
    ///
    /// # Errors
    ///
    /// When the session worker has already ended.
    pub fn request(&self, request: SessionRequest) -> Result<u64> {
        let id = self.inner.next_request.fetch_add(1, Ordering::Relaxed);
        // A session that already failed answers every request with its failure
        // reason (A3d: the UI always observes a terminal state, never a hang).
        let failed = {
            let status = self.inner.lock_status();
            if status.phase == SessionPhase::Failed {
                Some(status.message.clone())
            } else {
                None
            }
        };
        if let Some(message) = failed {
            self.inner
                .lock_requests()
                .insert(id, RequestState::Failed(message));
            return Ok(id);
        }
        self.inner.lock_requests().insert(id, RequestState::Pending);
        if self
            .inner
            .commands
            .send(Command::Request { id, request })
            .is_err()
        {
            self.inner.lock_requests().insert(
                id,
                RequestState::Failed("the LSP session worker is not running".to_string()),
            );
        }
        Ok(id)
    }

    /// Consume one finished request; `Unknown` once the id was polled.
    pub fn poll(&self, id: u64) -> PollOutcome {
        let mut requests = self.inner.lock_requests();
        match requests.remove(&id) {
            None => PollOutcome::Unknown,
            Some(RequestState::Pending) => {
                requests.insert(id, RequestState::Pending);
                PollOutcome::Pending
            }
            Some(RequestState::Ready(response)) => PollOutcome::Ready(response),
            Some(RequestState::Failed(message)) => PollOutcome::Failed(message),
        }
    }

    /// Ask the worker to shut the server down. Non-blocking and idempotent.
    pub fn stop(&self) {
        if !self.inner.stop_sent.swap(true, Ordering::SeqCst) {
            let _ = self.inner.commands.send(Command::Stop);
        }
    }

    /// True once the worker thread has ended (and reaped the server).
    pub fn is_stopped(&self) -> bool {
        let mut worker = self
            .worker
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        match worker.as_ref() {
            None => true,
            Some(handle) => {
                if handle.is_finished() {
                    if let Some(handle) = worker.take() {
                        let _ = handle.join();
                    }
                    true
                } else {
                    false
                }
            }
        }
    }

    /// Block until the worker ends or `budget` elapses. Returns whether it ended.
    pub fn wait_for_stop(&self) -> bool {
        self.stop();
        let deadline = Instant::now() + self.stop_budget;
        while Instant::now() < deadline {
            if self.is_stopped() {
                return true;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        self.is_stopped()
    }
}

impl Drop for SessionHandle {
    fn drop(&mut self) {
        self.stop();
    }
}

async fn session_loop(
    inner: Arc<Inner>,
    config: LspConfig,
    files: Vec<PathBuf>,
    diagnostics_budget: Duration,
    mut commands: mpsc::UnboundedReceiver<Command>,
) {
    let mut client = LspClient::new(config);
    let mut opened: Vec<(Url, PathBuf)> = Vec::new();
    let mut skipped: Vec<SkippedFile> = Vec::new();

    for path in &files {
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(error) => {
                skipped.push(SkippedFile {
                    path: path.clone(),
                    reason: format!("cannot read the file: {error}"),
                });
                continue;
            }
        };
        let uri = match Url::from_file_path(path) {
            Ok(uri) => uri,
            Err(()) => {
                skipped.push(SkippedFile {
                    path: path.clone(),
                    reason: "not an absolute file path".to_string(),
                });
                continue;
            }
        };
        if let Err(error) = client
            .open_document(uri.clone(), language_id(path), text)
            .await
        {
            inner.fail(format!(
                "cannot open {} on the server: {error}",
                path.display()
            ));
            return;
        }
        opened.push((uri, path.clone()));
    }

    if opened.is_empty() {
        inner.fail(if skipped.is_empty() {
            "the effective file set is empty: there is nothing to open".to_string()
        } else {
            format!(
                "none of the {} file(s) in the effective set could be opened",
                files.len()
            )
        });
        return;
    }

    // Wait for the first diagnostics of the whole set under ONE budget (A3d:
    // "reparsed within the agreed time"). A document with no diagnostics is
    // normal, so the loop stops as soon as every opened URI has an entry or the
    // budget expires; the count below is the observable value either way.
    let deadline = std::time::Instant::now() + diagnostics_budget;
    loop {
        let covered = {
            let snapshot = client.diagnostics().borrow().clone();
            opened
                .iter()
                .filter(|(uri, _)| snapshot.for_uri(uri).is_some())
                .count()
        };
        if covered >= opened.len() || std::time::Instant::now() >= deadline {
            break;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    let diagnostics = client.diagnostics().borrow().diagnostic_count();

    inner.set_ready(
        opened.len(),
        skipped,
        diagnostics,
        client.diagnostics_generation(),
        client.pid(),
        client.restart_count(),
    );

    while let Some(command) = commands.recv().await {
        match command {
            Command::Stop => break,
            Command::Request { id, request } => {
                let outcome = serve(&mut client, request).await;
                inner.finish_request(id, outcome);
                let snapshot_generation = client.diagnostics_generation();
                let count = client.diagnostics().borrow().diagnostic_count();
                inner.refresh_from(snapshot_generation, count);
            }
        }
    }

    if let Err(error) = client.shutdown().await {
        inner.lock_status().message = format!("shutdown reported: {error}");
    }
    inner.stopped();
}

async fn serve(client: &mut LspClient, request: SessionRequest) -> Result<SessionResponse> {
    let (file, line, character, new_name) = match &request {
        SessionRequest::Definition {
            file,
            line,
            character,
        }
        | SessionRequest::References {
            file,
            line,
            character,
        } => (file.clone(), *line, *character, None),
        SessionRequest::DocumentSymbols { file }
        | SessionRequest::Formatting { file }
        | SessionRequest::Diagnostics { file } => (file.clone(), 0, 0, None),
        SessionRequest::Rename {
            file,
            line,
            character,
            new_name,
        } => (file.clone(), *line, *character, Some(new_name.clone())),
    };
    let uri = ensure_open(client, &file).await?;
    let position = Position { line, character };
    match request {
        SessionRequest::Definition { .. } => Ok(SessionResponse::Locations(
            client.definition(&uri, position).await?,
        )),
        SessionRequest::References { .. } => Ok(SessionResponse::Locations(
            client.references(&uri, position, true).await?,
        )),
        SessionRequest::DocumentSymbols { .. } => Ok(SessionResponse::Symbols(flatten_symbols(
            client.document_symbols(&uri).await?,
        ))),
        SessionRequest::Rename { .. } => {
            let new_name = new_name.unwrap_or_default();
            Ok(SessionResponse::Edits(flatten_workspace_edit(
                client.rename(&uri, position, new_name).await?,
            )))
        }
        SessionRequest::Formatting { .. } => {
            let edits = client
                .formatting(&uri, DEFAULT_TAB_SIZE, true)
                .await?
                .into_iter()
                .map(|edit| WorkspaceTextEdit {
                    uri: uri.clone(),
                    edit,
                })
                .collect();
            Ok(SessionResponse::Edits(edits))
        }
        SessionRequest::Diagnostics { .. } => {
            let snapshot = client.diagnostics().borrow().clone();
            let diagnostics = snapshot
                .for_uri(&uri)
                .map(normalise_diagnostics)
                .unwrap_or_default();
            Ok(SessionResponse::Diagnostics(diagnostics))
        }
    }
}

async fn ensure_open(client: &mut LspClient, file: &Path) -> Result<Url> {
    let uri =
        Url::from_file_path(file).map_err(|()| anyhow!("not a file path: {}", file.display()))?;
    if client.document(&uri)?.is_some() {
        return Ok(uri);
    }
    let text = std::fs::read_to_string(file)
        .map_err(|error| anyhow!("cannot read {}: {error}", file.display()))?;
    client
        .open_document(uri.clone(), language_id(file), text)
        .await?;
    Ok(uri)
}

fn language_id(path: &Path) -> &'static str {
    match path.extension().and_then(|extension| extension.to_str()) {
        Some("sv" | "svh") => "systemverilog",
        _ => "verilog",
    }
}

fn normalise_diagnostics(published: &PublishedDiagnostics) -> Vec<SessionDiagnostic> {
    published
        .diagnostics
        .iter()
        .map(|diagnostic: &Diagnostic| SessionDiagnostic {
            range: diagnostic.range,
            severity: diagnostic.severity.map(severity_code_of),
            code: diagnostic.code.as_ref().map(|code| match code {
                lsp_types::NumberOrString::Number(number) => number.to_string(),
                lsp_types::NumberOrString::String(text) => text.clone(),
            }),
            source: diagnostic.source.clone(),
            message: diagnostic.message.clone(),
        })
        .collect()
}

fn flatten_symbols(response: Option<DocumentSymbolResponse>) -> Vec<SymbolNode> {
    match response {
        None => Vec::new(),
        Some(DocumentSymbolResponse::Flat(symbols)) => symbols
            .into_iter()
            .map(|symbol| SymbolNode {
                name: symbol.name,
                detail: symbol.container_name,
                kind: symbol_kind_code(symbol.kind),
                range: symbol.location.range,
            })
            .collect(),
        Some(DocumentSymbolResponse::Nested(symbols)) => {
            let mut flattened = Vec::new();
            for symbol in symbols {
                push_symbol(&mut flattened, symbol);
            }
            flattened
        }
    }
}

fn push_symbol(flattened: &mut Vec<SymbolNode>, symbol: DocumentSymbol) {
    flattened.push(SymbolNode {
        name: symbol.name,
        detail: symbol.detail,
        kind: symbol_kind_code(symbol.kind),
        range: symbol.selection_range,
    });
    for child in symbol.children.unwrap_or_default() {
        push_symbol(flattened, child);
    }
}

fn flatten_workspace_edit(edit: Option<WorkspaceEdit>) -> Vec<WorkspaceTextEdit> {
    let Some(edit) = edit else {
        return Vec::new();
    };
    let mut edits = Vec::new();
    if let Some(changes) = edit.changes {
        for (uri, text_edits) in changes {
            for text_edit in text_edits {
                edits.push(WorkspaceTextEdit {
                    uri: uri.clone(),
                    edit: text_edit,
                });
            }
        }
    }
    edits
}

/// Numeric code of a reported severity (1 error … 4 hint, `0` unknown).
fn severity_code_of(severity: lsp_types::DiagnosticSeverity) -> i32 {
    use lsp_types::DiagnosticSeverity as Severity;
    if severity == Severity::ERROR {
        1
    } else if severity == Severity::WARNING {
        2
    } else if severity == Severity::INFORMATION {
        3
    } else if severity == Severity::HINT {
        4
    } else {
        0
    }
}

/// Numeric code of an outline symbol kind (LSP `SymbolKind`: 1 … 26).
fn symbol_kind_code(kind: lsp_types::SymbolKind) -> i32 {
    use lsp_types::SymbolKind as Kind;
    const TABLE: [(Kind, i32); 26] = [
        (Kind::FILE, 1),
        (Kind::MODULE, 2),
        (Kind::NAMESPACE, 3),
        (Kind::PACKAGE, 4),
        (Kind::CLASS, 5),
        (Kind::METHOD, 6),
        (Kind::PROPERTY, 7),
        (Kind::FIELD, 8),
        (Kind::CONSTRUCTOR, 9),
        (Kind::ENUM, 10),
        (Kind::INTERFACE, 11),
        (Kind::FUNCTION, 12),
        (Kind::VARIABLE, 13),
        (Kind::CONSTANT, 14),
        (Kind::STRING, 15),
        (Kind::NUMBER, 16),
        (Kind::BOOLEAN, 17),
        (Kind::ARRAY, 18),
        (Kind::OBJECT, 19),
        (Kind::KEY, 20),
        (Kind::NULL, 21),
        (Kind::ENUM_MEMBER, 22),
        (Kind::STRUCT, 23),
        (Kind::EVENT, 24),
        (Kind::OPERATOR, 25),
        (Kind::TYPE_PARAMETER, 26),
    ];
    TABLE
        .iter()
        .find(|(candidate, _)| *candidate == kind)
        .map(|(_, code)| *code)
        .unwrap_or(0)
}

/// Severity numbers used by the C ABI (1 error … 4 hint), `0` unknown.
pub fn severity_code(severity: Option<i32>) -> i32 {
    severity.unwrap_or(0)
}

/// A range helper for the C ABI.
pub const fn range_parts(range: Range) -> (u32, u32, u32, u32) {
    (
        range.start.line,
        range.start.character,
        range.end.line,
        range.end.character,
    )
}

#[cfg(test)]
mod tests {
    use super::{flatten_symbols, flatten_workspace_edit, language_id, severity_code};
    use lsp_types::{
        DocumentSymbol, DocumentSymbolResponse, Location, SymbolInformation, SymbolKind, TextEdit,
        Url, WorkspaceEdit,
    };
    use std::collections::HashMap;
    use std::path::Path;

    #[test]
    fn language_ids_cover_the_product_extensions() {
        assert_eq!(language_id(Path::new("a.sv")), "systemverilog");
        assert_eq!(language_id(Path::new("a.svh")), "systemverilog");
        assert_eq!(language_id(Path::new("a.v")), "verilog");
        assert_eq!(language_id(Path::new("a.vh")), "verilog");
        assert_eq!(language_id(Path::new("Makefile")), "verilog");
    }

    #[test]
    #[allow(deprecated)] // lsp-types 0.95 requires the deprecated field in literals
    fn nested_symbols_are_flattened_in_document_order() {
        let child = DocumentSymbol {
            name: "inner".into(),
            detail: None,
            kind: SymbolKind::FUNCTION,
            tags: None,
            deprecated: None,
            range: lsp_types::Range::default(),
            selection_range: lsp_types::Range::default(),
            children: None,
        };
        let parent = DocumentSymbol {
            name: "outer".into(),
            detail: Some("module".into()),
            kind: SymbolKind::MODULE,
            tags: None,
            deprecated: None,
            range: lsp_types::Range::default(),
            selection_range: lsp_types::Range::default(),
            children: Some(vec![child]),
        };
        let flattened = flatten_symbols(Some(DocumentSymbolResponse::Nested(vec![parent])));
        let names: Vec<&str> = flattened
            .iter()
            .map(|symbol| symbol.name.as_str())
            .collect();
        assert_eq!(names, vec!["outer", "inner"]);
    }

    #[test]
    #[allow(deprecated)] // lsp-types 0.95 requires the deprecated field in literals
    fn flat_symbols_keep_their_location_range() {
        let Ok(uri) = Url::parse("file:///tmp/a.sv") else {
            return;
        };
        let symbol = SymbolInformation {
            name: "top".into(),
            kind: SymbolKind::MODULE,
            tags: None,
            deprecated: None,
            location: Location {
                uri,
                range: lsp_types::Range::default(),
            },
            container_name: Some("rtl".into()),
        };
        let flattened = flatten_symbols(Some(DocumentSymbolResponse::Flat(vec![symbol])));
        assert_eq!(flattened.len(), 1);
        assert_eq!(flattened[0].detail.as_deref(), Some("rtl"));
    }

    #[test]
    fn workspace_edits_are_flattened_per_uri() {
        let Ok(uri) = Url::parse("file:///tmp/a.sv") else {
            return;
        };
        let mut changes = HashMap::new();
        changes.insert(
            uri,
            vec![TextEdit {
                range: lsp_types::Range::default(),
                new_text: "wire".into(),
            }],
        );
        let flattened = flatten_workspace_edit(Some(WorkspaceEdit {
            changes: Some(changes),
            document_changes: None,
            change_annotations: None,
        }));
        assert_eq!(flattened.len(), 1);
        assert_eq!(flattened[0].edit.new_text, "wire");
    }

    #[test]
    fn severity_codes_are_zero_when_absent() {
        assert_eq!(severity_code(None), 0);
        assert_eq!(severity_code(Some(1)), 1);
    }
}
