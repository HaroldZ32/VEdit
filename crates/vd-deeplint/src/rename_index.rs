//! Engineering-wide rename: the project AST index and the rename plan
//! (batch 20261110, `rename-core`).
//!
//! Everything here is derived from **slang's own AST**, never from a text scan:
//! the elaboration runs with `--ast-json --ast-json-source-info` over the
//! project's file list (the caller supplies the include dirs and defines), and
//! two node shapes carry the whole rename:
//!
//! * a **declaration** node (`Variable`, `Net`, `Port`, `Parameter`, `Instance`)
//!   serialises `name`, `addr`, `source_file` and a 1-based **byte** `source_line`
//!   / `source_column` (measured: a line with CJK before the identifier reports
//!   the byte column, not the character column);
//! * a **reference** node (`NamedValue`) serialises a span
//!   (`source_*_start` / `source_*_end`, 1-based byte columns) AND
//!   `symbol: "<addr> <name>"` — slang has already resolved it to the
//!   declaration. Identity is therefore `addr`, and every occurrence is the
//!   declaration plus every `NamedValue` naming that `addr`. A comment, a
//!   string, a same-name symbol in another module or another project cannot be
//!   in that set, which is what makes a "rename" that never falls back to text
//!   replacement possible at all.
//!
//! # Resolution is by NAME, not by cursor
//!
//! The frozen C ABI's plan request carries `from` (the identifier), not a
//! cursor, so the engine resolves the NAME to exactly one declaration. Zero
//! declarations is "not nameable"; two or more is **ambiguous** and is rejected
//! with every declaration's `file:line` — the negative control "same-name locals
//! in other modules" deliberately lands here instead of being renamed.
//!
//! # Supported classes and their honesty
//!
//! | class | nodes | status |
//! |---|---|---|
//! | signal / net / var | `Variable`, `Net` (+ their references) | supported |
//! | port | `Port` (+ their references) | supported, unless the port is connected at an instantiation site (see below) |
//! | parameter / constant | `Parameter`, `LocalParameter` (+ their references) | **refused as a class** — a type-dimension use has no AST node |
//! | instance name | `Instance` | **refused as a class** — the instance prefix of a hierarchical reference has no AST binding |
//!
//! # Measured unresolvable categories (refused WHOLE)
//!
//! These positions do not exist in `slang --ast-json --ast-json-source-info`
//! (measured against the pinned 12.1 dump), so a partial rename would leave the
//! source inconsistent — and text replacement is never an answer:
//!
//! * **type dimensions** — slang resolves a port's type to a string
//!   (`logic[7:0]`), so `WIDTH` in `[WIDTH-1:0]` serialises **no node at all**
//!   (zero `NamedValue` for `WIDTH`). Every parameter/constant is refused.
//! * **instance hierarchical references** — `u_a.sig` serialises a
//!   `HierarchicalValue` that binds only the LEAF symbol (`sig`); the instance
//!   path has no binding, and an instance name has no reference node at all. Every
//!   instance name, and every signal reached through such a path, is refused.
//! * **instantiation connection labels** — a named connection `.din(din)` keeps
//!   its `din` label only in the child port's declaration node; the PARENT label
//!   has no position. A port a connection references is refused.
//!
//! Any other declaration kind that the name resolves to is reported as
//! **unsupported**, never renamed by guesswork, and a symbol whose occurrence set
//! contains one of the categories above is refused whole.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde_json::Value;

use crate::hierarchy::{run_ast_json, HierarchyOptions};
use crate::LintFailure;

/// Which family of symbol a rename may touch.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SymbolClass {
    /// `Variable` / `Net` / `Port`.
    Signal,
    /// `Parameter` / `LocalParameter`.
    Parameter,
    /// `Instance` (the instance name, not the module name).
    Instance,
}

impl SymbolClass {
    /// Stable label for logs and the UI.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Signal => "signal",
            Self::Parameter => "parameter",
            Self::Instance => "instance",
        }
    }

    fn from_kind(kind: &str) -> Option<Self> {
        match kind {
            "Variable" | "Net" | "Port" => Some(Self::Signal),
            "Parameter" | "LocalParameter" => Some(Self::Parameter),
            "Instance" => Some(Self::Instance),
            _ => None,
        }
    }
}

/// One declaration the index holds.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Declaration {
    /// slang's stable symbol address (`addr`), reinterpreted as an identifier.
    pub addr: u64,
    /// Which class the declaration belongs to.
    pub class: SymbolClass,
    /// The declared identifier.
    pub name: String,
    /// File the declaration came from (as slang reported it).
    pub file: PathBuf,
    /// 0-based line.
    pub line: u32,
    /// 0-based byte column.
    pub column: u32,
    /// The enclosing instance body (module) — the rename conflict scope.
    pub scope: String,
}

/// One reference site (`NamedValue`) linked to a declaration address.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reference {
    /// The `addr` the reference resolved to.
    pub addr: u64,
    /// File the reference came from.
    pub file: PathBuf,
    /// 0-based line.
    pub line: u32,
    /// 0-based byte column of the first byte.
    pub column: u32,
    /// 0-based byte column one past the last byte.
    pub end_column: u32,
}

/// What one index pass found.
#[derive(Debug, Clone, Default)]
pub struct RenameIndex {
    /// The files the AST run was asked to elaborate.
    pub files: Vec<PathBuf>,
    /// Every declaration the walk kept.
    pub declarations: Vec<Declaration>,
    /// Every resolved reference the walk kept.
    pub references: Vec<Reference>,
    /// Non-fatal problems (skipped reference, hierarchical instance name, …).
    pub warnings: Vec<String>,
    /// How long the elaboration took (for the evidence log).
    pub elapsed: Duration,
    /// Child-port addresses referenced by an instantiation's `connections`.
    ///
    /// A named connection `.port(...)` carries NO position for its label in
    /// slang's AST JSON (measured), so a port in this set has a position the
    /// engine cannot rewrite; [`plan`] refuses such a port whole.
    pub(crate) connected_ports: BTreeSet<u64>,
    /// LEAF addresses of hierarchical references (`u_a.sig`).
    ///
    /// The AST serialises the reference's whole span but binds only the leaf
    /// symbol; the instance path has no binding at all (measured). Such a leaf is
    /// therefore refused whole — rewriting its name would leave the path behind.
    pub(crate) hierarchical_refs: BTreeSet<u64>,
}

/// Summary of one index pass, matching the frozen C ABI's `VdRenameIndex`.
///
/// The ABI's index request carries no symbol, so `occurrences` counts every
/// reference site the index holds; the SYMBOL-specific count arrives with the
/// plan ([`RenamePlan::occurrences`]).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IndexSummary {
    /// Files parsed.
    pub files: usize,
    /// Distinct declaration addresses.
    pub identifiers: usize,
    /// Reference sites held.
    pub occurrences: usize,
    /// Non-fatal problems.
    pub warnings: Vec<String>,
}

/// Every failure a rename can answer with.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RenameError {
    /// The name is not declared anywhere in the project.
    NotNameable {
        /// The identifier that was asked for.
        name: String,
        /// What the walk saw, for the message.
        message: String,
    },
    /// Two or more declarations share the name: renaming by name is not safe.
    Ambiguous {
        /// The identifier that was asked for.
        name: String,
        /// Every declaration the index holds, as `file:line`.
        declarations: Vec<String>,
    },
    /// The declaration exists but its class is not one this engine renames.
    Unsupported {
        /// The declared kind slang reported.
        kind: String,
    },
    /// The new name is not a legal SystemVerilog identifier (or is a keyword).
    IllegalName {
        /// Rejection reason.
        reason: String,
    },
    /// The new name is already declared in the same scope.
    Conflict {
        /// The identifier that is already taken.
        name: String,
        /// The conflicting declaration, `file:line`.
        declaration: String,
    },
    /// `from == to` — an explicit no-op, never a silent success.
    SameName {
        /// The identifier.
        name: String,
    },
    /// No project file set was supplied: refuse instead of guessing a directory.
    NoProject {
        /// Why the set was unusable.
        message: String,
    },
    /// No slang binary was found anywhere the product resolver looks.
    ///
    /// Deliberately separate from [`RenameError::Io`]: a missing binary and a
    /// path/IO problem are different user actions (install slang vs. fix the
    /// project directory), so the code, not just the prose, must differ.
    BinaryNotFound {
        /// The resolver's one-line reason (it names the fixes).
        message: String,
    },
    /// Elaboration failed (timeout, nonzero exit, bad JSON) or the child could
    /// not start for a reason that is not a missing binary.
    Ast {
        /// The underlying message.
        message: String,
    },
    /// A filesystem problem: project path, reading a file, running the child.
    Io {
        /// The underlying message.
        message: String,
    },
}

impl RenameError {
    /// Stable machine code for the ABI and the evidence logs.
    pub fn code(&self) -> &'static str {
        match self {
            Self::NotNameable { .. } => "not_nameable",
            Self::Ambiguous { .. } => "ambiguous",
            Self::Unsupported { .. } => "unsupported",
            Self::IllegalName { .. } => "illegal_name",
            Self::Conflict { .. } => "conflict",
            Self::SameName { .. } => "same_name",
            Self::NoProject { .. } => "no_project",
            Self::BinaryNotFound { .. } => "binary_not_found",
            Self::Ast { .. } => "ast",
            Self::Io { .. } => "io",
        }
    }

    /// One line for the user.
    pub fn message(&self) -> String {
        match self {
            Self::NotNameable { name, message } => {
                format!("`{name}` is not a nameable symbol: {message}")
            }
            Self::Ambiguous { name, declarations } => format!(
                "`{name}` is ambiguous: {} declaration(s) — {}",
                declarations.len(),
                declarations.join(", ")
            ),
            Self::Unsupported { kind } => {
                format!("a `{kind}` cannot be renamed by this engine")
            }
            Self::IllegalName { reason } => format!("the new name is not usable: {reason}"),
            Self::Conflict { name, declaration } => {
                format!("the new name `{name}` is already declared at {declaration}")
            }
            Self::NoProject { message } => format!("no project to rename in: {message}"),
            Self::BinaryNotFound { message } => {
                format!("no slang binary is available for the rename index: {message}")
            }
            Self::SameName { name } => {
                format!("`{name}` is already the current name: this is an explicit no-op")
            }
            Self::Ast { message } => format!("the AST index could not be built: {message}"),
            Self::Io { message } => format!("the rename path/IO step failed: {message}"),
        }
    }
}

/// The inputs of one index pass.
#[derive(Debug, Clone)]
pub struct Project {
    /// Project root: the base a RELATIVE `files` entry is resolved against.
    pub root: PathBuf,
    /// The files to elaborate (the project file set).
    pub files: Vec<PathBuf>,
    /// `-I` include directories.
    pub include_dirs: Vec<PathBuf>,
    /// `-D` defines, already in `name=value` form.
    pub defines: Vec<String>,
    /// Wall-clock budget for the elaboration.
    pub timeout: Duration,
    /// Explicit slang binary (tests/packaging); `None` resolves like the lint lane.
    pub slang_binary: Option<PathBuf>,
}

impl Project {
    /// Build a project from a root and its file set.
    pub fn new(root: impl Into<PathBuf>, files: Vec<PathBuf>) -> Self {
        Self {
            root: root.into(),
            files,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: Duration::from_secs(120),
            slang_binary: None,
        }
    }
}

/// The outcome of `plan`: either a real plan or an explicit no-op.
#[derive(Debug, Clone)]
pub enum PlanOutcome {
    /// `from == to`; nothing to do and nothing was written.
    Noop {
        /// The identifier.
        name: String,
    },
    /// A plan with at least one occurrence.
    Plan(RenamePlan),
}

/// One replacement site in a file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Occurrence {
    /// Absolute path of the file.
    pub file: PathBuf,
    /// 0-based line.
    pub line: u32,
    /// 0-based UTF-16 start column (the ABI unit).
    pub column: u32,
    /// 0-based UTF-16 end column (exclusive).
    pub end_column: u32,
    /// Byte offset in the file — the apply anchor.
    pub byte_offset: u64,
    /// Identifier length in bytes.
    pub byte_len: u64,
    /// The source line before the edit (trimmed, capped).
    pub before: String,
    /// The same line after the edit (trimmed, capped).
    pub after: String,
}

/// The preview of one file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FilePlan {
    /// Absolute path.
    pub file: PathBuf,
    /// Replacement sites, in file order.
    pub hits: Vec<Occurrence>,
    /// True when the caller's per-file cap dropped hits.
    pub truncated: bool,
}

/// A rename plan: every file and every replacement.
#[derive(Debug, Clone)]
pub struct RenamePlan {
    /// Project root the plan was built for.
    pub root: PathBuf,
    /// The identifier being renamed.
    pub from: String,
    /// The new identifier.
    pub to: String,
    /// Files the index parsed.
    pub files: usize,
    /// Total planned replacements.
    pub occurrences: usize,
    /// Per-file previews, in file order.
    pub plans: Vec<FilePlan>,
    /// Non-fatal problems carried over from the index.
    pub warnings: Vec<String>,
}

/// Cap on one preview line's rendered length.
const MAX_LINE_CHARS: usize = 200;

/// Upper bound on a file the applier will rewrite (the rename lane is a source
/// editor, not a binary tool; a larger file is refused rather than copied).
pub const MAX_APPLY_BYTES: usize = 64 * 1024 * 1024;

impl RenameIndex {
    /// The ABI-shaped summary of this pass.
    pub fn summary(&self) -> IndexSummary {
        let mut addrs: Vec<u64> = self.declarations.iter().map(|decl| decl.addr).collect();
        addrs.sort_unstable();
        addrs.dedup();
        IndexSummary {
            files: self.files.len(),
            identifiers: addrs.len(),
            occurrences: self.references.len(),
            warnings: self.warnings.clone(),
        }
    }

    /// Every distinct declaration SITE (one per file/line/column), grouped from
    /// the declarations.
    ///
    /// A port is serialised TWICE by slang — a `Port` node whose
    /// `internalSymbol` addresses a separate `Net` node at the SAME position —
    /// so one written identifier owns several `addr`s. The source has ONE
    /// identifier there, therefore the rename identity is the SITE, and its
    /// occurrences are the union over every address that site owns.
    fn identities(&self) -> Vec<Identity> {
        let mut by_site: BTreeMap<(PathBuf, u32, u32), Identity> = BTreeMap::new();
        for declaration in &self.declarations {
            let key = (
                declaration.file.clone(),
                declaration.line,
                declaration.column,
            );
            let identity = by_site.entry(key).or_insert_with(|| Identity {
                name: declaration.name.clone(),
                class: declaration.class,
                scope: declaration.scope.clone(),
                file: declaration.file.clone(),
                line: declaration.line,
                column: declaration.column,
                addrs: Vec::new(),
            });
            if !identity.addrs.contains(&declaration.addr) {
                identity.addrs.push(declaration.addr);
            }
        }
        by_site.into_values().collect()
    }

    /// Every occurrence of an identity: its declaration sites first, then the
    /// references, deduplicated by position and ordered.
    fn sites_of(&self, identity: &Identity) -> Vec<Site> {
        let mut sites: Vec<Site> = Vec::new();
        for declaration in self
            .declarations
            .iter()
            .filter(|declaration| identity.addrs.contains(&declaration.addr))
        {
            sites.push(Site {
                file: declaration.file.clone(),
                line: declaration.line,
                column: declaration.column,
                end_column: declaration.column + declaration.name.len() as u32,
            });
        }
        for reference in self
            .references
            .iter()
            .filter(|item| identity.addrs.contains(&item.addr))
        {
            sites.push(Site {
                file: reference.file.clone(),
                line: reference.line,
                column: reference.column,
                end_column: reference.end_column,
            });
        }
        sites.sort_by(|left, right| {
            (&left.file, left.line, left.column).cmp(&(&right.file, right.line, right.column))
        });
        sites.dedup_by(|left, right| {
            left.file == right.file && left.line == right.line && left.column == right.column
        });
        sites
    }
}

/// One written declaration site and every slang address it owns.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Identity {
    /// The declared identifier.
    pub name: String,
    /// The class of the site.
    pub class: SymbolClass,
    /// The enclosing instance body.
    pub scope: String,
    /// Absolute file.
    pub file: PathBuf,
    /// 0-based line.
    pub line: u32,
    /// 0-based byte column.
    pub column: u32,
    /// The slang addresses that this one site owns (Port + its internal Net).
    pub addrs: Vec<u64>,
}

#[derive(Debug, Clone)]
struct Site {
    file: PathBuf,
    line: u32,
    column: u32,
    end_column: u32,
}

/// Build the project AST index.
pub fn index(project: &Project) -> Result<RenameIndex, RenameError> {
    if project.files.is_empty() {
        return Err(RenameError::NoProject {
            message: "the file set is empty (open a project or pass its file list)".to_string(),
        });
    }
    let started = Instant::now();
    let options = HierarchyOptions {
        slang_binary: project.slang_binary.clone(),
        top: None,
        include_dirs: project.include_dirs.clone(),
        defines: project.defines.clone(),
        timeout: project.timeout,
        ..HierarchyOptions::default()
    };
    // The child runs in the FIRST file's parent (slang reports `source_file`
    // relative to it), so that directory must be REAL and absolute: a bare
    // relative file name has an empty parent, and `Command::current_dir("")`
    // fails with ENOENT — which used to be reported as a missing binary. Resolve
    // every file against the project root first, then prove the directory exists
    // BEFORE anything is spawned, so a spawn failure can no longer mean "the
    // working directory was not a path".
    let (files, cwd) = resolve_files(project)?;
    let mut index = RenameIndex {
        files,
        ..RenameIndex::default()
    };
    let (report, captured) = run_ast_json(&index.files, &options, |path, _exit, _stderr| {
        build_index(path, &cwd)
    });
    let Some(mut built) = captured else {
        return Err(match report.failure.as_ref() {
            Some(failure) => failure_to_error(failure),
            None => RenameError::Ast {
                message: "the AST document was not produced".to_string(),
            },
        });
    };
    built.elapsed = started.elapsed();
    index.declarations.append(&mut built.declarations);
    index.references.append(&mut built.references);
    index.warnings.append(&mut built.warnings);
    index.connected_ports.append(&mut built.connected_ports);
    index.hierarchical_refs.append(&mut built.hierarchical_refs);
    index.elapsed = started.elapsed();
    Ok(index)
}

/// Resolve the project's file set to ABSOLUTE paths and return the directory the
/// child runs in (the first file's parent), refusing an unusable directory
/// before any process starts.
fn resolve_files(project: &Project) -> Result<(Vec<PathBuf>, PathBuf), RenameError> {
    let current = || {
        std::env::current_dir().map_err(|error| RenameError::Io {
            message: format!("the current directory is unavailable: {error}"),
        })
    };
    let base = if project.root.as_os_str().is_empty() {
        current()?
    } else if project.root.is_absolute() {
        project.root.clone()
    } else {
        current()?.join(&project.root)
    };
    let files: Vec<PathBuf> = project
        .files
        .iter()
        .map(|file| {
            if file.is_absolute() {
                file.clone()
            } else {
                base.join(file)
            }
        })
        .collect();
    let cwd = files
        .first()
        .and_then(|file| file.parent())
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or(&base)
        .to_path_buf();
    match std::fs::metadata(&cwd) {
        Ok(metadata) if metadata.is_dir() => Ok((files, cwd)),
        Ok(_) => Err(RenameError::Io {
            message: format!(
                "the AST index cannot run: the project directory `{}` is not a directory",
                cwd.display()
            ),
        }),
        Err(error) => Err(RenameError::Io {
            message: format!(
                "the AST index cannot run: the project directory `{}` is not usable: {error}",
                cwd.display()
            ),
        }),
    }
}

/// Map the elaboration's typed failure onto the rename error it really is.
///
/// `BinaryNotFound` keeps its own code; a child that cannot start and every
/// filesystem problem stay path/IO — a spawn failure is never reported as
/// "slang is missing" again.
fn failure_to_error(failure: &LintFailure) -> RenameError {
    match failure {
        LintFailure::BinaryNotFound { .. } => RenameError::BinaryNotFound {
            message: failure.user_message(),
        },
        LintFailure::Spawn { .. } | LintFailure::Io { .. } => RenameError::Io {
            message: failure.user_message(),
        },
        _ => RenameError::Ast {
            message: failure.user_message(),
        },
    }
}

/// Parse one `--ast-json` document into declarations + references.
fn build_index(path: &Path, cwd: &Path) -> Result<RenameIndex, LintFailure> {
    let text = std::fs::read_to_string(path).map_err(|error| LintFailure::Io {
        message: format!("reading {}: {error}", path.display()),
    })?;
    let document: Value = serde_json::from_str(&text).map_err(|error| LintFailure::Io {
        message: format!("parsing {}: {error}", path.display()),
    })?;
    let mut index = RenameIndex::default();
    let mut scope: Vec<String> = Vec::new();
    walk(&document, &mut scope, &mut index, cwd);
    Ok(index)
}

/// Walk every object of the document, keeping declarations and references.
fn walk(node: &Value, scope: &mut Vec<String>, index: &mut RenameIndex, cwd: &Path) {
    match node {
        Value::Object(map) => {
            let kind = map.get("kind").and_then(Value::as_str).unwrap_or("");
            let entered_scope = kind == "InstanceBody";
            if entered_scope {
                let name = map
                    .get("name")
                    .and_then(Value::as_str)
                    .unwrap_or("")
                    .to_string();
                scope.push(name);
            }
            match kind {
                "Variable" | "Net" | "Port" | "Parameter" | "LocalParameter" | "Instance" => {
                    if let Some(declaration) = declaration_of(map, scope, cwd) {
                        index.declarations.push(declaration);
                    }
                    if kind == "Instance" {
                        collect_connections(map, index);
                    }
                }
                "NamedValue" => match reference_of(map, cwd) {
                    Ok(reference) => index.references.push(reference),
                    Err(reason) => index.warnings.push(reason),
                },
                "HierarchicalValue" => match hierarchical_leaf(map) {
                    Ok(addr) => {
                        index.hierarchical_refs.insert(addr);
                    }
                    Err(reason) => index.warnings.push(reason),
                },
                _ => {}
            }
            for value in map.values() {
                walk(value, scope, index, cwd);
            }
            if entered_scope {
                scope.pop();
            }
        }
        Value::Array(items) => {
            for item in items {
                walk(item, scope, index, cwd);
            }
        }
        _ => {}
    }
}

/// Record the child-port ADDRESS of every instantiation connection.
///
/// The `.port` connection label itself has NO position in slang's AST JSON
/// (measured against the pinned 12.1 dump: a named connection serialises
/// `{port, expr}` only), so the label cannot be rewritten at the instantiation
/// site. The address is what lets [`plan`] refuse such a port whole.
fn collect_connections(map: &serde_json::Map<String, Value>, index: &mut RenameIndex) {
    let Some(Value::Array(connections)) = map.get("connections") else {
        return;
    };
    for connection in connections {
        let Some(addr) = connection
            .get("port")
            .and_then(|port| port.get("addr"))
            .and_then(Value::as_u64)
        else {
            continue;
        };
        index.connected_ports.insert(addr);
    }
}

/// The address a hierarchical reference binds — its LEAF symbol.
///
/// `u_a.sig` serialises a `HierarchicalValue` whose span covers the whole path
/// but whose `symbol` names only `sig`; the instance path has no binding at all,
/// which is why the leaf symbol is refused whole instead of half-renamed.
fn hierarchical_leaf(map: &serde_json::Map<String, Value>) -> Result<u64, String> {
    let symbol = map
        .get("symbol")
        .and_then(Value::as_str)
        .ok_or_else(|| "a HierarchicalValue carried no resolved symbol; skipped".to_string())?;
    symbol
        .split_whitespace()
        .next()
        .and_then(|addr| addr.parse::<u64>().ok())
        .ok_or_else(|| {
            format!("a HierarchicalValue symbol `{symbol}` is not `<addr> <name>`; skipped")
        })
}

fn declaration_of(
    map: &serde_json::Map<String, Value>,
    scope: &[String],
    cwd: &Path,
) -> Option<Declaration> {
    let kind = map.get("kind")?.as_str()?;
    let class = SymbolClass::from_kind(kind)?;
    let name = map.get("name")?.as_str()?;
    if name.is_empty() {
        return None;
    }
    let addr = map.get("addr")?.as_u64()?;
    let file = absolute(cwd, Path::new(map.get("source_file")?.as_str()?));
    let line = number(map, "source_line")?;
    let column = number(map, "source_column")?;
    Some(Declaration {
        addr,
        class,
        name: name.to_string(),
        file,
        line: line.saturating_sub(1),
        column: column.saturating_sub(1),
        scope: scope.join("."),
    })
}

fn reference_of(map: &serde_json::Map<String, Value>, cwd: &Path) -> Result<Reference, String> {
    let symbol = map
        .get("symbol")
        .and_then(Value::as_str)
        .ok_or_else(|| "a NamedValue carried no resolved symbol; skipped".to_string())?;
    let mut parts = symbol.splitn(2, ' ');
    let addr = parts
        .next()
        .and_then(|text| text.parse::<u64>().ok())
        .ok_or_else(|| format!("NamedValue symbol `{symbol}` has no address; skipped"))?;
    let file = map
        .get("source_file_start")
        .and_then(Value::as_str)
        .ok_or_else(|| "a NamedValue carried no start file; skipped".to_string())?;
    let file = absolute(cwd, Path::new(file));
    let line = number(map, "source_line_start")
        .ok_or_else(|| "a NamedValue carried no start line; skipped".to_string())?;
    let column = number(map, "source_column_start")
        .ok_or_else(|| "a NamedValue carried no start column; skipped".to_string())?;
    let end_column = number(map, "source_column_end")
        .ok_or_else(|| "a NamedValue carried no end column; skipped".to_string())?;
    let end_line = number(map, "source_line_end").unwrap_or(line);
    if end_line != line {
        return Err("a multi-line reference is not a rename site; skipped".to_string());
    }
    Ok(Reference {
        addr,
        file,
        line: line.saturating_sub(1),
        column: column.saturating_sub(1),
        end_column: end_column.saturating_sub(1),
    })
}

/// Make a slang-reported path absolute against the run's working directory.
fn absolute(cwd: &Path, file: &Path) -> PathBuf {
    if file.is_absolute() {
        file.to_path_buf()
    } else {
        cwd.join(file)
    }
}

fn number(map: &serde_json::Map<String, Value>, key: &str) -> Option<u32> {
    map.get(key)?
        .as_u64()
        .and_then(|value| u32::try_from(value).ok())
}

/// Resolve a name to exactly one declaration SITE.
///
/// Two declarations at one position (a port and its internal net) are ONE
/// identity; two sites with the same name (same-name symbols in different
/// modules) are ambiguous and are refused with every site's `file:line`.
pub fn resolve(index: &RenameIndex, from: &str) -> Result<Identity, RenameError> {
    let matches: Vec<Identity> = index
        .identities()
        .into_iter()
        .filter(|identity| identity.name == from)
        .collect();
    match matches.len() {
        0 => Err(RenameError::NotNameable {
            name: from.to_string(),
            message: index.not_nameable_reason(from),
        }),
        1 => Ok(matches
            .into_iter()
            .next()
            .unwrap_or_else(|| unreachable!("one match"))),
        _ => Err(RenameError::Ambiguous {
            name: from.to_string(),
            declarations: matches
                .iter()
                .map(|identity| {
                    format!(
                        "{}:{} ({})",
                        identity.file.display(),
                        identity.line + 1,
                        identity.class.as_str()
                    )
                })
                .collect(),
        }),
    }
}

impl RenameIndex {
    fn not_nameable_reason(&self, from: &str) -> String {
        if is_legal_identifier(from) {
            "no declaration with this name is in the indexed file set".to_string()
        } else {
            "not a SystemVerilog identifier".to_string()
        }
    }
}

/// True when `name` matches `[A-Za-z_][A-Za-z0-9_$]*`.
pub fn is_legal_identifier(name: &str) -> bool {
    let mut chars = name.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    if !(first.is_ascii_alphabetic() || first == '_') {
        return false;
    }
    chars.all(|ch| ch.is_ascii_alphanumeric() || ch == '_' || ch == '$')
}

/// SystemVerilog keywords this engine refuses as a NEW name.
///
/// A keyword is never a legal declaration name, so renaming to one could only
/// break the project; the list is the reserved set a user is realistically about
/// to type.
const KEYWORDS: &[&str] = &[
    "alias",
    "always",
    "always_comb",
    "always_ff",
    "always_latch",
    "and",
    "assert",
    "assign",
    "automatic",
    "begin",
    "bit",
    "break",
    "byte",
    "case",
    "casex",
    "casez",
    "class",
    "const",
    "continue",
    "cover",
    "default",
    "do",
    "else",
    "end",
    "endcase",
    "endclass",
    "endfunction",
    "endgenerate",
    "endmodule",
    "endpackage",
    "endtask",
    "enum",
    "event",
    "final",
    "for",
    "force",
    "foreach",
    "forever",
    "fork",
    "function",
    "generate",
    "genvar",
    "if",
    "import",
    "initial",
    "inout",
    "input",
    "int",
    "integer",
    "interface",
    "join",
    "localparam",
    "logic",
    "longint",
    "module",
    "nand",
    "negedge",
    "new",
    "nor",
    "not",
    "null",
    "or",
    "output",
    "package",
    "parameter",
    "posedge",
    "program",
    "property",
    "reg",
    "release",
    "repeat",
    "return",
    "shortint",
    "signed",
    "static",
    "string",
    "struct",
    "super",
    "task",
    "this",
    "time",
    "typedef",
    "union",
    "unsigned",
    "var",
    "virtual",
    "void",
    "wait",
    "while",
    "wire",
    "xnor",
    "xor",
];

/// Check a proposed new name; `Ok(())` when it is usable.
pub fn check_new_name(to: &str) -> Result<(), RenameError> {
    if !is_legal_identifier(to) {
        return Err(RenameError::IllegalName {
            reason: format!("`{to}` is not an identifier ([A-Za-z_][A-Za-z0-9_$]*)"),
        });
    }
    if KEYWORDS.contains(&to) {
        return Err(RenameError::IllegalName {
            reason: format!("`{to}` is a SystemVerilog keyword"),
        });
    }
    Ok(())
}

/// Build a per-file plan for renaming `from` to `to`.
///
/// `from == to` is an explicit [`PlanOutcome::Noop`]; an ambiguous, unnameable or
/// unsupported symbol, an illegal new name and a same-scope conflict are all
/// typed errors. Nothing is written here.
pub fn plan(
    project: &Project,
    index: &RenameIndex,
    from: &str,
    to: &str,
    max_entries_per_file: usize,
) -> Result<PlanOutcome, RenameError> {
    if from == to {
        return Ok(PlanOutcome::Noop {
            name: from.to_string(),
        });
    }
    check_new_name(to)?;
    let identity = resolve(index, from)?;
    // The MEASURED unresolvable categories are refused WHOLE — never a partial
    // rename and never a text replacement (the module doc records the dumps):
    // a parameter may be used as a type dimension, an instance name may be used
    // hierarchically, and a port may be referenced by an instantiation's
    // connection label. None of those positions exists in the AST JSON.
    match identity.class {
        SymbolClass::Parameter => {
            return Err(RenameError::Unsupported {
                kind: "parameter/constant (a type-dimension use of it has no AST position, so the rename would leave `[WIDTH-1:0]` behind)"
                    .to_string(),
            })
        }
        SymbolClass::Instance => {
            return Err(RenameError::Unsupported {
                kind: "instance name (the instance prefix of a hierarchical reference `u_a.sig` has no AST binding, so the rename would leave it behind)"
                    .to_string(),
            })
        }
        SymbolClass::Signal => {}
    }
    if identity
        .addrs
        .iter()
        .any(|addr| index.connected_ports.contains(addr))
    {
        return Err(RenameError::Unsupported {
            kind: "port connected at an instantiation site (the `.port` connection label has no AST position, so the rename would leave it behind)"
                .to_string(),
        });
    }
    if identity
        .addrs
        .iter()
        .any(|addr| index.hierarchical_refs.contains(addr))
    {
        return Err(RenameError::Unsupported {
            kind: "hierarchically referenced symbol (`u_a.sig`: the AST binds only the leaf name, never the instance path, so the rename would leave the path behind)"
                .to_string(),
        });
    }
    for other in index.identities() {
        let overlaps = other.addrs.iter().any(|addr| identity.addrs.contains(addr));
        if !overlaps && other.name == to && other.scope == identity.scope {
            return Err(RenameError::Conflict {
                name: to.to_string(),
                declaration: format!("{}:{}", other.file.display(), other.line + 1),
            });
        }
    }
    let sites = index.sites_of(&identity);
    if sites.is_empty() {
        return Err(RenameError::NotNameable {
            name: from.to_string(),
            message: "the declaration has no recorded occurrence".to_string(),
        });
    }
    let mut by_file: BTreeMap<PathBuf, Vec<Site>> = BTreeMap::new();
    for site in sites {
        by_file.entry(site.file.clone()).or_default().push(site);
    }
    let mut plans: Vec<FilePlan> = Vec::new();
    let mut occurrences = 0usize;
    let warnings = index.warnings.clone();
    for (file, mut file_sites) in by_file {
        let absolute = absolute_for(&project.root, &file);
        let text = std::fs::read_to_string(&absolute).map_err(|error| RenameError::Io {
            message: format!("{}: {error}", absolute.display()),
        })?;
        let lines = LineTable::new(&text);
        file_sites.sort_by_key(|site| (site.line, site.column));
        let mut hits: Vec<Occurrence> = Vec::new();
        let mut truncated = false;
        let cap = if max_entries_per_file == 0 {
            usize::MAX
        } else {
            max_entries_per_file
        };
        for site in &file_sites {
            if hits.len() >= cap {
                truncated = true;
                continue;
            }
            let hit = occurrence(&absolute, &text, &lines, site, from, to)?;
            hits.push(hit);
        }
        occurrences += file_sites.len();
        plans.push(FilePlan {
            file: absolute,
            hits,
            truncated,
        });
    }
    Ok(PlanOutcome::Plan(RenamePlan {
        root: project.root.clone(),
        from: from.to_string(),
        to: to.to_string(),
        files: index.files.len(),
        occurrences,
        plans,
        warnings,
    }))
}

fn absolute_for(root: &Path, file: &Path) -> PathBuf {
    if file.is_absolute() {
        file.to_path_buf()
    } else {
        root.join(file)
    }
}

fn occurrence(
    absolute: &Path,
    text: &str,
    lines: &LineTable,
    site: &Site,
    from: &str,
    to: &str,
) -> Result<Occurrence, RenameError> {
    let line_text = lines.line(site.line).ok_or_else(|| RenameError::Io {
        message: format!(
            "{}: line {} is out of range",
            absolute.display(),
            site.line + 1
        ),
    })?;
    // A scoped reference (`cfg::DEPTH`) spans the whole name, so the identifier
    // itself starts at the LAST occurrence of `from` inside the span.
    let span_start = site.column as usize;
    let span_end = (site.end_column as usize).min(line_text.len());
    let span_end = span_end.max(span_start);
    let span = line_text.get(span_start..span_end).unwrap_or("");
    let offset_in_span = if span == from {
        0
    } else {
        span.rfind(from).ok_or_else(|| RenameError::Io {
            message: format!(
                "{}:{}: the recorded span `{span}` does not contain `{from}`",
                absolute.display(),
                site.line + 1
            ),
        })?
    };
    let column = span_start + offset_in_span;
    let site = &Site {
        file: site.file.clone(),
        line: site.line,
        column: column as u32,
        end_column: (column + from.len()) as u32,
    };
    let byte_offset = lines.byte_offset(site.line, site.column);
    let expected_len = from.len();
    let actual = text
        .get(byte_offset as usize..(byte_offset as usize + expected_len))
        .ok_or_else(|| RenameError::Io {
            message: format!(
                "{}: {}:{} is outside the file",
                absolute.display(),
                site.line + 1,
                site.column + 1
            ),
        })?;
    if actual != from {
        return Err(RenameError::Io {
            message: format!(
                "{}:{}: expected `{from}` at the recorded position, found `{actual}`",
                absolute.display(),
                site.line + 1
            ),
        });
    }
    let before = render_line(line_text);
    let mut after_text = String::with_capacity(line_text.len() + to.len());
    after_text.push_str(&line_text[..site.column as usize]);
    after_text.push_str(to);
    after_text.push_str(&line_text[site.column as usize + expected_len..]);
    let after = render_line(&after_text);
    let utf16_start = utf16_len(&line_text[..site.column as usize]);
    let utf16_end = utf16_start + utf16_len(from);
    Ok(Occurrence {
        file: absolute.to_path_buf(),
        line: site.line,
        column: utf16_start,
        end_column: utf16_end,
        byte_offset,
        byte_len: expected_len as u64,
        before,
        after,
    })
}

fn render_line(line: &str) -> String {
    let trimmed = line.trim();
    if trimmed.chars().count() <= MAX_LINE_CHARS {
        trimmed.to_string()
    } else {
        let cut: String = trimmed.chars().take(MAX_LINE_CHARS).collect();
        format!("{cut}…")
    }
}

fn utf16_len(text: &str) -> u32 {
    text.encode_utf16().count() as u32
}

/// Byte offsets of every line, plus their text.
struct LineTable<'a> {
    text: &'a str,
    starts: Vec<u32>,
}

impl<'a> LineTable<'a> {
    fn new(text: &'a str) -> Self {
        let mut starts = vec![0u32];
        for (index, byte) in text.bytes().enumerate() {
            if byte == b'\n' {
                starts.push((index + 1) as u32);
            }
        }
        Self { text, starts }
    }

    fn line(&self, line: u32) -> Option<&'a str> {
        let start = *self.starts.get(line as usize)? as usize;
        let end = self
            .starts
            .get(line as usize + 1)
            .map_or(self.text.len(), |next| *next as usize);
        let slice = self.text.get(start..end)?;
        Some(slice.strip_suffix('\n').unwrap_or(slice))
    }

    fn byte_offset(&self, line: u32, column: u32) -> u64 {
        self.starts
            .get(line as usize)
            .map_or(0, |start| *start as u64 + column as u64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    type TestResult = Result<(), Box<dyn std::error::Error>>;

    fn fixture_root() -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/rename")
    }

    fn slang_binary() -> Option<PathBuf> {
        let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
        let candidate = repo.join("build/slang/out/slang");
        candidate.is_file().then_some(candidate)
    }

    fn project() -> Project {
        let root = fixture_root();
        let mut project = Project::new(
            &root,
            vec![
                root.join("rtl/pkg.sv"),
                root.join("rtl/alpha.sv"),
                root.join("rtl/beta.sv"),
                root.join("rtl/gamma.sv"),
            ],
        );
        project.include_dirs = vec![root.join("inc")];
        project.slang_binary = slang_binary();
        project
    }

    fn build() -> Result<RenameIndex, String> {
        index(&project()).map_err(|error| error.message())
    }

    fn message(error: RenameError) -> String {
        error.message()
    }

    fn refusal<T: std::fmt::Debug>(value: Result<T, RenameError>) -> Result<RenameError, String> {
        match value {
            Ok(_) => Err(format!("expected a refusal, got {value:?}")),
            Err(error) => Ok(error),
        }
    }

    #[test]
    fn the_fixture_indexes_the_three_symbol_classes() -> TestResult {
        let index = build()?;
        let classes: Vec<SymbolClass> = index.declarations.iter().map(|decl| decl.class).collect();
        assert!(classes.contains(&SymbolClass::Signal));
        assert!(classes.contains(&SymbolClass::Parameter));
        assert!(classes.contains(&SymbolClass::Instance));
        assert!(index.summary().files >= 3);
        Ok(())
    }

    #[test]
    fn a_unique_signal_resolves_to_exactly_one_declaration() -> TestResult {
        let index = build()?;
        let identity = resolve(&index, "alpha_cnt").map_err(message)?;
        assert_eq!(identity.class, SymbolClass::Signal);
        assert!(identity.file.ends_with("rtl/alpha.sv"));
        Ok(())
    }

    #[test]
    fn a_unique_parameter_and_instance_resolve() -> TestResult {
        let index = build()?;
        assert_eq!(
            resolve(&index, "WIDTH").map_err(message)?.class,
            SymbolClass::Parameter
        );
        assert_eq!(
            resolve(&index, "u_alpha").map_err(message)?.class,
            SymbolClass::Instance
        );
        Ok(())
    }

    #[test]
    fn a_package_constant_reaches_its_references_in_other_files() -> TestResult {
        let index = build()?;
        let identity = resolve(&index, "DEPTH").map_err(message)?;
        assert_eq!(identity.class, SymbolClass::Parameter);
        assert!(identity.file.ends_with("rtl/pkg.sv"));
        let files: Vec<String> = index
            .sites_of(&identity)
            .iter()
            .map(|site| site.file.display().to_string())
            .collect();
        for expected in ["pkg.sv", "alpha.sv", "beta.sv"] {
            assert!(
                files.iter().any(|file| file.ends_with(expected)),
                "{files:?}"
            );
        }
        Ok(())
    }

    #[test]
    fn a_parameter_rename_is_refused_as_unsupported() -> TestResult {
        // Measured: type-dimension uses of a parameter have no AST position, so
        // renaming the declaration alone would leave `[WIDTH-1:0]` behind.
        let index = build()?;
        let error = refusal(plan(&project(), &index, "WIDTH", "WIDTH_NEW", 200))?;
        assert_eq!(error.code(), "unsupported");
        assert!(
            error.message().contains("type-dimension"),
            "{}",
            error.message()
        );
        Ok(())
    }

    #[test]
    fn same_name_globals_in_other_modules_are_ambiguous_never_renamed() -> TestResult {
        let index = build()?;
        let error = refusal(resolve(&index, "cnt"))?;
        assert_eq!(error.code(), "ambiguous");
        assert!(error.message().contains("alpha.sv"), "{}", error.message());
        assert!(error.message().contains("gamma.sv"), "{}", error.message());
        Ok(())
    }

    #[test]
    fn a_name_outside_the_project_is_not_nameable() -> TestResult {
        let index = build()?;
        let error = refusal(resolve(&index, "only_in_the_other_project"))?;
        assert_eq!(error.code(), "not_nameable");
        Ok(())
    }

    #[test]
    fn comments_and_strings_are_not_occurrences() -> TestResult {
        let index = build()?;
        let identity = resolve(&index, "alpha_cnt").map_err(message)?;
        let sites = index.sites_of(&identity);
        assert_eq!(sites.len(), 3, "declaration + two references");
        Ok(())
    }

    #[test]
    fn same_name_is_an_explicit_noop() -> TestResult {
        let index = build()?;
        let outcome = plan(&project(), &index, "alpha_cnt", "alpha_cnt", 200).map_err(message)?;
        assert!(matches!(outcome, PlanOutcome::Noop { .. }));
        Ok(())
    }

    #[test]
    fn an_illegal_new_name_is_rejected() -> TestResult {
        let index = build()?;
        for bad in ["9lives", "has space", "end", "wire"] {
            let error = refusal(plan(&project(), &index, "alpha_cnt", bad, 200))?;
            assert_eq!(error.code(), "illegal_name", "{bad}");
        }
        Ok(())
    }

    #[test]
    fn a_conflicting_new_name_names_the_declaration() -> TestResult {
        let index = build()?;
        let error = refusal(plan(&project(), &index, "alpha_cnt", "din", 200))?;
        assert_eq!(error.code(), "conflict");
        assert!(error.message().contains("alpha.sv"), "{}", error.message());
        Ok(())
    }

    #[test]
    fn the_plan_counts_every_occurrence_and_renders_before_after() -> TestResult {
        let index = build()?;
        let outcome = plan(&project(), &index, "alpha_cnt", "alpha_count", 200).map_err(message)?;
        let PlanOutcome::Plan(plan) = outcome else {
            return Err("expected a plan".into());
        };
        assert_eq!(plan.occurrences, 3);
        let hit = plan
            .plans
            .iter()
            .flat_map(|file| file.hits.iter())
            .next()
            .ok_or("one hit")?;
        assert!(hit.before.contains("alpha_cnt"));
        assert!(hit.after.contains("alpha_count"));
        assert_eq!(hit.line, 8, "0-based line of the declaration");
        Ok(())
    }

    #[test]
    fn identifier_validation_matches_systemverilog() {
        assert!(is_legal_identifier("abc_def9"));
        assert!(is_legal_identifier("_x"));
        assert!(!is_legal_identifier(""));
        assert!(!is_legal_identifier("9abc"));
        assert!(!is_legal_identifier("a-b"));
    }

    /// A project whose ONE file is given as a bare relative name: its parent is
    /// the empty path, which is the shape that produced the ENOENT defect.
    fn relative_project() -> Result<(tempfile::TempDir, Project), String> {
        let temp = tempfile::tempdir().map_err(|error| error.to_string())?;
        let text = "module only (input logic clk, output logic q);\n  logic sole_sig;\n  always_ff @(posedge clk) sole_sig <= clk;\n  assign q = sole_sig;\nendmodule\n";
        std::fs::write(temp.path().join("only.sv"), text).map_err(|error| error.to_string())?;
        let mut project = Project::new(temp.path(), vec![PathBuf::from("only.sv")]);
        project.slang_binary = slang_binary();
        Ok((temp, project))
    }

    /// Regression (REAL spawn, not an assertion tweak): a relative file name used
    /// to make the child's `current_dir` the empty path, so `spawn` failed with
    /// ENOENT and the failure was reported as if slang were missing. The child
    /// must run in the project's real directory and the index must build.
    #[test]
    fn a_relative_file_path_runs_the_child_in_a_real_directory() -> TestResult {
        let (_temp, project) = relative_project()?;
        let index = index(&project).map_err(message)?;
        let identity = resolve(&index, "sole_sig").map_err(message)?;
        println!(
            "RELATIVE-PATH index ok: files={} resolved_file={}",
            index.files.len(),
            identity.file.display()
        );
        assert!(identity.file.is_absolute(), "{:?}", identity.file);
        Ok(())
    }

    /// AC-2.2 keyword control: a reserved word is never a nameable symbol.
    #[test]
    fn a_keyword_is_never_nameable() -> TestResult {
        let index = build()?;
        let error = refusal(resolve(&index, "end"))?;
        assert_eq!(error.code(), "not_nameable");
        assert!(
            error.message().contains("not a nameable symbol"),
            "{}",
            error.message()
        );
        Ok(())
    }

    /// AC-2.2 / AC-2.5 negative control: a comment or a string literal that
    /// contains the identifier is not an occurrence, even inside a file that IS
    /// in the index (the fixture's string lives in a file outside `project.f`,
    /// so this scratch project closes that gap).
    #[test]
    fn a_string_and_a_comment_in_an_indexed_file_are_not_occurrences() -> TestResult {
        let temp = tempfile::tempdir()?;
        let path = temp.path().join("str_ctrl.sv");
        std::fs::write(
            &path,
            "module str_ctrl (input logic clk);\n  logic uniq_mark;\n  // uniq_mark is only a comment here\n  string s = \"uniq_mark\";\n  always_ff @(posedge clk) uniq_mark <= clk;\nendmodule\n",
        )?;
        let mut project = Project::new(temp.path(), vec![path]);
        project.slang_binary = slang_binary();
        let index = index(&project).map_err(message)?;
        let identity = resolve(&index, "uniq_mark").map_err(message)?;
        let sites = index.sites_of(&identity);
        assert_eq!(
            sites.len(),
            2,
            "declaration + the one real reference only (comment/string excluded): {sites:?}"
        );
        Ok(())
    }

    /// A defect regression for the failure CLASSIFICATION: a missing binary and a
    /// path/IO problem must not share a code, or the user is sent to reinstall
    /// slang when the real problem is the project directory.
    #[test]
    fn failure_classes_keep_binary_missing_apart_from_path_io() {
        let missing_binary = LintFailure::BinaryNotFound {
            attempts: Vec::new(),
        };
        println!(
            "FAILURE-CLASS binary-missing code={} message={}",
            failure_to_error(&missing_binary).code(),
            failure_to_error(&missing_binary).message()
        );
        assert_eq!(failure_to_error(&missing_binary).code(), "binary_not_found");
        let spawn = LintFailure::Spawn {
            message: "/x/slang: permission denied".to_string(),
        };
        assert_eq!(failure_to_error(&spawn).code(), "io");
        let io = LintFailure::Io {
            message: "the project directory is not usable".to_string(),
        };
        assert_eq!(failure_to_error(&io).code(), "io");
        let timeout = LintFailure::Timeout {
            after: Duration::from_secs(1),
        };
        for (label, failure) in [("spawn", &spawn), ("io", &io), ("timeout", &timeout)] {
            println!(
                "FAILURE-CLASS {label} code={}",
                failure_to_error(failure).code()
            );
        }
        assert_eq!(failure_to_error(&spawn).code(), "io");
        assert_eq!(failure_to_error(&timeout).code(), "ast");
    }

    /// The same defect at the spawn boundary: a project directory that does not
    /// exist is a PATH error and is refused before any child runs.
    #[test]
    fn a_missing_project_directory_is_path_io_not_a_missing_binary() -> TestResult {
        let temp = tempfile::tempdir()?;
        let missing = temp.path().join("no_such_project_dir");
        let mut project = Project::new(&missing, vec![missing.join("rtl/only.sv")]);
        project.slang_binary = slang_binary();
        let error = refusal(index(&project))?;
        println!(
            "MISSING-PROJECT code={} message={}",
            error.code(),
            error.message()
        );
        assert_eq!(error.code(), "io", "{}", error.message());
        assert!(
            error.message().contains("no_such_project_dir"),
            "{}",
            error.message()
        );
        Ok(())
    }

    /// R2-1 (measured): a hierarchical reference to an instance name serialises
    /// NO reference node, so an instance rename is refused whole — the old
    /// "declare it, warn about the rest" partial rename is gone.
    #[test]
    fn an_instance_rename_is_refused_wholesale() -> TestResult {
        let index = build()?;
        let error = refusal(plan(&project(), &index, "u_alpha", "u_alpha_x", 200))?;
        assert_eq!(error.code(), "unsupported");
        assert!(
            error.message().contains("hierarchical"),
            "{}",
            error.message()
        );
        Ok(())
    }

    /// A scratch project with ONE child module instantiated twice: a NAMED
    /// connection (`.child_p(...)`) and a POSITIONAL one. Both connection
    /// expressions reference the parent's signal `link_sig` by identity.
    fn connection_project() -> Result<(tempfile::TempDir, Project), String> {
        let temp = tempfile::tempdir().map_err(|error| error.to_string())?;
        std::fs::write(
            temp.path().join("child.sv"),
            "module child (input logic child_p, output logic child_q);\n  assign child_q = child_p;\nendmodule\n",
        )
        .map_err(|error| error.to_string())?;
        std::fs::write(
            temp.path().join("top.sv"),
            "module top (input logic clk, output logic q);\n  logic link_sig;\n  logic named_out;\n  logic positional_out;\n  child u_named (.child_p(link_sig), .child_q(named_out));\n  child u_positional (link_sig, positional_out);\n  assign link_sig = clk;\n  assign q = named_out;\nendmodule\n",
        )
        .map_err(|error| error.to_string())?;
        let mut project = Project::new(
            temp.path(),
            vec![temp.path().join("child.sv"), temp.path().join("top.sv")],
        );
        project.slang_binary = slang_binary();
        Ok((temp, project))
    }

    /// AC-2.6 / R2-1 (measured): the `.child_p` label of a named connection has
    /// NO AST position (the dump carries `{port, expr}` only), so renaming the
    /// child port would leave the label behind. The port is refused WHOLE.
    #[test]
    fn a_connected_port_is_refused_wholesale() -> TestResult {
        let (_temp, project) = connection_project()?;
        let index = index(&project).map_err(message)?;
        let error = refusal(plan(&project, &index, "child_p", "child_port", 200))?;
        assert_eq!(error.code(), "unsupported");
        assert!(
            error.message().contains("instantiation"),
            "{}",
            error.message()
        );
        Ok(())
    }

    /// AC-2.6 second half: a signal referenced BY a connection expression (named
    /// and positional) is still renamed by SYMBOL IDENTITY — the connection
    /// label is the only unlocatable part, not the expression.
    #[test]
    fn a_signal_in_named_and_positional_connections_renames_by_identity() -> TestResult {
        let (_temp, project) = connection_project()?;
        let index = index(&project).map_err(message)?;
        let outcome = plan(&project, &index, "link_sig", "link_net", 200).map_err(message)?;
        let PlanOutcome::Plan(plan) = outcome else {
            return Err("expected a plan".into());
        };
        // declaration + named-connection expression + positional-connection
        // expression + `assign link_sig = clk;`
        assert_eq!(plan.occurrences, 4);
        assert_eq!(plan.plans.len(), 1, "one file owns every occurrence");
        Ok(())
    }

    /// A scratch project with a TRUE hierarchical reference (`u_child.child_q`).
    fn hierarchical_project() -> Result<(tempfile::TempDir, Project), String> {
        let temp = tempfile::tempdir().map_err(|error| error.to_string())?;
        std::fs::write(
            temp.path().join("child.sv"),
            "module child (input logic child_p, output logic child_q);\n  logic child_mark;\n  assign child_mark = child_p;\n  assign child_q = child_mark;\nendmodule\n",
        )
        .map_err(|error| error.to_string())?;
        std::fs::write(
            temp.path().join("top.sv"),
            "module top (input logic clk, output logic q);\n  logic w;\n  child u_child (.child_p(clk), .child_q(w));\n  assign q = u_child.child_mark;\nendmodule\n",
        )
        .map_err(|error| error.to_string())?;
        let mut project = Project::new(
            temp.path(),
            vec![temp.path().join("child.sv"), temp.path().join("top.sv")],
        );
        project.slang_binary = slang_binary();
        Ok((temp, project))
    }

    /// R2-1 (measured): `u_child.child_q` serialises a `HierarchicalValue` that
    /// binds the LEAF only, and the instance NAME has no reference node at all.
    /// Both are refused whole — never a leaf-only edit that leaves the path stale.
    #[test]
    fn a_hierarchically_referenced_symbol_is_refused_wholesale() -> TestResult {
        let (_temp, project) = hierarchical_project()?;
        let index = index(&project).map_err(message)?;
        let error = refusal(plan(&project, &index, "child_mark", "child_mark_x", 200))?;
        assert_eq!(error.code(), "unsupported");
        assert!(
            error.message().contains("hierarchical"),
            "{}",
            error.message()
        );
        let error = refusal(plan(&project, &index, "u_child", "u_child_x", 200))?;
        assert_eq!(error.code(), "unsupported");
        assert!(
            error.message().contains("hierarchical"),
            "{}",
            error.message()
        );
        Ok(())
    }

    /// The evidence line for R2-1: every measured unresolvable category is
    /// refused WHOLE, and the reason names the missing AST position. The test
    /// prints one `REFUSE` row per category so the log itself carries the fact.
    #[test]
    fn every_measured_unresolvable_category_is_refused_whole() -> TestResult {
        let fixture = project();
        let fixture_index = index(&fixture).map_err(message)?;
        let (connection_temp, connection_project) = connection_project()?;
        let connection_index = index(&connection_project).map_err(message)?;
        let (hierarchical_temp, hierarchical_project) = hierarchical_project()?;
        let hierarchical_index = index(&hierarchical_project).map_err(message)?;
        let cases: [(&str, &Project, &RenameIndex, &str, &str); 4] = [
            (
                "type-dimension (parameter)",
                &fixture,
                &fixture_index,
                "WIDTH",
                "WIDTH_NEW",
            ),
            (
                "instance name",
                &fixture,
                &fixture_index,
                "u_alpha",
                "u_alpha_new",
            ),
            (
                "connection label (port)",
                &connection_project,
                &connection_index,
                "child_p",
                "child_port",
            ),
            (
                "hierarchical reference (leaf)",
                &hierarchical_project,
                &hierarchical_index,
                "child_mark",
                "child_mark_x",
            ),
        ];
        for (category, project, rename_index, from, to) in cases {
            let error = refusal(plan(project, rename_index, from, to, 200))?;
            println!(
                "REFUSE category={category} symbol={from} code={} reason={}",
                error.code(),
                error.message()
            );
            assert_eq!(error.code(), "unsupported", "{category}");
        }
        drop(connection_temp);
        drop(hierarchical_temp);
        Ok(())
    }

    #[test]
    fn an_empty_file_set_is_an_explicit_no_project() -> TestResult {
        let error = refusal(index(&Project::new("nowhere", Vec::new())))?;
        assert_eq!(error.code(), "no_project");
        Ok(())
    }

    #[test]
    fn the_line_table_maps_byte_columns() {
        let text = "  /* 你好 */ logic cnt;\n next\n";
        let lines = LineTable::new(text);
        assert_eq!(lines.line(0), Some("  /* 你好 */ logic cnt;"));
        let column = "  /* 你好 */ logic ".len() as u32;
        assert_eq!(lines.byte_offset(0, column), column as u64);
    }

    #[test]
    fn spike_print_hit_lists() -> TestResult {
        let index = build()?;
        println!("SPIKE files={}", index.files.len());
        for class in [
            SymbolClass::Signal,
            SymbolClass::Parameter,
            SymbolClass::Instance,
        ] {
            let mut names: Vec<&str> = index
                .declarations
                .iter()
                .filter(|declaration| declaration.class == class)
                .map(|declaration| declaration.name.as_str())
                .collect();
            names.sort_unstable();
            names.dedup();
            println!("SPIKE class={} names={:?}", class.as_str(), names);
            for name in names {
                match resolve(&index, name) {
                    Ok(identity) => {
                        let sites = index.sites_of(&identity);
                        println!(
                            "SPIKE   {name} -> {}:{} occurrences={}",
                            identity.file.display(),
                            identity.line + 1,
                            sites.len()
                        );
                        for site in sites {
                            println!(
                                "SPIKE     {}:{}:{}",
                                site.file.display(),
                                site.line + 1,
                                site.column + 1
                            );
                        }
                    }
                    Err(error) => {
                        println!("SPIKE   {name} -> {}: {}", error.code(), error.message())
                    }
                }
            }
        }
        for control in ["cnt", "only_in_the_other_project", "defs"] {
            match resolve(&index, control) {
                Ok(identity) => println!(
                    "SPIKE control {control} -> resolves at {}:{}",
                    identity.file.display(),
                    identity.line + 1
                ),
                Err(error) => {
                    println!(
                        "SPIKE control {control} -> {}: {}",
                        error.code(),
                        error.message()
                    )
                }
            }
        }
        Ok(())
    }

    #[test]
    fn two_hundred_files_index_and_plan_within_budget() -> TestResult {
        let temp = tempfile::tempdir()?;
        let mut files = Vec::new();
        for module in 0..200 {
            let path = temp.path().join(format!("m{module:03}.sv"));
            let text = format!(
                "module m{module:03} (input logic clk, output logic q);\n  logic [7:0] wide_sig_{module:03};\n  always_ff @(posedge clk) wide_sig_{module:03} <= 8'h0;\n  assign q = wide_sig_{module:03}[0];\nendmodule\n"
            );
            std::fs::write(&path, text)?;
            files.push(path);
        }
        let mut budget = Project::new(temp.path(), files);
        budget.slang_binary = slang_binary();
        let started = std::time::Instant::now();
        let index = index(&budget).map_err(message)?;
        let index_elapsed = started.elapsed();
        let started = std::time::Instant::now();
        let outcome =
            plan(&budget, &index, "wide_sig_000", "wide_sig_renamed", 200).map_err(message)?;
        let plan_elapsed = started.elapsed();
        let PlanOutcome::Plan(plan) = outcome else {
            return Err("expected a plan".into());
        };
        println!(
            "TIMING files={} index_ms={} plan_ms={} occurrences={}",
            index.files.len(),
            index_elapsed.as_millis(),
            plan_elapsed.as_millis(),
            plan.occurrences
        );
        assert_eq!(index.files.len(), 200);
        assert!(plan.occurrences >= 3);
        assert!(
            index_elapsed.as_secs() < 120,
            "200-file index took {index_elapsed:?}"
        );
        Ok(())
    }
}
