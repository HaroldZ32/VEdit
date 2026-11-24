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
/// Each POST call consumes the scripted answer; once it is exhausted the
/// transport fails with a generic transport error, which keeps a test from
/// silently depending on more calls than it scripted. GET answers are scripted
/// **per URL** (detection walks a fixed candidate list, so "first candidate 404,
/// second candidate 200" has to be expressible).
#[derive(Default)]
pub struct RecordingTransport {
    calls: AtomicUsize,
    seen: Mutex<Vec<SeenRequest>>,
    answer: Mutex<Option<Result<String, FimError>>>,
    delay: Mutex<Duration>,
    get_calls: AtomicUsize,
    seen_gets: Mutex<Vec<SeenGet>>,
    get_script: Mutex<Vec<ScriptedGet>>,
}

/// One recorded GET request.
#[derive(Debug, Clone)]
pub struct SeenGet {
    /// Request URL.
    pub url: String,
    /// Request headers, in order.
    pub headers: Vec<(String, String)>,
    /// Timeout that was in force.
    pub timeout: Duration,
}

/// A scripted GET answer, consumed by the first call to exactly this URL.
#[derive(Debug, Clone)]
struct ScriptedGet {
    url: String,
    answer: Result<String, FimError>,
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

    /// Scripts a 2xx GET answer for exactly `url`.
    #[must_use]
    pub fn with_get_body(self, url: &str, body: &str) -> Self {
        self.script_get(url, Ok(body.to_string()));
        self
    }

    /// Scripts a failing GET answer for exactly `url`.
    #[must_use]
    pub fn with_get_failure(self, url: &str, error: FimError) -> Self {
        self.script_get(url, Err(error));
        self
    }

    fn script_get(&self, url: &str, answer: Result<String, FimError>) {
        if let Ok(mut slot) = self.get_script.lock() {
            slot.push(ScriptedGet {
                url: url.to_string(),
                answer,
            });
        }
    }

    /// Sleeps the scripted delay, so coalescing can be observed.
    fn wait_out_the_delay(&self) {
        let delay = self.delay.lock().map(|slot| *slot).unwrap_or_default();
        if !delay.is_zero() {
            std::thread::sleep(delay);
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

    /// Number of `get` calls observed.
    #[must_use]
    pub fn get_calls(&self) -> usize {
        self.get_calls.load(Ordering::SeqCst)
    }

    /// Every recorded GET request, in call order.
    #[must_use]
    pub fn get_requests(&self) -> Vec<SeenGet> {
        self.seen_gets
            .lock()
            .map(|seen| seen.clone())
            .unwrap_or_default()
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
        self.wait_out_the_delay();
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

    fn get(
        &self,
        url: &str,
        headers: &[(&str, &str)],
        timeout: Duration,
        _max_response_bytes: usize,
    ) -> Result<String, FimError> {
        self.get_calls.fetch_add(1, Ordering::SeqCst);
        self.wait_out_the_delay();
        if let Ok(mut seen) = self.seen_gets.lock() {
            seen.push(SeenGet {
                url: url.to_string(),
                headers: headers
                    .iter()
                    .map(|(name, value)| ((*name).to_string(), (*value).to_string()))
                    .collect(),
                timeout,
            });
        }
        let scripted = self.get_script.lock().ok().and_then(|mut slot| {
            let index = slot.iter().position(|entry| entry.url == url)?;
            Some(slot.remove(index))
        });
        scripted
            .map(|entry| entry.answer)
            .unwrap_or(Err(FimError::Transport {
                category: TransportCategory::Other,
            }))
    }
}
