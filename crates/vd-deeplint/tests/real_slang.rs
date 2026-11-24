//! Integration test against the REAL slang artifact built by T3.
//!
//! The binary is located through `VEDIT_SLANG`, or the repository-relative
//! `build/slang/out/slang` (produced by `bash build/slang/build.sh`). The test
//! fails loudly with a remediation message when neither exists — it never skips
//! silently.

use std::fs;
use std::path::PathBuf;
use std::time::Duration;

use vd_deeplint::{
    lint, lint_with, BatchHandle, BatchReport, EditorPosition, EditorRange, EngineOptions,
    LintEngine, LintOptions, Severity,
};

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures")
        .join(name)
}

fn slang_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        return PathBuf::from(value);
    }
    let candidate = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/slang/out/slang");
    if candidate.exists() {
        return candidate;
    }
    panic!(
        "slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh` (looked at {})",
        candidate.display()
    );
}

fn options() -> LintOptions {
    LintOptions {
        binary: Some(slang_binary()),
        ..LintOptions::default()
    }
}

#[test]
fn real_slang_reports_undeclared_identifier_exactly() {
    let report = lint(&fixture("undeclared.sv"), &options());
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(
        report.exit_code,
        Some(1),
        "slang reports errors with exit 1"
    );
    assert_eq!(report.total_diagnostics, 1);
    let diagnostic = &report.diagnostics[0];
    assert_eq!(diagnostic.severity, Severity::Error);
    assert_eq!(diagnostic.code, "UndeclaredIdentifier");
    assert!(diagnostic.message.contains("undefined_count"));
    // The identifier starts at line 3, byte column 5 (four spaces of indent) and
    // is 15 bytes long: editor position is line 2, UTF-16 character 4..19.
    assert_eq!(
        diagnostic.editor_range,
        Some(EditorRange {
            start: EditorPosition {
                line: 2,
                character: 4
            },
            end: EditorPosition {
                line: 2,
                character: 19
            },
        })
    );
    assert!(diagnostic.conversion_note.is_none());
    let location = diagnostic
        .location
        .as_ref()
        .unwrap_or_else(|| panic!("location missing"));
    assert_eq!(
        (location.line, location.column),
        (3, 5),
        "the raw byte location is preserved for diagnostics"
    );
}

#[test]
fn real_slang_unicode_columns_map_to_utf16_positions() {
    let report = lint(&fixture("unicode_columns.sv"), &options());
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.total_diagnostics, 2);

    // Line 4: `    /* 你好 */ undefined_cjk = 1;`
    let cjk = &report.diagnostics[0];
    assert_eq!(cjk.code, "UndeclaredIdentifier");
    assert!(cjk.message.contains("undefined_cjk"));
    let cjk_location = cjk
        .location
        .as_ref()
        .unwrap_or_else(|| panic!("cjk location missing"));
    assert_eq!(
        (cjk_location.line, cjk_location.column),
        (4, 18),
        "raw byte column"
    );
    assert_eq!(
        cjk.editor_range,
        Some(EditorRange {
            start: EditorPosition {
                line: 3,
                character: 13
            },
            end: EditorPosition {
                line: 3,
                character: 26
            },
        })
    );

    // Line 5: `    /* 😀 */ undefined_emoji = 2;` — the emoji is 2 UTF-16 units
    // but 4 UTF-8 bytes, which is exactly why display/byte columns differ.
    let emoji = &report.diagnostics[1];
    assert!(emoji.message.contains("undefined_emoji"));
    let emoji_location = emoji
        .location
        .as_ref()
        .unwrap_or_else(|| panic!("emoji location missing"));
    assert_eq!(
        (emoji_location.line, emoji_location.column),
        (5, 16),
        "raw byte column"
    );
    assert_eq!(
        emoji.editor_range,
        Some(EditorRange {
            start: EditorPosition {
                line: 4,
                character: 13
            },
            end: EditorPosition {
                line: 4,
                character: 28
            },
        })
    );
}

/// Real `ImplicitNet` warning: it has a location but **no ranges**, and it
/// carries `optionName` (a key this crate does not model directly; it must
/// survive in `raw`).
#[test]
fn real_slang_implicit_net_has_no_ranges_but_keeps_its_location() {
    let report = lint(&fixture("implicit_net.sv"), &options());
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.exit_code, Some(0), "a warning-only run exits 0");
    assert_eq!(report.total_diagnostics, 1);
    let diagnostic = &report.diagnostics[0];
    assert_eq!(diagnostic.severity, Severity::Warning);
    assert_eq!(diagnostic.code, "ImplicitNet");
    assert!(diagnostic.ranges.is_empty(), "ImplicitNet has no ranges");
    let location = diagnostic
        .location
        .as_ref()
        .unwrap_or_else(|| panic!("ImplicitNet location missing"));
    assert_eq!((location.line, location.column), (2, 10));
    assert_eq!(
        diagnostic
            .raw
            .get("optionName")
            .and_then(|value| value.as_str()),
        Some("implicit-net"),
        "unknown diagnostic keys must be preserved"
    );
    // The location is converted to a zero-width editor range.
    assert_eq!(
        diagnostic.editor_range,
        Some(EditorRange {
            start: EditorPosition {
                line: 1,
                character: 9
            },
            end: EditorPosition {
                line: 1,
                character: 9
            },
        })
    );
}

/// Real calibration run over tab / CJK / emoji / combining-mark lines. The
/// expected UTF-16 positions were computed independently from the fixture bytes
/// (see `evidence/deeplint/position-semantics.txt`).
#[test]
fn real_slang_calibration_set_matches_independent_utf16_positions() {
    type ExpectedRow = (u32, u32, u32, u32, u32, u32, &'static str);
    const EXPECTED: &[ExpectedRow] = &[
        (3, 17, 33, 2, 16, 32, "undeclared_plain"),
        (4, 18, 32, 3, 17, 31, "undeclared_tab"),
        (5, 15, 30, 4, 12, 27, "undeclared_cjk1"),
        (6, 18, 33, 5, 13, 28, "undeclared_cjk2"),
        (7, 21, 36, 6, 14, 29, "undeclared_cjk3"),
        (8, 16, 29, 7, 13, 26, "undeclared_e1"),
        (9, 20, 33, 8, 15, 28, "undeclared_e2"),
        (10, 24, 37, 9, 17, 30, "undeclared_e3"),
        (11, 19, 32, 10, 14, 27, "undeclared_ce"),
        (12, 19, 34, 11, 18, 33, "undeclared_comb"),
    ];
    let report = lint(&fixture("display_calibration.sv"), &options());
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.total_diagnostics, EXPECTED.len());
    for &(raw_line, byte_start, byte_end, editor_line, start, end, ident) in EXPECTED {
        let diagnostic = report
            .diagnostics
            .iter()
            .find(|diagnostic| diagnostic.message.contains(ident))
            .unwrap_or_else(|| panic!("no diagnostic for {ident}"));
        let location = diagnostic
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("{ident}: location missing"));
        assert_eq!(
            (location.line, location.column),
            (raw_line, byte_start),
            "{ident}: raw byte start"
        );
        assert_eq!(
            diagnostic.ranges[0].end.column, byte_end,
            "{ident}: ranges end is in the same byte unit"
        );
        assert_eq!(
            diagnostic.editor_range,
            Some(EditorRange {
                start: EditorPosition {
                    line: editor_line,
                    character: start
                },
                end: EditorPosition {
                    line: editor_line,
                    character: end
                },
            }),
            "{ident}: editor UTF-16 range"
        );
    }
}

#[test]
fn real_slang_clean_file_has_no_diagnostics() {
    let report = lint(&fixture("clean.sv"), &options());
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.exit_code, Some(0));
    assert_eq!(report.total_diagnostics, 0);
    assert!(report.diagnostics.is_empty());
    assert_eq!(report.summary(), "no diagnostics");
}

#[test]
fn real_slang_binary_attempts_are_recorded_even_on_success() {
    let report = lint(&fixture("clean.sv"), &options());
    assert!(report.is_success());
    assert!(!report.binary.attempts.is_empty());
    assert!(report.binary.chosen.is_some());
    assert_eq!(
        report.binary.attempts[0].source,
        vd_deeplint::AttemptSource::ExplicitOption
    );
}

/// The project options: the real binary, a project-scale timeout, slang.
fn project_options() -> EngineOptions {
    EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(slang_binary()),
        timeout: Duration::from_secs(30),
        ..EngineOptions::default()
    }
}

/// Run ONE whole-project batch pass over `files` and wait for its report.
fn project_batch(files: Vec<PathBuf>) -> BatchReport {
    let handle = BatchHandle::start(files, project_options())
        .unwrap_or_else(|error| panic!("batch start: {}", error.user_message()));
    let deadline = std::time::Instant::now() + Duration::from_secs(120);
    loop {
        if let Some(report) = handle.take_report() {
            return report;
        }
        assert!(
            std::time::Instant::now() < deadline,
            "the project pass did not finish"
        );
        std::thread::sleep(Duration::from_millis(10));
    }
}

/// The batch rows attributed to `file`.
fn rows_for<'a>(report: &'a BatchReport, file: &PathBuf) -> Vec<&'a vd_deeplint::BatchDiagnostic> {
    report
        .diagnostics
        .iter()
        .filter(|entry| &entry.file == file)
        .collect()
}

/// THE cross-file macro matrix (batch 20261110d, T3).
///
/// `a.sv` only defines a macro; `b.sv` only uses it. Before this fix the pass
/// spawned one slang per file, so b's process started with an empty preprocessor
/// and reported `UnknownDirective` - the RED control below is that exact
/// per-file invocation, kept as a live assertion. The green half runs ONE
/// `--single-unit` process over the set, and the reverse control changes the
/// macro VALUE to prove b sees the *current* definition (not merely that the
/// error disappeared). The isolation half proves a genuinely undefined macro
/// still errors, in the file that names it.
#[test]
fn real_slang_one_project_pass_sees_a_macro_defined_in_an_earlier_file() {
    let dir = tempfile::Builder::new()
        .prefix("vd-project-macro-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let a = dir.path().join("a.sv");
    let b = dir.path().join("b.sv");
    let c = dir.path().join("c.sv");
    fs::write(&a, "`define W 8\n").unwrap_or_else(|error| panic!("write a.sv: {error}"));
    fs::write(
        &b,
        "module b;\n  logic [`W-1:0] x;\n  logic [15:0] y;\n  assign y = x;\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("write b.sv: {error}"));
    fs::write(&c, "module c;\n  logic [`NOTDEF-1:0] z;\nendmodule\n")
        .unwrap_or_else(|error| panic!("write c.sv: {error}"));

    // RED control (the pre-fix path, still public as the single-file API): one
    // process per file cannot see a's macro.
    let per_file = lint_with(&b, &project_options());
    assert!(per_file.is_success(), "{:?}", per_file.failure);
    let unknown = per_file
        .diagnostics
        .iter()
        .find(|diagnostic| diagnostic.code == "UnknownDirective")
        .unwrap_or_else(|| {
            panic!(
                "the per-file invocation cannot see a cross-file define: {:?}",
                per_file.diagnostics
            )
        });
    assert_eq!(unknown.severity, Severity::Error);
    assert!(
        unknown.message.contains("`W"),
        "the RED control names the macro it cannot find: {}",
        unknown.message
    );

    // GREEN: one single-unit process, a before b.
    let report = project_batch(vec![a.clone(), b.clone()]);
    assert_eq!(
        report.fallback,
        None,
        "the project pass must not fall back here: {}",
        report.summary()
    );
    assert!(report.failures.is_empty(), "{:?}", report.failures);
    let b_rows = rows_for(&report, &b);
    assert!(
        b_rows
            .iter()
            .all(|entry| entry.diagnostic.code != "UnknownDirective"),
        "b must see `W from a: {:?}",
        b_rows
            .iter()
            .map(|entry| &entry.diagnostic)
            .collect::<Vec<_>>()
    );
    let width = b_rows
        .iter()
        .find(|entry| entry.diagnostic.code == "WidthExpand")
        .unwrap_or_else(|| {
            panic!(
                "b must see W=8 (an 8->16 width expansion): {:?}",
                b_rows
                    .iter()
                    .map(|entry| &entry.diagnostic)
                    .collect::<Vec<_>>()
            )
        });
    assert_eq!(width.diagnostic.severity, Severity::Warning);
    assert!(
        width.diagnostic.message.contains("8 to 16"),
        "the warning measures the macro VALUE: {}",
        width.diagnostic.message
    );

    // REVERSE control: change the macro in a; b must see the NEW value.
    fs::write(&a, "`define W 16\n").unwrap_or_else(|error| panic!("rewrite a.sv: {error}"));
    let report = project_batch(vec![a.clone(), b.clone()]);
    assert_eq!(report.fallback, None, "{}", report.summary());
    assert!(
        rows_for(&report, &b).is_empty(),
        "with W=16 b no longer expands 8->16, so it has no finding: {:?}",
        rows_for(&report, &b)
            .iter()
            .map(|entry| &entry.diagnostic)
            .collect::<Vec<_>>()
    );

    // ISOLATION: an undefined macro still errors, in the file that uses it.
    let report = project_batch(vec![a, b.clone(), c.clone()]);
    assert_eq!(report.fallback, None, "{}", report.summary());
    let c_rows = rows_for(&report, &c);
    assert_eq!(c_rows.len(), 1, "c has exactly one finding: {c_rows:?}");
    assert_eq!(c_rows[0].diagnostic.code, "UnknownDirective");
    assert_eq!(c_rows[0].diagnostic.severity, Severity::Error);
    assert!(
        c_rows[0].diagnostic.message.contains("NOTDEF"),
        "the error names the undefined macro: {}",
        c_rows[0].diagnostic.message
    );
    assert!(
        rows_for(&report, &b).is_empty(),
        "c's error is not attributed to b"
    );
}

/// A relative `` `include `` resolves against the INCLUDING file's directory
/// (measured on slang 12.1, independent of the child's cwd), so the one project
/// process resolves it exactly like the per-file pass did — this test keeps that
/// invariant, because a cwd-based resolution would have been a silent difference
/// between the two invocation shapes.
#[test]
fn real_slang_project_pass_resolves_a_relative_include_from_the_files_own_directory() {
    let dir = tempfile::Builder::new()
        .prefix("vd-project-include-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let rtl = dir.path().join("rtl");
    let tb = dir.path().join("tb");
    fs::create_dir_all(&rtl).unwrap_or_else(|error| panic!("mkdir rtl: {error}"));
    fs::create_dir_all(&tb).unwrap_or_else(|error| panic!("mkdir tb: {error}"));
    let a = rtl.join("a.sv");
    let b = tb.join("b.sv");
    let defs = tb.join("defs.svh");
    fs::write(&a, "module a;\nendmodule\n").unwrap_or_else(|error| panic!("write a: {error}"));
    fs::write(&defs, "`define W 8\n").unwrap_or_else(|error| panic!("write defs: {error}"));
    fs::write(
        &b,
        "`include \"defs.svh\"\nmodule b;\n  logic [`W-1:0] x;\n  logic [15:0] y;\n  assign y = x;\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("write b: {error}"));

    // rtl/a.sv is first, so the ONE process runs from rtl/ — yet b's own
    // directory is where its include must be found.
    let report = project_batch(vec![a, b.clone()]);
    assert_eq!(
        report.phase,
        vd_deeplint::BatchPhase::Finished,
        "{}",
        report.summary()
    );
    assert_eq!(
        report.fallback,
        None,
        "the include resolves per file, so the project pass must not fall back: {}",
        report.summary()
    );
    let rows = rows_for(&report, &b);
    let width = rows
        .iter()
        .find(|entry| entry.diagnostic.code == "WidthExpand")
        .unwrap_or_else(|| {
            panic!(
                "b saw tb/defs.svh (W=8), so it expands 8->16: {:?}",
                rows.iter()
                    .map(|entry| &entry.diagnostic)
                    .collect::<Vec<_>>()
            )
        });
    assert!(
        width.diagnostic.message.contains("8 to 16"),
        "the value came from b's own directory: {}",
        width.diagnostic.message
    );
}
