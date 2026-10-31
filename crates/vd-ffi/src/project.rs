//! Project model + `.f` filelist expansion (service 1 of 7).
//!
//! The seam owns the **shape** of the C entry point; the expansion itself is
//! `vd-config`'s public `Loader`/`ProjectContext` (C4: the project model is a
//! Rust service, and `vd-config` is the crate that resolves the root, the
//! configured filelists, `+incdir+`, `+define+` and nested `-f`).
//!
//! What this module adds on top of `vd-config` is flattening: the C side gets
//! one ordered file list with duplicates removed, plus the include dirs, the
//! defines and the warnings collected while parsing. The Qt side never parses a
//! `.f` file itself.

use crate::capi::{
    c_string_list, free_string_list, VdError, VdProjectInfo, VdStringList, VD_ERR_SERVICE,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::collections::BTreeSet;
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// Everything one project expansion produces.
pub(crate) struct ProjectData {
    /// Expanded file list, filelist order, duplicates removed.
    pub(crate) files: Vec<String>,
    /// Include directories from `+incdir+` entries.
    pub(crate) include_dirs: Vec<String>,
    /// Defines rendered as `NAME` or `NAME=VALUE`.
    pub(crate) defines: Vec<String>,
    /// Non-fatal diagnostics, already phrased for the UI.
    pub(crate) warnings: Vec<String>,
}

impl ProjectData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned lists must be released with `vd_project_info_free`.
    pub(crate) unsafe fn into_c(self) -> VdProjectInfo {
        VdProjectInfo {
            files: c_string_list(&self.files),
            include_dirs: c_string_list(&self.include_dirs),
            defines: c_string_list(&self.defines),
            warnings: c_string_list(&self.warnings),
        }
    }
}

/// Expand the project rooted at (or above) `start_dir`.
pub(crate) fn parse(start_dir: &Path) -> Result<ProjectData, VdError> {
    let loaded = vd_config::Loader::new(start_dir).load().map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!(
                "cannot load the project at {}: {error}",
                start_dir.display()
            ),
        )
    })?;

    let mut files = Vec::new();
    let mut seen: BTreeSet<PathBuf> = BTreeSet::new();
    for filelist in &loaded.project.filelists {
        for entry in &filelist.entries {
            if seen.insert(entry.clone()) {
                files.push(entry.display().to_string());
            }
        }
    }
    let include_dirs = loaded
        .project
        .include_dirs
        .iter()
        .map(|dir| dir.display().to_string())
        .collect();
    let defines = loaded
        .project
        .defines
        .iter()
        .map(|define| match &define.value {
            Some(value) => format!("{}={value}", define.name),
            None => define.name.clone(),
        })
        .collect();
    let warnings = loaded
        .warnings
        .iter()
        .map(|warning| match (&warning.file, warning.line) {
            (Some(file), Some(line)) => format!(
                "{}: {}:{}: {}",
                warning.code,
                file.display(),
                line,
                warning.message
            ),
            (Some(file), None) => {
                format!("{}: {}: {}", warning.code, file.display(), warning.message)
            }
            _ => format!("{}: {}", warning.code, warning.message),
        })
        .collect();

    Ok(ProjectData {
        files,
        include_dirs,
        defines,
        warnings,
    })
}

/// Start a project/.f expansion job.
///
/// Returns the job id, or `0` when the arguments were rejected (the reason is
/// readable with `vd_service_error`).
///
/// # Safety
///
/// `service` must be null or a live handle; `start_dir` must be null or a valid
/// NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_project_parse(
    service: *mut VdService,
    start_dir: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(start_dir) = crate::capi::c_str(start_dir) else {
        state.record_error(VdError::new(
            crate::capi::VD_ERR_INVALID_ARGUMENT,
            "start_dir must be a valid UTF-8 path",
        ));
        return 0;
    };
    let start = PathBuf::from(start_dir);
    state.start(move || parse(&start).map(JobValue::Project))
}

/// Poll a project job.
///
/// `VD_STATUS_READY` fills `out`; `VD_STATUS_FAILED` leaves it untouched and the
/// reason is readable with `vd_service_error`.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdProjectInfo`].
#[no_mangle]
pub unsafe extern "C" fn vd_project_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdProjectInfo,
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
        Polled::Ready(JobValue::Project(data)) => {
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

/// Release a filled [`VdProjectInfo`]; null-safe and idempotent.
///
/// # Safety
///
/// `info` must be null or point to a struct filled by [`vd_project_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_project_info_free(info: *mut VdProjectInfo) {
    if info.is_null() {
        return;
    }
    let info = &mut *info;
    free_string_list(&mut info.files);
    free_string_list(&mut info.include_dirs);
    free_string_list(&mut info.defines);
    free_string_list(&mut info.warnings);
    *info = VdProjectInfo {
        files: VdStringList::empty(),
        include_dirs: VdStringList::empty(),
        defines: VdStringList::empty(),
        warnings: VdStringList::empty(),
    };
}
