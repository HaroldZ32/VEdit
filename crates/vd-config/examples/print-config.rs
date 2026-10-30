//! Print the resolved configuration of a directory.
//!
//! Mirrors the `--print-config` debug exit the editor will expose (T21):
//!
//! ```text
//! cargo run -p vd-config --example print-config -- [--json|--debug] [DIR]
//! ```

use std::path::PathBuf;

use vd_config::{render_table, to_json, Env, Loader};

enum Mode {
    Table,
    Json,
    Debug,
}

fn main() -> anyhow::Result<()> {
    let mut mode = Mode::Table;
    let mut start: Option<PathBuf> = None;
    for argument in std::env::args().skip(1) {
        match argument.as_str() {
            "--json" => mode = Mode::Json,
            "--debug" => mode = Mode::Debug,
            "--help" | "-h" => {
                println!("usage: print-config [--json|--debug] [DIR]");
                return Ok(());
            }
            other => start = Some(PathBuf::from(other)),
        }
    }
    let start = match start {
        Some(path) => path,
        None => std::env::current_dir()?,
    };
    let loaded = Loader::new(start).with_env(Env::from_process()).load()?;
    match mode {
        Mode::Table => print!("{}", render_table(&loaded)),
        Mode::Json => {
            let document = to_json(&loaded);
            println!("{}", serde_json::to_string_pretty(&document)?);
        }
        Mode::Debug => println!("{loaded:?}"),
    }
    Ok(())
}
