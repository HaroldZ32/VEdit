//! Exercise the real host Subversion backend and observe changes on disk.
//!
//! The test looks for the product's host `svn` and a fixture `svnadmin`.
//! `VEDIT_SVN_BIN` and `VEDIT_SVNADMIN` can locate external fixtures when the
//! build image has neither. Without both tools the test prints a skip reason.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::service::{ProcessVcsService, VcsService};
use vd_vcs::svn_read::{self, PatchScope};
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

fn exercise_svn_parity(root: &Path, repository: &str, svn: &Path) -> Result<(), String> {
    let trunk = format!("{repository}/trunk");
    let branches = format!("{repository}/branches");
    must(
        svn,
        root,
        &["mkdir", "-m", "Create layout", &trunk, &branches],
    );
    let mut checkout = request(root, WriteOp::SvnCheckout);
    checkout.url = Some(trunk.clone());
    checkout.target = Some("work branch".into());
    product_write("checkout", &checkout, true);
    let wc = root.join("work branch");
    assert!(wc.join(".svn/wc.db").is_file());
    assert!(matches!(checkout.invocation(), Err(WriteError::Invalid(_))));
    checkout.target = Some("forbidden".into());
    checkout.url = Some("https://borrowed:secret@example.invalid/source".into());
    assert!(matches!(checkout.invocation(), Err(WriteError::Invalid(_))));

    assert!(write_file(
        &wc,
        "a space.sv",
        "module previous; endmodule\n"
    ));
    assert!(write_file(&wc, "b.sv", "module b; endmodule\n"));
    let mut add = request(&wc, WriteOp::SvnAdd);
    add.paths = vec!["a space.sv".into(), "b.sv".into()];
    product_write("add two files", &add, true);
    let mut commit = request(&wc, WriteOp::SvnCommit);
    commit.message = Some("Seed source & scope".into());
    product_write("commit initial", &commit, true);
    assert!(write_file(
        &wc,
        "a space.sv",
        "module selected; endmodule\n"
    ));
    assert!(write_file(&wc, "b.sv", "module outside; endmodule\n"));

    let mut list = request(&wc, WriteOp::SvnChangelist);
    list.name = Some("Second group".into());
    list.paths = vec!["b.sv".into()];
    product_write("set changelist", &list, true);
    let entries = svn_read::status(&wc, false)?;
    assert!(entries
        .iter()
        .any(|row| row.path == "b.sv" && row.changelist.as_deref() == Some("Second group")));
    let selected = svn_read::patch(&wc, PatchScope::Paths(&["a space.sv"]))?;
    assert!(selected.contains("module selected") && !selected.contains("module outside"));
    let all = svn_read::patch(&wc, PatchScope::All)?;
    assert!(all.contains("module selected") && all.contains("module outside"));
    let grouped = svn_read::patch(&wc, PatchScope::Changelist("Second group"))?;
    assert!(!grouped.contains("module selected") && grouped.contains("module outside"));
    assert!(svn_read::patch(&wc, PatchScope::Paths(&["../repo"])).is_err());
    eprintln!("EVIDENCE reads selected/all/changelist scopes match fixture");

    commit.paths = vec!["a space.sv".into()];
    commit.message = Some("Commit only selected scope".into());
    product_write("commit selected", &commit, true);
    assert!(!must(svn, &wc, &["status"]).contains("a space.sv"));
    assert!(must(svn, &wc, &["status"]).contains("b.sv"));
    commit.paths.clear();
    commit.name = Some("Second group".into());
    commit.message = Some("Commit named group".into());
    product_write("commit changelist", &commit, true);
    assert!(must(svn, &wc, &["status"]).is_empty());
    product_write("set changelist again", &list, true);
    let mut remove_list = request(&wc, WriteOp::SvnChangelistRemove);
    remove_list.paths = list.paths;
    product_write("remove changelist", &remove_list, true);

    let mut branch = request(&wc, WriteOp::SvnBranchCreate);
    branch.target = Some("^/branches/feature".into());
    branch.message = Some("Create feature".into());
    product_write("create branch", &branch, true);
    let mut switch = request(&wc, WriteOp::SvnSwitch);
    switch.target = branch.target;
    product_write("switch to branch", &switch, true);
    assert!(must(svn, &wc, &["info", "--show-item", "url"]).contains("/branches/feature"));
    assert!(write_file(
        &wc,
        "a space.sv",
        "module branch_side; endmodule\n"
    ));
    commit.name = None;
    commit.message = Some("Branch side".into());
    product_write("commit feature change", &commit, true);
    switch.target = Some("^/trunk".into());
    product_write("switch back", &switch, true);
    assert!(write_file(
        &wc,
        "a space.sv",
        "module trunk_side; endmodule\n"
    ));
    commit.message = Some("Trunk side".into());
    product_write("commit trunk change", &commit, true);
    product_write(
        "update trunk before merging",
        &request(&wc, WriteOp::SvnUpdate),
        true,
    );
    let mut merge = request(&wc, WriteOp::SvnMerge);
    merge.target = Some("^/branches/feature".into());
    product_write("merge creates real text conflict", &merge, true);
    assert!(svn_read::status(&wc, false)?
        .iter()
        .any(|row| row.kind == 5 && row.path == "a space.sv"));
    assert!(write_file(
        &wc,
        "a space.sv",
        "module resolved; endmodule\n"
    ));
    let mut resolve = request(&wc, WriteOp::SvnResolve);
    resolve.paths = vec!["a space.sv".into()];
    product_write("mark working contents resolved", &resolve, true);
    assert!(!svn_read::status(&wc, false)?
        .iter()
        .any(|row| row.kind == 5));

    let mut rename = request(&wc, WriteOp::SvnRename);
    rename.paths = vec!["b.sv".into()];
    rename.target = Some("moved name@.sv".into());
    product_write("rename with spaces and at sign", &rename, true);
    assert!(wc.join("moved name@.sv").is_file() && !wc.join("b.sv").exists());
    assert!(write_file(&wc, "generated.tmp", "ignore me\n"));
    assert!(svn_read::status(&wc, false)?
        .iter()
        .any(|row| row.kind == 7 && row.path == "generated.tmp"));
    must(svn, &wc, &["propset", "svn:ignore", "existing.log", "."]);
    let mut ignore = request(&wc, WriteOp::SvnIgnore);
    ignore.paths = vec!["generated.tmp".into()];
    product_write("append svn:ignore", &ignore, true);
    let property = must(svn, &wc, &["propget", "svn:ignore", "."]);
    assert!(property.contains("existing.log") && property.contains("generated.tmp"));
    assert!(!svn_read::status(&wc, false)?
        .iter()
        .any(|row| row.path == "generated.tmp"));
    product_write("cleanup", &request(&wc, WriteOp::SvnCleanup), true);
    assert!(wc.join("generated.tmp").is_file());
    let mut delete = request(&wc, WriteOp::SvnDeleteUnversioned);
    assert!(write_file(&wc, "remove-me.tmp", "scratch\n"));
    delete.paths = vec!["remove-me.tmp".into(), "a space.sv".into()];
    assert!(vd_vcs::write::write(&delete).is_err());
    assert!(wc.join("remove-me.tmp").is_file());
    delete.paths.pop();
    assert!(vd_vcs::write::write(&delete).is_ok());
    assert!(!wc.join("remove-me.tmp").exists());
    assert!(std::fs::create_dir_all(wc.join("scratch/subdir")).is_ok());
    assert!(write_file(
        &wc,
        "scratch/subdir/content.tmp",
        "nested scratch\n"
    ));
    assert!(std::fs::create_dir_all(wc.join("safeguard/nested/.git")).is_ok());
    delete.paths = vec!["scratch".into(), "safeguard".into()];
    assert!(vd_vcs::write::write(&delete).is_err());
    assert!(wc.join("scratch/subdir/content.tmp").exists());
    delete.paths.pop();
    assert!(vd_vcs::write::write(&delete).is_ok());
    assert!(!wc.join("scratch").exists() && wc.join("safeguard/nested/.git").exists());
    eprintln!("EVIDENCE scoped unversioned deletion refuses tracked and nested-repository paths, and deletes the selected scratch directory");
    must(
        svn,
        &wc,
        &["propset", "svn:ignore", "generated.tmp\nsafeguard", "."],
    );

    let secondary = root.join("observer");
    must(svn, root, &["checkout", "-q", &trunk, "observer"]);
    commit.message = Some("Merge with resolved contents".into());
    product_write("commit resolved merge", &commit, true);
    let incoming = svn_read::status(&secondary, true)?;
    assert!(incoming.iter().any(|row| row.path == "a space.sv"));
    let history = svn_read::log(&wc, "Merge with resolved", 20)?;
    assert_eq!(history.len(), 1);
    let number = history[0].revision.to_string();
    assert_eq!(svn_read::log(&wc, &format!("r{number}"), 20)?.len(), 1);
    assert!(svn_read::revision_detail(&wc, &number)?
        .changes
        .iter()
        .any(|change| change.path == "/trunk/a space.sv"));
    eprintln!(
        "EVIDENCE real incoming status, text search, revision search, and changed paths matched"
    );
    let bookmark = vd_vcs::svn_remote::Remote::new(root, &trunk)?;
    assert!(bookmark
        .log("Merge with resolved", 10)?
        .iter()
        .any(|entry| entry.revision.to_string() == number));
    let revision = history[0].revision;
    let detail = bookmark.detail(&number)?;
    assert!(detail
        .changes
        .iter()
        .any(|change| change.path == "/trunk/a space.sv"));
    let previous = bookmark
        .previous("/trunk/a space.sv", revision, revision)?
        .ok_or("missing previous remote revision")?;
    assert!(bookmark
        .snapshot("/trunk/a space.sv", previous, revision)?
        .is_some_and(|text| text.contains("trunk_side")));
    assert!(bookmark
        .snapshot("/trunk/a space.sv", revision, revision)?
        .is_some_and(|text| text.contains("module resolved")));
    assert!(bookmark
        .snapshot("/trunk/b.sv", revision, revision - 1)?
        .is_none());
    assert!(bookmark
        .snapshot("/trunk/../branches", revision, revision)
        .is_err());
    eprintln!(
        "EVIDENCE saved remote log, search, revision path, previous content and deletion matched"
    );
    Ok(())
}

#[test]
fn host_subversion_reads_and_writes_run_through_the_product_path() {
    let svn = vd_vcs::toolchain::resolve("svn")
        .map(|tool| tool.path)
        .or_else(|| executable_from_env("VEDIT_SVN_BIN"));
    let svnadmin = executable_from_env("VEDIT_SVNADMIN").or_else(|| {
        let path = std::env::var_os("PATH")?;
        std::env::split_paths(&path)
            .map(|dir| dir.join("svnadmin"))
            .find(|path| path.is_absolute() && path.is_file())
    });
    let (Some(svn), Some(svnadmin)) = (svn, svnadmin) else {
        eprintln!("SKIP host Subversion E2E: install svn and svnadmin or set VEDIT_SVN_BIN and VEDIT_SVNADMIN");
        return;
    };
    let (svn_ok, svn_version) = run_tool(&svn, Path::new("/tmp"), &["--version", "--quiet"]);
    let (admin_ok, admin_version) =
        run_tool(&svnadmin, Path::new("/tmp"), &["--version", "--quiet"]);
    assert!(svn_ok && admin_ok, "the host Subversion tools do not run");
    eprintln!(
        "EVIDENCE host svn={} svnadmin={}",
        svn_version.trim(),
        admin_version.trim()
    );
    std::env::set_var("VEDIT_SVN", &svn);

    let Ok(temp) = tempfile::tempdir() else {
        eprintln!("SKIP host Subversion E2E: no temporary directory");
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

    if let Err(error) = exercise_svn_parity(&root, &repo_url, &svn) {
        panic!("SVN parity read failed: {error}");
    }

    // --- the parameter contract stays before any process -------------------
    let mut no_message = request(&wc, WriteOp::SvnCommit);
    no_message.message = Some("   ".to_string());
    assert!(matches!(
        no_message.invocation(),
        Err(WriteError::Invalid(_))
    ));
}
