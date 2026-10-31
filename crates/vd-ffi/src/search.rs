//! Global search index (service 5 of 7).
//!
//! The index and the query are `vd-search`; this module is the C projection.
//! One job = one query over the project's expanded file list (the same list the
//! project service produced, so the editor searches exactly what it lints), and
//! the job runs on the worker thread — a query over a large tree never runs on
//! the UI thread.

use crate::capi::{
    c_str_array, c_string, free_string, VdError, VdSearchHit, VdSearchResults,
    VD_ERR_INVALID_ARGUMENT, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// One hit, owned.
pub(crate) struct SearchHitData {
    pub(crate) path: String,
    pub(crate) line: u32,
    pub(crate) column: u32,
    pub(crate) preview: String,
}

/// All hits of one query.
pub(crate) struct SearchData {
    pub(crate) hits: Vec<SearchHitData>,
}

impl SearchData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned array must be released with `vd_search_results_free`.
    pub(crate) unsafe fn into_c(self) -> VdSearchResults {
        if self.hits.is_empty() {
            return VdSearchResults {
                count: 0,
                items: std::ptr::null_mut(),
            };
        }
        let items: Vec<VdSearchHit> = self
            .hits
            .into_iter()
            .map(|hit| VdSearchHit {
                path: c_string(&hit.path),
                line: hit.line,
                column: hit.column,
                preview: c_string(&hit.preview),
            })
            .collect();
        VdSearchResults {
            count: items.len(),
            items: Box::into_raw(items.into_boxed_slice()) as *mut VdSearchHit,
        }
    }
}

/// Query the index over `files`, all resolved under `root`.
pub(crate) fn query(
    root: &Path,
    files: &[String],
    needle: &str,
    limit: usize,
) -> Result<SearchData, VdError> {
    let index = vd_search::SearchIndex::open(root, files.iter().map(PathBuf::from))
        .map_err(|error| VdError::new(VD_ERR_INVALID_ARGUMENT, &error.to_string()))?;
    let hits = index
        .query(needle, limit)
        .into_iter()
        .map(|hit| SearchHitData {
            path: hit.path.display().to_string(),
            line: hit.line,
            column: hit.column,
            preview: hit.preview,
        })
        .collect();
    Ok(SearchData { hits })
}

/// Start a search job.
///
/// `files` is a C array of `file_count` borrowed strings. Returns the job id, or
/// `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `root`/`needle` must be null or valid
/// NUL-terminated strings; `files` must be null or point to `file_count` valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_search_query(
    service: *mut VdService,
    root: *const c_char,
    files: *const *const c_char,
    file_count: usize,
    needle: *const c_char,
    limit: usize,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(root), Some(needle), Some(files)) = (
        crate::capi::c_str(root),
        crate::capi::c_str(needle),
        c_str_array(files, file_count),
    ) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "root, needle and files must be valid UTF-8 strings",
        ));
        return 0;
    };
    let root = PathBuf::from(root);
    let needle = needle.to_string();
    state.start(move || query(&root, &files, &needle, limit).map(JobValue::Search))
}

/// Poll a search job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdSearchResults`].
#[no_mangle]
pub unsafe extern "C" fn vd_search_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdSearchResults,
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
        Polled::Ready(JobValue::Search(data)) => {
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

/// Release a filled [`VdSearchResults`]; null-safe.
///
/// # Safety
///
/// `results` must be null or point to a struct filled by [`vd_search_poll`] and
/// not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_search_results_free(results: *mut VdSearchResults) {
    if results.is_null() {
        return;
    }
    let results = &mut *results;
    if !results.items.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(results.items, results.count);
        for item in slice.iter_mut() {
            free_string(item.path);
            free_string(item.preview);
            item.path = std::ptr::null_mut();
            item.preview = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            results.items,
            results.count,
        )));
    }
    results.count = 0;
    results.items = std::ptr::null_mut();
}
