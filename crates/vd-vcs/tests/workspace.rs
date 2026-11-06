//! Batch 5b (AC-5.2 / AC-5.4): the stash stack, the linked worktrees and the
//! submodule operations, against a REAL `git` in a `tempfile::tempdir()` fixture.
//! Every assertion is made with git itself (`stash list`, `worktree list`,
//! `submodule status`, the files on disk), never by re-reading the implementation.
//!
//! Baseline note: the delivery baseline (CentOS 7, git 1.8.3.1) has no
//! `git worktree` command at all. This test file therefore treats a supported
//! `git worktree` as a capability and proves the OTHER half of the contract on an
//! unsupported git: the read reports git's real refusal (`error = true` + the
//! tool's message) instead of pretending the list is empty. The lane selftest
//! (`src/workspace/vcs/vcs_selftest.cpp`, run on git 2.53 in the Qt6 image) owns
//! the panel-level worktree add → list → remove E2E.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::workspace::{self, WorkspaceEntry};
use vd_vcs::write::{execute, SystemWriteRunner, WriteError, WriteOp, WriteOutcome, WriteRequest};

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
        && run_git(dir, &["config", "user.email", "workspace@example.com"]).is_some()
        && run_git(dir, &["config", "user.name", "VEdit Workspace"]).is_some()
}

fn commit_all(dir: &Path, message: &str) -> bool {
    run_git(dir, &["add", "-A"]).is_some()
        && run_git(dir, &["commit", "-q", "-m", message]).is_some()
}

fn fixture() -> Option<(tempfile::TempDir, PathBuf)> {
    let temp = tempfile::tempdir().ok()?;
    let repo = temp.path().join("workspace-repo");
    std::fs::create_dir(&repo).ok()?;
    if !init_repo(&repo) {
        return None;
    }
    if !write(&repo, "rtl/a.sv", "module a;\nbase\nendmodule\n") || !commit_all(&repo, "base") {
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

fn stash_count(repo: &Path) -> usize {
    workspace::stashes(repo).entries.len()
}

fn stash_refs(repo: &Path) -> Vec<String> {
    workspace::stashes(repo)
        .entries
        .into_iter()
        .map(|entry| entry.ref_name)
        .collect()
}

/// `git worktree list` exists on this git (it does NOT on the 1.8.3.1 baseline).
fn supports_worktree(repo: &Path) -> bool {
    run_git(repo, &["worktree", "list"]).is_some()
}

// --- stash (AC-5.2) ---------------------------------------------------------

#[test]
fn stash_save_pop_apply_and_drop_round_trip() {
    let Some((_temp, repo)) = fixture() else {
        eprintln!("SKIP stash round trip: no usable tempdir / git");
        return;
    };
    // Save: a modified tracked file goes into the stash, the tree becomes clean.
    if !write(&repo, "rtl/a.sv", "module a;\nmodified one\nendmodule\n") {
        eprintln!("SKIP stash round trip: the fixture file is not writable");
        return;
    }
    let mut save = request(&repo, WriteOp::GitStashSave);
    save.message = Some("wip one".to_string());
    run_ok(&save);
    assert_eq!(
        read(&repo, "rtl/a.sv"),
        "module a;\nbase\nendmodule\n",
        "stash save must clean the working tree"
    );
    let listing = workspace::stashes(&repo);
    assert!(!listing.error, "stash list failed: {:?}", listing.note);
    assert_eq!(listing.backend, vd_vcs::Backend::Git);
    assert_eq!(listing.entries.len(), 1);
    assert_eq!(listing.entries[0].ref_name, "stash@{0}");
    assert!(
        listing.entries[0].subject.contains("wip one"),
        "subject: {:?}",
        listing.entries[0].subject
    );
    assert_eq!(listing.entries[0].hash.len(), 40);

    // Pop: the change comes back and the stash is gone.
    let pop = request(&repo, WriteOp::GitStashPop);
    run_ok(&pop);
    assert_eq!(
        read(&repo, "rtl/a.sv"),
        "module a;\nmodified one\nendmodule\n"
    );
    assert_eq!(stash_count(&repo), 0, "pop must drop the stash");

    // Apply: the change comes back but the stash STAYS; drop then removes it.
    if !write(&repo, "rtl/a.sv", "module a;\nchange two\nendmodule\n") {
        return;
    }
    let mut save = request(&repo, WriteOp::GitStashSave);
    save.message = Some("wip two".to_string());
    run_ok(&save);
    let apply = request(&repo, WriteOp::GitStashApply);
    run_ok(&apply);
    assert_eq!(
        read(&repo, "rtl/a.sv"),
        "module a;\nchange two\nendmodule\n"
    );
    assert_eq!(stash_count(&repo), 1, "apply must keep the stash");
    let drop = request(&repo, WriteOp::GitStashDrop);
    run_ok(&drop);
    assert_eq!(stash_count(&repo), 0, "drop must remove the stash");

    // A save with no message uses git's own default subject; the read still
    // reports one row.
    if !write(&repo, "rtl/a.sv", "module a;\nchange three\nendmodule\n") {
        return;
    }
    let save = request(&repo, WriteOp::GitStashSave);
    run_ok(&save);
    assert_eq!(stash_count(&repo), 1);
}

#[test]
fn stash_operations_on_an_empty_stack_report_the_real_git_reason() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    // Nothing to stash: the request runs, git reports its own reason, and the
    // request is NOT a parameter refusal (the seam is still honest about it).
    let outcome = run_ok(&request(&repo, WriteOp::GitStashSave));
    // On an unmodified tree git 1.8.3.1 says "No local changes to save"; newer
    // git exits 0 with the same message. Either way the stack stays empty.
    assert!(
        outcome.success || outcome.stderr.to_lowercase().contains("no local changes"),
        "unexpected stash save result: {outcome:?}"
    );
    assert_eq!(stash_count(&repo), 0);
    // `stash pop` with no stash is a normal tool FAILURE (success = false) with
    // git's message, not a refused request.
    let pop = match run(&request(&repo, WriteOp::GitStashPop)) {
        Ok(outcome) => outcome,
        Err(error) => panic!("stash pop was refused as a parameter error: {error:?}"),
    };
    assert!(!pop.success, "stash pop with no stash must report failure");
    assert!(
        pop.stderr.to_lowercase().contains("no stash"),
        "stderr: {:?}",
        pop.stderr
    );
}

#[test]
fn stash_parameters_are_validated_before_any_process() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    // A message with a newline cannot be one argv element.
    let mut bad_message = request(&repo, WriteOp::GitStashSave);
    bad_message.message = Some("line one\nline two".to_string());
    assert!(matches!(
        bad_message.invocation(),
        Err(WriteError::Invalid(_))
    ));
    // A stash ref that looks like an option is refused.
    let mut bad_ref = request(&repo, WriteOp::GitStashDrop);
    bad_ref.target = Some("--all".to_string());
    assert!(matches!(bad_ref.invocation(), Err(WriteError::Invalid(_))));
    // A ref with whitespace is refused (one argv element, no smuggling).
    let mut spaced = request(&repo, WriteOp::GitStashApply);
    spaced.target = Some("stash@{0} --index".to_string());
    assert!(matches!(spaced.invocation(), Err(WriteError::Invalid(_))));
    // Paths are not a stash parameter at all.
    let mut with_paths = request(&repo, WriteOp::GitStashSave);
    with_paths.paths = vec!["rtl/a.sv".to_string()];
    assert!(matches!(
        with_paths.invocation(),
        Err(WriteError::Invalid(_))
    ));
}

#[test]
fn stash_save_argv_is_positional_because_the_baseline_has_no_dash_m() {
    // The delivery baseline (git 1.8.3.1) rejects `stash save -m` outright
    // (measured: probe-git1831.log), so the message is positional and `--` is
    // used only when the message could be read as an option.
    let Some(tree) = marked_dir(".git") else {
        return;
    };
    let mut save = request(&tree, WriteOp::GitStashSave);
    save.message = Some("plain message".to_string());
    let invocation = save.invocation();
    let Ok(invocation) = invocation else {
        panic!("a plain stash message must be accepted");
    };
    assert_eq!(
        invocation.args,
        vec!["stash", "save", "plain message"]
            .into_iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
    );
    save.message = Some("-m looks like an option".to_string());
    let Ok(invocation) = save.invocation() else {
        panic!("a leading-dash stash message must be accepted with `--`");
    };
    assert_eq!(
        invocation.args,
        vec!["stash", "save", "--", "-m looks like an option"]
            .into_iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
    );
}

#[test]
fn stash_drop_and_pop_target_the_named_stash_only() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    // Two stashes, each a different change.
    if !write(&repo, "rtl/a.sv", "module a;\nfirst\nendmodule\n") {
        return;
    }
    let mut save = request(&repo, WriteOp::GitStashSave);
    save.message = Some("first".to_string());
    run_ok(&save);
    if !write(&repo, "rtl/a.sv", "module a;\nsecond\nendmodule\n") {
        return;
    }
    let mut save = request(&repo, WriteOp::GitStashSave);
    save.message = Some("second".to_string());
    run_ok(&save);
    let refs = stash_refs(&repo);
    assert_eq!(refs.len(), 2, "refs: {refs:?}");
    assert_eq!(refs[0], "stash@{0}");
    // Drop the OLDER one by its ref: exactly one row disappears and the newer
    // stash stays at stash@{0}.
    let mut drop = request(&repo, WriteOp::GitStashDrop);
    drop.target = Some("stash@{1}".to_string());
    run_ok(&drop);
    let refs = stash_refs(&repo);
    assert_eq!(refs, vec!["stash@{0}".to_string()], "refs: {refs:?}");
    let listing = workspace::stashes(&repo);
    assert!(
        listing.entries[0].subject.contains("second"),
        "the remaining stash must be the newer one: {:?}",
        listing.entries[0].subject
    );
}

#[test]
fn stashes_on_a_plain_directory_are_empty_with_a_note_not_an_error() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let listing = workspace::stashes(dir.path());
    assert!(!listing.error);
    assert!(listing.entries.is_empty());
    assert!(listing.note.is_some(), "a note must explain the empty list");
    let worktrees = workspace::worktrees(dir.path());
    assert!(!worktrees.error);
    assert!(worktrees.entries.is_empty());
    assert!(worktrees.note.is_some());
}

// --- worktrees (AC-5.2) -----------------------------------------------------

#[test]
fn worktree_list_reports_the_main_worktree_even_without_worktree_support() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    let listing = workspace::worktrees(&repo);
    assert_eq!(listing.backend, vd_vcs::Backend::Git);
    if supports_worktree(&repo) {
        assert!(!listing.error, "worktree list failed: {:?}", listing.note);
        assert_eq!(listing.entries.len(), 1);
        assert!(listing.entries[0].main_worktree);
        assert!(!listing.entries[0].detached);
        assert!(
            listing.entries[0].path.ends_with("workspace-repo"),
            "path: {:?}",
            listing.entries[0].path
        );
        assert!(!listing.entries[0].hash.is_empty());
    } else {
        // The delivery baseline: git has no `worktree` command. The read must
        // report git's REAL refusal, never an invented empty list.
        assert!(listing.error, "an unsupported git must be an error");
        let note = listing.note.unwrap_or_default();
        assert!(
            note.contains("not a git command"),
            "the note must be git's real reason, got {note:?}"
        );
    }
}

#[test]
fn worktree_add_list_remove_round_trip() {
    let Some((temp, repo)) = fixture() else {
        return;
    };
    if !supports_worktree(&repo) {
        eprintln!(
            "SKIP worktree round trip: this git has no 'worktree' command (the delivery \
             baseline, git 1.8.3.1); the lane E2E on git 2.53 owns the panel path"
        );
        return;
    }
    run_git(&repo, &["branch", "feature/wt"]);
    let worktree = temp.path().join("linked worktree");
    let mut add = request(&repo, WriteOp::GitWorktreeAdd);
    add.target = Some(worktree.display().to_string());
    add.name = Some("feature/wt".to_string());
    run_ok(&add);
    assert!(
        worktree.join("rtl/a.sv").is_file(),
        "the worktree is checked out"
    );
    assert_eq!(
        run_git(&worktree, &["rev-parse", "--abbrev-ref", "HEAD"])
            .unwrap_or_default()
            .trim(),
        "feature/wt"
    );
    let listing = workspace::worktrees(&repo);
    assert!(!listing.error, "worktree list failed: {:?}", listing.note);
    assert_eq!(listing.entries.len(), 2, "{listing:?}");
    let Some(linked) = listing.entries.iter().find(|entry| !entry.main_worktree) else {
        panic!("the linked worktree is listed: {listing:?}");
    };
    assert_eq!(linked.path, worktree.display().to_string());
    assert_eq!(linked.branch, "feature/wt");
    assert!(!linked.detached);
    // The linked worktree is an independent checkout: a commit there does not
    // move the main working tree's HEAD until the branch is merged.
    assert!(write(
        &worktree,
        "rtl/a.sv",
        "module a;\nwt edit\nendmodule\n"
    ));
    assert!(run_git(&worktree, &["commit", "-q", "-am", "wt edit"]).is_some());

    // A DIRTY worktree is refused by git itself (no `--force` is ever passed) and
    // stays on disk: the real reason, not a silent removal.
    if write(&worktree, "rtl/untracked.sv", "module u;\nendmodule\n") {
        let mut remove = request(&repo, WriteOp::GitWorktreeRemove);
        remove.target = Some(worktree.display().to_string());
        // The REQUEST is valid (it reaches git); git itself refuses, which is the
        // real reason the user sees — not a silent forced removal.
        let outcome = match run(&remove) {
            Ok(outcome) => outcome,
            Err(error) => panic!("the dirty removal was refused as a parameter error: {error:?}"),
        };
        assert!(
            !outcome.success,
            "a dirty worktree must not be removed: {outcome:?}"
        );
        assert!(
            outcome.stderr.contains("--force") || outcome.stderr.contains("untracked"),
            "git's real reason: {:?}",
            outcome.stderr
        );
        assert!(worktree.is_dir(), "the dirty worktree must still exist");
        // Clean the untracked file the fixture added, then the same request works.
        let _ = std::fs::remove_file(worktree.join("rtl/untracked.sv"));
    }
    let mut remove = request(&repo, WriteOp::GitWorktreeRemove);
    remove.target = Some(worktree.display().to_string());
    run_ok(&remove);
    assert!(!worktree.exists(), "the worktree directory is gone");
    assert_eq!(workspace::worktrees(&repo).entries.len(), 1);
    // The metadata directory under .git/worktrees is pruned by git itself.
    let metadata = repo.join(".git").join("worktrees");
    let remaining = std::fs::read_dir(&metadata)
        .map(|entries| entries.count())
        .unwrap_or(0);
    assert_eq!(remaining, 0, "git prunes the removed worktree's metadata");
}

#[test]
fn worktree_parameters_are_validated_before_any_process() {
    let Some(tree) = marked_dir(".git") else {
        return;
    };
    // The path is mandatory for both worktree operations.
    assert!(matches!(
        request(&tree, WriteOp::GitWorktreeAdd).invocation(),
        Err(WriteError::Invalid(_))
    ));
    assert!(matches!(
        request(&tree, WriteOp::GitWorktreeRemove).invocation(),
        Err(WriteError::Invalid(_))
    ));
    // An option-shaped path is refused; a `..` path is legitimate (the worktree
    // normally lives beside the repository).
    let mut option_path = request(&tree, WriteOp::GitWorktreeRemove);
    option_path.target = Some("--force".to_string());
    assert!(matches!(
        option_path.invocation(),
        Err(WriteError::Invalid(_))
    ));
    let mut relative = request(&tree, WriteOp::GitWorktreeAdd);
    relative.target = Some("../wt-beside".to_string());
    relative.name = Some("feature/x".to_string());
    let Ok(invocation) = relative.invocation() else {
        panic!("a relative worktree path must be accepted");
    };
    assert_eq!(
        invocation.args,
        vec!["worktree", "add", "../wt-beside", "feature/x"]
            .into_iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
    );
    // A branch name that is not a branch name is refused before any process.
    let mut bad_branch = request(&tree, WriteOp::GitWorktreeAdd);
    bad_branch.target = Some("../wt".to_string());
    bad_branch.name = Some("--detach".to_string());
    assert!(matches!(
        bad_branch.invocation(),
        Err(WriteError::Invalid(_))
    ));
}

// --- submodules (AC-5.4) ----------------------------------------------------

/// The fixture lets the PRODUCT's own `submodule update --init` child use the
/// file protocol: git 2.53 refuses `file://` submodule clones by default
/// (CVE-2022-39253) and the override is read from the PROCESS config, not from
/// the repository. The product never weakens that policy; only the fixture does,
/// for the duration of one call.
struct ProtocolAllowGuard {
    count: Option<String>,
    key: Option<String>,
    value: Option<String>,
}

impl ProtocolAllowGuard {
    fn new() -> Self {
        let guard = Self {
            count: std::env::var("GIT_CONFIG_COUNT").ok(),
            key: std::env::var("GIT_CONFIG_KEY_0").ok(),
            value: std::env::var("GIT_CONFIG_VALUE_0").ok(),
        };
        std::env::set_var("GIT_CONFIG_COUNT", "1");
        std::env::set_var("GIT_CONFIG_KEY_0", "protocol.file.allow");
        std::env::set_var("GIT_CONFIG_VALUE_0", "always");
        guard
    }
}

impl Drop for ProtocolAllowGuard {
    fn drop(&mut self) {
        for (name, previous) in [
            ("GIT_CONFIG_COUNT", &self.count),
            ("GIT_CONFIG_KEY_0", &self.key),
            ("GIT_CONFIG_VALUE_0", &self.value),
        ] {
            match previous {
                Some(value) => std::env::set_var(name, value),
                None => std::env::remove_var(name),
            }
        }
    }
}

fn submodule_fixture() -> Option<(tempfile::TempDir, PathBuf, PathBuf)> {
    let temp = tempfile::tempdir().ok()?;
    let source = temp.path().join("submodule-source");
    std::fs::create_dir(&source).ok()?;
    if !init_repo(&source) || !write(&source, "sub.txt", "submodule payload\n") {
        return None;
    }
    if !commit_all(&source, "submodule base") {
        return None;
    }
    let superproject = temp.path().join("superproject");
    std::fs::create_dir(&superproject).ok()?;
    if !init_repo(&superproject) || !write(&superproject, "top.sv", "module top;\nendmodule\n") {
        return None;
    }
    if !commit_all(&superproject, "super base") {
        return None;
    }
    // The FIXTURE adds the submodule with the file protocol allowed; the product
    // never runs `submodule add`.
    let source_url = source.display().to_string();
    let output = Command::new("git")
        .current_dir(&superproject)
        .args([
            "-c",
            "protocol.file.allow=always",
            "submodule",
            "add",
            source_url.as_str(),
            "sub",
        ])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    if !commit_all(&superproject, "add submodule") {
        return None;
    }
    Some((temp, source, superproject))
}

#[test]
fn submodule_init_and_update_check_out_the_pinned_commit() {
    let Some((temp, _source, superproject)) = submodule_fixture() else {
        eprintln!("SKIP submodule E2E: git could not build the local submodule fixture");
        return;
    };
    // A FRESH clone is the real case: `.gitmodules` is present, nothing is
    // initialized, the submodule directory is empty.
    let clone = temp.path().join("clone");
    let cloned = Command::new("git")
        .current_dir(temp.path())
        .args(["clone", "-q", &superproject.display().to_string(), "clone"])
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false);
    if !cloned {
        eprintln!("SKIP submodule E2E: git clone of the local fixture failed");
        return;
    }
    assert!(
        !clone.join("sub").join("sub.txt").is_file(),
        "a fresh clone must not have the submodule checked out"
    );
    let _guard = ProtocolAllowGuard::new();
    let init = run_ok(&request(&clone, WriteOp::GitSubmoduleInit));
    assert!(init.success, "submodule init failed: {init:?}");
    let update = run_ok(&request(&clone, WriteOp::GitSubmoduleUpdate));
    assert!(update.success, "submodule update failed: {update:?}");
    assert_eq!(
        read(&clone.join("sub"), "sub.txt"),
        "submodule payload\n",
        "the pinned submodule payload is checked out"
    );
    // `git submodule status` marks a checked-out, in-sync submodule with a
    // leading space (no '+' = different commit, no '-' = not initialized).
    let status = run_git(&clone, &["submodule", "status"]).unwrap_or_default();
    assert!(
        status.starts_with(' '),
        "the submodule must be initialized and in sync: {status:?}"
    );
}

#[test]
fn submodule_on_a_repository_without_submodules_is_a_normal_result() {
    let Some((_temp, repo)) = fixture() else {
        return;
    };
    let init = run_ok(&request(&repo, WriteOp::GitSubmoduleInit));
    assert!(init.success, "{init:?}");
    let update = run_ok(&request(&repo, WriteOp::GitSubmoduleUpdate));
    assert!(update.success, "{update:?}");
    assert!(update.stdout.is_empty() || !update.stdout.contains("error"));
}

#[test]
fn the_workspace_operations_are_git_only_and_parameter_checked() {
    let Some(svn_tree) = marked_dir(".svn") else {
        return;
    };
    for op in [
        WriteOp::GitStashSave,
        WriteOp::GitStashPop,
        WriteOp::GitWorktreeAdd,
        WriteOp::GitSubmoduleUpdate,
    ] {
        let mut bad = request(&svn_tree, op);
        if op == WriteOp::GitWorktreeAdd {
            bad.target = Some("../wt".to_string());
        }
        assert!(
            matches!(bad.invocation(), Err(WriteError::Invalid(_))),
            "{op:?} must be refused in a Subversion working copy"
        );
    }
    // A submodule operation takes no message, no name and no paths.
    let Some(tree) = marked_dir(".git") else {
        return;
    };
    let mut with_name = request(&tree, WriteOp::GitSubmoduleInit);
    with_name.name = Some("sub".to_string());
    assert!(matches!(
        with_name.invocation(),
        Err(WriteError::Invalid(_))
    ));
    let mut with_message = request(&tree, WriteOp::GitSubmoduleUpdate);
    with_message.message = Some("no message".to_string());
    assert!(matches!(
        with_message.invocation(),
        Err(WriteError::Invalid(_))
    ));
}

// --- listing caps ----------------------------------------------------------

#[test]
fn workspace_entries_are_plain_data_capped_by_the_reader() {
    let entry = WorkspaceEntry::default();
    assert!(entry.ref_name.is_empty() && entry.path.is_empty());
    assert_eq!(workspace::MAX_STASHES, 1000);
    assert_eq!(workspace::MAX_WORKTREES, 200);
}

/// Builds a directory with a backend marker so `detect()` selects it.
fn marked_dir(marker: &str) -> Option<PathBuf> {
    let dir = tempfile::tempdir().ok()?;
    let root = dir.path().to_path_buf();
    std::fs::create_dir(root.join(marker)).ok()?;
    std::mem::forget(dir);
    Some(root)
}
