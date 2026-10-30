//! Command-line parsing for the shell.
//!
//! Only the shell's own entry points live here; GTK never sees our arguments,
//! because the interactive path calls `Application::register`/`activate`
//! directly instead of `run()`. That keeps `--selftest`/`--file` out of GTK's
//! option parser.

use std::ffi::OsString;
use std::path::PathBuf;

use anyhow::{anyhow, bail, Result};

/// Product name shown in the window title and `--version`.
pub const APP_NAME: &str = "VEdit";
/// GApplication id; also the AppImage desktop id used by the packaging lanes.
pub const APP_ID: &str = "io.github.vedit.VEdit";

/// Selftest suites that exist (AC-02 uses `smoke`; `all` runs every suite).
pub const SELFTEST_SUITES: &[&str] = &["smoke", "highlight", "config", "lsp", "slang", "fim"];

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Mode {
    Interactive,
    Selftest(String),
    Version,
    Help,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Options {
    pub mode: Mode,
    /// File to open on startup (positional argument or `--file`).
    pub file: Option<PathBuf>,
    /// Diagnostics: keep the window alive this long after a selftest ends, so a
    /// test harness can query the X server about it (`xwininfo`).
    pub probe_hold_ms: u64,
    /// Diagnostics: write a PNG of the realized window before exiting.
    pub screenshot: Option<PathBuf>,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            mode: Mode::Interactive,
            file: None,
            probe_hold_ms: 0,
            screenshot: None,
        }
    }
}

impl Options {
    /// Parse the process arguments (without argv[0]).
    ///
    /// # Errors
    /// Returns an error for a non-UTF-8 argument, an unknown option, or a
    /// missing/invalid option value.
    pub fn parse<I: IntoIterator<Item = OsString>>(args: I) -> Result<Self> {
        let mut opts = Self::default();
        let mut args = args.into_iter();
        while let Some(arg) = args.next() {
            let text = arg
                .to_str()
                .ok_or_else(|| anyhow!("argument is not valid UTF-8: {arg:?}"))?;
            match text {
                "--selftest" => {
                    let raw = args
                        .next()
                        .ok_or_else(|| anyhow!("--selftest needs a suite name ({})", suites()))?;
                    let suite = raw
                        .to_str()
                        .ok_or_else(|| anyhow!("--selftest suite name is not valid UTF-8"))?;
                    if suite != "all" && suite != "list" && !SELFTEST_SUITES.contains(&suite) {
                        bail!("unknown selftest suite '{suite}' ({})", suites());
                    }
                    opts.mode = Mode::Selftest(suite.to_string());
                }
                "--file" => {
                    let path = args.next().ok_or_else(|| anyhow!("--file needs a path"))?;
                    opts.file = Some(PathBuf::from(path));
                }
                "--probe-hold-ms" => {
                    let raw = args
                        .next()
                        .ok_or_else(|| anyhow!("--probe-hold-ms needs a number of milliseconds"))?;
                    let text = raw
                        .to_str()
                        .ok_or_else(|| anyhow!("--probe-hold-ms value is not valid UTF-8"))?;
                    opts.probe_hold_ms = text
                        .parse::<u64>()
                        .map_err(|_| anyhow!("--probe-hold-ms is not a number: {text}"))?;
                }
                "--screenshot" => {
                    let path = args
                        .next()
                        .ok_or_else(|| anyhow!("--screenshot needs a PNG path"))?;
                    opts.screenshot = Some(PathBuf::from(path));
                }
                "--version" | "-V" => opts.mode = Mode::Version,
                "--help" | "-h" => opts.mode = Mode::Help,
                other if other.starts_with('-') => bail!("unknown option '{other}'\n\n{}", usage()),
                other => opts.file = Some(PathBuf::from(other)),
            }
        }
        Ok(opts)
    }
}

fn suites() -> String {
    let mut names = SELFTEST_SUITES.to_vec();
    names.push("all");
    names.push("list");
    names.join(", ")
}

/// Human-readable usage text.
pub fn usage() -> String {
    format!(
        "{name} <file.sv|file.v> — Verilog/SystemVerilog editor\n\
         \n\
         Usage:\n\
         \x20 vd-app [FILE]                 open the editor (the file is optional)\n\
         \x20 vd-app --selftest <suite>      run a self test and exit (suite: {suites})\n\
         \x20 vd-app --version              print the version and exit\n\
         \n\
         Options:\n\
         \x20 --file <PATH>                 file to open at startup\n\
         \x20 --screenshot <PNG>            diagnostics: save a screenshot before exit\n\
         \x20 --probe-hold-ms <MS>          diagnostics only: hold the window open after a selftest\n\
         \x20 -h, --help                    show this help\n",
        name = APP_NAME,
        suites = suites()
    )
}
