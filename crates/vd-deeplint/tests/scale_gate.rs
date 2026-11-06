//! U9c — the real-project-scale gate for the module catalog and the lint pass.
//!
//! This is the case the flat fixtures never had: U9a (the catalog failed a
//! 1176-file design on the 256 MiB `--ast-json` byte cap) and U9b (the lint
//! pass silently dropped files above the 8 MiB `--diag-json` cap) both shipped
//! into a suite whose biggest project is a handful of files. A regression to
//! either old behaviour can therefore pass every gate. This file closes that:
//! it generates a project at test time (nothing is committed), measures the real
//! documents, and asserts the acceptance instead of a proxy.
//!
//! # Gate registration (stable ids)
//!
//! The crate's neighbouring integration tests (`catalog.rs`, `real_slang.rs`,
//! `engine_batch.rs`) are plain cargo integration tests; they are run by the
//! repository's `tests` / `gates` gates through `cargo test --workspace`
//! (`build/gates/tests.sh`, `scripts/gates.sh`). This file follows the same
//! registration, with two stable test ids:
//!
//! * `the_scale_gate_catalog_survives_a_1176_file_design`
//! * `the_scale_gate_lint_survives_a_diagnostic_flood`
//!
//! No ctest registration is involved: the neighbouring Rust tests are not
//! registered in CMake either, and a ctest wrapper would only add a second
//! place for the id to drift.
//!
//! # Cost
//!
//! Generation is a few MB of source; the catalog case spends ~21 s in the
//! debug-profile stream walk of a ~297 MB `--ast-json` document plus ~2 s in
//! slang, and the lint flood ~3 s. Whole file ≈ 30 s on this machine (measured;
//! see `evidence/ux-batch-2/u9c-scale-gate/README.md`), which is why it lives
//! in its own test file/id instead of being folded into a tightly-timed one.
//!
//! # Negative-control switches (manual runs, never set in the gate)
//!
//! * `VEDIT_U9C_FORCE_RETAINED_NODES=N` — forces the catalog's retention budget
//!   down to N nodes. The design then degrades to a PARTIAL catalog, and the
//!   completeness assertion below FAILS (proving the assertion bites).
//! * `VEDIT_U9C_MAX_PASS_DIAGNOSTICS=N` — forces the pass count bound down. The
//!   flood still SUCCEEDS, and the test asserts the truncation counts that the
//!   forced bound must produce (proving the truncated-but-successful contract
//!   is asserted, not ignored).

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use vd_deeplint::{
    build_catalog, BatchHandle, BatchPhase, CatalogOptions, EngineOptions, LintEngine,
};

/// 1176 files: the size the real user reported.
const MODULES: usize = 1176;
/// Signals per module; the measured document size is ~297 MB (see the test log),
/// i.e. above the old 256 MiB `--ast-json` cap.
const SIGNALS_PER_MODULE: usize = 40;
/// Instantiation-chain segment: depth ≤ 10 keeps slang's own 128-instance limit
/// and serde_json's nesting guard far away, and it is the realistic shape.
const SEGMENT: usize = 10;
/// One file's worth of width-truncation warnings for the flood variant.
const FLOOD_WARNINGS: usize = 40_000;
/// The catalog cap this case exists for (`hierarchy::DEFAULT_MAX_JSON_BYTES`
/// before U9a).
const OLD_CATALOG_CAP: u64 = 256 * 1024 * 1024;
/// The lint cap this case exists for (`lib::DEFAULT_MAX_JSON_BYTES` before U9b).
const OLD_LINT_CAP: u64 = 8 * 1024 * 1024;

/// One test at a time: the two cases both spawn real tools and write hundreds of
/// MB of temp files, and the measured numbers in the log must not be polluted by
/// a parallel sibling.
static SERIAL: Mutex<bool> = Mutex::new(false);

fn serial() -> std::sync::MutexGuard<'static, bool> {
    match SERIAL.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

fn tempdir(label: &str) -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix(label)
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir failed: {error}"))
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

fn number_env(name: &str) -> Option<usize> {
    std::env::var(name)
        .ok()
        .and_then(|value| value.parse::<usize>().ok())
}

/// The process's own peak resident set in KiB (`VmHWM`), for the log.
fn peak_rss_kb() -> u64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|status| {
            status
                .lines()
                .find_map(|line| line.strip_prefix("VmHWM:"))
                .and_then(|rest| rest.split_whitespace().next()?.parse::<u64>().ok())
        })
        .unwrap_or(0)
}

/// Every live child of this process, from `/proc/self/task/*/children`.
fn live_children() -> Vec<u32> {
    let mut children = Vec::new();
    let Ok(tasks) = fs::read_dir("/proc/self/task") else {
        return children;
    };
    for task in tasks.flatten() {
        let path = task.path().join("children");
        let Ok(text) = fs::read_to_string(path) else {
            continue;
        };
        for pid in text.split_whitespace() {
            if let Ok(pid) = pid.parse::<u32>() {
                children.push(pid);
            }
        }
    }
    children
}

/// Wait (briefly) until no child remains: a reaped child leaves the list, a
/// zombie stays until it is waited for, so this is the no-zombie assertion.
fn assert_no_children(context: &str) {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let children = live_children();
        if children.is_empty() {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "{context}: the tool child was not reaped, live children: {children:?}"
        );
        std::thread::sleep(Duration::from_millis(20));
    }
}

/// One module source, the same shape as the u9a/u9b generators: `signals`
/// 8-bit nets each with a continuous assignment (the AST/JSON bulk), plus a
/// chain child except inside each `segment`.
fn module_source(index: usize, signals: usize) -> String {
    let mut source = String::with_capacity(256 + signals * 96);
    source.push_str(&format!(
        "module mod_{index} (input logic clk, input logic [7:0] din, output logic [7:0] q);\n"
    ));
    source.push_str("  logic [7:0] acc;\n");
    source.push_str("  always_ff @(posedge clk) acc <= din;\n");
    source.push_str("  assign q = acc;\n");
    for signal in 0..signals {
        source.push_str(&format!("  logic [7:0] sig_{signal};\n"));
        source.push_str(&format!(
            "  assign sig_{signal} = din ^ 8'd{};\n",
            signal & 255
        ));
    }
    let has_child = index > 0 && !index.is_multiple_of(SEGMENT);
    if has_child {
        source.push_str(&format!(
            "  mod_{} u_child (.clk(clk), .din(din ^ 8'd1), .q());\n",
            index - 1
        ));
    }
    source.push_str("endmodule\n");
    source
}

/// Write the 1176-module project into `root`; returns its file list.
fn write_scale_project(root: &Path) -> Vec<PathBuf> {
    let mut files = Vec::with_capacity(MODULES);
    for index in 0..MODULES {
        let path = root.join(format!("mod_{index}.sv"));
        fs::write(&path, module_source(index, SIGNALS_PER_MODULE))
            .unwrap_or_else(|error| panic!("writing {}: {error}", path.display()));
        files.push(path);
    }
    files
}

/// Write ONE file whose lint output floods `count` width-truncation warnings.
fn write_flood_project(root: &Path, count: usize) -> PathBuf {
    let path = root.join("flood.sv");
    let file = fs::File::create(&path).unwrap_or_else(|error| panic!("creating flood.sv: {error}"));
    let mut writer = std::io::BufWriter::new(file);
    writer
        .write_all(b"module flood (input logic [7:0] din, output logic [3:0] q);\n")
        .unwrap_or_else(|error| panic!("writing flood.sv: {error}"));
    for index in 0..count {
        writer
            .write_all(
                format!("  logic [3:0] narrow_{index};\n  assign narrow_{index} = din;\n")
                    .as_bytes(),
            )
            .unwrap_or_else(|error| panic!("writing flood.sv: {error}"));
    }
    writer
        .write_all(b"  assign q = din[3:0];\nendmodule\n")
        .unwrap_or_else(|error| panic!("writing flood.sv: {error}"));
    writer
        .flush()
        .unwrap_or_else(|error| panic!("flushing flood.sv: {error}"));
    path
}

/// Run the tool the way the catalog runs it and return the document size.
fn catalog_document_bytes(slang: &Path, files: &[PathBuf], dir: &Path) -> u64 {
    let json = dir.join("measured-ast.json");
    let status = std::process::Command::new(slang)
        .arg("--ast-json")
        .arg(&json)
        .arg("--ast-json-source-info")
        .args(files)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .unwrap_or_else(|error| panic!("spawning slang for the AST JSON: {error}"));
    let bytes = fs::metadata(&json)
        .unwrap_or_else(|error| panic!("stat {}: {error}", json.display()))
        .len();
    let _ = fs::remove_file(&json);
    println!("u9c: measured --ast-json bytes={bytes} (slang exit {status:?})");
    bytes
}

/// Run the way the lint pass runs it and return the diagnostics JSON size.
fn lint_document_bytes(slang: &Path, flood: &Path, dir: &Path) -> u64 {
    let json = dir.join("measured-diag.json");
    let status = std::process::Command::new(slang)
        .args([
            "--lint-only",
            "-Weverything",
            "--diag-column-unit",
            "byte",
            "--diag-json",
        ])
        .arg(&json)
        .arg(flood)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .unwrap_or_else(|error| panic!("spawning slang for the diagnostics JSON: {error}"));
    let bytes = fs::metadata(&json)
        .unwrap_or_else(|error| panic!("stat {}: {error}", json.display()))
        .len();
    let _ = fs::remove_file(&json);
    println!("u9c: measured --diag-json bytes={bytes} (slang exit {status:?})");
    bytes
}

fn run_pass(handle: &BatchHandle) -> vd_deeplint::BatchReport {
    let deadline = Instant::now() + Duration::from_secs(120);
    loop {
        if let Some(report) = handle.take_report() {
            return report;
        }
        assert!(Instant::now() < deadline, "the lint pass did not finish");
        std::thread::sleep(Duration::from_millis(10));
    }
}

#[test]
fn the_scale_gate_catalog_survives_a_1176_file_design() {
    let _serial = serial();
    let dir = tempdir("u9c-catalog-");
    let generated = Instant::now();
    let files = write_scale_project(dir.path());
    let generation_ms = generated.elapsed().as_millis();
    assert_eq!(files.len(), MODULES);

    let slang = slang_binary();
    let json_bytes = catalog_document_bytes(&slang, &files, dir.path());
    assert!(
        json_bytes > OLD_CATALOG_CAP,
        "the fixture must exceed the old 256 MiB cap: {json_bytes} bytes"
    );

    // Negative-control switch (manual runs only): forcing the retention budget
    // down makes this design a PARTIAL catalog, and the completeness assertion
    // below is what goes red — proving the gate catches the regression shape.
    let forced_nodes = number_env("VEDIT_U9C_FORCE_RETAINED_NODES").unwrap_or(0);

    let started = Instant::now();
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang),
            max_retained_nodes: forced_nodes,
            ..CatalogOptions::default()
        },
    );
    let catalog_ms = started.elapsed().as_millis();
    let warning = catalog.warning.clone().unwrap_or_default();
    println!(
        "u9c-catalog: files={} ast_json_bytes={} generation_ms={generation_ms} catalog_ms={catalog_ms} modules={} tops={} own_peak_rss_kb={} forced_nodes={forced_nodes} warning={warning:?} failure={:?}",
        files.len(),
        json_bytes,
        catalog.modules.len(),
        catalog.modules.iter().filter(|module| module.is_top).count(),
        peak_rss_kb(),
        catalog.failure,
    );

    assert!(
        catalog.failure.is_none(),
        "the catalog FAILED on the real-scale design: {:?}",
        catalog.failure
    );
    assert!(
        catalog.modules.len() >= MODULES,
        "the module tree is incomplete at real scale: {} of {MODULES} module(s); warning={warning:?}",
        catalog.modules.len()
    );
    assert!(
        !warning.contains("partial"),
        "the module tree must be complete, not truncated: {warning}"
    );
    assert!(
        warning.contains("size threshold"),
        "a document above the 256 MiB threshold must be catalogued in full AND noted: {warning:?}"
    );
    assert!(
        catalog.elapsed < Duration::from_secs(120),
        "the catalog must finish, took {:?}",
        catalog.elapsed
    );
    // (c) the elaboration child is reaped: no zombies, no leaked process.
    assert_no_children("catalog");
}

#[test]
fn the_scale_gate_lint_survives_a_diagnostic_flood() {
    let _serial = serial();
    let dir = tempdir("u9c-lint-");
    let generated = Instant::now();
    let flood = write_flood_project(dir.path(), FLOOD_WARNINGS);
    let generation_ms = generated.elapsed().as_millis();

    let slang = slang_binary();
    let json_bytes = lint_document_bytes(&slang, &flood, dir.path());
    assert!(
        json_bytes > OLD_LINT_CAP,
        "the flood must exceed the old 8 MiB lint cap: {json_bytes} bytes"
    );

    // Negative-control switch (manual runs only): a forced pass bound must still
    // SUCCEED, and this test asserts the truncation counts the bound produces.
    let forced_pass = number_env("VEDIT_U9C_MAX_PASS_DIAGNOSTICS").unwrap_or(0);
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(slang),
        timeout: Duration::from_secs(120),
        max_pass_diagnostics: forced_pass,
        ..EngineOptions::default()
    };
    let started = Instant::now();
    let handle = BatchHandle::start(vec![flood], options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let report = run_pass(&handle);
    let pass_ms = started.elapsed().as_millis();
    println!(
        "u9c-lint: warnings={} diag_json_bytes={} generation_ms={generation_ms} pass_ms={pass_ms} phase={:?} files={}/{} total_findings={} distinct={} dropped={} collapsed={} failures={} forced_pass={forced_pass} summary={}",
        FLOOD_WARNINGS,
        json_bytes,
        report.phase,
        report.done,
        report.total,
        report.total_diagnostics,
        report.diagnostics.len(),
        report.dropped,
        report.collapsed,
        report.failures.len(),
        report.summary(),
    );

    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert!(
        report.failure.is_none(),
        "a diagnostic flood must not fail the pass: {:?}",
        report.failure
    );
    assert_eq!(
        report.done, 1,
        "the flood file must be PROCESSED, not dropped"
    );
    assert_eq!(
        report.total_diagnostics, FLOOD_WARNINGS,
        "every finding must be counted"
    );
    assert!(
        report.dropped > 0,
        "the flood must trip a count bound so the truncation path is exercised; \
         summary={}",
        report.summary()
    );
    assert!(
        report.diagnostics.len() <= vd_deeplint::DEFAULT_MAX_DIAGNOSTICS,
        "retention is bounded by the per-file count ({} rows retained)",
        report.diagnostics.len()
    );
    assert!(
        report.summary().contains("dropped"),
        "the pass must REPORT the truncation, not hide it: {}",
        report.summary()
    );
    // (c) the tool child is reaped: no zombies, no leaked process.
    assert_no_children("lint");
}
