//! FIM ghost text: the service is `vd-fim`'s; this module only owns the pending
//! completion and the label. Ghost text is NEVER written to the buffer — the
//! buffer gets the text only when the user accepts with Tab, through `Document`.

use std::cell::{Cell, RefCell};

use gtk::prelude::*;
use vd_fim::{FimError, FimService, FimSettings, Generation};

/// Ghost-text state for the window.
pub struct Ghost {
    service: FimService,
    generation: RefCell<Option<Generation>>,
    pending: RefCell<Option<String>>,
    accepting: Cell<bool>,
    pub label: gtk::Label,
}

impl Ghost {
    /// Build the controller. A default configuration is disabled and never
    /// starts a worker or a request.
    #[must_use]
    pub fn new(settings: FimSettings) -> Self {
        let service = FimService::from_settings(settings);
        let label = gtk::Label::new(None);
        label.set_xalign(0.0);
        label.set_opacity(0.55);
        label.set_visible(false);
        Self {
            service,
            generation: RefCell::new(None),
            pending: RefCell::new(None),
            accepting: Cell::new(false),
            label,
        }
    }

    #[must_use]
    pub fn enabled(&self) -> bool {
        self.service.enabled()
    }

    /// Why FIM cannot produce a completion; safe to show in the status bar.
    #[must_use]
    pub fn reason(&self) -> Option<String> {
        self.service.unavailable_reason().map(str::to_string)
    }

    /// Ask for a completion for the text before/after the cursor.
    ///
    /// # Errors
    /// Returns the short configuration reason when FIM is unavailable.
    pub fn request(&self, prefix: &str, suffix: &str) -> Result<(), String> {
        if let Some(reason) = self.service.unavailable_reason() {
            return Err(reason.to_string());
        }
        let generation = self
            .service
            .start(prefix, suffix)
            .ok_or_else(|| "FIM is already generating".to_string())?;
        *self.generation.borrow_mut() = Some(generation);
        Ok(())
    }

    /// Poll the worker; returns a completed completion or an error reason.
    pub fn poll(&self) -> Option<Result<String, FimError>> {
        self.service.poll()
    }

    pub fn cancel(&self) {
        self.service.cancel();
        *self.generation.borrow_mut() = None;
        self.hide();
    }

    /// Show a completed completion as ghost text.
    pub fn show_pending(&self, text: String) {
        self.label.set_text(&text);
        self.label.set_visible(true);
        *self.pending.borrow_mut() = Some(text);
    }

    #[must_use]
    pub fn has_pending(&self) -> bool {
        self.pending.borrow().is_some()
    }

    #[must_use]
    pub fn pending_text(&self) -> Option<String> {
        self.pending.borrow().clone()
    }

    pub fn hide(&self) {
        self.label.set_visible(false);
        *self.pending.borrow_mut() = None;
    }

    pub fn set_accepting(&self, accepting: bool) {
        self.accepting.set(accepting);
    }

    #[must_use]
    pub fn is_accepting(&self) -> bool {
        self.accepting.get()
    }
}
