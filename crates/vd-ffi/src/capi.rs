//! C-facing types and ownership helpers.
//!
//! Everything in this module is a plain C type: `#[repr(C)]` structs of integers,
//! `size_t` lengths, raw pointers and fixed-size `char` arrays. The generated
//! header must never contain a Rust type (C2), so Rust-owned data (`String`,
//! `Vec`, `Option`) never crosses the boundary: memory is handed out as owned C
//! allocations here and released by the matching `vd_*_free` function.
//!
//! # Ownership rule
//!
//! * An `*Info`/`*Results`/`*Status` struct that a poll function filled is owned
//!   by the caller and must be released with its `vd_*_free` function.
//! * Strings inside those structs are NUL-terminated, valid UTF-8 (a `NUL` byte
//!   in Rust text is replaced by a space, never silently truncated), and owned by
//!   the struct.
//! * A null pointer or zero count is always accepted by the free functions, so a
//!   partial initialisation cannot leak or double-free.

use std::ffi::{c_char, CStr, CString};
use std::ptr;

/// Poll result: the job is still running.
pub const VD_STATUS_PENDING: i32 = 0;
/// Poll result: the job finished and the out-parameter holds its result.
pub const VD_STATUS_READY: i32 = 1;
/// Poll result: the job failed; `vd_service_error` holds the reason.
pub const VD_STATUS_FAILED: i32 = 2;

/// Error code: the caller passed a null or non-UTF-8 argument.
pub const VD_ERR_INVALID_ARGUMENT: i32 = 1;
/// Error code: a required file or directory does not exist.
pub const VD_ERR_NOT_FOUND: i32 = 2;
/// Error code: the underlying service failed (I/O, subprocess, parse).
pub const VD_ERR_SERVICE: i32 = 3;
/// Error code: the job was cancelled and its result discarded.
pub const VD_ERR_CANCELLED: i32 = 4;
/// Error code: the job id belongs to a different service.
pub const VD_ERR_WRONG_JOB_KIND: i32 = 5;
/// Error code: an internal invariant failed (worker panic, poisoned lock).
pub const VD_ERR_INTERNAL: i32 = 6;
/// Error code: the job id is unknown or was already consumed.
pub const VD_ERR_JOB_UNKNOWN: i32 = 7;
/// Error code: the operation is frozen in the C ABI but this build's engine
/// layer does not implement it yet. The message names the lane that does
/// (batch 20261110: `env-core`, `rename-core`, `scala-run`, `lint-engine`).
pub const VD_ERR_UNIMPLEMENTED: i32 = 8;
/// Error code: the job exceeded its wall-clock budget; its result was discarded.
pub const VD_ERR_TIMEOUT: i32 = 9;
/// Error code: the named top-level module could not be elaborated. Typed apart
/// from [`VD_ERR_SERVICE`] because it is a USER answer, not a tool failure: the
/// module tree may fall back to Auto exactly once for it, and must never do that
/// for a timeout or a broken toolchain.
pub const VD_ERR_UNKNOWN_TOP: i32 = 10;

/// Capacity of [`VdError::message`], including the terminating NUL.
pub const VD_ERROR_MESSAGE_CAPACITY: usize = 256;

/// Error details of the most recent rejected call or failed job.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdError {
    /// One of the `VD_ERR_*` codes.
    pub code: i32,
    /// NUL-terminated, human-readable reason.
    pub message: [c_char; VD_ERROR_MESSAGE_CAPACITY],
}

impl VdError {
    /// Build an error from a code and a reason, truncating at a UTF-8 boundary.
    pub(crate) fn new(code: i32, message: &str) -> Self {
        let mut error = Self {
            code,
            message: [0; VD_ERROR_MESSAGE_CAPACITY],
        };
        let capacity = VD_ERROR_MESSAGE_CAPACITY - 1;
        let mut fitted = String::new();
        for character in message.chars() {
            if fitted.len() + character.len_utf8() > capacity {
                break;
            }
            fitted.push(character);
        }
        for (slot, byte) in error.message.iter_mut().zip(fitted.as_bytes()) {
            *slot = *byte as c_char;
        }
        error
    }
}

/// A C array of NUL-terminated strings.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdStringList {
    /// Number of entries in `items` (`size_t` on the C side).
    pub count: usize,
    /// Owned array of owned strings; null when `count` is zero.
    pub items: *mut *mut c_char,
}

impl VdStringList {
    /// The empty list.
    pub(crate) const fn empty() -> Self {
        Self {
            count: 0,
            items: ptr::null_mut(),
        }
    }
}

/// One project member list: files, include dirs, defines and warnings.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdProjectInfo {
    /// Expanded `.f` file list, in filelist order, duplicates removed.
    pub files: VdStringList,
    /// `+incdir+` directories after expansion.
    pub include_dirs: VdStringList,
    /// `+define+` entries as `NAME` or `NAME=VALUE`.
    pub defines: VdStringList,
    /// Non-fatal diagnostics collected while resolving the project.
    pub warnings: VdStringList,
    /// The SAME file set relative to the project root (absolute only for a file
    /// outside it), same order as `files`.
    ///
    /// Recommended spelling for a generated `.f`: portable across a moved
    /// checkout. It is NOT the determining rule for correct jump URIs — the
    /// controlled 4-way spike (2026-11-02) showed the server's cwd must be the
    /// project root; with that cwd, absolute entries resolve correctly too, and
    /// with the wrong cwd even relative entries land on a sibling path. The root
    /// this projection is based on is the single source of truth for cwd and
    /// `rootUri` (never re-derive it in the launcher).
    pub relative_files: VdStringList,
    /// Filelist variable names still unresolved after the project variable
    /// table + the process environment (ADDENDUM A-9), unique and sorted. The
    /// UI asks for all of them in ONE dialog; every affected line also appears
    /// in `warnings` with its `.f` file and 1-based line.
    pub missing_variables: VdStringList,
    /// Lines that produced no entry (unresolved/invalid variable, unknown
    /// token, malformed define, missing/cyclic nested `-f`). A non-zero value
    /// must be shown to the user: a dropped line is a file the pass will not
    /// lint.
    pub skipped_lines: u32,
    /// Absolute path of the `.rules.verible_lint` the project loader DISCOVERED
    /// (the nearest one, searched upward, T114 semantics unchanged), or null
    /// when none was found. This is what the Lint settings page and the lint
    /// pass consume; it is not a user-typed path.
    pub rules_path: *mut c_char,
    /// Every filelist variable name the expansion referenced, unique and sorted
    /// by name (batch-20261114 E1). This is the "referenced" set, NOT the
    /// "available" one: a name no `.f` line mentions never appears here, and
    /// `missing_variables` keeps its existing meaning — its names are the
    /// `"missing"` entries of this list.
    pub referenced_variables: VdStringList,
    /// One entry per [`Self::referenced_variables`] name, same count and order:
    /// `"project"` (the project's `NAME=VALUE` table), `"imported"` (the
    /// Settings → Environment selection), `"environment"` (the process
    /// environment) or `"missing"` (no layer has it). The layer recorded is the
    /// one resolution actually used, i.e. project > imported > environment when
    /// several layers carry the same name. The two lists are parallel exactly
    /// like `files` / `relative_files`.
    pub referenced_variable_layers: VdStringList,
}

/// One lint diagnostic.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdDiagnostic {
    /// 0 error, 1 warning, 2 note, 3 ignored, 4 unknown.
    pub severity: i32,
    /// 0-based start line.
    pub line: u32,
    /// 0-based start column (UTF-16 code units).
    pub column: u32,
    /// 0-based end line.
    pub end_line: u32,
    /// 0-based end column (UTF-16 code units).
    pub end_column: u32,
    /// Diagnostic code (`UndeclaredIdentifier`, or the verible rule name).
    pub code: *mut c_char,
    /// Human-readable message.
    pub message: *mut c_char,
    /// The file the diagnostic belongs to (batch runs); null on the single-file
    /// call, where the caller already knows it.
    pub file: *mut c_char,
    /// The cross-engine dedup key (AC-2.13): file path, 0-based start line,
    /// severity class and normalised message separated by `0x1F`. Two engines
    /// reporting the same problem produce the same key.
    pub key: *mut c_char,
    /// 0 unknown, 1 LSP, 2 CLI, 3 both (the source column, AC-2.13).
    pub origin: i32,
    /// The engine's typed option name for this diagnostic — the `optionName`
    /// slang reports (`implicit-net`, `unused-variable`, …), the verible rule
    /// that fired, or null when the engine reported none (a slang error
    /// typically has no option name). It is what a waiver is written from, so
    /// the display layer never parses it out of `message`.
    pub option_name: *mut c_char,
}

/// All diagnostics of one deep-lint run.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdDiagnostics {
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of diagnostics.
    pub items: *mut VdDiagnostic,
}

/// One row of a language server's capability table (T138).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspCapability {
    /// Action name (`definition`, `references`, `documentSymbol`, …).
    pub action: *mut c_char,
    /// 1 supported, 0 unsupported, -1 unknown (not measured in this environment).
    pub supported: i32,
    /// Why; never null (an unsupported action always says why).
    pub reason: *mut c_char,
}

/// A backend's capability table, with how it was established.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspCapabilities {
    /// Backend name (`verible` / `slang`).
    pub backend: *mut c_char,
    /// How the table was established: `measured`, `live` or `declared`.
    pub basis: *mut c_char,
    /// Where the rows come from (binary version / live handshake).
    pub source: *mut c_char,
    /// What this backend offers that the other does not (the UI-gating
    /// asymmetry, one sentence; never null).
    pub note: *mut c_char,
    /// Number of rows.
    pub count: usize,
    /// Owned array of rows.
    pub items: *mut VdLspCapability,
}

/// Progress of one project-wide lint pass (AC-2.7/2.8/2.9).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLintProgress {
    /// 0 queued, 1 running, 2 finished, 3 cancelled, 4 failed.
    pub phase: i32,
    /// Files already linted.
    pub done: usize,
    /// Files in the pass's snapshot.
    pub total: usize,
    /// The file being linted right now, or null.
    pub current: *mut c_char,
    /// The tool that ran (`slang` / `verible-verilog-lint`).
    pub tool: *mut c_char,
    /// The failure stage (`resolve`, `spawn`, `run`, `parse`), or null.
    pub stage: *mut c_char,
    /// One-line state for the status bar.
    pub message: *mut c_char,
}

/// Inputs of one project-wide lint pass (borrowed for the duration of the call).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLintBatchOptions {
    /// 0 slang, 1 verible-verilog-lint.
    pub engine: i32,
    /// Explicit slang binary, or null to resolve it.
    pub slang_binary: *const c_char,
    /// Explicit verible-verilog-lint binary, or null to resolve it.
    pub verible_binary: *const c_char,
    /// Verible lint rules file (`--rules_config=`), or null.
    pub rules_path: *const c_char,
    /// Project include dirs (`-I`; slang only).
    pub include_dirs: VdStringList,
    /// Project defines (`-D`; slang only).
    pub defines: VdStringList,
    /// Per-file wall-clock budget in milliseconds; 0 selects the default.
    pub timeout_ms: u64,
    /// Maximum diagnostics kept per file; 0 selects the default.
    pub max_diagnostics: usize,
    /// Verible ruleset: 0 = default (NO flag), 1 = `--ruleset=all`,
    /// 2 = `--ruleset=none`; any other value is rejected. Verible-only knob —
    /// documented no-op for slang (no argv).
    pub ruleset: u8,
    /// Verible `--rules=` list (`+rule,-rule`, comma separated), or null/empty
    /// for no flag. Accepted characters: rule names `[A-Za-z0-9_-]` optionally
    /// prefixed by `+`/`-`, comma separated. Verible-only knob.
    pub rules: *const c_char,
    /// Verible `--waiver_files=` (comma-separated paths), or null/empty for no
    /// flag. Entries must be non-empty and free of control characters; a comma
    /// cannot appear inside a path (it is the separator). Verible-only knob.
    pub waiver_files: *const c_char,
    /// Extra slang `-W…` tokens (whitespace/comma separated), or null/empty for
    /// none. Each token must match `-W[A-Za-z0-9_=-]+` and becomes its OWN argv
    /// entry. Slang-only knob — documented no-op for verible.
    pub slang_suppressions: *const c_char,
    /// Slang `--error-limit=N`; 0 sends no flag. Slang-only knob.
    pub error_limit: u32,
    /// Verible `--autofix=` mode: `0` sends no flag (verible's own `no`),
    /// `1` = `patch`, `2` = `patch-interactive`, `3` = `inplace`,
    /// `4` = `inplace-interactive`, `5` = `generate-waiver`; any other value is
    /// rejected. Verible-only knob.
    pub verible_autofix: u8,
    /// Verible `--autofix_output_file=`: the patch file the `patch*` modes write,
    /// or the waiver file `generate-waiver` writes; null/empty sends no flag.
    /// Verible-only knob.
    pub autofix_output_file: *const c_char,
    /// Slang's own `--waiver-file`: ONE TOML waiver file (`[[waivers]]` blocks),
    /// or null/empty for no flag. Distinct from `waiver_files` above, which is
    /// verible's `--waiver_files=` list. Slang-only knob.
    pub slang_waiver_file: *const c_char,
}

/// State of one Verible LSP session.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdLspInfo {
    /// 0 stopped, 1 configured (lazy, no process yet), 2 error.
    pub state: i32,
    /// The server binary the client would start.
    pub server: *mut c_char,
    /// Human-readable status line.
    pub message: *mut c_char,
}

/// Result of one FIM completion request.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdFimResult {
    /// 0 disabled/not configured, 1 text returned, 2 error.
    pub state: i32,
    /// Completion text, or the reason no request was made.
    pub text: *mut c_char,
}

/// One global-search hit.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdSearchHit {
    /// File path as supplied to the index.
    pub path: *mut c_char,
    /// 1-based line number.
    pub line: u32,
    /// 1-based character column of the match start (character, not byte: this
    /// is the editor's position unit).
    pub column: u32,
    /// Trimmed, truncated matched line.
    pub preview: *mut c_char,
    /// 1-based character column immediately AFTER the match (exclusive end), so
    /// `end_column - column == match_length`. This is the range the caller
    /// replaces; `0` means the query reports no range (never for a tree hit).
    pub end_column: u32,
    /// Match length in characters.
    pub match_length: u32,
}

/// Options of one project (tree) search (ux-batch-2 A1).
///
/// Passed to [`crate::search::vd_search_tree_query_ex`]; every field is a plain
/// value with a documented default, so a zeroed struct is the old behaviour.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdSearchOptions {
    /// Case-sensitive matching (0 = insensitive, the default).
    pub case_sensitive: u8,
    /// Whole-word matching (0 = substring, the default).
    pub whole_word: u8,
    /// Treat `needle` as a regular expression (0 = literal, the default). The
    /// engine is RE2-style: no backreferences, no lookaround.
    pub regex: u8,
    /// Report every occurrence on a line as its own hit (0 = first only).
    pub all_matches: u8,
    /// Reserved; must be zero.
    pub _reserved: [u8; 4],
    /// File extensions to include (e.g. "v", ".svh"); empty/null = no extension
    /// filter. Blank entries are ignored.
    pub extensions: VdStringList,
    /// Result cap; 0 selects the default (2000).
    pub max_results: usize,
    /// Files larger than this are skipped; 0 selects the default (8 MiB).
    pub max_file_bytes: u64,
    /// Wall-clock budget in milliseconds; 0 selects the default (15 s).
    pub max_duration_ms: u64,
}

/// All hits of one search query.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdSearchResults {
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of hits.
    pub items: *mut VdSearchHit,
}

/// One changed path reported by the VCS backend.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdVcsChange {
    /// Path relative to the working tree root.
    pub path: *mut c_char,
    /// 1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown, 7 unversioned.
    pub kind: i32,
    /// Owned SVN changelist name, or NULL.
    pub changelist: *mut c_char,
}

/// The VCS status of one working tree.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdVcsStatus {
    /// 0 none, 1 git, 2 svn.
    pub backend: i32,
    /// Current branch when the backend reports one; may be null.
    pub branch: *mut c_char,
    /// Number of entries in `changes`.
    pub count: usize,
    /// Owned array of changes.
    pub changes: *mut VdVcsChange,
    /// Why the backend could not be queried; null on success.
    pub note: *mut c_char,
}

/// One stored setting.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdSetting {
    /// Flat key.
    pub key: *mut c_char,
    /// String value.
    pub value: *mut c_char,
}

/// Input pair for `vd_settings_save` (borrowed for the duration of the call).
#[repr(C)]
pub struct VdSettingInput {
    /// Flat key.
    pub key: *const c_char,
    /// String value.
    pub value: *const c_char,
}

/// All settings read from the store, plus warnings.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdSettings {
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of settings.
    pub items: *mut VdSetting,
    /// Non-fatal problems (invalid TOML, non-string values).
    pub warnings: VdStringList,
}

/// Copy a Rust string into an owned C string. Interior NULs become spaces.
pub(crate) fn c_string(text: &str) -> *mut c_char {
    let sanitized = if text.contains('\0') {
        text.replace('\0', " ")
    } else {
        text.to_string()
    };
    match CString::new(sanitized) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Copy a string list into an owned C array of owned C strings.
///
/// # Safety
///
/// The returned pointers must be released with [`free_string_list`]; they are
/// never aliased by Rust after this function returns.
pub(crate) unsafe fn c_string_list(items: &[String]) -> VdStringList {
    if items.is_empty() {
        return VdStringList::empty();
    }
    let mut raw: Vec<*mut c_char> = Vec::with_capacity(items.len());
    for item in items {
        raw.push(c_string(item));
    }
    let boxed = raw.into_boxed_slice();
    VdStringList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut *mut c_char,
    }
}

/// Release one C string produced by [`c_string`] or by a poll result.
///
/// # Safety
///
/// `text` must be null or a pointer returned by this crate and not freed yet.
pub(crate) unsafe fn free_string(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}

/// Release an owned byte block produced by this crate (a NUL-separated
/// environment blob, which is not a C string and carries its own length).
///
/// # Safety
///
/// `block` must be null or a pointer this crate returned with exactly `len`
/// bytes, and it must not be freed twice.
pub(crate) unsafe fn free_bytes(block: *mut c_char, len: usize) {
    if block.is_null() || len == 0 {
        return;
    }
    // SAFETY: the caller states the block came from `Box<[u8]>::into_raw`.
    drop(unsafe { Box::from_raw(ptr::slice_from_raw_parts_mut(block as *mut u8, len)) });
}

/// Release a list produced by [`c_string_list`], including every string in it.
///
/// # Safety
///
/// `list` must come from [`c_string_list`] and must not be freed twice.
pub(crate) unsafe fn free_string_list(list: &mut VdStringList) {
    if !list.items.is_null() {
        let slice = &mut *ptr::slice_from_raw_parts_mut(list.items, list.count);
        for slot in slice.iter_mut() {
            if !slot.is_null() {
                drop(CString::from_raw(*slot));
                *slot = ptr::null_mut();
            }
        }
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )));
    }
    list.count = 0;
    list.items = ptr::null_mut();
}

/// Read a borrowed C string as `&str`.
///
/// # Safety
///
/// `text` must be null or point to a NUL-terminated string that stays valid for
/// the duration of the call.
pub(crate) unsafe fn c_str<'a>(text: *const c_char) -> Option<&'a str> {
    if text.is_null() {
        return None;
    }
    CStr::from_ptr(text).to_str().ok()
}

/// Read a borrowed C string array into owned Rust strings.
///
/// # Safety
///
/// `items` must be null or point to `count` valid NUL-terminated strings that
/// stay valid for the duration of the call. A null entry is skipped.
pub(crate) unsafe fn c_str_array(items: *const *const c_char, count: usize) -> Option<Vec<String>> {
    if count == 0 {
        return Some(Vec::new());
    }
    if items.is_null() {
        return None;
    }
    let slice = std::slice::from_raw_parts(items, count);
    let mut values = Vec::with_capacity(count);
    for entry in slice {
        values.push(c_str(*entry)?.to_string());
    }
    Some(values)
}

/// The cross-engine dedup key of one diagnostic (AC-2.13).
///
/// `origin` selects how `severity` is read: `1` = LSP (1 error … 4 hint), `2` =
/// CLI (0 error … 4 unknown). The key is `file`, 0-based `line`, the shared
/// severity class and the normalised message separated by `0x1F`, so an LSP row
/// and a CLI row of the same problem produce the same string. Returns an owned
/// string (release with [`vd_string_free`]) or null for invalid arguments.
///
/// # Safety
///
/// `file` and `message` must be null or valid NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_diagnostic_key(
    origin: i32,
    file: *const c_char,
    line: u32,
    severity: i32,
    message: *const c_char,
) -> *mut c_char {
    // SAFETY: the caller supplies valid strings or null.
    let (Some(file), Some(message)) = (unsafe { c_str(file) }, unsafe { c_str(message) }) else {
        return ptr::null_mut();
    };
    let class = match origin {
        1 => vd_deeplint::class_of_lsp(severity),
        2 => vd_deeplint::class_of_cli_code(severity),
        _ => vd_deeplint::SeverityClass::Unknown,
    };
    c_string(&vd_deeplint::diagnostic_key(file, line, class, message))
}

/// Release an owned string returned by [`vd_diagnostic_key`],
/// `vd_lint_command_line` or `vd_lsp_command_line`; null-safe.
///
/// # Safety
///
/// `text` must be null or a pointer this library returned and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_string_free(text: *mut c_char) {
    // SAFETY: the caller contract says `text` came from this crate and is unfreed.
    unsafe { free_string(text) };
}

/// One elaborated instance (AC-3.1's data side).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierInstance {
    /// Hierarchical path (`top.u0`).
    pub path: *mut c_char,
    /// Module name (`mid`).
    pub module: *mut c_char,
    /// File of the instantiation site.
    pub file: *mut c_char,
    /// 0-based line of the instantiation site.
    pub line: u32,
    /// 0-based column of the instantiation site.
    pub column: u32,
}

/// Array of instances.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierInstanceList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of instances.
    pub items: *mut VdHierInstance,
}

/// One driver or load of a net.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierEndpoint {
    /// Hierarchical path of the driving/loading symbol.
    pub path: *mut c_char,
    /// File of the position.
    pub file: *mut c_char,
    /// 0-based line.
    pub line: u32,
    /// 0-based column.
    pub column: u32,
    /// 0 instance-output-port, 1 instance-input-port, 2 continuous-assign,
    /// 3 procedural.
    pub kind: i32,
    /// Extra label (`AlwaysFF`, `assign`, `port q (out)`).
    pub detail: *mut c_char,
}

/// Array of endpoints.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierEndpointList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of endpoints.
    pub items: *mut VdHierEndpoint,
}

/// One net with its drivers and loads (AC-3.2's data side).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierSignal {
    /// Hierarchical path (`top.link`).
    pub path: *mut c_char,
    /// `Net`, `Variable` or `Port`.
    pub kind: *mut c_char,
    /// 1 when a declaration position was reported.
    pub has_declaration: i32,
    /// Declaration file, or null.
    pub declaration_file: *mut c_char,
    /// 0-based declaration line.
    pub declaration_line: u32,
    /// 0-based declaration column.
    pub declaration_column: u32,
    /// Constructs that drive the net.
    pub drivers: VdHierEndpointList,
    /// Constructs that read the net.
    pub loads: VdHierEndpointList,
    /// Driver chain that continues inside a child instance.
    pub chain: VdHierEndpointList,
}

/// Array of nets.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierSignalList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of nets.
    pub items: *mut VdHierSignal,
}

/// One elaboration result.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierarchy {
    /// The tool that ran (`slang`).
    pub tool: *mut c_char,
    /// The requested top, or null when slang picked the roots.
    pub top: *mut c_char,
    /// Wall-clock time of the run, in milliseconds.
    pub elapsed_ms: u64,
    /// Peak RSS of the child, in KiB (sampled from `/proc/<pid>/status`).
    pub peak_rss_kb: u64,
    /// Oversize warning for the UI (AC-3.5), or null.
    pub warning: *mut c_char,
    /// Elaborated instances.
    pub instances: VdHierInstanceList,
    /// Nets of every instance. Named `signal_list`, not `signals`: `signals` is
    /// a Qt macro (`#define signals public`), and this header is included by Qt
    /// translation units.
    pub signal_list: VdHierSignalList,
}

/// Inputs of one elaboration run (borrowed for the duration of the call).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdHierOptions {
    /// Explicit slang binary, or null to resolve it.
    pub slang_binary: *const c_char,
    /// Top module, or null to let slang pick the roots.
    pub top: *const c_char,
    /// Project include dirs (`-I`).
    pub include_dirs: VdStringList,
    /// Project defines (`-D`).
    pub defines: VdStringList,
    /// Wall-clock budget in milliseconds; 0 selects the default (30 s).
    pub timeout_ms: u64,
    /// Oversize threshold for the instance count; 0 selects the default (10000).
    pub max_instances: usize,
    /// Oversize threshold for the file count; 0 selects the default (1000).
    pub max_files: usize,
}

/// One port of one module definition (ux-batch-2 A1).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModulePort {
    /// Port name.
    pub name: *mut c_char,
    /// `In`, `Out`, `InOut` or `Ref`.
    pub direction: *mut c_char,
    /// The declared type verbatim (`logic`, `logic[3:0]`, an interface name, …).
    pub type_name: *mut c_char,
    /// File of the port declaration.
    pub file: *mut c_char,
    /// 0-based line of the port declaration.
    pub line: u32,
    /// 0-based column of the port declaration.
    pub column: u32,
}

/// Array of module ports.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModulePortList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of ports.
    pub items: *mut VdModulePort,
}

/// One child instantiation inside a module definition.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModuleInstance {
    /// Instantiated module name.
    pub module: *mut c_char,
    /// Hierarchical path of the instance in the elaborated design
    /// (`top.u_mid`); stable across runs for the same project.
    pub path: *mut c_char,
    /// File of the instantiation site.
    pub file: *mut c_char,
    /// 0-based line of the instantiation site.
    pub line: u32,
    /// 0-based column of the instantiation site.
    pub column: u32,
}

/// Array of child instantiations.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModuleInstanceList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of instances.
    pub items: *mut VdModuleInstance,
}

/// One module definition with its ports and child instantiations.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModule {
    /// Module name (the deduplication key).
    pub name: *mut c_char,
    /// File of the module definition.
    pub file: *mut c_char,
    /// 0-based line of the module definition.
    pub line: u32,
    /// 0-based column of the module definition.
    pub column: u32,
    /// 1 when the module is never instantiated anywhere (a design top), else 0.
    pub is_top: u8,
    /// Ports in declaration order.
    pub ports: VdModulePortList,
    /// Child instantiations, one entry per instantiation site.
    pub children: VdModuleInstanceList,
}

/// Array of modules.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModuleList {
    /// Number of entries.
    pub count: usize,
    /// Owned array of modules, sorted by name.
    pub items: *mut VdModule,
}

/// One module catalog (ux-batch-2 A1).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdModuleCatalog {
    /// The tool that ran (`slang`).
    pub tool: *mut c_char,
    /// Wall-clock time of the elaboration run, in milliseconds.
    pub elapsed_ms: u64,
    /// Peak RSS of the child, in KiB.
    pub peak_rss_kb: u64,
    /// Oversize warning for the UI, or null.
    pub warning: *mut c_char,
    /// Modules, sorted by name.
    pub modules: VdModuleList,
    /// RM-17: the effective top the elaboration used, or null = Auto.
    pub top: *mut c_char,
    /// RM-17: absolute source files reachable from the top (the lint scope and
    /// the status-dot exclusion set both come from this ONE list).
    pub reachable_files: VdStringList,
    /// RM-17: module definitions outside the top (empty in Auto), sorted.
    pub outside_modules: VdStringList,
    /// G1-c: the files whose every module declaration lost the duplicate pick, so
    /// the compiler input no longer needs them. Sorted; empty when nothing is
    /// shadowed. APPENDED at the end of the struct: every offset above it is
    /// unchanged, but the STRUCT GREW (88 -> 112 bytes) and the provider writes
    /// the whole current struct in `vd_catalog_poll`, so a consumer built against
    /// an older header must be RECOMPILED - offsets are stable, the size is not
    /// (growth introduced in `VD_ABI_VERSION` 2).
    pub shadowed_files: VdStringList,
    /// G1-c: the sentence saying those files are excluded from the compiler
    /// input, or null. Meant to be shown right AFTER `warning` (the B-2
    /// duplicate sentence), never merged into it: that sentence is frozen.
    pub shadowed_warning: *mut c_char,
}

/// Inputs of one catalog run (borrowed for the duration of the call).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdCatalogOptions {
    /// Explicit slang binary, or null to resolve it.
    pub slang_binary: *const c_char,
    /// Project include dirs (`-I`).
    pub include_dirs: VdStringList,
    /// Project defines (`-D`).
    pub defines: VdStringList,
    /// Wall-clock budget for the `slang` child in milliseconds; 0 selects the
    /// catalog default (120 s). The streamed parse is not covered by it.
    pub timeout_ms: u64,
    /// Size threshold for the catalog's AST-JSON warning in bytes; 0 selects
    /// the default (256 MiB). The document is STREAMED, so a larger dump is
    /// still catalogued in full: this threshold only adds a size note to
    /// `VdModuleCatalog.warning`, it never fails the catalog.
    pub max_json_bytes: usize,
    /// Oversize threshold for the file count; 0 selects the default (1000).
    pub max_files: usize,
    /// RM-17: top-level module NAME to elaborate from, or null = Auto.
    pub top: *const c_char,
}

// --- batch 20261110: shell environment capture (the `env-core` lane) ---------
//
// The surface is frozen here, ahead of the engine that fills it: every entry
// point below is request -> poll -> free, with real cancel and a wall-clock
// timeout, so the settings UI can run the capture without ever blocking. The
// engine that implements it is `crates/vd-ffi/src/env.rs`'s owner lane
// (`env-core`); until it lands, the job fails with `VD_ERR_UNIMPLEMENTED` and
// names the lane, and no call panics.

/// Shell family the capture initialises: bash.
pub const VD_ENV_SHELL_BASH: i32 = 0;
/// Shell family the capture initialises: zsh.
pub const VD_ENV_SHELL_ZSH: i32 = 1;
/// Shell family the capture initialises: csh.
pub const VD_ENV_SHELL_CSH: i32 = 2;
/// Shell family the capture initialises: tcsh.
pub const VD_ENV_SHELL_TCSH: i32 = 3;
/// Shell family the capture initialises: fish.
pub const VD_ENV_SHELL_FISH: i32 = 4;
/// Shell family the capture initialises: sh (POSIX).
pub const VD_ENV_SHELL_SH: i32 = 5;
/// Shell family the capture initialises: ksh.
pub const VD_ENV_SHELL_KSH: i32 = 6;
/// An explicit command line: `shell_path` is the program and `extra_argv` is its
/// complete argv (no `-c` is added).
pub const VD_ENV_SHELL_CUSTOM: i32 = 7;

/// Inputs of one shell environment capture (borrowed for the duration of the
/// call). Every string is null or valid UTF-8; a zeroed struct is a plain
/// non-interactive `bash` capture that sources no file.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvCaptureOptions {
    /// One of the `VD_ENV_SHELL_*` values.
    pub shell: i32,
    /// Explicit shell binary (absolute, or a bare name resolved on PATH); null
    /// resolves the shell family's default, and `VD_ENV_SHELL_CUSTOM` requires it.
    pub shell_path: *const c_char,
    /// The rc file to source before the environment is read, or null for none.
    pub source_file: *const c_char,
    /// Working directory of the capture, or null to inherit this process's.
    pub workdir: *const c_char,
    /// Extra argv entries appended after the shell's own (`-i`, `-l`, …).
    pub extra_argv: VdStringList,
    /// Wall-clock budget in milliseconds; 0 selects the engine's default.
    pub timeout_ms: u64,
    /// 1 starts a login shell, 0 (default) starts a plain one.
    pub login: u8,
    /// 1 starts an interactive shell (`-i`), 0 (default) starts a
    /// non-interactive one.
    pub interactive: u8,
}

/// The complete environment one capture produced.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvCaptureResult {
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of owned `NAME=VALUE` strings in the order the shell emitted
    /// them. A name the chain overrides keeps BOTH entries, so the caller can
    /// see the override instead of a silently collapsed value.
    pub items: *mut *mut c_char,
    /// The environment as ONE NUL-separated blob, exactly as the child printed
    /// it: every entry is `NAME=VALUE` followed by `\0`, and the blob ends with
    /// a second `\0`. A value that contains spaces or newlines survives intact.
    /// The caller reads `raw_len` bytes; the block is a plain byte buffer, not a
    /// C string.
    pub raw: *mut c_char,
    /// Length of `raw` in bytes, NULs included; 0 when no blob was produced.
    pub raw_len: usize,
    /// The shell binary that ran (absolute when it was resolved), or null.
    pub shell: *mut c_char,
    /// The child's exit code; `-1` when a signal ended it.
    pub exit_code: i32,
    /// Truncated stderr of the capture, or null when it printed none.
    pub stderr: *mut c_char,
    /// Non-fatal problems (an entry without `=`, a masked name, …).
    pub warnings: VdStringList,
}

// --- batch 20261110c: the persisted environment selection (the `env-ui` lane) -
//
// The selection is the two layers the Settings page writes (`environment.json`):
// the GLOBAL layer every project inherits, and one layer per project root that
// overrides it (AC-4.4 project wins over global). `EnvironmentSelection::save`
// and `::load` own the file format — the sibling-temporary-plus-rename atomic
// write, and the "a broken file is the empty selection plus warnings" rule — so
// these verbs only transport ROWS: the Qt side never re-implements the JSON
// schema and the two sides cannot drift apart.

/// One stored selection row, as a load job returns it.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvSelectionEntry {
    /// The layer this row belongs to: null for the global layer, otherwise the
    /// project root exactly as it is stored.
    pub project_root: *mut c_char,
    /// Variable name; never null and never empty.
    pub name: *mut c_char,
    /// The value exactly as stored. The store is the user's own file, so a
    /// secret-named value is stored as the user selected it; the UI masks it for
    /// DISPLAY, not before storage (a child needs the real value).
    pub value: *mut c_char,
}

/// The selection a load job read, plus the non-fatal problems it found.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvSelection {
    /// Number of rows in `items`.
    pub count: usize,
    /// Owned rows: the global layer first, then every project layer in ascending
    /// root order, each layer in its stored order.
    pub items: *mut VdEnvSelectionEntry,
    /// Non-fatal problems. A broken file degrades to an EMPTY selection plus a
    /// warning here; it is never a failed job (AC-4.4).
    pub warnings: VdStringList,
}

/// One name/value pair of an effective-environment snapshot (batch-20261115,
/// AC-1.3 / contract §4.1.2).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvSnapshotEntry {
    /// Variable name; owned, never null and never empty.
    pub name: *mut c_char,
    /// The value a child would receive. It is the REAL value (a secret-named
    /// value included): the caller uses it to start a child and must never log
    /// it (AC-1.5).
    pub value: *mut c_char,
}

/// The environment a child of this process should see: the process environment
/// with the imported selection applied on top (`process < global < project`).
///
/// Released with [`crate::env::vd_env_snapshot_free`]. This is a READ: it never
/// spawns a process and never touches the disk.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvSnapshot {
    /// Number of rows in `items`.
    pub count: usize,
    /// Owned rows, in ascending name order.
    pub items: *mut VdEnvSnapshotEntry,
}

/// One row a save job stores. Borrowed for the duration of the call, and the
/// same shape as [`VdEnvSelectionEntry`] so a loaded selection can be saved back
/// unchanged.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEnvSelectionInput {
    /// The layer of this row: null or empty for the global layer, otherwise the
    /// project root. The empty string is accepted as "global" because a C caller
    /// has no `Option`.
    pub project_root: *const c_char,
    /// Variable name: non-empty, without `=` and without control characters (a
    /// name with `=` cannot exist in a process environment either).
    pub name: *const c_char,
    /// The value, byte for byte: `=` and newlines are preserved (AC-4.9).
    pub value: *const c_char,
}

// --- batch 20261110: engineering-wide rename (the `rename-core` lane) --------

/// Options of one engineering-wide rename job. A zeroed struct is the default:
/// whole identifiers only, really apply, engine defaults for the bounds, and no
/// project `-I`/`-D` (a project that needs an include dir must pass it).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenameOptions {
    /// 1 (default) treats `from`/`to` as whole SystemVerilog identifiers only;
    /// 0 is refused, because this ABI never falls back to text replacement.
    pub whole_identifier: u8,
    /// 1 makes an APPLY job check that every target file is writable without
    /// writing anything; 0 (default) really applies.
    pub dry_run: u8,
    /// Preview entries kept per file; 0 selects the engine's default (200).
    pub max_entries_per_file: usize,
    /// Wall-clock budget in milliseconds; 0 selects the engine's default.
    pub timeout_ms: u64,
    /// The project expansion's EFFECTIVE `-I` include directories — the SAME
    /// list the lint / language-server children receive (T72). Empty keeps the
    /// pre-T72 behaviour: only the including file's own directory is searched,
    /// so a project whose file needs `-I inc` could not be elaborated.
    pub include_dirs: VdStringList,
    /// `-D` defines, `NAME` or `NAME=VALUE` — the SAME spelling and meaning the
    /// lint batch options carry (`VdLintBatchOptions.defines`). Empty = none.
    pub defines: VdStringList,
}

/// The index phase: what the project contains and how often the symbol occurs.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenameIndex {
    /// Files the index parsed.
    pub files: usize,
    /// Distinct identifiers the index holds.
    pub identifiers: usize,
    /// Occurrences of the symbol being renamed.
    pub occurrences: usize,
    /// Non-fatal problems (an unreadable file, an unsupported construct, …).
    pub warnings: VdStringList,
}

/// One planned replacement site.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenameHit {
    /// 0-based line.
    pub line: u32,
    /// 0-based start column (UTF-16 code units).
    pub column: u32,
    /// 0-based end column (UTF-16 code units, exclusive).
    pub end_column: u32,
    /// Byte offset of the identifier in the file — the apply anchor.
    pub byte_offset: u64,
    /// Length of the identifier in bytes.
    pub byte_len: u64,
    /// The source line before the edit (trimmed and capped; never null).
    pub before: *mut c_char,
    /// The same line after the edit (never null).
    pub after: *mut c_char,
}

/// The preview of one file.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenameFilePlan {
    /// Absolute path of the file.
    pub file: *mut c_char,
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of hits.
    pub items: *mut VdRenameHit,
    /// 1 when the engine kept fewer hits than it found (`count` is a prefix).
    pub truncated: u8,
}

/// A rename plan: every file and every replacement, ready to apply.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenamePlan {
    /// The project root the plan was built for (never null), so the apply job
    /// never has to guess it.
    pub root: *mut c_char,
    /// The identifier being renamed (never null).
    pub from: *mut c_char,
    /// The new identifier (never null).
    pub to: *mut c_char,
    /// Files the index parsed.
    pub files: usize,
    /// Total occurrences in the plan.
    pub occurrences: usize,
    /// Number of entries in `items`.
    pub count: usize,
    /// Owned array of per-file previews, in file order.
    pub items: *mut VdRenameFilePlan,
    /// Non-fatal problems.
    pub warnings: VdStringList,
}

/// The result of one atomic apply.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdRenameApplyResult {
    /// Files written (0 for a dry run).
    pub applied_files: usize,
    /// Replacements written (0 for a dry run).
    pub applied_occurrences: usize,
    /// The file that could not be written, or null when every write succeeded.
    pub failed_file: *mut c_char,
    /// 1 when a failure restored every previously written file byte for byte.
    pub rolled_back: u8,
    /// 1 when every written file was re-read and the expected replacements were
    /// found there.
    pub verified: u8,
}

// --- batch 20261110: one-click Scala run (the `scala-run` lane) --------------

/// Inputs of one Scala run (borrowed for the duration of the call).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdScalaRunOptions {
    /// Project root the program runs in; null inherits this process's cwd.
    pub root: *const c_char,
    /// The complete argv; `items[0]` is the program. Required and never empty.
    pub argv: VdStringList,
    /// Extra `NAME=VALUE` entries applied on top of the sanitized child
    /// environment; empty for none.
    pub env: VdStringList,
    /// Wall-clock budget in milliseconds; 0 runs until the program exits.
    pub timeout_ms: u64,
    /// 1 merges stderr into the streamed output; 0 (default) streams stdout only.
    pub merge_stderr: u8,
}

/// State and the NEW output of one Scala run.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdScalaRunStatus {
    /// 0 running, 1 exited, 2 failed, 3 cancelled, 4 timed out.
    pub state: i32,
    /// The exit code when `state` is 1; `-1` otherwise.
    pub exit_code: i32,
    /// The output produced since the PREVIOUS poll of this job (never null; the
    /// empty string when there was none). Append it to the terminal: the next
    /// poll yields only the bytes after it, so nothing is delivered twice.
    pub output: *mut c_char,
    /// Length of `output` in bytes (UTF-8; invalid bytes are replaced).
    pub output_len: usize,
    /// 1 when the retained stream hit the engine's cap and older output was
    /// dropped.
    pub truncated: u8,
    /// The program that ran (`argv[0]`), or null before it starts.
    pub tool: *mut c_char,
}

// --- batch 20261110: LINT waivers (both engines) ----------------------------

/// Verible's waiver file: `waive --rule=… --line=… --location="…"` lines.
pub const VD_WAIVER_FORMAT_VERIBLE: i32 = 0;
/// Slang's waiver TOML: `[[waivers]]` blocks with exactly one scope field
/// (`file` or `hier`) plus `diagnostic`/`regex` — and nothing else: slang refuses
/// an unknown key, which the append path checks before it writes.
pub const VD_WAIVER_FORMAT_SLANG: i32 = 1;

/// One waiver to append (borrowed for the duration of the call).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdWaiverEntry {
    /// Verible rule name (`line-length`, `module-filename`, …); null for a slang
    /// entry, which is scoped by `option_name` instead.
    pub rule: *const c_char,
    /// The file the waiver is scoped to. Required. Verible derives its anchored
    /// `--location` regex from it; slang writes it as the `file` scope.
    pub file: *const c_char,
    /// 1-based line the waiver applies to; 0 writes a file-wide verible waiver
    /// (no `--line`) and is ignored for slang, whose waivers have no line scope.
    pub line: u32,
    /// The last line of a verible line RANGE (`--line=N:M`); 0 or the same value
    /// as `line` writes the single line `N`. Ignored for slang.
    pub line_end: u32,
    /// The engine's option name (`implicit-net`, `unused-variable`, …). Required
    /// for a slang entry: a diagnostic without one (typically an error) can only
    /// be waived for its whole scope, and this ABI refuses to silently widen it.
    pub option_name: *const c_char,
    /// An explicit verible `--location` value (a RE2 regex); null derives
    /// `(^|/)<escaped file name>$` from `file`.
    pub location: *const c_char,
}

/// Options of one waiver operation. A zeroed struct is the default: entries are
/// deduplicated and the target's parent directory must already exist.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdWaiverOptions {
    /// 1 (default) skips an entry the file already carries.
    pub dedupe: u8,
    /// 1 creates the waiver file's missing parent directories; 0 (default)
    /// requires the parent to exist and reports the missing path.
    pub create_parent_dirs: u8,
}

/// The outcome of one waiver operation.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdWaiverResult {
    /// 1 when a file was written; 0 for a preview-only generate.
    pub written: u8,
    /// 1 when the entry was already present, so nothing was written.
    pub deduped: u8,
    /// The file the operation touched, or null.
    pub path: *mut c_char,
    /// The resulting file content (for a settings registration: the new stored
    /// value). Never null, so a caller can always preview before writing.
    pub content: *mut c_char,
    /// Entries in the resulting file (for a registration: paths in the value).
    pub entries: usize,
    /// Diagnostics that could not become a waiver (generate only).
    pub skipped: usize,
    /// Why an entry was skipped or a path refused; non-fatal.
    pub warnings: VdStringList,
}

#[cfg(test)]
mod tests {
    /// Names that are macros in Qt (`qobjectdefs.h`) or in common Windows/legacy
    /// headers. A struct FIELD or a function PARAMETER with one of these names
    /// breaks every translation unit that includes the macro's header, which is
    /// exactly what happened when a hierarchy field was called `signals`
    /// (`#define signals public`) and took down the whole Qt lane.
    const FORBIDDEN_NAMES: &[&str] = &[
        "signals",
        "slots",
        "emit",
        "foreach",
        "Q_FOREACH",
        "interface",
        "near",
        "far",
        "small",
        "IN",
        "OUT",
        "min",
        "max",
    ];

    fn generated_header() -> String {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("include")
            .join("vd_ffi.h");
        std::fs::read_to_string(&path)
            .unwrap_or_else(|error| panic!("reading {}: {error}", path.display()))
    }

    /// Drop `/* … */` and `// …` comments so a parenthesis inside documentation
    /// ("failed (I/O)") is never mistaken for a parameter list.
    fn strip_comments(header: &str) -> String {
        let mut out = String::with_capacity(header.len());
        let mut chars = header.chars().peekable();
        while let Some(character) = chars.next() {
            if character == '/' && chars.peek() == Some(&'*') {
                chars.next();
                let mut previous = '\0';
                for next in chars.by_ref() {
                    if previous == '*' && next == '/' {
                        break;
                    }
                    previous = next;
                }
                out.push(' ');
                continue;
            }
            if character == '/' && chars.peek() == Some(&'/') {
                for next in chars.by_ref() {
                    if next == '\n' {
                        break;
                    }
                }
                out.push('\n');
                continue;
            }
            out.push(character);
        }
        out
    }

    /// `(struct, field)` for every `typedef struct X { … } X;` body.
    fn struct_fields(header: &str) -> Vec<(String, String)> {
        let mut fields = Vec::new();
        let mut current: Option<String> = None;
        for line in header.lines() {
            let trimmed = line.trim();
            if let Some(rest) = trimmed.strip_prefix("typedef struct ") {
                current = Some(rest.trim_end_matches('{').trim().to_string());
                continue;
            }
            if trimmed.starts_with("} ") {
                current = None;
                continue;
            }
            let Some(struct_name) = &current else {
                continue;
            };
            if !trimmed.ends_with(';')
                || trimmed.starts_with("//")
                || trimmed.starts_with("/*")
                || trimmed.starts_with('*')
            {
                continue;
            }
            let declaration = trimmed.trim_end_matches(';');
            let declaration = declaration
                .split('[')
                .next()
                .unwrap_or(declaration)
                .trim_end();
            let Some(name) = declaration.split_whitespace().last() else {
                continue;
            };
            let name = name.trim_start_matches('*').to_string();
            if name
                .chars()
                .next()
                .is_some_and(|first| first.is_ascii_alphabetic())
            {
                fields.push((struct_name.clone(), name));
            }
        }
        fields
    }

    /// Remove `struct`/`enum`/`union` bodies (they contain members, not
    /// parameters) and preprocessor lines.
    fn strip_declarations_noise(header: &str) -> String {
        let chars: Vec<char> = header.chars().collect();
        let mut out = String::with_capacity(header.len());
        let mut index = 0_usize;
        while index < chars.len() {
            let rest: String = chars[index..chars.len().min(index + 6)].iter().collect();
            let is_keyword = ["struct", "enum", "union"].iter().any(|keyword| {
                rest.starts_with(keyword)
                    && chars
                        .get(index + keyword.len())
                        .is_none_or(|next| !next.is_alphanumeric() && *next != '_')
            });
            if is_keyword {
                // Copy the keyword, then skip a body if one follows before ';'.
                out.push_str(&rest[..rest.len().min(6)]);
                let mut cursor = index + 6;
                while cursor < chars.len() && chars[cursor] != '{' && chars[cursor] != ';' {
                    cursor += 1;
                }
                if cursor < chars.len() && chars[cursor] == '{' {
                    let mut depth = 0_i32;
                    while cursor < chars.len() {
                        match chars[cursor] {
                            '{' => depth += 1,
                            '}' => {
                                depth -= 1;
                                if depth == 0 {
                                    cursor += 1;
                                    break;
                                }
                            }
                            _ => {}
                        }
                        cursor += 1;
                    }
                    out.push_str(" BODY ");
                    index = cursor;
                    continue;
                }
                index += 6;
                continue;
            }
            if chars[index] == '#' && (index == 0 || chars[index - 1] == '\n') {
                while index < chars.len() && chars[index] != '\n' {
                    index += 1;
                }
                continue;
            }
            out.push(chars[index]);
            index += 1;
        }
        out
    }

    /// `(function, parameter)` for every declaration `… name(args);`, for both
    /// one-line and cbindgen's wrapped form.
    fn function_parameters(header: &str) -> Vec<(String, String)> {
        let cleaned = strip_declarations_noise(&strip_comments(header));
        let chars: Vec<char> = cleaned.chars().collect();
        let mut parameters = Vec::new();
        let mut index = 0_usize;
        while index < chars.len() {
            if chars[index] != '(' {
                index += 1;
                continue;
            }
            // The function name is the identifier left of the '('.
            let mut cursor = index;
            while cursor > 0 && chars[cursor - 1].is_whitespace() {
                cursor -= 1;
            }
            let end_of_name = cursor;
            while cursor > 0 && (chars[cursor - 1].is_alphanumeric() || chars[cursor - 1] == '_') {
                cursor -= 1;
            }
            let function: String = chars[cursor..end_of_name].iter().collect();
            // Capture the argument list.
            let mut close = index;
            let mut inner_depth = 0_i32;
            while close < chars.len() {
                match chars[close] {
                    '(' => inner_depth += 1,
                    ')' => {
                        inner_depth -= 1;
                        if inner_depth == 0 {
                            break;
                        }
                    }
                    _ => {}
                }
                close += 1;
            }
            if close >= chars.len() {
                break;
            }
            // Only a declaration ends with ';' right after the ')'.
            let mut after = close + 1;
            while after < chars.len() && chars[after].is_whitespace() {
                after += 1;
            }
            let is_declaration = chars.get(after) == Some(&';');
            if is_declaration && !function.is_empty() {
                let arguments: String = chars[index + 1..close].iter().collect();
                for argument in arguments.split(',') {
                    let argument = argument.trim();
                    if argument.is_empty() || argument == "void" {
                        continue;
                    }
                    if let Some(name) = argument.split_whitespace().last() {
                        let name = name.trim_start_matches('*');
                        if name
                            .chars()
                            .next()
                            .is_some_and(|first| first.is_ascii_alphabetic())
                        {
                            parameters.push((function.clone(), name.to_string()));
                        }
                    }
                }
            }
            index = close;
        }
        parameters
    }

    fn collisions(header: &str) -> Vec<String> {
        let header = strip_comments(header);
        let mut found: Vec<String> = struct_fields(&header)
            .into_iter()
            .filter(|(_, field)| FORBIDDEN_NAMES.contains(&field.as_str()))
            .map(|(structure, field)| format!("field {structure}.{field}"))
            .collect();
        found.extend(
            function_parameters(&header)
                .into_iter()
                .filter(|(_, parameter)| FORBIDDEN_NAMES.contains(&parameter.as_str()))
                .map(|(function, parameter)| format!("parameter {function}({parameter})")),
        );
        found
    }

    #[test]
    fn no_generated_abi_name_collides_with_a_qt_or_windows_macro() {
        let header = generated_header();
        let found = collisions(&header);
        assert!(
            found.is_empty(),
            "these ABI names are macros in Qt/Windows and break every Qt translation unit that \
             includes vd_ffi.h: {found:?} — rename the Rust field/parameter (the header is \
             generated). See evidence/w6-hier/02-qt-macro-guard.txt"
        );
        // The historical defect must stay fixed.
        assert!(
            !struct_fields(&header)
                .iter()
                .any(|(_, field)| field == "signals"),
            "VdHierarchy.signal_list: `signals` is `#define signals public` in Qt"
        );
    }

    #[test]
    fn the_scanner_catches_a_known_collision() {
        // Proves the guard is not vacuous: the exact shape that broke the lane.
        let synthetic = "typedef struct VdProbe {\n   int signals;\n   int ok;\n} VdProbe;\n\
                         int vd_probe(int slots);\n";
        let found = collisions(synthetic);
        assert_eq!(found.len(), 2, "{found:?}");
        assert!(found.iter().any(|hit| hit.contains("VdProbe.signals")));
        assert!(found.iter().any(|hit| hit.contains("vd_probe(slots)")));
    }
}
