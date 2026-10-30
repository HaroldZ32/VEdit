//! The service: one worker thread, a single-slot queue, generation-based cancel.
//!
//! ## Default off, zero network, zero threads
//!
//! When the settings are disabled (the product default) or unusable, the service
//! is constructed in the *unavailable* state: no transport is constructed, no
//! backend is built, no thread is spawned. [`FimService::start`] then returns
//! `None` and [`FimService::poll`] returns `None` — there is nothing to send a
//! request, which is a structural guarantee, not a runtime check.
//!
//! ## Cancellation is result-level, and that is deliberate
//!
//! `ureq` exposes no cancellable handle, so an in-flight request cannot be
//! aborted on the wire. Cancellation therefore increments a generation counter:
//! the worker drops a result whose generation is stale, the queue keeps at most
//! one pending request, and the configured timeout reclaims the socket. Typing
//! again can only ever win — a stale completion never overwrites a newer one.
//!
//! ## One worker, never one thread per keystroke
//!
//! A single long-lived worker thread consumes a one-slot mailbox; a new
//! [`FimService::start`] replaces the pending request instead of queueing a
//! thread. Rapid typing therefore cannot grow the thread count or the queue.

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};

use crate::backends::{backend_from_settings, CompletionBackend, CompletionRequest};
use crate::error::FimError;
use crate::settings::FimSettings;
use crate::transport::{HttpTransport, Transport};

static WORKER_SPAWNS: AtomicUsize = AtomicUsize::new(0);

/// How many FIM worker threads this process has ever spawned.
///
/// Exposed so "the disabled path spawns nothing" is a countable fact in tests,
/// independent of unrelated threads the test harness itself creates.
#[must_use]
pub fn worker_spawns() -> usize {
    WORKER_SPAWNS.load(Ordering::SeqCst)
}

/// Identifier of one completion request.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Generation(u64);

impl Generation {
    /// The raw generation counter.
    #[must_use]
    pub fn value(self) -> u64 {
        self.0
    }
}

struct Job {
    generation: u64,
    request: CompletionRequest,
}

struct Shared {
    pending: Mutex<Option<Job>>,
    signal: Condvar,
    latest: AtomicU64,
    outcomes: Mutex<VecDeque<(u64, Result<String, FimError>)>>,
    stopping: AtomicBool,
}

/// The FIM service handed to the UI layer.
pub struct FimService {
    shared: Option<Arc<Shared>>,
    worker: Option<JoinHandle<()>>,
    reason: Option<String>,
}

impl FimService {
    /// Builds a service from the configuration, using the real HTTP transport
    /// when the configuration is usable.
    #[must_use]
    pub fn from_settings(settings: FimSettings) -> Self {
        Self::build(&settings)
    }

    /// Builds a service with an injected transport (tests, verification).
    ///
    /// An unusable configuration still yields the unavailable state: the injected
    /// transport is simply never stored, and cannot be called.
    #[must_use]
    pub fn with_transport(settings: FimSettings, transport: Arc<dyn Transport>) -> Self {
        if let Some(error) = settings.unavailable() {
            return Self::unavailable(error);
        }
        Self::spawn(&settings, transport)
    }

    fn build(settings: &FimSettings) -> Self {
        if let Some(error) = settings.unavailable() {
            return Self::unavailable(error);
        }
        // The transport is constructed only on this path, after validation.
        let transport: Arc<dyn Transport> = Arc::new(HttpTransport::new());
        Self::spawn(settings, transport)
    }

    fn unavailable(error: FimError) -> Self {
        Self {
            shared: None,
            worker: None,
            reason: Some(error.reason()),
        }
    }

    fn spawn(settings: &FimSettings, transport: Arc<dyn Transport>) -> Self {
        let backend = match backend_from_settings(settings, transport) {
            Ok(backend) => backend,
            Err(error) => return Self::unavailable(error),
        };
        let shared = Arc::new(Shared {
            pending: Mutex::new(None),
            signal: Condvar::new(),
            latest: AtomicU64::new(0),
            outcomes: Mutex::new(VecDeque::new()),
            stopping: AtomicBool::new(false),
        });
        let worker_shared = Arc::clone(&shared);
        let worker = thread::Builder::new()
            .name("vedit-fim".to_string())
            .spawn(move || worker_loop(&worker_shared, backend.as_ref()));
        match worker {
            Ok(worker) => {
                WORKER_SPAWNS.fetch_add(1, Ordering::SeqCst);
                Self {
                    shared: Some(shared),
                    worker: Some(worker),
                    reason: None,
                }
            }
            Err(_) => Self::unavailable(FimError::Transport {
                category: crate::error::TransportCategory::Other,
            }),
        }
    }

    /// Whether the service can produce completions at all.
    #[must_use]
    pub fn enabled(&self) -> bool {
        self.shared.is_some()
    }

    /// Why the service cannot produce completions, as a short status-bar sentence.
    ///
    /// Never contains the endpoint, the model name, a header or the key.
    #[must_use]
    pub fn unavailable_reason(&self) -> Option<&str> {
        self.reason.as_deref()
    }

    /// Whether a worker thread was started (always false in the disabled state).
    #[must_use]
    pub fn has_worker(&self) -> bool {
        self.worker.is_some()
    }

    /// Queues a completion request for `(prefix, suffix)`.
    ///
    /// Replaces any request that has not started yet, cancels the previous result
    /// by advancing the generation, and returns `None` when the service is
    /// unavailable.
    pub fn start(&self, prefix: &str, suffix: &str) -> Option<Generation> {
        let shared = self.shared.as_ref()?;
        let generation = shared.latest.fetch_add(1, Ordering::SeqCst) + 1;
        let job = Job {
            generation,
            request: CompletionRequest::new(prefix, suffix),
        };
        *lock(&shared.pending) = Some(job);
        shared.signal.notify_one();
        Some(Generation(generation))
    }

    /// Cancels the pending request and invalidates any in-flight result.
    pub fn cancel(&self) {
        if let Some(shared) = &self.shared {
            shared.latest.fetch_add(1, Ordering::SeqCst);
            lock(&shared.pending).take();
            lock(&shared.outcomes).clear();
        }
    }

    /// Takes the newest result, dropping anything produced for an older request.
    pub fn poll(&self) -> Option<Result<String, FimError>> {
        let shared = self.shared.as_ref()?;
        let latest = shared.latest.load(Ordering::SeqCst);
        let mut outcomes = lock(&shared.outcomes);
        while let Some((generation, result)) = outcomes.pop_front() {
            if generation == latest {
                return Some(result);
            }
        }
        None
    }
}

impl Drop for FimService {
    fn drop(&mut self) {
        if let Some(shared) = &self.shared {
            shared.stopping.store(true, Ordering::SeqCst);
            lock(&shared.pending).take();
            shared.signal.notify_all();
        }
        // The worker is detached rather than joined: an in-flight request is
        // bounded by the timeout, its result is discarded, and dropping the UI
        // must not block on the network.
        self.worker.take();
    }
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|poison| poison.into_inner())
}

fn next_job(shared: &Shared) -> Option<Job> {
    let mut slot = lock(&shared.pending);
    loop {
        if shared.stopping.load(Ordering::SeqCst) {
            return None;
        }
        if let Some(job) = slot.take() {
            return Some(job);
        }
        slot = shared
            .signal
            .wait(slot)
            .unwrap_or_else(|poison| poison.into_inner());
    }
}

fn worker_loop(shared: &Shared, backend: &dyn CompletionBackend) {
    while let Some(job) = next_job(shared) {
        // A newer keystroke already superseded this request: do not spend a call.
        if job.generation < shared.latest.load(Ordering::SeqCst) {
            continue;
        }
        let result = backend.complete(&job.request);
        if job.generation == shared.latest.load(Ordering::SeqCst) {
            lock(&shared.outcomes).push_back((job.generation, result));
        }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    use serde_json::json;

    use super::FimService;

    /// Serialises the tests that observe the process-wide spawn counter.
    static COUNTER_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    fn counter_guard() -> std::sync::MutexGuard<'static, ()> {
        COUNTER_LOCK
            .lock()
            .unwrap_or_else(|poison| poison.into_inner())
    }
    use crate::error::FimError;
    use crate::secret::ApiKey;
    use crate::settings::{FimSettings, Protocol};
    use crate::testing::RecordingTransport;
    use crate::transport::Transport;

    fn openai_settings() -> FimSettings {
        FimSettings {
            enabled: true,
            protocol: Protocol::OpenAi,
            endpoint: "http://127.0.0.1:8080".to_string(),
            model: "qwen2.5-coder".to_string(),
            ..FimSettings::default()
        }
    }

    fn wait_for_result(service: &FimService, budget: Duration) -> Option<Result<String, FimError>> {
        let started = Instant::now();
        loop {
            if let Some(result) = service.poll() {
                return Some(result);
            }
            if started.elapsed() >= budget {
                return None;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
    }

    #[test]
    fn disabled_service_is_inert_and_never_touches_a_transport() {
        let settings = FimSettings::disabled();
        let transport = Arc::new(RecordingTransport::answering("{}"));
        let dynamic: Arc<dyn Transport> = transport.clone();
        let service = FimService::with_transport(settings, dynamic);

        assert!(!service.enabled());
        assert_eq!(service.unavailable_reason(), Some("FIM 未启用"));
        assert!(!service.has_worker(), "no worker thread may be spawned");
        assert_eq!(service.start("a", "b"), None);
        assert_eq!(service.poll(), None);
        service.cancel();
        std::thread::sleep(Duration::from_millis(30));
        assert_eq!(transport.calls(), 0, "the transport must never be called");
    }

    #[test]
    fn half_configured_service_reports_without_requesting() {
        let mut settings = openai_settings();
        settings.endpoint.clear();
        let transport = Arc::new(RecordingTransport::answering("{}"));
        let dynamic: Arc<dyn Transport> = transport.clone();
        let service = FimService::with_transport(settings, dynamic);

        assert!(!service.enabled());
        assert_eq!(service.unavailable_reason(), Some("FIM 未配置端点"));
        assert!(!service.has_worker());
        assert_eq!(service.start("a", "b"), None);
        std::thread::sleep(Duration::from_millis(30));
        assert_eq!(transport.calls(), 0);
    }

    #[test]
    fn from_settings_does_not_spawn_anything_while_disabled() {
        let _serial = counter_guard();
        let before = super::worker_spawns();
        let service = FimService::from_settings(FimSettings::disabled());
        assert!(!service.has_worker());
        assert_eq!(service.start("x", "y"), None);
        assert_eq!(
            super::worker_spawns(),
            before,
            "the disabled path must not spawn a worker"
        );
    }

    #[test]
    fn a_completion_round_trips_through_one_worker() {
        let _serial = counter_guard();
        let transport = Arc::new(RecordingTransport::answering(
            r#"{"choices":[{"text":" <= 1;"}]}"#,
        ));
        let dynamic: Arc<dyn Transport> = transport.clone();
        let service = FimService::with_transport(openai_settings(), dynamic);
        assert!(service.enabled());
        assert!(service.has_worker());

        let generation = service.start("assign x", ";\n");
        assert!(generation.is_some());
        let result = wait_for_result(&service, Duration::from_secs(2));
        assert_eq!(result, Some(Ok(" <= 1;".to_string())));
        assert_eq!(transport.calls(), 1);
        let request = transport
            .last_request()
            .unwrap_or_else(|| panic!("no request"));
        assert_eq!(
            request.body,
            json!({
                "model": "qwen2.5-coder",
                "prompt": "assign x",
                "suffix": ";\n",
                "max_tokens": 64,
            })
        );
    }

    #[test]
    fn cancelling_drops_any_late_result() {
        let _serial = counter_guard();
        let transport = Arc::new(RecordingTransport::answering(
            r#"{"choices":[{"text":"stale"}]}"#,
        ));
        let dynamic: Arc<dyn Transport> = transport.clone();
        let service = FimService::with_transport(openai_settings(), dynamic);
        assert!(service.start("a", "b").is_some());
        service.cancel();
        std::thread::sleep(Duration::from_millis(50));
        assert_eq!(
            service.poll(),
            None,
            "a cancelled request must never surface"
        );
    }

    #[test]
    fn rapid_typing_does_not_spawn_more_workers_or_queue_requests() {
        let _serial = counter_guard();
        let transport = Arc::new(
            RecordingTransport::answering(r#"{"choices":[{"text":"x"}]}"#)
                .with_delay(Duration::from_millis(5)),
        );
        let dynamic: Arc<dyn Transport> = transport.clone();
        let service = FimService::with_transport(openai_settings(), dynamic);
        let spawns_before = super::worker_spawns();
        for index in 0..200 {
            let prefix = format!("assign x{index} =");
            let _ = service.start(&prefix, ";");
        }
        assert_eq!(
            super::worker_spawns(),
            spawns_before,
            "rapid starts must not spawn threads"
        );
        // The mailbox holds one pending request, so the 200 keystrokes coalesce:
        // with a 5 ms transport, 120 ms admits at most ~25 calls, never 200.
        std::thread::sleep(Duration::from_millis(120));
        let calls = transport.calls();
        assert!(
            calls < 40,
            "requests were queued instead of replacing each other: {calls}"
        );
    }

    #[test]
    fn a_failed_request_reaches_the_ui_as_a_short_reason() {
        let _serial = counter_guard();
        let transport = Arc::new(RecordingTransport::failing(FimError::HttpStatus {
            status: 401,
        }));
        let dynamic: Arc<dyn Transport> = transport.clone();
        let mut settings = openai_settings();
        settings.api_key = Some(ApiKey::new("sk-secret-value"));
        let service = FimService::with_transport(settings, dynamic);
        assert!(service.start("a", "b").is_some());
        let result = wait_for_result(&service, Duration::from_secs(2));
        match result {
            Some(Err(error)) => {
                assert_eq!(error.reason(), "FIM HTTP 401");
                assert!(!error.reason().contains("sk-secret-value"));
            }
            other => panic!("expected a failure, got {other:?}"),
        }
    }

    #[test]
    fn unavailable_reason_never_contains_configuration_detail() {
        let mut settings = openai_settings();
        settings.endpoint = "ftp://internal.example/?api_key=leak".to_string();
        let service = FimService::with_transport(settings, Arc::new(RecordingTransport::default()));
        let reason = service.unavailable_reason().unwrap_or_default().to_string();
        assert!(!reason.contains("leak"), "{reason}");
        assert!(!reason.contains("internal.example"), "{reason}");
    }
}
