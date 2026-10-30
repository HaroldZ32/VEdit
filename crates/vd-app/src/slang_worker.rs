//! Background SLANG deep lint. The logic is `vd-deeplint`'s; this module only
//! prepares the file to lint (a snapshot when the buffer is modified) and hands
//! the report back to the UI thread, which converts it with the same document.

use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::thread;

use anyhow::{anyhow, Result};

use vd_deeplint::{LintOptions, LintReport};

use crate::config_glue::Startup;
use crate::document_tab::Tab;

/// Everything the worker needs, owned so it can cross the thread boundary.
#[derive(Debug, Clone)]
pub struct LintRequest {
    /// File to lint (on disk).
    pub path: PathBuf,
    /// Buffer text, written to a snapshot when the buffer is modified so the
    /// lint sees exactly what the user sees (includes still resolve relative to
    /// the same directory).
    pub snapshot_text: Option<String>,
    /// SLANG binary override from the environment/config, if any.
    pub binary: Option<PathBuf>,
    /// Per-run timeout.
    pub timeout: std::time::Duration,
}

/// Build the request for a tab.
///
/// # Errors
/// Returns an error for an untitled tab (nothing on disk to lint yet).
pub fn request_for(tab: &Tab, startup: &Startup) -> Result<LintRequest> {
    let path = tab
        .path
        .borrow()
        .clone()
        .ok_or_else(|| anyhow!("save the file before running SLANG deep lint"))?;
    let snapshot_text = if tab.document.is_modified() {
        Some(tab.document.text())
    } else {
        None
    };
    Ok(LintRequest {
        path,
        snapshot_text,
        binary: startup.slang_binary.clone(),
        timeout: vd_deeplint::DEFAULT_TIMEOUT,
    })
}

/// Run the lint synchronously (used by the worker thread and by `--selftest`);
/// a modified buffer is snapshotted next to the file and removed afterwards.
#[must_use]
pub fn lint(request: &LintRequest) -> LintReport {
    let lint_path = match &request.snapshot_text {
        Some(text) => {
            let snapshot = snapshot_path(&request.path);
            if let Err(error) = std::fs::write(&snapshot, text) {
                eprintln!("vd-app: writing lint snapshot failed: {error}");
                return failure_report(
                    &request.binary,
                    format!("writing the lint snapshot failed: {error}"),
                );
            }
            Some(snapshot)
        }
        None => None,
    };
    let path = lint_path
        .as_deref()
        .unwrap_or(request.path.as_path())
        .to_path_buf();
    let options = LintOptions {
        binary: request.binary.clone(),
        timeout: request.timeout,
        ..LintOptions::default()
    };
    let report = vd_deeplint::lint(&path, &options);
    if let Some(snapshot) = lint_path {
        if let Err(error) = std::fs::remove_file(&snapshot) {
            eprintln!("vd-app: removing lint snapshot failed: {error}");
        }
    }
    report
}

fn failure_report(binary: &Option<PathBuf>, message: String) -> LintReport {
    LintReport {
        diagnostics: Vec::new(),
        total_diagnostics: 0,
        truncated: false,
        binary: vd_deeplint::BinaryResolution {
            chosen: binary.clone(),
            attempts: Vec::new(),
        },
        failure: Some(vd_deeplint::LintFailure::Io { message }),
        exit_code: None,
        stdout: String::new(),
        stderr: String::new(),
        elapsed: std::time::Duration::ZERO,
        child_pid: None,
    }
}

/// Run the lint on a background thread and send the report back.
pub fn spawn(request: LintRequest, sender: Sender<LintReport>) {
    thread::spawn(move || {
        let report = lint(&request);
        if sender.send(report).is_err() {
            eprintln!("vd-app: SLANG lint result had no receiver");
        }
    });
}

fn snapshot_path(path: &Path) -> PathBuf {
    let stem = path.file_stem().map_or_else(
        || "snapshot".to_string(),
        |stem| stem.to_string_lossy().into_owned(),
    );
    let name = format!(".{stem}.vedit-lint-{}.sv", std::process::id());
    path.with_file_name(name)
}
