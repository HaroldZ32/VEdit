//! Typed, read-only SVN inputs for the shared two-file comparison.

use crate::{detect, runner::read_output, Backend};
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
            && (reason.contains("W170000:") || reason.contains("has no pristine version")))
}

impl Target {
    fn new(root: &Path, path: &str) -> Result<Self, String> {
        let (Backend::Svn, Some(working_tree)) = detect(root) else {
            return Err("The project is not an SVN working copy.".into());
        };
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

    fn run(&self, command: &str, options: &[&str], target: &str) -> Result<String, String> {
        let args = [command, "--non-interactive", "--no-auth-cache"]
            .into_iter()
            .chain(options.iter().copied())
            .chain(["--", target])
            .map(str::to_owned)
            .collect::<Vec<_>>();
        let output = read_output("svn", &self.working_tree, &args)?;
        if !output.status.success() {
            return Err(crate::write::redact_url_userinfo(
                String::from_utf8_lossy(&output.stderr).trim(),
            ));
        }
        if output.stdout.len() > 8 * 1024 * 1024 {
            return Err("The SVN result exceeds the 8 MiB comparison limit.".into());
        }
        let text = String::from_utf8(output.stdout).map_err(|_| {
            "The SVN result uses an unsupported encoding. Comparison requires UTF-8 text."
        })?;
        if text.contains('\0') {
            return Err("The SVN result is binary. Comparison requires UTF-8 text.".into());
        }
        Ok(text)
    }

    fn info(&self, item: &str, target: &str) -> Result<String, String> {
        let value = self.run("info", &["--show-item", item, "--no-newline"], target)?;
        if matches!(item, "url" | "repos-root-url")
            && crate::write::redact_url_userinfo(&value) != value
        {
            return Err("SVN repository URL contains embedded user information.".into());
        }
        Ok(value)
    }

    fn url(&self) -> Result<String, String> {
        match self.info("url", &format!("{}@", self.local_path)) {
            Ok(url) => return Ok(url),
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
            match self.info("url", &format!("{parent_text}@")) {
                Ok(url) => {
                    suffix.reverse();
                    return Ok(format!(
                        "{}/{}",
                        url.trim_end_matches('/'),
                        suffix.join("/")
                    ));
                }
                Err(reason) if missing(&reason) && parent != self.working_tree => path = parent,
                Err(reason) => return Err(reason),
            }
        }
        Err("Cannot resolve the SVN repository path.".into())
    }

    fn local_peg(&self) -> Option<u64> {
        self.info("revision", &format!("{}@", self.local_path))
            .ok()?
            .parse()
            .ok()
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
    let cat = |address: &str| target.run("cat", &["--ignore-keywords", "-r", &operative], address);
    let result = match revision {
        Revision::Base => cat(&format!("{}@BASE", target.local_path)),
        Revision::Head => cat(&format!("{}@HEAD", target.url()?)),
        Revision::Number(number) => {
            let url = target.url()?;
            if let Some(peg) = peg {
                cat(&format!("{url}@{peg}"))
            } else if let Some(base) = target.local_peg().filter(|base| *base >= number) {
                cat(&format!("{url}@{base}"))
            } else {
                cat(&format!("{url}@{number}"))
            }
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

fn deleted_revision(target: &Target, url: &str) -> Result<Option<u64>, String> {
    let working_tree = target
        .working_tree
        .to_str()
        .ok_or("The working-copy path is not UTF-8.")?;
    let repository = target.info("repos-root-url", &format!("{working_tree}@"))?;
    let path = url
        .strip_prefix(&repository)
        .filter(|path| path.starts_with('/'))
        .and_then(decode_url_path)
        .ok_or("Cannot resolve the repository-relative SVN URL.")?;
    let pattern = path
        .replace('\\', "\\\\")
        .replace('[', "\\[")
        .replace('*', "\\*")
        .replace('?', "\\?");
    let escaped_path = path
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;");
    let xml = target.run(
        "log",
        &[
            "--xml",
            "--verbose",
            "--search",
            &pattern,
            "--limit",
            "1000",
        ],
        &format!("{repository}@HEAD"),
    )?;
    Ok(xml.split("<logentry").skip(1).find_map(|entry| {
        let deleted = entry.split("<path\n").skip(1).any(|node| {
            node.split_once('>').is_some_and(|(attributes, value)| {
                attributes.contains("action=\"D\"")
                    && value
                        .strip_prefix(&escaped_path)
                        .is_some_and(|rest| rest.starts_with("</path>"))
            })
        });
        deleted.then(|| revision_of(entry)).flatten()
    }))
}

fn revision_of(entry: &str) -> Option<u64> {
    let (header, _) = entry.split_once('>')?;
    header
        .split_once("revision=\"")?
        .1
        .split_once('"')?
        .0
        .parse()
        .ok()
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
    let url = target.url()?;
    let head = format!("{url}@HEAD");
    let (peg, deletion) = match peg {
        Some(peg) => (peg, None),
        None => match target.info("revision", &head) {
            Ok(revision) => (
                revision
                    .parse()
                    .map_err(|_| "SVN returned an invalid peg revision.")?,
                None,
            ),
            Err(reason) if missing(&reason) => match deleted_revision(&target, &url)? {
                Some(deleted) => (deleted.saturating_sub(1), Some(deleted)),
                None => (target.local_peg().ok_or(reason)?, None),
            },
            Err(reason) => return Err(reason),
        },
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
    let result = target.run(
        "log",
        &["--xml", "--limit", &log_limit, "-r", &format!("{upper}:1")],
        &format!("{url}@{peg}"),
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
