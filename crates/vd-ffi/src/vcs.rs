//! VCS backend (service 6 of 7).
//!
//! Backend selection, branch and per-file changes come from `vd-vcs`; this
//! module is the C projection. "No repository" is a normal result
//! (`backend = 0`) with a note, never an error — the panel has to render an
//! empty state, not a failure dialog.

use crate::capi::{
    c_string, free_string, VdError, VdVcsChange, VdVcsStatus, VD_ERR_INVALID_ARGUMENT,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// One changed path, owned.
pub(crate) struct VcsChangeData {
    pub(crate) path: String,
    pub(crate) kind: i32,
}

/// A status snapshot, owned.
pub(crate) struct VcsData {
    pub(crate) backend: i32,
    pub(crate) branch: Option<String>,
    pub(crate) changes: Vec<VcsChangeData>,
    pub(crate) note: Option<String>,
}

impl VcsData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_status_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsStatus {
        let items: Vec<VdVcsChange> = self
            .changes
            .into_iter()
            .map(|change| VdVcsChange {
                path: c_string(&change.path),
                kind: change.kind,
            })
            .collect();
        let (count, changes) = if items.is_empty() {
            (0, std::ptr::null_mut())
        } else {
            (
                items.len(),
                Box::into_raw(items.into_boxed_slice()) as *mut VdVcsChange,
            )
        };
        VdVcsStatus {
            backend: self.backend,
            branch: match &self.branch {
                Some(branch) => c_string(branch),
                None => std::ptr::null_mut(),
            },
            count,
            changes,
            note: match &self.note {
                Some(note) => c_string(note),
                None => std::ptr::null_mut(),
            },
        }
    }
}

/// Query the working tree containing `root`.
pub(crate) fn status(root: &Path) -> Result<VcsData, VdError> {
    if !root.is_dir() {
        return Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("not a directory: {}", root.display()),
        ));
    }
    let report = vd_vcs::status(root);
    Ok(VcsData {
        backend: report.backend.as_i32(),
        branch: report.branch,
        changes: report
            .changes
            .into_iter()
            .map(|change| VcsChangeData {
                path: change.path,
                kind: change.kind.as_i32(),
            })
            .collect(),
        note: report.note,
    })
}

/// Start a VCS status job for `root`.
///
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_status(service: *mut VdService, root: *const c_char) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(root) = crate::capi::c_str(root) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root must be a valid UTF-8 path",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    state.start(move || status(&root).map(JobValue::Vcs))
}

/// Poll a VCS job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsStatus`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsStatus,
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
        Polled::Ready(JobValue::Vcs(data)) => {
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

/// Release a filled [`VdVcsStatus`]; null-safe.
///
/// # Safety
///
/// `status` must be null or point to a struct filled by [`vd_vcs_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_status_free(status: *mut VdVcsStatus) {
    if status.is_null() {
        return;
    }
    let status = &mut *status;
    if !status.changes.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(status.changes, status.count);
        for change in slice.iter_mut() {
            free_string(change.path);
            change.path = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            status.changes,
            status.count,
        )));
    }
    free_string(status.branch);
    free_string(status.note);
    status.backend = 0;
    status.branch = std::ptr::null_mut();
    status.count = 0;
    status.changes = std::ptr::null_mut();
    status.note = std::ptr::null_mut();
}

// --- synchronous read-only queries for the VCS panel (T23) ------------------
//
// The job-based `vd_vcs_status`/`vd_vcs_poll` pair is the service-layer API; the
// panel needs the identity (branch + revision) and one file's diff, which are
// small read-only queries. These entry points are synchronous on purpose and
// stay inside the same read-only allow-list (they go through `ProcessVcsService`
// -> `SystemRunner`, which refuses any write subcommand before spawning).

use std::ffi::CStr;

/// Branch + revision of the working tree containing `root` (owned strings).
#[repr(C)]
pub struct VdVcsInfo {
    /// Backend: 0 none, 1 git, 2 svn.
    pub backend: i32,
    /// Branch name, or NULL.
    pub branch: *mut c_char,
    /// Short revision, or NULL.
    pub revision: *mut c_char,
    /// Why the identity is unavailable, or NULL.
    pub note: *mut c_char,
}

/// One file's unified diff (owned strings).
#[repr(C)]
pub struct VdVcsDiff {
    /// Path asked for, or NULL.
    pub path: *mut c_char,
    /// Unified diff text, or NULL.
    pub text: *mut c_char,
    /// 1 when the tool reported binary content.
    pub binary: i32,
    /// Why the diff is empty, or NULL.
    pub note: *mut c_char,
}

fn root_from(ptr: *const c_char) -> Option<PathBuf> {
    if ptr.is_null() {
        return None;
    }
    let bytes = unsafe { CStr::from_ptr(ptr) }.to_bytes();
    std::str::from_utf8(bytes).ok().map(PathBuf::from)
}

fn owned(text: Option<&str>) -> *mut c_char {
    text.map_or(std::ptr::null_mut(), c_string)
}

/// Branch + revision of the working tree containing `root`.
///
/// # Safety
///
/// `root` must be null or a valid NUL-terminated UTF-8 string; the result must be
/// released with `vd_vcs_info_free`.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info(root: *const c_char) -> VdVcsInfo {
    let Some(root) = root_from(root) else {
        return VdVcsInfo {
            backend: 0,
            branch: std::ptr::null_mut(),
            revision: std::ptr::null_mut(),
            note: owned(Some("root must be a valid UTF-8 path")),
        };
    };
    let service = vd_vcs::service::ProcessVcsService::production();
    let info = vd_vcs::service::VcsService::info(&service, &root);
    VdVcsInfo {
        backend: info.backend.as_i32(),
        branch: owned(info.branch.as_deref()),
        revision: owned(info.revision.as_deref()),
        note: owned(info.note.as_deref()),
    }
}

/// Releases the strings of a [`VdVcsInfo`] (the struct itself is by value).
///
/// # Safety
///
/// `info` must be null or a struct returned by `vd_vcs_info` that was not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_free(info: *mut VdVcsInfo) {
    let Some(info) = (if info.is_null() {
        None
    } else {
        Some(&mut *info)
    }) else {
        return;
    };
    for field in [&mut info.branch, &mut info.revision, &mut info.note] {
        if !field.is_null() {
            free_string(*field);
            *field = std::ptr::null_mut();
        }
    }
}

/// Unified diff of one file of the working tree containing `root`.
///
/// # Safety
///
/// `root` and `path` must be null or valid NUL-terminated UTF-8 strings; the
/// result must be released with `vd_vcs_diff_free`.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff(root: *const c_char, path: *const c_char) -> VdVcsDiff {
    let (Some(root), Some(path)) = (root_from(root), root_from(path)) else {
        return VdVcsDiff {
            path: std::ptr::null_mut(),
            text: std::ptr::null_mut(),
            binary: 0,
            note: owned(Some("root and path must be valid UTF-8 paths")),
        };
    };
    let service = vd_vcs::service::ProcessVcsService::production();
    let diff = vd_vcs::service::VcsService::file_diff(&service, &root, &path.display().to_string());
    VdVcsDiff {
        path: owned(Some(&diff.path)),
        text: owned(Some(&diff.text)),
        binary: i32::from(diff.binary),
        note: owned(diff.note.as_deref()),
    }
}

/// Releases the strings of a [`VdVcsDiff`].
///
/// # Safety
///
/// `diff` must be null or a struct returned by `vd_vcs_diff` that was not freed.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_free(diff: *mut VdVcsDiff) {
    let Some(diff) = (if diff.is_null() {
        None
    } else {
        Some(&mut *diff)
    }) else {
        return;
    };
    for field in [&mut diff.path, &mut diff.text, &mut diff.note] {
        if !field.is_null() {
            free_string(*field);
            *field = std::ptr::null_mut();
        }
    }
}
