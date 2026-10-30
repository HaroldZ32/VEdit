//! Stable, machine-checkable warning identifiers for config/project discovery.
//!
//! The string form of each code is part of the `--print-config` JSON contract:
//! reviewers and acceptance scripts assert on `code`, never on the message text.

use std::path::PathBuf;

/// Every warning this crate can emit.
///
/// The dotted strings are stable API: adding a variant is a compatible change,
/// renaming one is not.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, serde::Serialize)]
pub enum WarningCode {
    /// A `.f` line began with `+`/`-` but was not a construct we understand.
    #[serde(rename = "filelist.unknown_token")]
    FilelistUnknownToken,
    /// A nested `-f` pointed back into a filelist already being parsed.
    #[serde(rename = "filelist.cycle")]
    FilelistCycle,
    /// Nested `-f` recursion exceeded the depth limit.
    #[serde(rename = "filelist.depth_exceeded")]
    FilelistDepthExceeded,
    /// A filelist (top-level or nested) does not exist.
    #[serde(rename = "filelist.missing")]
    FilelistMissing,
    /// A filelist exists but could not be read.
    #[serde(rename = "filelist.unreadable")]
    FilelistUnreadable,
    /// A `+define+NAME[=VALUE]` token was malformed.
    #[serde(rename = "filelist.define_invalid")]
    FilelistDefineInvalid,
    /// The same source path appeared more than once; the duplicate was dropped.
    #[serde(rename = "filelist.duplicate_entry")]
    FilelistDuplicateEntry,
    /// The upward `.rules.verible_lint` search ended without a hit.
    #[serde(rename = "rules.search_stopped")]
    RulesSearchStopped,
    /// A `vedit.toml` was rejected (syntax or schema); lower layers were kept.
    #[serde(rename = "config.parse_failed")]
    ConfigParseFailed,
    /// A key is syntactically valid but not part of the known contract.
    #[serde(rename = "config.unknown_key")]
    ConfigUnknownKey,
    /// A value had the right TOML type but an unusable value (bad number, …).
    #[serde(rename = "config.invalid_value")]
    ConfigInvalidValue,
    /// An XDG variable was present but unusable (relative path, missing HOME).
    #[serde(rename = "xdg.invalid")]
    XdgInvalid,
}

impl WarningCode {
    /// The stable dotted string form.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::FilelistUnknownToken => "filelist.unknown_token",
            Self::FilelistCycle => "filelist.cycle",
            Self::FilelistDepthExceeded => "filelist.depth_exceeded",
            Self::FilelistMissing => "filelist.missing",
            Self::FilelistUnreadable => "filelist.unreadable",
            Self::FilelistDefineInvalid => "filelist.define_invalid",
            Self::FilelistDuplicateEntry => "filelist.duplicate_entry",
            Self::RulesSearchStopped => "rules.search_stopped",
            Self::ConfigParseFailed => "config.parse_failed",
            Self::ConfigUnknownKey => "config.unknown_key",
            Self::ConfigInvalidValue => "config.invalid_value",
            Self::XdgInvalid => "xdg.invalid",
        }
    }
}

impl std::fmt::Display for WarningCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

/// One non-fatal issue found while loading configuration or discovering a project.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Warning {
    /// Stable machine-checkable identifier.
    pub code: WarningCode,
    /// File the warning refers to, when the warning is file-scoped.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file: Option<PathBuf>,
    /// 1-based line in [`Self::file`], when known.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub line: Option<u32>,
    /// Human-readable explanation. Never contains configuration values that may
    /// be secret; this crate only interpolates paths and tokens it already knows
    /// are non-secret.
    pub message: String,
}

impl Warning {
    /// A warning without file/line context.
    pub fn new(code: WarningCode, message: impl Into<String>) -> Self {
        Self {
            code,
            file: None,
            line: None,
            message: message.into(),
        }
    }

    /// Attach the file the warning is about.
    pub fn with_file(mut self, file: impl Into<PathBuf>) -> Self {
        self.file = Some(file.into());
        self
    }

    /// Attach the 1-based line the warning is about.
    pub fn with_line(mut self, line: u32) -> Self {
        self.line = Some(line);
        self
    }
}
