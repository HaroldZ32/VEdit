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
