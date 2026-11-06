//! Process seam for the VCS backends, plus the read-only guarantee.
//!
//! Every backend command goes through a [`CommandRunner`]. The only runner used
//! in production is [`SystemRunner`], and it refuses to spawn anything that is
//! not on the per-backend read-only allow-list: the guarantee "no write
//! operation is ever executed" is enforced BEFORE the process exists, not by the
//! caller's discipline.
//!
//! The working directory is part of the seam, not of the argument list: the
//! runner spawns the tool with `Command::current_dir(working_tree)`. Passing the
//! repository as a global path option is NOT portable — the delivery baseline
//! (CentOS 7, git 1.8.3.1) does not support it — so the seam carries the cwd and
//! no caller can reintroduce the option.
//!
//! Tests inject a [`RecordingRunner`] (a stub) and assert three properties:
//!   * a write subcommand is refused and never reaches the spawn call;
//!   * a complete status + branch/revision + diff session issues only allowed
//!     read-only subcommands;
//!   * every invocation runs in the repository root (cwd isolation).
#![forbid(unsafe_code)]

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, Mutex};

/// One executed (or refused) tool invocation, as the tests see it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Invocation {
    /// Executable name (`git` / `svn`).
    pub program: String,
    /// Working directory the tool ran in (the repository root).
    pub working_tree: PathBuf,
    /// Arguments, without the executable.
    pub args: Vec<String>,
}

impl Invocation {
    /// The subcommand (`status`, `diff`, ...), skipping global options.
    #[must_use]
    pub fn subcommand(&self) -> String {
        subcommand_of(&self.program, &self.args)
    }
}

/// Index of the first non-option argument: the tool's subcommand.
///
/// git accepts global options BEFORE the subcommand (`-c k=v`, `--no-pager`,
/// `--git-dir <dir>`, `--work-tree <dir>`); the read-only gate and the test
/// assertions look past them. The working directory is NOT passed as an option
/// (see the module docs), so an option that takes a value and is not listed here
/// is refused — default deny rather than a guessed parse.
#[must_use]
pub fn subcommand_index(args: &[String]) -> Option<usize> {
    let mut index = 0;
    while index < args.len() {
        let arg = &args[index];
        if arg == "-c" || arg == "--git-dir" || arg == "--work-tree" {
            index += 2;
            continue;
        }
        if arg.starts_with('-') {
            index += 1;
            continue;
        }
        return Some(index);
    }
    None
}

/// The subcommand token, or the empty string when there is none.
#[must_use]
pub fn subcommand_of(_program: &str, args: &[String]) -> String {
    subcommand_index(args)
        .and_then(|index| args.get(index).cloned())
        .unwrap_or_default()
}

/// Read-only subcommands per backend: the ONLY ones that may be spawned.
pub const GIT_READ_ONLY: &[&str] = &[
    "status",
    "rev-parse",
    "diff",
    "log",
    "show",
    "ls-files",
    "blame",
    "describe",
    "cat-file",
    "for-each-ref",
    // `git remote` is BOTH a read (list remotes) and a write (`remote add`,
    // `remote rm`, `remote set-url`). It is allowed here only because
    // `remote_listing_only()` below refuses every form that is not a listing, so
    // the "reads cannot write" rule still holds by construction.
    "remote",
    // `git stash` is both a read (`stash list`) and a write (`stash save`, `pop`,
    // `apply`, `drop`); `git worktree` is both (`worktree list` vs `add` /
    // `remove`). Same rule as `remote`: only the LISTING form is read-allowed.
    "stash",
    "worktree",
];

/// `git remote` shares one subcommand token between a read and three writes, so
/// the read gate only lets the LISTING forms through: `git remote` and
/// `git remote -v` / `--verbose`. Everything else (`add`, `rm`, `remove`,
/// `set-url`, `rename`, `prune`, ...) is refused before any process exists.
fn remote_listing_only(args: &[String]) -> bool {
    let Some(index) = subcommand_index(args) else {
        return false;
    };
    if args.get(index).map(String::as_str) != Some("remote") {
        return true;
    }
    let rest = &args[index + 1..];
    rest.is_empty() || (rest.len() == 1 && (rest[0] == "-v" || rest[0] == "--verbose"))
}

/// `git stash` is a read only as `stash list` (optionally with log options);
/// `stash save` / `pop` / `apply` / `drop` / `clear` are writes and a bare
/// `git stash` is `stash save`, so the listing form is required explicitly.
fn stash_listing_only(args: &[String]) -> bool {
    listing_form(args, "stash")
}

/// `git worktree` is a read only as `worktree list`; `add` / `remove` / `prune`
/// are writes and a bare `git worktree` prints usage (not a listing).
fn worktree_listing_only(args: &[String]) -> bool {
    listing_form(args, "worktree")
}

/// Shared shape of the two narrowed read/write tokens: the token followed by
/// `list` and options only (no further positional argument can turn `list` into
/// another operation, and an option cannot be a subcommand because the token is
/// already consumed).
fn listing_form(args: &[String], token: &str) -> bool {
    let Some(index) = subcommand_index(args) else {
        return false;
    };
    if args.get(index).map(String::as_str) != Some(token) {
        return true;
    }
    let rest = &args[index + 1..];
    rest.first().map(String::as_str) == Some("list")
        && rest[1..].iter().all(|arg| arg.starts_with('-'))
}

/// Read-only Subversion subcommands.
pub const SVN_READ_ONLY: &[&str] = &[
    "status", "info", "diff", "log", "list", "cat", "propget", "proplist", "blame",
];

/// Write subcommands that must never run. Kept as data (not as "just don't call
/// it") so the refusal test can enumerate every one of them.
///
/// Two of these tokens (`stash`, `worktree`) are ALSO read tokens: the read gate
/// allows their listing form only (see `stash_listing_only` /
/// `worktree_listing_only`), so the bare token in this list — which is a WRITE
/// form (`git stash` == `stash save`, `git worktree` prints usage) — stays
/// refused before any process exists.
pub const GIT_WRITE: &[&str] = &[
    "commit",
    "push",
    "pull",
    "fetch",
    "checkout",
    "switch",
    "restore",
    "reset",
    "revert",
    "merge",
    "rebase",
    "cherry-pick",
    "stash",
    "apply",
    "am",
    "clean",
    "add",
    "rm",
    "mv",
    "init",
    "clone",
    "gc",
    "prune",
    "submodule",
    "tag",
    "branch-write",
    "worktree",
    "filter-branch",
    "bisect",
];

/// Write subcommands that must never run for Subversion.
pub const SVN_WRITE: &[&str] = &[
    "commit",
    "update",
    "up",
    "revert",
    "switch",
    "sw",
    "merge",
    "delete",
    "del",
    "remove",
    "rm",
    "add",
    "move",
    "mv",
    "rename",
    "mkdir",
    "copy",
    "cp",
    "propset",
    "propdel",
    "propedit",
    "import",
    "export",
    "cleanup",
    "upgrade",
    "lock",
    "unlock",
    "resolve",
    "relocate",
    "changelist",
];

fn read_only_table(program: &str) -> Option<&'static [&'static str]> {
    match program {
        "git" => Some(GIT_READ_ONLY),
        "svn" => Some(SVN_READ_ONLY),
        _ => None,
    }
}

/// `true` when `program` + the first argument is explicitly allowed.
///
/// Default-deny: an unknown tool, an empty argument list or an unlisted
/// subcommand is refused.
#[must_use]
pub fn is_allowed(program: &str, args: &[String]) -> bool {
    let Some(table) = read_only_table(program) else {
        return false;
    };
    let Some(index) = subcommand_index(args) else {
        return false;
    };
    let Some(subcommand) = args.get(index) else {
        return false;
    };
    if !table.contains(&subcommand.as_str()) {
        return false;
    }
    if program == "git" && subcommand == "remote" && !remote_listing_only(args) {
        return false;
    }
    if program == "git" && subcommand == "stash" && !stash_listing_only(args) {
        return false;
    }
    if program == "git" && subcommand == "worktree" && !worktree_listing_only(args) {
        return false;
    }
    true
}

/// The read-only gate. Returns the refusal reason for anything that is not on
/// the allow-list; the caller must not spawn the process in that case.
///
/// # Errors
///
/// Returns a human-readable reason naming the program and subcommand.
pub fn ensure_read_only(program: &str, args: &[String]) -> Result<(), String> {
    if read_only_table(program).is_none() {
        return Err(format!("unknown VCS tool '{program}': refused"));
    }
    let subcommand = subcommand_of(program, args);
    let subcommand = subcommand.as_str();
    if subcommand.is_empty() {
        return Err(format!("{program} without a subcommand: refused"));
    }
    if !is_allowed(program, args) {
        return Err(format!(
            "write operation '{program} {subcommand}' refused: the VCS backend is read-only"
        ));
    }
    Ok(())
}

/// Captured result of one tool invocation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandOutput {
    /// Exit status was zero.
    pub success: bool,
    /// `stdout` decoded lossily.
    pub stdout: String,
    /// `stderr` decoded lossily.
    pub stderr: String,
}

/// The seam the backends talk through.
pub trait CommandRunner {
    /// Runs one read-only invocation in `working_tree`.
    ///
    /// # Errors
    ///
    /// Returns a message when the gate refused the invocation or the process
    /// could not be started. A non-zero exit status is a normal
    /// [`CommandOutput`] with `success = false`.
    fn run(
        &self,
        program: &str,
        working_tree: &Path,
        args: &[String],
    ) -> Result<CommandOutput, String>;
}

/// Production runner: spawns the pinned tool in `working_tree`, after the
/// read-only gate.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemRunner;

impl CommandRunner for SystemRunner {
    fn run(
        &self,
        program: &str,
        working_tree: &Path,
        args: &[String],
    ) -> Result<CommandOutput, String> {
        ensure_read_only(program, args)?;
        // Resolve BEFORE spawning: bundled (AppDir) > PATH > env. The gate above
        // only ever sees the logical program name, so this cannot widen it.
        let resolved = crate::toolchain::resolve_or_error(program)?;
        let output = Command::new(&resolved.path)
            .current_dir(working_tree)
            .args(args)
            // The product runs with the AppDir's LD_LIBRARY_PATH; the bundled
            // git/svn wrapper starts with the HOST loader and then re-execs the
            // AppDir loader itself, so the bundle must not leak into the shell
            // that starts the wrapper (a 2.17 ld.so cannot load the bundled
            // glibc; measured on CentOS 7).
            .env_remove("LD_LIBRARY_PATH")
            .output()
            .map_err(|error| {
                format!(
                    "cannot run {program} ({}): {error}",
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

/// Stub runner for tests: records every invocation and answers from a fixed
/// table (so a whole session can run with no repository and no process).
///
/// Cloning shares the log, so a test can move the runner into a service and
/// still read what the session issued.
type RecordingAnswer = (String, String, bool, String, String);

#[derive(Debug, Default, Clone)]
pub struct RecordingRunner {
    invocations: Arc<Mutex<Vec<Invocation>>>,
    answers: Arc<Mutex<Vec<RecordingAnswer>>>,
}

impl RecordingRunner {
    /// A runner that answers every allowed invocation with empty success.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Registers one canned answer: (program, first argument, success, stdout, stderr).
    pub fn answer(
        &self,
        program: &str,
        subcommand: &str,
        success: bool,
        stdout: &str,
        stderr: &str,
    ) {
        let answer = (
            program.to_string(),
            subcommand.to_string(),
            success,
            stdout.to_string(),
            stderr.to_string(),
        );
        if let Ok(mut answers) = self.answers.lock() {
            answers.push(answer);
        }
    }

    /// Invocations that passed the read-only gate (i.e. would have spawned).
    #[must_use]
    pub fn invocations(&self) -> Vec<Invocation> {
        match self.invocations.lock() {
            Ok(invocations) => invocations.clone(),
            Err(_) => Vec::new(),
        }
    }

    /// Subcommands of every recorded invocation.
    #[must_use]
    pub fn subcommands(&self) -> Vec<String> {
        match self.invocations.lock() {
            Ok(invocations) => invocations.iter().map(Invocation::subcommand).collect(),
            Err(_) => Vec::new(),
        }
    }
}

impl CommandRunner for RecordingRunner {
    fn run(
        &self,
        program: &str,
        working_tree: &Path,
        args: &[String],
    ) -> Result<CommandOutput, String> {
        // The stub runs the SAME gate as production, so a test cannot "prove"
        // the read-only property with a runner that quietly allows everything.
        ensure_read_only(program, args)?;
        let invocation = Invocation {
            program: program.to_string(),
            working_tree: working_tree.to_path_buf(),
            args: args.to_vec(),
        };
        if let Ok(mut invocations) = self.invocations.lock() {
            invocations.push(invocation);
        }
        let subcommand = subcommand_of(program, args);
        let answers = match self.answers.lock() {
            Ok(answers) => answers.clone(),
            Err(_) => Vec::new(),
        };
        for (answer_program, answer_subcommand, success, stdout, stderr) in answers.iter() {
            if answer_program == program && answer_subcommand == &subcommand {
                return Ok(CommandOutput {
                    success: *success,
                    stdout: stdout.clone(),
                    stderr: stderr.clone(),
                });
            }
        }
        Ok(CommandOutput {
            success: true,
            stdout: String::new(),
            stderr: String::new(),
        })
    }
}
