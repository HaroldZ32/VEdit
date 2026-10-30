//! Typed FIM failures and the short, status-bar-ready summary T21 displays.
//!
//! Redaction is structural: every variant carries either a fixed string or a
//! number, never a URL, header, body or key. [`FimError::reason`] therefore
//! cannot leak configuration detail — a test asserts exactly that.

/// Upper bound for [`FimError::reason`], in characters.
pub const MAX_REASON_CHARS: usize = 80;

/// Sanitised category of a transport failure.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportCategory {
    /// The TCP connection was refused (nothing listening).
    ConnectionRefused,
    /// The host name did not resolve.
    HostNotFound,
    /// The TLS handshake failed.
    Tls,
    /// Any other I/O failure (reset, EOF, ...).
    Io,
    /// A failure ureq reports without a more specific category.
    Other,
}

/// Why a completion request produced no text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FimError {
    /// FIM is switched off in the configuration.
    Disabled,
    /// FIM is on but no endpoint is configured.
    EndpointMissing,
    /// FIM is on but no model is configured.
    ModelMissing,
    /// The endpoint is not a usable `http(s)://host...` URL.
    InvalidEndpoint,
    /// The request exceeded the configured timeout.
    Timeout {
        /// The timeout that was in force, in milliseconds.
        after_ms: u64,
    },
    /// The server answered with a non-2xx status.
    HttpStatus {
        /// The HTTP status code only; headers and body are discarded.
        status: u16,
    },
    /// The response body was larger than the configured cap.
    ResponseTooLarge {
        /// The cap that was exceeded, in bytes.
        limit_bytes: usize,
    },
    /// The response body did not have the shape the protocol requires.
    InvalidResponse {
        /// A fixed description, never server-controlled text.
        detail: &'static str,
    },
    /// The request never reached the server, or the connection broke.
    Transport {
        /// Sanitised failure category.
        category: TransportCategory,
    },
}

impl FimError {
    /// A short user-facing sentence for the status bar (at most
    /// [`MAX_REASON_CHARS`] characters, no secret, no URL, no header).
    #[must_use]
    pub fn reason(&self) -> String {
        match self {
            Self::Disabled => "FIM 未启用".to_string(),
            Self::EndpointMissing => "FIM 未配置端点".to_string(),
            Self::ModelMissing => "FIM 未配置模型".to_string(),
            Self::InvalidEndpoint => "FIM 端点无效".to_string(),
            Self::Timeout { after_ms } => format!("FIM 超时（{after_ms}ms）"),
            Self::HttpStatus { status } => format!("FIM HTTP {status}"),
            Self::ResponseTooLarge { .. } => "FIM 响应过大".to_string(),
            Self::InvalidResponse { .. } => "FIM 响应格式错误".to_string(),
            Self::Transport { category } => match category {
                TransportCategory::ConnectionRefused => "FIM 连接被拒绝".to_string(),
                TransportCategory::HostNotFound => "FIM 域名解析失败".to_string(),
                TransportCategory::Tls => "FIM TLS 失败".to_string(),
                TransportCategory::Io => "FIM 网络错误".to_string(),
                TransportCategory::Other => "FIM 传输失败".to_string(),
            },
        }
    }

    /// Whether the failure is a configuration problem rather than a runtime one.
    #[must_use]
    pub fn is_configuration(&self) -> bool {
        matches!(
            self,
            Self::Disabled | Self::EndpointMissing | Self::ModelMissing | Self::InvalidEndpoint
        )
    }
}

#[cfg(test)]
mod tests {
    use super::{FimError, TransportCategory, MAX_REASON_CHARS};

    fn every_error() -> Vec<FimError> {
        vec![
            FimError::Disabled,
            FimError::EndpointMissing,
            FimError::ModelMissing,
            FimError::InvalidEndpoint,
            FimError::Timeout { after_ms: 5_000 },
            FimError::HttpStatus { status: 401 },
            FimError::ResponseTooLarge {
                limit_bytes: 65_536,
            },
            FimError::InvalidResponse { detail: "bad json" },
            FimError::Transport {
                category: TransportCategory::ConnectionRefused,
            },
            FimError::Transport {
                category: TransportCategory::HostNotFound,
            },
            FimError::Transport {
                category: TransportCategory::Tls,
            },
            FimError::Transport {
                category: TransportCategory::Io,
            },
            FimError::Transport {
                category: TransportCategory::Other,
            },
        ]
    }

    #[test]
    fn every_reason_is_short_and_not_empty() {
        for error in every_error() {
            let reason = error.reason();
            assert!(!reason.is_empty(), "{error:?}");
            assert!(
                reason.chars().count() <= MAX_REASON_CHARS,
                "{reason} exceeds {MAX_REASON_CHARS} chars"
            );
        }
    }

    #[test]
    fn status_and_timeout_reasons_carry_the_number() {
        assert_eq!(
            FimError::HttpStatus { status: 401 }.reason(),
            "FIM HTTP 401"
        );
        assert_eq!(
            FimError::Timeout { after_ms: 5_000 }.reason(),
            "FIM 超时（5000ms）"
        );
    }

    #[test]
    fn reasons_never_contain_a_secret_or_a_url() {
        let secret = "sk-fim-do-not-print-1234567890";
        let endpoint = "https://llm.internal.example:8443/v1";
        for error in every_error() {
            let reason = error.reason();
            assert!(!reason.contains(secret), "secret leaked: {reason}");
            assert!(!reason.contains(endpoint), "endpoint leaked: {reason}");
            assert!(!reason.contains("Bearer"), "auth header leaked: {reason}");
        }
    }

    #[test]
    fn configuration_failures_are_classified_as_such() {
        assert!(FimError::Disabled.is_configuration());
        assert!(FimError::EndpointMissing.is_configuration());
        assert!(!FimError::Timeout { after_ms: 1 }.is_configuration());
    }
}
