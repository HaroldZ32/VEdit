//! Diagnostic model, `--diag-json` parsing and byte-column → UTF-16 conversion.
//!
//! slang is invoked with `--diag-column-unit byte`, so the columns in the JSON
//! are **1-based UTF-8 byte offsets inside the line** (both the start and the
//! end of every reported range; verified in
//! `evidence/deeplint/position-semantics.txt`). The editor (and LSP) use
//! **0-based lines and UTF-16 code-unit characters**, so every reported position
//! is converted here by slicing the source line at a character boundary and
//! counting UTF-16 units. A position that is not on a character boundary is a
//! typed error, never a silent truncation.
//!
//! Not every diagnostic has a position: slang's `ImplicitNet` has no `ranges`
//! and `TooManyErrors` has neither `ranges` nor `location` (measured on
//! slang 12.1). The model therefore keeps `location` optional, keeps every
//! original JSON object in [`Diagnostic::raw`] (so unknown keys such as
//! `optionName` survive), and never drops a diagnostic.

use std::io::Read;

use serde::de::{DeserializeSeed, SeqAccess, Visitor};
use serde::{Deserialize, Deserializer};

/// Diagnostic severity as reported by slang.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Severity {
    /// An error.
    Error,
    /// A warning.
    Warning,
    /// A note attached to another diagnostic.
    Note,
    /// Ignored/filtered diagnostic.
    Ignored,
    /// A severity this crate does not know yet (forward compatible).
    Unknown(String),
}

impl Severity {
    fn from_wire(value: &str) -> Self {
        match value.to_ascii_lowercase().as_str() {
            "error" => Self::Error,
            "warning" => Self::Warning,
            "note" => Self::Note,
            "ignored" => Self::Ignored,
            other => Self::Unknown(other.to_string()),
        }
    }

    /// Stable label for the UI.
    #[must_use]
    pub fn as_str(&self) -> &str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
            Self::Note => "note",
            Self::Ignored => "ignored",
            Self::Unknown(value) => value,
        }
    }
}

/// A position exactly as slang reported it: 1-based line, 1-based **byte**
/// column (because the runner passes `--diag-column-unit byte`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawLocation {
    /// File name as reported by slang.
    pub file: String,
    /// 1-based line number.
    pub line: u32,
    /// 1-based UTF-8 byte column.
    pub column: u32,
}

/// One end of a reported range (1-based line and byte column).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RawRangePosition {
    /// 1-based line number.
    pub line: u32,
    /// 1-based UTF-8 byte column.
    pub column: u32,
}

/// A range as reported by slang.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawRange {
    /// File name as reported by slang.
    pub file: String,
    /// Range start (inclusive).
    pub start: RawRangePosition,
    /// Range end (exclusive).
    pub end: RawRangePosition,
}

/// A position in the editor's model: 0-based line, UTF-16 code-unit character.
/// Identical semantics to `vd-editor`'s `Position` (T9).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct EditorPosition {
    /// 0-based line.
    pub line: u32,
    /// 0-based UTF-16 code-unit offset inside the line.
    pub character: u32,
}

/// A range in the editor's model (half-open, UTF-16 characters, 0-based lines).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EditorRange {
    /// Range start (inclusive).
    pub start: EditorPosition,
    /// Range end (exclusive).
    pub end: EditorPosition,
}

/// One diagnostic mapped into the editor's problem model.
#[derive(Debug, Clone, PartialEq)]
pub struct Diagnostic {
    /// Severity.
    pub severity: Severity,
    /// Human-readable message.
    pub message: String,
    /// slang diagnostic code (`UndeclaredIdentifier`, …).
    pub code: String,
    /// Primary location, when slang reported one (`TooManyErrors` has none).
    pub location: Option<RawLocation>,
    /// All ranges as reported (empty for `ImplicitNet` and `TooManyErrors`).
    pub ranges: Vec<RawRange>,
    /// Symbol path, when slang reports one.
    pub symbol_path: Option<String>,
    /// The original JSON object, so keys this crate does not model (`optionName`,
    /// future additions) survive.
    pub raw: serde_json::Value,
    /// Primary range converted to the editor model, when a position was reported
    /// and conversion succeeded.
    pub editor_range: Option<EditorRange>,
    /// Why conversion failed, when it did (the diagnostic is still usable).
    pub conversion_note: Option<String>,
    /// A problem with the raw fields themselves (e.g. an unparsable location
    /// string); the diagnostic is still usable and [`Self::raw`] has the data.
    pub parse_note: Option<String>,
    /// How many raw findings this row represents.
    ///
    /// `1` for a single-file run and for every row the language server pushes.
    /// The streaming pass parser de-duplicates findings that repeat verbatim
    /// (same file, line, column, code and message) — a warning about an
    /// instantiated module's connection otherwise repeats once per instance —
    /// and counts them here. Two DIFFERENT findings at the same location are
    /// never merged.
    pub repeat: usize,
}

/// Errors from parsing slang's JSON.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseError(pub String);

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for ParseError {}

/// Errors from source-line / column conversion.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConvertError {
    /// The source file is not valid UTF-8.
    InvalidUtf8,
    /// Reading the source file failed.
    Io(String),
    /// The reported line does not exist in the file.
    LineOutOfRange { line: u32 },
    /// The reported byte column is past the end of the line.
    ColumnOutOfRange {
        line: u32,
        column: u32,
        line_len: usize,
    },
    /// The byte column falls inside a multi-byte UTF-8 sequence.
    NotCharBoundary { line: u32, column: u32 },
}

impl std::fmt::Display for ConvertError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidUtf8 => f.write_str("source is not valid UTF-8"),
            Self::Io(message) => write!(f, "reading source failed: {message}"),
            Self::LineOutOfRange { line } => write!(f, "line {line} does not exist in the source"),
            Self::ColumnOutOfRange {
                line,
                column,
                line_len,
            } => write!(
                f,
                "byte column {column} is past the end of line {line} ({line_len} bytes)"
            ),
            Self::NotCharBoundary { line, column } => write!(
                f,
                "byte column {column} on line {line} is inside a multi-byte character"
            ),
        }
    }
}

impl std::error::Error for ConvertError {}

/// The linted source split into lines, with UTF-8 validation up front.
#[derive(Debug, Clone)]
pub struct SourceLines {
    /// Lines without their terminators (a trailing `\r` is stripped).
    lines: Vec<Vec<u8>>,
}

impl SourceLines {
    /// Split raw file bytes into lines.
    ///
    /// # Errors
    /// Returns [`ConvertError::InvalidUtf8`] when the file is not UTF-8.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, ConvertError> {
        std::str::from_utf8(bytes).map_err(|_| ConvertError::InvalidUtf8)?;
        let lines = bytes
            .split(|byte| *byte == b'\n')
            .map(|line| match line.split_last() {
                Some((b'\r', rest)) => rest.to_vec(),
                _ => line.to_vec(),
            })
            .collect();
        Ok(Self { lines })
    }

    /// Read and split the file at `path`.
    ///
    /// # Errors
    /// Returns [`ConvertError::Io`] or [`ConvertError::InvalidUtf8`].
    pub fn from_path(path: &std::path::Path) -> Result<Self, ConvertError> {
        let bytes = std::fs::read(path).map_err(|error| ConvertError::Io(error.to_string()))?;
        Self::from_bytes(&bytes)
    }

    /// Number of lines.
    #[must_use]
    pub fn line_count(&self) -> usize {
        self.lines.len()
    }

    /// Convert a 1-based reported line and 1-based byte column to the editor
    /// position (0-based line, UTF-16 character).
    ///
    /// # Errors
    /// Returns a [`ConvertError`] for an unknown line, a column past the line,
    /// or a column inside a multi-byte UTF-8 sequence.
    pub fn position(&self, line: u32, column: u32) -> Result<EditorPosition, ConvertError> {
        let line_index = line
            .checked_sub(1)
            .ok_or(ConvertError::LineOutOfRange { line })?;
        let line_bytes = self
            .lines
            .get(line_index as usize)
            .ok_or(ConvertError::LineOutOfRange { line })?;
        let offset = column.saturating_sub(1) as usize;
        if offset > line_bytes.len() {
            return Err(ConvertError::ColumnOutOfRange {
                line,
                column,
                line_len: line_bytes.len(),
            });
        }
        // The whole file was validated as UTF-8, so each line is valid too.
        let text = std::str::from_utf8(line_bytes).map_err(|_| ConvertError::InvalidUtf8)?;
        if !text.is_char_boundary(offset) {
            return Err(ConvertError::NotCharBoundary { line, column });
        }
        let character = u32::try_from(text[..offset].encode_utf16().count()).map_err(|_| {
            ConvertError::ColumnOutOfRange {
                line,
                column,
                line_len: line_bytes.len(),
            }
        })?;
        Ok(EditorPosition {
            line: line_index,
            character,
        })
    }

    /// Convert a reported range.
    ///
    /// # Errors
    /// Same as [`SourceLines::position`].
    pub fn range(&self, range: &RawRange) -> Result<EditorRange, ConvertError> {
        Ok(EditorRange {
            start: self.position(range.start.line, range.start.column)?,
            end: self.position(range.end.line, range.end.column)?,
        })
    }
}

/// Internal parse result before source-based conversion.
#[derive(Debug, Clone)]
pub(crate) struct ParsedDiagnostic {
    pub severity: Severity,
    pub message: String,
    pub code: String,
    pub location: Option<RawLocation>,
    pub ranges: Vec<RawRange>,
    pub symbol_path: Option<String>,
    pub raw: serde_json::Value,
    pub parse_note: Option<String>,
    /// How many raw findings this row represents. `1` everywhere except the
    /// streaming pass parser, which de-duplicates repeated findings and counts
    /// them here (a warning about an instantiated module repeats per instance).
    pub repeat: usize,
}

#[derive(Deserialize)]
struct WireDiagnostic {
    severity: String,
    message: String,
    code: String,
    #[serde(default)]
    location: Option<String>,
    #[serde(default)]
    ranges: Vec<WireRange>,
    #[serde(default, rename = "symbolPath")]
    symbol_path: Option<String>,
}

#[derive(Deserialize)]
struct WireRange {
    start: WireEnd,
    end: WireEnd,
}

#[derive(Deserialize)]
struct WireEnd {
    file: String,
    line: u32,
    column: u32,
}

/// Parse slang's `--diag-json` array.
///
/// # Errors
/// Returns a [`ParseError`] for invalid JSON, a non-array top level, or an
/// element missing a required field (`severity`, `message`, `code`). Missing
/// `location`/`ranges` is **not** an error; those diagnostics stay usable.
#[cfg(test)]
pub(crate) fn parse_diagnostics(json: &str) -> Result<Vec<ParsedDiagnostic>, ParseError> {
    let value: serde_json::Value =
        serde_json::from_str(json).map_err(|error| ParseError(format!("{error}")))?;
    let array = value
        .as_array()
        .ok_or_else(|| ParseError("diagnostics JSON must be an array".to_string()))?;
    array
        .iter()
        .enumerate()
        .map(|(index, element)| parse_wire_element(element.clone(), index))
        .collect()
}

/// One diagnostics document read from a READER.
#[derive(Debug, Clone)]
pub(crate) struct DiagnosticStream {
    /// The rows RETAINED, one per distinct finding, each with its `repeat`.
    pub diagnostics: Vec<ParsedDiagnostic>,
    /// Raw elements present in the document (retained + dropped).
    pub total: usize,
    /// Raw elements that are not represented in `diagnostics` because the
    /// count bound was reached for a new distinct finding.
    pub dropped: usize,
}

/// Stream slang's `--diag-json` array from a reader.
///
/// The document is NEVER held in memory. Two policies make a real project's
/// warning flood harmless:
///
/// * **de-duplication**: findings with the same (file, line, column, code,
///   message) are one row with `repeat` counting how many times they occurred.
///   A warning about an instantiated module's connection otherwise repeats once
///   per elaborated instance. Two different findings at the same location are
///   NEVER merged: the code and the message are part of the key.
/// * **count bound**: at most `max_unique` distinct findings are retained. Past
///   it, the walk keeps reading and counting so the caller can say how many
///   findings it did not keep — the pass never fails because of volume.
///
/// # Errors
/// Invalid JSON, a non-array top level, or a retained element missing a
/// required field. Elements read only for counting are still JSON-validated.
pub(crate) fn parse_diagnostics_reader<R: Read>(
    reader: R,
    max_unique: usize,
) -> Result<DiagnosticStream, ParseError> {
    let mut stream = DiagnosticStream {
        diagnostics: Vec::new(),
        total: 0,
        dropped: 0,
    };
    let mut deserializer = serde_json::Deserializer::from_reader(reader);
    StreamSeed {
        stream: &mut stream,
        max_unique,
    }
    .deserialize(&mut deserializer)
    .map_err(|error| ParseError(format!("{error}")))?;
    Ok(stream)
}

struct StreamSeed<'a> {
    stream: &'a mut DiagnosticStream,
    max_unique: usize,
}

impl<'de> DeserializeSeed<'de> for StreamSeed<'_> {
    type Value = ();

    fn deserialize<D>(self, deserializer: D) -> Result<(), D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_seq(StreamVisitor {
            stream: self.stream,
            max_unique: self.max_unique,
        })
    }
}

struct StreamVisitor<'a> {
    stream: &'a mut DiagnosticStream,
    max_unique: usize,
}

impl<'de> Visitor<'de> for StreamVisitor<'_> {
    type Value = ();

    fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("an array of slang diagnostics")
    }

    fn visit_seq<A>(self, mut seq: A) -> Result<(), A::Error>
    where
        A: SeqAccess<'de>,
    {
        use std::collections::HashMap;
        // The de-duplication identity of a retained row. The file is part of the
        // key so a diagnostic in an include does not merge with one in the
        // linted file at the same line/column; code+message keep two DIFFERENT
        // findings at one location apart.
        let mut index: HashMap<String, usize> = HashMap::new();
        while let Some(element) = seq.next_element::<serde_json::Value>()? {
            let position = self.stream.total;
            let parsed = parse_wire_element(element, position)
                .map_err(|error| <A::Error as serde::de::Error>::custom(error.to_string()))?;
            self.stream.total += 1;
            let key = dedupe_key(&parsed);
            if let Some(&retained) = index.get(&key) {
                self.stream.diagnostics[retained].repeat += 1;
            } else if self.stream.diagnostics.len() < self.max_unique {
                index.insert(key, self.stream.diagnostics.len());
                self.stream.diagnostics.push(parsed);
            } else {
                self.stream.dropped += 1;
            }
        }
        Ok(())
    }
}

/// The per-finding identity used by the pass de-duplication.
fn dedupe_key(parsed: &ParsedDiagnostic) -> String {
    let (file, line, column) = match &parsed.location {
        Some(location) => (location.file.clone(), location.line, location.column),
        None => ("".to_string(), 0, 0),
    };
    format!(
        "{file}\u{1f}{line}\u{1f}{column}\u{1f}{}\u{1f}{}",
        parsed.code, parsed.message
    )
}

/// Parse one wire element. Shared by the whole-string and the streaming path so
/// both produce exactly the same rows.
fn parse_wire_element(
    value: serde_json::Value,
    index: usize,
) -> Result<ParsedDiagnostic, ParseError> {
    let wire: WireDiagnostic = serde_json::from_value(value.clone())
        .map_err(|error| ParseError(format!("element {index}: {error}")))?;
    let (location, parse_note) = match wire.location.as_deref() {
        Some(text) => match parse_location_string(text) {
            Some(location) => (Some(location), None),
            None => (None, Some(format!("unparsable location string {text:?}"))),
        },
        // No location string: fall back to the first range, as some
        // diagnostics only carry ranges.
        None => (
            wire.ranges.first().map(|range| RawLocation {
                file: range.start.file.clone(),
                line: range.start.line,
                column: range.start.column,
            }),
            None,
        ),
    };
    Ok(ParsedDiagnostic {
        severity: Severity::from_wire(&wire.severity),
        message: wire.message,
        code: wire.code,
        location,
        ranges: wire
            .ranges
            .into_iter()
            .map(|range| RawRange {
                file: range.start.file.clone(),
                start: RawRangePosition {
                    line: range.start.line,
                    column: range.start.column,
                },
                end: RawRangePosition {
                    line: range.end.line,
                    column: range.end.column,
                },
            })
            .collect(),
        symbol_path: wire.symbol_path,
        raw: value,
        parse_note,
        repeat: 1,
    })
}

/// Parse the `"file:line:column"` location string (file names may contain
/// colons, so the split is taken from the right).
fn parse_location_string(text: &str) -> Option<RawLocation> {
    let mut parts = text.rsplitn(3, ':');
    let column = parts.next()?.parse::<u32>().ok()?;
    let line = parts.next()?.parse::<u32>().ok()?;
    let file = parts.next()?.to_string();
    Some(RawLocation { file, line, column })
}

/// Convert a parsed diagnostic to the public model using the linted source.
/// Convert a parsed diagnostic and make its reported paths absolute.
///
/// `tool_dir` is the directory the lint child actually ran in (the lint runner
/// sets it to the linted file's directory). slang reports `--diag-json` paths
/// relative to it (verified: an absolute argument comes back as
/// `../…/warn.sv` from a foreign cwd), so without this the diagnostics' file
/// identity would never match the editor's absolute paths - verdicts would key
/// on a path that does not exist. `None` keeps the reported spelling (the
/// unit-test fixtures, whose paths are already absolute or intentionally raw).
pub(crate) fn convert_diagnostic(
    mut parsed: ParsedDiagnostic,
    source: &SourceLines,
    tool_dir: Option<&std::path::Path>,
) -> Diagnostic {
    if let Some(tool_dir) = tool_dir {
        if let Some(location) = parsed.location.as_mut() {
            location.file = crate::tool_paths::absolute_path_str(&location.file, tool_dir);
        }
        for range in &mut parsed.ranges {
            range.file = crate::tool_paths::absolute_path_str(&range.file, tool_dir);
        }
    }
    let primary = parsed.ranges.first().cloned().or_else(|| {
        parsed.location.as_ref().map(|location| RawRange {
            file: location.file.clone(),
            start: RawRangePosition {
                line: location.line,
                column: location.column,
            },
            end: RawRangePosition {
                line: location.line,
                column: location.column,
            },
        })
    });
    let (editor_range, conversion_note) = match &primary {
        Some(range) => match source.range(range) {
            Ok(range) => (Some(range), None),
            Err(error) => (None, Some(error.to_string())),
        },
        None => (
            None,
            Some("the diagnostic has no location (e.g. TooManyErrors)".to_string()),
        ),
    };
    Diagnostic {
        severity: parsed.severity,
        message: parsed.message,
        code: parsed.code,
        location: parsed.location,
        ranges: parsed.ranges,
        symbol_path: parsed.symbol_path,
        raw: parsed.raw,
        editor_range,
        conversion_note,
        parse_note: parsed.parse_note,
        repeat: parsed.repeat,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    const RECORDED_UNICODE_BYTE: &str =
        include_str!("../tests/fixtures/recorded/unicode_byte.json");
    const RECORDED_UNDECLARED_BYTE: &str =
        include_str!("../tests/fixtures/recorded/undeclared_byte.json");
    const RECORDED_CLEAN: &str = include_str!("../tests/fixtures/recorded/clean.json");
    const RECORDED_IMPLICIT_NET: &str =
        include_str!("../tests/fixtures/recorded/implicit_net_byte.json");

    fn fixture_bytes(name: &str) -> Vec<u8> {
        let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("tests/fixtures")
            .join(name);
        std::fs::read(&path)
            .unwrap_or_else(|error| panic!("reading {} failed: {error}", path.display()))
    }

    #[test]
    fn parses_recorded_unicode_diagnostics_without_losing_fields() {
        let parsed = parse_diagnostics(RECORDED_UNICODE_BYTE)
            .unwrap_or_else(|error| panic!("parse failed: {error}"));
        assert_eq!(parsed.len(), 2);
        assert_eq!(parsed[0].code, "UndeclaredIdentifier");
        assert_eq!(parsed[0].severity, Severity::Error);
        let location = parsed[0]
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("location missing"));
        assert_eq!((location.line, location.column), (4, 18));
        assert_eq!(parsed[0].symbol_path.as_deref(), Some("$unit"));
        assert_eq!(parsed[0].ranges.len(), 1);
        assert_eq!(parsed[0].ranges[0].end.column, 31);
        let second = parsed[1]
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("location missing"));
        assert_eq!((second.line, second.column), (5, 16));
        // The original object is preserved verbatim.
        assert_eq!(
            parsed[0].raw.get("code").and_then(|v| v.as_str()),
            Some("UndeclaredIdentifier")
        );
    }

    #[test]
    fn parses_recorded_undeclared_diagnostic() {
        let parsed = parse_diagnostics(RECORDED_UNDECLARED_BYTE)
            .unwrap_or_else(|error| panic!("parse failed: {error}"));
        assert_eq!(parsed.len(), 1);
        let location = parsed[0]
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("location missing"));
        assert_eq!(
            location.file,
            "crates/vd-deeplint/tests/fixtures/undeclared.sv"
        );
        assert_eq!((location.line, location.column), (3, 5));
        assert_eq!(parsed[0].ranges[0].start.line, 3);
        assert_eq!(parsed[0].ranges[0].end.column, 20);
    }

    #[test]
    fn parses_the_empty_clean_report() {
        let parsed = parse_diagnostics(RECORDED_CLEAN)
            .unwrap_or_else(|error| panic!("parse failed: {error}"));
        assert!(parsed.is_empty());
    }

    #[test]
    fn implicit_net_has_a_location_but_no_ranges_and_keeps_option_name() {
        let parsed = parse_diagnostics(RECORDED_IMPLICIT_NET)
            .unwrap_or_else(|error| panic!("parse failed: {error}"));
        assert_eq!(parsed.len(), 1);
        let diagnostic = &parsed[0];
        assert_eq!(diagnostic.code, "ImplicitNet");
        assert_eq!(diagnostic.severity, Severity::Warning);
        assert!(diagnostic.ranges.is_empty());
        let location = diagnostic
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("ImplicitNet should have a location"));
        assert_eq!((location.line, location.column), (2, 10));
        assert_eq!(
            diagnostic
                .raw
                .get("optionName")
                .and_then(|value| value.as_str()),
            Some("implicit-net"),
            "unknown keys must survive in raw"
        );
    }

    #[test]
    fn a_diagnostic_without_location_or_ranges_is_kept() {
        // Shape measured for slang's `TooManyErrors` (code/message/severity only).
        let json = r#"[{"severity":"error","message":"too many errors","code":"TooManyErrors"}]"#;
        let parsed = parse_diagnostics(json).unwrap_or_else(|error| panic!("parse: {error}"));
        assert_eq!(parsed.len(), 1);
        assert!(parsed[0].location.is_none());
        assert!(parsed[0].ranges.is_empty());
        let source = SourceLines::from_bytes(b"module m;\nendmodule\n")
            .unwrap_or_else(|error| panic!("source: {error}"));
        let diagnostic = convert_diagnostic(
            parsed
                .into_iter()
                .next()
                .unwrap_or_else(|| panic!("missing")),
            &source,
            None,
        );
        assert_eq!(diagnostic.code, "TooManyErrors");
        assert!(diagnostic.editor_range.is_none());
        assert!(diagnostic
            .conversion_note
            .as_deref()
            .unwrap_or_default()
            .contains("no location"));
    }

    #[test]
    fn a_diagnostic_without_ranges_converts_the_location_as_a_point() {
        let json = r#"[{"severity":"warning","message":"implicit net 'q' created","code":"ImplicitNet","location":"x.sv:2:10","symbolPath":"$unit"}]"#;
        let parsed = parse_diagnostics(json).unwrap_or_else(|error| panic!("parse: {error}"));
        let source = SourceLines::from_bytes(b"module m;\n  assign q = 1;\nendmodule\n")
            .unwrap_or_else(|error| panic!("source: {error}"));
        let diagnostic = convert_diagnostic(
            parsed
                .into_iter()
                .next()
                .unwrap_or_else(|| panic!("missing")),
            &source,
            None,
        );
        assert!(diagnostic.ranges.is_empty());
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

    #[test]
    fn malformed_json_is_rejected() {
        assert!(parse_diagnostics("{not json").is_err());
        assert!(
            parse_diagnostics("{}").is_err(),
            "top level must be an array"
        );
        assert!(
            parse_diagnostics("[{\"severity\":\"error\"}]").is_err(),
            "missing required fields"
        );
        assert!(parse_diagnostics("[] trailing").is_err());
    }

    #[test]
    fn an_unparsable_location_string_is_a_note_not_a_failure() {
        let json = r#"[{"severity":"error","message":"m","code":"C","location":"not-a-location"}]"#;
        let parsed = parse_diagnostics(json).unwrap_or_else(|error| panic!("parse: {error}"));
        assert!(parsed[0].location.is_none());
        assert!(parsed[0]
            .parse_note
            .as_deref()
            .unwrap_or_default()
            .contains("unparsable"));
        assert!(parsed[0].raw.get("location").is_some());
    }

    #[test]
    fn severity_mapping_includes_unknown() {
        assert_eq!(Severity::from_wire("error"), Severity::Error);
        assert_eq!(Severity::from_wire("warning"), Severity::Warning);
        assert_eq!(Severity::from_wire("note"), Severity::Note);
        assert_eq!(Severity::from_wire("ignored"), Severity::Ignored);
        assert_eq!(
            Severity::from_wire("something-new"),
            Severity::Unknown("something-new".to_string())
        );
    }

    #[test]
    fn location_falls_back_to_first_range() {
        let json = r#"[{"severity":"error","message":"m","code":"C",
            "ranges":[{"start":{"file":"a.sv","line":2,"column":3},
                       "end":{"file":"a.sv","line":2,"column":4}}]}]"#;
        let parsed = parse_diagnostics(json).unwrap_or_else(|error| panic!("parse: {error}"));
        let location = parsed[0]
            .location
            .as_ref()
            .unwrap_or_else(|| panic!("location missing"));
        assert_eq!(location.file, "a.sv");
        assert_eq!((location.line, location.column), (2, 3));
    }

    #[test]
    fn location_string_with_colons_in_the_path_is_parsed_from_the_right() {
        let location = parse_location_string("/home/a:b/c.sv:12:5")
            .unwrap_or_else(|| panic!("location should parse"));
        assert_eq!(location.file, "/home/a:b/c.sv");
        assert_eq!((location.line, location.column), (12, 5));
    }

    #[test]
    fn byte_columns_convert_to_utf16_on_cjk_and_emoji() {
        let source = SourceLines::from_bytes(&fixture_bytes("unicode_columns.sv"))
            .unwrap_or_else(|error| panic!("source failed: {error}"));
        assert_eq!(
            source
                .position(4, 18)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 3,
                character: 13
            }
        );
        assert_eq!(
            source
                .position(4, 31)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 3,
                character: 26
            }
        );
        assert_eq!(
            source
                .position(5, 16)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 4,
                character: 13
            }
        );
        assert_eq!(
            source
                .position(5, 31)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 4,
                character: 28
            }
        );
    }

    /// The calibration fixture: every line mixes a different display-width
    /// hazard (tab, CJK, emoji, combining mark). Expected values were computed
    /// independently from the fixture bytes (Python), not from slang's display
    /// columns: `(line_1based, byte_start, byte_end, display_start, editor_line,
    /// editor_start_utf16, editor_end_utf16, identifier)`.
    type CalibrationRow = (u32, u32, u32, u32, u32, u32, u32, &'static str);
    const CALIBRATION: &[CalibrationRow] = &[
        (3, 17, 33, 17, 2, 16, 32, "undeclared_plain"),
        (4, 18, 32, 21, 3, 17, 31, "undeclared_tab"),
        (5, 15, 30, 14, 4, 12, 27, "undeclared_cjk1"),
        (6, 18, 33, 16, 5, 13, 28, "undeclared_cjk2"),
        (7, 21, 36, 18, 6, 14, 29, "undeclared_cjk3"),
        (8, 16, 29, 13, 7, 13, 26, "undeclared_e1"),
        (9, 20, 33, 14, 8, 15, 28, "undeclared_e2"),
        (10, 24, 37, 15, 9, 17, 30, "undeclared_e3"),
        (11, 19, 32, 15, 10, 14, 27, "undeclared_ce"),
        (12, 19, 34, 19, 11, 18, 33, "undeclared_comb"),
    ];

    #[test]
    fn calibration_table_converts_byte_columns_to_utf16() {
        let source = SourceLines::from_bytes(&fixture_bytes("display_calibration.sv"))
            .unwrap_or_else(|error| panic!("source failed: {error}"));
        for &(line, byte_start, byte_end, _display_start, editor_line, start, end, ident) in
            CALIBRATION
        {
            let start_position = source
                .position(line, byte_start)
                .unwrap_or_else(|error| panic!("{ident} start: {error}"));
            assert_eq!(
                start_position,
                EditorPosition {
                    line: editor_line,
                    character: start
                },
                "{ident} start"
            );
            let end_position = source
                .position(line, byte_end)
                .unwrap_or_else(|error| panic!("{ident} end: {error}"));
            assert_eq!(
                end_position,
                EditorPosition {
                    line: editor_line,
                    character: end
                },
                "{ident} end"
            );
        }
    }

    #[test]
    fn byte_columns_convert_inside_a_tab_line() {
        let source = SourceLines::from_bytes(
            b"module t;\n  initial begin\n\t/* x */ undef = 1;\n  end\nendmodule\n",
        )
        .unwrap_or_else(|error| panic!("source failed: {error}"));
        assert_eq!(
            source
                .position(3, 10)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 2,
                character: 9
            }
        );
    }

    #[test]
    fn conversion_rejects_bad_lines_columns_and_split_characters() {
        let source = SourceLines::from_bytes(&fixture_bytes("unicode_columns.sv"))
            .unwrap_or_else(|error| panic!("source failed: {error}"));
        assert!(matches!(
            source.position(0, 1),
            Err(ConvertError::LineOutOfRange { line: 0 })
        ));
        assert!(matches!(
            source.position(999, 1),
            Err(ConvertError::LineOutOfRange { line: 999 })
        ));
        assert!(matches!(
            source.position(1, 500),
            Err(ConvertError::ColumnOutOfRange { .. })
        ));
        assert!(matches!(
            source.position(4, 9),
            Err(ConvertError::NotCharBoundary { .. })
        ));
        assert!(matches!(
            source.position(5, 10),
            Err(ConvertError::NotCharBoundary { .. })
        ));
    }

    #[test]
    fn invalid_utf8_source_is_rejected() {
        assert_eq!(
            SourceLines::from_bytes(&[0xff, 0xfe]).err(),
            Some(ConvertError::InvalidUtf8)
        );
    }

    #[test]
    fn crlf_trailing_carriage_return_is_not_part_of_the_line() {
        let source = SourceLines::from_bytes(b"a\r\nbc\r\n")
            .unwrap_or_else(|error| panic!("source failed: {error}"));
        assert_eq!(source.line_count(), 3);
        assert_eq!(
            source
                .position(2, 3)
                .unwrap_or_else(|error| panic!("{error}")),
            EditorPosition {
                line: 1,
                character: 2
            }
        );
    }

    fn one_finding(message: &str, code: &str) -> String {
        format!(
            "{{\"severity\":\"warning\",\"message\":\"{message}\",\"code\":\"{code}\",\"location\":\"top.sv:10:5\"}}"
        )
    }

    #[test]
    fn the_streaming_parser_matches_the_whole_string_parse() {
        let json = format!(
            "[{},{}]",
            one_finding("first", "W1"),
            one_finding("second", "W2")
        );
        let whole = parse_diagnostics(&json).unwrap_or_else(|error| panic!("parse: {error}"));
        let streamed = parse_diagnostics_reader(json.as_bytes(), 500)
            .unwrap_or_else(|error| panic!("stream: {error}"));
        assert_eq!(whole.len(), streamed.diagnostics.len());
        for (a, b) in whole.iter().zip(streamed.diagnostics.iter()) {
            assert_eq!(a.message, b.message);
            assert_eq!(a.code, b.code);
            assert_eq!(a.location, b.location);
            assert_eq!(b.repeat, 1);
        }
        assert_eq!(streamed.total, 2);
        assert_eq!(streamed.dropped, 0);
    }

    #[test]
    fn repeated_findings_collapse_with_a_repeat_count() {
        let json = format!(
            "[{},{},{},{}]",
            one_finding("repeated", "Wrep"),
            one_finding("repeated", "Wrep"),
            one_finding("repeated", "Wrep"),
            one_finding("different", "Wother")
        );
        let streamed = parse_diagnostics_reader(json.as_bytes(), 500)
            .unwrap_or_else(|error| panic!("stream: {error}"));
        assert_eq!(streamed.total, 4, "every raw element is counted");
        assert_eq!(streamed.diagnostics.len(), 2, "two distinct findings");
        assert_eq!(streamed.diagnostics[0].repeat, 3);
        assert_eq!(streamed.diagnostics[1].repeat, 1);
        assert_eq!(streamed.dropped, 0, "collapsing is not dropping");
    }

    #[test]
    fn different_findings_at_one_location_stay_separate() {
        // Same file/line/column, different rule code and message: two findings,
        // never merged (the code and the message are part of the key).
        let a = "{\"severity\":\"warning\",\"message\":\"alpha\",\"code\":\"WA\",\"location\":\"top.sv:10:5\"}";
        let b = "{\"severity\":\"warning\",\"message\":\"beta\",\"code\":\"WB\",\"location\":\"top.sv:10:5\"}";
        let json = format!("[{a},{b}]");
        let streamed = parse_diagnostics_reader(json.as_bytes(), 500)
            .unwrap_or_else(|error| panic!("stream: {error}"));
        assert_eq!(
            streamed.diagnostics.len(),
            2,
            "same location is not the same finding"
        );
        assert!(streamed.diagnostics.iter().all(|row| row.repeat == 1));
    }

    #[test]
    fn the_count_bound_keeps_the_head_and_counts_the_tail() {
        let elements: Vec<String> = (0..5)
            .map(|index| one_finding(&format!("finding {index}"), &format!("W{index}")))
            .collect();
        let json = format!("[{}]", elements.join(","));
        let streamed = parse_diagnostics_reader(json.as_bytes(), 3)
            .unwrap_or_else(|error| panic!("stream: {error}"));
        assert_eq!(
            streamed.diagnostics.len(),
            3,
            "retention is bounded by count"
        );
        assert_eq!(streamed.total, 5, "the tail is counted");
        assert_eq!(streamed.dropped, 2);
    }

    #[test]
    fn a_non_array_diagnostics_document_is_a_typed_error() {
        assert!(parse_diagnostics_reader("{}".as_bytes(), 10).is_err());
        assert!(parse_diagnostics_reader("{not json".as_bytes(), 10).is_err());
    }
}
