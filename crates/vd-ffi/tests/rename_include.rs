//! T72 (batch 20261110c): the rename C ABI carries the project's `-I`/`-D`.
//!
//! The fixture is a REAL project that needs `-I inc`: `rtl/pkg.sv` and
//! `rtl/user.sv` include `defs.vh` from `inc/`, and the macro it defines is the
//! width of the signal that gets renamed. The tests therefore prove, end to end
//! through the exported C ABI:
//!
//! * **negative control** — with an EMPTY include-dir list the elaboration fails,
//!   exactly as it did before this card (a project that includes a header from
//!   another directory could not be renamed at all);
//! * **the fix** — with `-I <project>/inc` the index succeeds, the plan lists the
//!   declaration in `pkg.sv` plus the two scoped references in `user.sv` and
//!   `top.sv` (a cross-file change), and the apply verifies its own writes;
//! * the apply touches ONLY the expected files (sha256 before/after), keeps the
//!   `cfg::` scope spelling, and leaves the fixture and `defs.vh` untouched.
//!
//! Every job runs the REAL `slang` binary. When it is missing these tests FAIL
//! loudly: they prove the rename ABI carries `-I`/`-D`, so a run without slang
//! would assert NOTHING and must never be counted as a pass (review T73 F1; the
//! old `SKIP: …; return` printed its line only under `--nocapture` and reported
//! the empty run as `ok. 2 passed`).

use std::ffi::{c_char, CStr, CString};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

use vd_ffi::capi::{
    VdError, VdRenameApplyResult, VdRenameIndex, VdRenameOptions, VdRenamePlan, VdStringList,
    VD_ERR_INVALID_ARGUMENT, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use vd_ffi::rename::{
    vd_rename_apply_poll, vd_rename_apply_result_free, vd_rename_apply_start, vd_rename_index_free,
    vd_rename_index_poll, vd_rename_index_start, vd_rename_plan_free, vd_rename_plan_poll,
    vd_rename_plan_start, vd_rename_release,
};
use vd_ffi::service::{vd_service_create, vd_service_destroy, vd_service_error, VdService};

// --- shared helpers ---------------------------------------------------------

/// Strings plus the pointer buffer a [`VdStringList`] borrows.
struct List {
    _owned: Vec<CString>,
    _raw: Vec<*mut c_char>,
    list: VdStringList,
}

impl List {
    fn new(values: &[String]) -> Self {
        let owned: Vec<CString> = values
            .iter()
            .map(|value| {
                CString::new(value.as_str())
                    .unwrap_or_else(|_| panic!("a fixture path must not contain an interior NUL"))
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

fn wait_for<F: FnMut() -> i32>(mut check: F) -> i32 {
    let deadline = Instant::now() + Duration::from_secs(120);
    loop {
        let status = check();
        if status != VD_STATUS_PENDING {
            return status;
        }
        assert!(Instant::now() < deadline, "the rename job never settled");
        std::thread::sleep(Duration::from_millis(5));
    }
}

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
unsafe fn read_owned(text: *mut c_char) -> String {
    if text.is_null() {
        return String::new();
    }
    // SAFETY: stated above.
    unsafe { CStr::from_ptr(text) }
        .to_string_lossy()
        .into_owned()
}

/// The pinned slang binary: `VEDIT_SLANG`, else the checkout's build artifact.
fn slang_binary() -> Option<PathBuf> {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        // EXPLICIT and therefore authoritative: a set-but-missing path is a
        // configuration error (reported by `require_slang`), not a reason to
        // silently fall back to some other slang.
        let path = PathBuf::from(value);
        return path.is_file().then_some(path);
    }
    let candidate = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../build/slang/out/slang")
        .canonicalize()
        .ok()?;
    candidate.is_file().then_some(candidate)
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/rename-include")
}

/// Copy the fixture into a fresh directory (the apply WRITES files, so the
/// checked-in fixture must never be the target).
fn copy_tree(from: &Path, to: &Path) {
    std::fs::create_dir_all(to).unwrap_or_else(|error| panic!("mkdir {}: {error}", to.display()));
    let entries =
        std::fs::read_dir(from).unwrap_or_else(|error| panic!("read {}: {error}", from.display()));
    for entry in entries {
        let entry = entry.unwrap_or_else(|error| panic!("read {}: {error}", from.display()));
        let source = entry.path();
        let Some(name) = source.file_name() else {
            continue;
        };
        let target = to.join(name);
        if source.is_dir() {
            copy_tree(&source, &target);
        } else {
            std::fs::copy(&source, &target).unwrap_or_else(|error| {
                panic!("copy {} -> {}: {error}", source.display(), target.display())
            });
        }
    }
}

/// `sha256sum` of one file — the acceptance asks for hashes, not just content.
fn sha256(path: &Path) -> String {
    let output = Command::new("sha256sum")
        .arg(path)
        .output()
        .unwrap_or_else(|error| panic!("sha256sum {}: {error}", path.display()));
    assert!(
        output.status.success(),
        "sha256sum {} failed: {}",
        path.display(),
        String::from_utf8_lossy(&output.stderr)
    );
    let text = String::from_utf8_lossy(&output.stdout);
    text.split_whitespace()
        .next()
        .unwrap_or_else(|| panic!("sha256sum {} printed no hash", path.display()))
        .to_string()
}

/// The project's three source files as absolute paths (the ABI's file set).
fn project_files(root: &Path) -> Vec<String> {
    ["rtl/pkg.sv", "rtl/user.sv", "rtl/top.sv"]
        .iter()
        .map(|name| {
            let path: &Path = &root.join(name);
            path.display().to_string()
        })
        .collect()
}

/// A `VdRenameOptions` with whole identifiers, no dry run, engine bounds, and
/// the given project inputs.
fn options(include_dirs: &List, defines: &List) -> VdRenameOptions {
    VdRenameOptions {
        whole_identifier: 1,
        dry_run: 0,
        max_entries_per_file: 0,
        timeout_ms: 60_000,
        include_dirs: include_dirs.list,
        defines: defines.list,
    }
}

/// This test NEEDS the pinned slang: without it NOTHING is asserted, so a
/// missing toolchain is a VISIBLE FAILURE — never a `println!` that only shows
/// under `--nocapture` and never a `test result: ok. 2 passed`.
///
/// The message names the marker (`RENAME-INCLUDE`), what the test proves and the
/// two ways to get the binary, so a red run is self-explanatory in CI output.
fn require_slang() -> PathBuf {
    let Some(slang) = slang_binary() else {
        panic!(
            "RENAME-INCLUDE FAILED: this test needs the pinned `slang` binary and it was not \
             found, so a run here would assert NOTHING (it proves the rename C ABI carries the \
             project's -I/-D). Set VEDIT_SLANG=/path/to/slang, or build the pinned toolchain: \
             `bash build/slang/build.sh` (or `bash scripts/fetch-verible.sh` for the pinned \
             bundle). Looked at $VEDIT_SLANG and build/slang/out/slang."
        );
    };
    // The C ABI resolves its tool like the lint lane does: bundled > checkout >
    // PATH > VEDIT_SLANG. Pin the artifact so the test cannot pick up a
    // different slang from PATH.
    std::env::set_var("VEDIT_SLANG", &slang);
    println!("RENAME-INCLUDE slang={}", slang.display());
    slang
}

// --- the negative control and the fixed index -------------------------------

#[test]
fn the_index_needs_the_project_include_dir_and_succeeds_with_it() {
    let _slang = require_slang();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path().join("project");
    copy_tree(&fixture_dir(), &root);
    let files = List::new(&project_files(&root));
    let root_text = root.display().to_string();
    let root_c = CString::new(root_text.clone()).unwrap_or_else(|error| panic!("root: {error}"));

    let service = vd_service_create();
    assert!(!service.is_null());

    // 1) NEGATIVE CONTROL: the project's include dir is missing, so the
    //    elaboration cannot see `HAS_SHARED`/`CNT_W` and the signal is not
    //    declared at all — the plan has nothing to name and fails.
    let no_dirs = List::new(&[]);
    let no_defines = List::new(&[]);
    let without = options(&no_dirs, &no_defines);
    let from = CString::new("shared_cnt").unwrap_or_default();
    let to = CString::new("shared_cnt_v2").unwrap_or_default();
    // SAFETY: the service is live; every pointer lives through the call.
    let job = unsafe {
        vd_rename_plan_start(
            service,
            root_c.as_ptr(),
            &files.list,
            from.as_ptr(),
            to.as_ptr(),
            &without,
        )
    };
    assert_ne!(job, 0, "the plan request itself is valid");
    let mut plan = unsafe { std::mem::zeroed::<VdRenamePlan>() };
    // SAFETY: the service is live and `plan` is writable.
    let status = wait_for(|| unsafe { vd_rename_plan_poll(service, job, &mut plan) });
    // SAFETY: the service is live.
    let (code, message) = last_error(service);
    println!("T72 negative-control status={status} code={code} message={message}");
    assert_eq!(
        status, VD_STATUS_FAILED,
        "without `-I inc` the rename must fail (it did before T72): {code} {message}"
    );
    assert_eq!(code, VD_ERR_INVALID_ARGUMENT, "{message}");
    assert!(
        message.contains("not declared") || message.contains("nameable"),
        "the failure says there is no declaration: {message}"
    );
    // SAFETY: the service is live; the id is ours.
    unsafe { vd_rename_release(service, job) };

    // 2) THE FIX: the project's own include dir makes the same job succeed.
    let include_dirs = List::new(&[root.join("inc").display().to_string()]);
    let with = options(&include_dirs, &no_defines);
    let mut index = unsafe { std::mem::zeroed::<VdRenameIndex>() };
    // SAFETY: the service is live; every pointer lives through the call.
    let job = unsafe { vd_rename_index_start(service, root_c.as_ptr(), &files.list, &with) };
    assert_ne!(job, 0);
    // SAFETY: the service is live and `index` is writable.
    let status = wait_for(|| unsafe { vd_rename_index_poll(service, job, &mut index) });
    assert_eq!(
        status,
        VD_STATUS_READY,
        "with `-I inc` the index must succeed: {:?}",
        last_error(service)
    );
    println!(
        "T72 index files={} identifiers={} occurrences={} warnings={:?}",
        index.files, index.identifiers, index.occurrences, index.warnings.count
    );
    assert_eq!(index.files, 3, "the three project files were parsed");
    assert!(
        index.occurrences >= 2,
        "the scoped references were indexed: {}",
        index.occurrences
    );
    assert_eq!(index.warnings.count, 0, "the fixed project has no warnings");
    // SAFETY: the result was filled by the poll and is freed once.
    unsafe {
        vd_rename_index_free(&mut index);
        vd_rename_release(service, job);
        vd_service_destroy(service);
    }
}

// --- the cross-file plan and the verified apply -----------------------------

#[test]
fn the_plan_lists_the_cross_file_change_and_the_apply_verifies_it() {
    let _slang = require_slang();
    let temp = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = temp.path().join("project");
    copy_tree(&fixture_dir(), &root);
    let files = List::new(&project_files(&root));
    let include_dirs = List::new(&[root.join("inc").display().to_string()]);
    let no_defines = List::new(&[]);
    let plan_options = options(&include_dirs, &no_defines);
    let root_text = root.display().to_string();
    let root_c = CString::new(root_text.clone()).unwrap_or_else(|error| panic!("root: {error}"));
    let from = CString::new("shared_cnt").unwrap_or_default();
    let to = CString::new("shared_cnt_v2").unwrap_or_default();

    // Hashes BEFORE the apply: only the three source files may change.
    let targets = ["rtl/pkg.sv", "rtl/user.sv", "rtl/top.sv", "inc/defs.vh"];
    let before: Vec<String> = targets
        .iter()
        .map(|name| sha256(&root.join(name)))
        .collect();

    let service = vd_service_create();
    assert!(!service.is_null());

    // 1) PLAN: one declaration in pkg.sv + the scoped reference in user.sv and
    //    the scoped reference in top.sv = a three-file change.
    // SAFETY: the service is live; every pointer lives through the call.
    let job = unsafe {
        vd_rename_plan_start(
            service,
            root_c.as_ptr(),
            &files.list,
            from.as_ptr(),
            to.as_ptr(),
            &plan_options,
        )
    };
    assert_ne!(job, 0, "plan rejected: {:?}", last_error(service));
    let mut plan = unsafe { std::mem::zeroed::<VdRenamePlan>() };
    // SAFETY: the service is live and `plan` is writable.
    let status = wait_for(|| unsafe { vd_rename_plan_poll(service, job, &mut plan) });
    assert_eq!(
        status,
        VD_STATUS_READY,
        "the plan must succeed with `-I inc`: {:?}",
        last_error(service)
    );
    // SAFETY: the plan was filled by the poll and not freed yet.
    let plan_files: Vec<(String, usize)> = unsafe {
        let entries = std::slice::from_raw_parts(plan.items, plan.count);
        entries
            .iter()
            .map(|entry| {
                (
                    read_owned(entry.file)
                        .rsplit('/')
                        .next()
                        .unwrap_or_default()
                        .to_string(),
                    entry.count,
                )
            })
            .collect()
    };
    println!(
        "T72 plan files={} occurrences={} entries={plan_files:?}",
        plan.files, plan.occurrences
    );
    assert_eq!(plan.files, 3, "the three project files were indexed");
    assert_eq!(plan.occurrences, 3, "declaration + two references");
    assert_eq!(plan.count, 3, "three files carry a change");
    let mut sorted_files = plan_files.clone();
    sorted_files.sort();
    assert_eq!(
        sorted_files,
        vec![
            ("pkg.sv".to_string(), 1),
            ("top.sv".to_string(), 1),
            ("user.sv".to_string(), 1),
        ],
        "the plan names the declaration and both scoped references"
    );

    // 2) APPLY: every file or none, and the engine re-reads what it wrote.
    // SAFETY: the service is live; the plan is READ, never consumed.
    let job = unsafe { vd_rename_apply_start(service, &plan, &plan_options) };
    assert_ne!(job, 0, "apply rejected: {:?}", last_error(service));
    let mut result = unsafe { std::mem::zeroed::<VdRenameApplyResult>() };
    // SAFETY: the service is live and `result` is writable.
    let status = wait_for(|| unsafe { vd_rename_apply_poll(service, job, &mut result) });
    assert_eq!(
        status,
        VD_STATUS_READY,
        "the apply must succeed: {:?}",
        last_error(service)
    );
    println!(
        "T72 apply files={} occurrences={} rolled_back={} verified={}",
        result.applied_files, result.applied_occurrences, result.rolled_back, result.verified
    );
    assert_eq!(result.applied_files, 3);
    assert_eq!(result.applied_occurrences, 3);
    assert_eq!(result.rolled_back, 0);
    assert_eq!(result.verified, 1, "the engine re-read every written file");
    assert!(result.failed_file.is_null());

    // 3) sha256: exactly the three planned files changed; the include did not.
    let after: Vec<String> = targets
        .iter()
        .map(|name| sha256(&root.join(name)))
        .collect();
    for (index, name) in targets.iter().enumerate() {
        println!(
            "T72 sha256 {name} before={} after={}",
            before[index], after[index]
        );
    }
    for (index, name) in targets.iter().enumerate() {
        if *name == "inc/defs.vh" {
            assert_eq!(before[index], after[index], "`{name}` must not change");
        } else {
            assert_ne!(before[index], after[index], "`{name}` must change");
        }
    }

    // 4) the scope spelling survives: a scoped reference becomes `cfg::new`,
    //    never a bare replacement that would drop the `cfg::` prefix.
    let user = std::fs::read_to_string(root.join("rtl/user.sv")).unwrap_or_default();
    let top = std::fs::read_to_string(root.join("rtl/top.sv")).unwrap_or_default();
    let package = std::fs::read_to_string(root.join("rtl/pkg.sv")).unwrap_or_default();
    assert!(package.contains("shared_cnt_v2"), "{package}");
    assert!(user.contains("cfg::shared_cnt_v2"), "{user}");
    assert!(top.contains("cfg::shared_cnt_v2"), "{top}");
    assert!(!package.contains("shared_cnt "), "the old name is gone");

    // 5) the checked-in fixture is untouched: every job ran on the copy.
    let original = std::fs::read_to_string(fixture_dir().join("rtl/user.sv"))
        .unwrap_or_else(|error| panic!("read the fixture: {error}"));
    assert!(
        original.contains("assign cfg::shared_cnt = local_bus;") && !original.contains("_v2"),
        "the fixture on disk must never be written"
    );

    // SAFETY: each result was filled by its poll and is freed once.
    unsafe {
        vd_rename_plan_free(&mut plan);
        vd_rename_apply_result_free(&mut result);
        vd_rename_release(service, job);
        vd_service_destroy(service);
    }
}
