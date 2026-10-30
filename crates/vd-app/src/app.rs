//! GTK shell construction and the interactive run loop.
//!
//! The shell owns the window, the `GtkSourceView` editor widget, the status
//! bar and the command registry. Later lanes extend it (panels, gutter marks,
//! command implementations) without rewriting the construction path: they
//! register commands through `commands::CommandRegistry` and add widgets in
//! this module (T21 owns the wiring task).

use std::cell::RefCell;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use anyhow::{bail, Context, Result};
use gtk::prelude::*;
use sourceview4::prelude::*;

use crate::cli::{self, Options};
use crate::commands::{CommandRegistry, CommandSpec};
use crate::document;

/// The shell's widgets plus the per-window state the commands mutate.
pub struct EditorSession {
    pub window: gtk::ApplicationWindow,
    pub view: sourceview4::View,
    pub buffer: sourceview4::Buffer,
    pub status: gtk::Label,
    path: RefCell<Option<PathBuf>>,
}

impl EditorSession {
    /// The file this window is currently bound to, if any.
    #[must_use]
    pub fn current_path(&self) -> Option<PathBuf> {
        self.path.borrow().clone()
    }

    /// Load `path` into the buffer and bind the window to it.
    ///
    /// # Errors
    /// Returns an error when the file cannot be read.
    pub fn load(&self, path: &Path) -> Result<()> {
        document::load_into_buffer(&self.buffer, path)?;
        self.set_path(Some(path.to_path_buf()));
        self.set_status(&format!("Opened {}", path.display()));
        Ok(())
    }

    /// Save the buffer to `path` and bind the window to it.
    ///
    /// # Errors
    /// Returns an error when the file cannot be written.
    pub fn save_to(&self, path: &Path) -> Result<()> {
        document::save_from_buffer(&self.buffer, path)?;
        self.set_path(Some(path.to_path_buf()));
        self.set_status(&format!("Saved {}", path.display()));
        Ok(())
    }

    /// Show a failed command in the status bar and on stderr; a successful
    /// command has already updated the status bar itself.
    pub fn report(&self, result: Result<()>) {
        if let Err(err) = result {
            eprintln!("ERROR: {err:#}");
            self.set_status(&format!("Error: {err:#}"));
        }
    }

    fn set_path(&self, path: Option<PathBuf>) {
        let title = match path.as_deref() {
            Some(p) => format!("{} - {}", p.display(), cli::APP_NAME),
            None => format!("Untitled - {}", cli::APP_NAME),
        };
        self.window.set_title(&title);
        *self.path.borrow_mut() = path;
    }

    fn set_status(&self, text: &str) {
        self.status.set_text(text);
    }
}

/// A built shell: the session and the commands installed on the application.
pub struct Ui {
    pub session: Rc<EditorSession>,
    pub commands: CommandRegistry,
}

/// The application plus the slot the activation handler fills with the [`Ui`].
pub struct Shell {
    app: gtk::Application,
    ui: Rc<RefCell<Option<Ui>>>,
    build_error: Rc<RefCell<Option<String>>>,
}

impl Shell {
    /// Build the application object; the window is created when activate fires.
    #[must_use]
    pub fn new(opts: &Options) -> Self {
        let app = gtk::Application::builder()
            .application_id(cli::APP_ID)
            .build();
        let ui = Rc::new(RefCell::new(None));
        let build_error = Rc::new(RefCell::new(None));

        {
            let ui = Rc::clone(&ui);
            let build_error = Rc::clone(&build_error);
            let opts = opts.clone();
            app.connect_activate(move |app| match build_ui(app, &opts) {
                Ok(built) => {
                    built.session.window.show_all();
                    *ui.borrow_mut() = Some(built);
                }
                Err(err) => {
                    *build_error.borrow_mut() = Some(format!("{err:#}"));
                }
            });
        }

        Self {
            app,
            ui,
            build_error,
        }
    }

    /// The underlying `GtkApplication`.
    #[must_use]
    pub fn app(&self) -> &gtk::Application {
        &self.app
    }

    /// Shared slot that holds the built [`Ui`] once activation ran.
    #[must_use]
    pub fn ui_slot(&self) -> Rc<RefCell<Option<Ui>>> {
        Rc::clone(&self.ui)
    }

    /// Register the application and fire its activation.
    ///
    /// # Errors
    /// Returns an error when GTK cannot register (for example no `DISPLAY`),
    /// or when the activation handler could not build the window.
    pub fn register_and_activate(&self) -> Result<()> {
        self.app
            .register(None::<&gtk::gio::Cancellable>)
            .context("registering the GTK application (is DISPLAY set?)")?;
        self.app.activate();
        if let Some(err) = self.build_error.borrow_mut().take() {
            bail!("building the main window failed: {err}");
        }
        Ok(())
    }
}

/// Run the editor interactively (blocks until the window is closed).
///
/// # Errors
/// Returns an error when the window cannot be created or GTK cannot start.
pub fn run_interactive(opts: &Options) -> Result<()> {
    let shell = Shell::new(opts);
    shell.register_and_activate()?;
    if shell.ui_slot().borrow().is_none() {
        bail!("GTK activation did not create the main window");
    }
    gtk::main();
    Ok(())
}

fn toolbar_button(app: &gtk::Application, label: &str, action_id: &str) -> gtk::Button {
    let button = gtk::Button::with_label(label);
    let app = app.clone();
    let action_id = action_id.to_string();
    button.connect_clicked(move |_| {
        // gtk-rs 0.18's activate_action returns (), not a success flag.
        app.activate_action(&action_id, None);
    });
    button
}

fn choose_file(
    window: &gtk::ApplicationWindow,
    title: &str,
    action: gtk::FileChooserAction,
) -> Option<PathBuf> {
    let accept_label = match action {
        gtk::FileChooserAction::Save => "Save",
        _ => "Open",
    };
    let buttons: [(&str, gtk::ResponseType); 2] = [
        ("Cancel", gtk::ResponseType::Cancel),
        (accept_label, gtk::ResponseType::Accept),
    ];
    let dialog = gtk::FileChooserDialog::new(Some(title), Some(window), action);
    dialog.add_buttons(&buttons);
    let response = dialog.run();
    let path = if response == gtk::ResponseType::Accept {
        dialog.filename()
    } else {
        None
    };
    dialog.close();
    path
}

fn register_default_commands(
    registry: &mut CommandRegistry,
    session: &Rc<EditorSession>,
) -> Result<()> {
    {
        let session = Rc::clone(session);
        registry.register(
            CommandSpec {
                id: "open",
                label: "Open…",
                accels: &["<Primary>o"],
            },
            move |_app, window| {
                if let Some(path) = choose_file(window, "Open File", gtk::FileChooserAction::Open) {
                    session.report(session.load(&path));
                }
            },
        )?;
    }
    {
        let session = Rc::clone(session);
        registry.register(
            CommandSpec {
                id: "save",
                label: "Save",
                accels: &["<Primary>s"],
            },
            move |_app, window| {
                let result = match session.current_path() {
                    Some(path) => session.save_to(&path),
                    None => match choose_file(window, "Save File", gtk::FileChooserAction::Save) {
                        Some(path) => session.save_to(&path),
                        None => Ok(()),
                    },
                };
                session.report(result);
            },
        )?;
    }
    registry.register(
        CommandSpec {
            id: "quit",
            label: "Quit",
            accels: &["<Primary>q"],
        },
        |_app, window| window.close(),
    )?;
    Ok(())
}

fn build_ui(app: &gtk::Application, opts: &Options) -> Result<Ui> {
    let buffer = sourceview4::Buffer::new(None::<&gtk::TextTagTable>);
    let view = sourceview4::View::with_buffer(&buffer);
    view.set_show_line_numbers(true);
    view.set_monospace(true);
    view.set_highlight_current_line(true);

    let window = gtk::ApplicationWindow::new(app);
    window.set_default_size(1000, 720);

    let status = gtk::Label::new(Some("Ready"));
    status.set_xalign(0.0);

    let session = Rc::new(EditorSession {
        window: window.clone(),
        view: view.clone(),
        buffer: buffer.clone(),
        status: status.clone(),
        path: RefCell::new(None),
    });
    session.set_path(None);

    let mut commands = CommandRegistry::new();
    register_default_commands(&mut commands, &session)?;
    commands.install(app, &window);

    let toolbar = gtk::Box::new(gtk::Orientation::Horizontal, 6);
    toolbar.pack_start(&toolbar_button(app, "Open", "open"), false, false, 0);
    toolbar.pack_start(&toolbar_button(app, "Save", "save"), false, false, 0);

    let scroller = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroller.add(&view);

    let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
    root.pack_start(&toolbar, false, false, 0);
    root.pack_start(&scroller, true, true, 0);
    root.pack_start(&status, false, false, 0);
    window.add(&root);

    window.connect_destroy(|_| gtk::main_quit());

    if let Some(path) = &opts.file {
        session.report(session.load(path));
    }

    Ok(Ui { session, commands })
}
