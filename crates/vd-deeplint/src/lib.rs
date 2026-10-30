//! SLANG deep-lint execution and diagnostic model (T17).
//!
//! This crate does exactly three things: it **starts the slang process**, it
//! **parses the `--diag-json` output**, and it **maps the diagnostics into the
//! editor's problem model**. There is no SystemVerilog front end, parser or
//! linter here — all diagnostic semantics come from slang.
//!
//! # Invocation
//!
//! ```text
//! slang --lint-only -Weverything --diag-column-unit byte --diag-json <tmp.json> <file>
//! ```
//!
//! `-Wall` is not a slang option; individual checks use `-W<name>`, and
//! `-Weverything` enables all of them.
//!
//! `--diag-column-unit byte` is **required for correct CJK mapping** and is the
//! reason it is part of the fixed invocation: slang's `--diag-json` defaults to
//! `display` columns (UTF-8 display width with tab expansion — documented in
//! slang's `Driver.cpp`: `options.diagColumnUnit.value_or(ColumnUnit::Display)`).
//! Those columns cannot be converted back to text offsets (a tab counts as up to
//! eight columns, an emoji as one), so this crate asks for **byte** columns and
//! converts them to the editor's UTF-16 positions itself. The measured
//! byte/display comparison is in `evidence/deeplint/position-semantics.txt`.
//!
//! # Failure model (AC-11)
//!
//! Every degraded situation has a typed [`LintFailure`] and a short
//! [`LintFailure::user_message`] for the UI: binary not found (with the full
//! attempt list), spawn failure, timeout, cancellation, a nonzero exit without
//! JSON, malformed JSON, an oversized JSON file, and a non-UTF-8 source file.
//! A nonzero exit **with** parseable JSON is a normal result (slang exits `1`
//! when it reported errors).
//!
//! # Cancellation and leftovers
//!
//! [`LintService`] cancels the previous run when a new one starts, and also
//! offers an explicit [`LintService::cancel`]. Every exit path kills and `wait`s
//! the child, so no zombie is left behind (asserted by the runtime tests).
#![forbid(unsafe_code)]

mod binary;
mod diagnostic;
mod service;

pub use binary::{
    resolve_binary, resolve_binary_for_current_process, AttemptOutcome, AttemptSource,
    BinaryAttempt, BinaryResolution,
};
pub use diagnostic::{
    ConvertError, Diagnostic, EditorPosition, EditorRange, RawLocation, RawRange, RawRangePosition,
    Severity, SourceLines,
};
pub use service::{lint, LintReport, LintService};

use std::fmt;
use std::path::PathBuf;
use std::time::Duration;

/// Default timeout for one slang run.
pub const DEFAULT_TIMEOUT: Duration = Duration::from_secs(10);
/// Default cap for the `--diag-json` file.
pub const DEFAULT_MAX_JSON_BYTES: usize = 8 * 1024 * 1024;
/// Default cap for the number of diagnostics returned to the UI.
pub const DEFAULT_MAX_DIAGNOSTICS: usize = 500;

/// Options for one lint run.
#[derive(Debug, Clone)]
pub struct LintOptions {
    /// Wall-clock budget for the slang process.
    pub timeout: Duration,
    /// Maximum size of the `--diag-json` file that will be parsed.
    pub max_json_bytes: usize,
    /// Maximum number of diagnostics kept in [`LintReport::diagnostics`]; the
    /// total count is still reported.
    pub max_diagnostics: usize,
    /// Explicit binary override (tests, packaging). When `None`, the binary is
    /// resolved from `VEDIT_SLANG` and package-relative paths — never `PATH`.
    pub binary: Option<PathBuf>,
}

impl Default for LintOptions {
    fn default() -> Self {
        Self {
            timeout: DEFAULT_TIMEOUT,
            max_json_bytes: DEFAULT_MAX_JSON_BYTES,
            max_diagnostics: DEFAULT_MAX_DIAGNOSTICS,
            binary: None,
        }
    }
}

/// A degraded lint result. Each variant carries a short user-facing phrase.
#[derive(Debug)]
pub enum LintFailure {
    /// No slang binary was found; every attempt is listed.
    BinaryNotFound { attempts: Vec<BinaryAttempt> },
    /// The binary exists but the process could not start.
    Spawn { message: String },
    /// The process exceeded the timeout and was killed.
    Timeout { after: Duration },
    /// A newer lint run cancelled this one.
    Cancelled,
    /// The process exited without producing a usable JSON file.
    MissingOutput {
        exit_code: Option<i32>,
        stderr: String,
    },
    /// The JSON file could not be parsed.
    InvalidJson { message: String, stderr: String },
    /// The JSON file exceeded [`LintOptions::max_json_bytes`].
    OutputTooLarge { limit: usize },
    /// The linted file is not valid UTF-8, so positions cannot be mapped.
    InvalidSourceEncoding { path: PathBuf },
    /// Any other I/O problem.
    Io { message: String },
}

impl LintFailure {
    /// Short sentence the UI can show for AC-11.
    #[must_use]
    pub fn user_message(&self) -> String {
        match self {
            Self::BinaryNotFound { attempts } => format!(
                "slang binary not found (tried {} location(s); set VEDIT_SLANG)",
                attempts.len()
            ),
            Self::Spawn { message } => format!("slang could not start: {message}"),
            Self::Timeout { after } => {
                format!("slang timed out after {} ms", after.as_millis())
            }
            Self::Cancelled => "lint cancelled".to_string(),
            Self::MissingOutput { exit_code, .. } => match exit_code {
                Some(code) => format!("slang exited with code {code} and produced no diagnostics"),
                None => "slang terminated without a result".to_string(),
            },
            Self::InvalidJson { .. } => "slang produced malformed diagnostics".to_string(),
            Self::OutputTooLarge { limit } => {
                format!("slang diagnostics exceeded the {limit} byte limit")
            }
            Self::InvalidSourceEncoding { path } => {
                format!("{} is not valid UTF-8", path.display())
            }
            Self::Io { message } => format!("lint I/O error: {message}"),
        }
    }
}

impl fmt::Display for LintFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::BinaryNotFound { attempts } => {
                writeln!(f, "slang binary not found; attempts:")?;
                for attempt in attempts {
                    writeln!(f, "  {}", attempt.describe())?;
                }
                Ok(())
            }
            Self::Spawn { message } => write!(f, "spawn failed: {message}"),
            Self::Timeout { after } => write!(f, "timed out after {after:?}"),
            Self::Cancelled => f.write_str("cancelled"),
            Self::MissingOutput { exit_code, stderr } => write!(
                f,
                "no JSON output (exit {exit_code:?}); stderr: {}",
                stderr.lines().next().unwrap_or("")
            ),
            Self::InvalidJson { message, .. } => write!(f, "invalid diagnostics JSON: {message}"),
            Self::OutputTooLarge { limit } => {
                write!(f, "diagnostics JSON exceeds {limit} bytes")
            }
            Self::InvalidSourceEncoding { path } => {
                write!(f, "{} is not valid UTF-8", path.display())
            }
            Self::Io { message } => write!(f, "I/O error: {message}"),
        }
    }
}

impl std::error::Error for LintFailure {}
