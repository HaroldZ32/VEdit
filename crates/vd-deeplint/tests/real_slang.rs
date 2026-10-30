//! Integration test against the REAL slang artifact built by T3.
//!
//! The binary is located through `VEDIT_SLANG`, or the repository-relative
//! `build/slang/out/slang` (produced by `bash build/slang/build.sh`). The test
//! fails loudly with a remediation message when neither exists — it never skips
//! silently.

use std::path::PathBuf;

use vd_deeplint::{lint, EditorPosition, EditorRange, LintOptions, Severity};

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
