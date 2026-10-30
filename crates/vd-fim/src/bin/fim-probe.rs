//! Evidence probe for T19: default-off behaviour, both protocol bodies, status
//! texts, cancellation and thread boundedness.
//!
//! It uses the public API only and the [`vd_fim::RecordingTransport`] test double,
//! so every claim below is observable from outside the crate:
//!
//! ```text
//! cargo run -q -p vd-fim --bin fim-probe
//! ```
//!
//! Exit code `0` when every check passes, `1` otherwise. No socket is opened and
//! no display is needed.

#![forbid(unsafe_code)]

use std::sync::Arc;
use std::time::{Duration, Instant};

use serde_json::json;
use vd_fim::{
    openai_request_body, parse_llama_cpp_response, parse_openai_response, CompletionRequest,
    FimError, FimService, FimSettings, Protocol, RecordingTransport, Transport,
};

fn main() -> std::process::ExitCode {
    let failures = run();
    if failures == 0 {
        println!("PROBE PASS");
        std::process::ExitCode::SUCCESS
    } else {
        println!("PROBE FAIL failures={failures}");
        std::process::ExitCode::from(1)
    }
}

fn check(name: &str, ok: bool, failures: &mut usize) {
    if !ok {
        *failures += 1;
    }
    println!("CHECK {name} {}", if ok { "PASS" } else { "FAIL" });
}

fn thread_count() -> usize {
    std::fs::read_dir("/proc/self/task")
        .map(|entries| entries.filter_map(Result::ok).count())
        .unwrap_or(0)
}

fn wait_for(service: &FimService, budget: Duration) -> Option<Result<String, FimError>> {
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

fn openai_settings() -> FimSettings {
    FimSettings {
        enabled: true,
        protocol: Protocol::OpenAi,
        endpoint: "http://127.0.0.1:8080".to_string(),
        model: "qwen2.5-coder".to_string(),
        ..FimSettings::default()
    }
}

fn run() -> usize {
    let mut failures = 0_usize;

    // 1. Product default: disabled, inert, no worker, no transport call.
    let defaults = FimSettings::default();
    println!(
        "DEFAULTS enabled={} endpoint={:?} model={:?} timeout_ms={} api_key={}",
        defaults.enabled,
        defaults.endpoint,
        defaults.model,
        defaults.timeout_ms,
        if defaults.api_key.is_none() {
            "none"
        } else {
            "present"
        }
    );
    let transport = Arc::new(RecordingTransport::answering("{}"));
    let dynamic: Arc<dyn Transport> = transport.clone();
    let service = FimService::with_transport(defaults.clone(), dynamic);
    println!(
        "DISABLED enabled={} reason={:?} worker={} start={:?} poll={:?}",
        service.enabled(),
        service.unavailable_reason(),
        service.has_worker(),
        service.start("a", "b"),
        service.poll()
    );
    check("default_off", !service.enabled(), &mut failures);
    check(
        "default_reason",
        service.unavailable_reason() == Some("FIM 未启用"),
        &mut failures,
    );
    check("default_no_worker", !service.has_worker(), &mut failures);
    check(
        "default_no_request",
        service.start("a", "b").is_none(),
        &mut failures,
    );
    std::thread::sleep(Duration::from_millis(30));
    check(
        "default_zero_transport_calls",
        transport.calls() == 0,
        &mut failures,
    );

    // 2. Half-configured: enabled but no endpoint.
    let mut half = openai_settings();
    half.endpoint.clear();
    let half_transport = Arc::new(RecordingTransport::answering("{}"));
    let half_dynamic: Arc<dyn Transport> = half_transport.clone();
    let half_service = FimService::with_transport(half, half_dynamic);
    println!(
        "HALF_CONFIGURED enabled={} reason={:?} start={:?}",
        half_service.enabled(),
        half_service.unavailable_reason(),
        half_service.start("a", "b")
    );
    check(
        "half_configured_unavailable",
        !half_service.enabled(),
        &mut failures,
    );
    check(
        "half_configured_reason",
        half_service.unavailable_reason() == Some("FIM 未配置端点"),
        &mut failures,
    );
    std::thread::sleep(Duration::from_millis(30));
    check(
        "half_configured_zero_calls",
        half_transport.calls() == 0,
        &mut failures,
    );

    // 3. Protocol bodies, verbatim.
    let openai_body = openai_request_body("qwen2.5-coder", "assign x", ";\nendmodule", 64);
    println!("OPENAI_BODY {}", openai_body);
    check(
        "openai_body_fields",
        openai_body
            == json!({
                "model": "qwen2.5-coder",
                "prompt": "assign x",
                "suffix": ";\nendmodule",
                "max_tokens": 64,
            }),
        &mut failures,
    );
    let llama_body = vd_fim::llama_cpp_request_body("always_ff @(", ") begin", 32);
    println!("LLAMA_CPP_BODY {}", llama_body);
    check(
        "llama_cpp_body_fields",
        llama_body
            == json!({
                "input_prefix": "always_ff @(",
                "input_suffix": ") begin",
                "n_predict": 32,
            }),
        &mut failures,
    );

    // 4. Response parsing samples.
    let openai_sample = r#"{"choices":[{"text":" <= 1;"}]}"#;
    let llama_sample = r#"{"content":" <= '0;"}"#;
    println!(
        "OPENAI_RESPONSE {openai_sample} -> {:?}",
        parse_openai_response(openai_sample)
    );
    println!(
        "LLAMA_CPP_RESPONSE {llama_sample} -> {:?}",
        parse_llama_cpp_response(llama_sample)
    );
    check(
        "openai_response_parse",
        parse_openai_response(openai_sample).ok().as_deref() == Some(" <= 1;"),
        &mut failures,
    );
    check(
        "llama_cpp_response_parse",
        parse_llama_cpp_response(llama_sample).ok().as_deref() == Some(" <= '0;"),
        &mut failures,
    );

    // 5. A request round trip through the single worker.
    let roundtrip_transport = Arc::new(RecordingTransport::answering(openai_sample));
    let roundtrip_dynamic: Arc<dyn Transport> = roundtrip_transport.clone();
    let roundtrip = FimService::with_transport(openai_settings(), roundtrip_dynamic);
    let generation = roundtrip.start("assign x", ";\n");
    let completion = wait_for(&roundtrip, Duration::from_secs(2));
    let recorded = roundtrip_transport.last_request();
    println!(
        "ROUNDTRIP generation={:?} completion={:?} url={:?} body={:?}",
        generation.map(|generation| generation.value()),
        completion,
        recorded.as_ref().map(|request| request.url.clone()),
        recorded.as_ref().map(|request| request.body.clone())
    );
    check(
        "roundtrip_completion",
        completion == Some(Ok(" <= 1;".to_string())),
        &mut failures,
    );
    check(
        "roundtrip_one_call",
        roundtrip_transport.calls() == 1,
        &mut failures,
    );

    // 6. Failure surfaces as a short status-bar sentence with no secret.
    let failing = Arc::new(RecordingTransport::failing(FimError::HttpStatus {
        status: 401,
    }));
    let failing_dynamic: Arc<dyn Transport> = failing.clone();
    let mut keyed = openai_settings();
    keyed.api_key = Some(vd_fim::ApiKey::new("sk-fim-secret-value"));
    let failing_service = FimService::with_transport(keyed, failing_dynamic);
    let _ = failing_service.start("a", "b");
    let failed = wait_for(&failing_service, Duration::from_secs(2));
    let reason = match &failed {
        Some(Err(error)) => error.reason(),
        _ => String::new(),
    };
    println!("FAILURE result={failed:?} reason={reason:?}");
    check(
        "failure_reason_is_status_bar_ready",
        reason == "FIM HTTP 401",
        &mut failures,
    );
    check(
        "failure_reason_has_no_secret",
        !reason.contains("sk-fim-secret-value"),
        &mut failures,
    );

    // 7. Cancellation drops the late result.
    let cancel_transport = Arc::new(RecordingTransport::answering(openai_sample));
    let cancel_dynamic: Arc<dyn Transport> = cancel_transport.clone();
    let cancel_service = FimService::with_transport(openai_settings(), cancel_dynamic);
    let _ = cancel_service.start("a", "b");
    cancel_service.cancel();
    std::thread::sleep(Duration::from_millis(50));
    println!("CANCELLED poll={:?}", cancel_service.poll());
    check(
        "cancel_drops_result",
        cancel_service.poll().is_none(),
        &mut failures,
    );

    // 8. Two hundred rapid starts: one worker, one-slot mailbox.
    let burst_transport =
        Arc::new(RecordingTransport::answering(openai_sample).with_delay(Duration::from_millis(5)));
    let burst_dynamic: Arc<dyn Transport> = burst_transport.clone();
    let burst_service = FimService::with_transport(openai_settings(), burst_dynamic);
    let before = thread_count();
    for index in 0..200 {
        let prefix = format!("assign value_{index} =");
        let _ = burst_service.start(&prefix, ";");
    }
    let after = thread_count();
    std::thread::sleep(Duration::from_millis(120));
    println!(
        "BURST threads before={before} after={after} transport_calls={}",
        burst_transport.calls()
    );
    check(
        "burst_thread_count_bounded",
        after <= before + 1,
        &mut failures,
    );
    check(
        "burst_requests_coalesced",
        burst_transport.calls() < 40,
        &mut failures,
    );

    // 9. Request building blocks stay protocol-shaped.
    let request = CompletionRequest::new("p", "s");
    check(
        "completion_request_keeps_both_halves",
        request.prefix == "p" && request.suffix == "s",
        &mut failures,
    );

    failures
}
