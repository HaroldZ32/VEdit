//! The two protocol adapters, on top of one transport.
//!
//! An adapter only knows how to turn (prefix, suffix) into its protocol's request
//! body and how to read its response; retries, threading and cancellation live in
//! the service. Both share [`BackendSpec`], which validates the settings once.

use std::sync::Arc;
use std::time::Duration;

use crate::error::FimError;
use crate::protocol::{
    llama_cpp_request_body, openai_request_body, parse_llama_cpp_response, parse_openai_response,
    MAX_RESPONSE_BYTES,
};
use crate::secret::ApiKey;
use crate::settings::{FimSettings, Protocol};
use crate::transport::Transport;

/// The text around the cursor a completion is asked for.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CompletionRequest {
    /// Text before the cursor.
    pub prefix: String,
    /// Text after the cursor.
    pub suffix: String,
}

impl CompletionRequest {
    /// Builds a request from the buffer text around the cursor.
    #[must_use]
    pub fn new(prefix: impl Into<String>, suffix: impl Into<String>) -> Self {
        Self {
            prefix: prefix.into(),
            suffix: suffix.into(),
        }
    }
}

/// What the service needs from a completion backend.
pub trait CompletionBackend: Send + Sync {
    /// Runs one completion request and returns the text to insert.
    fn complete(&self, request: &CompletionRequest) -> Result<String, FimError>;
}

/// Validated, protocol-independent request parameters.
#[derive(Debug, Clone)]
pub struct BackendSpec {
    /// Absolute request URL (base endpoint + protocol path).
    pub url: String,
    /// Model name (used by the OpenAI-compatible protocol only).
    pub model: String,
    /// Request timeout.
    pub timeout: Duration,
    /// Maximum number of tokens to request.
    pub max_tokens: u32,
    /// Optional bearer token.
    pub api_key: Option<ApiKey>,
}

impl BackendSpec {
    /// Validates `settings` and derives the request parameters.
    ///
    /// # Errors
    /// Returns the configuration error when the settings are unusable (disabled,
    /// endpoint or model missing, endpoint not an `http(s)` URL).
    pub fn from_settings(settings: &FimSettings) -> Result<Self, FimError> {
        if let Some(error) = settings.unavailable() {
            return Err(error);
        }
        let url = settings.request_url().ok_or(FimError::InvalidEndpoint)?;
        Ok(Self {
            url,
            model: settings.model.clone(),
            timeout: settings.timeout(),
            max_tokens: settings.token_budget(),
            api_key: settings.api_key.clone(),
        })
    }

    /// Headers for a request: JSON content type plus a bearer token when set.
    ///
    /// The key value only ever leaves through [`ApiKey::expose_secret`] here, on
    /// its way to the socket.
    fn headers(&self) -> Vec<(&'static str, String)> {
        let mut headers: Vec<(&'static str, String)> =
            vec![("Content-Type", "application/json".to_string())];
        if let Some(key) = &self.api_key {
            if !key.is_empty() {
                headers.push(("Authorization", format!("Bearer {}", key.expose_secret())));
            }
        }
        headers
    }

    fn post(
        &self,
        transport: &dyn Transport,
        body: &serde_json::Value,
    ) -> Result<String, FimError> {
        let owned = self.headers();
        let borrowed: Vec<(&str, &str)> = owned
            .iter()
            .map(|(name, value)| (*name, value.as_str()))
            .collect();
        transport.post_json(&self.url, &borrowed, body, self.timeout, MAX_RESPONSE_BYTES)
    }
}

/// OpenAI-compatible `/v1/completions` adapter (`prompt` + `suffix`).
pub struct OpenAiBackend {
    spec: BackendSpec,
    transport: Arc<dyn Transport>,
}

impl OpenAiBackend {
    /// Builds the adapter from validated parameters.
    #[must_use]
    pub fn new(spec: BackendSpec, transport: Arc<dyn Transport>) -> Self {
        Self { spec, transport }
    }
}

impl CompletionBackend for OpenAiBackend {
    fn complete(&self, request: &CompletionRequest) -> Result<String, FimError> {
        let body = openai_request_body(
            &self.spec.model,
            &request.prefix,
            &request.suffix,
            self.spec.max_tokens,
        );
        let response = self.spec.post(self.transport.as_ref(), &body)?;
        parse_openai_response(&response)
    }
}

/// llama.cpp `/infill` adapter (`input_prefix` + `input_suffix`).
pub struct LlamaCppBackend {
    spec: BackendSpec,
    transport: Arc<dyn Transport>,
}

impl LlamaCppBackend {
    /// Builds the adapter from validated parameters.
    #[must_use]
    pub fn new(spec: BackendSpec, transport: Arc<dyn Transport>) -> Self {
        Self { spec, transport }
    }
}

impl CompletionBackend for LlamaCppBackend {
    fn complete(&self, request: &CompletionRequest) -> Result<String, FimError> {
        let body = llama_cpp_request_body(&request.prefix, &request.suffix, self.spec.max_tokens);
        let response = self.spec.post(self.transport.as_ref(), &body)?;
        parse_llama_cpp_response(&response)
    }
}

/// Builds the adapter the settings ask for.
///
/// # Errors
/// Returns the configuration error when the settings are unusable.
pub fn backend_from_settings(
    settings: &FimSettings,
    transport: Arc<dyn Transport>,
) -> Result<Box<dyn CompletionBackend>, FimError> {
    let spec = BackendSpec::from_settings(settings)?;
    Ok(match settings.protocol {
        Protocol::OpenAi => Box::new(OpenAiBackend::new(spec, transport)),
        Protocol::LlamaCpp => Box::new(LlamaCppBackend::new(spec, transport)),
    })
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use serde_json::json;

    use super::{backend_from_settings, CompletionRequest};
    use crate::error::FimError;
    use crate::secret::ApiKey;
    use crate::settings::{FimSettings, Protocol};
    use crate::testing::RecordingTransport;
    use crate::transport::Transport;

    fn openai_settings(key: Option<&str>) -> FimSettings {
        FimSettings {
            enabled: true,
            protocol: Protocol::OpenAi,
            endpoint: "http://127.0.0.1:8080".to_string(),
            model: "qwen2.5-coder".to_string(),
            max_tokens: 64,
            api_key: key.map(ApiKey::new),
            ..FimSettings::default()
        }
    }

    fn llama_settings() -> FimSettings {
        FimSettings {
            protocol: Protocol::LlamaCpp,
            endpoint: "http://127.0.0.1:8001".to_string(),
            ..openai_settings(None)
        }
    }

    fn as_dyn(transport: &Arc<RecordingTransport>) -> Arc<dyn Transport> {
        transport.clone()
    }

    #[test]
    fn openai_request_sends_prompt_suffix_model_and_bearer() {
        let transport = Arc::new(RecordingTransport::answering(
            r#"{"choices":[{"text":" <= 1;"}]}"#,
        ));
        let adapter =
            backend_from_settings(&openai_settings(Some("sk-test-key")), as_dyn(&transport))
                .unwrap_or_else(|error| panic!("settings should be valid: {error:?}"));
        let completion = adapter
            .complete(&CompletionRequest::new("assign x", ";\n"))
            .unwrap_or_default();
        assert_eq!(completion, " <= 1;");

        assert_eq!(transport.calls(), 1);
        let request = transport
            .last_request()
            .unwrap_or_else(|| panic!("no request recorded"));
        assert_eq!(request.url, "http://127.0.0.1:8080/v1/completions");
        assert_eq!(
            request.body,
            json!({
                "model": "qwen2.5-coder",
                "prompt": "assign x",
                "suffix": ";\n",
                "max_tokens": 64,
            })
        );
        assert_eq!(
            request.headers,
            vec![
                ("Content-Type".to_string(), "application/json".to_string()),
                (
                    "Authorization".to_string(),
                    "Bearer sk-test-key".to_string()
                ),
            ]
        );
    }

    #[test]
    fn llama_cpp_request_sends_input_prefix_and_suffix_without_model() {
        let transport = Arc::new(RecordingTransport::answering(r#"{"content":"x"}"#));
        let adapter = backend_from_settings(&llama_settings(), as_dyn(&transport))
            .unwrap_or_else(|error| panic!("settings should be valid: {error:?}"));
        let _ = adapter.complete(&CompletionRequest::new("always_ff @(", ") begin"));

        let request = transport
            .last_request()
            .unwrap_or_else(|| panic!("no request recorded"));
        assert_eq!(request.url, "http://127.0.0.1:8001/infill");
        assert_eq!(
            request.body,
            json!({
                "input_prefix": "always_ff @(",
                "input_suffix": ") begin",
                "n_predict": 64,
            })
        );
        // No key configured => no Authorization header at all.
        assert_eq!(
            request.headers,
            vec![("Content-Type".to_string(), "application/json".to_string())]
        );
    }

    #[test]
    fn an_empty_key_never_produces_an_authorization_header() {
        let transport = Arc::new(RecordingTransport::answering(
            r#"{"choices":[{"text":"a"}]}"#,
        ));
        let adapter = backend_from_settings(&openai_settings(Some("   ")), as_dyn(&transport))
            .unwrap_or_else(|error| panic!("{error:?}"));
        let _ = adapter.complete(&CompletionRequest::new("a", "b"));
        let request = transport
            .last_request()
            .unwrap_or_else(|| panic!("no request recorded"));
        assert!(!request
            .headers
            .iter()
            .any(|(name, _)| name.eq_ignore_ascii_case("authorization")));
    }

    #[test]
    fn a_failing_transport_propagates_the_sanitised_error() {
        let transport = Arc::new(RecordingTransport::failing(FimError::HttpStatus {
            status: 401,
        }));
        let adapter = backend_from_settings(&openai_settings(None), as_dyn(&transport))
            .unwrap_or_else(|error| panic!("{error:?}"));
        let error = adapter.complete(&CompletionRequest::new("a", "b"));
        assert_eq!(error.err(), Some(FimError::HttpStatus { status: 401 }));
        assert_eq!(transport.calls(), 1);
    }

    #[test]
    fn unusable_settings_do_not_produce_a_backend() {
        let mut settings = openai_settings(None);
        settings.endpoint.clear();
        let transport = Arc::new(RecordingTransport::answering("{}"));
        let result = backend_from_settings(&settings, as_dyn(&transport));
        assert_eq!(result.err(), Some(FimError::EndpointMissing));
        assert_eq!(transport.calls(), 0);
    }

    #[test]
    fn llama_cpp_does_not_require_a_model() {
        let mut settings = llama_settings();
        settings.model.clear();
        assert!(settings.unavailable().is_none());
        let transport = Arc::new(RecordingTransport::answering(r#"{"content":"x"}"#));
        assert!(backend_from_settings(&settings, as_dyn(&transport)).is_ok());
    }
}
