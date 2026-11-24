//! Endpoint detection: normalise a user-typed endpoint, infer the provider and
//! probe which models it serves.
//!
//! Frozen contract §5 defines every branch implemented here. Three properties
//! matter for the product:
//!
//! * **GET only** — detection never sends a completion request, so it spends no
//!   tokens and can never insert text into the editor.
//! * **Manual** — nothing here runs on a timer or at startup; the settings page's
//!   detect button is the only caller (FIM keeps its "off by default, zero
//!   network" guarantee until the user asks).
//! * **No JSON** — the report is a typed value. Turning it into the C ABI's JSON
//!   object is the `vd-ffi` layer's job, which keeps the algorithm unit-testable
//!   against [`RecordingTransport`](crate::testing::RecordingTransport).
//!
//! The key discipline holds here too: a key only becomes an `Authorization`
//! header (through the shared [`bearer_header`] rule), never a report field, a
//! URL or a reason sentence.

use std::time::{Duration, Instant};

use serde_json::Value;

use crate::backends::bearer_header;
use crate::error::FimError;
use crate::protocol::MAX_RESPONSE_BYTES;
use crate::secret::ApiKey;
use crate::settings::{valid_endpoint, Protocol};
use crate::transport::{HttpTransport, Transport};

/// Completion-URL suffixes a user may already have typed (contract §5.1 step 3).
///
/// Ordered longest-first by construction: the matcher picks the longest suffix
/// that fits, so `/beta/completions` wins over `/completions`.
const KNOWN_COMPLETION_SUFFIXES: [&str; 5] = [
    "/v1/chat/completions",
    "/v1/completions",
    "/beta/completions",
    "/completions",
    "/infill",
];

/// The service a normalised endpoint belongs to (contract §5.2).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Provider {
    /// `api.deepseek.com` or `*.deepseek.com`.
    DeepSeek,
    /// `api.openai.com` or `*.openai.com`.
    OpenAi,
    /// Port 11434, or a host starting with `ollama`.
    Ollama,
    /// An endpoint whose typed suffix is `/infill`.
    LlamaCpp,
    /// Anything else.
    Custom,
}

impl Provider {
    /// The provider's name in the frozen JSON vocabulary (contract §4).
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::DeepSeek => "deepseek",
            Self::OpenAi => "openai",
            Self::Ollama => "ollama",
            Self::LlamaCpp => "llama_cpp",
            Self::Custom => "custom",
        }
    }
}

/// The outcome of one endpoint detection.
///
/// `models` is empty both when the probe failed and when the endpoint answered
/// 2xx without listing any model, so [`Self::ok`] — not `models.is_empty()` — is
/// what tells the two apart.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DetectReport {
    /// Inferred provider.
    pub provider: Provider,
    /// The endpoint after normalisation (contract §5.1 steps 1-4).
    pub base_url: String,
    /// The URL a completion would be sent to (contract §5.3).
    pub completion_url: String,
    /// The protocol matching [`Self::completion_url`].
    pub protocol: Protocol,
    /// Model names the endpoint listed, in the order it listed them.
    pub models: Vec<String>,
    /// The model URL that answered 2xx, if one did.
    pub models_url: Option<String>,
    /// Wall-clock milliseconds of the whole detection.
    pub latency_ms: u64,
    /// Why the probe failed; `None` means at least one candidate answered 2xx.
    pub failure: Option<FimError>,
}

impl DetectReport {
    /// Whether the endpoint is reachable and authorised.
    #[must_use]
    pub fn ok(&self) -> bool {
        self.failure.is_none()
    }

    /// The short status-bar sentence, `None` on success (contract §4's `reason`).
    #[must_use]
    pub fn reason(&self) -> Option<String> {
        self.failure.as_ref().map(FimError::reason)
    }
}

/// Probes `endpoint` over `transport`, normalising it first.
///
/// An unusable endpoint fails with [`FimError::InvalidEndpoint`] and sends no
/// request at all (contract §5.1 step 1). Otherwise every candidate model URL is
/// tried in order and the first 2xx wins (contract §5.4).
#[must_use]
pub fn detect(
    transport: &dyn Transport,
    endpoint: &str,
    api_key: Option<&ApiKey>,
    timeout: Duration,
) -> DetectReport {
    let started = Instant::now();
    let normalised = normalise(endpoint);
    let provider = infer_provider(endpoint, normalised.pinned);
    let protocol = protocol_for(provider, normalised.pinned);
    let base = normalised.base;

    if !valid_endpoint(endpoint) {
        // Nothing usable to append to: the report carries the normalised prefix
        // so the UI can show what was typed, and the failure tells it not to
        // write it back (contract §4).
        return DetectReport {
            provider,
            base_url: base.clone(),
            completion_url: base,
            protocol,
            models: Vec::new(),
            models_url: None,
            latency_ms: elapsed_ms(started),
            failure: Some(FimError::InvalidEndpoint),
        };
    }

    let completion_url = completion_url(provider, &base, normalised.pinned);
    let owned_headers: Vec<(&'static str, String)> = bearer_header(api_key).into_iter().collect();
    let headers: Vec<(&str, &str)> = owned_headers
        .iter()
        .map(|(name, value)| (*name, value.as_str()))
        .collect();

    let mut failures: Vec<FimError> = Vec::new();
    let mut models: Vec<String> = Vec::new();
    let mut models_url: Option<String> = None;
    for candidate in models_candidates(provider, &models_base(provider, &base)) {
        match transport.get(&candidate, &headers, timeout, MAX_RESPONSE_BYTES) {
            Ok(body) => {
                models = parse_models(&body);
                models_url = Some(candidate);
                break;
            }
            Err(error) => failures.push(error),
        }
    }

    let failure = if models_url.is_some() {
        None
    } else {
        Some(failure_reason(&failures))
    };

    DetectReport {
        provider,
        base_url: base,
        completion_url,
        protocol,
        models,
        models_url,
        latency_ms: elapsed_ms(started),
        failure,
    }
}

/// The network entry point: [`detect`] over the real [`HttpTransport`].
#[must_use]
pub fn detect_http(endpoint: &str, api_key: Option<&ApiKey>, timeout: Duration) -> DetectReport {
    detect(&HttpTransport::new(), endpoint, api_key, timeout)
}

/// The endpoint after contract §5.1 normalisation.
#[derive(Debug, Clone, PartialEq, Eq)]
struct NormalisedEndpoint {
    /// The trimmed endpoint, without a trailing `/` and without a known
    /// completion suffix — contract §5.1 step 4.
    base: String,
    /// The completion suffix the user already typed, if any.
    pinned: Option<&'static str>,
}

/// Contract §5.1 steps 1-4 (validation itself is the caller's check).
fn normalise(endpoint: &str) -> NormalisedEndpoint {
    let trimmed = endpoint.trim().trim_end_matches('/');
    let pinned = KNOWN_COMPLETION_SUFFIXES
        .iter()
        .copied()
        .filter(|suffix| trimmed.ends_with(suffix))
        .max_by_key(|suffix| suffix.len());
    let base = match pinned {
        Some(suffix) => trimmed.strip_suffix(suffix).unwrap_or(trimmed),
        None => trimmed,
    };
    NormalisedEndpoint {
        base: base.to_string(),
        pinned,
    }
}

/// Contract §5.2: the first matching rule wins.
fn infer_provider(endpoint: &str, pinned: Option<&str>) -> Provider {
    let Some((host, port)) = authority(endpoint) else {
        return Provider::Custom;
    };
    if host == "api.deepseek.com" || host.ends_with(".deepseek.com") {
        return Provider::DeepSeek;
    }
    if host == "api.openai.com" || host.ends_with(".openai.com") {
        return Provider::OpenAi;
    }
    if port == Some(11434) || host.starts_with("ollama") {
        return Provider::Ollama;
    }
    if pinned == Some("/infill") {
        return Provider::LlamaCpp;
    }
    Provider::Custom
}

/// Lower-cased host and its port, for an `http(s)://` endpoint.
///
/// A bracketed IPv6 literal is not split further: it can never match a provider
/// host, and the only port rule (11434) is a plain decimal.
fn authority(endpoint: &str) -> Option<(String, Option<u16>)> {
    let trimmed = endpoint.trim();
    let rest = trimmed
        .strip_prefix("https://")
        .or_else(|| trimmed.strip_prefix("http://"))?;
    let host_port = rest.split('/').next().unwrap_or_default();
    let (host, port) = match host_port.rsplit_once(':') {
        Some((host, port)) => (host, port.parse::<u16>().ok()),
        None => (host_port, None),
    };
    Some((host.to_ascii_lowercase(), port))
}

/// Contract §5.3: the protocol implied by a provider and a typed suffix.
fn protocol_for(provider: Provider, pinned: Option<&str>) -> Protocol {
    match provider {
        Provider::DeepSeek | Provider::OpenAi | Provider::Ollama => Protocol::OpenAi,
        Provider::LlamaCpp => Protocol::LlamaCpp,
        Provider::Custom => match pinned {
            Some("/infill") => Protocol::LlamaCpp,
            _ => Protocol::OpenAi,
        },
    }
}

/// Contract §5.3: the completion URL for a normalised base.
fn completion_url(provider: Provider, base: &str, pinned: Option<&'static str>) -> String {
    match provider {
        Provider::DeepSeek => {
            if base.ends_with("/beta") {
                format!("{base}/completions")
            } else {
                format!("{base}/beta/completions")
            }
        }
        Provider::OpenAi | Provider::Ollama => format!("{base}/v1/completions"),
        Provider::LlamaCpp => {
            if base.ends_with("/infill") {
                base.to_string()
            } else {
                format!("{base}/infill")
            }
        }
        Provider::Custom => match pinned {
            Some(suffix) => format!("{base}{suffix}"),
            None => format!("{base}/v1/completions"),
        },
    }
}

/// Contract §5.4: the base the model endpoints are derived from.
fn models_base(provider: Provider, base: &str) -> String {
    match provider {
        Provider::DeepSeek => base.strip_suffix("/beta").unwrap_or(base).to_string(),
        Provider::OpenAi | Provider::Ollama | Provider::LlamaCpp | Provider::Custom => {
            base.to_string()
        }
    }
}

/// Contract §5.4: candidate model URLs, in the order they are tried.
fn models_candidates(provider: Provider, models_base: &str) -> Vec<String> {
    match provider {
        Provider::DeepSeek => vec![
            format!("{models_base}/models"),
            format!("{models_base}/v1/models"),
        ],
        Provider::OpenAi | Provider::Custom => vec![
            format!("{models_base}/v1/models"),
            format!("{models_base}/models"),
        ],
        Provider::Ollama => vec![
            format!("{models_base}/api/tags"),
            format!("{models_base}/v1/models"),
        ],
        Provider::LlamaCpp => vec![
            format!("{models_base}/props"),
            format!("{models_base}/v1/models"),
        ],
    }
}

/// Contract §5.5: the first shape that yields a non-empty list wins, deduplicated,
/// order preserved, empty strings dropped.
fn parse_models(body: &str) -> Vec<String> {
    let Ok(value) = serde_json::from_str::<Value>(body) else {
        return Vec::new();
    };
    for list in model_lists(&value) {
        let models = dedupe_non_empty(list);
        if !models.is_empty() {
            return models;
        }
    }
    Vec::new()
}

/// Contract §5.5's six shapes, in order.
fn model_lists(value: &Value) -> Vec<Vec<String>> {
    let entries = |key: &str| value.get(key).and_then(Value::as_array);
    let field = |key: &str, name: &str| {
        entries(key)
            .into_iter()
            .flatten()
            .filter_map(|entry| entry.get(name))
            .filter_map(Value::as_str)
            .map(str::to_string)
            .collect::<Vec<String>>()
    };
    let nested = value
        .get("default_generation_settings")
        .and_then(|settings| settings.get("model"))
        .and_then(Value::as_str)
        .map(str::to_string)
        .into_iter()
        .collect();
    let path = value
        .get("model_path")
        .and_then(Value::as_str)
        .map(model_from_path)
        .into_iter()
        .collect();
    vec![
        field("data", "id"),
        field("models", "id"),
        field("models", "name"),
        entries("models")
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .map(str::to_string)
            .collect(),
        nested,
        path,
    ]
}

/// The model name a llama.cpp `model_path` implies: basename without `.gguf`.
fn model_from_path(path: &str) -> String {
    let name = path.rsplit(['/', '\\']).next().unwrap_or(path);
    name.strip_suffix(".gguf").unwrap_or(name).to_string()
}

fn dedupe_non_empty(models: Vec<String>) -> Vec<String> {
    let mut unique: Vec<String> = Vec::with_capacity(models.len());
    for model in models {
        let model = model.trim().to_string();
        if model.is_empty() || unique.contains(&model) {
            continue;
        }
        unique.push(model);
    }
    unique
}

/// Contract §5.6: which failure sentence a fully failed probe reports.
fn failure_reason(failures: &[FimError]) -> FimError {
    if let Some(error) = failures
        .iter()
        .find(|error| matches!(error, FimError::HttpStatus { status: 401 | 403 }))
    {
        return error.clone();
    }
    if let Some(error) = failures.iter().find(|error| is_transport_layer(error)) {
        return error.clone();
    }
    if let Some(error) = failures
        .iter()
        .rev()
        .find(|error| matches!(error, FimError::HttpStatus { .. }))
    {
        return error.clone();
    }
    if let Some(error) = failures.last() {
        // Neither a status code nor a transport failure (an oversized body, say):
        // report it instead of claiming the endpoint is unconfigured.
        return error.clone();
    }
    FimError::EndpointMissing
}

/// Whether the request never produced a status code.
fn is_transport_layer(error: &FimError) -> bool {
    matches!(
        error,
        FimError::Timeout { .. } | FimError::Transport { .. } | FimError::InvalidEndpoint
    )
}

fn elapsed_ms(started: Instant) -> u64 {
    u64::try_from(started.elapsed().as_millis()).unwrap_or(u64::MAX)
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::{
        completion_url, detect, detect_http, failure_reason, infer_provider, model_lists,
        models_base, models_candidates, normalise, parse_models, protocol_for, Provider,
    };
    use crate::error::{FimError, TransportCategory};
    use crate::secret::ApiKey;
    use crate::settings::Protocol;
    use crate::testing::RecordingTransport;

    const TIME: Duration = Duration::from_millis(500);

    /// Convenience for comparing a `Vec<String>` against literals.
    fn strings(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| (*item).to_string()).collect()
    }

    fn candidates(provider: Provider, base: &str) -> Vec<String> {
        models_candidates(provider, &models_base(provider, base))
    }

    #[test]
    fn provider_names_match_the_frozen_json_vocabulary() {
        assert_eq!(Provider::DeepSeek.as_str(), "deepseek");
        assert_eq!(Provider::OpenAi.as_str(), "openai");
        assert_eq!(Provider::Ollama.as_str(), "ollama");
        assert_eq!(Provider::LlamaCpp.as_str(), "llama_cpp");
        assert_eq!(Provider::Custom.as_str(), "custom");
    }

    // ---- §5.1 normalisation -------------------------------------------------

    #[test]
    fn trailing_slashes_and_known_completion_suffixes_are_normalised_away() {
        // `/beta/completions` is 17 bytes and beats `/completions` (12 bytes), so
        // the whole typed suffix — `/beta` included — is what gets stripped; §5.3
        // puts the beta path back when it builds the completion URL.
        let normalised = normalise("  https://api.deepseek.com/beta/completions/  ");
        assert_eq!(normalised.base, "https://api.deepseek.com");
        assert_eq!(normalised.pinned, Some("/beta/completions"));

        let normalised = normalise("https://api.openai.com/v1/completions");
        assert_eq!(normalised.base, "https://api.openai.com");
        assert_eq!(normalised.pinned, Some("/v1/completions"));

        let normalised = normalise("http://h:8080");
        assert_eq!(normalised.base, "http://h:8080");
        assert_eq!(normalised.pinned, None);

        // An unsupported path is part of the base, not a pinned suffix.
        let normalised = normalise("http://h:8080/api");
        assert_eq!(normalised.base, "http://h:8080/api");
        assert_eq!(normalised.pinned, None);
    }

    #[test]
    fn the_longest_known_completion_suffix_wins() {
        let normalised = normalise("http://h/v1/chat/completions");
        assert_eq!(normalised.pinned, Some("/v1/chat/completions"));
        assert_eq!(normalised.base, "http://h");

        let normalised = normalise("http://h/completions");
        assert_eq!(normalised.pinned, Some("/completions"));
        assert_eq!(normalised.base, "http://h");

        // `/beta/completions` (17 bytes) beats `/completions` (12 bytes).
        let normalised = normalise("http://h/beta/completions");
        assert_eq!(normalised.pinned, Some("/beta/completions"));
        assert_eq!(normalised.base, "http://h");
    }

    #[test]
    fn an_unusable_endpoint_is_reported_without_a_single_request() {
        for endpoint in ["", "   ", "llm.internal", "ftp://llm.internal", "http://"] {
            let transport = RecordingTransport::default();
            let report = detect(&transport, endpoint, None, TIME);
            assert_eq!(
                report.failure,
                Some(FimError::InvalidEndpoint),
                "{endpoint}"
            );
            assert_eq!(report.reason().as_deref(), Some("FIM 端点无效"));
            assert_eq!(transport.get_calls(), 0, "{endpoint}");
            assert_eq!(transport.calls(), 0, "{endpoint}");
            assert!(report.models.is_empty());
            assert_eq!(report.models_url, None);
        }
    }

    // ---- §5.2 provider inference -------------------------------------------

    #[test]
    fn provider_inference_follows_the_frozen_table() {
        assert_eq!(
            infer_provider("https://api.deepseek.com", None),
            Provider::DeepSeek
        );
        assert_eq!(
            infer_provider("https://fim.deepseek.com/v1", None),
            Provider::DeepSeek
        );
        assert_eq!(
            infer_provider("https://api.openai.com", None),
            Provider::OpenAi
        );
        assert_eq!(
            infer_provider("https://eu.openai.com/v1", None),
            Provider::OpenAi
        );
        assert_eq!(
            infer_provider("http://127.0.0.1:11434", None),
            Provider::Ollama
        );
        assert_eq!(
            infer_provider("http://ollama-1.local:8080", None),
            Provider::Ollama
        );
        assert_eq!(
            infer_provider("http://10.0.0.5:8080/infill", Some("/infill")),
            Provider::LlamaCpp
        );
        assert_eq!(
            infer_provider("http://10.0.0.5:8080", None),
            Provider::Custom
        );
        // First match wins: deepseek and ollama outrank the suffix rule.
        assert_eq!(
            infer_provider("https://api.deepseek.com:11434/infill", Some("/infill")),
            Provider::DeepSeek
        );
        assert_eq!(
            infer_provider("http://ollama.local:8080/infill", Some("/infill")),
            Provider::Ollama
        );
    }

    // ---- §5.3 completion URL and protocol ----------------------------------

    #[test]
    fn deepseek_completion_url_is_derived_from_the_base() {
        assert_eq!(
            completion_url(Provider::DeepSeek, "https://api.deepseek.com", None),
            "https://api.deepseek.com/beta/completions"
        );
        // A base that already ends in /beta is not doubled.
        assert_eq!(
            completion_url(
                Provider::DeepSeek,
                "https://api.deepseek.com/beta",
                Some("/beta/completions")
            ),
            "https://api.deepseek.com/beta/completions"
        );
        // Even a typed /v1/completions routes to DeepSeek's FIM path.
        assert_eq!(
            completion_url(
                Provider::DeepSeek,
                "https://api.deepseek.com",
                Some("/v1/completions")
            ),
            "https://api.deepseek.com/beta/completions"
        );
    }

    #[test]
    fn openai_and_ollama_use_v1_completions() {
        assert_eq!(
            completion_url(Provider::OpenAi, "https://api.openai.com", None),
            "https://api.openai.com/v1/completions"
        );
        assert_eq!(
            completion_url(Provider::Ollama, "http://127.0.0.1:11434", None),
            "http://127.0.0.1:11434/v1/completions"
        );
        assert_eq!(protocol_for(Provider::OpenAi, None), Protocol::OpenAi);
        assert_eq!(protocol_for(Provider::Ollama, None), Protocol::OpenAi);
    }

    #[test]
    fn llama_cpp_uses_infill_and_does_not_double_it() {
        assert_eq!(
            completion_url(Provider::LlamaCpp, "http://h:8080", None),
            "http://h:8080/infill"
        );
        assert_eq!(
            completion_url(Provider::LlamaCpp, "http://h:8080/infill", Some("/infill")),
            "http://h:8080/infill"
        );
        assert_eq!(protocol_for(Provider::LlamaCpp, None), Protocol::LlamaCpp);
    }

    #[test]
    fn custom_keeps_the_pinned_suffix_or_defaults_to_v1() {
        assert_eq!(
            completion_url(Provider::Custom, "http://h:8080", None),
            "http://h:8080/v1/completions"
        );
        assert_eq!(
            completion_url(Provider::Custom, "http://h:8080", Some("/v1/completions")),
            "http://h:8080/v1/completions"
        );
        assert_eq!(
            completion_url(Provider::Custom, "http://h:8080", Some("/beta/completions")),
            "http://h:8080/beta/completions"
        );
        assert_eq!(
            completion_url(Provider::Custom, "http://h:8080", Some("/infill")),
            "http://h:8080/infill"
        );
        assert_eq!(protocol_for(Provider::Custom, None), Protocol::OpenAi);
        assert_eq!(
            protocol_for(Provider::Custom, Some("/beta/completions")),
            Protocol::OpenAi
        );
        assert_eq!(
            protocol_for(Provider::Custom, Some("/infill")),
            Protocol::LlamaCpp
        );
    }

    // ---- §5.4 model candidates --------------------------------------------

    #[test]
    fn models_candidates_follow_the_provider_table() {
        assert_eq!(
            candidates(Provider::DeepSeek, "https://api.deepseek.com"),
            strings(&[
                "https://api.deepseek.com/models",
                "https://api.deepseek.com/v1/models",
            ])
        );
        assert_eq!(
            candidates(Provider::OpenAi, "https://api.openai.com"),
            strings(&[
                "https://api.openai.com/v1/models",
                "https://api.openai.com/models",
            ])
        );
        assert_eq!(
            candidates(Provider::Ollama, "http://127.0.0.1:11434"),
            strings(&[
                "http://127.0.0.1:11434/api/tags",
                "http://127.0.0.1:11434/v1/models",
            ])
        );
        assert_eq!(
            candidates(Provider::LlamaCpp, "http://h:8080"),
            strings(&["http://h:8080/props", "http://h:8080/v1/models"])
        );
        assert_eq!(
            candidates(Provider::Custom, "http://h:8080"),
            strings(&["http://h:8080/v1/models", "http://h:8080/models",])
        );
        // Only DeepSeek's trailing /beta is stripped from the models base.
        assert_eq!(
            models_base(Provider::DeepSeek, "https://api.deepseek.com/beta"),
            "https://api.deepseek.com"
        );
        assert_eq!(
            models_base(Provider::Custom, "http://h:8080/beta"),
            "http://h:8080/beta"
        );
    }

    // ---- §5.5 response parsing --------------------------------------------

    #[test]
    fn parse_models_reads_data_ids_first() {
        let body =
            r#"{"object":"list","data":[{"id":"deepseek-chat"},{"id":"deepseek-reasoner"}]}"#;
        assert_eq!(
            parse_models(body),
            strings(&["deepseek-chat", "deepseek-reasoner"])
        );
    }

    #[test]
    fn parse_models_reads_model_ids_then_names() {
        assert_eq!(
            parse_models(r#"{"models":[{"id":"m1"},{"id":"m2"}]}"#),
            strings(&["m1", "m2"])
        );
        // Ollama's /api/tags lists `name` entries.
        assert_eq!(
            parse_models(r#"{"models":[{"name":"llama3:8b"},{"name":"qwen2.5:7b"}]}"#),
            strings(&["llama3:8b", "qwen2.5:7b"])
        );
    }

    #[test]
    fn parse_models_reads_plain_string_entries() {
        assert_eq!(
            parse_models(r#"{"models":["x","y"]}"#),
            strings(&["x", "y"])
        );
    }

    #[test]
    fn parse_models_reads_llama_cpp_props() {
        assert_eq!(
            parse_models(r#"{"default_generation_settings":{"model":"qwen2.5-coder-7b"}}"#),
            strings(&["qwen2.5-coder-7b"])
        );
    }

    #[test]
    fn parse_models_reads_the_model_path_basename() {
        assert_eq!(
            parse_models(r#"{"model_path":"/models/qwen2.5-coder-7b-q4.gguf"}"#),
            strings(&["qwen2.5-coder-7b-q4"])
        );
        assert_eq!(
            parse_models(r#"{"model_path":"/models/qwen2.5-coder-7b.gguf"}"#),
            strings(&["qwen2.5-coder-7b"])
        );
        // No `.gguf` suffix: the basename is kept as-is.
        assert_eq!(
            parse_models(r#"{"model_path":"/models/mistral-7b"}"#),
            strings(&["mistral-7b"])
        );
    }

    #[test]
    fn parse_models_dedupes_preserves_order_and_drops_empty_entries() {
        let body = r#"{"data":[{"id":"b"},{"id":""},{"id":"a"},{"id":"b"},{"id":"   "}]}"#;
        assert_eq!(parse_models(body), strings(&["b", "a"]));
        assert!(parse_models("not json").is_empty());
        assert!(parse_models("{}").is_empty());
        assert!(parse_models("[]").is_empty());
    }

    // ---- end-to-end over the transport seam --------------------------------

    #[test]
    fn a_deepseek_endpoint_is_normalised_and_probed_with_get_only() {
        let transport = RecordingTransport::default().with_get_body(
            "https://api.deepseek.com/models",
            r#"{"data":[{"id":"deepseek-chat"},{"id":"deepseek-reasoner"}]}"#,
        );
        let report = detect(
            &transport,
            "https://api.deepseek.com/beta/completions",
            None,
            TIME,
        );
        assert!(report.ok(), "{:?}", report.failure);
        assert_eq!(report.provider, Provider::DeepSeek);
        // The typed `/beta/completions` is the pinned suffix, so the base is the
        // host — and §5.3 rebuilds the very URL the user typed.
        assert_eq!(report.base_url, "https://api.deepseek.com");
        assert_eq!(
            report.completion_url,
            "https://api.deepseek.com/beta/completions"
        );
        assert_eq!(report.protocol, Protocol::OpenAi);
        assert_eq!(
            report.models,
            strings(&["deepseek-chat", "deepseek-reasoner"])
        );
        assert_eq!(
            report.models_url.as_deref(),
            Some("https://api.deepseek.com/models")
        );
        assert!(report.latency_ms < 5_000);
        // GET only: detection never posts a completion request.
        assert_eq!(transport.calls(), 0);
        assert_eq!(transport.get_calls(), 1);
    }

    #[test]
    fn the_contract_example_endpoint_produces_the_contract_report() {
        // Frozen contract §4's example object, produced from `…/beta` (no typed
        // completion suffix): base keeps `/beta`, the models base drops it.
        let transport = RecordingTransport::default().with_get_body(
            "https://api.deepseek.com/models",
            r#"{"data":[{"id":"deepseek-flash"},{"id":"deepseek-v4-pro"}]}"#,
        );
        let report = detect(&transport, "https://api.deepseek.com/beta", None, TIME);
        assert!(report.ok(), "{:?}", report.failure);
        assert_eq!(report.provider.as_str(), "deepseek");
        assert_eq!(report.base_url, "https://api.deepseek.com/beta");
        assert_eq!(
            report.completion_url,
            "https://api.deepseek.com/beta/completions"
        );
        assert_eq!(report.protocol, Protocol::OpenAi);
        assert_eq!(
            report.models,
            strings(&["deepseek-flash", "deepseek-v4-pro"])
        );
        assert_eq!(
            report.models_url.as_deref(),
            Some("https://api.deepseek.com/models")
        );
        assert_eq!(report.reason(), None);
    }

    #[test]
    fn a_llama_cpp_endpoint_reads_the_props_model() {
        let transport = RecordingTransport::default().with_get_body(
            "http://h:8080/props",
            r#"{"default_generation_settings":{"model":"qwen2.5-coder-7b"},"model_path":"/models/qwen2.5-coder-7b-q4.gguf"}"#,
        );
        let report = detect(&transport, "http://h:8080/infill", None, TIME);
        assert!(report.ok(), "{:?}", report.failure);
        assert_eq!(report.provider, Provider::LlamaCpp);
        assert_eq!(report.protocol, Protocol::LlamaCpp);
        assert_eq!(report.completion_url, "http://h:8080/infill");
        assert_eq!(report.models, strings(&["qwen2.5-coder-7b"]));
        assert_eq!(
            report.models_url.as_deref(),
            Some("http://h:8080/props"),
            "the /props candidate is tried first"
        );
    }

    #[test]
    fn the_first_2xx_candidate_wins_and_later_candidates_are_not_requested() {
        let transport = RecordingTransport::default()
            .with_get_failure(
                "https://api.openai.com/v1/models",
                FimError::HttpStatus { status: 404 },
            )
            .with_get_body(
                "https://api.openai.com/models",
                r#"{"data":[{"id":"gpt"}]}"#,
            );
        let report = detect(&transport, "https://api.openai.com", None, TIME);
        assert!(report.ok(), "{:?}", report.failure);
        assert_eq!(
            report.models_url.as_deref(),
            Some("https://api.openai.com/models")
        );
        assert_eq!(report.models, strings(&["gpt"]));
        assert_eq!(transport.get_calls(), 2);
        let urls: Vec<String> = transport
            .get_requests()
            .iter()
            .map(|request| request.url.clone())
            .collect();
        assert_eq!(
            urls,
            strings(&[
                "https://api.openai.com/v1/models",
                "https://api.openai.com/models",
            ])
        );
    }

    #[test]
    fn a_2xx_with_an_unparseable_body_is_a_successful_empty_detect() {
        let transport =
            RecordingTransport::default().with_get_body("http://h:8080/v1/models", "not json");
        let report = detect(&transport, "http://h:8080", None, TIME);
        assert!(report.ok(), "{:?}", report.failure);
        assert_eq!(report.failure, None);
        assert!(report.models.is_empty());
        assert_eq!(
            report.models_url.as_deref(),
            Some("http://h:8080/v1/models")
        );
    }

    #[test]
    fn authorization_is_sent_only_for_a_non_empty_key() {
        let url = "http://h:8080/v1/models";
        let transport = RecordingTransport::default().with_get_body(url, "{}");
        let key = ApiKey::new("sk-fim-test-key");
        let report = detect(&transport, "http://h:8080", Some(&key), TIME);
        assert!(report.ok(), "{:?}", report.failure);
        let request = transport
            .get_requests()
            .pop()
            .unwrap_or_else(|| panic!("no GET recorded"));
        assert!(request
            .headers
            .iter()
            .any(|(name, value)| name == "Authorization" && value == "Bearer sk-fim-test-key"));
        // The key never reaches the report (contract §3's key discipline).
        assert!(!format!("{report:?}").contains("sk-fim-test-key"));

        let transport = RecordingTransport::default().with_get_body(url, "{}");
        let blank = ApiKey::new("   ");
        let report = detect(&transport, "http://h:8080", Some(&blank), TIME);
        assert!(report.ok());
        assert!(transport
            .get_requests()
            .iter()
            .all(|request| request.headers.is_empty()));

        let transport = RecordingTransport::default().with_get_body(url, "{}");
        let report = detect(&transport, "http://h:8080", None, TIME);
        assert!(report.ok());
        assert!(transport
            .get_requests()
            .iter()
            .all(|request| request.headers.is_empty()));
    }

    // ---- §5.6 failure selection -------------------------------------------

    #[test]
    fn a_401_outranks_a_late_transport_error() {
        let transport = RecordingTransport::default()
            .with_get_failure(
                "https://api.openai.com/v1/models",
                FimError::HttpStatus { status: 401 },
            )
            .with_get_failure(
                "https://api.openai.com/models",
                FimError::Transport {
                    category: TransportCategory::ConnectionRefused,
                },
            );
        let report = detect(&transport, "https://api.openai.com", None, TIME);
        assert!(!report.ok());
        assert_eq!(report.reason().as_deref(), Some("FIM HTTP 401"));
        // A failed probe still reports the normalised completion URL; the UI must
        // not write it back (contract §4).
        assert_eq!(
            report.completion_url,
            "https://api.openai.com/v1/completions"
        );
        assert!(report.models.is_empty());
        assert_eq!(report.models_url, None);
    }

    #[test]
    fn a_transport_error_outranks_a_late_status_code() {
        let transport = RecordingTransport::default()
            .with_get_failure(
                "https://api.openai.com/v1/models",
                FimError::HttpStatus { status: 404 },
            )
            .with_get_failure(
                "https://api.openai.com/models",
                FimError::Transport {
                    category: TransportCategory::HostNotFound,
                },
            );
        let report = detect(&transport, "https://api.openai.com", None, TIME);
        assert_eq!(report.reason().as_deref(), Some("FIM 域名解析失败"));
    }

    #[test]
    fn the_last_status_code_is_reported_when_every_candidate_returns_one() {
        let transport = RecordingTransport::default()
            .with_get_failure(
                "https://api.openai.com/v1/models",
                FimError::HttpStatus { status: 404 },
            )
            .with_get_failure(
                "https://api.openai.com/models",
                FimError::HttpStatus { status: 500 },
            );
        let report = detect(&transport, "https://api.openai.com", None, TIME);
        assert_eq!(report.reason().as_deref(), Some("FIM HTTP 500"));
    }

    #[test]
    fn a_timeout_keeps_its_own_sentence() {
        let transport = RecordingTransport::default()
            .with_get_failure(
                "https://api.openai.com/v1/models",
                FimError::Timeout { after_ms: 500 },
            )
            .with_get_failure(
                "https://api.openai.com/models",
                FimError::HttpStatus { status: 404 },
            );
        let report = detect(&transport, "https://api.openai.com", None, TIME);
        assert_eq!(report.reason().as_deref(), Some("FIM 超时（500ms）"));
    }

    #[test]
    fn the_fallback_reason_is_endpoint_missing() {
        assert_eq!(failure_reason(&[]), FimError::EndpointMissing);
        assert_eq!(failure_reason(&[]).reason(), "FIM 未配置端点");
    }

    #[test]
    fn an_oversized_body_is_reported_as_such_not_as_unconfigured() {
        let error = FimError::ResponseTooLarge { limit_bytes: 64 };
        assert_eq!(
            failure_reason(&[error]),
            FimError::ResponseTooLarge { limit_bytes: 64 }
        );
    }

    #[test]
    fn detect_http_short_circuits_an_invalid_endpoint_without_a_request() {
        let report = detect_http("   ", None, TIME);
        assert_eq!(report.failure, Some(FimError::InvalidEndpoint));
        assert_eq!(report.models_url, None);
    }

    #[test]
    fn model_lists_are_six_shapes_in_order() {
        let value: serde_json::Value = serde_json::from_str(
            r#"{"data":[{"id":"d"}],"models":[{"id":"i","name":"n"},"s"],"default_generation_settings":{"model":"g"},"model_path":"/m/p.gguf"}"#,
        )
        .unwrap_or_else(|error| panic!("{error}"));
        let lists = model_lists(&value);
        assert_eq!(lists.len(), 6);
        assert_eq!(lists[0], strings(&["d"]));
        assert_eq!(lists[1], strings(&["i"]));
        assert_eq!(lists[2], strings(&["n"]));
        assert_eq!(lists[3], strings(&["s"]));
        assert_eq!(lists[4], strings(&["g"]));
        assert_eq!(lists[5], strings(&["p"]));
    }
}
