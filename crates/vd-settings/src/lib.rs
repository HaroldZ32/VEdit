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
//! # Status (Slice 0)
//!
//! This is the FFI seam's settings backend: `vd-ffi` exposes it through the C
//! ABI; the settings panel that edits themes and key bindings belongs to the
//! settings-ui task.
#![forbid(unsafe_code)]

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
