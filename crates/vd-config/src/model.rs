//! The resolved configuration model (`vedit.toml` schema) and its defaults.
//!
//! Every section is `#[serde(default)]` so a partial file only overrides what it
//! names — that is what makes the user/project deep merge meaningful. Unknown
//! keys are rejected (`deny_unknown_fields`), which turns a typo into one
//! observable `config.parse_failed` warning instead of a silently ignored key.

use std::collections::BTreeMap;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::secret::ApiKey;

/// Default UI language: Simplified Chinese, as recorded in the requirements.
pub const DEFAULT_LANGUAGE: &str = "zh-CN";
/// Default GtkSourceView style scheme (verified to colour SystemVerilog keywords).
pub const DEFAULT_THEME: &str = "cobalt";

/// Default key bindings. The requirements make these rebindable via `[keys]`;
/// this table is the baseline the configuration starts from.
pub const DEFAULT_KEY_BINDINGS: &[(&str, &str)] = &[
    ("format", "Ctrl+Shift+F"),
    ("deep_lint", "Ctrl+Shift+L"),
    ("goto_definition", "F12"),
    ("find_references", "Shift+F12"),
    ("symbol_outline", "Ctrl+Shift+O"),
    ("fim_accept", "Tab"),
    ("fim_cancel", "Esc"),
    ("fim_manual", "Alt+\\"),
];

/// The complete resolved configuration.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    /// Interface preferences.
    pub ui: UiConfig,
    /// Editing behaviour.
    pub editor: EditorConfig,
    /// Rebindable shortcuts.
    pub keys: KeysConfig,
    /// Absolute paths to the bundled Verible tools (empty means "not configured").
    pub verible: VeribleConfig,
    /// Project-level discovery inputs.
    pub project: ProjectSettings,
    /// Optional FIM completion settings. Off by default, no default endpoint.
    pub fim: FimConfig,
}

/// Interface preferences.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct UiConfig {
    /// UI language tag. Defaults to Simplified Chinese.
    pub language: String,
    /// GtkSourceView style scheme id.
    pub theme: String,
}

impl Default for UiConfig {
    fn default() -> Self {
        Self {
            language: DEFAULT_LANGUAGE.to_string(),
            theme: DEFAULT_THEME.to_string(),
        }
    }
}

/// Editing behaviour.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct EditorConfig {
    /// Number of columns a tab occupies.
    pub tab_width: u32,
    /// Insert spaces instead of tab characters.
    pub insert_spaces: bool,
    /// Show the line-number gutter.
    pub show_line_numbers: bool,
    /// Highlight the line that holds the cursor.
    pub highlight_current_line: bool,
}

impl Default for EditorConfig {
    fn default() -> Self {
        Self {
            tab_width: 4,
            insert_spaces: true,
            show_line_numbers: true,
            highlight_current_line: false,
        }
    }
}

/// Rebindable shortcuts, keyed by action name.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct KeysConfig(pub BTreeMap<String, String>);

impl Default for KeysConfig {
    fn default() -> Self {
        Self(
            DEFAULT_KEY_BINDINGS
                .iter()
                .map(|(action, binding)| ((*action).to_string(), (*binding).to_string()))
                .collect(),
        )
    }
}

impl KeysConfig {
    /// The configured binding for `action`, if any.
    pub fn binding(&self, action: &str) -> Option<&str> {
        self.0.get(action).map(String::as_str)
    }
}

/// Verible tool overrides. `None` means "resolve through the chain".
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct VeribleConfig {
    /// Path to `verible-verilog-ls`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ls: Option<PathBuf>,
    /// Path to `verible-verilog-format`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub format: Option<PathBuf>,
    /// Path to `verible-verilog-lint`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub lint: Option<PathBuf>,
}

/// Project discovery inputs from `[project]`.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct ProjectSettings {
    /// Extra filelists, relative to the project root (or absolute).
    pub filelists: Vec<PathBuf>,
    /// Extra include directories, relative to the project root (or absolute).
    pub include_dirs: Vec<PathBuf>,
    /// Extra defines, `NAME` or `NAME=VALUE`.
    pub defines: Vec<String>,
}

/// FIM completion settings. Defaults: disabled, no endpoint, no key.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct FimConfig {
    /// Enable FIM completion. Defaults to `false`.
    pub enabled: bool,
    /// Wire protocol of the endpoint.
    pub protocol: FimProtocol,
    /// Base endpoint. Empty means "not configured" (no default endpoint).
    pub endpoint: String,
    /// Model name. Empty means "not configured".
    pub model: String,
    /// Request timeout in milliseconds.
    pub timeout_ms: u64,
    /// Secret API key. Redacted in `Debug` and in every serialized form.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub api_key: Option<ApiKey>,
}

impl Default for FimConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            protocol: FimProtocol::default(),
            endpoint: String::new(),
            model: String::new(),
            timeout_ms: 5_000,
            api_key: None,
        }
    }
}

/// Supported FIM endpoint protocols.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FimProtocol {
    /// OpenAI-compatible `/v1/completions` with `prompt` + `suffix`.
    #[default]
    OpenAi,
    /// llama.cpp `/infill` with `input_prefix` + `input_suffix`.
    LlamaCpp,
}

#[cfg(test)]
mod tests {
    use super::{Config, FimProtocol};

    #[test]
    fn defaults_match_the_requirements() {
        let config = Config::default();
        assert_eq!(config.ui.language, "zh-CN");
        assert_eq!(config.ui.theme, "cobalt");
        assert_eq!(config.editor.tab_width, 4);
        assert!(config.editor.show_line_numbers);
        assert_eq!(config.keys.binding("format"), Some("Ctrl+Shift+F"));
        assert_eq!(config.keys.binding("deep_lint"), Some("Ctrl+Shift+L"));
        assert!(!config.fim.enabled, "FIM must default to off");
        assert!(config.fim.endpoint.is_empty(), "no default endpoint");
        assert!(config.fim.model.is_empty(), "no default model");
        assert!(config.fim.api_key.is_none(), "no default key");
        assert_eq!(config.fim.protocol, FimProtocol::OpenAi);
        assert_eq!(config.fim.timeout_ms, 5_000);
        assert!(config.verible.ls.is_none());
        assert!(config.project.filelists.is_empty());
    }

    #[test]
    fn a_partial_document_keeps_the_defaults_for_absent_keys(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let config: Config = toml::from_str("[editor]\ntab_width = 2\n")?;
        assert_eq!(config.editor.tab_width, 2);
        assert!(config.editor.insert_spaces);
        assert_eq!(config.ui.language, "zh-CN");
        assert_eq!(config.keys.binding("goto_definition"), Some("F12"));
        Ok(())
    }
}
