//! Lint engines behind the C ABI (service 3 of 7).
//!
//! The lint runs themselves are `vd-deeplint` (subprocess, parsing and the
//! byte-column → UTF-16 position conversion); this module only projects the
//! results onto the C ABI and owns the project-wide pass.
//!
//! Three entry shapes, each append-only:
//!
//! * [`vd_deeplint_lint`] — the original single-file call (slang, no project
//!   settings), kept unchanged for the existing callers;
//! * [`vd_deeplint_lint_v2`] — one file with an explicit engine and the
//!   project's include dirs / defines;
//! * [`vd_deeplint_batch_start`] + `_progress` / `_poll` / `_cancel` /
//!   `_release` — the project-wide pass: a job whose progress is observable
//!   while it runs and whose cancel really kills the running child.
//!
//! Every diagnostic carries the cross-engine dedup key and its origin (CLI), so
//! the display layer can merge an LSP row with a CLI row of the same problem
//! (AC-2.13).
//!
//! # Batch job ids
//!
//! Batch ids live in a registry of their own and start at `1 << 32`, so they can
//! never collide with the service job ids (which start at 1 and would need four
//! billion jobs to reach the batch range). `vd_job_cancel` therefore never
//! touches a batch; use [`vd_deeplint_batch_cancel`].

use crate::capi::{
    c_str, c_string, free_string, VdDiagnostic, VdDiagnostics, VdError, VdHierEndpoint,
    VdHierEndpointList, VdHierInstance, VdHierInstanceList, VdHierOptions, VdHierSignal,
    VdHierSignalList, VdHierarchy, VdLintBatchOptions, VdLintProgress, VdStringList,
    VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN, VD_ERR_WRONG_JOB_KIND, VD_STATUS_FAILED,
    VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::service::{service_state, unknown_job, JobValue, Polled, VdService, POLL_MISUSE};
use std::collections::HashMap;
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use vd_deeplint::{
    class_of_deeplint, diagnostic_key, BatchHandle, BatchPhase, DiagnosticSource, Endpoint,
    EngineOptions, HierarchyOptions, HierarchyReport, LintEngine, SignalInfo,
    DEFAULT_MAX_DIAGNOSTICS, DEFAULT_TIMEOUT,
};

/// The first batch job id. Disjoint from the service registry's ids.
const FIRST_BATCH_ID: u64 = 1 << 32;

/// One diagnostic, in the editor's position model.
pub(crate) struct DeeplintDiagnostic {
    pub(crate) severity: i32,
    pub(crate) line: u32,
    pub(crate) column: u32,
    pub(crate) end_line: u32,
    pub(crate) end_column: u32,
    pub(crate) code: String,
    pub(crate) message: String,
    /// The file the diagnostic belongs to (always set for batch rows).
    pub(crate) file: Option<String>,
    /// The cross-engine dedup key (AC-2.13).
    pub(crate) key: String,
    /// 1 LSP, 2 CLI, 3 both.
    pub(crate) origin: i32,
}

/// All diagnostics of one run.
pub(crate) struct DeeplintData {
    pub(crate) diagnostics: Vec<DeeplintDiagnostic>,
}

impl DeeplintData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned array must be released with `vd_diagnostics_free`.
    pub(crate) unsafe fn into_c(self) -> VdDiagnostics {
        if self.diagnostics.is_empty() {
            return VdDiagnostics {
                count: 0,
                items: std::ptr::null_mut(),
            };
        }
        let items: Vec<VdDiagnostic> = self
            .diagnostics
            .into_iter()
            .map(|diagnostic| VdDiagnostic {
                severity: diagnostic.severity,
                line: diagnostic.line,
                column: diagnostic.column,
                end_line: diagnostic.end_line,
                end_column: diagnostic.end_column,
                code: c_string(&diagnostic.code),
                message: c_string(&diagnostic.message),
                file: diagnostic.file.as_deref().map_or(ptr::null_mut(), c_string),
                key: c_string(&diagnostic.key),
                origin: diagnostic.origin,
            })
            .collect();
        VdDiagnostics {
            count: items.len(),
            items: Box::into_raw(items.into_boxed_slice()) as *mut VdDiagnostic,
        }
    }
}

fn severity_code(severity: &vd_deeplint::Severity) -> i32 {
    match severity {
        vd_deeplint::Severity::Error => 0,
        vd_deeplint::Severity::Warning => 1,
        vd_deeplint::Severity::Note => 2,
        vd_deeplint::Severity::Ignored => 3,
        vd_deeplint::Severity::Unknown(_) => 4,
    }
}

/// Run one lint with an explicit engine and map its diagnostics.
pub(crate) fn lint_with_engine(
    file: &Path,
    options: &EngineOptions,
) -> Result<DeeplintData, VdError> {
    let report = vd_deeplint::lint_with(file, options);
    if let Some(failure) = &report.failure {
        // AC-2.10: the reason names the tool AND the stage it failed at.
        return Err(VdError::new(
            crate::capi::VD_ERR_SERVICE,
            &format!(
                "{} failed at {}: {}",
                report.tool,
                report.stage,
                failure.user_message()
            ),
        ));
    }
    Ok(diagnostics_from_report(file, &report))
}

fn diagnostics_from_report(file: &Path, report: &vd_deeplint::LintReport) -> DeeplintData {
    let path = file.to_string_lossy().to_string();
    let diagnostics = report
        .diagnostics
        .iter()
        .map(|diagnostic| {
            let (line, column, end_line, end_column) = match &diagnostic.editor_range {
                Some(range) => (
                    range.start.line,
                    range.start.character,
                    range.end.line,
                    range.end.character,
                ),
                None => (0, 0, 0, 0),
            };
            let class = class_of_deeplint(&diagnostic.severity);
            DeeplintDiagnostic {
                severity: severity_code(&diagnostic.severity),
                line,
                column,
                end_line,
                end_column,
                code: diagnostic.code.clone(),
                message: diagnostic.message.clone(),
                file: Some(path.clone()),
                key: diagnostic_key(&path, line, class, &diagnostic.message),
                origin: DiagnosticSource::Cli.code(),
            }
        })
        .collect();
    DeeplintData { diagnostics }
}

fn empty_options(engine: LintEngine) -> EngineOptions {
    EngineOptions {
        engine,
        timeout: DEFAULT_TIMEOUT,
        max_diagnostics: DEFAULT_MAX_DIAGNOSTICS,
        ..EngineOptions::default()
    }
}

/// Start a SLANG lint job for one file (the original entry point).
///
/// `binary_override` may be null (the service then resolves the binary itself).
/// Returns the job id, or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; the string arguments must be null or valid
/// NUL-terminated strings.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_lint(
    service: *mut VdService,
    file: *const c_char,
    binary_override: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(file) = crate::capi::c_str(file) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "file must be a valid UTF-8 path",
        ));
        return 0;
    };
    let binary = if binary_override.is_null() {
        None
    } else {
        // SAFETY: a non-null override is documented as a valid string.
        match unsafe { crate::capi::c_str(binary_override) } {
            Some(value) if !value.is_empty() => Some(PathBuf::from(value)),
            _ => {
                state.record_error(VdError::new(
                    VD_ERR_INVALID_ARGUMENT,
                    "binary_override must be a valid non-empty path or null",
                ));
                return 0;
            }
        }
    };
    let file = PathBuf::from(file);
    let mut options = empty_options(LintEngine::Slang);
    options.slang_binary = binary;
    state.start(move || lint_with_engine(&file, &options).map(JobValue::Deeplint))
}

/// Start one lint job with an explicit engine and the project settings (T138).
///
/// `engine` is `0` (slang) or `1` (verible-verilog-lint). `slang_binary`,
/// `verible_binary` and `rules_path` may be null. `include_dirs` / `defines` may
/// be null; the slang engine receives them as `-I` / `-D`, the verible engine
/// does not accept them (see `vd_deeplint::knobs_note`). Returns the job id, or
/// `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; every `*const c_char` must be null or a valid
/// NUL-terminated string; the lists must satisfy the string-list contract.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_lint_v2(
    service: *mut VdService,
    file: *const c_char,
    engine: i32,
    slang_binary: *const c_char,
    verible_binary: *const c_char,
    rules_path: *const c_char,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let Some(engine) = LintEngine::from_code(engine) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "engine must be 0 (slang) or 1 (verible-verilog-lint)",
        ));
        return 0;
    };
    let Some(file) = crate::capi::c_str(file) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "file must be a valid UTF-8 path",
        ));
        return 0;
    };
    // SAFETY: each helper states its own pointer contract.
    let options = match unsafe {
        build_options(
            engine,
            slang_binary,
            verible_binary,
            rules_path,
            include_dirs,
            defines,
        )
    } {
        Ok(options) => options,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let file = PathBuf::from(file);
    state.start(move || lint_with_engine(&file, &options).map(JobValue::Deeplint))
}

/// Build engine options from the C arguments (shared by the single-file and
/// batch entry points).
///
/// # Safety
///
/// Every pointer must be null or valid for the duration of the call.
unsafe fn build_options(
    engine: LintEngine,
    slang_binary: *const c_char,
    verible_binary: *const c_char,
    rules_path: *const c_char,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> Result<EngineOptions, String> {
    let mut options = empty_options(engine);
    if !slang_binary.is_null() {
        // SAFETY: a non-null pointer is documented as a valid string.
        match unsafe { c_str(slang_binary) } {
            Some(value) if !value.is_empty() => {
                options.slang_binary = Some(PathBuf::from(value));
            }
            _ => return Err("slang_binary must be a valid non-empty path or null".to_string()),
        }
    }
    if !verible_binary.is_null() {
        // SAFETY: same contract.
        match unsafe { c_str(verible_binary) } {
            Some(value) if !value.is_empty() => {
                options.verible_binary = Some(PathBuf::from(value));
            }
            _ => return Err("verible_binary must be a valid non-empty path or null".to_string()),
        }
    }
    if !rules_path.is_null() {
        // SAFETY: same contract.
        match unsafe { c_str(rules_path) } {
            Some(value) if !value.is_empty() => {
                options.rules_config = Some(PathBuf::from(value));
            }
            _ => return Err("rules_path must be a valid non-empty path or null".to_string()),
        }
    }
    // SAFETY: a non-null list satisfies read_string_list's contract.
    options.include_dirs = unsafe { read_string_list(include_dirs)? }
        .into_iter()
        .map(PathBuf::from)
        .collect();
    // SAFETY: same contract.
    options.defines = unsafe { read_string_list(defines)? };
    Ok(options)
}

/// Apply the typed per-engine lint knobs from [`VdLintBatchOptions`].
///
/// Every value is validated by the crate (`vd_deeplint::parse_*`) BEFORE a job
/// starts: a bad character set is a typed ABI error, never a tool argument. The
/// knobs the selected engine does not support are documented no-ops — they are
/// accepted (a settings page may keep the other engine's values) and produce NO
/// argv; see [`vd_deeplint::unsupported_knobs`].
///
/// # Safety
///
/// Every pointer in `batch` must be null or valid for the duration of the call.
unsafe fn apply_lint_knobs(
    options: &mut EngineOptions,
    batch: &VdLintBatchOptions,
) -> Result<(), String> {
    let Some(ruleset) = vd_deeplint::Ruleset::from_code(batch.ruleset) else {
        return Err("ruleset must be 0 (default), 1 (all) or 2 (none)".to_string());
    };
    options.ruleset = Some(ruleset);
    if !batch.rules.is_null() {
        // SAFETY: a non-null pointer is documented as a valid C string.
        let text = unsafe { c_str(batch.rules) }
            .ok_or_else(|| "rules must be valid UTF-8 or null".to_string())?;
        options.rules = vd_deeplint::parse_rules_list(text)?;
    }
    if !batch.waiver_files.is_null() {
        // SAFETY: same contract.
        let text = unsafe { c_str(batch.waiver_files) }
            .ok_or_else(|| "waiver_files must be valid UTF-8 or null".to_string())?;
        options.waiver_files = vd_deeplint::parse_waiver_files(text)?;
    }
    if !batch.slang_suppressions.is_null() {
        // SAFETY: same contract.
        let text = unsafe { c_str(batch.slang_suppressions) }
            .ok_or_else(|| "slang_suppressions must be valid UTF-8 or null".to_string())?;
        options.slang_suppressions = vd_deeplint::parse_slang_suppressions(text)?;
    }
    if batch.error_limit > 0 {
        options.error_limit = Some(batch.error_limit);
    }
    Ok(())
}

/// # Safety
///
/// `list` must be null or point to `count` valid NUL-terminated strings.
pub(crate) unsafe fn read_string_list(list: *const VdStringList) -> Result<Vec<String>, String> {
    if list.is_null() {
        return Ok(Vec::new());
    }
    // SAFETY: the caller contract guarantees a readable VdStringList.
    let list = unsafe { *list };
    if list.count == 0 {
        return Ok(Vec::new());
    }
    if list.items.is_null() {
        return Err("items must not be null when count > 0".to_string());
    }
    if list.count > 100_000 {
        return Err("the list is implausibly large".to_string());
    }
    // SAFETY: the caller contract guarantees `items` points to `count` valid
    // NUL-terminated strings.
    let items = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    let mut values = Vec::with_capacity(list.count);
    for item in items {
        // SAFETY: each `*item` is one of the validated NUL-terminated strings.
        let Some(text) = (unsafe { c_str(*item) }) else {
            return Err("every entry must be valid UTF-8".to_string());
        };
        values.push(text.to_string());
    }
    Ok(values)
}

/// The exact argv one lint run would use, as one owned string (AC-2.5's dump).
///
/// Spawns nothing. `file` may be null (the dump then shows `<file>`). Returns
/// null only when `engine` is invalid. Release the result with `vd_string_free`.
///
/// # Safety
///
/// Every pointer argument must be null or valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_lint_command_line(
    engine: i32,
    binary: *const c_char,
    file: *const c_char,
    rules_path: *const c_char,
    include_dirs: *const VdStringList,
    defines: *const VdStringList,
) -> *mut c_char {
    let Some(engine) = LintEngine::from_code(engine) else {
        return ptr::null_mut();
    };
    // SAFETY: each argument is null or a valid string per the caller contract.
    let binary = unsafe { c_str(binary) }.unwrap_or(engine.tool_name());
    // SAFETY: same contract.
    let file = unsafe { c_str(file) }.map_or_else(|| PathBuf::from("<file>"), PathBuf::from);
    // SAFETY: same contract.
    let rules = unsafe { c_str(rules_path) }.map(PathBuf::from);
    // SAFETY: a non-null list satisfies read_string_list's contract.
    let include_dirs: Vec<PathBuf> = unsafe { read_string_list(include_dirs) }
        .unwrap_or_default()
        .into_iter()
        .map(PathBuf::from)
        .collect();
    // SAFETY: same contract.
    let defines = unsafe { read_string_list(defines) }.unwrap_or_default();
    c_string(&vd_deeplint::command_line(
        engine,
        Path::new(binary),
        &file,
        &include_dirs,
        &defines,
        rules.as_deref(),
    ))
}

/// The exact argv a PROJECT lint pass would run for `file`, as one line.
///
/// This is the batch twin of [`vd_lint_command_line`]: it applies the typed
/// per-engine knobs from [`VdLintBatchOptions`] and echoes the same argv the
/// pass would spawn (nothing is spawned). Returns null when the options are
/// null, the engine code is invalid, or a knob fails validation. Release the
/// result with `vd_string_free`.
///
/// # Safety
///
/// `options` must be null or point to a readable [`VdLintBatchOptions`] whose
/// lists satisfy the string-list contract; `file` must be null or a valid
/// NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn vd_lint_batch_command_line(
    options: *const VdLintBatchOptions,
    file: *const c_char,
) -> *mut c_char {
    if options.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let options = unsafe { *options };
    let Some(engine) = LintEngine::from_code(options.engine) else {
        return ptr::null_mut();
    };
    // SAFETY: null or a valid string per the contract.
    let file = unsafe { c_str(file) }.map_or_else(|| PathBuf::from("<file>"), PathBuf::from);
    // SAFETY: the option pointers are null or valid strings per the contract.
    let mut engine_options = match unsafe {
        build_options(
            engine,
            options.slang_binary,
            options.verible_binary,
            options.rules_path,
            &raw const options.include_dirs,
            &raw const options.defines,
        )
    } {
        Ok(built) => built,
        Err(_) => return ptr::null_mut(),
    };
    // SAFETY: same contract.
    if unsafe { apply_lint_knobs(&mut engine_options, &options) }.is_err() {
        return ptr::null_mut();
    }
    let binary = match engine {
        LintEngine::Slang => engine_options
            .slang_binary
            .clone()
            .unwrap_or_else(|| PathBuf::from(engine.tool_name())),
        LintEngine::Verible => engine_options
            .verible_binary
            .clone()
            .unwrap_or_else(|| PathBuf::from(engine.tool_name())),
    };
    c_string(&vd_deeplint::command_line_for(
        engine,
        &binary,
        &file,
        &engine_options,
    ))
}

/// The option groups the selected engine IGNORES (documented no-op), comma
/// separated, or null for an invalid engine code.
///
/// The settings page (ux-batch-2 §0.1 R1) greys those controls out and shows
/// the reason instead of sending an argument the tool would reject. Release the
/// result with `vd_string_free`.
///
/// # Safety
///
/// The return value is null or a crate-owned NUL-terminated string; it must be
/// released exactly once with `vd_string_free` and never freed by the caller's
/// allocator. The call itself dereferences nothing.
#[no_mangle]
pub unsafe extern "C" fn vd_lint_unsupported_knobs(engine: i32) -> *mut c_char {
    let Some(engine) = LintEngine::from_code(engine) else {
        return ptr::null_mut();
    };
    c_string(&vd_deeplint::unsupported_knobs(engine).join(","))
}

/// Poll a lint job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdDiagnostics`].
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdDiagnostics,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    match state.poll(job) {
        Polled::Unknown => unknown_job(state),
        Polled::Pending => VD_STATUS_PENDING,
        Polled::Failed => VD_STATUS_FAILED,
        Polled::Ready(JobValue::Deeplint(data)) => {
            // SAFETY: `out` is writable for one VdDiagnostics.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Polled::Ready(_) => {
            state.record_error(VdError::new(
                VD_ERR_WRONG_JOB_KIND,
                "this job id belongs to a different service",
            ));
            POLL_MISUSE
        }
    }
}

/// Release a filled [`VdDiagnostics`]; null-safe.
///
/// # Safety
///
/// `diagnostics` must be null or point to a struct filled by
/// [`vd_deeplint_poll`] / [`vd_deeplint_batch_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_diagnostics_free(diagnostics: *mut VdDiagnostics) {
    if diagnostics.is_null() {
        return;
    }
    let diagnostics = &mut *diagnostics;
    if !diagnostics.items.is_null() {
        let slice = &mut *std::ptr::slice_from_raw_parts_mut(diagnostics.items, diagnostics.count);
        for item in slice.iter_mut() {
            free_string(item.code);
            free_string(item.message);
            free_string(item.file);
            free_string(item.key);
            item.code = std::ptr::null_mut();
            item.message = std::ptr::null_mut();
            item.file = std::ptr::null_mut();
            item.key = std::ptr::null_mut();
        }
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
            diagnostics.items,
            diagnostics.count,
        )));
    }
    diagnostics.count = 0;
    diagnostics.items = std::ptr::null_mut();
}

// --- the project-wide pass ---------------------------------------------------

struct BatchSlot {
    handle: BatchHandle,
    /// Set once the report was handed to the caller.
    consumed: AtomicBool,
}

fn batches() -> MutexGuard<'static, HashMap<u64, Arc<BatchSlot>>> {
    static BATCHES: OnceLock<Mutex<HashMap<u64, Arc<BatchSlot>>>> = OnceLock::new();
    let batches = BATCHES.get_or_init(|| Mutex::new(HashMap::new()));
    batches
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn batch_for(id: u64) -> Option<Arc<BatchSlot>> {
    batches().get(&id).cloned()
}

fn progress_to_c(progress: &vd_deeplint::BatchProgress, tool: &str) -> VdLintProgress {
    VdLintProgress {
        phase: progress.phase.code(),
        done: progress.done,
        total: progress.total,
        current: progress.current.as_ref().map_or(ptr::null_mut(), |path| {
            c_string(&path.display().to_string())
        }),
        tool: c_string(tool),
        stage: progress.stage.map_or(ptr::null_mut(), c_string),
        message: c_string(&progress.message),
    }
}

/// Start a project-wide lint pass (AC-2.7/2.8).
///
/// `files` is the pass's snapshot of the file set (already unioned and
/// de-duplicated by the caller). `options` must not be null. Returns a job id
/// (`>= 1 << 32`) or `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `files` must satisfy the string-list
/// contract; `options` must be null or point to a readable
/// [`VdLintBatchOptions`] whose lists satisfy that same contract.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_batch_start(
    service: *mut VdService,
    files: *const VdStringList,
    options: *const VdLintBatchOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    if options.is_null() || files.is_null() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "files and options must not be null",
        ));
        return 0;
    }
    // SAFETY: the caller contract guarantees both structs are readable.
    let options = unsafe { *options };
    let Some(engine) = LintEngine::from_code(options.engine) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "engine must be 0 (slang) or 1 (verible-verilog-lint)",
        ));
        return 0;
    };
    // SAFETY: a non-null list satisfies read_string_list's contract.
    let file_set: Vec<PathBuf> = match unsafe { read_string_list(files) } {
        Ok(paths) => paths.into_iter().map(PathBuf::from).collect(),
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    if file_set.is_empty() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "the project file set is empty: there is nothing to lint",
        ));
        return 0;
    }
    // SAFETY: the option pointers are null or valid strings per the contract.
    let mut engine_options = match unsafe {
        build_options(
            engine,
            options.slang_binary,
            options.verible_binary,
            options.rules_path,
            &raw const options.include_dirs,
            &raw const options.defines,
        )
    } {
        Ok(built) => built,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    if options.timeout_ms > 0 {
        engine_options.timeout = std::time::Duration::from_millis(options.timeout_ms);
    }
    if options.max_diagnostics > 0 {
        engine_options.max_diagnostics = options.max_diagnostics;
    }
    // SAFETY: the option pointers are null or valid strings per the contract.
    if let Err(message) = unsafe { apply_lint_knobs(&mut engine_options, &options) } {
        state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
        return 0;
    }
    match BatchHandle::start(file_set, engine_options) {
        Ok(handle) => {
            let id = FIRST_BATCH_ID + NEXT_BATCH.fetch_add(1, Ordering::Relaxed);
            batches().insert(
                id,
                Arc::new(BatchSlot {
                    handle,
                    consumed: AtomicBool::new(false),
                }),
            );
            id
        }
        Err(error) => {
            state.record_error(VdError::new(
                crate::capi::VD_ERR_SERVICE,
                &format!(
                    "cannot start the project lint pass: {}",
                    error.user_message()
                ),
            ));
            0
        }
    }
}

static NEXT_BATCH: AtomicU64 = AtomicU64::new(0);

/// Progress of a project-wide pass.
///
/// Returns `VD_STATUS_PENDING` while it runs (with `out` filled),
/// `VD_STATUS_READY` once it reached a terminal phase (with `out` filled), and
/// `POLL_MISUSE` for an unknown id or a null `out`.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdLintProgress`].
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_batch_progress(
    service: *mut VdService,
    job: u64,
    out: *mut VdLintProgress,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = batch_for(job) else {
        state.record_error(VdError::new(
            VD_ERR_JOB_UNKNOWN,
            "unknown project lint job id: it was never started or was released",
        ));
        return POLL_MISUSE;
    };
    let progress = slot.handle.progress();
    // SAFETY: `out` is writable for one VdLintProgress.
    unsafe { *out = progress_to_c(&progress, slot.handle.tool_name()) };
    if progress.phase.is_terminal() {
        VD_STATUS_READY
    } else {
        VD_STATUS_PENDING
    }
}

/// Poll the result of a project-wide pass.
///
/// `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with the collected
/// diagnostics once it finished or was cancelled (a cancel delivers no
/// diagnostics collected after it); `VD_STATUS_FAILED` when the pass could not
/// run (the reason names the tool and the stage). The result is delivered once.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdDiagnostics`].
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_batch_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdDiagnostics,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = batch_for(job) else {
        state.record_error(VdError::new(
            VD_ERR_JOB_UNKNOWN,
            "unknown project lint job id: it was never started or was released",
        ));
        return POLL_MISUSE;
    };
    if slot.consumed.load(Ordering::SeqCst) {
        state.record_error(VdError::new(
            VD_ERR_JOB_UNKNOWN,
            "the project lint result was already consumed",
        ));
        return POLL_MISUSE;
    }
    let progress = slot.handle.progress();
    if !progress.phase.is_terminal() {
        return VD_STATUS_PENDING;
    }
    let Some(report) = slot.handle.take_report() else {
        // Terminal phase seen but the worker has not published the report yet:
        // ask the caller to poll again rather than inventing an empty result.
        return VD_STATUS_PENDING;
    };
    slot.consumed.store(true, Ordering::SeqCst);
    if report.phase == BatchPhase::Failed {
        let reason = report.failure.as_ref().map_or_else(
            || format!("{} failed", report.tool),
            vd_deeplint::LintFailure::user_message,
        );
        state.record_error(VdError::new(
            crate::capi::VD_ERR_SERVICE,
            &format!("{}: {reason}", report.tool),
        ));
        return VD_STATUS_FAILED;
    }
    let data = DeeplintData {
        diagnostics: report
            .diagnostics
            .iter()
            .map(|entry| {
                let path = entry.file.to_string_lossy().to_string();
                let diagnostic = &entry.diagnostic;
                let (line, column, end_line, end_column) = match &diagnostic.editor_range {
                    Some(range) => (
                        range.start.line,
                        range.start.character,
                        range.end.line,
                        range.end.character,
                    ),
                    None => (0, 0, 0, 0),
                };
                let class = class_of_deeplint(&diagnostic.severity);
                DeeplintDiagnostic {
                    severity: severity_code(&diagnostic.severity),
                    line,
                    column,
                    end_line,
                    end_column,
                    code: diagnostic.code.clone(),
                    message: diagnostic.message.clone(),
                    file: Some(path.clone()),
                    key: diagnostic_key(&path, line, class, &diagnostic.message),
                    origin: DiagnosticSource::Cli.code(),
                }
            })
            .collect(),
    };
    // SAFETY: `out` is writable for one VdDiagnostics.
    unsafe { *out = data.into_c() };
    VD_STATUS_READY
}

/// Cancel a project-wide pass: it stops after the current file, the running
/// child is killed, and no later file starts (AC-2.8).
///
/// Returns `1` when the job was live, `0` when the id was unknown.
///
/// # Safety
///
/// `service` must be null or live; only null-safe access happens here.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_batch_cancel(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match batch_for(job) {
        Some(slot) => {
            slot.handle.cancel();
            1
        }
        None => 0,
    }
}

/// Release a project-wide pass and its progress state (null-safe for an unknown
/// id). A running pass is cancelled first.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_deeplint_batch_release(service: *mut VdService, job: u64) -> i32 {
    let _ = service;
    match batches().remove(&job) {
        Some(slot) => {
            slot.handle.cancel();
            1
        }
        None => 0,
    }
}

/// Release a filled [`VdLintProgress`]; null-safe.
///
/// # Safety
///
/// `progress` must be null or point to a struct filled by
/// [`vd_deeplint_batch_progress`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_lint_progress_free(progress: *mut VdLintProgress) {
    if progress.is_null() {
        return;
    }
    let progress = &mut *progress;
    free_string(progress.current);
    free_string(progress.tool);
    free_string(progress.stage);
    free_string(progress.message);
    progress.current = ptr::null_mut();
    progress.tool = ptr::null_mut();
    progress.stage = ptr::null_mut();
    progress.message = ptr::null_mut();
}

// --- hierarchical elaboration (T7) -------------------------------------------

/// First hierarchy job id; its own range so it can never collide with the lint
/// jobs (`1 << 32`) or the service jobs (which start at 1).
const FIRST_HIER_ID: u64 = 1 << 48;

struct HierarchySlot {
    report: Mutex<Option<HierarchyReport>>,
}

static NEXT_HIERARCHY: AtomicU64 = AtomicU64::new(0);

fn hierarchies() -> MutexGuard<'static, HashMap<u64, Arc<HierarchySlot>>> {
    static JOBS: OnceLock<Mutex<HashMap<u64, Arc<HierarchySlot>>>> = OnceLock::new();
    let jobs = JOBS.get_or_init(|| Mutex::new(HashMap::new()));
    jobs.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn endpoint_to_c(endpoint: &Endpoint) -> VdHierEndpoint {
    VdHierEndpoint {
        path: c_string(&endpoint.path),
        file: c_string(&endpoint.file.to_string_lossy()),
        line: endpoint.line,
        column: endpoint.column,
        kind: endpoint.kind.code(),
        detail: c_string(&endpoint.detail),
    }
}

fn endpoints_to_c(endpoints: &[Endpoint]) -> VdHierEndpointList {
    if endpoints.is_empty() {
        return VdHierEndpointList {
            count: 0,
            items: ptr::null_mut(),
        };
    }
    let items: Vec<VdHierEndpoint> = endpoints.iter().map(endpoint_to_c).collect();
    let boxed = items.into_boxed_slice();
    VdHierEndpointList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdHierEndpoint,
    }
}

fn signal_to_c(signal: &SignalInfo) -> VdHierSignal {
    let (has_declaration, declaration_file, declaration_line, declaration_column) =
        match &signal.declaration {
            Some((file, line, column)) => (1, c_string(&file.to_string_lossy()), *line, *column),
            None => (0, ptr::null_mut(), 0, 0),
        };
    VdHierSignal {
        path: c_string(&signal.path),
        kind: c_string(&signal.kind),
        has_declaration,
        declaration_file,
        declaration_line,
        declaration_column,
        drivers: endpoints_to_c(&signal.drivers),
        loads: endpoints_to_c(&signal.loads),
        chain: endpoints_to_c(&signal.chain),
    }
}

/// Convert one report into the owned C representation.
///
/// # Safety
///
/// The returned struct must be released with [`vd_hierarchy_free`].
unsafe fn hierarchy_to_c(report: &HierarchyReport) -> VdHierarchy {
    let instances: Vec<VdHierInstance> = report
        .instances
        .iter()
        .map(|instance| VdHierInstance {
            path: c_string(&instance.path),
            module: c_string(&instance.module),
            file: c_string(&instance.file.to_string_lossy()),
            line: instance.line,
            column: instance.column,
        })
        .collect();
    let instances = if instances.is_empty() {
        VdHierInstanceList {
            count: 0,
            items: ptr::null_mut(),
        }
    } else {
        let boxed = instances.into_boxed_slice();
        VdHierInstanceList {
            count: boxed.len(),
            items: Box::into_raw(boxed) as *mut VdHierInstance,
        }
    };
    let signals: Vec<VdHierSignal> = report.signals.iter().map(signal_to_c).collect();
    let signals = if signals.is_empty() {
        VdHierSignalList {
            count: 0,
            items: ptr::null_mut(),
        }
    } else {
        let boxed = signals.into_boxed_slice();
        VdHierSignalList {
            count: boxed.len(),
            items: Box::into_raw(boxed) as *mut VdHierSignal,
        }
    };
    VdHierarchy {
        tool: c_string(&report.tool),
        top: report.top.as_deref().map_or(ptr::null_mut(), c_string),
        elapsed_ms: report.elapsed.as_millis() as u64,
        peak_rss_kb: report.peak_rss_kb,
        warning: report.warning.as_deref().map_or(ptr::null_mut(), c_string),
        instances,
        signal_list: signals,
    }
}

/// Build the hierarchy options from the C arguments.
///
/// # Safety
///
/// Every pointer must be null or valid for the duration of the call.
unsafe fn build_hierarchy_options(options: &VdHierOptions) -> Result<HierarchyOptions, String> {
    let mut built = HierarchyOptions::default();
    if !options.slang_binary.is_null() {
        // SAFETY: a non-null pointer is documented as a valid string.
        match unsafe { c_str(options.slang_binary) } {
            Some(value) if !value.is_empty() => built.slang_binary = Some(PathBuf::from(value)),
            _ => return Err("slang_binary must be a valid non-empty path or null".to_string()),
        }
    }
    if !options.top.is_null() {
        // SAFETY: same contract.
        match unsafe { c_str(options.top) } {
            Some(value) if !value.is_empty() => built.top = Some(value.to_string()),
            _ => return Err("top must be a valid non-empty module name or null".to_string()),
        }
    }
    // SAFETY: a non-null list satisfies read_string_list's contract.
    built.include_dirs = unsafe { read_string_list(&raw const options.include_dirs)? }
        .into_iter()
        .map(PathBuf::from)
        .collect();
    // SAFETY: same contract.
    built.defines = unsafe { read_string_list(&raw const options.defines)? };
    if options.timeout_ms > 0 {
        built.timeout = std::time::Duration::from_millis(options.timeout_ms);
    }
    if options.max_instances > 0 {
        built.max_instances = options.max_instances;
    }
    if options.max_files > 0 {
        built.max_files = options.max_files;
    }
    Ok(built)
}

/// Start a project-wide hierarchical elaboration (T7, AC-3.1/3.2/3.4).
///
/// `files` is the project's effective file set and `options` carries the top and
/// the `-I`/`-D` settings. The work runs on a worker thread, so the UI thread
/// never waits; poll with [`vd_hier_poll`]. Returns a job id (`>= 1 << 48`) or
/// `0` when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `files` must satisfy the string-list contract;
/// `options` must be null or point to a readable [`VdHierOptions`] whose lists
/// satisfy that same contract.
#[no_mangle]
pub unsafe extern "C" fn vd_hier_start(
    service: *mut VdService,
    files: *const VdStringList,
    options: *const VdHierOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    if files.is_null() || options.is_null() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "files and options must not be null",
        ));
        return 0;
    }
    // SAFETY: the caller contract guarantees both structs are readable.
    let options = unsafe { *options };
    // SAFETY: a non-null list satisfies read_string_list's contract.
    let file_set: Vec<PathBuf> = match unsafe { read_string_list(files) } {
        Ok(paths) => paths.into_iter().map(PathBuf::from).collect(),
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    if file_set.is_empty() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "the project file set is empty: nothing to elaborate",
        ));
        return 0;
    }
    // SAFETY: the option pointers are null or valid strings per the contract.
    let hierarchy = match unsafe { build_hierarchy_options(&options) } {
        Ok(built) => built,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let id = FIRST_HIER_ID + NEXT_HIERARCHY.fetch_add(1, Ordering::Relaxed);
    let slot = Arc::new(HierarchySlot {
        report: Mutex::new(None),
    });
    hierarchies().insert(id, Arc::clone(&slot));
    let worker_slot = Arc::clone(&slot);
    let spawned = std::thread::Builder::new()
        .name(format!("vd-hier-{id}"))
        .spawn(move || {
            let report = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                vd_deeplint::elaborate(&file_set, &hierarchy)
            }))
            .unwrap_or_else(|_| {
                let mut failed = HierarchyReport {
                    tool: "slang".to_string(),
                    top: hierarchy.top.clone(),
                    instances: Vec::new(),
                    signals: Vec::new(),
                    elapsed: std::time::Duration::ZERO,
                    peak_rss_kb: 0,
                    warning: None,
                    failure: Some(vd_deeplint::LintFailure::Io {
                        message: "the elaboration job panicked; its result was discarded"
                            .to_string(),
                    }),
                };
                failed.peak_rss_kb = 0;
                failed
            });
            if let Ok(mut slot) = worker_slot.report.lock() {
                *slot = Some(report);
            }
        });
    if spawned.is_err() {
        hierarchies().remove(&id);
        state.record_error(VdError::new(
            crate::capi::VD_ERR_INTERNAL,
            "cannot start a worker thread for the elaboration job",
        ));
        return 0;
    }
    id
}

/// Poll an elaboration job.
///
/// `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with `out` filled once it
/// finished; `VD_STATUS_FAILED` when the elaboration itself failed (the reason,
/// naming the tool and the stage, is readable with `vd_service_error`). The
/// result is delivered once.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdHierarchy`].
#[no_mangle]
pub unsafe extern "C" fn vd_hier_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdHierarchy,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = hierarchies().get(&job).cloned() else {
        state.record_error(VdError::new(
            VD_ERR_JOB_UNKNOWN,
            "unknown elaboration job id: it was never started or was released",
        ));
        return POLL_MISUSE;
    };
    let report = {
        let mut guard = match slot.report.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        guard.take()
    };
    let Some(report) = report else {
        return VD_STATUS_PENDING;
    };
    hierarchies().remove(&job);
    if let Some(failure) = &report.failure {
        state.record_error(VdError::new(
            crate::capi::VD_ERR_SERVICE,
            &format!("{} failed: {}", report.tool, failure.user_message()),
        ));
        return VD_STATUS_FAILED;
    }
    // SAFETY: `out` is writable for one VdHierarchy per the caller contract.
    unsafe { *out = hierarchy_to_c(&report) };
    VD_STATUS_READY
}

/// Release an elaboration job without delivering its result (null-safe for an
/// unknown id). A running job finishes into memory nobody reads.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_hier_release(service: *mut VdService, job: u64) -> i32 {
    let _ = service;
    i32::from(hierarchies().remove(&job).is_some())
}

/// Release a filled [`VdHierarchy`]; null-safe and idempotent.
///
/// # Safety
///
/// `hierarchy` must be null or point to a struct filled by [`vd_hier_poll`] and
/// not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_hierarchy_free(hierarchy: *mut VdHierarchy) {
    if hierarchy.is_null() {
        return;
    }
    let hierarchy = &mut *hierarchy;
    // SAFETY: every string was produced by this module and is not freed yet.
    unsafe {
        free_string(hierarchy.tool);
        free_string(hierarchy.top);
        free_string(hierarchy.warning);
        free_signal_lists(&mut hierarchy.signal_list);
        free_instance_list(&mut hierarchy.instances);
    }
    hierarchy.tool = ptr::null_mut();
    hierarchy.top = ptr::null_mut();
    hierarchy.warning = ptr::null_mut();
}

/// # Safety
///
/// The lists must come from [`hierarchy_to_c`] and not be freed yet.
unsafe fn free_instance_list(list: &mut VdHierInstanceList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdHierInstance]> that hierarchy_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for instance in slice.iter_mut() {
        // SAFETY: path/module/file were produced by c_string.
        unsafe {
            free_string(instance.path);
            free_string(instance.module);
            free_string(instance.file);
        }
        instance.path = ptr::null_mut();
        instance.module = ptr::null_mut();
        instance.file = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdHierInstance]>.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}

/// # Safety
///
/// The list must come from [`endpoints_to_c`] and not be freed yet.
unsafe fn free_endpoint_list(list: &mut VdHierEndpointList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdHierEndpoint]> that endpoints_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for endpoint in slice.iter_mut() {
        // SAFETY: path/file/detail were produced by c_string.
        unsafe {
            free_string(endpoint.path);
            free_string(endpoint.file);
            free_string(endpoint.detail);
        }
        endpoint.path = ptr::null_mut();
        endpoint.file = ptr::null_mut();
        endpoint.detail = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdHierEndpoint]>.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            list.items, list.count,
        )))
    };
    list.count = 0;
    list.items = ptr::null_mut();
}

/// # Safety
///
/// The lists must come from [`hierarchy_to_c`] and not be freed yet.
unsafe fn free_signal_lists(signals: &mut VdHierSignalList) {
    if signals.items.is_null() {
        signals.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdHierSignal]> that hierarchy_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(signals.items, signals.count) };
    for signal in slice.iter_mut() {
        // SAFETY: every string was produced by c_string or is null.
        unsafe {
            free_string(signal.path);
            free_string(signal.kind);
            free_string(signal.declaration_file);
            free_endpoint_list(&mut signal.drivers);
            free_endpoint_list(&mut signal.loads);
            free_endpoint_list(&mut signal.chain);
        }
        signal.path = ptr::null_mut();
        signal.kind = ptr::null_mut();
        signal.declaration_file = ptr::null_mut();
    }
    // SAFETY: reconstructs the same Box<[VdHierSignal]>.
    unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            signals.items,
            signals.count,
        )))
    };
    signals.count = 0;
    signals.items = ptr::null_mut();
}

#[cfg(test)]
mod tests {
    use super::{
        vd_deeplint_batch_cancel, vd_deeplint_batch_poll, vd_deeplint_batch_progress,
        vd_deeplint_batch_release, vd_deeplint_batch_start, vd_lint_command_line,
        vd_lint_progress_free,
    };
    use crate::capi::{
        vd_diagnostic_key, vd_string_free, VdDiagnostics, VdError, VdLintBatchOptions,
        VdLintProgress, VdStringList, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
    };
    use crate::service::{vd_service_create, vd_service_destroy};
    use std::ffi::{c_char, CStr, CString};
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::ptr;
    use std::time::{Duration, Instant};

    /// Owned C strings plus the `VdStringList` that borrows them; keeps both
    /// alive for the duration of a call.
    struct List {
        _owned: Vec<CString>,
        /// Keeps the pointer buffer `list.items` points into alive.
        _raw: Vec<*mut c_char>,
        list: VdStringList,
    }

    impl List {
        fn new(values: &[&str]) -> Self {
            let owned: Vec<CString> = values
                .iter()
                .map(|value| {
                    CString::new(*value).unwrap_or_else(|_| {
                        panic!("a fixture string must not contain an interior NUL")
                    })
                })
                .collect();
            let mut raw: Vec<*mut c_char> = owned
                .iter()
                .map(|value| value.as_ptr().cast_mut())
                .collect();
            let list = VdStringList {
                count: raw.len(),
                items: if raw.is_empty() {
                    ptr::null_mut()
                } else {
                    raw.as_mut_ptr()
                },
            };
            Self {
                _owned: owned,
                _raw: raw,
                list,
            }
        }

        fn as_ptr(&self) -> *const VdStringList {
            &raw const self.list
        }
    }

    fn c(text: &str) -> CString {
        CString::new(text).unwrap_or_else(|_| CString::default())
    }

    fn c_string(pointer: *mut c_char) -> String {
        // SAFETY: the caller passes a live string produced by this crate.
        unsafe { CStr::from_ptr(pointer) }
            .to_string_lossy()
            .into_owned()
    }

    fn set_executable(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mut permissions = fs::metadata(path)
                .unwrap_or_else(|error| panic!("stat failed: {error}"))
                .permissions();
            permissions.set_mode(0o755);
            fs::set_permissions(path, permissions).unwrap_or_else(|error| panic!("chmod: {error}"));
        }
    }

    fn script(dir: &Path, name: &str, body: &str) -> PathBuf {
        let path = dir.join(name);
        fs::write(&path, body).unwrap_or_else(|error| panic!("write failed: {error}"));
        set_executable(&path);
        path
    }

    fn tempdir(label: &str) -> tempfile::TempDir {
        tempfile::Builder::new()
            .prefix(label)
            .tempdir()
            .unwrap_or_else(|error| panic!("tempdir failed: {error}"))
    }

    #[test]
    fn the_slang_command_line_shows_the_project_settings() {
        let dirs = List::new(&["/proj/inc"]);
        let defines = List::new(&["WIDTH=8", "SIM"]);
        let file = c("/proj/top.sv");
        let binary = c("/bin/slang");
        // SAFETY: every pointer is valid for the duration of the call.
        let line = unsafe {
            vd_lint_command_line(
                0,
                binary.as_ptr(),
                file.as_ptr(),
                ptr::null(),
                dirs.as_ptr(),
                defines.as_ptr(),
            )
        };
        assert!(!line.is_null());
        // SAFETY: the returned pointer is a live string.
        let text = unsafe { CStr::from_ptr(line) }
            .to_string_lossy()
            .into_owned();
        // SAFETY: the string was produced by this crate and is not freed yet.
        unsafe { vd_string_free(line) };
        assert!(text.contains("engine=slang"), "{text}");
        assert!(text.contains("-I/proj/inc"), "{text}");
        assert!(text.contains("-DWIDTH=8"), "{text}");
        assert!(text.contains("-DSIM"), "{text}");
    }

    #[test]
    fn the_verible_command_line_states_that_it_takes_no_include_dirs() {
        let dirs = List::new(&["/proj/inc"]);
        let defines = List::new(&["SIM"]);
        let file = c("/proj/top.sv");
        let binary = c("/bin/verible-verilog-lint");
        // SAFETY: every pointer is valid for the duration of the call.
        let line = unsafe {
            vd_lint_command_line(
                1,
                binary.as_ptr(),
                file.as_ptr(),
                ptr::null(),
                dirs.as_ptr(),
                defines.as_ptr(),
            )
        };
        assert!(!line.is_null());
        // SAFETY: the returned pointer is a live string.
        let text = unsafe { CStr::from_ptr(line) }
            .to_string_lossy()
            .into_owned();
        // SAFETY: the string was produced by this crate and is not freed yet.
        unsafe { vd_string_free(line) };
        assert!(text.contains("engine=verible"), "{text}");
        assert!(!text.contains("-I/proj/inc"), "{text}");
        assert!(text.contains("accepts neither -I nor -D"), "{text}");
    }

    #[test]
    fn the_dedup_key_is_identical_for_an_lsp_and_a_cli_row() {
        let file = c("/proj/top.sv");
        let lsp_message = c("  Undeclared identifier   'x' ");
        let cli_message = c("Undeclared identifier 'x'");
        // SAFETY: every pointer is valid for the duration of the call.
        let lsp = unsafe { vd_diagnostic_key(1, file.as_ptr(), 7, 1, lsp_message.as_ptr()) };
        // SAFETY: same contract.
        let cli = unsafe { vd_diagnostic_key(2, file.as_ptr(), 7, 0, cli_message.as_ptr()) };
        assert!(!lsp.is_null() && !cli.is_null());
        // SAFETY: both pointers are live strings.
        let (lsp_text, cli_text) = unsafe {
            (
                CStr::from_ptr(lsp).to_string_lossy().into_owned(),
                CStr::from_ptr(cli).to_string_lossy().into_owned(),
            )
        };
        // SAFETY: both strings are unfreed crate allocations.
        unsafe {
            vd_string_free(lsp);
            vd_string_free(cli);
        }
        assert_eq!(lsp_text, cli_text);
        assert!(lsp_text.contains("Undeclared identifier 'x'"));
    }

    fn progress_of(service: *mut crate::service::VdService, job: u64) -> VdLintProgress {
        let mut progress = VdLintProgress {
            phase: -1,
            done: 0,
            total: 0,
            current: ptr::null_mut(),
            tool: ptr::null_mut(),
            stage: ptr::null_mut(),
            message: ptr::null_mut(),
        };
        // SAFETY: the service is live and `out` is writable.
        unsafe { vd_deeplint_batch_progress(service, job, &mut progress) };
        progress
    }

    fn free_progress(progress: &mut VdLintProgress) {
        // SAFETY: the struct was filled by vd_deeplint_batch_progress.
        unsafe { vd_lint_progress_free(progress) };
    }

    fn wait_terminal(service: *mut crate::service::VdService, job: u64) -> VdLintProgress {
        let deadline = Instant::now() + Duration::from_secs(20);
        loop {
            let progress = progress_of(service, job);
            if progress.phase >= 2 {
                return progress;
            }
            free_progress(&mut VdLintProgress { ..progress });
            assert!(
                Instant::now() < deadline,
                "the batch never reached a terminal phase"
            );
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    fn options(engine: i32, binary: &CString, timeout_ms: u64) -> VdLintBatchOptions {
        VdLintBatchOptions {
            engine,
            slang_binary: binary.as_ptr(),
            verible_binary: ptr::null(),
            rules_path: ptr::null(),
            include_dirs: VdStringList {
                count: 0,
                items: ptr::null_mut(),
            },
            defines: VdStringList {
                count: 0,
                items: ptr::null_mut(),
            },
            timeout_ms,
            max_diagnostics: 0,
            ruleset: 0,
            rules: ptr::null(),
            waiver_files: ptr::null(),
            slang_suppressions: ptr::null(),
            error_limit: 0,
        }
    }

    fn an_empty_report_script(dir: &Path) -> PathBuf {
        script(
            dir,
            "slang",
            "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '[]' > \"$out\"\nexit 0\n",
        )
    }

    #[test]
    fn a_batch_reports_progress_and_delivers_its_result_once() {
        let dir = tempdir("vd-ffi-batch-");
        let binary = c(script(dir.path(), "slang", "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '[]' > \"$out\"\nexit 0\n").to_str().unwrap_or_else(|| panic!("a temp path is UTF-8")));
        let mut files = Vec::new();
        for index in 0..2 {
            let file = dir.path().join(format!("f{index}.sv"));
            fs::write(&file, "module m;\nendmodule\n")
                .unwrap_or_else(|error| panic!("write: {error}"));
            files.push(file.to_string_lossy().into_owned());
        }
        let refs: Vec<&str> = files.iter().map(String::as_str).collect();
        let files_list = List::new(&refs);
        let options = options(0, &binary, 5_000);
        let service = vd_service_create();
        let job = unsafe { vd_deeplint_batch_start(service, files_list.as_ptr(), &options) };
        assert!(job >= (1u64 << 32), "batch ids are disjoint from job ids");
        let progress = wait_terminal(service, job);
        assert_eq!(progress.phase, 2, "finished");
        assert_eq!(progress.total, 2);
        assert_eq!(progress.done, 2);
        free_progress(&mut VdLintProgress { ..progress });

        let mut out = VdDiagnostics {
            count: 0,
            items: ptr::null_mut(),
        };
        // SAFETY: the service is live and `out` is writable.
        let status = unsafe { vd_deeplint_batch_poll(service, job, &mut out) };
        assert_eq!(status, VD_STATUS_READY);
        assert_eq!(out.count, 0);
        // SAFETY: a filled (empty) struct is safe to free.
        unsafe { crate::deeplint::vd_diagnostics_free(&mut out) };

        // A second poll reports the misuse instead of inventing a result.
        let mut again = VdDiagnostics {
            count: 0,
            items: ptr::null_mut(),
        };
        // SAFETY: the service is live and `out` is writable.
        let second = unsafe { vd_deeplint_batch_poll(service, job, &mut again) };
        assert_eq!(second, -1);
        // SAFETY: VdError is a plain C struct of integers and an array.
        let mut error = unsafe { std::mem::zeroed::<VdError>() };
        // SAFETY: the service is live and `out` is writable.
        unsafe { crate::service::vd_service_error(service, &mut error) };
        assert_eq!(error.code, crate::capi::VD_ERR_JOB_UNKNOWN);

        // SAFETY: the service is live; the id is ours.
        unsafe {
            assert_eq!(vd_deeplint_batch_release(service, job), 1);
            vd_service_destroy(service);
        }
        let _ = an_empty_report_script;
    }

    fn hierarchy_options(binary: &CString, top: &CString) -> crate::capi::VdHierOptions {
        crate::capi::VdHierOptions {
            slang_binary: binary.as_ptr(),
            top: top.as_ptr(),
            include_dirs: VdStringList {
                count: 0,
                items: ptr::null_mut(),
            },
            defines: VdStringList {
                count: 0,
                items: ptr::null_mut(),
            },
            timeout_ms: 30_000,
            max_instances: 0,
            max_files: 0,
        }
    }

    fn spike_fixture() -> std::path::PathBuf {
        std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../probes/slang-spike/fixture")
    }

    fn spike_slang() -> Option<CString> {
        let candidate = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../build/slang/out/slang");
        if candidate.exists() {
            CString::new(candidate.to_string_lossy().as_bytes()).ok()
        } else {
            None
        }
    }

    #[test]
    fn the_elaboration_job_returns_the_hierarchy_and_the_driver_load_positions() {
        let Some(slang) = spike_slang() else {
            eprintln!("SKIP: build/slang/out/slang is missing");
            return;
        };
        let mut files: Vec<String> = fs::read_dir(spike_fixture())
            .unwrap_or_else(|error| panic!("reading the fixture: {error}"))
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|path| path.extension().is_some_and(|extension| extension == "sv"))
            .map(|path| path.to_string_lossy().into_owned())
            .collect();
        files.sort();
        let refs: Vec<&str> = files.iter().map(String::as_str).collect();
        let files_list = List::new(&refs);
        let top = CString::new("top").unwrap_or_default();
        let options = hierarchy_options(&slang, &top);
        let service = vd_service_create();
        // SAFETY: the service is live and every pointer is valid.
        let job = unsafe { super::vd_hier_start(service, files_list.as_ptr(), &options) };
        assert!(
            job >= (1u64 << 48),
            "hierarchy ids have their own range: {job}"
        );

        let deadline = Instant::now() + Duration::from_secs(60);
        let mut out = crate::capi::VdHierarchy {
            tool: ptr::null_mut(),
            top: ptr::null_mut(),
            elapsed_ms: 0,
            peak_rss_kb: 0,
            warning: ptr::null_mut(),
            instances: crate::capi::VdHierInstanceList {
                count: 0,
                items: ptr::null_mut(),
            },
            signal_list: crate::capi::VdHierSignalList {
                count: 0,
                items: ptr::null_mut(),
            },
        };
        let status = loop {
            // SAFETY: the service is live and `out` is writable.
            let status = unsafe { super::vd_hier_poll(service, job, &mut out) };
            if status != VD_STATUS_PENDING {
                break status;
            }
            assert!(Instant::now() < deadline, "the elaboration never finished");
            std::thread::sleep(Duration::from_millis(10));
        };
        assert_eq!(status, VD_STATUS_READY);
        // SAFETY: the arrays were filled by the poll above.
        let instances =
            unsafe { std::slice::from_raw_parts(out.instances.items, out.instances.count) };
        let paths: Vec<String> = instances.iter().map(|node| c_string(node.path)).collect();
        assert_eq!(paths, vec!["top", "top.u0", "top.u1"]);
        assert_eq!(c_string(instances[1].module), "mid");
        assert!(c_string(instances[1].file).ends_with("top.sv"));
        assert_eq!(instances[1].line, 10, "0-based instantiation line");

        // SAFETY: the arrays were filled by the poll above.
        let signals =
            unsafe { std::slice::from_raw_parts(out.signal_list.items, out.signal_list.count) };
        let link = signals
            .iter()
            .find(|signal| c_string(signal.path) == "top.link")
            .unwrap_or_else(|| panic!("top.link is reported"));
        // SAFETY: the nested arrays were filled by the same poll.
        let drivers = unsafe { std::slice::from_raw_parts(link.drivers.items, link.drivers.count) };
        assert_eq!(c_string(drivers[0].path), "top.u0.q");
        assert_eq!(drivers[0].kind, 0, "instance-output-port");
        // SAFETY: same.
        let loads = unsafe { std::slice::from_raw_parts(link.loads.items, link.loads.count) };
        let load_paths: Vec<String> = loads.iter().map(|load| c_string(load.path)).collect();
        assert!(
            load_paths.iter().any(|path| path == "top.u1.d"),
            "{load_paths:?}"
        );
        // SAFETY: same.
        let chain = unsafe { std::slice::from_raw_parts(link.chain.items, link.chain.count) };
        assert!(
            chain
                .iter()
                .any(|step| step.kind == 3 && c_string(step.detail) == "AlwaysFF"),
            "the chain reaches the always_ff"
        );
        assert!(out.elapsed_ms < 30_000, "{} ms", out.elapsed_ms);
        assert!(out.peak_rss_kb > 0);
        assert!(!out.tool.is_null());
        println!(
            "hierarchy job: {job}, {} instance(s), {} net(s), {} ms, {} KiB peak",
            out.instances.count, out.signal_list.count, out.elapsed_ms, out.peak_rss_kb
        );
        // A machine-readable dump of exactly what the C side receives.
        let instances_json: Vec<serde_json::Value> = instances
            .iter()
            .map(|node| {
                serde_json::json!({
                    "path": c_string(node.path),
                    "module": c_string(node.module),
                    "file": c_string(node.file),
                    "line": node.line,
                    "column": node.column,
                })
            })
            .collect();
        let signals_json: Vec<serde_json::Value> = signals
            .iter()
            .map(|signal| {
                // SAFETY: the nested arrays were filled by the same poll.
                let endpoints = |list: &crate::capi::VdHierEndpointList| -> Vec<serde_json::Value> {
                    if list.items.is_null() {
                        return Vec::new();
                    }
                    // SAFETY: the list came from hierarchy_to_c.
                    let slice = unsafe { std::slice::from_raw_parts(list.items, list.count) };
                    slice
                        .iter()
                        .map(|endpoint| {
                            serde_json::json!({
                                "path": c_string(endpoint.path),
                                "file": c_string(endpoint.file),
                                "line": endpoint.line,
                                "column": endpoint.column,
                                "kind": endpoint.kind,
                                "detail": c_string(endpoint.detail),
                            })
                        })
                        .collect()
                };
                serde_json::json!({
                    "path": c_string(signal.path),
                    "kind": c_string(signal.kind),
                    "drivers": endpoints(&signal.drivers),
                    "loads": endpoints(&signal.loads),
                    "chain": endpoints(&signal.chain),
                })
            })
            .collect();
        println!(
            "VD_HIERARCHY_JSON={}",
            serde_json::json!({
                "tool": c_string(out.tool),
                "top": if out.top.is_null() { serde_json::Value::Null } else { serde_json::Value::String(c_string(out.top)) },
                "elapsed_ms": out.elapsed_ms,
                "peak_rss_kb": out.peak_rss_kb,
                "instances": instances_json,
                "signals": signals_json,
            })
        );
        // SAFETY: the struct was filled by the poll above and is not freed yet.
        unsafe { super::vd_hierarchy_free(&mut out) };
        // SAFETY: the service is live; the id was consumed by the poll.
        unsafe {
            assert_eq!(super::vd_hier_release(service, job), 0);
            vd_service_destroy(service);
        }
    }

    #[test]
    fn a_missing_slang_binary_fails_the_elaboration_with_its_tool_name() {
        let dir = tempdir("vd-ffi-hier-");
        let file = dir.path().join("top.sv");
        fs::write(&file, "module top;\nendmodule\n")
            .unwrap_or_else(|error| panic!("write: {error}"));
        let refs = [file.to_string_lossy().into_owned()];
        let refs: Vec<&str> = refs.iter().map(String::as_str).collect();
        let files_list = List::new(&refs);
        let binary = CString::new("/nonexistent/slang").unwrap_or_default();
        let top = CString::new("top").unwrap_or_default();
        let options = hierarchy_options(&binary, &top);
        let service = vd_service_create();
        // SAFETY: the service is live and every pointer is valid.
        let job = unsafe { super::vd_hier_start(service, files_list.as_ptr(), &options) };
        assert!(job >= (1u64 << 48));
        let deadline = Instant::now() + Duration::from_secs(20);
        let mut out = crate::capi::VdHierarchy {
            tool: ptr::null_mut(),
            top: ptr::null_mut(),
            elapsed_ms: 0,
            peak_rss_kb: 0,
            warning: ptr::null_mut(),
            instances: crate::capi::VdHierInstanceList {
                count: 0,
                items: ptr::null_mut(),
            },
            signal_list: crate::capi::VdHierSignalList {
                count: 0,
                items: ptr::null_mut(),
            },
        };
        let status = loop {
            // SAFETY: the service is live and `out` is writable.
            let status = unsafe { super::vd_hier_poll(service, job, &mut out) };
            if status != VD_STATUS_PENDING {
                break status;
            }
            assert!(Instant::now() < deadline, "the job never finished");
            std::thread::sleep(Duration::from_millis(10));
        };
        assert_eq!(status, VD_STATUS_FAILED);
        // SAFETY: VdError is a plain C struct of integers and an array.
        let mut error = unsafe { std::mem::zeroed::<VdError>() };
        // SAFETY: the service is live and `out` is writable.
        unsafe { crate::service::vd_service_error(service, &mut error) };
        assert_eq!(error.code, crate::capi::VD_ERR_SERVICE);
        // SAFETY: the service was created above.
        unsafe { vd_service_destroy(service) };
    }

    #[test]
    fn a_batch_cancel_really_stops_it_and_delivers_no_results() {
        let dir = tempdir("vd-ffi-cancel-");
        let binary = c(script(dir.path(), "slang", "#!/bin/sh\nexec sleep 30\n")
            .to_str()
            .unwrap_or_else(|| panic!("a temp path is UTF-8")));
        let file = dir.path().join("slow.sv");
        fs::write(&file, "module m;\nendmodule\n").unwrap_or_else(|error| panic!("write: {error}"));
        let refs = [file.to_string_lossy().into_owned()];
        let refs: Vec<&str> = refs.iter().map(String::as_str).collect();
        let files_list = List::new(&refs);
        let options = options(0, &binary, 30_000);
        let service = vd_service_create();
        let job = unsafe { vd_deeplint_batch_start(service, files_list.as_ptr(), &options) };
        assert!(job >= (1u64 << 32));

        // Wait until the worker is running, then cancel.
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let progress = progress_of(service, job);
            if progress.phase == 1 {
                free_progress(&mut VdLintProgress { ..progress });
                break;
            }
            free_progress(&mut VdLintProgress { ..progress });
            assert!(Instant::now() < deadline, "the batch never started");
            std::thread::sleep(Duration::from_millis(5));
        }
        // SAFETY: the service is live; the id is ours.
        assert_eq!(unsafe { vd_deeplint_batch_cancel(service, job) }, 1);

        let progress = wait_terminal(service, job);
        assert_eq!(progress.phase, 3, "cancelled");
        assert!(progress.done <= progress.total);
        free_progress(&mut VdLintProgress { ..progress });

        let mut out = VdDiagnostics {
            count: 0,
            items: ptr::null_mut(),
        };
        // SAFETY: the service is live and `out` is writable.
        let deadline = Instant::now() + Duration::from_secs(10);
        let status = loop {
            let status = unsafe { vd_deeplint_batch_poll(service, job, &mut out) };
            if status != crate::capi::VD_STATUS_PENDING {
                break status;
            }
            assert!(
                Instant::now() < deadline,
                "the cancelled pass never delivered"
            );
            std::thread::sleep(Duration::from_millis(5));
        };
        assert_eq!(status, VD_STATUS_READY);
        assert_eq!(out.count, 0, "no diagnostics after a cancel");
        // SAFETY: a filled (empty) struct is safe to free.
        unsafe { crate::deeplint::vd_diagnostics_free(&mut out) };

        // SAFETY: the service is live; the id is ours.
        unsafe {
            vd_deeplint_batch_release(service, job);
            vd_service_destroy(service);
        }
    }
}
