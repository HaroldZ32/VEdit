//! Integration tests against the REAL lint tools (T138).
//!
//! These prove the AC-2.5 claim — the project's include dirs and defines reach
//! the tool — by measuring the OUTCOME, not the argv: a file that needs
//! `` `include "'defs.svh" `` and a macro-defined width is clean only when the
//! settings are passed, and dirty when they are not. The verible test parses a
//! real `verible-verilog-lint` finding.
//!
//! Binaries: `VEDIT_SLANG` or `build/slang/out/slang`; `VEDIT_VERIBLE_BIN_DIR`
//! or `.mpd/recon/verible/bin` (the repository's fetched Verible suite). The
//! tests fail loudly with a remediation message rather than skipping.

use std::fs;
use std::path::{Path, PathBuf};

use vd_deeplint::{lint_with, EngineOptions, LintEngine, Severity};

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn slang_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        return PathBuf::from(value);
    }
    let candidate = repo_root().join("build/slang/out/slang");
    assert!(
        candidate.exists(),
        "slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh` (looked at {})",
        candidate.display()
    );
    candidate
}

fn verible_lint_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_VERIBLE_LINT") {
        return PathBuf::from(value);
    }
    let directory = std::env::var("VEDIT_VERIBLE_BIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root().join(".mpd/recon/verible/bin"));
    let path = directory.join("verible-verilog-lint");
    assert!(
        path.is_file(),
        "missing {}{} — set VEDIT_VERIBLE_BIN_DIR or run `bash scripts/fetch-verible.sh`",
        path.display(),
        ""
    );
    path
}

fn write_project(dir: &Path) -> PathBuf {
    fs::create_dir_all(dir.join("inc")).unwrap_or_else(|error| panic!("mkdir inc: {error}"));
    fs::write(
        dir.join("inc/defs.svh"),
        "`ifndef WIDTH\n`define WIDTH 4\n`endif\n",
    )
    .unwrap_or_else(|error| panic!("write header: {error}"));
    let top = dir.join("top.sv");
    fs::write(
        &top,
        "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data;\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("write top: {error}"));
    top
}

fn options(include: bool) -> EngineOptions {
    let mut options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(slang_binary()),
        ..EngineOptions::default()
    };
    if include {
        options.include_dirs = vec![PathBuf::from("inc")];
        options.defines = vec!["WIDTH=8".to_string()];
    }
    options
}

#[test]
fn slang_really_receives_the_include_dirs_and_defines() {
    let dir = tempfile::Builder::new()
        .prefix("vd-real-include-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let top = write_project(dir.path());

    // Run the tool from the project directory so `-I inc` is meaningful.
    let previous = std::env::current_dir().unwrap_or_else(|error| panic!("cwd: {error}"));
    std::env::set_current_dir(dir.path())
        .unwrap_or_else(|error| panic!("chdir into the project: {error}"));
    let without = lint_with(&top, &options(false));
    let with = lint_with(&top, &options(true));
    std::env::set_current_dir(&previous).unwrap_or_else(|error| panic!("chdir back: {error}"));

    assert!(without.is_success(), "{:?}", without.failure);
    assert!(with.is_success(), "{:?}", with.failure);
    assert!(
        !without.diagnostics.is_empty(),
        "without -I/-D the header cannot be found — the tool must complain"
    );
    assert!(
        with.diagnostics.is_empty(),
        "with -I inc -D WIDTH=8 the file is clean; got {:?}",
        with.diagnostics
            .iter()
            .map(|diagnostic| diagnostic.message.as_str())
            .collect::<Vec<_>>()
    );
    println!(
        "real slang settings echo:{}",
        vd_deeplint::command_line(
            LintEngine::Slang,
            &slang_binary(),
            &top,
            &options(true).include_dirs,
            &options(true).defines,
            None,
        )
    );
    println!(
        "without settings: {} diagnostic(s); with settings: {} diagnostic(s)",
        without.total_diagnostics, with.total_diagnostics
    );
    assert_eq!(with.engine, LintEngine::Slang);
    assert_eq!(with.tool, "slang");
    assert_eq!(with.stage, "done");
}

#[test]
fn verible_lint_findings_are_parsed_from_the_real_tool() {
    let dir = tempfile::Builder::new()
        .prefix("vd-real-verible-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let top = dir.path().join("top.sv");
    // A tab is a finding of verible's DEFAULT ruleset (`no-tabs`), so this runs
    // exactly the ruleset the engine's argv selects — no hidden `--ruleset=all`.
    fs::write(&top, "module top;\n\tlogic a;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write source: {error}"));

    let options = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_lint_binary()),
        ..EngineOptions::default()
    };
    let report = lint_with(&top, &options);
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.engine, LintEngine::Verible);
    assert_eq!(report.tool, "verible-verilog-lint");
    assert!(
        report.total_diagnostics >= 1,
        "the tab is a finding of the default ruleset"
    );
    let finding = report
        .diagnostics
        .iter()
        .find(|diagnostic| diagnostic.code == "no-tabs")
        .unwrap_or_else(|| panic!("the no-tabs rule fired"));
    assert_eq!(
        finding.severity,
        Severity::Warning,
        "verible has no error level"
    );
    assert!(
        finding.message.contains("Use spaces, not tabs"),
        "{}",
        finding.message
    );
    let range = finding
        .editor_range
        .unwrap_or_else(|| panic!("the position converts"));
    assert_eq!(range.start.line, 1, "0-based line of the tab");
}

/// RM-4 (lint side): `slang --diag-json` reports `file` relative to ITS process
/// cwd, exactly like `--ast-json` does. The lint child inherits the app's cwd
/// (no `current_dir`), so a project in a subdirectory produced diagnostics whose
/// file never matched the editor's absolute path - verdicts would key on a path
/// that does not exist, and a file dot could never attach. This test places the
/// source OUTSIDE the test's working directory on purpose, so the tool MUST
/// report it relatively, and asserts the diagnostic names the real absolute file.
#[test]
fn a_diagnostic_names_the_real_absolute_file() {
    let dir = tempfile::Builder::new()
        .prefix("vd-lint-subdir-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let source = dir.path().join("rtl/warn.sv");
    fs::create_dir_all(dir.path().join("rtl")).unwrap_or_else(|error| panic!("mkdir rtl: {error}"));
    // A width mismatch is one real slang warning (verified with 12.x): the
    // 1-bit `clk` is assigned to the 4-bit `y`.
    fs::write(
        &source,
        "module warn_me (input logic clk, output logic [3:0] y);\n  assign y = clk;\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("write warn.sv: {error}"));

    let report = lint_with(
        &source,
        &EngineOptions {
            engine: LintEngine::Slang,
            slang_binary: Some(slang_binary()),
            ..EngineOptions::default()
        },
    );
    assert!(report.is_success(), "{:?}", report.failure);
    assert!(
        !report.diagnostics.is_empty(),
        "the fixture must produce at least one real warning"
    );
    let expected = fs::canonicalize(&source)
        .unwrap_or_else(|error| panic!("canonicalise {}: {error}", source.display()))
        .display()
        .to_string();
    for diagnostic in &report.diagnostics {
        let location = diagnostic
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("{}: no location", diagnostic.code));
        assert!(
            std::path::Path::new(&location.file).is_absolute(),
            "{}: the diagnostic file is not absolute: {}",
            diagnostic.code,
            location.file
        );
        assert_eq!(
            fs::canonicalize(&location.file)
                .unwrap_or_else(|error| panic!("canonicalise {}: {error}", location.file))
                .display()
                .to_string(),
            expected,
            "{}: the diagnostic must name the file that was linted",
            diagnostic.code
        );
    }
}
