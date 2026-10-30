//! Public-API integration test for the product default (T19, AC-13).
//!
//! FIM must be off by default, send nothing, and start no worker. This test only
//! uses the crate's public surface, so the verification lane can reproduce it
//! with `cargo test -p vd-fim --test default_off`.

use std::sync::Arc;
use std::time::Duration;

use vd_fim::{worker_spawns, FimService, FimSettings, Protocol, RecordingTransport, Transport};

/// The spawn counter is process-wide, so the tests that observe it run serially.
static SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn serial() -> std::sync::MutexGuard<'static, ()> {
    SERIAL.lock().unwrap_or_else(|poison| poison.into_inner())
}

#[test]
fn default_settings_send_nothing_and_start_no_worker() {
    let _serial = serial();
    let spawns_before = worker_spawns();
    let transport = Arc::new(RecordingTransport::answering("{}"));
    let dynamic: Arc<dyn Transport> = transport.clone();

    let service = FimService::with_transport(FimSettings::default(), dynamic);
    assert!(!service.enabled());
    assert_eq!(service.unavailable_reason(), Some("FIM 未启用"));
    assert!(!service.has_worker());
    assert_eq!(service.start("assign x", ";"), None);
    assert_eq!(service.poll(), None);
    service.cancel();

    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(
        transport.calls(),
        0,
        "disabled FIM must not touch the network"
    );
    assert_eq!(
        worker_spawns(),
        spawns_before,
        "disabled FIM must not spawn a worker"
    );
}

#[test]
fn the_real_transport_is_not_constructed_while_disabled() {
    let _serial = serial();
    // `from_settings` is the production entry point: with the default settings it
    // returns the unavailable state without ever building an HTTP transport.
    let spawns_before = worker_spawns();
    let service = FimService::from_settings(FimSettings::default());
    assert!(!service.enabled());
    assert!(!service.has_worker());
    assert_eq!(service.start("a", "b"), None);
    assert_eq!(worker_spawns(), spawns_before);
}

#[test]
fn openai_without_a_model_is_reported_without_a_request() {
    let _serial = serial();
    let settings = FimSettings {
        enabled: true,
        protocol: Protocol::OpenAi,
        endpoint: "http://127.0.0.1:1".to_string(),
        model: String::new(),
        ..FimSettings::default()
    };
    let spawns_before = worker_spawns();
    let transport = Arc::new(RecordingTransport::answering("{}"));
    let dynamic: Arc<dyn Transport> = transport.clone();
    let service = FimService::with_transport(settings, dynamic);

    assert!(!service.enabled());
    assert_eq!(service.unavailable_reason(), Some("FIM 未配置模型"));
    assert_eq!(service.start("a", "b"), None);
    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(transport.calls(), 0);
    assert_eq!(worker_spawns(), spawns_before);
}

#[test]
fn llama_cpp_needs_no_model_but_sends_nothing_until_started() {
    let _serial = serial();
    let settings = FimSettings {
        enabled: true,
        protocol: Protocol::LlamaCpp,
        endpoint: "http://127.0.0.1:1".to_string(),
        model: String::new(),
        ..FimSettings::default()
    };
    let transport = Arc::new(RecordingTransport::answering(r#"{"content":"x"}"#));
    let dynamic: Arc<dyn Transport> = transport.clone();
    let service = FimService::with_transport(settings, dynamic);
    assert!(service.enabled(), "llama.cpp has no model field");
    assert_eq!(transport.calls(), 0, "nothing may be sent before a start()");
}
