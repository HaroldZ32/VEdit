//! Line-ending detection, per-line preservation and bookkeeping.
//!
//! A `GtkTextBuffer` stores text with `'\n'` line breaks only, so a CRLF file
//! has to be normalized on load and reconstructed on save. Unifying the whole
//! file would silently rewrite users' line endings, so the exact ending of
//! **every** line break is remembered in a vector with one entry per `'\n'`, and
//! that vector is maintained by the edits the document performs. Mixed files
//! therefore keep their line endings line by line.

/// The line-ending style of one line break.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LineEnding {
    /// Unix `\n`.
    Lf,
    /// Windows `\r\n`.
    Crlf,
    /// Legacy Mac `\r`.
    Cr,
}

impl LineEnding {
    /// The characters this ending contributes to the file.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Lf => "\n",
            Self::Crlf => "\r\n",
            Self::Cr => "\r",
        }
    }

    /// Split `content` into its `'\n'`-normalized text and the ending of every
    /// line break it contained.
    #[must_use]
    pub fn detect(content: &str) -> (String, Vec<Self>) {
        let mut normalized = String::with_capacity(content.len());
        let mut breaks = Vec::new();
        let mut chars = content.chars().peekable();
        while let Some(ch) = chars.next() {
            match ch {
                '\r' => {
                    if chars.peek() == Some(&'\n') {
                        chars.next();
                        breaks.push(Self::Crlf);
                    } else {
                        breaks.push(Self::Cr);
                    }
                    normalized.push('\n');
                }
                '\n' => {
                    breaks.push(Self::Lf);
                    normalized.push('\n');
                }
                other => normalized.push(other),
            }
        }
        (normalized, breaks)
    }

    /// Reconstruct the file contents from `'\n'`-normalized text and the ending
    /// of each line break. When `breaks` does not match the text (which should
    /// never happen for a `Document`), every break falls back to `default`.
    #[must_use]
    pub fn assemble(text: &str, breaks: &[Self], default: Self) -> String {
        let mut parts = text.split('\n');
        let mut out = String::with_capacity(text.len());
        if let Some(first) = parts.next() {
            out.push_str(first);
        }
        for (index, part) in parts.enumerate() {
            let ending = breaks.get(index).copied().unwrap_or(default);
            out.push_str(ending.as_str());
            out.push_str(part);
        }
        out
    }

    /// The most frequent ending in `breaks`, or `fallback` when `breaks` is
    /// empty.
    #[must_use]
    pub fn dominant(breaks: &[Self], fallback: Self) -> Self {
        let lf = breaks.iter().filter(|ending| **ending == Self::Lf).count();
        let crlf = breaks
            .iter()
            .filter(|ending| **ending == Self::Crlf)
            .count();
        let cr = breaks.iter().filter(|ending| **ending == Self::Cr).count();
        let mut best = fallback;
        let mut best_count = 0usize;
        for (ending, count) in [(Self::Lf, lf), (Self::Crlf, crlf), (Self::Cr, cr)] {
            if count > best_count {
                best = ending;
                best_count = count;
            }
        }
        best
    }
}

/// Bookkeeping for inserting text whose own line breaks are `inserted` at
/// `line` (0-based).
///
/// The ending that used to terminate `line` now terminates the last line of the
/// inserted block, so the inserted endings go in front of it.
#[must_use]
pub fn breaks_after_insert(
    breaks: &[LineEnding],
    line: usize,
    inserted: &[LineEnding],
) -> Vec<LineEnding> {
    let at = line.min(breaks.len());
    let mut updated = Vec::with_capacity(breaks.len() + inserted.len());
    updated.extend_from_slice(&breaks[..at]);
    updated.extend_from_slice(inserted);
    updated.extend_from_slice(&breaks[at..]);
    updated
}

/// Bookkeeping for deleting the text between `start_line` and the line the
/// deletion ends on: the merged line keeps the ending of the line that followed
/// the deleted region.
#[must_use]
pub fn breaks_after_delete(
    breaks: &[LineEnding],
    start_line: usize,
    removed_newlines: usize,
) -> Vec<LineEnding> {
    let mut updated = Vec::with_capacity(breaks.len());
    updated.extend_from_slice(&breaks[..start_line.min(breaks.len())]);
    let survivor = start_line + removed_newlines;
    if survivor < breaks.len() {
        updated.push(breaks[survivor]);
        updated.extend_from_slice(&breaks[survivor + 1..]);
    }
    updated
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_lf_crlf_and_cr() {
        assert_eq!(
            LineEnding::detect("a\nb\n"),
            ("a\nb\n".to_string(), vec![LineEnding::Lf, LineEnding::Lf])
        );
        assert_eq!(
            LineEnding::detect("a\r\nb\r\n"),
            (
                "a\nb\n".to_string(),
                vec![LineEnding::Crlf, LineEnding::Crlf]
            )
        );
        assert_eq!(
            LineEnding::detect("a\rb\r"),
            ("a\nb\n".to_string(), vec![LineEnding::Cr, LineEnding::Cr])
        );
    }

    #[test]
    fn mixed_line_endings_keep_their_order() {
        let (text, breaks) = LineEnding::detect("a\r\nb\nc\r\n");
        assert_eq!(text, "a\nb\nc\n");
        assert_eq!(
            breaks,
            vec![LineEnding::Crlf, LineEnding::Lf, LineEnding::Crlf]
        );
        assert_eq!(
            LineEnding::assemble(&text, &breaks, LineEnding::Lf),
            "a\r\nb\nc\r\n"
        );
    }

    #[test]
    fn assemble_falls_back_to_the_default_when_breaks_run_out() {
        assert_eq!(
            LineEnding::assemble("a\nb\nc", &[LineEnding::Crlf], LineEnding::Lf),
            "a\r\nb\nc"
        );
    }

    #[test]
    fn insert_bookkeeping_moves_the_original_ending_to_the_last_new_line() {
        // "a\nb\n" -> insert "x\r\ny" before "b": the inserted text's own
        // break is CRLF, the original b ending survives at the end.
        let breaks = [LineEnding::Lf, LineEnding::Lf];
        let (inserted_text, inserted_breaks) = LineEnding::detect("x\r\ny");
        assert_eq!(inserted_text, "x\ny");
        let after = breaks_after_insert(&breaks, 1, &inserted_breaks);
        assert_eq!(
            after,
            vec![LineEnding::Lf, LineEnding::Crlf, LineEnding::Lf]
        );
        let (_, expected) = LineEnding::detect("a\nx\r\nyb\n");
        assert_eq!(after, expected);
    }

    #[test]
    fn insert_at_end_without_trailing_break() {
        let breaks = [LineEnding::Lf];
        let after = breaks_after_insert(&breaks, 1, &[LineEnding::Lf]);
        assert_eq!(after, vec![LineEnding::Lf, LineEnding::Lf]);
    }

    #[test]
    fn delete_bookkeeping_keeps_the_surviving_ending() {
        // "a\r\nb\nc\r\nd": breaks [Crlf, Lf, Crlf]; delete "b\nc" -> "a\r\n\nd"? no:
        // delete from (1,0) to (2,1) removes "b\nc" leaving "a\r\n\nd" with breaks
        // [Crlf, Crlf].
        let breaks = [LineEnding::Crlf, LineEnding::Lf, LineEnding::Crlf];
        let after = breaks_after_delete(&breaks, 1, 1);
        assert_eq!(after, vec![LineEnding::Crlf, LineEnding::Crlf]);
    }

    #[test]
    fn delete_through_the_end_drops_the_last_ending() {
        let breaks = [LineEnding::Lf, LineEnding::Lf];
        let after = breaks_after_delete(&breaks, 1, 1);
        assert_eq!(after, vec![LineEnding::Lf]);
    }
}
