//! Fill-in-the-middle completion (service 4 of 7).
//!
//! The product default is **disabled**: `vd-fim::FimSettings::default()` has no
//! endpoint and no key, `FimService::from_settings` then builds no transport and
//! no worker thread, and this module returns `state = 0` with the reason — no
//! network, no DNS, no thread. That is the property Slice 3 has to keep and the
//! one the acceptance test re-measures.
//!
//! When FIM is enabled and configured, one request is started on the job's
//! worker thread and polled until its deadline. Enabling and configuring it from
//! the product (and proving zero network while disabled) belongs to the
//! fim-service task; the C shape fixed here does not change for it.

use crate::capi::{
    c_string, free_string, VdError, VdFimResult, VD_STATUS_FAILED, VD_STATUS_PENDING,
    VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::time::{Duration, Instant};

/// What one FIM request produced.
pub(crate) struct FimData {
    /// 0 disabled/not configured, 1 text returned, 2 error.
    pub(crate) state: i32,
    /// Completion text, or the reason no request was made.
    pub(crate) text: String,
}

impl FimData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned string must be released with `vd_fim_result_free`.
    pub(crate) unsafe fn into_c(self) -> VdFimResult {
        VdFimResult {
            state: self.state,
            text: c_string(&self.text),
        }
    }
}

/// Run one completion against the product settings.
pub(crate) fn complete(prefix: &str, suffix: &str) -> Result<FimData, VdError> {
    let settings = vd_fim::FimSettings::default();
    if !settings.enabled {
        let service = vd_fim::FimService::from_settings(settings);
        let reason = service
            .unavailable_reason()
            .unwrap_or("FIM is disabled")
            .to_string();
        return Ok(FimData {
            state: 0,
            text: reason,
        });
    }
    if settings.endpoint.trim().is_empty() || settings.model.trim().is_empty() {
        return Ok(FimData {
            state: 0,
            text: "FIM is enabled but has no endpoint or model; no request was made".to_string(),
        });
    }
    let service = vd_fim::FimService::from_settings(settings.clone());
    let Some(_generation) = service.start(prefix, suffix) else {
        return Ok(FimData {
            state: 0,
            text: "FIM refused to start a request".to_string(),
        });
    };
    let deadline = Instant::now() + Duration::from_millis(settings.timeout_ms);
    while Instant::now() < deadline {
        match service.poll() {
            Some(Ok(text)) => {
                return Ok(FimData { state: 1, text });
            }
            Some(Err(error)) => {
                return Err(VdError::new(
                    crate::capi::VD_ERR_SERVICE,
                    &format!("FIM: {}", error.reason()),
                ));
            }
            None => std::thread::sleep(Duration::from_millis(5)),
        }
    }
    service.cancel();
    Ok(FimData {
        state: 2,
        text: "FIM request timed out".to_string(),
    })
}

/// Start a FIM completion job.
///
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; the string arguments must be null or valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_fim_complete(
    service: *mut VdService,
    prefix: *const c_char,
    suffix: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let (Some(prefix), Some(suffix)) = (crate::capi::c_str(prefix), crate::capi::c_str(suffix))
    else {
        state.record_error(VdError::new(
            crate::capi::VD_ERR_INVALID_ARGUMENT,
            "prefix and suffix must be valid UTF-8 text",
        ));
        return 0;
    };
    let prefix = prefix.to_string();
    let suffix = suffix.to_string();
    state.start(move || complete(&prefix, &suffix).map(JobValue::Fim))
}

/// Poll a FIM job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdFimResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_fim_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdFimResult,
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
        Polled::Ready(JobValue::Fim(data)) => {
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

/// Release a filled [`VdFimResult`]; null-safe.
///
/// # Safety
///
/// `result` must be null or point to a struct filled by [`vd_fim_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_fim_result_free(result: *mut VdFimResult) {
    if result.is_null() {
        return;
    }
    let result = &mut *result;
    free_string(result.text);
    result.text = std::ptr::null_mut();
    result.state = 0;
}
