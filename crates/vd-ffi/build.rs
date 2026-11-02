//! Generate the C header (`include/vd_ffi.h`) from the `extern "C"` surface.
//!
//! cbindgen is a build dependency, not a workflow step: a plain `cargo build -p
//! vd-ffi` regenerates the header, so the header and the Rust declarations cannot
//! drift apart without the build failing (AC-S0.3's falsification depends on
//! exactly that: rename an export and the C side stops compiling).
//!
//! Any failure here is fatal. A stale header that still compiles would hide a
//! broken ABI behind a green build — the failure mode this repository has been
//! burned by before (docs/plans/v2-slices.md §1.2).

use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    println!("cargo:rerun-if-changed=Cargo.toml");

    let Ok(manifest_dir) = std::env::var("CARGO_MANIFEST_DIR") else {
        panic!("CARGO_MANIFEST_DIR is unset; cbindgen cannot locate the crate");
    };
    let crate_dir = PathBuf::from(manifest_dir);

    let config_path = crate_dir.join("cbindgen.toml");
    let Ok(config) = cbindgen::Config::from_file(&config_path) else {
        panic!(
            "cannot read the cbindgen configuration at {}",
            config_path.display()
        );
    };

    let generated = cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate();
    let Ok(bindings) = generated else {
        panic!("cbindgen could not translate the C ABI surface");
    };

    let header = crate_dir.join("include").join("vd_ffi.h");
    if let Some(parent) = header.parent() {
        if let Err(error) = std::fs::create_dir_all(parent) {
            panic!("cannot create {}: {error}", parent.display());
        }
    }
    bindings.write_to_file(&header);
}
