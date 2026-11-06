//! slang binary resolution.
//!
//! Order (the captain's contract): explicit option (tests / packaging) →
//! `VEDIT_SLANG` environment variable → package-relative paths under the
//! executable directory. **`PATH` is never consulted.** Every attempt is
//! recorded, so a failure tells the user exactly where the binary was expected.
//!
//! The executable directory is not always `bin/`: the AppImage launcher execs
//! the bundled loader from the AppDir root, so `/proc/self/exe` is
//! `<AppDir>/ld-linux-x86-64.so.2` and the tool is reached as
//! `<AppDir>/bin/slang` (`exe_dir/bin/slang`).

use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Where an attempt came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AttemptSource {
    /// `LintOptions::binary` (tests, packaging).
    ExplicitOption,
    /// The `VEDIT_SLANG` environment variable.
    Environment,
    /// `exe_dir/../lib/vedit/bin/slang` (packaged layout).
    ExeRelativePackageLib,
    /// `exe_dir/../bin/slang` (tarball layout).
    ExeRelativeBin,
    /// `exe_dir/bin/slang` (AppDir-root layout: the launcher executes its loader,
    /// so the executable dir is the package root).
    ExeRelativeRootBin,
    /// `exe_dir/slang` (side-by-side layout).
    ExeDirectory,
    /// The `VEDIT_VERIBLE_LINT` environment variable.
    EnvironmentVerible,
    /// `exe_dir/../lib/vedit/bin/verible-verilog-lint` (packaged layout).
    ExeRelativePackageLibVerible,
    /// `exe_dir/../bin/verible-verilog-lint` (tarball layout).
    ExeRelativeBinVerible,
    /// `exe_dir/bin/verible-verilog-lint` (AppDir-root layout).
    ExeRelativeRootBinVerible,
    /// `exe_dir/verible-verilog-lint` (side-by-side layout).
    ExeDirectoryVerible,
}

impl AttemptSource {
    /// Stable label for logs and `--selftest` output.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::ExplicitOption => "options.binary",
            Self::Environment => "VEDIT_SLANG",
            Self::ExeRelativePackageLib => "exe_dir/../lib/vedit/bin/slang",
            Self::ExeRelativeBin => "exe_dir/../bin/slang",
            Self::ExeRelativeRootBin => "exe_dir/bin/slang",
            Self::ExeDirectory => "exe_dir/slang",
            Self::EnvironmentVerible => "VEDIT_VERIBLE_LINT",
            Self::ExeRelativePackageLibVerible => "exe_dir/../lib/vedit/bin/verible-verilog-lint",
            Self::ExeRelativeBinVerible => "exe_dir/../bin/verible-verilog-lint",
            Self::ExeRelativeRootBinVerible => "exe_dir/bin/verible-verilog-lint",
            Self::ExeDirectoryVerible => "exe_dir/verible-verilog-lint",
        }
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
#[must_use]
pub fn resolve_binary(
    explicit: Option<&Path>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    let mut attempts = Vec::new();

    if let Some(path) = explicit {
        probe(AttemptSource::ExplicitOption, path, &mut attempts);
    }
    match env_value {
        Some(value) => probe(AttemptSource::Environment, Path::new(value), &mut attempts),
        None => attempts.push(BinaryAttempt {
            source: AttemptSource::Environment,
            path: None,
            outcome: AttemptOutcome::EnvUnset,
        }),
    }
    probe(
        AttemptSource::ExeRelativePackageLib,
        &exe_dir.join("../lib/vedit/bin/slang"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeRelativeBin,
        &exe_dir.join("../bin/slang"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeRelativeRootBin,
        &exe_dir.join("bin/slang"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeDirectory,
        &exe_dir.join("slang"),
        &mut attempts,
    );

    let chosen = attempts
        .iter()
        .find(|attempt| attempt.outcome == AttemptOutcome::Found)
        .and_then(|attempt| attempt.path.clone());
    BinaryResolution { chosen, attempts }
}

/// Resolve for the current process (uses `VEDIT_SLANG` and the executable dir).
#[must_use]
pub fn resolve_binary_for_current_process(explicit: Option<&Path>) -> BinaryResolution {
    let env_value = std::env::var_os("VEDIT_SLANG");
    let exe_dir = executable_dir();
    resolve_binary(explicit, env_value.as_deref(), &exe_dir)
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
pub(crate) fn sanitize_tool_child_env(command: &mut Command) {
    command.env_remove("LD_LIBRARY_PATH").env_remove("LOCPATH");
}

/// Resolve `verible-verilog-lint` from the given inputs (pure; testable).
///
/// Same order and the same rule as slang: explicit option → `VEDIT_VERIBLE_LINT`
/// → package-relative paths under the executable directory; **`PATH` is never
/// consulted**.
#[must_use]
pub fn resolve_verible_lint(
    explicit: Option<&Path>,
    env_value: Option<&OsStr>,
    exe_dir: &Path,
) -> BinaryResolution {
    let mut attempts = Vec::new();
    if let Some(path) = explicit {
        probe(AttemptSource::ExplicitOption, path, &mut attempts);
    }
    match env_value {
        Some(value) => probe(
            AttemptSource::EnvironmentVerible,
            Path::new(value),
            &mut attempts,
        ),
        None => attempts.push(BinaryAttempt {
            source: AttemptSource::EnvironmentVerible,
            path: None,
            outcome: AttemptOutcome::EnvUnset,
        }),
    }
    probe(
        AttemptSource::ExeRelativePackageLibVerible,
        &exe_dir.join("../lib/vedit/bin/verible-verilog-lint"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeRelativeBinVerible,
        &exe_dir.join("../bin/verible-verilog-lint"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeRelativeRootBinVerible,
        &exe_dir.join("bin/verible-verilog-lint"),
        &mut attempts,
    );
    probe(
        AttemptSource::ExeDirectoryVerible,
        &exe_dir.join("verible-verilog-lint"),
        &mut attempts,
    );
    let chosen = attempts
        .iter()
        .find(|attempt| attempt.outcome == AttemptOutcome::Found)
        .and_then(|attempt| attempt.path.clone());
    BinaryResolution { chosen, attempts }
}

/// Resolve `verible-verilog-lint` for the current process.
#[must_use]
pub fn resolve_verible_lint_for_current_process(explicit: Option<&Path>) -> BinaryResolution {
    let env_value = std::env::var_os("VEDIT_VERIBLE_LINT");
    let exe_dir = executable_dir();
    resolve_verible_lint(explicit, env_value.as_deref(), &exe_dir)
}

fn executable_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
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
        assert_eq!(resolution.attempts[1].outcome, AttemptOutcome::EnvUnset);
    }

    #[test]
    fn environment_variable_is_used_when_set() {
        let dir = TempDir::new("env");
        let binary = dir.script("slang-env");
        let os = binary.clone().into_os_string();
        let resolution = resolve_binary(None, Some(&os), &dir.0);
        assert_eq!(resolution.chosen, Some(binary));
        let env_attempt = &resolution.attempts[0];
        assert_eq!(env_attempt.source, AttemptSource::Environment);
        assert_eq!(env_attempt.outcome, AttemptOutcome::Found);
    }

    #[test]
    fn all_attempts_are_listed_when_nothing_is_found() {
        let dir = TempDir::new("missing");
        let resolution = resolve_binary(None, None, &dir.0);
        assert!(resolution.chosen.is_none());
        assert_eq!(resolution.attempts.len(), 5);
        assert_eq!(resolution.attempts[0].outcome, AttemptOutcome::EnvUnset);
        assert!(resolution
            .attempts
            .iter()
            .skip(1)
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
        assert_eq!(
            resolution.attempts[0].outcome,
            AttemptOutcome::NotExecutable
        );
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
}
