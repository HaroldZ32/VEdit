//! Startup configuration: one place that loads `vd-config`, exposes the tool
//! paths and maps the config types onto the lane types (FIM settings, editor
//! settings, language load paths). No lane logic is re-implemented here.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use vd_config::{Env, Loaded, Loader};
use vd_fim::{ApiKey, FimSettings, Protocol, DEFAULT_MAX_TOKENS};

/// Editor defaults from `[editor]`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditorSettings {
    pub tab_width: u32,
    pub insert_spaces: bool,
    pub show_line_numbers: bool,
    pub highlight_current_line: bool,
}

impl Default for EditorSettings {
    fn default() -> Self {
        Self {
            tab_width: 4,
            insert_spaces: true,
            show_line_numbers: true,
            highlight_current_line: true,
        }
    }
}

/// Resolved tool paths (each keeps its attempt list; `PATH` is never consulted).
#[derive(Debug, Clone, Default)]
pub struct ToolPaths {
    pub ls: Option<PathBuf>,
    pub format: Option<PathBuf>,
    pub lint: Option<PathBuf>,
    pub attempts: Vec<String>,
}

/// Everything the app needs from the environment at startup.
#[derive(Debug, Clone)]
pub struct Startup {
    pub loaded: Option<Loaded>,
    pub config_error: Option<String>,
    pub editor: EditorSettings,
    pub keys: BTreeMap<String, String>,
    pub tools: ToolPaths,
    pub rules_path: Option<PathBuf>,
    pub workspace_root: PathBuf,
    pub fim_settings: FimSettings,
    pub warnings: Vec<String>,
    pub language_warning: Option<String>,
    pub slang_binary: Option<PathBuf>,
}

impl Startup {
    /// Load the configuration for `cwd`, resolving tools relative to `exe_dir`.
    ///
    /// Must be called after GTK is initialised (the language check constructs a
    /// `GtkSourceLanguageManager`).
    #[must_use]
    pub fn load(cwd: &Path, exe_dir: &Path) -> Self {
        let loader = Loader::new(cwd)
            .with_env(Env::from_process())
            .with_exe_dir(exe_dir);
        match loader.load() {
            Ok(loaded) => Self::from_loaded(loaded),
            Err(error) => Self {
                loaded: None,
                config_error: Some(format!("{error}")),
                editor: EditorSettings::default(),
                keys: BTreeMap::new(),
                tools: ToolPaths::default(),
                rules_path: None,
                workspace_root: cwd.to_path_buf(),
                fim_settings: FimSettings::disabled(),
                warnings: Vec::new(),
                language_warning: language_warning(),
                slang_binary: slang_binary_from_env(),
            },
        }
    }

    fn from_loaded(loaded: Loaded) -> Self {
        let editor = EditorSettings {
            tab_width: loaded.config.editor.tab_width,
            insert_spaces: loaded.config.editor.insert_spaces,
            show_line_numbers: loaded.config.editor.show_line_numbers,
            highlight_current_line: loaded.config.editor.highlight_current_line,
        };
        let mut attempts = Vec::new();
        for tool in [&loaded.tools.ls, &loaded.tools.format, &loaded.tools.lint] {
            for attempt in &tool.attempts {
                attempts.push(format!(
                    "{}: {} [{}]",
                    attempt.source.as_str(),
                    attempt.path.display(),
                    if attempt.found { "found" } else { "not found" }
                ));
            }
        }
        let fim_settings = map_fim(&loaded.config.fim);
        Self {
            editor,
            keys: loaded.config.keys.0.clone(),
            tools: ToolPaths {
                ls: loaded.tools.ls.path.clone(),
                format: loaded.tools.format.path.clone(),
                lint: loaded.tools.lint.path.clone(),
                attempts,
            },
            rules_path: loaded.project.rules_path.clone(),
            workspace_root: loaded.project.root.clone(),
            fim_settings,
            warnings: loaded
                .warnings
                .iter()
                .map(|warning| format!("{warning:?}"))
                .collect(),
            language_warning: language_warning(),
            slang_binary: slang_binary_from_env(),
            loaded: Some(loaded),
            config_error: None,
        }
    }
}

fn map_fim(config: &vd_config::FimConfig) -> FimSettings {
    let protocol = match config.protocol {
        vd_config::FimProtocol::OpenAi => Protocol::OpenAi,
        vd_config::FimProtocol::LlamaCpp => Protocol::LlamaCpp,
    };
    FimSettings {
        enabled: config.enabled,
        protocol,
        endpoint: config.endpoint.clone(),
        model: config.model.clone(),
        timeout_ms: config.timeout_ms,
        max_tokens: DEFAULT_MAX_TOKENS,
        api_key: config
            .api_key
            .as_ref()
            .map(|key| ApiKey::new(key.expose_secret())),
    }
}

fn slang_binary_from_env() -> Option<PathBuf> {
    std::env::var_os("VEDIT_SLANG").map(PathBuf::from)
}

/// AC-05/startup warning: the Verilog/SystemVerilog language definitions must be
/// available, otherwise highlighting would silently do nothing.
#[must_use]
pub fn language_warning() -> Option<String> {
    let manager = vd_syntax::language_manager();
    let missing = vd_syntax::missing_languages(&manager);
    if missing.is_empty() {
        return None;
    }
    Some(format!(
        "language definitions missing: {missing:?} (highlighting will not work)"
    ))
}

/// The key binding for an action from the config, with the contract defaults as
/// fallback.
#[must_use]
pub fn key_binding(keys: &BTreeMap<String, String>, action: &str, fallback: &str) -> String {
    keys.get(action)
        .cloned()
        .unwrap_or_else(|| fallback.to_string())
}

/// Convert a key binding into a GTK accelerator.
///
/// Accepts both the config's GTK form (`<Ctrl>o`, `<Shift>F12`) and a human form
/// (`Ctrl+Shift+F`, `F12`): modifiers are preserved, the key token is normalised
/// (single characters lowercased, function keys uppercased, named keys kept).
#[must_use]
pub fn gtk_accel(binding: &str) -> String {
    let binding = binding.trim();
    if let Some(close) = binding.rfind('>') {
        let modifiers = &binding[..=close];
        let key = normalize_key(&binding[close + 1..]);
        return format!("{modifiers}{key}");
    }
    let mut modifiers = String::new();
    let mut key = String::new();
    for token in binding.split('+') {
        match token.trim() {
            "Ctrl" | "Control" => modifiers.push_str("<Ctrl>"),
            "Shift" => modifiers.push_str("<Shift>"),
            "Alt" => modifiers.push_str("<Alt>"),
            "Super" | "Meta" => modifiers.push_str("<Super>"),
            "" => {}
            other => key = normalize_key(other),
        }
    }
    format!("{modifiers}{key}")
}

fn normalize_key(key: &str) -> String {
    if key.is_empty() {
        return String::new();
    }
    if key == "\\" {
        return "backslash".to_string();
    }
    if key.chars().count() == 1 {
        return key.to_lowercase();
    }
    match key {
        "Tab" => "Tab".to_string(),
        "Esc" | "Escape" => "Escape".to_string(),
        "Enter" | "Return" => "Return".to_string(),
        "Space" => "space".to_string(),
        "Backslash" => "backslash".to_string(),
        other if other.starts_with('F') && other[1..].chars().all(|c| c.is_ascii_digit()) => {
            other.to_string()
        }
        other => other.to_lowercase(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_binding_falls_back_to_the_default() {
        let mut keys = BTreeMap::new();
        keys.insert("format".to_string(), "Ctrl+Shift+F".to_string());
        assert_eq!(key_binding(&keys, "format", "F2"), "Ctrl+Shift+F");
        assert_eq!(key_binding(&keys, "missing", "F2"), "F2");
    }

    #[test]
    fn accelerators_are_converted_for_gtk() {
        assert_eq!(gtk_accel("Ctrl+Shift+F"), "<Ctrl><Shift>f");
        assert_eq!(gtk_accel("F12"), "F12");
        assert_eq!(gtk_accel("Alt+\\"), "<Alt>backslash");
        assert_eq!(gtk_accel("<Ctrl>o"), "<Ctrl>o");
        assert_eq!(gtk_accel("<Shift>F12"), "<Shift>F12");
    }

    #[test]
    fn editor_defaults_match_the_config_defaults() {
        let defaults = EditorSettings::default();
        assert_eq!(defaults.tab_width, 4);
        assert!(defaults.show_line_numbers);
    }
}
