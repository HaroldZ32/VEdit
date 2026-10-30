//! One editor tab: a `vd_editor::Document` plus its GtkSourceView.
//!
//! The `Document` is the only writer of the buffer (T21 rule). The view is
//! created around `document.buffer()`, so the language/style APIs and the
//! document's line-ending bookkeeping see the same buffer.

use std::cell::RefCell;
use std::path::{Path, PathBuf};

use anyhow::{Context as _, Result};
use gtk::prelude::*;
use sourceview4::prelude::*;

use vd_editor::{Document, Position, Range};

use crate::config_glue::EditorSettings;
use crate::diagnostics::DiagnosticsStore;
use crate::position_adapter;

/// A tab: document + view + per-tab diagnostics.
pub struct Tab {
    pub document: Document,
    pub buffer: sourceview4::Buffer,
    pub view: sourceview4::View,
    pub widget: gtk::ScrolledWindow,
    pub path: RefCell<Option<PathBuf>>,
    pub language: RefCell<Option<String>>,
    pub store: RefCell<DiagnosticsStore>,
    /// Gutter marks created for the current diagnostics (owned so they can be
    /// removed precisely; removing by category is not reliable in GtkSourceView).
    pub marks: RefCell<Vec<sourceview4::Mark>>,
}

impl Tab {
    /// Open a file from disk.
    ///
    /// # Errors
    /// Returns the editor error when the file cannot be read.
    pub fn open(path: &Path, settings: &EditorSettings) -> Result<Self> {
        let document =
            Document::open(path).with_context(|| format!("opening {}", path.display()))?;
        let buffer = document.buffer().clone();
        let view = sourceview4::View::with_buffer(&buffer);
        let language = vd_syntax::apply_language(&buffer, path);
        let _scheme = vd_syntax::apply_style_scheme(&buffer);
        let tab = Self::from_parts(document, buffer, view, Some(path.to_path_buf()));
        *tab.language.borrow_mut() = language;
        tab.configure(settings);
        Ok(tab)
    }

    /// A new, unsaved tab.
    #[must_use]
    pub fn untitled(settings: &EditorSettings) -> Self {
        let document = Document::new();
        let buffer = document.buffer().clone();
        let view = sourceview4::View::with_buffer(&buffer);
        let tab = Self::from_parts(document, buffer, view, None);
        tab.configure(settings);
        tab
    }

    fn from_parts(
        document: Document,
        buffer: sourceview4::Buffer,
        view: sourceview4::View,
        path: Option<PathBuf>,
    ) -> Self {
        let scrolled = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
        scrolled.set_child(Some(&view));
        scrolled.set_hexpand(true);
        scrolled.set_vexpand(true);
        Self {
            document,
            buffer,
            view,
            widget: scrolled,
            path: RefCell::new(path),
            language: RefCell::new(None),
            store: RefCell::new(DiagnosticsStore::new()),
            marks: RefCell::new(Vec::new()),
        }
    }

    fn configure(&self, settings: &EditorSettings) {
        self.view.set_show_line_numbers(settings.show_line_numbers);
        self.view
            .set_highlight_current_line(settings.highlight_current_line);
        self.view.set_show_line_marks(true);
        self.view.set_auto_indent(true);
        self.view.set_tab_width(settings.tab_width);
        self.view
            .set_insert_spaces_instead_of_tabs(settings.insert_spaces);
        self.buffer.set_highlight_matching_brackets(true);
    }

    /// The title shown in the notebook tab.
    #[must_use]
    pub fn title(&self) -> String {
        match self.path.borrow().as_ref() {
            Some(path) => path.file_name().map_or_else(
                || path.display().to_string(),
                |name| name.to_string_lossy().into_owned(),
            ),
            None => "Untitled".to_string(),
        }
    }

    /// The cursor position in the UTF-16 model.
    #[must_use]
    pub fn cursor(&self) -> Position {
        position_adapter::cursor_position(&self.document, &self.buffer)
    }

    /// Put the cursor at a position and scroll it into view.
    pub fn set_cursor(&self, position: Position) -> bool {
        let Some(mut iter) = position_adapter::iter_for(&self.document, position) else {
            return false;
        };
        self.buffer.place_cursor(&iter);
        self.view.scroll_to_iter(&mut iter, 0.1, false, 0.0, 0.5);
        true
    }

    /// Select a range.
    pub fn select_range(&self, range: Range) -> bool {
        let (Some(start), Some(end)) = (
            position_adapter::iter_for(&self.document, range.start),
            position_adapter::iter_for(&self.document, range.end),
        ) else {
            return false;
        };
        self.buffer.select_range(&start, &end);
        self.set_cursor(range.start);
        true
    }

    #[must_use]
    pub fn text(&self) -> String {
        self.document.text()
    }

    /// The range covering the whole document.
    #[must_use]
    pub fn full_range(&self) -> Range {
        let last_line = self.document.line_count().saturating_sub(1);
        let end = self
            .document
            .line_range(last_line)
            .map_or(Position::new(0, 0), |range| range.end);
        Range::new(Position::new(0, 0), end)
    }

    /// The LSP document URI, when the tab has a path.
    #[must_use]
    pub fn uri(&self) -> Option<lsp_types::Url> {
        let path = self.path.borrow().clone()?;
        lsp_types::Url::from_file_path(path).ok()
    }

    /// The LSP language id (`systemverilog` / `verilog`).
    #[must_use]
    pub fn language_id(&self) -> String {
        if let Some(language) = self.language.borrow().as_deref() {
            return language.to_string();
        }
        let path = self.path.borrow().clone();
        path.and_then(|path| vd_syntax::language_id_for_path(&path))
            .map_or_else(|| "systemverilog".to_string(), |id| id.id().to_string())
    }

    /// Save to the existing path.
    ///
    /// # Errors
    /// Returns the editor error (including "no path") when saving fails.
    pub fn save(&self) -> Result<()> {
        self.document.save().context("saving the document")
    }

    /// Save under a new path and remember it.
    ///
    /// # Errors
    /// Returns the editor error when saving fails.
    pub fn save_as(&self, path: &Path) -> Result<()> {
        self.document
            .save_as(path)
            .with_context(|| format!("saving as {}", path.display()))?;
        *self.path.borrow_mut() = Some(path.to_path_buf());
        Ok(())
    }
}
