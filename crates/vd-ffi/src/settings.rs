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
///
/// The two tool axes are normalised here, at the ONE seam every consumer of
/// `vd_settings_load` reads: the Qt half selects a language server by comparing
/// the stored string, so an alias written by hand (`slang-server`) must arrive
/// already canonical (`slang`) instead of silently meaning the default backend.
/// A value that is not a backend at all keeps the documented default and adds a
/// warning naming it, which the ABI already carries in `VdSettings.warnings`.
pub(crate) fn load(path: &Path) -> SettingsData {
    let mut loaded = vd_settings::load(path);
    let mut warnings = std::mem::take(&mut loaded.warnings);
    for warning in vd_settings::schema::normalize_tool_entries(&mut loaded.entries) {
        warnings.push(format!("{}: {warning}", path.display()));
    }
    SettingsData {
        entries: loaded
            .entries
            .into_iter()
            .map(|entry| (entry.key, entry.value))
            .collect(),
        warnings,
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
///
/// This is the **whole-block** projection: every field the store does not carry
/// takes the schema default. The request path never uses it directly — when the
/// store takes over, the fields it does not carry must inherit `vd_config`, which
/// is [`fim_settings_inherited`]'s job (frozen contract §13 F1).
#[must_use]
pub fn fim_settings_from_entries(entries: &[(String, String)]) -> vd_fim::FimSettings {
    fim_settings_inherited(&vd_fim::FimSettings::default(), entries)
}

/// Project the store's `fim.*` block **field by field** on top of `base`.
///
/// Frozen contract §13 F1: the store owns only the fields it actually carries.
/// "Does the store carry this field" is answered by the KEY's presence, not by
/// the projected value (which is always defaulted): an old store holding just
/// `fim.endpoint` + `fim.model` must inherit `enabled` / `protocol` /
/// `timeout_ms` / `max_tokens` from `vd_config`, otherwise its absent `enabled`
/// would read as the schema default `false` and silently disable a user whose
/// `[fim]` lives in `vedit.toml` — the same regression §7 protects against, only
/// through a partial store instead of a full one.
///
/// `fim.ghost_text` has no counterpart on the transport settings type: inline
/// rendering is a UI decision, never part of the request, so there is nothing to
/// inherit for it. The API key is untouched here (see [`attach_store_key`]).
#[must_use]
pub fn fim_settings_inherited(
    base: &vd_fim::FimSettings,
    entries: &[(String, String)],
) -> vd_fim::FimSettings {
    use vd_settings::schema::keys;
    let entries: Vec<vd_settings::Entry> = entries
        .iter()
        .map(|(key, value)| vd_settings::Entry {
            key: key.clone(),
            value: value.clone(),
        })
        .collect();
    let stored = vd_settings::schema::FimSettings::from_entries(&entries);
    let present = |key: &str| entries.iter().any(|entry| entry.key == key);
    let mut settings = base.clone();
    if present(keys::FIM_ENABLED) {
        settings.enabled = stored.enabled;
    }
    if present(keys::FIM_PROTOCOL) {
        settings.protocol = match stored.protocol {
            vd_settings::schema::FimProtocol::OpenAi => vd_fim::Protocol::OpenAi,
            vd_settings::schema::FimProtocol::LlamaCpp => vd_fim::Protocol::LlamaCpp,
        };
    }
    if present(keys::FIM_ENDPOINT) {
        settings.endpoint = stored.endpoint;
    }
    if present(keys::FIM_MODEL) {
        settings.model = stored.model;
    }
    if present(keys::FIM_TIMEOUT_MS) {
        settings.timeout_ms = stored.timeout_ms;
    }
    if present(keys::FIM_MAX_TOKENS) {
        settings.max_tokens = stored.max_tokens;
    }
    settings
}

/// The store key with which the settings UI declares that it owns the FIM
/// configuration.
///
/// Frozen contract §7 as corrected by §12 B1 and §13 F1: the FIM page writes
/// this marker only when the user actually edited that page (or when the store
/// already carried it), and ownership is **per field**, never whole-block. Only
/// the `fim.*` keys the store actually carries are taken; every field it does
/// not carry inherits `vd_config` (see [`fim_settings_inherited`]). The marker
/// is still the gate because a store can carry `fim.*` keys for a page the user
/// never edited — e.g. the `fim.enabled = false` an older build's whole-block
/// "Apply" wrote — and taking those as ownership would silently override a user
/// who configured `[fim]` in `vedit.toml`.
pub const FIM_UI_OWNED_KEY: &str = "fim.ui_owned";

/// Whether a loaded store **declares UI ownership** of the FIM configuration.
///
/// Frozen contract §7 (as corrected by §12 B1): only `fim.ui_owned = "true"` may
/// make the store's FIM block take over the request path. A store that merely
/// exists, or that carries `fim.*` keys written by an "Apply" the user never made
/// on the FIM page — including the `fim.enabled = false` an older build stored —
/// is not authoritative at all. Fail closed: a missing marker, `false`, `1` or
/// any other spelling leaves the `vd_config` path in charge.
#[must_use]
pub fn has_fim_entries(entries: &[(String, String)]) -> bool {
    entries
        .iter()
        .any(|(key, value)| key.as_str() == FIM_UI_OWNED_KEY && value.trim() == "true")
}

/// The store's flat key/value pairs, read through `vd_settings::load` (which
/// never fails: a missing or unreadable store is simply empty).
fn store_entries(path: &Path) -> Vec<(String, String)> {
    vd_settings::load(path)
        .entries
        .into_iter()
        .map(|entry| (entry.key, entry.value))
        .collect()
}

/// Attach the API key the documented way: `environment_key` first (the caller
/// passes `VEDIT_FIM_API_KEY`), then the owner-only user key file next to the
/// store.
///
/// The documented order has a third level, the `[fim] api_key` of `vedit.toml`
/// (§7, §12 B2), which reaches the settings through the inheritance `base` of
/// [`fim_settings_inherited`]. So a resolved value here only ever **overrides**:
/// an absent env key and absent file leave the inherited key in place instead of
/// clobbering it with `None`.
///
/// Also called on the markerless path (§15 A34): a bound store that does not own
/// the FIM block keeps the `vd_config` values, but the key order is independent
/// of that decision, so the file next to the store still outranks `vedit.toml`.
pub(crate) fn attach_store_key(
    settings: &mut vd_fim::FimSettings,
    path: &Path,
    environment_key: Option<&str>,
) {
    if let Some(key) = vd_settings::schema::resolve_api_key(path, environment_key).key {
        settings.api_key = Some(vd_fim::ApiKey::new(key.expose_secret()));
    }
}

/// Read the settings store at `path` and project its FIM block, resolving the
/// API key the documented way: `environment_key` first (the caller passes
/// `VEDIT_FIM_API_KEY`), then the owner-only user key file next to the store.
///
/// This is the seam the FIM request path adopts to read the Preferences dialog's
/// FIM page instead of `vd_config`'s `[fim]` block.
///
/// Whole-block projection with schema defaults for the fields the store does not
/// carry; the request path uses [`fim_settings_from_store_if_present`], which
/// inherits those fields from `vd_config` instead (frozen contract §13 F1).
#[must_use]
pub fn fim_settings_from_store(path: &Path, environment_key: Option<&str>) -> vd_fim::FimSettings {
    let pairs = store_entries(path);
    let mut settings = fim_settings_from_entries(&pairs);
    attach_store_key(&mut settings, path, environment_key);
    settings
}

/// Read the store at `path` and project its FIM block **only when the store
/// declares UI ownership of it** (`fim.ui_owned = "true"`, frozen contract §7 as
/// corrected by §12 B1).
///
/// `None` means the store is not authoritative for FIM — it carries no ownership
/// marker, or it could not be read (both surface as an empty entry list). The
/// caller then keeps the `vd_config` path byte-for-byte, which is what protects a
/// `vedit.toml`-configured user from an unrelated settings save.
///
/// `base` is the `vd_config` projection the store's carried fields override
/// field by field (frozen contract §13 F1); a field the store does not carry
/// keeps `base` — it must not fall back to the schema default.
#[must_use]
pub fn fim_settings_from_store_if_present(
    path: &Path,
    environment_key: Option<&str>,
    base: &vd_fim::FimSettings,
) -> Option<vd_fim::FimSettings> {
    let pairs = store_entries(path);
    if !has_fim_entries(&pairs) {
        return None;
    }
    let mut settings = fim_settings_inherited(base, &pairs);
    attach_store_key(&mut settings, path, environment_key);
    Some(settings)
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

    fn lookup(data: &SettingsData, key: &str) -> Option<String> {
        data.entries
            .iter()
            .find(|(stored, _)| stored == key)
            .map(|(_, value)| value.clone())
    }

    #[test]
    fn the_delivered_entries_carry_canonical_backends_and_explain_a_refusal() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        // The four accepted spellings (canonical + display label, any case),
        // delivered canonical because the Qt half compares this exact string.
        for (stored, canonical) in [
            ("verible", "verible"),
            ("verible-verilog-ls", "verible"),
            ("slang", "slang"),
            (" SLANG-SERVER ", "slang"),
        ] {
            let path = store(dir.path(), &[("tools.lsp_backend", stored)]);
            let data = load(&path);
            assert_eq!(
                lookup(&data, "tools.lsp_backend").as_deref(),
                Some(canonical),
                "stored {stored:?}"
            );
            assert!(data.warnings.is_empty(), "{stored:?}: {:?}", data.warnings);
        }

        // ... and the fifth case: a value that is not a backend is refused out
        // loud — default kept, warning carried by the ABI.
        let path = store(dir.path(), &[("tools.lsp_backend", "emacs")]);
        let data = load(&path);
        assert_eq!(lookup(&data, "tools.lsp_backend").as_deref(), Some("slang"));
        assert_eq!(data.warnings.len(), 1, "{:?}", data.warnings);
        assert!(data.warnings[0].contains("emacs"), "{:?}", data.warnings);
        assert!(
            data.warnings[0].contains("tools.lsp_backend"),
            "{:?}",
            data.warnings
        );
        assert!(
            data.warnings[0].starts_with(&path.display().to_string()),
            "the warning names the file it came from: {:?}",
            data.warnings
        );
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

    #[test]
    fn a_partial_store_overrides_only_the_fields_it_carries() {
        let base = vd_fim::FimSettings {
            enabled: true,
            protocol: vd_fim::Protocol::LlamaCpp,
            endpoint: "http://base".to_string(),
            model: "base-model".to_string(),
            timeout_ms: 1_750,
            max_tokens: 32,
            api_key: None,
        };
        let entries = |pairs: &[(&str, &str)]| -> Vec<(String, String)> {
            pairs
                .iter()
                .map(|(key, value)| ((*key).to_string(), (*value).to_string()))
                .collect()
        };
        // Two carried fields override; the other four keep `base` — the KEY's
        // presence decides, so an absent field can never fall back to a schema
        // default (which would be `enabled = false` here).
        let inherited = fim_settings_inherited(
            &base,
            &entries(&[
                ("fim.endpoint", "http://store"),
                ("fim.model", "store-model"),
                ("theme", "dark"),
            ]),
        );
        assert_eq!(inherited.endpoint, "http://store");
        assert_eq!(inherited.model, "store-model");
        assert!(inherited.enabled, "an absent `fim.enabled` stays `base`'s");
        assert_eq!(inherited.protocol, vd_fim::Protocol::LlamaCpp);
        assert_eq!(inherited.timeout_ms, 1_750);
        assert_eq!(inherited.max_tokens, 32);
        // The whole-block projection over the SAME entries is what the pre-fix
        // request path produced: every missing field is a schema default, i.e. the
        // exact destructive reading the field-level rule replaces.
        let whole = fim_settings_from_entries(&entries(&[
            ("fim.endpoint", "http://store"),
            ("fim.model", "store-model"),
        ]));
        assert!(!whole.enabled);
        assert_eq!(whole.protocol, vd_fim::Protocol::OpenAi);
        assert_eq!(whole.timeout_ms, vd_fim::DEFAULT_TIMEOUT_MS);
        assert_eq!(whole.max_tokens, vd_fim::DEFAULT_MAX_TOKENS);
    }

    #[test]
    fn the_takeover_predicate_needs_the_ui_ownership_marker() {
        let entries = |pairs: &[(&str, &str)]| -> Vec<(String, String)> {
            pairs
                .iter()
                .map(|(key, value)| ((*key).to_string(), (*value).to_string()))
                .collect()
        };
        assert!(!has_fim_entries(&entries(&[])));
        assert!(!has_fim_entries(&entries(&[
            ("theme", "dark"),
            ("ui.font_point_size", "15"),
            ("fim", "value"),
            ("fimx.enabled", "true"),
            ("tools.fim.enabled", "true"),
        ])));
        // A `fim.*` key is NOT an ownership declaration: the settings page writes
        // this whole block on every "Apply", whether the user touched FIM or not,
        // and an older build stored `fim.enabled = false` the same way.
        assert!(!has_fim_entries(&entries(&[("fim.enabled", "true")])));
        assert!(!has_fim_entries(&entries(&[
            ("theme", "dark"),
            ("fim.endpoint", "http://h")
        ])));
        // The marker is the only opt-in, and only the literal `true` (the value
        // the UI writes) counts — everything else fails closed.
        assert!(has_fim_entries(&entries(&[(FIM_UI_OWNED_KEY, "true")])));
        assert!(has_fim_entries(&entries(&[
            ("theme", "dark"),
            ("fim.enabled", "true"),
            (FIM_UI_OWNED_KEY, " true "),
        ])));
        assert!(!has_fim_entries(&entries(&[(FIM_UI_OWNED_KEY, "false")])));
        assert!(!has_fim_entries(&entries(&[(FIM_UI_OWNED_KEY, "1")])));
        assert!(!has_fim_entries(&entries(&[(FIM_UI_OWNED_KEY, "")])));
    }

    #[test]
    fn a_store_without_the_ownership_marker_has_no_projection() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        // The inheritance base only matters when the store owns the block; for the
        // predicate half the defaults are enough.
        let base = vd_fim::FimSettings::default();
        let path = store(dir.path(), &[("theme", "dark")]);
        assert_eq!(fim_settings_from_store_if_present(&path, None, &base), None);
        // The old build's Apply defaults: `fim.*` keys, no marker.
        let path = store(dir.path(), &[("fim.enabled", "true")]);
        assert_eq!(fim_settings_from_store_if_present(&path, None, &base), None);
        let path = store(
            dir.path(),
            &[("fim.enabled", "true"), (FIM_UI_OWNED_KEY, "true")],
        );
        assert!(fim_settings_from_store_if_present(&path, None, &base).is_some());
    }
}
