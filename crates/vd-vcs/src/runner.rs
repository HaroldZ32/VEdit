//! Process seam for the VCS backends, plus the read-only guarantee.
//!
//! Every backend command goes through a [`CommandRunner`]. The only runner used
//! in production is [`SystemRunner`], and it refuses to spawn anything that is
//! not on the per-backend read-only allow-list: the guarantee "no write
//! operation is ever executed" is enforced BEFORE the process exists, not by the
//! caller's discipline.
//!
//! Tests inject a [`RecordingRunner`] (a stub) and assert two properties:
//!   * a write subcommand is refused and never reaches the spawn call;
//!   * a complete status + branch/revision + diff session issues only allowed
//!     read-only subcommands.
#![forbid(unsafe_code)]

use std::process::Command;
use std::sync::{Arc, Mutex};

/// One executed (or refused) tool invocation, as the tests see it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Invocation {
    /// Executable name (`git` / `svn`).
    pub program: String,
    /// Arguments, without the executable.
    pub args: Vec<String>,
}

impl Invocation {
    /// The subcommand (`status`, `diff`, ...), skipping global options such as
    /// git's `-C <path>`.
    #[must_use]
    pub fn subcommand(&self) -> String {
        subcommand_of(&self.program, &self.args)
    }
}

/// Index of the first non-option argument: the tool's subcommand.
///
/// git accepts global options BEFORE the subcommand (`-C <dir>`, `-c k=v`,
/// `--no-pager`, `--git-dir <dir>`, `--work-tree <dir>`); the read-only gate and
/// the test assertions must look past them, or `-C <path> status` would look
/// like an unknown subcommand `-C`.
#[must_use]
pub fn subcommand_index(args: &[String]) -> Option<usize> {
    let mut index = 0;
    while index < args.len() {
        let arg = &args[index];
        if arg == "-C" || arg == "-c" || arg == "--git-dir" || arg == "--work-tree" {
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
];

/// Read-only Subversion subcommands.
pub const SVN_READ_ONLY: &[&str] = &[
    "status", "info", "diff", "log", "list", "cat", "propget", "proplist", "blame",
];

/// Write subcommands that must never run. Kept as data (not as "just don't call
/// it") so the refusal test can enumerate every one of them.
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
    table.contains(&subcommand.as_str())
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
    /// Runs one read-only invocation.
    ///
    /// # Errors
    ///
    /// Returns a message when the process could not be started. A non-zero exit
    /// status is a normal [`CommandOutput`] with `success = false`.
    fn run(&self, program: &str, args: &[String]) -> Result<CommandOutput, String>;
}

/// Production runner: spawns the pinned tool, after the read-only gate.
#[derive(Debug, Default, Clone, Copy)]
pub struct SystemRunner;

impl CommandRunner for SystemRunner {
    fn run(&self, program: &str, args: &[String]) -> Result<CommandOutput, String> {
        ensure_read_only(program, args)?;
        let output = Command::new(program)
            .args(args)
            .output()
            .map_err(|error| format!("cannot run {program}: {error}"))?;
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
    fn run(&self, program: &str, args: &[String]) -> Result<CommandOutput, String> {
        // The stub runs the SAME gate as production, so a test cannot "prove"
        // the read-only property with a runner that quietly allows everything.
        ensure_read_only(program, args)?;
        let invocation = Invocation {
            program: program.to_string(),
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
