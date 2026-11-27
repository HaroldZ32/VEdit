//! Real local repository with a client that rejects options absent from SVN 1.7.

#![cfg(unix)]

use std::error::Error;
use std::ffi::OsStr;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use vd_vcs::service::{ProcessVcsService, VcsService};
use vd_vcs::svn_compare::{self, Revision};
use vd_vcs::svn_read::{self, NodeKind, PathAction};
use vd_vcs::svn_remote::Remote;

fn tool(name: &str, variable: &str) -> Option<PathBuf> {
    std::env::var_os(variable).map(PathBuf::from).or_else(|| {
        std::env::split_paths(&std::env::var_os("PATH")?)
            .map(|dir| dir.join(name))
            .find(|path| path.is_absolute() && path.is_file())
    })
}

fn run(program: &Path, cwd: &Path, args: &[&str]) -> Output {
    Command::new(program)
        .current_dir(cwd)
        .args(args)
        .output()
        .unwrap_or_else(|error| panic!("start fixture client {}: {error}", program.display()))
}

fn must(program: &Path, cwd: &Path, args: &[&str]) -> String {
    let output = run(program, cwd, args);
    assert!(
        output.status.success(),
        "{args:?}: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    String::from_utf8(output.stdout)
        .unwrap_or_else(|error| panic!("fixture command {args:?} returned non-UTF-8: {error}"))
}

#[test]
fn checkout_history_and_comparison_work_without_recent_client_options() -> Result<(), Box<dyn Error>>
{
    let (Some(svn), Some(svnadmin)) = (
        tool("svn", "VEDIT_SVN_BIN"),
        tool("svnadmin", "VEDIT_SVNADMIN"),
    ) else {
        eprintln!("SKIP SVN comparison E2E: both svn and svnadmin are required");
        return Ok(());
    };
    let temp = tempfile::tempdir()?;
    let dir = temp.path();
    let repository = format!("file://{}/repo", dir.display());
    let trunk = format!("{repository}/trunk");
    let sibling = format!("{repository}/trunkmate");
    let wc = dir.join("wc");
    let project = wc.join("opened & 中文%");
    let repository_project = "/trunk/opened & 中文%";
    let sibling_wc = dir.join("sibling");
    must(&svnadmin, dir, &["create", "repo"]);
    must(
        &svn,
        dir,
        &["mkdir", &trunk, &sibling, "-m", "create directories"],
    );
    must(&svn, dir, &["checkout", &trunk, "wc"]);
    must(&svn, dir, &["checkout", &sibling, "sibling"]);
    std::fs::create_dir(&project)?;
    let name = "a space @ & 中文%.sv";
    let literal = format!("{name}@");
    std::fs::write(project.join(name), "pristine\n")?;
    std::fs::write(project.join("delete.sv"), "removed file\n")?;
    std::fs::write(project.join("replace.sv"), "old replaced file\n")?;
    std::fs::write(project.join("working-delete.sv"), "working base\n")?;
    std::fs::write(project.join("move-old.sv"), "old name content\n")?;
    std::fs::write(project.join("binary.sv"), b"has a NUL\0")?;
    std::fs::write(project.join("non-utf8.sv"), [0xff, 0xfe])?;
    std::fs::write(wc.join("side.sv"), "outside project\n")?;
    must(&svn, &wc, &["add", "--", "opened & 中文%", "side.sv"]);
    must(&svn, &wc, &["commit", "-m", "initial file"]);
    must(&svn, &wc, &["update", "-q"]);

    std::fs::write(project.join(name), "changed\n")?;
    std::fs::write(project.join("add.sv"), "new file\n")?;
    let moved = "move-new @ 中文 & %.sv";
    must(&svn, &project, &["move", "--", "move-old.sv", moved]);
    let copied = "copied @ 中文 & %.sv";
    let copied_literal = format!("{copied}@");
    must(
        &svn,
        &project,
        &["copy", "-r", "2", "--", &literal, &copied_literal],
    );
    must(&svn, &project, &["delete", "delete.sv", "replace.sv"]);
    std::fs::write(project.join("replace.sv"), "new replaced file\n")?;
    must(&svn, &project, &["add", "add.sv", "replace.sv"]);
    std::fs::write(wc.join("side.sv"), "changed outside\n")?;
    must(&svn, &wc, &["commit", "-m", "Mixed project change"]);
    std::fs::write(sibling_wc.join("outside.sv"), "outside checkout\n")?;
    must(&svn, &sibling_wc, &["add", "outside.sv"]);
    must(
        &svn,
        &sibling_wc,
        &["commit", "-m", "sibling-only revision"],
    );
    must(&svn, &wc, &["update", "-q"]);

    std::fs::write(project.join(name), "dirty working file\n")?;
    std::fs::write(project.join("pending.sv"), "working addition\n")?;
    must(&svn, &project, &["add", "pending.sv"]);
    must(&svn, &project, &["copy", "--", &literal, "pending-copy.sv"]);
    must(&svn, &project, &["delete", "working-delete.sv"]);
    let version = must(&svn, dir, &["--version", "--quiet"]);
    eprintln!("EVIDENCE underlying SVN client: {}", version.trim());

    let bin = dir.join("bin");
    std::fs::create_dir(&bin)?;
    let wrapper = bin.join("svn");
    std::fs::write(
        &wrapper,
        r#"#!/bin/sh
for arg in "$@"; do
    case "$arg" in
        --show-item|--no-newline|--search)
            printf "svn: invalid option: %s\n" "$arg" >&2
            exit 1 ;;
        --ignore-keywords)
            printf "Subcommand '%s' doesn't accept option '%s'\n" "$1" "$arg" >&2
            exit 1 ;;
    esac
    [ "$arg" != '--' ] || break
done
exec "$VD_TEST_REAL_SVN" "$@"
"#,
    )?;
    std::fs::set_permissions(&wrapper, std::fs::Permissions::from_mode(0o755))?;
    std::env::set_var("VD_TEST_REAL_SVN", &svn);
    let real_legacy = std::env::var_os("VEDIT_TEST_LEGACY_SVN_DIRECT").is_some();
    let mut paths = if real_legacy {
        vec![svn
            .parent()
            .ok_or("SVN client is not an absolute path")?
            .to_owned()]
    } else {
        vec![bin]
    };
    std::env::set_var("VEDIT_SVN", &svn);
    paths.extend(std::env::split_paths(
        &std::env::var_os("PATH").unwrap_or_else(|| OsStr::new("/usr/bin:/bin").to_owned()),
    ));
    let path = std::env::join_paths(paths)?;
    std::env::set_var("PATH", &path);
    std::env::set_var("VEDIT_HOST_PATH", &path);

    let control_client = if real_legacy { &svn } else { &wrapper };
    let control = run(control_client, &wc, &["info", "--show-item", "url", "."]);
    assert!(!control.status.success());
    let detail = String::from_utf8_lossy(&control.stderr);
    assert!(detail.contains("--show-item"));
    eprintln!("EVIDENCE negative control: {}", detail.trim());

    assert_eq!(svn_read::checkout_repository_path(&wc)?, "/trunk");
    assert_eq!(
        svn_read::checkout_repository_path(&project)?,
        repository_project
    );
    assert_eq!(svn_read::checkout_repository_url(&project)?, repository);
    let status = svn_read::status(&project, false)?;
    for (filename, kind, base) in [
        (name, 2, Some("changed\n")),
        ("pending.sv", 1, None),
        ("pending-copy.sv", 1, Some("changed\n")),
        ("working-delete.sv", 3, Some("working base\n")),
    ] {
        let path = project.join(filename);
        let wc_path = path
            .strip_prefix(&wc)?
            .to_str()
            .ok_or("non-UTF-8 fixture path")?;
        assert!(status
            .iter()
            .any(|row| row.path == wc_path && row.kind == kind));
        let snapshot = svn_compare::snapshot(
            &project,
            path.to_str().ok_or("non-UTF-8 fixture path")?,
            Revision::Base,
            None,
        )?;
        assert_eq!(snapshot.as_deref(), base, "BASE for {filename}");
    }
    let identity = ProcessVcsService::production().info(&project);
    assert_eq!(identity.branch.as_deref(), Some("trunk"));
    assert_eq!(identity.revision.as_deref(), Some("4"));
    eprintln!("EVIDENCE working tree status and exact BASE for modified, added, copied-add and deleted files");

    let revisions = |query| -> Result<Vec<_>, String> {
        Ok(svn_read::log(&project, query, 10)?
            .into_iter()
            .map(|e| e.revision)
            .collect())
    };
    assert_eq!(revisions("")?, [3, 2]);
    assert_eq!(revisions("r3")?, [3]);
    assert_eq!(revisions("r4")?, []);
    assert_eq!(revisions("mixed project")?, [3]);
    let detail = svn_read::revision_detail(&project, "r3")?;
    assert_eq!(detail.repository_root, repository);
    assert_eq!(detail.repository_path, repository_project);
    assert_eq!(detail.entry.revision, 3);
    assert_eq!(
        detail.changes.len(),
        7,
        "same-revision siblings must be excluded"
    );
    for (file, action, copy_from) in [
        (name, PathAction::Modified, None),
        ("add.sv", PathAction::Added, None),
        ("delete.sv", PathAction::Deleted, None),
        ("replace.sv", PathAction::Replaced, None),
        ("move-old.sv", PathAction::Deleted, None),
        (moved, PathAction::Added, Some("move-old.sv")),
        (copied, PathAction::Added, Some(name)),
    ] {
        let path = format!("{repository_project}/{file}");
        let change = detail
            .changes
            .iter()
            .find(|change| change.path == path)
            .ok_or_else(|| format!("missing changed path {path}"))?;
        assert_eq!(change.action, action);
        assert_eq!(change.kind, NodeKind::File);
        let source = change
            .copy_from
            .as_ref()
            .map(|copy| (copy.path.as_str(), copy.revision));
        let expected_path = copy_from.map(|name| format!("{repository_project}/{name}"));
        assert_eq!(source, expected_path.as_deref().map(|path| (path, 2)));
    }
    eprintln!("EVIDENCE project log r3/r2 and seven in-subtree A/D/M/R rows with decoded paths and copy/move origins");

    let work_path = |file| project.join(file).display().to_string();
    let history = svn_compare::history(&project, &work_path(name), None, None, 10)?;
    assert_eq!(history.revisions, [3, 2]);
    assert_eq!(history.peg, 4);
    let history = svn_compare::history(&project, &work_path("delete.sv"), None, None, 10)?;
    assert_eq!(history.revisions, [3, 2]);
    assert_eq!(history.peg, 2);
    let history = svn_compare::history(&project, &work_path(copied), None, None, 10)?;
    assert_eq!(history.revisions, [3, 2]);
    let remote = Remote::new(dir, &repository)?;
    for (file, revision, peg, expected) in [
        (name, 2, 2, Some("pristine\n")),
        (name, 3, 3, Some("changed\n")),
        ("add.sv", 2, 2, None),
        ("add.sv", 3, 3, Some("new file\n")),
        ("delete.sv", 2, 2, Some("removed file\n")),
        ("delete.sv", 3, 3, None),
        ("replace.sv", 2, 2, Some("old replaced file\n")),
        ("replace.sv", 3, 3, Some("new replaced file\n")),
        (copied, 2, 3, Some("pristine\n")),
        (copied, 3, 3, Some("pristine\n")),
        ("move-old.sv", 2, 2, Some("old name content\n")),
        ("move-old.sv", 3, 3, None),
        (moved, 3, 3, Some("old name content\n")),
    ] {
        let snapshot = remote.snapshot(&format!("{repository_project}/{file}"), revision, peg)?;
        assert_eq!(snapshot.as_deref(), expected, "{file}@{peg} at r{revision}");
    }
    let saved = Remote::new(dir, &trunk)?;
    assert_eq!(saved.detail("3")?.repository_root, repository);
    assert_eq!(
        saved
            .snapshot(&format!("{repository_project}/{name}"), 2, 2)?
            .as_deref(),
        Some("pristine\n")
    );
    for (name, reason) in [
        ("binary.sv", "binary"),
        ("non-utf8.sv", "unsupported encoding"),
    ] {
        for result in [
            svn_compare::snapshot(&project, &work_path(name), Revision::Base, None),
            remote.snapshot(&format!("{repository_project}/{name}"), 2, 2),
        ] {
            assert!(matches!(result, Err(error) if error.contains(reason)));
        }
    }
    eprintln!("EVIDENCE file histories and exact comparison pegs matched and unsupported text was rejected");

    must(&svn, dir, &["checkout", "-r", "4", &trunk, "stale"]);
    let stale = dir.join("stale/opened & 中文%");
    let stale_file = stale.join(name).display().to_string();
    std::fs::write(project.join(name), "later edit of the original node\n")?;
    must(
        &svn,
        &project,
        &["commit", "-m", "later edit", "--", &literal],
    );
    let history = svn_compare::history(&stale, &stale_file, None, None, 10)?;
    assert_eq!(history.revisions, [5, 3, 2]);
    assert_eq!(history.peg, 5);
    must(&svn, &project, &["delete", "--", &literal]);
    must(
        &svn,
        &project,
        &["commit", "-m", "delete original", "--", &literal],
    );
    std::fs::write(project.join(name), "unrelated reused name\n")?;
    must(&svn, &project, &["add", "--", &literal]);
    must(
        &svn,
        &project,
        &["commit", "-m", "reuse deleted name", "--", &literal],
    );
    let history = svn_compare::history(&stale, &stale_file, None, None, 10)?;
    assert_eq!(
        history.revisions,
        [6, 5, 3, 2],
        "history must follow the stale WC node through deletion"
    );
    assert_eq!(history.peg, 5);
    for (revision, expected) in [(5, Some("later edit of the original node\n")), (6, None)] {
        let snapshot = svn_compare::snapshot(
            &stale,
            &stale_file,
            Revision::Number(revision),
            Some(history.peg),
        )?;
        assert_eq!(snapshot.as_deref(), expected);
    }
    eprintln!("EVIDENCE stale WC at r4 sees its original node's r5 edit and r6 deletion, excluding unrelated r7 re-add");
    Ok(())
}
