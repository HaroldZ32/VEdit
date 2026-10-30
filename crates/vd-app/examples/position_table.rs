//! Evidence helper: the app's three coordinate systems on one text line.
//!
//! Prints, for the characters of a CJK + emoji + combining-mark line, the UTF-8
//! byte column (what SLANG reports with `--diag-column-unit byte`), the GTK
//! character offset (what `TextIter` uses), and the UTF-16 offset (what LSP and
//! `vd-editor` use). The only conversion sites are `vd-deeplint` (byte column →
//! editor position) and `vd-app`'s position adapter (Document based); this table
//! is the evidence that they agree.

use vd_editor::{Document, Position};

fn main() {
    let text = "module m;\n  // 你好😀 e\u{0301}\nendmodule\n";
    let document = Document::from_text(text);
    let line_number = 1u32;
    let line_text = document
        .line_range(line_number)
        .map(|range| {
            let all: Vec<char> = text.chars().collect();
            let start = document.char_offset_at_position(range.start).unwrap_or(0);
            let end = document.char_offset_at_position(range.end).unwrap_or(0);
            all[start..end].iter().collect::<String>()
        })
        .unwrap_or_default();
    println!("# text: {text:?}");
    println!("# line {}: {line_text:?}", line_number + 1);
    println!("# char | byte_col(1) | gtk_char_offset | utf16(1) | char");
    let mut byte = 1usize;
    for (char_index, character) in line_text.chars().enumerate() {
        let utf16: usize = line_text
            .chars()
            .take(char_index)
            .map(|c| c.len_utf16())
            .sum();
        println!(
            "  {char_index:2}  | {byte:8} | {char_index:14} | {:7} | {character:?}",
            utf16 + 1
        );
        byte += character.len_utf8();
    }
    let position = Position::new(line_number, 9);
    let iter = document.iter_at_position(position).ok();
    let gtk_offset = iter.as_ref().map(gtk::TextIter::offset);
    let from_iter = iter
        .as_ref()
        .map(|iter| document.position_at_iter(iter))
        .unwrap_or(None);
    println!(
        "# probe position: utf16 {position:?} -> iter.offset {gtk_offset:?} -> position_at_iter {from_iter:?}"
    );
    let converted = vd_deeplint::SourceLines::from_bytes(text.as_bytes())
        .map_err(|error| error.to_string())
        .and_then(|source| {
            source
                .position(line_number + 1, 19)
                .map_err(|error| error.to_string())
        });
    println!(
        "# deeplint byte column (line {}, col 19) -> editor position: {converted:?}",
        line_number + 1
    );
}
