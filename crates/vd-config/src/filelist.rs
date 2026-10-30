//! A `.f` filelist parser covering the verible-relevant subset.
//!
//! Supported: one path per line, `//` and `#` comments, `+incdir+DIR`,
//! `+define+NAME[=VALUE]` and nested `-f FILE`. Unknown `+…`/`-…` tokens are
//! reported as warnings and skipped — a filelist written for a commercial
//! simulator must not stop the editor from opening the project.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use crate::project::{parse_define, Define, Filelist};
use crate::warnings::{Warning, WarningCode};

/// Maximum nesting depth for `-f` recursion.
pub(crate) const MAX_DEPTH: u32 = 16;

/// Parse one top-level filelist. Returns `None` when it cannot be read at all.
pub(crate) fn parse(path: &Path, warnings: &mut Vec<Warning>) -> Option<Filelist> {
    if !path.is_file() {
        warnings.push(
            Warning::new(WarningCode::FilelistMissing, "filelist does not exist")
                .with_file(path.to_path_buf()),
        );
        return None;
    }
    let mut parser = Parser::new(warnings);
    parser.visit(path, 0);
    let (entries, include_dirs, defines) = parser.finish();
    Some(Filelist {
        path: path.to_path_buf(),
        entries,
        include_dirs,
        defines,
    })
}

struct Parser<'a> {
    entries: Vec<PathBuf>,
    entry_seen: BTreeSet<PathBuf>,
    include_dirs: Vec<PathBuf>,
    include_seen: BTreeSet<PathBuf>,
    defines: Vec<Define>,
    define_seen: BTreeSet<Define>,
    visiting: Vec<PathBuf>,
    warnings: &'a mut Vec<Warning>,
}

impl<'a> Parser<'a> {
    fn new(warnings: &'a mut Vec<Warning>) -> Self {
        Self {
            entries: Vec::new(),
            entry_seen: BTreeSet::new(),
            include_dirs: Vec::new(),
            include_seen: BTreeSet::new(),
            defines: Vec::new(),
            define_seen: BTreeSet::new(),
            visiting: Vec::new(),
            warnings,
        }
    }

    fn finish(self) -> (Vec<PathBuf>, Vec<PathBuf>, Vec<Define>) {
        (self.entries, self.include_dirs, self.defines)
    }

    fn visit(&mut self, path: &Path, depth: u32) {
        if depth > MAX_DEPTH {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistDepthExceeded,
                    format!("nested -f recursion deeper than {MAX_DEPTH}; stopped"),
                )
                .with_file(path.to_path_buf()),
            );
            return;
        }
        let canonical = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        if self.visiting.contains(&canonical) {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistCycle,
                    "nested -f points back into a filelist already being parsed; skipped",
                )
                .with_file(path.to_path_buf()),
            );
            return;
        }
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(_) => {
                self.warnings.push(
                    Warning::new(
                        WarningCode::FilelistUnreadable,
                        "filelist could not be read; skipped",
                    )
                    .with_file(path.to_path_buf()),
                );
                return;
            }
        };

        let base = path.parent().unwrap_or_else(|| Path::new("."));
        self.visiting.push(canonical);
        for (index, raw_line) in text.lines().enumerate() {
            self.consume_line(base, raw_line, index as u32 + 1, depth);
        }
        self.visiting.pop();
    }

    fn consume_line(&mut self, base: &Path, raw_line: &str, line: u32, depth: u32) {
        let line_text = strip_comment(raw_line).trim();
        if line_text.is_empty() {
            return;
        }
        if let Some(rest) = line_text.strip_prefix("+incdir+") {
            self.consume_incdirs(base, rest);
            return;
        }
        if let Some(rest) = line_text.strip_prefix("+define+") {
            self.consume_defines(rest, base, line);
            return;
        }
        if let Some(nested) = filelist_argument(line_text) {
            self.consume_nested(base, nested, line, depth);
            return;
        }
        if line_text.starts_with('+') || line_text.starts_with('-') {
            let token = line_text.split_whitespace().next().unwrap_or(line_text);
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistUnknownToken,
                    format!("unsupported filelist token `{token}`; skipped"),
                )
                .with_file(base)
                .with_line(line),
            );
            return;
        }
        let resolved = resolve(base, line_text);
        if self.entry_seen.insert(resolved.clone()) {
            self.entries.push(resolved);
        } else {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistDuplicateEntry,
                    "duplicate source path in filelist; kept the first occurrence",
                )
                .with_file(base)
                .with_line(line),
            );
        }
    }

    fn consume_incdirs(&mut self, base: &Path, rest: &str) {
        for dir in rest.split('+').filter(|part| !part.trim().is_empty()) {
            let resolved = resolve(base, dir.trim());
            if self.include_seen.insert(resolved.clone()) {
                self.include_dirs.push(resolved);
            }
        }
    }

    fn consume_defines(&mut self, rest: &str, base: &Path, line: u32) {
        for token in rest.split('+').filter(|part| !part.trim().is_empty()) {
            match parse_define(token) {
                Some(define) => {
                    if self.define_seen.insert(define.clone()) {
                        self.defines.push(define);
                    }
                }
                None => self.warnings.push(
                    Warning::new(
                        WarningCode::FilelistDefineInvalid,
                        "malformed +define+ token; skipped",
                    )
                    .with_file(base)
                    .with_line(line),
                ),
            }
        }
    }

    fn consume_nested(&mut self, base: &Path, nested: &str, line: u32, depth: u32) {
        let nested_path = resolve(base, nested);
        if !nested_path.is_file() {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistMissing,
                    format!("nested -f target `{nested}` does not exist; skipped"),
                )
                .with_file(base)
                .with_line(line),
            );
            return;
        }
        self.visit(&nested_path, depth + 1);
    }
}

/// The argument of a `-f FILE` / `-F FILE` line, if this line is one.
fn filelist_argument(line: &str) -> Option<&str> {
    let rest = line
        .strip_prefix("-f")
        .or_else(|| line.strip_prefix("-F"))?;
    let trimmed = rest.trim_start();
    if trimmed.len() == rest.len() {
        // No whitespace after the flag: `-ffoo` is not a form we understand.
        return None;
    }
    let argument = trimmed.trim();
    (!argument.is_empty()).then_some(argument)
}

/// Drop `//` comments anywhere and `#` comments at line start or after space.
fn strip_comment(line: &str) -> &str {
    let mut text = line;
    if let Some(position) = text.find("//") {
        text = &text[..position];
    }
    if let Some(position) = text.find('#') {
        let before = &text[..position];
        let at_boundary = before
            .chars()
            .last()
            .is_none_or(|last| last.is_whitespace());
        if at_boundary {
            text = before;
        }
    }
    text
}

fn resolve(base: &Path, path: &str) -> PathBuf {
    let candidate = Path::new(path);
    if candidate.is_absolute() {
        candidate.to_path_buf()
    } else {
        base.join(candidate)
    }
}

#[cfg(test)]
mod tests {
    use super::{parse, MAX_DEPTH};
    use crate::test_support::{require, write};
    use crate::warnings::WarningCode;

    #[test]
    fn parses_entries_comments_incdirs_defines_and_nested_filelists(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("sub.f"), "nested.sv\n")?;
        write(
            &root.join("top.f"),
            "// a comment\n\
             rtl/a.sv   # trailing comment\n\
             +incdir+inc+inc2\n\
             +define+FOO=1+BAR\n\
             -f sub.f\n\
             +libext+.v+.sv\n",
        )?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("top.f"), &mut warnings))?;
        assert_eq!(
            filelist.entries,
            vec![root.join("rtl/a.sv"), root.join("nested.sv")]
        );
        assert_eq!(
            filelist.include_dirs,
            vec![root.join("inc"), root.join("inc2")]
        );
        assert_eq!(filelist.defines.len(), 2);
        assert_eq!(filelist.defines[0].name, "FOO");
        assert_eq!(filelist.defines[0].value.as_deref(), Some("1"));
        assert_eq!(filelist.defines[1].name, "BAR");
        assert!(warnings
            .iter()
            .any(|warning| warning.code == WarningCode::FilelistUnknownToken));
        Ok(())
    }

    #[test]
    fn detects_cycles_and_duplicates() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("a.f"), "-f b.f\nsame.sv\nsame.sv\n")?;
        write(&root.join("b.f"), "-f a.f\n")?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("a.f"), &mut warnings))?;
        assert_eq!(filelist.entries, vec![root.join("same.sv")]);
        assert!(warnings
            .iter()
            .any(|warning| warning.code == WarningCode::FilelistCycle));
        assert!(warnings
            .iter()
            .any(|warning| warning.code == WarningCode::FilelistDuplicateEntry));
        Ok(())
    }

    #[test]
    fn depth_limit_is_enforced() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        for index in 0..=(MAX_DEPTH + 1) {
            write(
                &root.join(format!("f{index}.f")),
                &format!("-f f{}.f\n", index + 1),
            )?;
        }
        let mut warnings = Vec::new();
        let _ = parse(&root.join("f0.f"), &mut warnings);
        assert!(warnings
            .iter()
            .any(|warning| warning.code == WarningCode::FilelistDepthExceeded));
        Ok(())
    }

    #[test]
    fn missing_nested_filelist_is_reported_not_fatal() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "-f missing.f\nok.sv\n")?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("top.f"), &mut warnings))?;
        assert_eq!(filelist.entries, vec![root.join("ok.sv")]);
        assert!(warnings
            .iter()
            .any(|warning| warning.code == WarningCode::FilelistMissing));
        Ok(())
    }
}
