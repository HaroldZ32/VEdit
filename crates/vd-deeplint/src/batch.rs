//! A project-wide lint pass: job + progress + real cancellation (T138).
//!
//! One batch runs the selected engine over the project's file set on a worker
//! thread, one file at a time (the tools are heavy; parallel instances would
//! compete for the same machine and make progress meaningless). The caller never
//! blocks:
//!
//! * [`BatchHandle::start`] returns immediately;
//! * [`BatchHandle::progress`] is a cheap snapshot (`done` / `total` / the current
//!   file / the phase) for the bottom progress bar;
//! * [`BatchHandle::cancel`] sets the cancel flag **and** the running child is
//!   killed by the runner, so cancelling really stops the pass (AC-2.8): no file
//!   starts after the flag is seen, and the report delivered at cancellation
//!   contains only what finished before it;
//! * [`BatchHandle::take_report`] hands the finished result over exactly once.
//!
//! Unrecoverable failures (no binary at all, the process cannot be spawned) end
//! the pass with [`BatchPhase::Failed`] and the recorded stage/tool; a single
//! file that times out is recorded and the pass continues, so one pathological
//! file cannot make the whole project un-lintable.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::JoinHandle;
use std::time::Instant;

use crate::diagnostic::Diagnostic;
use crate::engine::LintEngine;
use crate::service::LintService;
use crate::{EngineOptions, LintFailure};

/// Lifecycle of one project-wide pass.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BatchPhase {
    /// Accepted, the worker has not started the first file yet.
    Queued,
    /// At least one file was handed to the engine.
    Running,
    /// Every file was linted.
    Finished,
    /// Cancelled by the caller; no file was started after the cancel.
    Cancelled,
    /// An unrecoverable failure ended the pass.
    Failed,
}

impl BatchPhase {
    /// Stable code for the C ABI: 0 queued, 1 running, 2 finished, 3 cancelled,
    /// 4 failed.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Queued => 0,
            Self::Running => 1,
            Self::Finished => 2,
            Self::Cancelled => 3,
            Self::Failed => 4,
        }
    }

    /// Stable name for logs and the status line.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Queued => "queued",
            Self::Running => "running",
            Self::Finished => "finished",
            Self::Cancelled => "cancelled",
            Self::Failed => "failed",
        }
    }

    /// True once the pass reached a terminal state.
    #[must_use]
    pub const fn is_terminal(self) -> bool {
        matches!(self, Self::Finished | Self::Cancelled | Self::Failed)
    }
}

/// A cheap snapshot of a running pass (the progress bar reads this).
#[derive(Debug, Clone)]
pub struct BatchProgress {
    /// Lifecycle phase.
    pub phase: BatchPhase,
    /// Files already linted (finished or failed individually).
    pub done: usize,
    /// Files in the pass's snapshot of the project set.
    pub total: usize,
    /// The file the engine is working on right now.
    pub current: Option<PathBuf>,
    /// Whether the caller asked for a cancel.
    pub cancel_requested: bool,
    /// The failure stage (`resolve`, `spawn`, `run`, `parse`) once the pass
    /// failed (AC-2.10).
    pub stage: Option<&'static str>,
    /// One-line state for the status bar.
    pub message: String,
}

impl BatchProgress {
    /// `N/M` as the UI prints it.
    #[must_use]
    pub fn counter(&self) -> String {
        format!("{}/{}", self.done, self.total)
    }
}

/// One file whose individual run failed (the pass continued).
#[derive(Debug, Clone)]
pub struct BatchFileFailure {
    /// The file that failed.
    pub file: PathBuf,
    /// Why, as the UI shows it.
    pub message: String,
    /// How far the run got (`resolve`, `spawn`, `run`, `parse`).
    pub stage: &'static str,
    /// The tool that ran (`slang` / `verible-verilog-lint`).
    pub tool: String,
}

/// One diagnostic plus the file it belongs to.
#[derive(Debug, Clone)]
pub struct BatchDiagnostic {
    /// The linted file.
    pub file: PathBuf,
    /// The diagnostic, in the editor's position model.
    pub diagnostic: Diagnostic,
}

/// The finished result of one pass.
#[derive(Debug, Clone)]
pub struct BatchReport {
    /// The engine the pass used.
    pub engine: LintEngine,
    /// The tool that ran (the resolved binary's name when there was one).
    pub tool: String,
    /// Terminal phase.
    pub phase: BatchPhase,
    /// Diagnostics of the files that ran, in file order, one row per DISTINCT
    /// finding (`diagnostic.repeat` carries how many raw findings it stands for).
    pub diagnostics: Vec<BatchDiagnostic>,
    /// Raw findings in the files that ran (retained + collapsed + dropped).
    pub total_diagnostics: usize,
    /// Raw findings folded into another row by the pass de-duplication.
    pub collapsed: usize,
    /// Distinct findings the pass did not keep (per-file or pass count bound).
    pub dropped: usize,
    /// Whether any file's diagnostics were truncated by a count bound.
    pub truncated: bool,
    /// How many files were linted.
    pub files_linted: usize,
    /// Unrecoverable failure that ended the pass, when there was one.
    pub failure: Option<LintFailure>,
    /// Per-file failures that did not stop the pass.
    pub failures: Vec<BatchFileFailure>,
    /// Completion counter at the end.
    pub done: usize,
    /// Snapshot size.
    pub total: usize,
    /// Wall-clock time of the pass.
    pub elapsed: std::time::Duration,
}

impl BatchReport {
    /// One-line summary for the status bar (AC-2.9: `cancelled — N/M files`).
    #[must_use]
    pub fn summary(&self) -> String {
        match self.phase {
            BatchPhase::Cancelled => format!(
                "cancelled — {}/{} files, {} diagnostic(s)",
                self.done,
                self.total,
                self.diagnostics.len()
            ),
            BatchPhase::Failed => {
                let reason = self
                    .failure
                    .as_ref()
                    .map_or_else(|| "unknown failure".to_string(), LintFailure::user_message);
                format!(
                    "failed — {}/{} files; {}: {}",
                    self.done, self.total, self.tool, reason
                )
            }
            _ => {
                let mut line = format!(
                    "{} — {}/{} files, {} distinct diagnostic(s)",
                    self.tool,
                    self.done,
                    self.total,
                    self.diagnostics.len(),
                );
                if self.collapsed > 0 {
                    line.push_str(&format!(", {} repeat(s) collapsed", self.collapsed));
                }
                if self.dropped > 0 {
                    // Exact when every file answered (the parser COUNTS the
                    // elements it did not keep); `at least` when files failed
                    // and their diagnostic volume is unknown.
                    let prefix = if self.failures.is_empty() {
                        ""
                    } else {
                        "at least "
                    };
                    line.push_str(&format!(", {prefix}{} dropped", self.dropped));
                }
                if !self.failures.is_empty() {
                    // A file that could not be processed must be NAMED with its
                    // reason: silently losing a file from the pass is exactly
                    // the failure mode the byte cap used to hide.
                    let shown: Vec<String> = self
                        .failures
                        .iter()
                        .take(2)
                        .map(|failure| format!("{} ({})", failure.file.display(), failure.message))
                        .collect();
                    let more = self.failures.len().saturating_sub(shown.len());
                    line.push_str(&format!(
                        ", {} file(s) failed: {}{}",
                        self.failures.len(),
                        shown.join("; "),
                        if more > 0 {
                            format!("; and {more} more")
                        } else {
                            String::new()
                        }
                    ));
                }
                line
            }
        }
    }
}

struct BatchInner {
    progress: Mutex<BatchProgress>,
    /// Shared with the per-file runner, so one `cancel()` reaches the live child.
    cancel: Arc<AtomicBool>,
    report: Mutex<Option<BatchReport>>,
}

impl BatchInner {
    fn lock_progress(&self) -> MutexGuard<'_, BatchProgress> {
        self.progress
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn lock_report(&self) -> MutexGuard<'_, Option<BatchReport>> {
        self.report
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn set(&self, phase: BatchPhase, done: usize, current: Option<PathBuf>, message: String) {
        let mut progress = self.lock_progress();
        progress.phase = phase;
        progress.done = done;
        progress.current = current;
        progress.message = message;
    }
}

/// A running project-wide pass.
///
/// Dropping the handle cancels it (the worker kills its child and exits); the
/// worker thread is detached at that point, so a `drop` on the UI thread never
/// waits for a process.
pub struct BatchHandle {
    inner: Arc<BatchInner>,
    worker: Mutex<Option<JoinHandle<()>>>,
    engine: LintEngine,
}

impl BatchHandle {
    /// Start a pass over `files` with `options`; returns immediately.
    ///
    /// # Errors
    ///
    /// Only when the worker thread cannot be spawned; a missing binary is
    /// reported through the report (phase [`BatchPhase::Failed`]).
    pub fn start(files: Vec<PathBuf>, options: EngineOptions) -> Result<Self, LintFailure> {
        let total = files.len();
        let options_engine = options.engine;
        let inner = Arc::new(BatchInner {
            progress: Mutex::new(BatchProgress {
                phase: BatchPhase::Queued,
                done: 0,
                total,
                current: None,
                cancel_requested: false,
                stage: None,
                message: format!("queued: {total} file(s)"),
            }),
            cancel: Arc::new(AtomicBool::new(false)),
            report: Mutex::new(None),
        });
        let worker_inner = Arc::clone(&inner);
        let worker = std::thread::Builder::new()
            .name("vd-deeplint-batch".to_string())
            .spawn(move || run_batch(&worker_inner, files, options))
            .map_err(|error| LintFailure::Io {
                message: format!("cannot spawn the project lint worker: {error}"),
            })?;
        Ok(Self {
            inner,
            worker: Mutex::new(Some(worker)),
            engine: options_engine,
        })
    }

    /// A cheap snapshot; never blocks on the engine.
    #[must_use]
    pub fn progress(&self) -> BatchProgress {
        self.inner.lock_progress().clone()
    }

    /// Ask the pass to stop. The running child is killed by the worker, and no
    /// later file starts.
    pub fn cancel(&self) {
        self.inner.cancel.store(true, Ordering::SeqCst);
        let mut progress = self.inner.lock_progress();
        progress.cancel_requested = true;
        if !progress.phase.is_terminal() {
            progress.phase = BatchPhase::Cancelled;
            progress.message = format!("cancelling — {}", progress.counter());
        }
    }

    /// Whether the caller already asked for a cancel.
    #[must_use]
    pub fn is_cancel_requested(&self) -> bool {
        self.inner.cancel.load(Ordering::SeqCst)
    }

    /// The engine this pass runs.
    #[must_use]
    pub fn engine(&self) -> LintEngine {
        self.engine
    }

    /// The tool name the pass reports (`slang` / `verible-verilog-lint`).
    #[must_use]
    pub fn tool_name(&self) -> &'static str {
        self.engine.tool_name()
    }

    /// Take the finished report exactly once (`None` while the pass is running
    /// or after it was taken).
    #[must_use]
    pub fn take_report(&self) -> Option<BatchReport> {
        self.inner.lock_report().take()
    }

    /// Wait for the worker and return the report (tests, shutdown paths).
    pub fn wait(&self) -> Option<BatchReport> {
        let worker = {
            let mut slot = self
                .worker
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            slot.take()
        };
        if let Some(worker) = worker {
            let _ = worker.join();
        }
        self.take_report()
    }
}

impl Drop for BatchHandle {
    fn drop(&mut self) {
        self.cancel();
    }
}

fn run_batch(inner: &Arc<BatchInner>, files: Vec<PathBuf>, options: EngineOptions) {
    let started = Instant::now();
    let service = LintService::new();
    let mut diagnostics: Vec<BatchDiagnostic> = Vec::new();
    // Pass-level de-duplication: one row per distinct finding, `repeat` counting
    // how often it occurred. Two different findings at the same position stay
    // separate because the code and the message are part of the key.
    let mut seen: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut failures: Vec<BatchFileFailure> = Vec::new();
    let mut total_diagnostics = 0usize;
    let mut collapsed = 0usize;
    let mut dropped = 0usize;
    let mut truncated = false;
    let mut unrecoverable: Option<LintFailure> = None;
    let mut done = 0usize;
    let max_pass = if options.max_pass_diagnostics == 0 {
        crate::DEFAULT_MAX_PASS_DIAGNOSTICS
    } else {
        options.max_pass_diagnostics
    };

    inner.set(
        BatchPhase::Running,
        0,
        files.first().cloned(),
        format!("running: 0/{}", files.len()),
    );

    for file in &files {
        if inner.cancel.load(Ordering::SeqCst) {
            break;
        }
        inner.set(
            BatchPhase::Running,
            done,
            Some(file.clone()),
            format!("linting {} ({}/{})", file.display(), done + 1, files.len()),
        );
        let report = service.lint_with_cancel(file, &options, Arc::clone(&inner.cancel));
        if let Some(failure) = &report.failure {
            if matches!(
                failure,
                LintFailure::BinaryNotFound { .. } | LintFailure::Spawn { .. }
            ) {
                let (stage, tool) = (report.stage, report.tool.clone());
                {
                    let mut progress = inner.lock_progress();
                    progress.stage = Some(stage);
                }
                inner.set(
                    BatchPhase::Failed,
                    done,
                    Some(file.clone()),
                    format!("failed at {stage} ({tool}): {}", failure.user_message()),
                );
                unrecoverable = Some(LintFailure::Spawn {
                    message: format!(
                        "{} at {} while linting {}: {}",
                        tool,
                        stage,
                        file.display(),
                        failure.user_message()
                    ),
                });
                break;
            }
            if matches!(failure, LintFailure::Cancelled) {
                // Our own cancel reached the runner: stop without recording the
                // file as failed.
                break;
            }
            failures.push(BatchFileFailure {
                file: file.clone(),
                message: failure.user_message(),
                stage: report.stage,
                tool: report.tool.clone(),
            });
            done += 1;
            continue;
        }
        total_diagnostics += report.total_diagnostics;
        dropped += report.dropped;
        truncated |= report.truncated;
        for diagnostic in report.diagnostics {
            let key = batch_dedupe_key(file, &diagnostic);
            if let Some(&retained) = seen.get(&key) {
                diagnostics[retained].diagnostic.repeat += diagnostic.repeat;
                collapsed += diagnostic.repeat;
            } else if diagnostics.len() < max_pass {
                seen.insert(key, diagnostics.len());
                // The per-file parser already collapses repetitions, so a new
                // row may stand for more than one raw finding.
                collapsed += diagnostic.repeat.saturating_sub(1);
                diagnostics.push(BatchDiagnostic {
                    file: file.clone(),
                    diagnostic,
                });
            } else {
                dropped += diagnostic.repeat;
            }
        }
        done += 1;
    }

    let cancelled = inner.cancel.load(Ordering::SeqCst);
    let phase = if unrecoverable.is_some() {
        BatchPhase::Failed
    } else if cancelled {
        BatchPhase::Cancelled
    } else {
        BatchPhase::Finished
    };
    let failure = unrecoverable;
    let report = BatchReport {
        engine: options.engine,
        tool: options.engine.tool_name().to_string(),
        phase,
        diagnostics,
        total_diagnostics,
        collapsed,
        dropped,
        truncated,
        files_linted: done,
        failure,
        failures,
        done,
        total: files.len(),
        elapsed: started.elapsed(),
    };
    inner.set(
        phase,
        done,
        None,
        if phase == BatchPhase::Cancelled {
            format!("cancelled — {done}/{}", files.len())
        } else {
            report.summary()
        },
    );
    if let Ok(mut slot) = inner.report.lock() {
        *slot = Some(report);
    }
}

/// The pass de-duplication identity: the file the finding is reported in, its
/// position, and the rule/message pair. The DIAGNOSTIC's own location file is
/// used when it has one (a finding can be in an include), else the linted file.
fn batch_dedupe_key(file: &std::path::Path, diagnostic: &Diagnostic) -> String {
    let reported_file = diagnostic
        .ranges
        .first()
        .map(|range| range.file.clone())
        .unwrap_or_else(|| file.display().to_string());
    let (line, column) = diagnostic
        .editor_range
        .as_ref()
        .map(|range| (range.start.line, range.start.character))
        .unwrap_or((0, 0));
    format!(
        "{reported_file}\u{1f}{line}\u{1f}{column}\u{1f}{}\u{1f}{}",
        diagnostic.code, diagnostic.message
    )
}

#[cfg(test)]
mod tests {
    use super::{BatchHandle, BatchPhase};
    use crate::{EngineOptions, LintEngine};
    use std::path::PathBuf;
    use std::time::{Duration, Instant};

    #[test]
    fn phase_codes_and_terminals() {
        assert_eq!(BatchPhase::Queued.code(), 0);
        assert_eq!(BatchPhase::Running.code(), 1);
        assert_eq!(BatchPhase::Finished.code(), 2);
        assert_eq!(BatchPhase::Cancelled.code(), 3);
        assert_eq!(BatchPhase::Failed.code(), 4);
        assert!(!BatchPhase::Running.is_terminal());
        assert!(BatchPhase::Cancelled.is_terminal());
    }

    #[test]
    fn a_missing_binary_fails_the_pass_with_its_stage_and_tool() {
        let files = vec![PathBuf::from("/tmp/does-not-exist.sv")];
        let options = EngineOptions {
            engine: LintEngine::Slang,
            slang_binary: Some(PathBuf::from("/nonexistent/slang-binary")),
            timeout: Duration::from_millis(500),
            ..EngineOptions::default()
        };
        let handle = BatchHandle::start(files, options)
            .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
        let deadline = Instant::now() + Duration::from_secs(5);
        let report = loop {
            if let Some(report) = handle.take_report() {
                break report;
            }
            assert!(Instant::now() < deadline, "batch did not finish");
            std::thread::sleep(Duration::from_millis(10));
        };
        assert_eq!(report.phase, BatchPhase::Failed);
        assert_eq!(report.done, 0);
        assert!(report.failure.is_some());
        assert!(report.summary().contains("failed"));
    }
}
