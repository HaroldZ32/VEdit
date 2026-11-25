//! Host shell and child environment shared by the PTY and the VCS jobs.

use std::collections::BTreeMap;
use std::ffi::{OsStr, OsString};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{OnceLock, RwLock};

const SHELL_NAMES: &[&str] = &[
    "bash", "csh", "tcsh", "zsh", "fish", "sh", "dash", "ash", "ksh", "ksh93", "mksh",
];

static IMPORTED: RwLock<BTreeMap<String, String>> = RwLock::new(BTreeMap::new());
static PREFERENCE: OnceLock<fn() -> Option<PathBuf>> = OnceLock::new();

/// Publishes the merged global and project variables from the environment importer.
pub fn set_imported_environment(imported: BTreeMap<String, String>) {
    *IMPORTED.write().unwrap_or_else(|error| error.into_inner()) = imported;
}

/// Binds the application's persisted shell preference reader once.
pub fn bind_shell_preference(reader: fn() -> Option<PathBuf>) {
    let _reader = PREFERENCE.get_or_init(|| reader);
}

/// A snapshot of the sanitized environment for one host child and its tool lookup.
pub struct HostEnvironment {
    variables: BTreeMap<OsString, OsString>,
    app_dir: Option<PathBuf>,
    cwd: PathBuf,
    login_shell: Option<PathBuf>,
}

impl HostEnvironment {
    /// Captures the live process and imported layers for a child's working directory.
    pub fn capture(cwd: &Path) -> Self {
        let imported = IMPORTED.read().unwrap_or_else(|error| error.into_inner());
        Self::new(std::env::vars_os().collect(), &imported, cwd)
    }

    /// Merges explicit process and imported layers, with the imported layer winning.
    pub fn new(
        mut variables: BTreeMap<OsString, OsString>,
        imported: &BTreeMap<String, String>,
        cwd: &Path,
    ) -> Self {
        let cwd = std::path::absolute(cwd).unwrap_or_else(|_| cwd.to_path_buf());
        let app_dir = variables
            .get(OsStr::new("APPDIR"))
            .map(PathBuf::from)
            .filter(|path| path.is_absolute() && path.parent().is_some());
        let login_shell = variables.get(OsStr::new("SHELL")).map(PathBuf::from);
        if let Some(path) = variables.remove(OsStr::new("VEDIT_HOST_PATH")) {
            variables.insert(OsString::from("PATH"), path);
        }
        variables.extend(
            imported
                .iter()
                .filter(|(key, value)| {
                    !key.is_empty() && !key.contains(['=', '\0']) && !value.contains('\0')
                })
                .map(|(key, value)| (OsString::from(key), OsString::from(value))),
        );
        for key in ["LD_LIBRARY_PATH", "LOCPATH", "VEDIT_HOST_PATH"] {
            variables.remove(OsStr::new(key));
        }
        let mut host = Self {
            variables,
            app_dir,
            cwd,
            login_shell,
        };
        if let Some(path) = host.get("PATH") {
            let filtered = std::env::split_paths(path)
                .filter(|directory| !host.is_packaged(&host.cwd.join(directory)));
            if let Ok(path) = std::env::join_paths(filtered) {
                host.variables.insert(OsString::from("PATH"), path);
            }
        }
        host
    }

    /// Effective environment variables, including non-UTF-8 process values.
    pub fn variables(&self) -> &BTreeMap<OsString, OsString> {
        &self.variables
    }

    pub(crate) fn get(&self, name: &str) -> Option<&OsStr> {
        self.variables
            .get(OsStr::new(name))
            .map(OsString::as_os_str)
    }

    fn is_packaged(&self, path: &Path) -> bool {
        self.app_dir.as_ref().is_some_and(|root| {
            path.starts_with(root)
                || std::fs::canonicalize(path).is_ok_and(|resolved| {
                    resolved.starts_with(root)
                        || std::fs::canonicalize(root).is_ok_and(|root| resolved.starts_with(root))
                })
        })
    }

    pub(crate) fn executable(&self, path: &Path) -> bool {
        use rustix::fs::{accessat, Access, AtFlags, CWD};

        path.is_absolute()
            && !self.is_packaged(path)
            && std::fs::metadata(path).is_ok_and(|info| info.is_file())
            && accessat(CWD, path, Access::EXEC_OK, AtFlags::EACCESS).is_ok()
    }

    pub(crate) fn on_path<'a>(&'a self, name: &'a str) -> impl Iterator<Item = PathBuf> + 'a {
        self.get("PATH")
            .filter(|path| !path.is_empty())
            .into_iter()
            .flat_map(std::env::split_paths)
            .map(move |directory| self.cwd.join(directory).join(name))
    }

    /// Discovers executable supported shells in the system list and the effective PATH.
    pub fn shells(&self) -> Vec<PathBuf> {
        let registered = std::fs::read_to_string("/etc/shells").unwrap_or_default();
        let mut shells: Vec<_> = self
            .login_shell
            .iter()
            .cloned()
            .chain(registered.lines().map(str::trim).map(PathBuf::from))
            .chain(SHELL_NAMES.iter().flat_map(|name| {
                [
                    Path::new("/bin").join(name),
                    Path::new("/usr/bin").join(name),
                ]
                .into_iter()
                .chain(self.on_path(name))
            }))
            .filter(|path| ShellKind::of(path).is_some() && self.executable(path))
            .collect();
        shells.sort_unstable();
        shells.dedup();
        shells
    }

    /// Resolves an explicit PTY override or the persisted shell choice, with Auto as the default.
    ///
    /// # Errors
    /// Gives a settings recovery action when the chosen shell cannot be started.
    pub fn selected_shell(&self, explicit: Option<&Path>) -> Result<PathBuf, String> {
        let stored = PREFERENCE.get().and_then(|read| read());
        if let Some(path) = explicit.or(stored.as_deref()) {
            return if (explicit.is_some() || ShellKind::of(path).is_some()) && self.executable(path)
            {
                Ok(path.to_path_buf())
            } else {
                Err(format!(
                    "Shell '{}' is unavailable or unsupported. Choose Auto or an installed shell in Preferences → Tools.",
                    path.display()
                ))
            };
        }
        self.login_shell
            .iter()
            .map(PathBuf::as_path)
            .chain([Path::new("/bin/bash"), Path::new("/bin/sh")])
            .find(|path| ShellKind::of(path).is_some() && self.executable(path))
            .map(Path::to_path_buf)
            .or_else(|| self.shells().into_iter().next())
            .ok_or_else(|| "No supported host shell found. Install a shell such as bash and select it in Preferences → Tools.".to_string())
    }

    /// Prepares a host child from an already resolved shell and executable.
    pub fn command(&self, shell: &Path, program: &Path, args: &[String]) -> Command {
        let mut command = Command::new(shell);
        command
            .env_clear()
            .envs(&self.variables)
            .current_dir(&self.cwd);
        let kind = ShellKind::of(shell).unwrap_or(ShellKind::Posix);
        match kind {
            ShellKind::Csh => {
                // csh's $argv:q drops empty arguments. Prefix every argument and
                // decode in the fixed sh program after csh has passed its argv.
                command.args([
                    "-f", "-c",
                    "exec /bin/sh -c 'for arg do shift; set -- \"$@\" \"${arg#_}\"; done; exec \"$@\"' vedit $argv:q",
                ]);
                command.args(
                    std::iter::once(program.as_os_str())
                        .chain(args.iter().map(OsStr::new))
                        .map(|argument| {
                            let mut encoded = OsString::from("_");
                            encoded.push(argument);
                            encoded
                        }),
                );
                return command;
            }
            ShellKind::Fish => {
                command.args(["--no-config", "-c", "exec $argv", "--"]);
            }
            ShellKind::Bash | ShellKind::Ksh => {
                command.args(["-p", "-c", "exec \"$@\"", "vedit"]);
            }
            ShellKind::Zsh => {
                command.args(["-f", "-c", "exec \"$@\"", "vedit"]);
            }
            ShellKind::Posix => {
                command.args(["-c", "exec \"$@\"", "vedit"]);
            }
        }
        command.arg(program).args(args);
        command
    }
}

#[derive(Clone, Copy)]
enum ShellKind {
    Bash,
    Csh,
    Fish,
    Ksh,
    Posix,
    Zsh,
}

impl ShellKind {
    fn of(path: &Path) -> Option<Self> {
        match path.file_name()?.to_str()? {
            "bash" => Some(Self::Bash),
            "csh" | "tcsh" => Some(Self::Csh),
            "fish" => Some(Self::Fish),
            "ksh" | "ksh93" | "mksh" => Some(Self::Ksh),
            "sh" | "dash" | "ash" => Some(Self::Posix),
            "zsh" => Some(Self::Zsh),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_shells_keep_empty_and_multiline_arguments() {
        let host = HostEnvironment::capture(Path::new("."));
        let args = ["[%s]", "", "one\ntwo", "é"].map(str::to_string);
        let expected = "[][one\ntwo][é]".as_bytes();
        for shell in host.shells() {
            let output = host
                .command(&shell, Path::new("/usr/bin/printf"), &args)
                .output()
                .unwrap_or_else(|error| panic!("spawn installed shell: {error:?}"));
            assert!(
                output.status.success(),
                "{}: {}",
                shell.display(),
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(output.stdout, expected, "{}", shell.display());
        }
    }
}
