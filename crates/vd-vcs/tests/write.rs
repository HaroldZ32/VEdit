//! Write-seam proof for the VCS backend (batch 3a, AC-3.1 / AC-3.6).
//!
//! Two groups:
//!   * NEGATIVE: default deny at the operation, backend and parameter level —
//!     nothing is spawned for a request that is not explicitly accepted;
//!   * POSITIVE: every accepted operation builds exactly the documented
//!     invocation, and a real `git` round trip (stage -> unstage -> commit ->
//!     discard) runs inside a `tempfile::tempdir()` fixture, never in the
//!     VEdit working tree.
//!
//! The stub runner applies the same gate as production, so a permissive fake
//! cannot make these tests pass.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::runner::ensure_read_only;
use vd_vcs::write::{
    ensure_write_allowed, execute, is_write_allowed, RecordingWriteRunner, SystemWriteRunner,
    WriteError, WriteInvocation, WriteOp, WriteOutcome, WriteRequest, WriteRunner,
    GIT_WRITE_ALLOWED, SVN_WRITE_ALLOWED,
};
use vd_vcs::Backend;

fn args(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| (*value).to_string()).collect()
}

/// The outcome of an allowed invocation, or a loud test failure. The workspace
/// denies `clippy::expect_used`/`unwrap_used`, so the failure path is an explicit
/// match instead of `.expect(..)`.
fn ran(result: Result<WriteOutcome, WriteError>) -> WriteOutcome {
    match result {
        Ok(outcome) => outcome,
        Err(error) => panic!("the write did not run: {error:?}"),
    }
}

/// A directory carrying a backend marker so `detect()` selects that backend.
fn marked_dir(marker: &str) -> std::io::Result<PathBuf> {
    let dir = tempfile::tempdir()?;
    let root = dir.path().to_path_buf();
    std::fs::create_dir(root.join(marker))?;
    // Keep the file-system entry: the marker must outlive this function.
    std::mem::forget(dir);
    Ok(root)
}

fn request(root: &Path, op: WriteOp) -> WriteRequest {
    WriteRequest {
        root: root.to_path_buf(),
        op,
        paths: Vec::new(),
        message: None,
        author: None,
        amend: false,
        name: None,
        target: None,
        url: None,
        credential_user: None,
        credential_password: None,
        set_upstream: false,
        mode: None,
    }
}

#[test]
fn every_operation_code_round_trips_and_unknown_codes_are_refused() {
    for op in WriteOp::ALL {
        assert_eq!(
            WriteOp::from_code(op.code()),
            Some(op),
            "code {} did not round-trip",
            op.code()
        );
    }
    for code in [0, -1, 40, 99, 999] {
        assert_eq!(WriteOp::from_code(code), None, "code {code} was accepted");
    }
    // The codes are the ones the C ABI documents.
    assert_eq!(WriteOp::GitStage.code(), 1);
    assert_eq!(WriteOp::GitCommit.code(), 4);
    assert_eq!(WriteOp::SvnCommit.code(), 11);
    assert_eq!(WriteOp::SvnRemove.code(), 15);
    assert_eq!(WriteOp::GitFetch.code(), 5);
    assert_eq!(WriteOp::GitPush.code(), 7);
    assert_eq!(WriteOp::GitBranchDelete.code(), 10);
    assert_eq!(WriteOp::GitBranchRename.code(), 16);
    assert_eq!(WriteOp::GitSetUpstream.code(), 23);
    assert_eq!(WriteOp::GitStashSave.code(), 32);
    assert_eq!(WriteOp::GitStashDrop.code(), 35);
    assert_eq!(WriteOp::GitWorktreeAdd.code(), 36);
    assert_eq!(WriteOp::GitWorktreeRemove.code(), 37);
    assert_eq!(WriteOp::GitSubmoduleInit.code(), 38);
    assert_eq!(WriteOp::GitSubmoduleUpdate.code(), 39);
    assert_eq!(WriteOp::ALL.len(), 39);
    // Irreversible operations are marked for the UI confirmation (AC-5.3).
    assert!(WriteOp::GitDiscard.is_irreversible());
    assert!(WriteOp::SvnRevert.is_irreversible());
    assert!(WriteOp::SvnRemove.is_irreversible());
    assert!(!WriteOp::GitStage.is_irreversible());
    assert!(!WriteOp::GitCommit.is_irreversible());
    // Batch 5b: dropping a stash and removing a worktree cannot be undone from
    // the panel (AC-5.3 double confirmation); save/pop/apply and worktree add can.
    assert!(WriteOp::GitStashDrop.is_irreversible());
    assert!(WriteOp::GitWorktreeRemove.is_irreversible());
    assert!(!WriteOp::GitStashSave.is_irreversible());
    assert!(!WriteOp::GitStashPop.is_irreversible());
    assert!(!WriteOp::GitStashApply.is_irreversible());
    assert!(!WriteOp::GitWorktreeAdd.is_irreversible());
    assert!(!WriteOp::GitSubmoduleInit.is_irreversible());
    assert!(!WriteOp::GitSubmoduleUpdate.is_irreversible());
}

#[test]
fn a_directory_that_is_not_a_working_tree_is_refused() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let mut stage = request(dir.path(), WriteOp::GitStage);
    stage.paths = args(&["a.sv"]);
    let refused = stage.invocation();
    assert!(
        matches!(&refused, Err(WriteError::Invalid(message)) if message.contains("not a Git or Subversion working tree")),
        "expected a refusal, got {refused:?}"
    );
    // The root itself must exist.
    let missing = Path::new("/nonexistent/vedit-write-check");
    let mut nowhere = request(missing, WriteOp::GitStage);
    nowhere.paths = args(&["a.sv"]);
    assert!(matches!(nowhere.invocation(), Err(WriteError::Invalid(_))));
}

#[test]
fn the_backend_must_match_the_operation() {
    let Ok(svn_tree) = marked_dir(".svn") else {
        return;
    };
    let mut git_op = request(&svn_tree, WriteOp::GitStage);
    git_op.paths = args(&["a.sv"]);
    let refused = git_op.invocation();
    assert!(
        matches!(&refused, Err(WriteError::Invalid(message)) if message.contains("requires a Git working tree")),
        "expected a backend mismatch, got {refused:?}"
    );

    let Ok(git_tree) = marked_dir(".git") else {
        return;
    };
    let mut svn_op = request(&git_tree, WriteOp::SvnAdd);
    svn_op.paths = args(&["a.sv"]);
    let refused = svn_op.invocation();
    assert!(
        matches!(&refused, Err(WriteError::Invalid(message)) if message.contains("requires a Subversion working tree")),
        "expected a backend mismatch, got {refused:?}"
    );
}

#[test]
fn parameters_outside_the_operation_contract_are_refused() {
    let Ok(tree) = marked_dir(".git") else {
        return;
    };

    // A stage without a path is meaningless.
    let stage = request(&tree, WriteOp::GitStage);
    assert!(matches!(stage.invocation(), Err(WriteError::Invalid(_))));

    // A path may not be absolute, may not escape the tree, may not look like
    // an option.
    for bad in [
        "/etc/passwd",
        "../outside.sv",
        "rtl/../../outside.sv",
        "-rf",
    ] {
        let mut stage = request(&tree, WriteOp::GitStage);
        stage.paths = args(&[bad]);
        assert!(
            matches!(stage.invocation(), Err(WriteError::Invalid(_))),
            "path {bad} was accepted"
        );
    }

    // A commit needs a non-empty message.
    let commit = request(&tree, WriteOp::GitCommit);
    assert!(matches!(commit.invocation(), Err(WriteError::Invalid(_))));
    let mut blank = request(&tree, WriteOp::GitCommit);
    blank.message = Some("   \t ".to_string());
    assert!(matches!(blank.invocation(), Err(WriteError::Invalid(_))));

    // Parameters that do not belong to the operation are refused, not ignored.
    let mut stage_with_message = request(&tree, WriteOp::GitStage);
    stage_with_message.paths = args(&["a.sv"]);
    stage_with_message.message = Some("unused".to_string());
    assert!(matches!(
        stage_with_message.invocation(),
        Err(WriteError::Invalid(_))
    ));

    let mut stage_with_amend = request(&tree, WriteOp::GitStage);
    stage_with_amend.paths = args(&["a.sv"]);
    stage_with_amend.amend = true;
    assert!(matches!(
        stage_with_amend.invocation(),
        Err(WriteError::Invalid(_))
    ));

    let mut svn_commit_with_author = request(&tree, WriteOp::SvnCommit);
    svn_commit_with_author.message = Some("message".to_string());
    svn_commit_with_author.author = Some("Someone".to_string());
    assert!(matches!(
        svn_commit_with_author.invocation(),
        Err(WriteError::Invalid(_))
    ));

    // ... and nothing was recorded by any runner: the refusal is before spawn.
    let runner = RecordingWriteRunner::new();
    let mut session = request(&tree, WriteOp::GitCommit);
    session.message = Some("   ".to_string());
    assert!(execute(&session, &runner).is_err());
    assert!(runner.invocations().is_empty());
}

#[test]
fn accepted_operations_build_exactly_one_documented_invocation() {
    let runner = RecordingWriteRunner::new();
    let Ok(git_tree) = marked_dir(".git") else {
        return;
    };

    let mut stage = request(&git_tree, WriteOp::GitStage);
    stage.paths = args(&["rtl/top.sv"]);
    let outcome = execute(&stage, &runner);
    assert!(
        matches!(&outcome, Ok(value) if value.success),
        "{outcome:?}"
    );

    let mut unstage = request(&git_tree, WriteOp::GitUnstage);
    unstage.paths = args(&["rtl/top.sv"]);
    assert!(execute(&unstage, &runner).is_ok());

    let mut discard = request(&git_tree, WriteOp::GitDiscard);
    discard.paths = args(&["rtl/top.sv"]);
    assert!(execute(&discard, &runner).is_ok());

    let mut commit = request(&git_tree, WriteOp::GitCommit);
    commit.message = Some("  commit message  ".to_string());
    commit.author = Some("VEdit <vedit@example.com>".to_string());
    commit.amend = true;
    commit.paths = args(&["rtl/top.sv"]);
    assert!(execute(&commit, &runner).is_ok());

    let Ok(svn_tree) = marked_dir(".svn") else {
        return;
    };
    let mut svn_commit = request(&svn_tree, WriteOp::SvnCommit);
    svn_commit.message = Some("svn message".to_string());
    svn_commit.paths = args(&["trunk/a.sv"]);
    assert!(execute(&svn_commit, &runner).is_ok());
    let svn_update = request(&svn_tree, WriteOp::SvnUpdate);
    assert!(execute(&svn_update, &runner).is_ok());

    let invocations = runner.invocations();
    assert_eq!(invocations.len(), 6, "invocations: {invocations:?}");
    assert_eq!(invocations[0].program, "git");
    // No git global options: the process runs in the repository root, because
    // the CentOS 7 baseline (git 1.8.3) has no global path option.
    assert_eq!(invocations[0].working_tree, git_tree);
    assert_eq!(invocations[0].args, args(&["add", "--", "rtl/top.sv"]));
    assert_eq!(
        invocations[1].args,
        args(&["reset", "HEAD", "--", "rtl/top.sv"])
    );
    assert_eq!(invocations[2].args, args(&["checkout", "--", "rtl/top.sv"]));
    assert_eq!(
        invocations[3].args,
        args(&[
            "commit",
            "-m",
            "commit message",
            "--author=VEdit <vedit@example.com>",
            "--amend",
            "--",
            "rtl/top.sv",
        ])
    );
    assert_eq!(invocations[4].program, "svn");
    // Batch 6b: the Subversion network-capable operations never wait on a
    // terminal prompt and never cache credentials (AC-6.4), so the two global
    // options are part of the documented argv.
    assert_eq!(
        invocations[4].args,
        args(&[
            "commit",
            "--non-interactive",
            "--no-auth-cache",
            "-m",
            "svn message",
            "trunk/a.sv",
        ])
    );
    assert_eq!(
        invocations[5].args,
        args(&["update", "--non-interactive", "--no-auth-cache"])
    );
    assert_eq!(invocations[4].working_tree, svn_tree);
}

#[test]
fn the_write_gate_is_also_enforced_by_the_runner() {
    let runner = RecordingWriteRunner::new();
    let Ok(tree) = marked_dir(".git") else {
        return;
    };
    // A caller that bypasses `WriteRequest` and hand-builds an unlisted
    // invocation is stopped by the runner's own gate. `bisect` is a write that
    // no operation maps to (batch 5b added stash/worktree/submodule, so they are
    // no longer unlisted).
    let bisect = WriteInvocation {
        program: "git",
        working_tree: tree.clone(),
        args: args(&["bisect"]),
        env: Vec::new(),
    };
    assert!(runner.run_write(&bisect).is_err());
    let read = WriteInvocation {
        program: "git",
        working_tree: tree.clone(),
        args: args(&["status"]),
        env: Vec::new(),
    };
    assert!(runner.run_write(&read).is_err());
    assert!(runner.invocations().is_empty());
    assert!(!is_write_allowed("git", &args(&["bisect"])));
    assert!(ensure_write_allowed("git", &args(&["bisect"])).is_err());
}

#[test]
fn the_read_gate_still_refuses_every_subcommand_the_write_path_runs() {
    // The write seam does not widen the read table: each allowed write
    // subcommand is still refused by the read gate.
    for program in ["git", "svn"] {
        let table = if program == "git" {
            GIT_WRITE_ALLOWED
        } else {
            SVN_WRITE_ALLOWED
        };
        for subcommand in table {
            let list = args(&[subcommand]);
            assert!(is_write_allowed(program, &list));
            if program == "git" && *subcommand == "remote" {
                // `git remote` is BOTH a read (the listing) and a write (add /
                // rm / set-url): the read gate allows exactly the listing forms,
                // so the "reads cannot write" rule still holds by construction.
                assert!(ensure_read_only(program, &list).is_ok());
                assert!(ensure_read_only(program, &args(&["remote", "-v"])).is_ok());
                for write_form in [
                    args(&["remote", "add", "origin", "/tmp/x"]),
                    args(&["remote", "rm", "origin"]),
                    args(&["remote", "set-url", "origin", "/tmp/x"]),
                ] {
                    assert!(
                        ensure_read_only(program, &write_form).is_err(),
                        "the read gate accepted a `git remote` write form"
                    );
                }
                continue;
            }
            assert!(
                ensure_read_only(program, &list).is_err(),
                "the read gate accepted {program} {subcommand}"
            );
        }
    }
}

/// Runs the real `git` in `root` and returns its stdout, or `None` on failure.
fn run_git(root: &Path, parameters: &[&str]) -> Option<String> {
    // `current_dir`, not a global path option: git 1.8.3 (the CentOS 7
    // baseline) has no such option.
    let output = Command::new("git")
        .current_dir(root)
        .args(parameters)
        .output()
        .ok()?;
    if output.status.success() {
        Some(String::from_utf8_lossy(&output.stdout).into_owned())
    } else {
        None
    }
}

fn git_is_available() -> bool {
    Command::new("git")
        .arg("--version")
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false)
}

#[test]
fn git_round_trip_runs_in_a_temp_fixture() {
    // AC-0.4: the fixture is a fresh temporary directory, never the VEdit tree.
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path().to_path_buf();
    assert!(
        !root.starts_with(Path::new(env!("CARGO_MANIFEST_DIR"))),
        "the fixture must live outside the VEdit working tree: {}",
        root.display()
    );
    if !git_is_available() {
        return;
    }
    let created = std::fs::create_dir_all(root.join("rtl"));
    assert!(created.is_ok());
    let written = std::fs::write(root.join("rtl/top.sv"), "module top;\nendmodule\n");
    assert!(written.is_ok());
    assert!(run_git(&root, &["init", "-q"]).is_some(), "git init failed");
    assert!(run_git(&root, &["config", "user.email", "test@example.com"]).is_some());
    assert!(run_git(&root, &["config", "user.name", "VEdit Test"]).is_some());

    let mut stage = request(&root, WriteOp::GitStage);
    stage.paths = args(&["rtl/top.sv"]);
    let real = vd_vcs::write::write(&stage);
    assert!(
        matches!(&real, Ok(value) if value.success && value.backend == Backend::Git),
        "stage failed: {real:?}"
    );
    assert_eq!(
        run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
        Some("A  rtl/top.sv\n"),
        "the file must be staged after the write"
    );

    // Unstage returns it to untracked.
    let mut unstage = request(&root, WriteOp::GitUnstage);
    unstage.paths = args(&["rtl/top.sv"]);
    let unstaged = vd_vcs::write::write(&unstage);
    assert!(
        matches!(&unstaged, Ok(value) if value.success),
        "{unstaged:?}"
    );
    assert_eq!(
        run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
        Some("?? rtl/top.sv\n")
    );

    // Stage again and commit with an explicit author.
    assert!(matches!(&vd_vcs::write::write(&stage), Ok(value) if value.success));
    let mut commit = request(&root, WriteOp::GitCommit);
    commit.message = Some("add: fixture file".to_string());
    commit.author = Some("VEdit Author <author@example.com>".to_string());
    let committed = vd_vcs::write::write(&commit);
    assert!(
        matches!(&committed, Ok(value) if value.success),
        "commit failed: {committed:?}"
    );
    assert_eq!(
        run_git(&root, &["log", "-1", "--pretty=%s"]).as_deref(),
        Some("add: fixture file\n")
    );
    assert_eq!(
        run_git(&root, &["log", "-1", "--pretty=%an"]).as_deref(),
        Some("VEdit Author\n")
    );
    assert_eq!(
        run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
        Some("")
    );

    // Modify, stage, unstage, then discard: the working tree is clean again.
    let modified = std::fs::write(
        root.join("rtl/top.sv"),
        "module top;\n// changed\nendmodule\n",
    );
    assert!(modified.is_ok());
    assert!(matches!(&vd_vcs::write::write(&stage), Ok(value) if value.success));
    assert!(matches!(&vd_vcs::write::write(&unstage), Ok(value) if value.success));
    let mut discard = request(&root, WriteOp::GitDiscard);
    discard.paths = args(&["rtl/top.sv"]);
    let discarded = vd_vcs::write::write(&discard);
    assert!(
        matches!(&discarded, Ok(value) if value.success),
        "discard failed: {discarded:?}"
    );
    assert_eq!(
        run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
        Some("")
    );

    // The READ path still detects the same repository (backend selection is a
    // marker walk, no subprocess). The read queries themselves are outside this
    // task: on the CentOS 7 baseline (git 1.8.3) they currently fail because
    // `vd-vcs` used to pass a global path option, which 1.8.3 does not have —
    // reported as a
    // separate finding, not fixed here (the read gate must stay untouched).
    let status = vd_vcs::status(&root);
    assert_eq!(status.backend, Backend::Git);
}

#[test]
fn a_tool_failure_is_a_normal_result_not_a_rejected_request() {
    let Ok(tree) = marked_dir(".git") else {
        return;
    };
    let runner = RecordingWriteRunner::new();
    runner.answer(false, "", "fatal: not a git repository");
    let mut stage = request(&tree, WriteOp::GitStage);
    stage.paths = args(&["a.sv"]);
    let outcome = execute(&stage, &runner);
    match outcome {
        Ok(value) => {
            assert!(!value.success, "a non-zero tool exit must not be success");
            assert!(value.stderr.contains("not a git repository"));
        }
        Err(error) => panic!("a tool failure must be a result, got {error:?}"),
    }
}

// --- batch 4a (AC-4.1 / AC-4.2): remote sync, branches, tags, credentials ----

fn named_request(root: &Path, op: WriteOp, name: &str) -> WriteRequest {
    let mut value = request(root, op);
    value.name = Some(name.to_string());
    value
}

#[test]
fn batch_4a_operations_build_the_documented_invocations() {
    let runner = RecordingWriteRunner::new();
    let Ok(tree) = marked_dir(".git") else {
        return;
    };

    let mut fetch = request(&tree, WriteOp::GitFetch);
    fetch.target = Some("origin".to_string());
    assert!(execute(&fetch, &runner).is_ok());

    let mut pull = request(&tree, WriteOp::GitPull);
    pull.target = Some("origin".to_string());
    pull.name = Some("main".to_string());
    assert!(execute(&pull, &runner).is_ok());

    let mut push = request(&tree, WriteOp::GitPush);
    push.target = Some("origin".to_string());
    push.name = Some("main".to_string());
    push.set_upstream = true;
    assert!(execute(&push, &runner).is_ok());

    let mut create = named_request(&tree, WriteOp::GitBranchCreate, "feature/x");
    create.target = Some("HEAD".to_string());
    assert!(execute(&create, &runner).is_ok());
    assert!(execute(
        &named_request(&tree, WriteOp::GitBranchSwitch, "feature/x"),
        &runner
    )
    .is_ok());
    assert!(execute(
        &named_request(&tree, WriteOp::GitBranchDelete, "feature/x"),
        &runner
    )
    .is_ok());
    let mut rename = named_request(&tree, WriteOp::GitBranchRename, "feature/x");
    rename.target = Some("feature/y".to_string());
    assert!(execute(&rename, &runner).is_ok());
    let mut tag = named_request(&tree, WriteOp::GitTagCreate, "v1.0");
    tag.target = Some("HEAD".to_string());
    assert!(execute(&tag, &runner).is_ok());
    assert!(execute(
        &named_request(&tree, WriteOp::GitTagDelete, "v1.0"),
        &runner
    )
    .is_ok());
    let mut checkout = request(&tree, WriteOp::GitCheckout);
    checkout.target = Some("v1.0".to_string());
    assert!(execute(&checkout, &runner).is_ok());
    let mut remote_add = named_request(&tree, WriteOp::GitRemoteAdd, "origin");
    remote_add.url = Some("file:///tmp/bare.git".to_string());
    assert!(execute(&remote_add, &runner).is_ok());
    let mut set_url = named_request(&tree, WriteOp::GitRemoteSetUrl, "origin");
    set_url.url = Some("file:///tmp/other.git".to_string());
    assert!(execute(&set_url, &runner).is_ok());
    assert!(execute(
        &named_request(&tree, WriteOp::GitRemoteRemove, "origin"),
        &runner
    )
    .is_ok());
    let mut upstream = named_request(&tree, WriteOp::GitSetUpstream, "main");
    upstream.target = Some("origin/main".to_string());
    assert!(execute(&upstream, &runner).is_ok());

    let invocations = runner.invocations();
    let expected: Vec<Vec<String>> = vec![
        args(&["fetch", "origin"]),
        args(&["pull", "--ff-only", "origin", "main"]),
        args(&["push", "--set-upstream", "origin", "main"]),
        args(&["branch", "feature/x", "HEAD"]),
        args(&["checkout", "feature/x"]),
        args(&["branch", "-d", "feature/x"]),
        args(&["branch", "-m", "feature/x", "feature/y"]),
        args(&["tag", "v1.0", "HEAD"]),
        args(&["tag", "-d", "v1.0"]),
        args(&["checkout", "v1.0"]),
        args(&["remote", "add", "origin", "file:///tmp/bare.git"]),
        args(&["remote", "set-url", "origin", "file:///tmp/other.git"]),
        args(&["remote", "rm", "origin"]),
        args(&["branch", "--set-upstream-to=origin/main", "main"]),
    ];
    assert_eq!(
        invocations.len(),
        expected.len(),
        "invocations: {invocations:?}"
    );
    for (invocation, wanted) in invocations.iter().zip(expected) {
        assert_eq!(invocation.args, wanted);
        assert_eq!(invocation.program, "git");
        assert_eq!(invocation.working_tree, tree);
    }
}

#[test]
fn batch_4a_parameters_are_validated_before_any_process() {
    let Ok(tree) = marked_dir(".git") else {
        return;
    };
    let runner = RecordingWriteRunner::new();

    // A name that starts with '-' would become an option.
    let mut option_like = named_request(&tree, WriteOp::GitBranchCreate, "--force");
    option_like.target = None;
    assert!(matches!(
        option_like.invocation(),
        Err(WriteError::Invalid(_))
    ));

    // A ref with whitespace / '..' is refused.
    let bad_ref = named_request(&tree, WriteOp::GitTagCreate, "bad tag");
    assert!(matches!(bad_ref.invocation(), Err(WriteError::Invalid(_))));
    let mut range_ref = request(&tree, WriteOp::GitCheckout);
    range_ref.target = Some("a..b".to_string());
    assert!(matches!(
        range_ref.invocation(),
        Err(WriteError::Invalid(_))
    ));

    // A remote name must not contain '/'.
    let mut slashed = named_request(&tree, WriteOp::GitRemoteAdd, "origin/x");
    slashed.url = Some("file:///tmp/bare.git".to_string());
    assert!(matches!(slashed.invocation(), Err(WriteError::Invalid(_))));

    // A missing URL is refused; a URL that looks like an option is refused.
    let missing_url = named_request(&tree, WriteOp::GitRemoteAdd, "origin");
    assert!(matches!(
        missing_url.invocation(),
        Err(WriteError::Invalid(_))
    ));
    let mut option_url = named_request(&tree, WriteOp::GitRemoteAdd, "origin");
    option_url.url = Some("--upload-pack=evil".to_string());
    assert!(matches!(
        option_url.invocation(),
        Err(WriteError::Invalid(_))
    ));

    // Credentials only belong to network operations, and only as a pair.
    let mut offline_credentials = request(&tree, WriteOp::GitStage);
    offline_credentials.paths = args(&["a.sv"]);
    offline_credentials.credential_user = Some("user".to_string());
    offline_credentials.credential_password = Some("secret".to_string());
    assert!(matches!(
        offline_credentials.invocation(),
        Err(WriteError::Invalid(_))
    ));
    let mut lone_password = request(&tree, WriteOp::GitPush);
    lone_password.credential_password = Some("secret".to_string());
    assert!(matches!(
        lone_password.invocation(),
        Err(WriteError::Invalid(_))
    ));

    // Nothing reached a runner.
    assert!(execute(&offline_credentials, &runner).is_err());
    assert!(runner.invocations().is_empty());
}

#[test]
fn credentials_travel_in_the_environment_and_are_redacted() {
    let Ok(tree) = marked_dir(".git") else {
        return;
    };
    let runner = RecordingWriteRunner::new();
    runner.answer(
        false,
        "already tried https://alice:s3cr3t@example.com/repo",
        "fatal: authentication failed for 'https://alice:s3cr3t@example.com/repo' (alice/s3cr3t)",
    );
    let mut push = request(&tree, WriteOp::GitPush);
    push.target = Some("origin".to_string());
    push.name = Some("main".to_string());
    push.credential_user = Some("alice".to_string());
    push.credential_password = Some("s3cr3t".to_string());
    let outcome = ran(execute(&push, &runner));
    assert!(!outcome.success);

    let invocations = runner.invocations();
    assert_eq!(invocations.len(), 1);
    let invocation = &invocations[0];
    // The secret is NOT in argv (which `ps` exposes)...
    assert!(!invocation.args.iter().any(|arg| arg.contains("s3cr3t")));
    assert!(!invocation.args.iter().any(|arg| arg.contains("alice")));
    // ...and IS in the child environment.
    let env = &invocation.env;
    assert!(env
        .iter()
        .any(|(key, value)| key == "VD_GIT_PASSWORD" && value == "s3cr3t"));
    assert!(env
        .iter()
        .any(|(key, value)| key == "VD_GIT_USERNAME" && value == "alice"));
    assert!(env
        .iter()
        .any(|(key, value)| key == "GIT_TERMINAL_PROMPT" && value == "0"));
    // Every credential written by a helper is cleared first.
    assert!(invocation
        .args
        .iter()
        .any(|arg| arg == "credential.helper="));
    // The tool's own echo of the secret is redacted out of the result.
    assert!(!outcome.stdout.contains("s3cr3t"));
    assert!(!outcome.stderr.contains("s3cr3t"));
    assert!(!outcome.stderr.contains("alice"));
    assert!(outcome.stderr.contains("***"));
    // The Debug rendering of the request never prints the password either.
    let debug = format!("{push:?}");
    assert!(
        !debug.contains("s3cr3t"),
        "debug leaked the password: {debug}"
    );
}

#[test]
fn git_remote_round_trip_against_a_local_bare_repository() {
    // AC-4.1: fetch / pull / push against a local bare repository, asserted with
    // `git log` in the bare side and the working-tree content on the clone side.
    // Every directory is a fresh temporary one, never the VEdit tree.
    if !git_is_available() {
        return;
    }
    let Ok(work) = tempfile::tempdir() else {
        return;
    };
    let base = work.path().to_path_buf();
    assert!(!base.starts_with(Path::new(env!("CARGO_MANIFEST_DIR"))));
    let bare = base.join("origin.git");
    let seed = base.join("seed");
    let clone = base.join("clone");
    for dir in [&bare, &seed, &clone] {
        assert!(std::fs::create_dir_all(dir).is_ok());
    }
    assert!(Command::new("git")
        .current_dir(&bare)
        .args(["init", "--bare", "-q"])
        .status()
        .map(|status| status.success())
        .unwrap_or(false));
    for dir in [&seed, &clone] {
        assert!(run_git(dir, &["init", "-q"]).is_some());
        assert!(run_git(dir, &["config", "user.email", "e2e@example.com"]).is_some());
        assert!(run_git(dir, &["config", "user.name", "VEdit E2E"]).is_some());
    }
    assert!(std::fs::write(seed.join("a.sv"), "module a;\nendmodule\n").is_ok());
    assert!(run_git(&seed, &["add", "a.sv"]).is_some());
    assert!(run_git(&seed, &["commit", "-m", "seed"]).is_some());
    assert!(run_git(
        &seed,
        &["remote", "add", "origin", &bare.display().to_string()]
    )
    .is_some());
    let mut push = request(&seed, WriteOp::GitPush);
    push.target = Some("origin".to_string());
    push.name = Some("master".to_string());
    push.set_upstream = true;
    let outcome = ran(execute(&push, &SystemWriteRunner));
    assert!(outcome.success, "push failed: {}", outcome.stderr);
    let bare_log = run_git(&bare, &["log", "--pretty=%s", "-1"]).unwrap_or_default();
    assert_eq!(bare_log.trim(), "seed");

    // The clone fetches the commit and checks it out.
    assert!(run_git(
        &clone,
        &["remote", "add", "origin", &bare.display().to_string()]
    )
    .is_some());
    let mut fetch = request(&clone, WriteOp::GitFetch);
    fetch.target = Some("origin".to_string());
    assert!(execute(&fetch, &SystemWriteRunner)
        .map(|value| value.success)
        .unwrap_or(false));
    let mut pull = request(&clone, WriteOp::GitPull);
    pull.target = Some("origin".to_string());
    pull.name = Some("master".to_string());
    assert!(execute(&pull, &SystemWriteRunner)
        .map(|value| value.success)
        .unwrap_or(false));
    let pulled = std::fs::read_to_string(clone.join("a.sv")).unwrap_or_default();
    assert!(pulled.contains("module a"));

    // A second push to a diverged remote is reported, not panicked on.
    assert!(std::fs::write(seed.join("a.sv"), "module a;\n// second\nendmodule\n").is_ok());
    assert!(run_git(&seed, &["add", "a.sv"]).is_some());
    assert!(run_git(&seed, &["commit", "-m", "second"]).is_some());
    let rejecting = base.join("rejecting.git");
    assert!(std::fs::create_dir_all(&rejecting).is_ok());
    assert!(Command::new("git")
        .current_dir(&rejecting)
        .args(["init", "--bare", "-q"])
        .status()
        .map(|status| status.success())
        .unwrap_or(false));
    assert!(run_git(
        &seed,
        &[
            "remote",
            "set-url",
            "origin",
            &rejecting.display().to_string()
        ]
    )
    .is_some());
    // First push to the empty remote succeeds...
    let mut first_push = request(&seed, WriteOp::GitPush);
    first_push.target = Some("origin".to_string());
    first_push.name = Some("master".to_string());
    let first = ran(execute(&first_push, &SystemWriteRunner));
    assert!(first.success, "first push failed: {}", first.stderr);
    // ...then rewriting the local commit makes the next push a non-fast-forward,
    // which must be REPORTED (success=false + stderr), never panic.
    assert!(run_git(&seed, &["commit", "--amend", "-m", "rewritten"]).is_some());
    let mut bad_push = request(&seed, WriteOp::GitPush);
    bad_push.target = Some("origin".to_string());
    bad_push.name = Some("master".to_string());
    let failed = ran(execute(&bad_push, &SystemWriteRunner));
    assert!(!failed.success, "a non-fast-forward push must fail");
    assert!(!failed.stderr.trim().is_empty());
}
