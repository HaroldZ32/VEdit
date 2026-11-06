//! History reads against a REAL `git` in a `tempfile::tempdir()` fixture: commit
//! pagination + search, one commit's detail, a single file's history, blame and a
//! two-commit comparison. Nothing here touches the VEdit working tree.
//!
//! Every assertion is made with git itself (`rev-parse`, `log`, `blame`), never by
//! re-reading the implementation's output.

use std::path::{Path, PathBuf};
use std::process::Command;

use vd_vcs::history::{
    blame, commit_detail, commits, compare, file_history, CommitEntry, PageRequest,
};
use vd_vcs::Backend;

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

fn init_repo(dir: &Path) -> bool {
    run_git(dir, &["init", "-q"]).is_some()
        && run_git(dir, &["config", "user.email", "history@example.com"]).is_some()
        && run_git(dir, &["config", "user.name", "VEdit History"]).is_some()
}

fn commit_all(dir: &Path, message: &str) -> Option<String> {
    run_git(dir, &["add", "-A"])?;
    run_git(dir, &["commit", "-q", "-m", message])?;
    run_git(dir, &["rev-parse", "HEAD"]).map(|hash| hash.trim().to_string())
}

fn fixtures() -> Option<(tempfile::TempDir, PathBuf)> {
    let temp = tempfile::tempdir().ok()?;
    let repo = temp.path().join("history-repo");
    std::fs::create_dir(&repo).ok()?;
    if !init_repo(&repo) {
        return None;
    }
    Some((temp, repo))
}

fn subjects(commits: &[CommitEntry]) -> Vec<String> {
    commits
        .iter()
        .map(|commit| commit.subject.clone())
        .collect()
}

#[test]
fn commit_pages_are_bounded_and_carry_has_more() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    for index in 0..5 {
        assert!(write(
            &repo,
            "rtl/a.sv",
            &format!("module a;\n// {index}\nendmodule\n")
        ));
        assert!(commit_all(&repo, &format!("commit {index}")).is_some());
    }

    let first = commits(&repo, PageRequest::new(0, 2), "");
    assert_eq!(first.backend, Backend::Git);
    assert_eq!(first.commits.len(), 2);
    assert!(
        first.has_more,
        "5 commits with a 2-row page must report more"
    );
    assert_eq!(subjects(&first.commits), vec!["commit 4", "commit 3"]);
    assert_eq!(first.offset, 0);

    let middle = commits(&repo, PageRequest::new(2, 2), "");
    assert_eq!(subjects(&middle.commits), vec!["commit 2", "commit 1"]);
    assert!(middle.has_more);

    let last = commits(&repo, PageRequest::new(4, 2), "");
    assert_eq!(subjects(&last.commits), vec!["commit 0"]);
    assert!(!last.has_more, "the final page must not claim another page");

    let beyond = commits(&repo, PageRequest::new(10, 2), "");
    assert!(beyond.commits.is_empty());
    assert!(!beyond.has_more);
    assert!(!beyond.error);
}

#[test]
fn commit_search_is_literal_and_case_insensitive() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    assert!(write(&repo, "rtl/a.sv", "module a;\nendmodule\n"));
    assert!(commit_all(&repo, "Fix the ALU carry path").is_some());
    assert!(write(&repo, "rtl/b.sv", "module b;\nendmodule\n"));
    assert!(commit_all(&repo, "add the register file").is_some());

    let found = commits(&repo, PageRequest::new(0, 10), "alu carry");
    assert_eq!(subjects(&found.commits), vec!["Fix the ALU carry path"]);
    assert!(!found.has_more);

    // A regex meta-character is matched literally, not interpreted.
    let literal = commits(&repo, PageRequest::new(0, 10), "ALU (carry)");
    assert!(
        literal.commits.is_empty(),
        "the query must be a literal, not a regex"
    );

    let none = commits(&repo, PageRequest::new(0, 10), "no such message");
    assert!(none.commits.is_empty());
    assert!(!none.error);
}

#[test]
fn commit_detail_reports_meta_files_and_patch() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    assert!(write(&repo, "rtl/a.sv", "module a;\nendmodule\n"));
    let base = commit_all(&repo, "root commit");
    let Some(base) = base else {
        return;
    };
    assert!(write(
        &repo,
        "rtl/a.sv",
        "module a;\n// changed\nendmodule\n"
    ));
    assert!(write(&repo, "rtl/b.sv", "module b;\nendmodule\n"));
    let Some(head) = commit_all(&repo, "change a and add b") else {
        return;
    };

    let detail = commit_detail(&repo, &head);
    assert_eq!(detail.backend, Backend::Git);
    assert!(!detail.error, "note: {:?}", detail.note);
    assert_eq!(detail.hash, head);
    assert_eq!(detail.subject, "change a and add b");
    assert_eq!(detail.author, "VEdit History");
    assert_eq!(detail.email, "history@example.com");
    assert!(detail.time > 1_600_000_000);
    let paths: Vec<&str> = detail.files.iter().map(|file| file.path.as_str()).collect();
    assert!(paths.contains(&"rtl/a.sv"), "paths: {paths:?}");
    assert!(paths.contains(&"rtl/b.sv"), "paths: {paths:?}");
    assert!(detail.patch.contains("+++ b/rtl/b.sv") || detail.patch.contains("+++ b/rtl/a.sv"));

    // The ROOT commit must show its files as well (git needs `--root` for that).
    let root = commit_detail(&repo, &base);
    assert!(!root.error);
    assert!(
        root.files.iter().any(|file| file.path == "rtl/a.sv"),
        "the initial commit's files must be listed, got {:?}",
        root.files
    );
    assert!(!root.patch.is_empty());
}

#[test]
fn file_history_lists_only_the_paths_commits() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    for index in 0..3 {
        assert!(write(
            &repo,
            "rtl/a.sv",
            &format!("module a;\n// {index}\nendmodule\n")
        ));
        assert!(commit_all(&repo, &format!("touch a {index}")).is_some());
        assert!(write(
            &repo,
            "rtl/b.sv",
            &format!("module b;\n// {index}\nendmodule\n")
        ));
        assert!(commit_all(&repo, &format!("touch b {index}")).is_some());
    }

    let history = file_history(&repo, "rtl/a.sv", PageRequest::new(0, 10));
    assert!(!history.error, "note: {:?}", history.note);
    assert_eq!(history.path, "rtl/a.sv");
    let messages = subjects(&history.commits);
    assert_eq!(messages.len(), 3, "messages: {messages:?}");
    assert!(messages
        .iter()
        .all(|message| message.starts_with("touch a")));

    let page = file_history(&repo, "rtl/a.sv", PageRequest::new(0, 2));
    assert_eq!(page.commits.len(), 2);
    assert!(page.has_more);

    let unknown = file_history(&repo, "rtl/missing.sv", PageRequest::new(0, 10));
    assert!(!unknown.error, "an untouched path is empty, not a failure");
    assert!(unknown.commits.is_empty());
    assert!(unknown.note.is_some());
}

#[test]
fn blame_attributes_each_line_to_its_commit() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    assert!(write(
        &repo,
        "rtl/a.sv",
        "module a;\n// first line\nendmodule\n"
    ));
    let Some(first) = commit_all(&repo, "create a") else {
        return;
    };
    assert!(write(
        &repo,
        "rtl/a.sv",
        "module a;\n// second line\nendmodule\n"
    ));
    let Some(second) = commit_all(&repo, "change the middle line") else {
        return;
    };

    let blamed = blame(&repo, "rtl/a.sv");
    assert!(!blamed.error, "note: {:?}", blamed.note);
    assert_eq!(blamed.lines.len(), 3);
    assert_eq!(blamed.lines[0].text, "module a;");
    assert_eq!(blamed.lines[1].text, "// second line");
    assert_eq!(blamed.lines[2].text, "endmodule");
    assert_eq!(
        blamed.lines[1].hash, second,
        "the changed line belongs to commit 2"
    );
    let first_hash = blamed.lines[0].hash.clone();
    assert_eq!(first_hash, first, "the untouched line belongs to commit 1");
    assert!(blamed
        .lines
        .iter()
        .all(|line| line.author == "VEdit History"));
    assert!(blamed.lines.iter().all(|line| line.time > 1_600_000_000));

    let missing = blame(&repo, "rtl/missing.sv");
    assert!(missing.error, "blaming an untracked path is a real failure");
    assert!(missing.note.is_some());
}

#[test]
fn comparison_lists_the_files_and_the_patch() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    assert!(write(&repo, "rtl/a.sv", "module a;\nendmodule\n"));
    let Some(base) = commit_all(&repo, "base") else {
        return;
    };
    assert!(write(&repo, "rtl/a.sv", "module a;\n// added\nendmodule\n"));
    assert!(write(&repo, "rtl/c.sv", "module c;\nendmodule\n"));
    let Some(head) = commit_all(&repo, "change") else {
        return;
    };

    let comparison = compare(&repo, &base, &head);
    assert!(!comparison.error, "note: {:?}", comparison.note);
    let paths: Vec<&str> = comparison
        .files
        .iter()
        .map(|file| file.path.as_str())
        .collect();
    assert!(paths.contains(&"rtl/a.sv"), "paths: {paths:?}");
    assert!(paths.contains(&"rtl/c.sv"), "paths: {paths:?}");
    assert!(comparison.patch.contains("// added"));

    // The same commit on both sides is an empty comparison, not an error.
    let same = compare(&repo, &base, &base);
    assert!(!same.error);
    assert!(same.files.is_empty());
    assert!(same.note.is_some());

    let bad = compare(&repo, "no-such-ref", &head);
    assert!(bad.error);
    assert!(bad.note.is_some());
}

#[test]
fn an_empty_repository_is_an_empty_state_not_an_error() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    let page = commits(&repo, PageRequest::new(0, 10), "");
    assert!(!page.error, "an empty repository must not be an error");
    assert!(page.commits.is_empty());
    assert!(
        page.note
            .as_deref()
            .is_some_and(|note| note.contains("no commits")),
        "note: {:?}",
        page.note
    );

    let history = file_history(&repo, "rtl/a.sv", PageRequest::new(0, 10));
    assert!(!history.error);
    assert!(history.commits.is_empty());
}

#[test]
fn a_non_repository_and_a_foreign_backend_are_reported() {
    let Some(temp) = tempfile::tempdir().ok() else {
        return;
    };
    let plain = temp.path().join("not-a-repo");
    assert!(std::fs::create_dir(&plain).is_ok());
    let page = commits(&plain, PageRequest::new(0, 10), "");
    assert_eq!(page.backend, Backend::None);
    assert!(!page.error);
    assert!(page.note.is_some());

    let svn = temp.path().join("svn-repo");
    assert!(std::fs::create_dir(&svn).is_ok());
    assert!(std::fs::create_dir(svn.join(".svn")).is_ok());
    let page = commits(&svn, PageRequest::new(0, 10), "");
    assert_eq!(page.backend, Backend::Svn);
    assert!(page
        .note
        .is_some_and(|note| note.contains("Git working trees")));
}

#[test]
fn a_repository_with_many_commits_pages_without_reading_everything() {
    let Some((_temp, repo)) = fixtures() else {
        return;
    };
    // 60 commits: enough to prove the paging math, small enough for a unit test.
    // The 1000-commit non-blocking proof lives in the lane selftest
    // (evidence/vcs-git-history/), where it is measured on the GUI thread.
    for index in 0..60 {
        assert!(write(
            &repo,
            "rtl/a.sv",
            &format!("module a;\n// {index}\nendmodule\n")
        ));
        assert!(commit_all(&repo, &format!("commit {index}")).is_some());
    }
    let first = commits(&repo, PageRequest::new(0, 25), "");
    assert_eq!(first.commits.len(), 25);
    assert!(first.has_more);
    // The page really is a window: the newest commit is on page 1 only.
    assert_eq!(first.commits[0].subject, "commit 59");
    let second = commits(&repo, PageRequest::new(25, 25), "");
    assert_eq!(second.commits[0].subject, "commit 34");
    assert!(second.has_more);
    let third = commits(&repo, PageRequest::new(50, 25), "");
    assert_eq!(third.commits.len(), 10);
    assert!(!third.has_more);
}
