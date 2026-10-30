//! Test-only helpers: `unwrap`/`expect` are denied by the workspace lints, so
//! tests use `?` plus these shims instead.

use std::error::Error;
use std::path::Path;

/// Turn an `Option` into a `Result` for `?` in tests.
pub(crate) fn require<T>(value: Option<T>) -> Result<T, Box<dyn Error>> {
    value.ok_or_else(|| "expected Some(..) in a test".into())
}

/// Create parent directories and write a fixture file.
pub(crate) fn write(path: &Path, text: &str) -> Result<(), Box<dyn Error>> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(path, text)?;
    Ok(())
}
