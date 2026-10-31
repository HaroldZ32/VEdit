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
    c_string, free_string, VdError, VdLspInfo, VD_ERR_INVALID_ARGUMENT, VD_ERR_NOT_FOUND,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

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
