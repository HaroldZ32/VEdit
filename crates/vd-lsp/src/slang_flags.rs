//! Rebase compiler-only flags into the private workspace given to slang-server.

use std::collections::BTreeMap;
use std::io;
use std::path::{Path, PathBuf};

use crate::session::create_private_file;

#[derive(Clone, Copy)]
enum Input {
    WorkspaceCommand,
    RelativeCommand,
    IncludePath,
    Literal,
    ProjectSource,
}

const OPTIONS: &[(&str, Input, bool)] = &[
    ("--include-directory", Input::IncludePath, false),
    ("--isystem", Input::IncludePath, false),
    ("--suppress-warnings", Input::IncludePath, false),
    ("--suppress-macro-warnings", Input::IncludePath, false),
    ("--libfile", Input::ProjectSource, false),
    ("--libdir", Input::ProjectSource, false),
    ("--libmap", Input::ProjectSource, false),
    ("--define-macro", Input::Literal, false),
    ("--undefine-macro", Input::Literal, false),
    ("+incdir+", Input::IncludePath, true),
    ("-D", Input::Literal, true),
    ("-U", Input::Literal, true),
    ("-I", Input::IncludePath, true),
    ("-f", Input::WorkspaceCommand, true),
    ("-F", Input::RelativeCommand, true),
    ("-C", Input::ProjectSource, true),
    ("-v", Input::ProjectSource, true),
    ("-y", Input::ProjectSource, true),
];

pub(crate) struct CompilerFlags<'a> {
    workspace: &'a Path,
    output: &'a Path,
    environment: &'a BTreeMap<String, String>,
    rewritten: BTreeMap<(PathBuf, PathBuf), PathBuf>,
    stack: Vec<PathBuf>,
    next: usize,
}

impl<'a> CompilerFlags<'a> {
    pub(crate) fn new(
        workspace: &'a Path,
        output: &'a Path,
        environment: &'a BTreeMap<String, String>,
    ) -> Self {
        Self {
            workspace,
            output,
            environment,
            rewritten: BTreeMap::new(),
            stack: Vec::new(),
            next: 0,
        }
    }

    pub(crate) fn rebase(&mut self, flags: &str) -> io::Result<String> {
        self.rebase_from(flags, self.workspace)
    }

    fn rebase_from(&mut self, flags: &str, base: &Path) -> io::Result<String> {
        let mut result = String::with_capacity(flags.len());
        let mut tokens = tokens(flags).into_iter();
        while let Some(token) = tokens.next() {
            if let Some((option, kind, inline)) = option(token) {
                let raw = match inline {
                    Some(value) => value,
                    None => tokens
                        .next()
                        .ok_or_else(|| io::Error::other(format!("missing {option} argument")))?,
                };
                match kind {
                    Input::ProjectSource => {}
                    Input::Literal => {
                        result.push_str(option);
                        result.push(' ');
                        result.push_str(raw);
                        result.push('\n');
                    }
                    Input::IncludePath => {
                        result.push_str(option);
                        if option.starts_with("--") {
                            result.push('=');
                        }
                        let decoded = decode_path(raw, self.environment);
                        let delimiter = if option == "+incdir+" { '+' } else { ',' };
                        for (idx, path) in decoded.split(delimiter).enumerate() {
                            if idx > 0 {
                                result.push(delimiter);
                            }
                            if path.starts_with('~') {
                                result.push_str(&quote_path(Path::new(path)));
                            } else {
                                result.push_str(&quote_path(&base.join(path)));
                            }
                        }
                        result.push('\n');
                    }
                    Input::WorkspaceCommand | Input::RelativeCommand => {
                        let decoded = decode_path(raw, self.environment);
                        for path in decoded.split(',') {
                            let built = self.command_file(base, path, kind)?;
                            result.push_str("-f ");
                            result.push_str(&quote_path(&built));
                            result.push('\n');
                        }
                    }
                }
            } else if token.starts_with(['-', '+']) || !is_source_operand(token) {
                result.push_str(token);
                result.push(' ');
            }
        }
        Ok(result)
    }

    fn command_file(&mut self, base: &Path, input: &str, kind: Input) -> io::Result<PathBuf> {
        let requested = base.join(input);
        let original = requested.canonicalize().map_err(|error| {
            io::Error::new(
                error.kind(),
                format!(
                    "reading Slang command file {}: {error}",
                    requested.display()
                ),
            )
        })?;
        let nested_base = if matches!(kind, Input::RelativeCommand) {
            original
                .parent()
                .ok_or_else(|| io::Error::other("command file has no containing directory"))?
        } else {
            base
        };
        let key = (original.clone(), nested_base.to_path_buf());
        if let Some(output) = self.rewritten.get(&key) {
            return Ok(output.clone());
        }
        if self.stack.len() >= 64 || self.stack.contains(&original) || self.next >= 1024 {
            return Err(io::Error::other(format!(
                "Slang command file recursion or file limit at {}",
                original.display()
            )));
        }
        let text = std::fs::read_to_string(&original)?;
        self.stack.push(original.clone());
        let rewritten = self.rebase_from(&text, nested_base);
        self.stack.pop();
        let output = self.output.join(format!("compiler-{}.f", self.next));
        self.next += 1;
        create_private_file(&output, &rewritten?)?;
        self.rewritten.insert(key, output.clone());
        Ok(output)
    }
}

fn option(token: &str) -> Option<(&'static str, Input, Option<&str>)> {
    OPTIONS.iter().find_map(|&(name, kind, attached)| {
        let rest = token.strip_prefix(name)?;
        if rest.is_empty() {
            Some((name, kind, None))
        } else if let Some(value) = rest.strip_prefix('=') {
            Some((name, kind, Some(value)))
        } else if attached {
            Some((name, kind, Some(rest)))
        } else {
            None
        }
    })
}

fn is_source_operand(token: &str) -> bool {
    token.trim_matches(['\'', '"']).rsplit(',').any(|part| {
        if part.contains(['*', '?']) && !part.contains('=') {
            return true;
        }
        Path::new(part)
            .extension()
            .and_then(std::ffi::OsStr::to_str)
            .is_some_and(|ext| {
                ["sv", "v", "svh", "vh"]
                    .iter()
                    .any(|hdl| ext.trim_end_matches('$').eq_ignore_ascii_case(hdl))
            })
    })
}

fn quote_path(path: &Path) -> String {
    format!(
        "\"{}\"",
        path.display()
            .to_string()
            .replace('\\', "\\\\")
            .replace('"', "\\\"")
    )
}

fn decode_path(raw: &str, environment: &BTreeMap<String, String>) -> String {
    let bare = raw.trim_matches(['\'', '"']);
    if raw.starts_with('\'') {
        return bare.to_string();
    }
    if let Some(after) = bare.strip_prefix('$') {
        let (key, suffix) = if let Some(braced) = after.strip_prefix('{') {
            braced.split_once('}').unwrap_or((after, ""))
        } else {
            let split = after
                .find(|c: char| !c.is_ascii_alphanumeric() && c != '_')
                .unwrap_or(after.len());
            after.split_at(split)
        };
        if let Some(value) = environment
            .get(key)
            .cloned()
            .or_else(|| std::env::var(key).ok())
        {
            return value + suffix;
        }
    }
    bare.to_string()
}

// Keep native argument spelling for macros and quoted values while recognizing
// the whitespace and comments that separate compiler options.
fn tokens(text: &str) -> Vec<&str> {
    let mut result = Vec::new();
    let mut pos = 0;
    let bytes = text.as_bytes();
    while pos < bytes.len() {
        let c = bytes[pos];
        if c.is_ascii_whitespace() {
            pos += 1;
            continue;
        }
        if c == b'#' || bytes[pos..].starts_with(b"//") {
            pos += bytes[pos..]
                .iter()
                .position(|&byte| byte == b'\n')
                .unwrap_or(bytes.len() - pos);
            continue;
        }
        if bytes[pos..].starts_with(b"/*") {
            pos += bytes[pos + 2..]
                .windows(2)
                .position(|pair| pair == b"*/")
                .map_or(bytes.len() - pos, |idx| idx + 4);
            continue;
        }
        let start = pos;
        let mut quote = None;
        while pos < bytes.len() {
            let c = bytes[pos];
            if quote.is_none() && (c.is_ascii_whitespace() || c == b'#') {
                break;
            }
            if c == b'\\' && quote != Some(b'\'') && pos + 1 < bytes.len() {
                pos += 2;
                continue;
            }
            if quote == Some(c) {
                quote = None;
            } else if quote.is_none() && matches!(c, b'\'' | b'"') {
                quote = Some(c);
            }
            pos += 1;
        }
        result.push(&text[start..pos]);
    }
    result
}
