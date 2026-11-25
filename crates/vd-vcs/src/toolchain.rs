//! Resolves and runs host Git and SVN using the same environment as the PTY.

use std::path::{Path, PathBuf};
use std::process::Output;

use crate::host::HostEnvironment;

/// Where a VCS tool was found.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolSource {
    /// Found on the effective host `PATH`.
    Path,
    /// Named by the `VEDIT_GIT` / `VEDIT_SVN` fallback variable.
    Env,
}

/// One resolved host tool.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedTool {
    /// Absolute path that will be spawned.
    pub path: PathBuf,
    /// Which rule found it.
    pub source: ToolSource,
}

fn env_name(tool: &str) -> Option<&'static str> {
    match tool {
        "git" => Some("VEDIT_GIT"),
        "svn" => Some("VEDIT_SVN"),
        _ => None,
    }
}

/// Resolves a VCS tool in the effective host environment.
#[must_use]
pub fn resolve(tool: &str) -> Option<ResolvedTool> {
    resolve_in(tool, &HostEnvironment::capture(Path::new(".")))
}

/// Resolves a VCS tool using effective PATH, then its named environment fallback.
#[must_use]
pub fn resolve_in(tool: &str, host: &HostEnvironment) -> Option<ResolvedTool> {
    let variable = env_name(tool)?;
    if let Some(path) = host.on_path(tool).find(|path| host.executable(path)) {
        return Some(ResolvedTool {
            path,
            source: ToolSource::Path,
        });
    }
    let path = PathBuf::from(host.get(variable)?);
    host.executable(&path).then_some(ResolvedTool {
        path,
        source: ToolSource::Env,
    })
}

pub(crate) fn run(
    tool: &str,
    working_tree: &Path,
    args: &[String],
    overrides: &[(String, String)],
) -> Result<Output, String> {
    let host = HostEnvironment::capture(working_tree);
    let shell = host.selected_shell(None)?;
    let resolved = resolve_in(tool, &host).ok_or_else(|| {
        let fallback = env_name(tool).unwrap_or("VEDIT_<TOOL>");
        format!("Cannot run {tool}: host executable not found. Install {tool} and add it to PATH or set {fallback} to its absolute path.")
    })?;
    host.command(&shell, &resolved.path, args)
        .envs(overrides.iter().map(|(name, value)| (name, value)))
        .output()
        .map_err(|error| {
            format!(
                "Cannot run {tool} ({}) through shell '{}': {error}",
                resolved.path.display(),
                shell.display()
            )
        })
}
