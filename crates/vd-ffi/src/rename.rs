//! Engineering-wide rename (batch 20261110, the `rename-core` lane's transport).
//!
//! Three jobs, in the order the command uses them:
//!
//! 1. **index** ([`vd_rename_index_start`]) — parse the project's file set and
//!    answer "how many files, how many identifiers, how many occurrences of the
//!    symbol". The command shows that before it commits to a plan.
//! 2. **plan** ([`vd_rename_plan_start`]) — the preview: one entry per file, and
//!    inside it one entry per replacement site with the line before and after.
//!    It re-indexes internally, so the caller never has to keep the index alive.
//! 3. **apply** ([`vd_rename_apply_start`]) — write every planned file, or none:
//!    a failure restores every file already written and names the one that
//!    failed. `dry_run` runs the same checks without writing.
//!
//! All three are cancellable and carry a wall-clock budget; the apply job is the
//! one that must honour the cancel flag mid-run, because it writes files.
//!
//! # This is the frozen surface, wired to the engine
//!
//! The C ABI is the frozen surface (batch 20261110); the engine behind it is
//! `vd-deeplint`'s AST index plus the atomic applier. Every job calls that
//! engine: an index/plan refusal is mapped to the typed error below (input
//! errors stay [`VD_ERR_INVALID_ARGUMENT`], a missing binary or a path/IO
//! failure is [`VD_ERR_SERVICE`]), and an apply either rewrites every file or
//! restores every byte. It never pretends to have renamed anything, never falls
//! back to text replacement, and never panics across the boundary.
//!
//! # Identifiers only
//!
//! The ABI validates `from`/`to` as SystemVerilog identifiers and refuses anything
//! else, and it refuses `from == to` with an explicit "this is a no-op" reason
//! rather than starting a job that would apply zero replacements.

use crate::capi::{
    c_str, c_string, c_string_list, free_string, free_string_list, VdError, VdRenameApplyResult,
    VdRenameFilePlan, VdRenameHit, VdRenameIndex, VdRenameOptions, VdRenamePlan, VdStringList,
    VD_ERR_CANCELLED, VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN, VD_ERR_SERVICE,
    VD_ERR_WRONG_JOB_KIND, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::jobgate::{Registry, Slot, Taken, LANE_RENAME};
use crate::service::{service_state, unknown_job, ServiceState, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::ptr;

/// Default number of preview entries kept per file.
const DEFAULT_ENTRIES_PER_FILE: usize = 200;

/// Index jobs of this process.
static INDEX_JOBS: Registry<RenameIndexData> = Registry::new();
/// Plan jobs of this process.
static PLAN_JOBS: Registry<RenamePlanData> = Registry::new();
/// Apply jobs of this process.
static APPLY_JOBS: Registry<RenameApplyData> = Registry::new();

// The payloads below are what the jobs hand to the frozen C ABI, and the engine
// fills every one of them. The `#[allow(dead_code)]` markers are the
// frozen-surface guard: the field set is fixed by the ABI, so an unread field is
// a review question rather than a build error.

/// What the index phase found.
#[allow(dead_code)]
pub(crate) struct RenameIndexData {
    pub(crate) files: usize,
    pub(crate) identifiers: usize,
    pub(crate) occurrences: usize,
    pub(crate) warnings: Vec<String>,
}

#[allow(dead_code)]
impl RenameIndexData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_rename_index_free`].
    pub(crate) unsafe fn into_c(self) -> VdRenameIndex {
        VdRenameIndex {
            files: self.files,
            identifiers: self.identifiers,
            occurrences: self.occurrences,
            warnings: unsafe { c_string_list(&self.warnings) },
        }
    }
}

/// One planned replacement site.
#[allow(dead_code)]
pub(crate) struct RenameHitData {
    pub(crate) line: u32,
    pub(crate) column: u32,
    pub(crate) end_column: u32,
    pub(crate) byte_offset: u64,
    pub(crate) byte_len: u64,
    pub(crate) before: String,
    pub(crate) after: String,
}

/// The preview of one file.
#[allow(dead_code)]
pub(crate) struct RenameFilePlanData {
    pub(crate) file: String,
    pub(crate) hits: Vec<RenameHitData>,
    pub(crate) truncated: bool,
}

/// A rename plan.
#[allow(dead_code)]
pub(crate) struct RenamePlanData {
    pub(crate) root: String,
    pub(crate) from: String,
    pub(crate) to: String,
    pub(crate) files: usize,
    pub(crate) occurrences: usize,
    pub(crate) plans: Vec<RenameFilePlanData>,
    pub(crate) warnings: Vec<String>,
}

#[allow(dead_code)]
impl RenamePlanData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_rename_plan_free`].
    pub(crate) unsafe fn into_c(self) -> VdRenamePlan {
        let mut files: Vec<VdRenameFilePlan> = self
            .plans
            .iter()
            .map(|plan| {
                let hits: Vec<VdRenameHit> = plan
                    .hits
                    .iter()
                    .map(|hit| VdRenameHit {
                        line: hit.line,
                        column: hit.column,
                        end_column: hit.end_column,
                        byte_offset: hit.byte_offset,
                        byte_len: hit.byte_len,
                        before: c_string(&hit.before),
                        after: c_string(&hit.after),
                    })
                    .collect();
                let items = if hits.is_empty() {
                    ptr::null_mut()
                } else {
                    Box::into_raw(hits.into_boxed_slice()) as *mut VdRenameHit
                };
                VdRenameFilePlan {
                    file: c_string(&plan.file),
                    count: plan.hits.len(),
                    items,
                    truncated: u8::from(plan.truncated),
                }
            })
            .collect();
        files.shrink_to_fit();
        let items = if files.is_empty() {
            ptr::null_mut()
        } else {
            Box::into_raw(files.into_boxed_slice()) as *mut VdRenameFilePlan
        };
        VdRenamePlan {
            root: c_string(&self.root),
            from: c_string(&self.from),
            to: c_string(&self.to),
            files: self.files,
            occurrences: self.occurrences,
            count: self.plans.len(),
            items,
            warnings: unsafe { c_string_list(&self.warnings) },
        }
    }
}

/// The result of one apply.
#[allow(dead_code)]
pub(crate) struct RenameApplyData {
    pub(crate) applied_files: usize,
    pub(crate) applied_occurrences: usize,
    pub(crate) failed_file: Option<String>,
    pub(crate) rolled_back: bool,
    pub(crate) verified: bool,
}

#[allow(dead_code)]
impl RenameApplyData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_rename_apply_result_free`].
    pub(crate) unsafe fn into_c(self) -> VdRenameApplyResult {
        VdRenameApplyResult {
            applied_files: self.applied_files,
            applied_occurrences: self.applied_occurrences,
            failed_file: self
                .failed_file
                .as_deref()
                .map_or(ptr::null_mut(), c_string),
            rolled_back: u8::from(self.rolled_back),
            verified: u8::from(self.verified),
        }
    }
}

/// A validated index request: exactly what the index engine receives.
///
/// `#[allow(dead_code)]`: the frozen surface fixes the field set, so an unread
/// field must stay a review question, not a build error.
#[allow(dead_code)]
struct IndexRequest {
    root: String,
    files: Vec<String>,
    /// The project expansion's effective `-I` directories (T72).
    include_dirs: Vec<String>,
    /// `-D` defines in `NAME` / `NAME=VALUE` form (T72).
    defines: Vec<String>,
    timeout_ms: u64,
}

/// A validated plan or apply request.
///
/// `#[allow(dead_code)]`: the frozen surface fixes the field set, so an unread
/// field must stay a review question, not a build error.
#[allow(dead_code)]
struct RenameRequest {
    root: String,
    files: Vec<String>,
    from: String,
    to: String,
    dry_run: bool,
    max_entries_per_file: usize,
    /// The project expansion's effective `-I` directories (T72). The plan and
    /// apply jobs RE-RUN the index, so both need them.
    include_dirs: Vec<String>,
    /// `-D` defines in `NAME` / `NAME=VALUE` form (T72).
    defines: Vec<String>,
    timeout_ms: u64,
}

/// The validated option half of a request.
#[derive(Clone)]
struct RenameOptions {
    dry_run: bool,
    max_entries_per_file: usize,
    /// The project expansion's effective `-I` directories (T72). Read from the
    /// ABI's string list, so they are OWNED here.
    include_dirs: Vec<String>,
    /// `-D` defines in `NAME` / `NAME=VALUE` form (T72).
    defines: Vec<String>,
    timeout_ms: u64,
}

impl Default for RenameOptions {
    fn default() -> Self {
        Self {
            dry_run: false,
            max_entries_per_file: DEFAULT_ENTRIES_PER_FILE,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout_ms: 0,
        }
    }
}

/// Map one engine failure onto the C ABI's typed error.
///
/// Every rename refusal is the caller's input (an ambiguous name, an illegal new
/// name, a conflict, a no-op) rather than a service fault, so they all surface as
/// [`VD_ERR_INVALID_ARGUMENT`] with the engine's one-line reason; a missing
/// binary, an AST failure or a path/IO failure is [`VD_ERR_SERVICE`]. Nothing
/// here ever degrades to a text replacement.
fn engine_error(error: vd_deeplint::RenameError) -> VdError {
    let code = match error {
        vd_deeplint::RenameError::BinaryNotFound { .. }
        | vd_deeplint::RenameError::Ast { .. }
        | vd_deeplint::RenameError::Io { .. } => VD_ERR_SERVICE,
        _ => VD_ERR_INVALID_ARGUMENT,
    };
    VdError::new(code, &format!("{}: {}", error.code(), error.message()))
}

/// Build the engine's project from the ABI request.
///
/// The caller supplies the EXPANDED file set (the project model already appends
/// the tools-page `-I`/`-D` when it expands a filelist); an empty set is an
/// explicit "no project", never a guess at a directory.
fn engine_project(
    root: &str,
    files: &[String],
    include_dirs: &[String],
    defines: &[String],
    timeout_ms: u64,
) -> Result<vd_deeplint::RenameProject, VdError> {
    if files.is_empty() {
        return Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "no project: the rename file set is empty (open a project or pass --project)",
        ));
    }
    let mut project =
        vd_deeplint::RenameProject::new(root, files.iter().map(std::path::PathBuf::from).collect());
    // T72: the elaboration needs the project's own `-I`/`-D`. They are the
    // expansion's EFFECTIVE lists (the same ones lint/LSP children receive), so
    // a project that includes a header from `inc/` can be indexed and renamed.
    project.include_dirs = include_dirs.iter().map(std::path::PathBuf::from).collect();
    project.defines = defines.to_vec();
    if timeout_ms > 0 {
        project.timeout = std::time::Duration::from_millis(timeout_ms);
    }
    Ok(project)
}

/// Engine entry of the index job.
fn index(request: &IndexRequest, slot: &Slot<RenameIndexData>) -> Result<RenameIndexData, VdError> {
    let project = engine_project(
        &request.root,
        &request.files,
        &request.include_dirs,
        &request.defines,
        request.timeout_ms,
    )?;
    if slot.is_cancelled() {
        return Err(VdError::new(
            VD_ERR_CANCELLED,
            "the rename index was cancelled",
        ));
    }
    let index = vd_deeplint::rename_index(&project).map_err(engine_error)?;
    let summary = index.summary();
    Ok(RenameIndexData {
        files: summary.files,
        identifiers: summary.identifiers,
        occurrences: summary.occurrences,
        warnings: summary.warnings,
    })
}

/// Engine entry of the plan job.
fn plan(request: &RenameRequest, slot: &Slot<RenamePlanData>) -> Result<RenamePlanData, VdError> {
    let project = engine_project(
        &request.root,
        &request.files,
        &request.include_dirs,
        &request.defines,
        request.timeout_ms,
    )?;
    if slot.is_cancelled() {
        return Err(VdError::new(
            VD_ERR_CANCELLED,
            "the rename plan was cancelled",
        ));
    }
    let index = vd_deeplint::rename_index(&project).map_err(engine_error)?;
    match vd_deeplint::rename_plan(
        &project,
        &index,
        &request.from,
        &request.to,
        request.max_entries_per_file,
    ) {
        Ok(vd_deeplint::PlanOutcome::Noop { name }) => Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("`{name}` is already the current name: this is an explicit no-op"),
        )),
        Ok(vd_deeplint::PlanOutcome::Plan(plan)) => Ok(plan_data(plan)),
        Err(error) => Err(engine_error(error)),
    }
}

fn plan_data(plan: vd_deeplint::RenamePlan) -> RenamePlanData {
    RenamePlanData {
        root: plan.root.display().to_string(),
        from: plan.from,
        to: plan.to,
        files: plan.files,
        occurrences: plan.occurrences,
        plans: plan
            .plans
            .into_iter()
            .map(|file| RenameFilePlanData {
                file: file.file.display().to_string(),
                truncated: file.truncated,
                hits: file
                    .hits
                    .into_iter()
                    .map(|hit| RenameHitData {
                        line: hit.line,
                        column: hit.column,
                        end_column: hit.end_column,
                        byte_offset: hit.byte_offset,
                        byte_len: hit.byte_len,
                        before: hit.before,
                        after: hit.after,
                    })
                    .collect(),
            })
            .collect(),
        warnings: plan.warnings,
    }
}

/// Engine entry of the apply job.
fn apply(
    request: &RenameRequest,
    slot: &Slot<RenameApplyData>,
) -> Result<RenameApplyData, VdError> {
    let project = engine_project(
        &request.root,
        &request.files,
        &request.include_dirs,
        &request.defines,
        request.timeout_ms,
    )?;
    let index = vd_deeplint::rename_index(&project).map_err(engine_error)?;
    let plan = match vd_deeplint::rename_plan(
        &project,
        &index,
        &request.from,
        &request.to,
        request.max_entries_per_file,
    ) {
        Ok(vd_deeplint::PlanOutcome::Noop { name }) => {
            return Err(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                &format!("`{name}` is already the current name: this is an explicit no-op"),
            ))
        }
        Ok(vd_deeplint::PlanOutcome::Plan(plan)) => plan,
        Err(error) => return Err(engine_error(error)),
    };
    let outcome = vd_deeplint::apply(&plan, request.dry_run, &|| slot.is_cancelled());
    Ok(RenameApplyData {
        applied_files: outcome.applied_files,
        applied_occurrences: outcome.applied_occurrences,
        failed_file: outcome.failed_file.map(|file| file.display().to_string()),
        rolled_back: outcome.rolled_back,
        verified: outcome.verified,
    })
}

/// Whether `text` is a whole SystemVerilog identifier.
fn is_identifier(text: &str) -> bool {
    let mut characters = text.chars();
    let Some(first) = characters.next() else {
        return false;
    };
    if first != '_' && first != '$' && !first.is_ascii_alphabetic() {
        return false;
    }
    characters
        .all(|character| character == '_' || character == '$' || character.is_ascii_alphanumeric())
}

/// Read and validate [`VdRenameOptions`] (null selects every default).
///
/// # Safety
///
/// `options` must be null or readable for the duration of the call.
unsafe fn read_options(options: *const VdRenameOptions) -> Result<RenameOptions, String> {
    if options.is_null() {
        return Ok(RenameOptions::default());
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let options = unsafe { *options };
    if options.whole_identifier == 0 {
        return Err(
            "whole_identifier must be 1: this ABI renames identifiers only and never falls back \
             to text replacement"
                .to_string(),
        );
    }
    // T72: the project's effective `-I`/`-D`, the same inputs the lint children
    // get. An empty list is the documented no-op (a project that needs an
    // include dir must pass it); a malformed list is refused like any other
    // invalid argument.
    // SAFETY: the caller contract guarantees both lists are readable for the call.
    let include_dirs = unsafe { crate::deeplint::read_string_list(&options.include_dirs) }?;
    // SAFETY: same contract.
    let defines = unsafe { crate::deeplint::read_string_list(&options.defines) }?;
    Ok(RenameOptions {
        dry_run: options.dry_run != 0,
        max_entries_per_file: if options.max_entries_per_file == 0 {
            DEFAULT_ENTRIES_PER_FILE
        } else {
            options.max_entries_per_file
        },
        include_dirs,
        defines,
        timeout_ms: options.timeout_ms,
    })
}

/// Read and validate the project root and the file set.
///
/// # Safety
///
/// `root` must be null or a valid NUL-terminated string; `files` must be null or
/// satisfy the string-list contract, for the duration of the call.
unsafe fn read_targets(
    root: *const c_char,
    files: *const VdStringList,
) -> Result<(String, Vec<String>), String> {
    // SAFETY: the caller contract is stated above.
    let root = match unsafe { c_str(root) } {
        Some(text) if !text.is_empty() => text.to_string(),
        _ => return Err("root must be a non-empty project path".to_string()),
    };
    // SAFETY: a null list is an empty list; otherwise it satisfies the contract.
    let files = unsafe { crate::deeplint::read_string_list(files) }?;
    if files.is_empty() {
        return Err(
            "the project file set is empty: there is nothing to index, so there is nothing to \
             rename"
                .to_string(),
        );
    }
    if files.iter().any(String::is_empty) {
        return Err("every entry of the project file set must be a non-empty path".to_string());
    }
    Ok((root, files))
}

/// Read and validate the two identifiers.
///
/// # Safety
///
/// `from` and `to` must be null or valid NUL-terminated strings.
unsafe fn read_identifiers(
    from: *const c_char,
    to: *const c_char,
) -> Result<(String, String), String> {
    // SAFETY: the caller contract is stated above.
    let from = match unsafe { c_str(from) } {
        Some(text) => text.to_string(),
        None => return Err("from must not be null: a rename needs a symbol".to_string()),
    };
    // SAFETY: same contract.
    let to = match unsafe { c_str(to) } {
        Some(text) => text.to_string(),
        None => return Err("to must not be null: a rename needs a new name".to_string()),
    };
    if !is_identifier(&from) {
        return Err(format!(
            "from `{from}` is not a SystemVerilog identifier: only whole identifiers can be renamed"
        ));
    }
    if !is_identifier(&to) {
        return Err(format!(
            "to `{to}` is not a SystemVerilog identifier: the new name must be a name"
        ));
    }
    if from == to {
        return Err(format!(
            "the new name `{to}` is identical to the current name: the rename is a no-op"
        ));
    }
    Ok((from, to))
}

/// Record a poll that named a job of ANOTHER rename kind.
fn rename_unknown(state: &ServiceState, foreign: bool) -> i32 {
    if foreign {
        state.record_error(VdError::new(
            VD_ERR_WRONG_JOB_KIND,
            "this rename job id belongs to a different rename job kind (index / plan / apply)",
        ));
        return POLL_MISUSE;
    }
    unknown_job(state)
}

/// Record a poll of an already-taken rename result.
fn rename_consumed(state: &ServiceState) -> i32 {
    state.record_error(VdError::new(
        VD_ERR_JOB_UNKNOWN,
        "the rename result was already taken or the job id is unknown",
    ));
    POLL_MISUSE
}

/// Start one index job. Returns the job id, or `0` when the arguments were
/// rejected (the reason is then readable through `vd_service_error`).
///
/// # Safety
///
/// `service` must be null or live; every pointer must be null or valid for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_index_start(
    service: *mut VdService,
    root: *const c_char,
    files: *const VdStringList,
    options: *const VdRenameOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: each helper states its own contract.
    let request = match unsafe {
        read_targets(root, files).and_then(|(root, files)| {
            read_options(options).map(|options| IndexRequest {
                root,
                files,
                include_dirs: options.include_dirs,
                defines: options.defines,
                timeout_ms: options.timeout_ms,
            })
        })
    } {
        Ok(request) => request,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    INDEX_JOBS.start(LANE_RENAME, request.timeout_ms, move |slot| {
        index(&request, slot)
    })
}

/// Poll one index job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdRenameIndex`].
#[no_mangle]
pub unsafe extern "C" fn vd_rename_index_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdRenameIndex,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = INDEX_JOBS.get(job) else {
        return rename_unknown(
            state,
            PLAN_JOBS.get(job).is_some() || APPLY_JOBS.get(job).is_some(),
        );
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdRenameIndex.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => rename_consumed(state),
    }
}

/// Start one plan job and return the preview. Same argument contract as
/// [`vd_rename_index_start`], plus the two identifiers.
///
/// # Safety
///
/// `service` must be null or live; every pointer must be null or valid for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_plan_start(
    service: *mut VdService,
    root: *const c_char,
    files: *const VdStringList,
    from: *const c_char,
    to: *const c_char,
    options: *const VdRenameOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: each helper states its own contract.
    let request = match unsafe {
        read_targets(root, files).and_then(|(root, files)| {
            read_identifiers(from, to).and_then(|(from, to)| {
                read_options(options).map(|options| RenameRequest {
                    root,
                    files,
                    from,
                    to,
                    dry_run: options.dry_run,
                    max_entries_per_file: options.max_entries_per_file,
                    include_dirs: options.include_dirs,
                    defines: options.defines,
                    timeout_ms: options.timeout_ms,
                })
            })
        })
    } {
        Ok(request) => request,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    PLAN_JOBS.start(LANE_RENAME, request.timeout_ms, move |slot| {
        plan(&request, slot)
    })
}

/// Poll one plan job. The plan is a deep structure: release it with
/// [`vd_rename_plan_free`].
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdRenamePlan`].
#[no_mangle]
pub unsafe extern "C" fn vd_rename_plan_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdRenamePlan,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = PLAN_JOBS.get(job) else {
        return rename_unknown(
            state,
            INDEX_JOBS.get(job).is_some() || APPLY_JOBS.get(job).is_some(),
        );
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdRenamePlan.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => rename_consumed(state),
    }
}

/// Start one apply job for a plan produced by [`vd_rename_plan_poll`].
///
/// The plan is READ, never consumed: the caller keeps ownership and releases it
/// with [`vd_rename_plan_free`] after this call returns.
///
/// # Safety
///
/// `service` must be null or live; `plan` must be null or point to a plan that was
/// filled by [`vd_rename_plan_poll`] and not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_apply_start(
    service: *mut VdService,
    plan: *const VdRenamePlan,
    options: *const VdRenameOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    if plan.is_null() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "plan must not be null: an apply needs a plan to apply",
        ));
        return 0;
    }
    // SAFETY: the caller contract guarantees a readable plan.
    let plan = unsafe { *plan };
    // SAFETY: every pointer in `plan` is null or valid per its contract.
    let (Some(root), Some(from), Some(to)) = (
        unsafe { c_str(plan.root) },
        unsafe { c_str(plan.from) },
        unsafe { c_str(plan.to) },
    ) else {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "the plan must carry root, from and to",
        ));
        return 0;
    };
    if plan.count == 0 || plan.items.is_null() {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "the plan is empty: there is nothing to apply",
        ));
        return 0;
    }
    // SAFETY: `options` is null or readable.
    let options = match unsafe { read_options(options) } {
        Ok(options) => options,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let mut files: Vec<String> = Vec::with_capacity(plan.count);
    // SAFETY: `items` points to `count` entries per the plan's contract.
    let entries = unsafe { std::slice::from_raw_parts(plan.items, plan.count) };
    for entry in entries {
        // SAFETY: every `file` is null or a valid NUL-terminated string.
        let Some(file) = (unsafe { c_str(entry.file) }) else {
            state.record_error(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                "every planned file must carry a valid UTF-8 path",
            ));
            return 0;
        };
        files.push(file.to_string());
    }
    let request = RenameRequest {
        root: root.to_string(),
        files,
        from: from.to_string(),
        to: to.to_string(),
        dry_run: options.dry_run,
        max_entries_per_file: options.max_entries_per_file,
        // T72: the apply job re-runs the index, so it needs the same `-I`/`-D`
        // the plan used; the caller passes the current expansion's lists.
        include_dirs: options.include_dirs,
        defines: options.defines,
        timeout_ms: options.timeout_ms,
    };
    APPLY_JOBS.start(LANE_RENAME, request.timeout_ms, move |slot| {
        apply(&request, slot)
    })
}

/// Poll one apply job.
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdRenameApplyResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_rename_apply_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdRenameApplyResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = APPLY_JOBS.get(job) else {
        return rename_unknown(
            state,
            INDEX_JOBS.get(job).is_some() || PLAN_JOBS.get(job).is_some(),
        );
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdRenameApplyResult.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => rename_consumed(state),
    }
}

/// Cancel one rename job of any of the three kinds. The apply job writes files,
/// so its engine checks the flag between files and rolls back what it wrote.
///
/// Returns `1` when the job was live, `0` when the id was unknown.
///
/// # Safety
///
/// `service` must be null or live; only null-safe access happens here.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_cancel(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    if let Some(slot) = INDEX_JOBS.get(job) {
        slot.cancel();
        return 1;
    }
    if let Some(slot) = PLAN_JOBS.get(job) {
        slot.cancel();
        return 1;
    }
    if let Some(slot) = APPLY_JOBS.get(job) {
        slot.cancel();
        return 1;
    }
    0
}

/// Drop one rename job of any kind without reporting an error.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_release(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    let dropped = INDEX_JOBS.remove(job).is_some()
        || PLAN_JOBS.remove(job).is_some()
        || APPLY_JOBS.remove(job).is_some();
    i32::from(dropped)
}

/// Release a filled [`VdRenameIndex`]; null-safe.
///
/// # Safety
///
/// `index` must be null or a struct filled by [`vd_rename_index_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_index_free(index: *mut VdRenameIndex) {
    if index.is_null() {
        return;
    }
    let index = &mut *index;
    // SAFETY: `warnings` came from `c_string_list`.
    unsafe { free_string_list(&mut index.warnings) };
}

/// Release a filled [`VdRenamePlan`], including every per-file preview and every
/// hit; null-safe.
///
/// # Safety
///
/// `plan` must be null or a struct filled by [`vd_rename_plan_poll`] and not freed
/// yet.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_plan_free(plan: *mut VdRenamePlan) {
    if plan.is_null() {
        return;
    }
    let plan = &mut *plan;
    if !plan.items.is_null() {
        // SAFETY: `items` points to `count` previews from `into_c`.
        let files = &mut *std::ptr::slice_from_raw_parts_mut(plan.items, plan.count);
        for file in files.iter_mut() {
            if !file.items.is_null() {
                // SAFETY: each preview points to `count` hits from `into_c`.
                let hits = &mut *std::ptr::slice_from_raw_parts_mut(file.items, file.count);
                for hit in hits.iter_mut() {
                    // SAFETY: every string is an owned C string from `into_c`.
                    unsafe {
                        free_string(hit.before);
                        free_string(hit.after);
                    }
                    hit.before = ptr::null_mut();
                    hit.after = ptr::null_mut();
                }
                // SAFETY: the array itself came from `Box<[..]>::into_raw`.
                drop(unsafe {
                    Box::from_raw(std::ptr::slice_from_raw_parts_mut(file.items, file.count))
                });
            }
            // SAFETY: `file` is an owned C string from `into_c`.
            unsafe { free_string(file.file) };
            file.file = ptr::null_mut();
            file.count = 0;
            file.items = ptr::null_mut();
        }
        // SAFETY: the preview array came from `Box<[..]>::into_raw`.
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(plan.items, plan.count)) });
    }
    // SAFETY: every string below is null or an owned C string from `into_c`.
    unsafe {
        free_string(plan.root);
        free_string(plan.from);
        free_string(plan.to);
        free_string_list(&mut plan.warnings);
    }
    plan.root = ptr::null_mut();
    plan.from = ptr::null_mut();
    plan.to = ptr::null_mut();
    plan.count = 0;
    plan.items = ptr::null_mut();
}

/// Release a filled [`VdRenameApplyResult`]; null-safe.
///
/// # Safety
///
/// `result` must be null or a struct filled by [`vd_rename_apply_poll`] and not
/// freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_rename_apply_result_free(result: *mut VdRenameApplyResult) {
    if result.is_null() {
        return;
    }
    let result = &mut *result;
    // SAFETY: `failed_file` is null or an owned C string from `into_c`.
    unsafe { free_string(result.failed_file) };
    result.failed_file = ptr::null_mut();
}
