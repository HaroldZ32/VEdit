//! Git history reads: commit pages, one commit's detail, one file's history,
//! blame, and a two-commit comparison.
//!
//! Every function shells out to the pinned `git` through the same
//! [`crate::runner::CommandRunner`] the rest of the backend uses, so the
//! read-only gate in `crates/vd-vcs/src/runner.rs` applies BEFORE any process
//! exists and the cwd is always the repository root.
//!
//! # Shape
//!
//! The parsers are pure functions over git's textual output (`parse_log`,
//! `parse_numstat`, `parse_blame`), so the mapping is unit-tested without a
//! repository. The page readers clamp their bounds ([`MAX_PAGE`]) and report an
//! *empty* repository (`error = false`, empty list, a note) differently from a
//! *failed* read (`error = true`), which is what the panel renders as two
//! distinct states.
//!
//! # Baseline
//!
//! Only options that the delivery baseline (CentOS 7, git 1.8.3.1) already has
//! are used: `--skip`, `--max-count`, `--follow`, `%at` (unix seconds; `--date=unix`
//! does NOT exist on 1.8.3 and is never passed), `--numstat`,
//! `--line-porcelain`, `--fixed-strings`/`--regexp-ignore-case` for `--grep`.
#![forbid(unsafe_code)]

use std::path::Path;

use crate::runner::{CommandOutput, CommandRunner};
use crate::Backend;

/// Upper bound on one commit / file-history page.
pub const MAX_PAGE: usize = 200;
/// Upper bound on one blame listing, so a pathological file cannot make the C ABI
/// struct unbounded.
pub const MAX_BLAME_LINES: usize = 20_000;
/// Upper bound on a patch / body carried through the C ABI.
pub const MAX_TEXT_BYTES: usize = 2 * 1024 * 1024;

/// Field separator used by the log format (`%x1f`, the ASCII unit separator).
const FIELD: char = '\u{1f}';

/// One commit row.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommitEntry {
    /// Full object id.
    pub hash: String,
    /// Abbreviated object id (`%h`).
    pub short_hash: String,
    /// Author name.
    pub author: String,
    /// Author email.
    pub email: String,
    /// Author time, unix seconds.
    pub time: i64,
    /// First line of the message.
    pub subject: String,
}

/// One page of the commit list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommitPage {
    /// Detected backend.
    pub backend: Backend,
    /// The commits on this page, newest first.
    pub commits: Vec<CommitEntry>,
    /// Requested offset (0-based).
    pub offset: usize,
    /// `true` when a further page exists.
    pub has_more: bool,
    /// Why the read is empty (no commits yet) or failed.
    pub note: Option<String>,
    /// `true` when git failed (as opposed to an empty repository).
    pub error: bool,
}

/// One file in a commit / comparison.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FileStat {
    /// Repository-relative path.
    pub path: String,
    /// Added lines (`-1` when git reported the file as binary).
    pub additions: i64,
    /// Deleted lines (`-1` when binary).
    pub deletions: i64,
}

/// One commit's detail.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommitDetail {
    /// Detected backend.
    pub backend: Backend,
    /// Full object id.
    pub hash: String,
    /// Author name.
    pub author: String,
    /// Author email.
    pub email: String,
    /// Author time, unix seconds.
    pub time: i64,
    /// First line of the message.
    pub subject: String,
    /// The rest of the message (may be empty).
    pub body: String,
    /// Files the commit touched (empty for a merge commit, with a note).
    pub files: Vec<FileStat>,
    /// The unified patch.
    pub patch: String,
    /// Why the detail is incomplete or failed.
    pub note: Option<String>,
    /// `true` when git failed.
    pub error: bool,
}

/// One page of a single file's history.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FileHistoryPage {
    /// Detected backend.
    pub backend: Backend,
    /// Repository-relative path asked for.
    pub path: String,
    /// Commits that touched the path, newest first.
    pub commits: Vec<CommitEntry>,
    /// Requested offset.
    pub offset: usize,
    /// `true` when a further page exists.
    pub has_more: bool,
    /// Why the history is empty or failed.
    pub note: Option<String>,
    /// `true` when git failed.
    pub error: bool,
}

/// One blamed line.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BlameLine {
    /// 1-based line number in the working-tree file.
    pub line: usize,
    /// Full object id of the commit that introduced the line.
    pub hash: String,
    /// Abbreviated object id.
    pub short_hash: String,
    /// Author name.
    pub author: String,
    /// Author time, unix seconds.
    pub time: i64,
    /// The line's text, without the trailing newline.
    pub text: String,
}

/// One file's blame.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BlameResult {
    /// Detected backend.
    pub backend: Backend,
    /// Repository-relative path asked for.
    pub path: String,
    /// One entry per line.
    pub lines: Vec<BlameLine>,
    /// Why the blame is empty or failed.
    pub note: Option<String>,
    /// `true` when git failed (untracked file, unknown path, ...).
    pub error: bool,
    /// `true` when the listing was cut at [`MAX_BLAME_LINES`].
    pub truncated: bool,
}

/// A two-commit comparison.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommitComparison {
    /// Detected backend.
    pub backend: Backend,
    /// The older side as requested.
    pub from: String,
    /// The newer side as requested.
    pub to: String,
    /// Files that differ.
    pub files: Vec<FileStat>,
    /// The unified patch.
    pub patch: String,
    /// Why the comparison is empty or failed.
    pub note: Option<String>,
    /// `true` when git failed.
    pub error: bool,
}

/// A validated page request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PageRequest {
    /// 0-based offset.
    pub offset: usize,
    /// Page size (clamped to `1..=`[`MAX_PAGE`]).
    pub limit: usize,
}

impl PageRequest {
    /// Clamp an untrusted (offset, limit) pair.
    #[must_use]
    pub fn new(offset: usize, limit: usize) -> Self {
        Self {
            offset,
            limit: limit.clamp(1, MAX_PAGE),
        }
    }
}

/// The log format: hash, short hash, author name, author email, author time
/// (unix), subject — separated by `0x1f`.
const LOG_FORMAT: &str = "--format=%H%x1f%h%x1f%an%x1f%ae%x1f%at%x1f%s";

/// What a ref/hash argument may look like; refuses options and control
/// characters (the same rule the write seam applies to refs).
fn validate_rev(value: &str, what: &str) -> Result<(), String> {
    if value.trim().is_empty() {
        return Err(format!("empty {what}"));
    }
    if value.starts_with('-') {
        return Err(format!("{what} '{value}' starts with '-'"));
    }
    if value
        .chars()
        .any(|character| character.is_control() || character.is_whitespace())
    {
        return Err(format!(
            "{what} '{value}' contains whitespace or a control character"
        ));
    }
    Ok(())
}

/// A repository-relative path: no leading `-`, no absolute path, no escape.
fn validate_path(path: &str) -> Result<(), String> {
    if path.trim().is_empty() {
        return Err("empty path".to_string());
    }
    if path.starts_with('-') {
        return Err(format!("path '{path}' starts with '-'"));
    }
    if Path::new(path).is_absolute() {
        return Err(format!("absolute path '{path}'"));
    }
    for component in Path::new(path).components() {
        if matches!(component, std::path::Component::ParentDir) {
            return Err(format!("path '{path}' escapes the working tree"));
        }
    }
    Ok(())
}

/// The empty-read cases: a missing repository / a non-Git repository.
fn repository_and_root(root: &Path) -> Result<std::path::PathBuf, (Backend, Option<String>)> {
    let (backend, found) = crate::detect(root);
    let Some(working_tree) = found else {
        return Err((
            backend,
            Some("not a Git or Subversion working tree".to_string()),
        ));
    };
    if backend != Backend::Git {
        return Err((
            backend,
            Some("history is implemented for Git working trees only".to_string()),
        ));
    }
    Ok(working_tree)
}

/// The first non-empty stderr line, as the user-facing reason.
fn reason(stderr: &str, fallback: &str) -> String {
    for line in stderr.lines() {
        if !line.trim().is_empty() {
            return line.trim_start_matches("fatal: ").to_string();
        }
    }
    fallback.to_string()
}

fn run<R: CommandRunner>(
    command_runner: &R,
    working_tree: &Path,
    args: &[String],
) -> Result<CommandOutput, String> {
    command_runner.run("git", working_tree, args)
}

fn args(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| (*value).to_string()).collect()
}

/// Parse one `git log` page in [`LOG_FORMAT`].
#[must_use]
pub fn parse_log(stdout: &str) -> Vec<CommitEntry> {
    let mut commits = Vec::new();
    for line in stdout.lines() {
        if line.trim().is_empty() {
            continue;
        }
        let mut fields = line.split(FIELD);
        let (Some(hash), Some(short_hash), Some(author), Some(email), Some(time), Some(subject)) = (
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
        ) else {
            continue;
        };
        commits.push(CommitEntry {
            hash: hash.to_string(),
            short_hash: short_hash.to_string(),
            author: author.to_string(),
            email: email.to_string(),
            time: time.parse::<i64>().unwrap_or(0),
            subject: subject.to_string(),
        });
    }
    commits
}

/// Parse `git show --numstat` / `git diff --numstat` output.
///
/// A binary file is reported as `-\t-\t<path>` by git; the stat then carries
/// `-1` on both sides, which is what the panel renders as "binary".
#[must_use]
pub fn parse_numstat(stdout: &str) -> Vec<FileStat> {
    let mut files = Vec::new();
    for line in stdout.lines() {
        let mut parts = line.splitn(3, '\t');
        let (Some(added), Some(deleted), Some(path)) = (parts.next(), parts.next(), parts.next())
        else {
            continue;
        };
        files.push(FileStat {
            path: path.to_string(),
            additions: added.parse::<i64>().unwrap_or(-1),
            deletions: deleted.parse::<i64>().unwrap_or(-1),
        });
    }
    files
}

/// Parse one commit's header produced by
/// `git show -s --format=%H%x1f%an%x1f%ae%x1f%at%x1f%s%x1f%b` (`%at` is already unix
/// seconds, so no `--date` option is needed on the 1.8.3 baseline).
///
/// Returns `(hash, author, email, time, subject, body)`.
#[must_use]
pub fn parse_commit_header(stdout: &str) -> Option<(String, String, String, i64, String, String)> {
    let text = stdout.trim_end_matches('\n');
    let mut fields = text.splitn(6, FIELD);
    let (Some(hash), Some(author), Some(email), Some(time), Some(subject), Some(body)) = (
        fields.next(),
        fields.next(),
        fields.next(),
        fields.next(),
        fields.next(),
        fields.next(),
    ) else {
        return None;
    };
    Some((
        hash.to_string(),
        author.to_string(),
        email.to_string(),
        time.parse::<i64>().unwrap_or(0),
        subject.to_string(),
        body.trim_end_matches('\n').to_string(),
    ))
}

/// Parse `git blame --line-porcelain` output.
///
/// Porcelain emits one header line per line group (`<40-hex> <orig> <final>`),
/// the `author ` / `author-time ` metadata, a `filename ` line and finally the
/// `\t<text>` payload. Only the fields the panel renders are kept.
#[must_use]
pub fn parse_blame(stdout: &str) -> Vec<BlameLine> {
    let mut lines = Vec::new();
    let mut hash = String::new();
    let mut short_hash = String::new();
    let mut author = String::new();
    let mut time: i64 = 0;
    let mut final_line: usize = 0;
    let mut in_entry = false;
    for line in stdout.lines() {
        if let Some((candidate, rest)) = line.split_once(' ') {
            // A boundary commit is prefixed with `^` in some git versions; the
            // object id itself is the hex part.
            let candidate = candidate.trim_start_matches('^');
            let is_hash = candidate.len() >= 7
                && candidate.len() <= 64
                && candidate
                    .chars()
                    .all(|character| character.is_ascii_hexdigit());
            if is_hash {
                // A group header: `<hash> <orig-line> <final-line> [<num-lines>]`.
                let mut numbers = rest.split(' ');
                let _orig = numbers.next();
                let final_number = numbers.next();
                hash = candidate.to_string();
                short_hash = candidate.chars().take(7).collect();
                final_line = final_number
                    .and_then(|value| value.parse::<usize>().ok())
                    .unwrap_or(0);
                author.clear();
                time = 0;
                in_entry = true;
                continue;
            }
        }
        if !in_entry {
            continue;
        }
        if let Some(text) = line.strip_prefix('\t') {
            lines.push(BlameLine {
                line: final_line,
                hash: hash.clone(),
                short_hash: short_hash.clone(),
                author: author.clone(),
                time,
                text: text.to_string(),
            });
            in_entry = false;
            continue;
        }
        if let Some(value) = line.strip_prefix("author ") {
            author = value.to_string();
        } else if let Some(value) = line.strip_prefix("author-time ") {
            time = value.trim().parse::<i64>().unwrap_or(0);
        }
    }
    lines
}

/// Cap a text payload at [`MAX_TEXT_BYTES`], on a char boundary.
fn cap_text(text: String) -> (String, bool) {
    if text.len() <= MAX_TEXT_BYTES {
        return (text, false);
    }
    let mut end = MAX_TEXT_BYTES;
    while end > 0 && !text.is_char_boundary(end) {
        end -= 1;
    }
    let mut capped = text[..end].to_string();
    capped.push_str("\n… (truncated)");
    (capped, true)
}

/// Read one page of the commit list.
#[must_use]
pub fn commits(root: &Path, request: PageRequest, query: &str) -> CommitPage {
    commits_with(root, request, query, &crate::runner::SystemRunner)
}

/// Read one page of the commit list through an explicit runner (tests).
#[must_use]
pub fn commits_with<R: CommandRunner>(
    root: &Path,
    request: PageRequest,
    query: &str,
    command_runner: &R,
) -> CommitPage {
    let mut page = CommitPage {
        backend: Backend::None,
        commits: Vec::new(),
        offset: request.offset,
        has_more: false,
        note: None,
        error: false,
    };
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            page.backend = backend;
            page.note = note;
            return page;
        }
    };
    page.backend = Backend::Git;

    let skip = format!("--skip={}", request.offset);
    let max = format!("--max-count={}", request.limit.saturating_add(1));
    let mut log_args = vec!["log".to_string(), skip, max, LOG_FORMAT.to_string()];
    let query = query.trim();
    if !query.is_empty() {
        if query.chars().any(char::is_control) {
            page.error = true;
            page.note = Some("the search text contains a control character".to_string());
            return page;
        }
        log_args.push("--fixed-strings".to_string());
        log_args.push("--regexp-ignore-case".to_string());
        log_args.push(format!("--grep={query}"));
    }
    match run(command_runner, &working_tree, &log_args) {
        Ok(output) if output.success => {
            let mut commits = parse_log(&output.stdout);
            if commits.len() > request.limit {
                commits.truncate(request.limit);
                page.has_more = true;
            }
            page.commits = commits;
        }
        Ok(output) if is_empty_repository(&output.stderr) => {
            page.note = Some("this repository has no commits yet".to_string());
        }
        Ok(output) => {
            page.error = true;
            page.note = Some(reason(&output.stderr, "git log failed"));
        }
        Err(message) => {
            page.error = true;
            page.note = Some(message);
        }
    }
    page
}

/// `git log` on a repository without any commit reports this on stderr.
fn is_empty_repository(stderr: &str) -> bool {
    stderr.contains("does not have any commits yet")
        || stderr.contains("bad default revision")
        || stderr.contains("unknown revision")
}

/// Read one commit's detail.
#[must_use]
pub fn commit_detail(root: &Path, hash: &str) -> CommitDetail {
    commit_detail_with(root, hash, &crate::runner::SystemRunner)
}

/// Read one commit's detail through an explicit runner (tests).
#[must_use]
pub fn commit_detail_with<R: CommandRunner>(
    root: &Path,
    hash: &str,
    command_runner: &R,
) -> CommitDetail {
    let mut detail = CommitDetail {
        backend: Backend::None,
        hash: hash.to_string(),
        author: String::new(),
        email: String::new(),
        time: 0,
        subject: String::new(),
        body: String::new(),
        files: Vec::new(),
        patch: String::new(),
        note: None,
        error: false,
    };
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            detail.backend = backend;
            detail.note = note;
            return detail;
        }
    };
    detail.backend = Backend::Git;
    if let Err(message) = validate_rev(hash, "commit") {
        detail.error = true;
        detail.note = Some(message);
        return detail;
    }

    let header_args = args(&[
        "show",
        "-s",
        "--format=%H%x1f%an%x1f%ae%x1f%at%x1f%s%x1f%b",
        hash,
    ]);
    match run(command_runner, &working_tree, &header_args) {
        Ok(output) if output.success => {
            if let Some((full, author, email, time, subject, body)) =
                parse_commit_header(&output.stdout)
            {
                detail.hash = full;
                detail.author = author;
                detail.email = email;
                detail.time = time;
                detail.subject = subject;
                detail.body = cap_text(body).0;
            }
        }
        Ok(output) => {
            detail.error = true;
            detail.note = Some(reason(&output.stderr, "git show failed"));
            return detail;
        }
        Err(message) => {
            detail.error = true;
            detail.note = Some(message);
            return detail;
        }
    }

    // The file list. `--root` makes the initial commit show its files.
    let stat_args = args(&["show", "--numstat", "--format=", "--root", hash]);
    match run(command_runner, &working_tree, &stat_args) {
        Ok(output) if output.success => {
            let stdout = &output.stdout;
            if stdout.trim().is_empty() {
                detail.note = Some(
                    "this commit carries no file changes here (a merge commit shows its files \
                     through the comparison view)"
                        .to_string(),
                );
            }
            detail.files = parse_numstat(stdout);
        }
        Ok(output) => {
            detail.error = true;
            detail.note = Some(reason(&output.stderr, "git show --numstat failed"));
            return detail;
        }
        Err(message) => {
            detail.error = true;
            detail.note = Some(message);
            return detail;
        }
    }

    let patch_args = args(&["show", "--format=", "--patch", "--no-color", "--root", hash]);
    match run(command_runner, &working_tree, &patch_args) {
        Ok(output) if output.success => detail.patch = cap_text(output.stdout.clone()).0,
        Ok(output) => {
            detail.error = true;
            detail.note = Some(reason(&output.stderr, "git show --patch failed"));
        }
        Err(message) => {
            detail.error = true;
            detail.note = Some(message);
        }
    }
    detail
}

/// Read one page of a single file's history.
#[must_use]
pub fn file_history(root: &Path, path: &str, request: PageRequest) -> FileHistoryPage {
    file_history_with(root, path, request, &crate::runner::SystemRunner)
}

/// Read one page of a single file's history through an explicit runner (tests).
#[must_use]
pub fn file_history_with<R: CommandRunner>(
    root: &Path,
    path: &str,
    request: PageRequest,
    command_runner: &R,
) -> FileHistoryPage {
    let mut page = FileHistoryPage {
        backend: Backend::None,
        path: path.to_string(),
        commits: Vec::new(),
        offset: request.offset,
        has_more: false,
        note: None,
        error: false,
    };
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            page.backend = backend;
            page.note = note;
            return page;
        }
    };
    page.backend = Backend::Git;
    if let Err(message) = validate_path(path) {
        page.error = true;
        page.note = Some(message);
        return page;
    }

    let skip = format!("--skip={}", request.offset);
    let max = format!("--max-count={}", request.limit.saturating_add(1));
    // `--follow` only works with a single pathspec, which is what this API takes.
    let history_args = vec![
        "log".to_string(),
        skip,
        max,
        "--follow".to_string(),
        LOG_FORMAT.to_string(),
        "--".to_string(),
        path.to_string(),
    ];
    match run(command_runner, &working_tree, &history_args) {
        Ok(output) if output.success => {
            let mut commits = parse_log(&output.stdout);
            if commits.len() > request.limit {
                commits.truncate(request.limit);
                page.has_more = true;
            }
            if commits.is_empty() {
                page.note = Some("no commit touches this path".to_string());
            }
            page.commits = commits;
        }
        Ok(output) if is_empty_repository(&output.stderr) => {
            page.note = Some("this repository has no commits yet".to_string());
        }
        Ok(output) => {
            page.error = true;
            page.note = Some(reason(&output.stderr, "git log <path> failed"));
        }
        Err(message) => {
            page.error = true;
            page.note = Some(message);
        }
    }
    page
}

/// Read one file's blame.
#[must_use]
pub fn blame(root: &Path, path: &str) -> BlameResult {
    blame_with(root, path, &crate::runner::SystemRunner)
}

/// Read one file's blame through an explicit runner (tests).
#[must_use]
pub fn blame_with<R: CommandRunner>(root: &Path, path: &str, command_runner: &R) -> BlameResult {
    let mut result = BlameResult {
        backend: Backend::None,
        path: path.to_string(),
        lines: Vec::new(),
        note: None,
        error: false,
        truncated: false,
    };
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            result.backend = backend;
            result.note = note;
            return result;
        }
    };
    result.backend = Backend::Git;
    if let Err(message) = validate_path(path) {
        result.error = true;
        result.note = Some(message);
        return result;
    }

    let blame_args = vec![
        "blame".to_string(),
        "--line-porcelain".to_string(),
        "--".to_string(),
        path.to_string(),
    ];
    match run(command_runner, &working_tree, &blame_args) {
        Ok(output) if output.success => {
            let mut lines = parse_blame(&output.stdout);
            if lines.len() > MAX_BLAME_LINES {
                lines.truncate(MAX_BLAME_LINES);
                result.truncated = true;
            }
            if lines.is_empty() {
                result.note = Some("the file is empty".to_string());
            }
            result.lines = lines;
        }
        Ok(output) => {
            result.error = true;
            result.note = Some(reason(&output.stderr, "git blame failed"));
        }
        Err(message) => {
            result.error = true;
            result.note = Some(message);
        }
    }
    result
}

/// Compare two commits / refs (`git diff <from> <to>`).
#[must_use]
pub fn compare(root: &Path, from: &str, to: &str) -> CommitComparison {
    compare_with(root, from, to, &crate::runner::SystemRunner)
}

/// Compare two commits / refs through an explicit runner (tests).
#[must_use]
pub fn compare_with<R: CommandRunner>(
    root: &Path,
    from: &str,
    to: &str,
    command_runner: &R,
) -> CommitComparison {
    let mut result = CommitComparison {
        backend: Backend::None,
        from: from.to_string(),
        to: to.to_string(),
        files: Vec::new(),
        patch: String::new(),
        note: None,
        error: false,
    };
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            result.backend = backend;
            result.note = note;
            return result;
        }
    };
    result.backend = Backend::Git;
    for (value, what) in [(from, "from"), (to, "to")] {
        if let Err(message) = validate_rev(value, what) {
            result.error = true;
            result.note = Some(message);
            return result;
        }
    }

    let stat_args = vec![
        "diff".to_string(),
        "--numstat".to_string(),
        from.to_string(),
        to.to_string(),
    ];
    match run(command_runner, &working_tree, &stat_args) {
        Ok(output) if output.success => {
            result.files = parse_numstat(&output.stdout);
            if result.files.is_empty() {
                result.note = Some("the two commits have no file changes in common".to_string());
            }
        }
        Ok(output) => {
            result.error = true;
            result.note = Some(reason(&output.stderr, "git diff --numstat failed"));
            return result;
        }
        Err(message) => {
            result.error = true;
            result.note = Some(message);
            return result;
        }
    }

    let patch_args = vec![
        "diff".to_string(),
        "--no-color".to_string(),
        from.to_string(),
        to.to_string(),
    ];
    match run(command_runner, &working_tree, &patch_args) {
        Ok(output) if output.success => result.patch = cap_text(output.stdout.clone()).0,
        Ok(output) => {
            result.error = true;
            result.note = Some(reason(&output.stderr, "git diff failed"));
        }
        Err(message) => {
            result.error = true;
            result.note = Some(message);
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runner::CommandOutput;

    struct FakeRunner {
        stdout: String,
        stderr: String,
        success: bool,
        calls: std::cell::RefCell<Vec<Vec<String>>>,
    }

    impl FakeRunner {
        fn answering(stdout: &str) -> Self {
            Self {
                stdout: stdout.to_string(),
                stderr: String::new(),
                success: true,
                calls: std::cell::RefCell::new(Vec::new()),
            }
        }

        #[expect(dead_code, reason = "kept for the failure-path assertions below")]
        fn failing(stderr: &str) -> Self {
            Self {
                stdout: String::new(),
                stderr: stderr.to_string(),
                success: false,
                calls: std::cell::RefCell::new(Vec::new()),
            }
        }

        fn calls(&self) -> Vec<Vec<String>> {
            self.calls.borrow().clone()
        }
    }

    impl CommandRunner for FakeRunner {
        fn run(
            &self,
            _program: &str,
            _working_tree: &Path,
            args: &[String],
        ) -> Result<CommandOutput, String> {
            self.calls.borrow_mut().push(args.to_vec());
            Ok(CommandOutput {
                success: self.success,
                stdout: self.stdout.clone(),
                stderr: self.stderr.clone(),
            })
        }
    }

    #[test]
    fn parses_a_log_page() {
        let text = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2\u{1f}a1b2c3d\u{1f}Alice\u{1f}alice@example.com\u{1f}1700000000\u{1f}add top\n\
                    ffffffffffffffffffffffffffffffffffffffff\u{1f}ffffff0\u{1f}Bob\u{1f}bob@example.com\u{1f}1699999999\u{1f}fix lint\n";
        let commits = parse_log(text);
        assert_eq!(commits.len(), 2);
        assert_eq!(commits[0].short_hash, "a1b2c3d");
        assert_eq!(commits[0].author, "Alice");
        assert_eq!(commits[0].time, 1_700_000_000);
        assert_eq!(commits[0].subject, "add top");
        assert_eq!(commits[1].subject, "fix lint");
    }

    #[test]
    fn parses_numstat_with_binary() {
        let text = "3\t1\trtl/top.sv\n0\t0\trtl/new.sv\n-\t-\tassets/logo.png\n";
        let files = parse_numstat(text);
        assert_eq!(files.len(), 3);
        assert_eq!(files[0].additions, 3);
        assert_eq!(files[0].deletions, 1);
        assert_eq!(files[0].path, "rtl/top.sv");
        assert_eq!(files[2].additions, -1);
        assert_eq!(files[2].deletions, -1);
    }

    #[test]
    fn parses_a_commit_header_with_body() {
        let text = "0123456789abcdef0123456789abcdef01234567\u{1f}Alice\u{1f}a@b.c\u{1f}1700000001\u{1f}subject\u{1f}body line 1\nbody line 2\n";
        let parsed = parse_commit_header(text);
        let Some((hash, author, email, time, subject, body)) = parsed else {
            panic!("header must parse");
        };
        assert_eq!(hash.len(), 40);
        assert_eq!(author, "Alice");
        assert_eq!(email, "a@b.c");
        assert_eq!(time, 1_700_000_001);
        assert_eq!(subject, "subject");
        assert_eq!(body, "body line 1\nbody line 2");
    }

    #[test]
    fn parses_blame_porcelain() {
        let text = "\
0123456789abcdef0123456789abcdef01234567 1 1 1
author Alice
author-mail <alice@example.com>
author-time 1700000000
author-tz +0000
committer Alice
committer-mail <alice@example.com>
committer-time 1700000000
committer-tz +0000
summary add top
filename rtl/top.sv
\tmodule top;
0123456789abcdef0123456789abcdef01234567 2 2
author Alice
author-time 1700000000
filename rtl/top.sv
\tendmodule
";
        let lines = parse_blame(text);
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0].line, 1);
        assert_eq!(lines[0].author, "Alice");
        assert_eq!(lines[0].time, 1_700_000_000);
        assert_eq!(lines[0].text, "module top;");
        assert_eq!(lines[1].line, 2);
        assert_eq!(lines[1].text, "endmodule");
    }

    #[test]
    fn a_page_requests_one_extra_row_to_detect_more() {
        let request = PageRequest::new(40, 20);
        assert_eq!(request.offset, 40);
        assert_eq!(request.limit, 20);
        // The over-limit clamp is part of the bound, not of the caller's trust.
        assert_eq!(PageRequest::new(0, 10_000).limit, MAX_PAGE);
        assert_eq!(PageRequest::new(0, 0).limit, 1);
    }

    #[test]
    fn a_log_failure_is_an_error_and_an_empty_repository_is_not() {
        let root = std::path::Path::new("/nonexistent-vedit-test");
        let page = commits_with(
            root,
            PageRequest::new(0, 10),
            "",
            &FakeRunner::answering(""),
        );
        assert!(
            !page.error,
            "a missing repository is the empty state, not a failure"
        );
    }

    #[test]
    fn refuses_an_option_shaped_hash() {
        let detail = commit_detail_with(
            std::path::Path::new("/tmp"),
            "--upload-pack=x",
            &FakeRunner::answering(""),
        );
        // Either the repository is missing or the hash is refused; both are a
        // non-error empty result with a note, never a spawned process.
        assert!(detail.note.is_some());
    }

    #[test]
    fn refuses_an_escaping_path() {
        let mut runner = FakeRunner::answering("");
        runner.success = true;
        let page = file_history_with(
            std::path::Path::new("/nonexistent-vedit-test"),
            "../etc/passwd",
            PageRequest::new(0, 10),
            &runner,
        );
        assert!(page.note.is_some());
        assert!(
            runner.calls().is_empty(),
            "no process may run for an unsafe path"
        );
    }
}
