//! SLANG deep-lint (service 3 of 7).
//!
//! The lint run itself is `vd-deeplint` (subprocess + `--diag-json` parsing and
//! the byte-column → UTF-16 position conversion); this module only projects the
//! result onto the C ABI. One job = one file, and the job runs on the registry's
//! worker thread, so a slow slang invocation cannot block the UI thread.
//!
//! The binary is resolved by `vd-deeplint` (explicit override → `VEDIT_SLANG` →
//! packaged paths; `PATH` is never consulted). An absent binary is a typed job
//! failure with a readable reason, never an empty success.

use crate::capi::{
    c_string, free_string, VdDiagnostic, VdDiagnostics, VdError, VD_ERR_INVALID_ARGUMENT,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// One diagnostic, in the editor's position model.
pub(crate) struct DeeplintDiagnostic {
    pub(crate) severity: i32,
    pub(crate) line: u32,
    pub(crate) column: u32,
    pub(crate) end_line: u32,
    pub(crate) end_column: u32,
    pub(crate) code: String,
    pub(crate) message: String,
}

/// All diagnostics of one run.
pub(crate) struct DeeplintData {
    pub(crate) diagnostics: Vec<DeeplintDiagnostic>,
}

impl DeeplintData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned array must be released with `vd_diagnostics_free`.
    pub(crate) unsafe fn into_c(self) -> VdDiagnostics {
        if self.diagnostics.is_empty() {
            return VdDiagnostics {
                count: 0,
                items: std::ptr::null_mut(),
            };
        }
        let items: Vec<VdDiagnostic> = self
            .diagnostics
            .into_iter()
            .map(|diagnostic| VdDiagnostic {
                severity: diagnostic.severity,
                line: diagnostic.line,
                column: diagnostic.column,
                end_line: diagnostic.end_line,
                end_column: diagnostic.end_column,
                code: c_string(&diagnostic.code),
                message: c_string(&diagnostic.message),
            })
            .collect();
        VdDiagnostics {
            count: items.len(),
            items: Box::into_raw(items.into_boxed_slice()) as *mut VdDiagnostic,
        }
    }
}

fn severity_code(severity: &vd_deeplint::Severity) -> i32 {
    match severity {
        vd_deeplint::Severity::Error => 0,
        vd_deeplint::Severity::Warning => 1,
        vd_deeplint::Severity::Note => 2,
        vd_deeplint::Severity::Ignored => 3,
        vd_deeplint::Severity::Unknown(_) => 4,
    }
}

/// Run one lint and map its diagnostics.
pub(crate) fn lint(file: &Path, binary: Option<&Path>) -> Result<DeeplintData, VdError> {
    let options = vd_deeplint::LintOptions {
        timeout: vd_deeplint::DEFAULT_TIMEOUT,
        max_json_bytes: vd_deeplint::DEFAULT_MAX_JSON_BYTES,
        max_diagnostics: vd_deeplint::DEFAULT_MAX_DIAGNOSTICS,
        binary: binary.map(Path::to_path_buf),
    };
    let report = vd_deeplint::lint(file, &options);
    if let Some(failure) = &report.failure {
        return Err(VdError::new(
            crate::capi::VD_ERR_SERVICE,
            &format!("slang: {}", failure.user_message()),
        ));
    }
    let diagnostics = report
        .diagnostics
        .iter()
        .map(|diagnostic| {
            let (line, column, end_line, end_column) = match &diagnostic.editor_range {
                Some(range) => (
                    range.start.line,
                    range.start.character,
                    range.end.line,
                    range.end.character,
                ),
                None => (0, 0, 0, 0),
            };
            DeeplintDiagnostic {
                severity: severity_code(&diagnostic.severity),
                line,
                column,
                end_line,
                end_column,
                code: diagnostic.code.clone(),
                message: diagnostic.message.clone(),
            }
        })
        .collect();
    Ok(DeeplintData { diagnostics })
}

/// Start a SLANG lint job for one file.
///
/// `binary_override` may be null (the service then resolves the binary itself).
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; the string arguments must be null or valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_lint(
    service: *mut VdService,
    file: *const c_char,
    binary_override: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(file) = crate::capi::c_str(file) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "file must be a valid UTF-8 path",
        ));
        return 0;
    };
    let binary = if binary_override.is_null() {
        None
    } else {
        match crate::capi::c_str(binary_override) {
            Some(path) => Some(PathBuf::from(path)),
            None => {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "binary_override must be a valid UTF-8 path or null",
                ));
                return 0;
            }
        }
    };
    let file = PathBuf::from(file);
    state.start(move || lint(&file, binary.as_deref()).map(JobValue::Deeplint))
}

/// Poll a lint job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdDiagnostics`].
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdDiagnostics,
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
        Polled::Ready(JobValue::Deeplint(data)) => {
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

/// Release a filled [`VdDiagnostics`]; null-safe.
///
/// # Safety
///
/// `diagnostics` must be null or point to a struct filled by
/// [`vd_deeplint_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_diagnostics_free(diagnostics: *mut VdDiagnostics) {
    if diagnostics.is_null() {
        return;
    }
    let diagnostics = &mut *diagnostics;
    if !diagnostics.items.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(diagnostics.items, diagnostics.count);
        for item in slice.iter_mut() {
            free_string(item.code);
            free_string(item.message);
            item.code = std::ptr::null_mut();
            item.message = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            diagnostics.items,
            diagnostics.count,
        )));
    }
    diagnostics.count = 0;
    diagnostics.items = std::ptr::null_mut();
}
