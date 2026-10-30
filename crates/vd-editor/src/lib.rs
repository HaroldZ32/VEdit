//! Editor baseline data layer (T9).
//!
//! The text is stored in a `GtkSourceBuffer` (branch A: GTK owns the storage),
//! and undo/redo is the buffer's **native** `GtkSourceUndoManager` — the only
//! hand-written pieces are the glue the task allows:
//!
//! * LSP position/range model (UTF-16 code units, 0-based) and conversions;
//! * per-line CRLF/LF/CR preservation across load and save (mixed files keep
//!   their line endings line by line);
//! * atomic save (temp file in the same directory, fsync, rename) with a fault
//!   injection seam for tests;
//! * find/replace matching on top of GTK's own text search;
//! * jump-to-line data, dirty tracking and the "does the window need a close
//!   confirmation" question.
//!
//! No rope: the buffer is the single source of truth, so the UI lane (T21) does
//! not have to keep a second model in sync. This crate has no UI code.
#![forbid(unsafe_code)]

mod document;
mod line_ending;
mod position;
mod save;

pub use document::Document;
pub use line_ending::LineEnding;
pub use position::{
    advance_position, char_index_to_utf16, char_offset_to_position, line_start_char_offset,
    line_utf16_len, position_to_char_offset, utf16_len, Position, Range,
};
pub use save::SaveHooks;

use std::fmt;

/// Errors returned by the editor data layer.
#[derive(Debug)]
pub enum Error {
    /// Reading or writing the file backing the document failed.
    Io(std::io::Error),
    /// The file is not valid UTF-8 text.
    NotUtf8,
    /// The document has no path (it was never opened or saved as).
    NoPath,
    /// The LSP position does not exist in the document.
    InvalidPosition(Position),
    /// The LSP position points inside a UTF-16 surrogate pair.
    SplitSurrogate(Position),
    /// The bookkeeping of line endings and the buffer text disagree.
    LineEndingMismatch,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(err) => write!(f, "I/O error: {err}"),
            Self::NotUtf8 => f.write_str("file is not valid UTF-8"),
            Self::NoPath => f.write_str("document has no path; use save_as"),
            Self::InvalidPosition(pos) => write!(
                f,
                "position {}:{} is outside the document",
                pos.line, pos.character
            ),
            Self::SplitSurrogate(pos) => write!(
                f,
                "position {}:{} points inside a UTF-16 surrogate pair",
                pos.line, pos.character
            ),
            Self::LineEndingMismatch => {
                f.write_str("line-ending bookkeeping does not match the buffer text")
            }
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(err) => Some(err),
            _ => None,
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        Self::Io(err)
    }
}
