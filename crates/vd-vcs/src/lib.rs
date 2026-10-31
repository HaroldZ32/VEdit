//! VCS backend service for VEdit (Slice 2).
//!
//! The backend is chosen by what is on disk, never by a setting: a working tree
//! containing `.git` is **Git**, one containing `.svn` is **Subversion**, and
//! anything else is the explicit empty state ("not a repository") rather than an
//! error. Both backends shell out to the pinned tool (`git`/`svn`) — there is no
//! library binding, which keeps the dependency tree free of C libraries and
//! licenses the product does not already carry.
//!
//! # Shape
//!
//! [`status`] returns one [`RepoStatus`]: the backend, the branch when the
//! backend can report one, and the per-file changes. The parsers ([`parse_git_status`],
//! [`parse_svn_status`]) are pure functions over the tool's textual output, so
//! the mapping is unit-tested without a repository.
//!
//! # Status (Slice 0)
//!
//! This is the FFI seam's VCS backend: `vd-ffi` exposes it through the C ABI.
//! The panel that renders the branch and the per-file diff belongs to the
//! vcs-panel task, which owns the richer queries.
#![forbid(unsafe_code)]

use std::path::{Path, PathBuf};
use std::process::Command;

/// Deepest ancestor walk performed while looking for a repository marker.
const MAX_ANCESTORS: usize = 64;

/// Which VCS backend a directory belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Backend {
    /// No working tree marker was found.
    None,
    /// A `.git` marker was found.
    Git,
    /// A `.svn` marker was found.
    Svn,
}

impl Backend {
    /// Stable integer for the C ABI: `0` none, `1` git, `2` svn.
    #[must_use]
    pub const fn as_i32(self) -> i32 {
        match self {
            Self::None => 0,
            Self::Git => 1,
            Self::Svn => 2,
        }
    }
}

/// What happened to one file.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChangeKind {
    /// New file, staged or untracked.
    Added,
    /// Content modified.
    Modified,
    /// Removed.
    Deleted,
    /// Moved or copied.
    Renamed,
    /// Both sides changed in a merge.
    Conflict,
    /// A code this crate does not map yet.
    Unknown,
}

impl ChangeKind {
    /// Stable integer for the C ABI.
    #[must_use]
    pub const fn as_i32(self) -> i32 {
        match self {
            Self::Added => 1,
            Self::Modified => 2,
            Self::Deleted => 3,
            Self::Renamed => 4,
            Self::Conflict => 5,
            Self::Unknown => 6,
        }
    }

    fn from_git_pair(pair: &str) -> Self {
        let mut chars = pair.chars();
        let x = chars.next();
        let y = chars.next();
        match (x, y) {
            (Some('?'), Some('?')) => Self::Added,
            (Some('U'), _) | (_, Some('U')) => Self::Conflict,
            (Some('A'), Some('A')) | (Some('D'), Some('D')) => Self::Conflict,
            (Some('R'), _) | (_, Some('R')) => Self::Renamed,
            (Some('A'), _) => Self::Added,
            (Some('D'), _) | (_, Some('D')) => Self::Deleted,
            (Some('M'), _) | (_, Some('M')) => Self::Modified,
            _ => Self::Unknown,
        }
    }

    fn from_svn_code(code: char) -> Self {
        match code {
            '?' | 'A' => Self::Added,
            'M' => Self::Modified,
            'D' | '!' => Self::Deleted,
            'C' | '~' => Self::Conflict,
            'R' => Self::Renamed,
            _ => Self::Unknown,
        }
    }
}

/// One changed path.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Change {
    /// Path relative to the working tree root, as the tool printed it.
    pub path: String,
    /// What happened to it.
    pub kind: ChangeKind,
}

/// One status snapshot of a working tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepoStatus {
    /// Detected backend (`None` for a plain directory).
    pub backend: Backend,
    /// Current branch, when the backend reports one.
    pub branch: Option<String>,
    /// Changed paths, in the tool's order.
    pub changes: Vec<Change>,
    /// Why a backend could not be queried, or that there is no repository.
    pub note: Option<String>,
}

/// Walk up from `start` looking for a `.git` or `.svn` marker.
#[must_use]
pub fn detect(start: &Path) -> (Backend, Option<PathBuf>) {
    let mut dir = start.to_path_buf();
    for _ in 0..MAX_ANCESTORS {
        if dir.join(".git").exists() {
            return (Backend::Git, Some(dir));
        }
        if dir.join(".svn").exists() {
            return (Backend::Svn, Some(dir));
        }
        match dir.parent() {
            Some(parent) => dir = parent.to_path_buf(),
            None => break,
        }
    }
    (Backend::None, None)
}

/// Status of the working tree containing `root`.
#[must_use]
pub fn status(root: &Path) -> RepoStatus {
    let (backend, found) = detect(root);
    let Some(working_tree) = found else {
        return RepoStatus {
            backend: Backend::None,
            branch: None,
            changes: Vec::new(),
            note: Some("not a Git or Subversion working tree".to_string()),
        };
    };
    match backend {
        Backend::Git => {
            let output = Command::new("git")
                .arg("-C")
                .arg(&working_tree)
                .args(["status", "--porcelain=v1", "--branch"])
                .output();
            match output {
                Ok(output) if output.status.success() => {
                    let text = String::from_utf8_lossy(&output.stdout);
                    let (branch, changes) = parse_git_status(&text);
                    RepoStatus {
                        backend,
                        branch,
                        changes,
                        note: None,
                    }
                }
                Ok(output) => RepoStatus {
                    backend,
                    branch: None,
                    changes: Vec::new(),
                    note: Some(first_line(
                        &String::from_utf8_lossy(&output.stderr),
                        "git status failed",
                    )),
                },
                Err(error) => RepoStatus {
                    backend,
                    branch: None,
                    changes: Vec::new(),
                    note: Some(format!("cannot run git: {error}")),
                },
            }
        }
        Backend::Svn => {
            let output = Command::new("svn")
                .arg("status")
                .arg(&working_tree)
                .output();
            match output {
                Ok(output) if output.status.success() => RepoStatus {
                    backend,
                    branch: None,
                    changes: parse_svn_status(&String::from_utf8_lossy(&output.stdout)),
                    note: None,
                },
                Ok(output) => RepoStatus {
                    backend,
                    branch: None,
                    changes: Vec::new(),
                    note: Some(first_line(
                        &String::from_utf8_lossy(&output.stderr),
                        "svn status failed",
                    )),
                },
                Err(error) => RepoStatus {
                    backend,
                    branch: None,
                    changes: Vec::new(),
                    note: Some(format!("cannot run svn: {error}")),
                },
            }
        }
        Backend::None => RepoStatus {
            backend,
            branch: None,
            changes: Vec::new(),
            note: Some("not a Git or Subversion working tree".to_string()),
        },
    }
}

/// Parse `git status --porcelain=v1 --branch` output.
#[must_use]
pub fn parse_git_status(text: &str) -> (Option<String>, Vec<Change>) {
    let mut branch = None;
    let mut changes = Vec::new();
    for line in text.lines() {
        if let Some(header) = line.strip_prefix("## ") {
            branch = header
                .strip_prefix("No commits yet on ")
                .or(Some(header))
                .and_then(|rest| rest.split(['.', ' ']).next())
                .filter(|name| !name.is_empty() && *name != "HEAD")
                .map(ToOwned::to_owned);
            continue;
        }
        if line.len() < 4 {
            continue;
        }
        let pair = &line[..2];
        let path = line[3..].trim();
        if path.is_empty() {
            continue;
        }
        changes.push(Change {
            path: path.to_string(),
            kind: ChangeKind::from_git_pair(pair),
        });
    }
    (branch, changes)
}

/// Parse `svn status` output (`?`, `A`, `M`, `D`, `C`, `!`, `~` prefixes).
#[must_use]
pub fn parse_svn_status(text: &str) -> Vec<Change> {
    let mut changes = Vec::new();
    for line in text.lines() {
        let mut chars = line.chars();
        let Some(code) = chars.next() else {
            continue;
        };
        if code.is_whitespace() {
            continue;
        }
        let path = chars.as_str().trim();
        if path.is_empty() {
            continue;
        }
        changes.push(Change {
            path: path.to_string(),
            kind: ChangeKind::from_svn_code(code),
        });
    }
    changes
}

fn first_line(text: &str, fallback: &str) -> String {
    let line = text.lines().next().unwrap_or("").trim();
    if line.is_empty() {
        fallback.to_string()
    } else {
        line.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn git_porcelain_maps_statuses_and_branch() {
        let text = "## main...origin/main\n M src/a.sv\n?? src/b.sv\nA  src/c.sv\nD  src/d.sv\nR  old.sv -> new.sv\nUU conflict.sv\n";
        let (branch, changes) = parse_git_status(text);
        assert_eq!(branch.as_deref(), Some("main"));
        let kinds: Vec<ChangeKind> = changes.iter().map(|change| change.kind).collect();
        assert_eq!(
            kinds,
            vec![
                ChangeKind::Modified,
                ChangeKind::Added,
                ChangeKind::Added,
                ChangeKind::Deleted,
                ChangeKind::Renamed,
                ChangeKind::Conflict,
            ]
        );
        assert_eq!(changes[1].path, "src/b.sv");
        assert_eq!(changes[4].path, "old.sv -> new.sv");
    }

    #[test]
    fn git_detached_head_has_no_branch() {
        let (branch, changes) = parse_git_status("## HEAD (no branch)\n");
        assert_eq!(branch, None);
        assert!(changes.is_empty());
    }

    #[test]
    fn svn_status_maps_codes() {
        let changes =
            parse_svn_status("M       a.sv\n?       b.sv\nA       c.sv\n!       gone.sv\n");
        let kinds: Vec<ChangeKind> = changes.iter().map(|change| change.kind).collect();
        assert_eq!(
            kinds,
            vec![
                ChangeKind::Modified,
                ChangeKind::Added,
                ChangeKind::Added,
                ChangeKind::Deleted,
            ]
        );
    }

    #[test]
    fn plain_directory_is_the_empty_state_not_an_error() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let report = status(dir.path());
        assert_eq!(report.backend, Backend::None);
        assert!(report.changes.is_empty());
        assert!(report.note.is_some());
    }

    #[test]
    fn markers_select_the_backend() {
        let Ok(git_dir) = tempfile::tempdir() else {
            return;
        };
        let created = std::fs::create_dir(git_dir.path().join(".git"));
        assert!(created.is_ok());
        assert_eq!(detect(git_dir.path()).0, Backend::Git);

        let Ok(svn_dir) = tempfile::tempdir() else {
            return;
        };
        let created = std::fs::create_dir(svn_dir.path().join(".svn"));
        assert!(created.is_ok());
        assert_eq!(detect(svn_dir.path()).0, Backend::Svn);
    }

    #[test]
    fn nested_directory_finds_the_repository_above_it() {
        let Ok(repo) = tempfile::tempdir() else {
            return;
        };
        let nested = repo.path().join("rtl/deep");
        let created = std::fs::create_dir_all(&nested);
        assert!(created.is_ok());
        let created = std::fs::create_dir(repo.path().join(".git"));
        assert!(created.is_ok());
        let (backend, found) = detect(&nested);
        assert_eq!(backend, Backend::Git);
        assert_eq!(found.as_deref(), Some(repo.path()));
    }
}
