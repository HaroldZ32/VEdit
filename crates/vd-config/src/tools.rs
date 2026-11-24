//! Runtime tool resolution: ONE order, one record of what was probed.
//!
//! The order (earlier wins) is declared once in
//! [`crate::provenance::ResolutionStage::ORDER`] and implemented here:
//!
//! 1. **explicit setting** — `[verible].ls|format|lint` in a configuration file;
//! 2. **bundled** — the payload shipped next to the executable;
//! 3. **local checkout** — a source tree's own artifact (the C++ diagnostics
//!    lane discovers it; this layer has no checkout to walk);
//! 4. **imported project** — a variable imported from the project's shell
//!    environment;
//! 5. **imported global** — a variable imported from the user's global shell
//!    environment (project wins over global);
//! 6. **process PATH** — the first executable of that name on `PATH`;
//! 7. **process env** — the `VEDIT_*` override (the documented last resort).
//!
//! Every candidate probed is recorded in [`ResolvedTool::attempts`] with the
//! stage that produced it, so the chosen source AND the order are observable.
//! The same order applies to every [`ToolClass`], which is why a missing tool
//! and a resolved one are explained by the same record.

use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};

use serde::Serialize;

use crate::env::Env;
use crate::model::Config;
use crate::provenance::ResolutionStage;

/// A tool the product spawns at runtime.
///
/// The classes the delivery ships today (Verible, slang) and the VHDL/Scala
/// classes as their packs land: one class table keeps the resolution order,
/// the env-var convention and the bundled layout identical for all of them.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ToolClass {
    /// `verible-verilog-ls` (the Verilog LSP backend).
    VeribleLs,
    /// `verible-verilog-format`.
    VeribleFormat,
    /// `verible-verilog-lint`.
    VeribleLint,
    /// `slang` (deep lint).
    Slang,
    /// `slang-server` (the alternate LSP backend).
    SlangServer,
    /// `vhdl_ls` (the VHDL language server).
    VhdlLs,
    /// `ghdl` (VHDL analysis/elaboration; the analysis half of the lint pair).
    Ghdl,
    /// `vsg` (VHDL Style Guide; the style half of the lint pair).
    Vsg,
    /// `java` (the JDK launcher Metals runs on).
    Java,
    /// `scala-cli` (the Scala runner).
    ScalaCli,
    /// `sbt` (the build tool).
    Sbt,
    /// `metals` (the Scala language server launcher).
    Metals,
}

impl ToolClass {
    /// The binary file name for this tool.
    pub fn binary_name(self) -> &'static str {
        match self {
            Self::VeribleLs => "verible-verilog-ls",
            Self::VeribleFormat => "verible-verilog-format",
            Self::VeribleLint => "verible-verilog-lint",
            Self::Slang => "slang",
            Self::SlangServer => "slang-server",
            Self::VhdlLs => "vhdl_ls",
            Self::Ghdl => "ghdl",
            Self::Vsg => "vsg",
            Self::Java => "java",
            Self::ScalaCli => "scala-cli",
            Self::Sbt => "sbt",
            Self::Metals => "metals",
        }
    }

    /// The per-tool environment variable override.
    pub fn env_var(self) -> &'static str {
        match self {
            Self::VeribleLs => "VEDIT_VERIBLE_LS",
            Self::VeribleFormat => "VEDIT_VERIBLE_FORMAT",
            Self::VeribleLint => "VEDIT_VERIBLE_LINT",
            Self::Slang => "VEDIT_SLANG",
            Self::SlangServer => "VEDIT_SLANG_SERVER",
            Self::VhdlLs => "VEDIT_VHDL_LS",
            Self::Ghdl => "VEDIT_GHDL",
            Self::Vsg => "VEDIT_VSG",
            Self::Java => "VEDIT_JAVA",
            Self::ScalaCli => "VEDIT_SCALA_CLI",
            Self::Sbt => "VEDIT_SBT",
            Self::Metals => "VEDIT_METALS",
        }
    }

    /// The `[verible]` configuration override (the explicit-setting stage).
    ///
    /// Only the Verible trio has a configuration key today; the other classes
    /// start at the bundled stage until their settings keys land.
    pub fn configured_path(self, config: &Config) -> Option<&Path> {
        match self {
            Self::VeribleLs => config.verible.ls.as_deref(),
            Self::VeribleFormat => config.verible.format.as_deref(),
            Self::VeribleLint => config.verible.lint.as_deref(),
            _ => None,
        }
    }

    /// Legacy directory variables this class still honours, if any.
    ///
    /// The Verible trio predates the per-tool `VEDIT_<TOOL>_DIR` convention and
    /// shares one `VEDIT_VERIBLE_DIR`; it stays supported so an existing
    /// environment keeps resolving.
    pub fn legacy_dir_vars(self) -> &'static [&'static str] {
        match self {
            Self::VeribleLs | Self::VeribleFormat | Self::VeribleLint => &["VEDIT_VERIBLE_DIR"],
            _ => &[],
        }
    }

    /// Every class, in a stable order (the matrix test walks this).
    pub fn all() -> [Self; 12] {
        [
            Self::VeribleLs,
            Self::VeribleFormat,
            Self::VeribleLint,
            Self::Slang,
            Self::SlangServer,
            Self::VhdlLs,
            Self::Ghdl,
            Self::Vsg,
            Self::Java,
            Self::ScalaCli,
            Self::Sbt,
            Self::Metals,
        ]
    }
}

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
        self.class().binary_name()
    }

    /// The per-tool environment variable override.
    pub fn env_var(self) -> &'static str {
        self.class().env_var()
    }

    /// The `[verible]` configuration override, when set.
    pub fn configured_path(self, config: &Config) -> Option<&Path> {
        self.class().configured_path(config)
    }

    /// The general tool class this Verible tool resolves as.
    pub fn class(self) -> ToolClass {
        match self {
            Self::Ls => ToolClass::VeribleLs,
            Self::Format => ToolClass::VeribleFormat,
            Self::Lint => ToolClass::VeribleLint,
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
    /// `[verible].ls|format|lint` in a configuration file (explicit setting).
    Config,
    /// The payload shipped next to the executable.
    Bundled,
    /// `VEDIT_*` imported from the PROJECT's shell environment.
    ImportedProject,
    /// `VEDIT_*` imported from the USER's global shell environment.
    ImportedGlobal,
    /// A directory on the process `PATH`.
    Path,
    /// `VEDIT_VERIBLE_LS|FORMAT|LINT` / `VEDIT_*` in the process environment.
    Env,
}

impl ToolSource {
    /// Stable string form (matches the `source=` token in logs and the JSON).
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Config => "config",
            Self::Bundled => "bundled",
            Self::ImportedProject => "imported_project",
            Self::ImportedGlobal => "imported_global",
            Self::Path => "path",
            Self::Env => "env",
        }
    }

    /// The stage of the product-wide order this source belongs to.
    pub fn stage(self) -> ResolutionStage {
        match self {
            Self::Config => ResolutionStage::ExplicitSetting,
            Self::Bundled => ResolutionStage::Bundled,
            Self::ImportedProject => ResolutionStage::ImportedProject,
            Self::ImportedGlobal => ResolutionStage::ImportedGlobal,
            Self::Path => ResolutionStage::ProcessPath,
            Self::Env => ResolutionStage::ProcessEnv,
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

impl Attempt {
    /// The stage of the product-wide order this attempt belongs to.
    pub fn stage(&self) -> ResolutionStage {
        self.source.stage()
    }
}

/// The outcome of resolving one tool.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ResolvedTool {
    /// The tool this record is about.
    pub tool: ToolClass,
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
    /// Resolve all three Verible tools without imported environment variables.
    pub fn resolve(config: &Config, env: &Env, exe_dir: &Path) -> Self {
        Self::resolve_with_imported(config, env, &ImportedVars::default(), exe_dir)
    }

    /// Resolve all three Verible tools against the same inputs.
    ///
    /// `imported` carries the variables an imported shell environment
    /// contributed; it is tried at stages 4–5, between the bundled payload and
    /// the process `PATH`/environment.
    pub fn resolve_with_imported(
        config: &Config,
        env: &Env,
        imported: &ImportedVars,
        exe_dir: &Path,
    ) -> Self {
        Self {
            ls: resolve_tool(ToolClass::VeribleLs, config, env, imported, exe_dir),
            format: resolve_tool(ToolClass::VeribleFormat, config, env, imported, exe_dir),
            lint: resolve_tool(ToolClass::VeribleLint, config, env, imported, exe_dir),
        }
    }

    /// Iterate over the three records.
    pub fn iter(&self) -> impl Iterator<Item = &ResolvedTool> {
        [&self.ls, &self.format, &self.lint].into_iter()
    }
}

/// Environment variables imported from a shell profile (env-core's output).
///
/// The two layers keep their identity so the resolution order can honour
/// "project over global": a variable the PROJECT's shell sets wins over the same
/// variable coming from the user's GLOBAL shell.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ImportedVars {
    /// Variables imported from the project's shell environment.
    pub project: BTreeMap<String, String>,
    /// Variables imported from the user's global shell environment.
    pub global: BTreeMap<String, String>,
}

impl ImportedVars {
    /// Build the two-layer record from explicit maps.
    pub fn from_maps(project: BTreeMap<String, String>, global: BTreeMap<String, String>) -> Self {
        Self { project, global }
    }

    /// True when neither layer carries anything.
    pub fn is_empty(&self) -> bool {
        self.project.is_empty() && self.global.is_empty()
    }
}

/// Resolve one tool against the product-wide order.
///
/// A configured, imported or environment-provided path that does not exist is
/// recorded as a failed attempt and resolution continues with the next stage;
/// the eventual error lists every attempt. The order itself lives in
/// [`ResolutionStage::ORDER`].
pub fn resolve_tool(
    class: ToolClass,
    config: &Config,
    env: &Env,
    imported: &ImportedVars,
    exe_dir: &Path,
) -> ResolvedTool {
    let mut attempts: Vec<Attempt> = Vec::new();

    // 1) explicit setting.
    if let Some(path) = class.configured_path(config) {
        if let Some(hit) = probe(&mut attempts, ToolSource::Config, path.to_path_buf()) {
            return resolved(class, hit, attempts);
        }
    }

    // 2) bundled payload.
    for candidate in bundled_candidates(exe_dir, class.binary_name()) {
        if let Some(hit) = probe(&mut attempts, ToolSource::Bundled, candidate) {
            return resolved(class, hit, attempts);
        }
    }

    // 3–5) imported variables: the project's layer first, then the global one.
    if let Some(hit) =
        resolve_from_lookup(&mut attempts, ToolSource::ImportedProject, class, |name| {
            imported.project.get(name).map(String::as_str)
        })
    {
        return resolved(class, hit, attempts);
    }
    if let Some(hit) =
        resolve_from_lookup(&mut attempts, ToolSource::ImportedGlobal, class, |name| {
            imported.global.get(name).map(String::as_str)
        })
    {
        return resolved(class, hit, attempts);
    }

    // 6) process PATH.
    if let Some(raw) = env.get("PATH") {
        for dir in std::env::split_paths(OsStr::new(raw)) {
            if dir.as_os_str().is_empty() {
                continue;
            }
            let candidate = dir.join(class.binary_name());
            if let Some(hit) = probe(&mut attempts, ToolSource::Path, candidate) {
                return resolved(class, hit, attempts);
            }
        }
    }

    // 7) process environment override (the documented last resort).
    if let Some(hit) =
        resolve_from_lookup(&mut attempts, ToolSource::Env, class, |name| env.get(name))
    {
        return resolved(class, hit, attempts);
    }

    ResolvedTool {
        tool: class,
        path: None,
        source: None,
        error: Some(not_found_message(class, &attempts)),
        attempts,
    }
}

/// Resolve one Verible tool against the product-wide order.
///
/// Kept as the Verible entry point (the configuration keys are Verible's); the
/// ordering is [`resolve_tool`]'s, so adding a tool class never adds an order.
pub fn resolve_verible(
    tool: VeribleTool,
    config: &Config,
    env: &Env,
    exe_dir: &Path,
) -> ResolvedTool {
    resolve_tool(tool.class(), config, env, &ImportedVars::default(), exe_dir)
}

/// Probe the `VEDIT_<TOOL>` override and its `VEDIT_<TOOL>_DIR` form from one
/// variable layer, recording both attempts.
fn resolve_from_lookup<'a>(
    attempts: &mut Vec<Attempt>,
    source: ToolSource,
    class: ToolClass,
    lookup: impl Fn(&str) -> Option<&'a str>,
) -> Option<(PathBuf, ToolSource)> {
    if let Some(raw) = lookup(class.env_var()).map(str::trim) {
        if !raw.is_empty() {
            if let Some(hit) = probe(attempts, source, PathBuf::from(raw)) {
                return Some(hit);
            }
        }
    }
    let dir_var = format!("{}_DIR", class.env_var());
    if let Some(dir) = lookup(&dir_var).map(str::trim) {
        if !dir.is_empty() {
            let candidate = PathBuf::from(dir).join(class.binary_name());
            if let Some(hit) = probe(attempts, source, candidate) {
                return Some(hit);
            }
        }
    }
    for legacy in class.legacy_dir_vars() {
        if let Some(dir) = lookup(legacy).map(str::trim) {
            if !dir.is_empty() {
                let candidate = PathBuf::from(dir).join(class.binary_name());
                if let Some(hit) = probe(attempts, source, candidate) {
                    return Some(hit);
                }
            }
        }
    }
    None
}

fn resolved(class: ToolClass, hit: (PathBuf, ToolSource), attempts: Vec<Attempt>) -> ResolvedTool {
    ResolvedTool {
        tool: class,
        path: Some(hit.0),
        source: Some(hit.1),
        attempts,
        error: None,
    }
}

/// True for an existing regular file the owner can execute.
///
/// A candidate that exists but cannot be executed is NOT a hit: keeping it
/// would shadow a later, runnable copy of the same tool (and the C++ lane
/// applies the same rule, so both halves answer identically).
fn is_executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(path)
        .is_ok_and(|metadata| metadata.is_file() && metadata.permissions().mode() & 0o111 != 0)
}

fn probe(
    attempts: &mut Vec<Attempt>,
    source: ToolSource,
    path: PathBuf,
) -> Option<(PathBuf, ToolSource)> {
    let found = is_executable(&path);
    attempts.push(Attempt {
        source,
        path: path.clone(),
        found,
    });
    found.then_some((path, source))
}

/// Candidate locations inside a shipped payload, in probe order.
///
/// `exe_dir/bin/<tool>` covers the AppImage AppDir-root layout: the launcher
/// executes its bundled loader, so the product's executable directory is the
/// package root rather than `bin/`.
fn bundled_candidates(exe_dir: &Path, binary: &str) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(parent) = exe_dir.parent() {
        candidates.push(parent.join("lib/vedit/bin").join(binary));
        candidates.push(parent.join("bin").join(binary));
    }
    candidates.push(exe_dir.join("bin").join(binary));
    candidates.push(exe_dir.join(binary));
    candidates
}

fn not_found_message(class: ToolClass, attempts: &[Attempt]) -> String {
    let mut stages: Vec<&'static str> = Vec::new();
    for attempt in attempts {
        let stage = attempt.stage().as_str();
        if stages.last() != Some(&stage) {
            stages.push(stage);
        }
    }
    let mut message = format!(
        "{} was not found; probed {} location(s) in the product-wide order ({}):",
        class.binary_name(),
        attempts.len(),
        stages.join(" -> ")
    );
    for attempt in attempts {
        message.push_str(&format!(
            "\n  stage={} source={} path={} found={}",
            attempt.stage().as_str(),
            attempt.source.as_str(),
            attempt.path.display(),
            attempt.found
        ));
    }
    message
}

#[cfg(test)]
mod tests {
    use super::{resolve_verible, Attempt, ResolutionStage, ToolSource, VeribleTool};
    use crate::env::Env;
    use crate::model::Config;
    use crate::test_support::write;

    /// Write a fixture the resolver must accept: a regular file with the
    /// executable bit set (the resolver requires it, exactly like the C++ lane).
    fn write_exec(path: &std::path::Path) -> Result<(), Box<dyn std::error::Error>> {
        use std::os::unix::fs::PermissionsExt;
        write(path, "#!/bin/sh\n")?;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755))?;
        Ok(())
    }

    /// The attempts must be probed in the product-wide order: the recorded
    /// stages are non-decreasing under `ResolutionStage`'s `Ord`.
    fn assert_stage_order(attempts: &[Attempt]) {
        let stages: Vec<ResolutionStage> = attempts.iter().map(Attempt::stage).collect();
        let mut sorted = stages.clone();
        sorted.sort();
        assert_eq!(
            stages, sorted,
            "attempts must be probed in ResolutionStage::ORDER"
        );
    }

    #[test]
    fn configured_path_wins() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let binary = temp.path().join("verible-verilog-ls");
        write_exec(&binary)?;
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
    fn missing_config_falls_through_bundled_to_the_process_env(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let env_binary = temp.path().join("from-env");
        write_exec(&env_binary)?;
        let mut config = Config::default();
        config.verible.ls = Some(temp.path().join("does-not-exist"));
        let env = Env::from_pairs([("VEDIT_VERIBLE_LS", env_binary.display().to_string())]);
        let resolved = resolve_verible(VeribleTool::Ls, &config, &env, temp.path());
        // Config (missing) -> four bundled candidates (missing) -> env (found).
        assert_eq!(resolved.source, Some(ToolSource::Env));
        assert_eq!(resolved.attempts.len(), 6);
        assert_eq!(resolved.attempts[0].source, ToolSource::Config);
        assert!(!resolved.attempts[0].found);
        assert_eq!(
            resolved.attempts[1].source,
            ToolSource::Bundled,
            "the bundled payload is probed before the process environment"
        );
        assert!(resolved.attempts[5].found);
        assert_stage_order(&resolved.attempts);
        Ok(())
    }

    #[test]
    fn failure_lists_every_attempt_in_the_resolution_order(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let path_dir = temp.path().join("path-dir");
        std::fs::create_dir_all(&path_dir)?;
        let config = Config::default();
        let env = Env::from_pairs([
            ("PATH", path_dir.display().to_string()),
            (
                "VEDIT_VERIBLE_FORMAT",
                temp.path().join("env-miss").display().to_string(),
            ),
        ]);
        let resolved = resolve_verible(VeribleTool::Format, &config, &env, temp.path());
        assert!(resolved.path.is_none());
        assert!(resolved.source.is_none());
        // Four bundled candidates, the PATH directory, then the env override.
        assert_eq!(resolved.attempts.len(), 6, "bundled, then PATH, then env");
        assert_eq!(
            resolved.attempts.last().map(|attempt| attempt.source),
            Some(ToolSource::Env)
        );
        assert_stage_order(&resolved.attempts);
        let Some(error) = resolved.error else {
            return Err("a missing tool must carry an error".into());
        };
        assert!(error.contains("verible-verilog-format"));
        assert!(
            error.contains("bundled -> PATH -> env"),
            "the error prints the stages actually probed: {error}"
        );
        for attempt in &resolved.attempts {
            assert!(
                error.contains(&attempt.path.display().to_string()),
                "error must list every attempted path"
            );
        }
        Ok(())
    }

    #[test]
    fn the_legacy_verible_dir_variable_still_resolves() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let dir = temp.path().join("legacy");
        std::fs::create_dir_all(&dir)?;
        let binary = dir.join("verible-verilog-lint");
        write_exec(&binary)?;
        let env = Env::from_pairs([("VEDIT_VERIBLE_DIR", dir.display().to_string())]);
        let resolved = resolve_verible(VeribleTool::Lint, &Config::default(), &env, temp.path());
        assert_eq!(resolved.source, Some(ToolSource::Env));
        assert_eq!(resolved.path, Some(binary));
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
        write_exec(&binary)?;
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

    #[test]
    fn bundled_payload_is_found_in_the_appdir_root_bin() -> Result<(), Box<dyn std::error::Error>> {
        // The AppImage launcher runs its bundled loader from the AppDir root, so
        // the executable directory IS the package root and the tools sit in
        // `<AppDir>/bin/`. The tarball base `exe_dir/../bin` stays absent.
        let temp = tempfile::tempdir()?;
        let exe_dir = temp.path().join("AppDir");
        let bin_dir = exe_dir.join("bin");
        std::fs::create_dir_all(&bin_dir)?;
        let binary = bin_dir.join("verible-verilog-lint");
        write_exec(&binary)?;
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
