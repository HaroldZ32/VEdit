//! VEdit — a Verilog/SystemVerilog editor that runs on CentOS 7 (glibc 2.17).
//!
//! This crate is the GTK3 shell: the main window, the GtkSourceView editor
//! widget, open/save wiring, the status bar, the command dispatcher that later
//! lanes register their shortcuts through, and the `--selftest` entry point.
//!
//! Out of scope here (owned by their lanes): highlighting semantics (T11),
//! the editor baseline (T9), config discovery (T13), the Verible LSP client
//! (T15), SLANG deep lint (T17), FIM (T19) and the wiring/panels (T21).
#![forbid(unsafe_code)]

mod app;
mod cli;
mod commands;
mod document;
mod selftest;

use anyhow::Result;

fn main() -> Result<()> {
    let opts = cli::Options::parse(std::env::args_os().skip(1))?;
    match &opts.mode {
        cli::Mode::Interactive => app::run_interactive(&opts),
        cli::Mode::Selftest(suite) => selftest::run(suite, &opts),
        cli::Mode::Version => {
            println!("{} {}", cli::APP_NAME, env!("CARGO_PKG_VERSION"));
            Ok(())
        }
        cli::Mode::Help => {
            println!("{}", cli::usage());
            Ok(())
        }
    }
}
