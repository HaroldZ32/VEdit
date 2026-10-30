//! How the language-server process is started.
//!
//! The client never resolves a binary through `PATH`: the caller passes the
//! path T13 resolved (`loaded.tools.ls.path`). Rules are passed explicitly when
//! the project has a `.rules.verible_lint`; the client deliberately does **not**
//! enable `--rules_config_search`, so the rule file shown by `--print-config`
//! is exactly the one in force.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Duration;

/// Startup and timeout configuration for the Verible language server.
#[derive(Debug, Clone)]
pub struct LspConfig {
    /// Absolute path to `verible-verilog-ls` (resolved by `vd-config`, never `PATH`).
    pub ls_path: PathBuf,
    /// Project root; also the server's working directory.
    pub workspace_root: PathBuf,
    /// Explicit `.rules.verible_lint`; passed as `--rules_config=<path>`.
    pub rules_config: Option<PathBuf>,
    /// Optional Verible filelist (`--file_list_path=<path>`).
    pub file_list_path: Option<PathBuf>,
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
            ls_path: ls_path.into(),
            workspace_root: workspace_root.into(),
            rules_config: None,
            file_list_path: None,
            extra_args: Vec::new(),
            init_timeout: Duration::from_secs(10),
            request_timeout: Duration::from_secs(15),
            shutdown_timeout: Duration::from_secs(5),
        }
    }

    /// Pass an explicit rules file (`--rules_config=…`).
    pub fn with_rules_config(mut self, path: impl Into<PathBuf>) -> Self {
        self.rules_config = Some(path.into());
        self
    }

    /// Pass an explicit Verible filelist (`--file_list_path=…`).
    pub fn with_file_list(mut self, path: impl Into<PathBuf>) -> Self {
        self.file_list_path = Some(path.into());
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
    pub fn command_args(&self) -> Vec<OsString> {
        let mut args: Vec<OsString> = Vec::new();
        if let Some(rules) = &self.rules_config {
            args.push(flag("--rules_config=", rules));
        }
        if let Some(filelist) = &self.file_list_path {
            args.push(flag("--file_list_path=", filelist));
        }
        args.extend(self.extra_args.iter().cloned());
        args
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
    fn default_timeouts_are_bounded() {
        let config = LspConfig::new("ls", "/project");
        assert_eq!(config.init_timeout, Duration::from_secs(10));
        assert_eq!(config.request_timeout, Duration::from_secs(15));
        assert_eq!(config.shutdown_timeout, Duration::from_secs(5));
    }
}
