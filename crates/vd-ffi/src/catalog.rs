//! Module catalog (ux-batch-2 A1): the C projection of
//! [`vd_deeplint::ModuleCatalog`].
//!
//! The job shape is the hierarchy's, deliberately: start returns an id from its
//! own range (`>= 1 << 52`, so it can never collide with the service jobs at 1,
//! the lint jobs at `1 << 32` or the hierarchy jobs at `1 << 48`), the worker
//! thread runs one whole-project elaboration, and `poll` delivers the result
//! once. Nothing on the UI thread ever waits for `slang`.
//!
//! The catalog is DEFINITION-keyed: a module instantiated under two parents is
//! one entry, and BOTH parents list the edge. Interfaces are not modules and are
//! excluded by the backend (their `InstanceBody` would otherwise appear as a
//! module); interface-typed ports are reported with direction `InOut`.

use std::collections::HashMap;
use std::path::PathBuf;
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use vd_deeplint::{
    CatalogModule, CatalogOptions, LintFailure, ModuleCatalog, ModuleInstance, ModulePort,
};

use crate::capi::{
    c_str, c_string, free_string, VdCatalogOptions, VdError, VdModule, VdModuleCatalog,
    VdModuleInstance, VdModuleInstanceList, VdModuleList, VdModulePort, VdModulePortList,
    VdStringList, VD_ERR_CANCELLED, VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN, VD_ERR_SERVICE,
    VD_ERR_TIMEOUT, VD_ERR_UNKNOWN_TOP, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use crate::deeplint::read_string_list;
use crate::service::{service_state, VdService, POLL_MISUSE};

/// First catalog job id; its own range so it can never collide with the service,
/// lint or hierarchy job ranges.
const FIRST_CATALOG_ID: u64 = 1 << 52;

struct CatalogSlot {
    catalog: Mutex<Option<ModuleCatalog>>,
    /// Raised by [`vd_catalog_cancel`]; the worker hands a clone to the catalog
    /// options, where it kills the `slang` child instead of letting it run to
    /// completion into a slot nobody reads (the old `release` behaviour).
    cancel: Arc<AtomicBool>,
}

static NEXT_CATALOG: AtomicU64 = AtomicU64::new(0);

fn catalogs() -> MutexGuard<'static, HashMap<u64, Arc<CatalogSlot>>> {
    static JOBS: OnceLock<Mutex<HashMap<u64, Arc<CatalogSlot>>>> = OnceLock::new();
    let jobs = JOBS.get_or_init(|| Mutex::new(HashMap::new()));
    jobs.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn ports_to_c(ports: &[ModulePort]) -> VdModulePortList {
    if ports.is_empty() {
        return VdModulePortList {
            count: 0,
            items: ptr::null_mut(),
        };
    }
    let items: Vec<VdModulePort> = ports
        .iter()
        .map(|port| VdModulePort {
            name: c_string(&port.name),
            direction: c_string(port.direction.as_str()),
            type_name: c_string(&port.type_name),
            file: c_string(&port.file.to_string_lossy()),
            line: port.line,
            column: port.column,
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdModulePortList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdModulePort,
    }
}

fn children_to_c(children: &[ModuleInstance]) -> VdModuleInstanceList {
    if children.is_empty() {
        return VdModuleInstanceList {
            count: 0,
            items: ptr::null_mut(),
        };
    }
    let items: Vec<VdModuleInstance> = children
        .iter()
        .map(|child| VdModuleInstance {
            module: c_string(&child.module),
            path: c_string(&child.path),
            file: c_string(&child.file.to_string_lossy()),
            line: child.line,
            column: child.column,
        })
        .collect();
    let boxed = items.into_boxed_slice();
    VdModuleInstanceList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut VdModuleInstance,
    }
}

fn module_to_c(module: &CatalogModule) -> VdModule {
    VdModule {
        name: c_string(&module.name),
        file: c_string(&module.file.to_string_lossy()),
        line: module.line,
        column: module.column,
        is_top: u8::from(module.is_top),
        ports: ports_to_c(&module.ports),
        children: children_to_c(&module.children),
    }
}

/// Convert one catalog into the owned C representation.
///
/// # Safety
///
/// The returned struct must be released with [`vd_module_catalog_free`].
unsafe fn catalog_to_c(catalog: &ModuleCatalog) -> VdModuleCatalog {
    let modules: Vec<VdModule> = catalog.modules.iter().map(module_to_c).collect();
    let modules = if modules.is_empty() {
        VdModuleList {
            count: 0,
            items: ptr::null_mut(),
        }
    } else {
        let boxed = modules.into_boxed_slice();
        VdModuleList {
            count: boxed.len(),
            items: Box::into_raw(boxed) as *mut VdModule,
        }
    };
    let reachable: Vec<String> = catalog
        .reachable_files
        .iter()
        .map(|path| path.to_string_lossy().into_owned())
        .collect();
    let shadowed: Vec<String> = catalog
        .shadowed_files
        .iter()
        .map(|path| path.to_string_lossy().into_owned())
        .collect();
    VdModuleCatalog {
        tool: c_string(&catalog.tool),
        elapsed_ms: catalog.elapsed.as_millis() as u64,
        peak_rss_kb: catalog.peak_rss_kb,
        warning: catalog.warning.as_deref().map_or(ptr::null_mut(), c_string),
        modules,
        top: catalog.top.as_deref().map_or(ptr::null_mut(), c_string),
        reachable_files: strings_to_c(&reachable),
        outside_modules: strings_to_c(&catalog.outside_modules),
        shadowed_files: strings_to_c(&shadowed),
        shadowed_warning: catalog
            .shadowed_warning
            .as_deref()
            .map_or(ptr::null_mut(), c_string),
    }
}

/// RM-17: an owned C string list from owned strings (freed by
/// [`free_string_list`]).
unsafe fn strings_to_c(values: &[String]) -> VdStringList {
    if values.is_empty() {
        return VdStringList {
            count: 0,
            items: ptr::null_mut(),
        };
    }
    let items: Vec<*mut std::ffi::c_char> = values.iter().map(|value| c_string(value)).collect();
    let boxed = items.into_boxed_slice();
    VdStringList {
        count: boxed.len(),
        items: Box::into_raw(boxed) as *mut *mut std::ffi::c_char,
    }
}

/// Free one [`strings_to_c`] list.
unsafe fn free_string_list(list: &mut VdStringList) {
    if !list.items.is_null() && list.count > 0 {
        let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
        for item in slice.iter_mut() {
            free_string(*item);
            *item = ptr::null_mut();
        }
        drop(unsafe { Box::from_raw(slice as *mut [*mut std::ffi::c_char]) });
    }
    list.count = 0;
    list.items = ptr::null_mut();
}

/// Build the catalog options from the C arguments.
///
/// # Safety
///
/// Every pointer must be null or valid for the duration of the call.
unsafe fn build_catalog_options(options: &VdCatalogOptions) -> Result<CatalogOptions, String> {
    let mut built = CatalogOptions::default();
    if !options.slang_binary.is_null() {
        // SAFETY: a non-null pointer is documented as a valid string.
        match unsafe { c_str(options.slang_binary) } {
            Some(value) if !value.is_empty() => built.slang_binary = Some(PathBuf::from(value)),
            _ => return Err("slang_binary must be a valid non-empty path or null".to_string()),
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
    if options.max_json_bytes > 0 {
        built.max_json_bytes = options.max_json_bytes;
    }
    if options.max_files > 0 {
        built.max_files = options.max_files;
    }
    if !options.top.is_null() {
        // SAFETY: a non-null pointer is documented as a valid string.
        match unsafe { c_str(options.top) } {
            Some(value) if !value.trim().is_empty() => {
                built.top = Some(value.trim().to_string());
            }
            Some(_) => {}
            None => return Err("top must be a valid non-empty module name or null".to_string()),
        }
    }
    Ok(built)
}

/// Start a project-wide module catalog (ux-batch-2 A1).
///
/// `files` is the project's effective file set and `options` carries the `-I`/
/// `-D` settings. The work runs on a worker thread, so the UI thread never
/// waits; poll with [`vd_catalog_poll`]. Returns a job id (`>= 1 << 52`) or `0`
/// when the arguments were rejected.
///
/// # Safety
///
/// `service` must be null or live; `files` must be null or point to a
/// [`VdStringList`] of valid NUL-terminated strings; `options` must be null or
/// point to a readable [`VdCatalogOptions`] whose lists satisfy that contract.
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_start(
    service: *mut VdService,
    files: *const VdStringList,
    options: *const VdCatalogOptions,
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
            "the project file set is empty: there is nothing to catalogue",
        ));
        return 0;
    }
    // SAFETY: the option pointers are null or valid strings per the contract.
    let catalog_options = match unsafe { build_catalog_options(&options) } {
        Ok(built) => built,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    let id = FIRST_CATALOG_ID + NEXT_CATALOG.fetch_add(1, Ordering::Relaxed);
    let cancel = Arc::new(AtomicBool::new(false));
    let slot = Arc::new(CatalogSlot {
        catalog: Mutex::new(None),
        cancel: Arc::clone(&cancel),
    });
    catalogs().insert(id, Arc::clone(&slot));
    let worker_slot = Arc::clone(&slot);
    let spawned = std::thread::Builder::new()
        .name(format!("vd-catalog-{id}"))
        .spawn(move || {
            let mut catalog_options = catalog_options;
            catalog_options.cancel = Some(cancel);
            let catalog = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                vd_deeplint::build_catalog(&file_set, &catalog_options)
            }))
            .unwrap_or_else(|_| ModuleCatalog {
                tool: "slang".to_string(),
                modules: Vec::new(),
                elapsed: std::time::Duration::ZERO,
                peak_rss_kb: 0,
                warning: None,
                top: None,
                reachable_files: Vec::new(),
                outside_modules: Vec::new(),
                shadowed_files: Vec::new(),
                shadowed_warning: None,
                failure: Some(LintFailure::Io {
                    message: "the catalog job panicked; its result was discarded".to_string(),
                }),
            });
            if let Ok(mut slot) = worker_slot.catalog.lock() {
                *slot = Some(catalog);
            }
        });
    if spawned.is_err() {
        catalogs().remove(&id);
        state.record_error(VdError::new(
            crate::capi::VD_ERR_INTERNAL,
            "cannot start a worker thread for the catalog job",
        ));
        return 0;
    }
    id
}

/// Poll a catalog job.
///
/// `VD_STATUS_PENDING` while it runs; `VD_STATUS_READY` with `out` filled once it
/// finished; `VD_STATUS_FAILED` when the catalog itself failed (the reason,
/// naming the tool and the stage, is readable with `vd_service_error`). The
/// result is delivered once.
///
/// # Safety
///
/// `service` must be null or live; `out` must be null or writable for one
/// [`VdModuleCatalog`].
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdModuleCatalog,
) -> i32 {
    // SAFETY: forwards the caller's contract; the optional list is omitted.
    unsafe { vd_catalog_poll_v2(service, job, out, ptr::null_mut()) }
}

/// Poll a catalog with a parallel list of owning compilation units. An empty
/// list entry means that the corresponding module is declared directly in its source.
/// Release a filled list with `vd_catalog_compilation_units_free`.
///
/// # Safety
/// Same as `vd_catalog_poll`. `units` must be null or writable for one `VdStringList`.
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_poll_v2(
    service: *mut VdService,
    job: u64,
    out: *mut VdModuleCatalog,
    units: *mut VdStringList,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = catalogs().get(&job).cloned() else {
        state.record_error(VdError::new(
            VD_ERR_JOB_UNKNOWN,
            "unknown catalog job id: it was never started or was released",
        ));
        return POLL_MISUSE;
    };
    let catalog = {
        let mut guard = match slot.catalog.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        guard.take()
    };
    let Some(catalog) = catalog else {
        return VD_STATUS_PENDING;
    };
    catalogs().remove(&job);
    if let Some(message) = catalog.failure_message() {
        // The catalog re-words every failure in its own vocabulary (the AST
        // JSON), so an oversized AST dump can never be reported as a lint
        // diagnostic flood. The CODE carries the class the UI acts on: only
        // `UnknownTop` may fall back to Auto, and a timeout must keep the user's
        // top instead of clearing it (AC M1.1).
        state.record_error(VdError::new(
            failure_code(catalog.failure.as_ref()),
            &format!("{} failed: {message}", catalog.tool),
        ));
        return VD_STATUS_FAILED;
    }
    if !units.is_null() {
        let paths: Vec<_> = catalog
            .modules
            .iter()
            .map(|module| {
                module
                    .compilation_unit
                    .as_deref()
                    .map_or_else(String::new, |path| path.to_string_lossy().into_owned())
            })
            .collect();
        // SAFETY: a non-null `units` is writable and its content is owned by the caller.
        unsafe { *units = strings_to_c(&paths) };
    }
    // SAFETY: `out` is writable for one VdModuleCatalog per the caller contract.
    unsafe { *out = catalog_to_c(&catalog) };
    VD_STATUS_READY
}

/// Free the compilation unit list returned by `vd_catalog_poll_v2`.
///
/// # Safety
/// `units` must be null or point to a list filled by `vd_catalog_poll_v2`.
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_compilation_units_free(units: *mut VdStringList) {
    if !units.is_null() {
        // SAFETY: only a list allocated by `strings_to_c` may reach here.
        unsafe { free_string_list(&mut *units) };
    }
}

/// The ABI error code for one typed failure.
///
/// Only the classes a UI decision depends on are typed apart: `UnknownTop` (the
/// module tree may fall back to Auto once), `Timeout` and `Cancelled`. Everything
/// else stays [`VD_ERR_SERVICE`], which is what the previous catalogue reported
/// for all of them.
fn failure_code(failure: Option<&LintFailure>) -> i32 {
    match failure {
        Some(LintFailure::UnknownTop { .. }) => VD_ERR_UNKNOWN_TOP,
        Some(LintFailure::Timeout { .. }) => VD_ERR_TIMEOUT,
        Some(LintFailure::Cancelled) => VD_ERR_CANCELLED,
        _ => VD_ERR_SERVICE,
    }
}

/// Cancel a catalog job: the `slang` child is killed and the run stops before the
/// streamed parse, instead of finishing into memory nobody reads.
///
/// Returns 1 when the job was found and the flag raised, 0 for an unknown id
/// (already consumed or released). The job stays in the map: the worker answers
/// with a `Cancelled` failure, so a later [`vd_catalog_poll`] reports
/// `VD_STATUS_FAILED` with `VD_ERR_CANCELLED` rather than "unknown job" — the
/// caller that does not care releases it right after (cancel-then-release is the
/// pair the module tree uses).
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_cancel(service: *mut VdService, job: u64) -> i32 {
    let _ = service;
    match catalogs().get(&job) {
        Some(slot) => {
            slot.cancel.store(true, Ordering::SeqCst);
            1
        }
        None => 0,
    }
}

/// Release a catalog job without delivering its result (null-safe for an unknown
/// id). A running job finishes into memory nobody reads — pair it with
/// [`vd_catalog_cancel`] to actually stop the work.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_catalog_release(service: *mut VdService, job: u64) -> i32 {
    let _ = service;
    i32::from(catalogs().remove(&job).is_some())
}

/// Release a filled [`VdModuleCatalog`]; null-safe and idempotent.
///
/// # Safety
///
/// `catalog` must be null or point to a struct filled by [`vd_catalog_poll`] and
/// not freed yet.
#[no_mangle]
pub unsafe extern "C" fn vd_module_catalog_free(catalog: *mut VdModuleCatalog) {
    if catalog.is_null() {
        return;
    }
    let catalog = &mut *catalog;
    // SAFETY: every string was produced by this module and is not freed yet.
    unsafe {
        free_string(catalog.tool);
        free_string(catalog.warning);
        free_string(catalog.top);
        free_module_list(&mut catalog.modules);
        free_string_list(&mut catalog.reachable_files);
        free_string_list(&mut catalog.outside_modules);
        // G1-c: the two appended fields are owned exactly like the ones above.
        free_string_list(&mut catalog.shadowed_files);
        free_string(catalog.shadowed_warning);
    }
    catalog.tool = ptr::null_mut();
    catalog.warning = ptr::null_mut();
    catalog.top = ptr::null_mut();
    catalog.shadowed_warning = ptr::null_mut();
}

/// # Safety
///
/// The list must come from [`catalog_to_c`] and not be freed yet.
unsafe fn free_module_list(list: &mut VdModuleList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdModule]> that catalog_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for module in slice.iter_mut() {
        // SAFETY: name/file were produced by c_string.
        unsafe {
            free_string(module.name);
            free_string(module.file);
            free_port_list(&mut module.ports);
            free_child_list(&mut module.children);
        }
        module.name = ptr::null_mut();
        module.file = ptr::null_mut();
    }
    // SAFETY: the pointer came from Box::into_raw of a boxed slice.
    drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(list.items, list.count)) });
    list.count = 0;
    list.items = ptr::null_mut();
}

/// # Safety
///
/// The list must come from [`ports_to_c`] and not be freed yet.
unsafe fn free_port_list(list: &mut VdModulePortList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdModulePort]> that ports_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for port in slice.iter_mut() {
        // SAFETY: every string was produced by c_string.
        unsafe {
            free_string(port.name);
            free_string(port.direction);
            free_string(port.type_name);
            free_string(port.file);
        }
        port.name = ptr::null_mut();
        port.direction = ptr::null_mut();
        port.type_name = ptr::null_mut();
        port.file = ptr::null_mut();
    }
    // SAFETY: the pointer came from Box::into_raw of a boxed slice.
    drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(list.items, list.count)) });
    list.count = 0;
    list.items = ptr::null_mut();
}

/// # Safety
///
/// The list must come from [`children_to_c`] and not be freed yet.
unsafe fn free_child_list(list: &mut VdModuleInstanceList) {
    if list.items.is_null() {
        list.count = 0;
        return;
    }
    // SAFETY: the slice is the Box<[VdModuleInstance]> that children_to_c leaked.
    let slice = unsafe { &mut *ptr::slice_from_raw_parts_mut(list.items, list.count) };
    for child in slice.iter_mut() {
        // SAFETY: every string was produced by c_string.
        unsafe {
            free_string(child.module);
            free_string(child.path);
            free_string(child.file);
        }
        child.module = ptr::null_mut();
        child.path = ptr::null_mut();
        child.file = ptr::null_mut();
    }
    // SAFETY: the pointer came from Box::into_raw of a boxed slice.
    drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(list.items, list.count)) });
    list.count = 0;
    list.items = ptr::null_mut();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn job_ids_live_in_their_own_range() {
        // Compile-time: the catalog range must stay above the hierarchy's
        // `1 << 48` so one job id can never mean two things.
        const {
            assert!(
                FIRST_CATALOG_ID > 1 << 48,
                "catalog ids must beat the hierarchy ids"
            )
        };
        assert_eq!(FIRST_CATALOG_ID >> 52, 1);
    }

    #[test]
    fn the_failure_code_carries_the_class_the_ui_acts_on() {
        use std::time::Duration;
        // Only a name slang cannot elaborate may fall back to Auto (AC M1.1).
        assert_eq!(
            failure_code(Some(&LintFailure::UnknownTop {
                name: "nope".to_string()
            })),
            VD_ERR_UNKNOWN_TOP
        );
        // A timeout keeps the user's top: it is NOT an unknown top.
        assert_eq!(
            failure_code(Some(&LintFailure::Timeout {
                after: Duration::from_secs(120)
            })),
            VD_ERR_TIMEOUT
        );
        assert_eq!(
            failure_code(Some(&LintFailure::Cancelled)),
            VD_ERR_CANCELLED
        );
        assert_eq!(
            failure_code(Some(&LintFailure::Io {
                message: "boom".to_string()
            })),
            VD_ERR_SERVICE
        );
        assert_eq!(failure_code(None), VD_ERR_SERVICE);
    }

    #[test]
    fn cancelling_an_unknown_job_is_a_no_op() {
        // SAFETY: a null service is documented as "no state".
        let raised = unsafe { vd_catalog_cancel(ptr::null_mut(), FIRST_CATALOG_ID + 99_999) };
        assert_eq!(raised, 0, "an unknown job cannot be signalled");
    }

    /// AC M1.4: cancel is a REAL cancellation. A fake tool that only ends by a
    /// signal proves it: nothing but the kill can make the 30 s sleep finish in
    /// under a second.
    #[cfg(unix)]
    #[test]
    fn cancelling_a_running_job_kills_the_tool_and_reports_cancelled() {
        use std::ffi::{c_char, CString};
        use std::os::unix::fs::PermissionsExt;
        use std::time::{Duration, Instant};

        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let marker = dir.path().join("started.marker");
        let script = dir.path().join("fake-slang.sh");
        let body = format!("#!/bin/sh\n: > '{}'\nsleep 30\nexit 0\n", marker.display());
        if std::fs::write(&script, body).is_err() {
            return;
        }
        let Ok(mut permissions) = std::fs::metadata(&script).map(|meta| meta.permissions()) else {
            return;
        };
        permissions.set_mode(0o755);
        if std::fs::set_permissions(&script, permissions).is_err() {
            return;
        }
        let source = dir.path().join("top.sv");
        if std::fs::write(&source, "module top; endmodule\n").is_err() {
            return;
        }

        let file = CString::new(source.display().to_string()).unwrap_or_default();
        let slang = CString::new(script.display().to_string()).unwrap_or_default();
        let pointers: Vec<*mut c_char> = vec![file.as_ptr().cast_mut()];
        let boxed = pointers.into_boxed_slice();
        let files = VdStringList {
            count: boxed.len(),
            items: Box::into_raw(boxed) as *mut *mut c_char,
        };
        let service = crate::service::vd_service_create();
        let job = {
            let options = VdCatalogOptions {
                slang_binary: slang.as_ptr(),
                include_dirs: VdStringList {
                    count: 0,
                    items: ptr::null_mut(),
                },
                defines: VdStringList {
                    count: 0,
                    items: ptr::null_mut(),
                },
                timeout_ms: 60_000,
                max_json_bytes: 0,
                max_files: 0,
                top: ptr::null(),
            };
            // SAFETY: every pointer is valid for the call and the list layout is
            // the ABI's.
            unsafe { vd_catalog_start(service, &files, &options) }
        };
        // The ABI borrows the list; the job has copied what it needs.
        // SAFETY: the pointer came from Box::into_raw of this boxed slice.
        drop(unsafe {
            Box::from_raw(std::ptr::slice_from_raw_parts_mut(files.items, files.count))
        });
        assert_ne!(job, 0, "the catalog job was rejected");
        // Wait until the child is really running before cancelling, so the test
        // cannot pass by cancelling a job that had not started yet.
        let started = Instant::now();
        while !marker.exists() && started.elapsed() < Duration::from_secs(5) {
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(marker.exists(), "the fake tool never started");
        let cancel_sent = Instant::now();
        // SAFETY: `job` came from vd_catalog_start on this service.
        let raised = unsafe { vd_catalog_cancel(service, job) };
        assert_eq!(raised, 1, "the running job must be signalled");

        let mut catalog: VdModuleCatalog = unsafe { std::mem::zeroed() };
        let status = loop {
            // SAFETY: `out` is writable and the job belongs to this service.
            let status = unsafe { vd_catalog_poll(service, job, &mut catalog) };
            if status != VD_STATUS_PENDING || cancel_sent.elapsed() > Duration::from_secs(20) {
                break status;
            }
            std::thread::sleep(Duration::from_millis(20));
        };
        assert_eq!(
            status, VD_STATUS_FAILED,
            "a cancelled job is not a ready one"
        );
        assert!(
            cancel_sent.elapsed() < Duration::from_secs(10),
            "the kill must not wait for the tool's own 30 s sleep: {:?}",
            cancel_sent.elapsed()
        );
        let mut error: VdError = unsafe { std::mem::zeroed() };
        // SAFETY: `out` is writable for one VdError.
        let has_error = unsafe { crate::service::vd_service_error(service, &mut error) };
        assert_ne!(has_error, 0, "a failed job records its reason");
        assert_eq!(error.code, VD_ERR_CANCELLED, "the class is Cancelled");
        // SAFETY: the service handle came from vd_service_create and is live.
        unsafe { crate::service::vd_service_destroy(service) };
    }

    #[test]
    fn a_catalog_warning_reaches_the_abi_verbatim() {
        // batch-20261118 B: the duplicate-module sentence is produced by
        // `vd_deeplint` and carried to the module tree ONLY through
        // `VdModuleCatalog.warning`; this conversion is the single hop between
        // them, so a dropped or reworded warning would make the whole diagnostic
        // invisible to C++ (the tree renders that field in its summary line).
        let warning = "module 'uart' is defined in both sim/uart.v and rtl/uart.v; \
                       using rtl/uart.v";
        let catalog = ModuleCatalog {
            tool: "slang".to_string(),
            modules: Vec::new(),
            elapsed: std::time::Duration::ZERO,
            peak_rss_kb: 0,
            warning: Some(warning.to_string()),
            top: None,
            reachable_files: Vec::new(),
            outside_modules: Vec::new(),
            shadowed_files: Vec::new(),
            shadowed_warning: None,
            failure: None,
        };
        // SAFETY: the conversion owns its strings, the read happens before the
        // paired free, and the free is the one documented for this value.
        unsafe {
            let mut converted = catalog_to_c(&catalog);
            assert!(
                !converted.warning.is_null(),
                "the warning must be delivered"
            );
            let carried = std::ffi::CStr::from_ptr(converted.warning)
                .to_str()
                .unwrap_or_else(|error| panic!("the warning must be UTF-8: {error}"));
            assert_eq!(carried, warning);
            vd_module_catalog_free(&raw mut converted);
            assert!(converted.warning.is_null(), "the free clears the pointer");
        }
    }

    #[test]
    fn an_empty_catalog_converts_and_frees_cleanly() {
        let catalog = ModuleCatalog {
            tool: "slang".to_string(),
            modules: Vec::new(),
            elapsed: std::time::Duration::ZERO,
            peak_rss_kb: 0,
            warning: None,
            top: None,
            reachable_files: Vec::new(),
            outside_modules: Vec::new(),
            shadowed_files: Vec::new(),
            shadowed_warning: None,
            failure: None,
        };
        // SAFETY: the conversion owns its strings, and the free is the paired one.
        unsafe {
            let mut converted = catalog_to_c(&catalog);
            assert_eq!(converted.modules.count, 0);
            assert!(converted.modules.items.is_null());
            vd_module_catalog_free(&raw mut converted);
            assert!(converted.tool.is_null());
            assert!(converted.modules.items.is_null());
            assert_eq!(converted.shadowed_files.count, 0);
            assert!(converted.shadowed_files.items.is_null());
            assert!(converted.shadowed_warning.is_null());
        }
    }

    #[test]
    fn the_shadowed_file_list_reaches_the_abi_and_is_freed() {
        // G1-c: the losing-definition files are published on their own fields so
        // a C++ consumer can subtract them from the compiler input. The B-2
        // warning is NOT reworded: the two channels stay separate.
        let warning = "module 'uart' is defined in both sim/uart.v and rtl/uart.v; \
                       using rtl/uart.v";
        let sentence = "sim/uart.v is excluded from the compiler input because every module it \
                        defines lost the duplicate pick";
        let catalog = ModuleCatalog {
            tool: "slang".to_string(),
            modules: Vec::new(),
            elapsed: std::time::Duration::ZERO,
            peak_rss_kb: 0,
            warning: Some(warning.to_string()),
            top: None,
            reachable_files: Vec::new(),
            outside_modules: Vec::new(),
            shadowed_files: vec![PathBuf::from("sim/uart.v")],
            shadowed_warning: Some(sentence.to_string()),
            failure: None,
        };
        // SAFETY: the conversion owns its strings, the read happens before the
        // paired free, and the free is the one documented for this value.
        unsafe {
            let mut converted = catalog_to_c(&catalog);
            assert_eq!(converted.shadowed_files.count, 1);
            assert!(!converted.shadowed_files.items.is_null());
            let items = std::slice::from_raw_parts(
                converted.shadowed_files.items,
                converted.shadowed_files.count,
            );
            let published: Vec<String> = items
                .iter()
                .map(|item| {
                    std::ffi::CStr::from_ptr(*item)
                        .to_str()
                        .unwrap_or_else(|error| panic!("a published path must be UTF-8: {error}"))
                        .to_string()
                })
                .collect();
            assert_eq!(published, vec!["sim/uart.v".to_string()]);
            assert!(!converted.shadowed_warning.is_null());
            let carried = std::ffi::CStr::from_ptr(converted.shadowed_warning)
                .to_str()
                .unwrap_or_else(|error| panic!("the sentence must be UTF-8: {error}"));
            assert_eq!(carried, sentence);
            let kept = std::ffi::CStr::from_ptr(converted.warning)
                .to_str()
                .unwrap_or_else(|error| panic!("the warning must be UTF-8: {error}"));
            assert_eq!(kept, warning, "the B-2 sentence is not merged with it");
            vd_module_catalog_free(&raw mut converted);
            assert_eq!(converted.shadowed_files.count, 0);
            assert!(converted.shadowed_files.items.is_null());
            assert!(converted.shadowed_warning.is_null());
        }
    }
}
