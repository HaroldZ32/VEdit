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
    /// The project root exists but its directory entries cannot be read.
    UnreadableRoot(PathBuf),
}

impl fmt::Display for IndexError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingRoot(root) => {
                write!(f, "project root is not a directory: {}", root.display())
            }
            Self::UnreadableRoot(root) => {
                write!(f, "project root is not readable: {}", root.display())
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

/// Largest file a tree scan reads (matches the workspace search defaults).
pub const DEFAULT_TREE_MAX_FILE_BYTES: u64 = 8 * 1024 * 1024;
/// Wall-clock budget of one tree scan.
pub const DEFAULT_TREE_MAX_DURATION_MS: u64 = 15_000;
/// Maximum number of hits returned by one tree scan.
pub const DEFAULT_TREE_MAX_RESULTS: usize = 2000;
/// Maximum preview length (characters) of one tree hit.
pub const TREE_SNIPPET_CHARS: usize = 400;

/// Directories pruned by a tree scan unless the caller says otherwise.
pub const DEFAULT_EXCLUDED_DIRS: &[&str] = &[
    ".git",
    ".svn",
    ".hg",
    "build",
    "target",
    "node_modules",
    ".cache",
    ".idea",
    ".vscode",
    "CMakeFiles",
    ".ccache",
    ".venv",
];

/// Options of one tree scan (the Rust search index's public knobs).
#[derive(Debug, Clone)]
pub struct TreeOptions {
    /// Match case-sensitively (default: insensitive, like the UI search box).
    pub case_sensitive: bool,
    /// Restrict to Verilog / SystemVerilog file extensions.
    pub verilog_only: bool,
    /// Directory basenames pruned from the walk (default: [`DEFAULT_EXCLUDED_DIRS`]).
    pub excluded_dir_names: Vec<String>,
    /// Stop after this many hits (`truncated` is set).
    pub max_results: usize,
    /// Files larger than this are skipped as too large (never read).
    pub max_file_bytes: u64,
    /// Wall-clock budget; when exhausted the scan stops with `timed_out`.
    pub max_duration_ms: u64,
}

impl Default for TreeOptions {
    fn default() -> Self {
        Self {
            case_sensitive: false,
            verilog_only: false,
            excluded_dir_names: DEFAULT_EXCLUDED_DIRS
                .iter()
                .map(|name| (*name).to_string())
                .collect(),
            max_results: DEFAULT_TREE_MAX_RESULTS,
            max_file_bytes: DEFAULT_TREE_MAX_FILE_BYTES,
            max_duration_ms: DEFAULT_TREE_MAX_DURATION_MS,
        }
    }
}

/// What one tree scan did, including every skip (observable, not hidden).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TreeSummary {
    /// Files whose content was read (binary/oversized/unreadable excluded).
    pub files_scanned: u32,
    /// Files that produced at least one hit.
    pub files_matched: u32,
    /// Hits recorded (also the length of [`TreeOutcome::hits`]).
    pub hit_count: u32,
    /// Files skipped because their content contains a NUL byte.
    pub skipped_binary: u32,
    /// Files skipped because they exceed `max_file_bytes`.
    pub skipped_too_large: u32,
    /// Files and directories whose content/listing could not be read.
    pub skipped_unreadable: u32,
    /// `max_results` was reached.
    pub truncated: bool,
    /// `max_duration_ms` was reached.
    pub timed_out: bool,
    /// Wall-clock duration of the scan.
    pub elapsed_ms: u64,
}

/// One hit of a tree scan.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TreeHit {
    /// Path relative to the scanned root (what the UI displays).
    pub relative_path: PathBuf,
    /// 1-based line number.
    pub line: u32,
    /// 1-based character column of the first match on the line.
    pub column: u32,
    /// The matched line, trimmed and truncated to [`TREE_SNIPPET_CHARS`].
    pub text: String,
}

/// The hits plus the observable summary of one tree scan.
#[derive(Debug, Clone)]
pub struct TreeOutcome {
    /// Hits in deterministic (file name, line) order.
    pub hits: Vec<TreeHit>,
    /// Counters, limits and elapsed time.
    pub summary: TreeSummary,
}

struct TreeScanner<'a> {
    root: &'a Path,
    needle_lower: String,
    options: &'a TreeOptions,
    started: std::time::Instant,
    hits: Vec<TreeHit>,
    summary: TreeSummary,
}

impl TreeScanner<'_> {
    fn stopped(&mut self) -> bool {
        if self.summary.truncated || self.summary.timed_out {
            return true;
        }
        if self.started.elapsed().as_millis() as u64 >= self.options.max_duration_ms {
            self.summary.timed_out = true;
            return true;
        }
        false
    }

    fn walk(&mut self, directory: &Path, depth: usize) {
        if depth > 64 || self.stopped() {
            return;
        }
        let Ok(entries) = std::fs::read_dir(directory) else {
            self.summary.skipped_unreadable += 1;
            return;
        };
        let mut entries: Vec<_> = entries.filter_map(Result::ok).collect();
        entries.sort_by_key(|entry| entry.file_name().to_string_lossy().to_lowercase());
        for entry in entries {
            if self.stopped() {
                return;
            }
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if name.starts_with('.') {
                continue;
            }
            let Ok(file_type) = entry.file_type() else {
                self.summary.skipped_unreadable += 1;
                continue;
            };
            if file_type.is_symlink() {
                continue;
            }
            if file_type.is_dir() {
                if self
                    .options
                    .excluded_dir_names
                    .iter()
                    .any(|excluded| excluded == name.as_ref())
                {
                    continue;
                }
                self.walk(&entry.path(), depth + 1);
                continue;
            }
            if file_type.is_file() {
                self.scan_file(&entry.path());
            }
        }
    }

    fn scan_file(&mut self, path: &Path) {
        if self.stopped() {
            return;
        }
        if self.options.verilog_only && !is_verilog_path(path) {
            return;
        }
        let Ok(metadata) = std::fs::metadata(path) else {
            self.summary.skipped_unreadable += 1;
            return;
        };
        if metadata.len() > self.options.max_file_bytes {
            self.summary.skipped_too_large += 1;
            return;
        }
        let Ok(bytes) = std::fs::read(path) else {
            self.summary.skipped_unreadable += 1;
            return;
        };
        if bytes.contains(&0) {
            self.summary.skipped_binary += 1;
            return;
        }
        self.summary.files_scanned += 1;
        let text = String::from_utf8_lossy(&bytes);
        let relative = path.strip_prefix(self.root).unwrap_or(path).to_path_buf();
        let mut matched_this_file = false;
        for (index, raw) in text.split('\n').enumerate() {
            if index % 256 == 0 && self.stopped() {
                return;
            }
            if self.summary.truncated {
                return;
            }
            let raw = raw.strip_suffix('\r').unwrap_or(raw);
            if raw.is_empty() {
                continue;
            }
            let lowered;
            let (found, searched) = if self.options.case_sensitive {
                (raw.find(self.needle_lower.as_str()), raw)
            } else {
                lowered = raw.to_lowercase();
                (lowered.find(self.needle_lower.as_str()), lowered.as_str())
            };
            let Some(byte_offset) = found else {
                continue;
            };
            if self.summary.hit_count as usize >= self.options.max_results {
                self.summary.truncated = true;
                return;
            }
            let first_char = searched[..byte_offset].chars().count();
            let mut snippet: String = raw.trim().chars().take(TREE_SNIPPET_CHARS).collect();
            if raw.trim().chars().count() > TREE_SNIPPET_CHARS {
                snippet.push('…');
            }
            self.hits.push(TreeHit {
                relative_path: relative.clone(),
                line: u32::try_from(index + 1).unwrap_or(u32::MAX),
                column: u32::try_from(first_char + 1).unwrap_or(u32::MAX),
                text: snippet,
            });
            self.summary.hit_count += 1;
            matched_this_file = true;
        }
        if matched_this_file {
            self.summary.files_matched += 1;
        }
    }
}

fn is_verilog_path(path: &Path) -> bool {
    let Some(extension) = path.extension().and_then(|ext| ext.to_str()) else {
        return false;
    };
    matches!(
        extension.to_ascii_lowercase().as_str(),
        "v" | "vh" | "vlg" | "sv" | "svh"
    )
}

/// Walk `root` on the calling thread and return every hit plus the summary.
///
/// The walk is deterministic (entries sorted case-insensitively), never follows
/// symlinks, prunes [`DEFAULT_EXCLUDED_DIRS`] (or `excluded_dir_names`), skips
/// binary / oversized / unreadable files with a counter, stops at
/// `max_results` (truncated) or `max_duration_ms` (timed out), and matches the
/// same-origin literal query case-insensitively unless `case_sensitive` is set.
///
/// This is the Rust search index: Qt must consume this function through the
/// C ABI and must not implement a second walk or matcher.
pub fn search_tree(
    root: &Path,
    needle: &str,
    options: &TreeOptions,
) -> Result<TreeOutcome, IndexError> {
    let started = std::time::Instant::now();
    if !root.is_dir() {
        return Err(IndexError::MissingRoot(root.to_path_buf()));
    }
    if std::fs::read_dir(root).is_err() {
        return Err(IndexError::UnreadableRoot(root.to_path_buf()));
    }
    let mut scanner = TreeScanner {
        root,
        needle_lower: if options.case_sensitive {
            needle.to_string()
        } else {
            needle.to_lowercase()
        },
        options,
        started,
        hits: Vec::new(),
        summary: TreeSummary::default(),
    };
    if !needle.is_empty() {
        scanner.walk(root, 0);
    }
    scanner.summary.elapsed_ms = u64::try_from(started.elapsed().as_millis()).unwrap_or(u64::MAX);
    Ok(TreeOutcome {
        hits: scanner.hits,
        summary: scanner.summary,
    })
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
    fn tree_matches_case_insensitively_and_reports_summary() {
        let Some(dir) = tempfile::tempdir().ok() else {
            return;
        };
        write(
            dir.path(),
            "a.sv",
            "module Alpha;\n  ASSIGN x = 1;\nendmodule\n",
        );
        write(dir.path(), "sub/b.sv", "module beta;\nendmodule\n");
        write(dir.path(), "skip.txt", "assign hidden by verilog_only\n");
        let options = TreeOptions {
            verilog_only: true,
            ..TreeOptions::default()
        };
        let Ok(outcome) = search_tree(dir.path(), "assign", &options) else {
            return;
        };
        assert_eq!(outcome.hits.len(), 1);
        assert_eq!(outcome.hits[0].relative_path, PathBuf::from("a.sv"));
        assert_eq!(outcome.hits[0].line, 2);
        assert_eq!(outcome.summary.files_scanned, 2);
        assert_eq!(outcome.summary.files_matched, 1);
        assert_eq!(outcome.summary.hit_count, 1);
    }

    #[test]
    fn tree_skips_binary_oversized_and_excluded_dirs() {
        let Some(dir) = tempfile::tempdir().ok() else {
            return;
        };
        write(dir.path(), "a.sv", "needle\n");
        let binary = dir.path().join("b.bin");
        assert!(std::fs::write(&binary, [b'n', 0, b'x']).is_ok());
        let large = dir.path().join("big.v");
        assert!(std::fs::write(&large, vec![b'x'; 64]).is_ok());
        write(dir.path(), "build/ignored.sv", "needle\n");
        let options = TreeOptions {
            max_file_bytes: 32,
            ..TreeOptions::default()
        };
        let Ok(outcome) = search_tree(dir.path(), "needle", &options) else {
            return;
        };
        assert_eq!(outcome.hits.len(), 1);
        assert_eq!(outcome.summary.skipped_binary, 1);
        assert_eq!(outcome.summary.skipped_too_large, 1);
    }

    #[test]
    fn tree_honours_the_result_limit_and_timeout() {
        let Some(dir) = tempfile::tempdir().ok() else {
            return;
        };
        write(dir.path(), "a.sv", "needle\nneedle\nneedle\n");
        let options = TreeOptions {
            max_results: 1,
            ..TreeOptions::default()
        };
        let Ok(outcome) = search_tree(dir.path(), "needle", &options) else {
            return;
        };
        assert_eq!(outcome.hits.len(), 1);
        assert!(outcome.summary.truncated);

        let timeout = TreeOptions {
            max_duration_ms: 0,
            ..TreeOptions::default()
        };
        let Ok(outcome) = search_tree(dir.path(), "needle", &timeout) else {
            return;
        };
        assert!(outcome.summary.timed_out);
        assert!(outcome.hits.is_empty());
    }

    #[test]
    fn tree_missing_root_is_reported() {
        let result = search_tree(
            Path::new("/definitely/not/a/directory"),
            "x",
            &TreeOptions::default(),
        );
        assert!(matches!(result, Err(IndexError::MissingRoot(_))));
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
