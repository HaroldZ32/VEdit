//! Injected environment and XDG base directories.
//!
//! Nothing in this crate reads the process environment directly: [`Loader`](crate::Loader)
//! takes an [`Env`], which keeps every resolution purely logical and lets tests
//! run in parallel without mutating process state.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

/// A snapshot of the environment variables the config layer is allowed to read.
#[derive(Debug, Clone, Default)]
pub struct Env {
    vars: BTreeMap<String, String>,
}

impl Env {
    /// Snapshot the real process environment.
    ///
    /// Non-Unicode entries are skipped rather than panicking.
    pub fn from_process() -> Self {
        Self {
            vars: std::env::vars_os()
                .filter_map(|(key, value)| {
                    let key = key.into_string().ok()?;
                    let value = value.into_string().ok()?;
                    Some((key, value))
                })
                .collect(),
        }
    }

    /// Build an environment from explicit pairs (tests, embedding).
    pub fn from_pairs<I, K, V>(pairs: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
        K: Into<String>,
        V: Into<String>,
    {
        Self {
            vars: pairs
                .into_iter()
                .map(|(key, value)| (key.into(), value.into()))
                .collect(),
        }
    }

    /// The value of `name`, if set (an empty value is still "set").
    pub fn get(&self, name: &str) -> Option<&str> {
        self.vars.get(name).map(String::as_str)
    }

    /// Every pair, in name order. This is what a child environment or the
    /// filelist variable table is built from.
    #[must_use]
    pub fn to_pairs(&self) -> Vec<(String, String)> {
        self.vars
            .iter()
            .map(|(name, value)| (name.clone(), value.clone()))
            .collect()
    }

    /// Iterate over the pairs without cloning them.
    pub fn iter(&self) -> impl Iterator<Item = (&str, &str)> {
        self.vars
            .iter()
            .map(|(name, value)| (name.as_str(), value.as_str()))
    }

    /// `$HOME`, when usable.
    pub fn home(&self) -> Option<PathBuf> {
        self.get("HOME")
            .map(str::trim)
            .filter(|home| !home.is_empty())
            .map(PathBuf::from)
    }
}

/// XDG base directories, resolved per the XDG Base Directory Specification.
///
/// A variable is used only when it is an absolute path; unset, empty or
/// relative values fall back to the specification's defaults. The loader emits
/// `xdg.invalid` warnings for the relative case so it stays observable.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct XdgDirs {
    /// `$XDG_CONFIG_HOME` or `$HOME/.config`.
    pub config_home: Option<PathBuf>,
    /// `$XDG_DATA_HOME` or `$HOME/.local/share`.
    pub data_home: Option<PathBuf>,
    /// `$XDG_CACHE_HOME` or `$HOME/.cache`.
    pub cache_home: Option<PathBuf>,
}

impl XdgDirs {
    /// Resolve the three directories from an injected environment.
    pub fn from_env(env: &Env) -> Self {
        let home = env.home();
        Self {
            config_home: xdg_dir(
                env,
                "XDG_CONFIG_HOME",
                home.as_deref().map(|h| h.join(".config")),
            ),
            data_home: xdg_dir(
                env,
                "XDG_DATA_HOME",
                home.as_deref().map(|h| h.join(".local/share")),
            ),
            cache_home: xdg_dir(
                env,
                "XDG_CACHE_HOME",
                home.as_deref().map(|h| h.join(".cache")),
            ),
        }
    }

    /// The user-level configuration file: `$XDG_CONFIG_HOME/vedit/vedit.toml`.
    pub fn config_file(&self) -> Option<PathBuf> {
        self.config_home
            .as_ref()
            .map(|home| home.join("vedit").join("vedit.toml"))
    }

    /// The three XDG variable names, for diagnostics.
    pub fn variable_names() -> [&'static str; 3] {
        ["XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"]
    }

    /// True when `name`'s raw value is present but not an absolute path.
    pub fn invalid_override<'a>(env: &'a Env, name: &str) -> Option<&'a str> {
        let raw = env.get(name)?;
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            return None;
        }
        (!Path::new(trimmed).is_absolute()).then_some(trimmed)
    }
}

fn xdg_dir(env: &Env, name: &str, fallback: Option<PathBuf>) -> Option<PathBuf> {
    let Some(raw) = env.get(name) else {
        return fallback;
    };
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return fallback;
    }
    let path = PathBuf::from(trimmed);
    if path.is_absolute() {
        Some(path)
    } else {
        fallback
    }
}

// --- batch 20261110: shell environment import (AC-4.1/4.2/4.5/4.6/4.7/4.8) ----
//
// One explicit initialisation chain per shell, then ONE read of the environment
// it ended up with. The capture is a one-shot child process: it never leaves a
// shell resident, and every exit path kills and reaps the child.
//
// Why a shell at all: the point is the environment the USER's shell computes
// (rc files, aliases exported as variables, `export PATH=…` chains). Reading
// `std::env` would only see what launched the GUI, which on a desktop is
// usually nothing like the user's terminal.

use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

/// The shell family the capture initialises.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ShellKind {
    /// `bash`
    Bash,
    /// `zsh`
    Zsh,
    /// `csh`
    Csh,
    /// `tcsh`
    Tcsh,
    /// `fish`
    Fish,
    /// `sh` (POSIX)
    Sh,
    /// `ksh`
    Ksh,
    /// The caller supplies the program and the complete argv.
    Custom,
}

impl ShellKind {
    /// Decode the C ABI's `VD_ENV_SHELL_*` value (0..=7).
    #[must_use]
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Bash),
            1 => Some(Self::Zsh),
            2 => Some(Self::Csh),
            3 => Some(Self::Tcsh),
            4 => Some(Self::Fish),
            5 => Some(Self::Sh),
            6 => Some(Self::Ksh),
            7 => Some(Self::Custom),
            _ => None,
        }
    }

    /// The default program name, or `None` for [`ShellKind::Custom`].
    #[must_use]
    pub const fn program(self) -> Option<&'static str> {
        match self {
            Self::Bash => Some("bash"),
            Self::Zsh => Some("zsh"),
            Self::Csh => Some("csh"),
            Self::Tcsh => Some("tcsh"),
            Self::Fish => Some("fish"),
            Self::Sh => Some("sh"),
            Self::Ksh => Some("ksh"),
            Self::Custom => None,
        }
    }

    /// Detect the shell family from a program name or an absolute path.
    ///
    /// The basename is matched, so `/bin/tcsh`, `tcsh` and the login spelling
    /// `-tcsh` all resolve. An unknown program yields `None` on purpose: guessing
    /// a family would apply that family's quoting rules to a shell that does not
    /// share them, and the caller reports one diagnostic instead.
    #[must_use]
    pub fn detect(program: &str) -> Option<Self> {
        let name = program.rsplit('/').next().unwrap_or(program);
        let name = name.strip_prefix('-').unwrap_or(name);
        match name {
            "bash" => Some(Self::Bash),
            "zsh" => Some(Self::Zsh),
            "csh" => Some(Self::Csh),
            "tcsh" => Some(Self::Tcsh),
            "fish" => Some(Self::Fish),
            "sh" | "dash" => Some(Self::Sh),
            "ksh" | "ksh93" | "mksh" => Some(Self::Ksh),
            _ => None,
        }
    }

    /// How this shell is initialised, in one line, for the settings page and the
    /// evidence (AC-4.1: "login and interactive behaviour documented per shell").
    #[must_use]
    pub const fn documented_behaviour(self) -> &'static str {
        match self {
            Self::Bash | Self::Zsh | Self::Sh | Self::Ksh => {
                "POSIX shell: `set -a` before sourcing so the rc file's assignments are \
                 exported, then `env -0`. The source status is saved while `set -a` is \
                 active and relayed through shell variables AFTER `set +a`, so no \
                 capture-internal `_vd_` variable reaches the captured block while the \
                 exit status is still the source status. `login` adds `-l`, `interactive` \
                 adds `-i`; `-i` without a TTY prints \"no job control\" on stderr but \
                 still runs."
            }
            Self::Fish => {
                "fish: `source <file>; env -0`. fish has no `set -a`; only variables the rc \
                 file exports (`set -gx`) appear, and the saved status is a shell \
                 variable, not an exported one. `login` adds `-l`, `interactive` adds `-i`."
            }
            Self::Csh | Self::Tcsh => {
                "csh family: `source <file>; set _vd_rc=$status; env -0; exit $_vd_rc`. \
                 `env -0` is coreutils, not a shell feature, so the block is NUL-separated like \
                 the POSIX family and a value that contains a newline survives. The source \
                 status is saved BEFORE `env` runs and reported as the exit status; `set` \
                 makes a shell variable, not an environment variable, so the status never \
                 appears in the captured block. `login` adds `-l`."
            }
            Self::Custom => {
                "custom: the caller's complete argv, run as given; its stdout must be a \
                 NUL-separated `NAME=VALUE` block (a newline-separated block is accepted as a \
                 fallback with a warning)."
            }
        }
    }
}

/// One capture request: the shell, what to source, and the budget.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ShellInit {
    /// The shell family.
    pub shell: ShellKind,
    /// Explicit program (absolute or a `PATH` name); `None` uses the family default.
    pub program: Option<PathBuf>,
    /// The rc file to source before the environment is read.
    pub source_file: Option<PathBuf>,
    /// Working directory of the capture.
    pub workdir: Option<PathBuf>,
    /// Extra argv entries appended after the shell's own flags.
    pub extra_argv: Vec<String>,
    /// Start a login shell.
    pub login: bool,
    /// Start an interactive shell.
    pub interactive: bool,
    /// Wall-clock budget.
    pub timeout: Duration,
}

impl Default for ShellInit {
    fn default() -> Self {
        Self {
            shell: ShellKind::Bash,
            program: None,
            source_file: None,
            workdir: None,
            extra_argv: Vec::new(),
            login: false,
            interactive: false,
            timeout: Duration::from_millis(DEFAULT_CAPTURE_TIMEOUT_MS),
        }
    }
}

/// Default wall-clock budget of one capture.
pub const DEFAULT_CAPTURE_TIMEOUT_MS: u64 = 15_000;
/// Default cap on the retained stderr (the TAIL is kept: a shell reports errors
/// at the end).
pub const DEFAULT_STDERR_CAP: usize = 8 * 1024;
/// Cap on the captured environment block itself (a hostile rc file could dump
/// unbounded output; the environment of a desktop session is far below this).
pub const MAX_CAPTURE_BYTES: usize = 8 * 1024 * 1024;

/// Why a capture did not produce an environment.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CaptureError {
    /// The request is not usable (no program for `Custom`, an empty program, …).
    Invalid(String),
    /// The child could not be started (missing binary, permissions…).
    Spawn {
        /// The program that was tried.
        program: String,
        /// The operating system's reason.
        reason: String,
    },
    /// The child exceeded its wall-clock budget and was killed.
    TimedOut {
        /// How long it ran.
        elapsed: Duration,
    },
    /// The caller cancelled the capture; the child was killed.
    Cancelled,
    /// Waiting for the child failed after it started.
    Io(String),
    /// The child finished but its output could not be read as an environment.
    NotAnEnvironment(String),
}

impl std::fmt::Display for CaptureError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Invalid(reason) => write!(f, "{reason}"),
            Self::Spawn { program, reason } => {
                write!(f, "cannot start {program}: {reason}")
            }
            Self::TimedOut { elapsed } => write!(
                f,
                "the shell did not finish within {} ms and was killed",
                elapsed.as_millis()
            ),
            Self::Cancelled => f.write_str("the environment capture was cancelled"),
            Self::Io(reason) => f.write_str(reason),
            Self::NotAnEnvironment(reason) => f.write_str(reason),
        }
    }
}

impl std::error::Error for CaptureError {}

/// What one capture produced.
///
/// `Debug` is REDACTING (AC-4.8): the entry list holds the user's real values
/// (they are needed for the merge), but a secret-named value must never reach a
/// log line through `{:?}`. The raw bytes are never printed either.
#[derive(Clone, PartialEq, Eq)]
pub struct CaptureOutcome {
    /// The environment, in the order the shell printed it (duplicates kept: a
    /// later `export` of the same name is what the shell ended up with).
    pub entries: Vec<(String, String)>,
    /// The same environment as one NUL-separated block (`NAME=VALUE\0`… plus one
    /// final `\0`), exactly the bytes of the capture (AC-4.2).
    pub raw: Vec<u8>,
    /// The program that ran.
    pub program: String,
    /// The child's exit code (`-1` when a signal ended it).
    pub exit_code: i32,
    /// The TAIL of stderr, truncated to the cap (`None` when empty).
    pub stderr: Option<String>,
    /// Non-fatal problems (a line without `=`, a newline split in the fallback, …).
    pub warnings: Vec<String>,
}

impl CaptureOutcome {
    /// The captured environment as an [`Env`] (last value wins, like a shell).
    #[must_use]
    pub fn environment(&self) -> Env {
        Env::from_pairs(self.entries.clone())
    }
}

impl std::fmt::Debug for CaptureOutcome {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let entries: Vec<(String, String)> = self
            .entries
            .iter()
            .map(|(name, value)| {
                (
                    name.clone(),
                    if crate::secret::is_secret_name(name) {
                        crate::secret::MASKED_VALUE.to_string()
                    } else {
                        value.clone()
                    },
                )
            })
            .collect();
        f.debug_struct("CaptureOutcome")
            .field("entries", &entries)
            .field("raw_bytes", &self.raw.len())
            .field("program", &self.program)
            .field("exit_code", &self.exit_code)
            .field("stderr_bytes", &self.stderr.as_ref().map(String::len))
            .field("warnings", &self.warnings)
            .finish()
    }
}

/// The complete argv of the capture: `argv[0]` is the program.
///
/// # Errors
/// Returns [`CaptureError::Invalid`] when the request names no usable program.
pub fn capture_argv(init: &ShellInit) -> Result<Vec<std::ffi::OsString>, CaptureError> {
    use std::ffi::OsString;
    let program: PathBuf = match (&init.program, init.shell.program()) {
        (Some(program), _) if program.as_os_str().is_empty() => {
            return Err(CaptureError::Invalid(
                "the capture program must not be empty".to_string(),
            ));
        }
        (Some(program), _) => program.clone(),
        (None, Some(name)) => PathBuf::from(name),
        (None, None) => {
            return Err(CaptureError::Invalid(
                "a custom capture needs an explicit program".to_string(),
            ));
        }
    };
    let mut argv: Vec<OsString> = vec![program.into_os_string()];
    if init.shell == ShellKind::Custom {
        argv.extend(init.extra_argv.iter().map(OsString::from));
        return Ok(argv);
    }
    let mut push = |text: &str| argv.push(OsString::from(text));
    if init.login {
        push("-l");
    }
    if init.interactive {
        push("-i");
    }
    for extra in &init.extra_argv {
        push(extra);
    }
    match init.shell {
        ShellKind::Csh | ShellKind::Tcsh => {
            let mut script = String::new();
            if let Some(file) = &init.source_file {
                // The environment is emitted AND the capture's exit status is the
                // SOURCE status, so a broken rc file is observable (AC-4.5).
                // `env -0` is coreutils, not a shell feature: the csh family prints
                // the same NUL-separated block as the POSIX family, so a value that
                // contains a newline survives (A51). `$status` is captured BEFORE
                // `env` runs, so env's own status cannot replace the source's.
                script.push_str(&format!(
                    "source {}; set _vd_rc=$status; env -0; exit $_vd_rc",
                    csh_quote(file)
                ));
            } else {
                script.push_str("env -0");
            }
            push("-c");
            push(&script);
        }
        ShellKind::Fish => {
            let mut script = String::new();
            if let Some(file) = &init.source_file {
                script.push_str(&format!(
                    "source {}; set _vd_rc $status; env -0; exit $_vd_rc",
                    shell_quote(file)
                ));
            } else {
                script.push_str("env -0");
            }
            push("-c");
            push(&script);
        }
        _ => {
            let mut script = String::new();
            if let Some(file) = &init.source_file {
                // `set -a` makes the rc file's assignments exported; the source
                // status is returned AFTER the environment is emitted, so a broken
                // rc file is both diagnosable and non-destructive. The status is
                // first saved as `_vd_st` while `set -a` is ACTIVE (so that copy is
                // exported), then `set +a` ends the auto-export window and the value
                // moves to `_vd_rc` — an assignment made after `set +a` is NOT
                // exported — before `_vd_st` is unset. Saving straight into
                // `_vd_rc` under `set -a` exported a capture-internal variable into
                // every result (E4b); moving `set +a` before the save would make
                // `$?` observe `set`'s own status and lose the source status, and
                // unsetting `_vd_rc` just before `exit` would lose the status
                // entirely. `unset _vd_rc` first also drops an inherited export of
                // the reserved name, so no `_vd_` bookkeeping can reach the block.
                script.push_str(&format!(
                    "set -a; . {}; _vd_st=$?; set +a; unset _vd_rc; _vd_rc=$_vd_st; unset _vd_st; env -0; exit $_vd_rc",
                    shell_quote(file)
                ));
            } else {
                script.push_str("env -0");
            }
            push("-c");
            push(&script);
        }
    }
    Ok(argv)
}

/// Quote one path for a POSIX/fish script: single quotes, with `'` escaped.
fn shell_quote(path: &Path) -> String {
    let text = path.to_string_lossy();
    let mut quoted = String::with_capacity(text.len() + 2);
    quoted.push('\'');
    for character in text.chars() {
        if character == '\'' {
            quoted.push_str("'\\''");
        } else {
            quoted.push(character);
        }
    }
    quoted.push('\'');
    quoted
}

/// Quote one path for a csh/tcsh script.
///
/// The csh family needs one rule the POSIX form does not have: history
/// substitution runs BEFORE quoting, so a `!` inside single quotes is still
/// expanded (`source '/tmp/bang!x'` fails with "Event not found"). `\!` is literal
/// `!` even inside single quotes, so a bang is escaped there; the apostrophe rule
/// stays the POSIX one (`'\''`), which csh accepts — measured on csh/tcsh 6.24 for
/// apostrophes, bangs, backslashes, `$`, backticks and spaces together.
///
/// A newline cannot be represented inside a csh word at all; such a path makes
/// the shell reject its own script, which the capture reports through the exit
/// status and the stderr tail (never a panic, never a silently wrong block).
fn csh_quote(path: &Path) -> String {
    let text = path.to_string_lossy();
    let mut quoted = String::with_capacity(text.len() + 2);
    quoted.push('\'');
    for character in text.chars() {
        match character {
            '\'' => quoted.push_str("'\\''"),
            '!' => quoted.push_str("\\!"),
            _ => quoted.push(character),
        }
    }
    quoted.push('\'');
    quoted
}

/// Run one capture.
///
/// The child inherits this process's environment (that is the whole point: the
/// result is the environment the shell ends up with). Every exit path kills and
/// reaps the child, so no shell is left resident (AC-4.1).
///
/// # Errors
/// Returns a [`CaptureError`] describing the failure; a non-zero exit code is
/// NOT an error — it is reported in the outcome together with the stderr tail.
pub fn capture(
    init: &ShellInit,
    cancel: Option<&AtomicBool>,
) -> Result<CaptureOutcome, CaptureError> {
    capture_with(init, cancel, DEFAULT_STDERR_CAP)
}

/// [`capture`] with an explicit stderr cap (the ABI passes its own).
///
/// # Errors
/// Same as [`capture`].
pub fn capture_with(
    init: &ShellInit,
    cancel: Option<&AtomicBool>,
    stderr_cap: usize,
) -> Result<CaptureOutcome, CaptureError> {
    use std::process::{Command, Stdio};
    let argv = capture_argv(init)?;
    let program = argv
        .first()
        .map(|program| program.to_string_lossy().to_string())
        .unwrap_or_default();
    let mut command = Command::new(&argv[0]);
    command.args(&argv[1..]);
    command.stdin(Stdio::null());
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());
    // Own process group, so a timeout/cancel kills the whole chain the shell
    // started (an `sh -c 'sleep 30'` leaves `sleep` holding the stdout pipe open;
    // killing only the shell would leave the reader blocked for 30 seconds).
    configure_process_group(&mut command);
    if let Some(workdir) = &init.workdir {
        command.current_dir(workdir);
    }
    let started = Instant::now();
    let child = command.spawn().map_err(|error| CaptureError::Spawn {
        program: program.clone(),
        reason: error.to_string(),
    })?;
    let pid = child.id();
    let (sender, receiver) = std::sync::mpsc::channel();
    let worker = std::thread::Builder::new()
        .name("vd-env-capture".to_string())
        .spawn(move || {
            let outcome = child.wait_with_output();
            let _ = sender.send(outcome);
        })
        .map_err(|error| CaptureError::Io(format!("cannot start the capture reader: {error}")))?;

    let mut timed_out = false;
    let mut cancelled = false;
    let output = loop {
        if cancel.is_some_and(|flag| flag.load(Ordering::SeqCst)) {
            cancelled = true;
            terminate(pid);
            break receiver.recv_timeout(Duration::from_secs(5));
        }
        match receiver.recv_timeout(Duration::from_millis(25)) {
            Ok(output) => break Ok(output),
            Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => {
                break Ok(Err(std::io::Error::other(
                    "the capture reader stopped without a result",
                )))
            }
            Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {
                if started.elapsed() >= init.timeout {
                    timed_out = true;
                    terminate(pid);
                    break receiver.recv_timeout(Duration::from_secs(5));
                }
            }
        }
    };
    let _ = worker.join();
    if cancelled {
        return Err(CaptureError::Cancelled);
    }
    if timed_out {
        return Err(CaptureError::TimedOut {
            elapsed: started.elapsed(),
        });
    }
    let output = match output {
        Ok(Ok(output)) => output,
        Ok(Err(error)) => return Err(CaptureError::Io(format!("the shell failed: {error}"))),
        Err(_) => {
            return Err(CaptureError::Io(
                "the shell did not report its exit status after being terminated".to_string(),
            ))
        }
    };
    parse_output(output, &program, stderr_cap)
}

/// Put the capture child in its own process group (POSIX) so that cancelling or
/// timing out terminates everything it started.
#[cfg(unix)]
fn configure_process_group(command: &mut std::process::Command) {
    use std::os::unix::process::CommandExt;
    command.process_group(0);
}

#[cfg(not(unix))]
fn configure_process_group(_command: &mut std::process::Command) {}

/// Kill one capture child and everything in its process group, without a `libc`
/// dependency.
///
/// The product targets POSIX (the build image is CentOS 7); `kill` is part of
/// POSIX and is the only dependency-free way to signal a `Child` that a worker
/// thread is currently waiting on. The `--` keeps `kill` from reading the
/// negative group id as a signal name; the plain pid is signalled as well so a
/// composition where the group was not applied still stops the child.
fn terminate(pid: u32) {
    #[cfg(unix)]
    {
        let group = std::process::Command::new("kill")
            .args(["-9", "--", &format!("-{pid}")])
            .stderr(std::process::Stdio::null())
            .status();
        if group.map(|status| status.success()).unwrap_or(false) {
            return;
        }
    }
    let _ = std::process::Command::new("kill")
        .args(["-9", &pid.to_string()])
        .stderr(std::process::Stdio::null())
        .status();
}

fn parse_output(
    output: std::process::Output,
    program: &str,
    stderr_cap: usize,
) -> Result<CaptureOutcome, CaptureError> {
    let mut warnings = Vec::new();
    let (entries, raw) = parse_capture_block(&output.stdout, &mut warnings)?;
    let exit_code = output.status.code().unwrap_or(-1);
    let stderr = tail(&output.stderr, stderr_cap);
    Ok(CaptureOutcome {
        entries,
        raw,
        program: program.to_string(),
        exit_code,
        stderr,
        warnings,
    })
}

/// The entries of one capture and the NUL-separated blob it came from.
pub type ParsedCapture = (Vec<(String, String)>, Vec<u8>);

/// The last `cap` bytes of `bytes`, as text (invalid UTF-8 is replaced).
fn tail(bytes: &[u8], cap: usize) -> Option<String> {
    if bytes.is_empty() {
        return None;
    }
    let start = bytes.len().saturating_sub(cap);
    Some(String::from_utf8_lossy(&bytes[start..]).into_owned())
}

/// Parse a captured environment block.
///
/// The NUL-separated form is authoritative (values may contain spaces,
/// newlines and `=`); a newline-separated block from a caller-supplied custom
/// program is accepted as a fallback with a warning. Every shell family in this
/// crate asks for the NUL form (`env -0`, which is coreutils rather than a shell
/// feature), so the fallback is no longer the csh family's normal path.
///
/// # Errors
/// Returns [`CaptureError::NotAnEnvironment`] when the block is empty or larger
/// than [`MAX_CAPTURE_BYTES`], or when it contains no `NAME=VALUE` entry at all.
pub fn parse_capture_block(
    bytes: &[u8],
    warnings: &mut Vec<String>,
) -> Result<ParsedCapture, CaptureError> {
    if bytes.len() > MAX_CAPTURE_BYTES {
        return Err(CaptureError::NotAnEnvironment(format!(
            "the capture produced {} bytes, more than the {MAX_CAPTURE_BYTES}-byte cap",
            bytes.len()
        )));
    }
    let nul_separated = bytes.contains(&0);
    let mut entries = Vec::new();
    let mut raw: Vec<u8> = Vec::with_capacity(bytes.len() + 1);
    if nul_separated {
        for segment in bytes.split(|byte| *byte == 0) {
            if segment.is_empty() {
                continue;
            }
            if let Some(entry) = entry_from_segment(segment, warnings) {
                raw.extend_from_slice(entry.0.as_bytes());
                raw.push(b'=');
                raw.extend_from_slice(entry.1.as_bytes());
                raw.push(0);
                entries.push(entry);
            }
        }
        raw.push(0);
    } else {
        warnings.push(
            "the capture printed a newline-separated environment (no NUL form): a value \
             that contains a newline cannot be represented and was split"
                .to_string(),
        );
        for line in bytes.split(|byte| *byte == b'\n') {
            let line = line.strip_suffix(b"\r").unwrap_or(line);
            if line.is_empty() {
                continue;
            }
            if let Some(entry) = entry_from_segment(line, warnings) {
                raw.extend_from_slice(entry.0.as_bytes());
                raw.push(b'=');
                raw.extend_from_slice(entry.1.as_bytes());
                raw.push(0);
                entries.push(entry);
            }
        }
        raw.push(0);
    }
    if entries.is_empty() {
        return Err(CaptureError::NotAnEnvironment(
            "the capture produced no NAME=VALUE entry".to_string(),
        ));
    }
    Ok((entries, raw))
}

/// One `NAME=VALUE` pair; `None` when the segment is not one.
fn entry_from_segment(segment: &[u8], warnings: &mut Vec<String>) -> Option<(String, String)> {
    let (name, value) = match segment.iter().position(|byte| *byte == b'=') {
        Some(index) => (&segment[..index], &segment[index + 1..]),
        None => {
            warnings.push(format!(
                "ignored a capture entry without `=` ({} bytes)",
                segment.len()
            ));
            return None;
        }
    };
    let name = match std::str::from_utf8(name) {
        Ok(name) if !name.is_empty() && !name.chars().any(char::is_control) => name,
        _ => {
            warnings.push("ignored a capture entry whose name is not valid UTF-8".to_string());
            return None;
        }
    };
    let value = String::from_utf8_lossy(value).into_owned();
    Some((name.to_string(), value))
}

/// What an import changed, relative to the environment the product already has.
///
/// `Debug` is REDACTING, for the same reason as [`CaptureOutcome`]: this type is
/// what the settings page and the logs would print.
#[derive(Clone, PartialEq, Eq)]
pub struct EnvChange {
    /// The variable name.
    pub name: String,
    /// The value the process already had (`None` when the name is new).
    pub before: Option<String>,
    /// The value the shell computed.
    pub after: String,
}

impl std::fmt::Debug for EnvChange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("EnvChange")
            .field("name", &self.name)
            .field("before", &self.masked_before())
            .field("after", &self.display_value())
            .finish()
    }
}

impl EnvChange {
    /// True when the name matches the masking policy (`*TOKEN*`, `*SECRET*`, …).
    #[must_use]
    pub fn is_masked(&self) -> bool {
        crate::secret::is_secret_name(&self.name)
    }

    /// The `before` value as it may be SHOWN (`None` stays `None`).
    #[must_use]
    pub fn masked_before(&self) -> Option<&str> {
        self.before.as_deref().map(|value| {
            if self.is_masked() {
                crate::secret::MASKED_VALUE
            } else {
                value
            }
        })
    }

    /// The value safe to show in the UI/log (the masked placeholder for a secret
    /// name).
    #[must_use]
    pub fn display_value(&self) -> &str {
        if self.is_masked() {
            crate::secret::MASKED_VALUE
        } else {
            &self.after
        }
    }
}

/// The entries an import would ADD or CHANGE (AC-4.6: only added/changed are
/// exposed; an unchanged variable is not the user's business).
#[must_use]
pub fn diff(current: &Env, captured: &[(String, String)]) -> Vec<EnvChange> {
    let mut changes: Vec<EnvChange> = Vec::new();
    for (name, value) in captured {
        let before = current.get(name);
        let changed = match before {
            Some(existing) => existing != value,
            None => true,
        };
        if !changed {
            continue;
        }
        // A later duplicate of the same name is the value the shell ended with.
        if let Some(previous) = changes.iter_mut().find(|change| &change.name == name) {
            previous.after = value.clone();
            continue;
        }
        changes.push(EnvChange {
            name: name.clone(),
            before: before.map(str::to_string),
            after: value.clone(),
        });
    }
    changes
}

/// One layer of imported variables (already approved by the user).
///
/// `Debug` is REDACTING (the store itself keeps the values: the user's own file,
/// which a child needs); a log line never sees a secret-named value.
#[derive(Clone, Default, PartialEq, Eq)]
pub struct EnvironmentSelection {
    /// Variables selected for every project.
    pub global: Vec<(String, String)>,
    /// Variables selected per project root (absolute path as written).
    pub projects: std::collections::BTreeMap<String, Vec<(String, String)>>,
}

impl std::fmt::Debug for EnvironmentSelection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let masked = |entries: &[(String, String)]| -> Vec<(String, String)> {
            entries
                .iter()
                .map(|(name, value)| {
                    (
                        name.clone(),
                        if crate::secret::is_secret_name(name) {
                            crate::secret::MASKED_VALUE.to_string()
                        } else {
                            value.clone()
                        },
                    )
                })
                .collect()
        };
        let projects: std::collections::BTreeMap<&String, Vec<(String, String)>> = self
            .projects
            .iter()
            .map(|(root, entries)| (root, masked(entries)))
            .collect();
        f.debug_struct("EnvironmentSelection")
            .field("global", &masked(&self.global))
            .field("projects", &projects)
            .finish()
    }
}

impl EnvironmentSelection {
    /// The merged layer for one project: global first, then the project's own
    /// entries override them (AC-4.7 project wins over global).
    #[must_use]
    pub fn merged_for(&self, project_root: Option<&Path>) -> Vec<(String, String)> {
        let mut merged: Vec<(String, String)> = Vec::new();
        let mut insert = |entries: &[(String, String)]| {
            for (name, value) in entries {
                match merged.iter_mut().find(|(existing, _)| existing == name) {
                    Some(slot) => slot.1 = value.clone(),
                    None => merged.push((name.clone(), value.clone())),
                }
            }
        };
        insert(&self.global);
        if let Some(root) = project_root {
            if let Some(entries) = self.projects.get(&root.to_string_lossy().to_string()) {
                insert(entries);
            }
        }
        merged
    }

    /// Process < global < project: the order every child and the filelist table
    /// must see (AC-4.7).
    #[must_use]
    pub fn merged_environment(&self, process: &Env, project_root: Option<&Path>) -> Env {
        let mut pairs: Vec<(String, String)> = process.to_pairs();
        for (name, value) in self.merged_for(project_root) {
            match pairs.iter_mut().find(|(existing, _)| *existing == name) {
                Some(slot) => slot.1 = value,
                None => pairs.push((name, value)),
            }
        }
        Env::from_pairs(pairs)
    }

    /// Serialise the selection to the documented JSON form (values are stored as
    /// the user selected them; a secret name is never written to a LOG, but the
    /// store is the user's own file — the UI masks before display, not before
    /// storage, because the value is what a child needs).
    #[must_use]
    pub fn to_json(&self) -> String {
        let global: Vec<serde_json::Value> = self
            .global
            .iter()
            .map(|(name, value)| serde_json::json!({"name": name, "value": value}))
            .collect();
        let projects: Vec<serde_json::Value> = self
            .projects
            .iter()
            .map(|(root, entries)| {
                let rows: Vec<serde_json::Value> = entries
                    .iter()
                    .map(|(name, value)| serde_json::json!({"name": name, "value": value}))
                    .collect();
                serde_json::json!({"root": root, "entries": rows})
            })
            .collect();
        serde_json::json!({"version": 1, "global": global, "projects": projects}).to_string()
    }

    /// Read the selection back. Never fails: a broken file is an empty selection
    /// plus warnings (the same rule the config loader follows).
    #[must_use]
    pub fn from_json(text: &str) -> (Self, Vec<String>) {
        let mut warnings = Vec::new();
        let mut selection = Self::default();
        let value: serde_json::Value = match serde_json::from_str(text) {
            Ok(value) => value,
            Err(error) => {
                warnings.push(format!(
                    "the environment selection is not valid JSON: {error}"
                ));
                return (selection, warnings);
            }
        };
        let rows = |value: Option<&serde_json::Value>| -> Vec<(String, String)> {
            value
                .and_then(serde_json::Value::as_array)
                .map(|rows| {
                    rows.iter()
                        .filter_map(|row| {
                            let name = row.get("name").and_then(serde_json::Value::as_str)?;
                            let value = row.get("value").and_then(serde_json::Value::as_str)?;
                            Some((name.to_string(), value.to_string()))
                        })
                        .collect()
                })
                .unwrap_or_default()
        };
        selection.global = rows(value.get("global"));
        if let Some(projects) = value.get("projects").and_then(serde_json::Value::as_array) {
            for project in projects {
                let Some(root) = project.get("root").and_then(serde_json::Value::as_str) else {
                    warnings.push("a project entry without a root was ignored".to_string());
                    continue;
                };
                selection
                    .projects
                    .insert(root.to_string(), rows(project.get("entries")));
            }
        }
        (selection, warnings)
    }

    /// Load the selection from `path`; a missing file is an empty selection.
    #[must_use]
    pub fn load(path: &Path) -> (Self, Vec<String>) {
        match std::fs::read_to_string(path) {
            Ok(text) => Self::from_json(&text),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                (Self::default(), Vec::new())
            }
            Err(error) => (
                Self::default(),
                vec![format!("cannot read {}: {error}", path.display())],
            ),
        }
    }

    /// Write the selection atomically (sibling temporary file + rename).
    ///
    /// # Errors
    /// Returns the operating system's reason when the file cannot be written.
    pub fn save(&self, path: &Path) -> Result<(), String> {
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)
                .map_err(|error| format!("cannot create {}: {error}", parent.display()))?;
        }
        let name = path
            .file_name()
            .map(|name| name.to_string_lossy().to_string())
            .unwrap_or_else(|| "environment.json".to_string());
        let temporary = parent.join(format!(".{name}.tmp-{}", std::process::id()));
        std::fs::write(&temporary, self.to_json())
            .map_err(|error| format!("cannot write {}: {error}", temporary.display()))?;
        if let Err(error) = std::fs::rename(&temporary, path) {
            let mut message = format!("cannot replace {}: {error}", path.display());
            if let Err(cleanup) = std::fs::remove_file(&temporary) {
                message.push_str(&format!("; the temporary file was left behind: {cleanup}"));
            }
            return Err(message);
        }
        Ok(())
    }

    /// Add captured entries to the GLOBAL layer, leaving every name the user
    /// already selected untouched (a manual choice always wins), and return how
    /// many rows were added.
    pub fn add_global_missing(&mut self, entries: &[(String, String)]) -> usize {
        let mut added = 0usize;
        for (name, value) in entries {
            if self.global.iter().any(|(existing, _)| existing == name) {
                continue;
            }
            self.global.push((name.clone(), value.clone()));
            added += 1;
        }
        added
    }
}

/// Names the capture itself touches in every shell (shell bookkeeping, not the
/// user's environment). Importing them would put capture noise into every child,
/// so the automatic capture drops them.
pub const AUTO_CAPTURE_SKIP_NAMES: [&str; 4] = ["_", "SHLVL", "PWD", "OLDPWD"];

/// The file beside `environment.json` that records the automatic capture.
///
/// A separate file is deliberate: the Settings page rewrites `environment.json`
/// from the rows it knows, so a marker stored in that JSON would be dropped the
/// first time a user applies the page — and the capture would run again on the
/// next start.
pub const AUTO_CAPTURE_FILE_NAME: &str = "environment.auto.json";

/// The marker path belonging to one `environment.json` path.
#[must_use]
pub fn auto_capture_path(selection_path: &Path) -> PathBuf {
    selection_path.with_file_name(AUTO_CAPTURE_FILE_NAME)
}

/// What one automatic capture did (the marker's contents).
///
/// It holds COUNTS and the shell's identity, never a variable value: this file is
/// written next to the user's selection and may appear in a bug report or an
/// evidence run (AC-1.5).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AutoCaptureRecord {
    /// The family that was captured (`csh` / `tcsh`).
    pub shell: String,
    /// The program that ran (`$SHELL` at capture time).
    pub program: String,
    /// Unix seconds of the capture.
    pub captured_at: u64,
    /// How many variables the shell reported.
    pub captured: usize,
    /// How many were new or changed and therefore written to the global layer.
    pub imported: usize,
}

impl AutoCaptureRecord {
    /// Read the marker. `None` covers "not captured yet" AND an unreadable or
    /// malformed file: a broken marker must never block the startup path, and it
    /// is not repaired silently — the capture simply runs again.
    #[must_use]
    pub fn load(path: &Path) -> Option<Self> {
        let text = std::fs::read_to_string(path).ok()?;
        let value: serde_json::Value = serde_json::from_str(&text).ok()?;
        if value.get("version").and_then(serde_json::Value::as_u64) != Some(1) {
            return None;
        }
        let text_field = |name: &str| {
            value
                .get(name)
                .and_then(serde_json::Value::as_str)
                .map(str::to_string)
        };
        Some(Self {
            shell: text_field("shell")?,
            program: text_field("program")?,
            captured_at: value
                .get("capturedAt")
                .and_then(serde_json::Value::as_u64)?,
            captured: value.get("captured").and_then(serde_json::Value::as_u64)? as usize,
            imported: value.get("imported").and_then(serde_json::Value::as_u64)? as usize,
        })
    }

    /// Write the marker atomically (sibling temporary file + rename), the rule the
    /// selection store uses.
    ///
    /// # Errors
    /// Returns the operating system's reason when the file cannot be written.
    pub fn save(&self, path: &Path) -> Result<(), String> {
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)
                .map_err(|error| format!("cannot create {}: {error}", parent.display()))?;
        }
        let name = path
            .file_name()
            .map(|name| name.to_string_lossy().to_string())
            .unwrap_or_else(|| AUTO_CAPTURE_FILE_NAME.to_string());
        let body = serde_json::json!({
            "version": 1,
            "shell": self.shell,
            "program": self.program,
            "capturedAt": self.captured_at,
            "captured": self.captured,
            "imported": self.imported,
        })
        .to_string();
        let temporary = parent.join(format!(".{name}.tmp-{}", std::process::id()));
        std::fs::write(&temporary, body)
            .map_err(|error| format!("cannot write {}: {error}", temporary.display()))?;
        if let Err(error) = std::fs::rename(&temporary, path) {
            let mut message = format!("cannot replace {}: {error}", path.display());
            if let Err(cleanup) = std::fs::remove_file(&temporary) {
                message.push_str(&format!("; the temporary file was left behind: {cleanup}"));
            }
            return Err(message);
        }
        Ok(())
    }
}

/// The shell this process was launched from, resolved from `$SHELL`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DetectedShell {
    /// The detected family.
    pub kind: ShellKind,
    /// The program to run: `$SHELL` verbatim, so a shell outside `PATH` works.
    pub program: PathBuf,
}

/// Resolve `$SHELL` into a capture program, or the ONE diagnostic sentence that
/// says why the automatic capture will not run (AC-1.2: one line, no crash, no
/// blocked startup).
///
/// `is_executable` is injected so the policy is testable without a filesystem.
///
/// # Errors
/// Returns the diagnostic sentence (path text only, never a value).
pub fn detect_shell(
    shell_value: Option<&str>,
    is_executable: impl Fn(&Path) -> bool,
) -> Result<DetectedShell, String> {
    let raw = shell_value.map(str::trim).filter(|value| !value.is_empty());
    let Some(raw) = raw else {
        return Err(
            "$SHELL is not set; the automatic environment capture is skipped (manual capture \
             still works)"
                .to_string(),
        );
    };
    let program = PathBuf::from(raw);
    if !is_executable(&program) {
        return Err(format!(
            "$SHELL is not an executable file: {raw}; the automatic environment capture is \
             skipped"
        ));
    }
    let Some(kind) = ShellKind::detect(raw) else {
        return Err(format!(
            "$SHELL names a shell this capture does not support: {raw}; the automatic \
             environment capture is skipped"
        ));
    };
    Ok(DetectedShell { kind, program })
}

/// Why the automatic startup capture does not run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutoCaptureSkip {
    /// A previous automatic capture is recorded beside `environment.json`.
    AlreadyRecorded,
    /// `$SHELL` is missing, not executable or an unknown family (one diagnostic).
    ShellUnusable,
    /// `$SHELL` names a family outside the csh family this batch covers.
    NotCshFamily,
}

impl AutoCaptureSkip {
    /// The short token the startup probe prints.
    #[must_use]
    pub const fn reason(self) -> &'static str {
        match self {
            Self::AlreadyRecorded => "already-recorded",
            Self::ShellUnusable => "shell-unusable",
            Self::NotCshFamily => "shell-not-csh",
        }
    }
}

/// What the automatic startup capture should do.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AutoCapturePlan {
    /// Run one capture with this shell.
    Capture(DetectedShell),
    /// Do nothing; `diagnostic` is `Some` only when the user should hear about it.
    Skip {
        /// Why nothing ran.
        reason: AutoCaptureSkip,
        /// The one line to record, when there is something to say.
        diagnostic: Option<String>,
    },
}

/// Decide the startup capture from `$SHELL` and the persisted marker.
///
/// Scope (contract §0#2): the automatic capture covers the csh family; every other
/// shell keeps the manual page unchanged, so this batch does not alter a bash/zsh
/// startup.
#[must_use]
pub fn auto_capture_plan(
    shell_value: Option<&str>,
    is_executable: impl Fn(&Path) -> bool,
    recorded: bool,
) -> AutoCapturePlan {
    if recorded {
        return AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::AlreadyRecorded,
            diagnostic: None,
        };
    }
    match detect_shell(shell_value, is_executable) {
        Ok(shell) if matches!(shell.kind, ShellKind::Csh | ShellKind::Tcsh) => {
            AutoCapturePlan::Capture(shell)
        }
        Ok(_) => AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::NotCshFamily,
            diagnostic: None,
        },
        Err(diagnostic) => AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::ShellUnusable,
            diagnostic: Some(diagnostic),
        },
    }
}

/// The entries an automatic capture imports: what the shell CHANGED relative to
/// this process (a value the process already has is not the shell's contribution),
/// minus the shell's own bookkeeping ([`AUTO_CAPTURE_SKIP_NAMES`]).
///
/// Returns the entries and how many were dropped as bookkeeping.
#[must_use]
pub fn auto_capture_entries(
    process: &Env,
    captured: &[(String, String)],
) -> (Vec<(String, String)>, usize) {
    let mut skipped = 0usize;
    let mut entries = Vec::new();
    for change in diff(process, captured) {
        let bookkeeping = AUTO_CAPTURE_SKIP_NAMES.contains(&change.name.as_str());
        if bookkeeping || change.name.is_empty() || change.name.contains('=') {
            skipped += 1;
            continue;
        }
        entries.push((change.name, change.after));
    }
    (entries, skipped)
}

#[cfg(test)]
mod tests {
    use super::{Env, XdgDirs};
    use std::path::PathBuf;

    #[test]
    fn xdg_defaults_come_from_home() {
        let env = Env::from_pairs([("HOME", "/home/alice")]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/home/alice/.config")));
        assert_eq!(
            dirs.data_home,
            Some(PathBuf::from("/home/alice/.local/share"))
        );
        assert_eq!(dirs.cache_home, Some(PathBuf::from("/home/alice/.cache")));
        assert_eq!(
            dirs.config_file(),
            Some(PathBuf::from("/home/alice/.config/vedit/vedit.toml"))
        );
    }

    #[test]
    fn xdg_explicit_absolute_values_win() {
        let env = Env::from_pairs([
            ("HOME", "/home/alice"),
            ("XDG_CONFIG_HOME", "/srv/config"),
            ("XDG_DATA_HOME", "/srv/data"),
            ("XDG_CACHE_HOME", "/srv/cache"),
        ]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/srv/config")));
        assert_eq!(dirs.data_home, Some(PathBuf::from("/srv/data")));
        assert_eq!(dirs.cache_home, Some(PathBuf::from("/srv/cache")));
    }

    #[test]
    fn relative_xdg_value_falls_back_and_is_reported() {
        let env = Env::from_pairs([("HOME", "/home/alice"), ("XDG_CONFIG_HOME", "relative/dir")]);
        let dirs = XdgDirs::from_env(&env);
        assert_eq!(dirs.config_home, Some(PathBuf::from("/home/alice/.config")));
        assert_eq!(
            XdgDirs::invalid_override(&env, "XDG_CONFIG_HOME"),
            Some("relative/dir")
        );
    }

    #[test]
    fn no_home_means_no_default_directories() {
        let env = Env::from_pairs(Vec::<(String, String)>::new());
        let dirs = XdgDirs::from_env(&env);
        assert!(dirs.config_home.is_none());
        assert!(dirs.config_file().is_none());
    }
}
