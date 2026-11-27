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

fn svn_output(root: &Path, command: &str, options: &[&str]) -> Result<Vec<u8>, String> {
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
    Ok(output.stdout)
}

pub(crate) fn svn(root: &Path, command: &str, options: &[&str]) -> Result<String, String> {
    let stdout = svn_output(root, command, options)?;
    if stdout.len() > 16 * 1024 * 1024 {
        return Err("The SVN result exceeds the 16 MiB display limit.".into());
    }
    String::from_utf8(stdout).map_err(|_| "The SVN output is not UTF-8.".into())
}

fn unsupported_option(error: &str, option: &str) -> bool {
    error.contains(option)
        && (error.contains("invalid option") || error.contains("doesn't accept option"))
}

pub(crate) fn cat(root: &Path, revision: &str, target: &str) -> Result<String, String> {
    let args = ["-r", revision, "--", target];
    let stdout = svn_output(
        root,
        "cat",
        &["--ignore-keywords", "-r", revision, "--", target],
    )
    .or_else(|error| {
        if unsupported_option(&error, "--ignore-keywords") {
            svn_output(root, "cat", &args)
        } else {
            Err(error)
        }
    })?;
    if stdout.len() > 8 * 1024 * 1024 {
        return Err("The SVN result exceeds the 8 MiB comparison limit.".into());
    }
    let text = String::from_utf8(stdout).map_err(|_| {
        "The SVN result uses an unsupported encoding. Comparison requires UTF-8 text."
    })?;
    if text.contains('\0') {
        return Err("The SVN result is binary. Comparison requires UTF-8 text.".into());
    }
    Ok(text)
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

pub(crate) struct Info {
    pub url: String,
    pub repository_root: String,
    pub revision: Option<u64>,
}

impl Info {
    pub(crate) fn repository_path(&self) -> Result<String, String> {
        if self.url == self.repository_root {
            return Ok("/".into());
        }
        self.url
            .strip_prefix(self.repository_root.trim_end_matches('/'))
            .filter(|path| path.starts_with('/'))
            .and_then(crate::svn_compare::decode_url_path)
            .ok_or_else(|| "SVN did not report a repository-relative path.".into())
    }
}

pub(crate) fn info(cwd: &Path, target: &str) -> Result<Info, String> {
    parse_info(&svn(cwd, "info", &["--xml", "--", target])?)
}

pub(crate) fn info_at(cwd: &Path, target: &str, revision: u64) -> Result<Info, String> {
    let revision = revision.to_string();
    parse_info(&svn(
        cwd,
        "info",
        &["--xml", "-r", &revision, "--", target],
    )?)
}

fn parse_info(xml: &str) -> Result<Info, String> {
    let (header, entry) = block(xml, "entry").ok_or("SVN info did not report an entry.")?;
    let read_url = |tag| {
        let value = block(entry, tag)
            .map(|(_, text)| xml_text(text))
            .ok_or("SVN info did not report a repository URL.")?;
        crate::svn_write::checkout_url(&value).map_err(|error| error.message().to_owned())?;
        Ok::<_, String>(value)
    };
    Ok(Info {
        url: read_url("url")?,
        repository_root: read_url("root")?,
        revision: attr(header, "revision").and_then(|value| value.parse().ok()),
    })
}

fn checkout_info(root: &Path) -> Result<Info, String> {
    working_copy(root)?;
    info(root, ".")
}

/// Read history under the selected working-copy directory. A decimal or `r`-prefixed query selects one revision.
///
/// # Errors
/// Returns the SVN failure or an invalid paging request.
pub fn log(root: &Path, query: &str, limit: usize) -> Result<Vec<LogEntry>, String> {
    let info = checkout_info(root)?;
    log_at(root, &format!("{}@HEAD", info.url), query, limit, true)
}

fn log_xml(
    cwd: &Path,
    target: &str,
    options: &[&str],
    limit: usize,
    search: Option<&str>,
) -> Result<(String, bool), String> {
    let count = limit.to_string();
    let mut args = vec!["--xml"];
    args.extend_from_slice(options);
    if let Some(query) = search {
        let pattern = escape_pattern(query);
        let mut filtered = args.clone();
        filtered.extend(["--limit", &count, "--search", &pattern, "--", target]);
        match svn(cwd, "log", &filtered) {
            Ok(xml) => return Ok((xml, false)),
            Err(error) if unsupported_option(&error, "--search") => (),
            Err(error) => return Err(error),
        }
    }
    let count = if search.is_some() { "1001" } else { &count };
    args.extend(["--limit", count, "--", target]);
    svn(cwd, "log", &args).map(|xml| (xml, search.is_some()))
}

pub(crate) fn log_at(
    cwd: &Path,
    target: &str,
    query: &str,
    limit: usize,
    stop_on_copy: bool,
) -> Result<Vec<LogEntry>, String> {
    let limit = limit.clamp(1, 1001);
    let revision = query.strip_prefix('r').unwrap_or(query).parse::<u64>().ok();
    let search = (revision.is_none() && !query.is_empty()).then_some(query);
    let revision = revision.map(|value| value.to_string());
    let mut args = vec!["-r", revision.as_deref().unwrap_or("HEAD:1")];
    if stop_on_copy {
        args.push("--stop-on-copy");
    }
    let (xml, needs_filter) = log_xml(cwd, target, &args, limit, search)?;
    let needle = query.to_lowercase();
    Ok(log_entries(&xml)
        .filter(|entry| {
            !needs_filter
                || [&entry.author, &entry.date, &entry.message]
                    .into_iter()
                    .any(|field| field.to_lowercase().contains(&needle))
        })
        .take(limit)
        .collect())
}

/// The action recorded for a path in an SVN revision.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathAction {
    /// A new path, possibly copied from another path.
    Added = b'A' as isize,
    /// The path was deleted.
    Deleted = b'D' as isize,
    /// The path or its properties changed.
    Modified = b'M' as isize,
    /// The path was replaced in the same revision.
    Replaced = b'R' as isize,
}

/// The node kind reported by SVN, which older servers may omit.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NodeKind {
    /// The server did not report the kind.
    Unknown,
    /// A repository file.
    File,
    /// A repository directory.
    Directory,
}

/// The origin of a copied or moved node.
#[derive(Debug, PartialEq, Eq)]
pub struct CopySource {
    /// Repository-absolute path, decoded from XML only.
    pub path: String,
    /// Exact revision of that path in the copy operation.
    pub revision: u64,
}

/// A path changed by a revision.
#[derive(Debug, PartialEq, Eq)]
pub struct ChangedPath {
    /// Repository-absolute path, decoded from XML only.
    pub path: String,
    /// How this path changed.
    pub action: PathAction,
    /// File, directory or unknown.
    pub kind: NodeKind,
    /// Provenance as recorded by SVN.
    pub copy_from: Option<CopySource>,
}

/// A revision with changes under the selected repository subtree.
pub struct RevisionDetail {
    /// Commit metadata.
    pub entry: LogEntry,
    /// Credential-free repository root URL.
    pub repository_root: String,
    /// Decoded repository-absolute path of the selected subtree.
    pub repository_path: String,
    /// Changed nodes under that subtree.
    pub changes: Vec<ChangedPath>,
}

fn changed_paths(xml: &str) -> impl Iterator<Item = ChangedPath> + '_ {
    block(xml, "paths")
        .map_or("", |(_, text)| text)
        .split("<path")
        .skip(1)
        .filter_map(|node| {
            let (header, text) = node.split_once('>')?;
            let (path, _) = text.split_once("</path>")?;
            let action = match attr(header, "action")? {
                "A" => PathAction::Added,
                "D" => PathAction::Deleted,
                "M" => PathAction::Modified,
                "R" => PathAction::Replaced,
                _ => return None,
            };
            let kind = match attr(header, "kind") {
                Some("file") => NodeKind::File,
                Some("dir") => NodeKind::Directory,
                _ => NodeKind::Unknown,
            };
            let copy_from = (|| {
                Some(CopySource {
                    path: xml_text(attr(header, "copyfrom-path")?),
                    revision: attr(header, "copyfrom-rev")?.parse().ok()?,
                })
            })();
            Some(ChangedPath {
                path: xml_text(path),
                action,
                kind,
                copy_from,
            })
        })
}

pub(crate) fn deletion(
    cwd: &Path,
    target: &str,
    path: &str,
    base: Option<u64>,
) -> Result<Option<u64>, String> {
    let range;
    let (options, search) = match base {
        Some(base) => {
            range = format!("{}:HEAD", base.saturating_add(1));
            (&["--verbose", "-r", range.as_str()][..], None)
        }
        None => (&["--verbose"][..], Some(path)),
    };
    let (xml, _) = log_xml(cwd, target, options, 1000, search)?;
    Ok(xml.split("<logentry").skip(1).find_map(|entry| {
        let (header, _) = entry.split_once('>')?;
        changed_paths(entry)
            .any(|change| {
                matches!(change.action, PathAction::Deleted | PathAction::Replaced)
                    && (change.path == path
                        || (base.is_some()
                            && path
                                .strip_prefix(&change.path)
                                .is_some_and(|suffix| suffix.starts_with('/'))))
            })
            .then(|| attr(header, "revision")?.parse().ok())
            .flatten()
    }))
}

/// Read the commit message and changed paths under the selected working-copy directory.
///
/// # Errors
/// Returns the SVN failure or an invalid revision argument.
pub fn revision_detail(root: &Path, revision: &str) -> Result<RevisionDetail, String> {
    let info = checkout_info(root)?;
    detail_at(root, &format!("{}@HEAD", info.url), revision, info, true)
}

pub(crate) fn detail_at(
    cwd: &Path,
    target: &str,
    revision: &str,
    info: Info,
    stop_on_copy: bool,
) -> Result<RevisionDetail, String> {
    let revision = revision.strip_prefix('r').unwrap_or(revision);
    if revision.parse::<u64>().is_err() {
        return Err("Expected a numeric SVN revision.".into());
    }
    let repository_path = info.repository_path()?;
    let mut args = vec!["--xml", "--verbose", "-r", revision];
    if stop_on_copy {
        args.push("--stop-on-copy");
    }
    args.extend(["--", target]);
    let xml = svn(cwd, "log", &args)?;
    let entry = log_entries(&xml).next().ok_or("No such SVN revision.")?;
    let prefix = format!("{}/", repository_path.trim_end_matches('/'));
    let changes = changed_paths(&xml)
        .filter(|change| change.path == repository_path || change.path.starts_with(&prefix))
        .collect();
    Ok(RevisionDetail {
        entry,
        repository_root: info.repository_root,
        repository_path,
        changes,
    })
}

/// Return the checkout URL's decoded path relative to its repository root.
///
/// # Errors
/// Returns the SVN failure or an invalid repository-relative path.
pub fn checkout_repository_path(root: &Path) -> Result<String, String> {
    checkout_info(root)?.repository_path()
}

/// Return the credential-free URL of the current working copy's repository root.
///
/// # Errors
/// Returns the SVN failure or an unsafe URL error.
pub fn checkout_repository_url(root: &Path) -> Result<String, String> {
    Ok(checkout_info(root)?.repository_root)
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
