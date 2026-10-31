//! Verible LSP client (service 2 of 7).
//!
//! Slice 0 fixes the entry point and its error contract; the session operations
//! (open/change document, definition, references, diagnostics, format) belong to
//! the verible-lsp task, which owns the async lifecycle in `vd-lsp`.
//!
//! What exists here is the **handshake of the seam**: given the server binary,
//! the workspace root and an optional `.rules.verible_lint`, build the
//! `vd-lsp::LspConfig` the client would start with and report it. Constructing
//! `LspClient` is lazy by design (`vd-lsp`'s "spawns nothing" guarantee), so a
//! status poll can never start a process on the UI thread.

use crate::capi::{
    c_str, c_string, c_string_list, free_string, free_string_list, VdError, VdLspInfo,
    VdStringList, VD_ERR_INVALID_ARGUMENT, VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_STATUS_FAILED,
    VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::collections::HashMap;
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use vd_lsp::{
    range_parts, LspConfig, PollOutcome as LspPollOutcome, SessionHandle, SessionLocation,
    SessionRange, SessionRequest, SessionResponse, SessionStatus, SymbolNode, WorkspaceTextEdit,
};

/// What one LSP session probe reports.
pub(crate) struct LspData {
    /// 1 configured (lazy), 2 error.
    pub(crate) state: i32,
    /// The server binary the client would start.
    pub(crate) server: String,
    /// Human-readable status line.
    pub(crate) message: String,
}

impl LspData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned strings must be released with `vd_lsp_info_free`.
    pub(crate) unsafe fn into_c(self) -> VdLspInfo {
        VdLspInfo {
            state: self.state,
            server: c_string(&self.server),
            message: c_string(&self.message),
        }
    }
}

/// Validate the inputs and build the (not yet started) LSP session description.
pub(crate) fn configure(
    ls_path: &Path,
    workspace_root: &Path,
    rules: Option<&Path>,
) -> Result<LspData, VdError> {
    if !ls_path.is_file() {
        return Err(VdError::new(
            VD_ERR_NOT_FOUND,
            &format!("verible-verilog-ls not found at {}", ls_path.display()),
        ));
    }
    if !workspace_root.is_dir() {
        return Err(VdError::new(
            VD_ERR_NOT_FOUND,
            &format!(
                "workspace root is not a directory: {}",
                workspace_root.display()
            ),
        ));
    }
    if let Some(rules) = rules {
        if !rules.is_file() {
            return Err(VdError::new(
                VD_ERR_NOT_FOUND,
                &format!(".rules.verible_lint not found at {}", rules.display()),
            ));
        }
    }
    let mut config = vd_lsp::LspConfig::new(ls_path, workspace_root);
    if let Some(rules) = rules {
        config = config.with_rules_config(rules);
    }
    let argument_count = config.command_args().len();
    let client = vd_lsp::LspClient::new(config);
    Ok(LspData {
        state: 1,
        server: ls_path.display().to_string(),
        message: format!(
            "configured lazily: {argument_count} server argument(s), server_running={}",
            client.is_running()
        ),
    })
}

/// Start an LSP configuration job.
///
/// `rules_path` may be null. Returns the job id, or `0` when the arguments were
/// rejected.
///
/// # Safety
///
/// `service` must be null or live; every `*const c_char` must be null or a valid
/// NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_start(
    service: *mut VdService,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(ls_path), Some(workspace_root)) = (
        crate::capi::c_str(ls_path),
        crate::capi::c_str(workspace_root),
    ) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "ls_path and workspace_root must be valid UTF-8 paths",
        ));
        return 0;
    };
    let rules = if rules_path.is_null() {
        None
    } else {
        match crate::capi::c_str(rules_path) {
            Some(rules) => Some(PathBuf::from(rules)),
            None => {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "rules_path must be a valid UTF-8 path or null",
                ));
                return 0;
            }
        }
    };
    let ls_path = PathBuf::from(ls_path);
    let workspace_root = PathBuf::from(workspace_root);
    state.start(move || configure(&ls_path, &workspace_root, rules.as_deref()).map(JobValue::Lsp))
}

/// Poll an LSP job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdLspInfo`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdLspInfo,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::Lsp(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Release a filled [`VdLspInfo`]; null-safe.
///
/// # Safety
///
/// `info` must be null or point to a struct filled by [`vd_lsp_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_info_free(info: *mut VdLspInfo) {
    if info.is_null() {
        return;
    }
    let info = &mut *info;
    free_string(info.server);
    free_string(info.message);
    info.server = std::ptr::null_mut();
    info.message = std::ptr::null_mut();
    info.state = 0;
}

// ---------------------------------------------------------------------------
// T8 session service: one effective file set per session
// ---------------------------------------------------------------------------
//
// The Slice-0 entry points above (`vd_lsp_start`/`vd_lsp_poll`) configure a
// session lazily through the generic job registry; they remain the handshake
// probe. Everything below is the session the Qt diagnostics lane uses:
//
//   * it receives the **same** file list the project model produced
//     (`vd_project_poll` -> `VdProjectInfo.files`), so Verible and SLANG are fed
//     one effective set (A3c);
//   * it exposes `files_expected` / `files_opened` / `skipped` / phase /
//     diagnostics generation, so a file-set change is observable (A3d);
//   * every operation is a polled request; nothing blocks the UI thread (C2).
//
// Sessions live in a process-global registry keyed by an id. `service` is used
// only to report errors (`vd_service_error`); a session outlives a destroyed
// service handle by design, and the caller releases it with
// `vd_lsp_session_free`.

/// Request kind: `textDocument/definition`.
pub const VD_LSP_REQ_DEFINITION: i32 = 1;
/// Request kind: `textDocument/references`.
pub const VD_LSP_REQ_REFERENCES: i32 = 2;
/// Request kind: `textDocument/documentSymbol` (the outline).
pub const VD_LSP_REQ_SYMBOLS: i32 = 3;
/// Request kind: `textDocument/rename`.
pub const VD_LSP_REQ_RENAME: i32 = 4;
/// Request kind: `textDocument/formatting`.
pub const VD_LSP_REQ_FORMAT: i32 = 5;
/// Request kind: diagnostics currently recorded for one file.
pub const VD_LSP_REQ_DIAGNOSTICS: i32 = 6;

/// Response kind: locations (definition / references).
pub const VD_LSP_RES_LOCATIONS: i32 = 1;
/// Response kind: outline symbols.
pub const VD_LSP_RES_SYMBOLS: i32 = 2;
/// Response kind: text edits (formatting / rename).
pub const VD_LSP_RES_EDITS: i32 = 3;
/// Response kind: diagnostics.
pub const VD_LSP_RES_DIAGNOSTICS: i32 = 4;

/// Zero-based start/end position of a location, symbol or edit.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspPositionRange {
    /// Start line (zero-based).
    pub start_line: u32,
    /// Start UTF-16 character (zero-based).
    pub start_character: u32,
    /// End line (zero-based).
    pub end_line: u32,
    /// End UTF-16 character (zero-based).
    pub end_character: u32,
}

/// One location returned by definition/references.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspLocation {
    /// Target document URI (`file://…`).
    pub uri: *mut c_char,
    /// Range inside the target document.
    pub range: VdLspPositionRange,
}

/// Array of locations.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspLocationList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of locations; null when `count` is zero.
    pub items: *mut VdLspLocation,
}

impl VdLspLocationList {
    const fn empty() -> Self {
        Self {
            count: 0,
            items: ptr::null_mut(),
        }
    }
}

/// One outline entry.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspSymbol {
    /// Symbol name.
    pub name: *mut c_char,
    /// Detail text, or null.
    pub detail: *mut c_char,
    /// LSP `SymbolKind` number.
    pub kind: i32,
    /// Selection range.
    pub range: VdLspPositionRange,
}

/// Array of outline entries.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspSymbolList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of symbols; null when `count` is zero.
    pub items: *mut VdLspSymbol,
}

impl VdLspSymbolList {
    const fn empty() -> Self {
        Self {
            count: 0,
            items: ptr::null_mut(),
        }
    }
}

/// One text edit to apply.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspTextEdit {
    /// Target document URI.
    pub uri: *mut c_char,
    /// Range replaced by `new_text`.
    pub range: VdLspPositionRange,
    /// Replacement text.
    pub new_text: *mut c_char,
}

/// Array of text edits.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspTextEditList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of edits; null when `count` is zero.
    pub items: *mut VdLspTextEdit,
}

impl VdLspTextEditList {
    const fn empty() -> Self {
        Self {
            count: 0,
            items: ptr::null_mut(),
        }
    }
}

/// One diagnostic.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspDiagnostic {
    /// 1 error, 2 warning, 3 information, 4 hint, 0 unknown.
    pub severity: i32,
    /// Diagnostic range.
    pub range: VdLspPositionRange,
    /// Rule/code, or null.
    pub code: *mut c_char,
    /// Producing tool, or null.
    pub source: *mut c_char,
    /// Message text.
    pub message: *mut c_char,
}

/// Array of diagnostics.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspDiagnosticList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of diagnostics; null when `count` is zero.
    pub items: *mut VdLspDiagnostic,
}

impl VdLspDiagnosticList {
    const fn empty() -> Self {
        Self {
            count: 0,
            items: ptr::null_mut(),
        }
    }
}

/// Observable state of one session (A3d).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspStatus {
    /// 0 starting, 1 ready, 2 failed, 3 stopped.
    pub phase: i32,
    /// Files the caller handed in (the project model's effective set).
    pub files_expected: usize,
    /// Files actually opened on the server.
    pub files_opened: usize,
    /// Diagnostics currently recorded across the open documents.
    pub diagnostics: usize,
    /// Diagnostics generation counter.
    pub generation: u64,
    /// Server PID, or 0 when no process is running.
    pub server_pid: u32,
    /// How many times a crashed server was restarted.
    pub restarts: u32,
    /// Human-readable state line.
    pub message: *mut c_char,
    /// `path: reason` for files that could not be opened.
    pub skipped: VdStringList,
}

/// One polled response. Only the list matching `kind` is filled.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspResponse {
    /// 1 locations, 2 symbols, 3 edits, 4 diagnostics.
    pub kind: i32,
    /// Human-readable note (may be null).
    pub message: *mut c_char,
    /// Locations, when `kind` is 1.
    pub locations: VdLspLocationList,
    /// Symbols, when `kind` is 2.
    pub symbols: VdLspSymbolList,
    /// Edits, when `kind` is 3.
    pub edits: VdLspTextEditList,
    /// Diagnostics, when `kind` is 4.
    pub diagnostics: VdLspDiagnosticList,
}

impl VdLspResponse {
    const fn empty() -> Self {
        Self {
            kind: 0,
            message: ptr::null_mut(),
            locations: VdLspLocationList::empty(),
            symbols: VdLspSymbolList::empty(),
            edits: VdLspTextEditList::empty(),
            diagnostics: VdLspDiagnosticList::empty(),
        }
    }
}

struct SessionSlot {
    handle: SessionHandle,
}

fn sessions() -> MutexGuard<'static, HashMap<u64, Arc<SessionSlot>>> {
    static SESSIONS: OnceLock<Mutex<HashMap<u64, Arc<SessionSlot>>>> = OnceLock::new();
    let sessions = SESSIONS.get_or_init(|| Mutex::new(HashMap::new()));
    sessions
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

static NEXT_SESSION: AtomicU64 = AtomicU64::new(1);

fn session_for(id: u64) -> Option<Arc<SessionSlot>> {
    sessions().get(&id).cloned()
}

fn position_range(range: SessionRange) -> VdLspPositionRange {
    let (start_line, start_character, end_line, end_character) = range_parts(range);
    VdLspPositionRange {
        start_line,
        start_character,
        end_line,
        end_character,
    }
}

fn status_to_c(status: SessionStatus) -> VdLspStatus {
    let skipped: Vec<String> = status
        .skipped
        .iter()
        .map(|file| format!("{}: {}", file.path.display(), file.reason))
        .collect();
    VdLspStatus {
        phase: status.phase.code(),
        files_expected: status.files_expected,
        files_opened: status.files_opened,
        diagnostics: status.diagnostics,
        generation: status.generation,
        server_pid: status.server_pid.unwrap_or(0),
        restarts: status.restarts,
        message: c_string(&status.message),
        // SAFETY: c_string_list only allocates C strings and transfers their
        // ownership to the returned list, which the caller frees with
        // `vd_lsp_status_free`.
        skipped: unsafe { c_string_list(&skipped) },
    }
}

fn locations_to_c(locations: Vec<SessionLocation>) -> VdLspLocationList {
    if locations.is_empty() {
        return VdLspLocationList::empty();
    }
    let items: Vec<VdLspLocation> = locations
        .into_iter()
        .map(|location| VdLspLocation {
            uri: c_string(location.uri.as_str()),
            range: position_range(location.range),
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdLspLocationList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdLspLocation,
    }
}

fn symbols_to_c(symbols: Vec<SymbolNode>) -> VdLspSymbolList {
    if symbols.is_empty() {
        return VdLspSymbolList::empty();
    }
    let items: Vec<VdLspSymbol> = symbols
        .into_iter()
        .map(|symbol| VdLspSymbol {
            name: c_string(&symbol.name),
            detail: symbol.detail.as_deref().map_or(ptr::null_mut(), c_string),
            kind: symbol.kind,
            range: position_range(symbol.range),
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdLspSymbolList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdLspSymbol,
    }
}

fn edits_to_c(edits: Vec<WorkspaceTextEdit>) -> VdLspTextEditList {
    if edits.is_empty() {
        return VdLspTextEditList::empty();
    }
    let items: Vec<VdLspTextEdit> = edits
        .into_iter()
        .map(|edit| VdLspTextEdit {
            uri: c_string(edit.uri.as_str()),
            range: position_range(edit.edit.range),
            new_text: c_string(&edit.edit.new_text),
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdLspTextEditList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdLspTextEdit,
    }
}

fn diagnostics_to_c(diagnostics: Vec<vd_lsp::SessionDiagnostic>) -> VdLspDiagnosticList {
    if diagnostics.is_empty() {
        return VdLspDiagnosticList::empty();
    }
    let items: Vec<VdLspDiagnostic> = diagnostics
        .into_iter()
        .map(|diagnostic| VdLspDiagnostic {
            severity: diagnostic.severity.unwrap_or(0),
            range: position_range(diagnostic.range),
            code: diagnostic.code.as_deref().map_or(ptr::null_mut(), c_string),
            source: diagnostic
                .source
                .as_deref()
                .map_or(ptr::null_mut(), c_string),
            message: c_string(&diagnostic.message),
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdLspDiagnosticList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdLspDiagnostic,
    }
}

fn response_to_c(response: SessionResponse) -> VdLspResponse {
    let mut out = VdLspResponse::empty();
    match response {
        SessionResponse::Locations(locations) => {
            out.kind = VD_LSP_RES_LOCATIONS;
            out.locations = locations_to_c(locations);
        }
        SessionResponse::Symbols(symbols) => {
            out.kind = VD_LSP_RES_SYMBOLS;
            out.symbols = symbols_to_c(symbols);
        }
        SessionResponse::Edits(edits) => {
            out.kind = VD_LSP_RES_EDITS;
            out.edits = edits_to_c(edits);
        }
        SessionResponse::Diagnostics(diagnostics) => {
            out.kind = VD_LSP_RES_DIAGNOSTICS;
            out.diagnostics = diagnostics_to_c(diagnostics);
        }
    }
    out
}

/// Read a `VdStringList` of paths into owned `PathBuf`s.
///
/// # Safety
///
/// `files` must be null or point to a readable `VdStringList` whose `items`
/// array holds `count` valid NUL-terminated strings for the duration of the
/// call.
unsafe fn read_file_list(files: *const VdStringList) -> Result<Vec<PathBuf>, String> {
    if files.is_null() {
        return Err("files must not be null".to_string());
    }
    // SAFETY: the caller contract guarantees a readable VdStringList.
    let list = unsafe { *files };
    if list.count == 0 {
        return Ok(Vec::new());
    }
    if list.items.is_null() {
        return Err("files.items must not be null when files.count > 0".to_string());
    }
    if list.count > 100_000 {
        return Err("the effective file set is implausibly large".to_string());
    }
    // SAFETY: the caller contract guarantees `items` points to `count` valid
    // NUL-terminated strings.
    let items = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    let mut paths = Vec::with_capacity(list.count);
    for item in items {
        // SAFETY: each `*item` is one of the validated NUL-terminated strings.
        let Some(text) = (unsafe { c_str(*item) }) else {
            return Err("every file in the effective set must be valid UTF-8".to_string());
        };
        paths.push(PathBuf::from(text));
    }
    Ok(paths)
}

/// Start an LSP session over the effective file set.
///
/// `ls_path` must be the Verible language-server binary, `workspace_root` the
/// project root; `rules_path` may be null; `files` must point to the file list
/// the project model produced (the same list the caller passes to SLANG).
///
/// Returns a session id, or `0` when the arguments were rejected; the reason is
/// then readable with `vd_service_error`. No process is started here: the
/// worker starts the server on its first `didOpen`, so this call cannot block
/// the UI thread (C2).
///
/// # Safety
///
/// `service` must be null or live; the `*const c_char` arguments must be null or
/// valid NUL-terminated strings; `files` must satisfy [`read_file_list`]'s
/// contract.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_start(
    service: *mut VdService,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
    files: *const VdStringList,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(ls_path), Some(workspace_root)) = (
        // SAFETY: the caller supplies valid NUL-terminated strings or null.
        (unsafe { c_str(ls_path) }),
        // SAFETY: same contract for workspace_root.
        (unsafe { c_str(workspace_root) }),
    ) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "ls_path and workspace_root must be valid UTF-8 paths",
        ));
        return 0;
    };
    let rules = if rules_path.is_null() {
        None
    } else {
        // SAFETY: a non-null rules_path is documented as a valid string.
        match unsafe { c_str(rules_path) } {
            Some(rules) if !rules.is_empty() => Some(PathBuf::from(rules)),
            _ => {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "rules_path must be a valid non-empty UTF-8 path or null",
                ));
                return 0;
            }
        }
    };
    // SAFETY: the caller contract for `files` is stated on read_file_list.
    let file_set = match unsafe { read_file_list(files) } {
        Ok(paths) => paths,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let mut config = LspConfig::new(Path::new(ls_path), Path::new(workspace_root));
    if let Some(rules) = rules {
        config = config.with_rules_config(rules);
    }
    match SessionHandle::start(config, file_set) {
        Ok(handle) => {
            let id = NEXT_SESSION.fetch_add(1, Ordering::Relaxed);
            sessions().insert(id, Arc::new(SessionSlot { handle }));
            id
        }
        Err(error) => {
            state.record_error(VdError::new(
                VD_ERR_SERVICE,
                &format!("cannot start the LSP session: {error}"),
            ));
            0
        }
    }
}

/// Read the observable state of one session (A3d).
///
/// Returns `VD_STATUS_READY` with `out` filled, or `POLL_MISUSE` when the id is
/// unknown or `out` is null. The status is a snapshot: it never waits for the
/// server.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdLspStatus`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_status(
    service: *mut VdService,
    session: u64,
    out: *mut VdLspStatus,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(
            VD_ERR_NOT_FOUND,
            "unknown LSP session id (it was never started or was already freed)",
        ));
        return POLL_MISUSE;
    };
    let status = slot.handle.status();
    // SAFETY: `out` is writable for one VdLspStatus per the caller contract.
    unsafe { *out = status_to_c(status) };
    VD_STATUS_READY
}

/// Release a filled [`VdLspStatus`]; null-safe and idempotent.
///
/// # Safety
///
/// `status` must be null or point to a struct filled by
/// [`vd_lsp_session_status`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_status_free(status: *mut VdLspStatus) {
    if status.is_null() {
        return;
    }
    // SAFETY: the caller contract says `status` is a filled, unfreed struct.
    let status = unsafe { &mut *status };
    // SAFETY: message/skipped were produced by this module and are not freed yet.
    unsafe {
        free_string(status.message);
        free_string_list(&mut status.skipped);
    }
    status.message = ptr::null_mut();
    status.phase = 0;
}

/// Queue one request on a session.
///
/// `kind` is one of the `VD_LSP_REQ_*` constants. `file` is required for every
/// kind; `line`/`character` are zero-based and used by definition, references
/// and rename; `new_name` is used only by rename.
///
/// Returns a request id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `file` and `new_name` must be null or valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_request(
    service: *mut VdService,
    session: u64,
    kind: i32,
    file: *const c_char,
    line: u32,
    character: u32,
    new_name: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(VD_ERR_NOT_FOUND, "unknown LSP session id"));
        return 0;
    };
    // SAFETY: the caller supplies a valid NUL-terminated string or null.
    let Some(file) = (unsafe { c_str(file) }) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "file must be a valid UTF-8 path",
        ));
        return 0;
    };
    let file = PathBuf::from(file);
    let request = match kind {
        VD_LSP_REQ_DEFINITION => SessionRequest::Definition {
            file,
            line,
            character,
        },
        VD_LSP_REQ_REFERENCES => SessionRequest::References {
            file,
            line,
            character,
        },
        VD_LSP_REQ_SYMBOLS => SessionRequest::DocumentSymbols { file },
        VD_LSP_REQ_RENAME => {
            // SAFETY: new_name is documented as a valid string or null.
            let Some(new_name) = (unsafe { c_str(new_name) }) else {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "rename requires a valid UTF-8 new_name",
                ));
                return 0;
            };
            if new_name.is_empty() {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "rename requires a non-empty new_name",
                ));
                return 0;
            }
            SessionRequest::Rename {
                file,
                line,
                character,
                new_name: new_name.to_string(),
            }
        }
        VD_LSP_REQ_FORMAT => SessionRequest::Formatting { file },
        VD_LSP_REQ_DIAGNOSTICS => SessionRequest::Diagnostics { file },
        other => {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                &format!("unknown LSP request kind {other}"),
            ));
            return 0;
        }
    };
    match slot.handle.request(request) {
        Ok(id) => id,
        Err(error) => {
            state.record_error(VdError::new(
                VD_ERR_SERVICE,
                &format!("cannot queue the LSP request: {error}"),
            ));
            0
        }
    }
}

/// Poll one request.
///
/// `VD_STATUS_PENDING` while the worker is busy, `VD_STATUS_READY` with `out`
/// filled, `VD_STATUS_FAILED` with the reason in `vd_service_error`, or
/// `POLL_MISUSE` for an unknown/consumed id or a null `out`.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdLspResponse`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_request_poll(
    service: *mut VdService,
    session: u64,
    request: u64,
    out: *mut VdLspResponse,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(VD_ERR_NOT_FOUND, "unknown LSP session id"));
        return POLL_MISUSE;
    };
    match slot.handle.poll(request) {
        LspPollOutcome::Pending => VD_STATUS_PENDING,
        LspPollOutcome::Failed(message) => {
            state.record_error(VdError::new(VD_ERR_SERVICE, &message));
            VD_STATUS_FAILED
        }
        LspPollOutcome::Unknown => {
            state.record_error(VdError::new(
                VD_ERR_NOT_FOUND,
                "unknown LSP request id (already polled or never queued)",
            ));
            POLL_MISUSE
        }
        LspPollOutcome::Ready(response) => {
            // SAFETY: `out` is writable for one VdLspResponse per the contract.
            unsafe { *out = response_to_c(response) };
            VD_STATUS_READY
        }
    }
}

/// Release a filled [`VdLspResponse`]; null-safe and idempotent.
///
/// # Safety
///
/// `response` must be null or point to a struct filled by
/// [`vd_lsp_request_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_response_free(response: *mut VdLspResponse) {
    if response.is_null() {
        return;
    }
    // SAFETY: the caller contract says `response` is filled and not freed yet.
    let response = unsafe { &mut *response };
    // SAFETY: every pointer below was produced by this module for this response
    // and is not freed yet.
    unsafe {
        free_string(response.message);
        free_locations(&mut response.locations);
        free_symbols(&mut response.symbols);
        free_edits(&mut response.edits);
        free_diagnostics(&mut response.diagnostics);
    }
    *response = VdLspResponse::empty();
}

/// Ask a session to shut its server down. Non-blocking and idempotent.
///
/// Returns `0` when the request was delivered, `POLL_MISUSE` for an unknown id.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_stop(service: *mut VdService, session: u64) -> i32 {
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(VD_ERR_NOT_FOUND, "unknown LSP session id"));
        return POLL_MISUSE;
    };
    slot.handle.stop();
    0
}

/// Stop and forget a session (the worker is asked to shut down before the slot
/// is dropped). Null-safe for an unknown id.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_free(service: *mut VdService, session: u64) {
    let _ = service;
    if let Some(slot) = sessions().remove(&session) {
        slot.handle.stop();
    }
}

// SAFETY: each free_* helper below takes the exact allocation shape produced by
// the matching `*_to_c` conversion (a Box<[T]> with `count > 0` and a non-null
// `items`, or the empty list with a null `items`), and it frees every owned
// string before the array itself.
unsafe fn free_locations(list: &mut VdLspLocationList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: `items`/`count` come from locations_to_c's Box<[VdLspLocation]>.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for location in slice.iter_mut() {
        // SAFETY: each uri was produced by c_string and is not freed yet.
        unsafe { free_string(location.uri) };
        location.uri = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdLspLocation]> that was leaked.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}

unsafe fn free_symbols(list: &mut VdLspSymbolList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: `items`/`count` come from symbols_to_c's Box<[VdLspSymbol]>.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for symbol in slice.iter_mut() {
        // SAFETY: name/detail were produced by c_string or are null.
        unsafe {
            free_string(symbol.name);
            free_string(symbol.detail);
        }
        symbol.name = ptr::null_mut();
        symbol.detail = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdLspSymbol]> that was leaked.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}

unsafe fn free_edits(list: &mut VdLspTextEditList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: `items`/`count` come from edits_to_c's Box<[VdLspTextEdit]>.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for edit in slice.iter_mut() {
        // SAFETY: uri/new_text were produced by c_string and are not freed yet.
        unsafe {
            free_string(edit.uri);
            free_string(edit.new_text);
        }
        edit.uri = ptr::null_mut();
        edit.new_text = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdLspTextEdit]> that was leaked.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}

unsafe fn free_diagnostics(list: &mut VdLspDiagnosticList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: `items`/`count` come from diagnostics_to_c's Box<[VdLspDiagnostic]>.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for diagnostic in slice.iter_mut() {
        // SAFETY: code/source/message were produced by c_string or are null.
        unsafe {
            free_string(diagnostic.code);
            free_string(diagnostic.source);
            free_string(diagnostic.message);
        }
        diagnostic.code = ptr::null_mut();
        diagnostic.source = ptr::null_mut();
        diagnostic.message = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdLspDiagnostic]> that was leaked.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}
