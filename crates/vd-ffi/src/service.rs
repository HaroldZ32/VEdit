//! The opaque service handle and its non-blocking job registry.
//!
//! # Why a job registry and not blocking calls
//!
//! C2 forbids blocking the UI thread across the C ABI. Every service entry point
//! therefore returns a `u64` job id immediately: the work runs on a dedicated
//! worker thread, and the caller **polls** (`vd_<service>_poll`) for the typed
//! result. The start call never waits for the service, never touches the
//! filesystem on the calling thread, and never spawns a process there.
//!
//! # Lifetime
//!
//! `vd_service_create` boxes a [`ServiceState`] and hands its address out as an
//! opaque `VdService *`. The handle's real state is an `Arc<Inner>`, which the
//! worker threads clone, so `vd_service_destroy` while a job is still running
//! cannot use freed memory: the job finishes into an `Inner` that the worker
//! still owns, and the result is dropped when the last reference dies.
//!
//! # Failure model
//!
//! A job that fails carries a [`VdError`]; polling it moves that error into the
//! service's last-error slot, so the caller reads the reason with
//! `vd_service_error` right after a `VD_STATUS_FAILED` poll. A panicking worker
//! is caught (`catch_unwind`) and turned into `VD_ERR_INTERNAL`, so a bug in one
//! service cannot leave the UI polling a job that will never finish.

use crate::capi::{VdError, VD_ERR_CANCELLED, VD_ERR_INTERNAL, VD_ERR_JOB_UNKNOWN};
use std::collections::HashMap;
use std::panic::{self, AssertUnwindSafe};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};

/// Returned by a poll function when the call itself is invalid (null out
/// parameter, unknown job id, or a job id belonging to another service).
pub(crate) const POLL_MISUSE: i32 = -1;

/// The C-side opaque handle.
///
/// This type has no variants and no fields on purpose: it exists only as a
/// pointer type, and cbindgen renders it as `typedef struct VdService VdService;`.
/// The real state is [`ServiceState`]; the handle is a cast of its address.
pub enum VdService {}

/// Result payload of one finished job, tagged by the service that produced it.
pub(crate) enum JobValue {
    /// Project model / `.f` expansion.
    Project(crate::project::ProjectData),
    /// Verible LSP session description.
    Lsp(crate::lsp::LspData),
    /// SLANG deep-lint diagnostics.
    Deeplint(crate::deeplint::DeeplintData),
    /// FIM completion result.
    Fim(crate::fim::FimData),
    /// Global search hits.
    Search(crate::search::SearchData),
    /// VCS status.
    Vcs(crate::vcs::VcsData),
    /// Settings read from the store.
    Settings(crate::settings::SettingsData),
    /// Settings written successfully (no payload).
    SettingsSaved,
}

enum JobState {
    Pending,
    Done(JobValue),
    Failed(VdError),
}

/// One poll attempt.
pub(crate) enum Polled {
    /// The job is still running; the caller should poll again later.
    Pending,
    /// The job id is unknown, already consumed, or was cancelled.
    Unknown,
    /// The job finished; the payload is owned by this call.
    Ready(JobValue),
    /// The job failed; the reason is in the service's last-error slot.
    Failed,
}

/// The live state behind one `VdService *`.
pub(crate) struct ServiceState {
    inner: Arc<Inner>,
}

struct Inner {
    next_job: AtomicU64,
    jobs: Mutex<HashMap<u64, JobState>>,
    last_error: Mutex<Option<VdError>>,
}

impl ServiceState {
    /// Create an empty registry (no threads, no I/O).
    pub(crate) fn new() -> Self {
        Self {
            inner: Arc::new(Inner {
                next_job: AtomicU64::new(1),
                jobs: Mutex::new(HashMap::new()),
                last_error: Mutex::new(None),
            }),
        }
    }

    /// Run `job` on a worker thread and return its id immediately.
    pub(crate) fn start<F>(&self, job: F) -> u64
    where
        F: FnOnce() -> Result<JobValue, VdError> + Send + 'static,
    {
        let id = self.inner.next_job.fetch_add(1, Ordering::Relaxed);
        self.lock_jobs().insert(id, JobState::Pending);
        let inner = Arc::clone(&self.inner);
        let spawned = std::thread::Builder::new()
            .name(format!("vd-ffi-job-{id}"))
            .spawn(move || {
                let outcome = panic::catch_unwind(AssertUnwindSafe(job));
                let state = match outcome {
                    Ok(Ok(value)) => JobState::Done(value),
                    Ok(Err(error)) => JobState::Failed(error),
                    Err(_) => JobState::Failed(VdError::new(
                        VD_ERR_INTERNAL,
                        "a Rust service job panicked; the result was discarded",
                    )),
                };
                inner.finish(id, state);
            });
        if spawned.is_err() {
            self.inner.finish(
                id,
                JobState::Failed(VdError::new(
                    VD_ERR_INTERNAL,
                    "cannot start a worker thread for the service job",
                )),
            );
        }
        id
    }

    /// Poll one job. A terminal job is removed, so a result is delivered once.
    pub(crate) fn poll(&self, id: u64) -> Polled {
        let state = {
            let mut jobs = self.lock_jobs();
            match jobs.get(&id) {
                None => return Polled::Unknown,
                Some(JobState::Pending) => return Polled::Pending,
                Some(JobState::Done(_)) | Some(JobState::Failed(_)) => jobs.remove(&id),
            }
        };
        match state {
            Some(JobState::Done(value)) => Polled::Ready(value),
            Some(JobState::Failed(error)) => {
                self.record_error(error);
                Polled::Failed
            }
            _ => Polled::Unknown,
        }
    }

    /// Remember the reason of the most recent rejected call or failed job.
    pub(crate) fn record_error(&self, error: VdError) {
        if let Ok(mut slot) = self.inner.last_error.lock() {
            *slot = Some(error);
        }
    }

    /// The most recent error, if any.
    pub(crate) fn last_error(&self) -> Option<VdError> {
        match self.inner.last_error.lock() {
            Ok(slot) => *slot,
            Err(_) => None,
        }
    }

    /// Discard a pending or finished job. `true` when the id was live.
    pub(crate) fn cancel(&self, id: u64) -> bool {
        let removed = self.lock_jobs().remove(&id).is_some();
        if removed {
            self.record_error(VdError::new(
                VD_ERR_CANCELLED,
                "the job was cancelled and its result discarded",
            ));
        }
        removed
    }

    /// Drop a job and its result without reporting an error.
    pub(crate) fn release(&self, id: u64) -> bool {
        self.lock_jobs().remove(&id).is_some()
    }

    fn lock_jobs(&self) -> MutexGuard<'_, HashMap<u64, JobState>> {
        match self.inner.jobs.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }
}

impl Inner {
    fn finish(&self, id: u64, state: JobState) {
        let mut jobs = match self.jobs.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        let pending = matches!(jobs.get(&id), Some(JobState::Pending));
        if pending {
            jobs.insert(id, state);
        }
    }
}

/// Access the live state behind an opaque handle.
///
/// # Safety
///
/// `handle` must be null or a pointer produced by [`vd_service_create`] that has
/// not been destroyed yet.
pub(crate) unsafe fn service_state<'a>(handle: *mut VdService) -> Option<&'a ServiceState> {
    if handle.is_null() {
        return None;
    }
    (handle as *const ServiceState).as_ref()
}

/// Record `VD_ERR_JOB_UNKNOWN` and return the misuse code.
pub(crate) fn unknown_job(state: &ServiceState) -> i32 {
    state.record_error(VdError::new(
        VD_ERR_JOB_UNKNOWN,
        "unknown job id: it was never issued, already consumed, or cancelled",
    ));
    POLL_MISUSE
}

/// Copy the last error into `out`; returns 1 when an error was present.
///
/// # Safety
///
/// `service` must be null or live, and `out` must be null or writable for one
/// [`VdError`].
#[no_mangle]
pub unsafe extern "C" fn vd_service_error(service: *mut VdService, out: *mut VdError) -> i32 {
    if out.is_null() {
        return 0;
    }
    let Some(state) = service_state(service) else {
        return 0;
    };
    match state.last_error() {
        Some(error) => {
            *out = error;
            1
        }
        None => 0,
    }
}

/// The C ABI version implemented by this library (touches no memory).
#[no_mangle]
pub extern "C" fn vd_abi_version() -> u32 {
    crate::VD_ABI_VERSION
}

/// Create a service handle: no threads and no I/O until a job is started.
/// The returned pointer is released with [`vd_service_destroy`].
#[no_mangle]
pub extern "C" fn vd_service_create() -> *mut VdService {
    let state = Box::new(ServiceState::new());
    Box::into_raw(state) as *mut VdService
}

/// Destroy a service handle. Running jobs finish into memory nobody reads.
///
/// # Safety
///
/// `service` must be null or a handle from [`vd_service_create`] that was not
/// destroyed already; the pointer is invalid after this call.
#[no_mangle]
pub unsafe extern "C" fn vd_service_destroy(service: *mut VdService) {
    if service.is_null() {
        return;
    }
    drop(Box::from_raw(service as *mut ServiceState));
}

/// Cancel one job (`1` when it was live, `0` when the id was unknown).
///
/// A job already running is not interrupted; its result is discarded so a stale
/// completion can never overwrite a newer one.
///
/// # Safety
///
/// `service` must be null or a live handle; the function touches only null-safely.
#[no_mangle]
pub unsafe extern "C" fn vd_job_cancel(service: *mut VdService, job: u64) -> i32 {
    match service_state(service) {
        Some(state) => i32::from(state.cancel(job)),
        None => 0,
    }
}

/// Drop one job and its result without reporting an error.
///
/// # Safety
///
/// `service` must be null or a live handle; the function touches only null-safely.
#[no_mangle]
pub unsafe extern "C" fn vd_job_release(service: *mut VdService, job: u64) -> i32 {
    match service_state(service) {
        Some(state) => i32::from(state.release(job)),
        None => 0,
    }
}
