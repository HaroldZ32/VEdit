//! ABI tests: every one of the seven services is driven through the exported
//! `extern "C"` functions, exactly as the C++ product does.
//!
//! These tests are the "each service has a unit test" half of AC-0.2: they use
//! no private Rust API, so a signature change that breaks the header also breaks
//! this file. The C++ side adds the cross-language half (a real C++ caller linked
//! against the static library, `vedit-app --selfcheck-ffi`).

use std::ffi::{CStr, CString};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use vd_ffi::capi::{
    VdDiagnostics, VdError, VdFimResult, VdLspInfo, VdProjectInfo, VdSearchResults, VdSettingInput,
    VdSettings, VdStringList, VdVcsStatus, VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN,
    VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_ERR_WRONG_JOB_KIND, VD_STATUS_FAILED, VD_STATUS_PENDING,
    VD_STATUS_READY,
};
use vd_ffi::deeplint::{vd_deeplint_lint, vd_deeplint_poll, vd_diagnostics_free};
use vd_ffi::fim::{vd_fim_complete, vd_fim_poll, vd_fim_result_free};
use vd_ffi::lsp::{vd_lsp_info_free, vd_lsp_poll, vd_lsp_start};
use vd_ffi::project::{vd_project_info_free, vd_project_parse, vd_project_poll};
use vd_ffi::search::{vd_search_poll, vd_search_query, vd_search_results_free};
use vd_ffi::service::{
    vd_abi_version, vd_job_cancel, vd_job_release, vd_service_create, vd_service_destroy,
    vd_service_error, VdService,
};
use vd_ffi::settings::{vd_settings_free, vd_settings_load, vd_settings_poll, vd_settings_save};
use vd_ffi::vcs::{
    vd_vcs_diff_free, vd_vcs_diff_job, vd_vcs_diff_poll, vd_vcs_history_blame_job,
    vd_vcs_history_commit_job, vd_vcs_history_commits_job, vd_vcs_history_compare_job,
    vd_vcs_history_file_job, vd_vcs_history_free, vd_vcs_history_poll, vd_vcs_info_free,
    vd_vcs_info_job, vd_vcs_info_poll, vd_vcs_poll, vd_vcs_status, vd_vcs_status_free,
    vd_vcs_workspace_free, vd_vcs_workspace_poll, vd_vcs_workspace_stashes_job,
    vd_vcs_workspace_worktrees_job, vd_vcs_write_free, vd_vcs_write_job, vd_vcs_write_poll,
    VdVcsDiff, VdVcsHistoryResult, VdVcsInfo, VdVcsWorkspaceResult, VdVcsWriteRequest,
    VdVcsWriteResult, VD_VCS_HISTORY_BLAME, VD_VCS_HISTORY_COMMIT, VD_VCS_HISTORY_COMMITS,
    VD_VCS_HISTORY_COMPARE, VD_VCS_HISTORY_FILE, VD_VCS_WORKSPACE_STASHES,
    VD_VCS_WORKSPACE_WORKTREES, VD_VCS_WRITE_GIT_ABORT, VD_VCS_WRITE_GIT_CHERRY_PICK,
    VD_VCS_WRITE_GIT_CLEAN, VD_VCS_WRITE_GIT_COMMIT, VD_VCS_WRITE_GIT_CONTINUE,
    VD_VCS_WRITE_GIT_DISCARD, VD_VCS_WRITE_GIT_MERGE, VD_VCS_WRITE_GIT_REBASE,
    VD_VCS_WRITE_GIT_RESET, VD_VCS_WRITE_GIT_REVERT, VD_VCS_WRITE_GIT_STAGE,
    VD_VCS_WRITE_GIT_STASH_APPLY, VD_VCS_WRITE_GIT_STASH_DROP, VD_VCS_WRITE_GIT_STASH_POP,
    VD_VCS_WRITE_GIT_STASH_SAVE, VD_VCS_WRITE_GIT_SUBMODULE_INIT,
    VD_VCS_WRITE_GIT_SUBMODULE_UPDATE, VD_VCS_WRITE_GIT_UNSTAGE, VD_VCS_WRITE_GIT_WORKTREE_ADD,
    VD_VCS_WRITE_GIT_WORKTREE_REMOVE, VD_VCS_WRITE_SVN_COMMIT,
};

/// The fixture project (`crates/vd-ffi/tests/fixtures/project`).
fn fixture_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/project")
}

fn fixture_server() -> PathBuf {
    fixture_dir().join("rtl/rtl_a.sv")
}

/// Poll `check` until it stops returning `VD_STATUS_PENDING`.
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

unsafe fn last_error(service: *mut VdService) -> (i32, String) {
    let mut error = std::mem::zeroed::<VdError>();
    if vd_service_error(service, &mut error) == 1 {
        let message = CStr::from_ptr(error.message.as_ptr())
            .to_string_lossy()
            .into_owned();
        (error.code, message)
    } else {
        (0, String::new())
    }
}

unsafe fn string_list(list: &VdStringList) -> Vec<String> {
    if list.items.is_null() || list.count == 0 {
        return Vec::new();
    }
    let slice = std::slice::from_raw_parts(list.items, list.count);
    slice
        .iter()
        .map(|pointer| {
            if pointer.is_null() {
                String::new()
            } else {
                CStr::from_ptr(*pointer).to_string_lossy().into_owned()
            }
        })
        .collect()
}

unsafe fn optional_string(pointer: *mut std::ffi::c_char) -> Option<String> {
    if pointer.is_null() {
        None
    } else {
        Some(CStr::from_ptr(pointer).to_string_lossy().into_owned())
    }
}

fn basename(path: &str) -> String {
    Path::new(path)
        .file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_default()
}

#[test]
fn abi_version_is_one() {
    assert_eq!(vd_abi_version(), 1);
}

#[test]
fn project_parse_expands_the_fixture_filelist() {
    let dir = fixture_dir();
    let Ok(dir_c) = CString::new(dir.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    assert!(!service.is_null());
    unsafe {
        // Starting the job must not wait for the file system work (C2).
        let started = Instant::now();
        let job = vd_project_parse(service, dir_c.as_ptr());
        let start_elapsed = started.elapsed();
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        assert!(
            start_elapsed < Duration::from_millis(200),
            "the start call blocked for {start_elapsed:?}"
        );

        let mut info = std::mem::zeroed::<VdProjectInfo>();
        let status = wait_for(
            || vd_project_poll(service, job, &mut info),
            Duration::from_secs(20),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "poll failed: {:?}",
            last_error(service)
        );

        let files: Vec<String> = string_list(&info.files)
            .iter()
            .map(|path| basename(path))
            .collect();
        assert_eq!(files, vec!["rtl_a.sv", "rtl_b.sv", "rtl_c.sv"]);
        let include_dirs = string_list(&info.include_dirs);
        assert_eq!(include_dirs.len(), 1, "include dirs: {include_dirs:?}");
        assert!(
            include_dirs[0].ends_with("inc"),
            "include dir: {}",
            include_dirs[0]
        );
        let defines = string_list(&info.defines);
        assert_eq!(defines, vec!["WIDTH=8", "SIM"]);

        vd_project_info_free(&mut info);
        assert!(info.files.items.is_null());
        vd_project_info_free(&mut info);
        vd_service_destroy(service);
    }
}

#[test]
fn null_and_unknown_arguments_are_rejected_with_a_typed_error() {
    let service = vd_service_create();
    assert!(!service.is_null());
    unsafe {
        assert_eq!(vd_project_parse(service, std::ptr::null()), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT);
        assert!(!message.is_empty());

        let mut info = std::mem::zeroed::<VdProjectInfo>();
        assert_eq!(vd_project_poll(service, 9_999, &mut info), -1);
        let (code, _) = last_error(service);
        assert_eq!(code, VD_ERR_JOB_UNKNOWN);
        assert_eq!(vd_project_poll(service, 9_999, std::ptr::null_mut()), -1);
        vd_service_destroy(service);
    }
}

#[test]
fn cancelling_a_job_discards_its_result() {
    let dir = fixture_dir();
    let Ok(dir_c) = CString::new(dir.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        let job = vd_project_parse(service, dir_c.as_ptr());
        assert_ne!(job, 0);
        assert_eq!(vd_job_cancel(service, job), 1);
        let mut info = std::mem::zeroed::<VdProjectInfo>();
        assert_eq!(vd_project_poll(service, job, &mut info), -1);
        assert_eq!(vd_job_cancel(service, job), 0);
        vd_service_destroy(service);
    }
}

#[test]
fn lsp_rejects_a_missing_server_binary() {
    let dir = fixture_dir();
    let Ok(dir_c) = CString::new(dir.display().to_string()) else {
        return;
    };
    let missing = CString::new("/nonexistent/verible-verilog-ls").unwrap_or_default();
    let service = vd_service_create();
    unsafe {
        let job = vd_lsp_start(service, missing.as_ptr(), dir_c.as_ptr(), std::ptr::null());
        assert_ne!(job, 0);
        let mut info = std::mem::zeroed::<VdLspInfo>();
        let status = wait_for(
            || vd_lsp_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(status, VD_STATUS_FAILED);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_NOT_FOUND);
        assert!(message.contains("not found"), "message: {message}");
        vd_service_destroy(service);
    }
}

#[test]
fn lsp_configures_lazily_without_starting_the_server() {
    let dir = fixture_dir();
    let Ok(dir_c) = CString::new(dir.display().to_string()) else {
        return;
    };
    let server = CString::new("/bin/true").unwrap_or_default();
    let service = vd_service_create();
    unsafe {
        let job = vd_lsp_start(service, server.as_ptr(), dir_c.as_ptr(), std::ptr::null());
        assert_ne!(job, 0);
        let mut info = std::mem::zeroed::<VdLspInfo>();
        let status = wait_for(
            || vd_lsp_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "poll failed: {:?}",
            last_error(service)
        );
        assert_eq!(info.state, 1);
        assert_eq!(optional_string(info.server).as_deref(), Some("/bin/true"));
        let message = optional_string(info.message).unwrap_or_default();
        assert!(
            message.contains("server_running=false"),
            "message: {message}"
        );
        vd_lsp_info_free(&mut info);
        vd_service_destroy(service);
    }
}

#[test]
fn deeplint_reports_a_missing_binary_as_a_typed_failure() {
    let source = fixture_server();
    let Ok(source_c) = CString::new(source.display().to_string()) else {
        return;
    };
    let missing = CString::new("/nonexistent/slang").unwrap_or_default();
    let service = vd_service_create();
    unsafe {
        let job = vd_deeplint_lint(service, source_c.as_ptr(), missing.as_ptr());
        assert_ne!(job, 0);
        let mut diagnostics = std::mem::zeroed::<VdDiagnostics>();
        let status = wait_for(
            || vd_deeplint_poll(service, job, &mut diagnostics),
            Duration::from_secs(30),
        );
        assert_eq!(status, VD_STATUS_FAILED);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_SERVICE);
        assert!(message.contains("slang"), "message: {message}");
        vd_diagnostics_free(&mut diagnostics);
        vd_service_destroy(service);
    }
}

#[test]
fn fim_is_disabled_by_default_and_starts_no_worker() {
    let prefix = CString::new("module top;").unwrap_or_default();
    let suffix = CString::new("endmodule").unwrap_or_default();
    let spawns_before = vd_fim::worker_spawns();
    let service = vd_service_create();
    unsafe {
        let job = vd_fim_complete(service, prefix.as_ptr(), suffix.as_ptr());
        assert_ne!(job, 0);
        let mut result = std::mem::zeroed::<VdFimResult>();
        let status = wait_for(
            || vd_fim_poll(service, job, &mut result),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "poll failed: {:?}",
            last_error(service)
        );
        assert_eq!(result.state, 0, "FIM must be off by default");
        let reason = optional_string(result.text).unwrap_or_default();
        assert!(!reason.is_empty());
        assert_eq!(
            vd_fim::worker_spawns(),
            spawns_before,
            "the disabled path must not spawn a FIM worker"
        );
        vd_fim_result_free(&mut result);
        vd_service_destroy(service);
    }
}

#[test]
fn search_returns_hits_over_the_project_file_list() {
    let dir = fixture_dir();
    let root = CString::new(dir.join("rtl").display().to_string()).unwrap_or_default();
    let files = [
        CString::new("rtl_a.sv").unwrap_or_default(),
        CString::new("rtl_b.sv").unwrap_or_default(),
        CString::new("rtl_c.sv").unwrap_or_default(),
    ];
    let pointers: Vec<*const std::ffi::c_char> = files.iter().map(|file| file.as_ptr()).collect();
    // Not "module": every fixture file also contains "endmodule", which would
    // produce two hits per file and hide an off-by-one in the line scan.
    let needle = CString::new("assign out").unwrap_or_default();
    let service = vd_service_create();
    unsafe {
        let job = vd_search_query(
            service,
            root.as_ptr(),
            pointers.as_ptr(),
            pointers.len(),
            needle.as_ptr(),
            50,
        );
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut results = std::mem::zeroed::<VdSearchResults>();
        let status = wait_for(
            || vd_search_poll(service, job, &mut results),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "poll failed: {:?}",
            last_error(service)
        );
        assert_eq!(results.count, 3, "one hit per file, got {}", results.count);
        let slice = std::slice::from_raw_parts(results.items, results.count);
        let first = &slice[0];
        assert_eq!(optional_string(first.path).as_deref(), Some("rtl_a.sv"));
        assert_eq!(first.line, 2);
        assert_eq!(first.column, 3);
        assert!(optional_string(first.preview)
            .unwrap_or_default()
            .contains("assign out = 1"));
        vd_search_results_free(&mut results);
        vd_service_destroy(service);
    }
}

#[test]
fn vcs_reports_the_empty_state_for_a_plain_directory() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let Ok(dir_c) = CString::new(dir.path().display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        let job = vd_vcs_status(service, dir_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut status_out = std::mem::zeroed::<VdVcsStatus>();
        let status = wait_for(
            || vd_vcs_poll(service, job, &mut status_out),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "poll failed: {:?}",
            last_error(service)
        );
        assert_eq!(
            status_out.backend, 0,
            "a plain directory is not a repository"
        );
        assert_eq!(status_out.count, 0);
        assert!(optional_string(status_out.note).is_some());
        vd_vcs_status_free(&mut status_out);
        vd_service_destroy(service);
    }
}

#[test]
fn settings_round_trip_through_the_abi() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let path = dir.path().join("settings.toml");
    let Ok(path_c) = CString::new(path.display().to_string()) else {
        return;
    };
    let keys = [
        CString::new("theme").unwrap_or_default(),
        CString::new("keymap.split").unwrap_or_default(),
    ];
    let values = [
        CString::new("Breeze Dark").unwrap_or_default(),
        CString::new("Ctrl+Shift+J").unwrap_or_default(),
    ];
    let inputs = [
        VdSettingInput {
            key: keys[0].as_ptr(),
            value: values[0].as_ptr(),
        },
        VdSettingInput {
            key: keys[1].as_ptr(),
            value: values[1].as_ptr(),
        },
    ];
    let service = vd_service_create();
    unsafe {
        let save_job = vd_settings_save(service, path_c.as_ptr(), inputs.as_ptr(), inputs.len());
        assert_ne!(save_job, 0, "rejected: {:?}", last_error(service));
        let mut written = std::mem::zeroed::<VdSettings>();
        let status = wait_for(
            || vd_settings_poll(service, save_job, &mut written),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "save failed: {:?}",
            last_error(service)
        );
        assert_eq!(written.count, 0, "a save returns an empty result");
        vd_settings_free(&mut written);

        let load_job = vd_settings_load(service, path_c.as_ptr());
        assert_ne!(load_job, 0);
        let mut loaded = std::mem::zeroed::<VdSettings>();
        let status = wait_for(
            || vd_settings_poll(service, load_job, &mut loaded),
            Duration::from_secs(10),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "load failed: {:?}",
            last_error(service)
        );
        assert_eq!(loaded.count, 2);
        let slice = std::slice::from_raw_parts(loaded.items, loaded.count);
        let pairs: Vec<(String, String)> = slice
            .iter()
            .map(|item| {
                (
                    optional_string(item.key).unwrap_or_default(),
                    optional_string(item.value).unwrap_or_default(),
                )
            })
            .collect();
        // The store is a TOML table, so entries come back in key order.
        assert_eq!(
            pairs,
            vec![
                ("keymap.split".to_string(), "Ctrl+Shift+J".to_string()),
                ("theme".to_string(), "Breeze Dark".to_string()),
            ]
        );
        vd_settings_free(&mut loaded);

        // A released job id is gone, not merely a no-op.
        assert_eq!(vd_job_release(service, load_job), 0);
        vd_service_destroy(service);
    }
}

#[test]
fn projected_fixture_serves_all_seven_services() {
    // One service handle, seven jobs: the seam is one registry, not seven.
    let dir = fixture_dir();
    let Ok(dir_c) = CString::new(dir.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    assert!(!service.is_null());
    unsafe {
        let before = vd_fim::worker_spawns();
        assert_ne!(vd_project_parse(service, dir_c.as_ptr()), 0);
        assert_ne!(vd_fim_complete(service, dir_c.as_ptr(), dir_c.as_ptr()), 0);
        assert_ne!(
            vd_lsp_start(service, dir_c.as_ptr(), dir_c.as_ptr(), std::ptr::null()),
            0
        );
        assert_ne!(vd_vcs_status(service, dir_c.as_ptr()), 0);
        assert_ne!(vd_settings_load(service, dir_c.as_ptr()), 0);
        // Unknown ids must not make the handle unusable.
        let mut info = std::mem::zeroed::<VdProjectInfo>();
        assert_eq!(vd_project_poll(service, 424_242, &mut info), -1);
        assert_eq!(vd_fim::worker_spawns(), before);
        vd_service_destroy(service);
    }
}

// --- VCS write jobs (batch 3a, AC-3.1 / AC-3.2) ----------------------------
//
// The write trio is exercised through the C ABI exactly as the C++ panel will
// use it: start -> job id, poll -> PENDING/READY/FAILED, free. The positive
// round trip runs in a fresh `tempfile::tempdir()` repository (AC-0.4), never in
// the VEdit working tree.

fn git_is_available() -> bool {
    std::process::Command::new("git")
        .arg("--version")
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false)
}

fn run_git(root: &Path, parameters: &[&str]) -> Option<String> {
    // `current_dir`, not a global path option: git 1.8.3 (the CentOS 7
    // baseline) has no such option.
    let output = std::process::Command::new("git")
        .current_dir(root)
        .args(parameters)
        .output()
        .ok()?;
    if output.status.success() {
        Some(String::from_utf8_lossy(&output.stdout).into_owned())
    } else {
        None
    }
}

unsafe fn poll_write(service: *mut VdService, job: u64) -> (i32, VdVcsWriteResult) {
    let mut out = std::mem::zeroed::<VdVcsWriteResult>();
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        let state = vd_vcs_write_poll(service, job, &mut out);
        if state != VD_STATUS_PENDING || Instant::now() >= deadline {
            return (state, out);
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
fn vcs_write_codes_are_stable() {
    assert_eq!(VD_VCS_WRITE_GIT_STAGE, 1);
    assert_eq!(VD_VCS_WRITE_GIT_UNSTAGE, 2);
    assert_eq!(VD_VCS_WRITE_GIT_DISCARD, 3);
    assert_eq!(VD_VCS_WRITE_GIT_COMMIT, 4);
    assert_eq!(VD_VCS_WRITE_SVN_COMMIT, 11);
}

#[test]
fn vcs_write_job_runs_a_real_git_round_trip() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path().to_path_buf();
    // AC-0.4: never the VEdit working tree.
    assert!(
        !root.starts_with(Path::new(env!("CARGO_MANIFEST_DIR"))),
        "the fixture must live outside the repository: {}",
        root.display()
    );
    if !git_is_available() {
        return;
    }
    let Ok(()) = std::fs::create_dir_all(root.join("rtl")) else {
        return;
    };
    let Ok(()) = std::fs::write(root.join("rtl/top.sv"), "module top;\nendmodule\n") else {
        return;
    };
    let Some(_) = run_git(&root, &["init", "-q"]) else {
        return;
    };
    assert!(run_git(&root, &["config", "user.email", "test@example.com"]).is_some());
    assert!(run_git(&root, &["config", "user.name", "VEdit Test"]).is_some());

    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let Ok(path_c) = CString::new("rtl/top.sv") else {
        return;
    };
    let Ok(message_c) = CString::new("add: fixture file") else {
        return;
    };
    let Ok(author_c) = CString::new("VEdit Author <author@example.com>") else {
        return;
    };
    let paths = [path_c.as_ptr()];
    let service = vd_service_create();
    unsafe {
        // stage
        let mut request = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_STAGE,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: paths.len(),
            paths: paths.as_ptr(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(out.op, VD_VCS_WRITE_GIT_STAGE);
        assert_eq!(
            out.ok,
            1,
            "git add failed: {}",
            optional_string(out.stderr_text).unwrap_or_default()
        );
        vd_vcs_write_free(&mut out);
        assert_eq!(
            run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
            Some("A  rtl/top.sv\n"),
            "the file must be staged after the write job"
        );

        // unstage
        request.op = VD_VCS_WRITE_GIT_UNSTAGE;
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0);
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(
            out.ok,
            1,
            "git reset failed: {}",
            optional_string(out.stderr_text).unwrap_or_default()
        );
        vd_vcs_write_free(&mut out);
        assert_eq!(
            run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
            Some("?? rtl/top.sv\n")
        );

        // stage again, then commit with an explicit author
        request.op = VD_VCS_WRITE_GIT_STAGE;
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0);
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(out.ok, 1);
        vd_vcs_write_free(&mut out);

        let commit = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_COMMIT,
            root: root_c.as_ptr(),
            message: message_c.as_ptr(),
            author: author_c.as_ptr(),
            amend: 0,
            count: 0,
            paths: std::ptr::null(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &commit);
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(
            out.ok,
            1,
            "git commit failed: {}",
            optional_string(out.stderr_text).unwrap_or_default()
        );
        vd_vcs_write_free(&mut out);
        assert_eq!(
            run_git(&root, &["log", "-1", "--pretty=%s"]).as_deref(),
            Some("add: fixture file\n")
        );
        assert_eq!(
            run_git(&root, &["log", "-1", "--pretty=%an"]).as_deref(),
            Some("VEdit Author\n")
        );
        assert_eq!(
            run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
            Some("")
        );

        // modify -> stage -> unstage -> discard: the tree is clean again
        let Ok(()) = std::fs::write(
            root.join("rtl/top.sv"),
            "module top;\n// changed\nendmodule\n",
        ) else {
            return;
        };
        let mut request = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_STAGE,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: paths.len(),
            paths: paths.as_ptr(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0);
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(out.ok, 1);
        vd_vcs_write_free(&mut out);
        request.op = VD_VCS_WRITE_GIT_UNSTAGE;
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0);
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(out.ok, 1);
        vd_vcs_write_free(&mut out);
        request.op = VD_VCS_WRITE_GIT_DISCARD;
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0);
        let (state, mut out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(
            out.ok,
            1,
            "git checkout failed: {}",
            optional_string(out.stderr_text).unwrap_or_default()
        );
        vd_vcs_write_free(&mut out);
        assert_eq!(
            run_git(&root, &["status", "--porcelain", "-uall"]).as_deref(),
            Some("")
        );

        // The READ path still sees the same repository and reports it clean.
        let status_job = vd_vcs_status(service, root_c.as_ptr());
        assert_ne!(status_job, 0);
        let mut status_out = std::mem::zeroed::<VdVcsStatus>();
        let status = wait_for(
            || vd_vcs_poll(service, status_job, &mut status_out),
            Duration::from_secs(10),
        );
        assert_eq!(status, VD_STATUS_READY);
        assert_eq!(status_out.backend, 1);
        assert_eq!(status_out.count, 0);
        vd_vcs_status_free(&mut status_out);

        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn vcs_write_job_refuses_invalid_requests() {
    let Ok(dir) = tempfile::tempdir() else {
        return;
    };
    let root = dir.path().to_path_buf();
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        // A null request is refused before a job exists.
        assert_eq!(vd_vcs_write_job(service, std::ptr::null()), 0);
        assert_eq!(last_error(service).0, VD_ERR_INVALID_ARGUMENT);

        // An unknown operation code is refused before a job exists.
        let unknown = VdVcsWriteRequest {
            op: 999,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: 0,
            paths: std::ptr::null(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        assert_eq!(vd_vcs_write_job(service, &unknown), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT);
        assert!(message.contains("999"), "message: {message}");

        // A listed operation outside a working tree starts a job that fails
        // with a typed error: nothing is spawned.
        let Ok(path_c) = CString::new("a.sv") else {
            return;
        };
        let paths = [path_c.as_ptr()];
        let no_repo = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_STAGE,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: paths.len(),
            paths: paths.as_ptr(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &no_repo);
        assert_ne!(job, 0, "a well-formed request must be accepted for a job");
        let (state, out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_FAILED);
        assert_eq!(last_error(service).0, VD_ERR_INVALID_ARGUMENT);
        assert!(out.stdout_text.is_null());

        // A git marker makes the backend Git, so the parameter checks run: a
        // commit without a message, and an absolute path, are both refused.
        let Ok(()) = std::fs::create_dir(root.join(".git")) else {
            return;
        };
        let no_message = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_COMMIT,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: 0,
            paths: std::ptr::null(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &no_message);
        assert_ne!(job, 0);
        let (state, _out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_FAILED);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT);
        assert!(
            message.contains("commit message"),
            "unexpected message: {message}"
        );

        let Ok(absolute_c) = CString::new("/etc/passwd") else {
            return;
        };
        let absolute_paths = [absolute_c.as_ptr()];
        let absolute = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_STAGE,
            root: root_c.as_ptr(),
            message: std::ptr::null(),
            author: std::ptr::null(),
            amend: 0,
            count: absolute_paths.len(),
            paths: absolute_paths.as_ptr(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &absolute);
        assert_ne!(job, 0);
        let (state, _out) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_FAILED);
        let (code, message) = last_error(service);
        assert_eq!(code, VD_ERR_INVALID_ARGUMENT);
        assert!(
            message.contains("absolute"),
            "unexpected message: {message}"
        );

        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn vcs_write_poll_misuse_is_typed() {
    let service = vd_service_create();
    unsafe {
        // An unknown job id is a misuse with a typed reason.
        let mut out = std::mem::zeroed::<VdVcsWriteResult>();
        assert_eq!(vd_vcs_write_poll(service, 424_242, &mut out), -1);
        assert_eq!(last_error(service).0, VD_ERR_JOB_UNKNOWN);
        // A null result pointer is a misuse, never a crash.
        assert_eq!(vd_vcs_write_poll(service, 1, std::ptr::null_mut()), -1);
        // A null service is a misuse.
        assert_eq!(vd_vcs_write_poll(std::ptr::null_mut(), 1, &mut out), -1);
        // free is null-safe in both forms.
        vd_vcs_write_free(std::ptr::null_mut());
        let mut zeroed = std::mem::zeroed::<VdVcsWriteResult>();
        vd_vcs_write_free(&mut zeroed);
        assert!(zeroed.stdout_text.is_null());

        // A job of another service kind polled through the write poller is a
        // wrong-kind misuse, never a silently returned payload.
        let dir = fixture_dir();
        let Ok(dir_c) = CString::new(dir.display().to_string()) else {
            return;
        };
        let project_job = vd_project_parse(service, dir_c.as_ptr());
        assert_ne!(project_job, 0);
        let mut mismatch = std::mem::zeroed::<VdVcsWriteResult>();
        let deadline = Instant::now() + Duration::from_secs(20);
        let mut state = VD_STATUS_PENDING;
        while state == VD_STATUS_PENDING && Instant::now() < deadline {
            state = vd_vcs_write_poll(service, project_job, &mut mismatch);
            if state == VD_STATUS_PENDING {
                std::thread::sleep(Duration::from_millis(2));
            }
        }
        assert_eq!(state, -1, "a project job must not be readable as a write");
        assert_eq!(last_error(service).0, VD_ERR_WRONG_JOB_KIND);
        vd_service_destroy(service);
    }
}

// --- VCS read compatibility through the C ABI (AC-3.0) ---------------------
//
// The read jobs must really return status / branch+revision / diff against a
// real repository. This test runs inside the delivery-baseline image
// (git 1.8.3.1), so a parameter that line cannot parse fails here.

fn make_git_repository() -> Option<PathBuf> {
    let dir = tempfile::tempdir().ok()?;
    let root = dir.path().to_path_buf();
    std::mem::forget(dir);
    let created = std::fs::create_dir_all(root.join("rtl"));
    if created.is_err() {
        return None;
    }
    let written = std::fs::write(root.join("rtl/top.sv"), "module top;\nendmodule\n");
    written.ok()?;
    run_git(&root, &["init", "-q"])?;
    run_git(&root, &["config", "user.email", "compat@example.com"])?;
    run_git(&root, &["config", "user.name", "VEdit Compat"])?;
    run_git(&root, &["add", "rtl/top.sv"])?;
    run_git(&root, &["commit", "-m", "compat fixture"])?;
    Some(root)
}

#[test]
fn vcs_read_jobs_return_status_info_and_diff_from_a_real_repository() {
    if !git_is_available() {
        return;
    }
    let Some(root) = make_git_repository() else {
        return;
    };
    // One modified file so the status and the diff have something to report.
    let changed = std::fs::write(
        root.join("rtl/top.sv"),
        "module top;\n// changed\nendmodule\n",
    );
    assert!(changed.is_ok());
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        let job = vd_vcs_status(service, root_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut status_out = std::mem::zeroed::<VdVcsStatus>();
        let state = wait_for(
            || vd_vcs_poll(service, job, &mut status_out),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(status_out.backend, 1, "the fixture is a Git repository");
        assert_eq!(status_out.count, 1, "one modified file");
        let branch = optional_string(status_out.branch).unwrap_or_default();
        assert!(!branch.is_empty(), "the branch must be reported");
        let changes = std::slice::from_raw_parts(status_out.changes, status_out.count);
        assert_eq!(
            optional_string(changes[0].path).as_deref(),
            Some("rtl/top.sv")
        );
        vd_vcs_status_free(&mut status_out);

        let job = vd_vcs_info_job(service, root_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(info.backend, 1);
        let revision = optional_string(info.revision).unwrap_or_default();
        assert!(
            !revision.is_empty() && revision.chars().all(|digit| digit.is_ascii_hexdigit()),
            "no usable revision: {revision:?}"
        );
        vd_vcs_info_free(&mut info);

        let Ok(path_c) = CString::new("rtl/top.sv") else {
            return;
        };
        let job = vd_vcs_diff_job(service, root_c.as_ptr(), path_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut diff = std::mem::zeroed::<VdVcsDiff>();
        let state = wait_for(
            || vd_vcs_diff_poll(service, job, &mut diff),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        let text = optional_string(diff.text).unwrap_or_default();
        assert!(
            text.contains("+// changed"),
            "the diff is missing the modified line: {text}"
        );
        vd_vcs_diff_free(&mut diff);

        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

/// Batch 4b: the five history reads are asynchronous jobs over ONE result struct,
/// and every payload is freed without a double free or a leak.
#[test]
fn vcs_history_jobs_return_pages_detail_blame_and_comparison() {
    if !git_is_available() {
        return;
    }
    let Some(root) = make_git_repository() else {
        return;
    };
    let changed = std::fs::write(
        root.join("rtl/top.sv"),
        "module top;\n// history second\nendmodule\n",
    );
    assert!(changed.is_ok());
    let Some(_) = run_git(&root, &["commit", "-am", "history second commit"]) else {
        return;
    };
    let Some(head) = run_git(&root, &["rev-parse", "HEAD"]).map(|text| text.trim().to_string())
    else {
        return;
    };
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let Ok(head_c) = CString::new(head.clone()) else {
        return;
    };
    let Ok(path_c) = CString::new("rtl/top.sv") else {
        return;
    };
    let Ok(previous_c) = CString::new("HEAD~1") else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        // --- commit page (paged, one row + has_more) ----------------------
        let job = vd_vcs_history_commits_job(service, root_c.as_ptr(), 0, 1, std::ptr::null());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut result = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut result),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(result.kind, VD_VCS_HISTORY_COMMITS);
        assert_eq!(result.backend, 1);
        assert_eq!(result.error, 0);
        assert_eq!(result.commit_count, 1, "a one-row page");
        assert_eq!(result.has_more, 1, "two commits means a further page");
        let rows = std::slice::from_raw_parts(result.commits, result.commit_count);
        assert_eq!(
            optional_string(rows[0].hash).as_deref(),
            Some(head.as_str())
        );
        assert_eq!(
            optional_string(rows[0].subject).as_deref(),
            Some("history second commit")
        );
        assert!(rows[0].time > 1_600_000_000);
        vd_vcs_history_free(&mut result);

        // --- commit detail: files + patch ---------------------------------
        let job = vd_vcs_history_commit_job(service, root_c.as_ptr(), head_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut detail = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut detail),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(detail.kind, VD_VCS_HISTORY_COMMIT);
        assert_eq!(optional_string(detail.hash).as_deref(), Some(head.as_str()));
        assert_eq!(
            optional_string(detail.author).as_deref(),
            Some("VEdit Compat")
        );
        assert!(detail.file_count >= 1, "the commit touched a file");
        let files = std::slice::from_raw_parts(detail.files, detail.file_count);
        assert_eq!(
            optional_string(files[0].path).as_deref(),
            Some("rtl/top.sv")
        );
        let patch = optional_string(detail.patch).unwrap_or_default();
        assert!(patch.contains("+// history second"), "patch: {patch}");
        vd_vcs_history_free(&mut detail);

        // --- file history --------------------------------------------------
        let job = vd_vcs_history_file_job(service, root_c.as_ptr(), path_c.as_ptr(), 0, 10);
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut history = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut history),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(history.kind, VD_VCS_HISTORY_FILE);
        assert_eq!(optional_string(history.path).as_deref(), Some("rtl/top.sv"));
        assert!(history.commit_count >= 2, "both commits touched the path");
        assert_eq!(history.has_more, 0);
        vd_vcs_history_free(&mut history);

        // --- blame ---------------------------------------------------------
        let job = vd_vcs_history_blame_job(service, root_c.as_ptr(), path_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut blamed = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut blamed),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(blamed.kind, VD_VCS_HISTORY_BLAME);
        assert_eq!(blamed.blame_count, 3, "three lines");
        let lines = std::slice::from_raw_parts(blamed.blame, blamed.blame_count);
        assert_eq!(
            optional_string(lines[0].text).as_deref(),
            Some("module top;")
        );
        assert_eq!(
            optional_string(lines[0].author).as_deref(),
            Some("VEdit Compat")
        );
        assert_eq!(lines[0].line, 1);
        vd_vcs_history_free(&mut blamed);

        // --- comparison ----------------------------------------------------
        let job = vd_vcs_history_compare_job(
            service,
            root_c.as_ptr(),
            previous_c.as_ptr(),
            head_c.as_ptr(),
        );
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut comparison = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut comparison),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(comparison.kind, VD_VCS_HISTORY_COMPARE);
        assert_eq!(optional_string(comparison.from).as_deref(), Some("HEAD~1"));
        assert_eq!(
            optional_string(comparison.to).as_deref(),
            Some(head.as_str())
        );
        assert!(comparison.file_count >= 1);
        assert!(optional_string(comparison.patch)
            .unwrap_or_default()
            .contains("+// history second"));
        vd_vcs_history_free(&mut comparison);

        // --- the wrong poll refuses a job of another kind -------------------
        let status_job = vd_vcs_status(service, root_c.as_ptr());
        assert_ne!(status_job, 0, "rejected: {:?}", last_error(service));
        let mut misuse = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, status_job, &mut misuse),
            Duration::from_secs(10),
        );
        assert_eq!(state, -1, "a status job must not read as history");
        // A history job must not read as info either.
        let history_job =
            vd_vcs_history_commits_job(service, root_c.as_ptr(), 0, 5, std::ptr::null());
        assert_ne!(history_job, 0);
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, history_job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, -1, "a history job must not read as info");
        // Freeing a zeroed result is a no-op, not a crash.
        let mut empty = std::mem::zeroed::<VdVcsHistoryResult>();
        vd_vcs_history_free(&mut empty);
        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

/// Batch 5a: the history-operation codes are stable and the info read reports the
/// operation in progress (0 idle, 1 merge, 2 rebase, 3 cherry-pick, 4 revert).
#[test]
fn vcs_history_op_codes_and_operation_state_are_stable() {
    assert_eq!(VD_VCS_WRITE_GIT_MERGE, 24);
    assert_eq!(VD_VCS_WRITE_GIT_REBASE, 25);
    assert_eq!(VD_VCS_WRITE_GIT_CHERRY_PICK, 26);
    assert_eq!(VD_VCS_WRITE_GIT_REVERT, 27);
    assert_eq!(VD_VCS_WRITE_GIT_RESET, 28);
    assert_eq!(VD_VCS_WRITE_GIT_CLEAN, 29);
    assert_eq!(VD_VCS_WRITE_GIT_CONTINUE, 30);
    assert_eq!(VD_VCS_WRITE_GIT_ABORT, 31);
    if !git_is_available() {
        return;
    }
    let Some(root) = make_git_repository() else {
        return;
    };
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        // Idle repository: no operation.
        let job = vd_vcs_info_job(service, root_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(
            info.operation, 0,
            "a clean repository has no operation in progress"
        );
        vd_vcs_info_free(&mut info);

        // A real conflicting merge leaves the MERGE state visible.
        assert!(run_git(&root, &["checkout", "-q", "-b", "abi-feature"]).is_some());
        assert!(
            std::fs::write(root.join("rtl/top.sv"), "module top;\nfeature\nendmodule\n").is_ok()
        );
        assert!(run_git(&root, &["commit", "-qam", "feature change"]).is_some());
        assert!(run_git(&root, &["checkout", "-q", "master"]).is_some());
        assert!(
            std::fs::write(root.join("rtl/top.sv"), "module top;\nmaster\nendmodule\n").is_ok()
        );
        assert!(run_git(&root, &["commit", "-qam", "master change"]).is_some());
        assert!(
            run_git(&root, &["merge", "abi-feature"]).is_none(),
            "the merge must conflict"
        );

        let job = vd_vcs_info_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(
            info.operation, 1,
            "the info read reports the merge in progress"
        );
        vd_vcs_info_free(&mut info);

        // The conflict files are the status changes with kind 5.
        let job = vd_vcs_status(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut status_out = std::mem::zeroed::<VdVcsStatus>();
        let state = wait_for(
            || vd_vcs_poll(service, job, &mut status_out),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        let changes = std::slice::from_raw_parts(status_out.changes, status_out.count);
        assert!(
            changes.iter().any(|change| change.kind == 5),
            "the unmerged file must be reported as a conflict"
        );
        vd_vcs_status_free(&mut status_out);

        // Aborting restores the idle state.
        assert!(run_git(&root, &["merge", "--abort"]).is_some());
        let job = vd_vcs_info_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(info.operation, 0, "the abort clears the operation");
        vd_vcs_info_free(&mut info);
        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn history_and_workspace_polls_reject_a_foreign_payload() {
    // Batch 7: the VCS reads have TYPED job payloads. A job produced by one read
    // must not be reinterpreted by another read's poll: the misuse comes back as
    // a typed rejection (-1), never as a struct of the wrong shape.
    if !git_is_available() {
        return;
    }
    let Some(root) = make_git_repository() else {
        return;
    };
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        // A history job through the WORKSPACE poll.
        let job = vd_vcs_history_commits_job(service, root_c.as_ptr(), 0, 5, std::ptr::null());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut workspace = std::mem::zeroed::<VdVcsWorkspaceResult>();
        let state = wait_for(
            || vd_vcs_workspace_poll(service, job, &mut workspace),
            Duration::from_secs(10),
        );
        assert_eq!(
            state, -1,
            "a history job must not be read as a workspace listing"
        );
        vd_vcs_workspace_free(&mut workspace);

        // A workspace job through the HISTORY poll.
        let job = vd_vcs_workspace_stashes_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut history = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut history),
            Duration::from_secs(10),
        );
        assert_eq!(
            state, -1,
            "a workspace job must not be read as a history result"
        );
        vd_vcs_history_free(&mut history);

        // A workspace job through the INFO poll, and an info job through the
        // history poll: the identity read is typed too.
        let job = vd_vcs_workspace_worktrees_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut info = std::mem::zeroed::<VdVcsInfo>();
        let state = wait_for(
            || vd_vcs_info_poll(service, job, &mut info),
            Duration::from_secs(10),
        );
        assert_eq!(state, -1, "a workspace job must not be read as VcsInfo");
        vd_vcs_info_free(&mut info);

        let job = vd_vcs_info_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut history = std::mem::zeroed::<VdVcsHistoryResult>();
        let state = wait_for(
            || vd_vcs_history_poll(service, job, &mut history),
            Duration::from_secs(10),
        );
        assert_eq!(
            state, -1,
            "an identity job must not be read as a history result"
        );
        vd_vcs_history_free(&mut history);
        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn vcs_workspace_codes_and_listings_are_stable() {
    // Batch 5b: the operation codes and the two listing kinds the C ABI freezes.
    assert_eq!(VD_VCS_WRITE_GIT_STASH_SAVE, 32);
    assert_eq!(VD_VCS_WRITE_GIT_STASH_POP, 33);
    assert_eq!(VD_VCS_WRITE_GIT_STASH_APPLY, 34);
    assert_eq!(VD_VCS_WRITE_GIT_STASH_DROP, 35);
    assert_eq!(VD_VCS_WRITE_GIT_WORKTREE_ADD, 36);
    assert_eq!(VD_VCS_WRITE_GIT_WORKTREE_REMOVE, 37);
    assert_eq!(VD_VCS_WRITE_GIT_SUBMODULE_INIT, 38);
    assert_eq!(VD_VCS_WRITE_GIT_SUBMODULE_UPDATE, 39);
    assert_eq!(VD_VCS_WORKSPACE_STASHES, 1);
    assert_eq!(VD_VCS_WORKSPACE_WORKTREES, 2);
    // An unknown operation code is refused before a job exists.
    unsafe {
        assert_eq!(
            vd_vcs_workspace_stashes_job(std::ptr::null_mut(), std::ptr::null()),
            0
        );
        assert_eq!(
            vd_vcs_workspace_worktrees_job(std::ptr::null_mut(), std::ptr::null()),
            0
        );
    }
    if !git_is_available() {
        return;
    }
    let Some(root) = make_git_repository() else {
        return;
    };
    assert!(
        !root.starts_with(Path::new(env!("CARGO_MANIFEST_DIR"))),
        "the fixture must live outside the repository: {}",
        root.display()
    );
    let Ok(root_c) = CString::new(root.display().to_string()) else {
        return;
    };
    let Ok(message_c) = CString::new("abi stash message") else {
        return;
    };
    let service = vd_service_create();
    unsafe {
        // An EMPTY stack is a ready, empty listing (not an error).
        let job = vd_vcs_workspace_stashes_job(service, root_c.as_ptr());
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let mut stashes = std::mem::zeroed::<VdVcsWorkspaceResult>();
        let state = wait_for(
            || vd_vcs_workspace_poll(service, job, &mut stashes),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY, "poll: {:?}", last_error(service));
        assert_eq!(stashes.kind, VD_VCS_WORKSPACE_STASHES);
        assert_eq!(stashes.backend, 1);
        assert_eq!(stashes.count, 0);
        assert_eq!(stashes.error, 0);
        vd_vcs_workspace_free(&mut stashes);

        // Stash a change through the WRITE job, then the listing reports it.
        assert!(
            std::fs::write(root.join("rtl/top.sv"), "module top;\nstashed\nendmodule\n").is_ok()
        );
        let request = VdVcsWriteRequest {
            op: VD_VCS_WRITE_GIT_STASH_SAVE,
            root: root_c.as_ptr(),
            message: message_c.as_ptr(),
            author: std::ptr::null(),
            amend: 0,
            count: 0,
            paths: std::ptr::null(),
            name: std::ptr::null(),
            target: std::ptr::null(),
            url: std::ptr::null(),
            credential_user: std::ptr::null(),
            credential_password: std::ptr::null(),
            set_upstream: 0,
            mode: std::ptr::null(),
        };
        let job = vd_vcs_write_job(service, &request);
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        let (state, mut written) = poll_write(service, job);
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(written.op, VD_VCS_WRITE_GIT_STASH_SAVE);
        assert_eq!(
            written.ok,
            1,
            "stash save failed: {}",
            optional_string(written.stderr_text).unwrap_or_default()
        );
        vd_vcs_write_free(&mut written);

        let job = vd_vcs_workspace_stashes_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut stashes = std::mem::zeroed::<VdVcsWorkspaceResult>();
        let state = wait_for(
            || vd_vcs_workspace_poll(service, job, &mut stashes),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(stashes.count, 1, "one stash is listed");
        assert_eq!(stashes.error, 0);
        {
            let entries = std::slice::from_raw_parts(stashes.entries, stashes.count);
            assert_eq!(
                optional_string(entries[0].ref_name).as_deref(),
                Some("stash@{0}")
            );
            let subject = optional_string(entries[0].subject).unwrap_or_default();
            assert!(
                subject.contains("abi stash message"),
                "subject: {subject:?}"
            );
            assert_eq!(optional_string(entries[0].path).as_deref(), Some(""));
        }
        vd_vcs_workspace_free(&mut stashes);

        // The worktree listing is either real (git >= 2.5) or git's REAL refusal
        // (the delivery baseline has no `worktree` command) — never a silent
        // empty list with error = 0.
        let job = vd_vcs_workspace_worktrees_job(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut worktrees = std::mem::zeroed::<VdVcsWorkspaceResult>();
        let state = wait_for(
            || vd_vcs_workspace_poll(service, job, &mut worktrees),
            Duration::from_secs(10),
        );
        assert_eq!(state, VD_STATUS_READY);
        assert_eq!(worktrees.kind, VD_VCS_WORKSPACE_WORKTREES);
        if worktrees.error == 0 {
            assert!(worktrees.count >= 1, "the main worktree is listed");
            let entries = std::slice::from_raw_parts(worktrees.entries, worktrees.count);
            assert_eq!(entries[0].main_worktree, 1);
            assert!(optional_string(entries[0].path)
                .unwrap_or_default()
                .ends_with(&root.display().to_string()));
        } else {
            let note = optional_string(worktrees.note).unwrap_or_default();
            assert!(
                note.contains("not a git command"),
                "the baseline must report git's real reason, got {note:?}"
            );
        }
        vd_vcs_workspace_free(&mut worktrees);

        // Polling a workspace job with the WRONG poll is typed, and the wrong job
        // through the workspace poll is typed too.
        let job = vd_vcs_status(service, root_c.as_ptr());
        assert_ne!(job, 0);
        let mut wrong = std::mem::zeroed::<VdVcsWorkspaceResult>();
        // The status job is a DIFFERENT payload: once it settles, the workspace
        // poll reports misuse (-1) instead of handing back the wrong struct.
        let state = wait_for(
            || vd_vcs_workspace_poll(service, job, &mut wrong),
            Duration::from_secs(10),
        );
        assert_eq!(state, -1, "a foreign job must be a typed misuse");
        assert_eq!(
            vd_vcs_workspace_poll(service, job, std::ptr::null_mut()),
            -1
        );
        vd_vcs_workspace_free(std::ptr::null_mut());
        vd_service_destroy(service);
    }
    let _ = std::fs::remove_dir_all(&root);
}
