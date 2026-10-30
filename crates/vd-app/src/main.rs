//! VEdit — a Verilog/SystemVerilog editor that runs on CentOS 7 (glibc 2.17).
//!
//! The GTK shell plus the wiring between the editor data layer (`vd-editor`),
//! highlighting (`vd-syntax`), configuration (`vd-config`), the Verible LSP
//! client (`vd-lsp`), SLANG deep lint (`vd-deeplint`) and FIM (`vd-fim`). The
//! lanes own their logic; this crate only renders and glues — in particular all
//! text changes go through `vd_editor::Document`, never straight to the buffer.
#![forbid(unsafe_code)]

mod app;
mod cli;
mod commands;
mod config_glue;
mod diagnostics;
mod document_tab;
mod fim_ghost;
mod lsp_worker;
mod position_adapter;
mod selftest;
mod slang_worker;

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

#[cfg(test)]
pub(crate) mod test_support {
    use std::sync::{Mutex, MutexGuard};

    /// GLib/GTK object construction is not thread-safe; unit tests that build a
    /// `GtkSourceBuffer` (through `vd_editor::Document`) serialise on this lock.
    static GTK_TEST_LOCK: Mutex<()> = Mutex::new(());

    pub(crate) fn gtk_lock() -> MutexGuard<'static, ()> {
        match GTK_TEST_LOCK.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }
}
