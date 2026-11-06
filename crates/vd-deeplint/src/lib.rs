//! SLANG deep-lint execution and diagnostic model (T17).
//!
//! This crate does exactly three things: it **starts the slang process**, it
//! **parses the `--diag-json` output**, and it **maps the diagnostics into the
//! editor's problem model**. There is no SystemVerilog front end, parser or
//! linter here — all diagnostic semantics come from slang. On top of the same
//! tool it also answers the two structural questions the editor asks — the
//! elaborated hierarchy ([`hierarchy`]) and the definition-keyed module catalog
//! ([`catalog`]) — from the `--ast-json` document.
//!
//! # Two engines (T138)
//!
//! The engine is selected by the caller ([`LintEngine`]); each one keeps its own
//! argv, its own parser and its own binary resolution, and every report records
//! which engine and tool ran (AC-1.6). Verible's engine is described in
//! [`engine`]; the rest of this header describes slang.
//!
//! # Invocation (slang)
//!
//! ```text
//! slang --lint-only -Weverything --diag-column-unit byte --diag-json <tmp.json> [-I<dir>]… [-D<name>=<value>]… <file>
//! ```
//!
//! `-Wall` is not a slang option; individual checks use `-W<name>`, and
//! `-Weverything` enables all of them.
//!
//! `--diag-column-unit byte` is **required for correct CJK mapping** and is the
//! reason it is part of the fixed invocation: slang's `--diag-json` defaults to
//! `display` columns (UTF-8 display width with tab expansion — documented in
//! slang's `Driver.cpp`: `options.diagColumnUnit.value_or(ColumnUnit::Display)`).
//! Those columns cannot be converted back to text offsets (a tab counts as up to
//! eight columns, an emoji as one), so this crate asks for **byte** columns and
//! converts them to the editor's UTF-16 positions itself. The measured
//! byte/display comparison is in `evidence/deeplint/position-semantics.txt`.
//!
//! # Failure model (AC-11)
//!
//! Every degraded situation has a typed [`LintFailure`] and a short
//! [`LintFailure::user_message`] for the UI: binary not found (with the full
//! attempt list), spawn failure, timeout, cancellation, a nonzero exit without
//! JSON, malformed JSON, an oversized JSON file, and a non-UTF-8 source file.
//! A nonzero exit **with** parseable JSON is a normal result (slang exits `1`
//! when it reported errors).
//!
//! # Cancellation and leftovers
//!
//! [`LintService`] cancels the previous run when a new one starts, and also
//! offers an explicit [`LintService::cancel`]. Every exit path kills and `wait`s
//! the child, so no zombie is left behind (asserted by the runtime tests).
#![forbid(unsafe_code)]

mod ast_prune;
mod batch;
mod binary;
mod catalog;
mod dedup;
mod diagnostic;
mod engine;
mod hierarchy;
mod service;
mod tool_paths;

pub use batch::{
    BatchDiagnostic, BatchFileFailure, BatchHandle, BatchPhase, BatchProgress, BatchReport,
};
pub use binary::{
    resolve_binary, resolve_binary_for_current_process, resolve_verible_lint,
    resolve_verible_lint_for_current_process, AttemptOutcome, AttemptSource, BinaryAttempt,
    BinaryResolution,
};
pub use catalog::{
    build_catalog, parse_catalog_json, parse_catalog_json_in, CatalogModule, CatalogOptions,
    ModuleCatalog, ModuleInstance, ModulePort, PortDirection,
};
pub use dedup::{
    class_of_cli_code, class_of_deeplint, class_of_lsp, diagnostic_key, normalize_message,
    DiagnosticSource, SeverityClass,
};
pub use diagnostic::{
    ConvertError, Diagnostic, EditorPosition, EditorRange, RawLocation, RawRange, RawRangePosition,
    Severity, SourceLines,
};
pub use engine::{
    command_args, command_args_for, command_line, command_line_for, knobs_note, parse_rules_list,
    parse_slang_suppressions, parse_waiver_files, unsupported_knobs, LintEngine, Ruleset,
};
pub use hierarchy::{
    elaborate, is_empty, parse_ast_json, resolution as hierarchy_resolution, Endpoint,
    EndpointKind, HierarchyOptions, HierarchyReport, InstanceNode, SignalInfo, DEFAULT_MAX_FILES,
    DEFAULT_MAX_INSTANCES, DEFAULT_MAX_JSON_BYTES as DEFAULT_HIERARCHY_JSON_BYTES,
    DEFAULT_TIMEOUT as DEFAULT_HIERARCHY_TIMEOUT,
};
pub use service::{lint, lint_with, LintReport, LintService};

use std::fmt;
use std::path::PathBuf;
use std::time::Duration;

/// Default timeout for one slang run.
pub const DEFAULT_TIMEOUT: Duration = Duration::from_secs(10);
/// Default cap for the number of DISTINCT diagnostics one file retains.
///
/// The `--diag-json` document is streamed, so a file's warning flood is bounded
/// by this count (plus de-duplication), never by the document's byte size. The
/// per-file count bound is what the single-file UI and the pass show.
pub const DEFAULT_MAX_DIAGNOSTICS: usize = 500;

/// Default cap for the number of DISTINCT diagnostics one project-wide pass
/// retains across all files.
///
/// Low tens of thousands: enough to keep every finding a user could act on in
/// a large project, small enough that the UI model and the C list stay cheap.
/// Findings past it are COUNTED and reported as dropped; the pass never fails.
pub const DEFAULT_MAX_PASS_DIAGNOSTICS: usize = 20_000;

/// How many nested INSTANCE levels serde_json's default guard covers.
///
/// serde_json enforces a 128-value nesting limit, and one elaborated instance
/// costs three JSON levels (`Instance` -> `InstanceBody` -> `members`), so the
/// guard trips from ~42 nested instances — a NORMAL design depth, not a
/// pathological one. slang itself refuses elaborations deeper than 128
/// instances, which bounds the instance-driven JSON depth of a valid design at
/// ≈ 8 + 3 × 128 = 392 levels.
pub const AST_JSON_MAX_INSTANCE_DEPTH: usize = 42;

/// Hard bound on the JSON nesting depth the module catalog will parse (U9d).
///
/// The catalog measures the document's own nesting with an iterative, streaming
/// byte scan first, and only then disables serde_json's 128-value guard. The cap
/// exists because expression/statement nesting inside a body is NOT bounded by
/// slang's 128-instance elaboration limit: a pathological source could nest
/// deeper than any fixed stack. 10,000 = the 392-level instance ceiling + room
/// for expression nesting, ≈ 25× the valid-design bound; the walk runs on a
/// thread with a 64 MiB stack, which was measured to complete a document just
/// below this cap. A document deeper than the cap is REFUSED with its measured
/// depth — never parsed into a possible stack overflow.
pub const AST_JSON_MAX_NESTING: u64 = 10_000;

/// Options for one lint run.
#[derive(Debug, Clone)]
pub struct LintOptions {
    /// Wall-clock budget for the slang process.
    pub timeout: Duration,
    /// Maximum number of DISTINCT diagnostics kept in
    /// [`LintReport::diagnostics`]; the document is streamed, so there is no
    /// byte cap, and the total count is still reported.
    pub max_diagnostics: usize,
    /// Explicit binary override (tests, packaging). When `None`, the binary is
    /// resolved from `VEDIT_SLANG` and package-relative paths — never `PATH`.
    pub binary: Option<PathBuf>,
}

impl Default for LintOptions {
    fn default() -> Self {
        Self {
            timeout: DEFAULT_TIMEOUT,
            max_diagnostics: DEFAULT_MAX_DIAGNOSTICS,
            binary: None,
        }
    }
}

/// Options for one engine-aware lint run (T138).
///
/// [`LintOptions`] is the legacy single-binary form and stays exactly as it was
/// (its callers keep compiling); this type adds the engine choice, the project's
/// include dirs / defines and the second tool's path.
#[derive(Debug, Clone)]
pub struct EngineOptions {
    /// Which engine runs the file.
    pub engine: LintEngine,
    /// Explicit slang binary (tests, packaging). `None` resolves it like before.
    pub slang_binary: Option<PathBuf>,
    /// Explicit `verible-verilog-lint` binary. `None` resolves it per engine.
    pub verible_binary: Option<PathBuf>,
    /// Verible lint rules file (`--rules_config=<path>`; verible only).
    pub rules_config: Option<PathBuf>,
    /// Project include directories (`-I<dir>`; slang only — see [`knobs_note`]).
    pub include_dirs: Vec<PathBuf>,
    /// Project defines (`-D<name>=<value>`; slang only — see [`knobs_note`]).
    pub defines: Vec<String>,
    /// Wall-clock budget for one lint process.
    pub timeout: Duration,
    /// Maximum number of DISTINCT diagnostics kept per file (the `--diag-json`
    /// document is streamed; there is no byte cap).
    pub max_diagnostics: usize,
    /// Maximum number of DISTINCT diagnostics kept by one project-wide pass
    /// across all files; `0` selects [`DEFAULT_MAX_PASS_DIAGNOSTICS`]. Ignored
    /// by a single-file run.
    pub max_pass_diagnostics: usize,
    /// Verible `--ruleset=` selection. `None` (the default) sends NO flag: the
    /// tool's own default is the default, so "default value sends no flag".
    pub ruleset: Option<Ruleset>,
    /// Verible `--rules=` list (`+rule,-rule`, comma separated), validated by
    /// [`engine::parse_rules_list`]. `None` sends no flag.
    pub rules: Option<String>,
    /// Verible `--waiver_files=` (comma-joined into ONE argv value). Empty sends
    /// no flag.
    pub waiver_files: Vec<PathBuf>,
    /// Extra slang `-W…` tokens appended after `-Weverything` (validated by
    /// [`engine::parse_slang_suppressions`]). Empty sends no flag.
    pub slang_suppressions: Vec<String>,
    /// Slang `--error-limit=N`. `None` (or 0 at the ABI) sends no flag.
    pub error_limit: Option<u32>,
}

impl Default for EngineOptions {
    fn default() -> Self {
        Self {
            engine: LintEngine::Slang,
            slang_binary: None,
            verible_binary: None,
            rules_config: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: DEFAULT_TIMEOUT,
            max_diagnostics: DEFAULT_MAX_DIAGNOSTICS,
            max_pass_diagnostics: 0,
            ruleset: None,
            rules: None,
            waiver_files: Vec::new(),
            slang_suppressions: Vec::new(),
            error_limit: None,
        }
    }
}

impl EngineOptions {
    /// The slang-only legacy options, unchanged, as an engine-aware value.
    #[must_use]
    pub fn from_options(options: &LintOptions) -> Self {
        Self {
            engine: LintEngine::Slang,
            slang_binary: options.binary.clone(),
            verible_binary: None,
            rules_config: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: options.timeout,
            max_diagnostics: options.max_diagnostics,
            max_pass_diagnostics: 0,
            ..Self::default()
        }
    }
}

/// A degraded lint result. Each variant carries a short user-facing phrase.
#[derive(Debug, Clone)]
pub enum LintFailure {
    /// No slang binary was found; every attempt is listed.
    BinaryNotFound { attempts: Vec<BinaryAttempt> },
    /// The binary exists but the process could not start.
    Spawn { message: String },
    /// The process exceeded the timeout and was killed.
    Timeout { after: Duration },
    /// A newer lint run cancelled this one.
    Cancelled,
    /// The process exited without producing a usable JSON file.
    MissingOutput {
        exit_code: Option<i32>,
        stderr: String,
    },
    /// The JSON file could not be parsed.
    InvalidJson { message: String, stderr: String },
    /// The elaborated AST JSON could not be read. Separate from
    /// [`LintFailure::InvalidJson`] because its Display form says "diagnostics",
    /// and the catalog never reads a diagnostics document.
    InvalidAstJson { message: String },
    /// The requested top-level module does not exist in the elaborated design.
    ///
    /// RM-17: the user names a top in the module tree; slang rejects a name it
    /// cannot elaborate ("'<name>' is not a valid top-level module"). The tree
    /// must report the NAME, never silently fall back to the all-tops view.
    UnknownTop { name: String },
    /// The design nests deeper than the AST JSON reader supports.
    ///
    /// The document is VALID JSON; its nesting exceeds the catalog's
    /// [`AST_JSON_MAX_NESTING`] cap (or, on the hierarchy's whole-string path,
    /// serde_json's 128-value guard). `measured` is the deepest JSON nesting the
    /// catalog observed before refusing — `Some` when it knows the number
    /// (at least `cap + 1`), `None` when only the parser's error is available.
    /// Never reported as invalid JSON or as a diagnostic flood.
    AstJsonNestedTooDeep { measured: Option<u64> },
    /// A diagnostics JSON file exceeded a declared byte limit.
    ///
    /// The lint path NO LONGER produces this: `--diag-json` is streamed and
    /// bounded by COUNT, so volume cannot fail a file. The variant stays for
    /// API compatibility and for callers that still enforce a byte guard.
    OutputTooLarge { limit: usize },
    /// The elaborated `--ast-json` document exceeded
    /// [`HierarchyOptions::max_json_bytes`]. A separate variant because the
    /// artifact is the AST dump, not the diagnostics: the shared wording sent
    /// users and maintainers chasing a diagnostic flood that did not exist.
    AstJsonTooLarge { limit: usize },
    /// The linted file is not valid UTF-8, so positions cannot be mapped.
    InvalidSourceEncoding { path: PathBuf },
    /// Any other I/O problem.
    Io { message: String },
}

impl LintFailure {
    /// Short sentence the UI can show for AC-11.
    #[must_use]
    pub fn user_message(&self) -> String {
        match self {
            Self::BinaryNotFound { attempts } => format!(
                "slang binary not found (tried {} location(s); set VEDIT_SLANG)",
                attempts.len()
            ),
            Self::Spawn { message } => format!("slang could not start: {message}"),
            Self::Timeout { after } => {
                format!("slang timed out after {} ms", after.as_millis())
            }
            Self::Cancelled => "lint cancelled".to_string(),
            Self::MissingOutput { exit_code, stderr } => {
                let base = match exit_code {
                    Some(code) => {
                        format!("the lint tool exited with code {code} and produced no diagnostics")
                    }
                    None => "the lint tool terminated without a result".to_string(),
                };
                // The child's stderr is the only witness when a tool dies before
                // it can write JSON (e.g. the loader's `GLIBC_… not found`);
                // surface its first non-empty line instead of hiding it.
                match stderr.lines().map(str::trim).find(|line| !line.is_empty()) {
                    Some(line) => format!("{base}: {line}"),
                    None => base,
                }
            }
            Self::InvalidJson { .. } => "the lint tool produced malformed diagnostics".to_string(),
            Self::InvalidAstJson { message } => {
                format!("the elaborated AST JSON could not be read: {message}")
            }
            Self::UnknownTop { name } => format!(
                "the module '{name}' is not a valid top-level module in this project: \
                 the module tree and the lint scope were not changed"
            ),
            Self::AstJsonNestedTooDeep { measured } => match measured {
                Some(depth) => format!(
                    "this design's AST JSON nests at least {depth} levels deep, past the \
                     catalog's {AST_JSON_MAX_NESTING}-level limit: the module catalog is \
                     unavailable for this design"
                ),
                None => format!(
                    "this design's AST JSON nests deeper than the catalog's \
                     {AST_JSON_MAX_NESTING}-level limit: the module catalog is unavailable \
                     for this design"
                ),
            },
            Self::OutputTooLarge { limit } => {
                format!("lint diagnostics exceeded the {limit} byte limit")
            }
            Self::AstJsonTooLarge { limit } => format!(
                "the elaborated AST JSON exceeded the {limit} byte limit (the hierarchy view holds the whole document in memory; the module catalog streams it)"
            ),
            Self::InvalidSourceEncoding { path } => {
                format!("{} is not valid UTF-8", path.display())
            }
            Self::Io { message } => format!("lint I/O error: {message}"),
        }
    }
}

impl fmt::Display for LintFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::BinaryNotFound { attempts } => {
                writeln!(f, "slang binary not found; attempts:")?;
                for attempt in attempts {
                    writeln!(f, "  {}", attempt.describe())?;
                }
                Ok(())
            }
            Self::Spawn { message } => write!(f, "spawn failed: {message}"),
            Self::Timeout { after } => write!(f, "timed out after {after:?}"),
            Self::Cancelled => f.write_str("cancelled"),
            Self::MissingOutput { exit_code, stderr } => write!(
                f,
                "no JSON output (exit {exit_code:?}); stderr: {}",
                stderr.lines().next().unwrap_or("")
            ),
            Self::InvalidJson { message, .. } => write!(f, "invalid diagnostics JSON: {message}"),
            Self::InvalidAstJson { message } => write!(f, "invalid AST JSON: {message}"),
            Self::UnknownTop { name } => write!(
                f,
                "unknown top-level module '{name}'"
            ),
            Self::AstJsonNestedTooDeep { measured } => match measured {
                Some(depth) => write!(
                    f,
                    "the design's AST JSON nests at least {depth} levels deep (limit {AST_JSON_MAX_NESTING})"
                ),
                None => write!(
                    f,
                    "the design nests deeper than the AST JSON reader supports (limit {AST_JSON_MAX_NESTING})"
                ),
            },
            Self::OutputTooLarge { limit } => {
                write!(f, "diagnostics JSON exceeds {limit} bytes")
            }
            Self::AstJsonTooLarge { limit } => {
                write!(f, "elaborated AST JSON exceeds {limit} bytes")
            }
            Self::InvalidSourceEncoding { path } => {
                write!(f, "{} is not valid UTF-8", path.display())
            }
            Self::Io { message } => write!(f, "I/O error: {message}"),
        }
    }
}

impl std::error::Error for LintFailure {}

#[cfg(test)]
mod tests {
    use super::LintFailure;

    #[test]
    fn missing_output_message_names_the_first_non_empty_stderr_line() {
        let failure = LintFailure::MissingOutput {
            exit_code: Some(1),
            stderr: "\n  version `GLIBC_2.35' not found (required by libc.so.6)\nmore\n"
                .to_string(),
        };
        let message = failure.user_message();
        assert!(
            message.contains("exited with code 1"),
            "the exit code stays visible: {message}"
        );
        assert!(
            message.contains("version `GLIBC_2.35' not found"),
            "the loader's reason must reach the UI: {message}"
        );
    }

    #[test]
    fn an_ast_json_overflow_never_blames_lint_diagnostics() {
        let failure = LintFailure::AstJsonTooLarge { limit: 268_435_456 };
        let message = failure.user_message();
        assert!(
            message.contains("AST JSON"),
            "the message must name the artifact that was too large: {message}"
        );
        assert!(
            message.contains("268435456"),
            "the message must name the limit: {message}"
        );
        assert!(
            !message.contains("lint") && !message.contains("diagnostics"),
            "an AST-JSON overflow must not be reported as a diagnostic flood: {message}"
        );
        let rendered = failure.to_string();
        assert!(
            rendered.contains("AST JSON") && !rendered.contains("diagnostics"),
            "the Display form is truthful too: {rendered}"
        );
    }

    #[test]
    fn a_too_deep_design_names_the_measured_depth_and_the_cap() {
        let failure = LintFailure::AstJsonNestedTooDeep {
            measured: Some(crate::AST_JSON_MAX_NESTING + 1),
        };
        let message = failure.user_message();
        assert!(
            message.contains("AST JSON") && message.contains("nests"),
            "the message must name the artifact and the nesting: {message}"
        );
        assert!(
            message.contains(&(crate::AST_JSON_MAX_NESTING + 1).to_string())
                && message.contains(&crate::AST_JSON_MAX_NESTING.to_string()),
            "the message must name the measured depth AND the cap: {message}"
        );
        assert!(
            !message.contains("invalid") && !message.contains("malformed"),
            "a valid but too-deep document is not invalid JSON: {message}"
        );
        assert!(
            !message.contains("lint") && !message.contains("diagnostics"),
            "the catalog's nesting limit is not a diagnostic flood: {message}"
        );
        let rendered = failure.to_string();
        assert!(
            !rendered.contains("invalid") && !rendered.contains("diagnostics"),
            "the Display form is equally honest: {rendered}"
        );
        // The hierarchy path only has serde_json's error and still says the cap.
        let unknown = LintFailure::AstJsonNestedTooDeep { measured: None };
        assert!(
            unknown
                .user_message()
                .contains(&crate::AST_JSON_MAX_NESTING.to_string()),
            "the unknown-depth form still names the cap: {}",
            unknown.user_message()
        );
    }

    #[test]
    fn the_lint_output_cap_keeps_its_diagnostic_wording() {
        let message = LintFailure::OutputTooLarge { limit: 8_388_608 }.user_message();
        assert!(
            message.contains("lint diagnostics") && message.contains("8388608"),
            "the --diag-json cap still says what it is: {message}"
        );
    }

    #[test]
    fn missing_output_message_without_stderr_keeps_the_short_form() {
        let failure = LintFailure::MissingOutput {
            exit_code: None,
            stderr: String::new(),
        };
        assert_eq!(
            failure.user_message(),
            "the lint tool terminated without a result"
        );
    }
}
