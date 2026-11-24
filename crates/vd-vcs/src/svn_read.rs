//! Typed reads used by the SVN Working Tree and History panels.

use crate::runner::read_output;
use crate::svn_write::{literal_target, scoped_path};
use crate::{detect, Backend};
use std::path::{Path, PathBuf};

pub(crate) fn working_copy(root: &Path) -> Result<PathBuf, String> {
    match detect(root) {
        (Backend::Svn, Some(path)) => Ok(path),
        _ => Err("The project is not an SVN working copy.".into()),
    }
}

pub(crate) fn svn(root: &Path, command: &str, options: &[&str]) -> Result<String, String> {
    let args = [command, "--non-interactive", "--no-auth-cache"]
        .into_iter()
        .chain(options.iter().copied())
        .map(str::to_owned)
        .collect::<Vec<_>>();
    let output = read_output("svn", root, &args)?;
    if !output.status.success() {
        return Err(crate::write::redact_url_userinfo(
            String::from_utf8_lossy(&output.stderr).trim(),
        ));
    }
    if output.stdout.len() > 16 * 1024 * 1024 {
        return Err("The SVN result exceeds the 16 MiB display limit.".into());
    }
    String::from_utf8(output.stdout).map_err(|_| "The SVN output is not UTF-8.".into())
}

fn xml_text(mut text: &str) -> String {
    let mut output = String::new();
    while let Some((before, entity)) = text.split_once('&') {
        output.push_str(before);
        let Some((name, after)) = entity.split_once(';') else {
            output.push_str(text.get(before.len()..).unwrap_or_default());
            return output;
        };
        let decoded = match name {
            "amp" => Some('&'),
            "lt" => Some('<'),
            "gt" => Some('>'),
            "quot" => Some('"'),
            "apos" => Some('\''),
            _ => name
                .strip_prefix('#')
                .and_then(|number| {
                    number.strip_prefix('x').map_or_else(
                        || number.parse().ok(),
                        |hex| u32::from_str_radix(hex, 16).ok(),
                    )
                })
                .and_then(char::from_u32),
        };
        if let Some(value) = decoded {
            output.push(value);
        } else {
            output.push('&');
            output.push_str(name);
            output.push(';');
        }
        text = after;
    }
    output.push_str(text);
    output
}

fn attr<'a>(header: &'a str, name: &str) -> Option<&'a str> {
    header
        .split_once(&format!("{name}=\""))?
        .1
        .split_once('"')
        .map(|(value, _)| value)
}

fn block<'a>(xml: &'a str, tag: &str) -> Option<(&'a str, &'a str)> {
    let (_, tail) = xml.split_once(&format!("<{tag}"))?;
    let (header, text) = tail.split_once('>')?;
    Some((
        header,
        text.split_once(&format!("</{tag}>"))
            .map_or("", |(body, _)| body),
    ))
}

/// One entry in a local or remote SVN status response.
pub struct StatusEntry {
    /// Path relative to the working copy.
    pub path: String,
    /// Change kind as used by the VCS panel. Zero means remote only.
    pub kind: i32,
    /// The containing changelist, when one is assigned.
    pub changelist: Option<String>,
}

fn entries(xml: &str, list: Option<&str>, result: &mut Vec<StatusEntry>) {
    for entry in xml.split("<entry").skip(1) {
        let Some((header, contents)) = entry.split_once('>') else {
            continue;
        };
        let Some(path) = attr(header, "path") else {
            continue;
        };
        let Some((wc, _)) = block(contents, "wc-status") else {
            continue;
        };
        let remote = block(contents, "repos-status").is_some_and(|(header, _)| {
            !matches!(attr(header, "item"), None | Some("none"))
                || matches!(attr(header, "props"), Some("modified"))
        });
        let kind = if attr(wc, "tree-conflicted") == Some("true")
            || attr(wc, "props") == Some("conflicted")
        {
            5
        } else {
            match attr(wc, "item") {
                Some("added") => 1,
                Some("modified") => 2,
                Some("deleted" | "missing") => 3,
                Some("replaced") => 4,
                Some("conflicted" | "obstructed") => 5,
                Some("unversioned") => 7,
                _ if attr(wc, "props") == Some("modified") => 2,
                _ if remote => 0,
                _ => continue,
            }
        };
        result.push(StatusEntry {
            path: xml_text(path),
            kind,
            changelist: list.map(xml_text),
        });
    }
}

/// The local working-copy status, or the paths with incoming changes.
///
/// # Errors
/// Returns the SVN failure if status cannot be obtained.
pub fn status(root: &Path, remote: bool) -> Result<Vec<StatusEntry>, String> {
    let cwd = working_copy(root)?;
    let mut args = vec!["--xml", "--ignore-externals"];
    if remote {
        args.push("--show-updates");
    }
    let xml = svn(&cwd, "status", &args)?;
    let mut result = Vec::new();
    let mut sections = xml.split("<changelist");
    if let Some(target) = sections.next() {
        entries(target, None, &mut result);
    }
    for section in sections {
        if let Some((header, text)) = section.split_once('>') {
            entries(text, attr(header, "name"), &mut result);
        }
    }
    if remote {
        result.clear();
        for entry in xml.split("<entry").skip(1) {
            if let Some((header, content)) = entry.split_once('>') {
                if let (Some(path), Some((remote, _))) =
                    (attr(header, "path"), block(content, "repos-status"))
                {
                    if !matches!(attr(remote, "item"), None | Some("none"))
                        || attr(remote, "props") == Some("modified")
                    {
                        result.push(StatusEntry {
                            path: xml_text(path),
                            kind: 0,
                            changelist: None,
                        });
                    }
                }
            }
        }
    }
    Ok(result)
}

/// One SVN revision record.
pub struct LogEntry {
    /// Numeric revision.
    pub revision: u64,
    /// Commit author.
    pub author: String,
    /// ISO 8601 commit date.
    pub date: String,
    /// Commit message.
    pub message: String,
}

impl LogEntry {
    /// Seconds since the Unix epoch, for display alongside Git history.
    #[must_use]
    pub fn unix_time(&self) -> i64 {
        let parse = |range: std::ops::Range<usize>| self.date.get(range)?.parse::<i64>().ok();
        let Some((year, month, day, hour, minute, second)) = (|| {
            Some((
                parse(0..4)?,
                parse(5..7)?,
                parse(8..10)?,
                parse(11..13)?,
                parse(14..16)?,
                parse(17..19)?,
            ))
        })() else {
            return 0;
        };
        let year = year - i64::from(month <= 2);
        let era = year.div_euclid(400);
        let year_of_era = year.rem_euclid(400);
        let month = if month > 2 { month - 3 } else { month + 9 };
        let days = era * 146097 + year_of_era * 365 + year_of_era / 4 - year_of_era / 100
            + (153 * month + 2) / 5
            + day
            - 1
            - 719468;
        days * 86400 + hour * 3600 + minute * 60 + second
    }
}

pub(crate) fn log_entries(xml: &str) -> impl Iterator<Item = LogEntry> + '_ {
    xml.split("<logentry").skip(1).filter_map(|entry| {
        let (header, body) = entry.split_once('>')?;
        let revision = attr(header, "revision")?.parse().ok()?;
        let field = |tag| block(body, tag).map_or_else(String::new, |(_, text)| xml_text(text));
        Some(LogEntry {
            revision,
            author: field("author"),
            date: field("date"),
            message: field("msg"),
        })
    })
}

/// Read repository history. A decimal number prefixed with `r` selects one revision.
///
/// # Errors
/// Returns the SVN failure or an invalid paging request.
pub fn log(root: &Path, query: &str, limit: usize) -> Result<Vec<LogEntry>, String> {
    log_at(&working_copy(root)?, "^/", query, limit)
}

pub(crate) fn log_at(
    cwd: &Path,
    target: &str,
    query: &str,
    limit: usize,
) -> Result<Vec<LogEntry>, String> {
    let count = limit.clamp(1, 1001).to_string();
    let revision = query.strip_prefix('r').unwrap_or(query).parse::<u64>().ok();
    let revision = revision.map(|value| value.to_string());
    let mut args = vec![
        "--xml",
        "--limit",
        &count,
        "-r",
        revision.as_deref().unwrap_or("HEAD:1"),
    ];
    let pattern;
    if revision.is_none() && !query.is_empty() {
        pattern = escape_pattern(query);
        args.extend(["--search", pattern.as_str()]);
    }
    args.extend(["--", target]);
    Ok(log_entries(&svn(cwd, "log", &args)?).collect())
}

/// Read the commit message and repository paths for a specific revision.
///
/// # Errors
/// Returns the SVN failure or an invalid revision argument.
pub fn revision_detail(root: &Path, revision: &str) -> Result<(LogEntry, Vec<String>), String> {
    detail_at(&working_copy(root)?, "^/", revision)
}

pub(crate) fn detail_at(
    cwd: &Path,
    target: &str,
    revision: &str,
) -> Result<(LogEntry, Vec<String>), String> {
    if revision.parse::<u64>().is_err() {
        return Err("Expected a numeric SVN revision.".into());
    }
    let xml = svn(
        cwd,
        "log",
        &["--xml", "--verbose", "-r", revision, "--", target],
    )?;
    let entry = log_entries(&xml).next().ok_or("No such SVN revision.")?;
    let paths = xml
        .split("<path\n")
        .skip(1)
        .filter_map(|path| {
            let (header, text) = path.split_once('>')?;
            let (path, _) = text.split_once("</path>")?;
            Some(format!("{}  {}", attr(header, "action")?, xml_text(path)))
        })
        .collect();
    Ok((entry, paths))
}

/// Return the checkout URL's decoded path relative to its repository root.
///
/// # Errors
/// Returns the SVN failure or an invalid repository-relative path.
pub fn checkout_repository_path(root: &Path) -> Result<String, String> {
    let cwd = working_copy(root)?;
    let text = svn(
        &cwd,
        "info",
        &["--show-item", "relative-url", "--no-newline", "--", "."],
    )?;
    text.strip_prefix('^')
        .and_then(crate::svn_compare::decode_url_path)
        .ok_or_else(|| "SVN did not report a repository-relative path.".into())
}

/// Return the credential-free URL of the current working copy's repository root.
///
/// # Errors
/// Returns the SVN failure or an unsafe URL error.
pub fn checkout_repository_url(root: &Path) -> Result<String, String> {
    let cwd = working_copy(root)?;
    let url = svn(
        &cwd,
        "info",
        &["--show-item", "repos-root-url", "--no-newline", "--", "."],
    )?;
    crate::svn_write::checkout_url(&url)
        .map(str::to_owned)
        .map_err(|error| error.message().to_owned())
}

fn escape_pattern(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('[', "\\[")
        .replace('*', "\\*")
        .replace('?', "\\?")
}

/// The explicitly selected SVN patch scope.
pub enum PatchScope<'a> {
    /// Working copy changes.
    All,
    /// Explicit working-copy files.
    Paths(&'a [&'a str]),
    /// One named changelist.
    Changelist(&'a str),
}

/// Generate a unified SVN patch from the working copy.
///
/// # Errors
/// Returns a path validation error or the SVN failure.
pub fn patch(root: &Path, scope: PatchScope<'_>) -> Result<String, String> {
    let cwd = working_copy(root)?;
    match scope {
        PatchScope::All => svn(&cwd, "diff", &["--internal-diff"]),
        PatchScope::Paths(paths) if !paths.is_empty() => {
            let targets = paths
                .iter()
                .map(|path| {
                    if *path != "." {
                        scoped_path(&cwd, path).map_err(|error| error.message().to_owned())?;
                    }
                    Ok(literal_target(path))
                })
                .collect::<Result<Vec<_>, String>>()?;
            let mut args = vec!["--internal-diff", "--depth", "empty", "--"];
            args.extend(targets.iter().map(String::as_str));
            svn(&cwd, "diff", &args)
        }
        PatchScope::Changelist(name)
            if !name.trim().is_empty() && !name.chars().any(char::is_control) =>
        {
            svn(
                &cwd,
                "diff",
                &["--internal-diff", &format!("--changelist={name}")],
            )
        }
        _ => Err("Select an SVN patch path or changelist.".into()),
    }
}
