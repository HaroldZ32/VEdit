//! Test double for the transport seam.
//!
//! It is public (not `#[cfg(test)]`) so that the verification lane can assert
//! "zero network calls" and "exact request body" from its own test, and so the app
//! wiring can smoke-test the disabled path without a socket.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;
use std::time::Duration;

use serde_json::Value;

use crate::error::{FimError, TransportCategory};
use crate::transport::Transport;

/// A [`Transport`] that records every call and replays a scripted answer.
///
/// Each call consumes the scripted answer; once it is exhausted the transport
/// fails with a generic transport error, which keeps a test from silently
/// depending on more calls than it scripted.
#[derive(Default)]
pub struct RecordingTransport {
    calls: AtomicUsize,
    seen: Mutex<Vec<SeenRequest>>,
    answer: Mutex<Option<Result<String, FimError>>>,
    delay: Mutex<Duration>,
}

/// One recorded request.
#[derive(Debug, Clone)]
pub struct SeenRequest {
    /// Request URL.
    pub url: String,
    /// Request headers, in order.
    pub headers: Vec<(String, String)>,
    /// JSON request body.
    pub body: Value,
    /// Timeout that was in force.
    pub timeout: Duration,
}

impl RecordingTransport {
    /// A transport that answers the first call with `text`.
    #[must_use]
    pub fn answering(text: &str) -> Self {
        Self::scripted(Err(FimError::Transport {
            category: TransportCategory::Other,
        }))
        .with_first_answer(text)
    }

    /// A transport whose first call fails with `error`.
    #[must_use]
    pub fn failing(error: FimError) -> Self {
        Self::scripted(Err(error))
    }

    fn scripted(answer: Result<String, FimError>) -> Self {
        let transport = Self::default();
        transport.script(answer);
        transport
    }

    /// Adds a fixed delay before every answer, so coalescing can be observed.
    #[must_use]
    pub fn with_delay(self, delay: Duration) -> Self {
        if let Ok(mut slot) = self.delay.lock() {
            *slot = delay;
        }
        self
    }

    fn script(&self, answer: Result<String, FimError>) {
        if let Ok(mut slot) = self.answer.lock() {
            *slot = Some(answer);
        }
    }

    fn with_first_answer(self, text: &str) -> Self {
        self.script(Ok(text.to_string()));
        self
    }

    /// Number of `post_json` calls observed.
    #[must_use]
    pub fn calls(&self) -> usize {
        self.calls.load(Ordering::SeqCst)
    }

    /// Every recorded request.
    #[must_use]
    pub fn requests(&self) -> Vec<SeenRequest> {
        self.seen
            .lock()
            .map(|seen| seen.clone())
            .unwrap_or_default()
    }

    /// The last recorded request, if any.
    #[must_use]
    pub fn last_request(&self) -> Option<SeenRequest> {
        self.requests().pop()
    }
}

impl Transport for RecordingTransport {
    fn post_json(
        &self,
        url: &str,
        headers: &[(&str, &str)],
        body: &Value,
        timeout: Duration,
        _max_response_bytes: usize,
    ) -> Result<String, FimError> {
        self.calls.fetch_add(1, Ordering::SeqCst);
        let delay = self.delay.lock().map(|slot| *slot).unwrap_or_default();
        if !delay.is_zero() {
            std::thread::sleep(delay);
        }
        if let Ok(mut seen) = self.seen.lock() {
            seen.push(SeenRequest {
                url: url.to_string(),
                headers: headers
                    .iter()
                    .map(|(name, value)| ((*name).to_string(), (*value).to_string()))
                    .collect(),
                body: body.clone(),
                timeout,
            });
        }
        self.answer
            .lock()
            .ok()
            .and_then(|mut slot| slot.take())
            .unwrap_or(Err(FimError::Transport {
                category: TransportCategory::Other,
            }))
    }
}
