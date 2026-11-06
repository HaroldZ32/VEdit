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

pub mod history;
pub mod runner;
pub mod service;
pub mod toolchain;
pub mod workspace;
pub mod write;

use std::path::{Path, PathBuf};

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

/// Status of the working tree containing `root` (production runner).
#[must_use]
pub fn status(root: &Path) -> RepoStatus {
    status_with(root, &runner::SystemRunner)
}

/// Status through an explicit runner.
///
/// Tests inject a stub ([`runner::RecordingRunner`]) to prove a whole session
/// issues only read-only subcommands; production uses [`runner::SystemRunner`].
/// The runner applies the read-only gate, so even a status query cannot execute
/// a write subcommand.
#[must_use]
pub fn status_with<R: runner::CommandRunner>(root: &Path, command_runner: &R) -> RepoStatus {
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
            // `--porcelain` is the git 1.8 spelling and 2.x reports the same v1
            // format; the repository is passed as the process working directory
            // (the delivery baseline has no global path option).
            let args = vec![
                "status".to_string(),
                "--porcelain".to_string(),
                "--branch".to_string(),
            ];
            match command_runner.run("git", &working_tree, &args) {
                Ok(output) if output.success => {
                    let (branch, changes) = parse_git_status(&output.stdout);
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
                    note: Some(first_line(&output.stderr, "git status failed")),
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
            // NO path argument: `svn status` then prints paths RELATIVE to the
            // working copy root (exactly like `git status --porcelain`), which is
            // what the panel renders and what `file_diff` joins. Passing the
            // absolute root made svn echo absolute paths (batch 6b measured it),
            // so the change list stopped matching the Git panel's shape.
            let args = vec!["status".to_string()];
            match command_runner.run("svn", &working_tree, &args) {
                Ok(output) if output.success => RepoStatus {
                    backend,
                    branch: None,
                    changes: parse_svn_status(&output.stdout),
                    note: None,
                },
                Ok(output) => RepoStatus {
                    backend,
                    branch: None,
                    changes: Vec::new(),
                    note: Some(first_line(&output.stderr, "svn status failed")),
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

/// Parse `git status --porcelain --branch` output (the 1.8-compatible spelling;
/// 2.x emits the same v1 format).
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

pub(crate) fn first_line(text: &str, fallback: &str) -> String {
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

/// What one listed ref is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RefKind {
    /// `refs/heads/<name>`.
    Branch,
    /// `refs/tags/<name>`.
    Tag,
    /// `refs/remotes/<remote>/<branch>` (a remote-tracking ref).
    Remote,
    /// Anything else under the queried prefixes.
    Other,
}

impl RefKind {
    /// Stable integer for the C ABI: 1 branch, 2 tag, 3 remote, 4 other.
    #[must_use]
    pub const fn as_i32(self) -> i32 {
        match self {
            Self::Branch => 1,
            Self::Tag => 2,
            Self::Remote => 3,
            Self::Other => 4,
        }
    }
}

/// One branch / tag / remote-tracking ref.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RefEntry {
    /// Short name (the part after `refs/heads/`, `refs/tags/`, `refs/remotes/`).
    pub name: String,
    /// What kind of ref it is.
    pub kind: RefKind,
    /// Abbreviated object id (7 characters, or fewer in a tiny repository).
    pub target: String,
    /// The checked-out branch.
    pub current: bool,
}

/// One configured remote.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RemoteEntry {
    /// Remote name (`origin`).
    pub name: String,
    /// Fetch URL as configured.
    pub url: String,
}

/// The refs / remotes / upstream snapshot of one working tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepoRefs {
    /// Detected backend.
    pub backend: Backend,
    /// Branches, tags and remote-tracking refs (capped at [`MAX_REFS`]).
    pub refs: Vec<RefEntry>,
    /// Configured remotes.
    pub remotes: Vec<RemoteEntry>,
    /// Upstream of the current branch (`origin/main`), when one is set.
    pub upstream: Option<String>,
    /// Why the listing is empty/incomplete (not a repository, tool failure).
    pub note: Option<String>,
    /// `true` when the ref list was cut at [`MAX_REFS`].
    pub truncated: bool,
}

/// Upper bound on one listing, so a pathological repository cannot make the
/// panel (and the C ABI struct) unbounded.
pub const MAX_REFS: usize = 2000;

/// Branches / tags / remotes / upstream of the working tree containing `root`.
#[must_use]
pub fn refs(root: &Path) -> RepoRefs {
    refs_with(root, &runner::SystemRunner)
}

/// [`refs`] through an explicit runner (tests use a recording runner).
#[must_use]
pub fn refs_with<R: runner::CommandRunner>(root: &Path, command_runner: &R) -> RepoRefs {
    let (backend, found) = detect(root);
    let Some(working_tree) = found else {
        return RepoRefs {
            backend,
            refs: Vec::new(),
            remotes: Vec::new(),
            upstream: None,
            note: Some("not a Git or Subversion working tree".to_string()),
            truncated: false,
        };
    };
    if backend != Backend::Git {
        return RepoRefs {
            backend,
            refs: Vec::new(),
            remotes: Vec::new(),
            upstream: None,
            note: Some("ref listing is implemented for Git working trees".to_string()),
            truncated: false,
        };
    }

    let mut refs = Vec::new();
    let mut note = None;
    let mut truncated = false;
    // `%(refname)` / `%(objectname)` are the ancient for-each-ref atoms, so this
    // works on the delivery baseline git 1.8.3.1 as well as current git.
    let args: Vec<String> = [
        "for-each-ref",
        "--format=%(refname)%00%(objectname)",
        "refs/heads",
        "refs/tags",
        "refs/remotes",
    ]
    .iter()
    .map(ToString::to_string)
    .collect();
    match command_runner.run("git", &working_tree, &args) {
        Ok(output) if output.success => {
            for line in output.stdout.lines() {
                let mut parts = line.split('\0');
                let (Some(full), Some(oid)) = (parts.next(), parts.next()) else {
                    continue;
                };
                let Some((kind, name)) = classify_ref(full) else {
                    continue;
                };
                if refs.len() >= MAX_REFS {
                    truncated = true;
                    break;
                }
                refs.push(RefEntry {
                    name: name.to_string(),
                    kind,
                    target: oid.chars().take(7).collect(),
                    current: false,
                });
            }
        }
        Ok(output) => note = Some(first_line(&output.stderr, "git for-each-ref failed")),
        Err(message) => note = Some(message),
    }

    // The current branch is a separate ancient read: `%(HEAD)` is not available
    // on git 1.8.3.1.
    let current_args: Vec<String> = ["rev-parse", "--abbrev-ref", "HEAD"]
        .iter()
        .map(ToString::to_string)
        .collect();
    let current = match command_runner.run("git", &working_tree, &current_args) {
        Ok(output) if output.success => {
            let branch = output.stdout.trim().to_string();
            (!branch.is_empty() && branch != "HEAD").then_some(branch)
        }
        _ => None,
    };
    if let Some(branch) = &current {
        for entry in &mut refs {
            entry.current = entry.kind == RefKind::Branch && entry.name == *branch;
        }
    }

    RepoRefs {
        backend,
        refs,
        remotes: remotes_with(command_runner, &working_tree),
        upstream: upstream_with(command_runner, &working_tree),
        note,
        truncated,
    }
}

fn classify_ref(full: &str) -> Option<(RefKind, &str)> {
    if let Some(name) = full.strip_prefix("refs/heads/") {
        Some((RefKind::Branch, name))
    } else if let Some(name) = full.strip_prefix("refs/tags/") {
        Some((RefKind::Tag, name))
    } else if let Some(name) = full.strip_prefix("refs/remotes/") {
        Some((RefKind::Remote, name))
    } else {
        None
    }
}

/// `git remote -v` (a LISTING form the read gate allows): name + fetch URL.
fn remotes_with<R: runner::CommandRunner>(
    command_runner: &R,
    working_tree: &Path,
) -> Vec<RemoteEntry> {
    let args: Vec<String> = ["remote", "-v"].iter().map(ToString::to_string).collect();
    let Ok(output) = command_runner.run("git", working_tree, &args) else {
        return Vec::new();
    };
    if !output.success {
        return Vec::new();
    }
    let mut remotes: Vec<RemoteEntry> = Vec::new();
    for line in output.stdout.lines() {
        let mut fields = line.split_whitespace();
        let (Some(name), Some(url), Some(kind)) = (fields.next(), fields.next(), fields.next())
        else {
            continue;
        };
        if !kind.contains("fetch") || remotes.iter().any(|entry| entry.name == name) {
            continue;
        }
        remotes.push(RemoteEntry {
            name: name.to_string(),
            url: url.to_string(),
        });
    }
    remotes
}

/// `@{u}` of the current branch, or `None` when it has no upstream.
fn upstream_with<R: runner::CommandRunner>(
    command_runner: &R,
    working_tree: &Path,
) -> Option<String> {
    let args: Vec<String> = ["rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"]
        .iter()
        .map(ToString::to_string)
        .collect();
    match command_runner.run("git", working_tree, &args) {
        Ok(output) if output.success => {
            let upstream = output.stdout.trim().to_string();
            (!upstream.is_empty()).then_some(upstream)
        }
        _ => None,
    }
}

// --- multi-commit operation state (batch 5a, AC-5.1) ----------------------

/// The multi-commit operation currently in progress in a working tree.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OperationKind {
    /// A `git merge` stopped (usually on a conflict).
    Merge,
    /// A `git rebase` stopped.
    Rebase,
    /// A `git cherry-pick` stopped.
    CherryPick,
    /// A `git revert` stopped.
    Revert,
}

impl OperationKind {
    /// Stable integer for the C ABI: 1 merge, 2 rebase, 3 cherry-pick, 4 revert.
    #[must_use]
    pub const fn as_i32(self) -> i32 {
        match self {
            Self::Merge => 1,
            Self::Rebase => 2,
            Self::CherryPick => 3,
            Self::Revert => 4,
        }
    }

    /// The git word for the operation (`merge`, `rebase`, ...), which is also
    /// what `--continue` / `--abort` are run on.
    #[must_use]
    pub const fn token(self) -> &'static str {
        match self {
            Self::Merge => "merge",
            Self::Rebase => "rebase",
            Self::CherryPick => "cherry-pick",
            Self::Revert => "revert",
        }
    }

    /// The same operation for a git word, or `None`.
    #[must_use]
    pub fn from_token(token: &str) -> Option<Self> {
        match token {
            "merge" => Some(Self::Merge),
            "rebase" => Some(Self::Rebase),
            "cherry-pick" => Some(Self::CherryPick),
            "revert" => Some(Self::Revert),
            _ => None,
        }
    }
}

/// The operation in progress plus the unmerged files a resolution starts from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OperationState {
    /// Detected backend.
    pub backend: Backend,
    /// The operation in progress, `None` when the tree is not mid-operation.
    pub operation: Option<OperationKind>,
    /// The paths with an unmerged index entry (status kind 5).
    pub conflicts: Vec<String>,
    /// Why the state is unavailable (not a repository, tool failure).
    pub note: Option<String>,
    /// `true` when the read failed (as opposed to a clean, idle tree).
    pub error: bool,
}

/// Read the operation state of `root` (merge/rebase/cherry-pick/revert).
#[must_use]
pub fn operation_state(root: &Path) -> OperationState {
    operation_state_with(root, &runner::SystemRunner)
}

/// Read the operation state through an explicit runner (tests).
#[must_use]
pub fn operation_state_with<R: runner::CommandRunner>(
    root: &Path,
    command_runner: &R,
) -> OperationState {
    let mut state = OperationState {
        backend: Backend::None,
        operation: None,
        conflicts: Vec::new(),
        note: None,
        error: false,
    };
    let (backend, found) = detect(root);
    state.backend = backend;
    let Some(working_tree) = found else {
        state.note = Some("not a Git or Subversion working tree".to_string());
        return state;
    };
    if backend != Backend::Git {
        state.note = Some("operation state is implemented for Git working trees only".to_string());
        return state;
    }
    // The unmerged files come from the same status read the panel already uses.
    let status = status_with(root, command_runner);
    for change in status.changes {
        if change.kind == ChangeKind::Conflict {
            state.conflicts.push(change.path);
        }
    }
    if let Some(note) = status.note {
        state.note = Some(note);
        state.error = true;
    }

    // The operation itself is a marker file inside the git directory. Asking git
    // for the directory keeps linked worktrees correct (`.git` may be a file).
    let git_dir_args: Vec<String> = ["rev-parse", "--git-dir"]
        .iter()
        .map(ToString::to_string)
        .collect();
    let Ok(output) = command_runner.run("git", &working_tree, &git_dir_args) else {
        state.note = Some("git rev-parse --git-dir failed".to_string());
        state.error = true;
        return state;
    };
    if !output.success {
        state.note = Some(first_line(&output.stderr, "git rev-parse --git-dir failed"));
        state.error = true;
        return state;
    }
    let git_dir = PathBuf::from(output.stdout.trim());
    let git_dir = if git_dir.is_absolute() {
        git_dir
    } else {
        working_tree.join(git_dir)
    };
    if git_dir.join("rebase-merge").is_dir() || git_dir.join("rebase-apply").is_dir() {
        state.operation = Some(OperationKind::Rebase);
    } else if git_dir.join("MERGE_HEAD").exists() {
        state.operation = Some(OperationKind::Merge);
    } else if git_dir.join("CHERRY_PICK_HEAD").exists() {
        state.operation = Some(OperationKind::CherryPick);
    } else if git_dir.join("REVERT_HEAD").exists() {
        state.operation = Some(OperationKind::Revert);
    }
    state
}
