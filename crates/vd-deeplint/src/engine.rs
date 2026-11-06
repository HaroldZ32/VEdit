//! Lint engine selection and its argv (T138).
//!
//! Two engines can answer "lint this file":
//!
//! * **slang** — `slang --lint-only -Weverything --diag-column-unit byte
//!   --diag-json <tmp.json> [-I<dir>]… [-D<name>=<value>]… <file>`, parsed from
//!   its JSON report;
//! * **verible** — `verible-verilog-lint [--rules_config=<rules>] <file>`, a
//!   text reporter whose findings are parsed from stderr.
//!
//! The two are deliberately not blended: the caller picks one engine and the
//! report records which one ran, so a diagnostic can be attributed (AC-1.6).
//!
//! # What each engine can receive (measured)
//!
//! `slang --help` lists `-I,--include-directory,+incdir` and
//! `-D,--define-macro,+define`, so the project's `-I`/`-D` are real argv.
//! `verible-verilog-lint --helpfull` lists neither and rejects
//! `--file_list_path` ("Unknown command line flag"), so the verible engine
//! **cannot** take them — [`knobs_note`] says so instead of dropping them
//! silently. (The Verible *language server* does take `--file_list_path`, which
//! is how the LSP backend receives the same settings; see `vd-lsp`.)
//!
//! # Verible's output format (measured, v0.0-4298-g085eef05)
//!
//! ```text
//! path:line:col: message [Category] [rule-name]
//! path:line:col-endcol: message [Category] [rule-name]
//! path:line:col-endline:endcol: message [Category] [rule-name]
//! ```
//!
//! All findings are lint-rule violations (verible has no separate severity
//! levels), so they are reported as [`Severity::Warning`] with the rule name as
//! the diagnostic code. The stream is not fixed: lint-rule findings go to
//! stderr, while a syntax error (which stops the lint pass) goes to stdout as
//! `path:line:col: syntax error at token "…"` — measured on
//! v0.0-4298-g085eef05, so both streams are parsed. The tool exits `1` when it
//! found anything.
//!
//! # Non-UTF-8 / position caveat
//!
//! Verible reports 1-based byte columns, which is the same unit slang is asked
//! for (`--diag-column-unit byte`), so one conversion path serves both. That
//! equality is measured on ASCII; a CJK line goes through the same
//! [`crate::SourceLines`] conversion and is not separately measured for verible.

use std::ffi::OsString;
use std::path::{Path, PathBuf};

use crate::diagnostic::{
    Diagnostic, RawLocation, RawRange, RawRangePosition, Severity, SourceLines,
};
use crate::EngineOptions;

/// Which lint engine runs a file.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LintEngine {
    /// slang's deep lint (`--lint-only --diag-json`).
    #[default]
    Slang,
    /// Verible's `verible-verilog-lint` text reporter.
    Verible,
}

impl LintEngine {
    /// Stable code for the C ABI: 0 slang, 1 verible.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Slang => 0,
            Self::Verible => 1,
        }
    }

    /// Parse the C ABI code; unknown codes are rejected, never defaulted.
    #[must_use]
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Slang),
            1 => Some(Self::Verible),
            _ => None,
        }
    }

    /// Parse a settings value (`slang` / `verible`).
    #[must_use]
    pub fn parse(text: &str) -> Option<Self> {
        match text.trim().to_ascii_lowercase().as_str() {
            "slang" => Some(Self::Slang),
            "verible" | "verible-verilog-lint" | "verible_lint" => Some(Self::Verible),
            _ => None,
        }
    }

    /// Short name used in the status line and settings.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Slang => "slang",
            Self::Verible => "verible",
        }
    }

    /// The executable name (also the tool name shown on a failure, AC-2.10).
    #[must_use]
    pub const fn tool_name(self) -> &'static str {
        match self {
            Self::Slang => "slang",
            Self::Verible => "verible-verilog-lint",
        }
    }

    /// The environment variable that overrides the binary path.
    #[must_use]
    pub const fn env_var(self) -> &'static str {
        match self {
            Self::Slang => "VEDIT_SLANG",
            Self::Verible => "VEDIT_VERIBLE_LINT",
        }
    }
}

/// What this engine accepts from the project settings, for the settings echo.
#[must_use]
pub const fn knobs_note(engine: LintEngine) -> &'static str {
    match engine {
        LintEngine::Slang => {
            "include dirs and defines are passed as -I<dir> / -D<name>=<value> in the real argv; \
             extra -W… suppressions are appended after -Weverything and --error-limit=N is applied; \
             the verible knobs (ruleset / rules / waiver files) are a documented no-op here"
        }
        LintEngine::Verible => {
            "verible-verilog-lint accepts neither -I nor -D (measured with --helpfull: no include/define/\
             file_list_path flag); project include dirs and defines are not applied by this engine; \
             --ruleset= / --rules= / --waiver_files= are sent only when set, and the slang knobs \
             (suppressions / error limit) are a documented no-op here"
        }
    }
}

/// The Verible ruleset selector (`--ruleset=`), shared by the settings panel
/// and the argv builder.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Ruleset {
    /// Verible's own default ruleset: NO `--ruleset=` flag is sent.
    #[default]
    Default,
    /// `--ruleset=all`
    All,
    /// `--ruleset=none`
    None,
}

impl Ruleset {
    /// Stable code for the C ABI: 0 default, 1 all, 2 none.
    #[must_use]
    pub const fn code(self) -> u8 {
        match self {
            Self::Default => 0,
            Self::All => 1,
            Self::None => 2,
        }
    }

    /// Parse the C ABI code; unknown codes are rejected, never defaulted.
    #[must_use]
    pub const fn from_code(code: u8) -> Option<Self> {
        match code {
            0 => Some(Self::Default),
            1 => Some(Self::All),
            2 => Some(Self::None),
            _ => None,
        }
    }

    /// The value passed to `--ruleset=`.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::All => "all",
            Self::None => "none",
        }
    }
}

/// The option groups an engine ignores (documented no-op: no argv is sent).
///
/// This is the settings panel's grey-out/tooltip source (ux-batch-2 §0.1 R1):
/// a control whose knob is listed here must be disabled with the reason, not
/// silently sent to a tool that would reject it.
#[must_use]
pub const fn unsupported_knobs(engine: LintEngine) -> &'static [&'static str] {
    match engine {
        LintEngine::Slang => &["ruleset", "rules", "waiver_files"],
        LintEngine::Verible => &[
            "slang_suppressions",
            "error_limit",
            "include_dirs",
            "defines",
        ],
    }
}

/// Validate a Verible `--rules=` list.
///
/// One entry per rule, `+rule` / `-rule` / `rule`, comma separated. A rule name
/// is `[A-Za-z0-9_-]+`; the whole value becomes ONE argv entry, never a shell
/// fragment. Blank input is "no flag".
///
/// # Errors
///
/// Names an empty entry, an invalid sign, or a character outside the set.
pub fn parse_rules_list(text: &str) -> Result<Option<String>, String> {
    let text = text.trim();
    if text.is_empty() {
        return Ok(None);
    }
    let mut entries = Vec::new();
    for entry in text.split(',') {
        let entry = entry.trim();
        if entry.is_empty() {
            return Err("rules: an empty entry is not allowed (use `+rule,-rule`)".to_string());
        }
        let (sign, name) = match entry.chars().next() {
            Some('+') => ("+", &entry[1..]),
            Some('-') => ("-", &entry[1..]),
            _ => ("", entry),
        };
        if name.is_empty()
            || !name.chars().all(|character| {
                character.is_ascii_alphanumeric() || matches!(character, '_' | '-')
            })
        {
            return Err(format!(
                "rules: `{entry}` is not a rule name ([A-Za-z0-9_-]+, optionally prefixed by + or -)"
            ));
        }
        entries.push(format!("{sign}{name}"));
    }
    Ok(Some(entries.join(",")))
}

/// Validate Verible `--waiver_files=` (comma-separated paths).
///
/// Paths are passed verbatim inside ONE argv entry; a comma cannot appear in a
/// path because it is the separator. Blank input is "no flag".
///
/// # Errors
///
/// Names an empty entry or a control character.
pub fn parse_waiver_files(text: &str) -> Result<Vec<PathBuf>, String> {
    let text = text.trim();
    if text.is_empty() {
        return Ok(Vec::new());
    }
    let mut files = Vec::new();
    for entry in text.split(',') {
        let entry = entry.trim();
        if entry.is_empty() {
            return Err("waiver_files: an empty path entry is not allowed".to_string());
        }
        if entry.chars().any(char::is_control) {
            return Err(format!(
                "waiver_files: `{entry}` contains a control character"
            ));
        }
        files.push(PathBuf::from(entry));
    }
    Ok(files)
}

/// Validate extra slang `-W…` suppression tokens.
///
/// Tokens are whitespace- or comma-separated and each becomes its OWN argv
/// entry (no shell joining). The accepted shape is `-W` followed by
/// `[A-Za-z0-9_=-]+`, which covers `-Wno-foo`, `-Wfoo`, `-Werror=foo`,
/// `-Wwarn=foo` and `-Wnone`. Blank input is "no options".
///
/// # Errors
///
/// Names a token that does not start with `-W` or carries an invalid character.
pub fn parse_slang_suppressions(text: &str) -> Result<Vec<String>, String> {
    let text = text.trim();
    if text.is_empty() {
        return Ok(Vec::new());
    }
    let mut tokens = Vec::new();
    for token in text.split(|character: char| character.is_whitespace() || character == ',') {
        let token = token.trim();
        if token.is_empty() {
            continue;
        }
        let Some(rest) = token.strip_prefix("-W") else {
            return Err(format!(
                "slang_suppressions: `{token}` is not a -W… option (e.g. -Wno-unused)"
            ));
        };
        if rest.is_empty()
            || !rest.chars().all(|character| {
                character.is_ascii_alphanumeric() || matches!(character, '_' | '-' | '=')
            })
        {
            return Err(format!(
                "slang_suppressions: `{token}` is not a -W… option (allowed: -W[A-Za-z0-9_=-]+)"
            ));
        }
        tokens.push(token.to_string());
    }
    Ok(tokens)
}

/// The argv (without argv[0]) this engine runs for one file, with every knob
/// from `options` applied.
///
/// Fields the selected engine does not support are documented no-ops
/// ([`unsupported_knobs`]) and are NEVER turned into argv: the tool would reject
/// them, and silently sending an invalid argument is the failure mode §0.1 R1
/// forbids.
#[must_use]
pub fn command_args_for(
    engine: LintEngine,
    file: &Path,
    json_path: Option<&Path>,
    options: &EngineOptions,
) -> Vec<OsString> {
    let mut args: Vec<OsString> = Vec::new();
    match engine {
        LintEngine::Slang => {
            args.extend(
                [
                    "--lint-only",
                    "-Weverything",
                    "--diag-column-unit",
                    "byte",
                    "--diag-json",
                ]
                .into_iter()
                .map(OsString::from),
            );
            if let Some(json_path) = json_path {
                args.push(json_path.as_os_str().to_os_string());
            } else {
                args.push(OsString::from("<diagnostics.json>"));
            }
            for directory in &options.include_dirs {
                let mut value = OsString::from("-I");
                value.push(directory.as_os_str());
                args.push(value);
            }
            for define in &options.defines {
                let define = define.trim();
                if !define.is_empty() {
                    let mut value = OsString::from("-D");
                    value.push(define);
                    args.push(value);
                }
            }
            for suppression in &options.slang_suppressions {
                args.push(OsString::from(suppression));
            }
            if let Some(limit) = options.error_limit {
                args.push(OsString::from(format!("--error-limit={limit}")));
            }
        }
        LintEngine::Verible => {
            if let Some(rules) = &options.rules_config {
                let mut value = OsString::from("--rules_config=");
                value.push(rules.as_os_str());
                args.push(value);
            }
            if let Some(ruleset) = options.ruleset {
                if ruleset != Ruleset::Default {
                    args.push(OsString::from(format!("--ruleset={}", ruleset.as_str())));
                }
            }
            if let Some(rules) = &options.rules {
                args.push(OsString::from(format!("--rules={rules}")));
            }
            if !options.waiver_files.is_empty() {
                let mut value = OsString::from("--waiver_files=");
                for (index, file) in options.waiver_files.iter().enumerate() {
                    if index > 0 {
                        value.push(",");
                    }
                    value.push(file.as_os_str());
                }
                args.push(value);
            }
        }
    }
    args.push(file.as_os_str().to_os_string());
    args
}

/// The argv (without argv[0]) this engine runs for one file (legacy form).
///
/// Kept for callers that only have the three original knobs; [`command_args_for`]
/// carries the full set.
#[must_use]
pub fn command_args(
    engine: LintEngine,
    file: &Path,
    json_path: Option<&Path>,
    include_dirs: &[std::path::PathBuf],
    defines: &[String],
    rules_config: Option<&Path>,
) -> Vec<OsString> {
    let options = EngineOptions {
        engine,
        rules_config: rules_config.map(Path::to_path_buf),
        include_dirs: include_dirs.to_vec(),
        defines: defines.to_vec(),
        ..EngineOptions::default()
    };
    command_args_for(engine, file, json_path, &options)
}

/// The exact program plus argv as one line, with every knob applied. Spawns
/// nothing: this is the settings-page echo and the AC-2.5 dump.
#[must_use]
pub fn command_line_for(
    engine: LintEngine,
    binary: &Path,
    file: &Path,
    options: &EngineOptions,
) -> String {
    let mut line = format!(
        "engine={} tool={} program={}",
        engine.as_str(),
        engine.tool_name(),
        binary.display()
    );
    for argument in command_args_for(engine, file, None, options) {
        line.push(' ');
        line.push_str(&argument.to_string_lossy());
    }
    line.push_str(" | ");
    line.push_str(knobs_note(engine));
    line
}

/// The exact program plus argv as one line (legacy form, three original knobs).
#[must_use]
pub fn command_line(
    engine: LintEngine,
    binary: &Path,
    file: &Path,
    include_dirs: &[std::path::PathBuf],
    defines: &[String],
    rules_config: Option<&Path>,
) -> String {
    let options = EngineOptions {
        engine,
        rules_config: rules_config.map(Path::to_path_buf),
        include_dirs: include_dirs.to_vec(),
        defines: defines.to_vec(),
        ..EngineOptions::default()
    };
    command_line_for(engine, binary, file, &options)
}

/// Why a verible finding cannot be mapped, kept for the parse note.
const VERIBLE_PARSE_NOTE: &str = "parsed from verible-verilog-lint text output";

/// Parse `verible-verilog-lint`'s stderr.
///
/// Every recognised finding becomes a [`Diagnostic`] with 1-based byte columns;
/// unrecognised lines (verible's own log lines) are ignored. When `source` is
/// given, each finding also carries its converted [`EditorRange`].
#[must_use]
pub fn parse_verible_output(text: &str, source: Option<&SourceLines>) -> Vec<Diagnostic> {
    let mut diagnostics = Vec::new();
    for line in text.lines() {
        let Some(parsed) = parse_verible_line(line) else {
            continue;
        };
        let mut diagnostic = parsed;
        if let Some(source) = source {
            if let Some(range) = &diagnostic.ranges.first() {
                match source.range(range) {
                    Ok(editor) => diagnostic.editor_range = Some(editor),
                    Err(error) => diagnostic.conversion_note = Some(error.to_string()),
                }
            }
        }
        diagnostics.push(diagnostic);
    }
    diagnostics
}

/// One parsed verible line: `path:line:col[-end]: message [Category] [rule]`.
fn parse_verible_line(line: &str) -> Option<Diagnostic> {
    let bytes = line.as_bytes();
    let mut split = None;
    for index in 0..bytes.len() {
        if bytes[index] != b':' {
            continue;
        }
        let mut cursor = index + 1;
        let digits_start = cursor;
        while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
            cursor += 1;
        }
        if cursor == digits_start || cursor >= bytes.len() || bytes[cursor] != b':' {
            continue;
        }
        // Found `:line:`; the next run of digits must be the column.
        let after = cursor + 1;
        let mut column_cursor = after;
        while column_cursor < bytes.len() && bytes[column_cursor].is_ascii_digit() {
            column_cursor += 1;
        }
        if column_cursor == after {
            continue;
        }
        split = Some((index, digits_start, cursor, after, column_cursor));
        break;
    }
    let (path_end, line_start, line_end, column_start, column_end) = split?;
    let file = &line[..path_end];
    if file.is_empty() {
        return None;
    }
    let line_number: u32 = line[line_start..line_end].parse().ok()?;
    let column: u32 = line[column_start..column_end].parse().ok()?;

    let mut end_line = line_number;
    let mut end_column: Option<u32> = None;
    let mut cursor = column_end;
    if bytes.get(cursor) == Some(&b'-') {
        cursor += 1;
        let start = cursor;
        while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
            cursor += 1;
        }
        if cursor > start {
            let first: u32 = line[start..cursor].parse().ok()?;
            // `line:col-line:col` only when another number follows the colon;
            // otherwise the colon is the message separator and `first` is the
            // range end column on the same line.
            let cross_line = matches!(bytes.get(cursor), Some(b':'))
                && bytes.get(cursor + 1).is_some_and(u8::is_ascii_digit);
            if cross_line {
                // `line:col-line:col`
                cursor += 1;
                let second_start = cursor;
                while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
                    cursor += 1;
                }
                if cursor == second_start {
                    return None;
                }
                end_line = first;
                end_column = Some(line[second_start..cursor].parse().ok()?);
            } else {
                end_column = Some(first);
            }
        }
    }
    // The message starts after `: `.
    if bytes.get(cursor) != Some(&b':') {
        return None;
    }
    cursor += 1;
    if bytes.get(cursor) == Some(&b' ') {
        cursor += 1;
    }
    let raw_message = &line[cursor..];
    if raw_message.is_empty() {
        return None;
    }

    let (message, rule) = split_rule(raw_message);
    let end_column = end_column.unwrap_or(column);
    Some(Diagnostic {
        severity: Severity::Warning,
        message,
        code: rule,
        location: Some(RawLocation {
            file: file.to_string(),
            line: line_number,
            column,
        }),
        ranges: vec![RawRange {
            file: file.to_string(),
            start: RawRangePosition {
                line: line_number,
                column,
            },
            end: RawRangePosition {
                line: end_line,
                column: end_column,
            },
        }],
        symbol_path: None,
        raw: serde_json::Value::Null,
        editor_range: None,
        conversion_note: Some(VERIBLE_PARSE_NOTE.to_string()),
        parse_note: None,
    })
}

/// Split `message [Category] [rule]` into the message and the rule name. The
/// trailing bracket groups are verible's own metadata; the last one is the rule.
fn split_rule(text: &str) -> (String, String) {
    let mut message = text.trim_end().to_string();
    let mut rule = String::new();
    for _ in 0..2 {
        let Some(open) = message.rfind(" [") else {
            break;
        };
        if !message.ends_with(']') {
            break;
        }
        let group = &message[open + 2..message.len() - 1];
        if rule.is_empty() {
            rule = group.to_string();
        }
        message.truncate(open);
        message = message.trim_end().to_string();
    }
    (message, rule)
}

#[cfg(test)]
mod tests {
    use super::{
        command_args, command_args_for, command_line, command_line_for, knobs_note,
        parse_rules_list, parse_slang_suppressions, parse_verible_line, parse_verible_output,
        parse_waiver_files, unsupported_knobs, LintEngine, Ruleset,
    };
    use crate::diagnostic::{Severity, SourceLines};
    use crate::EngineOptions;
    use std::path::{Path, PathBuf};

    fn joined(args: Vec<std::ffi::OsString>) -> Vec<String> {
        args.into_iter()
            .map(|arg| arg.to_string_lossy().to_string())
            .collect()
    }

    #[test]
    fn engine_codes_round_trip_and_unknown_codes_are_rejected() {
        assert_eq!(LintEngine::Slang.code(), 0);
        assert_eq!(LintEngine::Verible.code(), 1);
        assert_eq!(LintEngine::from_code(0), Some(LintEngine::Slang));
        assert_eq!(LintEngine::from_code(1), Some(LintEngine::Verible));
        assert_eq!(LintEngine::from_code(-1), None);
        assert_eq!(
            LintEngine::parse("VERIBLE-VERILOG-LINT"),
            Some(LintEngine::Verible)
        );
        assert_eq!(LintEngine::parse("clang-tidy"), None);
    }

    #[test]
    fn slang_argv_carries_include_dirs_and_defines() {
        let args = joined(command_args(
            LintEngine::Slang,
            Path::new("/p/top.sv"),
            Some(Path::new("/tmp/d.json")),
            &[PathBuf::from("/p/inc")],
            &["WIDTH=8".to_string(), "SIM".to_string()],
            None,
        ));
        assert_eq!(
            args,
            vec![
                "--lint-only",
                "-Weverything",
                "--diag-column-unit",
                "byte",
                "--diag-json",
                "/tmp/d.json",
                "-I/p/inc",
                "-DWIDTH=8",
                "-DSIM",
                "/p/top.sv",
            ]
        );
    }

    #[test]
    fn verible_argv_carries_only_what_the_tool_accepts() {
        let args = joined(command_args(
            LintEngine::Verible,
            Path::new("/p/top.sv"),
            None,
            &[PathBuf::from("/p/inc")],
            &["SIM".to_string()],
            Some(Path::new("/p/.rules.verible_lint")),
        ));
        assert_eq!(
            args,
            vec!["--rules_config=/p/.rules.verible_lint", "/p/top.sv"]
        );
        // The limitation is stated, never silent.
        let line = command_line(
            LintEngine::Verible,
            Path::new("/bin/verible-verilog-lint"),
            Path::new("/p/top.sv"),
            &[],
            &[],
            None,
        );
        assert!(line.contains("engine=verible"));
        assert!(line.contains(knobs_note(LintEngine::Verible)));
        assert!(knobs_note(LintEngine::Verible).contains("accepts neither -I nor -D"));
    }

    #[test]
    fn verible_lines_parse_with_and_without_ranges() {
        let single = parse_verible_line(
            "top.sv:1:8: Declared module does not match the file name [Style: file-names] [module-filename]",
        )
        .unwrap_or_else(|| panic!("the single-position line must parse"));
        assert_eq!(single.severity, Severity::Warning);
        assert_eq!(single.code, "module-filename");
        assert_eq!(
            single.message,
            "Declared module does not match the file name"
        );
        let location = single
            .location
            .unwrap_or_else(|| panic!("a location is reported"));
        assert_eq!((location.line, location.column), (1, 8));
        assert_eq!(
            single.ranges[0].end,
            crate::RawRangePosition { line: 1, column: 8 }
        );

        let same_line =
            parse_verible_line("top.sv:2:1-9: explicit begin [Style: x] [explicit-begin]")
                .unwrap_or_else(|| panic!("a one-line range must parse"));
        assert_eq!(same_line.ranges[0].end.line, 2);
        assert_eq!(same_line.ranges[0].end.column, 9);

        let across = parse_verible_line("top.sv:2:1-4:12: across lines [Style: x] [rule-x]")
            .unwrap_or_else(|| panic!("a cross-line range must parse"));
        assert_eq!(across.ranges[0].start.line, 2);
        assert_eq!(across.ranges[0].end.line, 4);
        assert_eq!(across.ranges[0].end.column, 12);
    }

    #[test]
    fn verible_log_lines_are_not_findings() {
        let text = "E1102 14:40:57.169876       3 verilog-linter.cc:111] Can't read '/tmp/x.sv'\n\
                    top.sv:1:8: real finding [Style: x] [some-rule]\n";
        let diagnostics = parse_verible_output(text, None);
        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0].code, "some-rule");
    }

    #[test]
    fn verible_positions_convert_through_source_lines() {
        let source = SourceLines::from_bytes(b"module top;\nendmodule\n")
            .unwrap_or_else(|error| panic!("source lines: {error}"));
        let diagnostics =
            parse_verible_output("top.sv:1:8-10: name [Style: x] [rule-y]", Some(&source));
        assert_eq!(diagnostics.len(), 1);
        let range = diagnostics[0]
            .editor_range
            .unwrap_or_else(|| panic!("the range converts"));
        assert_eq!(range.start.line, 0);
        assert_eq!(range.start.character, 7);
        assert_eq!(range.end.line, 0);
        assert_eq!(range.end.character, 9);
        assert_eq!(
            (range.start.line, range.start.character),
            (0, 7),
            "1-based line 1 column 8 maps to 0-based line 0 column 7"
        );
    }
    fn full_options(engine: LintEngine) -> EngineOptions {
        EngineOptions {
            engine,
            ruleset: Some(Ruleset::All),
            rules: Some("+a,-b".to_string()),
            waiver_files: vec![PathBuf::from("/w/one.txt"), PathBuf::from("/w/two.txt")],
            slang_suppressions: vec!["-Wno-unused".to_string(), "-Werror=foo".to_string()],
            error_limit: Some(7),
            include_dirs: vec![PathBuf::from("/inc")],
            defines: vec!["WIDTH=8".to_string()],
            rules_config: Some(PathBuf::from("/p/.rules.verible_lint")),
            ..EngineOptions::default()
        }
    }

    #[test]
    fn default_lint_options_send_no_flag_on_either_engine() {
        for engine in [LintEngine::Slang, LintEngine::Verible] {
            let options = EngineOptions {
                engine,
                ..EngineOptions::default()
            };
            let args = joined(command_args_for(
                engine,
                Path::new("top.sv"),
                None,
                &options,
            ));
            for prefix in [
                "--ruleset=",
                "--rules=",
                "--waiver_files=",
                "--error-limit=",
                "-Wno-",
                "-Wwarn=",
            ] {
                assert!(
                    !args.iter().any(|argument| argument.starts_with(prefix)),
                    "{engine:?} must not send {prefix}: {args:?}"
                );
            }
        }
        // An explicit `Ruleset::Default` is the default too: still no flag.
        let options = EngineOptions {
            engine: LintEngine::Verible,
            ruleset: Some(Ruleset::Default),
            ..EngineOptions::default()
        };
        let args = joined(command_args_for(
            LintEngine::Verible,
            Path::new("top.sv"),
            None,
            &options,
        ));
        assert!(!args
            .iter()
            .any(|argument| argument.starts_with("--ruleset=")));
    }

    #[test]
    fn verible_receives_its_knobs_and_ignores_the_slang_ones() {
        let options = full_options(LintEngine::Verible);
        let args = joined(command_args_for(
            LintEngine::Verible,
            Path::new("top.sv"),
            None,
            &options,
        ));
        assert!(args.contains(&"--ruleset=all".to_string()), "{args:?}");
        assert!(args.contains(&"--rules=+a,-b".to_string()), "{args:?}");
        assert!(
            args.contains(&"--waiver_files=/w/one.txt,/w/two.txt".to_string()),
            "the waiver list is ONE argv entry: {args:?}"
        );
        assert!(args.contains(&"--rules_config=/p/.rules.verible_lint".to_string()));
        assert!(
            !args.iter().any(|argument| argument == "-Wno-unused"
                || argument == "-Werror=foo"
                || argument.starts_with("--error-limit=")),
            "slang-only knobs must not reach verible: {args:?}"
        );
    }

    #[test]
    fn slang_receives_its_knobs_and_ignores_the_verible_ones() {
        let options = full_options(LintEngine::Slang);
        let args = joined(command_args_for(
            LintEngine::Slang,
            Path::new("top.sv"),
            None,
            &options,
        ));
        assert!(args.contains(&"-Wno-unused".to_string()), "{args:?}");
        assert!(args.contains(&"-Werror=foo".to_string()), "{args:?}");
        assert!(args.contains(&"--error-limit=7".to_string()), "{args:?}");
        assert!(
            !args
                .iter()
                .any(|argument| argument.starts_with("--ruleset=")
                    || argument.starts_with("--rules=")
                    || argument.starts_with("--waiver_files=")),
            "verible-only knobs must not reach slang: {args:?}"
        );
    }

    #[test]
    fn ruleset_codes_map_to_the_flag_values() {
        assert_eq!(Ruleset::from_code(0), Some(Ruleset::Default));
        assert_eq!(Ruleset::from_code(1), Some(Ruleset::All));
        assert_eq!(Ruleset::from_code(2), Some(Ruleset::None));
        assert_eq!(Ruleset::from_code(3), None);
        assert_eq!(Ruleset::All.code(), 1);
        for (code, flag) in [(1_u8, "--ruleset=all"), (2, "--ruleset=none")] {
            let ruleset = Ruleset::from_code(code).unwrap_or_default();
            let options = EngineOptions {
                engine: LintEngine::Verible,
                ruleset: Some(ruleset),
                ..EngineOptions::default()
            };
            let args = joined(command_args_for(
                LintEngine::Verible,
                Path::new("top.sv"),
                None,
                &options,
            ));
            assert!(args.contains(&flag.to_string()), "{args:?}");
        }
    }

    #[test]
    fn the_lint_option_validators_accept_the_documented_shapes_and_reject_the_rest() {
        assert_eq!(parse_rules_list(""), Ok(None));
        assert_eq!(
            parse_rules_list(" +a, -b ,c "),
            Ok(Some("+a,-b,c".to_string()))
        );
        assert!(parse_rules_list("+a,,b").is_err());
        assert!(parse_rules_list("bad rule").is_err());
        assert!(parse_rules_list("+a;b").is_err());

        assert_eq!(parse_waiver_files(""), Ok(Vec::new()));
        assert_eq!(
            parse_waiver_files("/a, /b"),
            Ok(vec![PathBuf::from("/a"), PathBuf::from("/b")])
        );
        assert!(parse_waiver_files("/a,,/b").is_err());
        assert!(parse_waiver_files("/a\nb").is_err());

        assert_eq!(parse_slang_suppressions(""), Ok(Vec::new()));
        assert_eq!(
            parse_slang_suppressions("-Wno-unused, -Werror=foo"),
            Ok(vec!["-Wno-unused".to_string(), "-Werror=foo".to_string()])
        );
        assert!(parse_slang_suppressions("-Xfoo").is_err());
        assert!(parse_slang_suppressions("-Wfoo;bar").is_err());
        assert!(parse_slang_suppressions("-W").is_err());
    }

    #[test]
    fn unsupported_knobs_name_exactly_the_no_ops() {
        assert_eq!(
            unsupported_knobs(LintEngine::Slang),
            &["ruleset", "rules", "waiver_files"]
        );
        assert_eq!(
            unsupported_knobs(LintEngine::Verible),
            &[
                "slang_suppressions",
                "error_limit",
                "include_dirs",
                "defines"
            ]
        );
    }

    #[test]
    fn command_line_for_echoes_the_same_argv_the_run_uses() {
        let options = full_options(LintEngine::Verible);
        let line = command_line_for(
            LintEngine::Verible,
            Path::new("/tools/verible-verilog-lint"),
            Path::new("top.sv"),
            &options,
        );
        assert!(line.contains("--ruleset=all"), "{line}");
        assert!(line.contains("--rules=+a,-b"), "{line}");
        assert!(
            line.contains("--waiver_files=/w/one.txt,/w/two.txt"),
            "{line}"
        );
        assert!(line.contains("tool=verible-verilog-lint"), "{line}");
    }
}
