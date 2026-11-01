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

/// Load the product configuration (project `vedit.toml`, then the user file, then
/// the `VEDIT_FIM_*` environment variables) and project its FIM block.
///
/// There is **no default endpoint, model or key**: the product default is the
/// disabled `FimSettings`, so an unconfigured run never builds a transport.
fn load_settings() -> Result<vd_fim::FimSettings, VdError> {
    let start_dir = std::env::current_dir().map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!("FIM: cannot resolve the working directory: {error}"),
        )
    })?;
    let loaded = vd_config::Loader::new(start_dir).load().map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!("FIM: cannot load the configuration: {error}"),
        )
    })?;
    Ok(settings_from_config(&loaded.config.fim))
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
}
