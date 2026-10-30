//! Evidence helper: run the real slang on a fixture and print the position table
//! (raw 1-based byte line:column → editor 0-based UTF-16 line:character).
//!
//! Usage: `VEDIT_SLANG=/path/to/slang cargo run -p vd-deeplint --example diag_table -- FILE`

use std::path::{Path, PathBuf};

use vd_deeplint::{lint, LintOptions};

fn main() {
    let mut arguments = std::env::args().skip(1);
    let Some(file) = arguments.next() else {
        eprintln!("usage: diag_table <file.sv>");
        std::process::exit(2);
    };
    let options = LintOptions {
        binary: std::env::var_os("VEDIT_SLANG").map(PathBuf::from),
        ..LintOptions::default()
    };
    let report = lint(Path::new(&file), &options);
    println!("# file: {file}");
    println!("# binary attempts:");
    print!("{}", report.binary.describe());
    println!("# exit_code={:?}", report.exit_code);
    match &report.failure {
        Some(failure) => println!("# failure: {failure}"),
        None => println!("# failure: none"),
    }
    println!(
        "# total={} shown={} truncated={} elapsed_ms={} summary={}",
        report.total_diagnostics,
        report.diagnostics.len(),
        report.truncated,
        report.elapsed.as_millis(),
        report.summary()
    );
    println!("# raw_line:byte_col -> editor_line:char (UTF-16) | severity code message");
    for diagnostic in &report.diagnostics {
        let raw_start = diagnostic
            .ranges
            .first()
            .map(|range| (range.start.line, range.start.column))
            .or_else(|| {
                diagnostic
                    .location
                    .as_ref()
                    .map(|location| (location.line, location.column))
            });
        let raw_end = diagnostic
            .ranges
            .first()
            .map(|range| (range.end.line, range.end.column));
        let raw_text = match (raw_start, raw_end) {
            (Some(start), Some(end)) => format!("{}:{}..{}:{}", start.0, start.1, end.0, end.1),
            (Some(start), None) => format!("{}:{} (no ranges)", start.0, start.1),
            (None, _) => "<no location>".to_string(),
        };
        match &diagnostic.editor_range {
            Some(range) => println!(
                "{} -> {}:{}..{}:{} | {} {} {}",
                raw_text,
                range.start.line,
                range.start.character,
                range.end.line,
                range.end.character,
                diagnostic.severity.as_str(),
                diagnostic.code,
                diagnostic.message
            ),
            None => println!(
                "{} -> <no editor range: {:?}> | {} {} {}",
                raw_text,
                diagnostic.conversion_note,
                diagnostic.severity.as_str(),
                diagnostic.code,
                diagnostic.message
            ),
        }
    }
}
