//! The ONE position-conversion entry point of the app.
//!
//! Three coordinate systems meet here:
//! * LSP positions: 0-based line, UTF-16 code-unit character;
//! * `vd-editor` positions: the same UTF-16 semantics (T9);
//! * GTK `TextIter`: an offset measured in characters (code points).
//!
//! Everything else in the app goes through `vd_editor::Document`, whose
//! `position_at_iter` / `iter_at_position` already perform the UTF-16 ↔ GTK
//! character conversion. This module only adapts the LSP type and clamps
//! positions to the document; no other place in the app converts positions.
//! Pure-ASCII tests would hide a mistake here, so the tests use CJK and emoji.

use gtk::prelude::*;
use vd_editor::{Document, Position, Range};

/// Adapter from an LSP position (same UTF-16 units).
#[must_use]
pub fn position_from_lsp(position: lsp_types::Position) -> Position {
    Position::new(position.line, position.character)
}

/// Adapter to an LSP position.
#[must_use]
pub fn position_to_lsp(position: Position) -> lsp_types::Position {
    lsp_types::Position::new(position.line, position.character)
}

/// Clamp an arbitrary position (for example one from a server that still thinks
/// an edit happened) into the document.
#[must_use]
pub fn clamp_to_document(document: &Document, position: Position) -> Position {
    let line_count = document.line_count();
    if line_count == 0 {
        return Position::new(0, 0);
    }
    let line = position.line.min(line_count.saturating_sub(1));
    let character = document
        .line_range(line)
        .map_or(0, |range| position.character.min(range.end.character));
    Position::new(line, character)
}

/// A GTK iterator for a position (clamped).
#[must_use]
pub fn iter_for(document: &Document, position: Position) -> Option<gtk::TextIter> {
    let position = clamp_to_document(document, position);
    document.iter_at_position(position).ok()
}

/// The position of a GTK iterator.
#[must_use]
pub fn position_for_iter(document: &Document, iter: &gtk::TextIter) -> Position {
    document
        .position_at_iter(iter)
        .unwrap_or(Position::new(0, 0))
}

/// The cursor position of a view/buffer pair.
#[must_use]
pub fn cursor_position(document: &Document, buffer: &sourceview4::Buffer) -> Position {
    let Some(mark) = buffer.get_insert() else {
        return Position::new(0, 0);
    };
    let iter = buffer.iter_at_mark(&mark);
    position_for_iter(document, &iter)
}

/// Clamp an LSP range into the document.
#[must_use]
pub fn range_from_lsp(document: &Document, range: lsp_types::Range) -> Range {
    Range::new(
        clamp_to_document(document, position_from_lsp(range.start)),
        clamp_to_document(document, position_from_lsp(range.end)),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn document() -> Document {
        Document::from_text("module m;\n  // 你好😀\nendmodule\n")
    }

    #[test]
    fn lsp_positions_are_utf16_and_round_trip() {
        let document = document();
        // Line 1 is "  // 你好😀": UTF-16 positions 0..9.
        for character in [0u32, 5, 6, 7, 9] {
            let position = Position::new(1, character);
            let iter = iter_for(&document, position)
                .unwrap_or_else(|| panic!("iter for {position:?} missing"));
            assert_eq!(
                position_for_iter(&document, &iter),
                position,
                "round trip failed for {position:?}"
            );
            let lsp = position_to_lsp(position);
            assert_eq!(position_from_lsp(lsp), position);
        }
    }

    #[test]
    fn clamping_handles_out_of_range_positions() {
        let document = document();
        assert_eq!(
            clamp_to_document(&document, Position::new(999, 999)),
            Position::new(3, 0)
        );
        assert_eq!(
            clamp_to_document(&document, Position::new(1, 999)),
            Position::new(1, 9)
        );
    }

    #[test]
    fn lsp_ranges_are_clamped_into_document_ranges() {
        let document = document();
        let range = range_from_lsp(
            &document,
            lsp_types::Range::new(
                lsp_types::Position::new(1, 5),
                lsp_types::Position::new(1, 7),
            ),
        );
        assert_eq!(range.start, Position::new(1, 5));
        assert_eq!(range.end, Position::new(1, 7));
    }
}
