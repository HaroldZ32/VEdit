//! Shell environment capture (batch 20261110, the `env-core` lane's transport).
//!
//! One explicit run of the user's shell, then one read of the environment it
//! ended up with: bash/zsh/sh/ksh via `set -a; . <file>; env -0`, fish via
//! `source <file>; env -0`, csh/tcsh via `source <file>; env` (their `env` has no
//! NUL form, so the engine splits on newlines and records a warning). The result
//! is the same NUL-separated block either way, which is what makes a value that
//! contains a space or a newline survive the trip.
//!
//! # This is the frozen surface, not the engine
//!
//! Batch 20261110 freezes the C ABI before the feature lanes build on it, so the
//! capture **job shape** (start → poll → free, with real cancel and a wall-clock
//! budget) lives here while the engine that fills it lives in the `env-core` lane
//! (`crates/vd-config/src/env.rs`). Until that engine lands, every job rejects
//! itself with [`crate::capi::VD_ERR_UNIMPLEMENTED`] and names the lane — it never
//! pretends to have captured an environment and never panics across the boundary.
//!
//! The payload conversion ([`EnvCaptureData::into_c`]) is the engine's only exit
//! into C: it is written and reviewed here so the engine cannot invent a second
//! ownership model.
//!
//! The same module owns the C ABI of the persisted SELECTION store
//! (`vd_env_selection_*`, batch 20261110c): the global layer and the per-project
//! layers the Settings page writes. The file format and the atomic write stay in
//! [`vd_config::env::EnvironmentSelection`]; this side only carries its rows, so
//! the Qt page never parses or writes the file itself and the two sides cannot
//! drift apart.

use crate::capi::{
    c_str, c_string, c_string_list, free_bytes, free_string, free_string_list, VdEnvCaptureOptions,
    VdEnvCaptureResult, VdEnvSelection, VdEnvSelectionEntry, VdEnvSelectionInput, VdEnvSnapshot,
    VdEnvSnapshotEntry, VdError, VD_ERR_CANCELLED, VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN,
    VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_ERR_TIMEOUT, VD_STATUS_FAILED, VD_STATUS_PENDING,
    VD_STATUS_READY,
};
use crate::jobgate::{Registry, Taken, LANE_ENV};
use crate::service::{service_state, unknown_job, VdService, POLL_MISUSE};
use std::collections::{BTreeMap, HashSet};
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;

/// Wall-clock budget of one capture when the caller sends `timeout_ms = 0`.
const DEFAULT_TIMEOUT_MS: u64 = 15_000;

/// Every capture job of this process, keyed by its id.
static JOBS: Registry<EnvCaptureData> = Registry::new();

/// The environment one capture produced.
///
/// `#[allow(dead_code)]`: the frozen surface cannot produce a payload yet (the
/// `env-core` lane does), but the conversion below is the contract that engine
/// must fill, so it is compiled and reviewed from day one.
#[allow(dead_code)]
pub(crate) struct EnvCaptureData {
    /// `NAME=VALUE` entries in the order the shell emitted them.
    pub(crate) entries: Vec<String>,
    /// The same environment as one NUL-separated blob (each entry `NAME=VALUE\0`,
    /// plus one final `\0`).
    pub(crate) raw: Vec<u8>,
    /// The shell binary that ran.
    pub(crate) shell: String,
    /// The child's exit code (`-1` when a signal ended it).
    pub(crate) exit_code: i32,
    /// Truncated stderr, when the child printed any.
    pub(crate) stderr: Option<String>,
    /// Non-fatal problems collected while reading the environment.
    pub(crate) warnings: Vec<String>,
}

#[allow(dead_code)]
impl EnvCaptureData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_env_capture_result_free`].
    pub(crate) unsafe fn into_c(self) -> VdEnvCaptureResult {
        let warnings = unsafe { c_string_list(&self.warnings) };
        let (raw, raw_len) = c_bytes(&self.raw);
        let items = if self.entries.is_empty() {
            ptr::null_mut()
        } else {
            let items: Vec<*mut c_char> =
                self.entries.iter().map(|entry| c_string(entry)).collect();
            Box::into_raw(items.into_boxed_slice()) as *mut *mut c_char
        };
        VdEnvCaptureResult {
            count: self.entries.len(),
            items,
            raw,
            raw_len,
            shell: c_string(&self.shell),
            exit_code: self.exit_code,
            stderr: self.stderr.as_deref().map_or(ptr::null_mut(), c_string),
            warnings,
        }
    }
}

/// Copy a byte buffer into an owned C block. An empty buffer is null with a zero
/// length, so a caller never has to tell "no bytes" from "allocation failed".
fn c_bytes(bytes: &[u8]) -> (*mut c_char, usize) {
    if bytes.is_empty() {
        return (ptr::null_mut(), 0);
    }
    let owned = bytes.to_vec().into_boxed_slice();
    let len = owned.len();
    (Box::into_raw(owned) as *mut c_char, len)
}

/// A validated capture request: exactly what the engine receives.
///
/// `#[allow(dead_code)]`: the frozen surface validates every field, but only the
/// `env-core` engine reads them to build the argv.
#[allow(dead_code)]
struct CaptureRequest {
    shell: i32,
    shell_path: Option<PathBuf>,
    source_file: Option<PathBuf>,
    workdir: Option<PathBuf>,
    extra_argv: Vec<String>,
    timeout_ms: u64,
    login: bool,
    interactive: bool,
}

/// The engine entry (batch 20261110, the `env-core` lane): translate the
/// validated request into the config layer's [`vd_config::env::ShellInit`], run
/// ONE capture, and hand the outcome back as the ABI payload.
///
/// The slot is passed in so a caller cancel kills the shell immediately (the
/// engine polls the flag while it waits), and the engine's own budget is the
/// request's `timeout_ms` — two independent reasons a capture can be stopped.
fn capture(
    request: &CaptureRequest,
    slot: &crate::jobgate::Slot<EnvCaptureData>,
) -> Result<EnvCaptureData, VdError> {
    let shell = vd_config::env::ShellKind::from_code(request.shell).ok_or_else(|| {
        VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "shell must be one of 0 bash, 1 zsh, 2 csh, 3 tcsh, 4 fish, 5 sh, 6 ksh, 7 custom",
        )
    })?;
    let init = vd_config::env::ShellInit {
        shell,
        program: request.shell_path.clone(),
        source_file: request.source_file.clone(),
        workdir: request.workdir.clone(),
        extra_argv: request.extra_argv.clone(),
        login: request.login,
        interactive: request.interactive,
        timeout: std::time::Duration::from_millis(request.timeout_ms),
    };
    let outcome = vd_config::env::capture(&init, Some(slot.cancel_flag())).map_err(|error| {
        use vd_config::env::CaptureError;
        match error {
            CaptureError::Invalid(message) => VdError::new(VD_ERR_INVALID_ARGUMENT, &message),
            CaptureError::Spawn { program, reason } => VdError::new(
                VD_ERR_NOT_FOUND,
                &format!("cannot start {program}: {reason}"),
            ),
            CaptureError::TimedOut { elapsed } => VdError::new(
                VD_ERR_TIMEOUT,
                &format!(
                    "the shell did not finish within {} ms and was killed",
                    elapsed.as_millis()
                ),
            ),
            CaptureError::Cancelled => {
                VdError::new(VD_ERR_CANCELLED, "the environment capture was cancelled")
            }
            CaptureError::Io(message) | CaptureError::NotAnEnvironment(message) => {
                VdError::new(VD_ERR_SERVICE, &message)
            }
        }
    })?;
    Ok(EnvCaptureData {
        entries: outcome
            .entries
            .iter()
            .map(|(name, value)| format!("{name}={value}"))
            .collect(),
        raw: outcome.raw,
        shell: outcome.program,
        exit_code: outcome.exit_code,
        stderr: outcome.stderr,
        warnings: outcome.warnings,
    })
}

/// Apply the user's approved environment selection to the TOOL CHILDREN
/// (T10 seam 1 / AC-4.7).
///
/// The engine already merges the layers (`process < global < project`, the
/// project layer winning); this function hands the merged plain map to
/// `vd-deeplint`, which owns the one seam every lint / catalog / hierarchy child
/// passes (`binary::sanitize_tool_child_env`). The project's own filelist table
/// reads the SAME map (`vd-ffi::project` → `Variables::from_layers`), so both
/// consumers can never disagree.
///
/// # Exactly which consumers receive it (E5 + batch-20261115 AC-1.3)
///
/// Receives it: every `vd-deeplint` child (slang / verible lint / catalog /
/// hierarchy), project filelist variable expansion, and — since batch-20261115 —
/// the language-server child (`vd_lsp_session_start*` puts the same store into
/// `vd_lsp::LspConfig::environment`).
///
/// Reads the same store through a synchronous C ABI: the integrated terminal
/// (`vd_env_effective_snapshot`, `src/terminal/pty_session.cpp`) and the C++
/// toolchain resolver (the same verb → contract §4.1.2).
///
/// Still NOT covered: the VCS runner (`vd-vcs` only removes `LD_LIBRARY_PATH`).
/// Do not widen this list in prose without widening the code.
///
/// The parent process environment is NOT modified: the merge is applied to each
/// `Command` as an explicit override.
pub fn apply_environment_selection(
    selection: &vd_config::env::EnvironmentSelection,
    project_root: Option<&Path>,
) {
    let merged: BTreeMap<String, String> = selection.merged_for(project_root).into_iter().collect();
    vd_deeplint::binary::set_imported_environment(merged);
}

/// Load the persisted selection (the `environment.json` the Settings page
/// writes) and apply it. A broken or missing file degrades to an empty
/// selection plus warnings — the config loader's rule, never a panic — and the
/// returned warnings are safe to show (they name paths, never values).
#[must_use]
pub fn apply_environment_selection_file(path: &Path, project_root: Option<&Path>) -> Vec<String> {
    apply_environment_selection_layers(path, project_root).warnings
}

/// What one apply read and merged, for the startup probes (AC-1.1 / AC-1.4).
///
/// COUNTS only, so this value is safe to print (AC-1.5): it carries no variable
/// name and no value.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct AppliedLayers {
    /// Rows in the global layer.
    pub global: usize,
    /// Rows in the project layer that matched `project_root`.
    pub project: usize,
    /// Entries after the merge (a name present in both layers counts once).
    pub merged: usize,
    /// Non-fatal problems from reading the file.
    pub warnings: Vec<String>,
}

/// Load and apply the persisted selection, reporting the layer counts.
///
/// The product's startup path uses this form so its probe line shows WHICH layers
/// were applied — that is the AC-1.4 evidence: with no project root
/// `global > 0`, `project = 0`, `merged = global`.
#[must_use]
pub fn apply_environment_selection_layers(
    path: &Path,
    project_root: Option<&Path>,
) -> AppliedLayers {
    let (selection, warnings) = vd_config::env::EnvironmentSelection::load(path);
    let global = selection.global.len();
    let project = project_root
        .and_then(|root| selection.projects.get(&root.to_string_lossy().to_string()))
        .map(Vec::len)
        .unwrap_or(0);
    let merged = selection.merged_for(project_root).len();
    apply_environment_selection(&selection, project_root);
    AppliedLayers {
        global,
        project,
        merged,
        warnings,
    }
}

/// One machine-readable line for a startup seam, gated like the lane's other
/// debug lines (`VEDIT_DEBUG`): an ordinary start writes nothing to stdout.
fn debug_probe(line: &str) {
    if std::env::var_os("VEDIT_DEBUG").is_none() {
        return;
    }
    use std::io::Write;
    println!("{line}");
    let _ = std::io::stdout().flush();
}

/// The environment a child of this process should see (AC-1.3): this process's
/// own variables overlaid with the imported layer the tool children already
/// receive.
///
/// One order everywhere: `process < imported`, where the imported layer is
/// already `global < project` (that merge happens when it is applied).
#[must_use]
pub fn effective_environment() -> BTreeMap<String, String> {
    let mut merged: BTreeMap<String, String> = std::env::vars_os()
        .filter_map(|(name, value)| Some((name.into_string().ok()?, value.into_string().ok()?)))
        .collect();
    for (name, value) in vd_deeplint::binary::imported_environment() {
        merged.insert(name, value);
    }
    merged
}

/// Apply the persisted environment selection to the TOOL CHILDREN of this
/// process, from C — the ONE startup call (T63 / AC-4.7).
///
/// The application shell calls this once while it is starting up, before any
/// lint / catalog / hierarchy child can be spawned: it
/// loads `selection_path` (the `environment.json` the Preferences page writes
/// beside `settings.toml`) and hands the merged layer to
/// [`vd_deeplint::binary::set_imported_environment`], the seam every
/// `vd-deeplint` tool child passes (`binary::sanitize_tool_child_env`).
/// `project_root` selects the project layer of the selection (null or empty =
/// the global layer only). Language server, VCS and terminal children are NOT
/// covered (see [`apply_environment_selection`]).
///
/// Synchronous on purpose: it runs once during startup, before the event loop
/// exists and before a child can be spawned. It reads ONE small JSON file; a job
/// would either have to block the same thread in a poll loop (nothing else can
/// run at that point anyway) or let the first child spawn before the layer is in
/// place.
///
/// A missing file is the empty selection and no warning; a broken file is the
/// empty selection plus ONE warning (AC-4.4) and still returns `1` — the product
/// prints that warning and keeps working.
///
/// Returns `1` when the selection was applied and `0` when an argument was
/// refused (the reason is recorded on `service`, readable with
/// [`crate::service::vd_service_error`]). `warnings` may be null; when non-null
/// it receives an owned C string — possibly empty, never null on success —
/// holding the non-fatal problems joined by `"; "`, to be released with
/// [`crate::capi::vd_string_free`].
///
/// # Safety
///
/// `service` must be null or live; `selection_path` must be null or a valid
/// NUL-terminated string; `project_root` must be null or a valid NUL-terminated
/// string; `warnings` must be null or writable for one `*mut c_char`.
#[no_mangle]
pub unsafe extern "C" fn vd_env_apply_selection(
    service: *mut VdService,
    selection_path: *const c_char,
    project_root: *const c_char,
    warnings: *mut *mut c_char,
) -> i32 {
    let state = service_state(service);
    let Some(path) = (unsafe { c_str(selection_path) }) else {
        if let Some(state) = state {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                "selection_path must be a valid UTF-8 path to environment.json",
            ));
        }
        return 0;
    };
    let project_root = if project_root.is_null() {
        None
    } else {
        match unsafe { c_str(project_root) } {
            Some(root) if !root.is_empty() => Some(PathBuf::from(root)),
            Some(_) => None,
            None => {
                if let Some(state) = state {
                    state.record_error(VdError::new(
                        VD_ERR_INVALID_ARGUMENT,
                        "project_root must be a valid UTF-8 path or null",
                    ));
                }
                return 0;
            }
        }
    };
    let applied = apply_environment_selection_layers(Path::new(path), project_root.as_deref());
    debug_probe(&format!(
        "ENV-APPLY-LAYERS selection={path} project_root={} global={} project={} merged={} warnings={}",
        project_root
            .as_deref()
            .map_or_else(|| "<none>".to_string(), |root| root.display().to_string()),
        applied.global,
        applied.project,
        applied.merged,
        applied.warnings.len()
    ));
    if !warnings.is_null() {
        // SAFETY: the caller passes a writable slot for one owned string.
        unsafe { *warnings = c_string(&applied.warnings.join("; ")) };
    }
    1
}

/// The environment a child of this process should see, for the C++ side
/// (AC-1.3: the integrated terminal and the toolchain resolver).
///
/// It is exactly [`effective_environment`] (`process < global < project`), so the
/// PTY and the Rust tool children cannot disagree about what was imported. The
/// caller owns the rows and releases them with [`vd_env_snapshot_free`].
///
/// Returns `1` when the snapshot was filled and `0` when `out` was null (a null
/// `service` is accepted: this read records no service error of its own).
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdEnvSnapshot`] that has not been filled yet.
#[no_mangle]
pub unsafe extern "C" fn vd_env_effective_snapshot(
    service: *mut VdService,
    out: *mut VdEnvSnapshot,
) -> i32 {
    if out.is_null() {
        if let Some(state) = service_state(service) {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                "the snapshot output slot must not be null",
            ));
        }
        return 0;
    }
    let merged = effective_environment();
    let rows: Vec<VdEnvSnapshotEntry> = merged
        .iter()
        .map(|(name, value)| VdEnvSnapshotEntry {
            name: c_string(name),
            value: c_string(value),
        })
        .collect();
    let count = rows.len();
    let items = if rows.is_empty() {
        ptr::null_mut()
    } else {
        Box::into_raw(rows.into_boxed_slice()) as *mut VdEnvSnapshotEntry
    };
    // SAFETY: the caller passes a writable slot for one snapshot.
    unsafe {
        *out = VdEnvSnapshot { count, items };
    }
    1
}

/// Release a snapshot filled by [`vd_env_effective_snapshot`]; null-safe.
///
/// # Safety
///
/// `snapshot` must be null or point to a struct filled by
/// [`vd_env_effective_snapshot`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_env_snapshot_free(snapshot: *mut VdEnvSnapshot) {
    if snapshot.is_null() {
        return;
    }
    let snapshot = &mut *snapshot;
    if !snapshot.items.is_null() {
        // SAFETY: `items` came from `Box<[..]>::into_raw` with `count` rows.
        let rows = &mut *std::ptr::slice_from_raw_parts_mut(snapshot.items, snapshot.count);
        for row in rows.iter_mut() {
            // SAFETY: each string is null or an owned C string from the fill.
            unsafe {
                free_string(row.name);
                free_string(row.value);
            }
            row.name = ptr::null_mut();
            row.value = ptr::null_mut();
        }
        // SAFETY: the array itself came from `Box<[..]>::into_raw`.
        drop(unsafe {
            Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                snapshot.items,
                snapshot.count,
            ))
        });
    }
    snapshot.count = 0;
    snapshot.items = ptr::null_mut();
}

/// Set once so a second startup call cannot start a second capture.
static AUTO_CAPTURE_STARTED: std::sync::atomic::AtomicBool =
    std::sync::atomic::AtomicBool::new(false);

/// A file the shell can actually be executed from.
fn is_executable_file(path: &Path) -> bool {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::metadata(path)
            .map(|meta| meta.is_file() && meta.permissions().mode() & 0o111 != 0)
            .unwrap_or(false)
    }
    #[cfg(not(unix))]
    {
        path.is_file()
    }
}

/// The background body of the startup capture (AC-1.1 / AC-1.2).
///
/// It runs on its own thread so the window never waits for a shell. Everything it
/// can print is a COUNT or a path: a variable name or value never reaches a log
/// line (AC-1.5).
fn run_auto_capture(selection_path: &Path, project_root: Option<PathBuf>) {
    if std::env::var_os("VEDIT_NO_ENV_AUTOCAPTURE").is_some() {
        debug_probe("ENV-AUTOCAPTURE applied=0 reason=disabled-by-env");
        return;
    }
    let marker_path = vd_config::env::auto_capture_path(selection_path);
    let recorded = vd_config::env::AutoCaptureRecord::load(&marker_path).is_some();
    let shell_value = std::env::var("SHELL").ok();
    let plan =
        vd_config::env::auto_capture_plan(shell_value.as_deref(), is_executable_file, recorded);
    let shell = match plan {
        vd_config::env::AutoCapturePlan::Skip { reason, diagnostic } => {
            match diagnostic {
                // AC-1.2: exactly ONE diagnostic line for an unusable `$SHELL`,
                // and the startup path keeps going.
                Some(diagnostic) => eprintln!("vedit-app: environment auto-capture: {diagnostic}"),
                None => debug_probe(&format!(
                    "ENV-AUTOCAPTURE applied=0 reason={}",
                    reason.reason()
                )),
            }
            return;
        }
        vd_config::env::AutoCapturePlan::Capture(shell) => shell,
    };
    let init = vd_config::env::ShellInit {
        shell: shell.kind,
        program: Some(shell.program.clone()),
        ..vd_config::env::ShellInit::default()
    };
    let outcome = match vd_config::env::capture(&init, None) {
        Ok(outcome) => outcome,
        Err(error) => {
            eprintln!("vedit-app: environment auto-capture failed: {error}");
            debug_probe("ENV-AUTOCAPTURE applied=0 reason=capture-failed");
            return;
        }
    };
    if outcome.exit_code != 0 {
        eprintln!(
            "vedit-app: environment auto-capture: the shell exited with {} (stderr bytes: {})",
            outcome.exit_code,
            outcome.stderr.as_ref().map(String::len).unwrap_or(0)
        );
    }
    let captured = outcome.environment().to_pairs();
    let (entries, skipped) =
        vd_config::env::auto_capture_entries(&vd_config::Env::from_process(), &captured);
    let (mut selection, warnings) = vd_config::env::EnvironmentSelection::load(selection_path);
    let added = selection.add_global_missing(&entries);
    if let Err(error) = selection.save(selection_path) {
        eprintln!("vedit-app: environment auto-capture could not be stored: {error}");
        debug_probe("ENV-AUTOCAPTURE applied=0 reason=save-failed");
        return;
    }
    apply_environment_selection(&selection, project_root.as_deref());
    let project_rows = project_root
        .as_deref()
        .and_then(|root| selection.projects.get(&root.to_string_lossy().to_string()))
        .map(Vec::len)
        .unwrap_or(0);
    let record = vd_config::env::AutoCaptureRecord {
        shell: shell.kind.program().unwrap_or("custom").to_string(),
        program: shell.program.display().to_string(),
        captured_at: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|elapsed| elapsed.as_secs())
            .unwrap_or(0),
        captured: captured.len(),
        imported: added,
    };
    if let Err(error) = record.save(&marker_path) {
        // The values ARE applied and stored; only the "do not repeat" marker is
        // missing, so the next start captures again instead of losing the import.
        eprintln!("vedit-app: environment auto-capture marker not written: {error}");
    }
    debug_probe(&format!(
        "ENV-AUTOCAPTURE applied=1 shell={} program={} captured={} imported={} skipped={} global={} project={} warnings={} selection={}",
        record.shell,
        record.program,
        record.captured,
        added,
        skipped,
        selection.global.len(),
        project_rows,
        warnings.len(),
        selection_path.display()
    ));
}

/// Start the ONE automatic startup capture of the csh-family environment
/// (AC-1.1 / AC-1.2), in the background.
///
/// `selection_path` is the same `environment.json` the Settings page writes; the
/// marker file (`environment.auto.json`) is derived beside it, and the captured
/// entries go to the GLOBAL layer (never over a name the user already selected).
/// `project_root` selects the project layer for the immediate re-apply (null = the
/// global layer only, the AC-1.4 path).
///
/// Returns `1` when the decision was recorded (a capture was started, or a
/// previous one is already recorded) and `0` when an argument was refused (the
/// reason is readable through `vd_service_error`). A `$SHELL` that is missing,
/// not executable or outside the csh family records ONE diagnostic and still
/// returns `1`: nothing about the automatic capture may block a start.
///
/// One capture per process: a second call is a no-op (`1`).
///
/// # Safety
///
/// `service` must be null or live; `selection_path` must be a valid NUL-terminated
/// string; `project_root` must be null or a valid NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_env_autocapture_start(
    service: *mut VdService,
    selection_path: *const c_char,
    project_root: *const c_char,
) -> i32 {
    let state = service_state(service);
    // SAFETY: the caller supplies a valid NUL-terminated string or null.
    let Some(path) = (unsafe { c_str(selection_path) }) else {
        if let Some(state) = state {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                "selection_path must be a valid UTF-8 path to environment.json",
            ));
        }
        return 0;
    };
    let project_root = if project_root.is_null() {
        None
    } else {
        // SAFETY: same contract for project_root.
        match unsafe { c_str(project_root) } {
            Some("") => None,
            Some(root) => Some(PathBuf::from(root)),
            None => {
                if let Some(state) = state {
                    state.record_error(VdError::new(
                        VD_ERR_INVALID_ARGUMENT,
                        "project_root must be a valid UTF-8 path or null",
                    ));
                }
                return 0;
            }
        }
    };
    if AUTO_CAPTURE_STARTED.swap(true, std::sync::atomic::Ordering::SeqCst) {
        return 1;
    }
    let selection_path = PathBuf::from(path);
    let spawned = std::thread::Builder::new()
        .name("vd-env-autocapture".to_string())
        .spawn(move || run_auto_capture(&selection_path, project_root));
    match spawned {
        Ok(_) => 1,
        Err(error) => {
            AUTO_CAPTURE_STARTED.store(false, std::sync::atomic::Ordering::SeqCst);
            if let Some(state) = state {
                state.record_error(VdError::new(
                    VD_ERR_SERVICE,
                    &format!("the environment auto-capture thread could not start: {error}"),
                ));
            }
            0
        }
    }
}

/// Read and validate [`VdEnvCaptureOptions`]. `Err` is a `(code, message)` pair
/// the caller records before it returns `0`.
///
/// # Safety
///
/// `options` must be null or point to a readable [`VdEnvCaptureOptions`] whose
/// list satisfies the string-list contract for the duration of the call.
unsafe fn read_request(
    options: *const VdEnvCaptureOptions,
) -> Result<CaptureRequest, (i32, String)> {
    if options.is_null() {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "options must not be null: a capture needs at least the shell family".to_string(),
        ));
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let options = unsafe { *options };
    if !(0..=7).contains(&options.shell) {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "shell must be one of 0 bash, 1 zsh, 2 csh, 3 tcsh, 4 fish, 5 sh, 6 ksh, 7 custom"
                .to_string(),
        ));
    }
    // SAFETY: every pointer below is null or a valid NUL-terminated string.
    let shell_path = match unsafe { c_str(options.shell_path) } {
        None => None,
        Some("") => {
            return Err((
                VD_ERR_INVALID_ARGUMENT,
                "shell_path must be a non-empty path or null".to_string(),
            ));
        }
        Some(text) => Some(PathBuf::from(text)),
    };
    if options.shell == 7 && shell_path.is_none() {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "VD_ENV_SHELL_CUSTOM requires shell_path: there is no default program".to_string(),
        ));
    }
    // SAFETY: same contract.
    let source_file = match unsafe { c_str(options.source_file) } {
        None => None,
        Some("") => {
            return Err((
                VD_ERR_INVALID_ARGUMENT,
                "source_file must be a non-empty path or null".to_string(),
            ));
        }
        Some(text) => Some(PathBuf::from(text)),
    };
    // SAFETY: same contract.
    let workdir = match unsafe { c_str(options.workdir) } {
        None => None,
        Some("") => {
            return Err((
                VD_ERR_INVALID_ARGUMENT,
                "workdir must be a non-empty path or null".to_string(),
            ));
        }
        Some(text) => Some(PathBuf::from(text)),
    };
    // SAFETY: a non-null list satisfies read_string_list's contract.
    let extra_argv =
        match unsafe { crate::deeplint::read_string_list(&raw const options.extra_argv) } {
            Ok(values) => values,
            Err(message) => return Err((VD_ERR_INVALID_ARGUMENT, message)),
        };
    for entry in &extra_argv {
        if entry.is_empty() || entry.chars().any(char::is_control) {
            return Err((
                VD_ERR_INVALID_ARGUMENT,
                "every extra_argv entry must be non-empty and free of control characters"
                    .to_string(),
            ));
        }
    }
    Ok(CaptureRequest {
        shell: options.shell,
        shell_path,
        source_file,
        workdir,
        extra_argv,
        timeout_ms: if options.timeout_ms == 0 {
            DEFAULT_TIMEOUT_MS
        } else {
            options.timeout_ms
        },
        login: options.login != 0,
        interactive: options.interactive != 0,
    })
}

/// Start one shell environment capture.
///
/// Returns a job id immediately (`0` when the arguments were rejected, with the
/// reason readable through `vd_service_error`). Poll it with
/// [`vd_env_capture_poll`]; cancel with [`vd_env_capture_cancel`]; release with
/// [`vd_env_capture_release`]. The calling thread never spawns a process and never
/// reads the file system.
///
/// # Safety
///
/// `service` must be null or live; `options` must be null or readable for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_env_capture_start(
    service: *mut VdService,
    options: *const VdEnvCaptureOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let request = match unsafe { read_request(options) } {
        Ok(request) => request,
        Err((code, message)) => {
            state.record_error(VdError::new(code, &message));
            return 0;
        }
    };
    JOBS.start(LANE_ENV, request.timeout_ms, move |slot| {
        capture(&request, slot)
    })
}

/// Poll one capture job.
///
/// `VD_STATUS_PENDING` while it runs, `VD_STATUS_READY` with the environment,
/// `VD_STATUS_FAILED` when it failed (the reason is in `vd_service_error`), or
/// `POLL_MISUSE` for a null `out`, an unknown id or an already-taken result.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdEnvCaptureResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_env_capture_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdEnvCaptureResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = JOBS.get(job) else {
        return unknown_job(state);
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdEnvCaptureResult.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => {
            state.record_error(VdError::new(
                VD_ERR_JOB_UNKNOWN,
                "the capture result was already taken or the job id is unknown",
            ));
            POLL_MISUSE
        }
    }
}

/// Cancel one capture job: the engine stops between steps and no result is
/// delivered. Returns `1` when it was live, `0` when the id was unknown.
///
/// # Safety
///
/// `service` must be null or live; only null-safe access happens here.
#[no_mangle]
pub unsafe extern "C" fn vd_env_capture_cancel(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match JOBS.get(job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Drop one capture job and its result without reporting an error (null-safe for
/// an unknown id).
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_env_capture_release(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match JOBS.remove(job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Release a filled [`VdEnvCaptureResult`]; null-safe.
///
/// # Safety
///
/// `result` must be null or point to a struct filled by
/// [`vd_env_capture_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_env_capture_result_free(result: *mut VdEnvCaptureResult) {
    if result.is_null() {
        return;
    }
    let result = &mut *result;
    if !result.items.is_null() {
        // SAFETY: `items` came from `into_c` with `count` entries.
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(result.items, result.count);
        for item in slice.iter_mut() {
            // SAFETY: every entry is an owned C string from `c_string`.
            unsafe { free_string(*item) };
            *item = ptr::null_mut();
        }
        // SAFETY: the array itself came from `Box<[..]>::into_raw`.
        drop(unsafe {
            Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                result.items,
                result.count,
            ))
        });
    }
    // SAFETY: `raw` is the boxed byte block `into_c` allocated, `raw_len` long.
    unsafe { free_bytes(result.raw, result.raw_len) };
    // SAFETY: each string below is null or an owned C string from `into_c`.
    unsafe {
        free_string(result.shell);
        free_string(result.stderr);
        free_string_list(&mut result.warnings);
    }
    result.count = 0;
    result.items = ptr::null_mut();
    result.raw = ptr::null_mut();
    result.raw_len = 0;
    result.shell = ptr::null_mut();
    result.stderr = ptr::null_mut();
}

// --- batch 20261110c: the persisted environment selection (the `env-ui` lane) --
//
// Both directions are jobs, exactly like the settings store: the calling thread
// validates its arguments and never touches the file system. The store itself is
// `vd_config::env::EnvironmentSelection` — the atomic sibling-temporary-plus-rename
// write, and the "a broken file is the empty selection plus warnings" rule — so
// these entry points only carry rows across the boundary.

/// Selection jobs of this process. Load and save share ONE typed registry, so a
/// poll can only ever take one of them. It reuses [`LANE_ENV`] because the lane
/// selector names the feature, not the registry, and the id counter is
/// process-wide, so capture and selection ids never collide.
static SELECTION_JOBS: Registry<EnvSelectionData> = Registry::new();

/// One row of a stored selection (`project_root = None` is the global layer).
struct EnvSelectionRow {
    project_root: Option<String>,
    name: String,
    value: String,
}

/// What one selection job produced: the rows a load read, or none for a save.
#[derive(Default)]
struct EnvSelectionData {
    rows: Vec<EnvSelectionRow>,
    warnings: Vec<String>,
}

impl EnvSelectionData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_env_selection_free`].
    unsafe fn into_c(self) -> VdEnvSelection {
        let warnings = unsafe { c_string_list(&self.warnings) };
        let count = self.rows.len();
        let items = if count == 0 {
            ptr::null_mut()
        } else {
            let rows: Vec<VdEnvSelectionEntry> = self
                .rows
                .into_iter()
                .map(|row| VdEnvSelectionEntry {
                    project_root: row
                        .project_root
                        .as_deref()
                        .map_or(ptr::null_mut(), c_string),
                    name: c_string(&row.name),
                    value: c_string(&row.value),
                })
                .collect();
            Box::into_raw(rows.into_boxed_slice()) as *mut VdEnvSelectionEntry
        };
        VdEnvSelection {
            count,
            items,
            warnings,
        }
    }
}

/// A validated save request: the destination plus the exact layers to store.
struct SelectionRequest {
    path: PathBuf,
    selection: vd_config::env::EnvironmentSelection,
}

/// Read the store at `path` into rows: the global layer first, then every project
/// layer in ascending root order (the store's own `BTreeMap` order).
///
/// A project layer with NO rows is not represented: it overrides nothing, so it
/// produces no row and a later save drops it (a layer exists only while it has a
/// variable). Rows themselves keep their stored order and value byte for byte.
fn read_selection(path: &Path) -> EnvSelectionData {
    let (selection, warnings) = vd_config::env::EnvironmentSelection::load(path);
    let mut rows = Vec::new();
    for (name, value) in selection.global {
        rows.push(EnvSelectionRow {
            project_root: None,
            name,
            value,
        });
    }
    for (project_root, entries) in selection.projects {
        for (name, value) in entries {
            rows.push(EnvSelectionRow {
                project_root: Some(project_root.clone()),
                name,
                value,
            });
        }
    }
    EnvSelectionData { rows, warnings }
}

/// Validate one save request on the CALLING thread, like every other ABI entry: a
/// bad argument is a typed error with a `0` job id, never a job that fails later.
///
/// A value is accepted byte for byte (it may contain `=` and newlines, AC-4.9). A
/// name must be one a process environment could carry: non-empty, without `=`
/// and without control characters. The same name twice in one layer is refused,
/// because a layer could not say which value it means.
///
/// # Safety
///
/// `path` must be null or a valid NUL-terminated string, and `entries` must be
/// null or point to `count` readable [`VdEnvSelectionInput`] rows whose strings
/// are valid NUL-terminated strings, all for the duration of the call.
unsafe fn read_selection_request(
    path: *const c_char,
    entries: *const VdEnvSelectionInput,
    count: usize,
) -> Result<SelectionRequest, (i32, String)> {
    // SAFETY: the caller contract is stated above.
    let Some(path) = (unsafe { c_str(path) }) else {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "path must be a valid UTF-8 path".to_string(),
        ));
    };
    if path.is_empty() {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "path must not be empty".to_string(),
        ));
    }
    if count > 0 && entries.is_null() {
        return Err((
            VD_ERR_INVALID_ARGUMENT,
            "entries must not be null when count is non-zero".to_string(),
        ));
    }
    let mut selection = vd_config::env::EnvironmentSelection::default();
    let mut seen: HashSet<(Option<String>, String)> = HashSet::new();
    if count > 0 {
        // SAFETY: `entries` is non-null for a non-zero count and stays valid for
        // the duration of this call.
        let rows = unsafe { std::slice::from_raw_parts(entries, count) };
        for (index, row) in rows.iter().enumerate() {
            let layer = if row.project_root.is_null() {
                None
            } else {
                // SAFETY: same contract as `path` above.
                match unsafe { c_str(row.project_root) } {
                    Some("") => None,
                    Some(root) => Some(root.to_string()),
                    None => {
                        return Err((
                            VD_ERR_INVALID_ARGUMENT,
                            format!("row {index}: project_root must be valid UTF-8"),
                        ))
                    }
                }
            };
            // SAFETY: same contract as `path` above.
            let Some(name) = (unsafe { c_str(row.name) }) else {
                return Err((
                    VD_ERR_INVALID_ARGUMENT,
                    format!("row {index}: name must be valid UTF-8"),
                ));
            };
            if name.is_empty() || name.contains('=') || name.chars().any(char::is_control) {
                return Err((
                    VD_ERR_INVALID_ARGUMENT,
                    format!(
                        "row {index}: a variable name must be non-empty, without '=' and without \
                         control characters"
                    ),
                ));
            }
            // SAFETY: same contract as `path` above.
            let Some(value) = (unsafe { c_str(row.value) }) else {
                return Err((
                    VD_ERR_INVALID_ARGUMENT,
                    format!("row {index}: value must be valid UTF-8"),
                ));
            };
            if !seen.insert((layer.clone(), name.to_string())) {
                return Err((
                    VD_ERR_INVALID_ARGUMENT,
                    format!("row {index}: {name} appears twice in one layer"),
                ));
            }
            match layer {
                None => selection.global.push((name.to_string(), value.to_string())),
                Some(root) => selection
                    .projects
                    .entry(root)
                    .or_default()
                    .push((name.to_string(), value.to_string())),
            }
        }
    }
    Ok(SelectionRequest {
        path: PathBuf::from(path),
        selection,
    })
}

/// Start a job that reads the persisted selection at `path`.
///
/// A bad or missing FILE is not a failure: the config loader's rule applies, so a
/// missing file is the empty selection and a broken one is the empty selection
/// plus a warning, both `VD_STATUS_READY` (AC-4.4 — the page must survive a hand
/// broken file). Only a bad ARGUMENT is rejected here, with `0` and the reason in
/// `vd_service_error`. Poll with [`vd_env_selection_poll`], drop with
/// [`vd_env_selection_release`].
///
/// # Safety
///
/// `service` must be null or live; `path` must be null or a valid
/// NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_env_selection_load(
    service: *mut VdService,
    path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let Some(path) = (unsafe { c_str(path) }) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "path must be a valid UTF-8 path",
        ));
        return 0;
    };
    if path.is_empty() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "path must not be empty",
        ));
        return 0;
    }
    let path = PathBuf::from(path);
    SELECTION_JOBS.start(LANE_ENV, 0, move |_slot| Ok(read_selection(&path)))
}

/// Start a job that REPLACES the stored selection at `path` with `count` rows.
///
/// The whole file is written atomically; a caller that keeps a layer must pass its
/// rows back (load first, then save the union). Returns the job id, or `0` when an
/// argument was rejected. Poll with [`vd_env_selection_poll`]; a successful save
/// answers `VD_STATUS_READY` with an empty result.
///
/// # Safety
///
/// `service` must be null or live; `path` must be null or a valid
/// NUL-terminated string; `entries` must be null or point to `count` readable
/// [`VdEnvSelectionInput`] rows whose strings are valid NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_env_selection_save(
    service: *mut VdService,
    path: *const c_char,
    entries: *const VdEnvSelectionInput,
    count: usize,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let request = match unsafe { read_selection_request(path, entries, count) } {
        Ok(request) => request,
        Err((code, message)) => {
            state.record_error(VdError::new(code, &message));
            return 0;
        }
    };
    let SelectionRequest { path, selection } = request;
    SELECTION_JOBS.start(LANE_ENV, 0, move |_slot| {
        selection
            .save(&path)
            .map_err(|message| VdError::new(VD_ERR_SERVICE, &message))?;
        Ok(EnvSelectionData::default())
    })
}

/// Poll one selection job (load or save).
///
/// `VD_STATUS_PENDING` while it runs, `VD_STATUS_READY` with the rows (empty for
/// a save), `VD_STATUS_FAILED` when the job failed (the reason is in
/// `vd_service_error`), or `POLL_MISUSE` for a null `out`, an unknown id or an
/// already-taken result.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdEnvSelection`].
#[no_mangle]
pub unsafe extern "C" fn vd_env_selection_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdEnvSelection,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = SELECTION_JOBS.get(job) else {
        return unknown_job(state);
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdEnvSelection.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => {
            state.record_error(VdError::new(
                VD_ERR_JOB_UNKNOWN,
                "the selection result was already taken or the job id is unknown",
            ));
            POLL_MISUSE
        }
    }
}

/// Drop one selection job and its result without reporting an error (null-safe
/// for an unknown id).
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_env_selection_release(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match SELECTION_JOBS.remove(job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Release a filled [`VdEnvSelection`]; null-safe.
///
/// # Safety
///
/// `selection` must be null or point to a struct filled by
/// [`vd_env_selection_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_env_selection_free(selection: *mut VdEnvSelection) {
    if selection.is_null() {
        return;
    }
    let selection = &mut *selection;
    if !selection.items.is_null() {
        // SAFETY: `items` came from `into_c` with `count` rows.
        let rows = &mut *std::ptr::slice_from_raw_parts_mut(selection.items, selection.count);
        for row in rows.iter_mut() {
            // SAFETY: each string below is null or an owned C string from
            // `into_c`.
            unsafe {
                free_string(row.project_root);
                free_string(row.name);
                free_string(row.value);
            }
            row.project_root = ptr::null_mut();
            row.name = ptr::null_mut();
            row.value = ptr::null_mut();
        }
        // SAFETY: the array itself came from `Box<[..]>::into_raw`.
        drop(unsafe {
            Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                selection.items,
                selection.count,
            ))
        });
    }
    // SAFETY: the warning list is the one `into_c` produced.
    unsafe { free_string_list(&mut selection.warnings) };
    selection.count = 0;
    selection.items = ptr::null_mut();
}
