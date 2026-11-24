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
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{PoisonError, RwLock};
use std::time::{Duration, Instant};

use crate::capi::VD_ERR_SERVICE;

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

/// The transport-level projection of the loaded `[fim]` configuration.
///
/// This is the application-layer mapping `vd_fim::settings` documents: the plain
/// config data goes to `FimSettings`, and the secret keeps its redacting type
/// until the single transport call site reads it.
fn settings_from_config(config: &vd_config::FimConfig) -> vd_fim::FimSettings {
    let protocol = match config.protocol {
        vd_config::FimProtocol::OpenAi => vd_fim::Protocol::OpenAi,
        vd_config::FimProtocol::LlamaCpp => vd_fim::Protocol::LlamaCpp,
    };
    vd_fim::FimSettings {
        enabled: config.enabled,
        protocol,
        endpoint: config.endpoint.clone(),
        model: config.model.clone(),
        timeout_ms: config.timeout_ms,
        api_key: config
            .api_key
            .as_ref()
            .map(|key| vd_fim::ApiKey::new(key.expose_secret())),
        ..vd_fim::FimSettings::default()
    }
}

/// The settings store the FIM request path reads, bound at startup by
/// [`vd_fim_use_settings_store`]. `None` means unbound, which keeps the
/// `vd_config` path.
///
/// Why `RwLock`: the binding can be set, replaced or cleared while worker threads
/// read it, and several workers may read at once. The lock is held for a path
/// clone and nothing else — [`bound_settings_store`] returns an owned `PathBuf`
/// with the guard already dropped — so no file I/O ever happens under it and a
/// writer cannot be delayed by a slow disk. Handing a writer's new path to one
/// in-flight load is harmless: that load simply uses the path that was current
/// when it started, and the next request sees the new one.
static SETTINGS_STORE: RwLock<Option<PathBuf>> = RwLock::new(None);

/// Set the first time a bound store does not take over, so the fallback is
/// reported once instead of on every completion request.
static STORE_FALLBACK_REPORTED: AtomicBool = AtomicBool::new(false);

/// Serialises the tests that mutate the process-wide settings binding.
#[cfg(test)]
pub(crate) static BINDING_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn bind_settings_store(path: Option<PathBuf>) {
    // The writer only moves a `PathBuf`; a poisoned lock can therefore still be
    // used as-is instead of turning a binding mistake into a panic.
    let mut slot = SETTINGS_STORE
        .write()
        .unwrap_or_else(PoisonError::into_inner);
    *slot = path;
}

pub(crate) fn bound_settings_store() -> Option<PathBuf> {
    let slot = SETTINGS_STORE
        .read()
        .unwrap_or_else(PoisonError::into_inner);
    slot.clone()
}

/// A store path is usable when it names a file: a path that does not exist yet is
/// still usable (the store is created by the first save, and reading a missing
/// store yields no keys), but a bare root or an existing directory is not.
fn usable_store_path(path: &Path) -> bool {
    if path.file_name().is_none() {
        return false;
    }
    match std::fs::metadata(path) {
        Ok(metadata) => !metadata.is_dir(),
        Err(_) => true,
    }
}

/// Report once that a bound store did not take over and the `vd_config` path is
/// in charge. A warning that cannot reach stderr must not fail a completion.
fn warn_store_fallback(path: &Path) {
    if STORE_FALLBACK_REPORTED.swap(true, Ordering::SeqCst) {
        return;
    }
    let _ = writeln!(
        std::io::stderr(),
        "FIM: {} carries no `fim.ui_owned=\"true\"` marker (or cannot be read); \
         using vedit.toml / VEDIT_FIM_*",
        path.display()
    );
}

/// Bind (or unbind) the settings store the FIM request path reads.
///
/// `settings_toml_path == NULL` or an empty string unbinds. Returns `0` when the
/// binding is in force and `-1` when the path cannot be used as a store (invalid
/// UTF-8, a directory, or a path with no file name); on `-1` the previous binding
/// is kept, so a caller's state never changes underneath it.
///
/// # Safety
///
/// `settings_toml_path` must be null or a NUL-terminated string that stays valid
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_fim_use_settings_store(settings_toml_path: *const c_char) -> i32 {
    if settings_toml_path.is_null() {
        bind_settings_store(None);
        return 0;
    }
    let Some(raw) = crate::capi::c_str(settings_toml_path) else {
        return -1;
    };
    if raw.is_empty() {
        bind_settings_store(None);
        return 0;
    }
    let path = PathBuf::from(raw);
    if !usable_store_path(&path) {
        return -1;
    }
    bind_settings_store(Some(path));
    0
}

/// Load the product configuration.
///
/// Frozen contract §7 (as corrected by §12 B1): the bound settings store takes
/// over **only** when it declares UI ownership of the FIM page
/// (`fim.ui_owned = "true"`); otherwise the `vd_config` path (project
/// `vedit.toml`, then the user file, then the `VEDIT_FIM_*` environment
/// variables) is used unchanged. There is **no default endpoint, model or key**:
/// the product default is the disabled `FimSettings`, so an unconfigured run
/// never builds a transport.
fn load_settings() -> Result<vd_fim::FimSettings, VdError> {
    let start_dir = std::env::current_dir().map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!("FIM: cannot resolve the working directory: {error}"),
        )
    })?;
    load_settings_from(&start_dir, None)
}

/// Build the `vd_config` loader for `start_dir`.
///
/// Production passes `None` and therefore reads the real process environment;
/// tests pass an explicit [`vd_config::Env`] so the host's own configuration
/// cannot leak into them.
fn config_loader(start_dir: &Path, environment: Option<&vd_config::Env>) -> vd_config::Loader {
    let mut loader = vd_config::Loader::new(start_dir);
    if let Some(environment) = environment {
        loader = loader.with_env(environment.clone());
    }
    loader
}

/// The API key the caller passes for this load: the injected environment when
/// there is one (tests, embedding), the process environment otherwise.
fn environment_key(environment: Option<&vd_config::Env>) -> Option<String> {
    match environment {
        Some(environment) => environment
            .get(vd_settings::schema::API_KEY_ENV)
            .map(str::to_string),
        None => std::env::var(vd_settings::schema::API_KEY_ENV).ok(),
    }
}

/// [`load_settings`] with an explicit start directory, and — for tests only — an
/// explicit environment. Production passes `None` and therefore reads the real
/// process environment.
///
/// Frozen contract §13 F1: the store is authoritative, but only **field by
/// field**. `vd_config` is loaded first and becomes the inheritance base, so a
/// store that carries only `fim.endpoint` + `fim.model` keeps `vedit.toml`'s
/// `enabled` / `protocol` / `timeout_ms` instead of the schema defaults. The base
/// is best-effort here exactly like §12 B2's key fallback: once the store owns
/// the configuration, an unreadable `vedit.toml` must not turn a working
/// completion into an error — the request then simply inherits the defaults, and
/// the fallback path below (no ownership marker) keeps its strict error.
///
/// Frozen contract §15 A34: the API key order `env > 0600 file next to
/// `settings.toml` > `vedit.toml`` is independent of who owns the FIM block, so
/// the markerless fallback resolves it too — otherwise `fim_detect::resolve_key`
/// (which always reads the file) and the completion request would disagree. An
/// unbound path has no `settings.toml` directory, so its order degenerates to
/// `env > vedit.toml`.
fn load_settings_from(
    start_dir: &Path,
    environment: Option<vd_config::Env>,
) -> Result<vd_fim::FimSettings, VdError> {
    // Loaded ONCE: it is both the inheritance base of an owning store and the
    // strict vd_config path when the store does not own the configuration.
    let loaded = config_loader(start_dir, environment.as_ref()).load();
    let bound = bound_settings_store();
    let environment_key = environment_key(environment.as_ref());
    if let Some(path) = bound.as_deref() {
        let base = loaded
            .as_ref()
            .map(|loaded| settings_from_config(&loaded.config.fim))
            .unwrap_or_default();
        if let Some(settings) = crate::settings::fim_settings_from_store_if_present(
            path,
            environment_key.as_deref(),
            &base,
        ) {
            return Ok(settings);
        }
        warn_store_fallback(path);
    }
    let loaded = loaded.map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!("FIM: cannot load the configuration: {error}"),
        )
    })?;
    let mut settings = settings_from_config(&loaded.config.fim);
    if let Some(path) = bound.as_deref() {
        // The store does not own the FIM block, but it still names the directory
        // the 0600 key file lives in. `attach_store_key` only overrides when the
        // environment or the file resolves a key, so `vedit.toml`'s
        // `[fim] api_key` survives as the third level.
        crate::settings::attach_store_key(&mut settings, path, environment_key.as_deref());
    }
    Ok(settings)
}

/// Run one completion with explicit settings (the config-independent half).
///
/// A disabled or half-configured state returns `state = 0` **without starting a
/// worker or touching the network**; only a complete configuration reaches the
/// transport. A transport failure is reported as a service error, a deadline hit
/// as `state = 2`.
pub(crate) fn complete_with_settings(
    settings: vd_fim::FimSettings,
    prefix: &str,
    suffix: &str,
) -> Result<FimData, VdError> {
    let service = vd_fim::FimService::from_settings(settings.clone());
    if !service.enabled() {
        let reason = service
            .unavailable_reason()
            .unwrap_or("FIM is disabled")
            .to_string();
        return Ok(FimData {
            state: 0,
            text: reason,
        });
    }
    let Some(_generation) = service.start(prefix, suffix) else {
        return Ok(FimData {
            state: 0,
            text: "FIM refused to start a request".to_string(),
        });
    };
    let deadline = Instant::now() + settings.timeout();
    while Instant::now() < deadline {
        match service.poll() {
            Some(Ok(text)) => {
                return Ok(FimData { state: 1, text });
            }
            Some(Err(error)) => {
                return Err(VdError::new(
                    VD_ERR_SERVICE,
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

/// Run one completion against the product settings.
pub(crate) fn complete(prefix: &str, suffix: &str) -> Result<FimData, VdError> {
    let settings = load_settings()?;
    complete_with_settings(settings, prefix, suffix)
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

#[cfg(test)]
mod tests {
    //! Headless proof of the two acceptance properties, at the same layer the C
    //! ABI calls:
    //!   1. the product default is disabled and sends **nothing** (a bound
    //!      loopback endpoint records zero connections);
    //!   2. an explicitly configured service really performs the request through
    //!      the Rust transport (`vd-fim` -> ureq) against a local fake endpoint;
    //!      the fake endpoint is not a real model endpoint and the tests say so.
    //!
    //! The fake endpoint lives on 127.0.0.1 only, and every wait has a budget, so
    //! the tests are deterministic and never depend on the outside network.

    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
    use std::sync::{Arc, Mutex};
    use std::thread::{self, JoinHandle};

    static TEMP_COUNTER: AtomicU64 = AtomicU64::new(0);

    fn unique_temp_dir() -> std::path::PathBuf {
        let serial = TEMP_COUNTER.fetch_add(1, Ordering::SeqCst);
        std::env::temp_dir().join(format!("vd-ffi-fim-{}-{serial}", std::process::id()))
    }

    fn message_of(error: &VdError) -> String {
        let end = error
            .message
            .iter()
            .position(|byte| *byte == 0)
            .unwrap_or(error.message.len());
        let bytes: Vec<u8> = error.message[..end]
            .iter()
            .map(|byte| *byte as u8)
            .collect();
        String::from_utf8_lossy(&bytes).into_owned()
    }

    /// A loopback HTTP endpoint that answers `response_body` and records every
    /// raw request it saw. It is stopped (and joined) when dropped.
    struct FakeEndpoint {
        endpoint: String,
        requests: Arc<Mutex<Vec<String>>>,
        stop: Arc<AtomicBool>,
        handle: Option<JoinHandle<()>>,
    }

    impl FakeEndpoint {
        fn start(response_body: &'static str) -> Self {
            let listener = match TcpListener::bind("127.0.0.1:0") {
                Ok(listener) => listener,
                Err(error) => panic!("cannot bind the fake endpoint: {error}"),
            };
            let address = match listener.local_addr() {
                Ok(address) => address,
                Err(error) => panic!("cannot read the fake endpoint address: {error}"),
            };
            let _ = listener.set_nonblocking(true);
            let requests = Arc::new(Mutex::new(Vec::new()));
            let request_log = Arc::clone(&requests);
            let stop = Arc::new(AtomicBool::new(false));
            let stop_flag = Arc::clone(&stop);
            let handle = thread::spawn(move || {
                while !stop_flag.load(Ordering::SeqCst) {
                    match listener.accept() {
                        Ok((mut stream, _)) => {
                            let _ = stream.set_read_timeout(Some(Duration::from_millis(500)));
                            let mut raw = Vec::new();
                            let mut chunk = [0_u8; 2048];
                            loop {
                                match stream.read(&mut chunk) {
                                    Ok(0) => break,
                                    Ok(read) => {
                                        raw.extend_from_slice(&chunk[..read]);
                                        if request_is_complete(&raw) {
                                            break;
                                        }
                                    }
                                    Err(_) => break,
                                }
                            }
                            if let Ok(mut entries) = request_log.lock() {
                                entries.push(String::from_utf8_lossy(&raw).into_owned());
                            }
                            let response = format!(
                                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\
                                 Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                                response_body.len(),
                                response_body
                            );
                            let _ = stream.write_all(response.as_bytes());
                            let _ = stream.flush();
                        }
                        Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                            thread::sleep(Duration::from_millis(2));
                        }
                        Err(_) => break,
                    }
                }
            });
            Self {
                endpoint: format!("http://{address}"),
                requests,
                stop,
                handle: Some(handle),
            }
        }

        fn requests(&self) -> Vec<String> {
            self.requests
                .lock()
                .map(|entries| entries.clone())
                .unwrap_or_default()
        }
    }

    impl Drop for FakeEndpoint {
        fn drop(&mut self) {
            self.stop.store(true, Ordering::SeqCst);
            if let Some(handle) = self.handle.take() {
                let _ = handle.join();
            }
        }
    }

    fn request_is_complete(raw: &[u8]) -> bool {
        let Some(headers_end) = raw.windows(4).position(|window| window == b"\r\n\r\n") else {
            return false;
        };
        let headers = String::from_utf8_lossy(&raw[..headers_end]).to_lowercase();
        let content_length = headers
            .lines()
            .find_map(|line| line.strip_prefix("content-length:"))
            .and_then(|value| value.trim().parse::<usize>().ok())
            .unwrap_or(0);
        raw.len() >= headers_end + 4 + content_length
    }

    fn settings_for(endpoint: &str, timeout_ms: u64) -> vd_fim::FimSettings {
        vd_fim::FimSettings {
            enabled: true,
            protocol: vd_fim::Protocol::OpenAi,
            endpoint: endpoint.to_string(),
            model: "qwen2.5-coder".to_string(),
            timeout_ms,
            api_key: Some(vd_fim::ApiKey::new("SECRET-LOOPBACK")),
            ..vd_fim::FimSettings::default()
        }
    }

    #[test]
    fn default_config_is_disabled_and_sends_nothing() {
        let endpoint = FakeEndpoint::start("{\"choices\":[{\"text\":\"x\"}]}");
        let config = vd_config::FimConfig::default();
        let settings = settings_from_config(&config);
        assert!(!settings.enabled, "the product default must be off");
        assert!(settings.endpoint.is_empty(), "no default endpoint");
        assert!(settings.model.is_empty(), "no default model");
        assert!(settings.api_key.is_none(), "no default key");

        let result = complete_with_settings(settings, "module m;", "");
        match result {
            Ok(data) => {
                assert_eq!(data.state, 0, "disabled FIM reports the unavailable state");
                assert!(!data.text.is_empty(), "the reason is visible to the UI");
            }
            Err(error) => panic!("disabled FIM must not fail the job: {}", message_of(&error)),
        }
        std::thread::sleep(Duration::from_millis(50));
        assert!(
            endpoint.requests().is_empty(),
            "disabled FIM must not connect to anything (fake endpoint saw requests)"
        );
    }

    #[test]
    fn mapping_carries_every_configured_value_without_leaking_the_key() {
        let config = vd_config::FimConfig {
            enabled: true,
            protocol: vd_config::FimProtocol::LlamaCpp,
            endpoint: "http://127.0.0.1:8080".to_string(),
            model: "local-model".to_string(),
            timeout_ms: 1234,
            api_key: Some(vd_config::ApiKey::new("TEST-SECRET-KEY")),
        };
        let settings = settings_from_config(&config);
        assert!(settings.enabled);
        assert_eq!(settings.protocol, vd_fim::Protocol::LlamaCpp);
        assert_eq!(settings.endpoint, "http://127.0.0.1:8080");
        assert_eq!(settings.model, "local-model");
        assert_eq!(settings.timeout_ms, 1234);
        assert!(settings.api_key.is_some());
        let debug = format!("{settings:?}");
        assert!(
            !debug.contains("TEST-SECRET-KEY"),
            "the key must be redacted in Debug output"
        );
    }

    #[test]
    fn configured_loopback_request_reaches_the_rust_transport() {
        let endpoint = FakeEndpoint::start("{\"choices\":[{\"text\":\" assign x = 1;\"}]}");
        let settings = settings_for(&endpoint.endpoint, 2_000);
        let result = complete_with_settings(settings, "module m;\n  ", "");
        match result {
            Ok(data) => {
                assert_eq!(data.state, 1, "a configured endpoint returns text");
                assert!(data.text.contains("assign x = 1;"), "got {:?}", data.text);
            }
            Err(error) => panic!("configured request failed: {}", message_of(&error)),
        }
        let requests = endpoint.requests();
        assert_eq!(
            requests.len(),
            1,
            "exactly one request reached the endpoint"
        );
        let raw = &requests[0];
        assert!(
            raw.starts_with("POST /v1/completions"),
            "the OpenAI-compatible path is used: {raw}"
        );
        assert!(
            raw.to_lowercase()
                .contains("authorization: bearer secret-loopback"),
            "the key is sent only as the bearer header"
        );
        assert!(raw.contains("\"prompt\""), "the prompt field is present");
        assert!(raw.contains("module m;"), "the prefix reached the endpoint");
    }

    #[test]
    fn unreachable_endpoint_is_an_explicit_error_without_the_key() {
        let settings = vd_fim::FimSettings {
            enabled: true,
            endpoint: "http://127.0.0.1:1".to_string(),
            model: "qwen2.5-coder".to_string(),
            timeout_ms: 500,
            api_key: Some(vd_fim::ApiKey::new("SECRET-UNREACHABLE")),
            ..vd_fim::FimSettings::default()
        };
        let result = complete_with_settings(settings, "a", "b");
        match result {
            Err(error) => {
                assert_eq!(error.code, VD_ERR_SERVICE);
                let message = message_of(&error);
                assert!(message.contains("FIM"), "got {message:?}");
                assert!(!message.contains("SECRET-UNREACHABLE"), "no key in errors");
            }
            Ok(data) => panic!(
                "an unreachable endpoint must be an explicit error, got state={} text={}",
                data.state, data.text
            ),
        }
    }

    #[test]
    fn loaded_project_and_environment_configure_the_service() {
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let config_path = directory.join("vedit.toml");
        let body = "[fim]\nenabled = false\nendpoint = \"http://127.0.0.1:8\"\n\
                    model = \"file-model\"\ntimeout_ms = 1500\n";
        if let Err(error) = std::fs::write(&config_path, body) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        let environment = vd_config::Env::from_pairs([
            ("HOME", home.clone()),
            ("XDG_CONFIG_HOME", home),
            ("VEDIT_FIM_ENABLED", "1".to_string()),
            ("VEDIT_FIM_ENDPOINT", "http://127.0.0.1:7".to_string()),
            ("VEDIT_FIM_MODEL", "env-model".to_string()),
        ]);
        let loaded = vd_config::Loader::new(&directory)
            .with_env(environment)
            .load();
        let loaded = match loaded {
            Ok(loaded) => loaded,
            Err(error) => panic!("cannot load the temporary project: {error}"),
        };
        let settings = settings_from_config(&loaded.config.fim);
        assert!(settings.enabled, "the environment switches it on");
        assert_eq!(
            settings.endpoint, "http://127.0.0.1:7",
            "env wins over the file"
        );
        assert_eq!(settings.model, "env-model", "env wins over the file");
        assert_eq!(settings.timeout_ms, 1500, "the file value is kept");
        let _ = std::fs::remove_dir_all(&directory);
    }

    // ---- frozen contract §7: store takeover and its fallback ----------------

    /// Write a settings store the way the product does (flat quoted keys).
    fn write_store(path: &std::path::Path, entries: &[(&str, &str)]) {
        let entries: Vec<vd_settings::Entry> = entries
            .iter()
            .map(|(key, value)| vd_settings::Entry {
                key: (*key).to_string(),
                value: (*value).to_string(),
            })
            .collect();
        if let Err(error) = vd_settings::save(path, &entries) {
            panic!("cannot write the settings store: {error}");
        }
    }

    /// Bind `path` as the FIM settings store through the C ABI (`None` unbinds).
    fn bind_store(path: Option<&std::path::Path>) -> i32 {
        use std::ffi::CString;
        match path {
            Some(path) => {
                let text = CString::new(path.to_string_lossy().into_owned()).unwrap_or_default();
                // SAFETY: the text is a valid NUL-terminated path for this call.
                unsafe { vd_fim_use_settings_store(text.as_ptr()) }
            }
            // SAFETY: a null path unbinds by contract.
            None => unsafe { vd_fim_use_settings_store(std::ptr::null()) },
        }
    }

    /// The JSON body of the last request the fake endpoint saw.
    fn last_body(endpoint: &FakeEndpoint) -> serde_json::Value {
        let raw = endpoint
            .requests()
            .pop()
            .unwrap_or_else(|| panic!("no request recorded"));
        let body = raw.split("\r\n\r\n").nth(1).unwrap_or_default();
        serde_json::from_str(body)
            .unwrap_or_else(|error| panic!("the request body is not JSON ({error}): {body}"))
    }

    /// A project whose `vedit.toml` configures FIM, with an isolated environment
    /// so the host's own configuration and `VEDIT_FIM_*` cannot leak in.
    fn project_with_vedit_fim(directory: &std::path::Path, endpoint: &str) -> vd_config::Env {
        let body = format!(
            "[fim]\nenabled = true\nendpoint = \"{endpoint}\"\nmodel = \"file-model\"\n\
             timeout_ms = 1500\n"
        );
        if let Err(error) = std::fs::write(directory.join("vedit.toml"), body) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        vd_config::Env::from_pairs([("HOME", home.clone()), ("XDG_CONFIG_HOME", home)])
    }

    /// The same project config, with a non-default protocol and timeout so a
    /// field-level inheritance test can tell `vd_config`'s value apart from the
    /// schema default (`open_ai` / `5000`).
    fn project_with_vedit_fim_protocol(
        directory: &std::path::Path,
        endpoint: &str,
    ) -> vd_config::Env {
        let body = format!(
            "[fim]\nenabled = true\nprotocol = \"llama_cpp\"\nendpoint = \"{endpoint}\"\n\
             model = \"file-model\"\ntimeout_ms = 1750\n"
        );
        if let Err(error) = std::fs::write(directory.join("vedit.toml"), body) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        vd_config::Env::from_pairs([("HOME", home.clone()), ("XDG_CONFIG_HOME", home)])
    }

    #[test]
    fn a_ui_owned_store_takes_over_and_its_max_tokens_reach_the_transport() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let endpoint = FakeEndpoint::start("{\"choices\":[{\"text\":\"x\"}]}");
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the store directory: {error}");
        }
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "true"),
                ("fim.protocol", "open_ai"),
                ("fim.endpoint", &endpoint.endpoint),
                ("fim.model", "store-model"),
                ("fim.timeout_ms", "4000"),
                ("fim.max_tokens", "137"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0, "a readable store must bind");

        let data = match complete("module m;", "") {
            Ok(data) => data,
            Err(error) => panic!(
                "the store-configured completion failed: {}",
                message_of(&error)
            ),
        };
        assert_eq!(
            data.state, 1,
            "the store's endpoint must answer: {}",
            data.text
        );
        assert_eq!(data.text, "x");

        // The wire body is the proof that the store really drove the request and
        // that `max_tokens` reached the transport layer.
        let body = last_body(&endpoint);
        assert_eq!(
            body,
            serde_json::json!({
                "model": "store-model",
                "prompt": "module m;",
                "suffix": "",
                "max_tokens": 137,
            }),
            "the store's model, prompt, suffix and max_tokens must reach the wire"
        );

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_store_without_fim_keys_leaves_the_vd_config_path_untouched() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim(&directory, "http://127.0.0.1:9");
        // The exact regression frozen contract §7 prevents: the settings page was
        // applied once, so the store exists with unrelated keys only.
        let store = directory.join("settings.toml");
        write_store(&store, &[("theme", "dark"), ("ui.font_point_size", "15")]);
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!(
                "the vd_config fallback must not fail: {}",
                message_of(&error)
            ),
        };
        assert!(
            settings.enabled,
            "vedit.toml configured FIM; a store without fim.* must not disable it"
        );
        assert_eq!(settings.endpoint, "http://127.0.0.1:9");
        assert_eq!(settings.model, "file-model");
        assert_eq!(settings.timeout_ms, 1500);

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §12 B1 (written RED first): a store that carries `fim.*`
    /// keys but no ownership marker is NOT authoritative. The settings page writes
    /// the whole `fim.*` block on every "Apply" — even when the user never opened
    /// the FIM page — and an older build stored `fim.enabled = false` the same
    /// way. Taking over would silently disable a user who configured `[fim]` in
    /// `vedit.toml`, which is the exact regression §7 protects against.
    #[test]
    fn a_store_with_fim_keys_but_no_ui_owned_marker_leaves_the_vd_config_path_in_charge() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim(&directory, "http://127.0.0.1:21");
        let store = directory.join("settings.toml");

        // The old build's "Apply" defaults: a complete `fim.*` block, no marker.
        write_store(
            &store,
            &[
                ("fim.enabled", "false"),
                ("fim.protocol", "open_ai"),
                ("fim.timeout_ms", "5000"),
                ("fim.max_tokens", "64"),
                ("fim.ghost_text", "false"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);
        let settings = match load_settings_from(&directory, Some(environment.clone())) {
            Ok(settings) => settings,
            Err(error) => panic!(
                "the vd_config fallback must not fail: {}",
                message_of(&error)
            ),
        };
        assert!(
            settings.enabled,
            "an unmarked store must not disable a vedit.toml-configured user"
        );
        assert_eq!(settings.endpoint, "http://127.0.0.1:21");
        assert_eq!(settings.model, "file-model");

        // The reviewer's literal shape: even a store that says `fim.enabled=true`
        // gains no authority from a `fim.*` key — the marker is the only opt-in.
        write_store(
            &store,
            &[
                ("fim.enabled", "true"),
                ("fim.endpoint", "http://127.0.0.1:9"),
                ("fim.model", "store-model"),
            ],
        );
        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!(
                "the vd_config fallback must not fail: {}",
                message_of(&error)
            ),
        };
        assert!(
            settings.enabled,
            "the store's `fim.enabled=true` is not an ownership declaration"
        );
        assert_eq!(
            settings.endpoint, "http://127.0.0.1:21",
            "a `fim.*` key alone must not take over"
        );
        assert_eq!(settings.model, "file-model");

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §12 B1, the accepting half: the ownership marker is what
    /// hands FIM to the store — and then every field (not `vedit.toml`'s) drives
    /// the request path.
    #[test]
    fn a_store_that_declares_ui_ownership_takes_over() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim(&directory, "http://127.0.0.1:21");
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "false"),
                ("fim.protocol", "llama_cpp"),
                ("fim.endpoint", "http://127.0.0.1:31"),
                ("fim.model", "store-model"),
                ("fim.timeout_ms", "2500"),
                ("fim.max_tokens", "128"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        assert!(!settings.enabled, "the store owns FIM and says off");
        assert_eq!(settings.protocol, vd_fim::Protocol::LlamaCpp);
        assert_eq!(settings.endpoint, "http://127.0.0.1:31");
        assert_eq!(settings.model, "store-model");
        assert_eq!(settings.timeout_ms, 2_500);
        assert_eq!(settings.max_tokens, 128);

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §13 F1 (written RED first): a UI-owned store inherits the
    /// FIM fields it does **not** carry from `vd_config` instead of taking the
    /// schema defaults. The pre-fix code projected the whole block with defaults,
    /// so a store written by an older build that held only `fim.endpoint` +
    /// `fim.model` came back `enabled = false` and silently disabled a user whose
    /// `[fim]` lives in `vedit.toml`.
    #[test]
    fn a_ui_owned_store_inherits_the_fields_it_does_not_carry() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim_protocol(&directory, "http://127.0.0.1:51");
        let store = directory.join("settings.toml");
        // Only two of the seven `fim.*` fields are carried.
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.endpoint", "http://127.0.0.1:53"),
                ("fim.model", "store-model"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        // R1: the carried fields are the store's ...
        assert_eq!(settings.endpoint, "http://127.0.0.1:53");
        assert_eq!(settings.model, "store-model");
        // ... and every field it does not carry is `vedit.toml`'s, NOT a default.
        assert!(
            settings.enabled,
            "an absent `fim.enabled` must inherit vedit.toml's `true`, not the default `false`"
        );
        assert_eq!(
            settings.protocol,
            vd_fim::Protocol::LlamaCpp,
            "an absent `fim.protocol` must inherit vedit.toml's llama_cpp"
        );
        assert_eq!(
            settings.timeout_ms, 1_750,
            "an absent `fim.timeout_ms` must inherit vedit.toml's 1750"
        );
        // `vd_config::FimConfig` has no `max_tokens` field (a documented fact of
        // the config model), so the inherited value is the transport default —
        // byte-for-byte what the `vd_config` path yields for that field.
        assert_eq!(settings.max_tokens, vd_fim::DEFAULT_MAX_TOKENS);

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §13 F1, R2: an explicit store value beats the inherited
    /// one. `fim.enabled = false` is a real user decision and must stay `false`
    /// even though `vedit.toml` says `true`.
    #[test]
    fn an_explicit_store_field_beats_the_inherited_one() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim_protocol(&directory, "http://127.0.0.1:51");
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "false"),
                ("fim.endpoint", "http://127.0.0.1:53"),
                ("fim.model", "store-model"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        assert!(
            !settings.enabled,
            "the store's explicit `fim.enabled=false` must win over the inherited true"
        );
        assert_eq!(settings.endpoint, "http://127.0.0.1:53");
        assert_eq!(settings.model, "store-model");
        assert_eq!(
            settings.protocol,
            vd_fim::Protocol::LlamaCpp,
            "a field the store does not carry still inherits vedit.toml"
        );
        assert_eq!(settings.timeout_ms, 1_750);

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §13 F1, R3: when the store carries every field, every field
    /// comes from the store — the inheritance base never leaks through.
    ///
    /// `fim.ghost_text` is the seventh stored field and has no counterpart on
    /// `vd_fim::FimSettings` (inline rendering is a UI decision, not a request
    /// field), so it is written here to prove the full block is accepted; the six
    /// transport fields are asserted.
    #[test]
    fn every_field_the_store_carries_comes_from_the_store() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim_protocol(&directory, "http://127.0.0.1:61");
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "false"),
                ("fim.protocol", "open_ai"),
                ("fim.endpoint", "http://127.0.0.1:63"),
                ("fim.model", "store-model"),
                ("fim.timeout_ms", "2500"),
                ("fim.max_tokens", "128"),
                ("fim.ghost_text", "true"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        assert!(
            !settings.enabled,
            "the store's own `false`, not the inherited true"
        );
        assert_eq!(
            settings.protocol,
            vd_fim::Protocol::OpenAi,
            "the store's own protocol, not the inherited llama_cpp"
        );
        assert_eq!(settings.endpoint, "http://127.0.0.1:63");
        assert_eq!(settings.model, "store-model");
        assert_eq!(settings.timeout_ms, 2_500);
        assert_eq!(settings.max_tokens, 128);

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// Frozen contract §7's third key level (§12 B2): a UI-owned store that
    /// carries no key of its own, with `VEDIT_FIM_API_KEY` unset and no `0600`
    /// key file next to the store, still honours the `[fim] api_key` of
    /// `vedit.toml`. Only the key crosses over — the UI values stay the store's,
    /// and the 0600 file still outranks `vedit.toml`.
    #[test]
    fn a_ui_owned_store_takes_the_missing_key_from_vedit_toml() {
        const TOML_KEY: &str = "sk-vedit-toml-DO-NOT-LEAK-4242";
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let body = format!(
            "[fim]\nenabled = true\nendpoint = \"http://127.0.0.1:41\"\n\
             model = \"toml-model\"\napi_key = \"{TOML_KEY}\"\n"
        );
        if let Err(error) = std::fs::write(directory.join("vedit.toml"), body) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        let environment =
            vd_config::Env::from_pairs([("HOME", home.clone()), ("XDG_CONFIG_HOME", home)]);
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "true"),
                ("fim.endpoint", "http://127.0.0.1:43"),
                ("fim.model", "store-model"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment.clone())) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        assert!(settings.enabled);
        assert_eq!(
            settings.endpoint, "http://127.0.0.1:43",
            "only the key may come from vedit.toml; the UI fields stay the store's"
        );
        assert_eq!(settings.model, "store-model");
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(TOML_KEY),
            "the third level of the documented key order must be reachable"
        );
        assert!(
            !format!("{settings:?}").contains(TOML_KEY),
            "debug output leaked the key"
        );

        // The owner-only file next to the store still outranks `vedit.toml`.
        let key_file = vd_settings::schema::api_key_path(&store);
        if let Err(error) = vd_settings::schema::write_api_key_file(
            &key_file,
            &vd_settings::schema::ApiKey::new("from-file"),
        ) {
            panic!("cannot write the key file: {error}");
        }
        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the owned store must load: {}", message_of(&error)),
        };
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some("from-file"),
            "the 0600 file must still win over vedit.toml"
        );

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn without_a_binding_the_vd_config_path_is_used() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let environment = project_with_vedit_fim(&directory, "http://127.0.0.1:11");
        assert_eq!(bind_store(None), 0, "unbinding must succeed");

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the vd_config path must not fail: {}", message_of(&error)),
        };
        assert!(settings.enabled);
        assert_eq!(settings.endpoint, "http://127.0.0.1:11");
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// batch-20261114 F2: with NO bound store the documented key order
    /// degenerates to `VEDIT_FIM_API_KEY > vedit.toml` — the `0600 file next to
    /// settings.toml` level needs a bound store directory, which this path does
    /// not have. The documentation promised that order; no test pinned it.
    #[test]
    fn an_unbound_store_resolves_the_key_environment_then_vedit_toml() {
        const TOML_KEY: &str = "sk-fim-from-vedit-toml-2026";
        const ENVIRONMENT_KEY: &str = "sk-fim-from-the-environment-2026";
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        if let Err(error) = std::fs::write(
            directory.join("vedit.toml"),
            format!(
                "[fim]\nenabled = true\nendpoint = \"http://127.0.0.1:9\"\n\
                 model = \"file-model\"\napi_key = \"{TOML_KEY}\"\n"
            ),
        ) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        let environment = |key: Option<&str>| {
            let mut pairs = vec![
                ("HOME".to_string(), home.clone()),
                ("XDG_CONFIG_HOME".to_string(), home.clone()),
            ];
            if let Some(key) = key {
                pairs.push((
                    vd_settings::schema::API_KEY_ENV.to_string(),
                    key.to_string(),
                ));
            }
            vd_config::Env::from_pairs(pairs)
        };
        assert_eq!(bind_store(None), 0, "this test is the UNBOUND path");

        // Level 2: `vedit.toml` answers when the environment carries no key.
        let settings = match load_settings_from(&directory, Some(environment(None))) {
            Ok(settings) => settings,
            Err(error) => panic!("the unbound path must load: {}", message_of(&error)),
        };
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(TOML_KEY),
            "with no store and no environment key, vedit.toml's key is used"
        );

        // Level 1: the environment wins over `vedit.toml`.
        let settings =
            match load_settings_from(&directory, Some(environment(Some(ENVIRONMENT_KEY)))) {
                Ok(settings) => settings,
                Err(error) => panic!("the unbound path must load: {}", message_of(&error)),
            };
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(ENVIRONMENT_KEY),
            "the environment level wins over vedit.toml"
        );
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_refused_store_path_keeps_the_previous_binding() {
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let store = directory.join("settings.toml");
        write_store(
            &store,
            &[
                ("fim.ui_owned", "true"),
                ("fim.enabled", "true"),
                ("fim.endpoint", "http://127.0.0.1:9"),
                ("fim.model", "m"),
            ],
        );
        assert_eq!(bind_store(Some(&store)), 0);

        // A directory is not a store: refused, and the previous binding survives.
        assert_eq!(bind_store(Some(&directory)), -1);
        let settings = match load_settings_from(&directory, None) {
            Ok(settings) => settings,
            Err(error) => panic!(
                "the bound store must still be usable: {}",
                message_of(&error)
            ),
        };
        assert_eq!(
            settings.endpoint, "http://127.0.0.1:9",
            "a refused path must not change the binding"
        );

        // Non-UTF-8 bytes are not a path at all.
        let invalid = [0xff_u8, 0x00];
        // SAFETY: the bytes are NUL-terminated.
        let code =
            unsafe { vd_fim_use_settings_store(invalid.as_ptr() as *const std::ffi::c_char) };
        assert_eq!(code, -1);

        // A path that does not exist yet is usable: the store appears on first save.
        let future = directory.join("future-settings.toml");
        assert_eq!(bind_store(Some(&future)), 0);

        // Unbinding restores the vd_config path; with no `[fim]` there, the
        // product default is the disabled state.
        assert_eq!(bind_store(None), 0);
        let home = directory.to_string_lossy().into_owned();
        let environment =
            vd_config::Env::from_pairs([("HOME", home.clone()), ("XDG_CONFIG_HOME", home)]);
        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the default path must load: {}", message_of(&error)),
        };
        assert!(!settings.enabled, "the product default is disabled");
        assert!(settings.endpoint.is_empty());
        let _ = std::fs::remove_dir_all(&directory);
    }

    // ---- §15 A34: a bound store without the ownership marker still resolves
    // the key as `env > 0600 file next to the store > vedit.toml` -------------

    /// The reachable flow A34 names: the user only saved a key from the settings
    /// page, which marks no field as expressed, so the store carries no
    /// `fim.ui_owned` marker and the request path follows `vedit.toml`. The key
    /// order is not part of the takeover decision, so the 0600 file next to the
    /// store must still supply the key — otherwise detection could use a key the
    /// completion request could not.
    #[test]
    fn a_markerless_store_takes_the_missing_key_from_the_key_file_next_to_it() {
        const FILE_KEY: &str = "sk-file-DO-NOT-LEAK-1111";
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let endpoint = FakeEndpoint::start("{\"choices\":[{\"text\":\"x\"}]}");
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        // `vedit.toml` configures FIM but carries no key.
        let environment = project_with_vedit_fim(&directory, &endpoint.endpoint);
        let store = directory.join("settings.toml");
        // A real store: it exists, but carries neither a `fim.*` key nor the marker.
        write_store(&store, &[("theme", "dark")]);
        let key_file = vd_settings::schema::api_key_path(&store);
        if let Err(error) = vd_settings::schema::write_api_key_file(
            &key_file,
            &vd_settings::schema::ApiKey::new(FILE_KEY),
        ) {
            panic!("cannot write the key file: {error}");
        }
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the markerless path must load: {}", message_of(&error)),
        };
        assert!(
            settings.enabled,
            "without the marker the vd_config values stay in charge"
        );
        assert_eq!(settings.endpoint, endpoint.endpoint);
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(FILE_KEY),
            "the markerless request path must read the 0600 key file next to the bound store"
        );
        assert!(
            !format!("{settings:?}").contains(FILE_KEY),
            "debug output leaked the key"
        );

        // The key is not just a settings value: it reaches the transport.
        let data = match complete_with_settings(settings, "module m;", "") {
            Ok(data) => data,
            Err(error) => panic!("the markerless completion failed: {}", message_of(&error)),
        };
        assert_eq!(data.state, 1, "the completion must be sent: {}", data.text);
        let requests = endpoint.requests();
        assert_eq!(
            requests.len(),
            1,
            "exactly one request reached the endpoint"
        );
        assert!(
            requests[0].to_lowercase().contains(&format!(
                "authorization: bearer {}",
                FILE_KEY.to_lowercase()
            )),
            "the file's key must be the bearer header of the request"
        );

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// The environment key still outranks the 0600 file when the store carries no
    /// ownership marker (the first two levels of the documented order).
    #[test]
    fn a_markerless_store_prefers_the_environment_key_over_the_key_file() {
        const FILE_KEY: &str = "sk-file-DO-NOT-LEAK-2222";
        const ENV_KEY: &str = "sk-env-DO-NOT-LEAK-2222";
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let _ = project_with_vedit_fim(&directory, "http://127.0.0.1:12");
        let store = directory.join("settings.toml");
        write_store(&store, &[("theme", "dark")]);
        let key_file = vd_settings::schema::api_key_path(&store);
        if let Err(error) = vd_settings::schema::write_api_key_file(
            &key_file,
            &vd_settings::schema::ApiKey::new(FILE_KEY),
        ) {
            panic!("cannot write the key file: {error}");
        }
        assert_eq!(bind_store(Some(&store)), 0);

        let home = directory.to_string_lossy().into_owned();
        let environment = vd_config::Env::from_pairs([
            ("HOME", home.clone()),
            ("XDG_CONFIG_HOME", home),
            (vd_settings::schema::API_KEY_ENV, ENV_KEY.to_string()),
        ]);
        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the markerless path must load: {}", message_of(&error)),
        };
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(ENV_KEY),
            "VEDIT_FIM_API_KEY must win over the 0600 key file on the markerless path"
        );

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// `vedit.toml`'s `[fim] api_key` is the third level: the 0600 file next to a
    /// markerless store must override it, not be shadowed by it.
    #[test]
    fn a_markerless_store_prefers_the_key_file_over_the_vedit_toml_key() {
        const FILE_KEY: &str = "sk-file-DO-NOT-LEAK-3333";
        const TOML_KEY: &str = "sk-vedit-toml-DO-NOT-LEAK-3333";
        let _guard = BINDING_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
        let directory = unique_temp_dir();
        if let Err(error) = std::fs::create_dir_all(&directory) {
            panic!("cannot create the temporary project: {error}");
        }
        let body = format!(
            "[fim]\nenabled = true\nendpoint = \"http://127.0.0.1:13\"\n\
             model = \"file-model\"\napi_key = \"{TOML_KEY}\"\n"
        );
        if let Err(error) = std::fs::write(directory.join("vedit.toml"), body) {
            panic!("cannot write the project config: {error}");
        }
        let home = directory.to_string_lossy().into_owned();
        let environment =
            vd_config::Env::from_pairs([("HOME", home.clone()), ("XDG_CONFIG_HOME", home)]);
        let store = directory.join("settings.toml");
        write_store(&store, &[("theme", "dark")]);
        let key_file = vd_settings::schema::api_key_path(&store);
        if let Err(error) = vd_settings::schema::write_api_key_file(
            &key_file,
            &vd_settings::schema::ApiKey::new(FILE_KEY),
        ) {
            panic!("cannot write the key file: {error}");
        }
        assert_eq!(bind_store(Some(&store)), 0);

        let settings = match load_settings_from(&directory, Some(environment)) {
            Ok(settings) => settings,
            Err(error) => panic!("the markerless path must load: {}", message_of(&error)),
        };
        assert_eq!(
            settings.api_key.as_ref().map(vd_fim::ApiKey::expose_secret),
            Some(FILE_KEY),
            "the 0600 file must win over vedit.toml's [fim] api_key on the markerless path"
        );
        assert!(
            !format!("{settings:?}").contains(TOML_KEY)
                && !format!("{settings:?}").contains(FILE_KEY),
            "debug output leaked a key"
        );

        assert_eq!(bind_store(None), 0);
        let _ = std::fs::remove_dir_all(&directory);
    }
}
