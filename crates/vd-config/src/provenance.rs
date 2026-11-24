//! Configuration provenance: which layer set each value, and what it overrode.
//!
//! Layers are applied in precedence order (default → user → project → env), so
//! the recorded chain is also the explanation of precedence. Values are exposed
//! as [`ValueView`], a redaction-first projection: secret paths never become
//! text, which is why the printed configuration is safe by construction.

use std::collections::BTreeMap;

use serde::Serialize;

/// A configuration layer, in precedence order.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Source {
    /// The built-in defaults.
    Default,
    /// `$XDG_CONFIG_HOME/vedit/vedit.toml`.
    User,
    /// `<project root>/vedit.toml`.
    Project,
    /// Environment variables (FIM settings only).
    Env,
}

impl Source {
    /// Stable string form.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::User => "user",
            Self::Project => "project",
            Self::Env => "env",
        }
    }
}

impl std::fmt::Display for Source {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

/// The product-wide runtime tool-resolution order.
///
/// Declared in probe order, so [`ResolutionStage::ORDER`] and `Ord` agree: a
/// LOWER stage is probed EARLIER and therefore WINS over a higher one. This is
/// the single written order that the Rust config layer
/// (`crate::tools::resolve_tool`) and the C++ diagnostics toolchain
/// (`src/workspace/diagnostics/toolchain.cpp`) both implement:
///
/// 1. [`ResolutionStage::ExplicitSetting`] — a path the user set (`tools.*` in
///    `settings.toml`, `[verible].*` in a configuration file);
/// 2. [`ResolutionStage::Bundled`] — the payload shipped next to the product;
/// 3. [`ResolutionStage::LocalCheckout`] — a source checkout's own build
///    artifact (development trees only: the C++ lane discovers it, the config
///    layer has no checkout to walk);
/// 4. [`ResolutionStage::ImportedProject`] — a variable imported from the
///    PROJECT's shell environment;
/// 5. [`ResolutionStage::ImportedGlobal`] — a variable imported from the USER's
///    global shell environment (project wins over global);
/// 6. [`ResolutionStage::ProcessPath`] — the first executable on the process
///    `PATH`;
/// 7. [`ResolutionStage::ProcessEnv`] — the process environment's `VEDIT_*`
///    override (the documented last resort: `bundled > … > PATH > env`).
///
/// `git`/`svn` are resolved by `crates/vd-vcs`, whose order (bundled -> PATH ->
/// env) is this order with stages 1 and 3–5 absent — the same precedence, not a
/// different one.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ResolutionStage {
    /// An explicit setting (`tools.*`, `[verible].*`).
    ExplicitSetting,
    /// The payload shipped next to the executable.
    Bundled,
    /// A source checkout's own artifact (development trees only).
    LocalCheckout,
    /// A variable imported from the project's shell environment.
    ImportedProject,
    /// A variable imported from the user's global shell environment.
    ImportedGlobal,
    /// A directory on the process `PATH`.
    ProcessPath,
    /// The process environment's explicit override.
    ProcessEnv,
}

impl ResolutionStage {
    /// Every stage, in probe order — the written order itself.
    pub const ORDER: [Self; 7] = [
        Self::ExplicitSetting,
        Self::Bundled,
        Self::LocalCheckout,
        Self::ImportedProject,
        Self::ImportedGlobal,
        Self::ProcessPath,
        Self::ProcessEnv,
    ];

    /// Stable string form, used in the `TOOLCHAIN source=` line and in errors.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::ExplicitSetting => "setting",
            Self::Bundled => "bundled",
            Self::LocalCheckout => "local",
            Self::ImportedProject => "imported-project",
            Self::ImportedGlobal => "imported-global",
            Self::ProcessPath => "PATH",
            Self::ProcessEnv => "env",
        }
    }

    /// The zero-based precedence of this stage (lower wins).
    pub fn precedence(self) -> usize {
        self as usize
    }
}

/// A redaction-safe view of one configuration value.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ValueView {
    /// A non-secret scalar rendered as text.
    Text {
        /// The value.
        text: String,
    },
    /// A secret value: only its presence is exposed, never its content.
    Redacted {
        /// Whether a non-empty value is configured.
        set: bool,
    },
    /// A list; only its length is exposed.
    List {
        /// Number of elements.
        len: usize,
    },
    /// An integer value.
    Number {
        /// The value.
        value: i64,
    },
    /// A boolean value.
    Bool {
        /// The value.
        value: bool,
    },
}

/// The final state of one configuration key, with its precedence history.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Entry {
    /// Dotted key path, e.g. `fim.endpoint` or `keys.format`.
    pub key: String,
    /// The redaction-safe value that is in force.
    pub value: ValueView,
    /// The layer that set the value in force.
    pub source: Source,
    /// The layer this value shadowed most recently (nearest, not oldest).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub overridden_by: Option<Source>,
    /// Every layer this key has shadowed, oldest first.
    pub shadowed: Vec<Source>,
}

/// Accumulates provenance entries while layers are applied in order.
#[derive(Debug, Default)]
pub(crate) struct Provenance {
    entries: BTreeMap<String, Entry>,
}

impl Provenance {
    /// Record every leaf of a JSON layer object as having `source`.
    pub(crate) fn record_layer(&mut self, source: Source, layer: &serde_json::Value) {
        let mut leaves = Vec::new();
        flatten_json("", layer, &mut leaves);
        for (key, value) in leaves {
            self.record(key, value, source);
        }
    }

    fn record(&mut self, key: String, value: ValueView, source: Source) {
        if let Some(existing) = self.entries.get_mut(&key) {
            existing.shadowed.push(existing.source);
            existing.overridden_by = Some(existing.source);
            existing.value = value;
            existing.source = source;
            return;
        }
        self.entries.insert(
            key.clone(),
            Entry {
                key,
                value,
                source,
                overridden_by: None,
                shadowed: Vec::new(),
            },
        );
    }

    /// Consume the accumulator into a key-sorted list.
    pub(crate) fn into_entries(self) -> Vec<Entry> {
        self.entries.into_values().collect()
    }
}

/// Flatten a JSON object into dotted leaf paths with redaction applied.
fn flatten_json(prefix: &str, value: &serde_json::Value, out: &mut Vec<(String, ValueView)>) {
    match value {
        serde_json::Value::Object(map) => {
            for (key, child) in map {
                let path = if prefix.is_empty() {
                    key.clone()
                } else {
                    format!("{prefix}.{key}")
                };
                flatten_json(&path, child, out);
            }
        }
        other => out.push((prefix.to_string(), view_of(prefix, other))),
    }
}

fn view_of(path: &str, value: &serde_json::Value) -> ValueView {
    if is_secret_path(path) {
        let set = match value {
            serde_json::Value::Null => false,
            serde_json::Value::String(text) => !text.trim().is_empty(),
            _ => true,
        };
        return ValueView::Redacted { set };
    }
    match value {
        serde_json::Value::Bool(flag) => ValueView::Bool { value: *flag },
        serde_json::Value::Number(number) => match number.as_i64() {
            Some(int) => ValueView::Number { value: int },
            None => ValueView::Text {
                text: number.to_string(),
            },
        },
        serde_json::Value::String(text) => ValueView::Text { text: text.clone() },
        serde_json::Value::Array(items) => ValueView::List { len: items.len() },
        serde_json::Value::Null => ValueView::Text {
            text: String::new(),
        },
        serde_json::Value::Object(_) => ValueView::Text {
            text: String::from("<object>"),
        },
    }
}

/// Secret keys are matched by suffix so a future `deep_lint.api_key` is covered
/// without touching this module.
pub(crate) fn is_secret_path(path: &str) -> bool {
    path == "api_key" || path.ends_with(".api_key")
}

#[cfg(test)]
mod tests {
    use super::{Provenance, Source, ValueView};
    use crate::test_support::require;
    use serde_json::json;

    #[test]
    fn precedence_chain_is_recorded_nearest_first_in_overridden_by(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut provenance = Provenance::default();
        provenance.record_layer(Source::Default, &json!({"editor": {"tab_width": 4}}));
        provenance.record_layer(Source::User, &json!({"editor": {"tab_width": 2}}));
        provenance.record_layer(Source::Project, &json!({"editor": {"tab_width": 8}}));
        let entries = provenance.into_entries();
        let entry = require(
            entries
                .iter()
                .find(|entry| entry.key == "editor.tab_width")
                .cloned(),
        )?;
        assert_eq!(entry.source, Source::Project);
        assert_eq!(entry.overridden_by, Some(Source::User));
        assert_eq!(entry.shadowed, vec![Source::Default, Source::User]);
        assert_eq!(entry.value, ValueView::Number { value: 8 });
        Ok(())
    }

    #[test]
    fn secret_leaf_is_redacted_even_when_the_layer_carries_the_raw_value(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut provenance = Provenance::default();
        provenance.record_layer(
            Source::Env,
            &json!({"fim": {"api_key": "test-live-secret", "enabled": true}}),
        );
        let entries = provenance.into_entries();
        let key = require(
            entries
                .iter()
                .find(|entry| entry.key == "fim.api_key")
                .cloned(),
        )?;
        assert_eq!(key.value, ValueView::Redacted { set: true });
        let rendered = serde_json::to_string(&entries)?;
        assert!(
            !rendered.contains("test-live-secret"),
            "provenance leaked secret"
        );
        Ok(())
    }

    #[test]
    fn empty_secret_is_reported_as_unset() -> Result<(), Box<dyn std::error::Error>> {
        let mut provenance = Provenance::default();
        provenance.record_layer(Source::Project, &json!({"fim": {"api_key": ""}}));
        let entries = provenance.into_entries();
        let key = require(
            entries
                .iter()
                .find(|entry| entry.key == "fim.api_key")
                .cloned(),
        )?;
        assert_eq!(key.value, ValueView::Redacted { set: false });
        Ok(())
    }

    #[test]
    fn lists_reduce_to_their_length() -> Result<(), Box<dyn std::error::Error>> {
        let mut provenance = Provenance::default();
        provenance.record_layer(
            Source::Project,
            &json!({"project": {"filelists": ["a.f", "b.f"]}}),
        );
        let entries = provenance.into_entries();
        let entry = require(
            entries
                .iter()
                .find(|entry| entry.key == "project.filelists")
                .cloned(),
        )?;
        assert_eq!(entry.value, ValueView::List { len: 2 });
        Ok(())
    }
}
