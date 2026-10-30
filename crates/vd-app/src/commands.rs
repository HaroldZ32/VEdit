//! Command dispatcher for the shell.
//!
//! Later lanes register their commands (and the shortcuts that trigger them)
//! through [`CommandRegistry`] instead of editing `app.rs`. The registry is
//! installed on the `GtkApplication` as `gio::SimpleAction`s named `app.<id>`,
//! carrying the declared accelerators. `app.rs` builds the shell's own
//! open/save/quit commands through the same API, so the seam is exercised from
//! day one.

use std::collections::HashMap;
use std::rc::Rc;

use anyhow::{bail, Result};
use gtk::prelude::*;

/// One command: a stable id, a human-readable label and its accelerators.
#[derive(Debug, Clone, Copy)]
pub struct CommandSpec {
    /// Stable action id, installed as `app.<id>`.
    pub id: &'static str,
    /// Label for menus/help.
    pub label: &'static str,
    /// Accelerators in GTK syntax (e.g. `"<Primary>o"`).
    pub accels: &'static [&'static str],
}

/// A command body: receives the application and the main window.
type Handler = Rc<dyn Fn(&gtk::Application, &gtk::ApplicationWindow)>;

/// Registry of shell commands, filled before [`CommandRegistry::install`].
#[derive(Default)]
pub struct CommandRegistry {
    specs: Vec<CommandSpec>,
    handlers: HashMap<&'static str, Handler>,
}

impl CommandRegistry {
    /// An empty registry.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Register one command.
    ///
    /// # Errors
    /// Returns an error when a command with the same id is already registered.
    pub fn register(
        &mut self,
        spec: CommandSpec,
        handler: impl Fn(&gtk::Application, &gtk::ApplicationWindow) + 'static,
    ) -> Result<()> {
        if self.handlers.contains_key(spec.id) {
            bail!("command '{}' is already registered", spec.id);
        }
        self.specs.push(spec);
        self.handlers.insert(spec.id, Rc::new(handler));
        Ok(())
    }

    /// The registered commands, in registration order (used by `--selftest`).
    #[must_use]
    pub fn specs(&self) -> &[CommandSpec] {
        &self.specs
    }

    /// Install every registered command on the application as `app.<id>`
    /// actions with their accelerators.
    pub fn install(&self, app: &gtk::Application, window: &gtk::ApplicationWindow) {
        for spec in &self.specs {
            let Some(handler) = self.handlers.get(spec.id) else {
                // register() fills both collections together; this is unreachable.
                continue;
            };
            let action = gtk::gio::SimpleAction::new(spec.id, None);
            let handler = Rc::clone(handler);
            let app_for_action = app.clone();
            let window_for_action = window.clone();
            action.connect_activate(move |_, _| {
                handler(&app_for_action, &window_for_action);
            });
            app.add_action(&action);
            app.set_accels_for_action(&format!("app.{}", spec.id), spec.accels);
        }
    }
}
