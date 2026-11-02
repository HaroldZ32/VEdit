//! Settings storage (service 7 of 7).
//!
//! `vd-settings` owns the store (flat TOML, atomic save, degrade-on-bad-config);
//! this module is the C projection. Reading never fails — a broken file becomes
//! warnings that the settings UI shows — so the job's success path carries both
//! the values and the diagnostics, and the Qt side cannot lose the "why".
//!
//! Saving runs on the worker thread because it touches the disk and calls
//! `sync_all`; the UI thread only starts the job and polls it.

use crate::capi::{
    c_str, c_string, c_string_list, free_string, free_string_list, VdError, VdSetting,
    VdSettingInput, VdSettings, VdStringList, VD_ERR_INVALID_ARGUMENT, VD_ERR_SERVICE,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};

/// Settings read from the store.
pub(crate) struct SettingsData {
    pub(crate) entries: Vec<(String, String)>,
    pub(crate) warnings: Vec<String>,
}

impl SettingsData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with `vd_settings_free`.
    pub(crate) unsafe fn into_c(self) -> VdSettings {
        let warnings = c_string_list(&self.warnings);
        if self.entries.is_empty() {
            return VdSettings {
                count: 0,
                items: std::ptr::null_mut(),
                warnings,
            };
        }
        let items: Vec<VdSetting> = self
            .entries
            .into_iter()
            .map(|(key, value)| VdSetting {
                key: c_string(&key),
                value: c_string(&value),
            })
            .collect();
        VdSettings {
            count: items.len(),
            items: Box::into_raw(items.into_boxed_slice()) as *mut VdSetting,
            warnings,
        }
    }
}

/// Read the settings file at `path` (never fails; problems become warnings).
pub(crate) fn load(path: &Path) -> SettingsData {
    let loaded = vd_settings::load(path);
    SettingsData {
        entries: loaded
            .entries
            .into_iter()
            .map(|entry| (entry.key, entry.value))
            .collect(),
        warnings: loaded.warnings,
    }
}

/// Write the settings file at `path` atomically.
pub(crate) fn save(path: &Path, entries: Vec<(String, String)>) -> Result<(), VdError> {
    let entries: Vec<vd_settings::Entry> = entries
        .into_iter()
        .map(|(key, value)| vd_settings::Entry { key, value })
        .collect();
    vd_settings::save(path, &entries)
        .map_err(|error| VdError::new(VD_ERR_SERVICE, &error.to_string()))
}

/// Start a settings-read job.
///
/// # Safety
///
/// `service` must be null or live; `path` must be null or a valid NUL-terminated
/// string.
#[no_mangle]
pub unsafe extern "C" fn vd_settings_load(service: *mut VdService, path: *const c_char) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(path) = c_str(path) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "path must be a valid UTF-8 path",
        ));
        return 0;
    };
    let path = PathBuf::from(path);
    state.start(move || Ok(JobValue::Settings(load(&path))))
}

/// Start a settings-write job over `count` borrowed key/value pairs.
///
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `path` must be null or a valid NUL-terminated
/// string; `entries` must be null or point to `count` readable
/// [`VdSettingInput`] values whose strings are valid NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_settings_save(
    service: *mut VdService,
    path: *const c_char,
    entries: *const VdSettingInput,
    count: usize,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(path) = c_str(path) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "path must be a valid UTF-8 path",
        ));
        return 0;
    };
    let mut pairs: Vec<(String, String)> = Vec::with_capacity(count);
    if count > 0 {
        if entries.is_null() {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                "entries must not be null when count is non-zero",
            ));
            return 0;
        }
        let slice = std::slice::from_raw_parts(entries, count);
        for entry in slice {
            let (Some(key), Some(value)) = (c_str(entry.key), c_str(entry.value)) else {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "every setting key and value must be a valid UTF-8 string",
                ));
                return 0;
            };
            pairs.push((key.to_string(), value.to_string()));
        }
    }
    let path = PathBuf::from(path);
    state.start(move || {
        save(&path, pairs)?;
        Ok(JobValue::SettingsSaved)
    })
}

/// Poll a settings job (load or save). A successful save returns `READY` with an
/// empty result.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdSettings`].
#[no_mangle]
pub unsafe extern "C" fn vd_settings_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdSettings,
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
        Polled::Ready(JobValue::Settings(data)) => {
            *out = data.into_c();
            VD_STATUS_READY
        }
        Polled::Ready(JobValue::SettingsSaved) => {
            *out = VdSettings {
                count: 0,
                items: std::ptr::null_mut(),
                warnings: VdStringList::empty(),
            };
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

/// Release a filled [`VdSettings`]; null-safe.
///
/// # Safety
///
/// `settings` must be null or point to a struct filled by [`vd_settings_poll`]
/// and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_settings_free(settings: *mut VdSettings) {
    if settings.is_null() {
        return;
    }
    let settings = &mut *settings;
    if !settings.items.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(settings.items, settings.count);
        for item in slice.iter_mut() {
            free_string(item.key);
            free_string(item.value);
            item.key = std::ptr::null_mut();
            item.value = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            settings.items,
            settings.count,
        )));
    }
    free_string_list(&mut settings.warnings);
    settings.count = 0;
    settings.items = std::ptr::null_mut();
}
