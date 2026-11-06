//! Engine selection, argv passthrough and the project-wide batch pass.
//!
//! These tests use small shell scripts in place of the real tools, so they check
//! the contract (which argv is produced, what the parsers accept, whether a
//! cancel really stops the pass) without depending on the installed binaries.
//! The real-binary equality lives in `real_slang.rs`.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use vd_deeplint::{
    BatchHandle, BatchPhase, EngineOptions, LintEngine, LintService, Severity, SeverityClass,
};

fn set_executable(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(path)
            .unwrap_or_else(|error| panic!("stat {} failed: {error}", path.display()))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(path, permissions)
            .unwrap_or_else(|error| panic!("chmod {} failed: {error}", path.display()));
    }
}

fn script(dir: &Path, name: &str, body: &str) -> PathBuf {
    let path = dir.join(name);
    fs::write(&path, body)
        .unwrap_or_else(|error| panic!("write {} failed: {error}", path.display()));
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
fn the_slang_argv_carries_the_project_include_dirs_and_defines() {
    let dir = tempdir("vd-argv-");
    let argv_file = dir.path().join("argv.txt");
    // A fake slang: dump the raw argv, then write a valid empty JSON report.
    let fake = script(
        dir.path(),
        "slang",
        &format!(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" > '{}'\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '[]' > \"$out\"\nexit 0\n",
            argv_file.display()
        ),
    );
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write source: {error}"));

    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake),
        include_dirs: vec![dir.path().join("inc")],
        defines: vec!["WIDTH=8".to_string(), "SIMULATION".to_string()],
        timeout: Duration::from_secs(5),
        ..EngineOptions::default()
    };
    let report = LintService::new().lint_with(&file, &options);
    assert!(report.is_success(), "{}", report.summary());
    assert_eq!(report.engine, LintEngine::Slang);
    assert_eq!(report.stage, "done");

    let argv = fs::read_to_string(&argv_file).unwrap_or_else(|error| panic!("argv dump: {error}"));
    println!("observed slang argv (one argument per line):\n{argv}");
    println!(
        "settings echo:{}",
        vd_deeplint::command_line(
            LintEngine::Slang,
            &options.slang_binary.clone().unwrap_or_default(),
            &file,
            &options.include_dirs,
            &options.defines,
            None,
        )
    );
    let incdir = format!("-I{}", dir.path().join("inc").display());
    assert!(argv.lines().any(|arg| arg == incdir), "argv was:\n{argv}");
    assert!(
        argv.lines().any(|arg| arg == "-DWIDTH=8"),
        "argv was:\n{argv}"
    );
    assert!(
        argv.lines().any(|arg| arg == "-DSIMULATION"),
        "argv was:\n{argv}"
    );
    assert!(argv.lines().any(|arg| arg == file.to_string_lossy()));
}

#[test]
fn the_verible_engine_parses_findings_and_records_its_tool() {
    let dir = tempdir("vd-verible-");
    let fake = script(
        dir.path(),
        "verible-verilog-lint",
        "#!/bin/sh\necho 'top.sv:1:8-10: Declared module does not match the file name [Style: file-names] [module-filename]' >&2\nexit 1\n",
    );
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write source: {error}"));

    let options = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(fake),
        timeout: Duration::from_secs(5),
        ..EngineOptions::default()
    };
    let report = LintService::new().lint_with(&file, &options);
    assert!(report.is_success(), "{}", report.summary());
    assert_eq!(report.engine, LintEngine::Verible);
    assert_eq!(report.tool, "verible-verilog-lint");
    assert_eq!(report.total_diagnostics, 1);
    let diagnostic = &report.diagnostics[0];
    assert_eq!(diagnostic.severity, Severity::Warning);
    assert_eq!(diagnostic.code, "module-filename");
    assert!(diagnostic.message.contains("Declared module"));
    let range = diagnostic
        .editor_range
        .unwrap_or_else(|| panic!("the range converts"));
    assert_eq!((range.start.line, range.start.character), (0, 7));
    assert_eq!((range.end.line, range.end.character), (0, 9));
    let key = vd_deeplint::diagnostic_key(
        &file.to_string_lossy(),
        range.start.line,
        vd_deeplint::class_of_deeplint(&diagnostic.severity),
        &diagnostic.message,
    );
    assert!(
        !key.contains("module-filename"),
        "key is the message, not the rule"
    );
    assert!(key.contains("Declared module does not match the file name"));
    let _ = SeverityClass::Warning;
}

#[test]
fn a_batch_reports_progress_and_finishes_over_the_whole_set() {
    let dir = tempdir("vd-batch-");
    let fake = script(
        dir.path(),
        "slang",
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '[]' > \"$out\"\nexit 0\n",
    );
    let mut files = Vec::new();
    for index in 0..3 {
        let file = dir.path().join(format!("f{index}.sv"));
        fs::write(&file, "module m;\nendmodule\n")
            .unwrap_or_else(|error| panic!("write source: {error}"));
        files.push(file);
    }
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake),
        timeout: Duration::from_secs(5),
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(files.clone(), options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let progress = handle.progress();
    assert_eq!(progress.total, 3);

    let deadline = Instant::now() + Duration::from_secs(20);
    let report = loop {
        if let Some(report) = handle.take_report() {
            break report;
        }
        assert!(Instant::now() < deadline, "batch did not finish in time");
        std::thread::sleep(Duration::from_millis(10));
    };
    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert_eq!(report.done, 3);
    assert_eq!(report.files_linted, 3);
    assert_eq!(report.total_diagnostics, 0);
    assert!(report.diagnostics.is_empty());
    assert!(report.failures.is_empty());
    assert!(handle.progress().phase.is_terminal());
}

#[test]
fn cancel_stops_the_pass_and_no_later_file_is_linted() {
    let dir = tempdir("vd-cancel-");
    let marker = dir.path().join("invocations.txt");
    // Every invocation records itself, then sleeps: the pass must be cancellable
    // while a child is alive, and the child must actually die.
    let fake = script(
        dir.path(),
        "slang",
        &format!(
            "#!/bin/sh\necho run >> '{}'\nexec sleep 30\n",
            marker.display()
        ),
    );
    let mut files = Vec::new();
    for index in 0..5 {
        let file = dir.path().join(format!("c{index}.sv"));
        fs::write(&file, "module m;\nendmodule\n")
            .unwrap_or_else(|error| panic!("write source: {error}"));
        files.push(file);
    }
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake),
        timeout: Duration::from_secs(30),
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(files, options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));

    // Wait until the first child is running, then cancel.
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        assert!(Instant::now() < deadline, "the first child never started");
        if marker.exists()
            && fs::read_to_string(&marker)
                .map(|text| !text.is_empty())
                .unwrap_or(false)
        {
            break;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    handle.cancel();

    let deadline = Instant::now() + Duration::from_secs(10);
    let report = loop {
        if let Some(report) = handle.take_report() {
            break report;
        }
        assert!(Instant::now() < deadline, "cancel did not end the pass");
        std::thread::sleep(Duration::from_millis(10));
    };
    assert_eq!(report.phase, BatchPhase::Cancelled, "{}", report.summary());
    assert!(report.done < 5);
    assert!(
        report.diagnostics.is_empty(),
        "no diagnostics after a cancel"
    );

    // No later file may start after the cancel: the invocation count is frozen.
    let after_cancel = fs::read_to_string(&marker).unwrap_or_default();
    std::thread::sleep(Duration::from_millis(500));
    let later = fs::read_to_string(&marker).unwrap_or_default();
    println!(
        "cancel observation: phase={} done={}/{} diagnostics_after_cancel={} invocations_at_cancel={} invocations_500ms_later={}",
        report.phase.as_str(),
        report.done,
        report.total,
        report.diagnostics.len(),
        after_cancel.lines().count(),
        later.lines().count()
    );
    assert_eq!(
        after_cancel.lines().count(),
        later.lines().count(),
        "a file started after the cancel"
    );
    assert!(handle.is_cancel_requested());
}

/// A fake slang that writes `json` to the `--diag-json` path and exits 1.
fn diagnostics_script(dir: &Path, name: &str, json: &str) -> PathBuf {
    let escaped = json.replace('\\', "\\\\").replace('\'', "'\\''");
    script(
        dir,
        name,
        &format!(
            "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '%s' '{escaped}' > \"$out\"\nexit 1\n"
        ),
    )
}

/// `duplicates` copies of ONE finding plus `distinct` genuinely different ones.
fn flood_json(duplicates: usize, distinct: usize) -> String {
    let mut elements = Vec::with_capacity(duplicates + distinct);
    for _ in 0..duplicates {
        elements.push(
            "{\"severity\":\"warning\",\"message\":\"repeated finding\",\"code\":\"Wrepeated\",\"location\":\"flood.sv:10:5\"}"
                .to_string(),
        );
    }
    for index in 0..distinct {
        elements.push(format!(
            "{{\"severity\":\"warning\",\"message\":\"distinct {index}\",\"code\":\"W{index}\",\"location\":\"flood.sv:20:{index}\"}}"
        ));
    }
    format!("[{}]", elements.join(","))
}

fn run_pass(handle: &BatchHandle) -> vd_deeplint::BatchReport {
    let deadline = Instant::now() + Duration::from_secs(20);
    loop {
        if let Some(report) = handle.take_report() {
            return report;
        }
        assert!(Instant::now() < deadline, "the pass did not finish in time");
        std::thread::sleep(Duration::from_millis(10));
    }
}

#[test]
fn a_warning_flood_is_deduped_and_never_fails_the_pass() {
    let dir = tempdir("vd-flood-");
    let pid_file = dir.path().join("child.pid");
    let json = flood_json(300, 50);
    let fake = diagnostics_script(dir.path(), "slang", &json);
    // Record the child's PID before it writes the report, so the test can prove
    // the child was reaped after the flood.
    let fake_with_pid = script(
        dir.path(),
        "slang",
        &format!(
            "#!/bin/sh\necho $$ > '{}'\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '%s' '{}' > \"$out\"\nexit 1\n",
            pid_file.display(),
            json.replace('\\', "\\\\").replace('\'', "'\\''")
        ),
    );
    let _ = fake;
    let file = dir.path().join("flood.sv");
    fs::write(&file, "module m;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write source: {error}"));
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake_with_pid),
        timeout: Duration::from_secs(5),
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(vec![file], options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let report = run_pass(&handle);

    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert!(report.failure.is_none(), "a flood must not fail the pass");
    assert_eq!(
        report.total_diagnostics, 350,
        "the raw count stays truthful"
    );
    assert_eq!(
        report.diagnostics.len(),
        51,
        "300 copies collapse into one row"
    );
    let repeated = report
        .diagnostics
        .iter()
        .find(|entry| entry.diagnostic.code == "Wrepeated")
        .unwrap_or_else(|| panic!("the repeated finding is retained"));
    assert_eq!(repeated.diagnostic.repeat, 300, "the repeat count is kept");
    let distinct: Vec<&str> = report
        .diagnostics
        .iter()
        .filter(|entry| entry.diagnostic.code != "Wrepeated")
        .map(|entry| entry.diagnostic.code.as_str())
        .collect();
    assert_eq!(distinct.len(), 50, "distinct findings stay separate");
    assert!(distinct.contains(&"W0") && distinct.contains(&"W49"));
    assert_eq!(report.collapsed, 299, "299 copies were collapsed");
    assert_eq!(report.dropped, 0, "collapsing is not dropping");
    assert!(
        report.summary().contains("299") && report.summary().contains("collapsed"),
        "the pass summary names the collapse: {}",
        report.summary()
    );

    // No zombie: the child marked its PID and it is gone after the pass.
    #[cfg(target_os = "linux")]
    {
        std::thread::sleep(Duration::from_millis(50));
        let pid =
            fs::read_to_string(&pid_file).unwrap_or_else(|error| panic!("child pid: {error}"));
        let pid = pid.trim();
        assert!(!pid.is_empty(), "the child wrote its pid");
        assert!(
            !Path::new(&format!("/proc/{pid}")).exists(),
            "the flood child must be reaped, /proc/{pid} still exists"
        );
    }
}

#[test]
fn the_pass_count_bound_drops_but_reports_and_never_fails() {
    let dir = tempdir("vd-passcap-");
    let fake = diagnostics_script(dir.path(), "slang", &flood_json(0, 60));
    let file = dir.path().join("cap.sv");
    fs::write(&file, "module m;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write source: {error}"));
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake),
        timeout: Duration::from_secs(5),
        max_pass_diagnostics: 10,
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(vec![file], options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let report = run_pass(&handle);
    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert!(report.failure.is_none());
    assert_eq!(
        report.diagnostics.len(),
        10,
        "the pass keeps the first 10 distinct"
    );
    assert_eq!(report.dropped, 50, "the other 50 are counted, not kept");
    assert_eq!(report.total_diagnostics, 60);
    assert!(
        report.summary().contains("50 dropped"),
        "the pass summary names the dropped count: {}",
        report.summary()
    );
}

#[test]
fn a_failed_file_is_named_with_its_reason_and_the_other_files_still_report() {
    let dir = tempdir("vd-failnamed-");
    let fake = script(
        dir.path(),
        "slang",
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncase \"$*\" in\n  *bad.sv*) printf '{ this is not json' > \"$out\" ;;\n  *) printf '%s' '[{\"severity\":\"warning\",\"message\":\"ok\",\"code\":\"W1\",\"location\":\"good.sv:1:1\"}]' > \"$out\" ;;\nesac\nexit 1\n",
    );
    let good = dir.path().join("good.sv");
    let bad = dir.path().join("bad.sv");
    fs::write(&good, "module good;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write good: {error}"));
    fs::write(&bad, "module bad;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write bad: {error}"));
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(fake),
        timeout: Duration::from_secs(5),
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(vec![good, bad], options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let report = run_pass(&handle);

    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert_eq!(report.diagnostics.len(), 1, "the good file still reports");
    assert_eq!(
        report.failures.len(),
        1,
        "the bad file is recorded, not dropped"
    );
    let failure = &report.failures[0];
    assert!(
        failure.file.ends_with("bad.sv"),
        "the failed FILE is named: {}",
        failure.file.display()
    );
    assert!(
        failure.message.contains("malformed"),
        "the reason is recorded: {}",
        failure.message
    );
    let summary = report.summary();
    assert!(
        summary.contains("bad.sv") && summary.contains("malformed"),
        "the pass summary names the file and the reason: {summary}"
    );
    assert!(
        summary.contains("1 file(s) failed"),
        "the pass summary counts the failure: {summary}"
    );
}

fn real_slang_binary() -> PathBuf {
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

/// Real-machine flood (U9b): ONE file whose `--diag-json` is **28.3 MB** —
/// 3.4× the old 8 MiB byte cap, i.e. the file the old code dropped from the
/// pass with `slang: lint diagnostics exceeded 8388608 byte limit`.
///
/// Ignored by default: generate the fixture with
/// `evidence/ux-batch-2/u9b-lint-flood/gen_flood.py` and run
/// `VEDIT_U9B_FLOOD_FILE=... cargo test -p vd-deeplint --test engine_batch -- --ignored --nocapture`.
#[test]
#[ignore = "needs VEDIT_U9B_FLOOD_FILE (the generated 28 MB diagnostics flood)"]
fn a_real_diagnostics_flood_is_streamed_and_reported() {
    let path = std::env::var_os("VEDIT_U9B_FLOOD_FILE")
        .unwrap_or_else(|| panic!("set VEDIT_U9B_FLOOD_FILE to the generated flood.sv"));
    let started = Instant::now();
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(real_slang_binary()),
        timeout: Duration::from_secs(120),
        ..EngineOptions::default()
    };
    let handle = BatchHandle::start(vec![PathBuf::from(path)], options)
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let report = run_pass(&handle);
    println!(
        "real-flood: phase={:?} files={}/{} total_findings={} distinct={} dropped={} collapsed={} elapsed_ms={} failures={} summary={}",
        report.phase,
        report.done,
        report.total,
        report.total_diagnostics,
        report.diagnostics.len(),
        report.dropped,
        report.collapsed,
        report.elapsed.as_millis(),
        report.failures.len(),
        report.summary()
    );
    assert_eq!(report.phase, BatchPhase::Finished, "{}", report.summary());
    assert!(report.failure.is_none());
    assert_eq!(report.done, 1, "the flood file is processed, not dropped");
    assert_eq!(report.total_diagnostics, 40_000, "every finding is counted");
    assert_eq!(
        report.diagnostics.len(),
        500,
        "retention is the per-file count bound"
    );
    assert_eq!(report.dropped, 39_500, "the tail is counted, not kept");
    assert!(
        report.elapsed < Duration::from_secs(60),
        "the flood must finish promptly, took {:?}",
        report.elapsed
    );
    let _ = started;
}
