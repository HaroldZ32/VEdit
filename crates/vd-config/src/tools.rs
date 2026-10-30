//! Verible tool resolution: configuration → environment → bundled payload.
//!
//! `PATH` is never consulted. When all sources miss, the error and the JSON
//! `attempts` list are generated from the same attempt record, so a reviewer can
//! assert exactly which paths were probed and in which order.

use std::path::{Path, PathBuf};

use serde::Serialize;

use crate::env::Env;
use crate::model::Config;

/// One of the three bundled Verible binaries.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum VeribleTool {
    /// `verible-verilog-ls` (the LSP backend).
    Ls,
    /// `verible-verilog-format`.
    Format,
    /// `verible-verilog-lint`.
    Lint,
}

impl VeribleTool {
    /// The binary file name for this tool.
    pub fn binary_name(self) -> &'static str {
        match self {
            Self::Ls => "verible-verilog-ls",
            Self::Format => "verible-verilog-format",
            Self::Lint => "verible-verilog-lint",
        }
    }

    /// The per-tool environment variable override.
    pub fn env_var(self) -> &'static str {
        match self {
            Self::Ls => "VEDIT_VERIBLE_LS",
            Self::Format => "VEDIT_VERIBLE_FORMAT",
            Self::Lint => "VEDIT_VERIBLE_LINT",
        }
    }

    /// The `[verible]` configuration override, when set.
    pub fn configured_path(self, config: &Config) -> Option<&Path> {
        match self {
            Self::Ls => config.verible.ls.as_deref(),
            Self::Format => config.verible.format.as_deref(),
            Self::Lint => config.verible.lint.as_deref(),
        }
    }

    /// Every tool, for aggregate rendering.
    pub fn all() -> [Self; 3] {
        [Self::Ls, Self::Format, Self::Lint]
    }
}

/// Where a resolved tool path came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ToolSource {
    /// `[verible].ls|format|lint` in a configuration file.
    Config,
    /// `VEDIT_VERIBLE_LS|FORMAT|LINT` or `VEDIT_VERIBLE_DIR`.
    Env,
    /// The payload shipped next to the executable.
    Bundled,
}

impl ToolSource {
    /// Stable string form.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Config => "config",
            Self::Env => "env",
            Self::Bundled => "bundled",
        }
    }
}

/// One probed location, whether or not it existed.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Attempt {
    /// Which source produced this candidate path.
    pub source: ToolSource,
    /// The probed path.
    pub path: PathBuf,
    /// Whether the path was a regular file.
    pub found: bool,
}

/// The outcome of resolving one tool.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ResolvedTool {
    /// The tool this record is about.
    pub tool: VeribleTool,
    /// The resolved path, when found.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<PathBuf>,
    /// The source that produced the resolved path.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub source: Option<ToolSource>,
    /// Every location probed, in order, successful or not.
    pub attempts: Vec<Attempt>,
    /// The failure explanation, when nothing was found.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

/// Resolution of all three tools.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ToolsResolution {
    /// `verible-verilog-ls`.
    pub ls: ResolvedTool,
    /// `verible-verilog-format`.
    pub format: ResolvedTool,
    /// `verible-verilog-lint`.
    pub lint: ResolvedTool,
}

impl ToolsResolution {
    /// Resolve all three tools against the same inputs.
    pub fn resolve(config: &Config, env: &Env, exe_dir: &Path) -> Self {
        Self {
            ls: resolve_verible(VeribleTool::Ls, config, env, exe_dir),
            format: resolve_verible(VeribleTool::Format, config, env, exe_dir),
            lint: resolve_verible(VeribleTool::Lint, config, env, exe_dir),
        }
    }

    /// Iterate over the three records.
    pub fn iter(&self) -> impl Iterator<Item = &ResolvedTool> {
        [&self.ls, &self.format, &self.lint].into_iter()
    }
}

/// Resolve one tool: configuration, then environment, then bundled payload.
///
/// A configured or environment-provided path that does not exist is recorded as
/// a failed attempt and resolution continues with the next source; the eventual
/// error lists every attempt. `PATH` is never consulted.
pub fn resolve_verible(
    tool: VeribleTool,
    config: &Config,
    env: &Env,
    exe_dir: &Path,
) -> ResolvedTool {
    let mut attempts: Vec<Attempt> = Vec::new();

    if let Some(path) = tool.configured_path(config) {
        if let Some(hit) = probe(&mut attempts, ToolSource::Config, path.to_path_buf()) {
            return resolved(tool, hit, attempts);
        }
    }
    if let Some(raw) = env.get(tool.env_var()).map(str::trim) {
        if !raw.is_empty() {
            if let Some(hit) = probe(&mut attempts, ToolSource::Env, PathBuf::from(raw)) {
                return resolved(tool, hit, attempts);
            }
        }
    }
    if let Some(dir) = env.get("VEDIT_VERIBLE_DIR").map(str::trim) {
        if !dir.is_empty() {
            let candidate = PathBuf::from(dir).join(tool.binary_name());
            if let Some(hit) = probe(&mut attempts, ToolSource::Env, candidate) {
                return resolved(tool, hit, attempts);
            }
        }
    }
    for candidate in bundled_candidates(exe_dir, tool.binary_name()) {
        if let Some(hit) = probe(&mut attempts, ToolSource::Bundled, candidate) {
            return resolved(tool, hit, attempts);
        }
    }

    ResolvedTool {
        tool,
        path: None,
        source: None,
        error: Some(not_found_message(tool, &attempts)),
        attempts,
    }
}

fn resolved(tool: VeribleTool, hit: (PathBuf, ToolSource), attempts: Vec<Attempt>) -> ResolvedTool {
    ResolvedTool {
        tool,
        path: Some(hit.0),
        source: Some(hit.1),
        attempts,
        error: None,
    }
}

fn probe(
    attempts: &mut Vec<Attempt>,
    source: ToolSource,
    path: PathBuf,
) -> Option<(PathBuf, ToolSource)> {
    let found = path.is_file();
    attempts.push(Attempt {
        source,
        path: path.clone(),
        found,
    });
    found.then_some((path, source))
}

/// Candidate locations inside a shipped payload, in probe order.
fn bundled_candidates(exe_dir: &Path, binary: &str) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(parent) = exe_dir.parent() {
        candidates.push(parent.join("lib/vedit/bin").join(binary));
        candidates.push(parent.join("bin").join(binary));
    }
    candidates.push(exe_dir.join(binary));
    candidates
}

fn not_found_message(tool: VeribleTool, attempts: &[Attempt]) -> String {
    let mut message = format!(
        "{} was not found (PATH is never consulted); probed {} location(s):",
        tool.binary_name(),
        attempts.len()
    );
    for attempt in attempts {
        message.push_str(&format!(
            "\n  source={} path={} found={}",
            attempt.source.as_str(),
            attempt.path.display(),
            attempt.found
        ));
    }
    message
}

#[cfg(test)]
mod tests {
    use super::{resolve_verible, ToolSource, VeribleTool};
    use crate::env::Env;
    use crate::model::Config;
    use crate::test_support::write;

    #[test]
    fn configured_path_wins() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let binary = temp.path().join("verible-verilog-ls");
        write(&binary, "#!/bin/sh\n")?;
        let mut config = Config::default();
        config.verible.ls = Some(binary.clone());
        let env = Env::from_pairs(Vec::<(String, String)>::new());
        let resolved = resolve_verible(VeribleTool::Ls, &config, &env, temp.path());
        assert_eq!(resolved.path, Some(binary));
        assert_eq!(resolved.source, Some(ToolSource::Config));
        assert_eq!(resolved.attempts.len(), 1);
        assert!(resolved.attempts[0].found);
        assert!(resolved.error.is_none());
        Ok(())
    }

    #[test]
    fn missing_config_falls_through_to_env_and_records_both_attempts(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let env_binary = temp.path().join("from-env");
        write(&env_binary, "#!/bin/sh\n")?;
        let mut config = Config::default();
        config.verible.ls = Some(temp.path().join("does-not-exist"));
        let env = Env::from_pairs([("VEDIT_VERIBLE_LS", env_binary.display().to_string())]);
        let resolved = resolve_verible(VeribleTool::Ls, &config, &env, temp.path());
        assert_eq!(resolved.source, Some(ToolSource::Env));
        assert_eq!(resolved.attempts.len(), 2);
        assert_eq!(resolved.attempts[0].source, ToolSource::Config);
        assert!(!resolved.attempts[0].found);
        assert!(resolved.attempts[1].found);
        Ok(())
    }

    #[test]
    fn failure_lists_every_attempt_and_never_mentions_path_lookup(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let config = Config::default();
        let env = Env::from_pairs(Vec::<(String, String)>::new());
        let resolved = resolve_verible(VeribleTool::Format, &config, &env, temp.path());
        assert!(resolved.path.is_none());
        assert!(resolved.source.is_none());
        assert_eq!(
            resolved.attempts.len(),
            3,
            "three bundled candidates probed"
        );
        let Some(error) = resolved.error else {
            return Err("a missing tool must carry an error".into());
        };
        assert!(error.contains("verible-verilog-format"));
        assert!(error.contains("PATH is never consulted"));
        for attempt in &resolved.attempts {
            assert!(
                error.contains(&attempt.path.display().to_string()),
                "error must list every attempted path"
            );
        }
        Ok(())
    }

    #[test]
    fn bundled_payload_is_found_next_to_the_executable() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let exe_dir = temp.path().join("usr/bin");
        let lib_dir = temp.path().join("usr/lib/vedit/bin");
        std::fs::create_dir_all(&exe_dir)?;
        std::fs::create_dir_all(&lib_dir)?;
        let binary = lib_dir.join("verible-verilog-lint");
        write(&binary, "#!/bin/sh\n")?;
        let resolved = resolve_verible(
            VeribleTool::Lint,
            &Config::default(),
            &Env::from_pairs(Vec::<(String, String)>::new()),
            &exe_dir,
        );
        assert_eq!(resolved.source, Some(ToolSource::Bundled));
        assert_eq!(resolved.path, Some(binary));
        Ok(())
    }
}
