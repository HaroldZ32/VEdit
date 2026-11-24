//! FIM endpoint detection exposed as one owned JSON string (contract §3/§4).
//!
//! The algorithm lives in `vd_fim::detect` and returns typed values; this module
//! is the only place the frozen JSON object is produced, so the settings page has
//! exactly one parse path — the same shape for `ok=true` and `ok=false`.
//!
//! The key discipline is structural, not a promise: [`report_json`] takes a
//! [`DetectReport`], which has no key field, and this module's only other input
//! is the borrowed key value that goes to the transport. There is no code path
//! that could put a key, a header or a query into the JSON.
//!
//! Detection is GET-only (`vd_fim::detect_http`), so a probe spends no tokens and
//! cannot insert text. The call is **synchronous** (the frozen signature has no
//! job/poll pair), so the caller runs it on its own worker thread; it can block
//! that thread for at most one timeout per candidate (two candidates, contract
//! §5.4).

use std::ffi::c_char;
use std::path::Path;
use std::time::Duration;

use serde_json::{json, Value};

use crate::capi::{c_str, c_string, free_string};

/// `*out_json` was written. The probe itself may still have failed (`ok=false`).
const DETECT_WRITTEN: i32 = 0;
/// The arguments were unusable; nothing was written to `*out_json`.
const DETECT_UNUSABLE: i32 = -1;

/// The frozen field set (contract §4): exactly these nine keys, nothing else.
const JSON_FIELDS: [&str; 9] = [
    "ok",
    "provider",
    "base_url",
    "completion_url",
    "protocol",
    "models",
    "models_url",
    "latency_ms",
    "reason",
];

/// Project a typed report onto the frozen JSON object (contract §4).
///
/// `reason` is the existing [`vd_fim::FimError::reason`] sentence, so a failure
/// and a success share one shape and the UI never has to know Rust's error type.
#[must_use]
pub(crate) fn report_json(report: &vd_fim::DetectReport) -> Value {
    let value = json!({
        "ok": report.ok(),
        "provider": report.provider.as_str(),
        "base_url": report.base_url,
        "completion_url": report.completion_url,
        "protocol": report.protocol.as_str(),
        "models": report.models,
        "models_url": report.models_url,
        "latency_ms": report.latency_ms,
        "reason": report.reason(),
    });
    debug_assert_eq!(
        value.as_object().map(serde_json::Map::len),
        Some(JSON_FIELDS.len()),
        "the detect JSON must carry exactly the frozen field set"
    );
    value
}

/// Clamp the caller's timeout the way completion settings are clamped, so a `0`
/// cannot make every probe fail instantly.
fn detect_timeout(timeout_ms: u32) -> Duration {
    Duration::from_millis(
        u64::from(timeout_ms).clamp(vd_fim::MIN_TIMEOUT_MS, vd_fim::MAX_TIMEOUT_MS),
    )
}

/// Resolve the key the documented way when the caller passes `NULL`:
/// `VEDIT_FIM_API_KEY` first, then the owner-only `fim_api_key` file next to the
/// bound settings store, then `vedit.toml`'s `[fim] api_key` (batch-20261114 F1)
/// — the SAME order the completion request path resolves, so detect and
/// completion can never disagree about which key a project uses.
///
/// Without a bound store there is no "same directory" for the 0600 file, so the
/// order degenerates to `VEDIT_FIM_API_KEY > vedit.toml` (batch-20261114 F2).
fn resolve_key() -> Option<vd_fim::ApiKey> {
    let start_dir = std::env::current_dir().ok()?;
    resolve_key_from(&start_dir)
}

/// [`resolve_key`] with an explicit project start directory: the completion
/// request path reads `vedit.toml` from the process working directory, and the
/// product has no other notion of "the project" on this entry point.
fn resolve_key_from(start_dir: &Path) -> Option<vd_fim::ApiKey> {
    let environment = std::env::var(vd_settings::schema::API_KEY_ENV).ok();
    match crate::fim::bound_settings_store() {
        Some(path) => vd_settings::schema::resolve_api_key(&path, environment.as_deref())
            .key
            .map(|key| vd_fim::ApiKey::new(key.expose_secret()))
            .or_else(|| vedit_toml_key(start_dir)),
        None => environment
            .map(|raw| raw.trim().to_string())
            .filter(|raw| !raw.is_empty())
            .map(vd_fim::ApiKey::new)
            .or_else(|| vedit_toml_key(start_dir)),
    }
}

/// The third level: `vedit.toml`'s `[fim] api_key`, read through the same
/// `vd_config` loader the completion request path uses. Its own env overlay
/// folds `VEDIT_FIM_API_KEY` into `fim.api_key`, so the environment level keeps
/// winning when it is set — this function is the FALLBACK, never a replacement
/// for the explicit `env > 0600 file` levels above it.
fn vedit_toml_key(start_dir: &Path) -> Option<vd_fim::ApiKey> {
    let loaded = vd_config::Loader::new(start_dir).load().ok()?;
    loaded
        .config
        .fim
        .api_key
        .as_ref()
        .map(|key| vd_fim::ApiKey::new(key.expose_secret()))
}

/// Probe one endpoint and write the frozen JSON object to `*out_json`.
///
/// Returns `0` when `*out_json` was written — including a probe that failed at the
/// endpoint (`ok=false` with `reason`) — and `-1` when the arguments were
/// unusable, in which case `*out_json` is untouched.
///
/// `endpoint` is a value, not an argument check: an empty string is probed and
/// reported as `ok=false, reason="FIM 端点无效"` (contract §5.1), which keeps the
/// UI's single parse path. `api_key_or_null == NULL` resolves the key Rust-side;
/// an empty string means "no key" explicitly.
///
/// # Safety
///
/// `endpoint` must be null or a NUL-terminated string, `api_key_or_null` must be
/// null or a NUL-terminated string, and `out_json` must be null or writable for
/// one pointer. The string written there must be released with
/// [`vd_fim_detect_free`].
#[no_mangle]
pub unsafe extern "C" fn vd_fim_detect(
    endpoint: *const c_char,
    api_key_or_null: *const c_char,
    timeout_ms: u32,
    out_json: *mut *mut c_char,
) -> i32 {
    if out_json.is_null() {
        return DETECT_UNUSABLE;
    }
    let Some(endpoint) = c_str(endpoint) else {
        return DETECT_UNUSABLE;
    };
    let api_key = if api_key_or_null.is_null() {
        resolve_key()
    } else {
        match c_str(api_key_or_null) {
            Some(raw) if raw.trim().is_empty() => None,
            Some(raw) => Some(vd_fim::ApiKey::new(raw.trim())),
            None => return DETECT_UNUSABLE,
        }
    };
    let report = vd_fim::detect_http(endpoint, api_key.as_ref(), detect_timeout(timeout_ms));
    let text = c_string(&report_json(&report).to_string());
    if text.is_null() {
        return DETECT_UNUSABLE;
    }
    *out_json = text;
    DETECT_WRITTEN
}

/// Release a JSON string produced by [`vd_fim_detect`]; null-safe.
///
/// # Safety
///
/// `json` must be null or a pointer returned by [`vd_fim_detect`] and not freed
/// yet.
#[no_mangle]
pub unsafe extern "C" fn vd_fim_detect_free(json: *mut c_char) {
    free_string(json);
}

#[cfg(test)]
mod tests {
    //! Offline proof of the frozen JSON shape plus a real-socket prober: the fake
    //! server below answers on 127.0.0.1 only and every wait has a budget, so the
    //! tests never touch the outside network.

    use super::{detect_timeout, report_json, resolve_key, vd_fim_detect, vd_fim_detect_free};
    use serde_json::{json, Value};
    use std::ffi::{CStr, CString};
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::os::unix::fs::PermissionsExt;
    use std::ptr;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Mutex};
    use std::thread::{self, JoinHandle};
    use std::time::Duration;
    use vd_fim::{DetectReport, FimError, Protocol, Provider};

    const TEST_KEY: &str = "sk-detect-fixture-key";

    fn directory(name: &str) -> std::path::PathBuf {
        let path =
            std::env::temp_dir().join(format!("vd-ffi-detect-{}-{name}", std::process::id()));
        if let Err(error) = std::fs::create_dir_all(&path) {
            panic!("cannot create {}: {error}", path.display());
        }
        path
    }

    /// A loopback HTTP endpoint that answers every request with one status and
    /// body and records the raw request lines. Stopped (and joined) when dropped.
    struct FakeServer {
        endpoint: String,
        requests: Arc<Mutex<Vec<String>>>,
        stop: Arc<AtomicBool>,
        handle: Option<JoinHandle<()>>,
    }

    impl FakeServer {
        fn start(status: u16, body: &'static str) -> Self {
            Self::spawn(None, status, body)
        }

        /// A server that answers `401` unless the request carries
        /// `Authorization: Bearer <key>`, and `200` with `body` otherwise. The
        /// key level a probe resolved is only observable on the wire, so the
        /// acceptance test gates on the HEADER, not on a bare status.
        fn requiring_key(body: &'static str, key: &str) -> Self {
            Self::spawn(Some(key.to_string()), 200, body)
        }

        fn spawn(required_key: Option<String>, status: u16, body: &'static str) -> Self {
            let listener = match TcpListener::bind("127.0.0.1:0") {
                Ok(listener) => listener,
                Err(error) => panic!("cannot bind the fake server: {error}"),
            };
            let address = match listener.local_addr() {
                Ok(address) => address,
                Err(error) => panic!("cannot read the fake server address: {error}"),
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
                            let raw = read_request(&mut stream);
                            if let Ok(mut entries) = request_log.lock() {
                                entries.push(String::from_utf8_lossy(&raw).into_owned());
                            }
                            let authorised = match required_key.as_deref() {
                                Some(key) => {
                                    String::from_utf8_lossy(&raw).to_ascii_lowercase().contains(
                                        &format!("authorization: bearer {key}")
                                            .to_ascii_lowercase(),
                                    )
                                }
                                None => true,
                            };
                            let served = if authorised { status } else { 401 };
                            let reason = if served == 200 { "OK" } else { "Unauthorized" };
                            let response = format!(
                                "HTTP/1.1 {served} {reason}\r\nContent-Type: application/json\r\n\
                                 Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                                body.len(),
                                body
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

        /// The path of every request line seen, in arrival order.
        fn paths(&self) -> Vec<String> {
            self.requests()
                .iter()
                .filter_map(|raw| raw.split_whitespace().nth(1))
                .map(str::to_string)
                .collect()
        }
    }

    impl Drop for FakeServer {
        fn drop(&mut self) {
            self.stop.store(true, Ordering::SeqCst);
            if let Some(handle) = self.handle.take() {
                let _ = handle.join();
            }
        }
    }

    fn read_request(stream: &mut std::net::TcpStream) -> Vec<u8> {
        let mut raw = Vec::new();
        let mut chunk = [0_u8; 1024];
        loop {
            match stream.read(&mut chunk) {
                Ok(0) => break,
                Ok(read) => {
                    raw.extend_from_slice(&chunk[..read]);
                    if raw.windows(4).any(|window| window == b"\r\n\r\n") {
                        break;
                    }
                }
                Err(_) => break,
            }
        }
        raw
    }

    /// Probe through the C ABI and return the parsed JSON object.
    fn probe(endpoint: &str, key: Option<&str>, timeout_ms: u32) -> (i32, Option<Value>) {
        let endpoint = CString::new(endpoint).unwrap_or_else(|_| CString::default());
        let key = key.map(|value| CString::new(value).unwrap_or_else(|_| CString::default()));
        let key_pointer = key.as_ref().map_or(ptr::null(), |value| {
            value.as_ptr() as *const std::ffi::c_char
        });
        let mut out: *mut std::ffi::c_char = ptr::null_mut();
        // SAFETY: both strings outlive the call and `out` is writable.
        let code = unsafe { vd_fim_detect(endpoint.as_ptr(), key_pointer, timeout_ms, &mut out) };
        if out.is_null() {
            return (code, None);
        }
        // SAFETY: `vd_fim_detect` returned one owned C string and we free it below.
        let text = unsafe { CStr::from_ptr(out) }
            .to_string_lossy()
            .into_owned();
        // SAFETY: `out` came from `vd_fim_detect` and is not used again.
        unsafe { vd_fim_detect_free(out) };
        let value = serde_json::from_str(&text).ok();
        (code, value)
    }

    fn report(failure: Option<FimError>) -> DetectReport {
        DetectReport {
            provider: Provider::DeepSeek,
            base_url: "https://api.deepseek.com/beta".to_string(),
            completion_url: "https://api.deepseek.com/beta/completions".to_string(),
            protocol: Protocol::OpenAi,
            models: vec!["deepseek-flash".to_string(), "deepseek-v4-pro".to_string()],
            models_url: Some("https://api.deepseek.com/models".to_string()),
            latency_ms: 184,
            failure,
        }
    }

    #[test]
    fn the_json_carries_exactly_the_frozen_field_set() {
        let value = report_json(&report(None));
        let object = value
            .as_object()
            .unwrap_or_else(|| panic!("the detect JSON must be an object"));
        let mut keys: Vec<&str> = object.keys().map(String::as_str).collect();
        keys.sort_unstable();
        let mut expected = vec![
            "ok",
            "provider",
            "base_url",
            "completion_url",
            "protocol",
            "models",
            "models_url",
            "latency_ms",
            "reason",
        ];
        expected.sort_unstable();
        assert_eq!(keys, expected, "the frozen §4 field set changed");
        assert_eq!(object.get("ok"), Some(&Value::Bool(true)));
        assert_eq!(object.get("provider"), Some(&json!("deepseek")));
        assert_eq!(object.get("protocol"), Some(&json!("open_ai")));
        assert_eq!(object.get("latency_ms"), Some(&json!(184)));
        assert_eq!(object.get("reason"), Some(&Value::Null));
        assert_eq!(
            object.get("models"),
            Some(&json!(["deepseek-flash", "deepseek-v4-pro"]))
        );
    }

    #[test]
    fn a_failed_probe_keeps_the_same_shape_and_the_existing_reason() {
        let value = report_json(&report(Some(FimError::HttpStatus { status: 401 })));
        assert_eq!(value.get("ok"), Some(&Value::Bool(false)));
        assert_eq!(value.get("reason"), Some(&json!("FIM HTTP 401")));
        // The normalised completion URL is still reported on failure (contract §4);
        // the UI must not write it back.
        assert_eq!(
            value.get("completion_url"),
            Some(&json!("https://api.deepseek.com/beta/completions"))
        );
    }

    #[test]
    fn detection_is_get_only() {
        let server = FakeServer::start(200, r#"{"data":[{"id":"m1"}]}"#);
        let (code, value) = probe(&server.endpoint, Some(""), 2_000);
        assert_eq!(code, 0);
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(value.get("ok"), Some(&Value::Bool(true)));
        assert_eq!(server.paths(), vec!["/v1/models".to_string()]);
        let request = server
            .requests()
            .pop()
            .unwrap_or_else(|| panic!("no request recorded"));
        assert!(
            request.starts_with("GET "),
            "detection must only GET, got: {request}"
        );
        assert!(!request.contains("POST"));
    }

    #[test]
    fn a_real_probe_reports_models_and_the_normalised_completion_url() {
        let server = FakeServer::start(200, r#"{"data":[{"id":"a"},{"id":"b"}]}"#);
        let (code, value) = probe(&server.endpoint, Some(""), 2_000);
        assert_eq!(code, 0);
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(value.get("ok"), Some(&Value::Bool(true)));
        assert_eq!(value.get("provider"), Some(&json!("custom")));
        assert_eq!(value.get("models"), Some(&json!(["a", "b"])));
        assert_eq!(
            value.get("completion_url"),
            Some(&json!(format!("{}/v1/completions", server.endpoint)))
        );
        assert_eq!(
            value.get("models_url"),
            Some(&json!(format!("{}/v1/models", server.endpoint)))
        );
        assert_eq!(value.get("protocol"), Some(&json!("open_ai")));
        assert_eq!(value.get("reason"), Some(&Value::Null));
    }

    #[test]
    fn a_401_probe_reports_the_status_and_tries_the_candidates_in_order() {
        let server = FakeServer::start(401, r#"{"error":"denied"}"#);
        let (code, value) = probe(&server.endpoint, Some(""), 2_000);
        assert_eq!(code, 0, "a failed probe is still a written JSON report");
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(value.get("ok"), Some(&Value::Bool(false)));
        assert_eq!(value.get("reason"), Some(&json!("FIM HTTP 401")));
        assert_eq!(value.get("models"), Some(&json!([])));
        assert_eq!(value.get("models_url"), Some(&Value::Null));
        assert_eq!(
            server.paths(),
            vec!["/v1/models".to_string(), "/models".to_string()],
            "the custom-provider candidate order is frozen in contract §5.4"
        );
    }

    #[test]
    fn the_key_goes_on_the_wire_but_never_enters_the_json() {
        let server = FakeServer::start(200, r#"{"data":[]}"#);
        let (code, value) = probe(&server.endpoint, Some(TEST_KEY), 2_000);
        assert_eq!(code, 0);
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        let request = server
            .requests()
            .pop()
            .unwrap_or_else(|| panic!("no request recorded"));
        let lower = request.to_ascii_lowercase();
        assert!(
            lower.contains(&format!(
                "authorization: bearer {}",
                TEST_KEY.to_ascii_lowercase()
            )),
            "the key must be sent as a bearer token: {request}"
        );
        let rendered = value.to_string();
        assert!(
            !rendered.contains(TEST_KEY),
            "the key must never appear in the JSON: {rendered}"
        );
        assert!(
            !rendered.to_ascii_lowercase().contains("authorization"),
            "no header may appear in the JSON: {rendered}"
        );
    }

    #[test]
    fn an_unreachable_endpoint_reports_a_transport_reason() {
        // Bind a port, learn it, drop the listener: connecting to it is refused.
        let address = {
            let listener = TcpListener::bind("127.0.0.1:0")
                .unwrap_or_else(|error| panic!("cannot bind a throwaway port: {error}"));
            listener
                .local_addr()
                .unwrap_or_else(|error| panic!("cannot read the throwaway address: {error}"))
        };
        let (code, value) = probe(&format!("http://{address}"), Some(""), 1_000);
        assert_eq!(code, 0);
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(value.get("ok"), Some(&Value::Bool(false)));
        let reason = value
            .get("reason")
            .and_then(Value::as_str)
            .unwrap_or_default();
        assert!(
            reason.contains("连接") || reason.contains("网络") || reason.contains("传输"),
            "expected a transport-layer sentence, got {reason:?}"
        );
    }

    #[test]
    fn argument_misuse_returns_minus_one_and_writes_nothing() {
        let endpoint = CString::new("http://127.0.0.1:1").unwrap_or_default();
        let invalid_utf8 = [0xff_u8, 0xfe, 0x00];

        // Null `out_json`.
        // SAFETY: the endpoint is a valid C string; the output pointer is null by
        // contract misuse and must be refused.
        let code = unsafe { vd_fim_detect(endpoint.as_ptr(), ptr::null(), 100, ptr::null_mut()) };
        assert_eq!(code, -1);

        // Null endpoint, `out_json` untouched.
        let mut out: *mut std::ffi::c_char = ptr::null_mut();
        // SAFETY: the output pointer is writable and the endpoint is null.
        let code = unsafe { vd_fim_detect(ptr::null(), ptr::null(), 100, &mut out) };
        assert_eq!(code, -1);
        assert!(out.is_null(), "a refused call must not write *out_json");

        // Non-UTF-8 endpoint.
        let mut out: *mut std::ffi::c_char = ptr::null_mut();
        // SAFETY: the byte slice is NUL-terminated and `out` is writable.
        let code = unsafe {
            vd_fim_detect(
                invalid_utf8.as_ptr() as *const std::ffi::c_char,
                ptr::null(),
                100,
                &mut out,
            )
        };
        assert_eq!(code, -1);
        assert!(out.is_null());

        // Non-UTF-8 key, same refusal.
        let mut out: *mut std::ffi::c_char = ptr::null_mut();
        // SAFETY: same as above for both pointers.
        let code = unsafe {
            vd_fim_detect(
                endpoint.as_ptr(),
                invalid_utf8.as_ptr() as *const std::ffi::c_char,
                100,
                &mut out,
            )
        };
        assert_eq!(code, -1);
        assert!(out.is_null());
    }

    #[test]
    fn an_empty_endpoint_is_a_value_not_an_abi_error() {
        // Contract §5.1: an unusable endpoint is reported in the JSON, so the UI
        // keeps one parse path. No request is sent, so this needs no server.
        let (code, value) = probe("   ", Some(""), 100);
        assert_eq!(code, 0);
        let value = value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(value.get("ok"), Some(&Value::Bool(false)));
        assert_eq!(value.get("reason"), Some(&json!("FIM 端点无效")));
        assert_eq!(value.get("models_url"), Some(&Value::Null));
    }

    #[test]
    fn a_null_key_resolves_the_owner_only_file_next_to_the_store() {
        let _guard = crate::fim::BINDING_LOCK
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let directory = directory("key-file");
        let store = directory.join("settings.toml");
        if let Err(error) = std::fs::write(&store, "theme = \"dark\"\n") {
            panic!("cannot write the store: {error}");
        }
        let key_file = directory.join("fim_api_key");
        if let Err(error) = std::fs::write(&key_file, "sk-from-the-key-file\n") {
            panic!("cannot write the key file: {error}");
        }
        if let Err(error) =
            std::fs::set_permissions(&key_file, std::fs::Permissions::from_mode(0o600))
        {
            panic!("cannot set the key file mode: {error}");
        }
        let store_text = CString::new(store.to_string_lossy().into_owned())
            .unwrap_or_else(|_| panic!("the store path must be a valid C string"));
        // SAFETY: the path is a valid NUL-terminated string.
        let code = unsafe { crate::fim::vd_fim_use_settings_store(store_text.as_ptr()) };
        assert_eq!(code, 0, "binding a readable file must succeed");

        let resolved = resolve_key().map(|key| key.expose_secret().to_string());
        assert_eq!(resolved.as_deref(), Some("sk-from-the-key-file"));

        // Unbind so the process-wide state cannot leak into another test.
        // SAFETY: a null path unbinds by contract.
        let code = unsafe { crate::fim::vd_fim_use_settings_store(ptr::null()) };
        assert_eq!(code, 0);
        let _ = std::fs::remove_dir_all(&directory);
    }

    /// batch-20261114 F1 (written RED first): with a store bound, a key that
    /// lives ONLY in `vedit.toml` must let detect succeed. The completion
    /// request path already reads it as the third level
    /// (`env > 0600 file next to the store > vedit.toml`); detect used to stop
    /// after the 0600 file, so the same project answered 401 while completion
    /// worked.
    #[test]
    fn a_null_key_takes_the_vedit_toml_key_when_the_store_carries_none() {
        const TOML_KEY: &str = "sk-detect-vedit-toml-5150";
        let _guard = crate::fim::BINDING_LOCK
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let directory = directory("vedit-toml-key");
        // The store owns the FIM page but carries no key: the schema has no
        // `fim.api_key` key, so the secret never travels through the store.
        let store = directory.join("settings.toml");
        let store_text = "fim.ui_owned = \"true\"\nfim.model = \"store-model\"\n";
        if let Err(error) = std::fs::write(&store, store_text) {
            panic!("cannot write the store: {error}");
        }
        // `vedit.toml` carries ONLY the key.
        if let Err(error) = std::fs::write(
            directory.join("vedit.toml"),
            format!("[fim]\napi_key = \"{TOML_KEY}\"\n"),
        ) {
            panic!("cannot write vedit.toml: {error}");
        }
        let store_c = CString::new(store.to_string_lossy().into_owned())
            .unwrap_or_else(|_| panic!("the store path must be a valid C string"));
        // SAFETY: the path is a valid NUL-terminated string.
        let code = unsafe { crate::fim::vd_fim_use_settings_store(store_c.as_ptr()) };
        assert_eq!(code, 0, "binding a readable file must succeed");

        // The environment level legitimately wins; this scenario is about the
        // THIRD level, so the variable must not be present while probing.
        let previous_environment = std::env::var(vd_settings::schema::API_KEY_ENV).ok();
        std::env::remove_var(vd_settings::schema::API_KEY_ENV);
        let probe_in_project = |server: &FakeServer| {
            let previous = std::env::current_dir()
                .unwrap_or_else(|error| panic!("cannot read the working directory: {error}"));
            std::env::set_current_dir(&directory)
                .unwrap_or_else(|error| panic!("cannot enter the project directory: {error}"));
            let result = probe(&server.endpoint, None, 2_000);
            std::env::set_current_dir(&previous)
                .unwrap_or_else(|error| panic!("cannot restore the working directory: {error}"));
            result
        };
        let toml_server = FakeServer::requiring_key(r#"{"data":[{"id":"m1"}]}"#, TOML_KEY);
        let (toml_code, toml_value) = probe_in_project(&toml_server);

        // The 0600 file next to the store still OUTRANKS `vedit.toml`.
        const FILE_KEY: &str = "sk-detect-key-file-5151";
        let key_file = directory.join("fim_api_key");
        if let Err(error) = std::fs::write(&key_file, format!("{FILE_KEY}\n")) {
            panic!("cannot write the key file: {error}");
        }
        if let Err(error) =
            std::fs::set_permissions(&key_file, std::fs::Permissions::from_mode(0o600))
        {
            panic!("cannot set the key file mode: {error}");
        }
        let file_server = FakeServer::requiring_key(r#"{"data":[{"id":"m1"}]}"#, FILE_KEY);
        let (file_code, file_value) = probe_in_project(&file_server);

        if let Some(key) = previous_environment {
            std::env::set_var(vd_settings::schema::API_KEY_ENV, key);
        }
        // SAFETY: a null path unbinds by contract.
        let code = unsafe { crate::fim::vd_fim_use_settings_store(ptr::null()) };
        assert_eq!(code, 0);
        let _ = std::fs::remove_dir_all(&directory);

        assert_eq!(toml_code, 0);
        let toml_value = toml_value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(
            toml_value.get("ok"),
            Some(&Value::Bool(true)),
            "a key that lives only in vedit.toml must make detect succeed, got {toml_value}"
        );
        assert_eq!(
            toml_value.get("reason"),
            Some(&Value::Null),
            "no 401 may be reported when the request carried the vedit.toml key"
        );
        assert_eq!(file_code, 0);
        let file_value = file_value.unwrap_or_else(|| panic!("the probe must return JSON"));
        assert_eq!(
            file_value.get("ok"),
            Some(&Value::Bool(true)),
            "the 0600 file next to the store must outrank vedit.toml, got {file_value}"
        );
    }

    #[test]
    fn the_timeout_is_clamped_like_the_completion_settings() {
        assert_eq!(detect_timeout(0), Duration::from_millis(100));
        assert_eq!(detect_timeout(500), Duration::from_millis(500));
        assert_eq!(detect_timeout(u32::MAX), Duration::from_millis(60_000));
    }
}
