//! Process execution, cancellation, and the public lint entry points.
//!
//! One run: resolve the binary → spawn slang with the fixed invocation → poll
//! the child (cancel flag, timeout, exit) → read and parse the JSON → convert
//! the positions → build a [`LintReport`]. Every path kills and reaps the child,
//! so a timeout or cancellation never leaves a zombie (asserted by
//! `tests/fake_binary.rs`).

use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread;
use std::time::{Duration, Instant};

use crate::binary::{
    resolve_binary_for_current_process, resolve_verible_lint_for_current_process,
    sanitize_tool_child_env, BinaryResolution,
};
use crate::diagnostic::{
    convert_diagnostic, parse_diagnostics_reader, Diagnostic, Severity, SourceLines,
};
use crate::engine::{parse_verible_output, project_command_args_for, LintEngine};
use crate::hierarchy::{project_root, run_tool_once};
use crate::process::{receive_capped, spawn_reader, terminate, termination_signal};
use crate::tool_paths::absolute_path;
use crate::{command_args_for, EngineOptions, LintFailure, LintOptions};

/// How often the child is checked for exit / cancellation / timeout.
const POLL_INTERVAL: Duration = Duration::from_millis(5);

/// One lint result: diagnostics, limits, the binary resolution and any failure.
#[derive(Debug)]
pub struct LintReport {
    /// Diagnostics kept after the [`LintOptions::max_diagnostics`] cap.
    pub diagnostics: Vec<Diagnostic>,
    /// Number of raw findings the document contained (retained + dropped).
    pub total_diagnostics: usize,
    /// Distinct findings dropped by the per-file count bound.
    pub dropped: usize,
    /// Whether [`Self::diagnostics`] was truncated by the cap.
    pub truncated: bool,
    /// Where the binary was expected / found.
    pub binary: BinaryResolution,
    /// `None` on success; a typed failure otherwise.
    pub failure: Option<LintFailure>,
    /// Process exit code, when the child exited normally.
    pub exit_code: Option<i32>,
    /// Captured stdout (capped).
    pub stdout: String,
    /// Captured stderr (capped).
    pub stderr: String,
    /// Wall-clock time of the run.
    pub elapsed: Duration,
    /// PID of the spawned lint process (diagnostics/evidence; the process is
    /// reaped before this report is returned).
    pub child_pid: Option<u32>,
    /// Which engine ran (T138: the two engines are never blended).
    pub engine: LintEngine,
    /// The tool that actually ran: `slang` or `verible-verilog-lint` (the
    /// resolved binary's file name when a binary was found).
    pub tool: String,
    /// How far the run got: `resolve`, `spawn`, `run`, `parse`, or `done`. This
    /// is the failure stage the UI shows next to the tool name (AC-2.10).
    pub stage: &'static str,
}

impl LintReport {
    /// `true` when no failure was recorded.
    #[must_use]
    pub fn is_success(&self) -> bool {
        self.failure.is_none()
    }

    /// Short sentence for the UI/status bar (AC-11).
    #[must_use]
    pub fn summary(&self) -> String {
        match &self.failure {
            Some(failure) => failure.user_message(),
            None if self.total_diagnostics == 0 => "no diagnostics".to_string(),
            None if self.dropped > 0 => format!(
                "{} diagnostic(s) ({} distinct, {} dropped)",
                self.total_diagnostics,
                self.diagnostics.len(),
                self.dropped
            ),
            None if self.diagnostics.len() != self.total_diagnostics => format!(
                "{} diagnostic(s) ({} distinct)",
                self.total_diagnostics,
                self.diagnostics.len()
            ),
            None => format!("{} diagnostic(s)", self.total_diagnostics),
        }
    }

    /// Number of error-severity diagnostics in the report.
    #[must_use]
    pub fn error_count(&self) -> usize {
        self.diagnostics
            .iter()
            .filter(|diagnostic| diagnostic.severity == Severity::Error)
            .count()
    }

    /// Number of warning-severity diagnostics in the report.
    #[must_use]
    pub fn warning_count(&self) -> usize {
        self.diagnostics
            .iter()
            .filter(|diagnostic| diagnostic.severity == Severity::Warning)
            .count()
    }
}

fn empty_report(binary: BinaryResolution, engine: LintEngine) -> LintReport {
    LintReport {
        diagnostics: Vec::new(),
        total_diagnostics: 0,
        dropped: 0,
        truncated: false,
        binary,
        failure: None,
        exit_code: None,
        stdout: String::new(),
        stderr: String::new(),
        elapsed: Duration::ZERO,
        child_pid: None,
        engine,
        tool: engine.tool_name().to_string(),
        stage: "resolve",
    }
}

/// A cancellation handle for the currently running lint.
#[derive(Clone)]
struct RunHandle {
    id: u64,
    cancel: Arc<AtomicBool>,
}

#[derive(Default)]
struct ServiceState {
    next_id: u64,
    current: Option<RunHandle>,
}

/// Runs slang with timeout, cancellation and output limits.
///
/// A new [`LintService::lint`] call cancels the previous run; two edits in quick
/// succession therefore never pile up processes.
#[derive(Clone, Default)]
pub struct LintService {
    state: Arc<Mutex<ServiceState>>,
}

impl LintService {
    /// A new service with no current run.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    fn state(&self) -> MutexGuard<'_, ServiceState> {
        self.state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    /// Cancel the run that is currently in flight, if any.
    pub fn cancel(&self) {
        let state = self.state();
        if let Some(handle) = &state.current {
            handle.cancel.store(true, Ordering::SeqCst);
        }
    }

    /// Run the default (slang) engine on `file`. Cancels any previous in-flight
    /// run.
    #[must_use]
    pub fn lint(&self, file: &Path, options: &LintOptions) -> LintReport {
        self.lint_with(file, &EngineOptions::from_options(options))
    }

    /// Run the selected engine on `file` with the project's `-I`/`-D` settings.
    /// Cancels any previous in-flight run.
    #[must_use]
    pub fn lint_with(&self, file: &Path, options: &EngineOptions) -> LintReport {
        self.lint_with_cancel(file, options, Arc::new(AtomicBool::new(false)))
    }

    /// Like [`Self::lint_with`], but sharing an EXTERNAL cancel flag: the batch
    /// pass passes its own token, so one `cancel()` kills the running child and
    /// no later file starts (AC-2.8). The flag is only ever raised by the caller.
    #[must_use]
    pub fn lint_with_cancel(
        &self,
        file: &Path,
        options: &EngineOptions,
        cancel: Arc<AtomicBool>,
    ) -> LintReport {
        let started = Instant::now();
        let id = {
            let mut state = self.state();
            state.next_id += 1;
            if let Some(previous) = &state.current {
                previous.cancel.store(true, Ordering::SeqCst);
            }
            let id = state.next_id;
            state.current = Some(RunHandle {
                id,
                cancel: Arc::clone(&cancel),
            });
            id
        };

        let mut report = Self::run_engine(file, options, &cancel);
        report.elapsed = started.elapsed();

        {
            let mut state = self.state();
            if state.current.as_ref().is_some_and(|handle| handle.id == id) {
                state.current = None;
            }
        }
        report
    }

    fn run_engine(file: &Path, options: &EngineOptions, cancel: &AtomicBool) -> LintReport {
        let binary = match options.engine {
            LintEngine::Slang => {
                resolve_binary_for_current_process(options.slang_binary.as_deref())
            }
            LintEngine::Verible => {
                resolve_verible_lint_for_current_process(options.verible_binary.as_deref())
            }
        };
        let mut report = empty_report(binary.clone(), options.engine);

        let Some(chosen) = binary.chosen.clone() else {
            report.failure = Some(LintFailure::BinaryNotFound {
                attempts: binary.attempts.clone(),
            });
            return report;
        };
        report.tool = chosen
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| options.engine.tool_name().to_string());

        let temp = match tempfile::Builder::new().prefix("vd-deeplint-").tempdir() {
            Ok(temp) => temp,
            Err(error) => {
                report.failure = Some(LintFailure::Io {
                    message: format!("creating a temporary directory failed: {error}"),
                });
                return report;
            }
        };
        let json_path = temp.path().join("diagnostics.json");

        report.stage = "spawn";
        let mut command = Command::new(&chosen);
        // Run the tool in the LINTED FILE's directory: its `--diag-json` paths
        // are relative to its own cwd (verified: an absolute argument comes back
        // as `../…/warn.sv` from a foreign cwd), so the child's cwd must be a
        // directory we KNOW and can resolve against - never the process cwd,
        // which the application does not control (a file-manager launch runs in
        // $HOME). A relative `file` argument keeps the inherited cwd and is
        // resolved against it.
        let tool_dir = match file.parent() {
            Some(parent) if !parent.as_os_str().is_empty() && parent.is_absolute() => {
                command.current_dir(parent);
                Some(parent.to_path_buf())
            }
            _ => std::env::current_dir().ok(),
        };
        // Bundled tools must not inherit the AppDir's packaged loader paths; see
        // `binary::sanitize_tool_child_env`.
        sanitize_tool_child_env(&mut command);
        command
            .args(command_args_for(
                options.engine,
                file,
                Some(&json_path),
                options,
            ))
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());

        let mut child = match command.spawn() {
            Ok(child) => child,
            Err(error) => {
                report.failure = Some(LintFailure::Spawn {
                    message: format!("{}: {error}", chosen.display()),
                });
                return report;
            }
        };
        report.child_pid = Some(child.id());

        // Capture the streams on threads and hand the result back over a channel:
        // a bounded wait below keeps a grandchild that inherited a pipe from
        // stalling the lint result (and the no-zombie guarantee only covers the
        // child we spawned).
        let stdout_reader = child.stdout.take().map(spawn_reader);
        let stderr_reader = child.stderr.take().map(spawn_reader);

        report.stage = "run";
        let wait_started = Instant::now();
        let outcome = loop {
            if cancel.load(Ordering::SeqCst) {
                break ExitOutcome::Cancelled(terminate(&mut child).err());
            }
            match child.try_wait() {
                Ok(Some(status)) => {
                    break ExitOutcome::Finished {
                        exit_code: status.code(),
                        // `code()` is `None` for BOTH a signal death and an exit
                        // without a code; the signal is the missing witness.
                        signal: termination_signal(&status),
                    };
                }
                Ok(None) => {}
                Err(error) => {
                    break ExitOutcome::WaitFailed {
                        message: format!("waiting for slang failed: {error}"),
                        cleanup: terminate(&mut child).err(),
                    };
                }
            }
            if wait_started.elapsed() >= options.timeout {
                break ExitOutcome::TimedOut(terminate(&mut child).err());
            }
            thread::sleep(POLL_INTERVAL);
        };

        let (stdout, stdout_truncated) = stdout_reader
            .as_ref()
            .map_or_else(|| (String::new(), false), receive_capped);
        let (stderr, stderr_truncated) = stderr_reader
            .as_ref()
            .map_or_else(|| (String::new(), false), receive_capped);
        report.stdout = stdout;
        report.stderr = stderr;
        if stdout_truncated || stderr_truncated {
            report
                .stderr
                .push_str("\n[output truncated by vd-deeplint]\n");
        }

        match outcome {
            ExitOutcome::Finished { exit_code, signal } => {
                report.exit_code = exit_code;
                match options.engine {
                    LintEngine::Slang => read_json(
                        &mut report,
                        &json_path,
                        file,
                        options,
                        tool_dir.as_deref(),
                        signal,
                    ),
                    LintEngine::Verible => read_verible(&mut report, file, options, signal),
                }
            }
            ExitOutcome::TimedOut(cleanup_error) => {
                append_cleanup_error(&mut report, cleanup_error);
                report.failure = Some(LintFailure::Timeout {
                    after: options.timeout,
                });
            }
            ExitOutcome::Cancelled(cleanup_error) => {
                append_cleanup_error(&mut report, cleanup_error);
                report.failure = Some(LintFailure::Cancelled);
            }
            ExitOutcome::WaitFailed { message, cleanup } => {
                append_cleanup_error(&mut report, cleanup);
                report.failure = Some(LintFailure::Io { message });
            }
        }

        report
    }
}

/// The outcome of ONE whole-project slang invocation.
///
/// The project-wide lint pass (`batch.rs`) prefers this over N per-file
/// processes: a `` `define `` in one file can only reach the next when both are
/// compiled by the same process. The batch still owns progress, de-duplication
/// and the pass cap; this layer owns the invocation, the split and the reasons a
/// split is not usable.
pub(crate) enum ProjectPass {
    /// The pass produced usable rows, each attributed to its OWN file.
    Ready(ProjectReport),
    /// The caller cancelled the pass; no verdict is published.
    Cancelled,
    /// The pass could not be used. The batch falls back to per-file linting and
    /// NAMES this reason, so a fallback is never silent.
    Unusable(String),
}

/// The usable result of one whole-project slang invocation.
pub(crate) struct ProjectReport {
    /// Every binary-resolution attempt, in the product order.
    pub resolution: BinaryResolution,
    /// One row per retained finding, paired with the project file it belongs to.
    pub rows: Vec<(std::path::PathBuf, Diagnostic)>,
    /// Raw findings the document contained (retained + dropped).
    pub total_diagnostics: usize,
    /// Distinct findings dropped by the per-file count bound (or the stream's
    /// distinct-finding bound).
    pub dropped: usize,
    /// Whether `rows` was truncated by a count bound.
    pub truncated: bool,
}

/// The file a finding must be attributed to: the first range's file (that is
/// what [`convert_diagnostic`] converts), falling back to the location string.
fn reported_file(parsed: &crate::diagnostic::ParsedDiagnostic) -> Option<&str> {
    parsed
        .ranges
        .first()
        .map(|range| range.file.as_str())
        .filter(|file| !file.is_empty())
        .or_else(|| {
            parsed
                .location
                .as_ref()
                .map(|location| location.file.as_str())
                .filter(|file| !file.is_empty())
        })
}

impl LintService {
    /// Run ONE slang process over `files` and split the findings back by the
    /// file each one names.
    ///
    /// # Why one process
    ///
    /// `slang` starts every input file as its own compilation unit unless
    /// `--single-unit` is passed (measured on 12.1.0): only the single-unit run
    /// carries a `` `define `` from an earlier file into a later one. N per-file
    /// processes therefore can never see a project's cross-file macros.
    ///
    /// # Why the caller must handle a fallback
    ///
    /// The single process has ONE cwd (the file set's first directory), while
    /// the per-file pass gave every file its own directory. That is enough to
    /// keep, but not enough to trust blindly: this method returns
    /// [`ProjectPass::Unusable`] with a named reason whenever the split cannot
    /// reproduce the per-file verdict faithfully:
    ///
    /// * the process failed, timed out or wrote no/invalid JSON;
    /// * a finding has no position, or names a file outside the project set
    ///   (`location.file` is the only attribution the document carries, so
    ///   guessing a file would publish a row under the wrong file);
    /// * a row's source file cannot be read to convert its position.
    ///
    /// Relative `` `include `` resolution is NOT such a case: slang resolves it
    /// against the including file's directory (measured on 12.1.0, independent
    /// of the child's cwd), so the set pass and the per-file pass agree.
    ///
    /// The batch then lints per file and records the reason (never a silent
    /// fallback, never a false verdict).
    pub(crate) fn lint_project(
        &self,
        files: &[std::path::PathBuf],
        options: &EngineOptions,
        cancel: Arc<AtomicBool>,
    ) -> ProjectPass {
        debug_assert_eq!(
            options.engine,
            LintEngine::Slang,
            "only slang has a one-process project invocation"
        );
        let temp = match tempfile::Builder::new()
            .prefix("vd-deeplint-project-")
            .tempdir()
        {
            Ok(temp) => temp,
            Err(error) => {
                return ProjectPass::Unusable(format!(
                    "creating a temporary directory failed: {error}"
                ));
            }
        };
        let json_path = temp.path().join("diagnostics.json");
        let argv = project_command_args_for(files, Some(&json_path), options);
        // A whole-set run is bounded by the per-file budget times the file
        // count: the same worst case the per-file pass had, and a project of
        // N files cannot be expected to finish inside one file's budget.
        let files_bound = u32::try_from(files.len()).unwrap_or(u32::MAX).max(1);
        let timeout = options.timeout.saturating_mul(files_bound);
        let run = run_tool_once(
            files,
            options.slang_binary.as_deref(),
            &argv,
            timeout,
            Some(&cancel),
        );
        if matches!(run.failure, Some(LintFailure::Cancelled)) {
            return ProjectPass::Cancelled;
        }
        if let Some(failure) = run.failure {
            return ProjectPass::Unusable(format!(
                "the whole-project {} run failed: {}",
                run.tool,
                failure.user_message()
            ));
        }
        let base = project_root(files);
        let clean = ProjectReport {
            resolution: run.resolution.clone(),
            rows: Vec::new(),
            total_diagnostics: 0,
            dropped: 0,
            truncated: false,
        };
        match fs::metadata(&json_path) {
            Ok(metadata) if metadata.len() > 0 => {}
            // A zero-byte report carries no findings; only a failed process
            // makes that a degraded verdict.
            Ok(_) if run.exit_code == Some(0) => return ProjectPass::Ready(clean),
            Ok(_) => {
                return ProjectPass::Unusable(
                    LintFailure::MissingOutput {
                        exit_code: run.exit_code,
                        signal: run.signal,
                        stderr: run.stderr.clone(),
                    }
                    .user_message(),
                );
            }
            Err(_) if run.exit_code == Some(0) => return ProjectPass::Ready(clean),
            Err(_) => {
                return ProjectPass::Unusable(
                    LintFailure::MissingOutput {
                        exit_code: run.exit_code,
                        signal: run.signal,
                        stderr: run.stderr.clone(),
                    }
                    .user_message(),
                );
            }
        }

        // The document is STREAMED, and the bound is the pass bound: the batch
        // applies the per-file caps after the split (see below), so reading with
        // the per-file cap here would drop OTHER files' rows before they are
        // attributed.
        let max_pass = if options.max_pass_diagnostics == 0 {
            crate::DEFAULT_MAX_PASS_DIAGNOSTICS
        } else {
            options.max_pass_diagnostics
        };
        let stream = match fs::File::open(&json_path)
            .map_err(|error| error.to_string())
            .and_then(|handle| {
                parse_diagnostics_reader(handle, max_pass).map_err(|error| error.to_string())
            }) {
            Ok(stream) => stream,
            Err(error) => {
                return ProjectPass::Unusable(format!(
                    "the whole-project diagnostics JSON could not be read: {error}"
                ));
            }
        };

        let mut buckets: Vec<Vec<crate::diagnostic::ParsedDiagnostic>> =
            (0..files.len()).map(|_| Vec::new()).collect();
        let mut index_of: std::collections::HashMap<std::path::PathBuf, usize> =
            std::collections::HashMap::new();
        for (index, file) in files.iter().enumerate() {
            index_of.entry(absolute_path(file, &base)).or_insert(index);
        }
        for parsed in stream.diagnostics {
            let Some(reported) = reported_file(&parsed) else {
                return ProjectPass::Unusable(
                    "a finding without a position cannot be attributed to a project file, so the whole-project pass cannot be split"
                        .to_string(),
                );
            };
            let resolved = absolute_path(Path::new(reported), &base);
            let Some(&index) = index_of.get(&resolved) else {
                return ProjectPass::Unusable(format!(
                    "the whole-project pass reported a finding in `{}`, which is not in the project file set",
                    resolved.display()
                ));
            };
            buckets[index].push(parsed);
        }
        let mut rows: Vec<(std::path::PathBuf, Diagnostic)> = Vec::new();
        let mut dropped = stream.dropped;
        let mut truncated = stream.dropped > 0;
        for (index, bucket) in buckets.into_iter().enumerate() {
            if bucket.is_empty() {
                continue;
            }
            let file = &files[index];
            let resolved = absolute_path(file, &base);
            let source = match SourceLines::from_path(&resolved) {
                Ok(source) => source,
                Err(crate::ConvertError::InvalidUtf8) => {
                    return ProjectPass::Unusable(format!(
                        "{} is not valid UTF-8, so its positions cannot be converted",
                        resolved.display()
                    ));
                }
                Err(error) => {
                    return ProjectPass::Unusable(format!(
                        "reading {} failed: {error}",
                        resolved.display()
                    ));
                }
            };
            let mut kept = 0usize;
            for parsed in bucket {
                if kept >= options.max_diagnostics {
                    dropped += parsed.repeat;
                    truncated = true;
                    continue;
                }
                kept += 1;
                rows.push((
                    file.clone(),
                    convert_diagnostic(parsed, &source, Some(&base)),
                ));
            }
        }
        ProjectPass::Ready(ProjectReport {
            resolution: run.resolution,
            rows,
            total_diagnostics: stream.total,
            dropped,
            truncated,
        })
    }
}

/// Convenience wrapper for a single run of the default (slang) engine.
#[must_use]
pub fn lint(file: &Path, options: &LintOptions) -> LintReport {
    LintService::new().lint(file, options)
}

/// Convenience wrapper for a single run of the selected engine.
#[must_use]
pub fn lint_with(file: &Path, options: &EngineOptions) -> LintReport {
    LintService::new().lint_with(file, options)
}

enum ExitOutcome {
    Finished {
        exit_code: Option<i32>,
        /// Unix signal that ended the child, when it did not exit on its own.
        signal: Option<i32>,
    },
    TimedOut(Option<String>),
    Cancelled(Option<String>),
    WaitFailed {
        message: String,
        cleanup: Option<String>,
    },
}

fn append_cleanup_error(report: &mut LintReport, cleanup_error: Option<String>) {
    let Some(error) = cleanup_error else {
        return;
    };
    if !report.stderr.is_empty() {
        report.stderr.push('\n');
    }
    report.stderr.push_str(&format!("[vd-deeplint] {error}\n"));
}

/// Parse `verible-verilog-lint`'s text findings (T138).
///
/// Verible writes one `path:line:col[-end]: message [Category] [rule]` line per
/// finding, but not always to the same stream: *lint-rule* findings go to
/// stderr, *syntax errors* go to stdout (measured with
/// v0.0-4298-g085eef05 — `logic a b;` produces two `syntax error at token …`
/// lines on stdout and an empty stderr). Both streams are parsed. The tool exits
/// `1` when it found any — a nonzero exit *with* findings is a normal result,
/// exactly like slang's.
fn read_verible(
    report: &mut LintReport,
    file: &Path,
    options: &EngineOptions,
    signal: Option<i32>,
) {
    report.stage = "parse";
    let source = SourceLines::from_path(file);
    let source = match source {
        Ok(source) => Some(source),
        Err(crate::ConvertError::InvalidUtf8) => {
            report.failure = Some(LintFailure::InvalidSourceEncoding {
                path: file.to_path_buf(),
            });
            return;
        }
        Err(_) => None,
    };
    let mut parsed = parse_verible_output(&report.stdout, source.as_ref());
    parsed.extend(parse_verible_output(&report.stderr, source.as_ref()));
    report.total_diagnostics = parsed.len();
    if parsed.is_empty() {
        if report.exit_code != Some(0) {
            let mut captured = report.stdout.clone();
            if !captured.is_empty() && !report.stderr.is_empty() {
                captured.push('\n');
            }
            captured.push_str(&report.stderr);
            report.failure = Some(LintFailure::MissingOutput {
                exit_code: report.exit_code,
                signal,
                stderr: captured,
            });
        }
        report.stage = "done";
        return;
    }
    report.truncated = parsed.len() > options.max_diagnostics;
    report.diagnostics = parsed.into_iter().take(options.max_diagnostics).collect();
    report.stage = "done";
}

fn read_json(
    report: &mut LintReport,
    json_path: &Path,
    file: &Path,
    options: &EngineOptions,
    tool_dir: Option<&Path>,
    signal: Option<i32>,
) {
    match fs::metadata(json_path) {
        Ok(_) => {}
        Err(_) => {
            if report.exit_code == Some(0) {
                // slang exited successfully but wrote no report: treat as a
                // clean file rather than an error.
                return;
            }
            // A crashed or timed-out engine leaves no report: the only witness
            // is how the child terminated plus its own stderr. The temp path
            // that could not be stat'ed is an implementation detail, never a
            // user-visible reason.
            report.failure = Some(LintFailure::MissingOutput {
                exit_code: report.exit_code,
                signal,
                stderr: report.stderr.clone(),
            });
            return;
        }
    }
    // The document is STREAMED: no byte cap and no whole-string read. Retention
    // is bounded by `max_diagnostics` DISTINCT findings (repeats are collapsed
    // into a count), so a project-scale warning flood cannot fail the file.
    let handle = match fs::File::open(json_path) {
        Ok(handle) => handle,
        Err(error) => {
            // The report path is a per-run temp file: its name is an
            // implementation detail, never a user-visible reason.
            report.failure = Some(LintFailure::InvalidJson {
                message: format!("reading the lint report failed: {error}"),
                stderr: report.stderr.clone(),
            });
            return;
        }
    };
    let stream = match crate::diagnostic::parse_diagnostics_reader(
        std::io::BufReader::new(handle),
        options.max_diagnostics,
    ) {
        Ok(stream) => stream,
        Err(error) => {
            report.failure = Some(LintFailure::InvalidJson {
                message: error.0,
                stderr: report.stderr.clone(),
            });
            return;
        }
    };
    report.total_diagnostics = stream.total;
    report.dropped = stream.dropped;
    report.truncated = stream.dropped > 0;
    if stream.total == 0 {
        if report.exit_code != Some(0) {
            report.failure = Some(LintFailure::MissingOutput {
                exit_code: report.exit_code,
                signal,
                stderr: report.stderr.clone(),
            });
        }
        report.stage = "done";
        return;
    }
    let source = match SourceLines::from_path(file) {
        Ok(source) => source,
        Err(crate::ConvertError::InvalidUtf8) => {
            report.failure = Some(LintFailure::InvalidSourceEncoding {
                path: file.to_path_buf(),
            });
            return;
        }
        Err(other) => {
            report.failure = Some(LintFailure::Io {
                message: other.to_string(),
            });
            return;
        }
    };
    report.diagnostics = stream
        .diagnostics
        .into_iter()
        .map(|parsed| convert_diagnostic(parsed, &source, tool_dir))
        .collect();
    report.stage = "done";
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::process::read_capped;

    #[test]
    fn stream_capture_truncates_at_the_limit() {
        let input: Vec<u8> = (0..1000u32).map(|value| (value % 251) as u8).collect();
        let (captured, truncated) = read_capped(input.as_slice(), 128);
        assert!(truncated);
        assert_eq!(captured.len(), 128);
    }

    #[test]
    fn std_summary_messages_are_short() {
        let report = empty_report(
            BinaryResolution {
                chosen: None,
                attempts: Vec::new(),
            },
            LintEngine::Slang,
        );
        assert_eq!(report.summary(), "no diagnostics");
        assert!(report.is_success());
    }

    #[test]
    fn failure_user_messages_are_actionable() {
        let failure = LintFailure::Timeout {
            after: Duration::from_millis(250),
        };
        assert!(failure.user_message().contains("250"));
        assert!(
            failure.user_message().contains("seconds"),
            "a timeout is phrased in seconds for the user: {}",
            failure.user_message()
        );
        let failure = LintFailure::BinaryNotFound {
            attempts: Vec::new(),
        };
        assert!(failure.user_message().contains("VEDIT_SLANG"));
    }
}
