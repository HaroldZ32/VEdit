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
    c_str, c_string, c_string_list, free_string, free_string_list, VdError, VdLspCapabilities,
    VdLspCapability, VdLspInfo, VdStringList, VD_ERR_INVALID_ARGUMENT, VD_ERR_NOT_FOUND,
    VD_ERR_SERVICE, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::collections::HashMap;
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use vd_lsp::{
    capabilities, filelist_text, range_parts, LspBackend, LspConfig, PollOutcome as LspPollOutcome,
    SessionHandle, SessionLocation, SessionRange, SessionRequest, SessionResponse, SessionStatus,
    SymbolNode, WorkspaceTextEdit,
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

/// Request kind: `textDocument/hover` (T16).
pub const VD_LSP_REQ_HOVER: i32 = 7;

/// Request kind: `textDocument/signatureHelp` (T16).
pub const VD_LSP_REQ_SIGNATURE_HELP: i32 = 8;

/// Response kind: hover text (`text` + `range`).
pub const VD_LSP_RES_HOVER: i32 = 5;

/// Response kind: signature help (`signatures` + active indexes).
pub const VD_LSP_RES_SIGNATURE_HELP: i32 = 6;

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

/// One call signature of a `SignatureHelp` answer (T16).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspSignature {
    /// The signature as the server sent it (`foo(int a, int b)`).
    pub label: *mut c_char,
    /// Parameter labels, in order.
    pub parameters: VdStringList,
}

impl VdLspSignature {
    const fn empty() -> Self {
        Self {
            label: ptr::null_mut(),
            parameters: VdStringList::empty(),
        }
    }
}

/// Array of call signatures.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspSignatureList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of signatures; null when `count` is zero.
    pub items: *mut VdLspSignature,
}

impl VdLspSignatureList {
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
    /// 1 LSP, 2 CLI, 3 both (the source column, AC-2.13). LSP rows are always 1.
    pub origin: i32,
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
    /// The backend this session drives: 0 verible, 1 slang (T138).
    pub backend: i32,
    /// The backend name for the status line (`backend=slang`).
    pub backend_name: *mut c_char,
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
    /// Hover text, when `kind` is 5 (may be null when the server sent nothing).
    pub text: *mut c_char,
    /// Hover range, when `kind` is 5 and the server supplied one.
    pub range: VdLspPositionRange,
    /// Signatures, when `kind` is 6.
    pub signatures: VdLspSignatureList,
    /// Active signature index, when `kind` is 6 (`-1` = none).
    pub active_signature: i32,
    /// Active parameter index, when `kind` is 6 (`-1` = none).
    pub active_parameter: i32,
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
            text: ptr::null_mut(),
            range: VdLspPositionRange {
                start_line: 0,
                start_character: 0,
                end_line: 0,
                end_character: 0,
            },
            signatures: VdLspSignatureList::empty(),
            active_signature: -1,
            active_parameter: -1,
        }
    }
}

struct SessionSlot {
    handle: SessionHandle,
    /// The generated Verible file list (`+incdir+` / `+define+` / files) this
    /// session was started with; deleted when the slot is released.
    filelist_path: Option<PathBuf>,
    /// The exact program + argv this session was started with (AC-2.5's dump).
    command_line: String,
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
        backend: status.backend.code(),
        backend_name: c_string(status.backend.as_str()),
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
            origin: 1,
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
        SessionResponse::Hover(hover) => {
            out.kind = VD_LSP_RES_HOVER;
            out.text = c_string(&hover.text);
            if let Some(range) = hover.range {
                let (start_line, start_character, end_line, end_character) = range_parts(range);
                out.range = VdLspPositionRange {
                    start_line,
                    start_character,
                    end_line,
                    end_character,
                };
            }
        }
        SessionResponse::SignatureHelp(help) => {
            out.kind = VD_LSP_RES_SIGNATURE_HELP;
            out.signatures = signatures_to_c(help.signatures);
            out.active_signature = help.active_signature.map_or(-1, |value| value as i32);
            out.active_parameter = help.active_parameter.map_or(-1, |value| value as i32);
        }
    }
    out
}

fn signatures_to_c(signatures: Vec<vd_lsp::SignatureInfo>) -> VdLspSignatureList {
    if signatures.is_empty() {
        return VdLspSignatureList::empty();
    }
    let mut items: Vec<VdLspSignature> = signatures
        .into_iter()
        .map(|signature| {
            // SAFETY: the list contract is the one every other `*_to_c` helper
            // uses (the caller owns the strings until the `_free` entry point).
            let parameters = unsafe { c_string_list(&signature.parameters) };
            VdLspSignature {
                label: c_string(&signature.label),
                parameters,
            }
        })
        .collect();
    let list = VdLspSignatureList {
        count: items.len(),
        items: items.as_mut_ptr(),
    };
    std::mem::forget(items);
    list
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

/// The shared body of the two session-start entry points.
///
/// `backend` selects the server; `include_dirs` / `defines` are the project's
/// tool settings. For the verible backend they are materialised into a Verible
/// file list (`+incdir+` / `+define+`) handed over with `--file_list_path`,
/// because `verible-verilog-ls` has no include/define flag; for the slang
/// backend they are passed as `-I` / `-D`. `rules_path` is verible-only.
///
/// # Safety
///
/// The pointer contract is the same as the public entry points: every
/// `*const c_char` is null or a valid NUL-terminated string, every
/// `*const VdStringList` satisfies [`read_file_list`]'s contract.
#[allow(clippy::too_many_arguments)] // the C entry points expose every field
unsafe fn start_session_impl(
    service: *mut VdService,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
    files: *const VdStringList,
    backend_code: i32,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(backend) = LspBackend::from_code(backend_code) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "backend must be 0 (verible) or 1 (slang); the session was not started",
        ));
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
    let include_dirs = if include_dirs.is_null() {
        Vec::new()
    } else {
        // SAFETY: a non-null list satisfies read_file_list's contract.
        match unsafe { read_file_list(include_dirs) } {
            Ok(dirs) => dirs,
            Err(message) => {
                state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
                return 0;
            }
        }
    };
    let defines = if defines.is_null() {
        Vec::new()
    } else {
        // SAFETY: a non-null list satisfies read_string_list's contract.
        match unsafe { read_string_list(defines) } {
            Ok(values) => values,
            Err(message) => {
                state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
                return 0;
            }
        }
    };
    let mut config =
        LspConfig::new(Path::new(ls_path), Path::new(workspace_root)).with_backend(backend);
    if let Some(rules) = rules {
        config = config.with_rules_config(rules);
    }
    for directory in &include_dirs {
        config = config.with_include_dir(directory);
    }
    for define in &defines {
        config = config.with_define(define);
    }
    let mut filelist_path = None;
    if backend == LspBackend::Verible
        && config.file_list_path.is_none()
        && (!include_dirs.is_empty() || !defines.is_empty())
    {
        match write_session_filelist(&file_set, &include_dirs, &defines) {
            Ok(path) => {
                config = config.with_file_list(&path);
                filelist_path = Some(path);
            }
            Err(message) => {
                state.record_error(VdError::new(VD_ERR_SERVICE, &message));
                return 0;
            }
        }
    }
    let command_line = config.command_line();
    match SessionHandle::start(config, file_set) {
        Ok(handle) => {
            let id = NEXT_SESSION.fetch_add(1, Ordering::Relaxed);
            sessions().insert(
                id,
                Arc::new(SessionSlot {
                    handle,
                    filelist_path,
                    command_line,
                }),
            );
            id
        }
        Err(error) => {
            if let Some(path) = &filelist_path {
                let _ = std::fs::remove_file(path);
            }
            state.record_error(VdError::new(
                VD_ERR_SERVICE,
                &format!("cannot start the LSP session: {error}"),
            ));
            0
        }
    }
}

/// Write the Verible file list for one session (a temp file, deleted on release).
fn write_session_filelist(
    files: &[PathBuf],
    include_dirs: &[PathBuf],
    defines: &[String],
) -> Result<PathBuf, String> {
    static NEXT: AtomicU64 = AtomicU64::new(1);
    let id = NEXT.fetch_add(1, Ordering::Relaxed);
    let path = std::env::temp_dir().join(format!("vedit-filelist-{}-{id}.f", std::process::id()));
    std::fs::write(&path, filelist_text(files, include_dirs, defines)).map_err(|error| {
        format!(
            "cannot write the Verible file list {}: {error}",
            path.display()
        )
    })?;
    Ok(path)
}

/// Start an LSP session over the effective file set (verible backend).
///
/// Kept exactly as it was for the existing callers; it is equivalent to
/// [`vd_lsp_session_start_v2`] with `backend = 0` and no include dirs/defines.
///
/// # Safety
///
/// See [`start_session_impl`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_start(
    service: *mut VdService,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
    files: *const VdStringList,
) -> u64 {
    // SAFETY: the caller contract of this function is start_session_impl's.
    unsafe {
        start_session_impl(
            service,
            ls_path,
            workspace_root,
            rules_path,
            files,
            LspBackend::Verible.code(),
            ptr::null(),
            ptr::null(),
        )
    }
}

/// Start an LSP session with an explicit backend and the project settings (T138).
///
/// `backend` is `0` (verible) or `1` (slang). `include_dirs` / `defines` may be
/// null; when given, they reach the selected backend (see [`start_session_impl`]).
/// Returns a session id, or `0` when the arguments were rejected (the reason is
/// then readable with `vd_service_error`). Nothing is spawned here.
///
/// # Safety
///
/// `service` must be null or live; the `*const c_char` arguments must be null or
/// valid NUL-terminated strings; the lists must satisfy [`read_file_list`]'s
/// contract.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_start_v2(
    service: *mut VdService,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
    files: *const VdStringList,
    backend: i32,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> u64 {
    // SAFETY: this function documents the same pointer contract.
    unsafe {
        start_session_impl(
            service,
            ls_path,
            workspace_root,
            rules_path,
            files,
            backend,
            include_dirs,
            defines,
        )
    }
}

/// Read a `VdStringList` of plain strings (defines) into owned `String`s.
///
/// # Safety
///
/// `list` must satisfy [`read_file_list`]'s contract.
unsafe fn read_string_list(list: *const VdStringList) -> Result<Vec<String>, String> {
    if list.is_null() {
        return Err("the list must not be null".to_string());
    }
    // SAFETY: the caller contract guarantees a readable VdStringList.
    let list = unsafe { *list };
    if list.count == 0 {
        return Ok(Vec::new());
    }
    if list.items.is_null() {
        return Err("items must not be null when count > 0".to_string());
    }
    // SAFETY: the caller contract guarantees `items` points to `count` valid
    // NUL-terminated strings.
    let items = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    let mut values = Vec::with_capacity(list.count);
    for item in items {
        // SAFETY: each `*item` is one of the validated NUL-terminated strings.
        let Some(text) = (unsafe { c_str(*item) }) else {
            return Err("every entry must be valid UTF-8".to_string());
        };
        values.push(text.to_string());
    }
    Ok(values)
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
        free_string(status.backend_name);
        free_string_list(&mut status.skipped);
    }
    status.message = ptr::null_mut();
    status.backend_name = ptr::null_mut();
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
        VD_LSP_REQ_HOVER => SessionRequest::Hover {
            file,
            line,
            character,
        },
        VD_LSP_REQ_SIGNATURE_HELP => SessionRequest::SignatureHelp {
            file,
            line,
            character,
        },
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

/// Replace the tracked text of one document with the caller's EDITOR BUFFER
/// (a full-document `didChange`). Nothing is read from disk here, so diagnostics
/// can follow unsaved edits; the document is opened first when the session does
/// not have it. The updated diagnostics arrive through the normal
/// `VD_LSP_REQ_DIAGNOSTICS` request (the server publishes them).
///
/// Returns `VD_STATUS_READY` on success; `POLL_MISUSE` when the session id is
/// unknown or the arguments are not valid UTF-8 strings (see
/// `vd_service_error` for the reason).
///
/// # Safety
///
/// `service` must be null or live; `path` and `text` must be valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_change(
    service: *mut VdService,
    session: u64,
    path: *const c_char,
    text: *const c_char,
) -> i32 {
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(VD_ERR_NOT_FOUND, "unknown LSP session id"));
        return POLL_MISUSE;
    };
    // SAFETY: the caller supplies valid NUL-terminated strings or null.
    let (Some(path), Some(text)) = ((unsafe { c_str(path) }), (unsafe { c_str(text) })) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "path and text must be valid UTF-8 strings",
        ));
        return POLL_MISUSE;
    };
    match slot
        .handle
        .change_document(Path::new(path), text.to_string())
    {
        Ok(()) => VD_STATUS_READY,
        Err(error) => {
            state.record_error(VdError::new(
                VD_ERR_SERVICE,
                &format!("cannot change the document: {error}"),
            ));
            POLL_MISUSE
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
        free_string(response.text);
        free_signatures(&mut response.signatures);
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
        if let Some(path) = &slot.filelist_path {
            let _ = std::fs::remove_file(path);
        }
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

unsafe fn free_signatures(list: &mut VdLspSignatureList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: `items`/`count` come from signatures_to_c's Vec<VdLspSignature>.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for signature in slice.iter_mut() {
        // SAFETY: label/parameters were produced by this module and are not freed.
        unsafe {
            free_string(signature.label);
            free_string_list(&mut signature.parameters);
        }
        *signature = VdLspSignature::empty();
    }
    // SAFETY: reconstructs the same Vec<VdLspSignature> that was leaked.
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

fn capabilities_to_c(table: vd_lsp::BackendCapabilities) -> VdLspCapabilities {
    let items: Vec<VdLspCapability> = table
        .actions
        .iter()
        .map(|row| VdLspCapability {
            action: c_string(row.action.name()),
            supported: row.support.code(),
            reason: c_string(&row.reason),
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdLspCapabilities {
        backend: c_string(table.backend.as_str()),
        basis: c_string(table.basis.as_str()),
        source: c_string(&table.source),
        note: c_string(&table.note),
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdLspCapability,
    }
}

/// Release a filled [`VdLspCapabilities`]; null-safe and idempotent.
///
/// # Safety
///
/// `capabilities` must be null or point to a struct filled by
/// [`vd_lsp_backend_capabilities`] / [`vd_lsp_session_capabilities`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_capabilities_free(capabilities: *mut VdLspCapabilities) {
    if capabilities.is_null() {
        return;
    }
    // SAFETY: the caller contract says this is a filled, unfreed struct.
    let capabilities = unsafe { &mut *capabilities };
    // SAFETY: every string was produced by c_string and is not freed yet.
    unsafe {
        free_string(capabilities.backend);
        free_string(capabilities.basis);
        free_string(capabilities.source);
        free_string(capabilities.note);
        if !capabilities.items.is_null() {
            let slice = &mut *ptr::slice_from_raw_parts_mut(capabilities.items, capabilities.count);
            for row in slice.iter_mut() {
                free_string(row.action);
                free_string(row.reason);
                row.action = ptr::null_mut();
                row.reason = ptr::null_mut();
            }
            drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
                capabilities.items,
                capabilities.count,
            )));
        }
    }
    capabilities.backend = ptr::null_mut();
    capabilities.basis = ptr::null_mut();
    capabilities.source = ptr::null_mut();
    capabilities.note = ptr::null_mut();
    capabilities.count = 0;
    capabilities.items = ptr::null_mut();
}

/// The static capability table of one backend (T138, AC-1.3).
///
/// `backend` is `0` (verible) or `1` (slang). The verible rows are measured; the
/// slang rows are `-1` (unknown) until a live session answers, because no
/// slang-server binary is built in this environment. Never touches a session.
///
/// # Safety
///
/// `out` must be null or writable for one [`VdLspCapabilities`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_backend_capabilities(
    backend: i32,
    out: *mut VdLspCapabilities,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(backend) = LspBackend::from_code(backend) else {
        return POLL_MISUSE;
    };
    // SAFETY: `out` is writable for one VdLspCapabilities per the caller contract.
    unsafe { *out = capabilities_to_c(capabilities(backend)) };
    VD_STATUS_READY
}

/// The capability table of a live session (T138).
///
/// Before the server answers `initialize` this equals the static table; after it
/// the rows are the server's own advertisements (`basis = live`).
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdLspCapabilities`].
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_capabilities(
    service: *mut VdService,
    session: u64,
    out: *mut VdLspCapabilities,
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
    // SAFETY: `out` is writable for one VdLspCapabilities per the caller contract.
    unsafe { *out = capabilities_to_c(slot.handle.capabilities()) };
    VD_STATUS_READY
}

/// The exact argv a session would use, as one owned string (AC-2.5's dump).
///
/// Spawns nothing. `file_list_path` may be null (the caller can generate one with
/// the project settings instead; the string then omits it). Returns null only
/// when `backend`, `ls_path` or `workspace_root` is invalid. Release the result
/// with `vd_string_free`.
///
/// # Safety
///
/// Every pointer argument must be null or valid for the duration of the call; the
/// two lists must satisfy [`read_file_list`]'s contract.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_command_line(
    backend: i32,
    ls_path: *const c_char,
    workspace_root: *const c_char,
    rules_path: *const c_char,
    file_list_path: *const c_char,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> *mut c_char {
    let Some(backend) = LspBackend::from_code(backend) else {
        return ptr::null_mut();
    };
    // SAFETY: the caller supplies valid NUL-terminated strings or null.
    let (Some(ls_path), Some(workspace_root)) =
        (unsafe { c_str(ls_path) }, unsafe { c_str(workspace_root) })
    else {
        return ptr::null_mut();
    };
    let mut config =
        LspConfig::new(Path::new(ls_path), Path::new(workspace_root)).with_backend(backend);
    // SAFETY: a non-null rules_path is documented as a valid string.
    if let Some(rules) = unsafe { c_str(rules_path) } {
        let rules = PathBuf::from(rules);
        config = config.with_rules_config(&rules);
    }
    // SAFETY: a non-null file_list_path is documented as a valid string.
    if let Some(list) = unsafe { c_str(file_list_path) } {
        config = config.with_file_list(list);
    }
    if !include_dirs.is_null() {
        // SAFETY: a non-null list satisfies read_file_list's contract.
        if let Ok(dirs) = unsafe { read_file_list(include_dirs) } {
            for directory in &dirs {
                config = config.with_include_dir(directory);
            }
        }
    }
    if !defines.is_null() {
        // SAFETY: a non-null list satisfies read_string_list's contract.
        if let Ok(values) = unsafe { read_string_list(defines) } {
            for value in &values {
                config = config.with_define(value);
            }
        }
    }
    c_string(&config.command_line())
}

/// The Verible file list this session would write for the project settings.
///
/// This is the AC-2.5 "config dump" for the verible backend: the project's
/// include dirs and defines reach `verible-verilog-ls` as `+incdir+` /
/// `+define+` lines in a `.f` passed with `--file_list_path`, because the server
/// has no include/define flag. Spawns and writes nothing. Returns an owned string
/// (release with `vd_string_free`) or null for invalid arguments.
///
/// # Safety
///
/// `files`, `include_dirs` and `defines` must be null or satisfy the
/// string-list contract.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_file_list_preview(
    files: *const VdStringList,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> *mut c_char {
    if files.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: each list is null or satisfies read_file_list/read_string_list.
    let files = match unsafe { read_file_list(files) } {
        Ok(files) => files,
        Err(_) => return ptr::null_mut(),
    };
    // SAFETY: same contract.
    let include_dirs = if include_dirs.is_null() {
        Vec::new()
    } else {
        match unsafe { read_file_list(include_dirs) } {
            Ok(dirs) => dirs,
            Err(_) => return ptr::null_mut(),
        }
    };
    // SAFETY: same contract.
    let defines = if defines.is_null() {
        Vec::new()
    } else {
        match unsafe { read_string_list(defines) } {
            Ok(values) => values,
            Err(_) => return ptr::null_mut(),
        }
    };
    c_string(&filelist_text(&files, &include_dirs, &defines))
}

/// The exact program plus argv one session was started with (AC-2.5's dump).
///
/// For the verible backend with project include dirs/defines this is where the
/// generated file list shows up as `--file_list_path=<path>`; the file itself
/// contains the `+incdir+` / `+define+` lines (`vd_lsp_file_list_preview` shows
/// the same text before a session exists). Returns an owned string (release with
/// `vd_string_free`), or null for an unknown session.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_lsp_session_command_line(
    service: *mut VdService,
    session: u64,
) -> *mut c_char {
    let Some(state) = service_state(service) else {
        return ptr::null_mut();
    };
    let Some(slot) = session_for(session) else {
        state.record_error(VdError::new(VD_ERR_NOT_FOUND, "unknown LSP session id"));
        return ptr::null_mut();
    };
    c_string(&slot.command_line)
}

#[cfg(test)]
mod tests {
    use super::{
        response_to_c, vd_lsp_backend_capabilities, vd_lsp_capabilities_free, vd_lsp_command_line,
        vd_lsp_file_list_preview, vd_lsp_response_free, vd_lsp_session_start_v2, VdLspCapabilities,
        VD_LSP_RES_HOVER, VD_LSP_RES_SIGNATURE_HELP,
    };
    use crate::capi::{vd_string_free, VdError, VdStringList, VD_STATUS_READY};
    use crate::service::{vd_service_create, vd_service_destroy};
    use std::ffi::{c_char, CStr, CString};
    use std::ptr;

    struct List {
        _owned: Vec<CString>,
        _raw: Vec<*mut c_char>,
        list: VdStringList,
    }

    impl List {
        fn new(values: &[&str]) -> Self {
            let owned: Vec<CString> = values
                .iter()
                .map(|value| {
                    CString::new(*value).unwrap_or_else(|_| {
                        panic!("a fixture string must not contain an interior NUL")
                    })
                })
                .collect();
            let mut raw: Vec<*mut c_char> = owned
                .iter()
                .map(|value| value.as_ptr().cast_mut())
                .collect();
            let list = VdStringList {
                count: raw.len(),
                items: if raw.is_empty() {
                    ptr::null_mut()
                } else {
                    raw.as_mut_ptr()
                },
            };
            Self {
                _owned: owned,
                _raw: raw,
                list,
            }
        }

        fn as_ptr(&self) -> *const VdStringList {
            &raw const self.list
        }
    }

    fn empty_capabilities() -> VdLspCapabilities {
        VdLspCapabilities {
            backend: ptr::null_mut(),
            basis: ptr::null_mut(),
            source: ptr::null_mut(),
            note: ptr::null_mut(),
            count: 0,
            items: ptr::null_mut(),
        }
    }

    fn c_string(pointer: *mut c_char) -> String {
        // SAFETY: the caller passes a live string produced by this crate.
        unsafe { CStr::from_ptr(pointer) }
            .to_string_lossy()
            .into_owned()
    }

    #[test]
    fn the_verible_table_is_measured_and_answers_every_action() {
        let mut capabilities = empty_capabilities();
        // SAFETY: `out` is writable for one VdLspCapabilities.
        assert_eq!(
            unsafe { vd_lsp_backend_capabilities(0, &mut capabilities) },
            VD_STATUS_READY
        );
        assert_eq!(capabilities.count, 13);
        assert_eq!(c_string(capabilities.backend), "verible");
        assert_eq!(c_string(capabilities.basis), "measured");
        assert!(c_string(capabilities.source).contains("v0.0-4298"));
        assert!(c_string(capabilities.note).contains("no hover"));
        // The row order is the stable ABI order: 0 definition … 8 diagnostics,
        // 9 hover, 10 completion, 11 signatureHelp, 12 inlayHint.
        // SAFETY: `items`/`count` describe a live array.
        let rows = unsafe { std::slice::from_raw_parts(capabilities.items, capabilities.count) };
        assert_eq!(c_string(rows[0].action), "definition");
        assert_eq!(rows[0].supported, 1);
        assert_eq!(rows[9].supported, 0, "hover is not advertised");
        assert!(c_string(rows[9].reason).contains("hoverProvider"));
        assert_eq!(rows[10].supported, 0, "no completion");
        assert_eq!(rows[12].supported, 0, "no inlay hints");
        for row in rows {
            assert!(!row.reason.is_null(), "every row carries a reason");
        }
        // SAFETY: the struct was filled by the call above and is not freed yet.
        unsafe { vd_lsp_capabilities_free(&mut capabilities) };
        assert_eq!(capabilities.count, 0);
    }

    #[test]
    fn the_slang_table_is_the_measured_one_from_the_spike() {
        let mut capabilities = empty_capabilities();
        // SAFETY: `out` is writable for one VdLspCapabilities.
        assert_eq!(
            unsafe { vd_lsp_backend_capabilities(1, &mut capabilities) },
            VD_STATUS_READY
        );
        assert_eq!(c_string(capabilities.backend), "slang");
        assert_eq!(c_string(capabilities.basis), "measured");
        assert!(c_string(capabilities.source).contains("0.3.1+2ad996f"));
        assert!(c_string(capabilities.note).contains("NO document/range formatting"));
        // SAFETY: `items`/`count` describe a live array.
        let rows = unsafe { std::slice::from_raw_parts(capabilities.items, capabilities.count) };
        for row in rows {
            assert_ne!(row.supported, -1, "no row is unmeasured any more");
            let action = c_string(row.action);
            let supported = row.supported;
            match action.as_str() {
                // slug: hover/completion/inlayHint exist; formatting does not.
                "formatting" | "rangeFormatting" | "signatureHelp" => {
                    assert_eq!(supported, 0, "{action} must be unsupported")
                }
                _ => assert_eq!(supported, 1, "{action} must be supported"),
            }
        }
        // SAFETY: the struct was filled by the call above and is not freed yet.
        unsafe { vd_lsp_capabilities_free(&mut capabilities) };
    }

    #[test]
    fn the_verible_file_list_route_is_visible_in_the_session_argv() {
        // One real file so the generated list has a file entry.
        let dir = tempfile::Builder::new()
            .prefix("vd-lsp-filelist-")
            .tempdir()
            .unwrap_or_else(|error| panic!("tempdir: {error}"));
        let source = dir.path().join("top.sv");
        std::fs::write(
            &source,
            "module top;
endmodule
",
        )
        .unwrap_or_else(|error| panic!("write: {error}"));
        let dirs = List::new(&["/proj/inc"]);
        let defines = List::new(&["WIDTH=8"]);
        let files = List::new(&[source.to_str().unwrap_or_else(|| panic!("utf8 path"))]);
        let ls = CString::new("/nonexistent/verible-verilog-ls").unwrap_or_default();
        let root = CString::new(dir.path().to_str().unwrap_or("/tmp")).unwrap_or_default();
        let service = vd_service_create();

        // SAFETY: the service is live and every pointer is valid.
        let session = unsafe {
            vd_lsp_session_start_v2(
                service,
                ls.as_ptr(),
                root.as_ptr(),
                ptr::null(),
                files.as_ptr(),
                0,
                dirs.as_ptr(),
                defines.as_ptr(),
            )
        };
        assert!(session > 0, "the session handle exists");

        // SAFETY: the service is live and the id is ours.
        let line = unsafe { super::vd_lsp_session_command_line(service, session) };
        assert!(!line.is_null());
        let command_line = c_string(line);
        // SAFETY: the string was produced by this crate and is not freed yet.
        unsafe { vd_string_free(line) };
        assert!(
            command_line.contains("backend=verible"),
            "argv: {command_line}"
        );
        let marker = "--file_list_path=";
        let start = command_line
            .find(marker)
            .unwrap_or_else(|| panic!("no {marker} in the argv: {command_line}"));
        let path = command_line[start + marker.len()..]
            .split_whitespace()
            .next()
            .unwrap_or_else(|| panic!("no path after {marker}"));
        // The generated file exists and carries the project settings.
        println!("session argv: {command_line}");
        let content =
            std::fs::read_to_string(path).unwrap_or_else(|error| panic!("reading {path}: {error}"));
        println!("generated .f content:\n{content}");
        assert!(content.contains("+incdir+/proj/inc"), "{content}");
        assert!(content.contains("+define+WIDTH=8"), "{content}");
        assert!(content.contains("top.sv"), "{content}");

        // Releasing the session removes the generated file.
        // SAFETY: the service is live and the id is ours.
        unsafe { super::vd_lsp_session_free(service, session) };
        assert!(
            !std::path::Path::new(path).exists(),
            "the file list is deleted"
        );
        // SAFETY: the service was created above.
        unsafe { vd_service_destroy(service) };
    }

    #[test]
    fn an_invalid_backend_is_rejected() {
        let mut capabilities = empty_capabilities();
        // SAFETY: `out` is writable for one VdLspCapabilities.
        assert_eq!(
            unsafe { vd_lsp_backend_capabilities(7, &mut capabilities) },
            -1
        );
    }

    #[test]
    fn the_slang_command_line_shows_the_include_dirs_and_defines() {
        let dirs = List::new(&["/proj/inc"]);
        let defines = List::new(&["WIDTH=8"]);
        let ls = CString::new("/bin/slang-server").unwrap_or_default();
        let root = CString::new("/proj").unwrap_or_default();
        // SAFETY: every pointer is valid for the duration of the call.
        let line = unsafe {
            vd_lsp_command_line(
                1,
                ls.as_ptr(),
                root.as_ptr(),
                ptr::null(),
                ptr::null(),
                dirs.as_ptr(),
                defines.as_ptr(),
            )
        };
        assert!(!line.is_null());
        let text = c_string(line);
        // SAFETY: the string was produced by this crate and is not freed yet.
        unsafe { vd_string_free(line) };
        assert!(text.contains("backend=slang"), "{text}");
        assert!(text.contains("-I/proj/inc"), "{text}");
        assert!(text.contains("-DWIDTH=8"), "{text}");
    }

    #[test]
    fn the_verible_file_list_preview_carries_incdir_and_define() {
        let files = List::new(&["/proj/top.sv"]);
        let dirs = List::new(&["/proj/inc"]);
        let defines = List::new(&["WIDTH=8"]);
        // SAFETY: every pointer is valid for the duration of the call.
        let text =
            unsafe { vd_lsp_file_list_preview(files.as_ptr(), dirs.as_ptr(), defines.as_ptr()) };
        assert!(!text.is_null());
        let content = c_string(text);
        // SAFETY: the string was produced by this crate and is not freed yet.
        unsafe { vd_string_free(text) };
        println!("generated Verible file list:\n{content}");
        assert!(content.contains("+incdir+/proj/inc"), "{content}");
        assert!(content.contains("+define+WIDTH=8"), "{content}");
        assert!(content.contains("/proj/top.sv"), "{content}");
    }

    #[test]
    fn start_v2_rejects_an_unknown_backend_and_starts_a_known_one() {
        let service = vd_service_create();
        let files = List::new(&[]);
        let ls = CString::new("/nonexistent/slang-server").unwrap_or_default();
        let root = CString::new("/tmp").unwrap_or_default();
        // SAFETY: the service is live and every pointer is valid.
        let id = unsafe {
            vd_lsp_session_start_v2(
                service,
                ls.as_ptr(),
                root.as_ptr(),
                ptr::null(),
                files.as_ptr(),
                9,
                ptr::null(),
                ptr::null(),
            )
        };
        assert_eq!(id, 0, "an unknown backend starts nothing");
        // SAFETY: the service is live and `out` is writable.
        let mut error = unsafe { std::mem::zeroed::<VdError>() };
        // SAFETY: the service is live and `out` is writable.
        assert_eq!(
            unsafe { crate::service::vd_service_error(service, &mut error) },
            1
        );
        assert_eq!(error.code, crate::capi::VD_ERR_INVALID_ARGUMENT);

        // A known backend starts a session handle (lazy: no process is spawned).
        // SAFETY: same contract.
        let id = unsafe {
            vd_lsp_session_start_v2(
                service,
                ls.as_ptr(),
                root.as_ptr(),
                ptr::null(),
                files.as_ptr(),
                0,
                ptr::null(),
                ptr::null(),
            )
        };
        assert!(
            id > 0,
            "the session handle exists even when the binary is missing"
        );
        // SAFETY: the service is live; the id is ours.
        unsafe { super::vd_lsp_session_free(service, id) };
        // SAFETY: the service was created above.
        unsafe { vd_service_destroy(service) };
    }

    // --- T16: hover / signatureHelp ABI mapping ----------------------------

    #[test]
    fn hover_response_maps_to_kind_text_and_range() {
        let response = vd_lsp::SessionResponse::Hover(vd_lsp::HoverResult {
            text: "logic [7:0] data".to_string(),
            range: Some(vd_lsp::SessionRange::default()),
        });
        let mut raw = response_to_c(response);
        assert_eq!(raw.kind, VD_LSP_RES_HOVER);
        // SAFETY: `text` was produced by this module and is freed below.
        let text = unsafe { CStr::from_ptr(raw.text) }
            .to_string_lossy()
            .into_owned();
        assert_eq!(text, "logic [7:0] data");
        assert_eq!(raw.range.start_line, 0);
        assert_eq!(raw.range.end_character, 0);
        // SAFETY: `raw` is a filled response that is not freed yet.
        unsafe { vd_lsp_response_free(&mut raw) };
        assert!(raw.text.is_null());
    }

    #[test]
    fn signature_help_response_maps_labels_and_active_indexes() {
        let response = vd_lsp::SessionResponse::SignatureHelp(vd_lsp::SignatureHelpResult {
            signatures: vec![vd_lsp::SignatureInfo {
                label: "foo(int a, int b)".to_string(),
                parameters: vec!["int a".to_string(), "int b".to_string()],
            }],
            active_signature: Some(0),
            active_parameter: Some(1),
        });
        let mut raw = response_to_c(response);
        assert_eq!(raw.kind, VD_LSP_RES_SIGNATURE_HELP);
        assert_eq!(raw.signatures.count, 1);
        assert_eq!(raw.active_signature, 0);
        assert_eq!(raw.active_parameter, 1);
        // SAFETY: the array was produced by this module for `count` entries.
        let signatures = unsafe { &*raw.signatures.items };
        // SAFETY: the label is a NUL-terminated string owned by this response.
        let label = unsafe { CStr::from_ptr(signatures.label) }
            .to_string_lossy()
            .into_owned();
        assert_eq!(label, "foo(int a, int b)");
        assert_eq!(signatures.parameters.count, 2);
        // SAFETY: `raw` is a filled response that is not freed yet.
        unsafe { vd_lsp_response_free(&mut raw) };
        assert_eq!(raw.signatures.count, 0);
        assert_eq!(raw.active_signature, -1);
    }
}
