//! LSP wire positions are UTF-16 code units; editing needs byte offsets.
//!
//! The conversion is explicit here (no external crate) so the UTF-16 rule is
//! visible, unit-tested, and shared by every consumer of this crate.

use anyhow::{bail, Result};
use lsp_types::Position;

/// Byte offset of the first character of `line` (0-based).
pub fn line_start_byte(text: &str, line: u32) -> Result<usize> {
    if line == 0 {
        return Ok(0);
    }
    let mut current = 0u32;
    for (offset, byte) in text.bytes().enumerate() {
        if byte == b'\n' {
            current += 1;
            if current == line {
                return Ok(offset + 1);
            }
        }
    }
    bail!("line {line} is past the end of the document")
}

fn line_byte_end(text: &str, line_start: usize) -> usize {
    match text[line_start..].find('\n') {
        Some(index) => line_start + index,
        None => text.len(),
    }
}

/// Convert an LSP position (UTF-16 columns) into a byte offset in `text`.
///
/// A column that falls between the two halves of a surrogate pair is rejected
/// rather than silently rounded.
pub fn position_to_byte(text: &str, position: Position) -> Result<usize> {
    let start = line_start_byte(text, position.line)?;
    let mut end = line_byte_end(text, start);
    if end > start && text.as_bytes().get(end - 1) == Some(&b'\r') {
        end -= 1;
    }
    let content = &text[start..end];
    let mut units = 0u32;
    for (offset, character) in content.char_indices() {
        if units == position.character {
            return Ok(start + offset);
        }
        let width = character.len_utf16() as u32;
        if units + width > position.character {
            bail!("position {position:?} splits a UTF-16 surrogate pair");
        }
        units += width;
    }
    if units == position.character {
        Ok(end)
    } else {
        bail!(
            "position {position:?} is past the end of line {}",
            position.line
        )
    }
}

/// Convert a byte offset into an LSP position (UTF-16 columns).
pub fn byte_to_position(text: &str, byte: usize) -> Result<Position> {
    if byte > text.len() {
        bail!("byte offset {byte} is past the end of the document");
    }
    if !text.is_char_boundary(byte) {
        bail!("byte offset {byte} is not a character boundary");
    }
    let mut line = 0u32;
    let mut column = 0u32;
    for character in text[..byte].chars() {
        if character == '\n' {
            line += 1;
            column = 0;
        } else {
            column += character.len_utf16() as u32;
        }
    }
    Ok(Position {
        line,
        character: column,
    })
}

#[cfg(test)]
mod tests {
    use super::{byte_to_position, position_to_byte};
    use lsp_types::Position;

    #[test]
    fn ascii_round_trip() -> Result<(), Box<dyn std::error::Error>> {
        let text = "module top;\n  logic q;\nendmodule\n";
        let position = Position {
            line: 1,
            character: 2,
        };
        let byte = position_to_byte(text, position)?;
        assert_eq!(&text[byte..], "logic q;\nendmodule\n");
        assert_eq!(byte_to_position(text, byte)?, position);
        Ok(())
    }

    #[test]
    fn cjk_and_emoji_use_utf16_units() -> Result<(), Box<dyn std::error::Error>> {
        // One CJK char = 1 UTF-16 unit, one emoji = 2 UTF-16 units.
        let text = "中文😀x";
        let after_cjk = Position {
            line: 0,
            character: 2,
        };
        assert_eq!(&text[position_to_byte(text, after_cjk)?..], "😀x");
        let after_emoji = Position {
            line: 0,
            character: 4,
        };
        assert_eq!(&text[position_to_byte(text, after_emoji)?..], "x");
        assert!(
            position_to_byte(
                text,
                Position {
                    line: 0,
                    character: 3
                }
            )
            .is_err(),
            "a position inside an emoji must be rejected"
        );
        Ok(())
    }

    #[test]
    fn crlf_lines_are_handled() -> Result<(), Box<dyn std::error::Error>> {
        let text = "a\r\nb\r\n";
        let line_end = Position {
            line: 0,
            character: 1,
        };
        assert_eq!(position_to_byte(text, line_end)?, 1);
        let second = Position {
            line: 1,
            character: 0,
        };
        assert_eq!(position_to_byte(text, second)?, 3);
        Ok(())
    }

    #[test]
    fn out_of_range_positions_are_errors() {
        let text = "abc\n";
        assert!(position_to_byte(
            text,
            Position {
                line: 5,
                character: 0
            }
        )
        .is_err());
        assert!(position_to_byte(
            text,
            Position {
                line: 0,
                character: 9
            }
        )
        .is_err());
        assert!(byte_to_position(text, 2).is_ok());
        assert!(byte_to_position(text, 99).is_err());
    }
}
