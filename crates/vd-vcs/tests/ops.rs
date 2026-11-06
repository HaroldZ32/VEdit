//! Batch 5a (AC-5.1 / AC-5.3): merge / rebase / cherry-pick / revert / reset /
//! clean and the conflict state, against a REAL `git` in a `tempfile::tempdir()`
//! fixture. Every assertion is made with git itself (`rev-parse`, `log`,
//! `status --porcelain`), never by re-reading the implementation.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::write::{execute, SystemWriteRunner, WriteError, WriteOp, WriteOutcome, WriteRequest};
use vd_vcs::{operation_state, OperationKind};

fn run_git(dir: &Path, args: &[&str]) -> Option<String> {
    let output = Command::new("git")
        .current_dir(dir)
        .args(args)
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&output.stdout).into_owned())
}

fn git_fails(dir: &Path, args: &[&str]) -> bool {
    Command::new("git")
        .current_dir(dir)
        .args(args)
        .output()
        .map(|output| !output.status.success())
        .unwrap_or(false)
}

fn write(dir: &Path, name: &str, text: &str) -> bool {
    let path = dir.join(name);
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    std::fs::write(path, text).is_ok()
}

fn read(dir: &Path, name: &str) -> String {
    std::fs::read_to_string(dir.join(name)).unwrap_or_default()
}

fn init_repo(dir: &Path) -> bool {
    run_git(dir, &["init", "-q"]).is_some()
        && run_git(dir, &["config", "user.email", "ops@example.com"]).is_some()
        && run_git(dir, &["config", "user.name", "VEdit Ops"]).is_some()
}

fn commit_all(dir: &Path, message: &str) -> bool {
    run_git(dir, &["add", "-A"]).is_some()
        && run_git(dir, &["commit", "-q", "-m", message]).is_some()
}

fn head(dir: &Path) -> String {
    run_git(dir, &["rev-parse", "HEAD"])
        .unwrap_or_default()
        .trim()
        .to_string()
}

fn fixture() -> Option<(tempfile::TempDir, PathBuf)> {
    let temp = tempfile::tempdir().ok()?;
    let repo = temp.path().join("ops-repo");
    std::fs::create_dir(&repo).ok()?;
    if !init_repo(&repo) {
        return None;
    }
    if !write(&repo, "rtl/a.sv", "module a;\nline one\nendmodule\n") || !commit_all(&repo, "base") {
        return None;
    }
    Some((temp, repo))
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

fn run(request: &WriteRequest) -> Result<WriteOutcome, WriteError> {
    execute(request, &SystemWriteRunner)
}

/// Run a request that MUST succeed (a loud failure otherwise).
fn run_ok(request: &WriteRequest) -> WriteOutcome {
    match run(request) {
        Ok(outcome) => {
            assert!(
                outcome.success,
                "{:?} failed: {}",
                request.op, outcome.stderr
            );
            outcome
        }
        Err(error) => panic!("{:?} was refused: {error:?}", request.op),
    }
}

fn porcelain(dir: &Path) -> String {
    run_git(dir, &["status", "--porcelain"]).unwrap_or_default()
}

// ---------------------------------------------------------------------------
// merge
// ---------------------------------------------------------------------------

#[test]
fn merge_fast_forwards_and_merges_with_a_commit() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    // A branch with one commit on top of the base.
    assert!(run_git(&repo, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(&repo, "rtl/b.sv", "module b;\nendmodule\n"));
    assert!(commit_all(&repo, "feature commit"));
    assert!(run_git(&repo, &["checkout", "-q", "master"]).is_some());
    let base = head(&repo);

    // Fast-forward merge.
    let mut merge = request(&repo, WriteOp::GitMerge);
    merge.target = Some("feature".to_string());
    run_ok(&merge);
    assert_eq!(
        head(&repo),
        run_git(&repo, &["rev-parse", "feature"])
            .unwrap_or_default()
            .trim()
            .to_string()
    );
    assert_ne!(head(&repo), base);

    // A non-fast-forward merge creates a merge commit (two parents).
    assert!(write(&repo, "rtl/c.sv", "module c;\nendmodule\n"));
    assert!(commit_all(&repo, "master commit"));
    assert!(run_git(&repo, &["checkout", "-q", "-b", "other", "HEAD~1"]).is_some());
    assert!(write(&repo, "rtl/d.sv", "module d;\nendmodule\n"));
    assert!(commit_all(&repo, "other commit"));
    assert!(run_git(&repo, &["checkout", "-q", "master"]).is_some());
    let mut merge = request(&repo, WriteOp::GitMerge);
    merge.target = Some("other".to_string());
    run_ok(&merge);
    let parents = run_git(&repo, &["rev-list", "--parents", "-n", "1", "HEAD"]).unwrap_or_default();
    assert_eq!(
        parents.split_whitespace().count(),
        3,
        "a merge commit has two parents: {parents}"
    );
}

#[test]
fn a_merge_conflict_is_a_visible_state_that_continues_and_aborts() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    // Two branches changing the same line.
    assert!(run_git(&repo, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(
        &repo,
        "rtl/a.sv",
        "module a;\nfeature line\nendmodule\n"
    ));
    assert!(commit_all(&repo, "feature change"));
    assert!(run_git(&repo, &["checkout", "-q", "master"]).is_some());
    assert!(write(
        &repo,
        "rtl/a.sv",
        "module a;\nmaster line\nendmodule\n"
    ));
    assert!(commit_all(&repo, "master change"));
    let before = head(&repo);

    let mut merge = request(&repo, WriteOp::GitMerge);
    merge.target = Some("feature".to_string());
    let conflicted = run(&merge);
    match conflicted {
        Ok(outcome) => assert!(!outcome.success, "a conflicting merge must fail"),
        Err(error) => panic!("the merge was refused instead of conflicting: {error:?}"),
    }
    let state = operation_state(&repo);
    assert!(!state.error, "note: {:?}", state.note);
    assert_eq!(state.operation, Some(OperationKind::Merge));
    assert_eq!(state.conflicts, vec!["rtl/a.sv".to_string()]);
    assert!(
        porcelain(&repo).contains("UU rtl/a.sv"),
        "{}",
        porcelain(&repo)
    );

    // Resolve and continue: the merge completes with two parents.
    assert!(write(&repo, "rtl/a.sv", "module a;\nresolved\nendmodule\n"));
    assert!(run_git(&repo, &["add", "rtl/a.sv"]).is_some());
    let mut resolve = request(&repo, WriteOp::GitContinue);
    resolve.target = Some("merge".to_string());
    run_ok(&resolve);
    assert_eq!(operation_state(&repo).operation, None);
    let parents = run_git(&repo, &["rev-list", "--parents", "-n", "1", "HEAD"]).unwrap_or_default();
    assert_eq!(parents.split_whitespace().count(), 3, "{parents}");
    assert_ne!(head(&repo), before);

    // The same conflict, aborted: HEAD is back and the tree is idle.
    let Some(other) = tempfile::tempdir().ok() else {
        return;
    };
    let repo2 = other.path().join("abort-repo");
    assert!(std::fs::create_dir(&repo2).is_ok());
    assert!(init_repo(&repo2));
    assert!(write(&repo2, "rtl/a.sv", "module a;\nbase\nendmodule\n"));
    assert!(commit_all(&repo2, "base"));
    assert!(run_git(&repo2, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(&repo2, "rtl/a.sv", "module a;\nfeature\nendmodule\n"));
    assert!(commit_all(&repo2, "feature"));
    assert!(run_git(&repo2, &["checkout", "-q", "master"]).is_some());
    assert!(write(&repo2, "rtl/a.sv", "module a;\nmaster\nendmodule\n"));
    assert!(commit_all(&repo2, "master"));
    let before_abort = head(&repo2);
    let mut merge2 = request(&repo2, WriteOp::GitMerge);
    merge2.target = Some("feature".to_string());
    let _ = run(&merge2);
    assert_eq!(
        operation_state(&repo2).operation,
        Some(OperationKind::Merge)
    );
    let mut abort = request(&repo2, WriteOp::GitAbort);
    abort.target = Some("merge".to_string());
    run_ok(&abort);
    assert_eq!(operation_state(&repo2).operation, None);
    assert_eq!(head(&repo2), before_abort);
    assert!(
        porcelain(&repo2).is_empty(),
        "the abort restored the tree: {}",
        porcelain(&repo2)
    );
}

// ---------------------------------------------------------------------------
// rebase
// ---------------------------------------------------------------------------

#[test]
fn rebase_replays_the_branch_and_abort_restores_it() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    assert!(run_git(&repo, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(&repo, "rtl/b.sv", "module b;\nendmodule\n"));
    assert!(commit_all(&repo, "feature commit"));
    assert!(run_git(&repo, &["checkout", "-q", "master"]).is_some());
    assert!(write(&repo, "rtl/c.sv", "module c;\nendmodule\n"));
    assert!(commit_all(&repo, "master commit"));
    let master_tip = head(&repo);
    assert!(run_git(&repo, &["checkout", "-q", "feature"]).is_some());

    let mut rebase = request(&repo, WriteOp::GitRebase);
    rebase.target = Some("master".to_string());
    run_ok(&rebase);
    // feature now sits on top of master.
    assert_eq!(
        run_git(&repo, &["rev-parse", "feature~1"])
            .unwrap_or_default()
            .trim()
            .to_string(),
        master_tip
    );
    assert_eq!(operation_state(&repo).operation, None);

    // A conflicting rebase enters the rebase state; abort restores the branch.
    let Some(other) = tempfile::tempdir().ok() else {
        return;
    };
    let repo2 = other.path().join("rebase-abort");
    assert!(std::fs::create_dir(&repo2).is_ok());
    assert!(init_repo(&repo2));
    assert!(write(&repo2, "rtl/a.sv", "module a;\nbase\nendmodule\n"));
    assert!(commit_all(&repo2, "base"));
    assert!(run_git(&repo2, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(&repo2, "rtl/a.sv", "module a;\nfeature\nendmodule\n"));
    assert!(commit_all(&repo2, "feature"));
    assert!(run_git(&repo2, &["checkout", "-q", "master"]).is_some());
    assert!(write(&repo2, "rtl/a.sv", "module a;\nmaster\nendmodule\n"));
    assert!(commit_all(&repo2, "master"));
    let feature_tip = run_git(&repo2, &["rev-parse", "feature"])
        .unwrap_or_default()
        .trim()
        .to_string()
        .to_string();
    assert!(run_git(&repo2, &["checkout", "-q", "feature"]).is_some());
    let mut rebase2 = request(&repo2, WriteOp::GitRebase);
    rebase2.target = Some("master".to_string());
    let _ = run(&rebase2);
    assert_eq!(
        operation_state(&repo2).operation,
        Some(OperationKind::Rebase)
    );
    assert!(operation_state(&repo2)
        .conflicts
        .contains(&"rtl/a.sv".to_string()));
    let mut abort = request(&repo2, WriteOp::GitAbort);
    abort.target = Some("rebase".to_string());
    run_ok(&abort);
    assert_eq!(operation_state(&repo2).operation, None);
    assert_eq!(
        run_git(&repo2, &["rev-parse", "feature"])
            .unwrap_or_default()
            .trim()
            .to_string(),
        feature_tip
    );
}

// ---------------------------------------------------------------------------
// cherry-pick / revert
// ---------------------------------------------------------------------------

#[test]
fn cherry_pick_replays_one_commit_and_revert_undoes_it() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    assert!(run_git(&repo, &["checkout", "-q", "-b", "feature"]).is_some());
    assert!(write(
        &repo,
        "rtl/feature.sv",
        "module feature;\nendmodule\n"
    ));
    assert!(commit_all(&repo, "add the feature file"));
    let picked = head(&repo);
    assert!(run_git(&repo, &["checkout", "-q", "master"]).is_some());
    // Move master forward so the pick really replays onto a different base.
    assert!(write(&repo, "rtl/base.sv", "module base;\nendmodule\n"));
    assert!(commit_all(&repo, "master moves on"));

    let mut cherry = request(&repo, WriteOp::GitCherryPick);
    cherry.target = Some(picked.clone());
    run_ok(&cherry);
    assert!(repo.join("rtl/feature.sv").exists());
    assert_ne!(head(&repo), picked, "a cherry-pick creates a new commit");
    assert_eq!(
        run_git(&repo, &["log", "-1", "--pretty=%s"])
            .unwrap_or_default()
            .trim()
            .to_string(),
        "add the feature file"
    );

    let mut revert = request(&repo, WriteOp::GitRevert);
    revert.target = Some("HEAD".to_string());
    run_ok(&revert);
    assert!(
        !repo.join("rtl/feature.sv").exists(),
        "the revert removed the file"
    );
    assert_eq!(
        run_git(&repo, &["log", "-1", "--pretty=%s"])
            .unwrap_or_default()
            .trim()
            .to_string(),
        "Revert \"add the feature file\""
    );
}

// ---------------------------------------------------------------------------
// reset (soft / mixed / hard)
// ---------------------------------------------------------------------------

#[test]
fn reset_soft_mixed_and_hard_move_different_state() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    let base = head(&repo);
    assert!(write(&repo, "rtl/a.sv", "module a;\nchanged\nendmodule\n"));
    assert!(write(&repo, "rtl/new.sv", "module new;\nendmodule\n"));
    assert!(commit_all(&repo, "second commit"));
    assert_ne!(head(&repo), base);

    // soft: HEAD moves, the index keeps the changes.
    let mut reset = request(&repo, WriteOp::GitReset);
    reset.target = Some(base.clone());
    reset.mode = Some("soft".to_string());
    run_ok(&reset);
    assert_eq!(head(&repo), base);
    let soft = porcelain(&repo);
    assert!(soft.contains("M  rtl/a.sv"), "soft keeps the index: {soft}");
    assert!(
        soft.contains("A  rtl/new.sv"),
        "soft keeps the index: {soft}"
    );

    // mixed: HEAD moves, the index is reset, the working tree keeps the changes.
    let mut reset = request(&repo, WriteOp::GitReset);
    reset.target = Some(base.clone());
    reset.mode = Some("mixed".to_string());
    run_ok(&reset);
    assert_eq!(head(&repo), base);
    let mixed = porcelain(&repo);
    assert!(
        mixed.contains(" M rtl/a.sv"),
        "mixed keeps the tree: {mixed}"
    );
    assert!(
        mixed.contains("?? rtl/new.sv"),
        "mixed keeps the tree: {mixed}"
    );

    // hard: HEAD moves and the working tree is discarded.
    let mut reset = request(&repo, WriteOp::GitReset);
    reset.target = Some(base.clone());
    reset.mode = Some("hard".to_string());
    run_ok(&reset);
    assert_eq!(head(&repo), base);
    // A hard reset discards TRACKED changes; an untracked file is not touched
    // (that is what `clean` is for).
    assert!(
        repo.join("rtl/new.sv").exists(),
        "hard reset never deletes untracked files"
    );
    let hard = porcelain(&repo);
    assert!(
        !hard.contains("rtl/a.sv"),
        "a hard reset restores tracked files: {hard}"
    );
    assert!(
        hard.contains("?? rtl/new.sv"),
        "the untracked file stays: {hard}"
    );

    // A reset without a mode is refused before a process exists.
    let mut invalid = request(&repo, WriteOp::GitReset);
    invalid.target = Some(base);
    match run(&invalid) {
        Err(error) => assert!(error.is_invalid(), "{error:?}"),
        Ok(outcome) => panic!("a reset without a mode ran: {outcome:?}"),
    }
    let mut invalid_mode = request(&repo, WriteOp::GitReset);
    invalid_mode.target = Some("HEAD".to_string());
    invalid_mode.mode = Some("harder".to_string());
    match run(&invalid_mode) {
        Err(error) => assert!(error.is_invalid(), "{error:?}"),
        Ok(outcome) => panic!("an unknown reset mode ran: {outcome:?}"),
    }
}

// ---------------------------------------------------------------------------
// clean
// ---------------------------------------------------------------------------

#[test]
fn clean_removes_only_the_given_untracked_files() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    assert!(write(&repo, "scratch.txt", "temporary"));
    assert!(write(&repo, "rtl/a.sv", "module a;\nchanged\nendmodule\n"));
    assert!(write(&repo, "keep.txt", "also untracked"));

    let mut clean = request(&repo, WriteOp::GitClean);
    clean.paths = vec!["scratch.txt".to_string()];
    run_ok(&clean);
    assert!(
        !repo.join("scratch.txt").exists(),
        "the requested file is gone"
    );
    assert!(repo.join("keep.txt").exists(), "an unrequested file stays");
    assert!(
        read(&repo, "rtl/a.sv").contains("changed"),
        "tracked changes stay"
    );

    // clean without paths is refused.
    let mut empty = request(&repo, WriteOp::GitClean);
    match run(&empty) {
        Err(error) => assert!(error.is_invalid(), "{error:?}"),
        Ok(outcome) => panic!("a clean without paths ran: {outcome:?}"),
    }
    // clean never takes a path that escapes the tree.
    empty.paths = vec!["../outside.txt".to_string()];
    match run(&empty) {
        Err(error) => assert!(error.is_invalid(), "{error:?}"),
        Ok(outcome) => panic!("an escaping clean ran: {outcome:?}"),
    }
}

// ---------------------------------------------------------------------------
// continue / abort validation and the idle state
// ---------------------------------------------------------------------------

#[test]
fn continue_and_abort_require_the_operation_in_progress() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    for (op, target) in [
        (WriteOp::GitContinue, None),
        (WriteOp::GitAbort, None),
        (WriteOp::GitContinue, Some("stash".to_string())),
        (WriteOp::GitAbort, Some("reset".to_string())),
    ] {
        let mut invalid = request(&repo, op);
        invalid.target = target;
        match run(&invalid) {
            Err(error) => assert!(error.is_invalid(), "{op:?}: {error:?}"),
            Ok(outcome) => panic!("{op:?} ran without a pending operation: {outcome:?}"),
        }
    }
    // An idle repository reports no operation and no conflicts.
    let state = operation_state(&repo);
    assert_eq!(state.operation, None);
    assert!(!state.error, "note: {:?}", state.note);
    assert!(state.conflicts.is_empty());

    // ... and aborting with no operation in progress fails with git's reason.
    let mut abort = request(&repo, WriteOp::GitAbort);
    abort.target = Some("merge".to_string());
    match run(&abort) {
        Ok(outcome) => assert!(
            !outcome.success,
            "git must refuse an abort with nothing to abort"
        ),
        Err(error) => panic!("the abort was refused at validation: {error:?}"),
    }
}

#[test]
fn a_non_repository_reports_the_empty_operation_state() {
    let Some(temp) = tempfile::tempdir().ok() else {
        return;
    };
    let state = operation_state(temp.path());
    assert_eq!(state.backend, vd_vcs::Backend::None);
    assert_eq!(state.operation, None);
    assert!(!state.error);
    assert!(state.note.is_some());
}

#[test]
fn the_no_edit_flags_are_present_so_no_editor_blocks() {
    // `merge`/`revert` must not stop at an editor: the exact argv is asserted
    // through the documented invocation builder, not by guessing.
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    let mut merge = request(&repo, WriteOp::GitMerge);
    merge.target = Some("HEAD".to_string());
    let invocation = merge.invocation();
    match invocation {
        Ok(invocation) => {
            assert!(
                invocation.args.contains(&"--no-edit".to_string()),
                "{:?}",
                invocation.args
            );
        }
        Err(error) => panic!("merge invocation: {error:?}"),
    }
    let mut revert = request(&repo, WriteOp::GitRevert);
    revert.target = Some("HEAD".to_string());
    match revert.invocation() {
        Ok(invocation) => {
            assert!(
                invocation.args.contains(&"--no-edit".to_string()),
                "{:?}",
                invocation.args
            );
        }
        Err(error) => panic!("revert invocation: {error:?}"),
    }
    let mut clean = request(&repo, WriteOp::GitClean);
    clean.paths = vec!["x".to_string()];
    match clean.invocation() {
        Ok(invocation) => {
            assert!(
                invocation.args.contains(&"-f".to_string()),
                "{:?}",
                invocation.args
            );
            assert!(
                !invocation.args.contains(&"-d".to_string()),
                "clean must not remove directories"
            );
        }
        Err(error) => panic!("clean invocation: {error:?}"),
    }
    let mut cont = request(&repo, WriteOp::GitContinue);
    cont.target = Some("revert".to_string());
    match cont.invocation() {
        Ok(invocation) => {
            assert_eq!(invocation.args.first().map(String::as_str), Some("revert"));
            assert!(invocation.args.contains(&"--continue".to_string()));
            assert!(invocation
                .env
                .iter()
                .any(|(key, value)| key == "GIT_EDITOR" && value == "true"));
        }
        Err(error) => panic!("continue invocation: {error:?}"),
    }
    assert!(
        git_fails(&repo, &["rev-parse", "no-such-ref"]),
        "the fixture is a working repository"
    );
}
