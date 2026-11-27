//! Typed, read-only SVN inputs for the shared two-file comparison.

use crate::{detect, svn_read, Backend};
use std::path::{Component, Path, PathBuf};

/// The requested operative revision of a file.
#[derive(Clone, Copy)]
pub enum Revision {
    /// Pristine content of the working-copy node.
    Base,
    /// Latest repository revision.
    Head,
    /// An explicit repository revision, including r0.
    Number(u64),
}

impl Revision {
    fn argument(self) -> String {
        match self {
            Self::Base => "BASE".to_owned(),
            Self::Head => "HEAD".to_owned(),
            Self::Number(number) => number.to_string(),
        }
    }
}

/// SVN file log, newest change first. The peg anchors historical path lookup.
pub struct FileHistory {
    /// Existing revision at which the file name is resolved.
    pub peg: u64,
    /// Revisions that changed the file, following copy history.
    pub revisions: Vec<u64>,
    /// Author, date and message for the returned revisions where available.
    pub entries: Vec<crate::svn_read::LogEntry>,
}

struct Target {
    working_tree: PathBuf,
    local_path: String,
}

pub(crate) fn missing(reason: &str) -> bool {
    ["E195012:", "W160013:", "E160013:", "W155010:", "E155010:"]
        .iter()
        .any(|code| reason.contains(code))
        || (reason.contains("E200009:")
            && (reason.contains("W170000:")
                || reason.contains("has no pristine version")
                || reason.contains("has no base revision until it is committed")))
}

impl Target {
    fn new(root: &Path, path: &str) -> Result<Self, String> {
        let (Backend::Svn, Some(working_tree)) = detect(root) else {
            return Err("The project is not an SVN working copy.".into());
        };
        let working_tree = std::path::absolute(working_tree).map_err(|error| error.to_string())?;
        let full = if Path::new(path).is_absolute() {
            PathBuf::from(path)
        } else {
            working_tree.join(path)
        };
        let relative = full
            .strip_prefix(&working_tree)
            .map_err(|_| "The file is outside the SVN working copy.")?;
        if relative.as_os_str().is_empty()
            || !relative
                .components()
                .all(|part| matches!(part, Component::Normal(_)))
        {
            return Err("Expected an SVN file path inside the working copy.".into());
        }
        let path_text = relative.to_str().ok_or("The SVN path is not UTF-8.")?;
        crate::svn_write::scoped_path(&working_tree, path_text)
            .map_err(|error| error.message().to_owned())?;
        let local_path = full
            .into_os_string()
            .into_string()
            .map_err(|_| "The SVN path is not UTF-8.")?;
        Ok(Self {
            working_tree,
            local_path,
        })
    }

    fn info(&self, target: &str) -> Result<svn_read::Info, String> {
        svn_read::info(&self.working_tree, target)
    }

    fn location(&self) -> Result<svn_read::Info, String> {
        match self.info(&format!("{}@", self.local_path)) {
            Ok(info) => return Ok(info),
            Err(reason) if missing(&reason) => (),
            Err(reason) => return Err(reason),
        }
        let mut suffix = Vec::new();
        let mut path = Path::new(&self.local_path);
        while let (Some(parent), Some(name)) = (path.parent(), path.file_name()) {
            let Some(name) = name.to_str() else { break };
            suffix.push(encode_path_segment(name));
            let Some(parent_text) = parent.to_str() else {
                break;
            };
            match self.info(&format!("{parent_text}@")) {
                Ok(parent) => {
                    suffix.reverse();
                    return Ok(svn_read::Info {
                        url: format!("{}/{}", parent.url.trim_end_matches('/'), suffix.join("/")),
                        repository_root: parent.repository_root,
                        revision: None,
                    });
                }
                Err(reason) if missing(&reason) && parent != self.working_tree => path = parent,
                Err(reason) => return Err(reason),
            }
        }
        Err("Cannot resolve the SVN repository path.".into())
    }
}

pub(crate) fn encode_path_segment(segment: &str) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut result = String::with_capacity(segment.len());
    for byte in segment.bytes() {
        if byte.is_ascii_alphanumeric() || b"-_.~".contains(&byte) {
            result.push(char::from(byte));
        } else {
            result.push('%');
            result.push(char::from(HEX[usize::from(byte >> 4)]));
            result.push(char::from(HEX[usize::from(byte & 15)]));
        }
    }
    result
}

/// Read one snapshot. `None` means the node does not exist at that revision.
///
/// # Errors
/// Returns the actual command failure when the read could not be performed.
pub fn snapshot(
    root: &Path,
    path: &str,
    revision: Revision,
    peg: Option<u64>,
) -> Result<Option<String>, String> {
    let target = Target::new(root, path)?;
    let operative = revision.argument();
    let cat = |address: &str| svn_read::cat(&target.working_tree, &operative, address);
    let result = match revision {
        Revision::Base => cat(&format!("{}@BASE", target.local_path)),
        Revision::Head => cat(&format!("{}@HEAD", target.location()?.url)),
        Revision::Number(number) => {
            let info = target.location()?;
            let peg = peg
                .or(info.revision.filter(|base| *base >= number))
                .unwrap_or(number);
            cat(&format!("{}@{peg}", info.url))
        }
    };
    match result {
        Ok(text) => Ok(Some(text)),
        Err(reason) if missing(&reason) => Ok(None),
        Err(reason) => Err(reason),
    }
}

pub(crate) fn decode_url_path(path: &str) -> Option<String> {
    let mut decoded = Vec::with_capacity(path.len());
    let mut remaining = path.as_bytes();
    while let Some((byte, rest)) = remaining.split_first() {
        remaining = rest;
        if *byte == b'%' {
            let (escape, rest) = remaining.split_at_checked(2)?;
            decoded.push(u8::from_str_radix(std::str::from_utf8(escape).ok()?, 16).ok()?);
            remaining = rest;
        } else {
            decoded.push(*byte);
        }
    }
    String::from_utf8(decoded).ok()
}

fn follows_base(cwd: &Path, file: &svn_read::Info, peg: u64) -> Result<bool, String> {
    let Some(base) = file.revision else {
        return Ok(true);
    };
    if peg == base {
        return Ok(true);
    }
    match svn_read::info_at(cwd, &format!("{}@{peg}", file.url), base) {
        Ok(ancestor) => Ok(ancestor.url == file.url),
        Err(reason) if missing(&reason) => Ok(false),
        Err(reason) => Err(reason),
    }
}

fn history_peg(target: &Target, file: &svn_read::Info) -> Result<(u64, Option<u64>), String> {
    let cwd = &target.working_tree;
    let unavailable = match target.info(&format!("{}@HEAD", file.url)) {
        Ok(info) => {
            let peg = info
                .revision
                .ok_or("SVN returned an invalid peg revision.")?;
            if follows_base(cwd, file, peg)? {
                return Ok((peg, None));
            }
            "The SVN path at HEAD is unrelated to the working-copy node.".into()
        }
        Err(reason) if missing(&reason) => reason,
        Err(reason) => return Err(reason),
    };
    let deletion = svn_read::deletion(
        cwd,
        &format!("{}@HEAD", file.repository_root),
        &file.repository_path()?,
        file.revision,
    )?;
    if let Some(deleted) = deletion {
        let peg = deleted.saturating_sub(1);
        if follows_base(cwd, file, peg)? {
            return Ok((peg, Some(deleted)));
        }
    }
    file.revision.map(|base| (base, None)).ok_or(unavailable)
}

/// Read the most recent file changes or the changes at or before `upper`.
///
/// # Errors
/// Returns the actual command failure, including a missing or inaccessible peg.
pub fn history(
    root: &Path,
    path: &str,
    upper: Option<u64>,
    peg: Option<u64>,
    limit: usize,
) -> Result<FileHistory, String> {
    let target = Target::new(root, path)?;
    let file = target.location()?;
    let (peg, deletion) = match peg {
        Some(peg) => (peg, None),
        None => history_peg(&target, &file)?,
    };
    let first = deletion.filter(|deleted| upper.is_none_or(|bound| bound >= *deleted));
    let upper = upper.unwrap_or(peg).min(peg);
    if upper == 0 {
        return Ok(FileHistory {
            peg,
            revisions: Vec::new(),
            entries: Vec::new(),
        });
    }
    let limit = limit.clamp(1, 200);
    if limit == 1 && first.is_some() {
        return Ok(FileHistory {
            peg,
            revisions: first.into_iter().collect(),
            entries: Vec::new(),
        });
    }
    let log_limit = (limit - usize::from(first.is_some())).to_string();
    let result = svn_read::svn(
        &target.working_tree,
        "log",
        &[
            "--xml",
            "--limit",
            &log_limit,
            "-r",
            &format!("{upper}:1"),
            "--",
            &format!("{}@{peg}", file.url),
        ],
    );
    let entries: Vec<_> = match result {
        Ok(xml) => crate::svn_read::log_entries(&xml).collect(),
        Err(reason) if missing(&reason) && upper < peg => Vec::new(),
        Err(reason) => return Err(reason),
    };
    let revisions = first
        .into_iter()
        .chain(entries.iter().map(|entry| entry.revision))
        .collect();
    Ok(FileHistory {
        peg,
        revisions,
        entries,
    })
}
