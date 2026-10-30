//! LSP position model and UTF-16 conversions.
//!
//! LSP positions are 0-based `line`/`character`, and by default `character`
//! counts **UTF-16 code units** (so a non-BMP character such as an emoji counts
//! as two). GTK's `GtkTextIter` offsets count Unicode scalar values (code
//! points), so every conversion goes through this module.
//!
//! The helpers here are pure functions over `&str`, which makes the tricky
//! Unicode behaviour testable without a GTK buffer.

/// A 0-based LSP position; `character` is measured in UTF-16 code units.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Position {
    /// 0-based line number.
    pub line: u32,
    /// 0-based UTF-16 code-unit offset inside the line.
    pub character: u32,
}

impl Position {
    /// A position at `line`/`character`.
    #[must_use]
    pub const fn new(line: u32, character: u32) -> Self {
        Self { line, character }
    }
}

/// A half-open LSP range `[start, end)`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Range {
    /// First position of the range.
    pub start: Position,
    /// Position just past the range.
    pub end: Position,
}

impl Range {
    /// A range spanning `start..end`.
    #[must_use]
    pub const fn new(start: Position, end: Position) -> Self {
        Self { start, end }
    }

    /// A zero-width range at `position`.
    #[must_use]
    pub const fn point(position: Position) -> Self {
        Self {
            start: position,
            end: position,
        }
    }

    /// `true` when the range has no width.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.start.line == self.end.line && self.start.character == self.end.character
    }
}

/// The lines of `text` as `split('\n')` sees them (a trailing newline yields a
/// final empty line, matching the buffer's line count).
pub fn lines(text: &str) -> impl Iterator<Item = &str> {
    text.split('\n')
}

/// UTF-16 code units of `text`.
#[must_use]
pub fn utf16_len(text: &str) -> u32 {
    text.chars().map(|ch| ch.len_utf16() as u32).sum()
}

/// Convert a UTF-16 code-unit offset inside one line to a character index.
///
/// Returns `None` when the offset is past the line, or when it points between
/// the two code units of a surrogate pair (a position LSP clients should never
/// send).
#[must_use]
pub fn utf16_to_char_index(line_text: &str, utf16: u32) -> Option<usize> {
    let mut units = 0u32;
    for (char_index, ch) in line_text.chars().enumerate() {
        if units == utf16 {
            return Some(char_index);
        }
        units += ch.len_utf16() as u32;
        if units > utf16 {
            // The offset is inside this character's UTF-16 encoding.
            return None;
        }
    }
    (units == utf16).then(|| line_text.chars().count())
}

/// Convert a character index inside one line to a UTF-16 code-unit offset.
#[must_use]
pub fn char_index_to_utf16(line_text: &str, char_index: usize) -> Option<u32> {
    let mut units = 0u32;
    for (index, ch) in line_text.chars().enumerate() {
        if index == char_index {
            return Some(units);
        }
        units += ch.len_utf16() as u32;
    }
    (char_index == line_text.chars().count()).then_some(units)
}

/// Convert a position to a whole-document character offset (code points, with
/// `'\n'` counting as one character).
#[must_use]
pub fn position_to_char_offset(text: &str, position: Position) -> Option<usize> {
    let mut offset = 0usize;
    for (line_index, line) in lines(text).enumerate() {
        if line_index as u32 == position.line {
            let char_index = utf16_to_char_index(line, position.character)?;
            return Some(offset + char_index);
        }
        offset += line.chars().count() + 1;
    }
    None
}

/// Convert a whole-document character offset to a position.
#[must_use]
pub fn char_offset_to_position(text: &str, offset: usize) -> Option<Position> {
    let mut remaining = offset;
    for (line_index, line) in lines(text).enumerate() {
        let chars = line.chars().count();
        if remaining <= chars {
            let character = char_index_to_utf16(line, remaining)?;
            return Some(Position::new(line_index as u32, character));
        }
        remaining -= chars + 1;
    }
    None
}

/// The position reached after inserting `text` (which must not contain `'\r'`)
/// at `start`.
#[must_use]
pub fn advance_position(start: Position, text: &str) -> Position {
    let newlines = text.matches('\n').count();
    if newlines == 0 {
        return Position::new(start.line, start.character + utf16_len(text));
    }
    let last_line = text.rsplit('\n').next().unwrap_or("");
    let added_lines = u32::try_from(newlines).unwrap_or(u32::MAX);
    Position::new(start.line.saturating_add(added_lines), utf16_len(last_line))
}

/// The UTF-16 length of line `line` in `text`.
#[must_use]
pub fn line_utf16_len(text: &str, line: u32) -> Option<u32> {
    lines(text).nth(line as usize).map(utf16_len)
}

/// The whole-document character offset where `line` starts.
#[must_use]
pub fn line_start_char_offset(text: &str, line: u32) -> Option<usize> {
    let mut offset = 0usize;
    for (index, line_text) in lines(text).enumerate() {
        if index as u32 == line {
            return Some(offset);
        }
        offset += line_text.chars().count() + 1;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ascii_offsets() {
        let text = "abc\ndef\n";
        assert_eq!(position_to_char_offset(text, Position::new(1, 2)), Some(6));
        assert_eq!(char_offset_to_position(text, 6), Some(Position::new(1, 2)));
        assert_eq!(char_offset_to_position(text, 3), Some(Position::new(0, 3)));
        assert_eq!(char_offset_to_position(text, 4), Some(Position::new(1, 0)));
        assert_eq!(char_offset_to_position(text, 8), Some(Position::new(2, 0)));
        assert_eq!(char_offset_to_position(text, 9), None);
    }

    #[test]
    fn chinese_and_emoji_utf16_offsets() {
        // 3 chars: 你 (1 unit), 好 (1 unit), 😀 (2 units).
        let line = "你好😀x";
        assert_eq!(utf16_to_char_index(line, 0), Some(0));
        assert_eq!(utf16_to_char_index(line, 2), Some(2));
        assert_eq!(
            utf16_to_char_index(line, 3),
            None,
            "inside the emoji surrogate pair"
        );
        assert_eq!(utf16_to_char_index(line, 4), Some(3));
        assert_eq!(utf16_to_char_index(line, 5), Some(4));
        assert_eq!(utf16_to_char_index(line, 6), None);

        assert_eq!(char_index_to_utf16(line, 0), Some(0));
        assert_eq!(char_index_to_utf16(line, 2), Some(2));
        assert_eq!(char_index_to_utf16(line, 3), Some(4));
        assert_eq!(char_index_to_utf16(line, 4), Some(5));
        assert_eq!(char_index_to_utf16(line, 5), None);
    }

    #[test]
    fn positions_round_trip_on_mixed_text() {
        let text = "module m;\n  // 你好😀\nendmodule\n";
        for offset in 0..=text.chars().count() {
            let position = char_offset_to_position(text, offset)
                .unwrap_or_else(|| panic!("offset {offset} should map to a position"));
            let back = position_to_char_offset(text, position)
                .unwrap_or_else(|| panic!("position {position:?} should map back"));
            assert_eq!(back, offset, "round trip failed for offset {offset}");
        }
    }

    #[test]
    fn line_utf16_len_counts_units_not_chars() {
        let text = "a\n你好😀\n";
        assert_eq!(line_utf16_len(text, 1), Some(4));
        assert_eq!(line_start_char_offset(text, 1), Some(2));
        assert_eq!(line_start_char_offset(text, 2), Some(6));
        assert_eq!(line_start_char_offset(text, 3), None);
        assert_eq!(
            advance_position(Position::new(1, 0), "😀x"),
            Position::new(1, 3)
        );
        assert_eq!(
            advance_position(Position::new(0, 2), "a\nb"),
            Position::new(1, 1)
        );
    }
}
