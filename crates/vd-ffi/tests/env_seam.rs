//! T10 env seams through the REAL C ABI (batch 20261110c, AC-N1).
//!
//! * **A (seam 1, E-1)** — a real `vd_deeplint_lint` job spawns a probe tool as
//!   its child; the child's OWN environment dump proves
//!   `process < global < project`, that an imported `LD_LIBRARY_PATH` /
//!   `LOCPATH` is removed AFTER the merge, and that the parent process
//!   environment is never modified.
//! * **B** — a broken `environment.json` degrades to the EMPTY layer plus
//!   warnings, never a panic.
//! * **C (seam 2, E-3)** — the FROZEN `vd_project_expand_v2` does not read the
//!   imported layer; the additive `vd_project_expand_v3` does (negative control).
//! * **D (seam 2, E-2)** — the `.f` filelist matrix: `$VAR` / `${VAR}` and
//!   `+incdir+` / `+define+` resolve project table > imported project selection >
//!   imported global selection > process environment, with the SAME names in
//!   every layer, driven through `vd_project_expand_v3`.
//!
//! The imported layer is a PROCESS-WIDE static (`vd_deeplint::binary`), so every
//! test in this binary takes [`SERIAL`] first and starts from an empty layer.
//! This file is its own test binary, so the static cannot leak into another
//! target's tests.

use std::ffi::{c_char, CStr, CString};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

use vd_config::env::EnvironmentSelection;
use vd_ffi::capi::{
    vd_string_free, VdDiagnostics, VdError, VdProjectInfo, VdStringList, VD_ERR_INVALID_ARGUMENT,
    VD_STATUS_PENDING, VD_STATUS_READY,
};
use vd_ffi::deeplint::{vd_deeplint_lint, vd_deeplint_poll, vd_diagnostics_free};
use vd_ffi::env::vd_env_apply_selection;
use vd_ffi::project::{
    vd_project_expand_v2, vd_project_expand_v3, vd_project_info_free, vd_project_poll,
};
use vd_ffi::service::{vd_job_release, vd_service_create, vd_service_destroy, vd_service_error};

/// Serialises the tests of THIS binary: the imported layer is process-wide.
static SERIAL: Mutex<()> = Mutex::new(());

fn serial() -> MutexGuard<'static, ()> {
    SERIAL.lock().unwrap_or_else(PoisonError::into_inner)
}

// --- shared helpers ---------------------------------------------------------

fn wait_for<F: FnMut() -> i32>(mut check: F, budget: Duration) -> i32 {
    let deadline = Instant::now() + budget;
    loop {
        let status = check();
        if status != VD_STATUS_PENDING {
            return status;
        }
        if Instant::now() >= deadline {
            return -99;
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// The service's last typed error, as `(code, message)`.
///
/// # Safety
///
/// `service` must be null or a live handle from `vd_service_create`.
unsafe fn last_error(service: *mut vd_ffi::service::VdService) -> (i32, String) {
    let mut error = std::mem::zeroed::<VdError>();
    if unsafe { vd_service_error(service, &mut error) } == 1 {
        let message = unsafe { CStr::from_ptr(error.message.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        (error.code, message)
    } else {
        (0, String::new())
    }
}

fn write_file(root: &Path, name: &str, content: &str) -> PathBuf {
    let path = root.join(name);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .unwrap_or_else(|error| panic!("mkdir {}: {error}", parent.display()));
    }
    fs::write(&path, content).unwrap_or_else(|error| panic!("write {}: {error}", path.display()));
    path
}

/// Reset the process-wide imported layer to the EMPTY selection.
fn reset_imported_layer(root: &Path) {
    let missing = root.join("no-such-environment.json");
    let warnings = vd_ffi::env::apply_environment_selection_file(&missing, None);
    assert!(
        warnings.is_empty(),
        "a missing selection file is not a warning: {warnings:?}"
    );
    assert!(
        vd_deeplint::binary::imported_environment().is_empty(),
        "the imported layer must be empty after the reset"
    );
}

/// A borrowed [`VdStringList`] over live `CString`s; the pointer array is leaked
/// on purpose (the test process ends, and the C call must outlive this frame).
fn string_list(values: &[CString]) -> VdStringList {
    if values.is_empty() {
        return VdStringList {
            count: 0,
            items: std::ptr::null_mut(),
        };
    }
    let pointers: Vec<*mut c_char> = values
        .iter()
        .map(|value| value.as_ptr().cast_mut())
        .collect();
    let leaked: &'static mut [*mut c_char] = Box::leak(pointers.into_boxed_slice());
    VdStringList {
        count: leaked.len(),
        items: leaked.as_mut_ptr(),
    }
}

/// Copy a C string list filled by the ABI.
///
/// # Safety
///
/// `list` must be a live string list (as filled by `vd_project_poll`).
unsafe fn strings(list: &VdStringList) -> Vec<String> {
    if list.items.is_null() || list.count == 0 {
        return Vec::new();
    }
    let pointers = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    pointers
        .iter()
        .map(|pointer| {
            // SAFETY: every entry is an owned NUL-terminated string from the ABI.
            unsafe { CStr::from_ptr(*pointer) }
                .to_string_lossy()
                .into_owned()
        })
        .collect()
}

// --- A + B: seam 1, the child's own environment -----------------------------

/// Write the probe tool: a REAL child process. It dumps its environment (NUL
/// separated) and writes an empty slang diagnostic report to the path after
/// `--diag-json`, so the lint job completes as a clean file.
///
/// `env | tr '\n' '\0'` rather than `env -0`: the coreutils in the CentOS 7
/// build image predate `env -0`, and the values asserted here contain no
/// newline (a newline in a value would split a record, not corrupt the rest).
fn write_probe_tool(root: &Path, dump: &Path) -> PathBuf {
    let body = format!(
        "#!/bin/sh\n\
         # the child's OWN environment, NUL separated (E-1: real process, no mock).\n\
         # Absolute paths on purpose: this child's PATH is what the test overrides.\n\
         /usr/bin/env | /usr/bin/tr '\\n' '\\0' > '{dump}'\n\
         out=\n\
         while [ \"$#\" -gt 0 ]; do\n\
         \x20 case \"$1\" in\n\
         \x20   --diag-json) out=\"$2\"; shift 2 ;;\n\
         \x20   *) shift ;;\n\
         \x20 esac\n\
         done\n\
         if [ -n \"$out\" ]; then printf '[]' > \"$out\"; fi\n\
         exit 0\n",
        dump = dump.display()
    );
    let script = write_file(root, "probe-tool.sh", &body);
    let mut permissions = fs::metadata(&script)
        .unwrap_or_else(|error| panic!("stat {}: {error}", script.display()))
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(&script, permissions)
        .unwrap_or_else(|error| panic!("chmod {}: {error}", script.display()));
    script
}

fn parse_env_dump(bytes: &[u8]) -> Vec<(String, String)> {
    bytes
        .split(|byte| *byte == 0)
        .filter(|entry| !entry.is_empty())
        .filter_map(|entry| {
            let text = std::str::from_utf8(entry).ok()?;
            text.split_once('=')
                .map(|(name, value)| (name.to_string(), value.to_string()))
        })
        .collect()
}

fn env_value<'a>(entries: &'a [(String, String)], name: &str) -> Option<&'a str> {
    entries
        .iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.as_str())
}

#[test]
fn a_real_tool_child_receives_the_imported_environment() {
    let _guard = serial();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    reset_imported_layer(root);

    // A parent-only variable: it must SURVIVE into the child (merge, not
    // replace) and must still be the parent's own value after the apply.
    std::env::set_var("SEAM_PARENT_ONLY", "parent-value");
    let parent_home = std::env::var("HOME").ok();
    let parent_path = std::env::var("PATH").ok();

    // The selection: global first, then THIS project's layer, so the project
    // layer wins. The same PATH in both layers makes the winner unambiguous.
    let selection_path = root.join("environment.json");
    let mut selection = EnvironmentSelection {
        global: vec![
            ("PATH".to_string(), "/seam-global-path".to_string()),
            ("HOME".to_string(), "/seam-global-home".to_string()),
            ("SEAM_GLOBAL_ONLY".to_string(), "yes".to_string()),
            ("LD_LIBRARY_PATH".to_string(), "/seam-lib".to_string()),
            ("LOCPATH".to_string(), "/seam-loc".to_string()),
        ],
        ..Default::default()
    };
    selection.projects.insert(
        root.display().to_string(),
        vec![
            ("PATH".to_string(), "/seam-project-path".to_string()),
            ("SEAM_PROJECT_ONLY".to_string(), "yes".to_string()),
        ],
    );
    selection
        .save(&selection_path)
        .unwrap_or_else(|error| panic!("saving {}: {error}", selection_path.display()));
    let warnings = vd_ffi::env::apply_environment_selection_file(&selection_path, Some(root));
    assert!(
        warnings.is_empty(),
        "a valid selection must not warn: {warnings:?}"
    );

    // The layer the tool children merge is the MERGED one (project wins), and
    // the parent process environment is untouched.
    let layer = vd_deeplint::binary::imported_environment();
    assert_eq!(
        layer.get("PATH").map(String::as_str),
        Some("/seam-project-path"),
        "the project selection layer must beat the global one: {layer:?}"
    );
    assert_eq!(
        layer.get("HOME").map(String::as_str),
        Some("/seam-global-home")
    );
    assert_eq!(std::env::var("HOME").ok(), parent_home);
    assert_eq!(
        std::env::var("PATH").ok(),
        parent_path,
        "applying a selection must never rewrite the parent PATH"
    );
    assert!(
        std::env::var("SEAM_GLOBAL_ONLY").is_err(),
        "applying a selection must never write the parent environment"
    );
    assert!(
        std::env::var("SEAM_PROJECT_ONLY").is_err(),
        "the project layer is a selection, not a parent-process write"
    );
    assert_eq!(
        std::env::var("SEAM_PARENT_ONLY").ok().as_deref(),
        Some("parent-value")
    );

    // The REAL lint path: `vd_deeplint_lint` resolves the probe as its tool and
    // spawns it with the merged environment.
    let source = write_file(root, "top.sv", "module top; endmodule\n");
    let dump = root.join("child-environment.dump");
    let probe = write_probe_tool(root, &dump);

    let service = vd_service_create();
    let source_c = CString::new(source.display().to_string())
        .unwrap_or_else(|error| panic!("source path: {error}"));
    let probe_c = CString::new(probe.display().to_string())
        .unwrap_or_else(|error| panic!("probe path: {error}"));
    // SAFETY: `VdDiagnostics` is a plain C struct; zero is its empty state.
    let mut diagnostics = unsafe { std::mem::zeroed::<VdDiagnostics>() };
    // SAFETY: the strings live through the call; the result starts zeroed.
    let status = unsafe {
        let job = vd_deeplint_lint(service, source_c.as_ptr(), probe_c.as_ptr());
        assert_ne!(job, 0, "lint rejected: {:?}", last_error(service));
        let status = wait_for(
            || vd_deeplint_poll(service, job, &mut diagnostics),
            Duration::from_secs(20),
        );
        let error = last_error(service);
        vd_job_release(service, job);
        assert_eq!(
            status, VD_STATUS_READY,
            "the probe lint job must complete: {error:?}"
        );
        assert_eq!(diagnostics.count, 0, "the probe reports no diagnostics");
        vd_diagnostics_free(&mut diagnostics);
        status
    };
    assert_eq!(status, VD_STATUS_READY);
    // SAFETY: the handle was created here and is not used again.
    unsafe { vd_service_destroy(service) };

    let bytes = fs::read(&dump)
        .unwrap_or_else(|error| panic!("the probe did not dump {}: {error}", dump.display()));
    let child = parse_env_dump(&bytes);
    println!(
        "T10-SEAM1 child-vars={} PATH={:?} HOME={:?}",
        child.len(),
        env_value(&child, "PATH"),
        env_value(&child, "HOME")
    );

    assert_eq!(
        env_value(&child, "PATH"),
        Some("/seam-project-path"),
        "project > global > process for a name present in every layer"
    );
    assert_eq!(
        env_value(&child, "HOME"),
        Some("/seam-global-home"),
        "the global selection layer beats the process environment"
    );
    assert_eq!(env_value(&child, "SEAM_GLOBAL_ONLY"), Some("yes"));
    assert_eq!(env_value(&child, "SEAM_PROJECT_ONLY"), Some("yes"));
    assert_eq!(
        env_value(&child, "SEAM_PARENT_ONLY"),
        Some("parent-value"),
        "a parent-only variable is still inherited: the merge must not replace"
    );
    for removed in ["LD_LIBRARY_PATH", "LOCPATH"] {
        assert_eq!(
            env_value(&child, removed),
            None,
            "{removed} was imported and MUST be removed AFTER the merge"
        );
    }

    // Clean up the process-wide state this test created.
    std::env::remove_var("SEAM_PARENT_ONLY");
    reset_imported_layer(root);
}

#[test]
fn a_broken_selection_file_degrades_to_an_empty_layer_with_warnings() {
    let _guard = serial();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    reset_imported_layer(root);

    // Seed a LIVE layer first, so "degraded to empty" is observable.
    let good = root.join("good.json");
    let selection = EnvironmentSelection {
        global: vec![("SEAM_SEEDED".to_string(), "yes".to_string())],
        ..Default::default()
    };
    selection
        .save(&good)
        .unwrap_or_else(|error| panic!("saving {}: {error}", good.display()));
    let warnings = vd_ffi::env::apply_environment_selection_file(&good, None);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert!(vd_deeplint::binary::imported_environment().contains_key("SEAM_SEEDED"));

    // A file that is not JSON at all: warnings, an EMPTY layer, no panic.
    let broken = write_file(root, "environment.json", "{ this is not json\n");
    let warnings = vd_ffi::env::apply_environment_selection_file(&broken, None);
    println!("T10-SEAM1 broken-selection warnings={warnings:?}");
    assert!(
        !warnings.is_empty(),
        "a broken selection file must be reported, not silently ignored"
    );
    assert!(
        warnings
            .iter()
            .any(|warning| warning.contains("not valid JSON")),
        "the warning names the JSON failure: {warnings:?}"
    );
    assert!(
        vd_deeplint::binary::imported_environment().is_empty(),
        "a broken file degrades to the EMPTY layer: {:?}",
        vd_deeplint::binary::imported_environment()
    );
}

// --- C: the frozen v2 entry point vs the additive v3 ------------------------

/// Build the process-wide selection with ONE global entry.
fn apply_global_layer(root: &Path, name: &str, value: &str) {
    let path = root.join("selection.json");
    let selection = EnvironmentSelection {
        global: vec![(name.to_string(), value.to_string())],
        ..Default::default()
    };
    selection
        .save(&path)
        .unwrap_or_else(|error| panic!("saving {}: {error}", path.display()));
    let warnings = vd_ffi::env::apply_environment_selection_file(&path, None);
    assert!(warnings.is_empty(), "{warnings:?}");
}

#[test]
fn the_frozen_v2_entry_point_does_not_read_the_imported_layer() {
    let _guard = serial();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    reset_imported_layer(root);

    // CONTRACT CHANGE A-5 (2026-11-18 file-set decision): this test asserts the
    // LEGACY union file set (recursive scan + root `.f`), so it opts back into
    // it explicitly; the assertions below are unchanged.
    write_file(root, "vedit.toml", "[project]\nauto_discover = true\n");
    write_file(root, "top.sv", "module top; endmodule\n");
    write_file(root, "imported.sv", "module imported; endmodule\n");
    // The `.f` line only resolves through the imported layer.
    write_file(root, "seam.f", "$SEAM_FROZEN_FILE\n");
    apply_global_layer(root, "SEAM_FROZEN_FILE", "imported.sv");
    assert_eq!(
        vd_deeplint::binary::imported_environment()
            .get("SEAM_FROZEN_FILE")
            .map(String::as_str),
        Some("imported.sv")
    );

    let service = vd_service_create();
    let root_c = CString::new(root.display().to_string())
        .unwrap_or_else(|error| panic!("root path: {error}"));
    // SAFETY: every pointer is null or lives through the call.
    unsafe {
        // v2 is FROZEN: it must NOT read the imported layer.
        let job = vd_project_expand_v2(
            service,
            root_c.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        );
        assert_ne!(job, 0, "v2 rejected: {:?}", last_error(service));
        // SAFETY: `VdProjectInfo` is a plain C struct; zero is its empty state.
        let mut frozen = std::mem::zeroed::<VdProjectInfo>();
        let status = wait_for(
            || vd_project_poll(service, job, &mut frozen),
            Duration::from_secs(20),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "v2 job failed: {:?}",
            last_error(service)
        );
        let frozen_missing = strings(&frozen.missing_variables);
        let frozen_skipped = frozen.skipped_lines;
        vd_project_info_free(&mut frozen);
        vd_job_release(service, job);
        println!("T10-SEAM2 frozen v2: missing={frozen_missing:?} skipped={frozen_skipped}");
        assert!(
            frozen_missing.iter().any(|name| name == "SEAM_FROZEN_FILE"),
            "the FROZEN v2 path must report the name it cannot resolve: {frozen_missing:?}"
        );
        assert!(
            frozen_skipped >= 1,
            "the unresolved `.f` line is skipped on the v2 path (got {frozen_skipped})"
        );

        // v3 is ADDITIVE: it reads the same imported layer.
        let job = vd_project_expand_v3(
            service,
            root_c.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        );
        assert_ne!(job, 0, "v3 rejected: {:?}", last_error(service));
        // SAFETY: `VdProjectInfo` is a plain C struct; zero is its empty state.
        let mut additive = std::mem::zeroed::<VdProjectInfo>();
        let status = wait_for(
            || vd_project_poll(service, job, &mut additive),
            Duration::from_secs(20),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "v3 job failed: {:?}",
            last_error(service)
        );
        let missing = strings(&additive.missing_variables);
        let skipped = additive.skipped_lines;
        vd_project_info_free(&mut additive);
        vd_job_release(service, job);

        println!("T10-SEAM2 additive v3: missing={missing:?} skipped={skipped}");
        assert!(
            missing.is_empty(),
            "the v3 path resolves the imported variable: {missing:?}"
        );
        assert_eq!(skipped, 0, "no `.f` line is dropped on the v3 path");
    }
    // SAFETY: the handle was created here and is not used again.
    unsafe { vd_service_destroy(service) };
    reset_imported_layer(root);
}

// --- D: the `.f` four-layer priority matrix (E-2) ---------------------------

fn set_process_var(name: &str, value: &str) {
    std::env::set_var(name, value);
}

fn remove_process_var(name: &str) {
    std::env::remove_var(name);
}

#[test]
fn the_filelist_resolves_project_then_imported_then_process() {
    let _guard = serial();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    let outside = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    reset_imported_layer(root);

    // CONTRACT CHANGE A-5 (2026-11-18 file-set decision): this test asserts the
    // LEGACY union file set (recursive scan + root `.f`), so it opts back into
    // it explicitly; the assertions below are unchanged.
    write_file(root, "vedit.toml", "[project]\nauto_discover = true\n");

    // Candidate TARGETS live outside the project root, so the root source scan
    // can never add them: only a `.f` resolution can produce them.
    for name in [
        "proj_file.sv",
        "selproj_file.sv",
        "glob_file.sv",
        "proc_file.sv",
    ] {
        write_file(outside.path(), name, "// candidate\n");
    }
    for directory in ["proj_inc", "selproj_inc", "glob_inc", "proc_inc"] {
        fs::create_dir_all(root.join(directory))
            .unwrap_or_else(|error| panic!("mkdir {directory}: {error}"));
    }
    // `$VAR` and `${VAR}` forms, plus `+incdir+` and `+define+`, with the SAME
    // names in every layer so a wrong winner is visible.
    write_file(
        root,
        "seam.f",
        "$SEAM_FILE\n\
         +incdir+${SEAM_INCDIR}\n\
         +incdir+$SEAM_IMPORTED\n\
         +incdir+${SEAM_GLOBAL}\n\
         +incdir+$SEAM_PROC\n\
         +define+${SEAM_DEFINE}\n",
    );

    let outside_file = |name: &str| outside.path().join(name).display().to_string();
    let include = |name: &str| root.join(name).display().to_string();

    // Layer 1: the project's own variable table (the ABI argument).
    let project_table = vec![
        CString::new(format!("SEAM_FILE={}", outside_file("proj_file.sv")))
            .unwrap_or_else(|error| panic!("entry: {error}")),
        CString::new(format!("SEAM_INCDIR={}", include("proj_inc")))
            .unwrap_or_else(|error| panic!("entry: {error}")),
        CString::new("SEAM_DEFINE=PROJ=1".to_string())
            .unwrap_or_else(|error| panic!("entry: {error}")),
    ];

    // Layers 2-3: the imported selection (its project layer beats its global).
    let selection_path = root.join("environment.json");
    let mut selection = EnvironmentSelection {
        global: vec![
            ("SEAM_FILE".to_string(), outside_file("glob_file.sv")),
            ("SEAM_INCDIR".to_string(), include("glob_inc")),
            ("SEAM_DEFINE".to_string(), "GLOBAL=1".to_string()),
            ("SEAM_GLOBAL".to_string(), include("glob_inc")),
        ],
        ..Default::default()
    };
    selection.projects.insert(
        root.display().to_string(),
        vec![
            ("SEAM_FILE".to_string(), outside_file("selproj_file.sv")),
            ("SEAM_INCDIR".to_string(), include("selproj_inc")),
            ("SEAM_DEFINE".to_string(), "SELPROJ=1".to_string()),
            ("SEAM_IMPORTED".to_string(), include("selproj_inc")),
        ],
    );
    selection
        .save(&selection_path)
        .unwrap_or_else(|error| panic!("saving {}: {error}", selection_path.display()));
    let warnings = vd_ffi::env::apply_environment_selection_file(&selection_path, Some(root));
    assert!(warnings.is_empty(), "{warnings:?}");

    // Layer 4: the process environment (the same names again).
    set_process_var("SEAM_FILE", &outside_file("proc_file.sv"));
    set_process_var("SEAM_INCDIR", &include("proc_inc"));
    set_process_var("SEAM_DEFINE", "PROC=1");
    set_process_var("SEAM_IMPORTED", &include("proc_inc"));
    set_process_var("SEAM_PROC", &include("proc_inc"));

    let service = vd_service_create();
    let root_c = CString::new(root.display().to_string())
        .unwrap_or_else(|error| panic!("root path: {error}"));
    let variables = string_list(&project_table);
    // SAFETY: every pointer is null or lives through the call.
    let (files, include_dirs, defines, missing, skipped) = unsafe {
        let job = vd_project_expand_v3(
            service,
            root_c.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            &variables,
            std::ptr::null(),
            std::ptr::null(),
        );
        assert_ne!(job, 0, "v3 rejected: {:?}", last_error(service));
        // SAFETY: `VdProjectInfo` is a plain C struct; zero is its empty state.
        let mut info = std::mem::zeroed::<VdProjectInfo>();
        let status = wait_for(
            || vd_project_poll(service, job, &mut info),
            Duration::from_secs(20),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "v3 job failed: {:?}",
            last_error(service)
        );
        let data = (
            strings(&info.files),
            strings(&info.include_dirs),
            strings(&info.defines),
            strings(&info.missing_variables),
            info.skipped_lines,
        );
        vd_project_info_free(&mut info);
        vd_job_release(service, job);
        data
    };
    // SAFETY: the handle was created here and is not used again.
    unsafe { vd_service_destroy(service) };

    for name in [
        "SEAM_FILE",
        "SEAM_INCDIR",
        "SEAM_DEFINE",
        "SEAM_IMPORTED",
        "SEAM_PROC",
    ] {
        remove_process_var(name);
    }
    reset_imported_layer(root);

    println!(
        "T10-SEAM2 filelist layers: files={files:?} include_dirs={include_dirs:?} \
         defines={defines:?} missing={missing:?} skipped={skipped}"
    );
    assert!(missing.is_empty(), "every name resolves: {missing:?}");
    assert_eq!(skipped, 0, "no `.f` line is dropped");

    let canonical = |path: &str| {
        fs::canonicalize(path).unwrap_or_else(|error| panic!("canonicalize {path}: {error}"))
    };
    let canonical_files: Vec<PathBuf> = files.iter().map(|file| canonical(file)).collect();
    let canonical_dirs: Vec<PathBuf> = include_dirs
        .iter()
        .map(|directory| canonical(directory))
        .collect();

    // Layer 1 wins for every same-name variable.
    assert!(
        canonical_files.contains(&canonical(&outside_file("proj_file.sv"))),
        "`$SEAM_FILE` resolves from the project table: {files:?}"
    );
    for loser in ["selproj_file.sv", "glob_file.sv", "proc_file.sv"] {
        assert!(
            !canonical_files.contains(&canonical(&outside_file(loser))),
            "`{loser}` belongs to a lower layer and must not win: {files:?}"
        );
    }
    assert!(
        canonical_dirs.contains(&canonical(&include("proj_inc"))),
        "`+incdir+${{SEAM_INCDIR}}` resolves from the project table: {include_dirs:?}"
    );
    assert_eq!(
        defines,
        vec!["PROJ=1".to_string()],
        "`+define+${{SEAM_DEFINE}}` resolves from the project table, and a \
         lower-layer define of the same name never leaks"
    );
    // Layer 2 (imported project selection) beats layer 3 and layer 4.
    assert!(
        canonical_dirs.contains(&canonical(&include("selproj_inc"))),
        "`$SEAM_IMPORTED` resolves from the imported PROJECT layer: {include_dirs:?}"
    );
    // Layer 3 (imported global selection) beats layer 4.
    assert!(
        canonical_dirs.contains(&canonical(&include("glob_inc"))),
        "`${{SEAM_GLOBAL}}` resolves from the imported GLOBAL layer: {include_dirs:?}"
    );
    // Layer 4 (process environment) is still reachable.
    assert!(
        canonical_dirs.contains(&canonical(&include("proc_inc"))),
        "a process-only name still resolves (the environment is not dropped): {include_dirs:?}"
    );
}

// --- the C ABI startup verb (T63, batch 20261110c) --------------------------

/// Take ownership of the warnings string the ABI returned.
///
/// # Safety
///
/// `text` must be null or an owned string from `vd_env_apply_selection`.
unsafe fn take_warnings(text: *mut c_char) -> Option<String> {
    if text.is_null() {
        return None;
    }
    // SAFETY: the pointer is an owned C string from this library.
    let value = unsafe { CStr::from_ptr(text) }
        .to_string_lossy()
        .into_owned();
    // SAFETY: the string came from the ABI and is not freed yet.
    unsafe { vd_string_free(text) };
    Some(value)
}

#[test]
fn the_c_abi_apply_verb_sets_and_clears_the_tool_child_layer() {
    let _guard = serial();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path();
    reset_imported_layer(root);

    // A selection with BOTH layers: the project root selects the project layer.
    let selection_path = root.join("environment.json");
    let mut projects = std::collections::BTreeMap::new();
    projects.insert(
        root.display().to_string(),
        vec![("SEAM_ABI_PROJECT".to_string(), "project-value".to_string())],
    );
    let selection = EnvironmentSelection {
        global: vec![
            ("SEAM_ABI_GLOBAL".to_string(), "global-value".to_string()),
            (
                "SEAM_ABI_BOTH".to_string(),
                "global-wins-only-when-no-project".to_string(),
            ),
        ],
        projects,
    };
    selection
        .save(&selection_path)
        .unwrap_or_else(|error| panic!("saving {}: {error}", selection_path.display()));

    let service = vd_service_create();
    let path_c = CString::new(selection_path.display().to_string())
        .unwrap_or_else(|error| panic!("path: {error}"));
    let root_c =
        CString::new(root.display().to_string()).unwrap_or_else(|error| panic!("root: {error}"));

    // 1) applied: return 1, warnings is an EMPTY (not null) owned string, and
    //    the process-wide layer the tool children merge is the merged selection.
    let mut warnings: *mut c_char = std::ptr::null_mut();
    // SAFETY: the strings live through the call; `warnings` is writable.
    let applied =
        unsafe { vd_env_apply_selection(service, path_c.as_ptr(), root_c.as_ptr(), &mut warnings) };
    assert_eq!(applied, 1, "a valid selection must apply");
    let text = unsafe { take_warnings(warnings) };
    assert_eq!(
        text.as_deref(),
        Some(""),
        "a clean selection reports an empty warning string, not null"
    );
    let layer = vd_deeplint::binary::imported_environment();
    assert_eq!(
        layer.get("SEAM_ABI_GLOBAL").map(String::as_str),
        Some("global-value")
    );
    assert_eq!(
        layer.get("SEAM_ABI_PROJECT").map(String::as_str),
        Some("project-value"),
        "the project root selects the project layer of the selection"
    );

    // 2) missing file: the EMPTY selection, no warning, still applied.
    let missing = root.join("no-such-environment.json");
    let missing_c =
        CString::new(missing.display().to_string()).unwrap_or_else(|error| panic!("path: {error}"));
    let mut warnings: *mut c_char = std::ptr::null_mut();
    // SAFETY: the strings live through the call; `warnings` is writable.
    let applied = unsafe {
        vd_env_apply_selection(service, missing_c.as_ptr(), std::ptr::null(), &mut warnings)
    };
    assert_eq!(
        applied, 1,
        "a missing selection file is the empty selection"
    );
    assert_eq!(
        unsafe { take_warnings(warnings) }.as_deref(),
        Some(""),
        "a missing file is not a warning"
    );
    assert!(
        vd_deeplint::binary::imported_environment().is_empty(),
        "the failed load clears the previously applied layer"
    );

    // 3) broken file: still applied as the empty layer, with ONE observable
    //    warning (AC-4.4: the product prints it and keeps working).
    let broken = write_file(root, "broken-environment.json", "{ this is not json\n");
    let broken_c =
        CString::new(broken.display().to_string()).unwrap_or_else(|error| panic!("path: {error}"));
    let mut warnings: *mut c_char = std::ptr::null_mut();
    // SAFETY: the strings live through the call; `warnings` is writable.
    let applied = unsafe {
        vd_env_apply_selection(service, broken_c.as_ptr(), std::ptr::null(), &mut warnings)
    };
    assert_eq!(applied, 1, "a broken file degrades, it does not refuse");
    let text = unsafe { take_warnings(warnings) }.unwrap_or_default();
    assert!(
        text.contains("not valid JSON"),
        "the warning is carried to the caller: {text:?}"
    );
    assert!(vd_deeplint::binary::imported_environment().is_empty());

    // 4) null path: a typed refusal, no layer change.
    // SAFETY: null is the documented "no path" spelling.
    let refused = unsafe {
        vd_env_apply_selection(
            service,
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null_mut(),
        )
    };
    assert_eq!(refused, 0, "a null selection path is refused");
    // SAFETY: the service is live.
    let (code, message) = unsafe { last_error(service) };
    assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
    assert!(message.contains("selection_path"), "{message}");

    // SAFETY: the handle was created here and is not used again.
    unsafe { vd_service_destroy(service) };
    reset_imported_layer(root);
}
