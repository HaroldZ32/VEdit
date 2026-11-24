//! One-click Scala run (batch 20261110, the `scala-run` lane's transport).
//!
//! A program is an (argv, cwd, env) triple and a stream: the caller starts the
//! job, then polls it from a timer and appends every chunk it gets to the
//! terminal panel. Each poll returns **only the output produced since the
//! previous poll of that job**, so a byte is never delivered twice and the caller
//! never needs a buffer of its own.
//!
//! # Why this job does not use [`crate::jobgate`]
//!
//! The other batch lanes hand out a finished payload exactly once. A run is
//! different: its payload is a *growing* stream plus a terminal state, so the slot
//! here holds a pending-output buffer and a state machine instead of a one-shot
//! result. It reuses the gate's id space ([`crate::jobgate::next_id`]) so ids can
//! never collide across lanes.
//!
//! # Sanitized child environment
//!
//! The engine starts the child with a cleared environment and re-adds only what
//! this request names (`env`), plus the five variables a JVM needs to start at
//! all: `PATH`, `HOME`, `TMPDIR`, `LANG` and `LC_ALL` (`LANG` becomes `C.UTF-8`
//! when neither locale variable is inherited). That is what makes a run
//! reproducible and keeps a user's unrelated shell variables (and secrets) out of
//! the child.
//!
//! # The engine (the `scala-run` lane)
//!
//! [`vd_scala_run_start`] returns an id immediately and its worker thread runs
//! **exactly** the argv the request carries (`argv[0]` is resolved on the child's
//! `PATH`), in the request's root directory, with the sanitized environment
//! above. `argv` is already resolved on purpose: the LANE owns project-shape
//! detection (`build.sbt` → offline `sbt run`; a scala-cli project or a bare
//! `*.scala` → `scala-cli run --offline --server=false --jvm system`; neither →
//! one explicit error), and this module owns the child, the stream, the exit code
//! and the cancellation. It never pretends to have run anything, and it never
//! panics across the boundary.
//!
//! Cancelling — and exhausting the wall-clock budget — ends the child's whole
//! **process tree**: the child is started with `setsid(2)`, so its pid is also its
//! process-group id, and the group gets SIGTERM followed by SIGKILL after
//! `KILL_GRACE`. That is what stops the JVM a Scala runner starts, not just the
//! runner itself.

use crate::capi::{
    c_str, c_string, free_string, VdError, VdScalaRunOptions, VdScalaRunStatus,
    VD_ERR_INVALID_ARGUMENT, VD_ERR_NOT_FOUND, VD_STATUS_FAILED, VD_STATUS_PENDING,
    VD_STATUS_READY,
};
use crate::jobgate::{next_id, LANE_SCALA};
use crate::service::{service_state, unknown_job, VdService, POLL_MISUSE};
use std::collections::HashMap;
use std::io::{ErrorKind, Read};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::process::{CommandExt, ExitStatusExt};
use std::panic::{self, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};
use std::time::{Duration, Instant};

/// Pending output that has not been polled yet is capped at this many bytes; the
/// oldest bytes are dropped and `truncated` is set, so a chatty run can never grow
/// the UI's memory without bound.
const MAX_PENDING_OUTPUT: usize = 1 << 20;

/// How often a running child is re-checked: the cancel flag and the wall-clock
/// deadline are therefore honoured within this slice, never at an arbitrary
/// moment.
const WAIT_SLICE: Duration = Duration::from_millis(40);

/// How long a cancelled or timed-out child group gets between SIGTERM and
/// SIGKILL — long enough for a JVM to run its shutdown hooks.
const KILL_GRACE: Duration = Duration::from_millis(1_500);

/// How long the engine waits for the output pumps to end once the child is gone.
/// Normally they end with the pipes; the bound exists for a process that escaped
/// the group and still holds the pipe, which must not stall the job forever.
const TAIL_FLUSH_GRACE: Duration = Duration::from_millis(2_000);

/// The only variables inherited from this process besides the request's own
/// `env`: the minimum a JVM needs to start (a shell to exec, a home for its
/// caches, a temporary directory and a UTF-8 locale).
const INHERITED_ENV: [&str; 5] = ["PATH", "HOME", "TMPDIR", "LANG", "LC_ALL"];

/// SIGTERM — the polite half of the kill sequence.
const SIGTERM: i32 = 15;

/// SIGKILL — the half no process can ignore.
const SIGKILL: i32 = 9;

// Two POSIX calls turn one child into a killable tree. They are declared here
// instead of adding a `libc` dependency: vd-ffi has none and the root manifest is
// owned by the skeleton task.
//
// They MUST NOT reach the public C header: `kill` and `setsid` are libc names,
// and a generated `extern int32_t kill(int32_t, int32_t);` collides with
// <signal.h>/<unistd.h> in every C++ translation unit that includes vd_ffi.h.
// The bindings are therefore private, renamed on the Rust side through
// `#[link_name]`, and marked `cbindgen:ignore`, so the header keeps exactly the
// product surface and nothing else.
mod sys {
    extern "C" {
        /// cbindgen:ignore
        #[link_name = "setsid"]
        fn raw_setsid() -> i32;
        /// cbindgen:ignore
        #[link_name = "kill"]
        fn raw_kill(pid: i32, signal: i32) -> i32;
    }

    /// `setsid(2)`: make the child a session leader, so its pid is also its
    /// process-group id.
    pub(super) fn setsid() -> i32 {
        // SAFETY: `setsid` takes no argument and only touches the calling
        // process's session; the child calls it between fork and exec.
        unsafe { raw_setsid() }
    }

    /// `kill(2)`; a NEGATIVE pid names the process group the engine created.
    pub(super) fn kill_group(pid: i32, signal: i32) -> i32 {
        // SAFETY: `pid` names a group this engine created and `signal` is one of
        // the two valid signal numbers declared below.
        unsafe { raw_kill(pid, signal) }
    }
}

/// State codes of [`VdScalaRunStatus::state`].
const STATE_RUNNING: i32 = 0;
const STATE_EXITED: i32 = 1;
const STATE_FAILED: i32 = 2;
const STATE_CANCELLED: i32 = 3;
const STATE_TIMED_OUT: i32 = 4;

/// Every run job of this process.
static RUNS: OnceLock<Mutex<HashMap<u64, Arc<RunSlot>>>> = OnceLock::new();

fn runs() -> MutexGuard<'static, HashMap<u64, Arc<RunSlot>>> {
    RUNS.get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// The output a run produced and nobody polled yet.
struct OutputBuf {
    text: String,
    truncated: bool,
}

impl OutputBuf {
    /// Append one chunk, dropping the oldest bytes past the cap.
    ///
    /// The engine appends through exactly this method, so the cap is enforced in
    /// one place.
    fn push(&mut self, chunk: &str) {
        self.text.push_str(chunk);
        if self.text.len() > MAX_PENDING_OUTPUT {
            let overflow = self.text.len() - MAX_PENDING_OUTPUT;
            // A char boundary: drop whole characters, never half a sequence.
            let mut cut = overflow;
            while cut < self.text.len() && !self.text.is_char_boundary(cut) {
                cut += 1;
            }
            self.text.drain(..cut);
            self.truncated = true;
        }
    }

    /// Take everything appended since the previous call.
    fn drain(&mut self) -> (String, bool) {
        let text = std::mem::take(&mut self.text);
        let truncated = self.truncated;
        self.truncated = false;
        (text, truncated)
    }
}

/// The terminal state of a run.
///
/// `#[allow(clippy::large_enum_variant)]`: [`VdError`] is a fixed 256-byte message
/// array (the C ABI's error type), so a failure variant is large by construction —
/// the same reason the crate allows `clippy::result_large_err`. Boxing it would add
/// an allocation on every error path for no gain here.
#[allow(clippy::large_enum_variant)]
enum RunState {
    Running,
    Exited(i32),
    Failed(VdError),
    Cancelled,
    TimedOut,
}

/// One run job: its state, its not-yet-delivered output and its cancel flag.
struct RunSlot {
    cancel: AtomicBool,
    failure_reported: AtomicBool,
    deadline: Option<Instant>,
    program: String,
    output: Mutex<OutputBuf>,
    state: Mutex<RunState>,
}

impl RunSlot {
    fn new(program: String, timeout_ms: u64) -> Self {
        Self {
            cancel: AtomicBool::new(false),
            failure_reported: AtomicBool::new(false),
            deadline: (timeout_ms > 0).then(|| Instant::now() + Duration::from_millis(timeout_ms)),
            program,
            output: Mutex::new(OutputBuf {
                text: String::new(),
                truncated: false,
            }),
            state: Mutex::new(RunState::Running),
        }
    }

    /// Ask the engine to stop. A finished run keeps its result.
    fn cancel(&self) {
        self.cancel.store(true, Ordering::SeqCst);
    }

    fn is_cancelled(&self) -> bool {
        self.cancel.load(Ordering::SeqCst)
    }

    /// True once the wall-clock budget is spent.
    fn past_deadline(&self) -> bool {
        self.deadline
            .is_some_and(|deadline| Instant::now() >= deadline)
    }

    /// Append output the engine read from the child.
    fn push_output(&self, chunk: &str) {
        self.output
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .push(chunk);
    }

    /// Take every byte produced since the previous poll.
    fn drain_output(&self) -> (String, bool) {
        let mut output = self
            .output
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        output.drain()
    }

    /// Replace the terminal state unless it is already terminal.
    fn finish(&self, state: RunState) {
        let mut slot = self
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if matches!(&*slot, RunState::Running) {
            *slot = state;
        }
    }

    /// The state as the ABI reports it, with the cancel flag and the deadline
    /// folded in, so the caller never polls a run that can no longer progress.
    fn reported_state(&self) -> i32 {
        let state = self
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        match &*state {
            RunState::Running => {
                if self.is_cancelled() {
                    STATE_CANCELLED
                } else if self
                    .deadline
                    .is_some_and(|deadline| Instant::now() >= deadline)
                {
                    STATE_TIMED_OUT
                } else {
                    STATE_RUNNING
                }
            }
            RunState::Exited(_) => STATE_EXITED,
            RunState::Failed(_) => STATE_FAILED,
            RunState::Cancelled => STATE_CANCELLED,
            RunState::TimedOut => STATE_TIMED_OUT,
        }
    }

    /// The failure the engine recorded, if any (`vd_service_error` carries it).
    ///
    /// Reported once: a later poll never overwrites the reason a different call
    /// put there.
    fn take_failure(&self) -> Option<VdError> {
        let state = self
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let RunState::Failed(error) = &*state else {
            return None;
        };
        // Only a poll that actually OBSERVES the failure reports it, and only
        // once: an earlier poll that still saw "running" must not consume the
        // flag and silence the reason.
        if self.failure_reported.swap(true, Ordering::SeqCst) {
            return None;
        }
        Some(*error)
    }

    fn exit_code(&self) -> i32 {
        let state = self
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        match &*state {
            RunState::Exited(code) => *code,
            _ => -1,
        }
    }
}

/// A validated run request: exactly what the engine receives.
struct RunRequest {
    root: Option<PathBuf>,
    argv: Vec<String>,
    env: Vec<(String, String)>,
    timeout_ms: u64,
    merge_stderr: bool,
}

/// The engine entry (the `scala-run` lane): run exactly the request's argv as ONE
/// child, stream its output into the slot, and report how it ended.
fn run(slot: &Arc<RunSlot>, request: &RunRequest) -> RunState {
    let program = match resolve_program(&request.argv[0], &request.env) {
        Ok(program) => program,
        Err(error) => return RunState::Failed(error),
    };
    let mut command = Command::new(&program);
    command.args(&request.argv[1..]);
    if let Some(root) = request.root.as_deref() {
        if !root.is_dir() {
            return RunState::Failed(VdError::new(
                VD_ERR_NOT_FOUND,
                &format!("the project root is not a directory: {}", root.display()),
            ));
        }
        command.current_dir(root);
    }
    command.env_clear();
    command.envs(child_environment(request));
    command.stdin(Stdio::null());
    command.stdout(Stdio::piped());
    command.stderr(if request.merge_stderr {
        Stdio::piped()
    } else {
        // `merge_stderr == 0` streams stdout only, but the child's stderr stays
        // on this process's stderr instead of being discarded silently.
        Stdio::inherit()
    });
    // SAFETY: `setsid` is async-signal-safe and is the only work between fork and
    // exec; it gives the child its own session — and therefore its own process
    // group, which is what `kill_tree` signals.
    unsafe {
        command.pre_exec(|| {
            if sys::setsid() < 0 {
                Err(std::io::Error::last_os_error())
            } else {
                Ok(())
            }
        });
    }
    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(error) => {
            return RunState::Failed(VdError::new(
                crate::capi::VD_ERR_INTERNAL,
                &format!("cannot start `{}`: {error}", program.display()),
            ));
        }
    };
    // One pump per piped stream; both append into the same pending output, so a
    // `merge_stderr` run shows the interleaving the child produced (chunk order
    // between two pipes is best-effort, as with any two pipes).
    let (pump_done_tx, pump_done_rx) = std::sync::mpsc::channel::<()>();
    let mut pumps = 0_usize;
    if let Some(stream) = child.stdout.take() {
        if pump(stream, Arc::clone(slot), pump_done_tx.clone()) {
            pumps += 1;
        }
    }
    if let Some(stream) = child.stderr.take() {
        if pump(stream, Arc::clone(slot), pump_done_tx.clone()) {
            pumps += 1;
        }
    }
    let outcome = loop {
        match child.try_wait() {
            Ok(Some(status)) => break RunState::Exited(exit_code(status)),
            Ok(None) => {}
            Err(error) => {
                kill_tree(&mut child);
                break RunState::Failed(VdError::new(
                    crate::capi::VD_ERR_INTERNAL,
                    &format!("cannot wait for `{}`: {error}", program.display()),
                ));
            }
        }
        if slot.is_cancelled() {
            kill_tree(&mut child);
            break RunState::Cancelled;
        }
        if slot.past_deadline() {
            kill_tree(&mut child);
            break RunState::TimedOut;
        }
        std::thread::sleep(WAIT_SLICE);
    };
    // A terminal state means every byte the pumps READ has been published, so a
    // caller that stops polling on that state cannot lose the tail of the output.
    // The wait is BOUNDED: a process that escaped the group and still holds the
    // pipe must not keep this job alive forever.
    let flush_deadline = Instant::now() + TAIL_FLUSH_GRACE;
    for _ in 0..pumps {
        let remaining = flush_deadline.saturating_duration_since(Instant::now());
        if pump_done_rx.recv_timeout(remaining).is_err() {
            break;
        }
    }
    outcome
}

/// Resolve the program the way the child's own `exec` would: a path containing a
/// `/` is used as written, a bare name is searched in the child's `PATH`.
fn resolve_program(program: &str, env: &[(String, String)]) -> Result<PathBuf, VdError> {
    if program.contains('/') {
        return Ok(PathBuf::from(program));
    }
    let path = env
        .iter()
        .rev()
        .find(|(key, _)| key == "PATH")
        .map(|(_, value)| value.clone())
        .or_else(|| std::env::var("PATH").ok());
    let Some(path) = path else {
        return Err(VdError::new(
            VD_ERR_NOT_FOUND,
            &format!("cannot run `{program}`: no PATH in the request and none in this process"),
        ));
    };
    for directory in path.split(':').filter(|directory| !directory.is_empty()) {
        let candidate = Path::new(directory).join(program);
        if is_executable(&candidate) {
            return Ok(candidate);
        }
    }
    Err(VdError::new(
        VD_ERR_NOT_FOUND,
        &format!("`{program}` was not found on PATH: {path}"),
    ))
}

/// True for an existing regular file with an execute bit set.
fn is_executable(path: &Path) -> bool {
    path.metadata()
        .is_ok_and(|metadata| metadata.is_file() && metadata.permissions().mode() & 0o111 != 0)
}

/// The child environment: the inherited minimum, then the request's own entries
/// (the last entry for a key wins, exactly like `execve`'s environment).
fn child_environment(request: &RunRequest) -> Vec<(String, String)> {
    let mut env: Vec<(String, String)> = INHERITED_ENV
        .iter()
        .filter_map(|key| {
            std::env::var(key)
                .ok()
                .map(|value| ((*key).to_string(), value))
        })
        .collect();
    for (key, value) in &request.env {
        match env.iter_mut().find(|(name, _)| name == key) {
            Some(entry) => entry.1 = value.clone(),
            None => env.push((key.clone(), value.clone())),
        }
    }
    if !env.iter().any(|(key, _)| key == "LANG" || key == "LC_ALL") {
        env.push(("LANG".to_string(), "C.UTF-8".to_string()));
    }
    env
}

/// Read one child stream into the slot until EOF. `done` is signalled when the
/// thread ends, so the engine can wait for the tail without an unbounded join.
fn pump<R: Read + Send + 'static>(
    mut stream: R,
    slot: Arc<RunSlot>,
    done: std::sync::mpsc::Sender<()>,
) -> bool {
    std::thread::Builder::new()
        .name("vd-ffi-scala-output".to_string())
        .spawn(move || {
            let mut pending: Vec<u8> = Vec::new();
            let mut chunk = [0u8; 8192];
            loop {
                match stream.read(&mut chunk) {
                    Ok(0) => break,
                    Ok(read) => {
                        pending.extend_from_slice(&chunk[..read]);
                        decode_and_push(&mut pending, &slot);
                    }
                    Err(error) if error.kind() == ErrorKind::Interrupted => continue,
                    // The child closed the pipe: its exit status is the report.
                    Err(_) => break,
                }
            }
            // A trailing incomplete sequence is replaced, never dropped silently.
            if !pending.is_empty() {
                slot.push_output(&String::from_utf8_lossy(&pending));
            }
            if done.send(()).is_err() {
                // The engine already stopped waiting (the flush bound was hit).
            }
        })
        .is_ok()
}

/// Publish the complete UTF-8 prefix of `pending`, replacing an invalid sequence
/// with U+FFFD and keeping an incomplete tail for the next read — a character
/// split across two reads is never mangled.
fn decode_and_push(pending: &mut Vec<u8>, slot: &RunSlot) {
    loop {
        match std::str::from_utf8(pending) {
            Ok(text) => {
                let owned = text.to_string();
                pending.clear();
                slot.push_output(&owned);
                return;
            }
            Err(error) => {
                let valid = error.valid_up_to();
                if valid > 0 {
                    let text = String::from_utf8_lossy(&pending[..valid]).into_owned();
                    pending.drain(..valid);
                    slot.push_output(&text);
                }
                match error.error_len() {
                    Some(length) => {
                        pending.drain(..length);
                        slot.push_output("\u{FFFD}");
                    }
                    // An incomplete sequence at the end of this read.
                    None => return,
                }
            }
        }
    }
}

/// Kill the child's whole process group and reap it.
fn kill_tree(child: &mut Child) {
    let Ok(pid) = i32::try_from(child.id()) else {
        // A pid that does not fit the syscall's type cannot be signalled as a
        // group; the direct kill is all that is left.
        let _ = child.kill();
        return;
    };
    // SAFETY: `kill` on the process group this engine created. ESRCH (the group
    // is already gone) is the normal case and needs no handling.
    sys::kill_group(-pid, SIGTERM);
    let grace = Instant::now() + KILL_GRACE;
    while Instant::now() < grace {
        match child.try_wait() {
            Ok(Some(_)) | Err(_) => break,
            Ok(None) => std::thread::sleep(Duration::from_millis(20)),
        }
    }
    // SAFETY: the group is still ours; a child that ignored SIGTERM gets SIGKILL.
    sys::kill_group(-pid, SIGKILL);
    // The run is already terminal; a reaper error has no channel here.
    let _ = child.wait();
}

/// The exit code the ABI reports: the child's own code, `128 + signal` when a
/// signal ended it (the terminal panel's convention), `-1` when neither is known.
fn exit_code(status: ExitStatus) -> i32 {
    match (status.code(), status.signal()) {
        (Some(code), _) => code,
        (None, Some(signal)) => 128 + signal,
        (None, None) => -1,
    }
}

/// Read and validate [`VdScalaRunOptions`].
///
/// # Safety
///
/// `options` must be null or readable for the duration of the call; its lists
/// must satisfy the string-list contract.
unsafe fn read_request(options: *const VdScalaRunOptions) -> Result<RunRequest, String> {
    if options.is_null() {
        return Err("options must not be null: a run needs an argv".to_string());
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let options = unsafe { *options };
    // SAFETY: the pointer is null or a valid NUL-terminated string.
    let root = match unsafe { c_str(options.root) } {
        None => None,
        Some("") => {
            return Err("root must be a non-empty path or null".to_string());
        }
        Some(text) => Some(PathBuf::from(text)),
    };
    // SAFETY: a non-null list satisfies read_string_list's contract.
    let argv = unsafe { crate::deeplint::read_string_list(&raw const options.argv) }?;
    if argv.is_empty() {
        return Err("argv must not be empty: items[0] is the program to run".to_string());
    }
    if argv[0].is_empty() {
        return Err("argv[0] must name the program to run".to_string());
    }
    // SAFETY: same contract.
    let raw_env = unsafe { crate::deeplint::read_string_list(&raw const options.env) }?;
    let mut env = Vec::with_capacity(raw_env.len());
    for entry in raw_env {
        let Some((key, value)) = entry.split_once('=') else {
            return Err(format!(
                "every env entry must be NAME=VALUE; `{entry}` has no `=`"
            ));
        };
        if key.is_empty()
            || key.chars().any(char::is_control)
            || value.chars().any(char::is_control)
        {
            return Err(format!(
                "env entry `{entry}` is not a usable NAME=VALUE pair"
            ));
        }
        env.push((key.to_string(), value.to_string()));
    }
    Ok(RunRequest {
        root,
        argv,
        env,
        timeout_ms: options.timeout_ms,
        merge_stderr: options.merge_stderr != 0,
    })
}

/// Start one Scala run.
///
/// Returns the job id immediately (`0` when the arguments were rejected, with the
/// reason readable through `vd_service_error`). Poll it with
/// [`vd_scala_run_poll`]; cancel with [`vd_scala_run_cancel`]; release with
/// [`vd_scala_run_release`]. The calling thread never spawns the child.
///
/// # Safety
///
/// `service` must be null or live; `options` must be null or readable for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_scala_run_start(
    service: *mut VdService,
    options: *const VdScalaRunOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let request = match unsafe { read_request(options) } {
        Ok(request) => request,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let id = next_id(LANE_SCALA);
    let slot = Arc::new(RunSlot::new(request.argv[0].clone(), request.timeout_ms));
    runs().insert(id, Arc::clone(&slot));
    let worker = Arc::clone(&slot);
    let spawned = std::thread::Builder::new()
        .name("vd-ffi-scala-run".to_string())
        .spawn(move || {
            let outcome = panic::catch_unwind(AssertUnwindSafe(|| run(&worker, &request)));
            let finished = match outcome {
                Ok(state) => state,
                Err(_) => RunState::Failed(VdError::new(
                    crate::capi::VD_ERR_INTERNAL,
                    "the Scala run job panicked; its output was discarded",
                )),
            };
            worker.finish(finished);
        });
    if spawned.is_err() {
        slot.finish(RunState::Failed(VdError::new(
            crate::capi::VD_ERR_INTERNAL,
            "cannot start a worker thread for the Scala run",
        )));
    }
    id
}

/// Poll one run job.
///
/// `VD_STATUS_PENDING` while the run can still produce output, `VD_STATUS_READY`
/// once it reached a terminal state, or `POLL_MISUSE` for a null `out` or an
/// unknown id. Either status fills `out` with the state and the output produced
/// since the previous poll, so the caller appends it and keeps polling until
/// `VD_STATUS_READY`.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdScalaRunStatus`].
#[no_mangle]
pub unsafe extern "C" fn vd_scala_run_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdScalaRunStatus,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = runs().get(&job).cloned() else {
        return unknown_job(state);
    };
    let reported = slot.reported_state();
    if let Some(error) = slot.take_failure() {
        state.record_error(error);
    }
    let (chunk, truncated) = slot.drain_output();
    let output_len = chunk.len();
    // SAFETY: `out` is writable for one VdScalaRunStatus.
    unsafe {
        *out = VdScalaRunStatus {
            state: reported,
            exit_code: slot.exit_code(),
            output: c_string(&chunk),
            output_len,
            truncated: u8::from(truncated),
            tool: c_string(&slot.program),
        };
    }
    match reported {
        STATE_RUNNING => VD_STATUS_PENDING,
        STATE_FAILED | STATE_CANCELLED | STATE_TIMED_OUT => VD_STATUS_FAILED,
        _ => VD_STATUS_READY,
    }
}

/// Cancel one run: the engine kills the child's process tree and the run reports
/// `state = 3`. Returns `1` when the job was live, `0` when the id was unknown.
///
/// # Safety
///
/// `service` must be null or live; only null-safe access happens here.
#[no_mangle]
pub unsafe extern "C" fn vd_scala_run_cancel(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match runs().get(&job).cloned() {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Drop one run job and its output without reporting an error.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_scala_run_release(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match runs().remove(&job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Release a filled [`VdScalaRunStatus`]; null-safe. The output chunk is owned by
/// the caller and is released here.
///
/// # Safety
///
/// `status` must be null or a struct filled by [`vd_scala_run_poll`] and not freed
/// yet.
#[no_mangle]
pub unsafe extern "C" fn vd_scala_run_status_free(status: *mut VdScalaRunStatus) {
    if status.is_null() {
        return;
    }
    let status = &mut *status;
    // SAFETY: each string is null or an owned C string from `vd_scala_run_poll`.
    unsafe {
        free_string(status.output);
        free_string(status.tool);
    }
    status.output = std::ptr::null_mut();
    status.output_len = 0;
    status.tool = std::ptr::null_mut();
}
