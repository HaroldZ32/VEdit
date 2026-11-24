//! LINT waivers for both engines (batch 20261110).
//!
//! The two engines scope a waiver in completely different ways, so they get
//! different files and different writers:
//!
//! * **verible** reads `--waiver_files=` — a line-oriented file of
//!   `waive --rule=<rule> --line=<N[:M]> --location="<regex>"` commands. The
//!   location is a RE2 regex matched against the file path the linter reports, so
//!   an append derives an anchored file-name regex (`(^|/)<name>$`) unless the
//!   caller supplies one.
//! * **slang** reads `--waiver-file` — TOML with `[[waivers]]` blocks whose only
//!   allowed keys are `file`, `hier`, `diagnostic` and `regex`, exactly one scope
//!   key (`file` XOR `hier`), and a `diagnostic` that is slang's **warning option
//!   name** (`unused-variable`), not the JSON diagnostic code. slang refuses an
//!   unknown key at load time (measured on slang 12.1.0: `unknown key 'bogus'`,
//!   `waiver entry must have either 'file' or 'hier' as scope`), so an append
//!   refuses to touch a file slang itself would refuse to load and says which line
//!   is wrong.
//!
//! Everything here is real: the three operations below are the ones the
//! diagnostics panel and the settings page call, and they run on a worker thread
//! like every other job. A write is atomic (a sibling temporary file renamed over
//! the target), so an interrupted write can never truncate the user's waivers.
//!
//! # What this module does NOT do
//!
//! * verible's **batch** generation is the tool's own
//!   `--autofix=generate-waiver --autofix_output_file=<file>`; that argv is the
//!   lint engine's, not this module's (see [`crate::deeplint`]).
//! * slang has no generator, so [`generate_from_rows`] writes the
//!   `(file, optionName)` rules itself — and a diagnostic WITHOUT an option name
//!   (typically an error) is counted, explained and skipped, never widened into a
//!   whole-scope waiver (AC-5.8).

use crate::capi::{
    c_str, c_string, c_string_list, free_string, free_string_list, VdDiagnostics, VdError,
    VdWaiverEntry, VdWaiverOptions, VdWaiverResult, VD_ERR_INVALID_ARGUMENT, VD_ERR_JOB_UNKNOWN,
    VD_ERR_NOT_FOUND, VD_ERR_SERVICE, VD_STATUS_FAILED, VD_STATUS_PENDING, VD_STATUS_READY,
    VD_WAIVER_FORMAT_SLANG, VD_WAIVER_FORMAT_VERIBLE,
};
use crate::jobgate::{Registry, Taken, LANE_WAIVER};
use crate::service::{service_state, unknown_job, VdService, POLL_MISUSE};
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::ptr;

/// The settings key verible's waiver list lives under (one path per line).
const VERIBLE_WAIVER_KEY: &str = "lint.verible.waiver_files";
/// The settings key slang's single waiver file lives under.
const SLANG_WAIVER_KEY: &str = "lint.slang.waiver_file";

/// Every waiver job of this process.
static JOBS: Registry<WaiverResultData> = Registry::new();

/// The outcome of one waiver operation.
pub(crate) struct WaiverResultData {
    written: bool,
    deduped: bool,
    path: Option<String>,
    content: String,
    entries: usize,
    skipped: usize,
    warnings: Vec<String>,
}

impl WaiverResultData {
    /// Convert into the owned C representation.
    ///
    /// # Safety
    ///
    /// The returned struct must be released with [`vd_waiver_result_free`].
    pub(crate) unsafe fn into_c(self) -> VdWaiverResult {
        VdWaiverResult {
            written: u8::from(self.written),
            deduped: u8::from(self.deduped),
            path: self.path.as_deref().map_or(ptr::null_mut(), c_string),
            content: c_string(&self.content),
            entries: self.entries,
            skipped: self.skipped,
            warnings: unsafe { c_string_list(&self.warnings) },
        }
    }
}

/// The validated option half of a request.
#[derive(Clone, Copy)]
struct WaiverOptions {
    dedupe: bool,
    create_parent_dirs: bool,
}

impl Default for WaiverOptions {
    fn default() -> Self {
        Self {
            dedupe: true,
            create_parent_dirs: false,
        }
    }
}

/// One waiver to append, in owned form: safe to move to the worker thread (the C
/// struct is only borrowed for the duration of the start call).
#[derive(Clone)]
struct EntryRequest {
    /// The path as the waiver file spells it.
    file_text: String,
    /// 1-based first line (0 = file-wide verible waiver).
    line: u32,
    /// 1-based last line of the range (0 or `line` = a single line).
    line_end: u32,
    /// The verible rule name (empty for a slang entry).
    rule: String,
    /// The slang warning option name (empty for a verible entry).
    option_name: String,
    /// The explicit verible `--location` (empty derives one from `file_text`).
    location: String,
}

/// One diagnostic, reduced to what a waiver needs (owned, for the worker).
struct WaiverDiag {
    file: Option<String>,
    line: u32,
    end_line: u32,
    code: String,
    option_name: Option<String>,
}

// --- request reading ---------------------------------------------------------

/// Read and validate [`VdWaiverOptions`] (null selects every default).
///
/// # Safety
///
/// `options` must be null or readable for the duration of the call.
unsafe fn read_options(options: *const VdWaiverOptions) -> WaiverOptions {
    if options.is_null() {
        return WaiverOptions::default();
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let options = unsafe { *options };
    WaiverOptions {
        dedupe: options.dedupe != 0,
        create_parent_dirs: options.create_parent_dirs != 0,
    }
}

/// Validate a path argument: non-empty, no control characters.
///
/// # Safety
///
/// `text` must be null or a valid NUL-terminated string.
unsafe fn read_path(text: *const c_char, what: &str) -> Result<PathBuf, String> {
    // SAFETY: the caller contract is stated above.
    let Some(text) = (unsafe { c_str(text) }) else {
        return Err(format!("{what} must not be null"));
    };
    if text.is_empty() {
        return Err(format!("{what} must be a non-empty path"));
    }
    if text.chars().any(char::is_control) {
        return Err(format!("{what} must not contain control characters"));
    }
    Ok(PathBuf::from(text))
}

/// Copy and validate one [`VdWaiverEntry`] for the worker thread.
///
/// # Safety
///
/// `entry` must be null or readable for the duration of the call.
unsafe fn read_entry(entry: *const VdWaiverEntry, format: i32) -> Result<EntryRequest, String> {
    if entry.is_null() {
        return Err("entry must not be null".to_string());
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let entry = unsafe { *entry };
    // SAFETY: every pointer below is null or a valid NUL-terminated string.
    let file = unsafe { read_path(entry.file, "entry.file") }?;
    let file_text = file.to_string_lossy().to_string();
    // SAFETY: same contract.
    let raw_rule = unsafe { c_str(entry.rule) }.unwrap_or_default();
    // SAFETY: same contract.
    let raw_option = unsafe { c_str(entry.option_name) }.unwrap_or_default();
    // SAFETY: same contract.
    let raw_location = unsafe { c_str(entry.location) }.unwrap_or_default();
    match format {
        VD_WAIVER_FORMAT_VERIBLE => {
            if !is_rule_name(raw_rule) {
                return Err(format!(
                    "entry.rule `{raw_rule}` is not a verible rule name (letters, digits, `_`, \
                     `-`)"
                ));
            }
            if !raw_location.is_empty()
                && (raw_location.contains('"') || raw_location.chars().any(char::is_control))
            {
                return Err(
                    "entry.location must be a regex without `\"` or control characters".to_string(),
                );
            }
            Ok(EntryRequest {
                file_text,
                line: entry.line,
                line_end: entry.line_end,
                rule: raw_rule.to_string(),
                option_name: String::new(),
                location: raw_location.to_string(),
            })
        }
        VD_WAIVER_FORMAT_SLANG => {
            if !is_rule_name(raw_option) {
                return Err(
                    "this diagnostic has no option name (an error usually has none), so slang can \
                     only waive its whole scope — that would waive every diagnostic of the file, \
                     and this action refuses to widen it silently"
                        .to_string(),
                );
            }
            if entry.line != 0 || entry.line_end != 0 {
                return Err(
                    "slang waivers have no line scope: pass line = 0 and line_end = 0 (slang \
                     scopes by file, hierarchy, diagnostic or regex)"
                        .to_string(),
                );
            }
            Ok(EntryRequest {
                file_text,
                line: 0,
                line_end: 0,
                rule: String::new(),
                option_name: raw_option.to_string(),
                location: String::new(),
            })
        }
        other => Err(format!(
            "format must be 0 (verible) or 1 (slang), not {other}"
        )),
    }
}

/// Copy the diagnostics of a generate request.
///
/// # Safety
///
/// `diagnostics` must be null or point to a struct filled by a lint poll and not
/// freed yet.
unsafe fn read_diagnostics(diagnostics: *const VdDiagnostics) -> Result<Vec<WaiverDiag>, String> {
    if diagnostics.is_null() {
        return Err("diagnostics must not be null: generate needs the rows to waive".to_string());
    }
    // SAFETY: the caller contract guarantees a readable struct.
    let diagnostics = unsafe { *diagnostics };
    if diagnostics.count == 0 {
        return Ok(Vec::new());
    }
    if diagnostics.items.is_null() {
        return Err("diagnostics.items must not be null when count > 0".to_string());
    }
    // SAFETY: `items` points to `count` diagnostics per the contract.
    let items = unsafe { std::slice::from_raw_parts(diagnostics.items, diagnostics.count) };
    let mut rows = Vec::with_capacity(items.len());
    for item in items {
        // SAFETY: every pointer below is null or a valid NUL-terminated string.
        let file = unsafe { c_str(item.file) }
            .filter(|text| !text.is_empty())
            .map(str::to_string);
        let code = unsafe { c_str(item.code) }.unwrap_or_default().to_string();
        let option_name = unsafe { c_str(item.option_name) }
            .filter(|text| !text.is_empty())
            .map(str::to_string);
        rows.push(WaiverDiag {
            file,
            line: item.line,
            end_line: item.end_line,
            code,
            option_name,
        });
    }
    Ok(rows)
}

// --- waiver text -------------------------------------------------------------

/// Whether `text` is a rule / option name (`[A-Za-z0-9_-]+`).
fn is_rule_name(text: &str) -> bool {
    !text.is_empty()
        && text.chars().all(|character| {
            character.is_ascii_alphanumeric() || character == '_' || character == '-'
        })
}

/// One verible waiver command.
fn verible_line(rule: &str, line: u32, line_end: u32, location: &str) -> String {
    let mut text = format!("waive --rule={rule}");
    match (line, line_end) {
        (0, _) => {}
        (start, end) if end > start => text.push_str(&format!(" --line={start}:{end}")),
        (start, _) => text.push_str(&format!(" --line={start}")),
    }
    text.push_str(&format!(" --location=\"{location}\""));
    text
}

/// The anchored file-name regex verible matches against the reported path.
fn default_location(file: &str) -> String {
    let name = file.rsplit(['/', '\\']).next().unwrap_or(file);
    format!("(^|/){}$", regex_escape(name))
}

/// Escape the RE2 metacharacters a file name may plausibly contain.
fn regex_escape(text: &str) -> String {
    let mut escaped = String::with_capacity(text.len());
    for character in text.chars() {
        if matches!(
            character,
            '.' | '+' | '*' | '?' | '(' | ')' | '|' | '[' | ']' | '{' | '}' | '^' | '$' | '\\'
        ) {
            escaped.push('\\');
        }
        escaped.push(character);
    }
    escaped
}

/// The scope a slang waiver is written with.
enum SlangScope {
    File(String),
    /// A hierarchy-scoped waiver. The value is validated when the file is parsed
    /// but not kept: this module only needs to know that the entry is NOT
    /// file-scoped, because that is what decides whether a written entry is
    /// redundant.
    Hier,
}

/// One parsed `[[waivers]]` block of an existing slang waiver file.
struct SlangWaiver {
    scope: SlangScope,
    diagnostic: Option<String>,
}

impl SlangWaiver {
    /// Whether this block already covers exactly `(file, option)`.
    fn covers(&self, file: &str, option: &str) -> bool {
        match &self.scope {
            SlangScope::File(scope) => scope == file && self.diagnostic.as_deref() == Some(option),
            SlangScope::Hier => false,
        }
    }
}

/// The slang `file` scope value this ABI writes: the absolute path when the
/// diagnostic names one, otherwise `**/<path>` (slang matches it against the path
/// it was given).
fn slang_scope(file: &str) -> String {
    let normalised = file.replace('\\', "/");
    if normalised.starts_with('/') {
        normalised
    } else {
        format!("**/{normalised}")
    }
}

/// One slang waiver block.
fn slang_block(scope: &str, option: &str) -> String {
    format!(
        "[[waivers]]\nfile = \"{}\"\ndiagnostic = \"{}\"\n",
        toml_escape(scope),
        toml_escape(option)
    )
}

/// Escape a string for a TOML basic string.
fn toml_escape(text: &str) -> String {
    let mut escaped = String::with_capacity(text.len());
    for character in text.chars() {
        match character {
            '"' => escaped.push_str("\\\""),
            '\\' => escaped.push_str("\\\\"),
            '\n' => escaped.push_str("\\n"),
            '\r' => escaped.push_str("\\r"),
            '\t' => escaped.push_str("\\t"),
            other => escaped.push(other),
        }
    }
    escaped
}

/// Parse and VALIDATE an existing slang waiver file, the way slang reads it: only
/// `[[waivers]]` tables, only `file`/`hier`/`diagnostic`/`regex` keys, exactly one
/// scope key per block.
fn parse_slang_waivers(content: &str) -> Result<Vec<SlangWaiver>, String> {
    let mut waivers: Vec<SlangWaiver> = Vec::new();
    let mut open = false;
    for (index, raw) in content.lines().enumerate() {
        let number = index + 1;
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line == "[[waivers]]" {
            waivers.push(SlangWaiver {
                scope: SlangScope::File(String::new()),
                diagnostic: None,
            });
            open = true;
            continue;
        }
        if line.starts_with('[') {
            return Err(format!(
                "line {number}: only `[[waivers]]` tables are allowed, found `{line}`"
            ));
        }
        if !open {
            return Err(format!(
                "line {number}: `{line}` is outside a `[[waivers]]` table"
            ));
        }
        let Some((key, value)) = line.split_once('=') else {
            return Err(format!(
                "line {number}: expected `key = \"value\"`, found `{line}`"
            ));
        };
        let key = key.trim();
        let shown = value.trim().to_string();
        let Some(value) = parse_toml_string(value.trim()) else {
            return Err(format!(
                "line {number}: `{key}` must be a quoted string, found `{shown}`"
            ));
        };
        let Some(waiver) = waivers.last_mut() else {
            return Err(format!("line {number}: no open `[[waivers]]` table"));
        };
        match key {
            "file" => match &waiver.scope {
                SlangScope::File(scope) if scope.is_empty() => {
                    waiver.scope = SlangScope::File(value);
                }
                _ => {
                    return Err(format!(
                        "line {number}: `file` repeats a scope key (exactly one `file` or `hier` \
                         is allowed per waiver)"
                    ));
                }
            },
            "hier" => match &waiver.scope {
                SlangScope::File(scope) if scope.is_empty() => {
                    waiver.scope = SlangScope::Hier;
                }
                _ => {
                    return Err(format!(
                        "line {number}: `hier` repeats a scope key (exactly one `file` or `hier` \
                         is allowed per waiver)"
                    ));
                }
            },
            "diagnostic" => {
                if waiver.diagnostic.is_some() {
                    return Err(format!("line {number}: `diagnostic` repeats a key"));
                }
                waiver.diagnostic = Some(value);
            }
            "regex" => {
                // `regex` is a legal alternative to `diagnostic`; this module never
                // writes one, but an existing file may carry it.
            }
            other => {
                return Err(format!(
                    "line {number}: unknown key `{other}` — slang refuses a waiver file with an \
                     unknown key"
                ));
            }
        }
    }
    for (index, waiver) in waivers.iter().enumerate() {
        if matches!(&waiver.scope, SlangScope::File(scope) if scope.is_empty()) {
            return Err(format!(
                "waiver {}: no scope — exactly one `file` or `hier` is required",
                index + 1
            ));
        }
    }
    Ok(waivers)
}

/// Parse one TOML basic string (`"…"`), unescaping `\"`, `\\`, `\n`, `\r`, `\t`.
fn parse_toml_string(text: &str) -> Option<String> {
    let rest = text.strip_prefix('"')?;
    let mut value = String::new();
    let mut characters = rest.chars();
    loop {
        match characters.next()? {
            '"' => {
                let tail: String = characters.collect();
                let tail = tail.trim_start();
                return if tail.is_empty() || tail.starts_with('#') {
                    Some(value)
                } else {
                    None
                };
            }
            '\\' => match characters.next()? {
                '"' => value.push('"'),
                '\\' => value.push('\\'),
                'n' => value.push('\n'),
                'r' => value.push('\r'),
                't' => value.push('\t'),
                _ => return None,
            },
            other => value.push(other),
        }
    }
}

// --- file access -------------------------------------------------------------

/// Read a waiver file: a missing file is an empty document (a waiver file is
/// created by its first entry), anything unreadable is an error.
fn read_or_empty(path: &Path) -> Result<String, VdError> {
    match std::fs::read(path) {
        Ok(bytes) => String::from_utf8(bytes).map_err(|_| {
            VdError::new(
                VD_ERR_SERVICE,
                &format!(
                    "{} is not valid UTF-8, so it cannot be edited",
                    path.display()
                ),
            )
        }),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(String::new()),
        Err(error) => Err(VdError::new(
            VD_ERR_NOT_FOUND,
            &format!("cannot read {}: {error}", path.display()),
        )),
    }
}

/// Ensure the target can be created, honouring `create_parent_dirs`.
fn prepare_parent(path: &Path, options: WaiverOptions) -> Result<(), VdError> {
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty());
    let Some(parent) = parent else {
        return Ok(());
    };
    if parent.is_dir() {
        return Ok(());
    }
    if options.create_parent_dirs {
        return std::fs::create_dir_all(parent).map_err(|error| {
            VdError::new(
                VD_ERR_SERVICE,
                &format!("cannot create {}: {error}", parent.display()),
            )
        });
    }
    Err(VdError::new(
        VD_ERR_NOT_FOUND,
        &format!(
            "the directory {} does not exist: create it first, or pass create_parent_dirs = 1",
            parent.display()
        ),
    ))
}

/// Write `content` to `path` atomically: a sibling temporary file is renamed over
/// the target, so an interrupted write can never truncate what the user had.
fn write_atomic(path: &Path, content: &str) -> Result<(), VdError> {
    let parent = match path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        Some(parent) => parent.to_path_buf(),
        None => PathBuf::from("."),
    };
    let name = path
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_else(|| "waiver".to_string());
    let temporary = parent.join(format!(".{name}.tmp-{}", std::process::id()));
    std::fs::write(&temporary, content).map_err(|error| {
        VdError::new(
            VD_ERR_SERVICE,
            &format!("cannot write {}: {error}", temporary.display()),
        )
    })?;
    if let Err(error) = std::fs::rename(&temporary, path) {
        let mut message = format!("cannot replace {}: {error}", path.display());
        if let Err(cleanup) = std::fs::remove_file(&temporary) {
            message.push_str(&format!(
                "; the temporary file {} could not be removed either: {cleanup}",
                temporary.display()
            ));
        }
        return Err(VdError::new(VD_ERR_SERVICE, &message));
    }
    Ok(())
}

/// The document after appending `lines`; `blocks` separates whole TOML blocks with
/// one blank line. `None` when there is nothing to append.
fn append_text(existing: &str, lines: &[String], blocks: bool) -> Option<String> {
    if lines.is_empty() {
        return None;
    }
    let mut content = existing.to_string();
    for line in lines {
        if !content.is_empty() && !content.ends_with('\n') {
            content.push('\n');
        }
        if blocks && !content.is_empty() && !content.ends_with("\n\n") {
            content.push('\n');
        }
        content.push_str(line);
        if !line.ends_with('\n') {
            content.push('\n');
        }
    }
    Some(content)
}

// --- the three operations ----------------------------------------------------

/// Append one entry to the waiver file at `path`.
fn append(
    path: &Path,
    format: i32,
    entry: &EntryRequest,
    options: WaiverOptions,
) -> Result<WaiverResultData, VdError> {
    prepare_parent(path, options)?;
    let existing = read_or_empty(path)?;
    let mut warnings: Vec<String> = Vec::new();
    match format {
        VD_WAIVER_FORMAT_VERIBLE => {
            let location = if entry.location.is_empty() {
                default_location(&entry.file_text)
            } else {
                entry.location.clone()
            };
            let text = verible_line(&entry.rule, entry.line, entry.line_end, &location);
            let entries = existing
                .lines()
                .filter(|line| !line.trim().is_empty())
                .count();
            if options.dedupe && existing.lines().any(|line| line.trim() == text) {
                return Ok(WaiverResultData {
                    written: false,
                    deduped: true,
                    path: Some(path.display().to_string()),
                    content: existing,
                    entries,
                    skipped: 0,
                    warnings,
                });
            }
            if entry.line == 0 {
                warnings.push(
                    "line = 0 writes a file-wide waiver (no `--line`): it silences that rule for \
                     the whole file"
                        .to_string(),
                );
            }
            let content = append_text(&existing, &[text], false)
                .ok_or_else(|| VdError::new(VD_ERR_SERVICE, "nothing to append"))?;
            write_atomic(path, &content)?;
            let entries = content
                .lines()
                .filter(|line| !line.trim().is_empty())
                .count();
            Ok(WaiverResultData {
                written: true,
                deduped: false,
                path: Some(path.display().to_string()),
                content,
                entries,
                skipped: 0,
                warnings,
            })
        }
        VD_WAIVER_FORMAT_SLANG => {
            let waivers = parse_slang_waivers(&existing).map_err(|message| {
                VdError::new(
                    VD_ERR_SERVICE,
                    &format!(
                        "{} is not a waiver file slang would load, so it was left untouched: \
                         {message}",
                        path.display()
                    ),
                )
            })?;
            let scope = slang_scope(&entry.file_text);
            if options.dedupe
                && waivers
                    .iter()
                    .any(|waiver| waiver.covers(&scope, &entry.option_name))
            {
                return Ok(WaiverResultData {
                    written: false,
                    deduped: true,
                    path: Some(path.display().to_string()),
                    content: existing,
                    entries: waivers.len(),
                    skipped: 0,
                    warnings,
                });
            }
            let content = append_text(&existing, &[slang_block(&scope, &entry.option_name)], true)
                .ok_or_else(|| VdError::new(VD_ERR_SERVICE, "nothing to append"))?;
            write_atomic(path, &content)?;
            let entries = parse_slang_waivers(&content).map_or(0, |waivers| waivers.len());
            Ok(WaiverResultData {
                written: true,
                deduped: false,
                path: Some(path.display().to_string()),
                content,
                entries,
                skipped: 0,
                warnings,
            })
        }
        other => Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("format must be 0 (verible) or 1 (slang), not {other}"),
        )),
    }
}

/// Build a waiver document from already-copied rows and write it when a path was
/// given.
fn generate_from_rows(
    path: Option<&Path>,
    format: i32,
    rows: &[WaiverDiag],
    options: WaiverOptions,
) -> Result<WaiverResultData, VdError> {
    if format != VD_WAIVER_FORMAT_VERIBLE && format != VD_WAIVER_FORMAT_SLANG {
        return Err(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            &format!("format must be 0 (verible) or 1 (slang), not {format}"),
        ));
    }
    let existing = match path {
        Some(path) => {
            prepare_parent(path, options)?;
            read_or_empty(path)?
        }
        None => String::new(),
    };
    let mut warnings: Vec<String> = Vec::new();
    let mut skipped = 0usize;
    let mut lines: Vec<String> = Vec::new();
    let mut known: Vec<SlangWaiver> = if format == VD_WAIVER_FORMAT_SLANG {
        parse_slang_waivers(&existing).map_err(|message| {
            VdError::new(
                VD_ERR_SERVICE,
                &format!("the target is not a waiver file slang would load: {message}"),
            )
        })?
    } else {
        Vec::new()
    };
    for row in rows {
        let Some(file) = row.file.as_deref() else {
            skipped += 1;
            warnings.push("a diagnostic without a file cannot be waived".to_string());
            continue;
        };
        match format {
            VD_WAIVER_FORMAT_VERIBLE => {
                let rule = row
                    .option_name
                    .as_deref()
                    .filter(|text| is_rule_name(text))
                    .or_else(|| Some(row.code.as_str()).filter(|text| is_rule_name(text)));
                let Some(rule) = rule else {
                    skipped += 1;
                    warnings.push(format!(
                        "{file}:{} has no usable rule name, so no waiver was written",
                        row.line + 1
                    ));
                    continue;
                };
                lines.push(verible_line(
                    rule,
                    row.line + 1,
                    row.end_line + 1,
                    &default_location(file),
                ));
            }
            _ => {
                let Some(option) = row.option_name.as_deref().filter(|text| is_rule_name(text))
                else {
                    skipped += 1;
                    warnings.push(format!(
                        "{file}:{} has no option name (an error usually has none): only a \
                         whole-scope waiver is possible, and this generate refuses to widen it",
                        row.line + 1
                    ));
                    continue;
                };
                let scope = slang_scope(file);
                if known.iter().any(|waiver| waiver.covers(&scope, option)) {
                    continue;
                }
                lines.push(slang_block(&scope, option));
                known.push(SlangWaiver {
                    scope: SlangScope::File(scope),
                    diagnostic: Some(option.to_string()),
                });
            }
        }
    }
    let mut unique: Vec<String> = Vec::new();
    for line in lines {
        if !unique.contains(&line) {
            unique.push(line);
        }
    }
    let blocks = format == VD_WAIVER_FORMAT_SLANG;
    let had_lines = !unique.is_empty();
    let content = append_text(&existing, &unique, blocks).unwrap_or_else(|| existing.clone());
    let written = match path {
        Some(path) if content != existing => {
            write_atomic(path, &content)?;
            true
        }
        _ => false,
    };
    let entries = match format {
        VD_WAIVER_FORMAT_SLANG => parse_slang_waivers(&content).map_or(0, |waivers| waivers.len()),
        _ => content
            .lines()
            .filter(|line| !line.trim().is_empty())
            .count(),
    };
    Ok(WaiverResultData {
        written,
        deduped: !written && had_lines,
        path: path.map(|path| path.display().to_string()),
        content,
        entries,
        skipped,
        warnings,
    })
}

/// Register a waiver file in the settings store.
fn register(
    settings_path: &Path,
    format: i32,
    waiver_path: &Path,
) -> Result<WaiverResultData, VdError> {
    let key = match format {
        VD_WAIVER_FORMAT_VERIBLE => VERIBLE_WAIVER_KEY,
        VD_WAIVER_FORMAT_SLANG => SLANG_WAIVER_KEY,
        other => {
            return Err(VdError::new(
                VD_ERR_INVALID_ARGUMENT,
                &format!("format must be 0 (verible) or 1 (slang), not {other}"),
            ));
        }
    };
    let loaded = vd_settings::load(settings_path);
    let mut warnings = loaded.warnings;
    let mut entries: Vec<vd_settings::Entry> = loaded
        .entries
        .into_iter()
        .map(|entry| vd_settings::Entry {
            key: entry.key,
            value: entry.value,
        })
        .collect();
    let value_text = waiver_path.display().to_string();
    let current = entries
        .iter()
        .find(|entry| entry.key == key)
        .map(|entry| entry.value.clone())
        .unwrap_or_default();
    let (value, deduped) = match format {
        VD_WAIVER_FORMAT_VERIBLE => {
            let mut paths: Vec<String> = current
                .lines()
                .map(str::trim)
                .filter(|line| !line.is_empty())
                .map(str::to_string)
                .collect();
            if paths.iter().any(|path| path == &value_text) {
                (current.clone(), true)
            } else {
                paths.push(value_text.clone());
                if paths.len() > 1 {
                    warnings.push(format!(
                        "{key} now lists {} waiver files; they are sent as one \
                         --waiver_files=a,b argument",
                        paths.len()
                    ));
                }
                (paths.join("\n"), false)
            }
        }
        _ => {
            let deduped = current == value_text;
            (value_text.clone(), deduped)
        }
    };
    let count = value
        .lines()
        .filter(|line| !line.trim().is_empty())
        .count()
        .max(1);
    if !deduped {
        match entries.iter_mut().find(|entry| entry.key == key) {
            Some(entry) => entry.value = value.clone(),
            None => entries.push(vd_settings::Entry {
                key: key.to_string(),
                value: value.clone(),
            }),
        }
        vd_settings::save(settings_path, &entries)
            .map_err(|error| VdError::new(VD_ERR_SERVICE, &error.to_string()))?;
    }
    Ok(WaiverResultData {
        written: !deduped,
        deduped,
        path: Some(settings_path.display().to_string()),
        content: value,
        entries: count,
        skipped: 0,
        warnings,
    })
}

// --- the C surface -----------------------------------------------------------

/// Start one waiver append.
///
/// `path` is the waiver file (the project's `.verible.waiver`, a slang TOML, or
/// any path the user chose), `format` is 0 (verible) or 1 (slang). An entry that
/// is already present is not written twice, and a slang file slang itself would
/// refuse to load is left untouched. Returns the job id, or `0` when the arguments
/// were rejected.
///
/// # Safety
///
/// `service` must be null or live; `path`, `entry` and `options` must be null or
/// valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_append_start(
    service: *mut VdService,
    path: *const c_char,
    format: i32,
    entry: *const VdWaiverEntry,
    options: *const VdWaiverOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let path = match unsafe { read_path(path, "path") } {
        Ok(path) => path,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    if format != VD_WAIVER_FORMAT_VERIBLE && format != VD_WAIVER_FORMAT_SLANG {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "format must be 0 (verible) or 1 (slang)",
        ));
        return 0;
    }
    // SAFETY: the entry is copied and validated on the calling thread, so the
    // worker never reads the C struct after the start call returns.
    let request = match unsafe { read_entry(entry, format) } {
        Ok(request) => request,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    // SAFETY: `options` is null or readable.
    let options = unsafe { read_options(options) };
    JOBS.start(LANE_WAIVER, 0, move |_slot| {
        append(&path, format, &request, options)
    })
}

/// Start one waiver generate.
///
/// `path` may be null: the generated content is then returned for a preview and
/// nothing is written. `diagnostics` is a struct filled by a lint poll (the rows
/// the user is waiving) — it is READ and copied, never consumed. Verible entries
/// are written as exact `--line=N[:M]` commands; slang entries are `[[waivers]]`
/// blocks with the diagnostic's `optionName`, and a row without one is counted,
/// explained in `warnings` and skipped — never widened silently (AC-5.8).
///
/// # Safety
///
/// `service` must be null or live; every pointer must be null or valid for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_generate_start(
    service: *mut VdService,
    path: *const c_char,
    format: i32,
    diagnostics: *const VdDiagnostics,
    options: *const VdWaiverOptions,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    let target = if path.is_null() {
        None
    } else {
        // SAFETY: the caller contract is stated above.
        match unsafe { read_path(path, "path") } {
            Ok(path) => Some(path),
            Err(message) => {
                state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
                return 0;
            }
        }
    };
    if format != VD_WAIVER_FORMAT_VERIBLE && format != VD_WAIVER_FORMAT_SLANG {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "format must be 0 (verible) or 1 (slang)",
        ));
        return 0;
    }
    // SAFETY: the diagnostics are copied before the worker starts.
    let rows = match unsafe { read_diagnostics(diagnostics) } {
        Ok(rows) => rows,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    // SAFETY: `options` is null or readable.
    let options = unsafe { read_options(options) };
    JOBS.start(LANE_WAIVER, 0, move |_slot| {
        generate_from_rows(target.as_deref(), format, &rows, options)
    })
}

/// Start one waiver registration in the settings store.
///
/// Verible: `path` is APPENDED to `lint.verible.waiver_files` (one path per line,
/// deduplicated). Slang: `lint.slang.waiver_file` is set to `path`. The store is
/// written atomically by `vd-settings`; a bad existing store degrades into the
/// result's warnings, exactly like a settings load.
///
/// # Safety
///
/// `service` must be null or live; both strings must be null or valid for the
/// duration of the call.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_register_start(
    service: *mut VdService,
    settings_path: *const c_char,
    format: i32,
    path: *const c_char,
) -> u64 {
    let Some(state) = service_state(service) else {
        return 0;
    };
    // SAFETY: the caller contract is stated above.
    let settings = match unsafe { read_path(settings_path, "settings_path") } {
        Ok(path) => path,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    // SAFETY: same contract.
    let waiver = match unsafe { read_path(path, "path") } {
        Ok(path) => path,
        Err(message) => {
            state.record_error(VdError::new(VD_ERR_INVALID_ARGUMENT, &message));
            return 0;
        }
    };
    if format != VD_WAIVER_FORMAT_VERIBLE && format != VD_WAIVER_FORMAT_SLANG {
        state.record_error(VdError::new(
            VD_ERR_INVALID_ARGUMENT,
            "format must be 0 (verible) or 1 (slang)",
        ));
        return 0;
    }
    JOBS.start(LANE_WAIVER, 0, move |_slot| {
        register(&settings, format, &waiver)
    })
}

/// Poll one waiver job (any of the three).
///
/// # Safety
///
/// `service` must be null or live and `out` must be null or writable for one
/// [`VdWaiverResult`].
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_poll(
    service: *mut VdService,
    job: u64,
    out: *mut VdWaiverResult,
) -> i32 {
    if out.is_null() {
        return POLL_MISUSE;
    }
    let Some(state) = service_state(service) else {
        return POLL_MISUSE;
    };
    let Some(slot) = JOBS.get(job) else {
        return unknown_job(state);
    };
    match slot.take() {
        Taken::Pending => VD_STATUS_PENDING,
        Taken::Value(data) => {
            // SAFETY: `out` is writable for one VdWaiverResult.
            unsafe { *out = data.into_c() };
            VD_STATUS_READY
        }
        Taken::Error(error) => {
            state.record_error(error);
            VD_STATUS_FAILED
        }
        Taken::Consumed => {
            state.record_error(VdError::new(
                VD_ERR_JOB_UNKNOWN,
                "the waiver result was already taken or the job id is unknown",
            ));
            POLL_MISUSE
        }
    }
}

/// Cancel one waiver job. Returns `1` when it was live, `0` when the id was
/// unknown.
///
/// # Safety
///
/// `service` must be null or live; only null-safe access happens here.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_cancel(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match JOBS.get(job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Drop one waiver job and its result without reporting an error.
///
/// # Safety
///
/// `service` must be null or live.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_release(service: *mut VdService, job: u64) -> i32 {
    let Some(_state) = service_state(service) else {
        return 0;
    };
    match JOBS.remove(job) {
        Some(slot) => {
            slot.cancel();
            1
        }
        None => 0,
    }
}

/// Release a filled [`VdWaiverResult`]; null-safe.
///
/// # Safety
///
/// `result` must be null or a struct filled by [`vd_waiver_poll`] and not freed
/// yet.
#[no_mangle]
pub unsafe extern "C" fn vd_waiver_result_free(result: *mut VdWaiverResult) {
    if result.is_null() {
        return;
    }
    let result = &mut *result;
    // SAFETY: every string below is null or an owned C string from `into_c`.
    unsafe {
        free_string(result.path);
        free_string(result.content);
        free_string_list(&mut result.warnings);
    }
    result.path = ptr::null_mut();
    result.content = ptr::null_mut();
}
