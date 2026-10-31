//! Two renderings of one resolved state: a stable JSON document and a human table.
//!
//! Both are produced from the same [`Loaded`] value, so they cannot disagree.
//! The JSON shape is the machine contract (`schema_version` is asserted by
//! acceptance scripts); the table is for humans debugging a project.

use serde_json::json;

use crate::provenance::ValueView;
use crate::tools::ResolvedTool;
use crate::Loaded;

/// Version of the JSON document produced by [`to_json`].
///
/// Bump this whenever the shape changes; consumers assert on it.
pub const SCHEMA_VERSION: u64 = 1;

/// The stable JSON document (`--print-config --json`).
pub fn to_json(loaded: &Loaded) -> serde_json::Value {
    json!({
        "schema_version": SCHEMA_VERSION,
        "sources": ["default", "user", "project", "env"],
        "entries": loaded.entries,
        "config": loaded.config,
        "project": loaded.project,
        "tools": loaded.tools,
        "warnings": loaded.warnings,
    })
}

/// The human-readable table (`--print-config`).
///
/// Every line carries its provenance; a secret value is always rendered as
/// `<redacted>` because [`ValueView`] cannot hold the plaintext.
pub fn render_table(loaded: &Loaded) -> String {
    let mut out = String::new();
    out.push_str(&format!("schema_version={SCHEMA_VERSION}\n"));

    out.push_str("\n# config\n");
    for entry in &loaded.entries {
        out.push_str(&format!(
            "{:<24} {:<30} source={}",
            entry.key,
            render_value(&entry.value),
            entry.source
        ));
        if let Some(overridden_by) = entry.overridden_by {
            out.push_str(&format!("  overridden_by={overridden_by}"));
        }
        if !entry.shadowed.is_empty() {
            let chain = entry
                .shadowed
                .iter()
                .map(|source| source.as_str())
                .collect::<Vec<_>>()
                .join(",");
            out.push_str(&format!("  shadowed=[{chain}]"));
        }
        out.push('\n');
    }

    out.push_str("\n# project\n");
    out.push_str(&format!(
        "{:<24} {:<30} source={}\n",
        "root",
        loaded.project.root.display(),
        root_source_str(loaded.project.root_source)
    ));
    out.push_str(&format!(
        "{:<24} {}\n",
        "search_stop",
        loaded.project.search_stop.display()
    ));
    for filelist in &loaded.project.filelists {
        out.push_str(&format!(
            "{:<24} {}  entries={} include_dirs={} defines={}\n",
            "filelist",
            filelist.path.display(),
            filelist.entries.len(),
            filelist.include_dirs.len(),
            filelist.defines.len()
        ));
    }
    for dir in &loaded.project.include_dirs {
        out.push_str(&format!("{:<24} {}\n", "include_dir", dir.display()));
    }
    for define in &loaded.project.defines {
        let rendered = match &define.value {
            Some(value) => format!("{}={value}", define.name),
            None => define.name.clone(),
        };
        out.push_str(&format!("{:<24} {rendered}\n", "define"));
    }
    match &loaded.project.rules_path {
        Some(path) => out.push_str(&format!("{:<24} {}\n", "rules", path.display())),
        None => out.push_str(&format!("{:<24} <not found>\n", "rules")),
    }
    out.push_str(&format!(
        "{:<24} {}\n",
        "rules_search_stop",
        loaded.project.rules_search_stop.display()
    ));

    out.push_str("\n# tools\n");
    for tool in loaded.tools.iter() {
        out.push_str(&render_tool(tool));
    }

    out.push_str("\n# warnings\n");
    if loaded.warnings.is_empty() {
        out.push_str("none\n");
    }
    for warning in &loaded.warnings {
        out.push_str(&format!("warning {} ", warning.code.as_str()));
        if let Some(file) = &warning.file {
            out.push_str(&format!("file={} ", file.display()));
        }
        if let Some(line) = warning.line {
            out.push_str(&format!("line={line} "));
        }
        out.push_str(&format!("message={}\n", warning.message));
    }
    out
}

fn render_tool(tool: &ResolvedTool) -> String {
    let mut out = String::new();
    let name = tool.tool.binary_name();
    match (&tool.path, tool.source) {
        (Some(path), Some(source)) => out.push_str(&format!(
            "{:<24} {:<30} source={}\n",
            name,
            path.display(),
            source.as_str()
        )),
        _ => out.push_str(&format!("{:<24} <not found>\n", name)),
    }
    for attempt in &tool.attempts {
        out.push_str(&format!(
            "{:<24} source={} path={} found={}\n",
            format!("  attempt:{name}"),
            attempt.source.as_str(),
            attempt.path.display(),
            attempt.found
        ));
    }
    if let Some(error) = &tool.error {
        out.push_str(&format!(
            "{:<24} {error}\n",
            "  error",
            error = error.replace('\n', "\n  ")
        ));
    }
    out
}

fn render_value(value: &ValueView) -> String {
    match value {
        ValueView::Text { text } => text.clone(),
        ValueView::Redacted { set: true } => "<redacted>".to_string(),
        ValueView::Redacted { set: false } => "<unset>".to_string(),
        ValueView::List { len } => format!("[{len} item(s)]"),
        ValueView::Number { value } => value.to_string(),
        ValueView::Bool { value } => value.to_string(),
    }
}

fn root_source_str(source: crate::project::RootSource) -> &'static str {
    match source {
        crate::project::RootSource::VeditToml => "vedit_toml",
        crate::project::RootSource::GitDir => "git_dir",
        crate::project::RootSource::Filelist => "filelist",
        crate::project::RootSource::Fallback => "fallback",
    }
}

#[cfg(test)]
mod tests {
    use super::{render_table, to_json, SCHEMA_VERSION};
    use crate::model::Config;
    use crate::project::{ProjectContext, RootSource};
    use crate::provenance::{Entry, Source, ValueView};
    use crate::test_support::require;
    use crate::tools::ToolsResolution;
    use crate::{Env, Loaded};
    use std::path::PathBuf;

    fn loaded_with(entries: Vec<Entry>) -> Loaded {
        let config = Config::default();
        let tools = ToolsResolution::resolve(
            &config,
            &Env::from_pairs(Vec::<(String, String)>::new()),
            &PathBuf::from("/payload/usr/bin"),
        );
        Loaded {
            config,
            project: ProjectContext {
                root: PathBuf::from("/project"),
                root_source: RootSource::VeditToml,
                search_stop: PathBuf::from("/project"),
                filelists: Vec::new(),
                include_dirs: Vec::new(),
                defines: Vec::new(),
                rules_path: None,
                rules_search_stop: PathBuf::from("/project"),
            },
            entries,
            warnings: Vec::new(),
            tools,
        }
    }

    #[test]
    fn json_carries_the_schema_version_and_tool_attempts_even_on_success(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let loaded = loaded_with(Vec::new());
        let json = to_json(&loaded);
        assert_eq!(json["schema_version"], serde_json::json!(SCHEMA_VERSION));
        assert_eq!(
            json["sources"],
            serde_json::json!(["default", "user", "project", "env"])
        );
        for tool in ["ls", "format", "lint"] {
            let attempts = require(json["tools"][tool]["attempts"].as_array())?;
            assert!(!attempts.is_empty(), "attempts are always present");
            assert!(attempts[0].get("source").is_some());
            assert!(attempts[0].get("path").is_some());
            assert!(attempts[0].get("found").is_some());
        }
        assert!(json["project"]["root"].is_string());
        Ok(())
    }

    #[test]
    fn table_renders_redacted_secret_and_overridden_by() {
        let entries = vec![
            Entry {
                key: "fim.api_key".to_string(),
                value: ValueView::Redacted { set: true },
                source: Source::Env,
                overridden_by: Some(Source::Project),
                shadowed: vec![Source::Project],
            },
            Entry {
                key: "verible.ls".to_string(),
                value: ValueView::Text {
                    text: "/opt/verible/bin/verible-verilog-ls".to_string(),
                },
                source: Source::Project,
                overridden_by: Some(Source::User),
                shadowed: vec![Source::User],
            },
        ];
        let table = render_table(&loaded_with(entries));
        assert!(table.contains("fim.api_key"));
        assert!(table.contains("<redacted>"));
        assert!(table.contains("source=env"));
        assert!(table.contains("overridden_by=project"));
        assert!(table.contains("source=project"));
        assert!(table.contains("overridden_by=user"));
        assert!(!table.contains("sk-"), "no secret material in the table");
    }
}
