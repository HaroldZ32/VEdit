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

/// Project the settings store's `fim.*` block onto the transport settings type.
///
/// `vd-fim` is the transport crate and cannot depend on `vd-settings`, so the
/// application-layer mapping lives here, exactly like the `vd_config::FimConfig`
/// mapping in [`crate::fim`]. The API key is **not** part of this projection:
/// the store never holds one. Use [`fim_settings_from_store`] when the key has
/// to be attached as well.
#[must_use]
pub fn fim_settings_from_entries(entries: &[(String, String)]) -> vd_fim::FimSettings {
    let entries: Vec<vd_settings::Entry> = entries
        .iter()
        .map(|(key, value)| vd_settings::Entry {
            key: key.clone(),
            value: value.clone(),
        })
        .collect();
    let stored = vd_settings::schema::FimSettings::from_entries(&entries);
    let protocol = match stored.protocol {
        vd_settings::schema::FimProtocol::OpenAi => vd_fim::Protocol::OpenAi,
        vd_settings::schema::FimProtocol::LlamaCpp => vd_fim::Protocol::LlamaCpp,
    };
    vd_fim::FimSettings {
        enabled: stored.enabled,
        protocol,
        endpoint: stored.endpoint,
        model: stored.model,
        timeout_ms: stored.timeout_ms,
        max_tokens: stored.max_tokens,
        api_key: None,
    }
}

/// Read the settings store at `path` and project its FIM block, resolving the
/// API key the documented way: `environment_key` first (the caller passes
/// `VEDIT_FIM_API_KEY`), then the owner-only user key file next to the store.
///
/// This is the seam the FIM request path adopts to read the Preferences dialog's
/// FIM page instead of `vd_config`'s `[fim]` block.
#[must_use]
pub fn fim_settings_from_store(path: &Path, environment_key: Option<&str>) -> vd_fim::FimSettings {
    let loaded = vd_settings::load(path);
    let pairs: Vec<(String, String)> = loaded
        .entries
        .into_iter()
        .map(|entry| (entry.key, entry.value))
        .collect();
    let mut settings = fim_settings_from_entries(&pairs);
    settings.api_key = vd_settings::schema::resolve_api_key(path, environment_key)
        .key
        .map(|key| vd_fim::ApiKey::new(key.expose_secret()));
    settings
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;

    const SECRET: &str = "sk-test-DO-NOT-LEAK-123456";

    fn store(dir: &std::path::Path, pairs: &[(&str, &str)]) -> PathBuf {
        let path = dir.join("settings.toml");
        let entries: Vec<vd_settings::Entry> = pairs
            .iter()
            .map(|(key, value)| vd_settings::Entry {
                key: (*key).to_string(),
                value: (*value).to_string(),
            })
            .collect();
        assert_eq!(vd_settings::save(&path, &entries), Ok(()));
        path
    }

    #[test]
    fn the_settings_pages_reach_the_vd_fim_settings_type() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = store(
            dir.path(),
            &[
                ("fim.enabled", "true"),
                ("fim.protocol", "llama_cpp"),
                ("fim.endpoint", "http://127.0.0.1:8080"),
                ("fim.model", "qwen"),
                ("fim.timeout_ms", "2500"),
                ("fim.max_tokens", "128"),
            ],
        );
        let settings = fim_settings_from_store(&path, None);
        assert!(settings.enabled);
        assert_eq!(settings.protocol, vd_fim::Protocol::LlamaCpp);
        assert_eq!(settings.endpoint, "http://127.0.0.1:8080");
        assert_eq!(settings.model, "qwen");
        assert_eq!(settings.timeout_ms, 2_500);
        assert_eq!(settings.max_tokens, 128);
        assert_eq!(
            settings.request_url().as_deref(),
            Some("http://127.0.0.1:8080/infill")
        );
    }

    #[test]
    fn an_unconfigured_store_keeps_fim_disabled_without_a_transport() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let settings = fim_settings_from_store(&dir.path().join("settings.toml"), None);
        assert!(!settings.enabled);
        assert_eq!(settings.unavailable(), Some(vd_fim::FimError::Disabled));
        assert!(settings.api_key.is_none());
        assert_eq!(settings.request_url(), None);
    }

    #[test]
    fn the_environment_key_wins_over_the_user_file() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = store(dir.path(), &[("fim.enabled", "true")]);
        let key_file = vd_settings::schema::api_key_path(&path);
        assert_eq!(
            vd_settings::schema::write_api_key_file(
                &key_file,
                &vd_settings::schema::ApiKey::new("from-file"),
            ),
            Ok(())
        );
        let from_file = fim_settings_from_store(&path, None);
        assert_eq!(
            from_file
                .api_key
                .as_ref()
                .map(vd_fim::ApiKey::expose_secret),
            Some("from-file")
        );
        let from_env = fim_settings_from_store(&path, Some("from-env"));
        assert_eq!(
            from_env.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some("from-env")
        );
    }

    #[test]
    fn a_plaintext_store_key_never_reaches_the_transport_or_a_debug_line() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = store(
            dir.path(),
            &[
                ("fim.endpoint", "http://127.0.0.1:8080"),
                ("fim.api_key", SECRET),
            ],
        );
        let settings = fim_settings_from_store(&path, None);
        assert!(
            settings.api_key.is_none(),
            "a plaintext settings.toml key must be ignored"
        );
        let rendered = format!("{settings:?}");
        assert!(!rendered.contains(SECRET), "debug output leaked the key");
    }

    #[test]
    fn a_world_readable_key_file_is_refused() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = store(dir.path(), &[("fim.enabled", "true")]);
        let key_file = vd_settings::schema::api_key_path(&path);
        assert!(fs::write(&key_file, SECRET).is_ok());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert!(fs::set_permissions(&key_file, fs::Permissions::from_mode(0o644)).is_ok());
            let settings = fim_settings_from_store(&path, None);
            assert!(
                settings.api_key.is_none(),
                "an insecure key file must never be used"
            );
        }
    }
}
