//! Evidence helper: print the slang binary resolution and every attempt.
//!
//! Usage: `cargo run -p vd-deeplint --example binary_attempts`
//! Environment: `VEDIT_SLANG` is honoured (and recorded); `PATH` never is.

fn main() {
    let resolution = vd_deeplint::resolve_binary_for_current_process(None);
    println!("VEDIT_SLANG={:?}", std::env::var_os("VEDIT_SLANG"));
    println!(
        "exe_dir={:?}",
        std::env::current_exe()
            .ok()
            .and_then(|exe| exe.parent().map(std::path::Path::to_path_buf))
    );
    println!("attempts (in order):");
    print!("{}", resolution.describe());
    match &resolution.chosen {
        Some(path) => println!("chosen={}", path.display()),
        None => println!("chosen=<none>"),
    }
}
