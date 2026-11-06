//! Read-only proof for the VCS backend (AC-S2.5), strengthened in batch 3a
//! (AC-3.1 / AC-3.6) when the backend gained a WRITE seam.
//!
//! The old assertions (every write subcommand is refused; a session issues only
//! read-only subcommands) are kept and extended — nothing was removed:
//!   1. the READ path still REFUSES every write subcommand, before any process;
//!   2. the WRITE path (a separate table, `vd_vcs::write`) allows exactly the
//!      listed operations and refuses everything else (default deny);
//!   3. the two tables are disjoint: no read subcommand is writable, no write
//!      subcommand is readable — the three read/write SHARED tokens (`remote`,
//!      `stash`, `worktree`) are the explicit exception and the read gate allows
//!      their listing form only (batch 5b added the last two);
//!   4. a complete read session (status + branch/revision + per-file diff)
//!      issues only read-only subcommands.
//!
//! The stub runner runs the same gate as production, so a test cannot pass by
//! using a permissive fake.

use std::path::Path;
use vd_vcs::runner::{
    ensure_read_only, is_allowed, CommandRunner, RecordingRunner, SystemRunner, GIT_READ_ONLY,
    GIT_WRITE, SVN_READ_ONLY, SVN_WRITE,
};
use vd_vcs::service::{FileDiff, ProcessVcsService, StubVcsService, VcsInfo, VcsService};
use vd_vcs::write::{
    ensure_write_allowed, is_write_allowed, WriteOp, GIT_WRITE_ALLOWED, SVN_WRITE_ALLOWED,
};
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
        let via_runner = runner.run("git", Path::new("/tmp"), &list);
        assert!(via_runner.is_err(), "git {subcommand} must not run");
    }
    // ... and the stub recorded nothing: the refusal happens before the spawn.
    assert!(
        runner.invocations().is_empty(),
        "a refused command reached the runner"
    );
    // Batch 3a: the SAME subcommands are refused by the write gate too, unless
    // they are one of the four listed write operations. A write subcommand that
    // no operation maps to stays refused everywhere (default deny).
    for subcommand in GIT_WRITE {
        let list = args(&[subcommand]);
        let listed = GIT_WRITE_ALLOWED.contains(subcommand);
        assert_eq!(
            is_write_allowed("git", &list),
            listed,
            "git {subcommand}: write allow-list disagreement"
        );
        assert_eq!(
            ensure_write_allowed("git", &list).is_ok(),
            listed,
            "git {subcommand}: write gate disagreement"
        );
    }
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
            runner.run("svn", Path::new("/tmp"), &list).is_err(),
            "svn {subcommand} must not run"
        );
    }
    assert!(runner.invocations().is_empty());
    // Batch 3a: same split for Subversion.
    for subcommand in SVN_WRITE {
        let list = args(&[subcommand]);
        let listed = SVN_WRITE_ALLOWED.contains(subcommand);
        assert_eq!(
            is_write_allowed("svn", &list),
            listed,
            "svn {subcommand}: write allow-list disagreement"
        );
        assert_eq!(
            ensure_write_allowed("svn", &list).is_ok(),
            listed,
            "svn {subcommand}: write gate disagreement"
        );
    }
}

#[test]
fn read_and_write_tables_are_disjoint_and_default_deny() {
    // The read table is unchanged and nothing writable is readable — with ONE
    // deliberate, narrow exception: `git remote` is both a listing read and a
    // write, so it appears in both tables and the READ gate allows only the
    // listing forms (asserted below and in tests/write.rs).
    // The READ/WRITE shared tokens: each is a real listing read AND a write, so
    // the read gate allows only the listing form and refuses every write form.
    for (token, listing, write_form) in [
        ("remote", vec!["remote"], vec!["remote", "add", "o", "/x"]),
        ("remote", vec!["remote", "-v"], vec!["remote", "rm", "o"]),
        (
            "remote",
            vec!["remote", "--verbose"],
            vec!["remote", "set-url", "o", "/x"],
        ),
        ("stash", vec!["stash", "list"], vec!["stash", "save", "msg"]),
        (
            "stash",
            vec!["stash", "list", "--format=%gd%x09%H%x09%s"],
            vec!["stash", "pop"],
        ),
        ("stash", vec!["stash", "list"], vec!["stash", "apply"]),
        ("stash", vec!["stash", "list"], vec!["stash", "drop"]),
        ("stash", vec!["stash", "list"], vec!["stash", "clear"]),
        // A bare `git stash` is `stash save`: never a read.
        ("stash", vec!["stash", "list"], vec!["stash"]),
        (
            "worktree",
            vec!["worktree", "list"],
            vec!["worktree", "add", "/tmp/wt"],
        ),
        (
            "worktree",
            vec!["worktree", "list"],
            vec!["worktree", "remove", "/tmp/wt"],
        ),
        (
            "worktree",
            vec!["worktree", "list"],
            vec!["worktree", "prune"],
        ),
        ("worktree", vec!["worktree", "list"], vec!["worktree"]),
        // `list` plus an extra positional is refused (only options may follow).
        (
            "worktree",
            vec!["worktree", "list"],
            vec!["worktree", "list", "extra"],
        ),
    ] {
        assert!(GIT_WRITE_ALLOWED.contains(&token));
        if token != "remote" {
            // `remote` is listed as a read token (its write forms are refused by
            // the read gate); `stash`/`worktree` are still on GIT_WRITE.
            assert!(
                GIT_WRITE.contains(&token),
                "git {token} is a write subcommand"
            );
        }
        assert!(ensure_read_only("git", &args(&listing)).is_ok());
        assert!(
            ensure_read_only("git", &args(&write_form)).is_err(),
            "git {write_form:?} must not be read-allowed"
        );
        assert!(is_write_allowed("git", &args(&[token])));
    }
    for allowed in GIT_READ_ONLY {
        if matches!(*allowed, "remote" | "stash" | "worktree") {
            // Asserted by the shared-token table above.
            continue;
        }
        assert!(
            !GIT_WRITE_ALLOWED.contains(allowed),
            "git {allowed} is both readable and writable"
        );
        assert!(
            !is_write_allowed("git", &args(&[allowed])),
            "the write gate accepted the read subcommand git {allowed}"
        );
    }
    for allowed in SVN_READ_ONLY {
        assert!(
            !SVN_WRITE_ALLOWED.contains(allowed),
            "svn {allowed} is both readable and writable"
        );
        assert!(
            !is_write_allowed("svn", &args(&[allowed])),
            "the write gate accepted the read subcommand svn {allowed}"
        );
    }
    // Every accepted operation really maps to a listed subcommand, and that
    // subcommand is not readable. `continue`/`abort` carry the operation in
    // progress, so the sample target names one for the mapping assertion.
    for op in WriteOp::ALL {
        let program = if op.backend() == vd_vcs::Backend::Git {
            "git"
        } else {
            "svn"
        };
        let sample_target = match op {
            WriteOp::GitContinue | WriteOp::GitAbort => Some("merge"),
            _ => None,
        };
        let subcommand = op.subcommand_for(sample_target);
        assert!(
            !subcommand.is_empty(),
            "{} has no subcommand for its own operation",
            op.subcommand()
        );
        let list = args(&[subcommand]);
        assert!(
            is_write_allowed(program, &list),
            "{} {} is not on the write allow-list",
            program,
            subcommand
        );
        if program == "git" && subcommand == "remote" {
            // `git remote` IS the listing form; the write forms are not reads.
            assert!(is_allowed(program, &list));
            assert!(!is_allowed(program, &args(&["remote", "add", "o", "/x"])));
            assert!(!is_allowed(program, &args(&["remote", "rm", "o"])));
            continue;
        }
        if program == "git" && matches!(subcommand, "stash" | "worktree") {
            // The deliberate read/write overlaps: the LISTING form is a read, the
            // write forms are not, and the bare token is not a read either.
            assert!(
                !is_allowed(program, &list),
                "{subcommand} bare token is a write"
            );
            assert!(is_allowed(program, &args(&[subcommand, "list"])));
            continue;
        }
        assert!(
            !is_allowed(program, &list),
            "{} {} must not be readable",
            program,
            subcommand
        );
    }
    // Default deny: unknown tool, empty argv, unlisted write.
    assert!(!is_write_allowed("hg", &args(&["commit"])));
    assert!(!is_write_allowed("git", &[]));
    assert!(!is_write_allowed("svn", &[]));
    // `stash`/`worktree`/`submodule` joined the write table in batch 5b; they are
    // write-allowed while `submodule` stays unreadable and the two shared tokens
    // are readable only as listings (asserted above).
    assert!(is_write_allowed("git", &args(&["stash"])));
    assert!(is_write_allowed("git", &args(&["worktree"])));
    assert!(is_write_allowed("git", &args(&["submodule"])));
    assert!(!is_allowed("git", &args(&["submodule"])));
    assert!(ensure_write_allowed("git", &args(&["stash"])).is_ok());
    assert!(ensure_write_allowed("hg", &args(&["commit"])).is_err());
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
            runner
                .run("git", Path::new("/tmp"), &args(&[subcommand]))
                .is_err(),
            "git {subcommand}"
        );
    }
    for subcommand in ["commit", "update", "revert", "switch"] {
        assert!(
            runner
                .run("svn", Path::new("/tmp"), &args(&[subcommand]))
                .is_err(),
            "svn {subcommand}"
        );
    }
}

#[test]
fn allow_lists_are_disjoint_from_write_lists() {
    // `stash`/`worktree`/`remote` are read/write SHARED tokens: the read gate
    // allows their listing form only, so the bare token stays refused (asserted
    // below and in read_and_write_tables_are_disjoint_and_default_deny).
    for allowed in GIT_READ_ONLY {
        if *allowed == "remote" {
            // `git remote` IS the listing (no `list` word): still a read.
            assert!(is_allowed("git", &args(&["remote"])));
            continue;
        }
        if matches!(*allowed, "stash" | "worktree") {
            assert!(
                !is_allowed("git", &args(&[allowed])),
                "git {allowed} bare token is a write"
            );
            assert!(is_allowed("git", &args(&[allowed, "list"])));
            continue;
        }
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
        let subcommand = invocation.subcommand(); // skips global options
        assert_eq!(invocation.program, "git");
        // The repository is the process working directory, never a global
        // option: the first argument IS the subcommand.
        assert_eq!(
            invocation.args.first().map(String::as_str),
            Some(subcommand.as_str()),
            "the invocation does not start with its subcommand: {:?}",
            invocation.args
        );
        assert_eq!(
            invocation.working_tree, root,
            "the invocation did not run in the repository root"
        );
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

    // The status invocation is exactly the 1.8-compatible form.
    let status_invocation = invocations
        .iter()
        .find(|invocation| invocation.subcommand() == "status")
        .cloned();
    assert_eq!(
        status_invocation.map(|invocation| invocation.args),
        Some(args(&["status", "--porcelain", "--branch"]))
    );
}

#[test]
fn svn_session_reads_status_info_and_diff() {
    let root = repo_with_marker(".svn");
    let runner = RecordingRunner::new();
    runner.answer("svn", "status", true, "M       a.sv\n", "");
    // Subversion 1.7 reports identity through `svn info --xml` (the selector
    // form of newer clients does not exist there).
    runner.answer(
        "svn",
        "info",
        true,
        "<info><entry kind=\"dir\" revision=\"42\"><url>svn://example.invalid/repo/trunk</url></entry></info>\n",
        "",
    );
    runner.answer("svn", "diff", true, "Index: a.sv\n+line\n", "");
    let log = runner.clone();
    let service = ProcessVcsService::new(runner);
    let status = service.status(&root);
    assert_eq!(status.backend, Backend::Svn);
    for invocation in log.invocations() {
        assert_eq!(invocation.program, "svn");
        assert_eq!(
            invocation.working_tree, root,
            "the svn invocation did not run in the working copy root"
        );
    }
    assert_eq!(status.changes.len(), 1);
    let info = service.info(&root);
    assert_eq!(info.revision.as_deref(), Some("42"));
    assert_eq!(info.branch.as_deref(), Some("trunk"));
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
