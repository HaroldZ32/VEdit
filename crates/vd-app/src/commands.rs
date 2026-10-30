//! The default shortcut table (AC-03) and the window actions it drives.
//!
//! Keeping the table in one place lets the selftests and the report cite exactly
//! the bindings that are installed, instead of describing them by hand.

/// One command: stable id, label, GTK accelerator and a short description.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Shortcut {
    pub id: &'static str,
    pub label: &'static str,
    pub accel: &'static str,
    pub description: &'static str,
}

/// Every default binding. `Tab`/`Esc` are handled in the view's key handler (they
/// depend on pending ghost text) and listed here for the table/report.
pub const SHORTCUTS: &[Shortcut] = &[
    Shortcut {
        id: "open",
        label: "Open…",
        accel: "<Ctrl>o",
        description: "open a file in a new tab",
    },
    Shortcut {
        id: "new",
        label: "New",
        accel: "<Ctrl>n",
        description: "new untitled tab",
    },
    Shortcut {
        id: "save",
        label: "Save",
        accel: "<Ctrl>s",
        description: "save the current tab",
    },
    Shortcut {
        id: "save-as",
        label: "Save As…",
        accel: "<Ctrl><Shift>s",
        description: "save the current tab under a new path",
    },
    Shortcut {
        id: "close-tab",
        label: "Close Tab",
        accel: "<Ctrl>w",
        description: "close the tab (confirms unsaved changes)",
    },
    Shortcut {
        id: "quit",
        label: "Quit",
        accel: "<Ctrl>q",
        description: "quit the editor",
    },
    Shortcut {
        id: "undo",
        label: "Undo",
        accel: "<Ctrl>z",
        description: "undo the last edit (native GtkSource undo)",
    },
    Shortcut {
        id: "redo",
        label: "Redo",
        accel: "<Ctrl><Shift>z",
        description: "redo",
    },
    Shortcut {
        id: "find",
        label: "Find",
        accel: "<Ctrl>f",
        description: "show the find bar",
    },
    Shortcut {
        id: "replace",
        label: "Replace",
        accel: "<Ctrl>h",
        description: "show the replace bar",
    },
    Shortcut {
        id: "find-next",
        label: "Find Next",
        accel: "F3",
        description: "jump to the next match",
    },
    Shortcut {
        id: "goto-line",
        label: "Go to Line…",
        accel: "<Ctrl>g",
        description: "jump to a line",
    },
    Shortcut {
        id: "format",
        label: "Format File",
        accel: "<Ctrl><Shift>f",
        description: "one-key format through the LSP, one undo step",
    },
    Shortcut {
        id: "slang",
        label: "SLANG Deep Lint",
        accel: "<Ctrl><Shift>l",
        description: "run SLANG deep lint into the problem panel",
    },
    Shortcut {
        id: "definition",
        label: "Go to Definition",
        accel: "F12",
        description: "jump to the definition (LSP)",
    },
    Shortcut {
        id: "references",
        label: "Find References",
        accel: "<Shift>F12",
        description: "list references (LSP)",
    },
    Shortcut {
        id: "outline",
        label: "Outline",
        accel: "<Ctrl><Shift>o",
        description: "document symbols of the current file",
    },
    Shortcut {
        id: "rename",
        label: "Rename Preview…",
        accel: "F2",
        description: "preview the LSP rename edits (nothing is applied)",
    },
    Shortcut {
        id: "fim",
        label: "FIM Completion",
        accel: "<Alt>backslash",
        description: "request an FIM completion as ghost text",
    },
    Shortcut {
        id: "toggle-panel",
        label: "Toggle Problem Panel",
        accel: "<Ctrl><Shift>p",
        description: "show/hide the problem panel",
    },
    Shortcut {
        id: "accept-fim",
        label: "Accept Ghost Text",
        accel: "Tab",
        description: "insert the pending FIM completion through Document",
    },
    Shortcut {
        id: "cancel-fim",
        label: "Cancel Ghost Text",
        accel: "Esc",
        description: "discard the pending completion / cancel the request",
    },
];

/// Look up a shortcut by id.
#[must_use]
pub fn shortcut(id: &str) -> Option<&'static Shortcut> {
    SHORTCUTS.iter().find(|shortcut| shortcut.id == id)
}

/// Markdown-ish table for the report/evidence.
#[must_use]
pub fn table_text() -> String {
    let mut text = String::from("| action | shortcut | description |\n|---|---|---|\n");
    for shortcut in SHORTCUTS {
        text.push_str(&format!(
            "| `{}` | {} | {} |\n",
            shortcut.id, shortcut.accel, shortcut.description
        ));
    }
    text
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn contract_shortcuts_are_all_present_exactly_once() {
        let contract = [
            ("format", "<Ctrl><Shift>f"),
            ("slang", "<Ctrl><Shift>l"),
            ("definition", "F12"),
            ("references", "<Shift>F12"),
            ("outline", "<Ctrl><Shift>o"),
            ("fim", "<Alt>backslash"),
            ("accept-fim", "Tab"),
            ("cancel-fim", "Esc"),
        ];
        for (id, accel) in contract {
            let shortcut = shortcut(id).unwrap_or_else(|| panic!("missing shortcut {id}"));
            assert_eq!(shortcut.accel, accel, "wrong accelerator for {id}");
        }
        let mut ids: Vec<&str> = SHORTCUTS.iter().map(|shortcut| shortcut.id).collect();
        ids.sort_unstable();
        let unique = ids.len();
        ids.dedup();
        assert_eq!(ids.len(), unique, "duplicate shortcut id");
    }

    #[test]
    fn table_text_lists_every_shortcut() {
        let text = table_text();
        for shortcut in SHORTCUTS {
            assert!(text.contains(shortcut.accel), "{} missing", shortcut.accel);
        }
    }
}
