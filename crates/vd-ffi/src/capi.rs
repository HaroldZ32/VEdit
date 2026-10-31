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
}

/// One slang diagnostic.
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
    /// slang diagnostic code (`UndeclaredIdentifier`, …).
    pub code: *mut c_char,
    /// Human-readable message.
    pub message: *mut c_char,
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
    /// 1-based byte column.
    pub column: u32,
    /// Trimmed, truncated matched line.
    pub preview: *mut c_char,
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
    /// 1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown.
    pub kind: i32,
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
