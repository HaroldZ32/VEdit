//! The document: a `GtkSourceBuffer` plus the editing/save/search semantics.
//!
//! All mutations go through this type so the line-ending bookkeeping and the
//! undo bookkeeping stay in sync with the buffer. UI code (T21) is expected to
//! call these methods instead of mutating the buffer directly.

use std::cell::RefCell;
use std::path::{Path, PathBuf};

use gtk::prelude::*;
use sourceview4::prelude::*;

use crate::line_ending::{self, LineEnding};
use crate::position::{self, Position, Range};
use crate::save::{self, SaveHooks};
use crate::Error;

/// A text document backed by a `GtkSourceBuffer`.
///
/// The buffer is the single source of truth for the text; `Document` adds the
/// LSP position model, per-line line-ending preservation, atomic save, search
/// and dirty tracking. `GtkSourceBuffer` provides undo/redo (native undo
/// manager), exposed through [`Document::undo`] and [`Document::redo`].
pub struct Document {
    buffer: sourceview4::Buffer,
    state: RefCell<State>,
}

/// One replacement edit in a batch applied by
/// [`Document::apply_edits_one_step`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Edit {
    /// The range to replace.
    pub range: Range,
    /// Replacement text. Its own line endings are preserved for the line breaks
    /// it introduces (a plain `\n` stays `\n`).
    pub text: String,
}

impl Edit {
    /// A replacement of `range` with `text`.
    #[must_use]
    pub fn new(range: Range, text: impl Into<String>) -> Self {
        Self {
            range,
            text: text.into(),
        }
    }
}

struct State {
    path: Option<PathBuf>,
    /// One line ending per `'\n'` in the buffer text.
    breaks: Vec<LineEnding>,
    /// The text at the last load/save; used for dirty tracking.
    saved_text: String,
    /// Snapshots of `breaks` for the buffer's native undo/redo stacks.
    undo_breaks: Vec<Vec<LineEnding>>,
    redo_breaks: Vec<Vec<LineEnding>>,
}

impl Default for Document {
    fn default() -> Self {
        Self::new()
    }
}

impl Document {
    /// A new, empty, untitled document.
    #[must_use]
    pub fn new() -> Self {
        Self::from_buffer(&new_buffer())
    }

    /// Wrap an existing buffer (for example a `GtkSourceView` buffer created by
    /// the UI lane). The current buffer text becomes the saved baseline.
    #[must_use]
    pub fn from_buffer(buffer: &sourceview4::Buffer) -> Self {
        let raw = buffer_text(buffer);
        let (normalized, breaks) = LineEnding::detect(&raw);
        if normalized != raw {
            // The buffer invariant is LF-only text.
            buffer.set_text(&normalized);
        }
        Self {
            buffer: buffer.clone(),
            state: RefCell::new(State {
                path: None,
                breaks,
                saved_text: normalized,
                undo_breaks: Vec::new(),
                redo_breaks: Vec::new(),
            }),
        }
    }

    /// A document with in-memory contents (no path).
    #[must_use]
    pub fn from_text(text: &str) -> Self {
        let buffer = new_buffer();
        buffer.set_text(text);
        Self::from_buffer(&buffer)
    }

    /// Open `path` as UTF-8 text, detecting its line endings.
    ///
    /// # Errors
    /// Returns [`Error::Io`] when the file cannot be read and [`Error::NotUtf8`]
    /// when it is not valid UTF-8.
    pub fn open(path: &Path) -> Result<Self, Error> {
        let bytes = std::fs::read(path)?;
        let contents = String::from_utf8(bytes).map_err(|_| Error::NotUtf8)?;
        let buffer = new_buffer();
        buffer.set_text(&contents);
        let document = Self::from_buffer(&buffer);
        // AC-B2: a freshly opened document starts with the cursor at the top.
        // `from_buffer` normalises CRLF by replacing the text a second time, and
        // `set_text` leaves the insert mark at the end, so the placement has to
        // happen on the finished (normalised) buffer.
        document.buffer.place_cursor(&document.buffer.start_iter());
        document.state.borrow_mut().path = Some(path.to_path_buf());
        Ok(document)
    }

    /// The underlying buffer.
    #[must_use]
    pub fn buffer(&self) -> &sourceview4::Buffer {
        &self.buffer
    }

    /// The text with `'\n'` line endings (the buffer representation).
    #[must_use]
    pub fn text(&self) -> String {
        buffer_text(&self.buffer)
    }

    /// The bound file, if any.
    #[must_use]
    pub fn path(&self) -> Option<PathBuf> {
        self.state.borrow().path.clone()
    }

    /// The most frequent line ending of this document (for status display and
    /// for newly inserted text).
    #[must_use]
    pub fn line_ending(&self) -> LineEnding {
        LineEnding::dominant(&self.state.borrow().breaks, LineEnding::Lf)
    }

    /// The line ending of every line break, in order.
    #[must_use]
    pub fn line_endings(&self) -> Vec<LineEnding> {
        self.state.borrow().breaks.clone()
    }

    /// `true` when the buffer differs from the last loaded/saved text.
    #[must_use]
    pub fn is_modified(&self) -> bool {
        let text = self.text();
        text != self.state.borrow().saved_text
    }

    /// `true` when closing the window must ask for confirmation.
    #[must_use]
    pub fn needs_close_confirmation(&self) -> bool {
        self.is_modified()
    }

    /// Treat the current buffer text as the saved state.
    pub fn mark_saved(&self) {
        let text = self.text();
        self.state.borrow_mut().saved_text = text;
    }

    // ---------------------------------------------------------------- editing --

    /// Insert `text` at `position`. The inserted text's own line endings are
    /// preserved for the line breaks it introduces.
    ///
    /// # Errors
    /// Returns [`Error::InvalidPosition`] / [`Error::SplitSurrogate`] for an
    /// invalid position.
    pub fn insert(&self, position: Position, text: &str) -> Result<(), Error> {
        self.with_user_action(|| {
            let changed = self.insert_impl(position, text)?;
            Ok(((), changed))
        })
    }

    /// Delete `range` and return the removed text.
    ///
    /// # Errors
    /// Returns an error for an invalid range.
    pub fn delete(&self, range: Range) -> Result<String, Error> {
        self.with_user_action(|| self.delete_impl(range))
    }

    /// Replace `range` with `text` and return the removed text.
    ///
    /// # Errors
    /// Returns an error for an invalid range.
    pub fn replace(&self, range: Range, text: &str) -> Result<String, Error> {
        self.with_user_action(|| self.replace_impl(range, text))
    }

    /// Apply several non-overlapping edits as **one undo step**.
    ///
    /// The edits go through the same text path as [`Document::replace`], so the
    /// per-line line-ending bookkeeping stays in sync with the buffer. Edits are
    /// applied from the end of the document backwards, so the offsets of edits
    /// that come earlier stay valid; two edits that overlap are rejected with
    /// [`Error::OverlappingEdits`] *before* anything is changed.
    ///
    /// This is the entry the UI lane must use for multi-edit responses and for
    /// formatting: writing to the buffer directly would desynchronise the
    /// line-ending bookkeeping.
    ///
    /// # Errors
    /// Returns [`Error::InvalidPosition`] / [`Error::SplitSurrogate`] for an
    /// invalid range and [`Error::OverlappingEdits`] for overlapping edits.
    pub fn apply_edits_one_step(&self, edits: &[Edit]) -> Result<(), Error> {
        if edits.is_empty() {
            return Ok(());
        }
        let text = self.text();
        let mut resolved: Vec<(usize, usize, usize, &Edit)> = Vec::with_capacity(edits.len());
        for (index, edit) in edits.iter().enumerate() {
            let start = resolve_offset(&text, edit.range.start)?;
            let end = resolve_offset(&text, edit.range.end)?;
            if start > end {
                return Err(Error::InvalidPosition(edit.range.start));
            }
            resolved.push((start, end, index, edit));
        }
        // Sort by start offset, then by input order. Applying from the back keeps
        // the offsets of the edits that come earlier valid; same-position
        // insertions end up in input order.
        resolved.sort_by_key(|(start, _, index, _)| (*start, *index));
        for pair in resolved.windows(2) {
            if pair[1].0 < pair[0].1 {
                return Err(Error::OverlappingEdits {
                    first: pair[0].3.range,
                    second: pair[1].3.range,
                });
            }
        }
        self.with_user_action(|| {
            let mut changed = false;
            for (_, _, _, edit) in resolved.iter().rev() {
                let (_, deleted) = self.delete_impl(edit.range)?;
                let inserted = self.insert_impl(edit.range.start, &edit.text)?;
                changed |= deleted || inserted;
            }
            Ok(((), changed))
        })
    }

    fn insert_impl(&self, position: Position, text: &str) -> Result<bool, Error> {
        let (normalized, inserted_breaks) = LineEnding::detect(text);
        if normalized.is_empty() {
            return Ok(false);
        }
        let mut iter = self.iter_at_position(position)?;
        self.buffer.insert(&mut iter, &normalized);
        if !inserted_breaks.is_empty() {
            let mut state = self.state.borrow_mut();
            let breaks = line_ending::breaks_after_insert(
                &state.breaks,
                position.line as usize,
                &inserted_breaks,
            );
            state.breaks = breaks;
        }
        Ok(true)
    }

    fn delete_impl(&self, range: Range) -> Result<(String, bool), Error> {
        let (start_offset, end_offset) = self.range_offsets(range)?;
        let (mut start_iter, mut end_iter) = (
            self.iter_at_offset(start_offset, range.start)?,
            self.iter_at_offset(end_offset, range.end)?,
        );
        let removed = self
            .buffer
            .text(&start_iter, &end_iter, true)
            .map_or_else(String::new, |text| text.to_string());
        if start_offset == end_offset {
            return Ok((removed, false));
        }
        self.buffer.delete(&mut start_iter, &mut end_iter);
        let removed_breaks = removed.matches('\n').count();
        if removed_breaks > 0 {
            let mut state = self.state.borrow_mut();
            let breaks = line_ending::breaks_after_delete(
                &state.breaks,
                range.start.line as usize,
                removed_breaks,
            );
            state.breaks = breaks;
        }
        Ok((removed, true))
    }

    fn replace_impl(&self, range: Range, text: &str) -> Result<(String, bool), Error> {
        let (removed, deleted) = self.delete_impl(range)?;
        let inserted = self.insert_impl(range.start, text)?;
        Ok((removed, deleted || inserted))
    }

    /// Run `action` as one undoable user action. The line-ending snapshot is
    /// recorded only when the action actually changed the text, because GTK
    /// records no undo action for a no-op either; that keeps both stacks 1:1.
    fn with_user_action<T>(
        &self,
        action: impl FnOnce() -> Result<(T, bool), Error>,
    ) -> Result<T, Error> {
        let before = self.state.borrow().breaks.clone();
        self.buffer.begin_user_action();
        let result = action();
        self.buffer.end_user_action();
        match result {
            Ok((value, changed)) => {
                if changed {
                    let mut state = self.state.borrow_mut();
                    state.undo_breaks.push(before);
                    state.redo_breaks.clear();
                }
                Ok(value)
            }
            Err(error) => Err(error),
        }
    }

    // ------------------------------------------------------------ undo / redo --

    /// `true` when an undo step is available.
    ///
    /// The buffer's native undo manager also stores the initial load as one
    /// action; this only counts the edits made through [`Document`], so undoing
    /// a freshly opened document cannot empty it.
    #[must_use]
    pub fn can_undo(&self) -> bool {
        !self.state.borrow().undo_breaks.is_empty()
    }

    /// `true` when a redo step is available.
    #[must_use]
    pub fn can_redo(&self) -> bool {
        !self.state.borrow().redo_breaks.is_empty()
    }

    /// Undo one user action. Returns `false` when there is nothing to undo.
    pub fn undo(&self) -> bool {
        if !self.can_undo() {
            return false;
        }
        let current = self.state.borrow().breaks.clone();
        self.buffer.undo();
        let restored = { self.state.borrow_mut().undo_breaks.pop() };
        if let Some(restored) = restored {
            let mut state = self.state.borrow_mut();
            state.breaks = restored;
            state.redo_breaks.push(current);
        }
        true
    }

    /// Redo one user action. Returns `false` when there is nothing to redo.
    pub fn redo(&self) -> bool {
        if !self.can_redo() {
            return false;
        }
        let current = self.state.borrow().breaks.clone();
        self.buffer.redo();
        let restored = { self.state.borrow_mut().redo_breaks.pop() };
        if let Some(restored) = restored {
            let mut state = self.state.borrow_mut();
            state.breaks = restored;
            state.undo_breaks.push(current);
        }
        true
    }

    // -------------------------------------------------------------- positions --

    /// The whole-document character offset of an LSP position.
    ///
    /// # Errors
    /// Returns [`Error::InvalidPosition`] when the line does not exist or the
    /// character offset is past the line, and [`Error::SplitSurrogate`] when the
    /// position points inside a UTF-16 surrogate pair.
    pub fn char_offset_at_position(&self, position: Position) -> Result<usize, Error> {
        let text = self.text();
        resolve_offset(&text, position)
    }

    /// The LSP position of a whole-document character offset.
    #[must_use]
    pub fn position_at_char_offset(&self, offset: usize) -> Option<Position> {
        position::char_offset_to_position(&self.text(), offset)
    }

    /// A `GtkTextIter` for an LSP position (used by the UI to scroll/select).
    ///
    /// # Errors
    /// Same as [`Document::char_offset_at_position`].
    pub fn iter_at_position(&self, position: Position) -> Result<gtk::TextIter, Error> {
        let offset = self.char_offset_at_position(position)?;
        let offset = i32::try_from(offset).map_err(|_| Error::InvalidPosition(position))?;
        Ok(self.buffer.iter_at_offset(offset))
    }

    /// The LSP position of a `GtkTextIter`.
    #[must_use]
    pub fn position_at_iter(&self, iter: &gtk::TextIter) -> Option<Position> {
        let offset = usize::try_from(iter.offset()).ok()?;
        self.position_at_char_offset(offset)
    }

    // ------------------------------------------------------------------ lines --

    /// Number of lines in the buffer (a trailing newline yields a final empty
    /// line, like GTK's line count).
    #[must_use]
    pub fn line_count(&self) -> u32 {
        u32::try_from(self.buffer.line_count()).unwrap_or(u32::MAX)
    }

    /// The text of `line` without its newline.
    #[must_use]
    pub fn line_text(&self, line: u32) -> Option<String> {
        position::lines(&self.text())
            .nth(line as usize)
            .map(str::to_string)
    }

    /// The position at the start of `line` (target of "go to line").
    #[must_use]
    pub fn line_start_position(&self, line: u32) -> Option<Position> {
        self.line_range(line).map(|range| range.start)
    }

    /// The range covering `line` without its newline.
    #[must_use]
    pub fn line_range(&self, line: u32) -> Option<Range> {
        let text = self.text();
        let line_text = position::lines(&text).nth(line as usize)?;
        Some(Range::new(
            Position::new(line, 0),
            Position::new(line, position::utf16_len(line_text)),
        ))
    }

    // ----------------------------------------------------------------- search --

    /// Find the next occurrence of `needle` at or after `from`.
    #[must_use]
    pub fn find_next(&self, needle: &str, from: Position, case_insensitive: bool) -> Option<Range> {
        let start = self.iter_at_position(from).ok()?;
        let (match_start, match_end) =
            start.forward_search(needle, search_flags(case_insensitive), None)?;
        Some(Range::new(
            self.position_at_iter(&match_start)?,
            self.position_at_iter(&match_end)?,
        ))
    }

    /// Find the previous occurrence of `needle` strictly before `from`.
    #[must_use]
    pub fn find_previous(
        &self,
        needle: &str,
        from: Position,
        case_insensitive: bool,
    ) -> Option<Range> {
        let start = self.iter_at_position(from).ok()?;
        let (match_start, match_end) =
            start.backward_search(needle, search_flags(case_insensitive), None)?;
        Some(Range::new(
            self.position_at_iter(&match_start)?,
            self.position_at_iter(&match_end)?,
        ))
    }

    /// Replace the next occurrence at or after `from`; returns the range of the
    /// inserted replacement.
    ///
    /// # Errors
    /// Returns an error when the buffer cannot be edited or the position is
    /// invalid.
    pub fn replace_next(
        &self,
        needle: &str,
        replacement: &str,
        from: Position,
        case_insensitive: bool,
    ) -> Result<Option<Range>, Error> {
        let Some(range) = self.find_next(needle, from, case_insensitive) else {
            return Ok(None);
        };
        let (normalized, _) = LineEnding::detect(replacement);
        let end = position::advance_position(range.start, &normalized);
        self.replace(range, replacement)?;
        Ok(Some(Range::new(range.start, end)))
    }

    /// Replace every occurrence; the whole replacement is one undo step.
    /// Returns the number of replacements.
    ///
    /// # Errors
    /// Returns an error when the buffer cannot be edited.
    pub fn replace_all(
        &self,
        needle: &str,
        replacement: &str,
        case_insensitive: bool,
    ) -> Result<usize, Error> {
        if needle.is_empty() {
            return Ok(0);
        }
        let (normalized, _) = LineEnding::detect(replacement);
        self.with_user_action(|| {
            let mut count = 0usize;
            let mut from = Position::new(0, 0);
            while let Some(range) = self.find_next(needle, from, case_insensitive) {
                let (_removed, _changed) = self.replace_impl(range, replacement)?;
                count += 1;
                from = position::advance_position(range.start, &normalized);
            }
            Ok((count, count > 0))
        })
    }

    // ------------------------------------------------------------------- save --

    /// Save the document to its path.
    ///
    /// # Errors
    /// [`Error::NoPath`] when the document has no path, or [`Error::Io`].
    pub fn save(&self) -> Result<(), Error> {
        let path = self.state.borrow().path.clone().ok_or(Error::NoPath)?;
        self.save_to_path(&path, SaveHooks::none())
    }

    /// Save the document to `path` and bind the document to it.
    ///
    /// # Errors
    /// [`Error::Io`] when the write fails.
    pub fn save_as(&self, path: &Path) -> Result<(), Error> {
        self.save_to_path(path, SaveHooks::none())?;
        self.state.borrow_mut().path = Some(path.to_path_buf());
        Ok(())
    }

    /// Save with fault injection, for the atomic-save tests.
    #[cfg(test)]
    fn save_with_hooks(&self, path: &Path, hooks: SaveHooks<'_>) -> Result<(), Error> {
        self.save_to_path(path, hooks)
    }

    fn save_to_path(&self, path: &Path, hooks: SaveHooks<'_>) -> Result<(), Error> {
        let text = self.text();
        let breaks = self.state.borrow().breaks.clone();
        let default = LineEnding::dominant(&breaks, LineEnding::Lf);
        let contents = LineEnding::assemble(&text, &breaks, default);
        // Integrity check: the reconstructed file must normalize back to the
        // buffer text, otherwise the line-ending bookkeeping is broken.
        let (roundtrip, _) = LineEnding::detect(&contents);
        if roundtrip != text {
            return Err(Error::LineEndingMismatch);
        }
        save::save_atomic_with(path, contents.as_bytes(), hooks)?;
        self.mark_saved();
        Ok(())
    }

    // ---------------------------------------------------------------- private --

    fn range_offsets(&self, range: Range) -> Result<(usize, usize), Error> {
        let text = self.text();
        let start = resolve_offset(&text, range.start)?;
        let end = resolve_offset(&text, range.end)?;
        if start > end {
            return Err(Error::InvalidPosition(range.start));
        }
        Ok((start, end))
    }

    fn iter_at_offset(&self, offset: usize, position: Position) -> Result<gtk::TextIter, Error> {
        let offset = i32::try_from(offset).map_err(|_| Error::InvalidPosition(position))?;
        Ok(self.buffer.iter_at_offset(offset))
    }
}

/// Create the buffer.
///
/// `sourceview4::Buffer::new` asserts that GTK was initialised on the main
/// thread. The buffer itself is display-free (verified: a C probe can create and
/// edit one with no `DISPLAY`, see `evidence/headless-gtk.txt`), so headless
/// unit tests — which run on test threads — construct it through
/// `glib::Object::new` instead. Application code, which calls this after
/// `gtk::init`, takes the normal constructor.
fn new_buffer() -> sourceview4::Buffer {
    if gtk::is_initialized_main_thread() {
        sourceview4::Buffer::new(None::<&gtk::TextTagTable>)
    } else {
        gtk::glib::Object::new::<sourceview4::Buffer>()
    }
}

fn buffer_text(buffer: &sourceview4::Buffer) -> String {
    let start = buffer.start_iter();
    let end = buffer.end_iter();
    buffer
        .text(&start, &end, true)
        .map_or_else(String::new, |text| text.to_string())
}

fn search_flags(case_insensitive: bool) -> gtk::TextSearchFlags {
    let mut flags = gtk::TextSearchFlags::TEXT_ONLY;
    if case_insensitive {
        flags |= gtk::TextSearchFlags::CASE_INSENSITIVE;
    }
    flags
}

/// Resolve an LSP position to a character offset, classifying the failure.
fn resolve_offset(text: &str, position: Position) -> Result<usize, Error> {
    let line_text = position::lines(text)
        .nth(position.line as usize)
        .ok_or(Error::InvalidPosition(position))?;
    let line_units = position::utf16_len(line_text);
    match position::utf16_to_char_index(line_text, position.character) {
        Some(char_index) => {
            let line_start = position::line_start_char_offset(text, position.line)
                .ok_or(Error::InvalidPosition(position))?;
            Ok(line_start + char_index)
        }
        None if position.character > line_units => Err(Error::InvalidPosition(position)),
        None => Err(Error::SplitSurrogate(position)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    static TEST_DIRS: AtomicU64 = AtomicU64::new(0);

    struct TestDir(PathBuf);

    impl TestDir {
        fn new(label: &str) -> Self {
            let serial = TEST_DIRS.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir().join(format!(
                "vd-editor-doc-{}-{serial}-{label}",
                std::process::id()
            ));
            fs::create_dir_all(&path)
                .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
            Self(path)
        }

        fn file(&self, name: &str) -> PathBuf {
            self.0.join(name)
        }
    }

    impl Drop for TestDir {
        fn drop(&mut self) {
            if let Err(error) = fs::remove_dir_all(&self.0) {
                eprintln!("WARN: could not remove {}: {error}", self.0.display());
            }
        }
    }

    // GTK objects are not thread-safe and the test harness runs tests on parallel
    // threads; every test that creates a document holds this lock so GType
    // registration and buffer operations stay serialised.
    static GTK_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    fn gtk_test_lock() -> std::sync::MutexGuard<'static, ()> {
        GTK_TEST_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn sample() -> Document {
        Document::from_text("module top;\n  initial begin\n  end\nendmodule\n")
    }

    #[test]
    fn new_document_is_clean() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("");
        assert!(!document.is_modified());
        assert!(!document.needs_close_confirmation());
        assert_eq!(document.line_count(), 1);
    }

    #[test]
    fn edit_undo_redo_round_trip() {
        let _gtk = gtk_test_lock();
        let document = sample();
        let original = document.text();

        document
            .insert(Position::new(0, 0), "// header\n")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert_eq!(document.text(), format!("// header\n{original}"));
        assert!(document.can_undo());
        assert!(!document.can_redo());

        assert!(document.undo());
        assert_eq!(document.text(), original);
        assert!(document.can_redo());

        assert!(document.redo());
        assert_eq!(document.text(), format!("// header\n{original}"));
        assert!(!document.redo(), "there is nothing left to redo");
    }

    #[test]
    fn replace_is_one_undo_step() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("alpha beta gamma");
        let original = document.text();
        let removed = document
            .replace(
                Range::new(Position::new(0, 0), Position::new(0, 5)),
                "ALPHA",
            )
            .unwrap_or_else(|error| panic!("replace failed: {error}"));
        assert_eq!(removed, "alpha");
        assert_eq!(document.text(), "ALPHA beta gamma");
        assert!(document.undo());
        assert_eq!(document.text(), original);
        assert!(document.redo());
        assert_eq!(document.text(), "ALPHA beta gamma");
    }

    #[test]
    fn delete_spanning_lines_returns_the_removed_text() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("a\nb\nc\nd\n");
        let removed = document
            .delete(Range::new(Position::new(1, 0), Position::new(2, 1)))
            .unwrap_or_else(|error| panic!("delete failed: {error}"));
        assert_eq!(removed, "b\nc");
        assert_eq!(document.text(), "a\n\nd\n");
    }

    #[test]
    fn mixed_line_endings_are_preserved_line_by_line() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("mixed");
        let path = dir.file("mixed.sv");
        fs::write(&path, "a\r\nb\nc\r\nd\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));

        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        assert_eq!(
            document.line_endings(),
            vec![
                LineEnding::Crlf,
                LineEnding::Lf,
                LineEnding::Crlf,
                LineEnding::Lf
            ]
        );
        document
            .insert(Position::new(1, 0), "X")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        document
            .save()
            .unwrap_or_else(|error| panic!("save failed: {error}"));

        assert_eq!(
            fs::read_to_string(&path).unwrap_or_default(),
            "a\r\nXb\nc\r\nd\n",
            "unchanged lines must keep their original line ending"
        );
    }

    #[test]
    fn crlf_file_saves_with_crlf_line_endings() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("crlf");
        let path = dir.file("crlf.sv");
        fs::write(&path, "module a;\r\nendmodule\r\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));

        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        assert_eq!(document.line_ending(), LineEnding::Crlf);
        document
            .insert(Position::new(2, 0), "// end\r\n")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        document
            .save()
            .unwrap_or_else(|error| panic!("save failed: {error}"));

        let saved = fs::read(&path).unwrap_or_else(|error| panic!("read failed: {error}"));
        let text = String::from_utf8(saved).unwrap_or_else(|error| panic!("utf8 failed: {error}"));
        assert_eq!(text, "module a;\r\nendmodule\r\n// end\r\n");
        let mut previous = None;
        for byte in text.bytes() {
            if byte == b'\n' {
                assert_eq!(previous, Some(b'\r'), "saved file contains a lone LF");
            }
            previous = Some(byte);
        }
    }

    #[test]
    fn undo_after_an_edit_keeps_the_original_line_endings() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("undo-endings");
        let path = dir.file("mixed.sv");
        let original = "a\r\nb\nc\r\n";
        fs::write(&path, original).unwrap_or_else(|error| panic!("write failed: {error}"));

        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        document
            .insert(Position::new(1, 1), "X")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert!(document.undo());
        document
            .save()
            .unwrap_or_else(|error| panic!("save failed: {error}"));
        assert_eq!(
            fs::read_to_string(&path).unwrap_or_default(),
            original,
            "undo must restore the original line-ending bookkeeping"
        );
    }

    #[test]
    fn lsp_positions_on_chinese_and_emoji() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("module m;\n  // 你好😀\nendmodule\n");

        // Line 1 is "  // 你好😀": 5 ASCII units + 你 + 好 + 😀 (2 units) = 9 units.
        let after_hao = Position::new(1, 7);
        assert_eq!(
            document
                .char_offset_at_position(after_hao)
                .unwrap_or(usize::MAX),
            17
        );
        let end_of_line = Position::new(1, 9);
        assert_eq!(
            document
                .char_offset_at_position(end_of_line)
                .unwrap_or(usize::MAX),
            18
        );

        // Inside the emoji's surrogate pair.
        match document.char_offset_at_position(Position::new(1, 8)) {
            Err(Error::SplitSurrogate(position)) => assert_eq!(position, Position::new(1, 8)),
            other => panic!("expected SplitSurrogate, got {other:?}"),
        }

        for position in [
            Position::new(0, 0),
            Position::new(0, 9),
            Position::new(1, 0),
            after_hao,
            end_of_line,
            Position::new(2, 0),
            Position::new(2, 9),
            Position::new(3, 0),
        ] {
            let iter = document
                .iter_at_position(position)
                .unwrap_or_else(|error| panic!("iter for {position:?} failed: {error}"));
            assert_eq!(
                document.position_at_iter(&iter),
                Some(position),
                "round trip failed for {position:?}"
            );
        }
    }

    #[test]
    fn line_ranges_support_go_to_line() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("a\nbb\nccc\n");
        assert_eq!(document.line_count(), 4);
        assert_eq!(document.line_text(1), Some("bb".to_string()));
        assert_eq!(
            document.line_range(1),
            Some(Range::new(Position::new(1, 0), Position::new(1, 2)))
        );
        assert_eq!(document.line_start_position(2), Some(Position::new(2, 0)));
        assert_eq!(document.line_range(9), None);
    }

    #[test]
    fn find_and_replace() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("foo bar foo baz");

        assert_eq!(
            document.find_next("foo", Position::new(0, 0), false),
            Some(Range::new(Position::new(0, 0), Position::new(0, 3)))
        );
        assert_eq!(
            document.find_next("foo", Position::new(0, 3), false),
            Some(Range::new(Position::new(0, 8), Position::new(0, 11)))
        );
        assert_eq!(document.find_next("foo", Position::new(0, 11), false), None);
        assert_eq!(
            document.find_previous("foo", Position::new(0, 11), false),
            Some(Range::new(Position::new(0, 8), Position::new(0, 11)))
        );

        let replaced = document
            .replace_next("foo", "qux", Position::new(0, 0), false)
            .unwrap_or_else(|error| panic!("replace_next failed: {error}"));
        assert_eq!(
            replaced,
            Some(Range::new(Position::new(0, 0), Position::new(0, 3)))
        );
        assert_eq!(document.text(), "qux bar foo baz");

        assert_eq!(
            document
                .replace_all("foo", "FOO", false)
                .unwrap_or_else(|error| panic!("replace_all failed: {error}")),
            1
        );
        assert_eq!(document.text(), "qux bar FOO baz");

        // One undo returns the whole replace_all operation.
        assert!(document.undo());
        assert_eq!(document.text(), "qux bar foo baz");
    }

    #[test]
    fn find_is_case_insensitive_when_asked() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("Module top;");
        assert_eq!(
            document.find_next("module", Position::new(0, 0), false),
            None
        );
        assert_eq!(
            document.find_next("module", Position::new(0, 0), true),
            Some(Range::new(Position::new(0, 0), Position::new(0, 6)))
        );
    }

    #[test]
    fn dirty_tracking_and_close_confirmation() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("abc");
        assert!(!document.is_modified());

        document
            .insert(Position::new(0, 3), "d")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert!(document.is_modified());
        assert!(document.needs_close_confirmation());

        document.mark_saved();
        assert!(!document.is_modified());

        document
            .insert(Position::new(0, 0), "x")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert!(document.is_modified());
        assert!(document.undo());
        assert!(
            !document.is_modified(),
            "undo back to the saved text is clean"
        );
    }

    #[test]
    fn save_and_reopen_round_trip() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("round-trip");
        let path = dir.file("doc.sv");
        let document = Document::from_text("module a;\nendmodule\n");
        document
            .save_as(&path)
            .unwrap_or_else(|error| panic!("save_as failed: {error}"));
        assert!(!document.is_modified());
        assert_eq!(document.path(), Some(path.clone()));

        let reopened = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        assert_eq!(reopened.text(), document.text());
        assert!(!reopened.is_modified());
    }

    #[test]
    fn atomic_save_failure_keeps_the_file_and_the_dirty_flag() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("atomic-failure");
        let path = dir.file("doc.sv");
        fs::write(&path, "module a;\nendmodule\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));

        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        document
            .insert(Position::new(0, 0), "// edit\n")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert!(document.is_modified());

        let hook = |_temp: &Path| Err(std::io::Error::other("injected crash before rename"));
        let hooks = SaveHooks {
            during_temp_write: None,
            before_rename: Some(&hook),
        };
        let result = document.save_with_hooks(&path, hooks);
        assert!(result.is_err(), "save must fail");
        assert_eq!(
            fs::read_to_string(&path).unwrap_or_default(),
            "module a;\nendmodule\n",
            "the original file must be untouched"
        );
        assert!(
            document.is_modified(),
            "a failed save must not clear the dirty flag"
        );
    }

    #[test]
    fn save_as_rejects_a_missing_directory() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("missing");
        let path = dir.file("nope/doc.sv");
        let document = Document::from_text("x");
        assert!(document.save_as(&path).is_err());
        assert!(!path.exists());
    }

    #[test]
    fn freshly_loaded_document_cannot_undo_the_load() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("loaded");
        assert!(!document.can_undo(), "loading is not a user edit");
        assert!(!document.can_redo());
        assert!(!document.undo());
        assert_eq!(document.text(), "loaded");
    }

    #[test]
    fn empty_insert_does_not_create_an_undo_step() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("abc");
        document
            .insert(Position::new(0, 1), "")
            .unwrap_or_else(|error| panic!("insert failed: {error}"));
        assert_eq!(document.text(), "abc");
        assert!(
            !document.can_undo(),
            "a no-op edit must not add an undo step"
        );
        assert!(!document.is_modified());
    }

    #[test]
    fn batch_edits_are_one_undo_step() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("alpha beta gamma");
        let original = document.text();
        document
            .apply_edits_one_step(&[
                Edit::new(
                    Range::new(Position::new(0, 0), Position::new(0, 5)),
                    "ALPHA",
                ),
                Edit::new(
                    Range::new(Position::new(0, 11), Position::new(0, 16)),
                    "GAMMA",
                ),
            ])
            .unwrap_or_else(|error| panic!("batch edit failed: {error}"));
        assert_eq!(document.text(), "ALPHA beta GAMMA");
        assert!(document.undo());
        assert_eq!(document.text(), original, "one undo restores the original");
        assert!(document.redo());
        assert_eq!(document.text(), "ALPHA beta GAMMA");
    }

    #[test]
    fn batch_edits_keep_mixed_line_endings_line_by_line() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("batch-endings");
        let path = dir.file("mixed.sv");
        fs::write(&path, "a\r\nb\nc\r\n").unwrap_or_else(|error| panic!("write failed: {error}"));
        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        document
            .apply_edits_one_step(&[
                Edit::new(Range::new(Position::new(0, 0), Position::new(0, 1)), "A"),
                Edit::new(Range::new(Position::new(2, 0), Position::new(2, 1)), "C"),
            ])
            .unwrap_or_else(|error| panic!("batch edit failed: {error}"));
        document
            .save()
            .unwrap_or_else(|error| panic!("save failed: {error}"));
        assert_eq!(
            fs::read_to_string(&path).unwrap_or_default(),
            "A\r\nb\nC\r\n",
            "unchanged line breaks keep CRLF/LF per line"
        );
        assert!(document.undo());
        document
            .save()
            .unwrap_or_else(|error| panic!("save after undo failed: {error}"));
        assert_eq!(
            fs::read_to_string(&path).unwrap_or_default(),
            "a\r\nb\nc\r\n",
            "undo restores the original line endings too"
        );
    }

    #[test]
    fn overlapping_batch_edits_are_rejected_without_changes() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("0123456789");
        let error = match document.apply_edits_one_step(&[
            Edit::new(Range::new(Position::new(0, 0), Position::new(0, 5)), "A"),
            Edit::new(Range::new(Position::new(0, 3), Position::new(0, 8)), "B"),
        ]) {
            Ok(()) => panic!("overlapping edits must be rejected"),
            Err(error) => error,
        };
        match error {
            Error::OverlappingEdits { .. } => {}
            other => panic!("expected OverlappingEdits, got {other:?}"),
        }
        assert_eq!(document.text(), "0123456789");
        assert!(
            !document.can_undo(),
            "a rejected batch must not add an undo step"
        );
    }

    #[test]
    fn unsorted_batch_edits_apply_in_offset_order() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("one two three");
        let original = document.text();
        document
            .apply_edits_one_step(&[
                Edit::new(
                    Range::new(Position::new(0, 8), Position::new(0, 13)),
                    "THREE",
                ),
                Edit::new(Range::new(Position::new(0, 0), Position::new(0, 3)), "ONE"),
            ])
            .unwrap_or_else(|error| panic!("batch edit failed: {error}"));
        assert_eq!(document.text(), "ONE two THREE");
        assert!(document.undo());
        assert_eq!(document.text(), original);
    }

    #[test]
    fn same_position_insertions_keep_input_order() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("ab");
        document
            .apply_edits_one_step(&[
                Edit::new(Range::point(Position::new(0, 1)), "X"),
                Edit::new(Range::point(Position::new(0, 1)), "Y"),
            ])
            .unwrap_or_else(|error| panic!("batch insert failed: {error}"));
        assert_eq!(document.text(), "aXYb");
    }

    #[test]
    fn empty_batch_is_a_noop_without_undo_step() {
        let _gtk = gtk_test_lock();
        let document = Document::from_text("abc");
        document
            .apply_edits_one_step(&[])
            .unwrap_or_else(|error| panic!("empty batch failed: {error}"));
        assert_eq!(document.text(), "abc");
        assert!(!document.can_undo());
    }

    /// AC-B2 helper: the insert mark must sit at the document start.
    fn assert_insert_mark_at_start(document: &Document, label: &str) {
        let buffer = document.buffer();
        let mark = buffer
            .get_insert()
            .unwrap_or_else(|| panic!("{label}: buffer has no insert mark"));
        let iter = buffer.iter_at_mark(&mark);
        let start = buffer.start_iter();
        assert!(
            iter == start,
            "{label}: insert mark is at offset {} instead of the document start",
            iter.offset()
        );
    }

    #[test]
    fn opening_an_lf_file_puts_the_cursor_at_the_start() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("cursor-lf");
        let path = dir.file("lf.sv");
        fs::write(&path, "module lf;\n  logic a;\nendmodule\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));
        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        assert_insert_mark_at_start(&document, "lf");
    }

    #[test]
    fn opening_a_crlf_file_puts_the_cursor_at_the_start() {
        let _gtk = gtk_test_lock();
        let dir = TestDir::new("cursor-crlf");
        let path = dir.file("crlf.sv");
        fs::write(&path, "module crlf;\r\n  logic a;\r\nendmodule\r\n")
            .unwrap_or_else(|error| panic!("write failed: {error}"));
        let document = Document::open(&path).unwrap_or_else(|error| panic!("open failed: {error}"));
        assert_insert_mark_at_start(&document, "crlf");
    }

    #[test]
    fn readonly_probe_does_not_matter_because_rename_is_the_guard() {
        let _gtk = gtk_test_lock();
        // Guard against a regression where save() writes in place: a failing
        // rename must never leave a truncated target behind.
        let dir = TestDir::new("rename-guard");
        let path = dir.file("doc.sv");
        fs::write(&path, "original").unwrap_or_else(|error| panic!("write failed: {error}"));
        let hook = |_temp: &Path| Err(std::io::Error::from(std::io::ErrorKind::PermissionDenied));
        let document = Document::from_text("new contents");
        let hooks = SaveHooks {
            during_temp_write: None,
            before_rename: Some(&hook),
        };
        assert!(document.save_with_hooks(&path, hooks).is_err());
        assert_eq!(fs::read_to_string(&path).unwrap_or_default(), "original");
    }
}
