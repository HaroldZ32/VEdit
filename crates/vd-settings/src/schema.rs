//! Typed projection of the flat settings store (settings pages, item 10.2).
//!
//! [`crate::load`] and [`crate::save`] only move `key = "value"` pairs. This
//! module is where those pairs acquire meaning: the Qt Preferences dialog writes
//! exactly the keys declared in [`keys`], and the Rust services read them through
//! [`ToolSettings::from_entries`] / [`FimSettings::from_entries`] instead of each
//! inventing its own spelling. The store stays dumb; the spelling lives here.
//!
//! # Key contract
//!
//! | key | values | default |
//! | --- | --- | --- |
//! | `tools.lsp_backend` | `verible`, `slang` | `verible` |
//! | `tools.lint_engine` | `slang`, `verible` | `slang` |
//! | `tools.verible_ls` | path to `verible-verilog-ls` | empty (resolver decides) |
//! | `tools.slang` | path to `slang` | empty |
//! | `tools.slang_server` | path to `slang-server` | empty |
//! | `tools.verible_format` | path to `verible-verilog-format` | empty |
//! | `tools.verible_rules` | path to a `.rules.verible_lint` file | empty |
//! | `tools.include_dirs` | one `-I` directory per line | empty |
//! | `tools.defines` | one `-D` define per line | empty |
//! | `fim.enabled` | boolean | `false` |
//! | `fim.protocol` | `open_ai`, `llama_cpp` | `open_ai` |
//! | `fim.endpoint` | base URL | empty |
//! | `fim.model` | model name | empty |
//! | `fim.timeout_ms` | integer milliseconds | `5000` |
//! | `fim.max_tokens` | integer tokens | `64` |
//! | `fim.ghost_text` | boolean | `false` |
//!
//! List-valued keys hold **one item per line**; blank lines are ignored and a
//! trailing `\r` is stripped, so a file edited on Windows still reads back.
//! Booleans accept `true`/`false`, `1`/`0`, `yes`/`no` and `on`/`off`, in any
//! case; an unparsable value falls back to the default instead of failing — the
//! AC-S3.4 "bad configuration degrades" rule.
//!
//! # The FIM API key has no store key
//!
//! `settings.toml` is a plain readable file, so a key must never be written to
//! it. There is deliberately **no** `fim.api_key` key, [`FimSettings`] has no
//! field for one, and a `fim.api_key` pair written by an older build is ignored
//! (the dialog drops it on the next save). The key comes from, in order:
//!
//! 1. the `VEDIT_FIM_API_KEY` environment variable;
//! 2. the user-level key file [`API_KEY_FILE`] next to `settings.toml`, which
//!    must be readable by the owner only (`0600`). A file with group or other
//!    access is **refused** and reported as [`KeySource::InsecureUserFile`],
//!    never read.
//!
//! [`ApiKey`] has no `Display` and a redacted `Debug`, so the secret cannot leak
//! through a log line or a `{:?}` of the resolved settings.

use std::fmt;
use std::path::{Path, PathBuf};

use crate::Entry;

/// Store keys the settings pages read and write.
pub mod keys {
    /// LSP backend selector: which language server serves diagnostics/outline.
    pub const LSP_BACKEND: &str = "tools.lsp_backend";
    /// Lint engine selector: which tool runs the project-wide batch lint.
    pub const LINT_ENGINE: &str = "tools.lint_engine";
    /// Path to the `verible-verilog-ls` binary.
    pub const VERIBLE_LS: &str = "tools.verible_ls";
    /// Path to the `slang` binary.
    pub const SLANG: &str = "tools.slang";
    /// Path to the `slang-server` binary.
    pub const SLANG_SERVER: &str = "tools.slang_server";
    /// Path to the `verible-verilog-format` binary.
    pub const VERIBLE_FORMAT: &str = "tools.verible_format";
    /// Path to the Verible rules file.
    pub const VERIBLE_RULES: &str = "tools.verible_rules";
    /// Extra include directories (`-I`), one per line.
    pub const INCLUDE_DIRS: &str = "tools.include_dirs";
    /// Extra defines (`-D`), one per line.
    pub const DEFINES: &str = "tools.defines";
    /// FIM master switch.
    pub const FIM_ENABLED: &str = "fim.enabled";
    /// FIM wire protocol.
    pub const FIM_PROTOCOL: &str = "fim.protocol";
    /// FIM base endpoint.
    pub const FIM_ENDPOINT: &str = "fim.endpoint";
    /// FIM model name.
    pub const FIM_MODEL: &str = "fim.model";
    /// FIM request timeout in milliseconds.
    pub const FIM_TIMEOUT_MS: &str = "fim.timeout_ms";
    /// FIM completion length.
    pub const FIM_MAX_TOKENS: &str = "fim.max_tokens";
    /// Ghost-text rendering switch.
    pub const FIM_GHOST_TEXT: &str = "fim.ghost_text";

    // --- UX batch 2 / A4: appearance (font + scale) ------------------------
    /// Application font point size; absent (or `0`) follows the system font.
    pub const UI_FONT_POINT_SIZE: &str = "ui.font_point_size";
    /// Interface scale in percent; 100/125/150/200 (applied on the NEXT start).
    pub const UI_SCALE_PERCENT: &str = "ui.scale_percent";

    // --- UX batch 2 / A4: verible-verilog-format ---------------------------
    /// `--column_limit` (verible default 100; absent sends no flag).
    pub const FORMAT_COLUMN_LIMIT: &str = "format.column_limit";
    /// `--indentation_spaces` (verible default 2).
    pub const FORMAT_INDENTATION_SPACES: &str = "format.indentation_spaces";
    /// `--wrap_spaces` (verible default 4).
    pub const FORMAT_WRAP_SPACES: &str = "format.wrap_spaces";
    /// `--line_terminator` (verible default `auto`).
    pub const FORMAT_LINE_TERMINATOR: &str = "format.line_terminator";
    /// `--try_wrap_long_lines` (verible default false).
    pub const FORMAT_TRY_WRAP_LONG_LINES: &str = "format.try_wrap_long_lines";
    /// `--compact_indexing_and_selections` (verible default true).
    pub const FORMAT_COMPACT_INDEXING_AND_SELECTIONS: &str =
        "format.compact_indexing_and_selections";

    // --- UX batch 2 / A4 / §0.1 R1: two independent lint configurations ----
    /// verible: `--ruleset` (`default`/`all`/`none`).
    pub const LINT_VERIBLE_RULESET: &str = "lint.verible.ruleset";
    /// verible: `--rules=` (a comma-separated rule list, stored verbatim).
    pub const LINT_VERIBLE_RULES: &str = "lint.verible.rules";
    /// verible: `--rules_config=` (a path to a `.rules.verible_lint` file).
    pub const LINT_VERIBLE_RULES_CONFIG: &str = "lint.verible.rules_config";
    /// verible: `--waiver_files=` (one path per line).
    pub const LINT_VERIBLE_WAIVER_FILES: &str = "lint.verible.waiver_files";
    /// verible: `--autofix=` (`no`/`patch`/`patch-interactive`/`inplace`/
    /// `inplace-interactive`/`generate-waiver`).
    pub const LINT_VERIBLE_AUTOFIX: &str = "lint.verible.autofix";
    /// slang: `-W` values (one per line, e.g. `everything`, `none`,
    /// `no-unused-variable`).
    pub const LINT_SLANG_WARNINGS: &str = "lint.slang.warnings";
    /// slang: `--error-limit` (0 disables the limit, per `slang --help`).
    pub const LINT_SLANG_ERROR_LIMIT: &str = "lint.slang.error_limit";
}

/// The alignment knobs of `verible-verilog-format` (flag name without `--`).
///
/// Each is stored under `format.<flag name>` and accepts the value set in
/// [`Alignment`]. An absent key means "verible's own default" — the Qt side
/// then sends NO flag at all, which is why the stored value is optional
/// everywhere.
pub const FORMAT_ALIGNMENT_FLAGS: [&str; 15] = [
    "assignment_statement_alignment",
    "case_items_alignment",
    "class_member_variable_alignment",
    "distribution_items_alignment",
    "enum_assignment_statement_alignment",
    "formal_parameters_alignment",
    "module_net_variable_alignment",
    "named_parameter_alignment",
    "named_port_alignment",
    "port_declarations_alignment",
    "struct_union_members_alignment",
    // The two `{indent,wrap}` knobs share the same storage rule as the
    // alignment enums: an absent key means "no flag".
    "formal_parameters_indentation",
    "named_parameter_indentation",
    "named_port_indentation",
    "port_declarations_indentation",
];

/// The `{indent,wrap}` indentation knobs (a subset of the enum knobs above).
pub const FORMAT_INDENTATION_FLAGS: [&str; 4] = [
    "formal_parameters_indentation",
    "named_parameter_indentation",
    "named_port_indentation",
    "port_declarations_indentation",
];

/// Environment variable holding the FIM API key; it wins over any stored key.
pub const API_KEY_ENV: &str = "VEDIT_FIM_API_KEY";
/// Name of the optional user-level key file, next to `settings.toml`.
pub const API_KEY_FILE: &str = "fim_api_key";
/// Mode the key file must have on POSIX systems.
#[cfg(unix)]
pub const API_KEY_FILE_MODE: u32 = 0o600;

/// Which language server serves the open files.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LspBackend {
    /// `verible-verilog-ls` — the baseline backend.
    #[default]
    Verible,
    /// `slang-server`.
    Slang,
}

impl LspBackend {
    /// Stored spellings.
    pub const VALUES: [&'static str; 2] = ["verible", "slang"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Verible => "verible",
            Self::Slang => "slang",
        }
    }

    /// The binary name this backend runs.
    #[must_use]
    pub fn binary(self) -> &'static str {
        match self {
            Self::Verible => "verible-verilog-ls",
            Self::Slang => "slang-server",
        }
    }

    /// Parse a stored value; `None` for anything that is not a backend.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "verible" | "verible-verilog-ls" => Some(Self::Verible),
            "slang" | "slang-server" => Some(Self::Slang),
            _ => None,
        }
    }
}

/// Which tool runs the project-wide batch lint.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LintEngine {
    /// `slang --lint-only` — the baseline engine.
    #[default]
    Slang,
    /// `verible-verilog-lint`.
    Verible,
}

impl LintEngine {
    /// Stored spellings.
    pub const VALUES: [&'static str; 2] = ["slang", "verible"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Slang => "slang",
            Self::Verible => "verible",
        }
    }

    /// The binary name this engine runs.
    #[must_use]
    pub fn binary(self) -> &'static str {
        match self {
            Self::Slang => "slang",
            Self::Verible => "verible-verilog-lint",
        }
    }

    /// Parse a stored value; `None` for anything that is not an engine.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "slang" => Some(Self::Slang),
            "verible" | "verible-verilog-lint" => Some(Self::Verible),
            _ => None,
        }
    }
}

/// Wire protocol of the configured FIM endpoint.
///
/// The stored spellings match `vd_config::FimProtocol`'s serde names so the
/// same value can be handed to the config layer unchanged.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum FimProtocol {
    /// OpenAI-compatible `POST /v1/completions`.
    #[default]
    OpenAi,
    /// llama.cpp `POST /infill`.
    LlamaCpp,
}

impl FimProtocol {
    /// Stored spellings.
    pub const VALUES: [&'static str; 2] = ["open_ai", "llama_cpp"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::OpenAi => "open_ai",
            Self::LlamaCpp => "llama_cpp",
        }
    }

    /// Parse a stored value; `None` for anything that is not a protocol.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "open_ai" | "openai" => Some(Self::OpenAi),
            "llama_cpp" | "llamacpp" | "llama.cpp" => Some(Self::LlamaCpp),
            _ => None,
        }
    }
}

/// Values of the Preferences "Tools" page.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ToolSettings {
    /// Selected language server.
    pub lsp_backend: LspBackend,
    /// Selected batch-lint engine.
    pub lint_engine: LintEngine,
    /// Configured path to `verible-verilog-ls`; empty means "let the resolver decide".
    pub verible_ls: String,
    /// Configured path to `slang`; empty means "let the resolver decide".
    pub slang: String,
    /// Configured path to `slang-server`; empty means "let the resolver decide".
    pub slang_server: String,
    /// Configured path to `verible-verilog-format`; empty means "let the resolver decide".
    pub verible_format: String,
    /// Configured Verible rules file; empty means "the built-in rule set".
    pub verible_rules: String,
    /// `-I` directories, in the order the user listed them.
    pub include_dirs: Vec<String>,
    /// `-D` defines, `NAME` or `NAME=VALUE`, in the order the user listed them.
    pub defines: Vec<String>,
}

impl ToolSettings {
    /// Read the tool keys out of a loaded store.
    #[must_use]
    pub fn from_entries(entries: &[Entry]) -> Self {
        Self {
            lsp_backend: value(entries, keys::LSP_BACKEND)
                .and_then(LspBackend::parse)
                .unwrap_or_default(),
            lint_engine: value(entries, keys::LINT_ENGINE)
                .and_then(LintEngine::parse)
                .unwrap_or_default(),
            verible_ls: text(entries, keys::VERIBLE_LS),
            slang: text(entries, keys::SLANG),
            slang_server: text(entries, keys::SLANG_SERVER),
            verible_format: text(entries, keys::VERIBLE_FORMAT),
            verible_rules: text(entries, keys::VERIBLE_RULES),
            include_dirs: list(entries, keys::INCLUDE_DIRS),
            defines: list(entries, keys::DEFINES),
        }
    }
}

/// Values of the Preferences "FIM" page.
///
/// There is no `api_key` field on purpose: a secret never travels through the
/// store projection. [`resolve_api_key`] reads it from the environment or the
/// `0600` user file instead.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FimSettings {
    /// Whether FIM completions are switched on.
    pub enabled: bool,
    /// Wire protocol of the endpoint.
    pub protocol: FimProtocol,
    /// Base endpoint; empty means "not configured".
    pub endpoint: String,
    /// Model name; empty means "not configured".
    pub model: String,
    /// Request timeout in milliseconds.
    pub timeout_ms: u64,
    /// Completion length.
    pub max_tokens: u32,
    /// Whether the suggestion is drawn inline as ghost text.
    pub ghost_text: bool,
}

impl Default for FimSettings {
    fn default() -> Self {
        Self {
            enabled: false,
            protocol: FimProtocol::default(),
            endpoint: String::new(),
            model: String::new(),
            timeout_ms: 5_000,
            max_tokens: 64,
            ghost_text: false,
        }
    }
}

impl FimSettings {
    /// Read the FIM keys out of a loaded store.
    ///
    /// A `fim.api_key` pair in `entries` is ignored: this projection has nowhere
    /// to put it, so a hand-edited plaintext key never reaches the transport.
    #[must_use]
    pub fn from_entries(entries: &[Entry]) -> Self {
        let defaults = Self::default();
        Self {
            enabled: flag(entries, keys::FIM_ENABLED, defaults.enabled),
            protocol: value(entries, keys::FIM_PROTOCOL)
                .and_then(FimProtocol::parse)
                .unwrap_or(defaults.protocol),
            endpoint: text(entries, keys::FIM_ENDPOINT),
            model: text(entries, keys::FIM_MODEL),
            timeout_ms: number(entries, keys::FIM_TIMEOUT_MS, defaults.timeout_ms),
            max_tokens: count(entries, keys::FIM_MAX_TOKENS, defaults.max_tokens),
            ghost_text: flag(entries, keys::FIM_GHOST_TEXT, defaults.ghost_text),
        }
    }
}

/// One `verible-verilog-format` alignment knob value.
///
/// The stored spellings are verible's own (`{align,flush-left,preserve,infer}`),
/// because the Qt side hands the value to the formatter verbatim. The other
/// enum knobs (`{indent,wrap}`) are stored the same way under
/// [`FORMAT_INDENTATION_FLAGS`] and are read as plain strings by the Qt side.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Alignment {
    /// `align`.
    Align,
    /// `flush-left`.
    FlushLeft,
    /// `preserve`.
    Preserve,
    /// `infer` — verible's own default.
    #[default]
    Infer,
}

impl Alignment {
    /// The stored spellings (verible's value set, verbatim).
    pub const VALUES: [&'static str; 4] = ["align", "flush-left", "preserve", "infer"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Align => "align",
            Self::FlushLeft => "flush-left",
            Self::Preserve => "preserve",
            Self::Infer => "infer",
        }
    }

    /// Parse a stored value; `None` for anything outside verible's value set.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "align" => Some(Self::Align),
            "flush-left" => Some(Self::FlushLeft),
            "preserve" => Some(Self::Preserve),
            "infer" => Some(Self::Infer),
            _ => None,
        }
    }
}

/// `--line_terminator` of `verible-verilog-format` (measured value set:
/// `CRLF,LF,auto`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum FormatLineTerminator {
    /// Follow the input (`auto`, verible's default).
    #[default]
    Auto,
    /// Force `\n`.
    Lf,
    /// Force `\r\n`.
    Crlf,
}

impl FormatLineTerminator {
    /// The stored spellings — EXACTLY verible's (`CRLF`, `LF`, `auto`); any
    /// other spelling is rejected by the formatter.
    pub const VALUES: [&'static str; 3] = ["CRLF", "LF", "auto"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Auto => "auto",
            Self::Lf => "LF",
            Self::Crlf => "CRLF",
        }
    }

    /// Parse a stored value; `None` for anything outside verible's value set.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim() {
            "auto" | "Auto" | "AUTO" => Some(Self::Auto),
            "LF" | "lf" => Some(Self::Lf),
            "CRLF" | "crlf" => Some(Self::Crlf),
            _ => None,
        }
    }
}

/// Values of the Preferences "Appearance" page (UX batch 2 / A4).
///
/// `font_point_size: None` means "follow the system font": the Qt side then
/// applies no font at all. The scale is an explicit choice, so it always has a
/// value and an out-of-set stored value falls back to [`Self::default`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct UiSettings {
    /// Application font point size, or `None` for the system font.
    pub font_point_size: Option<u16>,
    /// Interface scale in percent; applied on the NEXT start (restart).
    pub scale_percent: u16,
}

impl Default for UiSettings {
    fn default() -> Self {
        Self {
            font_point_size: None,
            scale_percent: 100,
        }
    }
}

impl UiSettings {
    /// The accepted scale choices (the Preferences combo shows exactly these).
    pub const SCALE_PERCENTS: [u16; 4] = [100, 125, 150, 200];
    /// The accepted font sizes, in points (inclusive).
    pub const FONT_POINT_SIZE_RANGE: std::ops::RangeInclusive<u16> = 6..=72;

    /// Read the appearance keys out of a loaded store.
    ///
    /// A value outside the accepted set is treated as absent — the AC-S3.4
    /// "bad configuration degrades" rule — so a hand-edited `scale_percent =
    /// "1337"` cannot make the product start zoomed by an arbitrary factor.
    #[must_use]
    pub fn from_entries(entries: &[Entry]) -> Self {
        let defaults = Self::default();
        Self {
            font_point_size: value(entries, keys::UI_FONT_POINT_SIZE)
                .and_then(|text| text.trim().parse::<u16>().ok())
                .filter(|size| Self::FONT_POINT_SIZE_RANGE.contains(size)),
            scale_percent: value(entries, keys::UI_SCALE_PERCENT)
                .and_then(|text| text.trim().parse::<u16>().ok())
                .filter(|percent| Self::SCALE_PERCENTS.contains(percent))
                .unwrap_or(defaults.scale_percent),
        }
    }

    /// The `QT_SCALE_FACTOR` value this scale needs, or `None` for 100 %.
    ///
    /// 100 % deliberately returns `None`: the startup path then leaves an
    /// ambient `QT_SCALE_FACTOR` (a HiDPI environment) alone instead of
    /// overwriting it with `1`.
    #[must_use]
    pub fn scale_factor(self) -> Option<&'static str> {
        match self.scale_percent {
            125 => Some("1.25"),
            150 => Some("1.5"),
            200 => Some("2"),
            _ => None,
        }
    }
}

/// Values of the Preferences "Format" page (UX batch 2 / A4).
///
/// Every field is `Option`/empty by default, and "absent" means **send no
/// flag**: the defaults are verible's own, so the formatter behaves exactly as
/// it did before this page existed until the user changes something.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct FormatSettings {
    /// `--column_limit` (verible default 100).
    pub column_limit: Option<u16>,
    /// `--indentation_spaces` (verible default 2).
    pub indentation_spaces: Option<u8>,
    /// `--wrap_spaces` (verible default 4).
    pub wrap_spaces: Option<u8>,
    /// `--line_terminator` (verible default `auto`).
    pub line_terminator: Option<FormatLineTerminator>,
    /// `--try_wrap_long_lines` (verible default `false`).
    pub try_wrap_long_lines: Option<bool>,
    /// `--compact_indexing_and_selections` (verible default `true`).
    pub compact_indexing_and_selections: Option<bool>,
    /// Every `{align,flush-left,preserve,infer}` knob that is present and valid,
    /// keyed by its `verible-verilog-format` flag name (no `--`). The
    /// `{indent,wrap}` knobs stay strings and are not projected here.
    pub alignments: std::collections::BTreeMap<&'static str, Alignment>,
}

impl FormatSettings {
    /// Accepted `--column_limit` range (inclusive).
    pub const COLUMN_LIMIT_RANGE: std::ops::RangeInclusive<u16> = 1..=1000;
    /// Accepted `--indentation_spaces` / `--wrap_spaces` range (inclusive).
    pub const SPACE_RANGE: std::ops::RangeInclusive<u8> = 0..=16;

    /// Read the format keys out of a loaded store.
    #[must_use]
    pub fn from_entries(entries: &[Entry]) -> Self {
        let mut alignments = std::collections::BTreeMap::new();
        for flag in FORMAT_ALIGNMENT_FLAGS {
            if FORMAT_INDENTATION_FLAGS.contains(&flag) {
                continue;
            }
            let key = format!("format.{flag}");
            if let Some(parsed) = value(entries, &key).and_then(Alignment::parse) {
                alignments.insert(flag, parsed);
            }
        }
        Self {
            column_limit: optional_number(entries, keys::FORMAT_COLUMN_LIMIT)
                .and_then(|value| u16::try_from(value).ok())
                .filter(|limit| Self::COLUMN_LIMIT_RANGE.contains(limit)),
            indentation_spaces: optional_number(entries, keys::FORMAT_INDENTATION_SPACES)
                .and_then(|value| u8::try_from(value).ok())
                .filter(|spaces| Self::SPACE_RANGE.contains(spaces)),
            wrap_spaces: optional_number(entries, keys::FORMAT_WRAP_SPACES)
                .and_then(|value| u8::try_from(value).ok())
                .filter(|spaces| Self::SPACE_RANGE.contains(spaces)),
            line_terminator: value(entries, keys::FORMAT_LINE_TERMINATOR)
                .and_then(FormatLineTerminator::parse),
            try_wrap_long_lines: optional_flag(entries, keys::FORMAT_TRY_WRAP_LONG_LINES),
            compact_indexing_and_selections: optional_flag(
                entries,
                keys::FORMAT_COMPACT_INDEXING_AND_SELECTIONS,
            ),
            alignments,
        }
    }
}

/// `--ruleset` of `verible-verilog-lint`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum VeribleRuleset {
    /// `default`.
    #[default]
    Default,
    /// `all`.
    All,
    /// `none`.
    None,
}

impl VeribleRuleset {
    /// The stored spellings (verible's own).
    pub const VALUES: [&'static str; 3] = ["default", "all", "none"];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::All => "all",
            Self::None => "none",
        }
    }

    /// Parse a stored value; `None` for anything outside verible's value set.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "default" => Some(Self::Default),
            "all" => Some(Self::All),
            "none" => Some(Self::None),
            _ => None,
        }
    }
}

/// `--autofix` of `verible-verilog-lint`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum VeribleAutofix {
    /// `no` — the default; report only.
    #[default]
    No,
    /// `patch` — print a unified diff.
    Patch,
    /// `patch-interactive`.
    PatchInteractive,
    /// `inplace` — rewrite the file.
    Inplace,
    /// `inplace-interactive`.
    InplaceInteractive,
    /// `generate-waiver`.
    GenerateWaiver,
}

impl VeribleAutofix {
    /// The stored spellings (verible's own, from `--helpfull`).
    pub const VALUES: [&'static str; 6] = [
        "no",
        "patch",
        "patch-interactive",
        "inplace",
        "inplace-interactive",
        "generate-waiver",
    ];

    /// The stored value.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::No => "no",
            Self::Patch => "patch",
            Self::PatchInteractive => "patch-interactive",
            Self::Inplace => "inplace",
            Self::InplaceInteractive => "inplace-interactive",
            Self::GenerateWaiver => "generate-waiver",
        }
    }

    /// Parse a stored value; `None` for anything outside verible's value set.
    #[must_use]
    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "no" => Some(Self::No),
            "patch" => Some(Self::Patch),
            "patch-interactive" => Some(Self::PatchInteractive),
            "inplace" => Some(Self::Inplace),
            "inplace-interactive" => Some(Self::InplaceInteractive),
            "generate-waiver" => Some(Self::GenerateWaiver),
            _ => None,
        }
    }
}

/// The `verible-verilog-lint` half of the Preferences "Lint" page (§0.1 R1).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct LintVeribleSettings {
    /// `--ruleset`; `None` sends no flag (verible's own `default`).
    pub ruleset: Option<VeribleRuleset>,
    /// `--rules=` — a comma-separated rule list, stored verbatim.
    pub rules: String,
    /// `--rules_config=` — the path to a `.rules.verible_lint` file.
    pub rules_config: String,
    /// `--waiver_files=` — one path per line (blank lines ignored).
    pub waiver_files: Vec<String>,
    /// `--autofix`; `None` sends no flag (verible's own `no`).
    pub autofix: Option<VeribleAutofix>,
}

/// The `slang` half of the Preferences "Lint" page (§0.1 R1).
///
/// Only what `slang` really accepts: `-W` values and `--error-limit`
/// (measured with `slang --help`). slang has no ruleset, no
/// `.rules.verible_lint` and no autofix — the panel greys those out while
/// slang is the engine instead of pretending.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct LintSlangSettings {
    /// `-W` values, one per line (`everything`, `none`, `no-unused-variable`, …).
    pub warnings: Vec<String>,
    /// `--error-limit`; `0` disables the limit (slang's own documented value).
    pub error_limit: Option<u32>,
}

impl LintSlangSettings {
    /// Accepted `--error-limit` range (inclusive).
    pub const ERROR_LIMIT_RANGE: std::ops::RangeInclusive<u32> = 0..=1_000_000;
}

/// Both lint configurations, persisted independently (§0.1 R1).
///
/// The two halves never share a key, so switching `tools.lint_engine` cannot
/// overwrite the other engine's settings: that is the whole point of the R1
/// ruling, and it is asserted in this module's tests.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct LintSettings {
    /// The `lint.verible.*` set.
    pub verible: LintVeribleSettings,
    /// The `lint.slang.*` set.
    pub slang: LintSlangSettings,
}

impl LintSettings {
    /// Read both lint configurations out of a loaded store.
    #[must_use]
    pub fn from_entries(entries: &[Entry]) -> Self {
        Self {
            verible: LintVeribleSettings {
                ruleset: value(entries, keys::LINT_VERIBLE_RULESET).and_then(VeribleRuleset::parse),
                rules: text(entries, keys::LINT_VERIBLE_RULES),
                rules_config: text(entries, keys::LINT_VERIBLE_RULES_CONFIG),
                waiver_files: list(entries, keys::LINT_VERIBLE_WAIVER_FILES),
                autofix: value(entries, keys::LINT_VERIBLE_AUTOFIX).and_then(VeribleAutofix::parse),
            },
            slang: LintSlangSettings {
                warnings: list(entries, keys::LINT_SLANG_WARNINGS),
                error_limit: optional_number(entries, keys::LINT_SLANG_ERROR_LIMIT)
                    .and_then(|value| u32::try_from(value).ok())
                    .filter(|limit| LintSlangSettings::ERROR_LIMIT_RANGE.contains(limit)),
            },
        }
    }
}

/// A secret with redaction forced by its own type.
#[derive(Clone, PartialEq, Eq)]
pub struct ApiKey(String);
impl ApiKey {
    /// Wrap a raw key value.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// True when the key is empty or whitespace-only.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.0.trim().is_empty()
    }

    /// Borrow the raw key.
    ///
    /// Call this only from the transport adapter that has to send it. Never log,
    /// format or serialize the result.
    #[must_use]
    pub fn expose_secret(&self) -> &str {
        &self.0
    }
}

impl fmt::Debug for ApiKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("ApiKey(<redacted>)")
    }
}

/// Where a resolved API key came from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KeySource {
    /// No environment variable and no usable key file.
    None,
    /// `VEDIT_FIM_API_KEY`.
    Environment,
    /// The user-level key file, read because it is owner-only.
    UserFile(PathBuf),
    /// The key file exists but admits group/other access, so it was refused.
    InsecureUserFile(PathBuf),
}

/// The API key the transport should send, plus where it came from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedKey {
    /// The key, when one was found and is safe to use.
    pub key: Option<ApiKey>,
    /// Where it came from (also set when a file was refused).
    pub source: KeySource,
}

/// Why the key file could not be used.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KeyFileError {
    /// The file grants group or other access, so it is never read.
    Insecure,
    /// Reading or writing the file failed; the string is the OS message.
    Io(String),
}

impl fmt::Display for KeyFileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Insecure => {
                f.write_str("the API key file must be readable by the owner only (0600)")
            }
            Self::Io(message) => f.write_str(message),
        }
    }
}

impl std::error::Error for KeyFileError {}

/// The key file that belongs to `settings_path`.
#[must_use]
pub fn api_key_path(settings_path: &Path) -> PathBuf {
    match settings_path.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent.join(API_KEY_FILE),
        _ => PathBuf::from(API_KEY_FILE),
    }
}

/// Resolve the FIM API key: environment first, then the owner-only user file.
///
/// `environment` is injected rather than read from the process, so this stays
/// pure and the callers decide what "the environment" means.
#[must_use]
pub fn resolve_api_key(settings_path: &Path, environment: Option<&str>) -> ResolvedKey {
    if let Some(raw) = environment {
        let key = ApiKey::new(raw.trim());
        if !key.is_empty() {
            return ResolvedKey {
                key: Some(key),
                source: KeySource::Environment,
            };
        }
    }
    let path = api_key_path(settings_path);
    match read_api_key_file(&path) {
        Ok(Some(key)) => ResolvedKey {
            key: Some(key),
            source: KeySource::UserFile(path),
        },
        Ok(None) => ResolvedKey {
            key: None,
            source: KeySource::None,
        },
        Err(KeyFileError::Insecure) => ResolvedKey {
            key: None,
            source: KeySource::InsecureUserFile(path),
        },
        Err(KeyFileError::Io(_)) => ResolvedKey {
            key: None,
            source: KeySource::None,
        },
    }
}

/// Read the user-level key file.
///
/// `Ok(None)` when the file does not exist or holds no key; `Err(Insecure)` when
/// its mode lets another user read it. The content is trimmed before use.
pub fn read_api_key_file(path: &Path) -> Result<Option<ApiKey>, KeyFileError> {
    let metadata = match std::fs::metadata(path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(KeyFileError::Io(format!("{}: {error}", path.display()))),
    };
    if !metadata.is_file() {
        return Err(KeyFileError::Io(format!(
            "{}: not a regular file",
            path.display()
        )));
    }
    if !owner_only(&metadata) {
        return Err(KeyFileError::Insecure);
    }
    let text = std::fs::read_to_string(path)
        .map_err(|error| KeyFileError::Io(format!("{}: {error}", path.display())))?;
    let key = ApiKey::new(text.trim());
    if key.is_empty() {
        return Ok(None);
    }
    Ok(Some(key))
}

/// Write the user-level key file owner-only **from creation**, atomically.
///
/// The file is created under a temporary name with `O_CREAT|O_EXCL` and mode
/// `0600` (on POSIX the mode is set at creation, so a restrictive umask can only
/// narrow it), its mode is verified **before** the first secret byte is written,
/// and it is then renamed over the target. The NEW secret therefore never reaches
/// a group/other-readable file; a pre-existing loose file keeps its old content
/// until it is atomically replaced. An empty key removes the file instead.
///
/// Residual window, stated plainly: a crash between creating the temp file and
/// the rename leaves a `0600` temp file that contains the secret (no group/other
/// access, but debris with a secret in it). The Qt panel sweeps stale
/// `fim_api_key.tmp.<pid>.<n>` files before its next save; this function only
/// cleans up its own temp on every failure path.
pub fn write_api_key_file(path: &Path, key: &ApiKey) -> Result<(), KeyFileError> {
    if key.is_empty() {
        return remove_api_key_file(path);
    }
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)
                .map_err(|error| KeyFileError::Io(format!("{}: {error}", parent.display())))?;
        }
    }
    let (temporary, mut file) = open_temporary(path)
        .map_err(|error| KeyFileError::Io(format!("{}: {error}", path.display())))?;
    let owner_only_at_creation = file_is_owner_only(&file).unwrap_or(false);
    if !owner_only_at_creation {
        drop(file);
        let _ = std::fs::remove_file(&temporary);
        return Err(KeyFileError::Insecure);
    }
    let write = (|| -> std::io::Result<()> {
        use std::io::Write;
        file.write_all(key.expose_secret().as_bytes())?;
        file.sync_all()
    })();
    drop(file);
    if let Err(error) = write {
        let _ = std::fs::remove_file(&temporary);
        return Err(KeyFileError::Io(format!(
            "{}: {error}",
            temporary.display()
        )));
    }
    if let Err(error) = std::fs::rename(&temporary, path) {
        let _ = std::fs::remove_file(&temporary);
        return Err(KeyFileError::Io(format!("{}: {error}", path.display())));
    }
    match std::fs::metadata(path) {
        Ok(metadata) if owner_only(&metadata) => Ok(()),
        _ => {
            let _ = std::fs::remove_file(path);
            Err(KeyFileError::Insecure)
        }
    }
}

/// Remove the user-level key file. Missing is success.
pub fn remove_api_key_file(path: &Path) -> Result<(), KeyFileError> {
    match std::fs::remove_file(path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(KeyFileError::Io(format!("{}: {error}", path.display()))),
    }
}

fn value<'a>(entries: &'a [Entry], key: &str) -> Option<&'a str> {
    entries
        .iter()
        .find(|entry| entry.key == key)
        .map(|entry| entry.value.as_str())
}

fn text(entries: &[Entry], key: &str) -> String {
    value(entries, key).unwrap_or_default().to_string()
}

fn list(entries: &[Entry], key: &str) -> Vec<String> {
    value(entries, key).map(lines).unwrap_or_default()
}

fn lines(value: &str) -> Vec<String> {
    value
        .split('\n')
        .map(|line| line.trim_end_matches('\r').trim())
        .filter(|line| !line.is_empty())
        .map(str::to_string)
        .collect()
}

fn flag(entries: &[Entry], key: &str, default: bool) -> bool {
    match value(entries, key).map(|text| text.trim().to_ascii_lowercase()) {
        Some(text) if matches!(text.as_str(), "true" | "1" | "yes" | "on") => true,
        Some(text) if matches!(text.as_str(), "false" | "0" | "no" | "off") => false,
        _ => default,
    }
}

fn optional_number(entries: &[Entry], key: &str) -> Option<u64> {
    value(entries, key).and_then(|text| text.trim().parse::<u64>().ok())
}

fn number(entries: &[Entry], key: &str, default: u64) -> u64 {
    value(entries, key)
        .and_then(|text| text.trim().parse::<u64>().ok())
        .unwrap_or(default)
}

/// A tri-state boolean: absent (and anything unparsable) is `None`, which the
/// consumers read as "send no flag, keep the tool's own default".
///
/// This is deliberately NOT [`flag`] with a hardcoded default: for a tool whose
/// own default is `true` (`--compact_indexing_and_selections`) an `Option`
/// keeps "the user did not choose" distinguishable from "the user chose false".
fn optional_flag(entries: &[Entry], key: &str) -> Option<bool> {
    match value(entries, key).map(|text| text.trim().to_ascii_lowercase()) {
        Some(text) if matches!(text.as_str(), "true" | "1" | "yes" | "on") => Some(true),
        Some(text) if matches!(text.as_str(), "false" | "0" | "no" | "off") => Some(false),
        _ => None,
    }
}

fn count(entries: &[Entry], key: &str, default: u32) -> u32 {
    value(entries, key)
        .and_then(|text| text.trim().parse::<u32>().ok())
        .unwrap_or(default)
}

#[cfg(unix)]
fn owner_only(metadata: &std::fs::Metadata) -> bool {
    use std::os::unix::fs::PermissionsExt;
    metadata.permissions().mode() & 0o077 == 0
}

#[cfg(not(unix))]
fn owner_only(_metadata: &std::fs::Metadata) -> bool {
    // No POSIX mode bits to check; the platform's ACL story is out of scope.
    true
}

/// Create a NEW file that is owner-only at creation (`O_CREAT|O_EXCL`, mode 0600).
///
/// `create_new` makes a pre-existing file an error instead of a silent reuse, so
/// a stale loose file is never written into.
#[cfg(unix)]
fn create_private(path: &Path) -> std::io::Result<std::fs::File> {
    use std::os::unix::fs::OpenOptionsExt;
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(API_KEY_FILE_MODE)
        .open(path)
}

#[cfg(not(unix))]
fn create_private(path: &Path) -> std::io::Result<std::fs::File> {
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
}

/// True when the OPEN file itself is owner-only (checked before writing).
#[cfg(unix)]
fn file_is_owner_only(file: &std::fs::File) -> std::io::Result<bool> {
    use std::os::unix::fs::PermissionsExt;
    Ok(file.metadata()?.permissions().mode() & 0o077 == 0)
}

#[cfg(not(unix))]
fn file_is_owner_only(_file: &std::fs::File) -> std::io::Result<bool> {
    Ok(true)
}

/// Open a fresh temporary file beside `path` (same directory: the rename must not
/// cross a filesystem). Retries only on a real name collision.
fn open_temporary(path: &Path) -> std::io::Result<(PathBuf, std::fs::File)> {
    let mut last: Option<std::io::Error> = None;
    for attempt in 0..8u32 {
        let mut name = path
            .file_name()
            .map(|name| name.to_os_string())
            .unwrap_or_default();
        name.push(format!(".tmp.{}.{attempt}", std::process::id()));
        let candidate = match path.parent() {
            Some(parent) => parent.join(name),
            None => PathBuf::from(name),
        };
        match create_private(&candidate) {
            Ok(file) => return Ok((candidate, file)),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => last = Some(error),
            Err(error) => return Err(error),
        }
    }
    Err(last.unwrap_or_else(|| std::io::Error::other("cannot create a temporary key file")))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(key: &str, value: &str) -> Entry {
        Entry {
            key: key.to_string(),
            value: value.to_string(),
        }
    }

    const SECRET: &str = "sk-test-DO-NOT-LEAK-123456";

    #[test]
    fn key_contract_is_stable() {
        assert_eq!(keys::LSP_BACKEND, "tools.lsp_backend");
        assert_eq!(keys::LINT_ENGINE, "tools.lint_engine");
        assert_eq!(keys::VERIBLE_LS, "tools.verible_ls");
        assert_eq!(keys::SLANG, "tools.slang");
        assert_eq!(keys::SLANG_SERVER, "tools.slang_server");
        assert_eq!(keys::VERIBLE_FORMAT, "tools.verible_format");
        assert_eq!(keys::VERIBLE_RULES, "tools.verible_rules");
        assert_eq!(keys::INCLUDE_DIRS, "tools.include_dirs");
        assert_eq!(keys::DEFINES, "tools.defines");
        assert_eq!(keys::FIM_ENABLED, "fim.enabled");
        assert_eq!(keys::FIM_PROTOCOL, "fim.protocol");
        assert_eq!(keys::FIM_ENDPOINT, "fim.endpoint");
        assert_eq!(keys::FIM_MODEL, "fim.model");
        assert_eq!(keys::FIM_TIMEOUT_MS, "fim.timeout_ms");
        assert_eq!(keys::FIM_MAX_TOKENS, "fim.max_tokens");
        assert_eq!(keys::FIM_GHOST_TEXT, "fim.ghost_text");
        assert_eq!(keys::UI_FONT_POINT_SIZE, "ui.font_point_size");
        assert_eq!(keys::UI_SCALE_PERCENT, "ui.scale_percent");
        assert_eq!(keys::FORMAT_COLUMN_LIMIT, "format.column_limit");
        assert_eq!(keys::FORMAT_INDENTATION_SPACES, "format.indentation_spaces");
        assert_eq!(keys::FORMAT_WRAP_SPACES, "format.wrap_spaces");
        assert_eq!(keys::FORMAT_LINE_TERMINATOR, "format.line_terminator");
        assert_eq!(
            keys::FORMAT_TRY_WRAP_LONG_LINES,
            "format.try_wrap_long_lines"
        );
        assert_eq!(
            keys::FORMAT_COMPACT_INDEXING_AND_SELECTIONS,
            "format.compact_indexing_and_selections"
        );
        assert_eq!(keys::LINT_VERIBLE_RULESET, "lint.verible.ruleset");
        assert_eq!(keys::LINT_VERIBLE_RULES, "lint.verible.rules");
        assert_eq!(keys::LINT_VERIBLE_RULES_CONFIG, "lint.verible.rules_config");
        assert_eq!(keys::LINT_VERIBLE_WAIVER_FILES, "lint.verible.waiver_files");
        assert_eq!(keys::LINT_VERIBLE_AUTOFIX, "lint.verible.autofix");
        assert_eq!(keys::LINT_SLANG_WARNINGS, "lint.slang.warnings");
        assert_eq!(keys::LINT_SLANG_ERROR_LIMIT, "lint.slang.error_limit");
        assert_eq!(API_KEY_ENV, "VEDIT_FIM_API_KEY");
        assert_eq!(API_KEY_FILE, "fim_api_key");
    }

    #[test]
    fn the_appearance_keys_default_to_follow_the_system() {
        let empty = UiSettings::from_entries(&[]);
        assert_eq!(empty.font_point_size, None);
        assert_eq!(empty.scale_percent, 100);
        assert_eq!(empty.scale_factor(), None);

        let chosen = UiSettings::from_entries(&[
            entry(keys::UI_FONT_POINT_SIZE, "13"),
            entry(keys::UI_SCALE_PERCENT, "150"),
        ]);
        assert_eq!(chosen.font_point_size, Some(13));
        assert_eq!(chosen.scale_percent, 150);
        assert_eq!(chosen.scale_factor(), Some("1.5"));

        // Out of set / out of range values degrade to the defaults instead of
        // starting the product at an arbitrary zoom.
        let degraded = UiSettings::from_entries(&[
            entry(keys::UI_FONT_POINT_SIZE, "500"),
            entry(keys::UI_SCALE_PERCENT, "1337"),
        ]);
        assert_eq!(degraded, UiSettings::default());
        assert_eq!(
            UiSettings::from_entries(&[entry(keys::UI_SCALE_PERCENT, "125")]).scale_factor(),
            Some("1.25")
        );
        assert_eq!(
            UiSettings::from_entries(&[entry(keys::UI_SCALE_PERCENT, "200")]).scale_factor(),
            Some("2")
        );
    }

    #[test]
    fn the_format_keys_are_absent_until_the_user_sets_them() {
        let empty = FormatSettings::from_entries(&[]);
        assert_eq!(empty.column_limit, None);
        assert_eq!(empty.indentation_spaces, None);
        assert_eq!(empty.wrap_spaces, None);
        assert_eq!(empty.line_terminator, None);
        assert_eq!(empty.try_wrap_long_lines, None);
        assert_eq!(empty.compact_indexing_and_selections, None);
        assert!(empty.alignments.is_empty());

        let chosen = FormatSettings::from_entries(&[
            entry(keys::FORMAT_COLUMN_LIMIT, "120"),
            entry(keys::FORMAT_INDENTATION_SPACES, "4"),
            entry(keys::FORMAT_WRAP_SPACES, "8"),
            entry(keys::FORMAT_LINE_TERMINATOR, "CRLF"),
            entry(keys::FORMAT_TRY_WRAP_LONG_LINES, "true"),
            entry(keys::FORMAT_COMPACT_INDEXING_AND_SELECTIONS, "false"),
            entry("format.assignment_statement_alignment", "flush-left"),
            entry("format.port_declarations_indentation", "indent"),
        ]);
        assert_eq!(chosen.column_limit, Some(120));
        assert_eq!(chosen.indentation_spaces, Some(4));
        assert_eq!(chosen.wrap_spaces, Some(8));
        assert_eq!(chosen.line_terminator, Some(FormatLineTerminator::Crlf));
        assert_eq!(chosen.try_wrap_long_lines, Some(true));
        assert_eq!(chosen.compact_indexing_and_selections, Some(false));
        assert_eq!(
            chosen.alignments.get("assignment_statement_alignment"),
            Some(&Alignment::FlushLeft)
        );
        // The `{indent,wrap}` knobs are stored verbatim for the Qt side and are
        // not part of the alignment projection.
        assert_eq!(chosen.alignments.len(), 1);

        // A lower-case spelling is tolerated but normalised to verible's own
        // exact spelling, so the argv can never carry a value it refuses.
        assert_eq!(
            FormatSettings::from_entries(&[entry(keys::FORMAT_LINE_TERMINATOR, "crlf")])
                .line_terminator
                .map(FormatLineTerminator::as_str),
            Some("CRLF")
        );
        // An unknown enum value is dropped, not stored: the formatter would
        // refuse it, so the page keeps "no flag".
        let invalid = FormatSettings::from_entries(&[
            entry(keys::FORMAT_LINE_TERMINATOR, "cr"),
            entry("format.case_items_alignment", "sometimes"),
            entry(keys::FORMAT_COLUMN_LIMIT, "0"),
        ]);
        assert_eq!(invalid.line_terminator, None);
        assert!(invalid.alignments.is_empty());
        assert_eq!(invalid.column_limit, None);
    }

    #[test]
    fn the_two_lint_configurations_are_independent_key_sets() {
        // Only the verible half is stored: the slang half keeps its defaults.
        let verible_only = LintSettings::from_entries(&[
            entry(keys::LINT_VERIBLE_RULESET, "all"),
            entry(keys::LINT_VERIBLE_RULES, "line-length,no-trailing-spaces"),
            entry(keys::LINT_VERIBLE_RULES_CONFIG, "/p/.rules.verible_lint"),
            entry(
                keys::LINT_VERIBLE_WAIVER_FILES,
                "/p/a.waiver\n/p/b.waiver\n",
            ),
            entry(keys::LINT_VERIBLE_AUTOFIX, "patch"),
        ]);
        assert_eq!(verible_only.verible.ruleset, Some(VeribleRuleset::All));
        assert_eq!(verible_only.verible.rules, "line-length,no-trailing-spaces");
        assert_eq!(verible_only.verible.rules_config, "/p/.rules.verible_lint");
        assert_eq!(
            verible_only.verible.waiver_files,
            vec!["/p/a.waiver", "/p/b.waiver"]
        );
        assert_eq!(verible_only.verible.autofix, Some(VeribleAutofix::Patch));
        assert_eq!(verible_only.slang, LintSlangSettings::default());

        // Only the slang half is stored: the verible half keeps its defaults,
        // i.e. switching the engine cannot lose the other set (R1).
        let slang_only = LintSettings::from_entries(&[
            entry(
                keys::LINT_SLANG_WARNINGS,
                "everything\nno-unused-variable\n",
            ),
            entry(keys::LINT_SLANG_ERROR_LIMIT, "25"),
        ]);
        assert_eq!(
            slang_only.slang.warnings,
            vec!["everything", "no-unused-variable"]
        );
        assert_eq!(slang_only.slang.error_limit, Some(25));
        assert_eq!(slang_only.verible, LintVeribleSettings::default());

        // An out-of-set enum degrades to "no flag", never to an invalid argv.
        let invalid = LintSettings::from_entries(&[
            entry(keys::LINT_VERIBLE_RULESET, "sometimes"),
            entry(keys::LINT_VERIBLE_AUTOFIX, "magic"),
            entry(keys::LINT_SLANG_ERROR_LIMIT, "99999999"),
        ]);
        assert_eq!(invalid.verible.ruleset, None);
        assert_eq!(invalid.verible.autofix, None);
        assert_eq!(invalid.slang.error_limit, None);
    }

    #[test]
    fn the_documented_flag_lists_match_the_measured_value_sets() {
        assert_eq!(
            Alignment::VALUES,
            ["align", "flush-left", "preserve", "infer"]
        );
        assert_eq!(FormatLineTerminator::VALUES, ["CRLF", "LF", "auto"]);
        assert_eq!(VeribleRuleset::VALUES, ["default", "all", "none"]);
        assert_eq!(VeribleAutofix::VALUES.len(), 6);
        for value in FormatLineTerminator::VALUES {
            assert_eq!(
                FormatLineTerminator::parse(value).map(FormatLineTerminator::as_str),
                Some(value)
            );
        }
        for value in VeribleAutofix::VALUES {
            assert_eq!(
                VeribleAutofix::parse(value).map(VeribleAutofix::as_str),
                Some(value)
            );
        }
        assert_eq!(FORMAT_ALIGNMENT_FLAGS.len(), 15);
        assert!(FORMAT_INDENTATION_FLAGS
            .iter()
            .all(|flag| FORMAT_ALIGNMENT_FLAGS.contains(flag)));
    }

    #[test]
    fn an_empty_store_yields_the_baseline_defaults() {
        let tools = ToolSettings::from_entries(&[]);
        assert_eq!(tools, ToolSettings::default());
        assert_eq!(tools.lsp_backend, LspBackend::Verible);
        assert_eq!(tools.lint_engine, LintEngine::Slang);
        assert!(tools.include_dirs.is_empty());
        assert!(tools.defines.is_empty());

        let fim = FimSettings::from_entries(&[]);
        assert_eq!(fim, FimSettings::default());
        assert!(!fim.enabled);
        assert_eq!(fim.timeout_ms, 5_000);
        assert_eq!(fim.max_tokens, 64);
        assert!(!fim.ghost_text);
    }

    #[test]
    fn backend_values_round_trip_and_unknown_values_keep_the_default() {
        for backend in [LspBackend::Verible, LspBackend::Slang] {
            assert_eq!(LspBackend::parse(backend.as_str()), Some(backend));
        }
        assert_eq!(LspBackend::parse(" SLANG "), Some(LspBackend::Slang));
        assert_eq!(LspBackend::parse("slang-server"), Some(LspBackend::Slang));
        assert_eq!(LspBackend::parse("emacs"), None);
        for engine in [LintEngine::Slang, LintEngine::Verible] {
            assert_eq!(LintEngine::parse(engine.as_str()), Some(engine));
        }
        assert_eq!(
            LintEngine::parse("verible-verilog-lint"),
            Some(LintEngine::Verible)
        );
        assert_eq!(LintEngine::parse("elf"), None);
        for protocol in [FimProtocol::OpenAi, FimProtocol::LlamaCpp] {
            assert_eq!(FimProtocol::parse(protocol.as_str()), Some(protocol));
        }
        assert_eq!(FimProtocol::parse("wat"), None);

        let tools = ToolSettings::from_entries(&[
            entry(keys::LSP_BACKEND, "emacs"),
            entry(keys::LINT_ENGINE, "verible"),
        ]);
        assert_eq!(tools.lsp_backend, LspBackend::Verible);
        assert_eq!(tools.lint_engine, LintEngine::Verible);
    }

    #[test]
    fn tool_paths_and_lists_are_read_verbatim() {
        let tools = ToolSettings::from_entries(&[
            entry(keys::VERIBLE_LS, "/opt/verible/bin/verible-verilog-ls"),
            entry(keys::SLANG, "/opt/slang/bin/slang"),
            entry(keys::SLANG_SERVER, "/opt/slang/bin/slang-server"),
            entry(
                keys::VERIBLE_FORMAT,
                "/opt/verible/bin/verible-verilog-format",
            ),
            entry(keys::VERIBLE_RULES, "/project/.rules.verible_lint"),
            entry(keys::INCLUDE_DIRS, "rtl\n\n rtl/include \r\nsim"),
            entry(keys::DEFINES, "WIDTH=8\nSIM"),
        ]);
        assert_eq!(tools.verible_ls, "/opt/verible/bin/verible-verilog-ls");
        assert_eq!(tools.slang, "/opt/slang/bin/slang");
        assert_eq!(tools.slang_server, "/opt/slang/bin/slang-server");
        assert_eq!(
            tools.verible_format,
            "/opt/verible/bin/verible-verilog-format"
        );
        assert_eq!(tools.verible_rules, "/project/.rules.verible_lint");
        assert_eq!(tools.include_dirs, vec!["rtl", "rtl/include", "sim"]);
        assert_eq!(tools.defines, vec!["WIDTH=8", "SIM"]);
    }

    #[test]
    fn fim_values_parse_and_unparsable_numbers_keep_the_default() {
        let fim = FimSettings::from_entries(&[
            entry(keys::FIM_ENABLED, " YES "),
            entry(keys::FIM_PROTOCOL, "llama_cpp"),
            entry(keys::FIM_ENDPOINT, "http://127.0.0.1:8080"),
            entry(keys::FIM_MODEL, "qwen"),
            entry(keys::FIM_TIMEOUT_MS, "2500"),
            entry(keys::FIM_MAX_TOKENS, "128"),
            entry(keys::FIM_GHOST_TEXT, "on"),
        ]);
        assert!(fim.enabled);
        assert_eq!(fim.protocol, FimProtocol::LlamaCpp);
        assert_eq!(fim.endpoint, "http://127.0.0.1:8080");
        assert_eq!(fim.model, "qwen");
        assert_eq!(fim.timeout_ms, 2_500);
        assert_eq!(fim.max_tokens, 128);
        assert!(fim.ghost_text);

        let broken = FimSettings::from_entries(&[
            entry(keys::FIM_ENABLED, "maybe"),
            entry(keys::FIM_TIMEOUT_MS, "soon"),
            entry(keys::FIM_MAX_TOKENS, "-1"),
        ]);
        assert!(!broken.enabled);
        assert_eq!(broken.timeout_ms, 5_000);
        assert_eq!(broken.max_tokens, 64);
    }

    #[test]
    fn a_plaintext_api_key_in_the_store_is_ignored_and_never_debug_printed() {
        let entries = [
            entry(keys::FIM_ENDPOINT, "http://127.0.0.1:8080"),
            entry("fim.api_key", SECRET),
        ];
        let fim = FimSettings::from_entries(&entries);
        assert_eq!(fim.endpoint, "http://127.0.0.1:8080");
        let rendered = format!("{fim:?}");
        assert!(!rendered.contains(SECRET), "the projection leaked the key");
        // Nothing in the typed projection can hold the secret.
        assert!(!rendered.contains("api_key") || !rendered.contains(SECRET));
    }

    #[test]
    fn api_key_debug_is_redacted() {
        let key = ApiKey::new(SECRET);
        let rendered = format!("{key:?}");
        assert_eq!(rendered, "ApiKey(<redacted>)");
        assert!(!rendered.contains(SECRET));
        assert_eq!(key.expose_secret(), SECRET);
    }

    #[test]
    fn the_environment_key_wins_over_the_user_file() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let settings = dir.path().join("settings.toml");
        let file_key = ApiKey::new("from-file");
        assert!(write_api_key_file(&api_key_path(&settings), &file_key).is_ok());

        let resolved = resolve_api_key(&settings, Some("from-env"));
        assert_eq!(resolved.source, KeySource::Environment);
        assert_eq!(
            resolved.key.as_ref().map(ApiKey::expose_secret),
            Some("from-env")
        );

        let resolved = resolve_api_key(&settings, None);
        assert_eq!(
            resolved.source,
            KeySource::UserFile(api_key_path(&settings))
        );
        assert_eq!(
            resolved.key.as_ref().map(ApiKey::expose_secret),
            Some("from-file")
        );

        let blank = resolve_api_key(&settings, Some("   "));
        assert_eq!(blank.source, KeySource::UserFile(api_key_path(&settings)));
    }

    #[test]
    fn a_missing_store_and_key_file_resolve_to_no_key() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let resolved = resolve_api_key(&dir.path().join("settings.toml"), None);
        assert_eq!(resolved.source, KeySource::None);
        assert!(resolved.key.is_none());
        assert_eq!(
            resolved.key, None,
            "a missing file must not fabricate a key"
        );
    }

    #[test]
    fn the_key_file_is_written_owner_only_and_can_be_removed() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let settings = dir.path().join("nested/settings.toml");
        let path = api_key_path(&settings);
        assert_eq!(path, dir.path().join("nested").join(API_KEY_FILE));
        assert!(write_api_key_file(&path, &ApiKey::new(SECRET)).is_ok());
        let read = read_api_key_file(&path);
        assert_eq!(
            read.as_ref()
                .ok()
                .and_then(|key| key.as_ref().map(ApiKey::expose_secret)),
            Some(SECRET)
        );
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let Ok(metadata) = std::fs::metadata(&path) else {
                return;
            };
            assert_eq!(metadata.permissions().mode() & 0o777, 0o600);
        }
        assert!(remove_api_key_file(&path).is_ok());
        assert!(!path.exists());
        assert!(remove_api_key_file(&path).is_ok());
    }

    #[test]
    fn a_successful_write_leaves_only_the_key_file_behind() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join(API_KEY_FILE);
        assert!(write_api_key_file(&path, &ApiKey::new(SECRET)).is_ok());
        let mut names: Vec<String> = Vec::new();
        if let Ok(read_dir) = std::fs::read_dir(dir.path()) {
            for item in read_dir.flatten() {
                names.push(item.file_name().to_string_lossy().into_owned());
            }
        }
        assert_eq!(names, vec![API_KEY_FILE.to_string()], "debris: {names:?}");
    }

    #[test]
    fn writing_an_empty_key_removes_the_file() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join(API_KEY_FILE);
        assert!(write_api_key_file(&path, &ApiKey::new("secret")).is_ok());
        assert!(path.exists());
        assert!(write_api_key_file(&path, &ApiKey::new("  ")).is_ok());
        assert!(!path.exists());
    }

    #[test]
    fn a_world_readable_key_file_is_refused_not_used() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let settings = dir.path().join("settings.toml");
        let path = api_key_path(&settings);
        assert!(std::fs::write(&path, SECRET).is_ok());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert!(
                std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o644)).is_ok()
            );
            assert_eq!(read_api_key_file(&path), Err(KeyFileError::Insecure));
            let resolved = resolve_api_key(&settings, None);
            assert_eq!(resolved.source, KeySource::InsecureUserFile(path.clone()));
            assert!(resolved.key.is_none());
            let rendered = format!("{resolved:?}");
            assert!(!rendered.contains(SECRET), "refusal leaked the key");
        }
        assert!(
            path.exists(),
            "a refused key file is left for the user to fix"
        );
    }

    #[test]
    fn an_existing_loose_key_file_is_replaced_by_an_owner_only_one() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join(API_KEY_FILE);
        assert!(std::fs::write(&path, "old").is_ok());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert!(
                std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o644)).is_ok()
            );
        }
        assert!(write_api_key_file(&path, &ApiKey::new(SECRET)).is_ok());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let Ok(metadata) = std::fs::metadata(&path) else {
                return;
            };
            assert_eq!(metadata.permissions().mode() & 0o777, 0o600);
        }
        let read = read_api_key_file(&path);
        assert_eq!(
            read.as_ref()
                .ok()
                .and_then(|key| key.as_ref().map(ApiKey::expose_secret)),
            Some(SECRET)
        );
    }
}
