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

// --- job-based read-only queries for the VCS panel (T23, async since T99) ---
//
// `vd_vcs_status`, `vd_vcs_info_job` and `vd_vcs_diff_job` are the service-layer
// jobs; the panel polls them and never waits for a `git`/`svn` subprocess on the
// GUI thread. All of them go through the same read-only allow-list
// (`ProcessVcsService` -> `SystemRunner`, which refuses any write subcommand
// before spawning).

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

fn owned(text: Option<&str>) -> *mut c_char {
    text.map_or(std::ptr::null_mut(), c_string)
}

// --- job payloads for the two read-only panel queries (T99) ----------------
//
// T24 N-1: `vd_vcs_info` / `vd_vcs_diff` used to be synchronous C calls, so the
// panel ran one `git`/`svn` subprocess on the GUI thread. They are now job-based
// like `vd_vcs_status`: the start call only spawns the worker and returns the id,
// and the caller polls. The read-only gate is unchanged (the jobs call the same
// `ProcessVcsService`, whose runner refuses write subcommands before spawning).

/// Branch + revision identity, owned by the job until it is converted to C.
pub(crate) struct VcsInfoData {
    pub(crate) backend: i32,
    pub(crate) branch: Option<String>,
    pub(crate) revision: Option<String>,
    pub(crate) note: Option<String>,
}

impl VcsInfoData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_info_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsInfo {
        VdVcsInfo {
            backend: self.backend,
            branch: owned(self.branch.as_deref()),
            revision: owned(self.revision.as_deref()),
            note: owned(self.note.as_deref()),
        }
    }
}

/// One file's unified diff, owned by the job until it is converted to C.
pub(crate) struct VcsDiffData {
    pub(crate) path: String,
    pub(crate) text: String,
    pub(crate) binary: bool,
    pub(crate) note: Option<String>,
}

impl VcsDiffData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_vcs_diff_free`.
    pub(crate) unsafe fn into_c(self) -> VdVcsDiff {
        VdVcsDiff {
            path: owned(Some(&self.path)),
            text: owned(Some(&self.text)),
            binary: i32::from(self.binary),
            note: owned(self.note.as_deref()),
        }
    }
}

fn info_data(root: &Path) -> VcsInfoData {
    let service = vd_vcs::service::ProcessVcsService::production();
    let info = vd_vcs::service::VcsService::info(&service, root);
    VcsInfoData {
        backend: info.backend.as_i32(),
        branch: info.branch,
        revision: info.revision,
        note: info.note,
    }
}

fn diff_data(root: &Path, path: &Path) -> VcsDiffData {
    let service = vd_vcs::service::ProcessVcsService::production();
    let diff = vd_vcs::service::VcsService::file_diff(&service, root, &path.display().to_string());
    VcsDiffData {
        path: diff.path,
        text: diff.text,
        binary: diff.binary,
        note: diff.note,
    }
}

/// Starts a branch + revision job for `root`.
///
/// Returns the job id, or `0` when the arguments were rejected. The panel polls
/// with [`vd_vcs_info_poll`], so the GUI thread never waits for `git`/`svn`.
///
/// # Safety
///
/// `service` must be null or live; `root` must be null or a valid NUL-terminated
/// string.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_job(service: *mut VdService, root: *const c_char) -> u64 {
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
    state.start(move || Ok(JobValue::VcsInfo(info_data(&root))))
}

/// Polls a branch + revision job started by [`vd_vcs_info_job`].
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsInfo`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_info_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsInfo,
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
        Polled::Ready(JobValue::VcsInfo(data)) => {
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

/// Releases the strings of a [`VdVcsInfo`] (the struct itself is by value).
///
/// # Safety
///
/// `info` must be null or a struct returned by `vd_vcs_info_poll` that was not freed.
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

/// Starts a unified-diff job for one file of the working tree containing `root`.
///
/// Returns the job id, or `0` when the arguments were rejected. The panel polls
/// with [`vd_vcs_diff_poll`], so the GUI thread never waits for `git diff`.
///
/// # Safety
///
/// `service` must be null or live; `root` and `path` must be null or valid
/// NUL-terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_job(
    service: *mut VdService,
    root: *const c_char,
    path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (root, path) = (crate::capi::c_str(root), crate::capi::c_str(path));
    let (Some(root), Some(path)) = (root, path) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root and path must be valid UTF-8 paths",
        ));
        return 0;
    };
    let (root, path) = (PathBuf::from(root), PathBuf::from(path));
    state.start(move || Ok(JobValue::VcsDiff(diff_data(&root, &path))))
}

/// Polls a diff job started by [`vd_vcs_diff_job`].
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdVcsDiff`].
#[no_mangle]
pub unsafe extern "C" fn vd_vcs_diff_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdVcsDiff,
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
        Polled::Ready(JobValue::VcsDiff(data)) => {
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

/// Releases the strings of a [`VdVcsDiff`].
///
/// # Safety
///
/// `diff` must be null or a struct returned by `vd_vcs_diff_poll` that was not freed.
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::capi::VdError;
    use crate::service::{vd_service_create, vd_service_destroy, vd_service_error, POLL_MISUSE};
    use std::time::{Duration, Instant};

    fn temp_dir(tag: &str) -> Result<PathBuf, String> {
        let mut path = std::env::temp_dir();
        path.push(format!("vd-ffi-vcs-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path)
            .map_err(|error| format!("cannot create {}: {error}", path.display()))?;
        Ok(path)
    }

    fn c_path(path: &Path) -> Result<std::ffi::CString, String> {
        std::ffi::CString::new(path.display().to_string())
            .map_err(|error| format!("path contains a NUL: {error}"))
    }

    fn poll_info(service: *mut VdService, job: u64) -> (i32, VdVcsInfo) {
        let mut out = VdVcsInfo {
            backend: 0,
            branch: std::ptr::null_mut(),
            revision: std::ptr::null_mut(),
            note: std::ptr::null_mut(),
        };
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let state = unsafe { vd_vcs_info_poll(service, job, &mut out) };
            if state != VD_STATUS_PENDING || Instant::now() >= deadline {
                return (state, out);
            }
            std::thread::sleep(Duration::from_millis(2));
        }
    }

    fn poll_diff(service: *mut VdService, job: u64) -> (i32, VdVcsDiff) {
        let mut out = VdVcsDiff {
            path: std::ptr::null_mut(),
            text: std::ptr::null_mut(),
            binary: 0,
            note: std::ptr::null_mut(),
        };
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let state = unsafe { vd_vcs_diff_poll(service, job, &mut out) };
            if state != VD_STATUS_PENDING || Instant::now() >= deadline {
                return (state, out);
            }
            std::thread::sleep(Duration::from_millis(2));
        }
    }

    #[test]
    fn info_job_reaches_ready_for_a_plain_directory() -> Result<(), String> {
        let dir = temp_dir("info")?;
        let root = c_path(&dir)?;
        let service = vd_service_create();
        let job = unsafe { vd_vcs_info_job(service, root.as_ptr()) };
        assert_ne!(job, 0, "the start call must return a job id");
        let (state, mut out) = poll_info(service, job);
        assert_eq!(state, VD_STATUS_READY, "the job must complete");
        assert_eq!(out.backend, 0, "a plain directory has no backend");
        assert!(!out.note.is_null(), "the empty state must carry a note");
        let note = unsafe { std::ffi::CStr::from_ptr(out.note) };
        assert!(!note.to_bytes().is_empty());
        unsafe { vd_vcs_info_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn diff_job_reaches_ready_for_a_plain_directory() -> Result<(), String> {
        let dir = temp_dir("diff")?;
        let root = c_path(&dir)?;
        let path = c_path(Path::new("rtl/top.sv"))?;
        let service = vd_service_create();
        let job = unsafe { vd_vcs_diff_job(service, root.as_ptr(), path.as_ptr()) };
        assert_ne!(job, 0);
        let (state, mut out) = poll_diff(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert!(
            out.text.is_null() || unsafe { *out.text } == 0,
            "no diff off a repository"
        );
        assert!(!out.note.is_null(), "the reason must be reported");
        unsafe { vd_vcs_diff_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn polling_a_job_with_the_wrong_kind_is_misuse() -> Result<(), String> {
        let dir = temp_dir("wrong-kind")?;
        let root = c_path(&dir)?;
        let service = vd_service_create();
        // Start a status job, then poll it through the info poll: the id belongs
        // to another service, so the call must be rejected (never silently read).
        let status_job = unsafe { vd_vcs_status(service, root.as_ptr()) };
        assert_ne!(status_job, 0);
        let mut out = VdVcsInfo {
            backend: 0,
            branch: std::ptr::null_mut(),
            revision: std::ptr::null_mut(),
            note: std::ptr::null_mut(),
        };
        // Poll with the WRONG poller until the status job is terminal: reading a
        // status payload through the info poll must be refused, never returned.
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut state = VD_STATUS_PENDING;
        while state == VD_STATUS_PENDING && Instant::now() < deadline {
            state = unsafe { vd_vcs_info_poll(service, status_job, &mut out) };
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(state, POLL_MISUSE, "wrong-kind poll must be refused");
        let mut error = VdError {
            code: 0,
            message: [0; crate::capi::VD_ERROR_MESSAGE_CAPACITY],
        };
        assert_eq!(unsafe { vd_service_error(service, &mut error) }, 1);
        assert_eq!(error.code, crate::capi::VD_ERR_WRONG_JOB_KIND);
        unsafe { vd_vcs_info_free(&mut out) };
        unsafe { vd_service_destroy(service) };
        let _ = std::fs::remove_dir_all(&dir);
        Ok(())
    }

    #[test]
    fn invalid_arguments_return_no_job() {
        let service = vd_service_create();
        assert_eq!(unsafe { vd_vcs_info_job(service, std::ptr::null()) }, 0);
        assert_eq!(
            unsafe { vd_vcs_diff_job(service, std::ptr::null(), std::ptr::null()) },
            0
        );
        let mut error = VdError {
            code: 0,
            message: [0; crate::capi::VD_ERROR_MESSAGE_CAPACITY],
        };
        assert_eq!(unsafe { vd_service_error(service, &mut error) }, 1);
        assert_eq!(error.code, VD_ERR_INVALID_ARGUMENT);
        unsafe { vd_service_destroy(service) };
    }
}
