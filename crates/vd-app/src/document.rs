//! Thin file <-> `GtkTextBuffer` wiring for the shell.
//!
//! This is deliberately naive. T9 owns the real editor baseline in
//! `vd-editor` — undo/redo, UTF-8 position model, CRLF policy and atomic
//! save — and T21 rewires the UI to it. The shell only needs a working
//! open/save path so the window can be exercised by `--selftest` and by the
//! package acceptance lanes.

use std::fs;
use std::path::Path;

use anyhow::{Context, Result};
use gtk::prelude::*;

/// Load `path` into `buffer`, replacing its contents.
///
/// # Errors
/// Returns an error when the file cannot be read as UTF-8 text.
pub fn load_into_buffer(buffer: &sourceview4::Buffer, path: &Path) -> Result<()> {
    let text = fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
    buffer.set_text(&text);
    Ok(())
}

/// The buffer contents as a `String`.
#[must_use]
pub fn buffer_text(buffer: &sourceview4::Buffer) -> String {
    let start = buffer.start_iter();
    let end = buffer.end_iter();
    buffer
        .text(&start, &end, true)
        .map_or_else(String::new, |text| text.to_string())
}

/// Write the buffer contents to `path`.
///
/// This is not atomic and applies no line-ending policy; T9 replaces it with
/// the real save path (temporary file + rename, CRLF handling).
///
/// # Errors
/// Returns an error when the file cannot be written.
pub fn save_from_buffer(buffer: &sourceview4::Buffer, path: &Path) -> Result<()> {
    let text = buffer_text(buffer);
    fs::write(path, text.as_str()).with_context(|| format!("writing {}", path.display()))?;
    Ok(())
}
