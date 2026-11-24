//! Lint binary resolution — the product-wide order, shared with the config layer.
//!
//! Order (earlier stage wins; the same seven stages as `vd_config`'s
//! `ResolutionStage::ORDER`, pinned by both crates' tests):
//!
//! 1. explicit option (the caller's setting / tests / packaging);
//! 2. bundled — package-relative paths under the executable directory;
//! 3. local checkout — a development tree's own build artifacts;
//! 4. imported project — a variable imported from the project's shell env;
//! 5. imported global — a variable imported from the user's global shell env;
//! 6. `PATH` — the user's shell `PATH`;
//! 7. `VEDIT_SLANG` / `VEDIT_VERIBLE_LINT` — the process environment, LAST.
//!
//! Every attempt is recorded, so a failure tells the user exactly where the
//! binary was expected and in which stage.
//!
//! The executable directory is not always `bin/`: the AppImage launcher execs
//! the bundled loader from the AppDir root, so `/proc/self/exe` is
//! `<AppDir>/ld-linux-x86-64.so.2` and the tool is reached as
//! `<AppDir>/bin/slang` (`exe_dir/bin/slang`).

use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{OnceLock, RwLock};

/// The product-wide tool-resolution order, one label per stage.
///
/// This is the SAME sequence as `vd_config::provenance::ResolutionStage::ORDER`
/// (`crates/vd-config/src/provenance.rs`), whose test
/// `the_written_order_is_declared_once` pins the identical label list. The two
/// crates do not depend on each other, so the shared contract is the literal
/// list: `stage_labels_match_the_product_order` here fails if this file drifts,
/// and the vd-config test fails if that one drifts.
pub const STAGE_ORDER: [&str; 7] = [
    "setting",
    "bundled",
    "local",
    "imported-project",
    "imported-global",
    "PATH",
    "env",
];

/// Where an attempt came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AttemptSource {
    /// `LintOptions::binary` (tests, packaging) — stage 1, the explicit setting.
    ExplicitOption,
    /// `exe_dir/../lib/vedit/bin/slang` (packaged layout, bundled stage).
    ExeRelativePackageLib,
    /// `exe_dir/../bin/slang` (tarball layout, bundled stage).
    ExeRelativeBin,
    /// `exe_dir/bin/slang` (AppDir-root layout, bundled stage).
    ExeRelativeRootBin,
    /// `exe_dir/slang` (side-by-side layout, bundled stage).
    ExeDirectory,
    /// A source checkout's own build artifact (stage 3).
    LocalCheckout,
    /// A variable imported from the PROJECT's shell environment (stage 4).
    ImportedProject,
    /// A variable imported from the USER's global shell environment (stage 5).
    ImportedGlobal,
    /// A directory on the user's `PATH` (stage 6).
    Path,
    /// The `VEDIT_SLANG` environment variable (stage 7, last).
    Environment,
    /// The `VEDIT_VERIBLE_LINT` environment variable (stage 7, last).
    EnvironmentVerible,
    /// `exe_dir/../lib/vedit/bin/verible-verilog-lint` (bundled stage).
    ExeRelativePackageLibVerible,
    /// `exe_dir/../bin/verible-verilog-lint` (bundled stage).
    ExeRelativeBinVerible,
    /// `exe_dir/bin/verible-verilog-lint` (AppDir-root layout, bundled stage).
    ExeRelativeRootBinVerible,
    /// `exe_dir/verible-verilog-lint` (side-by-side layout, bundled stage).
    ExeDirectoryVerible,
}

impl AttemptSource {
    /// Stable label for logs and `--selftest` output.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::ExplicitOption => "options.binary",
            Self::ExeRelativePackageLib => "exe_dir/../lib/vedit/bin/slang",
            Self::ExeRelativeBin => "exe_dir/../bin/slang",
            Self::ExeRelativeRootBin => "exe_dir/bin/slang",
            Self::ExeDirectory => "exe_dir/slang",
            Self::LocalCheckout => "local checkout",
            Self::ImportedProject => "imported project",
            Self::ImportedGlobal => "imported global",
            Self::Path => "PATH",
            Self::Environment => "VEDIT_SLANG",
            Self::EnvironmentVerible => "VEDIT_VERIBLE_LINT",
            Self::ExeRelativePackageLibVerible => "exe_dir/../lib/vedit/bin/verible-verilog-lint",
            Self::ExeRelativeBinVerible => "exe_dir/../bin/verible-verilog-lint",
            Self::ExeRelativeRootBinVerible => "exe_dir/bin/verible-verilog-lint",
            Self::ExeDirectoryVerible => "exe_dir/verible-verilog-lint",
        }
    }

    /// The stage index in [`STAGE_ORDER`] (lower is probed earlier and wins).
    #[must_use]
    pub const fn stage(self) -> usize {
        match self {
            Self::ExplicitOption => 0,
            Self::ExeRelativePackageLib
            | Self::ExeRelativeBin
            | Self::ExeRelativeRootBin
            | Self::ExeDirectory
            | Self::ExeRelativePackageLibVerible
            | Self::ExeRelativeBinVerible
            | Self::ExeRelativeRootBinVerible
            | Self::ExeDirectoryVerible => 1,
            Self::LocalCheckout => 2,
            Self::ImportedProject => 3,
            Self::ImportedGlobal => 4,
            Self::Path => 5,
            Self::Environment | Self::EnvironmentVerible => 6,
        }
    }

    /// The stage label, the token shared with the config layer's order table.
    #[must_use]
    pub const fn stage_label(self) -> &'static str {
        STAGE_ORDER[self.stage()]
    }
}

/// Result of probing one candidate.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AttemptOutcome {
    /// Existing, a regular file and executable.
    Found,
    /// No such file.
    NotFound,
    /// Present but not a regular executable file.
    NotExecutable,
    /// The environment variable was not set (recorded for completeness).
    EnvUnset,
}

impl AttemptOutcome {
    /// Stable label for logs.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Found => "found",
            Self::NotFound => "not found",
            Self::NotExecutable => "not executable",
            Self::EnvUnset => "unset",
        }
    }
}

/// One line of the resolution report.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BinaryAttempt {
    /// Where the candidate came from.
    pub source: AttemptSource,
    /// The probed path (`None` for an unset environment variable).
    pub path: Option<PathBuf>,
    /// What happened.
    pub outcome: AttemptOutcome,
}

impl BinaryAttempt {
    /// One-line description for logs.
    #[must_use]
    pub fn describe(&self) -> String {
        match &self.path {
            Some(path) => format!(
                "{}: {} [{}]",
                self.source.as_str(),
                path.display(),
                self.outcome.as_str()
            ),
            None => format!("{}: {}", self.source.as_str(), self.outcome.as_str()),
        }
    }
}

/// The chosen binary plus every attempt that was made.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BinaryResolution {
    /// The first executable candidate, if any.
    pub chosen: Option<PathBuf>,
    /// Every candidate that was probed, in order.
    pub attempts: Vec<BinaryAttempt>,
}

impl BinaryResolution {
    /// Multi-line description (used by examples and failure messages).
    #[must_use]
    pub fn describe(&self) -> String {
        let mut out = String::new();
        for attempt in &self.attempts {
            out.push_str("  ");
            out.push_str(&attempt.describe());
            out.push('\n');
        }
        out
    }
}

/// Resolve the slang binary from the given inputs (pure; testable).
///
/// This form consults no process `PATH` and no imported layers, so tests stay
/// deterministic; [`resolve_binary_for_current_process`] is the product entry
/// point and passes them. Every stage of [`STAGE_ORDER`] is still probed.
#[must_use]
pub fn resolve_binary(
    explicit: Option<&Path>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    let empty = std::collections::BTreeMap::new();
    resolve_binary_with_imported(explicit, &empty, &empty, None, env_value, exe_dir)
}

/// Resolve slang through the FULL product order (all seven stages).
#[must_use]
pub fn resolve_binary_with_imported(
    explicit: Option<&Path>,
    imported_project: &std::collections::BTreeMap<String, String>,
    imported_global: &std::collections::BTreeMap<String, String>,
    path_value: Option<&str>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    resolve_in_product_order(
        "slang",
        "VEDIT_SLANG",
        AttemptSource::Environment,
        explicit,
        slang_bundled_candidates(exe_dir),
        imported_project,
        imported_global,
        path_value,
        env_value,
        exe_dir,
    )
}

/// The bundled-payload candidates for slang, in probe order.
fn slang_bundled_candidates(exe_dir: &Path) -> [(AttemptSource, PathBuf); 4] {
    [
        (
            AttemptSource::ExeRelativePackageLib,
            exe_dir.join("../lib/vedit/bin/slang"),
        ),
        (AttemptSource::ExeRelativeBin, exe_dir.join("../bin/slang")),
        (AttemptSource::ExeRelativeRootBin, exe_dir.join("bin/slang")),
        (AttemptSource::ExeDirectory, exe_dir.join("slang")),
    ]
}

/// Resolve for the current process (the product entry point: full order).
#[must_use]
pub fn resolve_binary_for_current_process(explicit: Option<&Path>) -> BinaryResolution {
    let env_value = std::env::var_os("VEDIT_SLANG");
    let path_value = std::env::var("PATH").ok();
    let exe_dir = executable_dir();
    let empty = std::collections::BTreeMap::new();
    resolve_binary_with_imported(
        explicit,
        &empty,
        &empty,
        path_value.as_deref(),
        env_value.as_deref(),
        &exe_dir,
    )
}

/// The imported environment every tool child receives (T10 seam 1 / AC-4.7).
///
/// `vd-deeplint` does not depend on `vd-config`, so the ALREADY MERGED layer is
/// handed over as a plain map by the one writer,
/// `crates/vd-ffi/src/env.rs` (`EnvironmentSelection::merged_for`: global first,
/// then the project's own entries, so the project layer wins). The lock exists
/// because a re-import replaces the map at runtime.
static IMPORTED_ENVIRONMENT: OnceLock<RwLock<BTreeMap<String, String>>> = OnceLock::new();

fn imported_environment_slot() -> &'static RwLock<BTreeMap<String, String>> {
    IMPORTED_ENVIRONMENT.get_or_init(|| RwLock::new(BTreeMap::new()))
}

/// Replace the imported environment that every tool child merges in (T10 seam 1).
///
/// The caller passes the MERGED selection, so the child sees
/// `process < global < project` with the project layer winning (the process
/// values are the inherited ones; this map overrides them). The sanitize rule
/// still wins over an imported variable of the same name: `LD_LIBRARY_PATH` /
/// `LOCPATH` are removed AFTER the merge ([`sanitize_tool_child_env`]).
pub fn set_imported_environment(environment: BTreeMap<String, String>) {
    let mut slot = imported_environment_slot()
        .write()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    *slot = environment;
}

/// A copy of the imported environment: the SAME layer the tool children get,
/// read by the project-filelist seam (`Variables::imported`) so both consumers
/// see one merged selection.
#[must_use]
pub fn imported_environment() -> BTreeMap<String, String> {
    imported_environment_slot()
        .read()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .clone()
}

/// Start a bundled tool child without the packaged loader environment.
///
/// The AppDir's `AppRun` execs the product through the packaged loader with
/// `LD_LIBRARY_PATH=<AppDir>/lib` (packaged glibc) and
/// `LOCPATH=<AppDir>/usr/lib/locale`. A bundled tool such as slang is a
/// glibc-2.17 build whose interpreter is the **host** loader; with
/// `LD_LIBRARY_PATH` inherited, that host loader (e.g. CentOS 7, glibc 2.17)
/// tries to load the packaged glibc and aborts before `main` with
/// `version 'GLIBC_2.35' not found`. `LOCPATH` follows the same packaged-data
/// rule (locale archives are glibc-version specific). Tool children therefore
/// never inherit either variable; they use the host's own libraries and locale
/// archive. The VCS runner applies the same rule
/// (`crates/vd-vcs/src/runner.rs`).
///
/// T10 (AC-4.7): this is also the ONE seam every lint / catalog / hierarchy
/// child passes, so the imported environment is merged here — AFTER the child's
/// inherited process environment (explicit command envs override inherited
/// ones) and BEFORE the two loader removals, which is what keeps the sanitize
/// rule absolute. The PARENT process environment is never modified.
pub fn sanitize_tool_child_env(command: &mut Command) {
    for (name, value) in imported_environment() {
        command.env(name, value);
    }
    command.env_remove("LD_LIBRARY_PATH").env_remove("LOCPATH");
}

/// Resolve `verible-verilog-lint` from the given inputs (pure; testable).
///
/// Same full product order as slang. This form consults no process `PATH` (so
/// tests stay deterministic); the product entry point does.
#[must_use]
pub fn resolve_verible_lint(
    explicit: Option<&Path>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    let empty = std::collections::BTreeMap::new();
    resolve_verible_lint_with_imported(explicit, &empty, &empty, None, env_value, exe_dir)
}

/// Resolve `verible-verilog-lint` through the FULL product order.
#[must_use]
pub fn resolve_verible_lint_with_imported(
    explicit: Option<&Path>,
    imported_project: &std::collections::BTreeMap<String, String>,
    imported_global: &std::collections::BTreeMap<String, String>,
    path_value: Option<&str>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    resolve_in_product_order(
        "verible-verilog-lint",
        "VEDIT_VERIBLE_LINT",
        AttemptSource::EnvironmentVerible,
        explicit,
        verible_bundled_candidates(exe_dir),
        imported_project,
        imported_global,
        path_value,
        env_value,
        exe_dir,
    )
}

/// The bundled-payload candidates for verible-verilog-lint, in probe order.
fn verible_bundled_candidates(exe_dir: &Path) -> [(AttemptSource, PathBuf); 4] {
    [
        (
            AttemptSource::ExeRelativePackageLibVerible,
            exe_dir.join("../lib/vedit/bin/verible-verilog-lint"),
        ),
        (
            AttemptSource::ExeRelativeBinVerible,
            exe_dir.join("../bin/verible-verilog-lint"),
        ),
        (
            AttemptSource::ExeRelativeRootBinVerible,
            exe_dir.join("bin/verible-verilog-lint"),
        ),
        (
            AttemptSource::ExeDirectoryVerible,
            exe_dir.join("verible-verilog-lint"),
        ),
    ]
}

/// Resolve `verible-verilog-lint` for the current process (full product order).
#[must_use]
pub fn resolve_verible_lint_for_current_process(explicit: Option<&Path>) -> BinaryResolution {
    let env_value = std::env::var_os("VEDIT_VERIBLE_LINT");
    let path_value = std::env::var("PATH").ok();
    let exe_dir = executable_dir();
    let empty = std::collections::BTreeMap::new();
    resolve_verible_lint_with_imported(
        explicit,
        &empty,
        &empty,
        path_value.as_deref(),
        env_value.as_deref(),
        &exe_dir,
    )
}

fn executable_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// The ONE product-wide resolver: both tools walk [`STAGE_ORDER`] in the same
/// sequence and every probe is recorded with its stage, so a failure shows the
/// user exactly where the binary was expected and in which stage.
///
/// The imported layers are an empty seam in the product today (their source is
/// the shell-environment importer); the parameters exist so a caller or a test
/// can stage them, exactly like `vd-config`'s `ImportedVars`.
#[allow(clippy::too_many_arguments)]
fn resolve_in_product_order(
    name: &str,
    env_name: &str,
    env_unset_source: AttemptSource,
    explicit: Option<&Path>,
    bundled: [(AttemptSource, PathBuf); 4],
    imported_project: &std::collections::BTreeMap<String, String>,
    imported_global: &std::collections::BTreeMap<String, String>,
    path_value: Option<&str>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    let mut attempts = Vec::new();

    // 1) explicit setting.
    if let Some(path) = explicit {
        probe(AttemptSource::ExplicitOption, path, &mut attempts);
    }
    // 2) bundled payload.
    for (source, candidate) in &bundled {
        probe(*source, candidate, &mut attempts);
    }
    // 3) local checkout.
    for candidate in checkout_candidates(exe_dir, name) {
        probe(AttemptSource::LocalCheckout, &candidate, &mut attempts);
    }
    // 4-5) imported variables: the project's layer before the global one.
    probe_variable_layer(
        imported_project,
        env_name,
        name,
        AttemptSource::ImportedProject,
        &mut attempts,
    );
    probe_variable_layer(
        imported_global,
        env_name,
        name,
        AttemptSource::ImportedGlobal,
        &mut attempts,
    );
    // 6) the user's PATH.
    for directory in path_directories(path_value) {
        probe(AttemptSource::Path, &directory.join(name), &mut attempts);
    }
    // 7) the process environment, LAST (never silently preferred over the rest).
    match env_value {
        Some(value) => probe(env_unset_source, Path::new(value), &mut attempts),
        None => attempts.push(BinaryAttempt {
            source: env_unset_source,
            path: None,
            outcome: AttemptOutcome::EnvUnset,
        }),
    }

    let chosen = attempts
        .iter()
        .find(|attempt| attempt.outcome == AttemptOutcome::Found)
        .and_then(|attempt| attempt.path.clone());
    BinaryResolution { chosen, attempts }
}

/// Probe one variable layer: the `VEDIT_<TOOL>` override then its `_DIR` form.
fn probe_variable_layer(
    vars: &std::collections::BTreeMap<String, String>,
    env_name: &str,
    name: &str,
    source: AttemptSource,
    attempts: &mut Vec<BinaryAttempt>,
) {
    if let Some(value) = vars
        .get(env_name)
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
    {
        probe(source, Path::new(value), attempts);
    }
    let dir_name = format!("{env_name}_DIR");
    if let Some(value) = vars
        .get(&dir_name)
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
    {
        probe(source, &Path::new(value).join(name), attempts);
    }
}

fn path_directories(value: Option<&str>) -> Vec<PathBuf> {
    let Some(raw) = value else {
        return Vec::new();
    };
    std::env::split_paths(OsStr::new(raw))
        .filter(|directory| !directory.as_os_str().is_empty())
        .collect()
}

/// The source-checkout root: walk up from the executable until the repository's
/// own marker files are seen (the same markers the C++ toolchain lane uses).
fn checkout_root(start: &Path) -> Option<PathBuf> {
    let mut directory = start.to_path_buf();
    for _ in 0..10 {
        if directory.join("scripts/fetch-verible.sh").is_file()
            || directory.join("build/slang/build.sh").is_file()
        {
            return Some(directory);
        }
        if !directory.pop() {
            break;
        }
    }
    None
}

/// A source checkout's own artifacts (bare-build discovery).
fn checkout_candidates(exe_dir: &Path, name: &str) -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    let Some(root) = checkout_root(exe_dir) else {
        return candidates;
    };
    match name {
        "slang" => candidates.push(root.join("build/slang/out/slang")),
        "verible-verilog-lint" => {
            candidates.push(root.join(".mpd/recon/verible/bin/verible-verilog-lint"));
            let extract = root.join("target/verible-pin/extract");
            if let Ok(entries) = std::fs::read_dir(&extract) {
                let mut versions: Vec<PathBuf> = entries
                    .filter_map(|entry| entry.ok().map(|entry| entry.path()))
                    .filter(|path| {
                        path.file_name()
                            .map(|name| name.to_string_lossy().starts_with("verible-"))
                            .unwrap_or(false)
                    })
                    .collect();
                versions.sort();
                for version in versions {
                    candidates.push(version.join("bin/verible-verilog-lint"));
                }
            }
        }
        _ => {}
    }
    candidates
}

fn probe(source: AttemptSource, path: &Path, attempts: &mut Vec<BinaryAttempt>) {
    let outcome = match std::fs::metadata(path) {
        Err(_) => AttemptOutcome::NotFound,
        Ok(metadata) => {
            if metadata.is_file() && is_executable(&metadata) {
                AttemptOutcome::Found
            } else {
                AttemptOutcome::NotExecutable
            }
        }
    };
    attempts.push(BinaryAttempt {
        source,
        path: Some(path.to_path_buf()),
        outcome,
    });
}

#[cfg(unix)]
fn is_executable(metadata: &std::fs::Metadata) -> bool {
    use std::os::unix::fs::PermissionsExt;
    metadata.permissions().mode() & 0o111 != 0
}

#[cfg(not(unix))]
fn is_executable(_metadata: &std::fs::Metadata) -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    static DIRS: AtomicU64 = AtomicU64::new(0);

    struct TempDir(PathBuf);

    impl TempDir {
        fn new(label: &str) -> Self {
            let serial = DIRS.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir().join(format!(
                "vd-deeplint-bin-{}-{serial}-{label}",
                std::process::id()
            ));
            fs::create_dir_all(&path)
                .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
            Self(path)
        }

        fn script(&self, name: &str) -> PathBuf {
            let path = self.0.join(name);
            fs::write(&path, "#!/bin/sh\nexit 0\n")
                .unwrap_or_else(|error| panic!("writing script failed: {error}"));
            make_executable(&path);
            path
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            if let Err(error) = fs::remove_dir_all(&self.0) {
                eprintln!("WARN: could not remove {}: {error}", self.0.display());
            }
        }
    }

    #[cfg(unix)]
    fn make_executable(path: &Path) {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(metadata) = fs::metadata(path) {
            let mut permissions = metadata.permissions();
            permissions.set_mode(0o755);
            if let Err(error) = fs::set_permissions(path, permissions) {
                panic!("chmod failed: {error}");
            }
        }
    }

    #[cfg(not(unix))]
    fn make_executable(_path: &Path) {}

    #[test]
    fn explicit_option_wins_and_is_recorded() {
        let dir = TempDir::new("explicit");
        let binary = dir.script("slang");
        let resolution = resolve_binary(Some(&binary), None, &dir.0);
        assert_eq!(resolution.chosen, Some(binary));
        assert_eq!(resolution.attempts[0].source, AttemptSource::ExplicitOption);
        assert_eq!(resolution.attempts[0].outcome, AttemptOutcome::Found);
        assert_eq!(
            resolution.attempts.last().map(|attempt| attempt.outcome),
            Some(AttemptOutcome::EnvUnset),
            "the environment stage is probed LAST"
        );
    }

    #[test]
    fn environment_variable_is_used_when_set() {
        let dir = TempDir::new("env");
        let binary = dir.script("slang-env");
        let os = binary.clone().into_os_string();
        let resolution = resolve_binary(None, Some(&os), &dir.0);
        assert_eq!(resolution.chosen, Some(binary));
        let env_attempt = resolution
            .attempts
            .last()
            .unwrap_or_else(|| panic!("the resolution records at least one attempt"));
        assert_eq!(env_attempt.source, AttemptSource::Environment);
        assert_eq!(env_attempt.outcome, AttemptOutcome::Found);
        assert!(
            resolution.attempts.len() > 1,
            "the environment stage must be the LAST of several stages"
        );
    }

    #[test]
    fn all_attempts_are_listed_when_nothing_is_found() {
        let dir = TempDir::new("missing");
        let resolution = resolve_binary(None, None, &dir.0);
        assert!(resolution.chosen.is_none());
        // 4 bundled probes + the env stage (unset) = 5; no PATH, no checkout.
        assert_eq!(resolution.attempts.len(), 5);
        assert_eq!(
            resolution.attempts.last().map(|attempt| attempt.outcome),
            Some(AttemptOutcome::EnvUnset)
        );
        assert!(resolution
            .attempts
            .iter()
            .take(4)
            .all(|attempt| attempt.outcome == AttemptOutcome::NotFound));
        let described = resolution.describe();
        assert!(described.contains("VEDIT_SLANG"));
        assert!(described.contains("../lib/vedit/bin/slang"));
        assert!(described.contains("../bin/slang"));
        assert!(described.contains("exe_dir/bin/slang"));
    }

    #[test]
    fn package_layout_is_found_relative_to_the_executable() {
        let dir = TempDir::new("package");
        // exe_dir = <dir>/lib/x, so `exe_dir/../bin/slang` is <dir>/lib/bin/slang.
        let bin_dir = dir.0.join("lib").join("bin");
        fs::create_dir_all(&bin_dir).unwrap_or_else(|error| panic!("mkdir failed: {error}"));
        let binary = bin_dir.join("slang");
        fs::write(&binary, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));
        make_executable(&binary);
        // The exe_dir must exist for `..` to resolve on POSIX.
        let exe_dir = dir.0.join("lib").join("x");
        fs::create_dir_all(&exe_dir).unwrap_or_else(|error| panic!("mkdir failed: {error}"));
        let resolution = resolve_binary(None, None, &exe_dir);
        assert_eq!(resolution.chosen, Some(exe_dir.join("../bin/slang")));
    }

    #[test]
    fn appdir_root_layout_is_found_relative_to_the_executable() {
        // The AppImage AppRun execs the bundled loader from the AppDir root, so
        // `/proc/self/exe` is the loader and `executable_dir()` is the AppDir
        // ROOT. The binary still lives in `<AppDir>/bin/slang`.
        let dir = TempDir::new("appdir-root");
        let appdir = dir.0.join("AppDir");
        let bin_dir = appdir.join("bin");
        fs::create_dir_all(&bin_dir).unwrap_or_else(|error| panic!("mkdir failed: {error}"));
        let binary = bin_dir.join("slang");
        fs::write(&binary, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));
        make_executable(&binary);
        // Negative control: the tarball base `exe_dir/../bin/slang` must be absent,
        // so the chosen path can only come from the new `exe_dir/bin/slang` base.
        assert!(!dir.0.join("bin/slang").exists());
        let resolution = resolve_binary(None, None, &appdir);
        assert_eq!(resolution.chosen, Some(binary));
        let found = resolution
            .attempts
            .iter()
            .find(|attempt| attempt.outcome == AttemptOutcome::Found)
            .unwrap_or_else(|| panic!("a found attempt must be recorded"));
        assert_eq!(found.source, AttemptSource::ExeRelativeRootBin);
    }

    #[test]
    fn appdir_root_layout_finds_verible_lint() {
        let dir = TempDir::new("appdir-root-verible");
        let appdir = dir.0.join("AppDir");
        let bin_dir = appdir.join("bin");
        fs::create_dir_all(&bin_dir).unwrap_or_else(|error| panic!("mkdir failed: {error}"));
        let binary = bin_dir.join("verible-verilog-lint");
        fs::write(&binary, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));
        make_executable(&binary);
        assert!(!dir.0.join("bin/verible-verilog-lint").exists());
        let resolution = resolve_verible_lint(None, None, &appdir);
        assert_eq!(resolution.chosen, Some(binary));
        let found = resolution
            .attempts
            .iter()
            .find(|attempt| attempt.outcome == AttemptOutcome::Found)
            .unwrap_or_else(|| panic!("a found attempt must be recorded"));
        assert_eq!(found.source, AttemptSource::ExeRelativeRootBinVerible);
    }

    #[test]
    fn non_executable_file_is_not_chosen() {
        let dir = TempDir::new("noexec");
        let path = dir.0.join("slang");
        fs::write(&path, "not a program").unwrap_or_else(|error| panic!("write failed: {error}"));
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            if let Ok(metadata) = fs::metadata(&path) {
                let mut permissions = metadata.permissions();
                permissions.set_mode(0o644);
                if let Err(error) = fs::set_permissions(&path, permissions) {
                    panic!("chmod failed: {error}");
                }
            }
        }
        let resolution = resolve_binary(None, Some(path.as_os_str()), &dir.0);
        assert!(resolution.chosen.is_none());
        // T8 / AC-T8.1: the product order probes the environment LAST, so the
        // non-executable override is its own attempt — not `attempts[0]`, which
        // is the first bundled spelling now.
        let env_attempt = resolution
            .attempts
            .iter()
            .find(|attempt| attempt.source == AttemptSource::Environment)
            .unwrap_or_else(|| panic!("the env stage must be recorded: {:?}", resolution.attempts));
        assert_eq!(env_attempt.outcome, AttemptOutcome::NotExecutable);
    }

    #[test]
    fn tool_children_request_the_packaged_loader_variables_be_removed() {
        let mut command = Command::new("true");
        command
            .env("LD_LIBRARY_PATH", "/appdir/lib")
            .env("LOCPATH", "/appdir/usr/lib/locale")
            .env("VEDIT_SLANG", "/appdir/bin/slang");
        sanitize_tool_child_env(&mut command);
        let envs: Vec<(std::ffi::OsString, Option<std::ffi::OsString>)> = command
            .get_envs()
            .map(|(key, value)| (key.to_os_string(), value.map(|value| value.to_os_string())))
            .collect();
        for removed in ["LD_LIBRARY_PATH", "LOCPATH"] {
            assert!(
                envs.iter()
                    .any(|(key, value)| key.as_os_str() == OsStr::new(removed) && value.is_none()),
                "{removed} must be removed, got {envs:?}"
            );
        }
        assert!(
            envs.iter().any(|(key, value)| key.as_os_str() == OsStr::new("VEDIT_SLANG")
                && value.is_some()),
            "unrelated variables are kept, got {envs:?}"
        );
    }

    /// The literal seven-label order shared with `vd-config`'s
    /// `ResolutionStage::ORDER`; a drift on EITHER side fails a test.
    #[test]
    fn stage_labels_match_the_product_order() {
        assert_eq!(
            STAGE_ORDER,
            [
                "setting",
                "bundled",
                "local",
                "imported-project",
                "imported-global",
                "PATH",
                "env"
            ]
        );
        let sources = [
            AttemptSource::ExplicitOption,
            AttemptSource::ExeRelativeRootBin,
            AttemptSource::LocalCheckout,
            AttemptSource::ImportedProject,
            AttemptSource::ImportedGlobal,
            AttemptSource::Path,
            AttemptSource::Environment,
        ];
        for (index, source) in sources.iter().enumerate() {
            assert_eq!(
                source.stage(),
                index,
                "{} is at the wrong stage",
                source.as_str()
            );
            assert_eq!(source.stage_label(), STAGE_ORDER[index]);
        }
        // The verible arm walks the same stages.
        assert_eq!(AttemptSource::EnvironmentVerible.stage(), 6);
        assert_eq!(AttemptSource::ExeDirectoryVerible.stage(), 1);
    }

    #[test]
    fn bundled_payload_beats_the_environment_variable() {
        let dir = TempDir::new("bundled-first");
        let bundled = dir.0.join("bin").join("slang");
        fs::create_dir_all(dir.0.join("bin"))
            .unwrap_or_else(|error| panic!("create bin dir: {error}"));
        fs::write(&bundled, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write bundled slang: {error}"));
        make_executable(&bundled);
        let env = dir.script("slang-env");
        let os = env.into_os_string();
        let resolution = resolve_binary(None, Some(&os), &dir.0);
        assert_eq!(
            resolution.chosen,
            Some(bundled),
            "bundled must win over env"
        );
        let env_attempt = resolution
            .attempts
            .iter()
            .find(|attempt| attempt.source == AttemptSource::Environment)
            .unwrap_or_else(|| panic!("the env stage is still recorded"));
        assert_eq!(env_attempt.outcome, AttemptOutcome::Found);
    }

    #[test]
    fn path_is_consulted_when_nothing_else_has_the_tool() {
        let dir = TempDir::new("path-stage");
        let path_dir = dir.0.join("on-path");
        fs::create_dir_all(&path_dir).unwrap_or_else(|error| panic!("create path dir: {error}"));
        let binary = path_dir.join("slang");
        fs::write(&binary, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write path slang: {error}"));
        make_executable(&binary);
        let exe_dir = dir.0.join("absent");
        fs::create_dir_all(&exe_dir).unwrap_or_else(|error| panic!("create exe dir: {error}"));
        let empty = std::collections::BTreeMap::new();
        let resolution = resolve_binary_with_imported(
            None,
            &empty,
            &empty,
            Some(path_dir.to_str().unwrap_or_else(|| panic!("utf8 path"))),
            None,
            &exe_dir,
        );
        assert_eq!(resolution.chosen, Some(binary));
        let path_attempt = resolution
            .attempts
            .iter()
            .find(|attempt| attempt.source == AttemptSource::Path)
            .unwrap_or_else(|| panic!("a PATH attempt exists"));
        assert_eq!(path_attempt.outcome, AttemptOutcome::Found);
    }

    #[test]
    fn local_checkout_beats_path_and_environment() {
        let dir = TempDir::new("local-stage");
        let root = dir.0.join("repo");
        fs::create_dir_all(root.join("scripts"))
            .unwrap_or_else(|error| panic!("create scripts: {error}"));
        fs::write(root.join("scripts").join("fetch-verible.sh"), "marker")
            .unwrap_or_else(|error| panic!("write marker: {error}"));
        let checkout = root.join("build/slang/out/slang");
        fs::create_dir_all(root.join("build/slang/out"))
            .unwrap_or_else(|error| panic!("create out dir: {error}"));
        fs::write(&checkout, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write checkout slang: {error}"));
        make_executable(&checkout);
        let path_dir = dir.0.join("on-path");
        fs::create_dir_all(&path_dir).unwrap_or_else(|error| panic!("create path dir: {error}"));
        let on_path = path_dir.join("slang");
        fs::write(&on_path, "#!/bin/sh\nexit 0\n")
            .unwrap_or_else(|error| panic!("write path slang: {error}"));
        make_executable(&on_path);
        let env = dir.script("slang-env");
        let exe_dir = root.join("target/debug");
        fs::create_dir_all(&exe_dir).unwrap_or_else(|error| panic!("create exe dir: {error}"));
        let empty = std::collections::BTreeMap::new();
        let resolution = resolve_binary_with_imported(
            None,
            &empty,
            &empty,
            Some(path_dir.to_str().unwrap_or_else(|| panic!("utf8 path"))),
            Some(env.as_os_str()),
            &exe_dir,
        );
        assert_eq!(resolution.chosen, Some(checkout), "local checkout wins");
        assert_eq!(
            resolution
                .attempts
                .iter()
                .find(|attempt| attempt.outcome == AttemptOutcome::Found)
                .map(|attempt| attempt.source),
            Some(AttemptSource::LocalCheckout)
        );
    }

    #[test]
    fn imported_project_beats_imported_global_and_path() {
        let dir = TempDir::new("imported-stage");
        let exe_dir = dir.0.join("absent");
        fs::create_dir_all(&exe_dir).unwrap_or_else(|error| panic!("create exe dir: {error}"));
        let project_bin = dir.script("slang-project");
        let global_bin = dir.script("slang-global");
        let mut project = std::collections::BTreeMap::new();
        project.insert(
            "VEDIT_SLANG".to_string(),
            project_bin.to_string_lossy().into_owned(),
        );
        let mut global = std::collections::BTreeMap::new();
        global.insert(
            "VEDIT_SLANG".to_string(),
            global_bin.to_string_lossy().into_owned(),
        );
        let resolution = resolve_binary_with_imported(
            None,
            &project,
            &global,
            Some(dir.0.to_str().unwrap_or_else(|| panic!("utf8 path"))),
            None,
            &exe_dir,
        );
        assert_eq!(resolution.chosen, Some(project_bin));
        assert_eq!(
            resolution
                .attempts
                .iter()
                .find(|attempt| attempt.outcome == AttemptOutcome::Found)
                .map(|attempt| attempt.source),
            Some(AttemptSource::ImportedProject)
        );
    }

    #[test]
    fn attempts_are_recorded_in_product_order() {
        let dir = TempDir::new("order");
        let exe_dir = dir.0.join("absent");
        fs::create_dir_all(&exe_dir).unwrap_or_else(|error| panic!("create exe dir: {error}"));
        let mut project = std::collections::BTreeMap::new();
        project.insert("VEDIT_SLANG".to_string(), "/nowhere/project".to_string());
        let mut global = std::collections::BTreeMap::new();
        global.insert("VEDIT_SLANG".to_string(), "/nowhere/global".to_string());
        let resolution = resolve_binary_with_imported(
            None,
            &project,
            &global,
            Some("/nowhere/bin"),
            None,
            &exe_dir,
        );
        let stages: Vec<usize> = resolution
            .attempts
            .iter()
            .map(|attempt| attempt.source.stage())
            .collect();
        let mut sorted = stages.clone();
        sorted.sort_unstable();
        assert_eq!(stages, sorted, "attempts must be probed in STAGE_ORDER");
    }
}
