//! Batch 6b (AC-6.2 / AC-6.5): the REAL Subversion backend against the
//! **bundled** client (subversion 1.14.5, shipped in the AppDir by batch 6a).
//!
//! The host and every build image have no `svn` on `PATH`, so this test is
//! env-gated: it runs only when BOTH variables name the bundled tools,
//!
//!   * `VEDIT_SVN_BIN`     — `<appdir>/bin/svn` (the AppDir loader wrapper)
//!   * `VEDIT_SVNADMIN`    — `build/vcs-tools/svnadmin` (build-only, never shipped)
//!
//! and SKIPs with the reason otherwise, so the plain `cargo test` gate stays
//! green on a machine without them. The product path is the one under test:
//! `VEDIT_SVN` is set so `crates/vd-vcs/src/toolchain.rs` resolves the SAME
//! client the fixture uses (bundled candidates do not apply to a test binary,
//! PATH has no svn, so the env fallback is what the runner spawns), and every
//! read/write goes through `vd_vcs::status` / `ProcessVcsService` /
//! `vd_vcs::write::write` — never through a private shortcut.
//!
//! Every assertion is made with the real `svn`/`svnadmin` commands as well, so a
//! green test means the repository state really changed.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::service::{ProcessVcsService, VcsService};
use vd_vcs::write::{execute, SystemWriteRunner, WriteError, WriteOp, WriteOutcome, WriteRequest};
use vd_vcs::{detect, status, Backend};

fn executable_from_env(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os(name).map(PathBuf::from)?;
    if !path.is_file() {
        return None;
    }
    Some(path)
}

/// Runs one fixture tool and returns `(success, stdout+stderr)`.
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

/// Runs a fixture command that MUST succeed, and records it in the log.
fn must(program: &Path, working_tree: &Path, args: &[&str]) -> String {
    eprintln!("EVIDENCE $ {} {}", program.display(), args.join(" "));
    let (success, text) = run_tool(program, working_tree, args);
    for line in text.lines() {
        eprintln!("EVIDENCE | {line}");
    }
    assert!(success, "fixture command failed: {args:?}: {text}");
    text
}

fn write_file(dir: &Path, name: &str, text: &str) -> bool {
    std::fs::write(dir.join(name), text).is_ok()
}

fn read_file(dir: &Path, name: &str) -> String {
    std::fs::read_to_string(dir.join(name)).unwrap_or_default()
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

/// Runs one product write and asserts its result, recording it in the log.
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
fn bundled_subversion_reads_and_writes_run_through_the_product_path() {
    let Some(svn) = executable_from_env("VEDIT_SVN_BIN") else {
        eprintln!(
            "SKIP bundled subversion E2E: VEDIT_SVN_BIN is not set (expected the AppDir \
             bin/svn loader wrapper); see evidence/svn-e2e/run-all.sh"
        );
        return;
    };
    let Some(svnadmin) = executable_from_env("VEDIT_SVNADMIN") else {
        eprintln!(
            "SKIP bundled subversion E2E: VEDIT_SVNADMIN is not set (expected \
             build/vcs-tools/svnadmin)"
        );
        return;
    };
    // AC-6.2: the BUILD-IMAGE path is the fixture actor and the product spawns
    // the bundled client. Record both versions so the log proves which binaries ran.
    let (svn_ok, svn_version) = run_tool(&svn, Path::new("/tmp"), &["--version", "--quiet"]);
    let (admin_ok, admin_version) =
        run_tool(&svnadmin, Path::new("/tmp"), &["--version", "--quiet"]);
    assert!(svn_ok && admin_ok, "the bundled svn tools do not run");
    eprintln!(
        "EVIDENCE bundled svn={} svnadmin={}",
        svn_version.trim(),
        admin_version.trim()
    );
    // The product's resolver is bundled > PATH > env; a test binary has no
    // AppDir and PATH carries no svn, so this env value is what it spawns.
    std::env::set_var("VEDIT_SVN", &svn);

    let Ok(temp) = tempfile::tempdir() else {
        eprintln!("SKIP bundled subversion E2E: no temporary directory");
        return;
    };
    let root = temp.path().to_path_buf();
    let repo = root.join("repo");
    let wc = root.join("wc");
    let wc2 = root.join("wc2");
    must(&svnadmin, &root, &["create", "repo"]);
    let repo_url = format!("file://{}", repo.display());
    must(&svn, &root, &["checkout", "-q", &repo_url, "wc"]);
    must(&svn, &root, &["checkout", "-q", &repo_url, "wc2"]);

    // --- the product detects the .svn working copy --------------------------
    let (backend, found) = detect(&wc);
    eprintln!("EVIDENCE detect backend={backend:?} root={found:?}");
    assert_eq!(
        backend,
        Backend::Svn,
        "a .svn working copy must be detected"
    );
    assert!(
        matches!(&found, Some(path) if path.join(".svn").is_dir()),
        "the working-copy root (the directory holding .svn) is reported: {found:?}"
    );

    // --- SvnAdd + SvnCommit: the first revision -----------------------------
    assert!(write_file(&wc, "a.sv", "module a;\nendmodule\n"));
    let mut add = request(&wc, WriteOp::SvnAdd);
    add.paths = vec!["a.sv".to_string()];
    product_write("svn add", &add, true);
    let status_text = must(&svn, &wc, &["status"]);
    assert!(
        status_text.contains("A       a.sv")
            || status_text.contains("A  a.sv")
            || status_text.contains("A   a.sv"),
        "svn status must report the added file as A: {status_text:?}"
    );
    let mut commit = request(&wc, WriteOp::SvnCommit);
    commit.message = Some("base revision".to_string());
    product_write("svn commit", &commit, true);
    // `svn log` is per-path AND bounded by the path's base revision; the WC root
    // is still at r0 after the commit, so the repository URL is queried.
    let log = must(&svn, &wc, &["log", "--xml", &repo_url]);
    assert_eq!(
        log.matches("<logentry").count(),
        1,
        "the commit must create exactly revision 1: {log:?}"
    );

    // --- reads: status / info / single-file diff ---------------------------
    assert!(write_file(
        &wc,
        "a.sv",
        "module a;\n// changed\nendmodule\n"
    ));
    let repo_status = status(&wc);
    eprintln!(
        "EVIDENCE product status backend={:?} changes={:?}",
        repo_status.backend, repo_status.changes
    );
    assert_eq!(repo_status.backend, Backend::Svn);
    assert_eq!(repo_status.note, None, "no 'cannot run svn' note");
    assert_eq!(
        repo_status
            .changes
            .iter()
            .map(|change| change.path.as_str())
            .collect::<Vec<_>>(),
        vec!["a.sv"],
        "the change list carries the WORKING-COPY-RELATIVE path, like the Git panel"
    );
    let service = ProcessVcsService::production();
    let info = service.info(&wc);
    eprintln!(
        "EVIDENCE product info backend={:?} branch={:?} revision={:?} note={:?}",
        info.backend, info.branch, info.revision, info.note
    );
    assert_eq!(info.backend, Backend::Svn);
    // Subversion keeps PER-PATH base revisions: the working-copy ROOT is still at
    // r0 after committing a file (that is exactly what `svn info` answers), and
    // the next `svn update` moves it to the repository revision. The panel shows
    // the tool's own answer; the evidence README records that semantics.
    assert_eq!(
        info.revision.as_deref(),
        Some("0"),
        "svn info reports the working-copy root's base revision"
    );
    assert_eq!(
        info.branch.as_deref(),
        Some("repo"),
        "the URL's last segment"
    );
    let diff = service.file_diff(&wc, "a.sv");
    eprintln!("EVIDENCE product diff note={:?}\n{}", diff.note, diff.text);
    assert!(
        diff.text.contains("// changed"),
        "the diff must contain the change: {:?}",
        diff.text
    );
    assert!(!diff.binary);

    // --- SvnRevert ---------------------------------------------------------
    let mut revert = request(&wc, WriteOp::SvnRevert);
    revert.paths = vec!["a.sv".to_string()];
    product_write("svn revert", &revert, true);
    assert_eq!(
        read_file(&wc, "a.sv"),
        "module a;\nendmodule\n",
        "svn revert restores the committed content"
    );
    let clean = must(&svn, &wc, &["status"]);
    assert!(
        clean.trim().is_empty(),
        "the working copy is clean: {clean:?}"
    );

    // --- another working copy commits; SvnUpdate brings it in --------------
    assert!(write_file(&wc2, "b.sv", "module b;\nendmodule\n"));
    must(&svn, &wc2, &["add", "b.sv"]);
    must(&svn, &wc2, &["commit", "-q", "-m", "second revision"]);
    let update = request(&wc, WriteOp::SvnUpdate);
    product_write("svn update", &update, true);
    assert!(
        wc.join("b.sv").is_file(),
        "svn update brings the new file in"
    );
    let info_after_update = service.info(&wc);
    assert_eq!(
        info_after_update.revision.as_deref(),
        Some("2"),
        "svn update moves the working-copy root to the repository revision"
    );
    let status_after_update = status(&wc);
    assert_eq!(status_after_update.backend, Backend::Svn);
    assert!(
        status_after_update.changes.is_empty(),
        "an updated working copy is clean: {:?}",
        status_after_update.changes
    );

    // --- SvnRemove + SvnCommit --------------------------------------------
    let mut remove = request(&wc, WriteOp::SvnRemove);
    remove.paths = vec!["b.sv".to_string()];
    product_write("svn remove", &remove, true);
    let removed = must(&svn, &wc, &["status"]);
    assert!(
        removed.contains("D "),
        "the file is scheduled for deletion: {removed:?}"
    );
    let mut commit = request(&wc, WriteOp::SvnCommit);
    commit.message = Some("remove b".to_string());
    product_write("svn commit remove", &commit, true);
    assert!(!wc.join("b.sv").exists());
    let log_all = must(&svn, &wc, &["log", "--xml", &repo_url]);
    eprintln!("EVIDENCE svn log after remove:\n{log_all}");
    assert_eq!(
        log_all.matches("<logentry").count(),
        3,
        "three revisions exist: {log_all:?}"
    );

    // --- a failing operation is a TOOL failure with the real reason --------
    let mut missing = request(&wc, WriteOp::SvnRemove);
    missing.paths = vec!["missing.sv".to_string()];
    let failed = product_write("svn remove missing", &missing, false);
    eprintln!("EVIDENCE svn remove missing stderr={:?}", failed.stderr);
    assert!(
        failed.stderr.to_lowercase().contains("not found")
            || failed
                .stderr
                .to_lowercase()
                .contains("not under version control"),
        "svn's real reason must be preserved: {:?}",
        failed.stderr
    );
    // The failure is NOT a parameter refusal: the request reached the tool.
    assert!(execute(&missing, &SystemWriteRunner).is_ok());

    // --- a plain directory is still the empty state, not a failure ---------
    let plain = root.join("plain");
    assert!(std::fs::create_dir(&plain).is_ok());
    let plain_status = status(&plain);
    assert_eq!(plain_status.backend, Backend::None);
    assert!(plain_status.note.is_some());

    // --- the parameter contract stays before any process -------------------
    let mut no_message = request(&wc, WriteOp::SvnCommit);
    no_message.message = Some("   ".to_string());
    assert!(matches!(
        no_message.invocation(),
        Err(WriteError::Invalid(_))
    ));
}
