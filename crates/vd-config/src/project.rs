//! Project root, `.f` filelists and `.rules.verible_lint` discovery.
//!
//! The file set is the **declared** filelists' expansion, not a directory
//! crawl: `[project] filelists` and the imported `.f` files always apply, while
//! the root-level `*.f` auto-discovery and the recursive source scan are the
//! legacy behaviour behind [`ProjectSettings::auto_discover`] (default off).

use std::collections::{BTreeMap, BTreeSet};
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
    /// Every filelist that contributed: the configured `[project] filelists`,
    /// plus the root-level `.f` files when [`ProjectSettings::auto_discover`]
    /// is on; sorted by path.
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

/// Variable lookup for the filelist preprocessor (ADDENDUM A-9).
///
/// Three layers, most specific first: the project's own persisted table, the
/// IMPORTED environment selection (T10 / AC-4.7 — global first, then the
/// project's entries, so the project layer of the selection wins), and finally
/// the process environment. `HOME` feeds the leading `~` form. Expansion is
/// deterministic for a given table + imported layer + environment.
#[derive(Debug, Clone, Default)]
pub struct Variables {
    project: BTreeMap<String, String>,
    /// The imported selection, already merged by
    /// [`crate::env::EnvironmentSelection::merged_for`] (project wins over
    /// global). T10 seam 2: this is the layer `.f` variables get from the
    /// Settings → Environment import, between the project table and the process
    /// environment.
    imported: BTreeMap<String, String>,
    environment: BTreeMap<String, String>,
    home: Option<PathBuf>,
}

impl Variables {
    /// The persisted `NAME=VALUE` project table plus the process environment.
    ///
    /// No imported layer: the legacy form (the loader's first pass, tests that
    /// do not care about imports). The product uses [`Self::from_layers`].
    #[must_use]
    pub fn from_pairs(project: &[(String, String)]) -> Self {
        Self::from_layers(project, std::iter::empty())
    }

    /// The product form (T10 seam 2): the project table, the merged imported
    /// layer, and the process environment.
    ///
    /// `imported` is the output of
    /// [`crate::env::EnvironmentSelection::merged_for`] (or any equal map): the
    /// resolution order is project > imported > process.
    #[must_use]
    pub fn from_layers(
        project: &[(String, String)],
        imported: impl IntoIterator<Item = (String, String)>,
    ) -> Self {
        Self {
            project: project.iter().cloned().collect(),
            imported: imported.into_iter().collect(),
            environment: std::env::vars().collect(),
            home: std::env::var_os("HOME").map(PathBuf::from),
        }
    }

    /// Explicit environment/home (tests and embedding; never reads the process).
    #[must_use]
    pub fn with_environment(
        project: &[(String, String)],
        environment: impl IntoIterator<Item = (String, String)>,
        home: Option<PathBuf>,
    ) -> Self {
        Self::with_layers(project, std::iter::empty(), environment, home)
    }

    /// The explicit three-layer form (tests and embedding; never reads the
    /// process). Resolution order: project > imported > environment.
    #[must_use]
    pub fn with_layers(
        project: &[(String, String)],
        imported: impl IntoIterator<Item = (String, String)>,
        environment: impl IntoIterator<Item = (String, String)>,
        home: Option<PathBuf>,
    ) -> Self {
        Self {
            project: project.iter().cloned().collect(),
            imported: imported.into_iter().collect(),
            environment: environment.into_iter().collect(),
            home,
        }
    }

    /// A copy of the imported layer (the filelist diagnostics can name it).
    #[must_use]
    pub fn imported(&self) -> &BTreeMap<String, String> {
        &self.imported
    }

    /// Project table first, then the imported selection, then the environment.
    #[must_use]
    pub fn resolve(&self, name: &str) -> Option<&str> {
        self.resolve_with_layer(name).map(|(value, _)| value)
    }

    /// [`Self::resolve`] plus the layer that answered (batch-20261114 E1).
    #[must_use]
    pub fn resolve_with_layer(&self, name: &str) -> Option<(&str, VariableLayer)> {
        if let Some(value) = self.project.get(name) {
            return Some((value.as_str(), VariableLayer::Project));
        }
        if let Some(value) = self.imported.get(name) {
            return Some((value.as_str(), VariableLayer::Imported));
        }
        self.environment
            .get(name)
            .map(|value| (value.as_str(), VariableLayer::Environment))
    }

    #[must_use]
    pub fn home(&self) -> Option<&Path> {
        self.home.as_deref()
    }
}

/// Which layer satisfied one filelist variable reference (batch-20261114 E1).
///
/// The order is the resolution order, most specific first, so
/// `Project < Imported < Environment < Missing` when the variants are compared.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum VariableLayer {
    /// The project's own persisted `NAME=VALUE` table.
    Project,
    /// The imported environment selection (Settings → Environment).
    Imported,
    /// The process environment.
    Environment,
    /// No layer has the name; the line that referenced it was skipped.
    Missing,
}

impl VariableLayer {
    /// The stable lower-case spelling the C ABI carries.
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Project => "project",
            Self::Imported => "imported",
            Self::Environment => "environment",
            Self::Missing => "missing",
        }
    }
}

/// One variable name a filelist referenced, and the layer that satisfied it
/// (batch-20261114 E1). `name` is the spelling the `.f` used, without `$`/`{}`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ReferencedVariable {
    /// The referenced name (`A` for `$A`, `${A}`, `$(A)`).
    pub name: String,
    /// The layer that answered [`Variables::resolve`].
    pub layer: VariableLayer,
}

/// Discover the project context inside an already discovered root using only
/// the process environment for filelist variables (the legacy path).
pub(crate) fn discover_in_root(
    root: PathBuf,
    root_source: RootSource,
    search_stop: PathBuf,
    settings: &ProjectSettings,
    home: Option<&Path>,
    warnings: &mut Vec<Warning>,
) -> ProjectContext {
    let variables = Variables::from_pairs(&[]);
    let mut missing = BTreeSet::new();
    let mut referenced = BTreeMap::new();
    let mut skipped = 0usize;
    discover_in_root_with(
        root,
        root_source,
        search_stop,
        settings,
        home,
        warnings,
        &variables,
        &mut missing,
        &mut referenced,
        &mut skipped,
    )
}

/// Discover the project context inside an already discovered root, expanding
/// the filelist variables from `variables` (ADDENDUM A-9).
#[allow(clippy::too_many_arguments)]
pub(crate) fn discover_in_root_with(
    root: PathBuf,
    root_source: RootSource,
    search_stop: PathBuf,
    settings: &ProjectSettings,
    home: Option<&Path>,
    warnings: &mut Vec<Warning>,
    variables: &Variables,
    missing: &mut BTreeSet<String>,
    referenced: &mut BTreeMap<String, VariableLayer>,
    skipped: &mut usize,
) -> ProjectContext {
    let mut filelists = discover_filelists(
        root.as_path(),
        settings,
        warnings,
        variables,
        missing,
        referenced,
        skipped,
    );
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

/// Source-file extensions the directory scan collects (AC-2.2).
///
/// A PROJECTION of [`crate::language::LANGUAGES`] for documentation/readers;
/// the scan itself asks the registry (`language::supports(.., Source)`), and the
/// registry tests pin this list so the two can never drift.
pub const SCANNED_EXTENSIONS: [&str; 4] = ["v", "sv", "vh", "svh"];

/// One fully expanded project: the declared filelists (and the legacy scan in
/// `auto_discover` mode), plus the include directories and defines the
/// backends receive.
///
/// This is the project model of C7: the root, the imported `.f` list, the
/// expansion, and the include dirs and defines. The file set is
/// `expand(filelists)` — the declared filelists alone — with duplicates
/// removed; only the explicit [`ProjectSettings::auto_discover`] switch adds
/// the legacy `scan(root)` union.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ProjectExpansion {
    /// The folder the user opened; never an ancestor of it (AC-2.1).
    pub root: PathBuf,
    /// How configuration discovery found the configuration layers. When the
    /// opened folder is pinned as the root this is [`RootSource::Fallback`].
    pub root_source: RootSource,
    /// Every filelist that contributed: configured ones first (`[project]
    /// filelists`, plus the root-level `.f` files when `auto_discover` is on),
    /// then the imported ones, first occurrence wins.
    pub filelists: Vec<Filelist>,
    /// The file set: filelist entries in filelist order, then — only with
    /// `auto_discover` — the sorted scan; duplicates removed.
    pub files: Vec<PathBuf>,
    /// Include directories (`-I`): configuration and `+incdir+` first, then the
    /// extra (tools-page) directories appended, duplicates removed.
    pub include_dirs: Vec<PathBuf>,
    /// Defines (`-D`): configuration and `+define+` first, then the extra
    /// (tools-page) defines appended, duplicates removed.
    pub defines: Vec<Define>,
    /// Non-fatal diagnostics, each naming the `.f` file and 1-based line that
    /// caused it where a line exists.
    pub warnings: Vec<Warning>,
    /// Variable names still unresolved after the project table + environment
    /// (ADDENDUM A-9), unique and sorted. The import dialog fills all of them in
    /// one pass.
    pub missing_variables: Vec<String>,
    /// Every variable name the expansion referenced, unique and sorted by name,
    /// each with the layer that satisfied it (batch-20261114 E1). This is the
    /// "referenced" set, not the "available" one: a name no `.f` line mentions
    /// never appears, and the `Missing` entries are exactly the names in
    /// [`Self::missing_variables`] — kept as a separate field so the existing
    /// meaning of `missing_variables` does not change.
    pub referenced_variables: Vec<ReferencedVariable>,
    /// Lines that produced no entry (unresolved/!invalid variable, unknown
    /// token, malformed define, missing/cyclic nested `-f`). The panel shows a
    /// non-zero counter whenever this is non-zero, so a dropped file is visible.
    pub skipped_lines: usize,
}

/// Expand the project rooted at `root` (AC-2.2/AC-2.4).
///
/// * The **opened folder is the root**: `Loader`'s upward discovery is used only
///   for its configuration layers, never to move the root above the folder the
///   user opened (AC-2.1 — otherwise opening `repo/rtl` inside a git checkout
///   would silently expand `repo`).
/// * The `[project]` table is **project membership, not a preference** (RM-2):
///   a `vedit.toml` that lives ABOVE the opened folder does not contribute its
///   `filelists` / `include_dirs` / `defines` to a folder that is not that
///   project. Its other layers (UI, editor, tool paths) still apply, and a
///   user-level `[project]` table still applies when no enclosing `vedit.toml`
///   exists. Discovery is re-run from the new root on every expansion, so a
///   project switch cannot inherit the previous project's merge state.
/// * `imported_filelists` are additional `.f` files the user imported; each is
///   parsed by [`filelist::parse`] with the same diagnostics as a discovered
///   one, and a `.f` that is both discovered and imported is parsed once.
/// * `extra_include_dirs` / `extra_defines` are the tools-page values
///   (`tools.include_dirs` / `tools.defines` in `settings.toml`). They are
///   **appended** to what the project itself declares and de-duplicated, so the
///   page can add an include path without losing the filelist's own.
/// * The file set is what the **declared filelists** name: the imported `.f`
///   files plus `[project] filelists`, in filelist order, de-duplicated by
///   canonical path. A `.f` sitting in the root that nobody declared is NOT
///   part of it, and neither is a source file the filelists do not mention
///   (the 2026-11-18 file-set decision: the user's declaration is the project).
///   Setting `[project] auto_discover = true` restores the legacy file set —
///   `.f` auto-discovery plus a recursive scan of `root` for
///   [`SCANNED_EXTENSIONS`] — exactly as it was before that decision.
///
/// Everything here is filesystem work, so it runs inside the C ABI's worker job,
/// never on the GUI thread (C4).
pub fn expand(
    root: &Path,
    imported_filelists: &[PathBuf],
    extra_include_dirs: &[PathBuf],
    extra_defines: &[String],
) -> anyhow::Result<ProjectExpansion> {
    expand_with_variables(
        root,
        imported_filelists,
        extra_include_dirs,
        extra_defines,
        &Variables::from_pairs(&[]),
    )
}

/// [`expand`] with an explicit filelist variable table (ADDENDUM A-9: the
/// persisted project table wins over the environment).
#[allow(clippy::too_many_arguments)]
pub fn expand_with_variables(
    root: &Path,
    imported_filelists: &[PathBuf],
    extra_include_dirs: &[PathBuf],
    extra_defines: &[String],
    variables: &Variables,
) -> anyhow::Result<ProjectExpansion> {
    let loaded = crate::Loader::new(root).load()?;
    let crate::Loaded {
        config,
        project: discovered,
        warnings: loader_warnings,
        ..
    } = loaded;
    // `Loader` parsed the discovered `.f` files with the ENVIRONMENT only, so
    // its filelist/rules warnings are the wrong ones when a project variable
    // table exists (a `$VAR` the table resolves was reported as unresolved).
    // Discovery is re-run below with the table; keep only the unrelated layers
    // (xdg/config), which are not re-derived here.
    let mut warnings: Vec<Warning> = loader_warnings
        .into_iter()
        .filter(|warning| {
            let filelist_scoped = warning
                .file
                .as_deref()
                .and_then(std::path::Path::extension)
                .and_then(std::ffi::OsStr::to_str)
                .is_some_and(|extension| extension.eq_ignore_ascii_case("f"));
            !filelist_scoped && !warning.code.as_str().starts_with("rules.")
        })
        .collect();

    let root = root.to_path_buf();
    let home = std::env::var_os("HOME").map(PathBuf::from);
    // The opened folder wins over the upward discovery (AC-2.1). Discovery is
    // re-run WITH the variable table so `$VAR` inside a discovered `.f` expands
    // too (the Loader parsed it with the environment only).
    let (root_source, search_stop) = if discovered.root == root {
        (discovered.root_source, discovered.search_stop)
    } else {
        (RootSource::Fallback, root.clone())
    };
    // RM-2: the `[project]` table is PROJECT MEMBERSHIP, not a preference.
    //
    // `Loader` walks UP from the opened folder, so opening `repo/rtl` inside a
    // checkout whose `repo/vedit.toml` carries `[project] filelists/include_dirs/
    // defines` used to KEEP that enclosing project's membership: the new folder
    // was expanded with the OTHER project's filelists, -I and -D — the
    // real-machine "stale files pile up after switching projects" defect (the
    // folder shown was new, the environment and file set were not).
    //
    // The rule is now explicit and self-contained per expansion:
    //   * the opened folder is the root AND the discovery root is that same
    //     folder (its own `vedit.toml`, or a user/config-only `[project]`):
    //     the `[project]` table applies, exactly as before;
    //   * the discovered `vedit.toml` lives ABOVE the opened folder: its
    //     `[project]` table described the enclosing project and is dropped.
    //     Non-project layers from it (UI, editor, tool paths) still apply —
    //     those are preferences, not membership — and the user-level
    //     `[project]` table still applies when no enclosing `vedit.toml`
    //     exists.
    // Every expansion re-reads the layers from the NEW root, so no previous
    // project's merge state can be carried over by construction.
    let project_settings =
        if discovered.root != root && discovered.root.join(CONFIG_FILE_NAME).is_file() {
            ProjectSettings::default()
        } else {
            config.project.clone()
        };
    let mut missing: BTreeSet<String> = BTreeSet::new();
    let mut referenced: BTreeMap<String, VariableLayer> = BTreeMap::new();
    let mut skipped = 0usize;
    let mut discovery_warnings: Vec<Warning> = Vec::new();
    let project = discover_in_root_with(
        root.clone(),
        root_source,
        search_stop,
        &project_settings,
        home.as_deref(),
        &mut discovery_warnings,
        variables,
        &mut missing,
        &mut referenced,
        &mut skipped,
    );
    warnings.append(&mut discovery_warnings);

    let mut all_lists: Vec<Filelist> = Vec::new();
    let mut list_seen: BTreeSet<PathBuf> = BTreeSet::new();
    for filelist in project.filelists {
        if list_seen.insert(canonical_path(&filelist.path)) {
            all_lists.push(filelist);
        }
    }
    for path in imported_filelists {
        // Skip a list the discovery already parsed BEFORE parsing it again:
        // parsing twice would double its diagnostics and the skipped counter.
        let key = canonical_path(path);
        if list_seen.contains(&key) {
            continue;
        }
        if let Some(filelist) = filelist::parse_with(
            path,
            &mut warnings,
            variables,
            &mut missing,
            &mut referenced,
            &mut skipped,
        ) {
            if list_seen.insert(canonical_path(&filelist.path)) {
                all_lists.push(filelist);
            }
        }
    }

    let mut files: Vec<PathBuf> = Vec::new();
    let mut file_seen: BTreeSet<PathBuf> = BTreeSet::new();
    for filelist in &all_lists {
        for entry in &filelist.entries {
            // A filelist names SOURCE files. An entry whose language this build
            // does not analyse (a nested `.f` listed as a path, a `.tcl` build
            // script, a `.md` note ...) is not a project source: the registry
            // has no language for it, so it never enters the file set and no
            // consumer can ever hand it to a parser. Positive, not a skip: it
            // was never a candidate.
            if !crate::language::supports(entry, crate::language::Capability::Source) {
                continue;
            }
            if file_seen.insert(canonical_path(entry)) {
                files.push(entry.clone());
            }
        }
    }
    // The recursive scan belongs to the LEGACY file set: with `auto_discover`
    // OFF (the default) the project's files are exactly what its filelists
    // name, so an undeclared sibling source can never shadow a declared one
    // (`R2` in the 2026-11-18 file-set contract).
    if project_settings.auto_discover {
        for scanned in scan_sources(&root, &mut warnings) {
            if file_seen.insert(canonical_path(&scanned)) {
                files.push(scanned);
            }
        }
    }

    let mut include_dirs: Vec<PathBuf> = project.include_dirs;
    let mut include_seen: BTreeSet<PathBuf> =
        include_dirs.iter().map(|dir| canonical_path(dir)).collect();
    for filelist in &all_lists {
        for dir in &filelist.include_dirs {
            if include_seen.insert(canonical_path(dir)) {
                include_dirs.push(dir.clone());
            }
        }
    }
    for dir in extra_include_dirs {
        let resolved = resolve_relative(&root, dir);
        if include_seen.insert(canonical_path(&resolved)) {
            include_dirs.push(resolved);
        }
    }

    let mut defines: Vec<Define> = project.defines;
    let mut define_seen: BTreeSet<Define> = defines.iter().cloned().collect();
    for filelist in &all_lists {
        for define in &filelist.defines {
            if define_seen.insert(define.clone()) {
                defines.push(define.clone());
            }
        }
    }
    for entry in extra_defines {
        match parse_define(entry) {
            Some(define) => {
                if define_seen.insert(define.clone()) {
                    defines.push(define);
                }
            }
            None => warnings.push(Warning::new(
                WarningCode::ConfigInvalidValue,
                format!("tools.defines entry `{entry}` is not NAME or NAME=VALUE; ignored"),
            )),
        }
    }

    Ok(ProjectExpansion {
        root,
        root_source: project.root_source,
        filelists: all_lists,
        files,
        include_dirs,
        defines,
        warnings,
        missing_variables: missing.into_iter().collect(),
        // BTreeMap iteration is name order: the collection is deterministic for
        // a given table + imported layer + environment (E1).
        referenced_variables: referenced
            .into_iter()
            .map(|(name, layer)| ReferencedVariable { name, layer })
            .collect(),
        skipped_lines: skipped,
    })
}

/// [`expand_with_variables`] plus the project panel's own editable `-I`/`-D`
/// entries (AC-1.5).
///
/// The entries arrive **as the user typed them** (the panel persists that
/// spelling), so this is where a relative include directory is resolved against
/// the project root. They are inserted BEFORE the tools-page values, so the
/// project's own declaration wins the order while the de-duplication below
/// still keeps one copy per real directory / define.
///
/// An entry that cannot become a `-I`/`-D` argument is a **hard error naming
/// the offending value**: silently dropping it would let the editor, the lint
/// engine and the LSP server disagree about the project with no one the wiser
/// (AC-1.5 "空值/重复拒绝").
pub fn expand_with_project_entries(
    root: &Path,
    imported_filelists: &[PathBuf],
    extra_include_dirs: &[PathBuf],
    extra_defines: &[String],
    project_include_dirs: &[String],
    project_defines: &[String],
    variables: &Variables,
) -> anyhow::Result<ProjectExpansion> {
    let mut include_dirs: Vec<PathBuf> =
        Vec::with_capacity(project_include_dirs.len() + extra_include_dirs.len());
    for entry in project_include_dirs {
        include_dirs.push(resolve_project_include_dir(root, entry)?);
    }
    include_dirs.extend_from_slice(extra_include_dirs);

    let mut defines: Vec<String> = Vec::with_capacity(project_defines.len() + extra_defines.len());
    for entry in project_defines {
        let trimmed = entry.trim();
        reject_unsafe_entry(entry, "define")?;
        if parse_define(trimmed).is_none() {
            anyhow::bail!(
                "the project define `{entry}` is not NAME or NAME=VALUE; remove or fix it"
            );
        }
        defines.push(trimmed.to_string());
    }
    defines.extend(extra_defines.iter().cloned());

    expand_with_variables(root, imported_filelists, &include_dirs, &defines, variables)
}

/// Resolve ONE typed include directory: trimmed, relative to the project root,
/// and safe to hand to a child process as `-I<path>`.
fn resolve_project_include_dir(root: &Path, entry: &str) -> anyhow::Result<PathBuf> {
    reject_unsafe_entry(entry, "include dir")?;
    let trimmed = entry.trim();
    if trimmed.is_empty() {
        anyhow::bail!("an include-dir entry is empty; every -I entry must name a directory");
    }
    Ok(resolve_relative(root, Path::new(trimmed)))
}

/// Values that cannot be spelled inside a `.f` filelist line or an argv entry
/// are refused with the value quoted, never rewritten.
fn reject_unsafe_entry(entry: &str, kind: &str) -> anyhow::Result<()> {
    if entry.trim().is_empty() {
        anyhow::bail!("a project {kind} entry is empty; remove it or give it a value");
    }
    if entry.chars().any(char::is_control) {
        anyhow::bail!("the project {kind} entry `{entry}` contains a control character");
    }
    if entry.contains('"') {
        anyhow::bail!("the project {kind} entry `{entry}` contains a double quote");
    }
    Ok(())
}

/// Recursively collect the project's source files (AC-2.2; legacy).
///
/// Only called when [`ProjectSettings::auto_discover`] restores the old file
/// set: by default the file set is the declared filelists' expansion, and no
/// directory is crawled.
///
/// Hidden directories are skipped: `.git`/`.svn` metadata and editor caches
/// never hold project sources, and walking them would let one checkout double
/// the scan. Symlinked directories are not followed, so a link back to an
/// ancestor cannot recurse forever.
fn scan_sources(root: &Path, warnings: &mut Vec<Warning>) -> Vec<PathBuf> {
    let mut found: Vec<PathBuf> = Vec::new();
    scan_directory(root, warnings, &mut found);
    found.sort();
    found
}

fn scan_directory(dir: &Path, warnings: &mut Vec<Warning>, found: &mut Vec<PathBuf>) {
    let read_dir = match std::fs::read_dir(dir) {
        Ok(read_dir) => read_dir,
        Err(error) => {
            warnings.push(Warning::new(
                WarningCode::FilelistUnreadable,
                format!(
                    "directory scan could not read `{}`: {error}; its files are missing from the project",
                    dir.display()
                ),
            ));
            return;
        }
    };
    let mut entries: Vec<(PathBuf, std::fs::FileType)> = read_dir
        .filter_map(Result::ok)
        .filter_map(|entry| entry.file_type().ok().map(|kind| (entry.path(), kind)))
        .collect();
    entries.sort_by(|left, right| left.0.cmp(&right.0));
    for (path, kind) in entries {
        if kind.is_dir() {
            let hidden = path
                .file_name()
                .and_then(std::ffi::OsStr::to_str)
                .is_some_and(|name| name.starts_with('.'));
            if !hidden {
                scan_directory(&path, warnings, found);
            }
        } else if kind.is_file() && is_scanned_source(&path) {
            found.push(path);
        }
    }
}

/// The scan's one question: does this build's registry give `path` the
/// [`Capability::Source`] answer? (`SCANNED_EXTENSIONS` is only this table's
/// projection — see `language::extensions_with`.)
fn is_scanned_source(path: &Path) -> bool {
    crate::language::supports(path, crate::language::Capability::Source)
}

/// Path identity for de-duplication: symlinks resolve to one entry when the
/// target exists; a path that does not exist yet compares by spelling.
fn canonical_path(path: &Path) -> PathBuf {
    std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
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

/// The declared filelists, plus the legacy root-level `*.f` auto-discovery.
///
/// The explicitly configured `[project] filelists` entries ALWAYS apply: the
/// user wrote them down, so no switch can make them disappear. The `*.f` files
/// sitting directly in the project root are only discovered when
/// [`ProjectSettings::auto_discover`] is on — the default OFF, because a folder
/// can hold several filelists and the one the user imported is the one that
/// defines the project (`R1` in the 2026-11-18 file-set contract).
fn discover_filelists(
    root: &Path,
    settings: &ProjectSettings,
    warnings: &mut Vec<Warning>,
    variables: &Variables,
    missing: &mut BTreeSet<String>,
    referenced: &mut BTreeMap<String, VariableLayer>,
    skipped: &mut usize,
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
    if settings.auto_discover {
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
    }

    let mut seen: BTreeSet<PathBuf> = BTreeSet::new();
    let mut parsed: Vec<Filelist> = Vec::new();
    for candidate in candidates {
        let key = std::fs::canonicalize(&candidate).unwrap_or_else(|_| candidate.clone());
        if !seen.insert(key) {
            continue;
        }
        if let Some(filelist) = filelist::parse_with(
            &candidate, warnings, variables, missing, referenced, skipped,
        ) {
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
        discover_root, discover_rules, expand, expand_with_project_entries, expand_with_variables,
        parse_define, Define, ProjectExpansion, RootSource, VariableLayer, Variables,
        VERIBLE_RULES_FILE_NAME,
    };
    use crate::test_support::write;
    use crate::warnings::WarningCode;
    use std::path::PathBuf;

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

    /// ADDENDUM A-9: variables expand in the filelist preprocessor, the project
    /// table wins over the environment, and every dropped line is reported with
    /// file:line + the unresolved name (never a silent loss).
    ///
    /// CONTRACT CHANGE (2026-11-18 file-set decision, A-5): the `.f` used to be
    /// auto-discovered; it is imported explicitly now, and the file assertion
    /// no longer includes the recursively scanned `rtl/b.sv` — the default file
    /// set is the declared filelist alone.
    #[test]
    fn expand_with_variables_reports_unresolved_lines_and_is_deterministic(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;
        write(&root.join("rtl/b.sv"), "module b; endmodule\n")?;
        write(
            &root.join("top.f"),
            "$SUB/a.sv\nrtl/$MISSING.sv\n+define+ARCH=$ARCH\n",
        )?;
        let top_f = root.join("top.f");
        let project: Vec<(String, String)> = vec![
            ("SUB".to_string(), "rtl".to_string()),
            ("ARCH".to_string(), "x86".to_string()),
            ("WHICH".to_string(), "project".to_string()),
        ];
        let vars = Variables::with_environment(
            &project,
            vec![("WHICH".to_string(), "environment".to_string())],
            Some(root.join("home")),
        );
        let expansion = expand_with_variables(root, std::slice::from_ref(&top_f), &[], &[], &vars)?;
        println!(
            "A9 files={:?} missing={:?} skipped={} warnings={:?}",
            expansion.files,
            expansion.missing_variables,
            expansion.skipped_lines,
            expansion.warnings
        );
        assert_eq!(
            expansion.files,
            vec![root.join("rtl/a.sv")],
            "the declared entry alone: the default state does not scan rtl/b.sv"
        );
        assert_eq!(expansion.missing_variables, vec!["MISSING".to_string()]);
        assert_eq!(expansion.skipped_lines, 1);
        assert!(expansion.defines.contains(&Define {
            name: "ARCH".to_string(),
            value: Some("x86".to_string()),
        }));
        assert!(expansion.warnings.iter().any(|warning| {
            warning.line == Some(2)
                && warning.message.contains("unresolved variable `$MISSING`")
                && warning.file.as_deref() == Some(root.join("top.f").as_path())
        }));

        // Same table + environment => byte-identical expansion (A-9.6).
        let again = expand_with_variables(root, std::slice::from_ref(&top_f), &[], &[], &vars)?;
        assert_eq!(again.files, expansion.files);
        assert_eq!(again.include_dirs, expansion.include_dirs);
        assert_eq!(again.defines, expansion.defines);
        assert_eq!(again.missing_variables, expansion.missing_variables);
        Ok(())
    }

    /// The E1 collection as `(name, layer)` pairs, in the recorded order.
    fn referenced_layers(expansion: &ProjectExpansion) -> Vec<(String, VariableLayer)> {
        expansion
            .referenced_variables
            .iter()
            .map(|reference| (reference.name.clone(), reference.layer))
            .collect()
    }

    /// E1 ① (batch-20261114): each of the three layers satisfies one referenced
    /// name and the collection records WHICH layer did it; a name no layer has
    /// is recorded as `Missing`. `missing_variables` keeps its own meaning.
    #[test]
    fn referenced_variables_mark_the_layer_that_satisfied_them(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("rtl/project.sv"), "module p; endmodule\n")?;
        write(&root.join("rtl/imported.sv"), "module i; endmodule\n")?;
        write(&root.join("rtl/environment.sv"), "module e; endmodule\n")?;
        write(
            &root.join("top.f"),
            "$FROM_PROJECT/project.sv\n$FROM_IMPORTED/imported.sv\n\
             $FROM_ENVIRONMENT/environment.sv\n$FROM_NOWHERE/x.sv\n",
        )?;
        let project = vec![("FROM_PROJECT".to_string(), "rtl".to_string())];
        let imported = vec![("FROM_IMPORTED".to_string(), "rtl".to_string())];
        let environment = vec![("FROM_ENVIRONMENT".to_string(), "rtl".to_string())];
        let vars =
            Variables::with_layers(&project, imported, environment, Some(root.to_path_buf()));

        // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): the `.f` is
        // imported explicitly; the default state does not auto-discover it.
        let expansion = expand_with_variables(
            root,
            std::slice::from_ref(&root.join("top.f")),
            &[],
            &[],
            &vars,
        )?;
        assert_eq!(
            referenced_layers(&expansion),
            vec![
                ("FROM_ENVIRONMENT".to_string(), VariableLayer::Environment),
                ("FROM_IMPORTED".to_string(), VariableLayer::Imported),
                ("FROM_NOWHERE".to_string(), VariableLayer::Missing),
                ("FROM_PROJECT".to_string(), VariableLayer::Project),
            ],
            "every referenced name appears once, sorted, with the layer that answered"
        );
        assert_eq!(
            expansion.missing_variables,
            vec!["FROM_NOWHERE".to_string()],
            "missing_variables keeps its existing semantics"
        );
        Ok(())
    }

    /// E1 ②: one name present in several layers is recorded ONCE, by the
    /// resolution precedence project > imported > environment.
    #[test]
    fn a_referenced_name_in_several_layers_is_recorded_by_precedence(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("rtl/p.sv"), "module p; endmodule\n")?;
        write(&root.join("top.f"), "$WHICH/p.sv\n$IMPORTED_WINS/p.sv\n")?;
        let project = vec![("WHICH".to_string(), "rtl".to_string())];
        let imported = vec![
            ("WHICH".to_string(), "imported".to_string()),
            ("IMPORTED_WINS".to_string(), "rtl".to_string()),
        ];
        let environment = vec![
            ("WHICH".to_string(), "environment".to_string()),
            ("IMPORTED_WINS".to_string(), "environment".to_string()),
        ];
        let vars =
            Variables::with_layers(&project, imported, environment, Some(root.to_path_buf()));

        // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): explicit import.
        let expansion = expand_with_variables(
            root,
            std::slice::from_ref(&root.join("top.f")),
            &[],
            &[],
            &vars,
        )?;
        assert_eq!(
            referenced_layers(&expansion),
            vec![
                ("IMPORTED_WINS".to_string(), VariableLayer::Imported),
                ("WHICH".to_string(), VariableLayer::Project),
            ],
            "the most specific layer owns the name"
        );
        Ok(())
    }

    /// E1 ③: `$$` is the literal-dollar escape and `~` is home expansion — the
    /// contract's non-names. Neither may enter the referenced collection.
    #[test]
    fn dollar_escapes_and_home_are_not_variable_names() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("top.f"), "$$/literal.sv\n~/home.sv\n")?;
        let vars = Variables::with_layers(&[], [], [], Some(root.to_path_buf()));

        // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): explicit import.
        let expansion = expand_with_variables(
            root,
            std::slice::from_ref(&root.join("top.f")),
            &[],
            &[],
            &vars,
        )?;
        assert!(
            expansion.referenced_variables.is_empty(),
            "`$$` and `~` are not variable names, got {:?}",
            expansion.referenced_variables
        );
        assert!(expansion.missing_variables.is_empty());
        Ok(())
    }

    /// E1 ④: the collection is "referenced", not "available": a name that lives
    /// in one of the three tables but no `.f` line names it never appears.
    #[test]
    fn unreferenced_names_do_not_enter_the_collection() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("rtl/p.sv"), "module p; endmodule\n")?;
        write(&root.join("top.f"), "$USED/p.sv\n")?;
        let project = vec![
            ("USED".to_string(), "rtl".to_string()),
            ("UNUSED_PROJECT".to_string(), "rtl".to_string()),
        ];
        let imported = vec![("UNUSED_IMPORTED".to_string(), "rtl".to_string())];
        let environment = vec![("UNUSED_ENVIRONMENT".to_string(), "rtl".to_string())];
        let vars =
            Variables::with_layers(&project, imported, environment, Some(root.to_path_buf()));

        // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): explicit import.
        let expansion = expand_with_variables(
            root,
            std::slice::from_ref(&root.join("top.f")),
            &[],
            &[],
            &vars,
        )?;
        assert_eq!(
            referenced_layers(&expansion),
            vec![("USED".to_string(), VariableLayer::Project)],
            "an unreferenced table entry must not appear"
        );
        Ok(())
    }

    /// AC-2.2 legacy behaviour (declared CONTRACT CHANGE by the 2026-11-18
    /// file-set decision, A-2/A-5): the file set is
    /// `scan(root) ∪ expand(filelists)`, de-duplicated, with the filelist order
    /// first and the scan (sorted) after it — but that union is no longer the
    /// default. The switch is explicit here; the default (declared filelists
    /// only) is pinned by `tests/file_set_contract.rs` (A-1/A-3).
    #[test]
    fn expand_unions_the_scan_and_the_filelists_without_duplicates(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("vedit.toml"),
            "[project]\nauto_discover = true\n",
        )?;
        write(&root.join("top.f"), "rtl/a.sv\nrtl/a.sv\n")?;
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;
        write(&root.join("rtl/b.v"), "module b; endmodule\n")?;
        write(&root.join("inc/c.vh"), "`define C 1\n")?;
        write(&root.join("notes.txt"), "not a source\n")?;
        write(&root.join(".git/d.sv"), "module d; endmodule\n")?;

        let expansion = expand(root, &[], &[], &[])?;
        let relative: Vec<PathBuf> = expansion
            .files
            .iter()
            .map(|path| path.strip_prefix(root).unwrap_or(path).to_path_buf())
            .collect();
        assert_eq!(
            relative,
            vec![
                PathBuf::from("rtl/a.sv"),
                PathBuf::from("inc/c.vh"),
                PathBuf::from("rtl/b.v"),
            ],
            "filelist order first, then the sorted scan, duplicates removed"
        );
        assert_eq!(expansion.filelists.len(), 1);
        Ok(())
    }

    /// AC-2.1: the folder the user opened IS the root, even inside a checkout
    /// that carries a `.git` marker above it.
    ///
    /// CONTRACT CHANGE (2026-11-18 file-set decision, A-5): this test used to
    /// get its one file from the unconditional directory scan. The scan is off
    /// by default now, so the file arrives through an imported `.f` placed
    /// OUTSIDE the opened folder (a `.f` inside it would itself be a root
    /// marker and change the `RootSource::Fallback` assertion). The root
    /// assertions are unchanged.
    #[test]
    fn expand_pins_the_opened_folder_as_the_root() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let repo = temp.path();
        std::fs::create_dir_all(repo.join(".git"))?;
        let opened = repo.join("sub");
        write(&opened.join("rtl/x.sv"), "module x; endmodule\n")?;
        write(&repo.join("outside.f"), "sub/rtl/x.sv\n")?;

        let expansion = expand(&opened, &[repo.join("outside.f")], &[], &[])?;
        assert_eq!(expansion.root, opened);
        assert_eq!(expansion.root_source, RootSource::Fallback);
        assert_eq!(expansion.files, vec![opened.join("rtl/x.sv")]);
        Ok(())
    }

    /// The scan's extension list is a PROJECTION of the language registry, not
    /// a second predicate (RM-11): drift between the two is a test failure.
    #[test]
    fn the_scan_extensions_are_the_registrys_source_projection() {
        let mut registry = crate::language::extensions_with(crate::language::Capability::Source);
        let mut scan: Vec<&str> = super::SCANNED_EXTENSIONS.to_vec();
        registry.sort_unstable();
        scan.sort_unstable();
        assert_eq!(scan, registry);
    }

    /// RM-2/RM-11: a filelist names SOURCE files. Its own path, a nested `.f`
    /// listed as a path, and a `.md`/`.tcl`/`.json` entry are not languages this
    /// build analyses, so they never enter the file set - while the HDL entries
    /// it lists do. A manifest can therefore never be parsed as HDL.
    ///
    /// CONTRACT CHANGE (2026-11-18 file-set decision, A-5): the two filelists
    /// used to be found by root-level auto-discovery; that discovery is off by
    /// default now, so the test opts into the legacy behaviour explicitly and
    /// both lists are discovered in the same sorted order as before.
    #[test]
    fn a_filelist_contributes_sources_never_itself_or_non_hdl_entries(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(
            &root.join("vedit.toml"),
            "[project]\nauto_discover = true\n",
        )?;
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;
        write(&root.join("rtl/b.vh"), "`define B 1\n")?;
        write(&root.join("notes.md"), "# notes\n")?;
        write(&root.join("run.tcl"), "puts hi\n")?;
        write(&root.join("pins.xdc"), "set_property\n")?;
        write(&root.join("nested.f"), "rtl/b.vh\n")?;
        write(
            &root.join("top.f"),
            "rtl/a.sv\nnotes.md\nrun.tcl\npins.xdc\nnested.f\n",
        )?;

        let expansion = expand(root, &[], &[], &[])?;
        let relative: Vec<PathBuf> = expansion
            .files
            .iter()
            .map(|path| path.strip_prefix(root).unwrap_or(path).to_path_buf())
            .collect();
        assert_eq!(
            relative,
            vec![PathBuf::from("rtl/b.vh"), PathBuf::from("rtl/a.sv")],
            "only the HDL entries are sources (filelist order: nested.f sorts first)"
        );
        assert!(
            !expansion
                .files
                .iter()
                .any(|path| path == &root.join("top.f")),
            "a manifest is never a source"
        );
        // Imported lists follow the same rule as discovered ones.
        let imported = expand(root, &[root.join("nested.f")], &[], &[])?;
        assert!(imported
            .files
            .iter()
            .all(|path| path != &root.join("nested.f")));
        Ok(())
    }

    /// RM-2: a switch re-roots project membership. Opening a folder INSIDE a
    /// project whose `vedit.toml` declares `[project] filelists/include_dirs/
    /// defines` must not keep that enclosing project's file set or environment
    /// (the real-machine "stale files pile up after switching projects" bug);
    /// the same `[project]` table DOES apply when its own folder is opened.
    #[test]
    fn expand_drops_an_ancestor_projects_membership_but_keeps_its_own(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let outer = temp.path();
        std::fs::create_dir_all(outer.join(".git"))?;
        write(
            &outer.join("vedit.toml"),
            "[project]\nfilelists = [\"outer.f\"]\ninclude_dirs = [\"outer_inc\"]\n\
             defines = [\"OUTER_ONLY=1\"]\n",
        )?;
        write(&outer.join("outer.f"), "outer.sv\n")?;
        write(&outer.join("outer.sv"), "module outer; endmodule\n")?;
        let inner = outer.join("inner");
        write(&inner.join("inner.sv"), "module inner; endmodule\n")?;

        // Opening the OUTER project keeps its own [project] table.
        let from_outer = expand(outer, &[], &[], &[])?;
        assert_eq!(from_outer.root, std::path::absolute(outer)?);
        assert!(
            from_outer.files.contains(&outer.join("outer.sv")),
            "the outer project's configured filelist must expand: {:?}",
            from_outer.files
        );
        assert!(
            from_outer.include_dirs.contains(&outer.join("outer_inc")),
            "the outer project's -I must apply: {:?}",
            from_outer.include_dirs
        );
        assert!(
            from_outer
                .defines
                .iter()
                .any(|define| define.name == "OUTER_ONLY"),
            "the outer project's -D must apply: {:?}",
            from_outer.defines
        );

        // Opening a folder INSIDE it (a project switch): the enclosing
        // project's membership is not inherited. CONTRACT CHANGE (2026-11-18
        // file-set decision, A-5): the default no longer scans the opened
        // folder, so `inner/inner.sv` — which no filelist declares — is NOT a
        // project file; the empty set proves the outer project's file set and
        // environment were dropped rather than inherited.
        let from_inner = expand(&inner, &[], &[], &[])?;
        assert_eq!(from_inner.root, std::path::absolute(&inner)?);
        assert!(
            from_inner.files.is_empty(),
            "no declared filelist and no scan: no files, never the outer project's: {:?}",
            from_inner.files
        );
        assert!(
            !from_inner.include_dirs.contains(&outer.join("outer_inc")),
            "the outer project's -I must not leak: {:?}",
            from_inner.include_dirs
        );
        assert!(
            !from_inner
                .defines
                .iter()
                .any(|define| define.name == "OUTER_ONLY"),
            "the outer project's -D must not leak: {:?}",
            from_inner.defines
        );

        // A-5: the OLD scan-derived expectation is kept, under the new switch,
        // on the inner project itself (its own `[project]` table now exists, so
        // this also proves the switch is read from the OPENED folder's table).
        write(
            &inner.join("vedit.toml"),
            "[project]\nauto_discover = true\n",
        )?;
        let legacy_inner = expand(&inner, &[], &[], &[])?;
        assert_eq!(
            legacy_inner.files,
            vec![inner.join("inner.sv")],
            "switch on: the legacy scan file set is reproducible"
        );
        assert!(
            !legacy_inner.include_dirs.contains(&outer.join("outer_inc")),
            "switch on must not resurrect the outer project's -I: {:?}",
            legacy_inner.include_dirs
        );
        Ok(())
    }

    /// AC-2.3, the three specified bad entries: a `+incdir+` directory that does
    /// not exist, an unknown directive, and a missing nested `-f`. Each one is
    /// reported with the `.f` and its 1-based line — none is silently dropped.
    /// A top-level imported filelist that is missing is reported too (there is no
    /// line for a file that does not exist).
    #[test]
    fn expand_reports_bad_filelist_entries_with_file_and_line(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("good.sv"), "module good; endmodule\n")?;
        write(
            &root.join("bad.f"),
            "good.sv\n+incdir+no_such_dir\n+frobnicate now\n-f missing_nested.f\n",
        )?;
        let bad = root.join("bad.f");
        // CONTRACT CHANGE (2026-11-18 file-set decision, A-5): the broken `.f`
        // is imported explicitly; the default state no longer auto-discovers a
        // root-level filelist (the diagnostics below are unchanged).
        let expansion = expand(root, std::slice::from_ref(&bad), &[], &[])?;
        let at = |code: &str, line: u32| {
            expansion.warnings.iter().any(|warning| {
                warning.code.as_str() == code
                    && warning.line == Some(line)
                    && warning.file.as_deref() == Some(bad.as_path())
            })
        };
        assert!(
            at("filelist.missing", 2),
            "missing +incdir+: {:?}",
            expansion.warnings
        );
        assert!(
            at("filelist.unknown_token", 3),
            "unknown directive: {:?}",
            expansion.warnings
        );
        assert!(
            at("filelist.missing", 4),
            "missing nested -f: {:?}",
            expansion.warnings
        );

        let imported = root.join("gone.f");
        let with_import = expand(root, std::slice::from_ref(&imported), &[], &[])?;
        assert!(
            with_import.warnings.iter().any(|warning| {
                warning.code == WarningCode::FilelistMissing
                    && warning.file.as_deref() == Some(imported.as_path())
            }),
            "an imported filelist that vanished must be reported: {:?}",
            with_import.warnings
        );
        Ok(())
    }

    /// AC-2.5: the tools-page values are appended (and de-duplicated) after the
    /// project's own include dirs and defines; a malformed define is reported
    /// instead of being passed through.
    #[test]
    fn expand_appends_tools_page_include_dirs_and_defines() -> Result<(), Box<dyn std::error::Error>>
    {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        std::fs::create_dir_all(root.join("extra_inc"))?;
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;

        let expansion = expand(
            root,
            &[],
            &[PathBuf::from("extra_inc")],
            &["WIDTH=8".to_string(), "BAD DEFINE".to_string()],
        )?;
        assert_eq!(expansion.include_dirs, vec![root.join("extra_inc")]);
        assert_eq!(
            expansion.defines,
            vec![Define {
                name: "WIDTH".to_string(),
                value: Some("8".to_string()),
            }]
        );
        assert!(
            expansion
                .warnings
                .iter()
                .any(|warning| warning.code == WarningCode::ConfigInvalidValue),
            "a malformed tools.defines entry must be reported: {:?}",
            expansion.warnings
        );
        Ok(())
    }

    /// AC-1.5: the project panel's own `-I` entries are resolved against the
    /// root, inserted before the tools-page ones, and de-duplicated by real
    /// path (`inc`, `./inc`, `inc/` and an absolute twin are one directory).
    #[test]
    fn project_include_entries_resolve_relative_and_dedupe(
    ) -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        std::fs::create_dir_all(root.join("inc"))?;
        std::fs::create_dir_all(root.join("vendor"))?;
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;

        let expansion = expand_with_project_entries(
            root,
            &[],
            &[PathBuf::from("vendor")],
            &[],
            &["inc".to_string(), "./inc".to_string(), "inc/".to_string()],
            &[],
            &Variables::from_pairs(&[]),
        )?;
        assert_eq!(
            expansion.include_dirs,
            vec![root.join("inc"), root.join("vendor")],
            "the typed `inc` resolves against the root and appears once"
        );
        Ok(())
    }

    /// AC-1.5: a project define is accepted once and de-duplicated; a value
    /// that is not NAME / NAME=VALUE is refused WITH the value quoted.
    #[test]
    fn project_defines_dedupe_and_reject_bad_values() -> Result<(), Box<dyn std::error::Error>> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        write(&root.join("rtl/a.sv"), "module a; endmodule\n")?;

        let expansion = expand_with_project_entries(
            root,
            &[],
            &[],
            &["SIM".to_string()],
            &[],
            &["BOARD=7".to_string(), "BOARD=7".to_string()],
            &Variables::from_pairs(&[]),
        )?;
        assert_eq!(
            expansion.defines,
            vec![
                Define {
                    name: "BOARD".to_string(),
                    value: Some("7".to_string()),
                },
                Define {
                    name: "SIM".to_string(),
                    value: None,
                },
            ],
            "the project's own define comes first and is not duplicated"
        );

        // `expect_err` is banned by `[workspace.lints.clippy] expect_used = deny`
        // (T27): the assertion is identical, only the failure path is explicit.
        let error = match expand_with_project_entries(
            root,
            &[],
            &[],
            &[],
            &[],
            &["1BAD".to_string()],
            &Variables::from_pairs(&[]),
        ) {
            Ok(_) => panic!("a define that is not an identifier must be refused"),
            Err(error) => error,
        };
        assert!(
            error.to_string().contains("1BAD"),
            "the refusal must name the value: {error}"
        );
        Ok(())
    }

    /// AC-1.5: an empty, whitespace-only, control-character or quoted entry is
    /// refused with a message naming the value — never silently dropped.
    #[test]
    fn project_entries_reject_invalid_values() {
        let temp = match tempfile::tempdir() {
            Ok(temp) => temp,
            Err(error) => panic!("cannot create a temp dir: {error}"),
        };
        let root = temp.path();
        let cases: [(&str, &str); 4] = [
            ("", "empty"),
            ("   ", "empty"),
            ("inc\nsub", "control character"),
            ("inc\"sub", "double quote"),
        ];
        for (value, expected) in cases {
            // Same match/let-else rule as above (`expect_used = deny`).
            let error = match expand_with_project_entries(
                root,
                &[],
                &[],
                &[],
                &[value.to_string()],
                &[],
                &Variables::from_pairs(&[]),
            ) {
                Ok(_) => panic!("an invalid include dir must be refused"),
                Err(error) => error,
            };
            assert!(
                error.to_string().contains(expected),
                "`{value}` must be refused with `{expected}`: {error}"
            );
        }
    }
}
