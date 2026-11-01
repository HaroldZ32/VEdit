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
/// A MISSING rules file is normal: verible falls back to its built-in rule set,
/// so it produces **no diagnostic at all** (T114 — "environment not configured"
/// is not an error, the same rule T85 applied to a missing toolchain).
///
/// Only a rules file that EXISTS but cannot be used (permissions, I/O error, or
/// the name is not a regular file) is reported, as
/// [`WarningCode::RulesUnreadable`]. That message never contains the user's
/// absolute path: it names the file relative to the project root, or just the
/// file name when it lives above the root, and the [`Warning::file`] field is
/// left empty for the same reason.
///
/// The search stops at `$HOME` when `root` lives under it, otherwise at the
/// filesystem root, so it can never walk out of the user's tree. That boundary
/// is a deliberate safety limit, not a bug.
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
        if candidate.exists() {
            // Exists: usable only when it is a regular file we can open. Anything
            // else is an actionable problem the user has to fix.
            if candidate.is_file() && std::fs::File::open(&candidate).is_ok() {
                return (Some(candidate), dir);
            }
            warnings.push(Warning::new(
                WarningCode::RulesUnreadable,
                unreadable_rules_message(root, &candidate),
            ));
            return (None, dir);
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
    // No warning: the optional file is simply absent (built-in rules apply).
    (None, stop)
}

/// The short, path-free sentence for an unusable rules file.
fn unreadable_rules_message(root: &Path, candidate: &Path) -> String {
    let shown = candidate
        .strip_prefix(root)
        .map(|relative| relative.display().to_string())
        .unwrap_or_else(|_| VERIBLE_RULES_FILE_NAME.to_string());
    // Say what it means for the user: the feature is still working with the
    // built-in rules, only their project-specific rules are not applied (T85's
    // lesson: explain the impact, not just the error).
    format!(
        "the Verilog lint rules file {shown} could not be read; falling back to \
         built-in rules"
    )
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
    use super::{
        discover_root, discover_rules, parse_define, Define, RootSource, VERIBLE_RULES_FILE_NAME,
    };
    use crate::test_support::write;
    use crate::warnings::WarningCode;

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

    // --- T114: a missing optional rules file is not a diagnostic ------------

    /// The regression the real-machine GUI reported: no `.rules.verible_lint`
    /// anywhere produced an Error-looking line with the user's home directory.
    /// A missing optional file is now completely silent (built-in rules apply).
    #[test]
    fn missing_optional_rules_file_is_silent() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let mut warnings = Vec::new();
        let (found, stop) = discover_rules(temp.path(), None, &mut warnings);
        assert_eq!(found, None, "nothing to find");
        assert!(
            warnings.is_empty(),
            "a missing optional rules file must not produce a warning: {warnings:?}"
        );
        assert!(stop.starts_with("/"), "the walk ended at a filesystem path");
        Ok(())
    }

    /// Exists-but-unusable is a REAL problem and still reported, but the message
    /// must stay actionable and path-free (no `/home/<user>`), and it must say
    /// that the feature keeps working with the built-in rules.
    #[test]
    fn unreadable_rules_file_is_reported_without_an_absolute_path(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        let rules = root.join(VERIBLE_RULES_FILE_NAME);
        // A directory with that name is "exists but not a readable regular file".
        // It is deterministic even in a root container, where chmod 000 would
        // still be readable.
        std::fs::create_dir_all(&rules)?;

        let mut warnings = Vec::new();
        let (found, _stop) = discover_rules(root, None, &mut warnings);
        assert_eq!(found, None);
        assert_eq!(warnings.len(), 1, "one actionable warning: {warnings:?}");
        let warning = &warnings[0];
        assert_eq!(warning.code, WarningCode::RulesUnreadable);
        assert!(
            warning.file.is_none(),
            "the file field must not carry an absolute path"
        );
        assert!(
            warning.message.contains("could not be read"),
            "the reason must be visible: {}",
            warning.message
        );
        assert!(
            warning.message.contains("falling back to built-in rules"),
            "the message must say the feature still works: {}",
            warning.message
        );
        let absolute = root.display().to_string();
        assert!(
            !warning.message.contains(&absolute),
            "the message leaks the project path: {}",
            warning.message
        );
        assert!(
            !warning.message.contains("/home/"),
            "the message leaks a home directory: {}",
            warning.message
        );
        Ok(())
    }

    /// The positive control: a real rules file is adopted and produces no noise
    /// (no "found" message either).
    #[test]
    fn a_present_rules_file_is_found_without_warnings() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join(VERIBLE_RULES_FILE_NAME), "line-length: 120\n")?;
        let mut warnings = Vec::new();
        let (found, stop) = discover_rules(root, None, &mut warnings);
        assert_eq!(found, Some(root.join(VERIBLE_RULES_FILE_NAME)));
        assert_eq!(stop, root);
        assert!(
            warnings.is_empty(),
            "no noise for a found file: {warnings:?}"
        );
        Ok(())
    }

    /// `chmod 000` coverage. In the pinned root container the file stays
    /// readable, so the test asserts the readable branch; on a normal account it
    /// asserts the unreadable branch. Either way something is asserted, never
    /// silently skipped.
    #[test]
    fn chmod_000_rules_file_hits_one_of_the_two_expectations(
    ) -> Result<(), Box<dyn std::error::Error>> {
        use std::os::unix::fs::PermissionsExt;
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        let rules = root.join(VERIBLE_RULES_FILE_NAME);
        write(&rules, "line-length: 120\n")?;
        std::fs::set_permissions(&rules, std::fs::Permissions::from_mode(0o000))?;
        let readable_by_us = std::fs::File::open(&rules).is_ok();

        let mut warnings = Vec::new();
        let (found, _stop) = discover_rules(root, None, &mut warnings);
        std::fs::set_permissions(&rules, std::fs::Permissions::from_mode(0o644))?;
        println!(
            "chmod-000 readable_by_this_process={readable_by_us} found={:?} warnings={:?}",
            found.is_some(),
            warnings.iter().map(|w| w.code.as_str()).collect::<Vec<_>>()
        );
        if readable_by_us {
            assert_eq!(found, Some(rules), "readable => adopted");
            assert!(warnings.is_empty());
        } else {
            assert_eq!(found, None);
            assert_eq!(warnings.len(), 1);
            assert_eq!(warnings[0].code, WarningCode::RulesUnreadable);
        }
        Ok(())
    }

    /// The `$HOME` boundary is a deliberate safety limit: the walk stops there
    /// and never reaches `/`, and a miss is silent.
    #[test]
    fn rules_search_stops_at_the_home_boundary() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let home = temp.path().join("home");
        let project = home.join("work/project");
        std::fs::create_dir_all(&project)?;
        let mut warnings = Vec::new();
        let (found, stop) = discover_rules(&project, Some(home.as_path()), &mut warnings);
        assert_eq!(found, None);
        assert_eq!(stop, home, "the search must stop at $HOME, not at /");
        assert!(warnings.is_empty());
        Ok(())
    }
}
