//! Backend abstraction: one panel-facing service over Git or Subversion.
//!
//! The panel does not know which tool is behind it ([`VcsService`]); the
//! production implementation ([`ProcessVcsService`]) shells out through a
//! [`CommandRunner`] (read-only gate included), and tests swap in
//! [`StubVcsService`] to drive the panel with canned data and no process at all.
#![forbid(unsafe_code)]

use crate::runner::{CommandRunner, SystemRunner};
use crate::{detect, first_line, Backend, Change, ChangeKind, RepoStatus};
use std::path::Path;

/// Branch + revision identity of the working tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VcsInfo {
    /// Detected backend.
    pub backend: Backend,
    /// Current branch (`None` for a detached HEAD or a plain directory).
    pub branch: Option<String>,
    /// Short revision (`git rev-parse --short HEAD`, `svn info --show-item revision`).
    pub revision: Option<String>,
    /// Why the identity could not be read, when it could not.
    pub note: Option<String>,
}

impl VcsInfo {
    /// The explicit empty state (not a repository), never an error.
    #[must_use]
    pub fn empty() -> Self {
        Self {
            backend: Backend::None,
            branch: None,
            revision: None,
            note: Some("not a Git or Subversion working tree".to_string()),
        }
    }
}

/// The diff of one file, as the panel renders it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FileDiff {
    /// Path relative to the working tree root.
    pub path: String,
    /// Unified diff text (empty with a note when there is nothing to show).
    pub text: String,
    /// The tool reported binary content.
    pub binary: bool,
    /// Why the diff is empty (untracked file, tool error, ...).
    pub note: Option<String>,
}

impl FileDiff {
    /// An empty diff carrying the reason.
    #[must_use]
    pub fn empty(path: &str, note: &str) -> Self {
        Self {
            path: path.to_string(),
            text: String::new(),
            binary: false,
            note: Some(note.to_string()),
        }
    }
}

/// Panel-facing VCS service: one object, two backends, one stub.
pub trait VcsService {
    /// Backend identity.
    fn backend(&self) -> Backend;
    /// Branch + revision.
    fn info(&self, root: &Path) -> VcsInfo;
    /// Working-tree status.
    fn status(&self, root: &Path) -> RepoStatus;
    /// Unified diff of one changed file.
    fn file_diff(&self, root: &Path, path: &str) -> FileDiff;
}

/// Production service: Git or Subversion behind one read-only runner.
pub struct ProcessVcsService<R: CommandRunner> {
    runner: R,
}

impl<R: CommandRunner> ProcessVcsService<R> {
    /// Wraps a runner (production: [`SystemRunner`]; tests: a recording stub).
    pub fn new(runner: R) -> Self {
        Self { runner }
    }

    fn git(&self, root: &Path, args: &[&str]) -> Result<String, String> {
        let mut owned = Vec::with_capacity(args.len() + 2);
        owned.push("-C".to_string());
        owned.push(root.display().to_string());
        owned.extend(args.iter().map(|arg| (*arg).to_string()));
        let output = self.runner.run("git", &owned)?;
        if output.success {
            Ok(output.stdout)
        } else {
            Err(first_line(&output.stderr, "git command failed"))
        }
    }

    fn svn(&self, args: &[&str]) -> Result<String, String> {
        let owned: Vec<String> = args.iter().map(|arg| (*arg).to_string()).collect();
        let output = self.runner.run("svn", &owned)?;
        if output.success {
            Ok(output.stdout)
        } else {
            Err(first_line(&output.stderr, "svn command failed"))
        }
    }
}

fn parse_revision(text: &str) -> Option<String> {
    let line = text.lines().next().map(str::trim).unwrap_or("");
    if line.is_empty() {
        None
    } else {
        Some(line.to_string())
    }
}

impl<R: CommandRunner> VcsService for ProcessVcsService<R> {
    fn backend(&self) -> Backend {
        Backend::None
    }

    fn info(&self, root: &Path) -> VcsInfo {
        let (backend, found) = detect(root);
        let Some(working_tree) = found else {
            return VcsInfo::empty();
        };
        match backend {
            Backend::Git => {
                let branch = self
                    .git(&working_tree, &["rev-parse", "--abbrev-ref", "HEAD"])
                    .ok()
                    .map(|text| text.trim().to_string())
                    .filter(|name| !name.is_empty() && name != "HEAD");
                let revision = self
                    .git(&working_tree, &["rev-parse", "--short", "HEAD"])
                    .ok()
                    .and_then(|text| parse_revision(&text));
                let note = if branch.is_none() && revision.is_none() {
                    Some("git repository has no commit yet".to_string())
                } else {
                    None
                };
                VcsInfo {
                    backend,
                    branch,
                    revision,
                    note,
                }
            }
            Backend::Svn => {
                let revision = self
                    .svn(&[
                        "info",
                        "--show-item",
                        "revision",
                        working_tree.to_str().unwrap_or("."),
                    ])
                    .ok()
                    .and_then(|text| parse_revision(&text));
                let url = self
                    .svn(&[
                        "info",
                        "--show-item",
                        "url",
                        working_tree.to_str().unwrap_or("."),
                    ])
                    .ok()
                    .map(|text| text.trim().to_string())
                    .filter(|value| !value.is_empty());
                let branch = url.map(|value| {
                    value
                        .rsplit('/')
                        .find(|part| !part.is_empty())
                        .unwrap_or("trunk")
                        .to_string()
                });
                let note = if revision.is_none() {
                    Some("svn info did not report a revision".to_string())
                } else {
                    None
                };
                VcsInfo {
                    backend,
                    branch,
                    revision,
                    note,
                }
            }
            Backend::None => VcsInfo::empty(),
        }
    }

    fn status(&self, root: &Path) -> RepoStatus {
        crate::status_with(root, &self.runner)
    }

    fn file_diff(&self, root: &Path, path: &str) -> FileDiff {
        let (backend, found) = detect(root);
        let Some(working_tree) = found else {
            return FileDiff::empty(path, "not a repository");
        };
        match backend {
            Backend::Git => {
                // Staged + unstaged are both read-only queries; each is a
                // separate invocation so the panel can show the whole delta.
                let staged = self
                    .git(
                        &working_tree,
                        &["diff", "--cached", "--no-color", "--", path],
                    )
                    .unwrap_or_default();
                let unstaged = self
                    .git(&working_tree, &["diff", "--no-color", "--", path])
                    .unwrap_or_default();
                let mut text = staged;
                text.push_str(&unstaged);
                if text.trim().is_empty() {
                    // An untracked file has no diff against the index; --no-index
                    // against /dev/null is still a read-only `diff` invocation.
                    if let Ok(untracked) = self.git(
                        &working_tree,
                        &["diff", "--no-index", "--no-color", "--", "/dev/null", path],
                    ) {
                        if !untracked.trim().is_empty() {
                            let binary = untracked.contains("Binary files");
                            return FileDiff {
                                path: path.to_string(),
                                text: untracked,
                                binary,
                                note: None,
                            };
                        }
                    }
                    return FileDiff::empty(path, "no diff to show (unchanged or untracked)");
                }
                let binary = text.contains("Binary files") || text.contains("GIT binary patch");
                FileDiff {
                    path: path.to_string(),
                    text,
                    binary,
                    note: None,
                }
            }
            Backend::Svn => {
                let text = self
                    .svn(&[
                        "diff",
                        "--internal-diff",
                        working_tree.join(path).to_str().unwrap_or("."),
                    ])
                    .unwrap_or_default();
                if text.trim().is_empty() {
                    return FileDiff::empty(path, "no diff to show (unchanged, added or binary)");
                }
                let binary = text.contains("Cannot display: file marked as a binary type");
                FileDiff {
                    path: path.to_string(),
                    text,
                    binary,
                    note: None,
                }
            }
            Backend::None => FileDiff::empty(path, "not a repository"),
        }
    }
}

impl ProcessVcsService<SystemRunner> {
    /// Production service.
    #[must_use]
    pub fn production() -> Self {
        Self::new(SystemRunner)
    }
}

/// Stub service: canned data, zero processes. This is the seam the panel tests
/// and the "no write operation was executed" evidence use.
#[derive(Debug, Clone)]
pub struct StubVcsService {
    info: VcsInfo,
    status: RepoStatus,
    diffs: Vec<FileDiff>,
}

impl StubVcsService {
    /// Builds a stub from canned values.
    #[must_use]
    pub fn new(info: VcsInfo, status: RepoStatus, diffs: Vec<FileDiff>) -> Self {
        Self {
            info,
            status,
            diffs,
        }
    }
}

impl VcsService for StubVcsService {
    fn backend(&self) -> Backend {
        self.info.backend
    }

    fn info(&self, _root: &Path) -> VcsInfo {
        self.info.clone()
    }

    fn status(&self, _root: &Path) -> RepoStatus {
        self.status.clone()
    }

    fn file_diff(&self, _root: &Path, path: &str) -> FileDiff {
        self.diffs
            .iter()
            .find(|diff| diff.path == path)
            .cloned()
            .unwrap_or_else(|| FileDiff::empty(path, "stub has no diff for this path"))
    }
}

/// Convenience: a change with a modified kind (used by the panel tests).
#[must_use]
pub fn modified(path: &str) -> Change {
    Change {
        path: path.to_string(),
        kind: ChangeKind::Modified,
    }
}
