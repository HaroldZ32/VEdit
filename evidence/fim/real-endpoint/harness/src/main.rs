//! Evidence-only harness: drives the real `vd-fim` `HttpTransport` against a
//! local path-rewriting proxy (see ../proxy.py) that forwards to the real
//! DeepSeek FIM endpoint. No product code is modified or rebuilt.

use std::sync::Arc;
use std::time::{Duration, Instant};

use vd_fim::{FimError, FimService, FimSettings, HttpTransport, Protocol, MAX_REASON_CHARS};

fn settings(endpoint: &str, model: &str) -> FimSettings {
    FimSettings {
        enabled: true,
        protocol: Protocol::OpenAi,
        endpoint: endpoint.to_string(),
        model: model.to_string(),
        timeout_ms: 30_000,
        max_tokens: 24,
        api_key: None, // the proxy injects the Authorization header
    }
}

fn poll_until(service: &FimService, limit: Duration) -> Option<Result<String, FimError>> {
    let start = Instant::now();
    while start.elapsed() < limit {
        if let Some(result) = service.poll() {
            return Some(result);
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    None
}

fn escape(text: &str) -> String {
    text.replace('\\', "\\\\").replace('"', "\\\"")
}

fn main() {
    let mode = std::env::args().nth(1).unwrap_or_else(|| "positive".to_string());
    let endpoint = std::env::var("FIM_BASE").unwrap_or_else(|_| "http://127.0.0.1:1".to_string());
    let model = std::env::var("FIM_MODEL").unwrap_or_else(|_| "deepseek-flash".to_string());
    let service = FimService::with_transport(settings(&endpoint, &model), Arc::new(HttpTransport::new()));

    match mode.as_str() {
        "positive" => {
            let started = Instant::now();
            let Some(_generation) = service.start(
                "module top;\n  logic a;\n  assign a = ",
                ";\nendmodule\n",
            ) else {
                println!("RESULT {{\"mode\":\"positive\",\"ok\":false,\"error\":\"start returned None\"}}");
                std::process::exit(1);
            };
            match poll_until(&service, Duration::from_secs(45)) {
                Some(Ok(text)) => {
                    let elapsed = started.elapsed().as_millis();
                    let preview: String = text.chars().take(40).collect();
                    println!(
                        "RESULT {{\"mode\":\"positive\",\"ok\":true,\"text_len\":{},\"latency_ms\":{},\"preview\":\"{}\",\"model\":\"{}\"}}",
                        text.chars().count(),
                        elapsed,
                        escape(&preview),
                        escape(&model)
                    );
                }
                Some(Err(error)) => {
                    println!(
                        "RESULT {{\"mode\":\"positive\",\"ok\":false,\"error\":\"{}\"}}",
                        escape(&error.reason())
                    );
                    std::process::exit(1);
                }
                None => {
                    println!("RESULT {{\"mode\":\"positive\",\"ok\":false,\"error\":\"timeout before reply\"}}");
                    std::process::exit(1);
                }
            }
        }
        "cancel" => {
            let Some(_first) = service.start(
                "module slow_cancel;\n  logic a;\n  assign a = ",
                ";\nendmodule\n",
            ) else {
                println!("RESULT {{\"mode\":\"cancel\",\"ok\":false,\"error\":\"start returned None\"}}");
                std::process::exit(1);
            };
            service.cancel();
            let mut stale_delivered = false;
            let start = Instant::now();
            while start.elapsed() < Duration::from_secs(5) {
                if service.poll().is_some() {
                    stale_delivered = true;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            let Some(_second) = service.start(
                "module fresh_after_cancel;\n  logic b;\n  assign b = ",
                ";\nendmodule\n",
            ) else {
                println!("RESULT {{\"mode\":\"cancel\",\"ok\":false,\"error\":\"second start returned None\"}}");
                std::process::exit(1);
            };
            let fresh = poll_until(&service, Duration::from_secs(45));
            let (fresh_ok, fresh_len) = match fresh {
                Some(Ok(text)) => (true, text.chars().count()),
                _ => (false, 0),
            };
            println!(
                "RESULT {{\"mode\":\"cancel\",\"ok\":{},\"stale_delivered\":{},\"fresh_ok\":{},\"fresh_text_len\":{}}}",
                !stale_delivered && fresh_ok,
                stale_delivered,
                fresh_ok,
                fresh_len
            );
            std::process::exit(if !stale_delivered && fresh_ok { 0 } else { 1 });
        }
        "degraded" => {
            let started = Instant::now();
            let Some(_generation) = service.start(
                "module dead_endpoint;\n  logic a;\n  assign a = ",
                ";\nendmodule\n",
            ) else {
                println!("RESULT {{\"mode\":\"degraded\",\"ok\":false,\"error\":\"start returned None\"}}");
                std::process::exit(1);
            };
            match poll_until(&service, Duration::from_secs(15)) {
                Some(Err(error)) => {
                    let reason = error.reason();
                    let length = reason.chars().count();
                    let leaked = reason.contains("http")
                        || reason.contains("Bearer")
                        || reason.contains("Authorization");
                    println!(
                        "RESULT {{\"mode\":\"degraded\",\"ok\":{},\"reason_len\":{},\"max_reason_chars\":{},\"contains_url_or_header\":{},\"latency_ms\":{},\"reason\":\"{}\"}}",
                        length <= MAX_REASON_CHARS && !leaked,
                        length,
                        MAX_REASON_CHARS,
                        leaked,
                        started.elapsed().as_millis(),
                        escape(&reason)
                    );
                    std::process::exit(if length <= MAX_REASON_CHARS && !leaked { 0 } else { 1 });
                }
                Some(Ok(_)) => {
                    println!("RESULT {{\"mode\":\"degraded\",\"ok\":false,\"error\":\"unexpected success\"}}");
                    std::process::exit(1);
                }
                None => {
                    println!("RESULT {{\"mode\":\"degraded\",\"ok\":false,\"error\":\"no failure within 15s\"}}");
                    std::process::exit(1);
                }
            }
        }
        other => {
            println!("RESULT {{\"mode\":\"{other}\",\"ok\":false,\"error\":\"unknown mode\"}}");
            std::process::exit(2);
        }
    }
}
