//! The transport seam: one trait, one real HTTP implementation, mocks in tests.
//!
//! Splitting the transport out is what makes "FIM off means zero network calls" a
//! countable fact instead of a belief: a disabled service never receives (or
//! constructs) a transport at all, and a test can inject one whose `post_json`
//! increments a counter.
//!
//! TLS is rustls (`ureq` is built with `rustls` and without `native-tls`), so the
//! product never links OpenSSL — a hard constraint for a CentOS 7 AppImage.

use std::time::Duration;

use serde_json::Value;

use crate::error::{FimError, TransportCategory};

/// One HTTP JSON POST, abstracted so tests can count and script it.
pub trait Transport: Send + Sync {
    /// POSTs `body` as JSON to `url` and returns the response text.
    ///
    /// Implementations must not buffer more than `max_response_bytes` and must
    /// report overruns as [`FimError::ResponseTooLarge`].
    fn post_json(
        &self,
        url: &str,
        headers: &[(&str, &str)],
        body: &Value,
        timeout: Duration,
        max_response_bytes: usize,
    ) -> Result<String, FimError>;
}

/// The real transport: `ureq` over rustls, one fresh agent per request so the
/// configured timeout applies without touching global state.
#[derive(Debug, Default, Clone, Copy)]
pub struct HttpTransport;

impl HttpTransport {
    /// Creates the HTTP transport.
    #[must_use]
    pub fn new() -> Self {
        Self
    }
}

impl Transport for HttpTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(&str, &str)],
        body: &Value,
        timeout: Duration,
        max_response_bytes: usize,
    ) -> Result<String, FimError> {
        let config = ureq::Agent::config_builder()
            .timeout_global(Some(timeout))
            .build();
        let agent = ureq::Agent::new_with_config(config);
        let mut request = agent.post(url);
        for (name, value) in headers {
            request = request.header(*name, *value);
        }
        let response = request
            .send_json(body)
            .map_err(|error| map_ureq_error(&error, timeout))?;
        let limit = max_response_bytes.saturating_add(1) as u64;
        let text = response
            .into_body()
            .with_config()
            .limit(limit)
            .read_to_string()
            .map_err(|error| map_ureq_error(&error, timeout))?;
        enforce_response_cap(text, max_response_bytes)
    }
}

/// Rejects a body over the configured cap instead of buffering it.
fn enforce_response_cap(body: String, limit_bytes: usize) -> Result<String, FimError> {
    if body.len() > limit_bytes {
        return Err(FimError::ResponseTooLarge { limit_bytes });
    }
    Ok(body)
}

/// Maps a `ureq` error to a sanitised [`FimError`].
///
/// The raw `ureq` message is never propagated: several variants embed the request
/// URL (which may carry query parameters the user typed) and must not reach the
/// status bar or a log.
fn map_ureq_error(error: &ureq::Error, timeout: Duration) -> FimError {
    let after_ms = u64::try_from(timeout.as_millis()).unwrap_or(u64::MAX);
    match error {
        ureq::Error::StatusCode(status) => FimError::HttpStatus { status: *status },
        ureq::Error::Timeout(_) => FimError::Timeout { after_ms },
        ureq::Error::HostNotFound => FimError::Transport {
            category: TransportCategory::HostNotFound,
        },
        ureq::Error::BadUri(_) => FimError::InvalidEndpoint,
        ureq::Error::Io(io_error) => classify_io(io_error),
        ureq::Error::ConnectionFailed => FimError::Transport {
            category: TransportCategory::ConnectionRefused,
        },
        _ => FimError::Transport {
            category: TransportCategory::Other,
        },
    }
}

fn classify_io(error: &std::io::Error) -> FimError {
    match error.kind() {
        std::io::ErrorKind::ConnectionRefused => FimError::Transport {
            category: TransportCategory::ConnectionRefused,
        },
        std::io::ErrorKind::TimedOut => FimError::Timeout { after_ms: 0 },
        _ => {
            // rustls handshake failures surface as an io error; the message is not
            // inspected so a certificate detail cannot leak into the status bar.
            FimError::Transport {
                category: TransportCategory::Io,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::{classify_io, enforce_response_cap, map_ureq_error};
    use crate::error::{FimError, TransportCategory};

    #[test]
    fn http_status_maps_to_the_status_code_only() {
        let error = map_ureq_error(&ureq::Error::StatusCode(401), Duration::from_millis(5_000));
        assert_eq!(error, FimError::HttpStatus { status: 401 });
        assert_eq!(error.reason(), "FIM HTTP 401");
    }

    #[test]
    fn timeout_maps_to_the_configured_milliseconds() {
        let error = map_ureq_error(
            &ureq::Error::Timeout(ureq::Timeout::Global),
            Duration::from_millis(5_000),
        );
        assert_eq!(error, FimError::Timeout { after_ms: 5_000 });
    }

    #[test]
    fn host_lookup_failure_is_sanitised() {
        assert_eq!(
            map_ureq_error(&ureq::Error::HostNotFound, Duration::from_millis(1)),
            FimError::Transport {
                category: TransportCategory::HostNotFound
            }
        );
    }

    #[test]
    fn a_bad_uri_never_echoes_the_url() {
        let error = map_ureq_error(
            &ureq::Error::BadUri("https://user:secret@host/?api_key=leaked".to_string()),
            Duration::from_millis(1),
        );
        assert_eq!(error, FimError::InvalidEndpoint);
        let reason = error.reason();
        assert!(!reason.contains("secret"), "{reason}");
        assert!(!reason.contains("leaked"), "{reason}");
    }

    #[test]
    fn the_response_cap_is_enforced_at_the_byte() {
        assert_eq!(
            enforce_response_cap("abcd".to_string(), 4).ok().as_deref(),
            Some("abcd")
        );
        assert_eq!(
            enforce_response_cap("abcde".to_string(), 4).err(),
            Some(FimError::ResponseTooLarge { limit_bytes: 4 })
        );
    }

    #[test]
    fn io_errors_are_classified_without_their_message() {
        let refused = std::io::Error::from(std::io::ErrorKind::ConnectionRefused);
        assert_eq!(
            classify_io(&refused),
            FimError::Transport {
                category: TransportCategory::ConnectionRefused
            }
        );
        let other = std::io::Error::other("certificate for secret failed");
        let mapped = classify_io(&other);
        assert_eq!(
            mapped,
            FimError::Transport {
                category: TransportCategory::Io
            }
        );
        assert!(!mapped.reason().contains("secret"));
    }
}
