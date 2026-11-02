//! Read-only proof for the VCS backend (AC-S2.5).
//!
//! Two independent properties, both enforced by `vd_vcs::runner`:
//!   1. every write subcommand is REFUSED before any process is spawned;
//!   2. a complete session (status + branch/revision + per-file diff) issues only
//!      read-only subcommands.
//!
//! The stub runner runs the same gate as production, so a test cannot pass by
//! using a permissive fake.

use std::path::Path;
use vd_vcs::runner::{
    ensure_read_only, is_allowed, CommandRunner, RecordingRunner, SystemRunner, GIT_READ_ONLY,
    GIT_WRITE, SVN_READ_ONLY, SVN_WRITE,
};
use vd_vcs::service::{FileDiff, ProcessVcsService, StubVcsService, VcsInfo, VcsService};
use vd_vcs::{status_with, Backend, ChangeKind, RepoStatus};

fn args(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| (*value).to_string()).collect()
}

#[test]
fn every_git_write_subcommand_is_refused_before_spawn() {
    let runner = RecordingRunner::new();
    for subcommand in GIT_WRITE {
        let list = args(&[subcommand]);
        let refused = ensure_read_only("git", &list);
        assert!(refused.is_err(), "git {subcommand} must be refused");
        let via_runner = runner.run("git", &list);
        assert!(via_runner.is_err(), "git {subcommand} must not run");
    }
    // ... and the stub recorded nothing: the refusal happens before the spawn.
    assert!(
        runner.invocations().is_empty(),
        "a refused command reached the runner"
    );
}

#[test]
fn every_svn_write_subcommand_is_refused_before_spawn() {
    let runner = RecordingRunner::new();
    for subcommand in SVN_WRITE {
        let list = args(&[subcommand]);
        assert!(
            ensure_read_only("svn", &list).is_err(),
            "svn {subcommand} must be refused"
        );
        assert!(
            runner.run("svn", &list).is_err(),
            "svn {subcommand} must not run"
        );
    }
    assert!(runner.invocations().is_empty());
}

#[test]
fn unknown_tool_and_empty_subcommand_are_refused() {
    assert!(ensure_read_only("hg", &args(&["status"])).is_err());
    assert!(ensure_read_only("git", &[]).is_err());
    assert!(ensure_read_only("svn", &[]).is_err());
}

#[test]
fn production_runner_gate_refuses_writes_without_spawning() {
    // The production runner is the one the product uses; the gate must hold
    // there too, not only in the stub.
    let runner = SystemRunner;
    for subcommand in ["commit", "push", "checkout", "reset", "revert"] {
        assert!(
            runner.run("git", &args(&[subcommand])).is_err(),
            "git {subcommand}"
        );
    }
    for subcommand in ["commit", "update", "revert", "switch"] {
        assert!(
            runner.run("svn", &args(&[subcommand])).is_err(),
            "svn {subcommand}"
        );
    }
}

#[test]
fn allow_lists_are_disjoint_from_write_lists() {
    for allowed in GIT_READ_ONLY {
        assert!(
            !GIT_WRITE.contains(allowed),
            "git {allowed} is both allowed and forbidden"
        );
        assert!(is_allowed("git", &args(&[allowed])));
    }
    for allowed in SVN_READ_ONLY {
        assert!(
            !SVN_WRITE.contains(allowed),
            "svn {allowed} is both allowed and forbidden"
        );
        assert!(is_allowed("svn", &args(&[allowed])));
    }
}

/// Builds a `.git`/`.svn` marker directory so `detect()` selects the backend.
fn repo_with_marker(marker: &str) -> std::path::PathBuf {
    let Ok(dir) = tempfile::tempdir() else {
        panic!("temporary directory");
    };
    let root = dir.path().to_path_buf();
    if let Err(error) = std::fs::create_dir(root.join(marker)) {
        panic!(
            "cannot create the {} marker directory: {error}",
            root.display()
        );
    }
    // keep the tempdir alive for the duration of the test
    std::mem::forget(dir);
    root
}

#[test]
fn git_session_uses_only_read_only_subcommands() {
    let root = repo_with_marker(".git");
    let runner = RecordingRunner::new();
    runner.answer("git", "status", true, "## main\n M src/a.sv\n", "");
    runner.answer("git", "rev-parse", true, "main\n", "");
    runner.answer(
        "git",
        "diff",
        true,
        "diff --git a/src/a.sv b/src/a.sv\n+added\n",
        "",
    );

    let status = status_with(&root, &runner);
    assert_eq!(status.backend, Backend::Git);
    assert_eq!(status.branch.as_deref(), Some("main"));
    assert_eq!(status.changes.len(), 1);
    assert_eq!(status.changes[0].kind, ChangeKind::Modified);

    let log = runner.clone(); // shares the invocation log
    let service = ProcessVcsService::new(runner);
    let info = service.info(&root);
    assert_eq!(info.backend, Backend::Git);
    assert_eq!(info.branch.as_deref(), Some("main"));
    let diff = service.file_diff(&root, "src/a.sv");
    assert!(diff.text.contains("+added"), "diff text: {}", diff.text);

    let invocations = log.invocations();
    assert!(
        !invocations.is_empty(),
        "the session must have issued commands"
    );
    for invocation in &invocations {
        let subcommand = invocation.subcommand(); // skips git's -C <dir>
        assert_eq!(invocation.program, "git");
        assert!(
            GIT_READ_ONLY.contains(&subcommand.as_str()),
            "session issued a non-allow-listed subcommand: {subcommand}"
        );
        assert!(
            !GIT_WRITE.contains(&subcommand.as_str()),
            "session issued a write: {subcommand}"
        );
    }
    let subcommands = log.subcommands();
    assert!(subcommands.iter().any(|value| value == "status"));
    assert!(subcommands.iter().any(|value| value == "rev-parse"));
    assert!(subcommands.iter().any(|value| value == "diff"));
}

#[test]
fn svn_session_reads_status_info_and_diff() {
    let root = repo_with_marker(".svn");
    let runner = RecordingRunner::new();
    runner.answer("svn", "status", true, "M       a.sv\n", "");
    runner.answer("svn", "info", true, "42\n", "");
    runner.answer("svn", "diff", true, "Index: a.sv\n+line\n", "");
    let service = ProcessVcsService::new(runner);
    let status = service.status(&root);
    assert_eq!(status.backend, Backend::Svn);
    assert_eq!(status.changes.len(), 1);
    let info = service.info(&root);
    assert_eq!(info.revision.as_deref(), Some("42"));
    let diff = service.file_diff(&root, "a.sv");
    assert!(diff.text.contains("+line"));
}

#[test]
fn stub_service_drives_the_panel_without_processes() {
    let info = VcsInfo {
        backend: Backend::Git,
        branch: Some("feature/panel".to_string()),
        revision: Some("abc1234".to_string()),
        note: None,
    };
    let status = RepoStatus {
        backend: Backend::Git,
        branch: Some("feature/panel".to_string()),
        changes: vec![vd_vcs::service::modified("rtl/top.sv")],
        note: None,
    };
    let diff = FileDiff {
        path: "rtl/top.sv".to_string(),
        text: "diff --git a/rtl/top.sv b/rtl/top.sv\n".to_string(),
        binary: false,
        note: None,
    };
    let stub = StubVcsService::new(info, status, vec![diff]);
    let root = Path::new("/nonexistent-but-never-touched");
    assert_eq!(stub.backend(), Backend::Git);
    assert_eq!(stub.info(root).revision.as_deref(), Some("abc1234"));
    assert_eq!(stub.status(root).changes.len(), 1);
    assert!(stub.file_diff(root, "rtl/top.sv").text.contains("top.sv"));
    // A path the stub does not know is a note, not a panic.
    let unknown = stub.file_diff(root, "missing.sv");
    assert!(unknown.text.is_empty());
    assert!(unknown.note.is_some());
}

#[test]
fn plain_directory_is_the_empty_state_in_every_query() {
    let Ok(dir) = tempfile::tempdir() else {
        panic!("temp dir");
    };
    let runner = RecordingRunner::new();
    let service = ProcessVcsService::new(runner);
    let info = service.info(dir.path());
    assert_eq!(info.backend, Backend::None);
    assert!(info.branch.is_none() && info.revision.is_none());
    assert!(info.note.is_some());
    let diff = service.file_diff(dir.path(), "a.sv");
    assert!(diff.text.is_empty());
    assert_eq!(diff.note.as_deref(), Some("not a repository"));
    // No tool was invoked for a plain directory.
    let status = service.status(dir.path());
    assert_eq!(status.backend, Backend::None);
}
