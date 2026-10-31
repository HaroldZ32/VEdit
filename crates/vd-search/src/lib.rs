//! Search index service for VEdit (Slice 2).
//!
//! The index is a **file set plus a query**, not a second project model: the
//! caller passes the project's expanded file list (the same list SLANG and
//! Verible are fed, C4/AC-S1.3), and this crate answers substring queries with
//! `path:line:column` hits. There is exactly one implementation; the Qt side
//! consumes results through `vd-ffi` and never walks the tree itself.
//!
//! # Determinism
//!
//! Files are queried in the order they were given, lines in ascending order, and
//! the per-query `limit` truncates from the tail. The same inputs therefore
//! produce the same hit list, which is what makes the flat editor result list
//! testable without a filesystem race.
//!
//! # Bounds
//!
//! A file larger than [`MAX_FILE_BYTES`] and a file that is not valid UTF-8 are
//! skipped instead of being partially read: a binary blob in a Verilog tree must
//! not produce mojibake hits, and a query must not read a 2 GiB log file.
//!
//! # Status (Slice 0)
//!
//! This crate is the FFI seam's search backend: `vd-ffi` exposes it through the
//! C ABI, and the 10k-file latency budget (AC-S2.1) belongs to the search-palette
//! task, which owns the index strategy. What exists here is the contract that
//! task extends, not its final performance work.
#![forbid(unsafe_code)]

use std::fmt;
use std::path::{Path, PathBuf};

/// Files larger than this are not searched.
pub const MAX_FILE_BYTES: u64 = 512 * 1024;
/// Longest preview returned for one hit, in characters.
pub const MAX_PREVIEW_CHARS: usize = 160;
/// Hits returned when the caller does not ask for a limit.
pub const DEFAULT_LIMIT: usize = 200;

/// One query hit: a file, a 1-based line and 1-based column, and a preview.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Hit {
    /// Absolute or project-relative path, exactly as the index was opened with.
    pub path: PathBuf,
    /// 1-based line number.
    pub line: u32,
    /// 1-based byte column of the first match on that line.
    pub column: u32,
    /// The matched line, trimmed and truncated to [`MAX_PREVIEW_CHARS`].
    pub preview: String,
}

/// Why an index could not be opened.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IndexError {
    /// The project root does not exist or is not a directory.
    MissingRoot(PathBuf),
}

impl fmt::Display for IndexError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingRoot(root) => {
                write!(f, "project root is not a directory: {}", root.display())
            }
        }
    }
}

impl std::error::Error for IndexError {}

/// The searchable file set of one project.
#[derive(Debug, Clone)]
pub struct SearchIndex {
    root: PathBuf,
    files: Vec<PathBuf>,
}

impl SearchIndex {
    /// Open an index over `files`, all interpreted under `root`.
    pub fn open(
        root: impl Into<PathBuf>,
        files: impl IntoIterator<Item = PathBuf>,
    ) -> Result<Self, IndexError> {
        let root = root.into();
        if !root.is_dir() {
            return Err(IndexError::MissingRoot(root));
        }
        Ok(Self {
            root,
            files: files.into_iter().collect(),
        })
    }

    /// The project root this index was opened with.
    #[must_use]
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// The file set, in query order.
    #[must_use]
    pub fn files(&self) -> &[PathBuf] {
        &self.files
    }

    /// Substring query over the file set.
    ///
    /// An empty `needle` returns no hits (callers must not turn a cleared query
    /// field into a full-tree scan). Unreadable, oversized and non-UTF-8 files
    /// are skipped.
    #[must_use]
    pub fn query(&self, needle: &str, limit: usize) -> Vec<Hit> {
        if needle.is_empty() || limit == 0 {
            return Vec::new();
        }
        let mut hits = Vec::new();
        for file in &self.files {
            if hits.len() >= limit {
                break;
            }
            let path = self.resolve(file);
            let Ok(metadata) = std::fs::metadata(&path) else {
                continue;
            };
            if !metadata.is_file() || metadata.len() > MAX_FILE_BYTES {
                continue;
            }
            let Ok(text) = std::fs::read_to_string(&path) else {
                continue;
            };
            for (index, line) in text.lines().enumerate() {
                let Some(offset) = line.find(needle) else {
                    continue;
                };
                hits.push(Hit {
                    path: file.clone(),
                    line: index as u32 + 1,
                    column: offset as u32 + 1,
                    preview: preview(line),
                });
                if hits.len() >= limit {
                    break;
                }
            }
        }
        hits
    }

    fn resolve(&self, file: &Path) -> PathBuf {
        if file.is_absolute() {
            file.to_path_buf()
        } else {
            self.root.join(file)
        }
    }
}

fn preview(line: &str) -> String {
    let trimmed = line.trim();
    if trimmed.chars().count() <= MAX_PREVIEW_CHARS {
        return trimmed.to_string();
    }
    let mut out: String = trimmed.chars().take(MAX_PREVIEW_CHARS).collect();
    out.push('…');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(root: &Path, name: &str, content: &str) {
        let path = root.join(name);
        if let Some(parent) = path.parent() {
            let created = std::fs::create_dir_all(parent);
            assert!(
                created.is_ok(),
                "create_dir_all({}) failed",
                parent.display()
            );
        }
        let written = std::fs::write(&path, content);
        assert!(written.is_ok(), "write({}) failed", path.display());
    }

    fn temp_project() -> Option<(tempfile::TempDir, SearchIndex)> {
        let dir = tempfile::tempdir().ok()?;
        write(
            dir.path(),
            "a.sv",
            "module a;\n  assign x = 1;\nendmodule\n",
        );
        write(
            dir.path(),
            "sub/b.sv",
            "module b;\n  assign x = 2;\nendmodule\n",
        );
        let files = vec![PathBuf::from("a.sv"), PathBuf::from("sub/b.sv")];
        let index = SearchIndex::open(dir.path().to_path_buf(), files).ok()?;
        Some((dir, index))
    }

    #[test]
    fn query_finds_every_line_in_file_order() {
        let Some((_dir, index)) = temp_project() else {
            return;
        };
        let hits = index.query("assign x", DEFAULT_LIMIT);
        assert_eq!(hits.len(), 2);
        assert_eq!(hits[0].path, PathBuf::from("a.sv"));
        assert_eq!(hits[0].line, 2);
        assert_eq!(hits[0].column, 3);
        assert_eq!(hits[1].path, PathBuf::from("sub/b.sv"));
        assert_eq!(hits[1].line, 2);
    }

    #[test]
    fn empty_needle_and_zero_limit_return_nothing() {
        let Some((_dir, index)) = temp_project() else {
            return;
        };
        assert!(index.query("", DEFAULT_LIMIT).is_empty());
        assert!(index.query("module", 0).is_empty());
    }

    #[test]
    fn limit_truncates_from_the_tail() {
        let Some((_dir, index)) = temp_project() else {
            return;
        };
        let hits = index.query("module", 1);
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].path, PathBuf::from("a.sv"));
    }

    #[test]
    fn non_utf8_and_missing_files_are_skipped() {
        let Some((dir, index)) = temp_project() else {
            return;
        };
        let binary = dir.path().join("c.bin");
        let written = std::fs::write(&binary, [0xff_u8, 0xfe, 0x00, 0x01]);
        assert!(written.is_ok());
        let mut files = index.files().to_vec();
        files.push(PathBuf::from("c.bin"));
        files.push(PathBuf::from("does-not-exist.sv"));
        let widened = SearchIndex::open(dir.path().to_path_buf(), files);
        assert!(widened.is_ok(), "opening the widened index failed");
        let Ok(widened) = widened else { return };
        // "module" would also match "endmodule" (two hits per file); this needle
        // appears exactly once per fixture file, so the count is the assertion.
        let hits = widened.query("assign x", DEFAULT_LIMIT);
        assert_eq!(hits.len(), 2);
    }

    #[test]
    fn missing_root_is_reported() {
        let result = SearchIndex::open("/definitely/not/a/directory", Vec::new());
        assert_eq!(
            result.err(),
            Some(IndexError::MissingRoot(PathBuf::from(
                "/definitely/not/a/directory"
            )))
        );
    }

    #[test]
    fn preview_is_trimmed_and_capped() {
        let long = format!("  {}  ", "x".repeat(MAX_PREVIEW_CHARS + 10));
        let capped = preview(&long);
        assert!(capped.ends_with('…'));
        assert_eq!(capped.chars().count(), MAX_PREVIEW_CHARS + 1);
        assert_eq!(preview("  module a;  "), "module a;");
    }
}
