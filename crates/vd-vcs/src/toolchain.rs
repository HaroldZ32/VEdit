//! VCS toolchain resolution: **bundled > PATH > env** (AC-3.0 order).
//!
//! The product ships its own runtime, so it also ships `git`/`svn`
//! (`<appdir>/usr/lib/vcs/bin/`, wrappers that start the bundled binaries
//! through the AppDir loader). The resolver below decides WHICH program the
//! read/write runners spawn, in that order:
//!
//! 1. **bundled** — a tool next to the product binary in the AppDir layout:
//!    `<exe_dir>/../usr/lib/vcs/bin/<tool>`, then `<exe_dir>/<tool>` (the
//!    "next to the product binary" convention used by the other bundled tools);
//! 2. **PATH** — the first executable `<dir>/<tool>` found on `PATH`;
//! 3. **env** — `VEDIT_GIT` / `VEDIT_SVN` (last, exactly as the delivery
//!    contract orders it: an explicit fallback, not an override).
//!
//! The choice is observable from the C++ lane (`src/workspace/vcs/`), which
//! implements the same order for its `VCS-TOOLCHAIN` line and panel property.
//! The read/write gates are unaffected: they still see the logical program name
//! (`git` / `svn`) and run before this resolver spawns anything.
#![forbid(unsafe_code)]

use std::ffi::OsStr;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

/// Where a VCS tool was found.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolSource {
    /// Shipped inside the AppDir.
    Bundled,
    /// Found on `PATH`.
    Path,
    /// Named by the `VEDIT_GIT` / `VEDIT_SVN` environment variable.
    Env,
}

impl ToolSource {
    /// Stable lowercase name (`bundled` / `path` / `env`), for diagnostics.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Bundled => "bundled",
            Self::Path => "path",
            Self::Env => "env",
        }
    }
}

/// One resolved tool.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedTool {
    /// Logical tool name (`git` / `svn`).
    pub tool: String,
    /// Absolute path that will be spawned.
    pub path: PathBuf,
    /// Which rule found it.
    pub source: ToolSource,
}

/// `git`'s explicit fallback variable.
pub const GIT_ENV: &str = "VEDIT_GIT";
/// `svn`'s explicit fallback variable.
pub const SVN_ENV: &str = "VEDIT_SVN";

/// The environment variable for a tool, when the tool has one.
#[must_use]
pub fn env_name(tool: &str) -> Option<&'static str> {
    match tool {
        "git" => Some(GIT_ENV),
        "svn" => Some(SVN_ENV),
        _ => None,
    }
}

fn is_executable(path: &Path) -> bool {
    match std::fs::metadata(path) {
        Ok(metadata) => metadata.is_file() && metadata.permissions().mode() & 0o111 != 0,
        Err(_) => false,
    }
}

/// The bundled candidates for `tool`, in probe order.
///
/// The product is started through the AppDir's own loader, so `current_exe()`
/// may be the LOADER (`<appdir>/ld-linux-x86-64.so.2`) rather than the product
/// binary — hence both layouts are probed:
///   * `<exe_dir>/../usr/lib/vcs/bin/<tool>` when exe_dir is `<appdir>/bin`;
///   * `<exe_dir>/usr/lib/vcs/bin/<tool>` when exe_dir is the AppDir root.
#[must_use]
pub fn bundled_candidates(tool: &str, exe_dir: &Path) -> Vec<PathBuf> {
    vec![
        exe_dir.join("../usr/lib/vcs/bin").join(tool),
        exe_dir.join("usr/lib/vcs/bin").join(tool),
        // next to the product binary (the verible/slang bundling convention)
        exe_dir.join(tool),
        // <appdir>/bin/<tool> for an executable at the AppDir root
        exe_dir.join("bin").join(tool),
    ]
}

/// Resolves `tool` in the live process environment.
#[must_use]
pub fn resolve(tool: &str) -> Option<ResolvedTool> {
    let executable = std::env::current_exe().ok()?;
    let exe_dir = executable.parent()?;
    let path_env = std::env::var_os("PATH");
    let env_value = env_name(tool).and_then(std::env::var_os);
    resolve_with(tool, exe_dir, path_env.as_deref(), env_value.as_deref())
}

/// Resolves `tool` against explicit inputs (the unit-test seam).
///
/// `path_env` is the `PATH` value (colon-separated on unix); `env_value` is the
/// value of [`env_name`]'s variable, when set.
#[must_use]
pub fn resolve_with(
    tool: &str,
    exe_dir: &Path,
    path_env: Option<&OsStr>,
    env_value: Option<&OsStr>,
) -> Option<ResolvedTool> {
    // 1. bundled: relative to the product executable inside the AppDir.
    for candidate in bundled_candidates(tool, exe_dir) {
        if is_executable(&candidate) {
            return Some(ResolvedTool {
                tool: tool.to_string(),
                path: candidate,
                source: ToolSource::Bundled,
            });
        }
    }
    // 2. PATH.
    if let Some(path_env) = path_env {
        for directory in std::env::split_paths(path_env) {
            let candidate = directory.join(tool);
            if is_executable(&candidate) {
                return Some(ResolvedTool {
                    tool: tool.to_string(),
                    path: candidate,
                    source: ToolSource::Path,
                });
            }
        }
    }
    // 3. env (last: the delivery contract's explicit fallback).
    if let Some(value) = env_value {
        let candidate = PathBuf::from(value);
        if is_executable(&candidate) {
            return Some(ResolvedTool {
                tool: tool.to_string(),
                path: candidate,
                source: ToolSource::Env,
            });
        }
    }
    None
}

/// Resolves `tool`, or returns the refusal message naming every rule it tried.
///
/// # Errors
///
/// Returns a message when neither the bundled layout, `PATH`, nor the explicit
/// environment variable provides an executable.
pub fn resolve_or_error(tool: &str) -> Result<ResolvedTool, String> {
    resolve(tool).ok_or_else(|| {
        let env = env_name(tool).unwrap_or("VEDIT_<TOOL>");
        format!(
            "cannot run {tool}: not found (looked at the bundled \
             <exe>/../usr/lib/vcs/bin/{tool} and <exe>/{tool}, then PATH, then {env})"
        )
    })
}
