//! Project root, `.f` filelists and `.rules.verible_lint` discovery.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use serde::Serialize;

use crate::filelist;
use crate::model::ProjectSettings;
use crate::warnings::{Warning, WarningCode};

/// Name of the project configuration file.
pub const CONFIG_FILE_NAME: &str = "vedit.toml";
/// Name of the Verible lint rule file.
pub const VERIBLE_RULES_FILE_NAME: &str = ".rules.verible_lint";

/// How the project root was found.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RootSource {
    /// A `vedit.toml` marked the root.
    VeditToml,
    /// A `.git` directory marked the root.
    GitDir,
    /// A `.f` filelist held directly by the directory marked the root.
    ///
    /// A folder that ships its own filelist is a project even when it lives
    /// inside a larger checkout: `tests/fixtures/verilog-project` (the
    /// repository-owned acceptance fixture) documents itself as the project
    /// root, and every service resolving that project must agree on it.
    Filelist,
    /// Nothing marked a root; the discovery start directory was used.
    Fallback,
}

/// One preprocessor define.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct Define {
    /// Macro name.
    pub name: String,
    /// Optional value (`NAME=VALUE`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub value: Option<String>,
}

/// One parsed `.f` filelist (nested `-f` files are flattened into it).
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Filelist {
    /// The filelist this entry was discovered from.
    pub path: PathBuf,
    /// Source files it names, absolute, first occurrence wins.
    pub entries: Vec<PathBuf>,
    /// Include directories it contributes, absolute.
    pub include_dirs: Vec<PathBuf>,
    /// Defines it contributes.
    pub defines: Vec<Define>,
}

/// Everything the LSP, deep-lint and FIM lanes need to know about the project.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ProjectContext {
    /// The discovered project root.
    pub root: PathBuf,
    /// How the root was found.
    pub root_source: RootSource,
    /// Where the upward root search stopped.
    pub search_stop: PathBuf,
    /// Every filelist found or configured, sorted by path.
    pub filelists: Vec<Filelist>,
    /// Deduplicated union of configured and filelist include directories.
    pub include_dirs: Vec<PathBuf>,
    /// Deduplicated union of configured and filelist defines.
    pub defines: Vec<Define>,
    /// The nearest `.rules.verible_lint`, if any.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rules_path: Option<PathBuf>,
    /// Where the upward rules search stopped.
    pub rules_search_stop: PathBuf,
}

/// Discover the project context inside an already discovered root.
pub(crate) fn discover_in_root(
    root: PathBuf,
    root_source: RootSource,
    search_stop: PathBuf,
    settings: &ProjectSettings,
    home: Option<&Path>,
    warnings: &mut Vec<Warning>,
) -> ProjectContext {
    let mut filelists = discover_filelists(&root, settings, warnings);
    filelists.sort_by(|left, right| left.path.cmp(&right.path));

    let mut include_dirs: Vec<PathBuf> = Vec::new();
    let mut include_seen: BTreeSet<PathBuf> = BTreeSet::new();
    let mut defines: Vec<Define> = Vec::new();
    let mut define_seen: BTreeSet<Define> = BTreeSet::new();

    for dir in &settings.include_dirs {
        push_unique(
            &mut include_dirs,
            &mut include_seen,
            resolve_relative(&root, dir),
        );
    }
    for entry in &settings.defines {
        match parse_define(entry) {
            Some(define) => push_unique_define(&mut defines, &mut define_seen, define),
            None => warnings.push(Warning::new(
                WarningCode::ConfigInvalidValue,
                "[project] defines entry is not NAME or NAME=VALUE; ignored",
            )),
        }
    }
    for filelist in &filelists {
        for dir in &filelist.include_dirs {
            push_unique(&mut include_dirs, &mut include_seen, dir.clone());
        }
        for define in &filelist.defines {
            push_unique_define(&mut defines, &mut define_seen, define.clone());
        }
    }

    let (rules_path, rules_search_stop) = discover_rules(&root, home, warnings);

    ProjectContext {
        root,
        root_source,
        search_stop,
        filelists,
        include_dirs,
        defines,
        rules_path,
        rules_search_stop,
    }
}

/// Walk up from `start_dir` until a `vedit.toml`, `.git` or direct `.f` marker is found.
///
/// A directory that holds a `.f` filelist directly is a root of its own, so a
/// project opened from its own folder inside a larger git checkout expands that
/// folder's filelists instead of the enclosing repository's (see
/// [`RootSource::Filelist`]).
pub(crate) fn discover_root(start_dir: &Path) -> (PathBuf, RootSource, PathBuf) {
    let start = std::path::absolute(start_dir).unwrap_or_else(|_| start_dir.to_path_buf());
    let mut dir = start.clone();
    loop {
        if dir.join(CONFIG_FILE_NAME).is_file() {
            return (dir.clone(), RootSource::VeditToml, dir);
        }
        if dir.join(".git").exists() {
            return (dir.clone(), RootSource::GitDir, dir);
        }
        if has_direct_filelist(&dir) {
            return (dir.clone(), RootSource::Filelist, dir);
        }
        match dir.parent() {
            Some(parent) => dir = parent.to_path_buf(),
            None => return (start.clone(), RootSource::Fallback, dir),
        }
    }
}

/// Does `dir` itself (not a descendant) hold a `.f` filelist?
fn has_direct_filelist(dir: &Path) -> bool {
    let Ok(read_dir) = std::fs::read_dir(dir) else {
        return false;
    };
    read_dir
        .filter_map(Result::ok)
        .any(|entry| entry.path().is_file() && has_f_extension(&entry.path()))
}

/// Find the nearest `.rules.verible_lint` at or above `root`.
///
/// The search stops at `$HOME` when `root` lives under it, otherwise at the
/// filesystem root, so it can never walk out of the user's tree.
pub(crate) fn discover_rules(
    root: &Path,
    home: Option<&Path>,
    warnings: &mut Vec<Warning>,
) -> (Option<PathBuf>, PathBuf) {
    let boundary = match home {
        Some(home) if root.starts_with(home) => home.to_path_buf(),
        _ => PathBuf::from("/"),
    };
    let mut dir = root.to_path_buf();
    loop {
        let candidate = dir.join(VERIBLE_RULES_FILE_NAME);
        if candidate.is_file() {
            return (Some(candidate), dir);
        }
        if dir == boundary {
            break;
        }
        match dir.parent() {
            Some(parent) => dir = parent.to_path_buf(),
            None => break,
        }
    }
    let stop = dir;
    warnings.push(
        Warning::new(
            WarningCode::RulesSearchStopped,
            format!(
                "no {VERIBLE_RULES_FILE_NAME} found from the project root upward; search stopped at {}",
                stop.display()
            ),
        )
        .with_file(stop.clone()),
    );
    (None, stop)
}

fn discover_filelists(
    root: &Path,
    settings: &ProjectSettings,
    warnings: &mut Vec<Warning>,
) -> Vec<Filelist> {
    let mut candidates: Vec<PathBuf> = Vec::new();
    for configured in &settings.filelists {
        let path = resolve_relative(root, configured);
        if path.is_file() {
            candidates.push(path);
        } else {
            warnings.push(
                Warning::new(
                    WarningCode::FilelistMissing,
                    "configured filelist does not exist; ignored",
                )
                .with_file(path),
            );
        }
    }
    match std::fs::read_dir(root) {
        Ok(read_dir) => {
            let mut scanned: Vec<PathBuf> = read_dir
                .filter_map(Result::ok)
                .map(|entry| entry.path())
                .filter(|path| path.is_file() && has_f_extension(path))
                .collect();
            scanned.sort();
            candidates.extend(scanned);
        }
        Err(_) => warnings.push(
            Warning::new(
                WarningCode::FilelistMissing,
                "project root could not be read; no filelist was auto-discovered",
            )
            .with_file(root.to_path_buf()),
        ),
    }

    let mut seen: BTreeSet<PathBuf> = BTreeSet::new();
    let mut parsed: Vec<Filelist> = Vec::new();
    for candidate in candidates {
        let key = std::fs::canonicalize(&candidate).unwrap_or_else(|_| candidate.clone());
        if !seen.insert(key) {
            continue;
        }
        if let Some(filelist) = filelist::parse(&candidate, warnings) {
            parsed.push(filelist);
        }
    }
    parsed
}

fn has_f_extension(path: &Path) -> bool {
    path.extension()
        .and_then(std::ffi::OsStr::to_str)
        .is_some_and(|ext| ext.eq_ignore_ascii_case("f"))
}

fn resolve_relative(base: &Path, path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        base.join(path)
    }
}

fn push_unique(target: &mut Vec<PathBuf>, seen: &mut BTreeSet<PathBuf>, value: PathBuf) {
    if seen.insert(value.clone()) {
        target.push(value);
    }
}

fn push_unique_define(target: &mut Vec<Define>, seen: &mut BTreeSet<Define>, value: Define) {
    if seen.insert(value.clone()) {
        target.push(value);
    }
}

/// Parse `NAME` or `NAME=VALUE` into a define. Returns `None` when the name is
/// not a legal preprocessor identifier.
pub(crate) fn parse_define(raw: &str) -> Option<Define> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return None;
    }
    let (name, value) = match trimmed.split_once('=') {
        Some((name, value)) => (name.trim(), Some(value.trim().to_string())),
        None => (trimmed, None),
    };
    if !is_identifier(name) {
        return None;
    }
    Some(Define {
        name: name.to_string(),
        value,
    })
}

fn is_identifier(text: &str) -> bool {
    let mut chars = text.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    if !(first.is_ascii_alphabetic() || first == '_') {
        return false;
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

#[cfg(test)]
mod tests {
    use super::{discover_root, parse_define, Define, RootSource};
    use crate::test_support::write;

    #[test]
    fn define_parsing() {
        assert_eq!(
            parse_define("FOO"),
            Some(Define {
                name: "FOO".to_string(),
                value: None
            })
        );
        assert_eq!(
            parse_define("FOO=1"),
            Some(Define {
                name: "FOO".to_string(),
                value: Some("1".to_string())
            })
        );
        assert_eq!(parse_define("1BAD=1"), None);
        assert_eq!(parse_define(""), None);
    }

    #[test]
    fn root_discovery_prefers_vedit_toml_then_git() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        let nested = root.join("a/b");
        std::fs::create_dir_all(&nested)?;
        std::fs::create_dir_all(root.join(".git"))?;
        let (found, source, _stop) = discover_root(&nested);
        assert_eq!(found, root, "the .git marker defines the root");
        assert_eq!(source, RootSource::GitDir);
        write(&root.join("vedit.toml"), "[ui]\n")?;
        let (found, source, _stop) = discover_root(&nested);
        assert_eq!(found, root);
        assert_eq!(source, RootSource::VeditToml);
        Ok(())
    }

    #[test]
    fn root_discovery_falls_back_to_the_start_directory() -> Result<(), Box<dyn std::error::Error>>
    {
        let temp = tempfile::tempdir()?;
        let absolute = std::path::absolute(temp.path())?;
        let (found, source, stop) = discover_root(temp.path());
        assert_eq!(source, RootSource::Fallback);
        assert_eq!(found, absolute);
        assert!(stop.starts_with("/"));
        Ok(())
    }

    /// The counter-example for the `.f` marker: a project folder nested inside a
    /// git checkout must resolve to itself when opened directly, while the outer
    /// checkout must keep resolving to the outer root (a `.f` deeper in the tree
    /// never re-roots the walk).
    #[test]
    fn a_direct_filelist_roots_its_own_directory_but_never_the_outer_checkout(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let outer = temp.path();
        std::fs::create_dir_all(outer.join(".git"))?;
        let inner = outer.join("nested-project");
        std::fs::create_dir_all(&inner)?;
        write(&inner.join("project.f"), "rtl/a.sv\n")?;
        let inner_absolute = std::path::absolute(&inner)?;

        // From the outer checkout root: the .f lives one level down, so it is
        // not a marker on the way up — the outer root wins.
        let (found, source, _stop) = discover_root(outer);
        assert_eq!(
            found,
            std::path::absolute(outer)?,
            "a nested .f must not move the root when starting at the outer checkout"
        );
        assert_eq!(source, RootSource::GitDir);

        // From the nested folder itself: its own filelist marks the root.
        let (found, source, _stop) = discover_root(&inner);
        assert_eq!(found, inner_absolute);
        assert_eq!(source, RootSource::Filelist);

        // A directory holding BOTH markers keeps the same root; git stays the
        // reported source because it is the stronger marker.
        let both = outer.join("repo-with-filelist");
        std::fs::create_dir_all(both.join(".git"))?;
        write(&both.join("top.f"), "rtl/b.sv\n")?;
        let (found, source, _stop) = discover_root(&both);
        assert_eq!(found, std::path::absolute(&both)?);
        assert_eq!(source, RootSource::GitDir);
        Ok(())
    }
}
