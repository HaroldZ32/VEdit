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
use std::ffi::OsStr;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::JoinHandle;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use anyhow::{anyhow, Result};
use lsp_types::{
    Diagnostic, DocumentSymbol, DocumentSymbolResponse, HoverContents, Location, MarkedString,
    ParameterLabel, Position, Range, SignatureHelp, TextDocumentContentChangeEvent, TextEdit, Url,
    WorkspaceEdit,
};
use serde_json::Value;
use tokio::sync::mpsc;

use crate::backend::{capabilities_from_server, BackendCapabilities, LspBackend};
use crate::client::{LspClient, MAX_RESTARTS};
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
    /// Which language server this session drives (T138).
    pub backend: LspBackend,
    /// Capability table: the static one until the server answered `initialize`,
    /// then the live one. `None` only before the worker has a table to report.
    pub capabilities: Option<BackendCapabilities>,
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
    fn new(files_expected: usize, backend: LspBackend) -> Self {
        Self {
            phase: SessionPhase::Starting,
            backend,
            capabilities: Some(crate::backend::capabilities(backend)),
            files_expected,
            files_opened: 0,
            skipped: Vec::new(),
            diagnostics: 0,
            generation: 0,
            server_pid: None,
            restarts: 0,
            message: format!(
                "opening {files_expected} file(s) with {}",
                backend.display_name()
            ),
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
    /// `textDocument/hover` at a zero-based position (T16).
    Hover {
        /// Source file.
        file: PathBuf,
        /// Zero-based line.
        line: u32,
        /// Zero-based UTF-16 character.
        character: u32,
    },
    /// `textDocument/signatureHelp` at a zero-based position (T16).
    SignatureHelp {
        /// Source file.
        file: PathBuf,
        /// Zero-based line.
        line: u32,
        /// Zero-based UTF-16 character.
        character: u32,
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

/// One call signature of a `textDocument/signatureHelp` answer (T16).
#[derive(Debug, Clone, Default)]
pub struct SignatureInfo {
    /// The signature's label, exactly as the server sent it.
    pub label: String,
    /// Parameter labels (resolved from `LabelOffsets` against `label`).
    pub parameters: Vec<String>,
}

/// A `textDocument/signatureHelp` answer, flattened for the C ABI (T16).
#[derive(Debug, Clone, Default)]
pub struct SignatureHelpResult {
    /// The signatures, in the server's order.
    pub signatures: Vec<SignatureInfo>,
    /// Which signature is active, when the server selected one.
    pub active_signature: Option<u32>,
    /// Which parameter is active, when the server selected one.
    pub active_parameter: Option<u32>,
}

/// A `textDocument/hover` answer (T16): the text the server sent back, plus the
/// range it applies to.
#[derive(Debug, Clone, Default)]
pub struct HoverResult {
    /// Plain text of the hover (markup values are passed through unchanged).
    pub text: String,
    /// The range the hover applies to, when the server supplied one.
    pub range: Option<Range>,
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
    /// Diagnostics for one file. `published` is false when the server has no
    /// entry for the document at all (it never sent `publishDiagnostics` for
    /// it): an empty list with `published == false` is "not analysed", not
    /// "clean" (batch-20261118 AC-2).
    Diagnostics {
        /// The rows the server currently holds for the document.
        rows: Vec<SessionDiagnostic>,
        /// Whether the server ever published a diagnostics set for it.
        published: bool,
    },
    /// Hover text (T16).
    Hover(HoverResult),
    /// Signature help (T16).
    SignatureHelp(SignatureHelpResult),
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
    Request {
        id: u64,
        request: SessionRequest,
    },
    /// Replace one tracked document's text with the caller's buffer (a
    /// full-document `didChange`); this is how diagnostics follow the EDITOR
    /// BUFFER instead of the bytes on disk.
    Change {
        path: PathBuf,
        text: String,
    },
    /// Push the session's build context to the server (batch-20261118 AC-1):
    /// include dirs and defines. The files slang compiles are the SESSION's own
    /// file set, which the worker already owns, so they are not carried here.
    /// Applied through `workspace/executeCommand`, never argv.
    BuildContext {
        include_dirs: Vec<PathBuf>,
        defines: Vec<String>,
    },
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

    // One call site fills every observable field at once, which is clearer than
    // a builder for a worker-thread hand-off.
    #[allow(clippy::too_many_arguments)]
    fn set_ready(
        &self,
        opened: usize,
        skipped: Vec<SkippedFile>,
        diagnostics: usize,
        generation: u64,
        pid: Option<u32>,
        restarts: u32,
        capabilities: Option<BackendCapabilities>,
    ) {
        let mut status = self.lock_status();
        status.phase = SessionPhase::Ready;
        if capabilities.is_some() {
            status.capabilities = capabilities;
        }
        status.files_opened = opened;
        status.skipped = skipped;
        status.diagnostics = diagnostics;
        status.generation = generation;
        status.server_pid = pid;
        status.restarts = restarts;
        status.message = format!(
            "ready: backend={} {opened}/{} file(s) open, {diagnostics} diagnostic(s), {} skipped",
            status.backend.as_str(),
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

    /// A command succeeded: a session that had been marked `Failed` is answering
    /// again, so it leaves the failed state (batch-20261118 AC-3). The restart
    /// count stays visible; the message keeps the reason it had failed for.
    fn note_success(&self, message: String, restarts: u32) {
        let mut status = self.lock_status();
        status.restarts = restarts;
        if status.phase == SessionPhase::Failed {
            status.phase = SessionPhase::Ready;
            status.message = format!("recovered: {message}");
        } else if status.phase == SessionPhase::Ready && !message.is_empty() {
            status.message = message;
        }
    }
}

/// The bounded tail of the child's stderr, for the `Failed` status line
/// (batch-20261118 AC-3: the reason must name what the server said, without an
/// ABI change - `VdLspStatus.message` is the existing string channel).
fn stderr_tail_text(client: &LspClient) -> String {
    const TAIL_LINES: usize = 5;
    const TAIL_CHARS: usize = 400;
    let tail = client.stderr_tail();
    if tail.is_empty() {
        return String::new();
    }
    let start = tail.len().saturating_sub(TAIL_LINES);
    let mut joined = tail[start..].join(" | ");
    if joined.chars().count() > TAIL_CHARS {
        joined = joined.chars().take(TAIL_CHARS).collect::<String>() + "…";
    }
    format!(" — server stderr: {joined}")
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
        let backend = config.backend;
        let inner = Arc::new(Inner {
            status: Mutex::new(SessionStatus::new(files.len(), backend)),
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

    /// The backend this session drives.
    pub fn backend(&self) -> LspBackend {
        self.inner.lock_status().backend
    }

    /// The capability table to show the UI: the static table until the server
    /// answered `initialize`, then the table refined from that live answer.
    pub fn capabilities(&self) -> BackendCapabilities {
        let status = self.inner.lock_status();
        status
            .capabilities
            .clone()
            .unwrap_or_else(|| crate::backend::capabilities(status.backend))
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

    /// Replace one document's tracked text with `text` (a full-document
    /// `didChange`; the document is opened first when the session does not have
    /// it). Nothing is read from disk: this is the editor-buffer path.
    ///
    /// # Errors
    ///
    /// When the session worker has already ended.
    pub fn change_document(&self, path: &Path, text: String) -> Result<()> {
        self.inner
            .commands
            .send(Command::Change {
                path: path.to_path_buf(),
                text,
            })
            .map_err(|_| anyhow!("the LSP session worker is not running"))
    }

    /// Push the project build context to the session (batch-20261118 AC-1).
    ///
    /// For slang-server this is the ONLY channel that reaches the server:
    /// 0.3.1 ignores argv `-I`/`-D`. The worker turns the context into ONE
    /// generated `.f` - `+incdir+` per include directory, `-D<name>[=<value>]`
    /// per define, and then the SESSION's own file set, one absolute path per
    /// line - and pushes it with a single `slang.setBuildFile`. The session
    /// file set is what slang compiles; the project's imported `.f` lists are
    /// deliberately not nested, because that made the server compile the WHOLE
    /// project and let slang's own last-processed rule pick a same-named module
    /// that disagrees with the module tree (the realtime "module has no such
    /// port" false positives). `slang.addDefine` is deliberately unused: on
    /// 0.3.1 it persists `<workspace>/.slang/local/server.json` in the user's
    /// project (and keeps applying to later sessions), so nothing here writes
    /// into the project directory. The verible backend is untouched - its build
    /// context already travels in the file list it is started with.
    ///
    /// # Errors
    ///
    /// When the session worker has already ended; a server-side rejection is
    /// reported through the status message of the session.
    pub fn set_build_context(
        &self,
        include_dirs: Vec<PathBuf>,
        defines: Vec<String>,
    ) -> Result<()> {
        self.inner
            .commands
            .send(Command::BuildContext {
                include_dirs,
                defines,
            })
            .map_err(|_| anyhow!("the LSP session worker is not running"))
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

    let capabilities = client
        .server_capabilities()
        .map(|advertised| capabilities_from_server(inner.lock_status().backend, advertised));
    inner.set_ready(
        opened.len(),
        skipped,
        diagnostics,
        client.diagnostics_generation(),
        client.pid(),
        client.restart_count(),
        capabilities,
    );

    // batch-20261118 AC-3: a command failure, a child exit or the end of the
    // main loop after Ready is a SESSION state, not a log line. One witness
    // flag keeps the periodic check from re-reporting the same death.
    let backend = inner.lock_status().backend;
    let mut child_exit_noted = false;
    let mut context_file: Option<ContextFile> = None;
    loop {
        let command = tokio::select! {
            command = commands.recv() => command,
            () = tokio::time::sleep(Duration::from_millis(250)) => {
                if !child_exit_noted && client.session_ended() {
                    child_exit_noted = true;
                    let message = format!(
                        "the language server exited after Ready ({}/{} restart(s)){}",
                        client.restart_count(),
                        MAX_RESTARTS,
                        stderr_tail_text(&client)
                    );
                    inner.fail(message);
                }
                continue;
            }
        };
        let Some(command) = command else {
            break;
        };
        match command {
            Command::Stop => break,
            Command::Request { id, request } => {
                let outcome = serve(&mut client, request).await;
                let error = outcome.as_ref().err().map(ToString::to_string);
                inner.finish_request(id, outcome);
                match error {
                    Some(error) => {
                        child_exit_noted = client.session_ended();
                        inner.fail(format!(
                            "request failed: {error} ({}/{} restart(s)){}",
                            client.restart_count(),
                            MAX_RESTARTS,
                            stderr_tail_text(&client)
                        ));
                    }
                    None if client.session_ended() => {
                        child_exit_noted = true;
                        inner.fail(format!(
                            "the language server exited after Ready ({}/{} restart(s)){}",
                            client.restart_count(),
                            MAX_RESTARTS,
                            stderr_tail_text(&client)
                        ));
                    }
                    None => inner.note_success(String::new(), client.restart_count()),
                }
                let snapshot_generation = client.diagnostics_generation();
                let count = client.diagnostics().borrow().diagnostic_count();
                inner.refresh_from(snapshot_generation, count);
            }
            Command::Change { path, text } => {
                let outcome = apply_buffer_change(&mut client, &path, text).await;
                match outcome {
                    Ok(bytes) => {
                        child_exit_noted = false;
                        inner.note_success(
                            format!("buffer changed ({} bytes): {}", bytes, path.display()),
                            client.restart_count(),
                        );
                    }
                    Err(error) => {
                        child_exit_noted = client.session_ended();
                        inner.fail(format!(
                            "buffer change failed: {error} ({}/{} restart(s)){}",
                            client.restart_count(),
                            MAX_RESTARTS,
                            stderr_tail_text(&client)
                        ));
                    }
                }
                let snapshot_generation = client.diagnostics_generation();
                let count = client.diagnostics().borrow().diagnostic_count();
                inner.refresh_from(snapshot_generation, count);
            }
            Command::BuildContext {
                include_dirs,
                defines,
            } => {
                let outcome = apply_build_context(
                    &mut client,
                    backend,
                    &include_dirs,
                    &defines,
                    &files,
                    &mut context_file,
                )
                .await;
                match outcome {
                    Ok(note) => {
                        child_exit_noted = false;
                        if !note.is_empty() {
                            inner.note_success(note, client.restart_count());
                        }
                    }
                    Err(error) => {
                        child_exit_noted = client.session_ended();
                        inner.fail(format!(
                            "build context was not accepted: {error}{}",
                            stderr_tail_text(&client)
                        ));
                    }
                }
            }
        }
    }
    // F5: the guard removes the generated file AND its private directory on
    // every exit path this loop can take - a normal stop, a closed command
    // channel, or an unwinding panic. A `SIGKILL` cannot run it; under
    // `XDG_RUNTIME_DIR` the session manager removes the directory then.
    drop(context_file);

    if let Err(error) = client.shutdown().await {
        inner.lock_status().message = format!("shutdown reported: {error}");
    }
    inner.stopped();
}

/// The generated slang build file and the private directory holding it.
///
/// The `Drop` guard removes both, so no exit path of the session worker leaves
/// the project's absolute include paths (and its defines) behind. The file is
/// never created inside the project: it lives in a fresh 0700 directory under
/// `XDG_RUNTIME_DIR` (or the system temp directory).
#[derive(Debug)]
pub(crate) struct ContextFile {
    path: PathBuf,
    pub(crate) directory: PathBuf,
}

impl Drop for ContextFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.directory);
    }
}

impl LspConfig {
    /// Limit the workspace index to the effective HDL sources of the imported project.
    /// The session's smaller compilation and open-document sets remain separate.
    ///
    /// # Errors
    /// Returns an error if the private server configuration cannot be written.
    pub fn with_project_index(mut self, files: &[PathBuf]) -> Result<Self> {
        let mut failure = anyhow!("no writable directory for the project index");
        for base in context_base_dirs() {
            let result = create_context_file_with(&base, "", |path, _| match self.backend {
                LspBackend::Verible => create_private_file(
                    path,
                    &crate::filelist_text(files, &self.include_dirs, &self.defines),
                ),
                LspBackend::Slang => write_slang_project_index(path, files),
            });
            match result {
                Ok(index) => {
                    match self.backend {
                        LspBackend::Verible => self.file_list_path = Some(index.path.clone()),
                        LspBackend::Slang => {
                            // Slang merges ~/.slang/server.json into its workspace config.
                            // Imported project inputs already supply the include dirs and defines.
                            self.environment.insert(
                                "HOME".to_string(),
                                index.directory.join("home").to_string_lossy().into_owned(),
                            );
                        }
                    }
                    self.project_index = Some(Arc::new(index));
                    return Ok(self);
                }
                Err(error) => failure = error,
            }
        }
        Err(failure)
    }
}

fn write_slang_project_index(context: &Path, files: &[PathBuf]) -> std::io::Result<()> {
    let root = context
        .parent()
        .ok_or_else(|| std::io::Error::other("missing index directory"))?;
    let settings = root.join(".slang");
    std::fs::create_dir(&settings)?;
    std::fs::create_dir(root.join("home"))?;
    let mut patterns = Vec::with_capacity(files.len());
    for (index, file) in files.iter().enumerate() {
        let path = file.to_string_lossy();
        // Slang's glob syntax has no literal escape. It canonicalizes matches,
        // so an exact symlink alias keeps metacharacters from adding other files.
        if path.contains(['*', '?']) || path.split('/').any(|part| part.starts_with("...")) {
            #[cfg(unix)]
            {
                let alias = root.join(format!("source-{index}"));
                std::os::unix::fs::symlink(file, &alias)?;
                patterns.push(alias.to_string_lossy().into_owned());
            }
            #[cfg(not(unix))]
            return Err(std::io::Error::other(format!(
                "unsupported slang index path: {path}"
            )));
        } else {
            patterns.push(path.into_owned());
        }
    }
    let json = serde_json::json!({"index": [{"dirs": []}], "indexGlobs": patterns});
    create_private_file(&settings.join("server.json"), &json.to_string())
}

/// Candidate directories for the generated context, in preference order:
/// `XDG_RUNTIME_DIR` when the session environment provides an absolute,
/// existing one (its owner - the session manager - removes it at logout, which
/// also covers a crashed process), then the system temp directory as a fallback
/// for a runtime directory that disappeared or is not writable.
fn context_base_dirs() -> Vec<PathBuf> {
    context_base_dirs_from(
        std::env::var_os("XDG_RUNTIME_DIR").as_deref(),
        std::env::temp_dir(),
    )
}

/// [`context_base_dirs`] with its two inputs spelled out, so the selection is
/// testable without mutating the process environment.
fn context_base_dirs_from(runtime: Option<&OsStr>, temp: PathBuf) -> Vec<PathBuf> {
    let mut bases = Vec::new();
    if let Some(value) = runtime {
        let path = PathBuf::from(value);
        if path.is_absolute() && path.is_dir() && path != temp {
            bases.push(path);
        }
    }
    bases.push(temp);
    bases
}

/// Create one private directory (`0700` on unix). `create_dir` never adopts an
/// existing path, so a planted symlink or directory of the same name fails the
/// call instead of being followed.
fn create_private_dir(path: &Path) -> std::io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::DirBuilderExt;
        std::fs::DirBuilder::new().mode(0o700).create(path)
    }
    #[cfg(not(unix))]
    {
        std::fs::create_dir(path)
    }
}

/// Create one context file exclusively (`create_new`) and not world-readable
/// (`0600` on unix). `create_new` is `O_CREAT | O_EXCL`, which refuses a
/// pre-planted symlink instead of writing through it.
fn create_private_file(path: &Path, text: &str) -> std::io::Result<()> {
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)?;
    file.write_all(text.as_bytes())?;
    file.flush()
}

/// Write `text` into one fresh private directory under `base` and return the
/// guard for it. The name is unpredictable (pid + wall-clock nanoseconds + sequence)
/// and both the directory and the file are created exclusively, so a planted
/// path is refused instead of followed.
fn create_context_file(base: &Path, text: &str) -> Result<ContextFile> {
    create_context_file_with(base, text, create_private_file)
}

/// [`create_context_file`] with its file writer spelled out, so the cleanup after
/// a failed write is testable without a full disk.
fn create_context_file_with(
    base: &Path,
    text: &str,
    write: impl FnOnce(&Path, &str) -> std::io::Result<()>,
) -> Result<ContextFile> {
    static NEXT: AtomicU64 = AtomicU64::new(0);
    let mut attempts = 0;
    loop {
        let unique = format!(
            "vedit-slang-context-{}-{:x}-{}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        );
        let directory = base.join(unique);
        match create_private_dir(&directory) {
            Ok(()) => {}
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
                attempts += 1;
                if attempts >= 16 {
                    return Err(anyhow!(
                        "cannot create a private build-context directory in {}: {error}",
                        base.display()
                    ));
                }
                continue;
            }
            Err(error) => {
                return Err(anyhow!(
                    "cannot create the build-context directory {}: {error}",
                    directory.display()
                ));
            }
        }
        let path = directory.join("context.f");
        if let Err(error) = write(&path, text) {
            // The writer may have left a partial file or private settings behind.
            let _ = std::fs::remove_dir_all(&directory);
            return Err(anyhow!(
                "cannot write the build-context file {}: {error}",
                path.display()
            ));
        }
        return Ok(ContextFile { path, directory });
    }
}

/// Quote one argument for the line-based slang build file.
///
/// The parser splits a line like a command line, so an unquoted argument loses
/// everything after its first whitespace: a legal include directory such as
/// `/home/user/project with space/inc` becomes two tokens and the include stays
/// unresolved. Measured on the shipped slang-server 0.3.1: wrapping the whole
/// argument in double quotes (`+incdir+"…"`, `-D"NAME=VALUE"`, `"…/a.sv"`) keeps
/// it one token, and a backslash escapes a literal `"` or `\` inside the
/// quotes. A line break cannot be represented by the line-based format, so it
/// is refused instead of silently truncating the value.
fn push_quoted_argument(text: &mut String, prefix: &str, value: &str, kind: &str) -> Result<()> {
    if value.contains(['\n', '\r']) {
        return Err(anyhow!("a {kind} must not contain a line break: {value:?}"));
    }
    text.push_str(prefix);
    text.push('"');
    for character in value.chars() {
        if character == '"' || character == '\\' {
            text.push('\\');
        }
        text.push(character);
    }
    text.push('"');
    text.push('\n');
    Ok(())
}

/// The text of the slang build context: every include directory as
/// `+incdir+"<escaped path>"`, every project define as `-D"<escaped
/// name[=value]>"`, then the session's own files, ONE quoted absolute path per
/// line.
///
/// The trailing lines make this text a file list: slang compiles exactly the
/// session file set (the current file plus the one-level module closure the
/// module tree selected), so the realtime diagnostics and the module tree agree
/// on which definition of a same-named module is in force. The project's
/// imported `.f` is deliberately NOT nested with `-f`: that made slang-server
/// compile the whole project, and slang's own last-processed rule then picked a
/// definition the catalog never chose - a module from another directory could
/// win and every instantiation of it reported a bogus "no such port".
///
/// Measured on slang-server 0.3.1: a later `setBuildFile` REPLACES the previous
/// driver, so one file carries the whole context and re-pushing it is
/// idempotent (no define accumulates). The verible backend uses
/// [`crate::filelist_text`] for the same semantics (`+incdir+` / `+define+` /
/// bare paths) on its command line.
pub fn build_context_text(
    include_dirs: &[PathBuf],
    defines: &[String],
    files: &[PathBuf],
) -> Result<String> {
    let mut text = String::new();
    for directory in include_dirs {
        push_quoted_argument(
            &mut text,
            "+incdir+",
            &directory.display().to_string(),
            "include directory",
        )?;
    }
    for define in defines {
        push_quoted_argument(&mut text, "-D", define, "define")?;
    }
    for file in files {
        push_quoted_argument(&mut text, "", &file.display().to_string(), "session file")?;
    }
    Ok(text)
}

/// Write the generated slang build file into a fresh private directory.
///
/// `slang.addDefine` is deliberately NOT used: measured on the same build, it
/// creates `<workspace>/.slang/local/server.json` inside the user's project and
/// merges into it on every call (batch-20261118 AC-1 forbids touching the
/// project), and the accumulated flags outlive the session.
fn write_context_file(
    include_dirs: &[PathBuf],
    defines: &[String],
    files: &[PathBuf],
) -> Result<ContextFile> {
    let text = build_context_text(include_dirs, defines, files)?;

    let mut last_error = None;
    for base in context_base_dirs() {
        match create_context_file(&base, &text) {
            Ok(context) => return Ok(context),
            Err(error) => last_error = Some(error),
        }
    }
    Err(last_error.unwrap_or_else(|| anyhow!("no writable directory for the build context")))
}

/// Deliver the session's build context to the server (AC-1).
///
/// slang-server 0.3.1 ignores argv `-I`/`-D`; its `workspace/executeCommand`
/// surface is the channel that works. ONE `slang.setBuildFile` call carries the
/// whole context - include dirs, defines and the SESSION's own file set - as a
/// generated file list in a private temp directory, so the user's project is
/// never written to. The file set, not the project's imported `.f`, is what
/// slang compiles (see [`build_context_text`]). `slang.addDefine` is NOT used
/// (it persists `<workspace>/.slang/local/server.json` in the project, measured
/// on 0.3.1). `slang.setIncludeContext` is NOT this channel - it selects among
/// the locations an already-resolved file was included from.
async fn apply_build_context(
    client: &mut LspClient,
    backend: LspBackend,
    include_dirs: &[PathBuf],
    defines: &[String],
    files: &[PathBuf],
    context_file: &mut Option<ContextFile>,
) -> Result<String> {
    if backend != LspBackend::Slang {
        // Verible's build context already reaches it as a file list on its
        // command line; pushing anything here would change its behaviour.
        return Ok(String::new());
    }
    if include_dirs.is_empty() && defines.is_empty() && files.is_empty() {
        return Ok(String::new());
    }
    let context = write_context_file(include_dirs, defines, files)?;
    let argument = Value::String(context.path.display().to_string());
    // A rejected push returns here; `context` drops with it, so the generated
    // file and its private directory are removed on that path too.
    client
        .execute_command("slang.setBuildFile", vec![argument])
        .await?;
    if let Some(previous) = context_file.replace(context) {
        // A later push REPLACES the driver; the previous generated file (and
        // its private directory) is removed by the guard.
        drop(previous);
    }
    Ok(format!(
        "build context: {} include dir(s), {} define(s), {} session file(s)",
        include_dirs.len(),
        defines.len(),
        files.len()
    ))
}

/// Apply the editor buffer's text to one document: open it when the session does
/// not have it, otherwise send a full-document `didChange`. Returns the byte
/// length applied. Never touches the file on disk.
async fn apply_buffer_change(client: &mut LspClient, path: &Path, text: String) -> Result<usize> {
    let uri =
        Url::from_file_path(path).map_err(|()| anyhow!("{} is not a file path", path.display()))?;
    let bytes = text.len();
    if client.is_open(&uri) {
        client
            .change_document(
                &uri,
                vec![TextDocumentContentChangeEvent {
                    range: None,
                    range_length: None,
                    text,
                }],
            )
            .await?;
    } else {
        client.open_document(uri, language_id(path), text).await?;
    }
    Ok(bytes)
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
        }
        | SessionRequest::Hover {
            file,
            line,
            character,
        }
        | SessionRequest::SignatureHelp {
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
            // "No entry" and "an empty published set" are different answers:
            // only the second one is a clean verdict (AC-2). The server's
            // silence is never manufactured into "0 problems".
            let published = snapshot.for_uri(&uri);
            let rows = published.map(normalise_diagnostics).unwrap_or_default();
            Ok(SessionResponse::Diagnostics {
                rows,
                published: published.is_some(),
            })
        }
        SessionRequest::Hover { .. } => Ok(SessionResponse::Hover(
            client
                .hover(&uri, position)
                .await?
                .map(hover_result)
                .unwrap_or_default(),
        )),
        SessionRequest::SignatureHelp { .. } => Ok(SessionResponse::SignatureHelp(
            client
                .signature_help(&uri, position)
                .await?
                .map(signature_help_result)
                .unwrap_or_default(),
        )),
    }
}

/// Flatten an LSP hover payload into plain text.
///
/// The three wire forms (`MarkupContent`, a single `MarkedString`, a list of
/// them) all become text; nothing is invented when the server sent nothing.
fn hover_result(hover: lsp_types::Hover) -> HoverResult {
    HoverResult {
        text: hover_text(hover.contents),
        range: hover.range,
    }
}

fn hover_text(contents: HoverContents) -> String {
    match contents {
        HoverContents::Scalar(marked) => marked_string_text(marked),
        HoverContents::Array(items) => items
            .into_iter()
            .map(marked_string_text)
            .collect::<Vec<String>>()
            .join("\n\n"),
        HoverContents::Markup(markup) => markup.value,
    }
}

fn marked_string_text(marked: MarkedString) -> String {
    match marked {
        MarkedString::String(text) => text,
        MarkedString::LanguageString(code) => code.value,
    }
}

/// Flatten a `textDocument/signatureHelp` answer for the C ABI.
fn signature_help_result(help: SignatureHelp) -> SignatureHelpResult {
    SignatureHelpResult {
        signatures: help
            .signatures
            .into_iter()
            .map(|signature| SignatureInfo {
                parameters: signature
                    .parameters
                    .unwrap_or_default()
                    .into_iter()
                    .map(|parameter| parameter_label_text(&signature.label, parameter.label))
                    .collect(),
                label: signature.label,
            })
            .collect(),
        active_signature: help.active_signature,
        active_parameter: help.active_parameter,
    }
}

/// Resolve one parameter label against its signature: a plain label is used
/// verbatim, `LabelOffsets` are sliced out of the signature label.
fn parameter_label_text(signature_label: &str, label: ParameterLabel) -> String {
    match label {
        ParameterLabel::Simple(text) => text,
        ParameterLabel::LabelOffsets([start, end]) => {
            let units: Vec<u16> = signature_label.encode_utf16().collect();
            let start = start as usize;
            let end = (end as usize).min(units.len());
            if start >= end {
                return String::new();
            }
            String::from_utf16_lossy(&units[start..end])
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
    use super::{
        context_base_dirs, context_base_dirs_from, create_context_file_with, create_private_file,
        flatten_symbols, flatten_workspace_edit, hover_text, language_id, parameter_label_text,
        severity_code, signature_help_result, write_context_file,
    };
    use anyhow::Result;
    use lsp_types::{
        DocumentSymbol, DocumentSymbolResponse, Location, SymbolInformation, SymbolKind, TextEdit,
        Url, WorkspaceEdit,
    };
    use std::collections::HashMap;
    use std::ffi::OsStr;
    use std::path::{Path, PathBuf};
    use std::time::{SystemTime, UNIX_EPOCH};

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

    // --- T16: hover / signatureHelp conversion (no server needed) ----------

    #[test]
    fn hover_text_handles_all_three_wire_forms() {
        // Scalar markup string.
        let scalar = lsp_types::HoverContents::Scalar(lsp_types::MarkedString::String(
            "logic [7:0] data".into(),
        ));
        assert_eq!(hover_text(scalar), "logic [7:0] data");
        // Markup content: passed through unchanged (never re-rendered here).
        let markup = lsp_types::HoverContents::Markup(lsp_types::MarkupContent {
            kind: lsp_types::MarkupKind::Markdown,
            value: "```systemverilog\nlogic a;\n```".into(),
        });
        assert_eq!(hover_text(markup), "```systemverilog\nlogic a;\n```");
        // Array of marked strings, joined with a blank line.
        let array = lsp_types::HoverContents::Array(vec![
            lsp_types::MarkedString::String("first".into()),
            lsp_types::MarkedString::LanguageString(lsp_types::LanguageString {
                language: "systemverilog".into(),
                value: "second".into(),
            }),
        ]);
        assert_eq!(hover_text(array), "first\n\nsecond");
    }

    #[test]
    fn signature_help_resolves_parameter_labels() {
        let help = lsp_types::SignatureHelp {
            signatures: vec![
                lsp_types::SignatureInformation {
                    label: "foo(int a, int b)".into(),
                    documentation: None,
                    parameters: Some(vec![
                        lsp_types::ParameterInformation {
                            label: lsp_types::ParameterLabel::Simple("int a".into()),
                            documentation: None,
                        },
                        lsp_types::ParameterInformation {
                            label: lsp_types::ParameterLabel::LabelOffsets([11, 16]),
                            documentation: None,
                        },
                    ]),
                    active_parameter: None,
                },
                lsp_types::SignatureInformation {
                    label: "foo()".into(),
                    documentation: None,
                    parameters: None,
                    active_parameter: None,
                },
            ],
            active_signature: Some(0),
            active_parameter: Some(1),
        };
        let result = signature_help_result(help);
        assert_eq!(result.signatures.len(), 2);
        assert_eq!(result.signatures[0].label, "foo(int a, int b)");
        assert_eq!(result.signatures[0].parameters, vec!["int a", "int b"]);
        assert!(result.signatures[1].parameters.is_empty());
        assert_eq!(result.active_signature, Some(0));
        assert_eq!(result.active_parameter, Some(1));
    }

    #[test]
    fn parameter_labels_clamp_out_of_range_offsets() {
        assert_eq!(
            parameter_label_text("short", lsp_types::ParameterLabel::LabelOffsets([99, 120])),
            ""
        );
        assert_eq!(
            parameter_label_text("a,b", lsp_types::ParameterLabel::LabelOffsets([2, 99])),
            "b"
        );
    }

    // --- batch-20261118 F1/F2, batch-20261119 lint scope: the generated file --

    #[test]
    fn the_context_file_carries_incdirs_defines_and_the_session_files() -> Result<()> {
        let context = write_context_file(
            &[PathBuf::from("/project/inc")],
            &["WIDTH=8".to_string(), "SIM".to_string()],
            &[
                PathBuf::from("/project/rtl/top.sv"),
                PathBuf::from("/project/rtl/uart.v"),
            ],
        )?;
        let text = std::fs::read_to_string(&context.path)?;
        assert_eq!(
            text,
            "+incdir+\"/project/inc\"\n-D\"WIDTH=8\"\n-D\"SIM\"\n\
             \"/project/rtl/top.sv\"\n\"/project/rtl/uart.v\"\n"
        );
        // The context is a file list, so the files it names are what slang
        // compiles. The project's imported `.f` must NOT be nested: `-f` made
        // the server compile the whole project, and slang's own selection then
        // disagreed with the module tree.
        assert!(!text.contains("-f "), "{text}");
        assert!(context_base_dirs()
            .iter()
            .any(|base| context.path.starts_with(base)));
        Ok(())
    }

    /// H1: every value is one quoted token, and `"` / `\` inside it are escaped
    /// so the file-list parser cannot split or terminate it early.
    #[test]
    fn the_context_file_quotes_and_escapes_every_argument() -> Result<()> {
        let context = write_context_file(
            &[
                PathBuf::from("/project/inc dir"),
                PathBuf::from("/project/q\"uote\\dir"),
            ],
            &["WIDTH=8 + 4".to_string(), "MSG=\"hi\"".to_string()],
            &[PathBuf::from("/project/build dir/top file.sv")],
        )?;
        let text = std::fs::read_to_string(&context.path)?;
        assert_eq!(
            text.lines().collect::<Vec<_>>(),
            vec![
                r#"+incdir+"/project/inc dir""#,
                r#"+incdir+"/project/q\"uote\\dir""#,
                r#"-D"WIDTH=8 + 4""#,
                r#"-D"MSG=\"hi\"""#,
                r#""/project/build dir/top file.sv""#,
            ]
        );
        Ok(())
    }

    #[test]
    fn a_path_with_a_line_break_is_refused() {
        let error = match write_context_file(&[PathBuf::from("/project/a\nb")], &[], &[]) {
            Ok(_) => panic!("a line break inside an include directory must be refused"),
            Err(error) => error,
        };
        assert!(error.to_string().contains("line break"), "{error}");
        let error = match write_context_file(&[], &[], &[PathBuf::from("/project/a\rb")]) {
            Ok(_) => panic!("a line break inside a session file must be refused"),
            Err(error) => error,
        };
        assert!(error.to_string().contains("line break"), "{error}");
    }

    #[test]
    fn a_define_with_a_line_break_is_refused() {
        let error = match write_context_file(&[], &["A=1\n-DEVIL".to_string()], &[]) {
            Ok(_) => panic!("a line break inside a define must be refused"),
            Err(error) => error,
        };
        assert!(error.to_string().contains("line break"), "{error}");
    }

    /// N1: a write/flush error after the private file exists must remove the
    /// partial file AND its directory, not leave the 0600 artefact behind.
    #[test]
    fn a_failed_context_write_removes_the_partial_file_and_its_directory() -> Result<()> {
        let base = std::env::temp_dir().join(format!(
            "vedit-context-fail-{}-{:x}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&base)?;
        let partial = std::cell::RefCell::new(None);
        let result = create_context_file_with(&base, "-D\"WIDTH=8\"\n", |path, _text| {
            std::fs::write(path, "partial")?;
            *partial.borrow_mut() = Some(path.to_path_buf());
            Err(std::io::Error::new(
                std::io::ErrorKind::StorageFull,
                "simulated write failure",
            ))
        });
        let path = partial.into_inner().ok_or_else(|| {
            anyhow::anyhow!("the writer must have been called with the context path")
        })?;
        assert!(
            result.is_err(),
            "the simulated write failure must be reported"
        );
        assert!(!path.exists(), "the partial context file must be removed");
        let directory = path
            .parent()
            .ok_or_else(|| anyhow::anyhow!("the context file must have a parent"))?;
        assert!(
            !directory.exists(),
            "the private context directory must be removed too"
        );
        assert!(
            base.exists(),
            "the base directory is not the context directory"
        );
        let _ = std::fs::remove_dir_all(&base);
        Ok(())
    }

    #[test]
    fn the_runtime_dir_is_preferred_and_a_bad_one_falls_back_to_temp() -> Result<()> {
        let temp = std::env::temp_dir();
        assert_eq!(
            context_base_dirs_from(None, temp.clone()),
            vec![temp.clone()]
        );
        assert_eq!(
            context_base_dirs_from(Some(OsStr::new("/does/not/exist")), temp.clone()),
            vec![temp.clone()]
        );
        assert_eq!(
            context_base_dirs_from(Some(OsStr::new("relative/path")), temp.clone()),
            vec![temp.clone()]
        );
        assert_eq!(
            context_base_dirs_from(Some(temp.as_os_str()), temp.clone()),
            vec![temp.clone()]
        );
        let runtime = temp.join(format!(
            "vedit-context-runtime-{}-{:x}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&runtime)?;
        assert_eq!(
            context_base_dirs_from(Some(runtime.as_os_str()), temp.clone()),
            vec![runtime.clone(), temp.clone()]
        );
        let _ = std::fs::remove_dir_all(&runtime);
        Ok(())
    }

    #[test]
    fn the_context_directory_and_file_are_private_and_removed_by_the_guard() -> Result<()> {
        let context = write_context_file(&[], &["WIDTH=8".to_string()], &[])?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let directory = std::fs::symlink_metadata(&context.directory)?;
            assert!(directory.file_type().is_dir());
            assert_eq!(directory.permissions().mode() & 0o777, 0o700);
            let file = std::fs::symlink_metadata(&context.path)?;
            assert!(!file.file_type().is_symlink());
            assert_eq!(file.permissions().mode() & 0o777, 0o600);
        }
        let (path, directory) = (context.path.clone(), context.directory.clone());
        drop(context);
        assert!(!path.exists(), "the guard must remove the context file");
        assert!(
            !directory.exists(),
            "the guard must remove the private directory too"
        );
        Ok(())
    }

    #[cfg(unix)]
    #[test]
    fn a_planted_symlink_is_not_followed_or_truncated() -> Result<()> {
        let base = std::env::temp_dir().join(format!(
            "vedit-context-link-{}-{:x}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&base)?;
        let victim = base.join("victim.txt");
        std::fs::write(&victim, "ORIGINAL\n")?;
        let link = base.join("context.f");
        std::os::unix::fs::symlink(&victim, &link)?;
        let error = match create_private_file(&link, "-DWIDTH=8\n") {
            Ok(()) => panic!("create_new must refuse a pre-planted symlink"),
            Err(error) => error,
        };
        assert_eq!(error.kind(), std::io::ErrorKind::AlreadyExists);
        assert_eq!(std::fs::read_to_string(&victim)?, "ORIGINAL\n");
        let _ = std::fs::remove_dir_all(&base);
        Ok(())
    }
}
