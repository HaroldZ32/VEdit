//! Print the module catalog of one flat project directory (ux-batch-2 A1).
//!
//! ```text
//! cargo run -p vd-deeplint --example catalog_dump -- <project-dir> [slang-binary]
//! ```
//!
//! This is a readability aid, not a product path: the product's file set comes
//! from the project model, while this example reads the `*.sv` / `*.v` files of
//! one directory (the fixture layout). Positions are printed 1-based for humans;
//! the ABI and the crate use 0-based lines/columns.

use std::path::{Path, PathBuf};

use vd_deeplint::{build_catalog, CatalogOptions};

fn main() {
    let mut arguments = std::env::args().skip(1);
    let Some(root) = arguments.next() else {
        eprintln!("usage: catalog_dump <project-dir> [slang-binary]");
        std::process::exit(2);
    };
    // The child runs with the project directory as its cwd, so a relative
    // binary path would be resolved there: anchor it here.
    let slang = arguments
        .next()
        .map(PathBuf::from)
        .map(|path| std::fs::canonicalize(&path).unwrap_or(path));
    let root = match Path::new(&root).canonicalize() {
        Ok(canonical) => canonical,
        Err(error) => {
            eprintln!("cannot resolve {root}: {error}");
            std::process::exit(2);
        }
    };
    let Ok(entries) = std::fs::read_dir(&root) else {
        eprintln!("cannot read {}", root.display());
        std::process::exit(2);
    };
    let mut files: Vec<PathBuf> = entries
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| {
            path.extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| matches!(extension, "sv" | "v" | "svh" | "vh"))
        })
        .collect();
    files.sort();
    if files.is_empty() {
        eprintln!("no Verilog sources under {}", root.display());
        std::process::exit(2);
    }
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: slang,
            ..CatalogOptions::default()
        },
    );
    if let Some(failure) = &catalog.failure {
        eprintln!("catalog failed: {}", failure.user_message());
        std::process::exit(1);
    }
    println!(
        "{} module(s) in {} ms",
        catalog.modules.len(),
        catalog.elapsed.as_millis()
    );
    for module in &catalog.modules {
        println!(
            "module {} (defined at {}:{}:{}){}",
            module.name,
            module.file.display(),
            module.line + 1,
            module.column + 1,
            if module.is_top { " [top]" } else { "" }
        );
        for port in &module.ports {
            println!(
                "  port {} {} {} @{}:{}",
                port.name,
                port.direction.as_str(),
                port.type_name,
                port.line + 1,
                port.column + 1
            );
        }
        for child in &module.children {
            println!(
                "  child {} path={} @{}:{}:{}",
                child.module,
                child.path,
                child.file.display(),
                child.line + 1,
                child.column + 1
            );
        }
    }
}
