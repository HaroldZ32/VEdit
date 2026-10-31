//! Project and configuration discovery for VEdit.
//!
//! # Precedence (observable)
//!
//! Values are layered in this order, later layers overriding earlier ones:
//!
//! 1. **defaults** — the built-in table in [`model`];
//! 2. **user config** — `$XDG_CONFIG_HOME/vedit/vedit.toml` (default
//!    `$HOME/.config/vedit/vedit.toml`);
//! 3. **project config** — `<project root>/vedit.toml` (found by walking up from
//!    the discovery start directory);
//! 4. **environment** — the `VEDIT_FIM_*` variables (FIM settings only).
//!
//! Every key records which layer set it and which layers it shadowed; see
//! [`provenance::Entry`] and [`render::render_table`].
//!
//! # Secret discipline
//!
//! Secret configuration is modelled by [`secret::ApiKey`], which has a redacted
//! `Debug`/`Serialize` and no `Display`, and is projected into
//! [`provenance::ValueView::Redacted`] for both renderings. Secret plaintext is
//! therefore absent from `--print-config`, from `{:?}` output and from warnings
//! by construction.
//!
//! # Example
//!
//! ```
//! # fn main() -> anyhow::Result<()> {
//! use vd_config::{Env, Loader};
//! use std::path::PathBuf;
//!
//! let env = Env::from_pairs([("HOME", "/home/alice")]);
//! let loaded = Loader::new("/home/alice/project")
//!     .with_env(env)
//!     .with_exe_dir(PathBuf::from("/opt/vedit/usr/bin"))
//!     .load()?;
//! assert_eq!(loaded.config.ui.language, "zh-CN");
//! # Ok(())
//! # }
//! ```

#![forbid(unsafe_code)]

pub mod env;
pub mod filelist;
pub mod model;
pub mod project;
pub mod provenance;
pub mod render;
pub mod secret;
pub mod tools;
pub mod warnings;

#[cfg(test)]
mod test_support;

use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

pub use env::{Env, XdgDirs};
pub use model::{
    Config, EditorConfig, FimConfig, FimProtocol, KeysConfig, ProjectSettings, UiConfig,
    VeribleConfig, DEFAULT_KEY_BINDINGS, DEFAULT_LANGUAGE, DEFAULT_THEME, KNOWN_BINDING_ACTIONS,
};
pub use project::{
    Define, Filelist, ProjectContext, RootSource, CONFIG_FILE_NAME, VERIBLE_RULES_FILE_NAME,
};
pub use provenance::{Entry, Source, ValueView};
pub use render::{render_table, to_json, SCHEMA_VERSION};
pub use secret::ApiKey;
pub use tools::{resolve_verible, Attempt, ResolvedTool, ToolSource, ToolsResolution, VeribleTool};
pub use warnings::{Warning, WarningCode};

/// Version of this crate, from its manifest.
pub const CRATE_VERSION: &str = env!("CARGO_PKG_VERSION");

/// Environment variable for the FIM endpoint.
const ENV_FIM_ENDPOINT: &str = "VEDIT_FIM_ENDPOINT";
/// Environment variable for the FIM model.
const ENV_FIM_MODEL: &str = "VEDIT_FIM_MODEL";
/// Environment variable for the FIM API key (secret).
const ENV_FIM_API_KEY: &str = "VEDIT_FIM_API_KEY";
/// Environment variable for the FIM protocol.
const ENV_FIM_PROTOCOL: &str = "VEDIT_FIM_PROTOCOL";
/// Environment variable for the FIM timeout.
const ENV_FIM_TIMEOUT_MS: &str = "VEDIT_FIM_TIMEOUT_MS";
/// Environment variable for the FIM on/off switch.
const ENV_FIM_ENABLED: &str = "VEDIT_FIM_ENABLED";

/// Loads the configuration layers and the project context for one directory.
///
/// `Loader::new(start_dir)`: **`start_dir` is the origin of the upward project
/// discovery** — the directory that holds the file the editor just opened, as
/// passed by the application layer (`vd-app`). It is *not* the process working
/// directory. The root is the nearest ancestor containing `vedit.toml`, `.git`,
/// or a `.f` filelist directly (a folder that ships its own filelists is a
/// project even inside a larger checkout); when nothing marks a root,
/// `start_dir` itself is used and [`ProjectContext::root_source`] says
/// `fallback`.
#[derive(Debug, Clone)]
pub struct Loader {
    start_dir: PathBuf,
    env: Env,
    xdg: XdgDirs,
    exe_dir: Option<PathBuf>,
}

impl Loader {
    /// Start a load rooted at `start_dir` (see the type-level documentation).
    pub fn new(start_dir: impl Into<PathBuf>) -> Self {
        let start_dir = start_dir.into();
        let env = Env::from_process();
        let xdg = XdgDirs::from_env(&env);
        Self {
            start_dir,
            env,
            xdg,
            exe_dir: None,
        }
    }

    /// Replace the environment (recomputes XDG directories from it).
    pub fn with_env(mut self, env: Env) -> Self {
        self.xdg = XdgDirs::from_env(&env);
        self.env = env;
        self
    }

    /// Replace the XDG directories explicitly (after [`Self::with_env`]).
    pub fn with_xdg(mut self, xdg: XdgDirs) -> Self {
        self.xdg = xdg;
        self
    }

    /// Set the directory that holds the running executable, for bundled tool
    /// resolution. Defaults to the real executable's directory.
    pub fn with_exe_dir(mut self, exe_dir: impl Into<PathBuf>) -> Self {
        self.exe_dir = Some(exe_dir.into());
        self
    }

    /// Load everything.
    ///
    /// Unreadable or invalid configuration files degrade to a warning plus the
    /// next lower layer; they never make the editor unusable.
    pub fn load(self) -> Result<Loaded> {
        let Self {
            start_dir,
            env,
            xdg,
            exe_dir,
        } = self;
        let mut warnings: Vec<Warning> = Vec::new();

        for name in XdgDirs::variable_names() {
            if let Some(raw) = XdgDirs::invalid_override(&env, name) {
                warnings.push(Warning::new(
                    WarningCode::XdgInvalid,
                    format!("{name} is not an absolute path (`{raw}`); using the default"),
                ));
            }
        }
        if xdg.config_home.is_none() {
            warnings.push(Warning::new(
                WarningCode::XdgInvalid,
                "HOME is unset and XDG_CONFIG_HOME is unusable; no user-level configuration is read",
            ));
        }

        let mut provenance = provenance::Provenance::default();
        provenance.record_layer(
            Source::Default,
            &serde_json::to_value(Config::default()).unwrap_or(serde_json::Value::Null),
        );
        let mut merged = serde_json::Value::Object(serde_json::Map::new());

        // Layer 2: user configuration.
        if let Some(user_file) = xdg.config_file() {
            if user_file.is_file() {
                apply_config_file(
                    &user_file,
                    Source::User,
                    &mut merged,
                    &mut provenance,
                    &mut warnings,
                );
            }
        }

        // Layer 3: project configuration (needs the discovered project root).
        let (root, root_source, search_stop) = project::discover_root(&start_dir);
        let project_file = root.join(CONFIG_FILE_NAME);
        if project_file.is_file() {
            apply_config_file(
                &project_file,
                Source::Project,
                &mut merged,
                &mut provenance,
                &mut warnings,
            );
        }

        // Layer 4: environment overrides (FIM only).
        let env_layer = env_layer(&env, &mut warnings);
        if env_layer.as_object().is_some_and(|layer| !layer.is_empty()) {
            provenance.record_layer(Source::Env, &env_layer);
            merge_json(&mut merged, &env_layer);
        }

        let mut config: Config = serde_json::from_value(merged).context(
            "internal error: merged configuration layers no longer form a valid document",
        )?;
        resolve_configured_paths(&mut config, &root);
        if config.fim.api_key.as_ref().is_some_and(ApiKey::is_empty) {
            config.fim.api_key = None;
        }
        warn_unknown_actions(&config, &mut warnings);

        let project = project::discover_in_root(
            root.clone(),
            root_source,
            search_stop,
            &config.project,
            env.home().as_deref(),
            &mut warnings,
        );
        let exe_dir = exe_dir
            .or_else(|| {
                std::env::current_exe()
                    .ok()
                    .and_then(|exe| exe.parent().map(Path::to_path_buf))
            })
            .unwrap_or_else(|| PathBuf::from("."));
        let tools = ToolsResolution::resolve(&config, &env, &exe_dir);

        let mut entries = provenance.into_entries();
        sync_verible_provenance(&config, &mut entries);

        Ok(Loaded {
            config,
            project,
            entries,
            warnings,
            tools,
        })
    }
}

/// A fully resolved configuration plus its project context and provenance.
#[derive(Debug, Clone)]
pub struct Loaded {
    /// The resolved configuration values.
    pub config: Config,
    /// The discovered project context.
    pub project: ProjectContext,
    /// Per-key provenance, key-sorted.
    pub entries: Vec<Entry>,
    /// Non-fatal issues collected along the way.
    pub warnings: Vec<Warning>,
    /// Verible tool resolution.
    pub tools: ToolsResolution,
}

/// Resolve configured Verible paths against the project root so a relative
/// `[verible] ls = "bin/verible-verilog-ls"` is usable and not cwd-dependent.
fn resolve_configured_paths(config: &mut Config, root: &Path) {
    for path in [
        config.verible.ls.as_mut(),
        config.verible.format.as_mut(),
        config.verible.lint.as_mut(),
    ]
    .into_iter()
    .flatten()
    {
        if path.is_relative() {
            *path = root.join(&*path);
        }
    }
}

/// Reflect the resolved Verible paths back into the provenance table.
fn sync_verible_provenance(config: &Config, entries: &mut [Entry]) {
    let resolved = [
        ("verible.ls", config.verible.ls.as_deref()),
        ("verible.format", config.verible.format.as_deref()),
        ("verible.lint", config.verible.lint.as_deref()),
    ];
    for (key, path) in resolved {
        let Some(path) = path else {
            continue;
        };
        if let Some(entry) = entries.iter_mut().find(|entry| entry.key == key) {
            entry.value = ValueView::Text {
                text: path.display().to_string(),
            };
        }
    }
}

/// Read, validate and merge one configuration file as a layer.
fn apply_config_file(
    path: &Path,
    source: Source,
    merged: &mut serde_json::Value,
    provenance: &mut provenance::Provenance,
    warnings: &mut Vec<Warning>,
) {
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(_) => {
            warnings.push(
                Warning::new(
                    WarningCode::ConfigParseFailed,
                    "configuration file could not be read; lower layers kept",
                )
                .with_file(path.to_path_buf()),
            );
            return;
        }
    };
    match parse_layer(&text) {
        Ok(parsed) => {
            for (old, new) in &parsed.renamed_keys {
                warnings.push(
                    Warning::new(
                        WarningCode::ConfigUnknownKey,
                        format!("configuration key `keys.{old}` was renamed to `{new}`; ignored"),
                    )
                    .with_file(path.to_path_buf()),
                );
            }
            for key in &parsed.unknown_keys {
                warnings.push(
                    Warning::new(
                        WarningCode::ConfigUnknownKey,
                        format!("unknown configuration key `{key}`; ignored"),
                    )
                    .with_file(path.to_path_buf()),
                );
            }
            provenance.record_layer(source, &parsed.value);
            merge_json(merged, &parsed.value);
        }
        Err(error) => {
            let mut warning = Warning::new(
                WarningCode::ConfigParseFailed,
                format!(
                    "configuration file rejected ({}); lower layers kept",
                    error.message
                ),
            )
            .with_file(path.to_path_buf());
            if let Some(line) = error.line {
                warning = warning.with_line(line);
            }
            warnings.push(warning);
        }
    }
}

struct LayerError {
    line: Option<u32>,
    message: &'static str,
}

struct ParsedLayer {
    value: serde_json::Value,
    unknown_keys: Vec<String>,
    renamed_keys: Vec<(String, String)>,
}

/// Known configuration layout: section -> scalar keys. `keys` accepts any
/// action name (it is a user-extensible binding map), everything else is fixed.
const KNOWN_SECTIONS: &[(&str, &[&str])] = &[
    ("ui", &["language", "theme"]),
    (
        "editor",
        &[
            "tab_width",
            "insert_spaces",
            "show_line_numbers",
            "highlight_current_line",
        ],
    ),
    ("keys", &[]),
    ("verible", &["ls", "format", "lint"]),
    ("project", &["filelists", "include_dirs", "defines"]),
    (
        "fim",
        &[
            "enabled",
            "protocol",
            "endpoint",
            "model",
            "timeout_ms",
            "api_key",
        ],
    ),
];

/// Parse a TOML layer, strip unknown keys (each reported), then validate.
///
/// Syntax errors carry a line number. Schema errors are reported without the
/// underlying error text, because TOML/serde messages can quote offending
/// values and a quoted value may be a secret.
fn parse_layer(text: &str) -> Result<ParsedLayer, LayerError> {
    let value: toml::Value = toml::from_str(text).map_err(|error: toml::de::Error| LayerError {
        line: toml_error_line(text, &error),
        message: "invalid TOML syntax",
    })?;
    let mut json = serde_json::to_value(value).map_err(|_| LayerError {
        line: None,
        message: "the TOML document cannot be represented as JSON",
    })?;
    let stripped = strip_unknown_keys(&mut json);
    let _validated: Config = serde_json::from_value(json.clone()).map_err(|_| LayerError {
        line: None,
        message: "configuration schema mismatch",
    })?;
    Ok(ParsedLayer {
        value: json,
        unknown_keys: stripped.unknown,
        renamed_keys: stripped.renamed,
    })
}

/// What `strip_unknown_keys` removed from one layer.
struct StrippedKeys {
    /// Dotted paths that are not part of the contract at all.
    unknown: Vec<String>,
    /// Legacy `[keys]` names and the action they were renamed to.
    renamed: Vec<(String, String)>,
}

/// Remove unknown sections/keys, returning their dotted paths.
fn strip_unknown_keys(value: &mut serde_json::Value) -> StrippedKeys {
    let mut unknown = Vec::new();
    let mut renamed = Vec::new();
    let Some(root) = value.as_object_mut() else {
        return StrippedKeys { unknown, renamed };
    };
    root.retain(|section, child| {
        let Some((name, known)) = KNOWN_SECTIONS
            .iter()
            .find(|(name, _)| name == section)
            .map(|(name, known)| (*name, *known))
        else {
            unknown.push(section.clone());
            return false;
        };
        if name == "keys" {
            if let Some(map) = child.as_object_mut() {
                map.retain(|action, _| {
                    if crate::model::KNOWN_BINDING_ACTIONS.contains(&action.as_str()) {
                        return true;
                    }
                    if let Some(replacement) = crate::model::renamed_binding(action) {
                        renamed.push((action.clone(), replacement.to_string()));
                    } else {
                        unknown.push(format!("keys.{action}"));
                    }
                    false
                });
                // A layer whose every [keys] entry was dropped must not replace
                // the defaults with an empty map.
                if map.is_empty() {
                    return false;
                }
            }
            return true;
        }
        let Some(map) = child.as_object_mut() else {
            return true;
        };
        map.retain(|key, _| {
            let keep = known.contains(&key.as_str());
            if !keep {
                unknown.push(format!("{section}.{key}"));
            }
            keep
        });
        true
    });
    StrippedKeys { unknown, renamed }
}

/// 1-based line of a TOML error, when its span is available.
fn toml_error_line(text: &str, error: &toml::de::Error) -> Option<u32> {
    let span = error.span()?;
    let offset = span.start.min(text.len());
    Some(text[..offset].bytes().filter(|byte| *byte == b'\n').count() as u32 + 1)
}

/// Environment overrides, FIM settings only.
fn env_layer(env: &Env, warnings: &mut Vec<Warning>) -> serde_json::Value {
    let mut fim = serde_json::Map::new();
    insert_env_string(&mut fim, env, "endpoint", ENV_FIM_ENDPOINT);
    insert_env_string(&mut fim, env, "model", ENV_FIM_MODEL);
    insert_env_string(&mut fim, env, "api_key", ENV_FIM_API_KEY);
    if let Some(raw) = non_empty(env, ENV_FIM_PROTOCOL) {
        let protocol = match raw {
            "openai" => Some("open_ai"),
            "llamacpp" => Some("llama_cpp"),
            other => {
                warnings.push(Warning::new(
                    WarningCode::ConfigInvalidValue,
                    format!(
                        "{ENV_FIM_PROTOCOL} must be `openai` or `llamacpp`, got `{other}`; ignored"
                    ),
                ));
                None
            }
        };
        if let Some(protocol) = protocol {
            fim.insert(
                "protocol".to_string(),
                serde_json::Value::String(protocol.to_string()),
            );
        }
    }
    if let Some(raw) = non_empty(env, ENV_FIM_TIMEOUT_MS) {
        match raw.parse::<u64>() {
            Ok(timeout) => {
                fim.insert(
                    "timeout_ms".to_string(),
                    serde_json::Value::Number(timeout.into()),
                );
            }
            Err(_) => warnings.push(Warning::new(
                WarningCode::ConfigInvalidValue,
                format!("{ENV_FIM_TIMEOUT_MS} is not an integer; ignored"),
            )),
        }
    }
    if let Some(raw) = non_empty(env, ENV_FIM_ENABLED) {
        match raw {
            "1" | "true" | "yes" => {
                fim.insert("enabled".to_string(), serde_json::Value::Bool(true));
            }
            "0" | "false" | "no" => {
                fim.insert("enabled".to_string(), serde_json::Value::Bool(false));
            }
            other => warnings.push(Warning::new(
                WarningCode::ConfigInvalidValue,
                format!("{ENV_FIM_ENABLED} must be a boolean, got `{other}`; ignored"),
            )),
        }
    }
    if fim.is_empty() {
        serde_json::Value::Object(serde_json::Map::new())
    } else {
        let mut root = serde_json::Map::new();
        root.insert("fim".to_string(), serde_json::Value::Object(fim));
        serde_json::Value::Object(root)
    }
}

fn insert_env_string(
    target: &mut serde_json::Map<String, serde_json::Value>,
    env: &Env,
    key: &str,
    variable: &str,
) {
    if let Some(raw) = non_empty(env, variable) {
        target.insert(key.to_string(), serde_json::Value::String(raw.to_string()));
    }
}

fn non_empty<'a>(env: &'a Env, name: &str) -> Option<&'a str> {
    env.get(name)
        .map(str::trim)
        .filter(|value| !value.is_empty())
}

/// Deep-merge JSON objects; later scalars/arrays replace earlier ones.
fn merge_json(base: &mut serde_json::Value, overlay: &serde_json::Value) {
    match (base, overlay) {
        (serde_json::Value::Object(base_map), serde_json::Value::Object(overlay_map)) => {
            for (key, value) in overlay_map {
                match base_map.get_mut(key) {
                    Some(existing) => merge_json(existing, value),
                    None => {
                        base_map.insert(key.clone(), value.clone());
                    }
                }
            }
        }
        (base_slot, overlay_value) => *base_slot = overlay_value.clone(),
    }
}

fn warn_unknown_actions(config: &Config, warnings: &mut Vec<Warning>) {
    let known: Vec<&str> = KNOWN_BINDING_ACTIONS.to_vec();
    for action in config.keys.0.keys() {
        if !known.contains(&action.as_str()) {
            warnings.push(Warning::new(
                WarningCode::ConfigUnknownKey,
                format!("[keys] has an unknown action `{action}`"),
            ));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{merge_json, parse_layer};
    use serde_json::json;

    #[test]
    fn deep_merge_keeps_unrelated_keys_and_replaces_lists() {
        let mut base = json!({
            "editor": {"tab_width": 4, "insert_spaces": true},
            "project": {"defines": ["A"]}
        });
        let overlay = json!({
            "editor": {"tab_width": 2},
            "project": {"defines": ["B", "C"]}
        });
        merge_json(&mut base, &overlay);
        assert_eq!(base["editor"]["tab_width"], json!(2));
        assert_eq!(base["editor"]["insert_spaces"], json!(true));
        assert_eq!(base["project"]["defines"], json!(["B", "C"]));
    }

    #[test]
    fn parse_layer_reports_a_line_without_quoting_the_source() {
        let text = "[fim]\napi_key = \"test-live-TOPSECRET\n";
        let Some(error) = parse_layer(text).err() else {
            panic!("invalid TOML must be rejected");
        };
        assert!(error.line.is_some(), "a line number is reported");
        assert!(
            !error.message.contains("TOPSECRET"),
            "no source text in the error"
        );
    }
}
