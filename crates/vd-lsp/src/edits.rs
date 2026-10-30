//! Apply LSP edits to a text buffer.
//!
//! The LSP layer never mutates the editor buffer itself: it returns
//! `TextEdit`s and the caller applies them through [`apply_text_edits`], which
//! is also what makes AC-07 (byte-identical formatter output) testable.

use anyhow::{bail, Result};
use lsp_types::{TextDocumentContentChangeEvent, TextEdit};

use crate::position::position_to_byte;

/// Apply a set of non-overlapping [`TextEdit`]s to `text`.
///
/// Edits are sorted by range, so the order the server returned them in does not
/// matter; overlapping edits are rejected instead of silently resolved.
pub fn apply_text_edits(text: &str, edits: &[TextEdit]) -> Result<String> {
    let mut resolved: Vec<(usize, usize, &str)> = Vec::with_capacity(edits.len());
    for edit in edits {
        let start = position_to_byte(text, edit.range.start)?;
        let end = position_to_byte(text, edit.range.end)?;
        if start > end {
            bail!("edit range is reversed: {:?}", edit.range);
        }
        resolved.push((start, end, edit.new_text.as_str()));
    }
    resolved.sort_by_key(|(start, end, _)| (*start, *end));
    for pair in resolved.windows(2) {
        if pair[0].1 > pair[1].0 {
            bail!("overlapping text edits are not supported");
        }
    }
    let mut output = String::with_capacity(text.len());
    let mut cursor = 0usize;
    for (start, end, replacement) in resolved {
        output.push_str(&text[cursor..start]);
        output.push_str(replacement);
        cursor = end;
    }
    output.push_str(&text[cursor..]);
    Ok(output)
}

/// Apply `textDocument/didChange` content changes to the tracked text.
///
/// A change without a range replaces the whole document (full sync); changes
/// with a range are applied in order (incremental sync).
pub fn apply_content_changes(
    text: &str,
    changes: &[TextDocumentContentChangeEvent],
) -> Result<String> {
    let mut current = text.to_string();
    for change in changes {
        match &change.range {
            Some(range) => {
                let start = position_to_byte(&current, range.start)?;
                let end = position_to_byte(&current, range.end)?;
                if start > end {
                    bail!("change range is reversed: {range:?}");
                }
                current.replace_range(start..end, &change.text);
            }
            None => current = change.text.clone(),
        }
    }
    Ok(current)
}

#[cfg(test)]
mod tests {
    use super::{apply_content_changes, apply_text_edits};
    use lsp_types::{Position, Range, TextDocumentContentChangeEvent, TextEdit};

    fn edit(start: (u32, u32), end: (u32, u32), text: &str) -> TextEdit {
        TextEdit {
            range: Range {
                start: Position {
                    line: start.0,
                    character: start.1,
                },
                end: Position {
                    line: end.0,
                    character: end.1,
                },
            },
            new_text: text.to_string(),
        }
    }

    #[test]
    fn edits_are_applied_in_document_order_regardless_of_input_order(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let text = "one two three";
        let edits = vec![
            edit((0, 8), (0, 13), "THREE"),
            edit((0, 0), (0, 3), "ONE"),
            edit((0, 4), (0, 7), "TWO"),
        ];
        assert_eq!(apply_text_edits(text, &edits)?, "ONE TWO THREE");
        Ok(())
    }

    #[test]
    fn overlapping_edits_are_rejected() {
        let text = "abcdef";
        let edits = vec![edit((0, 0), (0, 3), "x"), edit((0, 2), (0, 4), "y")];
        assert!(apply_text_edits(text, &edits).is_err());
    }

    #[test]
    fn utf16_columns_apply_to_multibyte_text() -> Result<(), Box<dyn std::error::Error>> {
        let text = "// 中文\nwire a;";
        // Replace "wire" on line 1.
        let edits = vec![edit((1, 0), (1, 4), "logic")];
        assert_eq!(apply_text_edits(text, &edits)?, "// 中文\nlogic a;");
        Ok(())
    }

    #[test]
    fn incremental_changes_track_a_document() -> Result<(), Box<dyn std::error::Error>> {
        let text = "module m;\nendmodule\n";
        let changes = vec![TextDocumentContentChangeEvent {
            range: Some(Range {
                start: Position {
                    line: 0,
                    character: 9,
                },
                end: Position {
                    line: 0,
                    character: 9,
                },
            }),
            range_length: None,
            text: "\n  wire q;".to_string(),
        }];
        let updated = apply_content_changes(text, &changes)?;
        assert_eq!(updated, "module m;\n  wire q;\nendmodule\n");
        let full = vec![TextDocumentContentChangeEvent {
            range: None,
            range_length: None,
            text: "// replaced\n".to_string(),
        }];
        assert_eq!(apply_content_changes(&updated, &full)?, "// replaced\n");
        Ok(())
    }
}
