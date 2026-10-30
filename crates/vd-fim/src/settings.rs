//! FIM configuration as the transport layer needs it.
//!
//! This is a plain-data projection of `vd_config::FimConfig` (which `vd-fim`
//! cannot depend on without a manifest change). The application layer maps one to
//! the other: `FimProtocol::{OpenAi, LlamaCpp}` → [`Protocol`],
//! `timeout_ms` → [`FimSettings::timeout_ms`], and
//! `api_key.as_ref().map(|key| ApiKey::new(key.expose_secret()))`.
//!
//! There is **no default endpoint, model or key**: an empty endpoint/model means
//! "not configured", which is reported as unavailable instead of guessed.

use std::time::Duration;

use crate::error::FimError;
use crate::secret::ApiKey;

/// Timeout used when the configuration does not say otherwise.
pub const DEFAULT_TIMEOUT_MS: u64 = 5_000;
/// Lower clamp for the configured timeout.
pub const MIN_TIMEOUT_MS: u64 = 100;
/// Upper clamp for the configured timeout.
pub const MAX_TIMEOUT_MS: u64 = 60_000;
/// Completion length requested when the configuration does not say otherwise.
pub const DEFAULT_MAX_TOKENS: u32 = 64;
/// Upper clamp for the requested completion length.
pub const MAX_MAX_TOKENS: u32 = 512;

/// Wire protocol of the configured endpoint.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Protocol {
    /// OpenAI-compatible `POST /v1/completions` with `prompt` + `suffix`.
    OpenAi,
    /// llama.cpp `POST /infill` with `input_prefix` + `input_suffix`.
    LlamaCpp,
}

impl Protocol {
    /// The endpoint path this protocol appends to the configured base URL.
    #[must_use]
    pub fn request_path(self) -> &'static str {
        match self {
            Self::OpenAi => "/v1/completions",
            Self::LlamaCpp => "/infill",
        }
    }
}

/// Everything the FIM transport needs, and nothing else.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FimSettings {
    /// Whether FIM is switched on at all.
    pub enabled: bool,
    /// Wire protocol of the endpoint.
    pub protocol: Protocol,
    /// Base endpoint, e.g. `http://127.0.0.1:8080`. Empty means not configured.
    pub endpoint: String,
    /// Model name. Empty means not configured.
    pub model: String,
    /// Request timeout in milliseconds (clamped on use).
    pub timeout_ms: u64,
    /// Maximum number of tokens to request.
    pub max_tokens: u32,
    /// Optional bearer token. Redacted by its own type.
    pub api_key: Option<ApiKey>,
}

impl Default for FimSettings {
    fn default() -> Self {
        Self {
            enabled: false,
            protocol: Protocol::OpenAi,
            endpoint: String::new(),
            model: String::new(),
            timeout_ms: DEFAULT_TIMEOUT_MS,
            max_tokens: DEFAULT_MAX_TOKENS,
            api_key: None,
        }
    }
}

impl FimSettings {
    /// Settings with everything switched off (the product default).
    #[must_use]
    pub fn disabled() -> Self {
        Self::default()
    }

    /// Clamped request timeout.
    #[must_use]
    pub fn timeout(&self) -> Duration {
        Duration::from_millis(self.timeout_ms.clamp(MIN_TIMEOUT_MS, MAX_TIMEOUT_MS))
    }

    /// Clamped completion length.
    #[must_use]
    pub fn token_budget(&self) -> u32 {
        self.max_tokens.clamp(1, MAX_MAX_TOKENS)
    }

    /// Why this configuration cannot produce a completion, if it cannot.
    ///
    /// Checked in a fixed order so the status bar shows the first real problem.
    #[must_use]
    pub fn unavailable(&self) -> Option<FimError> {
        if !self.enabled {
            return Some(FimError::Disabled);
        }
        if self.endpoint.trim().is_empty() {
            return Some(FimError::EndpointMissing);
        }
        if self.protocol == Protocol::OpenAi && self.model.trim().is_empty() {
            // llama.cpp /infill has no model field, so only the OpenAI-compatible
            // protocol requires a configured model.
            return Some(FimError::ModelMissing);
        }
        if !valid_endpoint(&self.endpoint) {
            return Some(FimError::InvalidEndpoint);
        }
        None
    }

    /// The URL a request would go to, or `None` when the configuration is unusable.
    #[must_use]
    pub fn request_url(&self) -> Option<String> {
        if self.unavailable().is_some() {
            return None;
        }
        Some(join_url(&self.endpoint, self.protocol.request_path()))
    }
}

/// True for `http://host...` / `https://host...` with a non-empty, space-free host.
fn valid_endpoint(endpoint: &str) -> bool {
    let rest = endpoint
        .trim()
        .strip_prefix("https://")
        .or_else(|| endpoint.trim().strip_prefix("http://"));
    let Some(rest) = rest else {
        return false;
    };
    let host = rest.split('/').next().unwrap_or_default();
    !host.is_empty() && !host.contains(char::is_whitespace)
}

/// Appends `path` to `base` without doubling a path the user already typed.
fn join_url(base: &str, path: &str) -> String {
    let base = base.trim().trim_end_matches('/');
    if base.ends_with(path) {
        base.to_string()
    } else {
        format!("{base}{path}")
    }
}

#[cfg(test)]
mod tests {
    use super::{join_url, FimSettings, Protocol, DEFAULT_TIMEOUT_MS};
    use crate::error::FimError;

    fn enabled() -> FimSettings {
        FimSettings {
            enabled: true,
            endpoint: "http://127.0.0.1:8080".to_string(),
            model: "qwen2.5-coder".to_string(),
            ..FimSettings::default()
        }
    }

    #[test]
    fn default_settings_are_disabled_and_unconfigured() {
        let settings = FimSettings::default();
        assert!(!settings.enabled);
        assert_eq!(settings.timeout_ms, DEFAULT_TIMEOUT_MS);
        assert_eq!(settings.unavailable(), Some(FimError::Disabled));
        assert_eq!(settings.request_url(), None);
        assert!(settings.api_key.is_none());
        assert!(settings.endpoint.is_empty());
        assert!(settings.model.is_empty());
    }

    #[test]
    fn half_configured_settings_are_reported_not_guessed() {
        let mut settings = enabled();
        settings.endpoint.clear();
        assert_eq!(settings.unavailable(), Some(FimError::EndpointMissing));

        let mut settings = enabled();
        settings.model.clear();
        assert_eq!(settings.unavailable(), Some(FimError::ModelMissing));

        let mut settings = enabled();
        settings.endpoint = "llm.internal".to_string();
        assert_eq!(settings.unavailable(), Some(FimError::InvalidEndpoint));

        let mut settings = enabled();
        settings.endpoint = "ftp://llm.internal".to_string();
        assert_eq!(settings.unavailable(), Some(FimError::InvalidEndpoint));
    }

    #[test]
    fn url_join_uses_the_protocol_path_without_doubling_it() {
        assert_eq!(
            join_url("http://127.0.0.1:8080", "/v1/completions"),
            "http://127.0.0.1:8080/v1/completions"
        );
        assert_eq!(
            join_url("http://127.0.0.1:8080/", "/infill"),
            "http://127.0.0.1:8080/infill"
        );
        assert_eq!(
            join_url("http://127.0.0.1:8080/v1/completions", "/v1/completions"),
            "http://127.0.0.1:8080/v1/completions"
        );
        let mut settings = enabled();
        assert_eq!(
            settings.request_url().as_deref(),
            Some("http://127.0.0.1:8080/v1/completions")
        );
        settings.protocol = Protocol::LlamaCpp;
        assert_eq!(
            settings.request_url().as_deref(),
            Some("http://127.0.0.1:8080/infill")
        );
    }

    #[test]
    fn timeout_and_token_budget_are_clamped() {
        let mut settings = enabled();
        settings.timeout_ms = 0;
        assert_eq!(settings.timeout().as_millis(), 100);
        settings.timeout_ms = 10_000_000;
        assert_eq!(settings.timeout().as_millis(), 60_000);
        settings.max_tokens = 0;
        assert_eq!(settings.token_budget(), 1);
        settings.max_tokens = 100_000;
        assert_eq!(settings.token_budget(), 512);
    }
}
