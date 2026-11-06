//! Batch 5b (AC-5.2 / AC-5.4): the two Git WORKSPACE listings.
//!
//! These are READS and they go through the same read-only runner gate as the
//! rest of the backend ([`crate::runner::ensure_read_only`]): `git stash list`
//! and `git worktree list` are the ONLY forms of their shared tokens the gate
//! lets through (see `stash_listing_only` / `worktree_listing_only` in
//! `runner.rs`). Both are LISTINGS: no stash entry is created or dropped and no
//! worktree is added or removed by this module.
//!
//! The delivery baseline (git 1.8.3.1) has no `git worktree` command at all, so
//! the listing surfaces git's REAL refusal (`git: 'worktree' is not a git
//! command`) as `error = true` + `note`, which is what the panel shows. That is
//! the honest answer on that baseline; it is not replaced by an empty list.
//!
//! Parsing is deliberately tolerant of spaces in a worktree path: `git worktree
//! list` prints `<path> <40-hex-hash> [<branch>]` separated by arbitrary
//! whitespace, and `--porcelain` does not exist on the delivery baseline, so the
//! path is found by locating the 40-character hex hash rather than by splitting
//! on the first whitespace run.
#![forbid(unsafe_code)]

use std::path::{Path, PathBuf};

use crate::runner::CommandRunner;
use crate::Backend;

/// Upper bound on the stash rows one read returns (the panel does not page them).
pub const MAX_STASHES: usize = 1000;

/// Upper bound on the worktree rows one read returns.
pub const MAX_WORKTREES: usize = 200;

/// One stash entry or one linked worktree.
///
/// The two listings share one row type (like the C ABI's
/// `VdVcsWorkspaceEntry`): a stash fills `ref_name` / `hash` / `subject`, a
/// worktree fills `path` / `hash` / `branch` and the two flags.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct WorkspaceEntry {
    /// Stash reference (`stash@{0}`), empty for a worktree.
    pub ref_name: String,
    /// Commit id the entry points at.
    pub hash: String,
    /// Stash subject (`On main: ...`), empty for a worktree.
    pub subject: String,
    /// Absolute worktree path, empty for a stash.
    pub path: String,
    /// Checked-out branch of a worktree, empty when detached or bare.
    pub branch: String,
    /// The MAIN worktree (the first line `git worktree list` prints).
    pub main_worktree: bool,
    /// The worktree has a detached HEAD.
    pub detached: bool,
    /// The entry is a bare repository (`git worktree list` marks it).
    pub bare: bool,
}

/// The stashes or the worktrees of one working tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WorkspaceListing {
    /// Backend the listing was read from.
    pub backend: Backend,
    /// The rows, oldest-first for stashes, git's order for worktrees.
    pub entries: Vec<WorkspaceEntry>,
    /// The listing hit [`MAX_STASHES`] / [`MAX_WORKTREES`].
    pub truncated: bool,
    /// The read FAILED (the tool reported an error) as opposed to being empty.
    pub error: bool,
    /// Why the listing is empty or failed; the real tool reason when it failed.
    pub note: Option<String>,
}

impl WorkspaceListing {
    fn empty() -> Self {
        Self {
            backend: Backend::None,
            entries: Vec::new(),
            truncated: false,
            error: false,
            note: None,
        }
    }
}

/// The stash stack of the working tree containing `root`.
#[must_use]
pub fn stashes(root: &Path) -> WorkspaceListing {
    stashes_with(root, &crate::runner::SystemRunner)
}

/// The stash stack through an explicit runner (tests).
#[must_use]
pub fn stashes_with<R: CommandRunner>(root: &Path, command_runner: &R) -> WorkspaceListing {
    listing(
        root,
        command_runner,
        &[
            "stash".to_string(),
            "list".to_string(),
            "--format=%gd%x09%H%x09%s".to_string(),
        ],
        MAX_STASHES,
        parse_stash_list,
        "git stash list failed",
    )
}

/// The linked worktrees of the working tree containing `root`.
#[must_use]
pub fn worktrees(root: &Path) -> WorkspaceListing {
    worktrees_with(root, &crate::runner::SystemRunner)
}

/// The worktree listing through an explicit runner (tests).
#[must_use]
pub fn worktrees_with<R: CommandRunner>(root: &Path, command_runner: &R) -> WorkspaceListing {
    listing(
        root,
        command_runner,
        &["worktree".to_string(), "list".to_string()],
        MAX_WORKTREES,
        parse_worktree_list,
        "git worktree list failed",
    )
}

fn listing<R: CommandRunner>(
    root: &Path,
    command_runner: &R,
    args: &[String],
    cap: usize,
    parse: fn(&str) -> Vec<WorkspaceEntry>,
    fallback: &str,
) -> WorkspaceListing {
    let mut result = WorkspaceListing::empty();
    let working_tree = match repository_and_root(root) {
        Ok(path) => path,
        Err((backend, note)) => {
            result.backend = backend;
            result.note = note;
            return result;
        }
    };
    result.backend = Backend::Git;
    match command_runner.run("git", &working_tree, args) {
        Ok(output) if output.success => {
            let mut entries = parse(&output.stdout);
            if entries.len() > cap {
                entries.truncate(cap);
                result.truncated = true;
            }
            result.entries = entries;
        }
        Ok(output) => {
            result.error = true;
            result.note = Some(reason(&output.stderr, fallback));
        }
        Err(message) => {
            result.error = true;
            result.note = Some(message);
        }
    }
    result
}

fn repository_and_root(root: &Path) -> Result<PathBuf, (Backend, Option<String>)> {
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
            Some(
                "stashes and worktrees are Git concepts; this working tree is Subversion"
                    .to_string(),
            ),
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

/// Parse `git stash list --format=%gd%x09%H%x09%s`.
///
/// One row per line: `stash@{0}\t<40-hex>\t<subject>`. A line that does not have
/// the three tab-separated fields is skipped rather than guessed at.
#[must_use]
pub fn parse_stash_list(stdout: &str) -> Vec<WorkspaceEntry> {
    let mut entries = Vec::new();
    for line in stdout.lines() {
        if line.trim().is_empty() {
            continue;
        }
        let mut fields = line.splitn(3, '\t');
        let (Some(ref_name), Some(hash)) = (fields.next(), fields.next()) else {
            continue;
        };
        let subject = fields.next().unwrap_or_default();
        if ref_name.trim().is_empty() || hash.trim().is_empty() {
            continue;
        }
        entries.push(WorkspaceEntry {
            ref_name: ref_name.trim().to_string(),
            hash: hash.trim().to_string(),
            subject: subject.trim().to_string(),
            ..WorkspaceEntry::default()
        });
    }
    entries
}

/// Parse `git worktree list`.
///
/// One row per line: `<path> <hash> [<branch>]`, `<path> <hash> (detached HEAD)`
/// or `<path> (bare)`. The hash is located as a 40-character hex token so a path
/// containing spaces stays intact; the first row is the MAIN worktree.
#[must_use]
pub fn parse_worktree_list(stdout: &str) -> Vec<WorkspaceEntry> {
    let mut entries = Vec::new();
    for (index, line) in stdout.lines().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        let mut entry = WorkspaceEntry {
            main_worktree: index == 0,
            ..WorkspaceEntry::default()
        };
        match hash_token(line) {
            Some((start, end)) => {
                entry.path = line[..start].trim_end().to_string();
                entry.hash = line[start..end].to_string();
                let tail = line[end..].trim();
                if let Some(branch) = tail
                    .strip_prefix('[')
                    .and_then(|rest| rest.strip_suffix(']'))
                {
                    entry.branch = branch.trim().to_string();
                } else if tail.contains("detached HEAD") {
                    entry.detached = true;
                } else if tail.contains("bare") {
                    entry.bare = true;
                }
            }
            None => {
                // A bare repository is listed without a commit id:
                // `/path/to/repo.git (bare)`.
                entry.path = line.trim_end().trim_end_matches(" (bare)").to_string();
                entry.bare = line.contains("(bare)");
            }
        }
        if entry.path.is_empty() {
            continue;
        }
        entries.push(entry);
    }
    entries
}

/// Byte range of the abbreviated commit id in one `git worktree list` line.
///
/// The hash is ABBREVIATED (git prints the short id, `215cde1`, not 40 hex), and
/// it is the token right before ` [<branch>]`, ` (detached HEAD)` or the end of
/// the line. That anchor — plus the fact that the LAST such token is taken — keeps
/// a path containing spaces (even one containing a hex-looking word) intact.
fn hash_token(line: &str) -> Option<(usize, usize)> {
    let bytes = line.as_bytes();
    let mut found = None;
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] != b' ' {
            index += 1;
            continue;
        }
        let start = index + 1;
        let mut end = start;
        while end < bytes.len() && bytes[end].is_ascii_hexdigit() {
            end += 1;
        }
        let length = end - start;
        if (4..=40).contains(&length) {
            let rest = &line[end..];
            if rest.is_empty() || rest.starts_with(" [") || rest.starts_with(" (") {
                found = Some((start, end));
            }
        }
        index = if end > index { end } else { index + 1 };
    }
    found
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stash_rows_are_split_on_tabs() {
        let rows = parse_stash_list(
            "stash@{0}\t1111111111111111111111111111111111111111\tOn main: wip one\n\
             stash@{1}\t2222222222222222222222222222222222222222\tWIP on main: 1234567 base\n",
        );
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].ref_name, "stash@{0}");
        assert_eq!(rows[0].hash, "1111111111111111111111111111111111111111");
        assert_eq!(rows[0].subject, "On main: wip one");
        assert_eq!(rows[1].ref_name, "stash@{1}");
    }

    #[test]
    fn stash_rows_keep_a_tab_in_the_subject() {
        let rows = parse_stash_list("stash@{0}\t1111111111111111111111111111111111111111\ta\tb");
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].subject, "a\tb");
    }

    #[test]
    fn malformed_stash_rows_are_skipped() {
        assert!(parse_stash_list("").is_empty());
        assert!(parse_stash_list("stash@{0}\n").is_empty());
        assert!(parse_stash_list("\t1111\n").is_empty());
    }

    #[test]
    fn worktree_rows_survive_spaces_in_the_path() {
        // The hashes are ABBREVIATED, exactly as git 2.53 prints them.
        let rows = parse_worktree_list(
            "/repo            215cde1 [main]\n\
             /repo/wt with space 215cde1 [feature/x]\n\
             /repo/wt-detached a1b2c3d (detached HEAD)\n",
        );
        assert_eq!(rows.len(), 3);
        assert_eq!(rows[0].path, "/repo");
        assert!(rows[0].main_worktree);
        assert_eq!(rows[0].branch, "main");
        assert_eq!(rows[1].path, "/repo/wt with space");
        assert!(!rows[1].main_worktree);
        assert_eq!(rows[1].branch, "feature/x");
        assert_eq!(rows[2].path, "/repo/wt-detached");
        assert!(rows[2].detached);
        assert!(rows[2].branch.is_empty());
    }

    #[test]
    fn a_bare_worktree_row_has_no_commit_id() {
        let rows = parse_worktree_list("/srv/repo.git (bare)\n");
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].path, "/srv/repo.git");
        assert!(rows[0].bare);
        assert!(rows[0].hash.is_empty());
    }
}
