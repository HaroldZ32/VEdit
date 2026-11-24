//! Settings storage service for VEdit (Slice 3).
//!
//! The store is deliberately dumb: one flat TOML table of `key = "value"` pairs,
//! loaded into an ordered list and saved atomically. It does not know what a
//! theme, a key binding or a font size is — those semantics belong to the
//! settings UI, and the storage layer must not grow a second configuration
//! model next to `vd-config`.
//!
//! # Bad configuration degrades, it never blocks startup
//!
//! [`load`] turns every failure into a warning plus an empty (or partial) value
//! set: a missing file is simply "no user overrides yet", an unreadable file, a
//! TOML syntax error and a non-string value each produce a [`Loaded::warnings`]
//! entry that the settings UI shows. That is the AC-S3.4 fallback behaviour.
//!
//! # Atomic save
//!
//! [`save`] writes `<file>.tmp` next to the target, calls `sync_all`, then
//! renames it into place, so a crash mid-write leaves either the old file or the
//! new one — never a half-written file. The temporary file is removed when the
//! rename fails, so a failed save leaves no debris next to the user's settings.
//!
//! # Typed keys live in [`schema`]
//!
//! The store does not know what a key means; [`schema`] does. It declares the
//! keys the Preferences dialog writes (`tools.*`, `fim.*`), projects them onto
//! typed values the services consume, and owns the FIM API-key rules (never a
//! store value; environment first, then an owner-only `0600` user file).
//!
//! # Status (Slice 0)
//!
//! This is the FFI seam's settings backend: `vd-ffi` exposes it through the C
//! ABI; the settings panel that edits themes and key bindings belongs to the
//! settings-ui task.
#![forbid(unsafe_code)]

pub mod schema;

use std::fmt;
use std::io::Write;
use std::path::{Path, PathBuf};

/// One stored setting.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    /// Flat key, e.g. `theme` or `keymap.split.horizontal`.
    pub key: String,
    /// String value as stored.
    pub value: String,
}

/// The result of reading the settings file.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Loaded {
    /// Values in TOML key order (the table is key-sorted).
    pub entries: Vec<Entry>,
    /// Non-fatal problems, already phrased for the UI.
    pub warnings: Vec<String>,
}

/// Why a save failed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SaveError {
    /// The parent directory could not be created.
    CreateDir(String),
    /// Writing or syncing the temporary file failed.
    Write(String),
    /// The rename into place failed.
    Rename(String),
}

impl fmt::Display for SaveError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::CreateDir(message) => write!(f, "cannot create settings directory: {message}"),
            Self::Write(message) => write!(f, "cannot write settings: {message}"),
            Self::Rename(message) => write!(f, "cannot replace settings atomically: {message}"),
        }
    }
}

impl std::error::Error for SaveError {}

/// Read the settings file. Never fails: a broken file degrades to warnings.
#[must_use]
pub fn load(path: &Path) -> Loaded {
    let mut loaded = Loaded::default();
    if !path.is_file() {
        return loaded;
    }
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(error) => {
            loaded.warnings.push(format!(
                "cannot read {}: {error}; using defaults",
                path.display()
            ));
            return loaded;
        }
    };
    let document: toml::Table = match toml::from_str(&text) {
        Ok(document) => document,
        Err(error) => {
            loaded.warnings.push(format!(
                "invalid TOML in {}: {error}; using defaults",
                path.display()
            ));
            return loaded;
        }
    };
    for (key, value) in document {
        match value {
            toml::Value::String(text) => loaded.entries.push(Entry { key, value: text }),
            toml::Value::Table(nested) => {
                // A bare dotted key (`tools.lsp_backend = "slang"`) is valid TOML
                // that parses as a NESTED table, while the store is flat. Name
                // the key the user meant, so a hand-edited file says how to fix
                // itself instead of silently meaning defaults.
                let flat: Vec<String> = nested
                    .keys()
                    .map(|name| format!("\"{key}.{name}\""))
                    .collect();
                loaded.warnings.push(format!(
                    "{}: `{key}` is a table but the store holds flat keys; write {} = \"…\" (one quoted key) instead",
                    path.display(),
                    flat.join(" / ")
                ));
            }
            other => loaded.warnings.push(format!(
                "{}: `{key}` is {} but the store holds strings; ignored",
                path.display(),
                other.type_str()
            )),
        }
    }
    loaded
}

/// Write the settings file atomically (temporary file + rename).
pub fn save(path: &Path, entries: &[Entry]) -> Result<(), SaveError> {
    let mut table = toml::Table::new();
    for entry in entries {
        table.insert(entry.key.clone(), toml::Value::String(entry.value.clone()));
    }
    let body = toml::to_string(&table)
        .map_err(|error| SaveError::Write(format!("serialising settings: {error}")))?;

    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)
                .map_err(|error| SaveError::CreateDir(format!("{}: {error}", parent.display())))?;
        }
    }
    let temporary = temporary_path(path);
    let write = (|| -> std::io::Result<()> {
        let mut file = std::fs::File::create(&temporary)?;
        file.write_all(body.as_bytes())?;
        file.sync_all()
    })();
    if let Err(error) = write {
        let _ = std::fs::remove_file(&temporary);
        return Err(SaveError::Write(format!(
            "{}: {error}",
            temporary.display()
        )));
    }
    if let Err(error) = std::fs::rename(&temporary, path) {
        let _ = std::fs::remove_file(&temporary);
        return Err(SaveError::Rename(format!("{}: {error}", path.display())));
    }
    Ok(())
}

fn temporary_path(path: &Path) -> PathBuf {
    let mut name = path
        .file_name()
        .map(|name| name.to_os_string())
        .unwrap_or_default();
    name.push(".tmp");
    match path.parent() {
        Some(parent) => parent.join(name),
        None => PathBuf::from(name),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(key: &str, value: &str) -> Entry {
        Entry {
            key: key.to_string(),
            value: value.to_string(),
        }
    }

    #[test]
    fn round_trip_preserves_every_pair() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        let entries = vec![entry("theme", "Breeze Dark"), entry("font.size", "13")];
        assert_eq!(save(&path, &entries), Ok(()));
        let loaded = load(&path);
        assert!(
            loaded.warnings.is_empty(),
            "unexpected warnings: {:?}",
            loaded.warnings
        );
        // The store is a TOML table, so the read side is key-sorted; compare as
        // a set of pairs rather than as a sequence.
        let mut observed: Vec<(String, String)> = loaded
            .entries
            .iter()
            .map(|entry| (entry.key.clone(), entry.value.clone()))
            .collect();
        observed.sort();
        let mut expected: Vec<(String, String)> = entries
            .iter()
            .map(|entry| (entry.key.clone(), entry.value.clone()))
            .collect();
        expected.sort();
        assert_eq!(observed, expected);
    }

    #[test]
    fn missing_file_is_the_default_state() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let loaded = load(&dir.path().join("does-not-exist.toml"));
        assert!(loaded.entries.is_empty());
        assert!(loaded.warnings.is_empty());
    }

    #[test]
    fn broken_toml_degrades_to_a_warning() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        let written = std::fs::write(&path, "theme = \n[unclosed\n");
        assert!(written.is_ok());
        let loaded = load(&path);
        assert!(loaded.entries.is_empty());
        assert_eq!(loaded.warnings.len(), 1);
        assert!(loaded.warnings[0].contains("invalid TOML"));
    }

    #[test]
    fn non_string_values_are_reported_and_skipped() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        let written = std::fs::write(&path, "theme = \"Breeze\"\nfont_size = 13\n");
        assert!(written.is_ok());
        let loaded = load(&path);
        assert_eq!(loaded.entries, vec![entry("theme", "Breeze")]);
        assert_eq!(loaded.warnings.len(), 1);
        assert!(loaded.warnings[0].contains("font_size"));
    }

    #[test]
    fn a_bare_dotted_key_names_the_flat_key_to_write() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        // The exact shape a hand-edited store produces: a dotted key outside the
        // quotes. It is valid TOML, so parsing alone cannot reject it.
        let written = std::fs::write(&path, "tools.lsp_backend = \"slang\"\n");
        assert!(written.is_ok());
        let loaded = load(&path);
        assert!(
            loaded.entries.is_empty(),
            "a nested table is not a flat entry: {:?}",
            loaded.entries
        );
        assert_eq!(loaded.warnings.len(), 1, "{:?}", loaded.warnings);
        let warning = &loaded.warnings[0];
        assert!(
            warning.contains("\"tools.lsp_backend\""),
            "the warning names the key the user meant: {warning}"
        );
        assert!(
            warning.contains("quoted"),
            "the warning says how to fix the file: {warning}"
        );
    }

    #[test]
    fn a_refused_tool_value_is_explained_and_never_dropped() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        let written = std::fs::write(&path, "\"tools.lsp_backend\" = \"emacs\"\n");
        assert!(written.is_ok());
        // The loader keeps the value it read; refusing/canonicalising is the
        // schema step the ABI seam runs, and it must hand back a warning.
        let mut loaded = load(&path);
        assert_eq!(loaded.entries, vec![entry("tools.lsp_backend", "emacs")]);
        let warnings = schema::normalize_tool_entries(&mut loaded.entries);
        assert_eq!(warnings.len(), 1, "{warnings:?}");
        assert!(
            warnings[0].contains("emacs"),
            "names the refused value: {}",
            warnings[0]
        );
        assert!(
            warnings[0].contains("slang-server"),
            "lists the accepted spellings: {}",
            warnings[0]
        );
        assert_eq!(loaded.entries, vec![entry("tools.lsp_backend", "verible")]);
    }

    #[test]
    fn a_canonical_pair_survives_a_fresh_load() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        let entries = vec![
            entry("tools.lsp_backend", "slang"),
            entry("tools.lint_engine", "verible"),
        ];
        assert_eq!(save(&path, &entries), Ok(()));
        // A restart is a NEW load of the same store: both reads must agree with
        // what was written, so the pair does not depend on process state.
        let first = load(&path);
        let second = load(&path);
        // The table is key-sorted on the read side; compare as a set of pairs.
        let mut observed: Vec<(String, String)> = first
            .entries
            .iter()
            .map(|entry| (entry.key.clone(), entry.value.clone()))
            .collect();
        observed.sort();
        let mut expected: Vec<(String, String)> = entries
            .iter()
            .map(|entry| (entry.key.clone(), entry.value.clone()))
            .collect();
        expected.sort();
        assert_eq!(observed, expected);
        assert_eq!(second.entries, first.entries);
        assert!(first.warnings.is_empty(), "{:?}", first.warnings);
    }

    #[test]
    fn save_replaces_atomically_and_leaves_no_temporary_file() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("nested/settings.toml");
        assert_eq!(save(&path, &[entry("theme", "one")]), Ok(()));
        assert_eq!(save(&path, &[entry("theme", "two")]), Ok(()));
        assert_eq!(load(&path).entries, vec![entry("theme", "two")]);
        let mut leftover = Vec::new();
        if let Ok(read_dir) = std::fs::read_dir(path.parent().unwrap_or(dir.path())) {
            for item in read_dir.flatten() {
                leftover.push(item.file_name());
            }
        }
        assert_eq!(
            leftover.len(),
            1,
            "temporary file left behind: {leftover:?}"
        );
    }

    #[test]
    fn failed_rename_leaves_the_original_file_intact() {
        let Ok(dir) = tempfile::tempdir() else {
            return;
        };
        let path = dir.path().join("settings.toml");
        assert_eq!(save(&path, &[entry("theme", "first")]), Ok(()));
        let directory_target = dir.path().join("settings.toml-dir");
        let created = std::fs::create_dir(&directory_target);
        assert!(created.is_ok());
        let blocked = save(&directory_target, &[entry("theme", "second")]);
        assert!(matches!(blocked, Err(SaveError::Rename(_))));
        assert_eq!(load(&path).entries, vec![entry("theme", "first")]);
        assert!(!dir.path().join("settings.toml-dir.tmp").exists());
    }
}
