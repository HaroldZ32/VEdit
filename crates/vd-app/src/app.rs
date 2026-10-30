//! The GTK shell: tabs, the problem panel, gutter marks, shortcuts, the LSP /
//! SLANG / FIM wiring and the status bar.
//!
//! Every text change goes through `vd_editor::Document`; formatting is applied
//! with `Document::apply_edits_one_step`, and ghost text is never written to the
//! buffer until the user accepts it.

use std::cell::{Cell, RefCell};
use std::path::Path;
use std::rc::Rc;
use std::sync::mpsc::Receiver;
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context as _, Result};
use gtk::gdk::prelude::*;
use gtk::prelude::*;
use sourceview4::prelude::*;

use vd_editor::{advance_position, Edit, Position};

use crate::cli::{Options, APP_ID, APP_NAME};
use crate::commands::SHORTCUTS;
use crate::config_glue::{self, EditorSettings, Startup};
use crate::diagnostics::{self, Severity, ViewDiagnostic};
use crate::document_tab::Tab;
use crate::fim_ghost::Ghost;
use crate::lsp_worker::{LspBridge, LspEvent};
use crate::position_adapter;
use crate::slang_worker;

/// Message shown when a feature needs a saved file.
const NEEDS_FILE: &str = "save the file first";

/// What `capture_screenshot` wrote (the pre-write pixel validation is the
/// authoritative "not blank" check; the file size/signature is checked too).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScreenshotSummary {
    pub width: i32,
    pub height: i32,
    pub bytes: u64,
    pub sampled_colours: usize,
    pub channel_range: u8,
}

/// Test seam for the modal close confirmation (`None` restores the dialog).
pub type CloseConfirmation = Box<dyn Fn(&str) -> bool>;

/// The status bar's model: the last message plus the live LSP state.
///
/// Rendering is pure. A background poll only REFRESHES the LSP status; it never
/// appends to the message, so polling cannot grow the composed text (issue #2 /
/// AC-4) and the LSP state is rendered at most once.
#[derive(Debug, Clone, Default)]
pub struct StatusModel {
    info: String,
    lsp: Option<String>,
}

impl StatusModel {
    pub fn set_info(&mut self, info: impl Into<String>) {
        self.info = info.into();
    }

    pub fn set_lsp(&mut self, status: String) {
        self.lsp = Some(status);
    }

    /// Compose the bar: the fixed columns, the last message, then the live LSP
    /// status unless the message already names it.
    #[must_use]
    pub fn text(&self, parts: &[String]) -> String {
        let mut all: Vec<&str> = parts.iter().map(String::as_str).collect();
        if !self.info.is_empty() {
            all.push(&self.info);
        }
        if let Some(status) = self.lsp.as_deref() {
            if !all.iter().any(|part| part.contains("Verible LSP")) {
                all.push(status);
            }
        }
        all.join("  |  ")
    }
}

/// Shared state (widgets live in [`Ui`]).
pub struct State {
    pub startup: Startup,
    pub settings: EditorSettings,
    pub tabs: RefCell<Vec<Rc<Tab>>>,
    pub lsp: RefCell<Option<LspBridge>>,
    pub slang_rx: RefCell<Option<Receiver<vd_deeplint::LintReport>>>,
    pub ghost: Ghost,
    pub status: RefCell<StatusModel>,
    pub format_result: RefCell<Option<Result<String, String>>>,
    pub find_anchor: RefCell<Option<Position>>,
    pub lsp_generation: Cell<u64>,
}

/// The widgets a user interacts with.
pub struct Ui {
    pub window: gtk::ApplicationWindow,
    pub notebook: gtk::Notebook,
    pub status: gtk::Label,
    pub panel_list: gtk::ListBox,
    pub panel_revealer: gtk::Revealer,
    pub find_bar: gtk::Revealer,
    pub find_entry: gtk::SearchEntry,
    pub replace_entry: gtk::Entry,
    pub panel_rows: RefCell<Vec<ViewDiagnostic>>,
    pub state: Rc<State>,
    /// The overlay that carries the ghost text; its child must be pointer
    /// transparent (issue #2 / AC-3).
    pub overlay: gtk::Overlay,
    pub ghost_fixed: gtk::Fixed,
    gutter_refresh: Cell<bool>,
    close_confirm: RefCell<Option<CloseConfirmation>>,
    close_request: RefCell<Option<String>>,
}

impl Ui {
    fn current_index(&self) -> Option<usize> {
        self.notebook.current_page().map(|page| page as usize)
    }

    #[must_use]
    pub fn current_tab(&self) -> Option<Rc<Tab>> {
        let index = self.current_index()?;
        self.state.tabs.borrow().get(index).cloned()
    }

    /// Put keyboard focus on the current tab's editor.
    ///
    /// Issue #1: the window opened with focus on the toolbar / tab strip, so
    /// ordinary characters never reached the buffer. The editor view must own
    /// the focus at startup and after every tab change (AC-1, AC-2).
    fn focus_editor(&self) {
        if let Some(tab) = self.current_tab() {
            tab.view.grab_focus();
        }
    }

    /// Open a file in a new tab (or switch to it when it is already open).
    ///
    /// # Errors
    /// Returns the editor error when the file cannot be read.
    pub fn open_path(self: &Rc<Self>, path: &Path) -> Result<Rc<Tab>> {
        let canonical = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        if let Some(existing) = self
            .state
            .tabs
            .borrow()
            .iter()
            .find(|tab| {
                tab.path
                    .borrow()
                    .as_ref()
                    .is_some_and(|open| *open == canonical)
            })
            .cloned()
        {
            if let Some(page) = self.notebook.page_num(&existing.widget) {
                self.notebook.set_current_page(Some(page));
            }
            return Ok(existing);
        }
        let tab = Tab::open(&canonical, &self.state.settings)?;
        Ok(self.add_tab(tab))
    }

    /// A new untitled tab.
    pub fn new_untitled(self: &Rc<Self>) -> Rc<Tab> {
        let tab = Tab::untitled(&self.state.settings);
        self.add_tab(tab)
    }

    fn add_tab(self: &Rc<Self>, tab: Tab) -> Rc<Tab> {
        let tab = Rc::new(tab);
        let label = gtk::Label::new(Some(&tab.title()));
        let page = self.notebook.append_page(&tab.widget, Some(&label));
        // Tabs opened after the window was shown must be shown themselves,
        // otherwise GTK keeps the current page on the already-visible tab.
        tab.widget.show_all();
        label.show();
        self.state.tabs.borrow_mut().push(Rc::clone(&tab));
        setup_gutter(&tab);
        self.notebook.set_current_page(Some(page));

        let weak = Rc::downgrade(self);
        tab.buffer.connect_changed(move |_| {
            if let Some(ui) = weak.upgrade() {
                if !ui.state.ghost.is_accepting() {
                    ui.state.ghost.cancel();
                }
                ui.refresh_titles();
                ui.refresh_status();
            }
        });
        let weak = Rc::downgrade(self);
        tab.buffer.connect_mark_set(move |_, _, _| {
            if let Some(ui) = weak.upgrade() {
                if ui.state.ghost.has_pending() {
                    ui.position_ghost();
                }
                ui.refresh_status();
            }
        });
        let weak = Rc::downgrade(self);
        tab.view.connect_key_press_event(move |_, event| {
            let Some(ui) = weak.upgrade() else {
                return gtk::glib::Propagation::Proceed;
            };
            match event.keyval() {
                gtk::gdk::keys::constants::Tab if ui.state.ghost.has_pending() => {
                    ui.accept_fim();
                    gtk::glib::Propagation::Stop
                }
                gtk::gdk::keys::constants::Escape if ui.state.ghost.has_pending() => {
                    ui.state.ghost.cancel();
                    ui.refresh_status();
                    gtk::glib::Propagation::Stop
                }
                _ => gtk::glib::Propagation::Proceed,
            }
        });

        if let Some(uri) = tab.uri() {
            let lsp = self.state.lsp.borrow();
            if let Some(bridge) = lsp.as_ref() {
                bridge.open(uri, tab.language_id(), tab.text());
            }
        }
        self.refresh_status();
        tab
    }

    /// Save the current tab (asks for a path when it has none).
    pub fn save_current(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        if tab.path.borrow().is_none() {
            self.save_as_dialog();
            return;
        }
        match tab.save() {
            Ok(()) => {
                self.set_info(format!("saved {}", tab.title()));
                self.refresh_titles();
                self.refresh_status();
            }
            Err(error) => self.set_info(format!("save failed: {error:#}")),
        }
    }

    fn save_as_dialog(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let dialog = gtk::FileChooserDialog::new(
            Some("Save As"),
            Some(&self.window),
            gtk::FileChooserAction::Save,
        );
        dialog.add_buttons(&[
            ("Cancel", gtk::ResponseType::Cancel),
            ("Save", gtk::ResponseType::Accept),
        ]);
        if let Some(path) = tab.path.borrow().as_ref() {
            if let Some(name) = path.file_name() {
                dialog.set_current_name(name.to_string_lossy().as_ref());
            }
        }
        let response = dialog.run();
        let path = dialog.filename();
        dialog.close();
        if response != gtk::ResponseType::Accept {
            return;
        }
        let Some(path) = path else {
            return;
        };
        match tab.save_as(&path) {
            Ok(()) => {
                self.set_info(format!("saved as {}", path.display()));
                self.refresh_titles();
                self.refresh_status();
                if let Some(uri) = tab.uri() {
                    let lsp = self.state.lsp.borrow();
                    if let Some(bridge) = lsp.as_ref() {
                        bridge.open(uri, tab.language_id(), tab.text());
                    }
                }
            }
            Err(error) => self.set_info(format!("save as failed: {error:#}")),
        }
    }

    fn open_dialog(self: &Rc<Self>) {
        let dialog = gtk::FileChooserDialog::new(
            Some("Open"),
            Some(&self.window),
            gtk::FileChooserAction::Open,
        );
        dialog.add_buttons(&[
            ("Cancel", gtk::ResponseType::Cancel),
            ("Open", gtk::ResponseType::Accept),
        ]);
        let filter = gtk::FileFilter::new();
        filter.set_name(Some("Verilog / SystemVerilog"));
        for pattern in ["*.v", "*.vh", "*.sv", "*.svh", "*.svi"] {
            filter.add_pattern(pattern);
        }
        dialog.add_filter(filter);
        let response = dialog.run();
        let path = dialog.filename();
        dialog.close();
        if response != gtk::ResponseType::Accept {
            return;
        }
        let Some(path) = path else {
            return;
        };
        if let Err(error) = self.open_path(&path) {
            self.set_info(format!("open failed: {error:#}"));
        }
    }

    /// Close a tab, asking about unsaved changes when requested.
    /// Replace the modal close confirmation for tests: `Some(false)` keeps the
    /// tab, `Some(true)` closes without saving. `None` restores the dialog.
    pub fn set_close_confirmation(&self, confirm: Option<CloseConfirmation>) {
        *self.close_confirm.borrow_mut() = confirm;
    }

    /// The tab title of the most recent close confirmation request, if any.
    #[must_use]
    pub fn last_close_request(&self) -> Option<String> {
        self.close_request.borrow().clone()
    }

    /// Jump to a 1-based line (the same path the Ctrl+G dialog uses).
    pub fn goto_line_now(&self, line: u32) -> bool {
        let Some(tab) = self.current_tab() else {
            return false;
        };
        let line = line
            .saturating_sub(1)
            .min(tab.document.line_count().saturating_sub(1));
        tab.set_cursor(Position::new(line, 0))
    }

    fn confirm_close_dialog(self: &Rc<Self>, tab: &Tab) -> bool {
        let dialog = gtk::MessageDialog::new(
            Some(&self.window),
            gtk::DialogFlags::MODAL,
            gtk::MessageType::Question,
            gtk::ButtonsType::None,
            &format!("Save changes to {}?", tab.title()),
        );
        dialog.add_button("Cancel", gtk::ResponseType::Cancel);
        dialog.add_button("Discard", gtk::ResponseType::Reject);
        dialog.add_button("Save", gtk::ResponseType::Accept);
        let response = dialog.run();
        dialog.close();
        match response {
            gtk::ResponseType::Accept => {
                if tab.path.borrow().is_none() {
                    self.save_as_dialog();
                    false
                } else if let Err(error) = tab.save() {
                    self.set_info(format!("save failed: {error:#}"));
                    false
                } else {
                    true
                }
            }
            gtk::ResponseType::Reject => true,
            _ => false,
        }
    }

    /// Close a tab, asking about unsaved changes when requested.
    pub fn close_tab_at(self: &Rc<Self>, index: usize, ask: bool) {
        let tab = self.state.tabs.borrow().get(index).cloned();
        let Some(tab) = tab else {
            return;
        };
        if ask && tab.document.needs_close_confirmation() {
            *self.close_request.borrow_mut() = Some(tab.title());
            let injected = self
                .close_confirm
                .borrow()
                .as_ref()
                .map(|confirm| confirm(&tab.title()));
            match injected {
                Some(false) => return,
                Some(true) => {}
                None => {
                    if !self.confirm_close_dialog(&tab) {
                        return;
                    }
                }
            }
        }
        self.state.tabs.borrow_mut().remove(index);
        if let Some(page) = self.notebook.page_num(&tab.widget) {
            self.notebook.remove_page(Some(page));
        }
        self.refresh_status();
    }

    fn undo(self: &Rc<Self>) {
        if let Some(tab) = self.current_tab() {
            if tab.document.undo() {
                self.refresh_titles();
                self.refresh_status();
            } else {
                self.set_info("nothing to undo");
            }
        }
    }

    fn redo(self: &Rc<Self>) {
        if let Some(tab) = self.current_tab() {
            if tab.document.redo() {
                self.refresh_titles();
                self.refresh_status();
            } else {
                self.set_info("nothing to redo");
            }
        }
    }

    fn show_find(self: &Rc<Self>, replace: bool) {
        self.find_bar.set_reveal_child(true);
        self.replace_entry.set_visible(replace);
        self.find_entry.grab_focus();
    }

    /// Find the next match from the last match (or the cursor).
    pub fn find_next_now(self: &Rc<Self>) {
        let needle = self.find_entry.text().to_string();
        if needle.is_empty() {
            return;
        }
        let Some(tab) = self.current_tab() else {
            return;
        };
        let from = self
            .state
            .find_anchor
            .borrow_mut()
            .take()
            .unwrap_or_else(|| tab.cursor());
        match tab.document.find_next(&needle, from, true) {
            Some(range) => {
                tab.select_range(range);
                *self.state.find_anchor.borrow_mut() = Some(range.end);
                self.set_info(format!("match at line {}", range.start.line + 1));
            }
            None => self.set_info(format!("no match for {needle:?}")),
        }
    }

    pub fn replace_next_now(self: &Rc<Self>) {
        let needle = self.find_entry.text().to_string();
        let replacement = self.replace_entry.text().to_string();
        if needle.is_empty() {
            return;
        }
        let Some(tab) = self.current_tab() else {
            return;
        };
        let from = self
            .state
            .find_anchor
            .borrow_mut()
            .take()
            .unwrap_or_else(|| tab.cursor());
        match tab.document.replace_next(&needle, &replacement, from, true) {
            Ok(Some(range)) => {
                tab.select_range(range);
                *self.state.find_anchor.borrow_mut() = Some(range.end);
                self.set_info("replaced one match");
            }
            Ok(None) => self.set_info(format!("no match for {needle:?}")),
            Err(error) => self.set_info(format!("replace failed: {error}")),
        }
    }

    fn replace_all_now(self: &Rc<Self>) {
        let needle = self.find_entry.text().to_string();
        let replacement = self.replace_entry.text().to_string();
        if needle.is_empty() {
            return;
        }
        let Some(tab) = self.current_tab() else {
            return;
        };
        match tab.document.replace_all(&needle, &replacement, true) {
            Ok(count) => self.set_info(format!("replaced {count} match(es) in one undo step")),
            Err(error) => self.set_info(format!("replace all failed: {error}")),
        }
    }

    fn goto_dialog(self: &Rc<Self>) {
        if self.current_tab().is_none() {
            return;
        }
        let dialog = gtk::Dialog::with_buttons(
            Some("Go to Line"),
            Some(&self.window),
            gtk::DialogFlags::MODAL,
            &[] as &[(&str, gtk::ResponseType)],
        );
        dialog.add_button("Cancel", gtk::ResponseType::Cancel);
        dialog.add_button("Go", gtk::ResponseType::Accept);
        let entry = gtk::Entry::new();
        entry.set_placeholder_text(Some("line number"));
        dialog.content_area().add(&entry);
        let response = dialog.run();
        let text = entry.text().to_string();
        dialog.close();
        if response != gtk::ResponseType::Accept {
            return;
        }
        let Ok(line) = text.trim().parse::<u32>() else {
            self.set_info(format!("not a line number: {text}"));
            return;
        };
        self.goto_line_now(line);
    }

    /// Request formatting from the LSP; the reply is applied as one undo step.
    pub fn request_format(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(uri) = tab.uri() else {
            self.set_info(format!(
                "formatting needs a saved file and the Verible LSP ({NEEDS_FILE})"
            ));
            return;
        };
        let lsp = self.state.lsp.borrow();
        let Some(bridge) = lsp.as_ref() else {
            self.set_info("formatting needs the Verible LSP");
            return;
        };
        *self.state.format_result.borrow_mut() = None;
        bridge.format(
            uri,
            self.state.settings.tab_width,
            self.state.settings.insert_spaces,
        );
        self.set_info("formatting…");
    }

    /// Run SLANG deep lint on the current tab in the background.
    pub fn request_slang(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let request = match slang_worker::request_for(&tab, &self.state.startup) {
            Ok(request) => request,
            Err(error) => {
                self.set_info(format!("{error}"));
                return;
            }
        };
        let (sender, receiver) = std::sync::mpsc::channel();
        slang_worker::spawn(request, sender);
        *self.state.slang_rx.borrow_mut() = Some(receiver);
        self.set_info("SLANG deep lint running…");
    }

    /// Synchronous variant used by `--selftest slang` (same production path).
    ///
    /// # Errors
    /// Returns an error for an untitled tab.
    pub fn run_slang_now(&self) -> Result<vd_deeplint::LintReport> {
        let tab = self
            .current_tab()
            .ok_or_else(|| anyhow!("no current tab"))?;
        let request = slang_worker::request_for(&tab, &self.state.startup)?;
        Ok(slang_worker::lint(&request))
    }

    fn request_definition(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(uri) = tab.uri() else {
            self.set_info(format!("definition needs a saved file ({NEEDS_FILE})"));
            return;
        };
        let lsp = self.state.lsp.borrow();
        let Some(bridge) = lsp.as_ref() else {
            self.set_info("definition needs the Verible LSP");
            return;
        };
        bridge.definition(uri, position_adapter::position_to_lsp(tab.cursor()));
        self.set_info("looking up definition…");
    }

    fn request_references(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(uri) = tab.uri() else {
            self.set_info(format!("references need a saved file ({NEEDS_FILE})"));
            return;
        };
        let lsp = self.state.lsp.borrow();
        let Some(bridge) = lsp.as_ref() else {
            self.set_info("references need the Verible LSP");
            return;
        };
        bridge.references(uri, position_adapter::position_to_lsp(tab.cursor()));
        self.set_info("looking up references…");
    }

    fn request_rename(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(uri) = tab.uri() else {
            self.set_info(format!("rename needs a saved file ({NEEDS_FILE})"));
            return;
        };
        let dialog = gtk::Dialog::with_buttons(
            Some("Rename Preview"),
            Some(&self.window),
            gtk::DialogFlags::MODAL,
            &[] as &[(&str, gtk::ResponseType)],
        );
        dialog.add_button("Cancel", gtk::ResponseType::Cancel);
        dialog.add_button("Preview", gtk::ResponseType::Accept);
        let entry = gtk::Entry::new();
        entry.set_placeholder_text(Some("new name"));
        dialog.content_area().add(&entry);
        let response = dialog.run();
        let new_name = entry.text().to_string();
        dialog.close();
        if response != gtk::ResponseType::Accept || new_name.trim().is_empty() {
            return;
        }
        let lsp = self.state.lsp.borrow();
        let Some(bridge) = lsp.as_ref() else {
            self.set_info("rename needs the Verible LSP");
            return;
        };
        bridge.rename(
            uri,
            position_adapter::position_to_lsp(tab.cursor()),
            new_name.trim().to_string(),
        );
        self.set_info("rename preview requested (nothing is applied)");
    }

    fn request_outline(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(uri) = tab.uri() else {
            self.set_info(format!("outline needs a saved file ({NEEDS_FILE})"));
            return;
        };
        let lsp = self.state.lsp.borrow();
        let Some(bridge) = lsp.as_ref() else {
            self.set_info("outline needs the Verible LSP");
            return;
        };
        bridge.symbols(uri);
        self.set_info("loading document symbols…");
    }

    /// Request an FIM completion for the text around the cursor.
    pub fn request_fim(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        if !self.state.ghost.enabled() {
            // AC-B1: the status bar's FIM column already renders this reason; a
            // `set_info` copy here printed it a second time.
            return;
        }
        let (prefix, suffix) = fim_context(&tab);
        match self.state.ghost.request(&prefix, &suffix) {
            Ok(()) => self.set_info("FIM: waiting for a completion (Esc cancels)"),
            // A transient rejection is still worth a message unless it is the
            // persistent reason the FIM column already renders.
            Err(reason) => {
                if self.state.ghost.reason().as_deref() != Some(reason.as_str()) {
                    self.set_info(reason);
                }
            }
        }
    }

    fn accept_fim(self: &Rc<Self>) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(text) = self.state.ghost.pending_text() else {
            return;
        };
        self.state.ghost.set_accepting(true);
        let position = tab.cursor();
        match tab.document.insert(position, &text) {
            Ok(()) => {
                let end = advance_position(position, &text);
                tab.set_cursor(end);
                self.set_info("FIM completion accepted");
            }
            Err(error) => self.set_info(format!("inserting the completion failed: {error}")),
        }
        self.state.ghost.set_accepting(false);
        self.state.ghost.hide();
        self.refresh_titles();
        self.refresh_status();
    }

    fn toggle_panel(self: &Rc<Self>) {
        let visible = !self.panel_revealer.reveals_child();
        self.panel_revealer.set_reveal_child(visible);
    }

    pub fn set_info(&self, info: impl Into<String>) {
        self.state.status.borrow_mut().set_info(info);
        self.refresh_status();
    }

    /// Rebuild the problem panel from the current tab's store.
    pub fn refresh_panel(&self) {
        while let Some(row) = self.panel_list.row_at_index(0) {
            self.panel_list.remove(&row);
        }
        let mut rows = Vec::new();
        if let Some(tab) = self.current_tab() {
            rows.extend(tab.store.borrow().all().cloned());
        }
        for row in &rows {
            let label = gtk::Label::new(Some(&format!(
                "{} [{}] {}",
                row.severity.as_str(),
                row.source.as_str(),
                row.panel_text()
            )));
            label.set_xalign(0.0);
            label.set_ellipsize(gtk::pango::EllipsizeMode::End);
            let list_row = gtk::ListBoxRow::new();
            list_row.add(&label);
            self.panel_list.add(&list_row);
        }
        *self.panel_rows.borrow_mut() = rows;
        self.panel_revealer
            .set_reveal_child(!self.panel_rows.borrow().is_empty());
    }

    fn jump_to_diagnostic(&self, index: usize) {
        let Some(diagnostic) = self.panel_rows.borrow().get(index).cloned() else {
            return;
        };
        let Some(tab) = self.current_tab() else {
            return;
        };
        match diagnostic.range {
            Some(range) => {
                tab.select_range(range);
                tab.set_cursor(range.start);
                self.set_info(format!(
                    "{}: {}",
                    diagnostic.severity.as_str(),
                    diagnostic.message
                ));
            }
            None => self.set_info("this diagnostic has no position (no location reported)"),
        }
    }

    fn refresh_gutter_marks(&self) {
        if self.gutter_refresh.get() {
            return;
        }
        self.gutter_refresh.set(true);
        self.refresh_gutter_marks_inner();
        self.gutter_refresh.set(false);
    }

    fn refresh_gutter_marks_inner(&self) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let worst = tab.store.borrow().worst_severity_per_line();
        // AC-5: an unchanged diagnostic set must not delete and recreate every
        // mark (each mark change used to re-enter refresh_status through
        // `mark-set`, which made the 50 ms poll the main CPU cost of issue #2).
        if *tab.marks_key.borrow() == worst {
            return;
        }
        let previous = std::mem::take(&mut *tab.marks.borrow_mut());
        for mark in previous {
            tab.buffer.delete_mark(&mark);
        }
        let mut created = Vec::new();
        for (&line, &severity) in &worst {
            let Some(position) = tab.document.line_start_position(line) else {
                continue;
            };
            let Ok(iter) = tab.document.iter_at_position(position) else {
                continue;
            };
            let category = format!("vedit-{}", severity.as_str());
            // Mark names must be unique while a mark exists.
            let name = format!("vedit-diagnostic-{line}-{}", severity.as_str());
            if let Some(mark) = tab.buffer.create_source_mark(Some(&name), &category, &iter) {
                created.push(mark);
            }
        }
        *tab.marks.borrow_mut() = created;
        *tab.marks_key.borrow_mut() = worst;
    }

    fn refresh_titles(&self) {
        let tabs = self.state.tabs.borrow();
        for (index, tab) in tabs.iter().enumerate() {
            if let Some(page) = self.notebook.page_num(&tab.widget) {
                let mut title = tab.title();
                if tab.document.needs_close_confirmation() {
                    title.push_str(" *");
                }
                if let Some(child) = self.notebook.tab_label(&tab.widget) {
                    if let Some(label) = child.downcast_ref::<gtk::Label>() {
                        label.set_text(&title);
                    }
                }
                let _ = index;
                let _ = page;
            }
        }
    }

    /// The status bar text (also used by the selftests).
    #[must_use]
    pub fn status_text(&self) -> String {
        let mut parts = Vec::new();
        if let Some(tab) = self.current_tab() {
            let cursor = tab.cursor();
            parts.push(format!("{}:{}", cursor.line + 1, cursor.character + 1));
            if let Some(path) = tab.path.borrow().as_ref() {
                parts.push(path.display().to_string());
            }
            let language = tab.language_id();
            parts.push(language);
            let (verible, slang) = tab.store.borrow().counts();
            parts.push(format!("verible {verible}"));
            parts.push(format!("slang {slang}"));
        }
        // AC-B1: the reason already carries its "FIM" tag, so the column adds no
        // label of its own (the old `"FIM: {reason}"` printed "FIM: FIM …"). The
        // column is the ONLY place the reason is rendered: no `set_info` path
        // repeats it, so it appears exactly once and never flickers in/out.
        if let Some(reason) = self.state.ghost.reason() {
            parts.push(reason);
        }
        // The live LSP status is composed here, never stored in the message:
        // repeated polls therefore cannot grow the text (AC-4).
        self.state.status.borrow().text(&parts)
    }

    pub(crate) fn refresh_status(&self) {
        let text = self.status_text();
        if self.status.text() != text {
            self.status.set_text(&text);
        }
        // Marks are owned and deleted explicitly now, so this cannot duplicate
        // them; keeping the gutter in sync here covers every store change.
        self.refresh_gutter_marks();
    }

    /// The current tab's diagnostic counts.
    #[must_use]
    pub fn diagnostics_counts(&self) -> (usize, usize) {
        self.current_tab()
            .map_or((0, 0), |tab| tab.store.borrow().counts())
    }

    /// Apply formatting edits (public for the selftest: the LSP event path uses
    /// the same function).
    ///
    /// # Errors
    /// Returns an error when the edits cannot be applied to the buffer.
    pub fn apply_format_edits(&self, edits: &[lsp_types::TextEdit]) -> Result<String> {
        let tab = self
            .current_tab()
            .ok_or_else(|| anyhow!("no current tab"))?;
        let old_text = tab.text();
        let new_text = vd_lsp::apply_text_edits(&old_text, edits)
            .context("the formatter returned edits that do not apply")?;
        if new_text == old_text {
            return Ok("already formatted".to_string());
        }
        let edit = Edit::new(tab.full_range(), new_text);
        tab.document
            .apply_edits_one_step(&[edit])
            .context("applying the formatting edits")?;
        Ok("formatted".to_string())
    }

    fn position_ghost(&self) {
        let Some(tab) = self.current_tab() else {
            return;
        };
        let Some(iter) = position_adapter::iter_for(&tab.document, tab.cursor()) else {
            return;
        };
        let rect = tab.view.iter_location(&iter);
        let (x, y) =
            tab.view
                .buffer_to_window_coords(gtk::TextWindowType::Widget, rect.x(), rect.y());
        if let Some((fixed_x, fixed_y)) = tab.view.translate_coordinates(&self.ghost_fixed, x, y) {
            self.ghost_fixed
                .move_(&self.state.ghost.label, fixed_x + 2, fixed_y);
        }
    }

    /// Poll all background producers; called on a 50 ms timer and by the tests.
    pub fn poll_background(self: &Rc<Self>) {
        // R3: a missing LSP bridge only means there are no LSP events — it must
        // not hide the SLANG and FIM producers (AC-6).
        let events: Vec<LspEvent> = {
            let mut lsp = self.state.lsp.borrow_mut();
            let mut events = Vec::new();
            if let Some(bridge) = lsp.as_mut() {
                while let Ok(event) = bridge.events.try_recv() {
                    events.push(event);
                }
            }
            events
        };
        for event in events {
            self.handle_lsp_event(event);
        }

        let report = self
            .state
            .slang_rx
            .borrow_mut()
            .as_ref()
            .and_then(|receiver| receiver.try_recv().ok());
        if let Some(report) = report {
            self.apply_slang_report(&report);
        }

        if !self.state.ghost.has_pending() {
            if let Some(result) = self.state.ghost.poll() {
                match result {
                    Ok(text) => {
                        self.state.ghost.show_pending(text);
                        self.position_ghost();
                        self.set_info("FIM completion ready (Tab accepts, Esc cancels)");
                    }
                    Err(error) => {
                        self.state.ghost.cancel();
                        self.set_info(error.reason());
                    }
                }
            }
        } else {
            self.position_ghost();
        }
        self.sync_lsp_status();
    }

    fn sync_lsp_status(&self) {
        let message = {
            let lsp = self.state.lsp.borrow();
            match lsp.as_ref() {
                None => "Verible LSP: not available".to_string(),
                Some(_) => {
                    let generation = self.state.lsp_generation.get();
                    format!("Verible LSP: generation {generation}")
                }
            }
        };
        // REPLACE the live status; never append it to the message (AC-4).
        self.state.status.borrow_mut().set_lsp(message);
        self.refresh_status();
    }

    fn handle_lsp_event(self: &Rc<Self>, event: LspEvent) {
        match event {
            LspEvent::Status {
                message,
                running,
                restarts,
            } => {
                let text = format!("{message} (running: {running}, restarts: {restarts})");
                self.set_info(text);
            }
            LspEvent::Diagnostics {
                uri,
                diagnostics: published,
                generation,
            } => {
                self.state.lsp_generation.set(generation);
                let target: Option<Rc<Tab>> = self
                    .state
                    .tabs
                    .borrow()
                    .iter()
                    .find(|tab| tab.uri().is_some_and(|tab_uri| tab_uri.to_string() == uri))
                    .cloned();
                if let Some(tab) = target {
                    let view = diagnostics::from_lsp(&tab.document, &published);
                    tab.store.borrow_mut().set_verible(view);
                    self.refresh_panel();
                    self.refresh_gutter_marks();
                    self.refresh_status();
                }
            }
            LspEvent::Opened { ok, error } => {
                if !ok {
                    self.set_info(format!(
                        "LSP could not open the document: {}",
                        error.unwrap_or_default()
                    ));
                }
            }
            LspEvent::Formatted { ok, edits, error } => {
                if ok {
                    match self.apply_format_edits(&edits) {
                        Ok(message) => {
                            *self.state.format_result.borrow_mut() = Some(Ok(message.clone()));
                            self.set_info(message);
                        }
                        Err(err) => {
                            *self.state.format_result.borrow_mut() = Some(Err(format!("{err:#}")));
                            self.set_info(format!("formatting failed: {err:#}"));
                        }
                    }
                    self.refresh_titles();
                    self.refresh_status();
                } else {
                    let message = error.unwrap_or_else(|| "formatting failed".to_string());
                    *self.state.format_result.borrow_mut() = Some(Err(message.clone()));
                    self.set_info(message);
                }
            }
            LspEvent::Definitions { locations, error } => {
                if let Some(error) = error {
                    self.set_info(format!("definition failed: {error}"));
                } else if locations.is_empty() {
                    self.set_info("no definition found");
                } else if locations.len() == 1 {
                    self.jump_to_location(&locations[0]);
                } else {
                    self.show_locations("Definitions", locations);
                }
            }
            LspEvent::References { locations, error } => {
                if let Some(error) = error {
                    self.set_info(format!("references failed: {error}"));
                } else if locations.is_empty() {
                    self.set_info("no references found");
                } else if locations.len() == 1 {
                    self.jump_to_location(&locations[0]);
                } else {
                    self.show_locations("References", locations);
                }
            }
            LspEvent::Symbols { symbols, error } => {
                if let Some(error) = error {
                    self.set_info(format!("outline failed: {error}"));
                } else if symbols.is_empty() {
                    self.set_info("no document symbols reported");
                } else {
                    self.show_symbols(&symbols);
                }
            }
            LspEvent::Renamed { preview, error } => {
                if let Some(error) = error {
                    self.set_info(format!("rename preview failed: {error}"));
                } else {
                    show_message(&self.window, "Rename preview (not applied)", &preview);
                }
            }
        }
    }

    fn jump_to_location(self: &Rc<Self>, location: &lsp_types::Location) {
        let Ok(path) = location.uri.to_file_path() else {
            self.set_info(format!("cannot open non-file location {}", location.uri));
            return;
        };
        let same_file = self
            .current_tab()
            .and_then(|tab| tab.path.borrow().clone())
            .is_some_and(|open| open == path);
        let target = if same_file {
            self.current_tab()
        } else {
            match self.open_path(&path) {
                Ok(tab) => Some(tab),
                Err(error) => {
                    self.set_info(format!("open failed: {error:#}"));
                    None
                }
            }
        };
        if let Some(tab) = target {
            let range = position_adapter::range_from_lsp(&tab.document, location.range);
            tab.select_range(range);
            tab.set_cursor(range.start);
        }
    }

    fn show_locations(self: &Rc<Self>, title: &str, locations: Vec<lsp_types::Location>) {
        let dialog = gtk::Dialog::with_buttons(
            Some(title),
            Some(&self.window),
            gtk::DialogFlags::MODAL,
            &[] as &[(&str, gtk::ResponseType)],
        );
        dialog.add_button("Close", gtk::ResponseType::Close);
        dialog.set_default_size(620, 380);
        let list = gtk::ListBox::new();
        let scroller = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
        scroller.set_child(Some(&list));
        dialog.content_area().add(&scroller);
        let ui = self.clone();
        let rows = locations.clone();
        list.connect_row_activated(move |_, row| {
            ui.jump_to_location(&rows[row.index() as usize]);
            if let Some(window) = row
                .toplevel()
                .and_then(|widget| widget.downcast::<gtk::Dialog>().ok())
            {
                window.close();
            }
        });
        for location in &locations {
            let text = format!(
                "{}:{}:{}",
                location.uri,
                location.range.start.line + 1,
                location.range.start.character + 1
            );
            add_row(&list, &text);
        }
        dialog.show_all();
        dialog.run();
        dialog.close();
    }

    fn show_symbols(self: &Rc<Self>, symbols: &[(String, Option<lsp_types::Range>)]) {
        let dialog = gtk::Dialog::with_buttons(
            Some("Outline"),
            Some(&self.window),
            gtk::DialogFlags::MODAL,
            &[] as &[(&str, gtk::ResponseType)],
        );
        dialog.add_button("Close", gtk::ResponseType::Close);
        dialog.set_default_size(520, 380);
        let list = gtk::ListBox::new();
        let scroller = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
        scroller.set_child(Some(&list));
        dialog.content_area().add(&scroller);
        let ui = self.clone();
        let rows = symbols.to_vec();
        list.connect_row_activated(move |_, row| {
            if let Some((_, Some(range))) = rows.get(row.index() as usize) {
                if let Some(tab) = ui.current_tab() {
                    let range = position_adapter::range_from_lsp(&tab.document, *range);
                    tab.select_range(range);
                    tab.set_cursor(range.start);
                }
            }
            if let Some(window) = row
                .toplevel()
                .and_then(|widget| widget.downcast::<gtk::Dialog>().ok())
            {
                window.close();
            }
        });
        for (name, range) in symbols {
            let text = match range {
                Some(range) => format!("{}  (line {})", name, range.start.line + 1),
                None => name.clone(),
            };
            add_row(&list, &text);
        }
        dialog.show_all();
        dialog.run();
        dialog.close();
    }

    fn apply_slang_report(&self, report: &vd_deeplint::LintReport) {
        if let Some(failure) = &report.failure {
            self.set_info(failure.user_message());
            return;
        }
        if let Some(tab) = self.current_tab() {
            let view = diagnostics::from_slang(report, &tab.document);
            tab.store.borrow_mut().set_slang(view);
            self.refresh_panel();
            self.refresh_gutter_marks();
            self.refresh_status();
        }
        self.set_info(format!("SLANG: {}", report.summary()));
    }

    /// Capture the realized window into a PNG (diagnostics/screenshots).
    ///
    /// # Errors
    /// Returns an error when Cairo cannot create the surface or write the PNG.
    pub fn capture_screenshot(&self, path: &Path) -> Result<ScreenshotSummary> {
        // `gtk_widget_draw` is a no-op while the toplevel still has a pending
        // size allocation (`widget->priv->alloc_needed`, a Gtk-CRITICAL we saw in
        // the clean room), which would silently produce a black PNG. Map the
        // window, force an explicit allocation right before the draw, verify the
        // pixels are not uniform, and only then write the file.
        let main_context = gtk::glib::MainContext::default();
        self.window.show_all();
        self.window.present();
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            while main_context.iteration(false) {}
            if self.window.is_mapped()
                && self.window.allocated_width() > 0
                && self.window.allocated_height() > 0
            {
                break;
            }
            if Instant::now() >= deadline {
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        let (default_width, default_height) = self.window.default_size();
        let mut last_detail = String::from("no draw attempt");
        for attempt in 0..3 {
            if !self.window.is_mapped() {
                last_detail = "the window is not mapped (no window manager?)".to_string();
                break;
            }
            let allocation = self.window.allocation();
            let width = if allocation.width() > 0 {
                allocation.width()
            } else {
                default_width.max(1)
            };
            let height = if allocation.height() > 0 {
                allocation.height()
            } else {
                default_height.max(1)
            };
            // Clear the pending allocation synchronously; do not pump the main
            // loop afterwards (a late resize idle would set alloc_needed again).
            self.window
                .size_allocate(&gtk::Allocation::new(0, 0, width, height));
            let mut surface =
                gtk::cairo::ImageSurface::create(gtk::cairo::Format::Rgb24, width, height)
                    .context("creating the screenshot surface")?;
            {
                let context =
                    gtk::cairo::Context::new(&surface).context("creating a Cairo context")?;
                self.window.draw(&context);
            }
            surface.flush();
            let stride = usize::try_from(surface.stride()).context("negative surface stride")?;
            let data = surface.data().context("reading the screenshot pixels")?;
            let width_usize = usize::try_from(width).context("negative width")?;
            let height_usize = usize::try_from(height).context("negative height")?;
            let mut rgba = Vec::with_capacity(width_usize * height_usize * 4);
            let mut samples = std::collections::BTreeSet::new();
            let mut min_channel = 255u8;
            let mut max_channel = 0u8;
            for row in 0..height_usize {
                let start = row * stride;
                for pixel in 0..width_usize {
                    let offset = start + pixel * 4;
                    let (blue, green, red) = (data[offset], data[offset + 1], data[offset + 2]);
                    rgba.push(red);
                    rgba.push(green);
                    rgba.push(blue);
                    rgba.push(0xff);
                    min_channel = min_channel.min(red).min(green).min(blue);
                    max_channel = max_channel.max(red).max(green).max(blue);
                    if row % 64 == 0 && pixel % 64 == 0 {
                        samples.insert((red, green, blue));
                    }
                }
            }
            drop(data);
            if max_channel.saturating_sub(min_channel) >= 8 && samples.len() >= 4 {
                let bytes = glib::Bytes::from(&rgba);
                let pixbuf = gtk::gdk_pixbuf::Pixbuf::from_bytes(
                    &bytes,
                    gtk::gdk_pixbuf::Colorspace::Rgb,
                    true,
                    8,
                    width,
                    height,
                    width * 4,
                );
                pixbuf
                    .savev(path, "png", &[])
                    .with_context(|| format!("writing {}", path.display()))?;
                let bytes = std::fs::metadata(path)
                    .with_context(|| format!("stat {}", path.display()))?
                    .len();
                return Ok(ScreenshotSummary {
                    width,
                    height,
                    bytes,
                    sampled_colours: samples.len(),
                    channel_range: max_channel.saturating_sub(min_channel),
                });
            }
            last_detail = format!(
                "attempt {}: the rendered window is uniform/blank ({} sampled colours, channel range {})",
                attempt + 1,
                samples.len(),
                max_channel.saturating_sub(min_channel)
            );
            // Give the toolkit one more frame and retry the explicit allocation.
            for _ in 0..20 {
                while main_context.iteration(false) {}
                std::thread::sleep(Duration::from_millis(3));
            }
        }
        bail!(
            "refusing to write a blank screenshot ({last_detail}); window mapped={}, allocated={}x{}",
            self.window.is_mapped(),
            self.window.allocated_width(),
            self.window.allocated_height()
        );
    }
}

fn add_row(list: &gtk::ListBox, text: &str) {
    let label = gtk::Label::new(Some(text));
    label.set_xalign(0.0);
    label.set_ellipsize(gtk::pango::EllipsizeMode::End);
    let row = gtk::ListBoxRow::new();
    row.add(&label);
    list.add(&row);
}

fn show_message(parent: &gtk::ApplicationWindow, title: &str, text: &str) {
    let dialog = gtk::MessageDialog::new(
        Some(parent),
        gtk::DialogFlags::MODAL,
        gtk::MessageType::Info,
        gtk::ButtonsType::Close,
        text,
    );
    dialog.set_title(title);
    dialog.show_all();
    dialog.run();
    dialog.close();
}

fn setup_gutter(tab: &Tab) {
    for severity in [Severity::Error, Severity::Warning, Severity::Note] {
        let attributes = sourceview4::MarkAttributes::new();
        attributes.set_icon_name(severity.icon_name());
        tab.view
            .set_mark_attributes(&format!("vedit-{}", severity.as_str()), &attributes, 0);
    }
}

fn fim_context(tab: &Tab) -> (String, String) {
    let text = tab.document.text();
    let offset = tab
        .document
        .char_offset_at_position(tab.cursor())
        .unwrap_or(0);
    let chars: Vec<char> = text.chars().collect();
    let start = offset.saturating_sub(2000);
    let end = (offset + 2000).min(chars.len());
    (
        chars[start..offset.min(chars.len())].iter().collect(),
        chars[offset.min(chars.len())..end].iter().collect(),
    )
}

/// Build and own the application.
pub struct App {
    application: gtk::Application,
    inner: Rc<RefCell<Option<Rc<Ui>>>>,
}

impl App {
    pub fn new(opts: &Options) -> Self {
        let application = gtk::Application::new(Some(APP_ID), gtk::gio::ApplicationFlags::empty());
        let inner: Rc<RefCell<Option<Rc<Ui>>>> = Rc::new(RefCell::new(None));
        {
            let inner = Rc::clone(&inner);
            let opts = opts.clone();
            application.connect_activate(move |application| match build_ui(application, &opts) {
                Ok(ui) => {
                    ui.window.present();
                    let startup_messages = startup_messages(&ui.state.startup);
                    for message in startup_messages {
                        eprintln!("vd-app: {message}");
                        ui.set_info(message);
                    }
                    *inner.borrow_mut() = Some(ui);
                }
                Err(error) => {
                    eprintln!("vd-app: startup failed: {error:#}");
                    application.quit();
                }
            });
        }
        Self { application, inner }
    }

    /// Register and activate without GTK parsing our arguments (selftests).
    ///
    /// # Errors
    /// Returns the GApplication registration error.
    pub fn register_and_activate(&self) -> Result<()> {
        self.application
            .register(None::<&gtk::gio::Cancellable>)
            .map_err(|error| anyhow!("registering the application failed: {error}"))?;
        self.application.activate();
        Ok(())
    }

    #[must_use]
    pub fn ui(&self) -> Option<Rc<Ui>> {
        self.inner.borrow().clone()
    }

    /// Run the interactive loop (arguments already parsed).
    pub fn run(&self) -> i32 {
        let empty: [&str; 0] = [];
        self.application.run_with_args(&empty).into()
    }

    /// Shut the application down (selftest teardown).
    pub fn quit(&self) {
        self.application.quit();
    }
}

fn startup_messages(startup: &Startup) -> Vec<String> {
    let mut messages = Vec::new();
    if let Some(error) = &startup.config_error {
        messages.push(format!("configuration: {error}"));
    }
    if let Some(warning) = &startup.language_warning {
        messages.push(format!("highlighting: {warning}"));
    }
    if startup.tools.ls.is_none() {
        messages.push(format!(
            "Verible LS not found ({} resolution attempt(s)); LSP features are unavailable",
            startup.tools.attempts.len()
        ));
    }
    if startup.config_error.is_none() && !startup.warnings.is_empty() {
        messages.push(format!(
            "configuration warnings: {}",
            startup.warnings.len()
        ));
    }
    messages
}

fn build_ui(application: &gtk::Application, opts: &Options) -> Result<Rc<Ui>> {
    let cwd = std::env::current_dir().context("reading the current directory")?;
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|exe| exe.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| cwd.clone());
    let startup = Startup::load(&cwd, &exe_dir);
    let settings = startup.editor.clone();
    let ghost = Ghost::new(startup.fim_settings.clone());

    let window = gtk::ApplicationWindow::new(application);
    window.set_title(APP_NAME);
    window.set_default_size(1120, 780);

    let toolbar = gtk::Box::new(gtk::Orientation::Horizontal, 4);
    toolbar.set_margin_start(6);
    toolbar.set_margin_end(6);
    toolbar.set_margin_top(4);
    toolbar.set_margin_bottom(4);
    let new_button = gtk::Button::with_label("New");
    let open_button = gtk::Button::with_label("Open");
    let save_button = gtk::Button::with_label("Save");
    let save_as_button = gtk::Button::with_label("Save As");
    let format_button = gtk::Button::with_label("Format");
    let slang_button = gtk::Button::with_label("SLANG Lint");
    let rename_button = gtk::Button::with_label("Rename");
    let panel_button = gtk::Button::with_label("Problems");
    for button in [
        &new_button,
        &open_button,
        &save_button,
        &save_as_button,
        &format_button,
        &slang_button,
        &rename_button,
        &panel_button,
    ] {
        toolbar.pack_start(button, false, false, 0);
    }

    let notebook = gtk::Notebook::new();
    notebook.set_scrollable(true);
    let ghost_fixed = gtk::Fixed::new();
    ghost_fixed.add(&ghost.label);
    ghost.label.set_no_show_all(true);
    let overlay = gtk::Overlay::new();
    overlay.set_child(Some(&notebook));
    overlay.add_overlay(&ghost_fixed);
    // R4: the ghost text is decoration only, so its overlay must never consume
    // a pointer event (issue #2: the wheel stopped reaching the editor).
    overlay.set_overlay_pass_through(&ghost_fixed, true);

    let panel_list = gtk::ListBox::new();
    let panel_scroller =
        gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    panel_scroller.set_child(Some(&panel_list));
    panel_scroller.set_min_content_height(120);
    let panel_title = gtk::Label::new(Some("Problems"));
    panel_title.set_xalign(0.0);
    let panel_box = gtk::Box::new(gtk::Orientation::Vertical, 2);
    panel_box.pack_start(&panel_title, false, false, 0);
    panel_box.pack_start(&panel_scroller, true, true, 0);
    let panel_revealer = gtk::Revealer::new();
    panel_revealer.set_child(Some(&panel_box));
    panel_revealer.set_reveal_child(false);

    let paned = gtk::Paned::new(gtk::Orientation::Vertical);
    paned.add1(&overlay);
    paned.add2(&panel_revealer);
    paned.set_position(540);
    paned.set_vexpand(true);

    let find_entry = gtk::SearchEntry::new();
    find_entry.set_placeholder_text(Some("Find"));
    find_entry.set_hexpand(true);
    let replace_entry = gtk::Entry::new();
    replace_entry.set_placeholder_text(Some("Replace"));
    replace_entry.set_hexpand(true);
    replace_entry.set_visible(false);
    let find_prev = gtk::Button::with_label("Previous");
    let find_next = gtk::Button::with_label("Next");
    let replace_one = gtk::Button::with_label("Replace");
    let replace_all = gtk::Button::with_label("Replace All");
    let find_close = gtk::Button::with_label("Close");
    let find_box = gtk::Box::new(gtk::Orientation::Horizontal, 4);
    find_box.pack_start(&find_entry, true, true, 0);
    find_box.pack_start(&replace_entry, true, true, 0);
    find_box.pack_start(&find_prev, false, false, 0);
    find_box.pack_start(&find_next, false, false, 0);
    find_box.pack_start(&replace_one, false, false, 0);
    find_box.pack_start(&replace_all, false, false, 0);
    find_box.pack_start(&find_close, false, false, 0);
    let find_bar = gtk::Revealer::new();
    find_bar.set_child(Some(&find_box));
    find_bar.set_reveal_child(false);

    let status = gtk::Label::new(Some("ready"));
    status.set_xalign(0.0);
    status.set_ellipsize(gtk::pango::EllipsizeMode::End);
    status.set_margin_start(6);
    status.set_margin_end(6);
    status.set_margin_bottom(3);

    let content = gtk::Box::new(gtk::Orientation::Vertical, 2);
    content.pack_start(&toolbar, false, false, 0);
    content.pack_start(&paned, true, true, 0);
    content.pack_start(&find_bar, false, false, 0);
    content.pack_start(&status, false, false, 0);
    window.add(&content);

    let state = Rc::new(State {
        startup,
        settings,
        tabs: RefCell::new(Vec::new()),
        lsp: RefCell::new(None),
        slang_rx: RefCell::new(None),
        ghost,
        status: RefCell::new(StatusModel::default()),
        format_result: RefCell::new(None),
        find_anchor: RefCell::new(None),
        lsp_generation: Cell::new(0),
    });
    let ui = Rc::new(Ui {
        window,
        notebook,
        status,
        panel_list,
        panel_revealer,
        find_bar,
        find_entry,
        replace_entry,
        panel_rows: RefCell::new(Vec::new()),
        state,
        overlay,
        ghost_fixed,
        gutter_refresh: Cell::new(false),
        close_confirm: RefCell::new(None),
        close_request: RefCell::new(None),
    });

    // Signal wiring that needs the Rc<Ui>.
    {
        let weak = Rc::downgrade(&ui);
        ui.panel_list.connect_row_activated(move |_, row| {
            if let Some(ui) = weak.upgrade() {
                ui.jump_to_diagnostic(row.index() as usize);
            }
        });
    }
    {
        // Switching tabs must re-render that tab's problems and gutter marks,
        // and the keyboard focus must follow the visible editor (AC-2).
        let weak = Rc::downgrade(&ui);
        ui.notebook.connect_switch_page(move |_, _, _| {
            if let Some(ui) = weak.upgrade() {
                ui.refresh_panel();
                ui.refresh_status();
                ui.focus_editor();
                if ui.state.ghost.has_pending() {
                    ui.position_ghost();
                }
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        new_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.new_untitled();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        open_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.open_dialog();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        save_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.save_current();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        save_as_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.save_as_dialog();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        format_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.request_format();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        slang_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.request_slang();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        rename_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.request_rename();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        panel_button.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.toggle_panel();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        find_next.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.find_next_now();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        find_prev.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.set_info("previous match uses the same anchor; F3 continues forward");
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        replace_one.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.replace_next_now();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        replace_all.connect_clicked(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.replace_all_now();
            }
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        let bar = ui.find_bar.clone();
        find_close.connect_clicked(move |_| {
            bar.set_reveal_child(false);
            let _ = weak.upgrade();
        });
    }
    {
        let weak = Rc::downgrade(&ui);
        ui.find_entry.connect_activate(move |_| {
            if let Some(ui) = weak.upgrade() {
                ui.find_next_now();
            }
        });
    }
    install_shortcuts(&ui);

    // Initial tab.
    if let Some(path) = &opts.file {
        match ui.open_path(path) {
            Ok(_) => {}
            Err(error) => {
                eprintln!("vd-app: could not open {}: {error:#}", path.display());
                ui.new_untitled();
            }
        }
    } else {
        ui.new_untitled();
    }

    // Start the LSP worker (the UI shows the tool attempts when unavailable).
    if let Some(bridge) = LspBridge::start(&ui.state.startup) {
        *ui.state.lsp.borrow_mut() = Some(bridge);
        ui.set_info("Verible LSP starting…");
        if let Some(tab) = ui.current_tab() {
            if let Some(uri) = tab.uri() {
                let lsp = ui.state.lsp.borrow();
                if let Some(bridge) = lsp.as_ref() {
                    bridge.open(uri, tab.language_id(), tab.text());
                }
            }
        }
    } else {
        let attempts = ui.state.startup.tools.attempts.len();
        ui.set_info(format!(
            "Verible LSP unavailable ({attempts} resolution attempt(s)); lint/navigation disabled"
        ));
    }
    // AC-B1: the FIM column of `status_text` is the single producer of the
    // reason. Appending it to the message here as well printed it twice (and as
    // "FIM: FIM …", because the reason carries its own tag).

    ui.window.show_all();
    // AC-1: the editor owns the keyboard focus as soon as the window is shown.
    ui.focus_editor();
    ghost_label_hidden(&ui);
    {
        let weak = Rc::downgrade(&ui);
        glib::timeout_add_local(Duration::from_millis(50), move || match weak.upgrade() {
            Some(ui) => {
                ui.poll_background();
                glib::ControlFlow::Continue
            }
            None => glib::ControlFlow::Break,
        });
    }
    {
        let state = Rc::clone(&ui.state);
        ui.window.connect_destroy(move |_| {
            if let Some(bridge) = state.lsp.borrow().as_ref() {
                bridge.shutdown();
            }
        });
    }
    ui.refresh_titles();
    ui.refresh_status();
    Ok(ui)
}

fn ghost_label_hidden(ui: &Rc<Ui>) {
    ui.state.ghost.label.set_visible(false);
}

fn install_shortcuts(ui: &Rc<Ui>) {
    let application = ui.window.application();
    for shortcut in SHORTCUTS {
        if shortcut.id == "accept-fim" || shortcut.id == "cancel-fim" {
            continue; // handled in the view key handler, depends on pending text
        }
        let action = gtk::gio::SimpleAction::new(shortcut.id, None);
        let weak = Rc::downgrade(ui);
        let id = shortcut.id;
        action.connect_activate(move |_, _| {
            let Some(ui) = weak.upgrade() else {
                return;
            };
            match id {
                "open" => ui.open_dialog(),
                "new" => {
                    ui.new_untitled();
                }
                "save" => ui.save_current(),
                "save-as" => ui.save_as_dialog(),
                "close-tab" => {
                    if let Some(index) = ui.current_index() {
                        ui.close_tab_at(index, true);
                    }
                }
                "quit" => {
                    if let Some(application) = ui.window.application() {
                        application.quit();
                    }
                }
                "undo" => ui.undo(),
                "redo" => ui.redo(),
                "find" => ui.show_find(false),
                "replace" => ui.show_find(true),
                "find-next" => ui.find_next_now(),
                "goto-line" => ui.goto_dialog(),
                "format" => ui.request_format(),
                "slang" => ui.request_slang(),
                "definition" => ui.request_definition(),
                "references" => ui.request_references(),
                "outline" => ui.request_outline(),
                "rename" => ui.request_rename(),
                "fim" => ui.request_fim(),
                "toggle-panel" => ui.toggle_panel(),
                other => eprintln!("vd-app: unknown action {other}"),
            }
        });
        ui.window.add_action(&action);
        let binding = config_glue::key_binding(&ui.state.startup.keys, shortcut.id, shortcut.accel);
        let accel = config_glue::gtk_accel(&binding);
        if let Some(application) = application.as_ref() {
            application.set_accels_for_action(&format!("win.{}", shortcut.id), &[accel.as_str()]);
        }
    }
    ui.refresh_status();
}

/// Run the editor interactively.
///
/// # Errors
/// Returns an error when the GTK application cannot be registered.
pub fn run_interactive(opts: &Options) -> Result<()> {
    // `run()` performs registration and activation; calling register/activate
    // here as well made GTK create a second ApplicationWindow (one blank).
    let app = App::new(opts);
    app.run();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::StatusModel;

    /// AC-4 regression (display-free): 200 background-poll refreshes with no
    /// new event must keep the composed status text byte-identical, and the
    /// live LSP state must appear at most once. This is what the 50 ms timer
    /// did NOT do before the fix: it appended the status to the message on
    /// every tick, so the bar grew without bound (issue #2).
    #[test]
    fn repeated_lsp_status_refreshes_keep_the_status_text_bounded() {
        let parts = vec!["1:1".to_string(), "a.sv".to_string()];
        let mut model = StatusModel::default();
        model.set_info("LSP running (pid 7), restarts 0 (running: true, restarts: 0)");
        model.set_lsp("Verible LSP: generation 3".to_string());
        let expected = model.text(&parts);
        assert_eq!(expected.matches("Verible LSP").count(), 1, "{expected}");
        for _ in 0..200 {
            // Exactly what `sync_lsp_status` does on every poll: replace.
            model.set_lsp("Verible LSP: generation 3".to_string());
            let text = model.text(&parts);
            assert_eq!(text, expected, "the status text grew across polls");
            assert_eq!(text.matches("Verible LSP").count(), 1, "{text}");
        }
        // The message is rendered as its own segment; assert that segment by
        // equality (not as a substring) so a rewritten/merged message fails.
        let message = "LSP running (pid 7), restarts 0 (running: true, restarts: 0)";
        let composed = model.text(&parts);
        assert!(
            composed.split("  |  ").any(|part| part == message),
            "polling must not rewrite the message: {composed:?}"
        );
    }

    /// The message and exactly one LSP state are rendered; a message that
    /// already names the LSP is not duplicated.
    #[test]
    fn status_text_renders_the_message_and_at_most_one_lsp_state() {
        let parts = vec!["1:1".to_string()];
        let mut model = StatusModel::default();
        model.set_lsp("Verible LSP: not available".to_string());
        assert_eq!(model.text(&parts), "1:1  |  Verible LSP: not available");

        model.set_info("saved a.sv");
        assert_eq!(
            model.text(&parts),
            "1:1  |  saved a.sv  |  Verible LSP: not available"
        );

        let mut named = StatusModel::default();
        named.set_info("Verible LSP starting…");
        named.set_lsp("Verible LSP: generation 0".to_string());
        assert_eq!(named.text(&parts), "1:1  |  Verible LSP starting…");
    }
}
