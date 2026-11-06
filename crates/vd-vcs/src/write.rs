//! Write side of the VCS backend: a second, explicit allow-list.
//!
//! The read seam ([`crate::runner::ensure_read_only`]) is deliberately left
//! untouched: every write subcommand is still refused there, before any process
//! exists. This module is the INDEPENDENT write seam. It is not a widened read
//! table — it is a separate table ([`GIT_WRITE_ALLOWED`] / [`SVN_WRITE_ALLOWED`])
//! plus a typed operation list ([`WriteOp`]) and a separate runner entry point
//! ([`WriteRunner::run_write`]).
//!
//! Default deny applies at three levels:
//!
//! 1. an operation code that is not listed is refused ([`WriteOp::from_code`]);
//! 2. an operation whose backend does not match the working tree on disk is
//!    refused (a Git operation never runs in a Subversion working copy);
//! 3. a parameter that does not satisfy the operation's contract (a commit
//!    without a message, an absolute path, `..`, a leading `-`, `--amend` on
//!    anything but a commit) is refused.
//!
//! The caller never supplies the tool argv: [`WriteRequest::invocation`] builds
//! it from the typed fields, so a path cannot turn into an option and a caller
//! cannot smuggle in an unlisted subcommand.
#![forbid(unsafe_code)]

use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, Mutex};

use crate::runner::{subcommand_index, subcommand_of, CommandOutput};
use crate::{detect, Backend};

/// Write operations the seam accepts, one stable code each (the same integers
/// are the C ABI codes in `vd_ffi.h`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WriteOp {
    /// `git add -- <paths>`: stage the given files.
    GitStage = 1,
    /// `git reset HEAD -- <paths>`: unstage the given files.
    GitUnstage = 2,
    /// `git checkout -- <paths>`: discard working-tree changes (irreversible).
    GitDiscard = 3,
    /// `git commit -m <message> [--author=<author>] [--amend] [-- <paths>]`.
    GitCommit = 4,
    /// `git fetch [<remote>]`.
    GitFetch = 5,
    /// `git pull --ff-only [<remote> [<branch>]]`.
    GitPull = 6,
    /// `git push [--set-upstream] [<remote> [<branch>]]`.
    GitPush = 7,
    /// `git branch <name> [<start>]`.
    GitBranchCreate = 8,
    /// `git checkout <name>`: switch to an existing branch.
    GitBranchSwitch = 9,
    /// `git branch -d <name>` (irreversible, confirmed by the UI).
    GitBranchDelete = 10,
    /// `svn commit -m <message> [paths...]`.
    SvnCommit = 11,
    /// `svn update [paths...]`.
    SvnUpdate = 12,
    /// `svn revert <paths...>` (irreversible).
    SvnRevert = 13,
    /// `svn add <paths...>`.
    SvnAdd = 14,
    /// `svn remove <paths...>` (irreversible).
    SvnRemove = 15,
    /// `git branch -m <old> <new>`.
    GitBranchRename = 16,
    /// `git tag <name> [<target>]`.
    GitTagCreate = 17,
    /// `git tag -d <name>` (irreversible, confirmed by the UI).
    GitTagDelete = 18,
    /// `git checkout <ref>`: check out a branch, tag or commit (detached HEAD for
    /// anything but a branch).
    GitCheckout = 19,
    /// `git remote add <name> <url>`.
    GitRemoteAdd = 20,
    /// `git remote rm <name>` (irreversible, confirmed by the UI).
    GitRemoteRemove = 21,
    /// `git remote set-url <name> <url>`.
    GitRemoteSetUrl = 22,
    /// `git branch --set-upstream-to=<upstream> <branch>`.
    GitSetUpstream = 23,
    /// `git merge --no-edit <target>`: merge a branch/commit into HEAD.
    GitMerge = 24,
    /// `git rebase <target> [<name>]`: rebase `name` (default HEAD) onto target.
    GitRebase = 25,
    /// `git cherry-pick <target>`.
    GitCherryPick = 26,
    /// `git revert --no-edit <target>`: the inverse commit, without an editor.
    GitRevert = 27,
    /// `git reset --<mode> <target>`: `mode` is `soft`, `mixed` or `hard`
    /// (hard is irreversible and the UI double-confirms it).
    GitReset = 28,
    /// `git clean -f -- <paths>`: delete the given UNTRACKED files (irreversible).
    /// Directories are not removed (`-d` is never passed).
    GitClean = 29,
    /// `git <target> --continue`, where `target` names the operation in progress
    /// (`merge`, `rebase`, `cherry-pick`, `revert`).
    GitContinue = 30,
    /// `git <target> --abort`, where `target` names the operation in progress.
    GitAbort = 31,
    /// `git stash save [--] [<message>]`: push a stash. (`stash push` does not
    /// exist on the delivery baseline, git 1.8.3.1, and `stash save -m` is not
    /// accepted there either, so the message is positional.)
    GitStashSave = 32,
    /// `git stash pop [<stash>]`: apply AND drop the newest/selected stash.
    GitStashPop = 33,
    /// `git stash apply [<stash>]`: apply the newest/selected stash, keep it.
    GitStashApply = 34,
    /// `git stash drop [<stash>]`: delete the newest/selected stash
    /// (irreversible, double-confirmed by the UI).
    GitStashDrop = 35,
    /// `git worktree add <path> [<branch>]`: create a linked worktree. `target`
    /// is the path, `name` the EXISTING branch to check out (empty = HEAD).
    GitWorktreeAdd = 36,
    /// `git worktree remove <path>`: delete a linked worktree. No `--force` is
    /// ever passed, so a dirty worktree is refused by git itself (irreversible,
    /// double-confirmed by the UI).
    GitWorktreeRemove = 37,
    /// `git submodule init`: register the submodules of `.gitmodules`.
    GitSubmoduleInit = 38,
    /// `git submodule update --init`: check the submodules out at their pinned
    /// commits (`--init` makes it work in a fresh clone too).
    GitSubmoduleUpdate = 39,
}

impl WriteOp {
    /// Every operation the write seam accepts, in code order.
    pub const ALL: [Self; 39] = [
        Self::GitStage,
        Self::GitUnstage,
        Self::GitDiscard,
        Self::GitCommit,
        Self::GitFetch,
        Self::GitPull,
        Self::GitPush,
        Self::GitBranchCreate,
        Self::GitBranchSwitch,
        Self::GitBranchDelete,
        Self::SvnCommit,
        Self::SvnUpdate,
        Self::SvnRevert,
        Self::SvnAdd,
        Self::SvnRemove,
        Self::GitBranchRename,
        Self::GitTagCreate,
        Self::GitTagDelete,
        Self::GitCheckout,
        Self::GitRemoteAdd,
        Self::GitRemoteRemove,
        Self::GitRemoteSetUrl,
        Self::GitSetUpstream,
        Self::GitMerge,
        Self::GitRebase,
        Self::GitCherryPick,
        Self::GitRevert,
        Self::GitReset,
        Self::GitClean,
        Self::GitContinue,
        Self::GitAbort,
        Self::GitStashSave,
        Self::GitStashPop,
        Self::GitStashApply,
        Self::GitStashDrop,
        Self::GitWorktreeAdd,
        Self::GitWorktreeRemove,
        Self::GitSubmoduleInit,
        Self::GitSubmoduleUpdate,
    ];

    /// Stable integer for the C ABI.
    #[must_use]
    pub const fn code(self) -> i32 {
        self as i32
    }

    /// The operation for a stable code, or `None` (default deny).
    #[must_use]
    pub fn from_code(code: i32) -> Option<Self> {
        Self::ALL.iter().copied().find(|op| op.code() == code)
    }

    /// The backend this operation belongs to.
    #[must_use]
    pub const fn backend(self) -> Backend {
        match self {
            Self::GitStage
            | Self::GitUnstage
            | Self::GitDiscard
            | Self::GitCommit
            | Self::GitFetch
            | Self::GitPull
            | Self::GitPush
            | Self::GitBranchCreate
            | Self::GitBranchSwitch
            | Self::GitBranchDelete
            | Self::GitBranchRename
            | Self::GitTagCreate
            | Self::GitTagDelete
            | Self::GitCheckout
            | Self::GitRemoteAdd
            | Self::GitRemoteRemove
            | Self::GitRemoteSetUrl
            | Self::GitSetUpstream
            | Self::GitMerge
            | Self::GitRebase
            | Self::GitCherryPick
            | Self::GitRevert
            | Self::GitReset
            | Self::GitClean
            | Self::GitContinue
            | Self::GitAbort
            | Self::GitStashSave
            | Self::GitStashPop
            | Self::GitStashApply
            | Self::GitStashDrop
            | Self::GitWorktreeAdd
            | Self::GitWorktreeRemove
            | Self::GitSubmoduleInit
            | Self::GitSubmoduleUpdate => Backend::Git,
            Self::SvnCommit
            | Self::SvnUpdate
            | Self::SvnRevert
            | Self::SvnAdd
            | Self::SvnRemove => Backend::Svn,
        }
    }

    /// The exact subcommand this operation maps onto.
    #[must_use]
    pub const fn subcommand(self) -> &'static str {
        match self {
            Self::GitStage => "add",
            Self::GitUnstage => "reset",
            Self::GitDiscard => "checkout",
            Self::GitCommit => "commit",
            Self::GitFetch => "fetch",
            Self::GitPull => "pull",
            Self::GitPush => "push",
            Self::GitBranchCreate | Self::GitBranchDelete | Self::GitBranchRename => "branch",
            Self::GitSetUpstream => "branch",
            Self::GitBranchSwitch | Self::GitCheckout => "checkout",
            Self::GitTagCreate | Self::GitTagDelete => "tag",
            Self::GitRemoteAdd | Self::GitRemoteRemove | Self::GitRemoteSetUrl => "remote",
            Self::GitMerge => "merge",
            Self::GitRebase => "rebase",
            Self::GitCherryPick => "cherry-pick",
            Self::GitRevert => "revert",
            Self::GitReset => "reset",
            Self::GitClean => "clean",
            // Continue/abort name the operation in `target`; see subcommand_for().
            Self::GitContinue | Self::GitAbort => "",
            // Batch 5b: the workspace tokens. `stash`/`worktree` are ALSO read
            // tokens; only their listing form is read-allowed (runner.rs).
            Self::GitStashSave | Self::GitStashPop | Self::GitStashApply | Self::GitStashDrop => {
                "stash"
            }
            Self::GitWorktreeAdd | Self::GitWorktreeRemove => "worktree",
            Self::GitSubmoduleInit | Self::GitSubmoduleUpdate => "submodule",
            Self::SvnCommit => "commit",
            Self::SvnUpdate => "update",
            Self::SvnRevert => "revert",
            Self::SvnAdd => "add",
            Self::SvnRemove => "remove",
        }
    }

    /// The subcommand for an operation whose token depends on `target`
    /// (`GitContinue` / `GitAbort` name merge/rebase/cherry-pick/revert there).
    #[must_use]
    pub fn subcommand_for(self, target: Option<&str>) -> &'static str {
        match self {
            // On the delivery baseline (git 1.8.3.1) `git merge --continue` does
            // not exist: a resolved merge is completed with `git commit --no-edit`.
            // cherry-pick/revert/rebase all have `--continue` there.
            Self::GitContinue => match target {
                Some("merge") => "commit",
                Some("rebase") => "rebase",
                Some("cherry-pick") => "cherry-pick",
                Some("revert") => "revert",
                _ => "",
            },
            Self::GitAbort => match target {
                Some("merge") => "merge",
                Some("rebase") => "rebase",
                Some("cherry-pick") => "cherry-pick",
                Some("revert") => "revert",
                _ => "",
            },
            other => other.subcommand(),
        }
    }

    /// Whether at least one path is mandatory.
    #[must_use]
    pub const fn requires_paths(self) -> bool {
        matches!(
            self,
            Self::GitStage
                | Self::GitUnstage
                | Self::GitDiscard
                | Self::SvnRevert
                | Self::SvnAdd
                | Self::SvnRemove
                | Self::GitClean
        )
    }

    /// Whether a non-empty commit message is mandatory.
    #[must_use]
    pub const fn requires_message(self) -> bool {
        matches!(self, Self::GitCommit | Self::SvnCommit)
    }

    /// Whether the operation takes a message at all. `GitStashSave` takes an
    /// OPTIONAL one (git's own default subject is used when it is empty).
    #[must_use]
    pub const fn uses_message(self) -> bool {
        self.requires_message() || matches!(self, Self::GitStashSave)
    }

    /// Whether the operation talks to a remote (and therefore may need
    /// credentials, and must never hang waiting for a terminal prompt).
    #[must_use]
    pub const fn network(self) -> bool {
        matches!(self, Self::GitFetch | Self::GitPull | Self::GitPush)
    }

    /// Whether the operation needs `name`.
    #[must_use]
    pub const fn requires_name(self) -> bool {
        matches!(
            self,
            Self::GitBranchCreate
                | Self::GitBranchSwitch
                | Self::GitBranchDelete
                | Self::GitBranchRename
                | Self::GitTagCreate
                | Self::GitTagDelete
                | Self::GitRemoteAdd
                | Self::GitRemoteRemove
                | Self::GitRemoteSetUrl
                | Self::GitSetUpstream
        )
    }

    /// Whether the operation accepts `name` (required for some, optional for
    /// `push`/`pull`/`worktree add`, where it is the branch).
    #[must_use]
    pub const fn uses_name(self) -> bool {
        self.requires_name()
            || matches!(
                self,
                Self::GitPull | Self::GitPush | Self::GitRebase | Self::GitWorktreeAdd
            )
    }

    /// Whether `name` is a ref name (slashes allowed) rather than a remote name.
    #[must_use]
    pub const fn name_is_ref(self) -> bool {
        matches!(
            self,
            Self::GitBranchCreate
                | Self::GitBranchSwitch
                | Self::GitBranchDelete
                | Self::GitBranchRename
                | Self::GitTagCreate
                | Self::GitTagDelete
                | Self::GitSetUpstream
                | Self::GitPull
                | Self::GitPush
                | Self::GitWorktreeAdd
        )
    }

    /// Whether the operation accepts `target` (start point / new name / ref /
    /// remote / upstream / stash ref / worktree path).
    #[must_use]
    pub const fn uses_target(self) -> bool {
        matches!(
            self,
            Self::GitBranchCreate
                | Self::GitTagCreate
                | Self::GitFetch
                | Self::GitPull
                | Self::GitPush
                | Self::GitBranchRename
                | Self::GitCheckout
                | Self::GitSetUpstream
                | Self::GitMerge
                | Self::GitRebase
                | Self::GitCherryPick
                | Self::GitRevert
                | Self::GitReset
                | Self::GitContinue
                | Self::GitAbort
                | Self::GitStashPop
                | Self::GitStashApply
                | Self::GitStashDrop
                | Self::GitWorktreeAdd
                | Self::GitWorktreeRemove
        )
    }

    /// Whether `target` is mandatory.
    #[must_use]
    pub const fn requires_target(self) -> bool {
        matches!(
            self,
            Self::GitBranchRename
                | Self::GitCheckout
                | Self::GitSetUpstream
                | Self::GitMerge
                | Self::GitRebase
                | Self::GitCherryPick
                | Self::GitRevert
                | Self::GitReset
                | Self::GitContinue
                | Self::GitAbort
                | Self::GitWorktreeAdd
                | Self::GitWorktreeRemove
        )
    }

    /// Whether the operation takes a remote `url`.
    #[must_use]
    pub const fn uses_url(self) -> bool {
        matches!(self, Self::GitRemoteAdd | Self::GitRemoteSetUrl)
    }

    /// Whether the UI must ask for confirmation before running it (AC-5.3 /
    /// batch 4a: branch delete and tag delete join this list).
    #[must_use]
    pub const fn is_irreversible(self) -> bool {
        matches!(
            self,
            Self::GitDiscard
                | Self::GitBranchDelete
                | Self::GitTagDelete
                | Self::GitRemoteRemove
                | Self::GitReset
                | Self::GitClean
                | Self::GitStashDrop
                | Self::GitWorktreeRemove
                | Self::SvnRevert
                | Self::SvnRemove
        )
    }
}

/// Git write subcommands the write seam may issue, one per accepted operation.
///
/// Separate from [`crate::runner::GIT_READ_ONLY`]: the read table is unchanged
/// and none of these entries appears in it (asserted by the tests).
pub const GIT_WRITE_ALLOWED: &[&str] = &[
    "add",
    "reset",
    "checkout",
    "commit",
    "fetch",
    "pull",
    "push",
    "branch",
    "tag",
    "remote",
    // batch 5a history operations
    "merge",
    "rebase",
    "cherry-pick",
    "revert",
    "clean",
    // batch 5b workspace operations (`stash`/`worktree` share their token with a
    // read listing, narrowed in runner.rs; `submodule` is write-only)
    "stash",
    "worktree",
    "submodule",
];

/// Subversion write subcommands the write seam may issue.
///
/// Separate from [`crate::runner::SVN_READ_ONLY`], which is unchanged.
pub const SVN_WRITE_ALLOWED: &[&str] = &["commit", "update", "revert", "add", "remove"];

fn write_table(program: &str) -> Option<&'static [&'static str]> {
    match program {
        "git" => Some(GIT_WRITE_ALLOWED),
        "svn" => Some(SVN_WRITE_ALLOWED),
        _ => None,
    }
}

/// `true` when `program` + its subcommand is on the WRITE allow-list.
///
/// Default deny: an unknown tool, an empty argument list, a read subcommand or
/// any unlisted write subcommand is refused.
#[must_use]
pub fn is_write_allowed(program: &str, args: &[String]) -> bool {
    let Some(table) = write_table(program) else {
        return false;
    };
    let Some(index) = subcommand_index(args) else {
        return false;
    };
    let Some(subcommand) = args.get(index) else {
        return false;
    };
    table.contains(&subcommand.as_str())
}

/// The write gate: the refusal reason for anything not on the write allow-list.
///
/// Runs before the process exists, exactly like the read gate.
///
/// # Errors
///
/// Returns a human-readable reason naming the program and subcommand.
pub fn ensure_write_allowed(program: &str, args: &[String]) -> Result<(), String> {
    if write_table(program).is_none() {
        return Err(format!("unknown VCS tool '{program}': refused"));
    }
    let subcommand = subcommand_of(program, args);
    if subcommand.is_empty() {
        return Err(format!("{program} without a subcommand: refused"));
    }
    if !is_write_allowed(program, args) {
        return Err(format!(
            "operation '{program} {subcommand}' is not on the VCS write allow-list: refused"
        ));
    }
    Ok(())
}

/// The exact process invocation one validated request produces.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WriteInvocation {
    /// Executable (`git` / `svn`).
    pub program: &'static str,
    /// Directory the process runs in (the repository root).
    pub working_tree: PathBuf,
    /// Arguments, without the executable.
    pub args: Vec<String>,
    /// Environment variables the tool runs with. Credentials travel ONLY here
    /// (never in `args`, which `ps` exposes, and never on disk).
    pub env: Vec<(String, String)>,
}

/// A write request as the panel builds it: a typed operation plus parameters.
#[derive(Clone, PartialEq, Eq)]
pub struct WriteRequest {
    /// The panel's project root; the repository root is found by walking up.
    pub root: PathBuf,
    /// The operation to run.
    pub op: WriteOp,
    /// Repository-relative paths (never absolute, never escaping the tree).
    pub paths: Vec<String>,
    /// Commit message (`GitCommit` / `SvnCommit` only).
    pub message: Option<String>,
    /// Author override (`GitCommit` only).
    pub author: Option<String>,
    /// Amend the previous commit (`GitCommit` only).
    pub amend: bool,
    /// Branch / tag / remote name (create, switch, delete, rename source,
    /// remote name, upstream local branch; for `push`/`pull` it is the branch).
    pub name: Option<String>,
    /// Start point (branch/tag create), new name (rename), ref to check out, the
    /// remote (`fetch`/`pull`/`push`), or the upstream ref (`set-upstream`).
    pub target: Option<String>,
    /// Remote URL (`GitRemoteAdd` / `GitRemoteSetUrl` only).
    pub url: Option<String>,
    /// Remote username (network operations only), held in memory.
    pub credential_user: Option<String>,
    /// Remote password (network operations only), held in memory. It never enters
    /// the tool argv, is never written to disk, and is redacted out of the
    /// captured output.
    pub credential_password: Option<String>,
    /// Push the branch and set its upstream (`GitPush` only).
    pub set_upstream: bool,
    /// Reset mode (`GitReset` only): `soft`, `mixed` or `hard`.
    pub mode: Option<String>,
}

impl std::fmt::Debug for WriteRequest {
    /// The password is REDACTED: a `{:?}` of a request must never leak it into a
    /// log line, a panic message or a test failure report.
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("WriteRequest")
            .field("root", &self.root)
            .field("op", &self.op)
            .field("paths", &self.paths)
            .field("message", &self.message)
            .field("author", &self.author)
            .field("amend", &self.amend)
            .field("name", &self.name)
            .field("target", &self.target)
            .field("url", &self.url)
            .field("credential_user", &self.credential_user)
            .field(
                "credential_password",
                &self.credential_password.as_ref().map(|_| "***"),
            )
            .field("set_upstream", &self.set_upstream)
            .finish()
    }
}

/// Why a request was rejected or a tool could not be started.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum WriteError {
    /// The request violates the operation's contract; nothing was spawned.
    Invalid(String),
    /// The process could not be started (or the write gate refused it).
    Failed(String),
}

impl WriteError {
    /// The human-readable reason.
    #[must_use]
    pub fn message(&self) -> &str {
        match self {
            Self::Invalid(message) | Self::Failed(message) => message,
        }
    }

    /// Whether this is a rejected request (maps to `VD_ERR_INVALID_ARGUMENT`).
    #[must_use]
    pub fn is_invalid(&self) -> bool {
        matches!(self, Self::Invalid(_))
    }
}

/// Outcome of a write that really ran (a non-zero tool exit is a normal result).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WriteOutcome {
    /// Backend that ran the operation.
    pub backend: Backend,
    /// The operation that ran.
    pub op: WriteOp,
    /// The tool exited zero.
    pub success: bool,
    /// Captured stdout.
    pub stdout: String,
    /// Captured stderr (the real reason when `success` is false).
    pub stderr: String,
}

fn backend_name(backend: Backend) -> &'static str {
    match backend {
        Backend::Git => "Git",
        Backend::Svn => "Subversion",
        Backend::None => "no",
    }
}

impl WriteRequest {
    /// The repository root this request writes into, or a refusal.
    fn working_tree(&self) -> Result<PathBuf, WriteError> {
        if !self.root.is_dir() {
            return Err(WriteError::Invalid(format!(
                "not a directory: {}",
                self.root.display()
            )));
        }
        let (backend, found) = detect(&self.root);
        let Some(working_tree) = found else {
            return Err(WriteError::Invalid(format!(
                "not a Git or Subversion working tree: {}",
                self.root.display()
            )));
        };
        if backend != self.op.backend() {
            return Err(WriteError::Invalid(format!(
                "{} operation requires a {} working tree, but {} is {}",
                self.op.subcommand(),
                backend_name(self.op.backend()),
                working_tree.display(),
                backend_name(backend)
            )));
        }
        Ok(working_tree)
    }

    /// Reject parameters that the operation does not take or cannot accept.
    fn validate_parameters(&self) -> Result<(), WriteError> {
        let op = self.op;
        let takes_paths = op.requires_paths()
            || matches!(
                op,
                WriteOp::GitCommit | WriteOp::SvnCommit | WriteOp::SvnUpdate
            );
        if op.requires_paths() && self.paths.is_empty() {
            return Err(WriteError::Invalid(format!(
                "{} requires at least one path",
                op.subcommand()
            )));
        }
        if !takes_paths && !self.paths.is_empty() {
            return Err(WriteError::Invalid(format!(
                "{} does not take paths",
                op.subcommand()
            )));
        }
        for path in &self.paths {
            validate_path(path)?;
        }
        if op.requires_message() {
            let message = self.message.as_deref().map(str::trim).unwrap_or("");
            if message.is_empty() {
                return Err(WriteError::Invalid(format!(
                    "{} requires a non-empty commit message",
                    op.subcommand()
                )));
            }
        } else if self.message.is_some() && !op.uses_message() {
            return Err(WriteError::Invalid(format!(
                "{} does not take a commit message",
                op.subcommand()
            )));
        } else if op == WriteOp::GitStashSave {
            if let Some(message) = self.message.as_deref() {
                validate_message_text(message, "stash message")?;
            }
        }
        if self.author.is_some() && op != WriteOp::GitCommit {
            return Err(WriteError::Invalid(format!(
                "{} does not take an author override",
                op.subcommand()
            )));
        }
        if self.amend && op != WriteOp::GitCommit {
            return Err(WriteError::Invalid(format!(
                "{} does not support --amend",
                op.subcommand()
            )));
        }
        if self.set_upstream && op != WriteOp::GitPush {
            return Err(WriteError::Invalid(format!(
                "{} does not support --set-upstream",
                op.subcommand()
            )));
        }
        // --- batch 4a: names, refs, remotes and credentials -----------------
        if op.requires_name() && self.name.as_deref().map(str::trim).unwrap_or("").is_empty() {
            return Err(WriteError::Invalid(format!(
                "{} requires a name",
                op.subcommand()
            )));
        }
        if let Some(name) = self.name.as_deref() {
            if !op.uses_name() {
                return Err(WriteError::Invalid(format!(
                    "{} does not take a name",
                    op.subcommand()
                )));
            }
            if op.name_is_ref() {
                validate_ref(name, "name")?;
            } else {
                validate_remote_name(name)?;
            }
        }
        if let Some(target) = self.target.as_deref() {
            if !op.uses_target() {
                return Err(WriteError::Invalid(format!(
                    "{} does not take a target",
                    op.subcommand()
                )));
            }
            match op {
                // A worktree path is a filesystem path, not a ref: it may be
                // absolute and may contain `..` on purpose (the worktree usually
                // lives BESIDE the repository), so only the argument-shape rules
                // apply.
                WriteOp::GitWorktreeAdd | WriteOp::GitWorktreeRemove => {
                    validate_worktree_path(target)?;
                }
                WriteOp::GitMerge
                | WriteOp::GitRebase
                | WriteOp::GitCherryPick
                | WriteOp::GitRevert
                | WriteOp::GitReset
                | WriteOp::GitStashPop
                | WriteOp::GitStashApply
                | WriteOp::GitStashDrop => validate_revision(target, "target")?,
                _ => validate_ref(target, "target")?,
            }
        } else if op.requires_target() {
            return Err(WriteError::Invalid(format!(
                "{} requires a target",
                op.subcommand()
            )));
        }
        if let Some(url) = self.url.as_deref() {
            if !op.uses_url() {
                return Err(WriteError::Invalid(format!(
                    "{} does not take a URL",
                    op.subcommand()
                )));
            }
            validate_url(url)?;
        } else if op.uses_url() {
            return Err(WriteError::Invalid(format!(
                "{} requires a URL",
                op.subcommand()
            )));
        }
        // --- batch 5a: reset mode and the continue/abort operation -----------
        match self.op {
            WriteOp::GitReset => {
                let mode = self.mode.as_deref().map(str::trim).unwrap_or("");
                if !matches!(mode, "soft" | "mixed" | "hard") {
                    return Err(WriteError::Invalid(
                        "reset mode must be one of soft, mixed, hard".to_string(),
                    ));
                }
            }
            WriteOp::GitContinue | WriteOp::GitAbort => {
                let operation = self.target.as_deref().map(str::trim).unwrap_or("");
                if !matches!(operation, "merge" | "rebase" | "cherry-pick" | "revert") {
                    return Err(WriteError::Invalid(
                        "the operation in progress must be merge, rebase, cherry-pick or revert"
                            .to_string(),
                    ));
                }
            }
            _ => {
                if self.mode.is_some() {
                    return Err(WriteError::Invalid(format!(
                        "{} does not take a reset mode",
                        self.op.subcommand_for(self.target.as_deref())
                    )));
                }
            }
        }
        match (&self.credential_user, &self.credential_password) {
            (None, None) => {}
            (Some(user), Some(password)) => {
                if !op.network() {
                    return Err(WriteError::Invalid(format!(
                        "{} does not take credentials",
                        op.subcommand()
                    )));
                }
                if user.is_empty() || password.is_empty() {
                    return Err(WriteError::Invalid(
                        "credentials must be non-empty when provided".to_string(),
                    ));
                }
            }
            _ => {
                return Err(WriteError::Invalid(
                    "credential user and password must be provided together".to_string(),
                ));
            }
        }
        Ok(())
    }

    /// Validate the request and build the exact invocation the tool will see.
    ///
    /// # Errors
    ///
    /// [`WriteError::Invalid`] when the request does not satisfy the operation's
    /// contract; no process is spawned in that case.
    pub fn invocation(&self) -> Result<WriteInvocation, WriteError> {
        let working_tree = self.working_tree()?;
        self.validate_parameters()?;
        let op = self.op;
        // The process runs with the repository root as its working directory —
        // NOT with a global path option: the CentOS 7 baseline (git 1.8.3) has
        // no such option, so passing one would fail on the delivery target.
        // Paths are repository-relative by construction (see `validate_path`).
        let mut args: Vec<String> = Vec::new();
        let mut env: Vec<(String, String)> = Vec::new();
        if op.network() {
            // A missing credential must fail fast, never hang waiting for a TTY.
            env.push(("GIT_TERMINAL_PROMPT".to_string(), "0".to_string()));
            if let (Some(user), Some(password)) = (&self.credential_user, &self.credential_password)
            {
                // The secret travels in the CHILD ENVIRONMENT only: it is not in
                // argv (which `ps` exposes), not in a config file, and not in
                // ~/.git-credentials. An empty helper first clears every
                // configured helper, so git can never store it behind our back.
                env.push(("VD_GIT_USERNAME".to_string(), user.clone()));
                env.push(("VD_GIT_PASSWORD".to_string(), password.clone()));
                args.push("-c".to_string());
                args.push("credential.helper=".to_string());
                args.push("-c".to_string());
                args.push(
                    concat!(
                        "credential.helper=!f() { if [ \"$1\" = get ]; then ",
                        "printf 'username=%s\\npassword=%s\\n' \"$VD_GIT_USERNAME\" ",
                        "\"$VD_GIT_PASSWORD\"; fi; }; f"
                    )
                    .to_string(),
                );
            }
        }
        let subcommand = op.subcommand_for(self.target.as_deref());
        if subcommand.is_empty() {
            return Err(WriteError::Invalid(
                "this operation needs the operation in progress in `target`".to_string(),
            ));
        }
        args.push(subcommand.to_string());
        match op {
            WriteOp::GitStage | WriteOp::GitDiscard => {
                args.push("--".to_string());
                args.extend(self.paths.iter().cloned());
            }
            WriteOp::GitUnstage => {
                args.push("HEAD".to_string());
                args.push("--".to_string());
                args.extend(self.paths.iter().cloned());
            }
            WriteOp::GitCommit => {
                args.push("-m".to_string());
                args.push(
                    self.message
                        .as_deref()
                        .map(str::trim)
                        .unwrap_or_default()
                        .to_string(),
                );
                if let Some(author) = self.author.as_deref().map(str::trim) {
                    if !author.is_empty() {
                        args.push(format!("--author={author}"));
                    }
                }
                if self.amend {
                    args.push("--amend".to_string());
                }
                if !self.paths.is_empty() {
                    args.push("--".to_string());
                    args.extend(self.paths.iter().cloned());
                }
            }
            WriteOp::GitFetch => {
                if let Some(remote) = self.target.as_deref() {
                    args.push(remote.to_string());
                }
            }
            WriteOp::GitPull => {
                // `--ff-only`: a divergence is reported instead of silently
                // creating a merge commit the user never asked for.
                args.push("--ff-only".to_string());
                if let Some(remote) = self.target.as_deref() {
                    args.push(remote.to_string());
                }
                if let Some(branch) = self.name.as_deref() {
                    args.push(branch.to_string());
                }
            }
            WriteOp::GitPush => {
                if self.set_upstream {
                    args.push("--set-upstream".to_string());
                }
                if let Some(remote) = self.target.as_deref() {
                    args.push(remote.to_string());
                }
                if let Some(branch) = self.name.as_deref() {
                    args.push(branch.to_string());
                }
            }
            WriteOp::GitBranchCreate => {
                args.push(self.name.clone().unwrap_or_default());
                if let Some(start) = self.target.as_deref() {
                    args.push(start.to_string());
                }
            }
            WriteOp::GitBranchSwitch => {
                args.push(self.name.clone().unwrap_or_default());
            }
            WriteOp::GitBranchDelete | WriteOp::GitTagDelete => {
                args.push("-d".to_string());
                args.push(self.name.clone().unwrap_or_default());
            }
            WriteOp::GitBranchRename => {
                args.push("-m".to_string());
                args.push(self.name.clone().unwrap_or_default());
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitTagCreate => {
                args.push(self.name.clone().unwrap_or_default());
                if let Some(target) = self.target.as_deref() {
                    args.push(target.to_string());
                }
            }
            WriteOp::GitCheckout => {
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitRemoteAdd => {
                args.push("add".to_string());
                args.push(self.name.clone().unwrap_or_default());
                args.push(self.url.clone().unwrap_or_default());
            }
            WriteOp::GitRemoteRemove => {
                args.push("rm".to_string());
                args.push(self.name.clone().unwrap_or_default());
            }
            WriteOp::GitRemoteSetUrl => {
                args.push("set-url".to_string());
                args.push(self.name.clone().unwrap_or_default());
                args.push(self.url.clone().unwrap_or_default());
            }
            WriteOp::GitSetUpstream => {
                let upstream = self.target.clone().unwrap_or_default();
                args.push(format!("--set-upstream-to={upstream}"));
                args.push(self.name.clone().unwrap_or_default());
            }
            WriteOp::GitMerge => {
                // `--no-edit`: the merge commit message comes from git, no editor.
                args.push("--no-edit".to_string());
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitRebase => {
                args.push(self.target.clone().unwrap_or_default());
                if let Some(branch) = self.name.as_deref() {
                    args.push(branch.to_string());
                }
            }
            WriteOp::GitCherryPick => {
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitRevert => {
                args.push("--no-edit".to_string());
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitReset => {
                let mode = self.mode.as_deref().unwrap_or("mixed");
                args.push(format!("--{mode}"));
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitClean => {
                args.push("-f".to_string());
                args.push("--".to_string());
                args.extend(self.paths.iter().cloned());
            }
            WriteOp::GitContinue => {
                // A continue may open an editor for the pending commit message;
                // `true` keeps it non-interactive without changing the message.
                env.push(("GIT_EDITOR".to_string(), "true".to_string()));
                if self.target.as_deref() == Some("merge") {
                    // git 1.8.3: `merge --continue` does not exist; the resolved
                    // merge is the pending commit, so `commit --no-edit` completes it.
                    args.push("--no-edit".to_string());
                } else {
                    args.push("--continue".to_string());
                }
            }
            WriteOp::GitAbort => {
                args.push("--abort".to_string());
            }
            WriteOp::GitStashSave => {
                args.push("save".to_string());
                // git 1.8.3.1 has no `stash save -m` (verified in the pinned
                // image), so the message is positional. `--` is emitted only when
                // the message could be read as an option, which keeps the stored
                // subject clean in the normal case.
                let message = self.message.as_deref().map(str::trim).unwrap_or_default();
                if !message.is_empty() {
                    if message.starts_with('-') {
                        args.push("--".to_string());
                    }
                    args.push(message.to_string());
                }
            }
            WriteOp::GitStashPop => {
                args.push("pop".to_string());
                if let Some(stash) = self.target.as_deref() {
                    args.push(stash.to_string());
                }
            }
            WriteOp::GitStashApply => {
                args.push("apply".to_string());
                if let Some(stash) = self.target.as_deref() {
                    args.push(stash.to_string());
                }
            }
            WriteOp::GitStashDrop => {
                args.push("drop".to_string());
                if let Some(stash) = self.target.as_deref() {
                    args.push(stash.to_string());
                }
            }
            WriteOp::GitWorktreeAdd => {
                args.push("add".to_string());
                args.push(self.target.clone().unwrap_or_default());
                if let Some(branch) = self.name.as_deref() {
                    args.push(branch.to_string());
                }
            }
            WriteOp::GitWorktreeRemove => {
                // No `--force`: a worktree with modified or untracked files is
                // refused by git with its real reason instead of being destroyed.
                args.push("remove".to_string());
                args.push(self.target.clone().unwrap_or_default());
            }
            WriteOp::GitSubmoduleInit => {
                args.push("init".to_string());
            }
            WriteOp::GitSubmoduleUpdate => {
                args.push("update".to_string());
                // `--init` registers and checks out in a fresh clone, so one
                // operation covers "initialize and update".
                args.push("--init".to_string());
            }
            WriteOp::SvnCommit => {
                // AC-6.4: a commit may contact a remote repository, so it must
                // never wait on a terminal prompt and must never store credentials
                // on disk. (`svn update` gets the same two flags below.)
                args.push("--non-interactive".to_string());
                args.push("--no-auth-cache".to_string());
                args.push("-m".to_string());
                args.push(
                    self.message
                        .as_deref()
                        .map(str::trim)
                        .unwrap_or_default()
                        .to_string(),
                );
                args.extend(self.paths.iter().cloned());
            }
            WriteOp::SvnUpdate => {
                args.push("--non-interactive".to_string());
                args.push("--no-auth-cache".to_string());
                args.extend(self.paths.iter().cloned());
            }
            WriteOp::SvnRevert | WriteOp::SvnAdd | WriteOp::SvnRemove => {
                args.extend(self.paths.iter().cloned());
            }
        }
        Ok(WriteInvocation {
            program: match op.backend() {
                Backend::Git => "git",
                _ => "svn",
            },
            working_tree,
            args,
            env,
        })
    }
}

/// Reject a REVISION argument (history-operation target) that is unsafe as an
/// argument. Unlike a branch name, a revision may legitimately use `~` / `^`
/// (`HEAD~1`, `main^2`), so only the shapes that could become an option or smuggle
/// control characters, a range or an escape are refused.
fn validate_revision(value: &str, what: &str) -> Result<(), WriteError> {
    if value.trim().is_empty() {
        return Err(WriteError::Invalid(format!("empty {what}")));
    }
    if value.starts_with('-') {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' starts with '-': refused"
        )));
    }
    if value.chars().any(char::is_control) || value.chars().any(char::is_whitespace) {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' contains whitespace or a control character: refused"
        )));
    }
    if value.contains("..") || value.contains(':') {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' is a range or a path, not one revision: refused"
        )));
    }
    if value.starts_with('/') || value.ends_with('/') {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' is not a valid revision: refused"
        )));
    }
    Ok(())
}

/// Reject a worktree path that is unsafe as an argument.
///
/// A worktree path is a real filesystem path: it may be absolute and it may
/// contain `..` (a worktree normally lives beside the repository), so it is NOT
/// held to the repository-relative rule that `-- <paths>` follows. What is
/// refused is the shape that could be read as an option (`-x`, `--force`) or
/// smuggle a control character / newline into the argument.
fn validate_worktree_path(value: &str) -> Result<(), WriteError> {
    let path = value.trim();
    if path.is_empty() {
        return Err(WriteError::Invalid("empty worktree path".to_string()));
    }
    if path.starts_with('-') {
        return Err(WriteError::Invalid(format!(
            "worktree path '{path}' starts with '-': refused"
        )));
    }
    if path.chars().any(char::is_control) {
        return Err(WriteError::Invalid(
            "worktree path contains a control character: refused".to_string(),
        ));
    }
    Ok(())
}

/// Reject a free-text message that cannot be one argv element (a control
/// character such as a newline would split what the user sees into lines the
/// status panel cannot render).
fn validate_message_text(value: &str, what: &str) -> Result<(), WriteError> {
    if value.chars().any(char::is_control) {
        return Err(WriteError::Invalid(format!(
            "the {what} contains a control character: refused"
        )));
    }
    Ok(())
}

/// Reject a ref (branch/tag/upstream/commit-ish) that is unsafe as an argument.
fn validate_ref(value: &str, what: &str) -> Result<(), WriteError> {
    if value.trim().is_empty() {
        return Err(WriteError::Invalid(format!("empty {what}")));
    }
    if value.starts_with('-') {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' starts with '-': refused"
        )));
    }
    if value.chars().any(char::is_control) {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' contains a control character: refused"
        )));
    }
    if value.chars().any(char::is_whitespace) {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' contains whitespace: refused"
        )));
    }
    if value.contains("..") {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' contains '..': refused"
        )));
    }
    if value
        .chars()
        .any(|character| matches!(character, '~' | '^' | ':' | '?' | '*' | '[' | '\\'))
    {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' contains a character git refuses in a ref: refused"
        )));
    }
    if value.ends_with('.')
        || value.ends_with(".lock")
        || value.ends_with('/')
        || value.starts_with('/')
    {
        return Err(WriteError::Invalid(format!(
            "{what} '{value}' is not a valid ref name: refused"
        )));
    }
    Ok(())
}

/// Reject a remote name (no slash: a remote name is a single path component).
fn validate_remote_name(value: &str) -> Result<(), WriteError> {
    validate_ref(value, "remote name")?;
    if value.contains('/') {
        return Err(WriteError::Invalid(format!(
            "remote name '{value}' must not contain '/': refused"
        )));
    }
    Ok(())
}

/// Reject a URL that could become an option or smuggle control characters.
fn validate_url(value: &str) -> Result<(), WriteError> {
    if value.trim().is_empty() {
        return Err(WriteError::Invalid("empty URL".to_string()));
    }
    if value.starts_with('-') {
        return Err(WriteError::Invalid(format!(
            "URL '{value}' starts with '-': refused"
        )));
    }
    if value.chars().any(char::is_control) || value.chars().any(char::is_whitespace) {
        return Err(WriteError::Invalid(
            "URL contains whitespace or a control character: refused".to_string(),
        ));
    }
    Ok(())
}

/// Reject a path that could leave the working tree or become an option.
fn validate_path(path: &str) -> Result<(), WriteError> {
    if path.trim().is_empty() {
        return Err(WriteError::Invalid("empty path".to_string()));
    }
    if path.starts_with('-') {
        return Err(WriteError::Invalid(format!(
            "path '{path}' starts with '-': refused"
        )));
    }
    let candidate = Path::new(path);
    if candidate.is_absolute() {
        return Err(WriteError::Invalid(format!(
            "absolute path '{path}': paths must be repository-relative"
        )));
    }
    if candidate
        .components()
        .any(|component| matches!(component, Component::ParentDir))
    {
        return Err(WriteError::Invalid(format!(
            "path '{path}' escapes the working tree: refused"
        )));
    }
    Ok(())
}

/// The seam a write goes through. Production is [`SystemWriteRunner`]; tests
/// inject [`RecordingWriteRunner`], which applies the SAME gate.
pub trait WriteRunner {
    /// Run one validated write invocation.
    ///
    /// # Errors
    ///
    /// Returns a message when the gate refused the invocation or the process
    /// could not be started. A non-zero exit status is a normal
    /// [`CommandOutput`] with `success = false`.
    fn run_write(&self, invocation: &WriteInvocation) -> Result<CommandOutput, String>;
}

/// Production runner: runs the write gate, then spawns the pinned tool.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemWriteRunner;

impl WriteRunner for SystemWriteRunner {
    fn run_write(&self, invocation: &WriteInvocation) -> Result<CommandOutput, String> {
        // The gate runs BEFORE the process exists, exactly like the read gate.
        ensure_write_allowed(invocation.program, &invocation.args)?;
        // Same resolution order as the read path (bundled > PATH > env); the
        // write allow-list above is untouched.
        let resolved = crate::toolchain::resolve_or_error(invocation.program)?;
        let output = Command::new(&resolved.path)
            .current_dir(&invocation.working_tree)
            .args(&invocation.args)
            .envs(invocation.env.iter().map(|(key, value)| (key, value)))
            // See the read path: the wrapper starts with the host loader and
            // re-execs the AppDir loader, so the bundle's LD_LIBRARY_PATH must
            // not leak into the shell that starts it.
            .env_remove("LD_LIBRARY_PATH")
            .output()
            .map_err(|error| {
                format!(
                    "cannot run {} ({}): {error}",
                    invocation.program,
                    resolved.path.display()
                )
            })?;
        Ok(CommandOutput {
            success: output.status.success(),
            stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
        })
    }
}

/// Stub write runner: applies the production gate, records the invocation and
/// answers from one canned result (so the gate is tested without a process).
type RecordingAnswer = CommandOutput;

#[derive(Debug, Default, Clone)]
pub struct RecordingWriteRunner {
    invocations: Arc<Mutex<Vec<WriteInvocation>>>,
    answer: Arc<Mutex<Option<RecordingAnswer>>>,
}

impl RecordingWriteRunner {
    /// A runner that answers every allowed invocation with empty success.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Replaces the canned answer.
    pub fn answer(&self, success: bool, stdout: &str, stderr: &str) {
        if let Ok(mut slot) = self.answer.lock() {
            *slot = Some(CommandOutput {
                success,
                stdout: stdout.to_string(),
                stderr: stderr.to_string(),
            });
        }
    }

    /// Every invocation that passed the write gate.
    #[must_use]
    pub fn invocations(&self) -> Vec<WriteInvocation> {
        match self.invocations.lock() {
            Ok(invocations) => invocations.clone(),
            Err(_) => Vec::new(),
        }
    }
}

impl WriteRunner for RecordingWriteRunner {
    fn run_write(&self, invocation: &WriteInvocation) -> Result<CommandOutput, String> {
        // Same gate as production: a test cannot prove the allow-list with a
        // permissive stub.
        ensure_write_allowed(invocation.program, &invocation.args)?;
        if let Ok(mut invocations) = self.invocations.lock() {
            invocations.push(invocation.clone());
        }
        match self.answer.lock() {
            Ok(answer) => Ok(answer.clone().unwrap_or(CommandOutput {
                success: true,
                stdout: String::new(),
                stderr: String::new(),
            })),
            Err(_) => Err("recording write runner is poisoned".to_string()),
        }
    }
}

/// Run a validated request through an explicit runner.
///
/// # Errors
///
/// [`WriteError::Invalid`] when the request is refused before any process, and
/// [`WriteError::Failed`] when the runner could not start the tool.
pub fn execute<R: WriteRunner>(
    request: &WriteRequest,
    runner: &R,
) -> Result<WriteOutcome, WriteError> {
    let invocation = request.invocation()?;
    match runner.run_write(&invocation) {
        Ok(output) => Ok(WriteOutcome {
            backend: request.op.backend(),
            op: request.op,
            success: output.success,
            stdout: redact(&output.stdout, request),
            stderr: redact(&output.stderr, request),
        }),
        Err(message) => Err(WriteError::Failed(redact(&message, request))),
    }
}

/// Remove any credential material from tool output before it can reach a log, a
/// panel line or an evidence file (AC-4.4).
fn redact(text: &str, request: &WriteRequest) -> String {
    let mut redacted = text.to_string();
    for secret in [&request.credential_password, &request.credential_user] {
        if let Some(secret) = secret.as_deref() {
            if secret.len() >= 2 {
                redacted = redacted.replace(secret, "***");
            }
        }
    }
    redact_url_userinfo(&redacted)
}

/// `scheme://user:pass@host` -> `scheme://***@host` (and the same for any other
/// URL userinfo git may echo).
fn redact_url_userinfo(text: &str) -> String {
    let mut output = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(position) = rest.find("://") {
        let (head, tail) = rest.split_at(position + 3);
        output.push_str(head);
        match tail.find('@') {
            Some(at) if !tail[..at].contains('/') => {
                output.push_str("***@");
                rest = &tail[at + 1..];
            }
            _ => rest = tail,
        }
    }
    output.push_str(rest);
    output
}

/// Production entry point: a [`WriteRequest`] through [`SystemWriteRunner`].
///
/// # Errors
///
/// As [`execute`].
pub fn write(request: &WriteRequest) -> Result<WriteOutcome, WriteError> {
    execute(request, &SystemWriteRunner)
}
