//! How the language-server process is started.
//!
//! The client never resolves a binary through `PATH`: the caller passes the
//! path T13 resolved (`loaded.tools.ls.path`). Rules are passed explicitly when
//! the project has a `.rules.verible_lint`; the client deliberately does **not**
//! enable `--rules_config_search`, so the rule file shown by `--print-config`
//! is exactly the one in force.
//!
//! # Two backends, two argv shapes (T138)
//!
//! * **verible** has no include/define flag (measured with `--helpfull`): the
//!   project's `-I` / `-D` reach it through a Verible file list passed with
//!   `--file_list_path` ([`crate::filelist_text`] builds that file). Rules are
//!   passed as `--rules_config=<path>`.
//! * **slang-server** takes slang's own driver flags, so `-I<dir>` and
//!   `-D<name>=<value>` are appended verbatim. Verible's lint rules do not
//!   apply to it, so `rules_config` is never passed to this backend.
//!
//! [`LspConfig::command_line`] renders the exact program plus argv, which is the
//! dump AC-2.5 asks for (it spawns nothing).

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Duration;

use crate::backend::LspBackend;

/// Startup and timeout configuration for the language server.
#[derive(Debug, Clone)]
pub struct LspConfig {
    /// Which language server this configuration starts.
    pub backend: LspBackend,
    /// Absolute path to the server binary (resolved by `vd-config`, never `PATH`).
    pub ls_path: PathBuf,
    /// Project root; also the server's working directory.
    pub workspace_root: PathBuf,
    /// Explicit `.rules.verible_lint`; passed as `--rules_config=<path>` (verible only).
    pub rules_config: Option<PathBuf>,
    /// Optional Verible filelist (`--file_list_path=<path>`).
    pub file_list_path: Option<PathBuf>,
    /// Project include directories (`-I` for slang, `+incdir+` in the file list for verible).
    pub include_dirs: Vec<PathBuf>,
    /// Project defines (`-D` for slang, `+define+` in the file list for verible).
    pub defines: Vec<String>,
    /// Extra arguments appended verbatim (tests, future flags).
    pub extra_args: Vec<OsString>,
    /// Budget for the `initialize` round trip.
    pub init_timeout: Duration,
    /// Budget for one ordinary request.
    pub request_timeout: Duration,
    /// Budget for the graceful `shutdown`/`exit` sequence.
    pub shutdown_timeout: Duration,
}

impl LspConfig {
    /// Default timeouts: 10 s initialise, 15 s request, 5 s shutdown.
    pub fn new(ls_path: impl Into<PathBuf>, workspace_root: impl Into<PathBuf>) -> Self {
        Self {
            backend: LspBackend::Verible,
            ls_path: ls_path.into(),
            workspace_root: workspace_root.into(),
            rules_config: None,
            file_list_path: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            extra_args: Vec::new(),
            init_timeout: Duration::from_secs(10),
            request_timeout: Duration::from_secs(15),
            shutdown_timeout: Duration::from_secs(5),
        }
    }

    /// Select the backend (verible is the default, so old callers are unchanged).
    pub fn with_backend(mut self, backend: LspBackend) -> Self {
        self.backend = backend;
        self
    }

    /// Pass an explicit rules file (`--rules_config=…`; verible only).
    pub fn with_rules_config(mut self, path: impl Into<PathBuf>) -> Self {
        self.rules_config = Some(path.into());
        self
    }

    /// Pass an explicit Verible filelist (`--file_list_path=…`).
    pub fn with_file_list(mut self, path: impl Into<PathBuf>) -> Self {
        self.file_list_path = Some(path.into());
        self
    }

    /// Add one project include directory.
    pub fn with_include_dir(mut self, directory: impl Into<PathBuf>) -> Self {
        self.include_dirs.push(directory.into());
        self
    }

    /// Add one project define (`NAME` or `NAME=VALUE`).
    pub fn with_define(mut self, define: impl Into<String>) -> Self {
        self.defines.push(define.into());
        self
    }

    /// Append an extra argument.
    pub fn with_extra_arg(mut self, argument: impl Into<OsString>) -> Self {
        self.extra_args.push(argument.into());
        self
    }

    /// Override the three timeouts at once.
    pub fn with_timeouts(mut self, init: Duration, request: Duration, shutdown: Duration) -> Self {
        self.init_timeout = init;
        self.request_timeout = request;
        self.shutdown_timeout = shutdown;
        self
    }

    /// The argv (without argv[0]) used to spawn the server.
    ///
    /// The shape depends on the backend (see the module docs); verible is the
    /// default, so a configuration that never calls [`Self::with_backend`]
    /// produces exactly the argv it always did.
    pub fn command_args(&self) -> Vec<OsString> {
        let mut args: Vec<OsString> = Vec::new();
        match self.backend {
            LspBackend::Verible => {
                if let Some(rules) = &self.rules_config {
                    args.push(flag("--rules_config=", rules));
                }
                if let Some(filelist) = &self.file_list_path {
                    args.push(flag("--file_list_path=", filelist));
                }
            }
            LspBackend::Slang => {
                for directory in &self.include_dirs {
                    args.push(flag("-I", directory));
                }
                for define in &self.defines {
                    let define = define.trim();
                    if !define.is_empty() {
                        args.push(flag("-D", Path::new(define)));
                    }
                }
            }
        }
        args.extend(self.extra_args.iter().cloned());
        args
    }

    /// The exact program plus argv, as one line. Spawns nothing: this is the
    /// settings-page echo and the AC-2.5 dump.
    #[must_use]
    pub fn command_line(&self) -> String {
        let mut line = format!(
            "backend={} program={}",
            self.backend.as_str(),
            self.ls_path.display()
        );
        for argument in self.command_args() {
            line.push(' ');
            line.push_str(&argument.to_string_lossy());
        }
        line
    }

    /// The working directory used for the child process.
    pub fn child_working_dir(&self) -> &Path {
        &self.workspace_root
    }
}

fn flag(prefix: &str, path: &Path) -> OsString {
    let mut value = OsString::from(prefix);
    value.push(path.as_os_str());
    value
}

#[cfg(test)]
mod tests {
    use super::LspConfig;
    use std::ffi::OsString;
    use std::path::PathBuf;
    use std::time::Duration;

    #[test]
    fn rules_are_passed_explicitly_and_never_searched() {
        let config = LspConfig::new("/payload/verible-verilog-ls", "/project")
            .with_rules_config("/project/.rules.verible_lint");
        let args: Vec<String> = config
            .command_args()
            .iter()
            .map(|arg| arg.to_string_lossy().to_string())
            .collect();
        assert_eq!(
            args,
            vec!["--rules_config=/project/.rules.verible_lint".to_string()],
            "no --rules_config_search is ever added"
        );
    }

    #[test]
    fn no_rules_file_means_no_rules_flag() {
        let config = LspConfig::new("/payload/verible-verilog-ls", "/project");
        assert!(config.command_args().is_empty());
    }

    #[test]
    fn filelist_and_extra_args_are_appended_in_order() {
        let config = LspConfig::new("/ls", "/project")
            .with_file_list("/project/top.f")
            .with_extra_arg("--push_diagnostic_notifications");
        assert_eq!(
            config.command_args(),
            vec![
                OsString::from("--file_list_path=/project/top.f"),
                OsString::from("--push_diagnostic_notifications")
            ]
        );
        assert_eq!(config.child_working_dir(), PathBuf::from("/project"));
    }

    #[test]
    fn the_verible_file_list_route_lands_in_the_argv() {
        // -I/-D cannot be flags for verible, so they travel inside the file list
        // that --file_list_path hands over. Both halves are asserted here.
        let text = crate::filelist_text(
            &[PathBuf::from("/p/top.sv")],
            &[PathBuf::from("/p/inc")],
            &["WIDTH=8".to_string()],
        );
        assert!(text.contains("+incdir+/p/inc"), "{text}");
        assert!(text.contains("+define+WIDTH=8"), "{text}");
        let config = LspConfig::new("/bin/verible-verilog-ls", "/p")
            .with_include_dir("/p/inc")
            .with_define("WIDTH=8")
            .with_file_list("/p/generated.f");
        let args: Vec<String> = config
            .command_args()
            .iter()
            .map(|arg| arg.to_string_lossy().to_string())
            .collect();
        assert_eq!(args, vec!["--file_list_path=/p/generated.f"]);
        assert!(config
            .command_line()
            .contains("--file_list_path=/p/generated.f"));
        // For the slang backend the same settings are real flags, not a file.
        let slang = LspConfig::new("/bin/slang-server", "/p")
            .with_backend(crate::LspBackend::Slang)
            .with_include_dir("/p/inc")
            .with_define("WIDTH=8");
        let args: Vec<String> = slang
            .command_args()
            .iter()
            .map(|arg| arg.to_string_lossy().to_string())
            .collect();
        assert_eq!(args, vec!["-I/p/inc", "-DWIDTH=8"]);
    }

    #[test]
    fn default_timeouts_are_bounded() {
        let config = LspConfig::new("ls", "/project");
        assert_eq!(config.init_timeout, Duration::from_secs(10));
        assert_eq!(config.request_timeout, Duration::from_secs(15));
        assert_eq!(config.shutdown_timeout, Duration::from_secs(5));
    }
}
