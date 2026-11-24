//! ABI selfcheck for the batch 20261110 surface (env capture, engineering-wide
//! rename, one-click Scala run, LINT waivers, the lint autofix/waiver knobs and
//! the typed diagnostic `optionName`).
//!
//! Two halves, both driven through the exported `extern "C"` functions exactly
//! like the C++ product:
//!
//! * a **link + signature** check, so a rename or a changed parameter type breaks
//!   this file instead of the Qt build;
//! * a **behaviour** check per lane: argument rejection carries a typed code, an
//!   engine that cannot start the work answers a typed failure naming the program
//!   or the reason (it never pretends to have done the work and never panics), the
//!   waiver writers produce the bytes both tools load, a knob the engine cannot
//!   honour is refused instead of silently dropped, and the one-click Scala run is
//!   asserted against a real short-lived child: streamed output, its own exit code
//!   and a cancel that ends the whole process tree.
//!
//! The waiver cases run against real files in a temporary directory, so a verifier
//! can read the bytes on disk rather than trusting a log line.

use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::time::{Duration, Instant};

use vd_ffi::capi::{
    VdDiagnostic, VdDiagnostics, VdEnvCaptureOptions, VdEnvCaptureResult, VdEnvSelection,
    VdEnvSelectionInput, VdError, VdLintBatchOptions, VdRenameApplyResult, VdRenameIndex,
    VdRenameOptions, VdRenamePlan, VdScalaRunOptions, VdScalaRunStatus, VdStringList,
    VdWaiverEntry, VdWaiverOptions, VdWaiverResult, VD_ERR_CANCELLED, VD_ERR_INVALID_ARGUMENT,
    VD_ERR_JOB_UNKNOWN, VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_ERR_TIMEOUT, VD_ERR_WRONG_JOB_KIND,
    VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY, VD_WAIVER_FORMAT_SLANG,
    VD_WAIVER_FORMAT_VERIBLE,
};
use vd_ffi::deeplint::{vd_deeplint_batch_start, vd_diagnostics_free, vd_lint_batch_command_line};
use vd_ffi::env::{
    vd_env_capture_cancel, vd_env_capture_poll, vd_env_capture_release, vd_env_capture_result_free,
    vd_env_capture_start, vd_env_selection_free, vd_env_selection_load, vd_env_selection_poll,
    vd_env_selection_release, vd_env_selection_save,
};
use vd_ffi::rename::{
    vd_rename_apply_poll, vd_rename_apply_result_free, vd_rename_apply_start, vd_rename_cancel,
    vd_rename_index_free, vd_rename_index_poll, vd_rename_index_start, vd_rename_plan_free,
    vd_rename_plan_poll, vd_rename_plan_start, vd_rename_release,
};
use vd_ffi::scala::{
    vd_scala_run_cancel, vd_scala_run_poll, vd_scala_run_release, vd_scala_run_start,
    vd_scala_run_status_free,
};
use vd_ffi::service::{
    vd_abi_version, vd_service_create, vd_service_destroy, vd_service_error, VdService,
};
use vd_ffi::waiver::{
    vd_waiver_append_start, vd_waiver_generate_start, vd_waiver_poll, vd_waiver_register_start,
    vd_waiver_result_free,
};

/// The empty list literal (a null pointer with a zero count).
fn empty_list() -> VdStringList {
    VdStringList {
        count: 0,
        items: ptr::null_mut(),
    }
}

/// A C string whose ownership is transferred to a C struct (the matching free
/// function releases it).
fn owned_raw(text: &str) -> *mut c_char {
    CString::new(text)
        .unwrap_or_else(|_| panic!("a fixture string must not contain an interior NUL"))
        .into_raw()
}

/// Poll `check` until it stops returning `VD_STATUS_PENDING`.
fn wait_for<F: FnMut() -> i32>(mut check: F) -> i32 {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        let status = check();
        if status != VD_STATUS_PENDING {
            return status;
        }
        assert!(Instant::now() < deadline, "the job never settled");
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Read the service's last error.
fn last_error(service: *mut VdService) -> (i32, String) {
    let mut error = unsafe { std::mem::zeroed::<VdError>() };
    // SAFETY: the service is live and `error` is writable.
    if unsafe { vd_service_error(service, &mut error) } == 1 {
        // SAFETY: the message is a NUL-terminated array filled by the service.
        let message = unsafe { CStr::from_ptr(error.message.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        (error.code, message)
    } else {
        (0, String::new())
    }
}

/// Read one owned C string a poll filled in.
///
/// # Safety
///
/// `text` must be null or a NUL-terminated string owned by the struct it came
/// from and not freed yet.
fn read_owned(text: *mut c_char) -> String {
    if text.is_null() {
        return String::new();
    }
    // SAFETY: stated above.
    unsafe { CStr::from_ptr(text) }
        .to_string_lossy()
        .into_owned()
}

/// Strings plus the pointer buffer that a [`VdStringList`] borrows.
struct List {
    _owned: Vec<CString>,
    _raw: Vec<*mut c_char>,
    list: VdStringList,
}

impl List {
    fn new(values: &[&str]) -> Self {
        let owned: Vec<CString> = values
            .iter()
            .map(|value| {
                CString::new(*value)
                    .unwrap_or_else(|_| panic!("a fixture string must not contain an interior NUL"))
            })
            .collect();
        let mut raw: Vec<*mut c_char> = owned
            .iter()
            .map(|value| value.as_ptr() as *mut c_char)
            .collect();
        let list = VdStringList {
            count: raw.len(),
            items: raw.as_mut_ptr(),
        };
        Self {
            _owned: owned,
            _raw: raw,
            list,
        }
    }
}

/// The frozen surface must keep every signature: renaming an export or changing a
/// parameter type is a compile error here, before the Qt build can break.
#[test]
fn the_frozen_surface_is_linked_and_still_abi_version_two() {
    // SAFETY: these are function-item bindings; none of them is called.
    let _: unsafe extern "C" fn(*mut VdService, *const VdEnvCaptureOptions) -> u64 =
        vd_env_capture_start;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdEnvCaptureResult) -> i32 =
        vd_env_capture_poll;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_env_capture_cancel;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_env_capture_release;

    let _: unsafe extern "C" fn(*mut VdService, *const c_char) -> u64 = vd_env_selection_load;
    let _: unsafe extern "C" fn(
        *mut VdService,
        *const c_char,
        *const VdEnvSelectionInput,
        usize,
    ) -> u64 = vd_env_selection_save;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdEnvSelection) -> i32 =
        vd_env_selection_poll;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_env_selection_release;
    let _: unsafe extern "C" fn(*mut VdEnvSelection) = vd_env_selection_free;

    let _: unsafe extern "C" fn(
        *mut VdService,
        *const c_char,
        *const VdStringList,
        *const VdRenameOptions,
    ) -> u64 = vd_rename_index_start;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdRenameIndex) -> i32 =
        vd_rename_index_poll;
    let _: unsafe extern "C" fn(
        *mut VdService,
        *const c_char,
        *const VdStringList,
        *const c_char,
        *const c_char,
        *const VdRenameOptions,
    ) -> u64 = vd_rename_plan_start;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdRenamePlan) -> i32 =
        vd_rename_plan_poll;
    let _: unsafe extern "C" fn(
        *mut VdService,
        *const VdRenamePlan,
        *const VdRenameOptions,
    ) -> u64 = vd_rename_apply_start;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdRenameApplyResult) -> i32 =
        vd_rename_apply_poll;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_rename_cancel;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_rename_release;

    let _: unsafe extern "C" fn(*mut VdService, *const VdScalaRunOptions) -> u64 =
        vd_scala_run_start;
    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdScalaRunStatus) -> i32 =
        vd_scala_run_poll;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_scala_run_cancel;
    let _: unsafe extern "C" fn(*mut VdService, u64) -> i32 = vd_scala_run_release;

    let _: unsafe extern "C" fn(
        *mut VdService,
        *const c_char,
        i32,
        *const VdWaiverEntry,
        *const VdWaiverOptions,
    ) -> u64 = vd_waiver_append_start;
    let _: unsafe extern "C" fn(
        *mut VdService,
        *const c_char,
        i32,
        *const VdDiagnostics,
        *const VdWaiverOptions,
    ) -> u64 = vd_waiver_generate_start;
    let _: unsafe extern "C" fn(*mut VdService, *const c_char, i32, *const c_char) -> u64 =
        vd_waiver_register_start;

    let _: unsafe extern "C" fn(*mut VdService, u64, *mut VdWaiverResult) -> i32 = vd_waiver_poll;
    let _: unsafe extern "C" fn(*mut VdWaiverResult) = vd_waiver_result_free;
    let _: unsafe extern "C" fn(*mut VdRenamePlan) = vd_rename_plan_free;
    let _: unsafe extern "C" fn(*mut VdRenameIndex) = vd_rename_index_free;
    let _: unsafe extern "C" fn(*mut VdRenameApplyResult) = vd_rename_apply_result_free;
    let _: unsafe extern "C" fn(*mut VdScalaRunStatus) = vd_scala_run_status_free;

    assert_eq!(
        vd_abi_version(),
        2,
        "AC: the ABI version is 2 (VdModuleCatalog grew)"
    );
}

/// The env-capture lane: arguments are validated on the calling thread, a real
/// shell runs exactly once, and every failure mode is a typed code.
///
/// Batch 20261110 (T10) turned this from a frozen stub into the real engine, so
/// the assertions here changed from "states the missing engine" to "runs the
/// shell and reports what happened" — the same test, the new contract.
#[test]
fn env_capture_runs_a_real_shell_and_reports_each_failure_mode() {
    // SAFETY: null handles are the documented "invalid call" path.
    unsafe {
        assert_eq!(vd_env_capture_start(ptr::null_mut(), ptr::null()), 0);
    }
    // SAFETY: a live service with a null options pointer.
    let service = vd_service_create();
    assert!(!service.is_null());
    // SAFETY: the service is live.
    unsafe {
        assert_eq!(vd_env_capture_start(service, ptr::null()), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("options"), "{message}");
    }

    let mut invalid_shell = VdEnvCaptureOptions {
        shell: 99,
        shell_path: ptr::null(),
        source_file: ptr::null(),
        workdir: ptr::null(),
        extra_argv: empty_list(),
        timeout_ms: 0,
        login: 0,
        interactive: 0,
    };
    // SAFETY: the service is live; the struct is fully initialised.
    unsafe {
        assert_eq!(vd_env_capture_start(service, &invalid_shell), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("bash"), "{message}");
    }

    // `custom` has no default program, so a null shell_path is refused.
    invalid_shell.shell = 7;
    // SAFETY: same contract.
    unsafe {
        assert_eq!(vd_env_capture_start(service, &invalid_shell), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("shell_path"), "{message}");
    }

    // A real capture: bash runs ONCE and its environment is delivered.
    let options = VdEnvCaptureOptions {
        shell: 0,
        shell_path: ptr::null(),
        source_file: ptr::null(),
        workdir: ptr::null(),
        extra_argv: empty_list(),
        timeout_ms: 5_000,
        login: 0,
        interactive: 0,
    };
    // SAFETY: the service is live; the struct is fully initialised.
    let job = unsafe { vd_env_capture_start(service, &options) };
    assert_ne!(job, 0, "a valid capture request must get a job id");
    let mut out = unsafe { std::mem::zeroed::<VdEnvCaptureResult>() };
    // SAFETY: the service is live and `out` is writable.
    let status = wait_for(|| unsafe { vd_env_capture_poll(service, job, &mut out) });
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    assert!(out.count > 0, "a bash environment has variables");
    assert_eq!(out.exit_code, 0);
    assert!(
        !out.raw.is_null() && out.raw_len > 0,
        "the raw blob is delivered"
    );
    let items = unsafe { std::slice::from_raw_parts(out.items, out.count) };
    let mut saw_path = false;
    for item in items {
        let text = read_owned(*item);
        if text.starts_with("PATH=") {
            saw_path = true;
        }
        assert!(text.contains('='), "every item is NAME=VALUE: {text:?}");
    }
    assert!(saw_path, "the captured environment carries PATH");
    // SAFETY: the payload was filled by the poll and is owned by this test.
    unsafe {
        assert_eq!(
            vd_env_capture_poll(service, job, &mut out),
            -1,
            "a second poll must be a typed misuse"
        );
        vd_env_capture_result_free(&mut out);
        assert_eq!(vd_env_capture_release(service, job), 1);
        assert_eq!(vd_env_capture_release(service, job), 0);
    }

    // A program that does not exist is a typed NOT_FOUND, not a hang.
    let missing = CString::new("/nonexistent/vd-shell-for-tests").unwrap_or_default();
    let absent = VdEnvCaptureOptions {
        shell: 0,
        shell_path: missing.as_ptr(),
        source_file: ptr::null(),
        workdir: ptr::null(),
        extra_argv: empty_list(),
        timeout_ms: 2_000,
        login: 0,
        interactive: 0,
    };
    // SAFETY: the service is live; the string outlives the call.
    let job = unsafe { vd_env_capture_start(service, &absent) };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `out` is writable.
    let status = wait_for(|| unsafe { vd_env_capture_poll(service, job, &mut out) });
    assert_eq!(status, VD_STATUS_FAILED);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_NOT_FOUND, "{message}");
    assert!(message.contains("vd-shell-for-tests"), "{message}");

    // A capture that overruns its budget is killed and reported as a timeout.
    let sh = CString::new("/bin/sh").unwrap_or_default();
    let sleep_argv = List::new(&["-c", "sleep 30"]);
    let overrun = VdEnvCaptureOptions {
        shell: 7,
        shell_path: sh.as_ptr(),
        source_file: ptr::null(),
        workdir: ptr::null(),
        extra_argv: sleep_argv.list,
        timeout_ms: 300,
        login: 0,
        interactive: 0,
    };
    // SAFETY: the service is live; every pointer outlives the call.
    let job = unsafe { vd_env_capture_start(service, &overrun) };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `out` is writable.
    let status = wait_for(|| unsafe { vd_env_capture_poll(service, job, &mut out) });
    assert_eq!(status, VD_STATUS_FAILED);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_TIMEOUT, "{message}");

    // A cancel kills the child and is reported as such.
    // SAFETY: the service is live; every pointer outlives the call.
    let job = unsafe { vd_env_capture_start(service, &overrun) };
    assert_ne!(job, 0);
    // SAFETY: the service is live; the id is ours.
    assert_eq!(unsafe { vd_env_capture_cancel(service, job) }, 1);
    // SAFETY: the service is live and `out` is writable.
    let status = wait_for(|| unsafe { vd_env_capture_poll(service, job, &mut out) });
    assert_eq!(status, VD_STATUS_FAILED);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_CANCELLED, "{message}");

    // SAFETY: the service is live.
    unsafe { vd_service_destroy(service) };
}

/// The rename lane: identifiers only, explicit no-op, and three job kinds that
/// cannot be polled with each other's result type.
#[test]
fn rename_validates_identifiers_and_separates_its_three_job_kinds() {
    let root = CString::new("/tmp/project").unwrap_or_default();
    let from = CString::new("old_name").unwrap_or_default();
    let to = CString::new("new_name").unwrap_or_default();
    let bad = CString::new("old-name").unwrap_or_default();
    let files = List::new(&["rtl/a.sv"]);
    let empty = List::new(&[]);
    let options = VdRenameOptions {
        whole_identifier: 1,
        dry_run: 0,
        max_entries_per_file: 0,
        timeout_ms: 2_000,
        // T72: no project `-I`/`-D` in this validation-only case; the list
        // fields are the additive project-input block.
        include_dirs: empty_list(),
        defines: empty_list(),
    };
    // SAFETY: a live service.
    let service = vd_service_create();
    assert!(!service.is_null());

    // SAFETY: the service is live; the lists and strings outlive each call.
    unsafe {
        assert_eq!(
            vd_rename_index_start(service, root.as_ptr(), &empty.list, &options),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("file set is empty"), "{message}");

        assert_eq!(
            vd_rename_plan_start(
                service,
                root.as_ptr(),
                &files.list,
                bad.as_ptr(),
                to.as_ptr(),
                &options
            ),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(
            message.contains("not a SystemVerilog identifier"),
            "{message}"
        );

        // Same name: an explicit no-op, never a job that applies zero edits.
        assert_eq!(
            vd_rename_plan_start(
                service,
                root.as_ptr(),
                &files.list,
                from.as_ptr(),
                from.as_ptr(),
                &options
            ),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("no-op"), "{message}");

        // Text replacement is refused at the ABI, not just by the engine.
        let not_whole = VdRenameOptions {
            whole_identifier: 0,
            ..options
        };
        assert_eq!(
            vd_rename_index_start(service, root.as_ptr(), &files.list, &not_whole),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("identifiers only"), "{message}");
    }

    // A valid index job reaches the real engine and reports the lane. Batch
    // 20261110b: the rename index is IMPLEMENTED now (it runs slang's AST), so
    // the T1 stub's `VD_ERR_UNIMPLEMENTED` is gone; here the engine's own
    // failure is a SERVICE error that names the stage. The contract change is
    // recorded in `evidence/batch-20261110b/lint-engine/AUTHORIZATION.md`.
    // SAFETY: the service is live; every pointer outlives the call.
    let index_job = unsafe { vd_rename_index_start(service, root.as_ptr(), &files.list, &options) };
    assert_ne!(index_job, 0);
    let mut index = unsafe { std::mem::zeroed::<VdRenameIndex>() };
    // SAFETY: the service is live and `index` is writable.
    let status = wait_for(|| unsafe { vd_rename_index_poll(service, index_job, &mut index) });
    assert_eq!(status, VD_STATUS_FAILED);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_SERVICE, "{message}");
    assert!(message.contains("AST index"), "{message}");

    // The index id is not a plan id: the wrong poll names the job kind.
    let mut plan = unsafe { std::mem::zeroed::<VdRenamePlan>() };
    // SAFETY: the service is live and `plan` is writable.
    let misused = unsafe { vd_rename_plan_poll(service, index_job, &mut plan) };
    assert_eq!(misused, -1);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_WRONG_JOB_KIND, "{message}");

    // A null plan and an empty plan are refused before a job starts.
    // SAFETY: the service is live; the null plan is the documented refusal path.
    unsafe {
        assert_eq!(vd_rename_apply_start(service, ptr::null(), &options), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("plan"), "{message}");
        assert_eq!(vd_rename_apply_start(service, &plan, &options), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("must carry"), "{message}");

        // The result was already taken: the id is unknown, not a silent re-delivery.
        assert_eq!(vd_rename_index_poll(service, index_job, &mut index), -1);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_JOB_UNKNOWN, "{message}");

        assert_eq!(vd_rename_cancel(service, index_job), 1);
        assert_eq!(vd_rename_release(service, index_job), 1);
        assert_eq!(vd_rename_release(service, index_job), 0);
        vd_rename_index_free(&mut index);
        vd_rename_plan_free(&mut plan);
        vd_service_destroy(service);
    }
}

/// Poll one Scala run to its terminal state, accumulating every streamed chunk.
///
/// Each poll hands out only the bytes produced since the previous one, so they
/// are appended before the status strings are released; `state`/`exit_code`
/// outlive the free (it nulls the string pointers only).
fn poll_scala_run(
    service: *mut VdService,
    job: u64,
    deadline: Instant,
) -> (i32, VdScalaRunStatus, String, String) {
    // SAFETY: the struct is a POD of plain values and null pointers.
    let mut status = unsafe { std::mem::zeroed::<VdScalaRunStatus>() };
    let mut output = String::new();
    let mut tool = String::new();
    loop {
        // SAFETY: the service is live and `status` is writable.
        let polled = unsafe { vd_scala_run_poll(service, job, &mut status) };
        // SAFETY: the poll filled the strings, each released here exactly once.
        unsafe {
            if !status.output.is_null() && status.output_len > 0 {
                output.push_str(&read_owned(status.output));
            }
            if !status.tool.is_null() {
                tool = read_owned(status.tool);
            }
            vd_scala_run_status_free(&mut status);
        }
        if polled != VD_STATUS_PENDING {
            return (polled, status, output, tool);
        }
        assert!(Instant::now() < deadline, "the run never settled");
        std::thread::sleep(Duration::from_millis(5));
    }
}

/// True while `/proc/<pid>/stat` reports a live process.
///
/// A zombie (`Z`) or a dead entry is `false`: for this test that is the honest
/// meaning of "killed" — the process can no longer run even if its parent has not
/// reaped it yet.
fn process_is_running(pid: u32) -> bool {
    let Ok(stat) = std::fs::read_to_string(format!("/proc/{pid}/stat")) else {
        return false;
    };
    // The format is `pid (comm) state …`; `comm` may contain spaces and ')'.
    let Some(close) = stat.rfind(')') else {
        return false;
    };
    !matches!(
        stat.get(close + 1..)
            .and_then(|rest| rest.trim_start().chars().next()),
        None | Some('Z') | Some('X') | Some('x')
    )
}

/// Wait until `done` is true or `timeout` elapses (returns the final answer).
fn wait_until(timeout: Duration, mut done: impl FnMut() -> bool) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        if done() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

/// The Scala run lane: argv validation, a missing program named in a typed
/// error, a REAL short-lived child whose streamed output and exit code are
/// reported, and a cancel that ends the whole process tree.
///
/// The name is historical: until batch 20261110's `scala-run` lane landed this
/// entry point could only fail with `VD_ERR_UNIMPLEMENTED`. The engine really
/// runs the plan now, so the same frozen exports are asserted against a real
/// child (contract change authorised by the captain, mail
/// `mail-20261112014159-478`; evidence: `evidence/batch-20261110c/scala-run/`).
#[test]
fn scala_run_validates_argv_and_reports_the_missing_engine() {
    // SAFETY: a live service.
    let service = vd_service_create();
    assert!(!service.is_null());

    // --- 1. argument validation (engine-independent, unchanged) -------------
    let no_argv = VdScalaRunOptions {
        root: ptr::null(),
        argv: empty_list(),
        env: empty_list(),
        timeout_ms: 0,
        merge_stderr: 0,
    };
    // SAFETY: the service is live; the struct is fully initialised.
    unsafe {
        assert_eq!(vd_scala_run_start(service, &no_argv), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("argv"), "{message}");
    }

    // --- 2. a program this machine does not have: typed, named failure ------
    let missing = List::new(&["scala-cli", "run", "--offline", "."]);
    let missing_options = VdScalaRunOptions {
        root: ptr::null(),
        argv: missing.list,
        env: empty_list(),
        timeout_ms: 0,
        merge_stderr: 1,
    };
    // SAFETY: the service is live; every pointer outlives the call.
    let job = unsafe { vd_scala_run_start(service, &missing_options) };
    assert_ne!(job, 0);
    let (polled, status, output, tool) =
        poll_scala_run(service, job, Instant::now() + Duration::from_secs(10));
    assert_eq!(
        polled, VD_STATUS_FAILED,
        "an unresolvable program fails the run"
    );
    assert_eq!(status.state, 2, "failed");
    assert_eq!(
        status.exit_code, -1,
        "no exit code from a job that never ran"
    );
    assert_eq!(tool, "scala-cli", "the ABI names argv[0]");
    assert!(
        output.is_empty(),
        "a job that never started produced no output: {output:?}"
    );
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_NOT_FOUND, "{message}");
    assert!(message.contains("scala-cli"), "{message}");
    // SAFETY: the service is live; the job settled above.
    unsafe {
        assert_eq!(vd_scala_run_cancel(service, job), 1);
        assert_eq!(vd_scala_run_release(service, job), 1);
    }

    // --- 3. a REAL short-lived child: streamed output + its own exit code ---
    let real = List::new(&[
        "/bin/sh",
        "-c",
        "printf 'hello-from-the-scala-child\\n'; exit 3",
    ]);
    let real_options = VdScalaRunOptions {
        root: ptr::null(),
        argv: real.list,
        env: empty_list(),
        timeout_ms: 0,
        merge_stderr: 1,
    };
    // SAFETY: the service is live; every pointer outlives the call.
    let job = unsafe { vd_scala_run_start(service, &real_options) };
    assert_ne!(job, 0);
    let (polled, status, output, tool) =
        poll_scala_run(service, job, Instant::now() + Duration::from_secs(30));
    assert_eq!(
        polled, VD_STATUS_READY,
        "a real child reaches a terminal state"
    );
    assert_eq!(status.state, 1, "exited");
    assert_eq!(status.exit_code, 3, "the child's own exit code is reported");
    assert_eq!(tool, "/bin/sh", "the ABI reports argv[0]");
    assert!(
        output.contains("hello-from-the-scala-child"),
        "the child's output was streamed: {output:?}"
    );
    // SAFETY: the service is live; the job settled above.
    unsafe {
        assert_eq!(vd_scala_run_cancel(service, job), 1);
        assert_eq!(vd_scala_run_release(service, job), 1);
    }

    // --- 4. cancel ends the whole process TREE -------------------------------
    // The fixture prints its own pid and the pid of a background grandchild, so
    // this asserts the TREE, not just the direct child. The lane's probe proves
    // the same at the UI level (checks 33/34 of the evidence log).
    let tree = List::new(&["/bin/sh", "-c", "echo $$; sleep 60 & echo $!; wait"]);
    let tree_options = VdScalaRunOptions {
        root: ptr::null(),
        argv: tree.list,
        env: empty_list(),
        timeout_ms: 0,
        merge_stderr: 1,
    };
    // SAFETY: the service is live; every pointer outlives the call.
    let job = unsafe { vd_scala_run_start(service, &tree_options) };
    assert_ne!(job, 0);
    let mut seen = String::new();
    let mut pids: Vec<u32> = Vec::new();
    let deadline = Instant::now() + Duration::from_secs(10);
    while pids.len() < 2 {
        assert!(
            Instant::now() < deadline,
            "the fixture printed no pids: {seen:?}"
        );
        // SAFETY: the struct is a POD of plain values and null pointers.
        let mut status = unsafe { std::mem::zeroed::<VdScalaRunStatus>() };
        // SAFETY: the service is live and `status` is writable.
        let polled = unsafe { vd_scala_run_poll(service, job, &mut status) };
        // SAFETY: the poll filled the strings, each released here exactly once.
        unsafe {
            if !status.output.is_null() && status.output_len > 0 {
                seen.push_str(&read_owned(status.output));
            }
            vd_scala_run_status_free(&mut status);
        }
        assert_eq!(
            polled, VD_STATUS_PENDING,
            "the fixture must still be running"
        );
        pids = seen
            .lines()
            .filter_map(|line| line.trim().parse::<u32>().ok())
            .take(2)
            .collect();
        std::thread::sleep(Duration::from_millis(5));
    }
    let (child_pid, grandchild_pid) = (pids[0], pids[1]);
    assert!(
        process_is_running(child_pid),
        "child {child_pid} runs before the cancel"
    );
    assert!(
        process_is_running(grandchild_pid),
        "grandchild {grandchild_pid} runs before the cancel"
    );
    // SAFETY: the service is live; the job is running.
    assert_eq!(unsafe { vd_scala_run_cancel(service, job) }, 1);
    let (polled, status, _output, _tool) =
        poll_scala_run(service, job, Instant::now() + Duration::from_secs(30));
    assert_eq!(
        polled, VD_STATUS_FAILED,
        "a cancelled run reports the FAILED status code"
    );
    assert_eq!(status.state, 3, "cancelled");
    assert_eq!(status.exit_code, -1, "a cancelled run has no exit code");
    assert!(
        wait_until(Duration::from_secs(10), || {
            !process_is_running(child_pid) && !process_is_running(grandchild_pid)
        }),
        "cancel must kill the process tree (child {child_pid}, grandchild {grandchild_pid})"
    );
    // SAFETY: the service is live; the job is terminal.
    unsafe {
        assert_eq!(vd_scala_run_release(service, job), 1);
        vd_service_destroy(service);
    }
}

/// A verible waiver is one exact line, written once, and the file keeps whatever
/// the user already had.
#[test]
fn waiver_append_writes_one_verible_line_and_dedupes_it() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let path = dir.path().join(".verible.waiver");
    let existing = "waive --rule=line-length --line=3 --location=\"(^|/)keep[.]sv$\"\n";
    if let Err(error) = std::fs::write(&path, existing) {
        panic!("writing the fixture: {error}");
    }
    let path_text = CString::new(path.display().to_string()).unwrap_or_default();
    let file = CString::new("rtl/a.sv").unwrap_or_default();
    let rule = CString::new("module-filename").unwrap_or_default();
    let entry = VdWaiverEntry {
        rule: rule.as_ptr(),
        file: file.as_ptr(),
        line: 7,
        line_end: 0,
        option_name: ptr::null(),
        location: ptr::null(),
    };
    let options = VdWaiverOptions {
        dedupe: 1,
        create_parent_dirs: 0,
    };
    // SAFETY: a live service; every pointer outlives the call.
    let service = vd_service_create();
    let mut result = unsafe { std::mem::zeroed::<VdWaiverResult>() };

    // SAFETY: the service is live and the strings outlive the call.
    let job = unsafe {
        vd_waiver_append_start(
            service,
            path_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            &entry,
            &options,
        )
    };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, job, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 1);
    assert_eq!(result.deduped, 0);
    assert_eq!(result.entries, 2, "the user's line is preserved");
    // SAFETY: `content` is an owned C string filled by the poll.
    let content = read_owned(result.content);
    assert!(
        content.contains("waive --rule=module-filename --line=7 --location=\"(^|/)a\\.sv$\""),
        "{content}"
    );
    let on_disk = std::fs::read_to_string(&path).unwrap_or_else(|error| panic!("{error}"));
    assert!(
        on_disk.contains("line-length"),
        "existing content kept: {on_disk}"
    );
    assert_eq!(on_disk.lines().count(), 2, "{on_disk}");
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // The same entry again: nothing new, nothing written.
    // SAFETY: the service is live and the strings outlive the call.
    let again = unsafe {
        vd_waiver_append_start(
            service,
            path_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            &entry,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, again, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 0);
    assert_eq!(result.deduped, 1);
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // A line range is one `--line=N:M` value.
    let range = VdWaiverEntry {
        line: 3,
        line_end: 5,
        ..entry
    };
    // SAFETY: the service is live and the strings outlive the call.
    let ranged = unsafe {
        vd_waiver_append_start(
            service,
            path_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            &range,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, ranged, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    // SAFETY: `content` is an owned C string filled by the poll.
    let content = read_owned(result.content);
    assert!(content.contains("--line=3:5"), "{content}");
    // SAFETY: the result is owned by this test.
    unsafe {
        vd_waiver_result_free(&mut result);
        vd_service_destroy(service);
    }
}

/// A slang waiver is a `[[waivers]]` block that slang itself loads; a file slang
/// would refuse, and an entry without an option name, are refused here.
#[test]
fn waiver_append_writes_a_slang_block_and_refuses_an_unloadable_file() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let path = dir.path().join("slang.waivers.toml");
    let path_text = CString::new(path.display().to_string()).unwrap_or_default();
    let file = CString::new("rtl/a.sv").unwrap_or_default();
    let option = CString::new("unused-variable").unwrap_or_default();
    let entry = VdWaiverEntry {
        rule: ptr::null(),
        file: file.as_ptr(),
        line: 0,
        line_end: 0,
        option_name: option.as_ptr(),
        location: ptr::null(),
    };
    let options = VdWaiverOptions {
        dedupe: 1,
        create_parent_dirs: 0,
    };
    // SAFETY: a live service.
    let service = vd_service_create();
    let mut result = unsafe { std::mem::zeroed::<VdWaiverResult>() };

    // SAFETY: the service is live and every string outlives the call.
    let job = unsafe {
        vd_waiver_append_start(
            service,
            path_text.as_ptr(),
            VD_WAIVER_FORMAT_SLANG,
            &entry,
            &options,
        )
    };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, job, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 1);
    // SAFETY: `content` is an owned C string filled by the poll.
    let content = read_owned(result.content);
    assert!(content.contains("[[waivers]]"), "{content}");
    assert!(content.contains("file = \"**/rtl/a.sv\""), "{content}");
    assert!(
        content.contains("diagnostic = \"unused-variable\""),
        "slang wants the warning option name, not the JSON code: {content}"
    );
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // Dedupe: the identical block is not written twice.
    // SAFETY: the service is live and every string outlives the call.
    let again = unsafe {
        vd_waiver_append_start(
            service,
            path_text.as_ptr(),
            VD_WAIVER_FORMAT_SLANG,
            &entry,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, again, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.deduped, 1);
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // An entry with no option name is refused with the AC-5.8 explanation, before
    // any job starts.
    let nameless = VdWaiverEntry {
        option_name: ptr::null(),
        ..entry
    };
    // SAFETY: the service is live and every string outlives the call.
    unsafe {
        assert_eq!(
            vd_waiver_append_start(
                service,
                path_text.as_ptr(),
                VD_WAIVER_FORMAT_SLANG,
                &nameless,
                &options
            ),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("whole scope"), "{message}");
    }

    // slang has no line scope, so a line number is refused rather than dropped.
    let with_line = VdWaiverEntry { line: 3, ..entry };
    // SAFETY: the service is live and every string outlives the call.
    unsafe {
        assert_eq!(
            vd_waiver_append_start(
                service,
                path_text.as_ptr(),
                VD_WAIVER_FORMAT_SLANG,
                &with_line,
                &options
            ),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("no line scope"), "{message}");
    }

    // A file slang would refuse (unknown key) is left untouched.
    let broken = dir.path().join("broken.toml");
    if let Err(error) = std::fs::write(&broken, "[[waivers]]\nbogus = \"x\"\n") {
        panic!("writing the fixture: {error}");
    }
    let broken_text = CString::new(broken.display().to_string()).unwrap_or_default();
    // SAFETY: the service is live and every string outlives the call.
    let refused = unsafe {
        vd_waiver_append_start(
            service,
            broken_text.as_ptr(),
            VD_WAIVER_FORMAT_SLANG,
            &entry,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, refused, &mut result) });
    assert_eq!(status, VD_STATUS_FAILED);
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_SERVICE, "{message}");
    assert!(message.contains("unknown key"), "{message}");
    let untouched = std::fs::read_to_string(&broken).unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(untouched, "[[waivers]]\nbogus = \"x\"\n");
    // SAFETY: the service is live.
    unsafe { vd_service_destroy(service) };
}

/// The diagnostics of a generate request: one warning with an option name, one
/// error without one, and one row with no file at all.
fn generate_fixture() -> VdDiagnostics {
    let rows = vec![
        VdDiagnostic {
            severity: 1,
            line: 4,
            column: 2,
            end_line: 4,
            end_column: 9,
            code: owned_raw("ImplicitNet"),
            message: owned_raw("implicit net `n`"),
            file: owned_raw("rtl/a.sv"),
            key: ptr::null_mut(),
            origin: 2,
            option_name: owned_raw("implicit-net"),
        },
        VdDiagnostic {
            severity: 0,
            line: 9,
            column: 0,
            end_line: 9,
            end_column: 4,
            code: owned_raw("UndeclaredIdentifier"),
            message: owned_raw("undeclared `x`"),
            file: owned_raw("rtl/a.sv"),
            key: ptr::null_mut(),
            origin: 2,
            option_name: ptr::null_mut(),
        },
        VdDiagnostic {
            severity: 1,
            line: 0,
            column: 0,
            end_line: 0,
            end_column: 0,
            code: owned_raw("UnusedVariable"),
            message: owned_raw("nowhere"),
            file: ptr::null_mut(),
            key: ptr::null_mut(),
            origin: 2,
            option_name: owned_raw("unused-variable"),
        },
    ];
    VdDiagnostics {
        count: rows.len(),
        items: Box::into_raw(rows.into_boxed_slice()) as *mut VdDiagnostic,
    }
}

/// Generate: rows with an option name become slang rules, a row without one is
/// counted and explained (AC-5.8), and a null path writes nothing.
#[test]
fn waiver_generate_builds_rules_and_reports_skipped_rows() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let mut diagnostics = generate_fixture();
    let options = VdWaiverOptions {
        dedupe: 1,
        create_parent_dirs: 0,
    };
    // SAFETY: a live service.
    let service = vd_service_create();
    let mut result = unsafe { std::mem::zeroed::<VdWaiverResult>() };

    // A null path is a preview: nothing is written.
    // SAFETY: the service is live; every pointer outlives the call.
    let preview = unsafe {
        vd_waiver_generate_start(
            service,
            ptr::null(),
            VD_WAIVER_FORMAT_SLANG,
            &diagnostics,
            &options,
        )
    };
    assert_ne!(preview, 0);
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, preview, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 0, "a preview writes nothing");
    assert_eq!(result.entries, 1);
    assert_eq!(result.skipped, 2, "the error row and the file-less row");
    // SAFETY: `content` is an owned C string filled by the poll.
    let content = read_owned(result.content);
    assert!(
        content.contains("diagnostic = \"implicit-net\""),
        "{content}"
    );
    assert!(
        !content.contains("unused-variable"),
        "a row without a file must not become a whole-scope rule: {content}"
    );
    // SAFETY: the result is owned by this test.
    unsafe {
        assert_eq!(result.warnings.count, 2, "one explanation per skipped row");
        vd_waiver_result_free(&mut result);
    }

    // With a path the same rules are written.
    let slang_text =
        CString::new(dir.path().join("slang.toml").display().to_string()).unwrap_or_default();
    // SAFETY: the service is live; every pointer outlives the call.
    let written = unsafe {
        vd_waiver_generate_start(
            service,
            slang_text.as_ptr(),
            VD_WAIVER_FORMAT_SLANG,
            &diagnostics,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, written, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 1);
    let on_disk = std::fs::read_to_string(dir.path().join("slang.toml"))
        .unwrap_or_else(|error| panic!("{error}"));
    assert!(on_disk.contains("[[waivers]]"), "{on_disk}");
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // Verible: every row becomes its own exact line, so nothing is skipped.
    let verible_text =
        CString::new(dir.path().join("verible.waiver").display().to_string()).unwrap_or_default();
    // SAFETY: the service is live; every pointer outlives the call.
    let verible = unsafe {
        vd_waiver_generate_start(
            service,
            verible_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            &diagnostics,
            &options,
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, verible, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 1);
    assert_eq!(result.skipped, 1, "only the file-less row is skipped");
    let on_disk = std::fs::read_to_string(dir.path().join("verible.waiver"))
        .unwrap_or_else(|error| panic!("{error}"));
    assert!(
        on_disk.contains("waive --rule=implicit-net --line=5 --location=\"(^|/)a\\.sv$\""),
        "{on_disk}"
    );
    assert!(
        on_disk.contains("waive --rule=UndeclaredIdentifier --line=10"),
        "a verible code IS its rule name: {on_disk}"
    );
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // SAFETY: the diagnostics were built by this fixture and are released once.
    unsafe {
        vd_diagnostics_free(&mut diagnostics);
        vd_service_destroy(service);
    }
}

/// Register: the verible list gains one path once; slang's single key is set.
#[test]
fn waiver_register_updates_the_documented_settings_keys() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let settings_path = dir.path().join("settings.toml");
    let settings_text = CString::new(settings_path.display().to_string()).unwrap_or_default();
    let waiver_path = dir.path().join(".verible.waiver");
    let waiver_text = CString::new(waiver_path.display().to_string()).unwrap_or_default();
    // SAFETY: a live service.
    let service = vd_service_create();
    let mut result = unsafe { std::mem::zeroed::<VdWaiverResult>() };

    // SAFETY: the service is live and every string outlives the call.
    let job = unsafe {
        vd_waiver_register_start(
            service,
            settings_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            waiver_text.as_ptr(),
        )
    };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, job, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 1);
    assert_eq!(result.entries, 1);
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };
    let stored = std::fs::read_to_string(&settings_path).unwrap_or_else(|error| panic!("{error}"));
    assert!(stored.contains("lint.verible.waiver_files"), "{stored}");
    assert!(
        stored.contains(&waiver_path.display().to_string()),
        "{stored}"
    );

    // Registering the same path again writes nothing.
    // SAFETY: the service is live and every string outlives the call.
    let again = unsafe {
        vd_waiver_register_start(
            service,
            settings_text.as_ptr(),
            VD_WAIVER_FORMAT_VERIBLE,
            waiver_text.as_ptr(),
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, again, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(result.written, 0);
    assert_eq!(result.deduped, 1);
    // SAFETY: the result is owned by this test.
    unsafe { vd_waiver_result_free(&mut result) };

    // The slang key holds exactly one path.
    // SAFETY: the service is live and every string outlives the call.
    let slang = unsafe {
        vd_waiver_register_start(
            service,
            settings_text.as_ptr(),
            VD_WAIVER_FORMAT_SLANG,
            waiver_text.as_ptr(),
        )
    };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_waiver_poll(service, slang, &mut result) });
    assert_eq!(status, VD_STATUS_READY);
    let stored = std::fs::read_to_string(&settings_path).unwrap_or_else(|error| panic!("{error}"));
    assert!(stored.contains("lint.slang.waiver_file"), "{stored}");
    // SAFETY: the result is owned by this test.
    unsafe {
        vd_waiver_result_free(&mut result);
        vd_service_destroy(service);
    }
}

/// The lint job's new knobs: batch 20261110b WIRES them, so this test asserts
/// the TRANSPORT (the ABI value reaches the engine's argv) instead of the two
/// rounds of refusal the earlier contract pinned. The refusal path that remains
/// is real argument validation: an unknown `--autofix` code and a waiver TOML
/// slang would reject are typed `VD_ERR_INVALID_ARGUMENT` errors, never a job.
/// The contract change is recorded in
/// `evidence/batch-20261110b/lint-engine/AUTHORIZATION.md`.
#[test]
fn lint_autofix_and_slang_waiver_knobs_are_refused_until_wired() {
    let file = CString::new("top.sv").unwrap_or_default();
    let files = List::new(&["top.sv"]);
    let output = CString::new("/tmp/autofix.patch").unwrap_or_default();
    // SAFETY: a live service.
    let service = vd_service_create();

    let base = VdLintBatchOptions {
        engine: 1,
        slang_binary: ptr::null(),
        verible_binary: ptr::null(),
        rules_path: ptr::null(),
        include_dirs: empty_list(),
        defines: empty_list(),
        timeout_ms: 0,
        max_diagnostics: 0,
        ruleset: 0,
        rules: ptr::null(),
        waiver_files: ptr::null(),
        slang_suppressions: ptr::null(),
        error_limit: 0,
        verible_autofix: 0,
        autofix_output_file: ptr::null(),
        slang_waiver_file: ptr::null(),
    };

    /// The argv echo the product's preview shows — the SAME struct the batch
    /// start consumes, so this is the real mapping, not a test-local copy.
    fn echo(batch: &VdLintBatchOptions, file: &CStr) -> String {
        // SAFETY: every pointer in `batch` is null or valid for this call.
        let line = unsafe { vd_lint_batch_command_line(batch, file.as_ptr()) };
        assert!(!line.is_null(), "the argv echo must exist for a wired knob");
        // SAFETY: the returned string is owned by this call.
        let text = unsafe { CStr::from_ptr(line) }
            .to_string_lossy()
            .into_owned();
        // SAFETY: the string was produced by this ABI and is freed once.
        unsafe { vd_ffi::capi::vd_string_free(line) };
        let argv = text.split(" | ").next().unwrap_or("").to_string();
        argv
    }

    // (1) --autofix=patch rides in ONE argv element.
    let with_autofix = VdLintBatchOptions {
        verible_autofix: 1,
        ..base
    };
    let argv = echo(&with_autofix, &file);
    assert!(argv.contains("--autofix=patch"), "{argv}");

    // (2) the output file is transported next to the mode.
    let with_output = VdLintBatchOptions {
        verible_autofix: 5,
        autofix_output_file: output.as_ptr(),
        ..base
    };
    let argv = echo(&with_output, &file);
    assert!(argv.contains("--autofix=generate-waiver"), "{argv}");
    assert!(
        argv.contains("--autofix_output_file=/tmp/autofix.patch"),
        "{argv}"
    );

    // (3) slang's own `--waiver-file <toml>`: TWO argv tokens, and the document
    // is LOADED + validated (a file slang would refuse is a typed error).
    let directory = std::env::temp_dir();
    let waiver_path = directory.join(format!("vd-ffi-waiver-{}.toml", std::process::id()));
    std::fs::write(
        &waiver_path,
        "[[waivers]]\nfile = \"top.sv\"\ndiagnostic = \"implicit-net\"\n",
    )
    .unwrap_or_else(|error| panic!("write the waiver fixture: {error}"));
    let waiver = CString::new(waiver_path.to_string_lossy().into_owned()).unwrap_or_default();
    let with_waiver = VdLintBatchOptions {
        engine: 0,
        slang_waiver_file: waiver.as_ptr(),
        ..base
    };
    let argv = echo(&with_waiver, &file);
    assert!(
        argv.contains(&format!("--waiver-file {}", waiver_path.display())),
        "{argv}"
    );

    let bad_path = directory.join(format!("vd-ffi-bad-waiver-{}.toml", std::process::id()));
    std::fs::write(&bad_path, "[[waivers]]\nfile = \"top.sv\"\nbogus = \"x\"\n")
        .unwrap_or_else(|error| panic!("write the bad fixture: {error}"));
    let bad = CString::new(bad_path.to_string_lossy().into_owned()).unwrap_or_default();
    let with_bad_waiver = VdLintBatchOptions {
        engine: 0,
        slang_waiver_file: bad.as_ptr(),
        ..base
    };
    // SAFETY: the service is live; every pointer outlives the call.
    unsafe {
        assert_eq!(
            vd_deeplint_batch_start(service, &files.list, &with_bad_waiver),
            0,
            "a waiver document slang would refuse must never reach a job"
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("unknown key"), "{message}");

        // An unknown autofix code is rejected before any child starts.
        let invalid = VdLintBatchOptions {
            verible_autofix: 6,
            ..base
        };
        assert_eq!(vd_deeplint_batch_start(service, &files.list, &invalid), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("verible_autofix"), "{message}");

        vd_service_destroy(service);
    }
    let _ = std::fs::remove_file(&waiver_path);
    let _ = std::fs::remove_file(&bad_path);
}

/// Borrowed rows for `vd_env_selection_save`, with the backing strings kept alive
/// for the duration of the call (the ABI borrows, it never takes ownership).
struct SelectionRows {
    _layers: Vec<Option<CString>>,
    _names: Vec<CString>,
    _values: Vec<CString>,
    rows: Vec<VdEnvSelectionInput>,
}

impl SelectionRows {
    fn new(rows: &[(Option<&str>, &str, &str)]) -> Self {
        let mut layers: Vec<Option<CString>> = Vec::with_capacity(rows.len());
        let mut names: Vec<CString> = Vec::with_capacity(rows.len());
        let mut values: Vec<CString> = Vec::with_capacity(rows.len());
        for (layer, name, value) in rows {
            layers.push(layer.map(|text| CString::new(text).unwrap_or_default()));
            names.push(CString::new(*name).unwrap_or_default());
            values.push(CString::new(*value).unwrap_or_default());
        }
        let rows = names
            .iter()
            .zip(values.iter())
            .zip(layers.iter())
            .map(|((name, value), layer)| VdEnvSelectionInput {
                project_root: layer.as_ref().map_or(ptr::null(), |text| text.as_ptr()),
                name: name.as_ptr(),
                value: value.as_ptr(),
            })
            .collect();
        Self {
            _layers: layers,
            _names: names,
            _values: values,
            rows,
        }
    }
}

/// Read one `VdStringList` a poll filled in.
fn list_texts(list: &VdStringList) -> Vec<String> {
    if list.items.is_null() {
        return Vec::new();
    }
    // SAFETY: a list filled by a poll holds `count` valid strings.
    let items = unsafe { std::slice::from_raw_parts(list.items, list.count) };
    items.iter().map(|item| read_owned(*item)).collect()
}

/// Poll a selection job to completion; the caller owns the returned struct and
/// must release it with `vd_env_selection_free`.
fn poll_selection(service: *mut VdService, job: u64) -> (i32, VdEnvSelection) {
    // SAFETY: every field is an integer or a pointer, so zeroed is the empty
    // result (a null pointer with a zero count is the empty list).
    let mut out = unsafe { std::mem::zeroed::<VdEnvSelection>() };
    // SAFETY: the service is live and `out` is writable.
    let status = wait_for(|| unsafe { vd_env_selection_poll(service, job, &mut out) });
    (status, out)
}

/// The rows one selection poll filled in, as `(layer, name, value)`.
fn selection_rows(selection: &VdEnvSelection) -> Vec<(Option<String>, String, String)> {
    if selection.items.is_null() {
        return Vec::new();
    }
    // SAFETY: a filled result holds `count` rows.
    let rows = unsafe { std::slice::from_raw_parts(selection.items, selection.count) };
    rows.iter()
        .map(|row| {
            (
                if row.project_root.is_null() {
                    None
                } else {
                    Some(read_owned(row.project_root))
                },
                read_owned(row.name),
                read_owned(row.value),
            )
        })
        .collect()
}

/// Assert a save refused on the calling thread: a `0` job id, a typed code and a
/// message naming the problem.
fn assert_save_refused(
    service: *mut VdService,
    path: *const c_char,
    rows: &SelectionRows,
    needle: &str,
) {
    // SAFETY: a live service; the path and rows outlive the call.
    let job = unsafe { vd_env_selection_save(service, path, rows.rows.as_ptr(), rows.rows.len()) };
    assert_eq!(job, 0, "a refused row set must not start a job");
    let (code, message) = last_error(service);
    assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
    assert!(message.contains(needle), "{message}");
}

/// The selection store (batch 20261110c): the two layers the Settings page writes
/// survive a real save/load round trip byte for byte — including a value that
/// contains `=` and a newline (AC-4.9) — and the file on disk stays the store's
/// own documented JSON form, so the Qt page never re-implements it.
#[test]
fn env_selection_round_trips_both_layers_through_the_real_store() {
    let Ok(dir) = tempfile::tempdir() else {
        panic!("cannot create a temporary directory");
    };
    let path = dir.path().join("environment.json");
    let Ok(path_text) = CString::new(path.to_string_lossy().into_owned()) else {
        panic!("the fixture path has no interior NUL");
    };

    let service = vd_service_create();
    assert!(!service.is_null());

    let rows = SelectionRows::new(&[
        (None, "PATH", "/opt/tools/bin:/usr/bin"),
        (None, "API_TOKEN", "secret=part\nsecond line"),
        (Some("/tmp/project-a"), "WIDTH", "8"),
        (Some("/tmp/project-b"), "WIDTH", "16"),
    ]);
    // SAFETY: a live service; the path and the rows outlive the call.
    let job = unsafe {
        vd_env_selection_save(
            service,
            path_text.as_ptr(),
            rows.rows.as_ptr(),
            rows.rows.len(),
        )
    };
    assert_ne!(
        job,
        0,
        "a valid save gets a job id: {:?}",
        last_error(service)
    );
    let (status, mut saved) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    assert_eq!(saved.count, 0, "a save produces no rows");
    // SAFETY: `saved` was filled by the poll and is owned here.
    unsafe {
        vd_env_selection_free(&mut saved);
        assert_eq!(vd_env_selection_release(service, job), 1);
        assert_eq!(vd_env_selection_release(service, job), 0);
    }

    // The file is the store's own JSON form (version 1, global + projects).
    let on_disk = std::fs::read_to_string(&path).unwrap_or_else(|error| panic!("{error}"));
    let document: serde_json::Value =
        serde_json::from_str(&on_disk).unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(document["version"].as_i64(), Some(1));
    assert_eq!(
        document["global"].as_array().map(|rows| rows.len()),
        Some(2)
    );
    assert_eq!(
        document["projects"].as_array().map(|rows| rows.len()),
        Some(2)
    );

    // Load it back: layers, rows and order are identical.
    // SAFETY: a live service; the path outlives the call.
    let job = unsafe { vd_env_selection_load(service, path_text.as_ptr()) };
    assert_ne!(job, 0);
    let (status, mut loaded) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    assert_eq!(
        selection_rows(&loaded),
        vec![
            (
                None,
                "PATH".to_string(),
                "/opt/tools/bin:/usr/bin".to_string()
            ),
            (
                None,
                "API_TOKEN".to_string(),
                "secret=part\nsecond line".to_string()
            ),
            (
                Some("/tmp/project-a".to_string()),
                "WIDTH".to_string(),
                "8".to_string()
            ),
            (
                Some("/tmp/project-b".to_string()),
                "WIDTH".to_string(),
                "16".to_string()
            ),
        ],
        "both layers round trip exactly, global first"
    );
    assert_eq!(
        list_texts(&loaded.warnings),
        Vec::<String>::new(),
        "a well-formed store warns about nothing"
    );
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut loaded);
        assert_eq!(vd_env_selection_release(service, job), 1);
    }

    // A save REPLACES the file: the contract is `EnvironmentSelection::save`, so
    // a caller that keeps a layer must load it first and pass its rows back.
    let replacement = SelectionRows::new(&[(None, "ONLY_GLOBAL", "1")]);
    // SAFETY: same contract as the first save.
    let job = unsafe {
        vd_env_selection_save(
            service,
            path_text.as_ptr(),
            replacement.rows.as_ptr(),
            replacement.rows.len(),
        )
    };
    assert_ne!(job, 0);
    let (status, mut saved) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut saved);
        vd_env_selection_release(service, job);
    }
    // SAFETY: a live service; the path outlives the call.
    let job = unsafe { vd_env_selection_load(service, path_text.as_ptr()) };
    let (status, mut loaded) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    assert_eq!(
        selection_rows(&loaded),
        vec![(None, "ONLY_GLOBAL".to_string(), "1".to_string())],
        "a save is a whole-file write, not a merge"
    );
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut loaded);
        vd_env_selection_release(service, job);
        vd_service_destroy(service);
    }
}

/// A broken or missing store never fails the page (AC-4.4): the load job answers
/// READY with the empty selection, a warning names the problem, the broken file
/// is left untouched, and a missing file is the normal first run with no warning.
#[test]
fn env_selection_load_degrades_a_broken_or_missing_store() {
    let Ok(dir) = tempfile::tempdir() else {
        panic!("cannot create a temporary directory");
    };
    let broken = dir.path().join("broken.json");
    let broken_text = "{ this is not json";
    if let Err(error) = std::fs::write(&broken, broken_text) {
        panic!("{error}");
    }
    let Ok(broken_c) = CString::new(broken.to_string_lossy().into_owned()) else {
        panic!("the fixture path has no interior NUL");
    };
    let missing = dir.path().join("not-there.json");
    let Ok(missing_c) = CString::new(missing.to_string_lossy().into_owned()) else {
        panic!("the fixture path has no interior NUL");
    };

    let service = vd_service_create();
    assert!(!service.is_null());

    // SAFETY: a live service; the path outlives the call.
    let job = unsafe { vd_env_selection_load(service, broken_c.as_ptr()) };
    assert_ne!(job, 0);
    let (status, mut loaded) = poll_selection(service, job);
    assert_eq!(
        status, VD_STATUS_READY,
        "a broken file is a degraded read, not a failed job"
    );
    assert_eq!(loaded.count, 0, "a broken file loads the empty selection");
    let warnings = list_texts(&loaded.warnings);
    assert_eq!(warnings.len(), 1, "{warnings:?}");
    assert!(
        warnings[0].contains("not valid JSON"),
        "the warning must name the problem: {warnings:?}"
    );
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut loaded);
        vd_env_selection_release(service, job);
    }
    let untouched = std::fs::read_to_string(&broken).unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(untouched, broken_text, "a load never writes the store");

    // A missing file is the empty selection with no warning (first run).
    // SAFETY: a live service; the path outlives the call.
    let job = unsafe { vd_env_selection_load(service, missing_c.as_ptr()) };
    assert_ne!(job, 0);
    let (status, mut loaded) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY);
    assert_eq!(loaded.count, 0);
    assert_eq!(
        list_texts(&loaded.warnings),
        Vec::<String>::new(),
        "a missing file is normal, not a warning"
    );
    assert!(!missing.exists(), "the load must not create the file");
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut loaded);
        vd_env_selection_release(service, job);
        vd_service_destroy(service);
    }
}

/// The save boundary: every bad argument is refused on the CALLING thread with a
/// typed code and a `0` job id (so nothing was written), a value is accepted byte
/// for byte, the same name in two layers is legitimate, and the poll/release/free
/// contract is the lane's own.
#[test]
fn env_selection_save_validates_its_rows_before_any_job() {
    let Ok(dir) = tempfile::tempdir() else {
        panic!("cannot create a temporary directory");
    };
    let path = dir.path().join("environment.json");
    let Ok(path_c) = CString::new(path.to_string_lossy().into_owned()) else {
        panic!("the fixture path has no interior NUL");
    };
    let Ok(empty_c) = CString::new("") else {
        panic!("an empty literal has no interior NUL");
    };
    // A path that is not UTF-8 (a lone 0xFF byte) must be refused, not lossily
    // accepted.
    let Ok(not_utf8_c) = CString::new(vec![0x66_u8, 0xFF]) else {
        panic!("the fixture bytes contain no NUL");
    };

    let service = vd_service_create();
    assert!(!service.is_null());

    // SAFETY: a live service; null pointers are the documented invalid path.
    unsafe {
        assert_eq!(
            vd_env_selection_save(service, ptr::null(), ptr::null(), 0),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("path"), "{message}");

        assert_eq!(
            vd_env_selection_save(service, empty_c.as_ptr(), ptr::null(), 0),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("empty"), "{message}");

        assert_eq!(
            vd_env_selection_save(service, not_utf8_c.as_ptr(), ptr::null(), 0),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("UTF-8"), "{message}");

        assert_eq!(
            vd_env_selection_save(service, path_c.as_ptr(), ptr::null(), 1),
            0,
            "a non-zero count with a null entries pointer is refused"
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("entries"), "{message}");
    }

    // A name with '=', an empty name and a duplicate in one layer are all refused
    // before any job exists.
    let bad_equal = SelectionRows::new(&[(None, "BAD=NAME", "x")]);
    assert_save_refused(service, path_c.as_ptr(), &bad_equal, "name");
    let bad_empty = SelectionRows::new(&[(None, "", "x")]);
    assert_save_refused(service, path_c.as_ptr(), &bad_empty, "name");
    let bad_duplicate = SelectionRows::new(&[(None, "WIDTH", "8"), (None, "WIDTH", "16")]);
    assert_save_refused(service, path_c.as_ptr(), &bad_duplicate, "twice");
    assert!(
        !path.exists(),
        "no refused argument may have written the store"
    );

    // A name that is not UTF-8 (a lone 0xFF byte) is refused.
    let Ok(bad_name) = CString::new(vec![0x41_u8, 0xFF]) else {
        panic!("the fixture bytes contain no NUL");
    };
    let value = CString::new("1").unwrap_or_default();
    let invalid_name_row = VdEnvSelectionInput {
        project_root: ptr::null(),
        name: bad_name.as_ptr(),
        value: value.as_ptr(),
    };
    // SAFETY: a live service; every pointer outlives the call.
    unsafe {
        assert_eq!(
            vd_env_selection_save(service, path_c.as_ptr(), &invalid_name_row, 1),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
        assert!(message.contains("UTF-8"), "{message}");
    }

    // The same name in two DIFFERENT layers is legitimate, and a value keeps its
    // `=` and newline exactly (AC-4.9).
    let legal = SelectionRows::new(&[
        (None, "WIDTH", "8"),
        (Some("/tmp/project-a"), "WIDTH", "16"),
        (None, "MOTD", "line=one\ntwo"),
    ]);
    // SAFETY: a live service; the path and rows outlive the call.
    let job = unsafe {
        vd_env_selection_save(
            service,
            path_c.as_ptr(),
            legal.rows.as_ptr(),
            legal.rows.len(),
        )
    };
    assert_ne!(job, 0, "{:?}", last_error(service));
    let (status, mut saved) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut saved);
        assert_eq!(vd_env_selection_release(service, job), 1);
    }

    // SAFETY: a live service; the path outlives the call.
    let job = unsafe { vd_env_selection_load(service, path_c.as_ptr()) };
    let (status, mut loaded) = poll_selection(service, job);
    assert_eq!(status, VD_STATUS_READY, "{:?}", last_error(service));
    assert_eq!(
        selection_rows(&loaded),
        vec![
            (None, "WIDTH".to_string(), "8".to_string()),
            (None, "MOTD".to_string(), "line=one\ntwo".to_string()),
            (
                Some("/tmp/project-a".to_string()),
                "WIDTH".to_string(),
                "16".to_string()
            ),
        ]
    );
    // SAFETY: filled by the poll and owned here.
    unsafe {
        vd_env_selection_free(&mut loaded);
        assert_eq!(vd_env_selection_release(service, job), 1);
    }

    // The job contract: misuse is typed and never a panic.
    // SAFETY: a live service and a null result pointer are the documented misuse.
    unsafe {
        let mut out = std::mem::zeroed::<VdEnvSelection>();
        assert_eq!(vd_env_selection_poll(service, u64::MAX, &mut out), -1);
        let (code, _) = last_error(service);
        assert_eq!(code, VD_ERR_JOB_UNKNOWN);
        assert_eq!(
            vd_env_selection_poll(service, u64::MAX, ptr::null_mut()),
            -1
        );
        assert_eq!(
            vd_env_selection_poll(ptr::null_mut(), u64::MAX, &mut out),
            -1
        );
        assert_eq!(vd_env_selection_release(service, u64::MAX), 0);
        assert_eq!(vd_env_selection_release(ptr::null_mut(), u64::MAX), 0);
        vd_env_selection_free(ptr::null_mut());
        vd_service_destroy(service);
    }
}
