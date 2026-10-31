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
    VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
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
use vd_ffi::vcs::{vd_vcs_poll, vd_vcs_status, vd_vcs_status_free};

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
