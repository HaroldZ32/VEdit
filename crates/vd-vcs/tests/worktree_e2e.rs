//! Batch 6b (AC-6.7): `git worktree add / list / remove` on the **bundled**
//! git 2.53.0, THROUGH THE PRODUCT PATH (`vd_vcs::write` + `vd_vcs::workspace`).
//!
//! The old delivery baseline (system git 1.8.3.1) has no `worktree` command at
//! all, which is why batch 5b could only prove the worktree listing's honest
//! refusal on that baseline and had to leave the round trip to a host with a
//! modern git. Batch 6a ships git 2.53.0 inside the AppDir, so the round trip is
//! now runnable with the product's own client.
//!
//! Env-gated (the plain `cargo test` gate stays green without it):
//!
//!   * `VEDIT_GIT_BIN` — `<appdir>/bin/git` (the AppDir loader wrapper).
//!
//! The product's resolver order is bundled > PATH > env; a test binary is not in
//! an AppDir, so the test PREPENDS the wrapper's directory to `PATH` (the
//! resolver's second rule) and also sets `VEDIT_GIT` (the documented fallback),
//! so the spawned program is the bundled wrapper either way. It is recorded in
//! the evidence log which binary and version really ran.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::workspace;
use vd_vcs::write::{execute, SystemWriteRunner, WriteError, WriteOp, WriteOutcome, WriteRequest};

fn executable_from_env(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os(name).map(PathBuf::from)?;
    if !path.is_file() {
        return None;
    }
    Some(path)
}

fn run_tool(program: &Path, working_tree: &Path, args: &[&str]) -> (bool, String) {
    match Command::new(program)
        .current_dir(working_tree)
        .args(args)
        .output()
    {
        Ok(output) => (
            output.status.success(),
            format!(
                "{}{}",
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr)
            ),
        ),
        Err(error) => (false, format!("cannot run {}: {error}", program.display())),
    }
}

fn must(program: &Path, working_tree: &Path, args: &[&str]) -> String {
    eprintln!("EVIDENCE $ {} {}", program.display(), args.join(" "));
    let (success, text) = run_tool(program, working_tree, args);
    for line in text.lines() {
        eprintln!("EVIDENCE | {line}");
    }
    assert!(success, "fixture command failed: {args:?}: {text}");
    text
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

fn product_write(what: &str, request: &WriteRequest, expect_success: bool) -> WriteOutcome {
    eprintln!("EVIDENCE product write {what}: {:?}", request.op);
    let outcome = match execute(request, &SystemWriteRunner) {
        Ok(outcome) => outcome,
        Err(error) => panic!("{what} was refused as a parameter error: {error:?}"),
    };
    for line in format!("{}\n{}", outcome.stdout, outcome.stderr).lines() {
        eprintln!("EVIDENCE | {line}");
    }
    assert_eq!(
        outcome.success, expect_success,
        "{what}: expected success={expect_success}, got {outcome:?}"
    );
    outcome
}

#[test]
fn bundled_git_worktree_round_trip_runs_through_the_product_path() {
    let Some(git) = executable_from_env("VEDIT_GIT_BIN") else {
        eprintln!(
            "SKIP bundled git worktree E2E: VEDIT_GIT_BIN is not set (expected the AppDir \
             bin/git loader wrapper); see evidence/vcs-git-workspace/run-worktree-e2e.sh"
        );
        return;
    };
    let (version_ok, version) = run_tool(&git, Path::new("/tmp"), &["--version"]);
    assert!(version_ok, "the bundled git does not run: {version}");
    eprintln!(
        "EVIDENCE bundled {version_trimmed}",
        version_trimmed = version.trim()
    );
    // The product path must spawn THIS client: PATH is the resolver's second rule
    // and env its last, so both are pointed at the bundled wrapper.
    if let Some(bin_dir) = git.parent() {
        let mut path = bin_dir.to_path_buf().into_os_string();
        if let Some(existing) = std::env::var_os("PATH") {
            path.push(":");
            path.push(existing);
        }
        std::env::set_var("PATH", &path);
    }
    std::env::set_var("VEDIT_GIT", &git);

    let Ok(temp) = tempfile::tempdir() else {
        eprintln!("SKIP bundled git worktree E2E: no temporary directory");
        return;
    };
    let repo = temp.path().join("repo");
    assert!(std::fs::create_dir(&repo).is_ok());
    assert!(std::fs::write(repo.join("a.sv"), "module a;\nendmodule\n").is_ok());
    must(&git, &repo, &["init", "-q"]);
    must(
        &git,
        &repo,
        &["config", "user.email", "worktree@example.com"],
    );
    must(&git, &repo, &["config", "user.name", "VEdit Worktree"]);
    must(&git, &repo, &["add", "a.sv"]);
    must(&git, &repo, &["commit", "-q", "-m", "worktree base"]);
    must(&git, &repo, &["branch", "feature/bundled"]);

    // --- the listing starts with the main worktree only --------------------
    let before = workspace::worktrees(&repo);
    eprintln!("EVIDENCE product worktrees before: {before:?}");
    assert!(
        !before.error,
        "the listing must not fail: {:?}",
        before.note
    );
    assert_eq!(before.entries.len(), 1);
    assert!(before.entries[0].main_worktree);

    // --- worktree add (an EXISTING branch, like the panel offers) ----------
    let linked = temp.path().join("linked worktree");
    let mut add = request(&repo, WriteOp::GitWorktreeAdd);
    add.target = Some(linked.display().to_string());
    add.name = Some("feature/bundled".to_string());
    product_write("git worktree add", &add, true);
    assert!(
        linked.join("a.sv").is_file(),
        "the linked worktree is checked out"
    );
    let branch = must(&git, &linked, &["rev-parse", "--abbrev-ref", "HEAD"]);
    assert_eq!(branch.trim(), "feature/bundled");

    let after = workspace::worktrees(&repo);
    eprintln!("EVIDENCE product worktrees after add: {after:?}");
    assert_eq!(
        after.entries.len(),
        2,
        "the product listing sees both worktrees"
    );
    let Some(linked_entry) = after.entries.iter().find(|entry| !entry.main_worktree) else {
        panic!("the linked worktree is listed: {after:?}");
    };
    assert_eq!(linked_entry.path, linked.display().to_string());
    assert_eq!(linked_entry.branch, "feature/bundled");
    assert!(!linked_entry.detached);

    // --- a dirty worktree is refused by git itself (no --force) -----------
    assert!(std::fs::write(linked.join("untracked.sv"), "module u;\nendmodule\n").is_ok());
    let mut remove = request(&repo, WriteOp::GitWorktreeRemove);
    remove.target = Some(linked.display().to_string());
    let refused = product_write("git worktree remove (dirty)", &remove, false);
    eprintln!("EVIDENCE dirty removal stderr={:?}", refused.stderr);
    assert!(
        refused.stderr.contains("--force") || refused.stderr.contains("untracked"),
        "git's real reason must be preserved: {:?}",
        refused.stderr
    );
    assert!(linked.is_dir(), "the dirty worktree is NOT deleted");
    assert!(std::fs::remove_file(linked.join("untracked.sv")).is_ok());

    // --- the clean removal succeeds and the listing shrinks ----------------
    product_write("git worktree remove", &remove, true);
    assert!(!linked.exists(), "the worktree directory is gone");
    let after_remove = workspace::worktrees(&repo);
    eprintln!("EVIDENCE product worktrees after remove: {after_remove:?}");
    assert_eq!(after_remove.entries.len(), 1);

    // --- parameter refusals still happen before any process ---------------
    let mut option_path = request(&repo, WriteOp::GitWorktreeRemove);
    option_path.target = Some("--force".to_string());
    assert!(matches!(
        option_path.invocation(),
        Err(WriteError::Invalid(_))
    ));
}
