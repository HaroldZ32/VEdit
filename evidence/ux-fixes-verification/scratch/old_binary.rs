//! slang binary resolution.
//!
//! Order (the captain's contract): explicit option (tests / packaging) →
//! `VEDIT_SLANG` environment variable → package-relative paths under the
//! executable directory. **`PATH` is never consulted.** Every attempt is
//! recorded, so a failure tells the user exactly where the binary was expected.

use std::ffi::OsStr;
use std::path::{Path, PathBuf};

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
    /// `exe_dir/slang` (side-by-side layout).
    ExeDirectory,
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
            Self::ExeDirectory => "exe_dir/slang",
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
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."));
    resolve_binary(explicit, env_value.as_deref(), &exe_dir)
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
        assert_eq!(resolution.attempts.len(), 4);
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
}
