//! Delivery-baseline compatibility proof (AC-3.0).
//!
//! The read path must really return status, branch+revision and a per-file diff
//! on the delivery baseline (CentOS 7, git 1.8.3.1) **and** on current git. This
//! test drives the production runner ([`SystemRunner`]) against real
//! `tempfile::tempdir()` fixtures and asserts the parsed values, so a parameter
//! the baseline cannot parse fails here instead of in the field.
//!
//! The same binary is also run by `evidence/vcs-read-compat/run-all.sh` with a
//! controlled `git` shim in front of `PATH` (the shim rejects the two legacy
//! spellings and forwards everything else), which proves the code no longer emits
//! them.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::runner::{ensure_read_only, CommandRunner, RecordingRunner, SystemRunner, GIT_WRITE};
use vd_vcs::service::{ProcessVcsService, VcsService};
use vd_vcs::{status_with, Backend, ChangeKind};

fn git_is_available() -> bool {
    Command::new("git")
        .arg("--version")
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false)
}

/// Runs `git` in `dir`; `None` when it is unavailable or exits non-zero.
fn run_git(dir: &Path, parameters: &[&str]) -> Option<String> {
    let output = Command::new("git")
        .current_dir(dir)
        .args(parameters)
        .output()
        .ok()?;
    if output.status.success() {
        Some(String::from_utf8_lossy(&output.stdout).into_owned())
    } else {
        None
    }
}

/// Creates a committed repository in a fresh temporary directory.
///
/// Returns the root; the directory is leaked on purpose so the fixture outlives
/// the helper (the OS reaps it with the test process).
fn make_repo(path: &str, content: &str) -> Option<PathBuf> {
    let dir = tempfile::tempdir().ok()?;
    let root = dir.path().to_path_buf();
    std::mem::forget(dir);
    let file = root.join(path);
    if let Some(parent) = file.parent() {
        std::fs::create_dir_all(parent).ok()?;
    }
    std::fs::write(&file, content).ok()?;
    run_git(&root, &["init", "-q"])?;
    run_git(&root, &["config", "user.email", "compat@example.com"])?;
    run_git(&root, &["config", "user.name", "VEdit Compat"])?;
    run_git(&root, &["add", path])?;
    run_git(&root, &["commit", "-m", "compat fixture"])?;
    Some(root)
}

#[test]
fn read_path_returns_status_info_and_diff_on_the_installed_git() {
    if !git_is_available() {
        return;
    }
    let Some(root) = make_repo("rtl/top.sv", "module top;\nendmodule\n") else {
        return;
    };
    let changed = std::fs::write(
        root.join("rtl/top.sv"),
        "module top;\n// changed\nendmodule\n",
    );
    assert!(changed.is_ok());
    let added = std::fs::write(root.join("rtl/extra.sv"), "module extra;\nendmodule\n");
    assert!(added.is_ok());

    // status: one modified tracked file + one untracked file, real branch.
    let status = status_with(&root, &SystemRunner);
    assert_eq!(status.backend, Backend::Git);
    assert!(
        status.note.is_none(),
        "the status read reported a failure: {:?}",
        status.note
    );
    assert!(
        status
            .branch
            .as_deref()
            .is_some_and(|branch| !branch.is_empty()),
        "no branch parsed from git status --branch"
    );
    let modified = status
        .changes
        .iter()
        .find(|change| change.path == "rtl/top.sv");
    assert_eq!(
        modified.map(|change| change.kind),
        Some(ChangeKind::Modified),
        "changes: {:?}",
        status.changes
    );
    assert!(
        status
            .changes
            .iter()
            .any(|change| change.path == "rtl/extra.sv"),
        "the untracked file is missing: {:?}",
        status.changes
    );

    // info: branch + short revision from the same working tree.
    let service = ProcessVcsService::production();
    let info = service.info(&root);
    assert_eq!(info.backend, Backend::Git);
    assert!(
        info.note.is_none(),
        "the info read reported a failure: {:?}",
        info.note
    );
    let revision = info.revision.as_deref().unwrap_or("");
    assert!(
        !revision.is_empty()
            && revision.len() <= 40
            && revision.chars().all(|digit| digit.is_ascii_hexdigit()),
        "no usable revision parsed: {revision:?}"
    );

    // diff: the modified line really comes back.
    let diff = service.file_diff(&root, "rtl/top.sv");
    assert!(
        diff.note.is_none(),
        "the diff read reported a failure: {:?}",
        diff.note
    );
    assert!(
        diff.text.contains("+// changed"),
        "the diff is missing the modified line: {}",
        diff.text
    );
}

#[test]
fn two_repositories_keep_their_own_working_tree() {
    if !git_is_available() {
        return;
    }
    let Some(first) = make_repo("rtl/first.sv", "module first;\nendmodule\n") else {
        return;
    };
    let Some(second) = make_repo("rtl/second.sv", "module second;\nendmodule\n") else {
        return;
    };
    // Only the first repository is dirty; the second must stay clean. A read
    // that ignored the working directory would see the same tree twice.
    let changed = std::fs::write(
        first.join("rtl/first.sv"),
        "module first;\n// a\nendmodule\n",
    );
    assert!(changed.is_ok());

    let first_status = status_with(&first, &SystemRunner);
    assert_eq!(first_status.backend, Backend::Git);
    assert_eq!(first_status.changes.len(), 1, "{:?}", first_status.changes);
    assert_eq!(first_status.changes[0].path, "rtl/first.sv");

    let second_status = status_with(&second, &SystemRunner);
    assert_eq!(second_status.backend, Backend::Git);
    assert!(
        second_status.changes.is_empty(),
        "the second repository saw {:?}",
        second_status.changes
    );
    let service = ProcessVcsService::production();
    assert!(service.info(&first).revision.is_some());
    assert!(service.info(&second).revision.is_some());
}

#[test]
fn the_read_gate_still_refuses_every_write_subcommand_on_the_new_seam() {
    // The cwd parameter must not have widened the read path: every write
    // subcommand is still refused before a process exists, on both runners.
    let dir = std::env::temp_dir();
    let recorder = RecordingRunner::new();
    for subcommand in GIT_WRITE {
        let args = vec![(*subcommand).to_string()];
        assert!(
            ensure_read_only("git", &args).is_err(),
            "git {subcommand} passed the gate"
        );
        assert!(
            SystemRunner.run("git", &dir, &args).is_err(),
            "git {subcommand} was spawned"
        );
        assert!(
            recorder.run("git", &dir, &args).is_err(),
            "git {subcommand} reached the stub"
        );
    }
    assert!(
        recorder.invocations().is_empty(),
        "a refused command was recorded"
    );
    // A read still passes the gate (the run itself may fail in /tmp; the seam
    // only promises to let a read through and never resume a write).
    let read = vec!["status".to_string()];
    assert!(SystemRunner.run("git", &dir, &read).is_ok());
}
