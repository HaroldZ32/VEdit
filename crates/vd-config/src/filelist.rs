//! A `.f` filelist preprocessor + parser covering the verible-relevant subset.
//!
//! Supported: one path per line, `//` and `#` comments, `+incdir+DIR`,
//! `+define+NAME[=VALUE]`, nested `-f FILE` (relative to the containing
//! filelist, cycle- and depth-guarded) and variable references (ADDENDUM A-9):
//! `$VAR`, `${VAR}`, `$(VAR)`, `$$` as a literal `$`, and a leading `~` for the
//! home directory. A `$` that starts none of those forms is a **literal `$`**
//! (ADDENDUM A-17, the shell rule), so `out_dollar$.sv` and `out_tail.sv$` are
//! legal paths instead of hard errors that drop the entry; `$$` stays the
//! explicit escape. Variables resolve **project table first, environment
//! second**. An entry that only came into being through expansion (or through a
//! leading `~` that could not expand) and does not exist is reported with its
//! `file:line` and the path actually looked up — it is kept, never lost
//! silently. Unknown `+…`/`-…` tokens are reported and skipped — a filelist
//! written for a commercial simulator must not stop the editor from opening the
//! project — but nothing is ever dropped silently: every dropped line produces
//! a `file:line` diagnostic, and unresolved variable names are collected so the
//! UI can ask for all of them in ONE dialog.
//!
//! Every line-scoped diagnostic names the `.f` file the line came from and its
//! 1-based line number, so a bad entry is actionable in the UI (a warning that
//! pointed at the containing directory could not be told apart from its
//! siblings).

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use crate::project::{parse_define, Define, Filelist, Variables};
use crate::warnings::{Warning, WarningCode};

/// Maximum nesting depth for `-f` recursion.
pub(crate) const MAX_DEPTH: u32 = 16;

/// Parse one top-level filelist using only the process environment for
/// variables. Returns `None` when it cannot be read at all.
///
/// Kept for the parser's own tests; production paths go through
/// [`parse_with`] with the project's variable table.
#[cfg(test)]
pub(crate) fn parse(path: &Path, warnings: &mut Vec<Warning>) -> Option<Filelist> {
    let variables = Variables::from_pairs(&[]);
    let mut missing = BTreeSet::new();
    let mut skipped = 0usize;
    parse_with(path, warnings, &variables, &mut missing, &mut skipped)
}

/// Parse one top-level filelist with an explicit variable table (ADDENDUM A-9).
///
/// `missing` collects every unresolved variable name (unique, sorted); `skipped`
/// counts the lines that produced no entry because of a missing variable, an
/// unusable variable reference, an unknown token, a malformed define or a
/// missing/cyclic nested filelist. The caller reports both so "the file set
/// silently lost files" cannot happen.
pub(crate) fn parse_with(
    path: &Path,
    warnings: &mut Vec<Warning>,
    variables: &Variables,
    missing: &mut BTreeSet<String>,
    skipped: &mut usize,
) -> Option<Filelist> {
    if !path.is_file() {
        warnings.push(
            Warning::new(WarningCode::FilelistMissing, "filelist does not exist")
                .with_file(path.to_path_buf()),
        );
        return None;
    }
    let mut parser = Parser::new(warnings, variables, missing, skipped);
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
    /// Active nesting stack: (canonical path, path as it was named).
    visiting: Vec<(PathBuf, PathBuf)>,
    variables: &'a Variables,
    missing: &'a mut BTreeSet<String>,
    skipped: &'a mut usize,
    warnings: &'a mut Vec<Warning>,
}

impl<'a> Parser<'a> {
    fn new(
        warnings: &'a mut Vec<Warning>,
        variables: &'a Variables,
        missing: &'a mut BTreeSet<String>,
        skipped: &'a mut usize,
    ) -> Self {
        Self {
            entries: Vec::new(),
            entry_seen: BTreeSet::new(),
            include_dirs: Vec::new(),
            include_seen: BTreeSet::new(),
            defines: Vec::new(),
            define_seen: BTreeSet::new(),
            visiting: Vec::new(),
            variables,
            missing,
            skipped,
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
                    format!(
                        "nested -f recursion deeper than {MAX_DEPTH}; skipped `{}`",
                        path.display()
                    ),
                )
                .with_file(path.to_path_buf()),
            );
            *self.skipped += 1;
            return;
        }
        let canonical = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        // consume_nested() reports the cycle with the offending line; this is
        // the unreachable safety net for a direct call.
        if self.visiting.iter().any(|(known, _)| known == &canonical) {
            self.warnings.push(
                Warning::new(WarningCode::FilelistCycle, "nested -f cycle; skipped")
                    .with_file(path.to_path_buf()),
            );
            *self.skipped += 1;
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
                *self.skipped += 1;
                return;
            }
        };

        let base = path.parent().unwrap_or_else(|| Path::new("."));
        self.visiting.push((canonical, path.to_path_buf()));
        let file = path.to_path_buf();
        for (index, raw_line) in text.lines().enumerate() {
            self.consume_line(&file, base, raw_line, index as u32 + 1, depth);
        }
        self.visiting.pop();
    }

    /// `file` is the filelist the line came from (the file a diagnostic points
    /// at); `base` is its directory, which relative paths resolve against.
    fn consume_line(&mut self, file: &Path, base: &Path, raw_line: &str, line: u32, depth: u32) {
        let line_text = strip_comment(raw_line).trim();
        if line_text.is_empty() {
            return;
        }
        // ADDENDUM A-9: expand `$VAR` / `${VAR}` / `$(VAR)` / `~` BEFORE any
        // token is interpreted. An unresolved variable drops the whole line (a
        // literal `$VAR` path would silently point nowhere) and every name is
        // collected for the one-dialog collection.
        let expanded = expand_line(line_text, self.variables);
        if let Some(reason) = expanded.invalid {
            self.warnings.push(
                Warning::new(
                    WarningCode::ConfigInvalidValue,
                    format!("cannot expand this line ({reason}); skipped"),
                )
                .with_file(file)
                .with_line(line),
            );
            *self.skipped += 1;
            return;
        }
        if !expanded.missing.is_empty() {
            for name in expanded.missing {
                self.missing.insert(name.clone());
                self.warnings.push(
                    Warning::new(
                        WarningCode::ConfigInvalidValue,
                        format!(
                            "unresolved variable `${name}` (set it in the project variables or \
                             the environment); the line is skipped"
                        ),
                    )
                    .with_file(file)
                    .with_line(line),
                );
            }
            *self.skipped += 1;
            return;
        }
        let line_text = expanded.text.trim();
        if line_text.is_empty() {
            return;
        }
        if let Some(rest) = line_text.strip_prefix("+incdir+") {
            self.consume_incdirs(base, rest, file, line);
            return;
        }
        if let Some(rest) = line_text.strip_prefix("+define+") {
            self.consume_defines(rest, file, line);
            return;
        }
        if let Some(nested) = filelist_argument(line_text) {
            self.consume_nested(base, file, nested, line, depth);
            return;
        }
        if line_text.starts_with('+') || line_text.starts_with('-') {
            let token = line_text.split_whitespace().next().unwrap_or(line_text);
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistUnknownToken,
                    format!("unsupported filelist token `{token}`; skipped"),
                )
                .with_file(file)
                .with_line(line),
            );
            *self.skipped += 1;
            return;
        }
        let resolved = resolve(base, line_text);
        // ADDENDUM A-17: an entry that only exists because of an expansion must
        // not appear in the file set unannounced. A plain relative path is not
        // reported — a generated file is a normal workflow, the same rule
        // AC-2.3 applies to `+incdir+` — but an expansion (or a leading `~` that
        // stayed literal because it is not `~/`) that resolves to nothing is a
        // `file:line` diagnostic naming the path actually looked up. The entry
        // itself is KEPT: the mirror of "nothing is dropped silently".
        let unverified_reason = if expanded.substituted {
            Some("after variable expansion")
        } else if line_text.starts_with('~') {
            Some("a leading `~` is only expanded as `~/`, so the `~` is literal")
        } else {
            None
        };
        if let Some(reason) = unverified_reason {
            if !resolved.exists() {
                self.warnings.push(
                    Warning::new(
                        WarningCode::FilelistMissing,
                        format!(
                            "entry `{}` does not exist ({reason}); the path is kept so a \
                             generated file is picked up once it appears",
                            resolved.display()
                        ),
                    )
                    .with_file(file)
                    .with_line(line),
                );
            }
        }
        if self.entry_seen.insert(resolved.clone()) {
            self.entries.push(resolved);
        } else {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistDuplicateEntry,
                    "duplicate source path in filelist; kept the first occurrence",
                )
                .with_file(file)
                .with_line(line),
            );
        }
    }

    /// `+incdir+DIR[+DIR…]`: every directory is kept even when it does not
    /// exist yet (a generated include directory is a normal workflow), but a
    /// missing one is reported with the filelist and line that named it
    /// (AC-2.3): otherwise the only symptom is an include that silently fails
    /// to resolve later. The stable code is `filelist.missing`; the message
    /// says which entry kind it is, because the enum is the machine contract and
    /// a new variant would change that contract.
    fn consume_incdirs(&mut self, base: &Path, rest: &str, file: &Path, line: u32) {
        for dir in rest.split('+').filter(|part| !part.trim().is_empty()) {
            let dir = dir.trim();
            let resolved = resolve(base, dir);
            if !resolved.is_dir() {
                self.warnings.push(
                    Warning::new(
                        WarningCode::FilelistMissing,
                        format!(
                            "include directory `{dir}` does not exist (resolved to `{}`, or it \
                             is not a directory); the -I entry is kept so it works once the \
                             directory appears",
                            resolved.display()
                        ),
                    )
                    .with_file(file)
                    .with_line(line),
                );
            }
            if self.include_seen.insert(resolved.clone()) {
                self.include_dirs.push(resolved);
            }
        }
    }

    fn consume_defines(&mut self, rest: &str, file: &Path, line: u32) {
        for token in rest.split('+').filter(|part| !part.trim().is_empty()) {
            match parse_define(token) {
                Some(define) => {
                    if self.define_seen.insert(define.clone()) {
                        self.defines.push(define);
                    }
                }
                None => {
                    self.warnings.push(
                        Warning::new(
                            WarningCode::FilelistDefineInvalid,
                            "malformed +define+ token; skipped",
                        )
                        .with_file(file)
                        .with_line(line),
                    );
                    *self.skipped += 1;
                }
            }
        }
    }

    fn consume_nested(&mut self, base: &Path, file: &Path, nested: &str, line: u32, depth: u32) {
        let nested_path = resolve(base, nested);
        if !nested_path.is_file() {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistMissing,
                    format!("nested -f target `{nested}` does not exist; skipped"),
                )
                .with_file(file)
                .with_line(line),
            );
            *self.skipped += 1;
            return;
        }
        let canonical = std::fs::canonicalize(&nested_path).unwrap_or_else(|_| nested_path.clone());
        if let Some(position) = self
            .visiting
            .iter()
            .position(|(known, _)| known == &canonical)
        {
            // A-9: name BOTH files (and the whole chain) on the offending line.
            let chain: Vec<String> = self.visiting[position..]
                .iter()
                .map(|(_, named)| named.display().to_string())
                .chain(std::iter::once(nested_path.display().to_string()))
                .collect();
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistCycle,
                    format!("nested -f cycle: {}; skipped", chain.join(" -> ")),
                )
                .with_file(file)
                .with_line(line),
            );
            *self.skipped += 1;
            return;
        }
        if depth + 1 > MAX_DEPTH {
            self.warnings.push(
                Warning::new(
                    WarningCode::FilelistDepthExceeded,
                    format!(
                        "nested -f recursion deeper than {MAX_DEPTH}; skipped `{}`",
                        nested_path.display()
                    ),
                )
                .with_file(file)
                .with_line(line),
            );
            *self.skipped += 1;
            return;
        }
        self.visit(&nested_path, depth + 1);
    }
}

/// One expanded line: the text with every reference substituted, the names that
/// could not be resolved, whether anything was actually substituted (an entry
/// born through substitution gets an existence check, ADDENDUM A-17), or the
/// reason the line cannot be used at all.
struct ExpandedLine {
    text: String,
    missing: Vec<String>,
    /// At least one `$NAME`/`${NAME}`/`$(NAME)`/leading-`~` reference was
    /// substituted. `$$` is an escape, not a substitution.
    substituted: bool,
    invalid: Option<String>,
}

impl ExpandedLine {
    fn invalid(reason: impl Into<String>) -> Self {
        Self {
            text: String::new(),
            missing: Vec::new(),
            substituted: false,
            invalid: Some(reason.into()),
        }
    }
}

/// Expand `$VAR`, `${VAR}`, `$(VAR)`, `$$` (a literal `$`) and a leading `~`
/// (home) using `variables` (project table first, environment second).
///
/// A `$` that starts no reference form is a literal `$` (ADDENDUM A-17): the
/// shell rule, so file names such as `out_dollar$.sv` and `out_tail.sv$` are not
/// mistaken for broken references and dropped. `$$` remains the explicit escape.
fn expand_line(text: &str, variables: &Variables) -> ExpandedLine {
    let bytes = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut missing: Vec<String> = Vec::new();
    let mut substituted = false;
    let mut index = 0usize;
    while index < bytes.len() {
        let Some(character) = text[index..].chars().next() else {
            break;
        };
        if character == '~' && at_token_boundary(&out) {
            let followed_by_separator = index + 1 == bytes.len() || bytes[index + 1] == b'/';
            if followed_by_separator {
                match variables.home() {
                    Some(home) => {
                        out.push_str(&home.to_string_lossy());
                        substituted = true;
                        index += 1;
                        continue;
                    }
                    None => {
                        return ExpandedLine::invalid(
                            "`~` cannot be expanded because HOME is unset",
                        )
                    }
                }
            }
        }
        if character != '$' {
            out.push(character);
            index += character.len_utf8();
            continue;
        }
        let after = index + 1;
        if after >= bytes.len() {
            // A-17: a trailing `$` is a literal dollar sign.
            out.push('$');
            index = after;
            continue;
        }
        let introducer = bytes[after];
        if introducer == b'$' {
            out.push('$');
            index = after + 1;
            continue;
        }
        let (name, consumed_end) = if introducer == b'(' || introducer == b'{' {
            let opener = introducer as char;
            let close = if introducer == b'(' { ')' } else { '}' };
            let rest = &text[after + 1..];
            match rest.find(close) {
                Some(offset) => {
                    let name = rest[..offset].trim();
                    if name.is_empty() {
                        return ExpandedLine::invalid(format!(
                            "an empty `${opener}...{close}` reference is not a variable"
                        ));
                    }
                    (name.to_string(), after + 1 + offset + close.len_utf8())
                }
                None => {
                    return ExpandedLine::invalid(format!("`${opener}` has no closing `{close}`"))
                }
            }
        } else if introducer.is_ascii_alphabetic() || introducer == b'_' {
            let mut end = after;
            while end < bytes.len() && (bytes[end].is_ascii_alphanumeric() || bytes[end] == b'_') {
                end += 1;
            }
            (text[after..end].to_string(), end)
        } else {
            // A-17: `$` followed by something that cannot start an identifier is
            // a literal dollar sign (`.`, a digit, another separator, …). Only
            // the `$` is consumed; the next character is processed normally.
            out.push('$');
            index = after;
            continue;
        };
        match variables.resolve(&name) {
            Some(value) => {
                out.push_str(value);
                substituted = true;
            }
            None => missing.push(name),
        }
        index = consumed_end;
    }
    ExpandedLine {
        text: out,
        missing,
        substituted,
        invalid: None,
    }
}

/// `~` expands only at the start of a token (line start, after whitespace, or
/// after one of the filelist separators `+`/`=`).
fn at_token_boundary(out: &str) -> bool {
    match out.chars().next_back() {
        None => true,
        Some(previous) => previous.is_whitespace() || previous == '+' || previous == '=',
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
    let joined = if candidate.is_absolute() {
        candidate.to_path_buf()
    } else {
        base.join(candidate)
    };
    normalize(&joined)
}

/// Lexically normalize `.` and `..` components.
///
/// The parser resolves paths relative to the containing filelist, so a nested
/// filelist naturally produces `filelists/../tb/x.sv`. Left unnormalized, two
/// spellings of the same file would defeat duplicate detection and the UI would
/// show paths with `..`. Nothing touches the filesystem here: the file may not
/// exist yet, and symlinks must not be resolved.
fn normalize(path: &Path) -> PathBuf {
    use std::path::Component;

    let mut out = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                let last_is_name =
                    matches!(out.components().next_back(), Some(Component::Normal(_)));
                if last_is_name {
                    out.pop();
                } else if !out.has_root() {
                    out.push("..");
                }
            }
            other => out.push(other.as_os_str()),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::{parse, parse_with, MAX_DEPTH};
    use crate::project::{Filelist, Variables};
    use crate::test_support::{require, write};
    use crate::warnings::WarningCode;
    use std::collections::BTreeSet;
    use std::path::{Path, PathBuf};

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
    fn parent_components_are_normalized() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        // The directory exists: this test is about path normalization, not about
        // the "include directory is missing" diagnostic (AC-2.3).
        std::fs::create_dir_all(root.join("inc"))?;
        write(&root.join("sub/top.f"), "../rtl/a.sv\n+incdir+../inc\n")?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("sub/top.f"), &mut warnings))?;
        assert_eq!(filelist.entries, vec![root.join("rtl/a.sv")]);
        assert_eq!(filelist.include_dirs, vec![root.join("inc")]);
        assert!(warnings.is_empty(), "normalization must not add warnings");
        Ok(())
    }

    /// AC-2.3: a `+incdir+` entry that does not resolve is reported with the
    /// filelist and its 1-based line — never silently dropped (the entry itself
    /// stays in the list, because generated include directories appear later).
    #[test]
    fn missing_include_directory_is_reported_with_the_file_and_line(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "ok.sv\n+incdir+missing\n")?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("top.f"), &mut warnings))?;
        assert_eq!(filelist.include_dirs, vec![root.join("missing")]);
        let missing = warnings
            .iter()
            .find(|warning| warning.code == WarningCode::FilelistMissing)
            .unwrap_or_else(|| panic!("no include-directory warning: {warnings:?}"));
        assert_eq!(missing.file.as_deref(), Some(root.join("top.f").as_path()));
        assert_eq!(missing.line, Some(2));
        assert!(
            missing.message.contains("include directory `missing`"),
            "{}",
            missing.message
        );
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

    fn variables(project: &[(&str, &str)], environment: &[(&str, &str)]) -> Variables {
        let project: Vec<(String, String)> = project
            .iter()
            .map(|(name, value)| ((*name).to_string(), (*value).to_string()))
            .collect();
        let environment = environment
            .iter()
            .map(|(name, value)| ((*name).to_string(), (*value).to_string()));
        Variables::with_environment(&project, environment, Some(PathBuf::from("/home/tester")))
    }

    fn parse_expanding(
        path: &Path,
        vars: &Variables,
    ) -> (
        Option<Filelist>,
        Vec<crate::warnings::Warning>,
        Vec<String>,
        usize,
    ) {
        let mut warnings = Vec::new();
        let mut missing = BTreeSet::new();
        let mut skipped = 0usize;
        let filelist = parse_with(path, &mut warnings, vars, &mut missing, &mut skipped);
        (filelist, warnings, missing.into_iter().collect(), skipped)
    }

    /// ADDENDUM A-9: `$VAR`, `${VAR}`, `$(VAR)` and a leading `~` expand before
    /// any token is interpreted (entries, `+incdir+`, `+define+`).
    #[test]
    fn expands_variable_forms_and_tilde_before_tokens() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("top.f"),
            "rtl/$A.sv\nrtl/${B}.sv\nrtl/$(C).sv\n~/home.sv\n+incdir+$INC\n+define+D=$A\n",
        )?;
        let vars = variables(&[("A", "a"), ("B", "b"), ("C", "c"), ("INC", "inc")], &[]);
        let (filelist, warnings, missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(
            filelist.entries,
            vec![
                root.join("rtl/a.sv"),
                root.join("rtl/b.sv"),
                root.join("rtl/c.sv"),
                PathBuf::from("/home/tester/home.sv"),
            ]
        );
        assert_eq!(filelist.include_dirs, vec![root.join("inc")]);
        assert_eq!(filelist.defines.len(), 1);
        assert_eq!(filelist.defines[0].name, "D");
        assert_eq!(filelist.defines[0].value.as_deref(), Some("a"));
        assert!(missing.is_empty(), "{missing:?}");
        assert_eq!(skipped, 0);
        assert!(
            !warnings
                .iter()
                .any(|warning| warning.code == WarningCode::ConfigInvalidValue),
            "{warnings:?}"
        );
        Ok(())
    }

    /// ADDENDUM A-9: the project table wins over the process environment.
    #[test]
    fn project_variable_table_wins_over_the_environment() -> Result<(), Box<dyn std::error::Error>>
    {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "rtl/$WHICH.sv\n")?;
        let vars = variables(&[("WHICH", "project")], &[("WHICH", "environment")]);
        let (filelist, _warnings, missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(filelist.entries, vec![root.join("rtl/project.sv")]);
        assert!(missing.is_empty());
        assert_eq!(skipped, 0);
        Ok(())
    }

    /// ADDENDUM A-9: every unresolved variable is collected (for the one dialog)
    /// and each affected line is reported with file + 1-based line, never
    /// silently dropped.
    #[test]
    fn missing_variables_are_collected_and_reported_per_line(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("top.f"),
            "ok.sv\nrtl/$MISSING.sv\nrtl/${ALSO_MISSING}.sv\n",
        )?;
        let vars = variables(&[], &[]);
        let (filelist, warnings, missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(filelist.entries, vec![root.join("ok.sv")]);
        assert_eq!(
            missing,
            vec!["ALSO_MISSING".to_string(), "MISSING".to_string()]
        );
        assert_eq!(skipped, 2);
        let unresolved: Vec<&crate::warnings::Warning> = warnings
            .iter()
            .filter(|warning| {
                warning.code == WarningCode::ConfigInvalidValue
                    && warning.message.contains("unresolved variable")
            })
            .collect();
        assert_eq!(unresolved.len(), 2);
        assert!(unresolved.iter().any(|warning| warning.line == Some(2)
            && warning.file.as_deref() == Some(root.join("top.f").as_path())
            && warning.message.contains("MISSING")));
        assert!(unresolved.iter().any(|warning| warning.line == Some(3)));
        Ok(())
    }

    /// ADDENDUM A-9: a cycle names both files (and the chain) on the offending
    /// line instead of recursing.
    #[test]
    fn a_nested_cycle_names_both_files() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("a.f"), "-f b.f\n")?;
        write(&root.join("b.f"), "-f a.f\n")?;
        let vars = variables(&[], &[]);
        let (filelist, warnings, _missing, skipped) = parse_expanding(&root.join("a.f"), &vars);
        assert!(filelist.is_some());
        assert_eq!(skipped, 1, "{warnings:?}");
        let cycle = warnings
            .iter()
            .find(|warning| warning.code == WarningCode::FilelistCycle)
            .unwrap_or_else(|| panic!("no cycle warning: {warnings:?}"));
        assert!(cycle.message.contains("a.f"), "{}", cycle.message);
        assert!(cycle.message.contains("b.f"), "{}", cycle.message);
        assert!(cycle.message.contains("->"), "{}", cycle.message);
        assert_eq!(cycle.line, Some(1));
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

    /// ADDENDUM A-17: a `$` that starts none of the reference forms is a literal
    /// dollar sign, so `out_dollar$.sv`, `out_tail.sv$` and `$5.star.sv` are
    /// legal paths — not hard errors that drop the entry.
    #[test]
    fn a_dollar_that_starts_no_reference_is_literal() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("top.f"),
            "out_dollar$.sv\nout_tail.sv$\n$5.star.sv\nrtl/ok.sv\n",
        )?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("top.f"), &mut warnings))?;
        assert_eq!(
            filelist.entries,
            vec![
                root.join("out_dollar$.sv"),
                root.join("out_tail.sv$"),
                root.join("$5.star.sv"),
                root.join("rtl/ok.sv"),
            ],
            "the literal `$` lines must survive as entries"
        );
        assert!(
            warnings.is_empty(),
            "a literal `$` must not warn: {warnings:?}"
        );
        Ok(())
    }

    /// ADDENDUM A-17 keeps the explicit escape: `$$` is exactly one literal `$`
    /// in the resulting path, and escaping is not an expansion that demands the
    /// file exist.
    #[test]
    fn a_double_dollar_is_still_the_escape() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "out_dollar$$.sv\n$$head.sv\n")?;
        let mut warnings = Vec::new();
        let filelist = require(parse(&root.join("top.f"), &mut warnings))?;
        assert_eq!(
            filelist.entries,
            vec![root.join("out_dollar$.sv"), root.join("$head.sv")]
        );
        assert!(warnings.is_empty(), "{warnings:?}");
        Ok(())
    }

    /// ADDENDUM A-17 negative control: a real reference is unchanged — it
    /// resolves, or it is collected and its line skipped. The literal rule must
    /// not swallow `$NAME` semantics.
    #[test]
    fn a_real_reference_still_resolves_or_is_reported() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "rtl/$KNOWN.sv\nrtl/$UNKNOWN.sv\n")?;
        write(&root.join("rtl/known.sv"), "module known; endmodule\n")?;
        let vars = variables(&[("KNOWN", "known")], &[]);
        let (filelist, warnings, missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(filelist.entries, vec![root.join("rtl/known.sv")]);
        assert_eq!(missing, vec!["UNKNOWN".to_string()]);
        assert_eq!(skipped, 1);
        let unresolved = warnings
            .iter()
            .find(|warning| warning.code == WarningCode::ConfigInvalidValue)
            .unwrap_or_else(|| panic!("no unresolved-variable warning: {warnings:?}"));
        assert_eq!(unresolved.line, Some(2));
        assert!(
            unresolved.message.contains("UNKNOWN"),
            "{}",
            unresolved.message
        );
        Ok(())
    }

    /// ADDENDUM A-17: an entry born through expansion that does not exist is
    /// reported with the file, the 1-based line and the path actually looked up.
    /// It stays in the file set (a generated file is a normal workflow): the
    /// mirror of "nothing is dropped silently". A plain literal path is the
    /// negative control — it is not reported, exactly like `+incdir+`.
    #[test]
    fn an_expanded_entry_that_does_not_exist_is_reported_and_kept(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("top.f"),
            "$RTL/generated.sv\nrtl/$GENERATED.sv\nliteral/not_yet.sv\n",
        )?;
        let vars = variables(&[("RTL", "rtl"), ("GENERATED", "gen")], &[]);
        let (filelist, warnings, _missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(
            filelist.entries,
            vec![
                root.join("rtl/generated.sv"),
                root.join("rtl/gen.sv"),
                root.join("literal/not_yet.sv"),
            ],
            "the entries are kept, not dropped"
        );
        assert_eq!(skipped, 0, "nothing was dropped, so nothing is skipped");
        let missing_entries: Vec<&crate::warnings::Warning> = warnings
            .iter()
            .filter(|warning| warning.code == WarningCode::FilelistMissing)
            .collect();
        assert_eq!(missing_entries.len(), 2, "{warnings:?}");
        assert!(missing_entries.iter().any(|warning| {
            warning.line == Some(1)
                && warning.file.as_deref() == Some(root.join("top.f").as_path())
                && warning
                    .message
                    .contains(&root.join("rtl/generated.sv").display().to_string())
        }));
        assert!(missing_entries
            .iter()
            .any(|warning| warning.line == Some(2)));
        Ok(())
    }

    /// ADDENDUM A-17: a leading `~` that is not `~/` stays literal (there is no
    /// user lookup) — but it must not enter the file set without a word.
    #[test]
    fn an_unexpanded_home_path_is_reported() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "~nosuchuser/never_home.sv\n")?;
        let vars = variables(&[], &[]);
        let (filelist, warnings, _missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert_eq!(
            filelist.entries,
            vec![root.join("~nosuchuser/never_home.sv")]
        );
        assert_eq!(skipped, 0);
        let warning = warnings
            .iter()
            .find(|warning| warning.code == WarningCode::FilelistMissing)
            .unwrap_or_else(|| panic!("no diagnostic for a literal `~` path: {warnings:?}"));
        assert_eq!(warning.line, Some(1));
        assert!(warning.message.contains("`~/`"), "{}", warning.message);
        Ok(())
    }

    /// ADDENDUM A-17: the depth diagnostic names the nested filelist it refused
    /// to open, so "somewhere too deep" is not all the user gets.
    #[test]
    fn depth_limit_names_the_skipped_target() -> Result<(), Box<dyn std::error::Error>> {
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
        let depth = warnings
            .iter()
            .find(|warning| warning.code == WarningCode::FilelistDepthExceeded)
            .unwrap_or_else(|| panic!("no depth warning: {warnings:?}"));
        assert!(
            depth.message.contains("f17.f"),
            "the skipped target must be named: {}",
            depth.message
        );
        Ok(())
    }

    /// ADDENDUM A-17 boundary: only a `$` that starts NO reference is literal. A
    /// `${` / `$(` that opens a reference and is malformed (empty name) or
    /// unterminated stays an error instead of silently turning into a path.
    #[test]
    fn a_malformed_reference_is_still_an_error() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "rtl/${}.sv\nrtl/$(BROKEN.sv\n")?;
        let vars = variables(&[], &[]);
        let (filelist, warnings, missing, skipped) = parse_expanding(&root.join("top.f"), &vars);
        let filelist = require(filelist)?;
        assert!(
            filelist.entries.is_empty(),
            "an unusable line contributes no entry"
        );
        assert!(missing.is_empty());
        assert_eq!(skipped, 2);
        assert_eq!(
            warnings
                .iter()
                .filter(|warning| warning.code == WarningCode::ConfigInvalidValue)
                .count(),
            2,
            "{warnings:?}"
        );
        Ok(())
    }
}
