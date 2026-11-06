//! ABI tests for the ux-batch-2 A1 additions, driven exactly as the C++ product
//! drives them: public `extern "C"` surface only.
//!
//! * module catalog (`vd_catalog_*`) against the REAL slang binary;
//! * extended project search (`vd_search_tree_query_ex`) with the exact match
//!   ranges, whole-word/regex/extension options;
//! * the typed lint knobs (`VdLintBatchOptions` / `vd_lint_batch_command_line`)
//!   including the "default value sends no flag" and per-engine no-op rules;
//! * `VdProjectInfo.rules_path` from a DISCOVERED `.rules.verible_lint`.

use std::ffi::{CStr, CString};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use vd_ffi::capi::{
    VdCatalogOptions, VdError, VdLintBatchOptions, VdModuleCatalog, VdProjectInfo, VdSearchOptions,
    VdSearchResults, VdStringList, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
};
use vd_ffi::catalog::{
    vd_catalog_poll, vd_catalog_release, vd_catalog_start, vd_module_catalog_free,
};
use vd_ffi::deeplint::{
    vd_deeplint_batch_start, vd_lint_batch_command_line, vd_lint_unsupported_knobs,
};
use vd_ffi::project::{vd_project_expand, vd_project_info_free, vd_project_poll};
use vd_ffi::search::{
    vd_search_results_free, vd_search_tree_poll, vd_search_tree_query_ex, VdSearchSummary,
};
use vd_ffi::service::{vd_service_create, vd_service_destroy, vd_service_error, VdService};

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

unsafe fn optional_string(pointer: *mut std::ffi::c_char) -> Option<String> {
    if pointer.is_null() {
        None
    } else {
        Some(CStr::from_ptr(pointer).to_string_lossy().into_owned())
    }
}

/// A borrowed `VdStringList` over live `CString`s.
unsafe fn string_list(values: &[CString]) -> VdStringList {
    if values.is_empty() {
        return VdStringList {
            count: 0,
            items: std::ptr::null_mut(),
        };
    }
    let mut pointers: Vec<*mut std::ffi::c_char> = values
        .iter()
        .map(|value| value.as_ptr().cast_mut())
        .collect();
    let list = VdStringList {
        count: pointers.len(),
        items: pointers.as_mut_ptr(),
    };
    // The caller keeps `pointers` alive through the call; leaking is deliberate
    // here so the struct does not borrow a dropped Vec. Tests free nothing: the
    // process ends.
    std::mem::forget(pointers);
    list
}

fn slang_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        return PathBuf::from(value);
    }
    let candidate = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/slang/out/slang");
    assert!(
        candidate.exists(),
        "slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh` (looked at {})",
        candidate.display()
    );
    candidate
}

fn write_file(root: &Path, name: &str, content: &str) {
    let path = root.join(name);
    if let Some(parent) = path.parent() {
        let created = std::fs::create_dir_all(parent);
        assert!(
            created.is_ok(),
            "create_dir_all({}) failed",
            parent.display()
        );
    }
    assert!(std::fs::write(&path, content).is_ok(), "write failed");
}

fn error_string(pointer: *mut std::ffi::c_char) -> String {
    // SAFETY: the export returns a crate-owned NUL-terminated string.
    let text = unsafe { CStr::from_ptr(pointer) }
        .to_string_lossy()
        .into_owned();
    // SAFETY: the string came from vd_string_free-compatible allocations.
    unsafe { vd_ffi::capi::vd_string_free(pointer) };
    text
}

// --- module catalog ---------------------------------------------------------

/// The fixture is written by the test, so the expectation is visible next to the
/// source it describes: `shared` is instantiated under BOTH parents, `orphan` is
/// defined but never instantiated, `bus_if` is an interface (NOT a module).
fn write_catalog_project(root: &Path) {
    write_file(
        root,
        "bus.sv",
        "interface bus_if;\n  logic req;\nendinterface\n",
    );
    write_file(
        root,
        "shared.sv",
        "module shared (input logic a, output logic [3:0] y);\n  assign y = {4{a}};\nendmodule\n",
    );
    write_file(
        root,
        "consumer.sv",
        "module consumer (bus_if bus, input logic clk, output logic [3:0] y);\n  assign y = {4{clk}};\nendmodule\n",
    );
    write_file(
        root,
        "top_a.sv",
        "module top_a (input logic clk, inout wire pad, output logic [3:0] y);\n  bus_if bus();\n  logic link;\n  shared u_shared (.a(clk), .y(y));\n  consumer u_consumer (.bus(bus), .clk(clk), .y());\n  reflink u_ref (.x(link));\nendmodule\n",
    );
    write_file(
        root,
        "top_b.sv",
        "module top_b (input logic clk, output logic [3:0] y);\n  shared u_shared (.a(clk), .y(y));\nendmodule\n",
    );
    write_file(
        root,
        "orphan.sv",
        "module orphan (input logic a, output logic b);\n  assign b = a;\nendmodule\n",
    );
    write_file(
        root,
        "reflink.sv",
        "module reflink (ref logic x);\nendmodule\n",
    );
}

#[test]
fn catalog_abi_returns_the_definition_keyed_module_tree() {
    let Some(dir) = tempfile::tempdir().ok() else {
        return;
    };
    write_catalog_project(dir.path());
    let mut sources: Vec<PathBuf> = std::fs::read_dir(dir.path())
        .unwrap_or_else(|error| panic!("reading the fixture: {error}"))
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "sv"))
        .collect();
    sources.sort();
    let files: Vec<CString> = sources
        .iter()
        .map(|path| CString::new(path.display().to_string()).unwrap_or_default())
        .collect();
    let slang = CString::new(slang_binary().display().to_string()).unwrap_or_default();
    let service = vd_service_create();
    // SAFETY: every pointer stays alive for the duration of the calls.
    unsafe {
        let file_list = string_list(&files);
        let options = VdCatalogOptions {
            slang_binary: slang.as_ptr(),
            include_dirs: string_list(&[]),
            defines: string_list(&[]),
            timeout_ms: 30_000,
            max_json_bytes: 0,
            max_files: 0,
        };
        let job = vd_catalog_start(service, &file_list, &options);
        assert_ne!(job, 0, "rejected: {:?}", last_error(service));
        assert!(
            job >= 1 << 52,
            "catalog jobs live in their own id range: {job}"
        );
        let mut catalog = std::mem::zeroed::<VdModuleCatalog>();
        let status = wait_for(
            || vd_catalog_poll(service, job, &mut catalog),
            Duration::from_secs(30),
        );
        assert_eq!(
            status,
            VD_STATUS_READY,
            "catalog failed: {:?}",
            last_error(service)
        );
        assert_eq!(catalog.modules.count, 6, "bus_if must not be a module");
        let modules = std::slice::from_raw_parts(catalog.modules.items, catalog.modules.count);
        let names: Vec<String> = modules
            .iter()
            .map(|module| optional_string(module.name).unwrap_or_default())
            .collect();
        assert_eq!(
            names,
            vec!["consumer", "orphan", "reflink", "shared", "top_a", "top_b"],
            "modules are sorted by name and the interface is excluded"
        );
        let find = |name: &str| {
            modules
                .iter()
                .find(|module| optional_string(module.name).as_deref() == Some(name))
                .unwrap_or_else(|| panic!("module {name} missing"))
        };
        assert_eq!(find("orphan").is_top, 1);
        assert_eq!(find("shared").is_top, 0);
        let top_a = find("top_a");
        let ports = std::slice::from_raw_parts(top_a.ports.items, top_a.ports.count);
        let port_tuples: Vec<(String, String, String)> = ports
            .iter()
            .map(|port| {
                (
                    optional_string(port.name).unwrap_or_default(),
                    optional_string(port.direction).unwrap_or_default(),
                    optional_string(port.type_name).unwrap_or_default(),
                )
            })
            .collect();
        assert_eq!(
            port_tuples,
            vec![
                ("clk".to_string(), "In".to_string(), "logic".to_string()),
                ("pad".to_string(), "InOut".to_string(), "logic".to_string()),
                ("y".to_string(), "Out".to_string(), "logic[3:0]".to_string()),
            ]
        );
        // Both parents list the `shared` edge; the interface instance does not.
        for parent in ["top_a", "top_b"] {
            let module = find(parent);
            let children = std::slice::from_raw_parts(module.children.items, module.children.count);
            let child_modules: Vec<String> = children
                .iter()
                .map(|child| optional_string(child.module).unwrap_or_default())
                .collect();
            assert!(
                child_modules.iter().any(|child| child == "shared"),
                "{parent}: {child_modules:?}"
            );
            assert!(
                !child_modules.iter().any(|child| child == "bus_if"),
                "an interface instance is not a child module: {child_modules:?}"
            );
        }
        let consumer = find("consumer");
        let consumer_ports = std::slice::from_raw_parts(consumer.ports.items, consumer.ports.count);
        assert_eq!(
            optional_string(consumer_ports[0].direction).as_deref(),
            Some("InOut"),
            "an interface port is bidirectional and must not crash"
        );
        assert_eq!(
            optional_string(consumer_ports[0].type_name).as_deref(),
            Some("bus_if")
        );
        let reflink = find("reflink");
        let reflink_ports = std::slice::from_raw_parts(reflink.ports.items, reflink.ports.count);
        assert_eq!(
            optional_string(reflink_ports[0].direction).as_deref(),
            Some("Ref")
        );
        vd_module_catalog_free(&mut catalog);
        assert!(catalog.modules.items.is_null());
        vd_service_destroy(service);
    }
}

#[test]
fn catalog_rejects_an_empty_file_set_with_a_typed_error() {
    let service = vd_service_create();
    // SAFETY: the pointers are null/zero by contract.
    unsafe {
        let files = VdStringList {
            count: 0,
            items: std::ptr::null_mut(),
        };
        let options: VdCatalogOptions = std::mem::zeroed();
        let job = vd_catalog_start(service, &files, &options);
        assert_eq!(job, 0);
        let (code, message) = last_error(service);
        assert_eq!(code, 1, "typed invalid-argument error: {message}");
        assert!(message.contains("empty"), "{message}");
        assert_eq!(vd_catalog_release(service, 12345), 0);
        vd_service_destroy(service);
    }
}

// --- extended project search ------------------------------------------------

unsafe fn search(
    service: *mut VdService,
    root: &Path,
    needle: &str,
    options: &VdSearchOptions,
) -> (i32, VdSearchResults, VdSearchSummary, (i32, String)) {
    let root = CString::new(root.display().to_string()).unwrap_or_default();
    let needle = CString::new(needle).unwrap_or_default();
    let job = vd_search_tree_query_ex(service, root.as_ptr(), needle.as_ptr(), options);
    assert_ne!(job, 0, "search rejected: {:?}", last_error(service));
    let mut results = std::mem::zeroed::<VdSearchResults>();
    let mut summary = std::mem::zeroed::<VdSearchSummary>();
    let status = wait_for(
        || vd_search_tree_poll(service, job, &mut results, &mut summary),
        Duration::from_secs(10),
    );
    let error = last_error(service);
    (status, results, summary, error)
}

unsafe fn hit_ranges(results: &VdSearchResults) -> Vec<(String, u32, u32, u32, u32)> {
    let hits = std::slice::from_raw_parts(results.items, results.count);
    hits.iter()
        .map(|hit| {
            (
                optional_string(hit.path).unwrap_or_default(),
                hit.line,
                hit.column,
                hit.end_column,
                hit.match_length,
            )
        })
        .collect()
}

#[test]
fn extended_search_returns_every_occurrence_with_its_exact_range() {
    let Some(dir) = tempfile::tempdir().ok() else {
        return;
    };
    write_file(dir.path(), "a.sv", "foo foo foofoo\n");
    write_file(dir.path(), "b.txt", "foo\n");
    let service = vd_service_create();
    // SAFETY: the option lists are empty and the strings live through the call.
    unsafe {
        let extensions = [CString::new("sv").unwrap_or_default()];
        let options = VdSearchOptions {
            case_sensitive: 0,
            whole_word: 1,
            regex: 0,
            all_matches: 1,
            _reserved: [0; 4],
            extensions: string_list(&extensions),
            max_results: 100,
            max_file_bytes: 0,
            max_duration_ms: 0,
        };
        let (status, mut results, summary, error) = search(service, dir.path(), "foo", &options);
        assert_eq!(status, VD_STATUS_READY, "{error:?}");
        assert_eq!(
            hit_ranges(&results),
            vec![
                ("a.sv".to_string(), 1, 1, 4, 3),
                ("a.sv".to_string(), 1, 5, 8, 3),
            ],
            "whole-word rejects the two `foo`s inside `foofoo`; every occurrence keeps its exact range"
        );
        assert_eq!(summary.files_scanned, 1, "b.txt is excluded by extension");
        assert_eq!(summary.hit_count, 2);
        vd_search_results_free(&mut results);
        vd_service_destroy(service);
    }
}

#[test]
fn extended_search_regex_and_invalid_pattern_are_observable() {
    let Some(dir) = tempfile::tempdir().ok() else {
        return;
    };
    write_file(dir.path(), "a.sv", "wire [3:0] a;\nwire axb;\n");
    let service = vd_service_create();
    // SAFETY: the strings live through each call.
    unsafe {
        let options = VdSearchOptions {
            case_sensitive: 1,
            whole_word: 0,
            regex: 1,
            all_matches: 0,
            _reserved: [0; 4],
            extensions: string_list(&[]),
            max_results: 100,
            max_file_bytes: 0,
            max_duration_ms: 0,
        };
        let (status, mut results, _, error) =
            search(service, dir.path(), r"\[[0-9]+:0\]", &options);
        assert_eq!(status, VD_STATUS_READY, "{error:?}");
        assert_eq!(
            hit_ranges(&results),
            vec![("a.sv".to_string(), 1, 6, 11, 5)]
        );
        vd_search_results_free(&mut results);

        let bad = CString::new("(").unwrap_or_default();
        let root = CString::new(dir.path().display().to_string()).unwrap_or_default();
        let job = vd_search_tree_query_ex(service, root.as_ptr(), bad.as_ptr(), &options);
        assert_ne!(job, 0, "the job starts and fails in the worker");
        let mut results = std::mem::zeroed::<VdSearchResults>();
        let mut summary = std::mem::zeroed::<VdSearchSummary>();
        let status = wait_for(
            || vd_search_tree_poll(service, job, &mut results, &mut summary),
            Duration::from_secs(10),
        );
        let (_, message) = last_error(service);
        assert_eq!(status, VD_STATUS_FAILED);
        assert!(message.contains("invalid search query"), "{message}");
        vd_service_destroy(service);
    }
}

// --- typed lint knobs -------------------------------------------------------

fn batch_options(engine: i32) -> VdLintBatchOptions {
    VdLintBatchOptions {
        engine,
        slang_binary: std::ptr::null(),
        verible_binary: std::ptr::null(),
        rules_path: std::ptr::null(),
        include_dirs: VdStringList {
            count: 0,
            items: std::ptr::null_mut(),
        },
        defines: VdStringList {
            count: 0,
            items: std::ptr::null_mut(),
        },
        timeout_ms: 0,
        max_diagnostics: 0,
        ruleset: 0,
        rules: std::ptr::null(),
        waiver_files: std::ptr::null(),
        slang_suppressions: std::ptr::null(),
        error_limit: 0,
    }
}

unsafe fn option_line(options: &VdLintBatchOptions) -> String {
    let file = CString::new("top.sv").unwrap_or_default();
    let line = vd_lint_batch_command_line(options, file.as_ptr());
    assert!(!line.is_null(), "the echo must not be null");
    error_string(line)
}

/// The argv half of the echo line: `knobs_note` documents the flag spellings, so
/// a `contains` check on the whole line would match the documentation instead of
/// the argv that would really be spawned.
fn argv_part(line: &str) -> &str {
    line.split(" | ").next().unwrap_or(line)
}

#[test]
fn lint_defaults_send_no_flag_and_each_engine_gets_only_its_own_knobs() {
    // SAFETY: all pointers are null or live for the call.
    unsafe {
        let defaults = batch_options(0);
        let line = option_line(&defaults);
        let argv = argv_part(&line);
        for flag in [
            "--ruleset=",
            "--rules=",
            "--waiver_files=",
            "--error-limit=",
            "-Wno-",
        ] {
            assert!(!argv.contains(flag), "default {flag} leaked: {argv}");
        }

        let rules = CString::new("+a,-b").unwrap_or_default();
        let waivers = CString::new("/w/one.txt,/w/two.txt").unwrap_or_default();
        let suppressions = CString::new("-Wno-unused -Werror=foo").unwrap_or_default();
        let verible = VdLintBatchOptions {
            engine: 1,
            ruleset: 1,
            rules: rules.as_ptr(),
            waiver_files: waivers.as_ptr(),
            slang_suppressions: suppressions.as_ptr(),
            error_limit: 5,
            ..batch_options(1)
        };
        let line = option_line(&verible);
        let argv = argv_part(&line);
        assert!(argv.contains("--ruleset=all"), "{argv}");
        assert!(argv.contains("--rules=+a,-b"), "{argv}");
        assert!(
            argv.contains("--waiver_files=/w/one.txt,/w/two.txt"),
            "{argv}"
        );
        assert!(
            !argv.contains("--error-limit=") && !argv.contains("-Wno-unused"),
            "slang-only knobs must not reach verible: {argv}"
        );

        let slang = VdLintBatchOptions {
            engine: 0,
            ruleset: 2,
            rules: rules.as_ptr(),
            waiver_files: waivers.as_ptr(),
            slang_suppressions: suppressions.as_ptr(),
            error_limit: 5,
            ..batch_options(0)
        };
        let line = option_line(&slang);
        let argv = argv_part(&line);
        assert!(argv.contains("-Wno-unused"), "{argv}");
        assert!(argv.contains("-Werror=foo"), "{argv}");
        assert!(argv.contains("--error-limit=5"), "{argv}");
        assert!(
            !argv.contains("--ruleset=")
                && !argv.contains("--rules=")
                && !argv.contains("--waiver_files="),
            "verible-only knobs must not reach slang: {argv}"
        );
    }
}

#[test]
fn lint_batch_rejects_an_invalid_knob_before_any_job_starts() {
    let service = vd_service_create();
    let file = CString::new("top.sv").unwrap_or_default();
    let files = [file.clone()];
    // SAFETY: the pointers live through the calls.
    unsafe {
        let file_list = string_list(&files);

        let bad_ruleset = VdLintBatchOptions {
            ruleset: 3,
            ..batch_options(0)
        };
        assert_eq!(
            vd_deeplint_batch_start(service, &file_list, &bad_ruleset),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, 1, "{message}");
        assert!(message.contains("ruleset must be 0"), "{message}");

        let bad_rules = CString::new("bad rule").unwrap_or_default();
        let bad_charset = VdLintBatchOptions {
            engine: 1,
            rules: bad_rules.as_ptr(),
            ..batch_options(1)
        };
        assert_eq!(
            vd_deeplint_batch_start(service, &file_list, &bad_charset),
            0
        );
        let (code, message) = last_error(service);
        assert_eq!(code, 1, "{message}");
        assert!(message.contains("rules:"), "{message}");

        let bad_suppression = CString::new("-Xfoo").unwrap_or_default();
        let bad_slang = VdLintBatchOptions {
            slang_suppressions: bad_suppression.as_ptr(),
            ..batch_options(0)
        };
        assert_eq!(vd_deeplint_batch_start(service, &file_list, &bad_slang), 0);
        let (code, message) = last_error(service);
        assert_eq!(code, 1, "{message}");
        assert!(message.contains("slang_suppressions"), "{message}");

        vd_service_destroy(service);
    }
}

#[test]
fn unsupported_knob_lists_are_exposed_for_the_settings_page() {
    // SAFETY: the export returns an owned string for a valid engine code.
    unsafe {
        let slang = vd_lint_unsupported_knobs(0);
        assert_eq!(
            error_string(slang),
            "ruleset,rules,waiver_files",
            "slang ignores the verible knobs"
        );
        let verible = vd_lint_unsupported_knobs(1);
        assert_eq!(
            error_string(verible),
            "slang_suppressions,error_limit,include_dirs,defines"
        );
        assert!(vd_lint_unsupported_knobs(7).is_null());
    }
}

// --- discovered rules path --------------------------------------------------

unsafe fn project_rules_path(service: *mut VdService, root: &Path) -> Option<String> {
    let start = CString::new(root.display().to_string()).unwrap_or_default();
    let job = vd_project_expand(service, start.as_ptr(), std::ptr::null(), std::ptr::null());
    assert_ne!(job, 0, "project expansion rejected");
    let mut info = std::mem::zeroed::<VdProjectInfo>();
    let status = wait_for(
        || vd_project_poll(service, job, &mut info),
        Duration::from_secs(20),
    );
    assert_eq!(status, VD_STATUS_READY, "project expansion failed");
    let rules = optional_string(info.rules_path);
    vd_project_info_free(&mut info);
    rules
}

#[test]
fn project_info_exposes_the_discovered_rules_file_absolute_or_null() {
    let Some(with_rules) = tempfile::tempdir().ok() else {
        return;
    };
    write_file(
        with_rules.path(),
        "vedit.toml",
        "[project]\nfilelists = []\n",
    );
    write_file(
        with_rules.path(),
        ".rules.verible_lint",
        "lint:\n  rules:\n    - always-comb\n",
    );
    let Some(without_rules) = tempfile::tempdir().ok() else {
        return;
    };
    write_file(
        without_rules.path(),
        "vedit.toml",
        "[project]\nfilelists = []\n",
    );
    let service = vd_service_create();
    // SAFETY: the strings live through each call.
    unsafe {
        let found = project_rules_path(service, with_rules.path());
        let expected = with_rules.path().join(".rules.verible_lint");
        assert_eq!(
            found.as_deref(),
            Some(expected.display().to_string().as_str()),
            "the discovered file is reported as an absolute path"
        );
        assert!(
            Path::new(found.as_deref().unwrap_or_default()).is_absolute(),
            "the ABI promises an absolute path"
        );
        assert_eq!(project_rules_path(service, without_rules.path()), None);
        vd_service_destroy(service);
    }
}
