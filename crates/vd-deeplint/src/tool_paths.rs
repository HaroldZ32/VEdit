//! Tool-reported paths and columns, made usable by the editor.
//!
//! `slang` reports **both** of its outputs relative to the working directory of
//! its own process:
//!
//! * `--ast-json` writes `source_file` as a path relative to the child's cwd —
//!   verified with absolute file arguments: from `cwd=/tmp` an absolute
//!   `/fx/top.sv` comes back as `../fx/top.sv`, and from `cwd=/p/rtl` a sibling
//!   `/p/tb/b.sv` comes back as `../tb/b.sv`;
//! * `--diag-json` writes `file` the same way (`../vedit-build/…/warn.sv` for an
//!   absolute argument from `cwd=/tmp`).
//!
//! Every consumer of this crate needs an ABSOLUTE path. Resolving a relative
//! path against the wrong base silently loses the subdirectory: with the
//! project's sources in `rtl/` and the folder root as the base, the module
//! catalog produced `<root>/child.sv` for the real `<root>/rtl/child.sv`, so the
//! editor could not open it and EVERY jump was refused (RM-4).
//!
//! So the rule is one rule and it lives here: **resolve a reported path against
//! the directory the tool actually ran in**. Resolution is LEXICAL (`..` / `.`
//! collapsed) and never canonicalises — the path keeps the user's spelling, and
//! a path that does not exist still resolves (the consumer decides what to do).
//!
//! Columns are the second half: `slang`'s `source_column` is a **UTF-8 byte**
//! column (`--diag-column-unit byte` is explicit for diagnostics; the AST JSON
//! behaves the same). The editor's cursor unit is the UTF-16 code unit, so
//! [`Columns`] converts a reported byte column to that unit exactly once, for
//! every consumer of the catalog. The boundary rules are documented on
//! [`Columns::char_column`].

use serde_json::Value;
use std::collections::HashMap;
use std::path::{Component, Path, PathBuf};

/// The AST-JSON keys that carry a position's file. `symbol_position` reads
/// `source_file`; expressions use the `_start` / `_end` flavours.
const AST_FILE_KEYS: [&str; 3] = ["source_file", "source_file_start", "source_file_end"];

/// Resolve one reported path against the directory the tool ran in.
///
/// An absolute path is returned lexically normalised; a relative one is joined
/// with `base`. Nothing is read from disk, so this is safe for a path that no
/// longer exists (the consumer reports that).
#[must_use]
pub(crate) fn absolute_path(file: &Path, base: &Path) -> PathBuf {
    if file.as_os_str().is_empty() {
        return PathBuf::new();
    }
    if file.is_absolute() || base.as_os_str().is_empty() {
        return lexical(file);
    }
    lexical(&base.join(file))
}

/// [`absolute_path`] for one `&str` from a diagnostics document.
#[must_use]
pub(crate) fn absolute_path_str(file: &str, base: &Path) -> String {
    if file.is_empty() {
        return String::new();
    }
    absolute_path(Path::new(file), base)
        .to_string_lossy()
        .into_owned()
}

/// Rewrite every file field of a parsed AST-JSON document in place.
///
/// Called ONCE at the parse boundary of each consumer, so no consumer has to
/// remember the tool's cwd rule.
pub(crate) fn absolutize_ast(value: &mut Value, base: &Path) {
    match value {
        Value::Object(map) => {
            for key in AST_FILE_KEYS {
                if let Some(Value::String(file)) = map.get_mut(key) {
                    if !file.is_empty() && !Path::new(file.as_str()).is_absolute() {
                        *file = absolute_path_str(file, base);
                    }
                }
            }
            for child in map.values_mut() {
                absolutize_ast(child, base);
            }
        }
        Value::Array(items) => {
            for item in items {
                absolutize_ast(item, base);
            }
        }
        _ => {}
    }
}

/// Lexical normalisation: drop `.`, collapse `..` against a normal component.
/// Kept lexical on purpose — canonicalising would resolve symlinks and rewrite
/// the spelling the user and the tools agreed on.
fn lexical(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                // `..` after a normal component pops it; after a root/prefix it
                // stays (the path is then clamped at `/` by the OS).
                let popped = matches!(out.components().next_back(), Some(Component::Normal(_)));
                if popped {
                    out.pop();
                } else if !out.has_root() {
                    out.push(Component::ParentDir.as_os_str());
                }
            }
            other => out.push(other.as_os_str()),
        }
    }
    out
}

/// Byte-column → editor-column conversion, cached per file.
///
/// The editor position unit is the **UTF-16 code unit** (KTextEditor cursors),
/// which is what the diagnostics lane's `SourceLines::position` already
/// produces; this type applies the same rule to the catalog's positions.
#[derive(Default)]
pub(crate) struct Columns {
    files: HashMap<PathBuf, Option<Vec<Vec<u8>>>>,
    cached_bytes: usize,
}

impl Columns {
    /// Cap on cached source bytes; the cache is dropped as a whole when it is
    /// crossed (positions are grouped per file in practice, so the hit rate
    /// stays high and the memory is bounded).
    const MAX_CACHED_BYTES: usize = 32 * 1024 * 1024;

    #[must_use]
    pub(crate) fn new() -> Self {
        Self::default()
    }

    /// Convert a 0-based line and 0-based UTF-8 **byte** column to a 0-based
    /// UTF-16 code-unit column.
    ///
    /// Boundary rules (every one of them is a documented answer, never a panic):
    ///
    /// * **unreadable / not UTF-8 / line out of range** — the byte column is
    ///   returned unchanged; the consumer's own clamp is the fallback and the
    ///   conversion never invents a position.
    /// * **an offset past the end of the line** — clamped to the line's last
    ///   character position (one past its last character); a reported column is
    ///   never allowed to point outside the text.
    /// * **an offset inside a multi-byte sequence** — rounded DOWN to the
    ///   character that contains it: a symbol cannot start mid-character, and
    ///   rounding down keeps the reported identifier reachable.
    /// * **empty line** — column 0 maps to 0.
    pub(crate) fn char_column(&mut self, file: &Path, line0: u32, byte_column0: u32) -> u32 {
        let Some(lines) = self.lines(file) else {
            return byte_column0;
        };
        let Some(line) = lines.get(line0 as usize) else {
            return byte_column0;
        };
        let offset = (byte_column0 as usize).min(line.len());
        // Round down to a character boundary (see the rule above).
        let mut boundary = offset;
        while boundary < line.len() && (line[boundary] & 0xC0) == 0x80 {
            boundary -= 1;
        }
        // The whole file was validated as UTF-8, so every line is valid too.
        let Ok(text) = std::str::from_utf8(&line[..boundary]) else {
            return byte_column0;
        };
        u32::try_from(text.encode_utf16().count()).unwrap_or(byte_column0)
    }

    fn lines(&mut self, file: &Path) -> Option<&Vec<Vec<u8>>> {
        if !self.files.contains_key(file) {
            let loaded = std::fs::read(file)
                .ok()
                .filter(|bytes| std::str::from_utf8(bytes).is_ok())
                .map(|bytes| split_lines(&bytes));
            let added = loaded
                .as_ref()
                .map_or(0, |lines| lines.iter().map(Vec::len).sum::<usize>());
            if self.cached_bytes + added > Self::MAX_CACHED_BYTES {
                self.files.clear();
                self.cached_bytes = 0;
            }
            self.cached_bytes += added;
            self.files.insert(file.to_path_buf(), loaded);
        }
        self.files.get(file).and_then(Option::as_ref)
    }
}

/// Split file bytes into lines without their terminators (a trailing `\r` is
/// stripped), exactly like the diagnostics lane's `SourceLines`.
fn split_lines(bytes: &[u8]) -> Vec<Vec<u8>> {
    bytes
        .split(|byte| *byte == b'\n')
        .map(|line| match line.split_last() {
            Some((b'\r', rest)) => rest.to_vec(),
            _ => line.to_vec(),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_paths_resolve_against_the_tool_directory() {
        assert_eq!(
            absolute_path_str("child.sv", Path::new("/root/rtl")),
            "/root/rtl/child.sv"
        );
        assert_eq!(
            absolute_path_str("../tb/b.sv", Path::new("/root/rtl")),
            "/root/tb/b.sv"
        );
        assert_eq!(
            absolute_path_str("/abs/x.sv", Path::new("/root/rtl")),
            "/abs/x.sv"
        );
        assert_eq!(absolute_path_str("", Path::new("/root")), "");
    }

    #[test]
    fn multi_directory_file_sets_resolve_each_file_correctly() {
        // The child ran in `rtl/`; files elsewhere come back with `..`, and each
        // must resolve to its own directory (the base is arbitrary, the
        // resolution is exact).
        let base = Path::new("/proj/rtl");
        assert_eq!(absolute_path_str("child.sv", base), "/proj/rtl/child.sv");
        assert_eq!(absolute_path_str("../tb/tb.sv", base), "/proj/tb/tb.sv");
        assert_eq!(
            absolute_path_str("../include/defs.svh", base),
            "/proj/include/defs.svh"
        );
    }

    fn columns_for(text: &str) -> (std::path::PathBuf, Columns) {
        let dir = std::env::temp_dir().join(format!(
            "vd-tool-paths-{}-{}",
            std::process::id(),
            text.len()
        ));
        std::fs::create_dir_all(&dir).ok();
        let file = dir.join("f.sv");
        std::fs::write(&file, text).ok();
        let columns = Columns::new();
        (file, columns)
    }

    #[test]
    fn byte_columns_convert_to_utf16_units() {
        // `/* 中文 */ clk`: `clk` starts at character 9 and at byte 13
        // (`/* ` = 3 bytes, `中文` = 6, ` */ ` = 4).
        let (file, mut columns) = columns_for("/* 中文 */ clk\n");
        assert_eq!(columns.char_column(&file, 0, 13), 9);
        assert_eq!(columns.char_column(&file, 0, 0), 0);
    }

    #[test]
    fn boundary_rules_are_total() {
        // `a中文b`: characters a=0, 中=1..4, 文=4..7, b=7
        let (file, mut columns) = columns_for("a中文b\n");
        // past the end of the line -> clamped to one past the last character
        assert_eq!(columns.char_column(&file, 0, 999), 4);
        // inside `中` (byte 3): rounded down to the character that contains it
        assert_eq!(columns.char_column(&file, 0, 3), 1);
        // a line that does not exist keeps the reported column
        assert_eq!(columns.char_column(&file, 9, 7), 7);
        // an unreadable file keeps the reported column
        assert_eq!(
            columns.char_column(Path::new("/nonexistent-vd/x.sv"), 0, 42),
            42
        );
        // an empty line maps to column 0
        let (empty, mut columns2) = columns_for("\n");
        assert_eq!(columns2.char_column(&empty, 0, 0), 0);
    }
}
