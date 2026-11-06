//! Project-wide module catalog (ux-batch-2 A1).
//!
//! Source of truth is the same `slang --ast-json --ast-json-source-info`
//! document the hierarchy report consumes ([`crate::hierarchy::run_ast_json`]);
//! the two views differ in what they keep:
//!
//! * the hierarchy report is the **elaborated tree** — one node per instance,
//!   hierarchical paths, drivers and loads;
//! * the catalog is keyed by module **definition** (`InstanceBody`): definitions
//!   are deduplicated by name, every module carries its ports and the child
//!   instantiations that appear inside it, and `is_top` means "this module is
//!   never instantiated anywhere" — so a module instantiated under two parents
//!   contributes one entry while BOTH parents' `children` list the edge.
//!
//! # What the catalog deliberately excludes (and why)
//!
//! An interface is not a module, but `slang` serialises an interface instance
//! (`bus_if bus();`) with its own `InstanceBody`, exactly like a module body, and
//! an interface-typed port is an `InterfacePort` member rather than a `Port`
//! member. Listing bodies without checking the declaration kind therefore puts
//! `bus_if` into the module tree. The kind comes from the AST's `definitions`
//! array (`definitionKind`: `Module`, `Interface`, `Package`, …) and is resolved
//! through the body's `definition` reference. When the document carries no
//! definition for a body (an older tool), the body is treated as a module so a
//! real design is never silently emptied.
//!
//! Interface **ports** are kept: an `InterfacePort` member is reported with
//! direction `InOut` (an interface port is bidirectional by construction) and
//! with the interface name as its `type`.
//!
//! # Positions
//!
//! 0-based lines and columns, like the hierarchy report and `VdDiagnostic`.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::PathBuf;
use std::time::Duration;

use serde_json::Value;

use crate::hierarchy::{
    join_path, node_kind, project_root, run_ast_json, scope_instances, symbol_position, Bodies,
};
use crate::tool_paths::{absolutize_ast, Columns};
use crate::{HierarchyOptions, LintFailure};

/// Direction of one module port, as the C ABI spells it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PortDirection {
    /// `input`
    In,
    /// `output`
    Out,
    /// `inout`
    InOut,
    /// `ref`
    Ref,
}

impl PortDirection {
    /// The ABI spelling (`In`, `Out`, `InOut`, `Ref`).
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::In => "In",
            Self::Out => "Out",
            Self::InOut => "InOut",
            Self::Ref => "Ref",
        }
    }

    /// Map the AST spelling.
    ///
    /// `slang` serialises `PortDirection` as `In`, `Out`, `InOut` or `Ref`;
    /// anything else is unknown, and an honest `InOut` is the only safe
    /// fallback for a four-value ABI (the alternative would be to drop a port
    /// the design really has). Interface ports are reported as `InOut` for the
    /// same reason: a bus is bidirectional.
    fn from_ast(value: &str) -> Self {
        match value {
            "In" => Self::In,
            "Out" => Self::Out,
            "InOut" => Self::InOut,
            "Ref" => Self::Ref,
            _ => Self::InOut,
        }
    }
}

/// One port of one module definition.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModulePort {
    /// Port name.
    pub name: String,
    /// Declared direction.
    pub direction: PortDirection,
    /// The declared type, verbatim (`logic`, `logic[3:0]`, `bus_if`, …).
    pub type_name: String,
    /// File of the port declaration.
    pub file: PathBuf,
    /// 0-based line of the port declaration.
    pub line: u32,
    /// 0-based column of the port declaration.
    pub column: u32,
}

/// One child instantiation inside a module definition.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModuleInstance {
    /// Instantiated module name.
    pub module: String,
    /// Hierarchical path of the instance in the elaborated design (`top1.u_mid`).
    pub path: String,
    /// File of the instantiation site.
    pub file: PathBuf,
    /// 0-based line of the instantiation site.
    pub line: u32,
    /// 0-based column of the instantiation site.
    pub column: u32,
}

/// One module definition with its ports and child instantiations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CatalogModule {
    /// Module name (the deduplication key).
    pub name: String,
    /// File of the module definition (`InstanceBody.source_file`).
    pub file: PathBuf,
    /// 0-based line of the module definition.
    pub line: u32,
    /// 0-based column of the module definition.
    pub column: u32,
    /// `true` when the module is never instantiated anywhere.
    pub is_top: bool,
    /// Ports in declaration order.
    pub ports: Vec<ModulePort>,
    /// Child instantiations, in elaboration order, one entry per site
    /// (generate-loop iterations collapse onto the same source position).
    pub children: Vec<ModuleInstance>,
}

/// One catalog run's observable result.
#[derive(Debug, Clone)]
pub struct ModuleCatalog {
    /// The tool that ran (`slang`).
    pub tool: String,
    /// Modules sorted by name (deterministic).
    pub modules: Vec<CatalogModule>,
    /// Wall-clock time of the elaboration run.
    pub elapsed: Duration,
    /// Peak RSS of the child in KiB.
    pub peak_rss_kb: u64,
    /// Oversize warning for the UI (same rule as the hierarchy report).
    pub warning: Option<String>,
    /// The effective top the elaboration used (`None` = Auto/all roots).
    pub top: Option<String>,
    /// Every source file of an ELABORATED definition (module or interface),
    /// absolute, sorted. With a top set this is exactly "the files reachable
    /// from that top"; without one it is the whole project. RM-17: this is the
    /// ONE derivation the lint scope and the status dots consume.
    pub reachable_files: Vec<PathBuf>,
    /// Module definitions the document declares but the elaboration did not
    /// reach (only non-empty when a top is set). Sorted; the tree reports the
    /// count so modules never silently vanish.
    pub outside_modules: Vec<String>,
    /// `None` on success; a typed failure otherwise.
    pub failure: Option<LintFailure>,
}

impl ModuleCatalog {
    /// The module with `name`, if the definition was catalogued.
    #[must_use]
    pub fn module(&self, name: &str) -> Option<&CatalogModule> {
        self.modules.iter().find(|module| module.name == name)
    }

    /// One-line summary for the status bar.
    #[must_use]
    pub fn summary(&self) -> String {
        match self.failure_message() {
            Some(message) => message,
            None => format!(
                "{} module(s), {} ms",
                self.modules.len(),
                self.elapsed.as_millis()
            ),
        }
    }

    /// The truthful failure sentence for the UI, in the CATALOG's vocabulary.
    ///
    /// [`LintFailure::user_message`] speaks about lint diagnostics; sharing it
    /// verbatim made an AST-JSON overflow read `slang failed: lint diagnostics
    /// exceeded the 268435456 byte limit`, which sent users and maintainers
    /// after a diagnostic flood the catalog never produces. Every failure the
    /// catalog can report is re-worded here around what actually happened to
    /// the elaborated AST JSON.
    #[must_use]
    pub fn failure_message(&self) -> Option<String> {
        let failure = self.failure.as_ref()?;
        Some(match failure {
            LintFailure::MissingOutput { exit_code, stderr } => {
                let base = match exit_code {
                    Some(code) => {
                        format!("slang exited with code {code} and produced no AST JSON")
                    }
                    None => "slang terminated without producing an AST JSON".to_string(),
                };
                match stderr.lines().map(str::trim).find(|line| !line.is_empty()) {
                    Some(line) => format!("{base}: {line}"),
                    None => base,
                }
            }
            LintFailure::InvalidJson { message, .. } => {
                format!("the elaborated AST JSON could not be read: {message}")
            }
            LintFailure::OutputTooLarge { limit } | LintFailure::AstJsonTooLarge { limit } => {
                format!("the elaborated AST JSON exceeded the {limit} byte limit")
            }
            other => other.user_message(),
        })
    }
}

/// Options for one catalog run.
#[derive(Debug, Clone)]
pub struct CatalogOptions {
    /// Explicit slang binary (tests, packaging).
    pub slang_binary: Option<PathBuf>,
    /// Project include dirs (`-I`).
    pub include_dirs: Vec<PathBuf>,
    /// Project defines (`-D`).
    pub defines: Vec<String>,
    /// Wall-clock budget for the `slang` child (the streamed parse runs after
    /// it exits and is not covered). A whole-project elaboration is heavier
    /// than a single-file lint, so the default is 120 s.
    pub timeout: Duration,
    /// Document-size threshold for the catalog's warning, in bytes. A larger
    /// `--ast-json` document is still catalogued in full — it is streamed, so
    /// the size can neither fail nor exhaust the catalog — and the size is
    /// named in [`ModuleCatalog::warning`] so the UI can explain a long run.
    /// The default keeps the hierarchy's 256 MiB threshold; `0` disables the
    /// note (it never gates the catalog).
    pub max_json_bytes: usize,
    /// Maximum structural nodes the streaming walk keeps (`0` selects
    /// [`crate::ast_prune::DEFAULT_MAX_RETAINED_NODES`]).
    ///
    /// This is the catalog's real memory bound: it counts module bodies,
    /// instantiation sites and generate blocks, NOT the expressions and
    /// statements that make a dump large. Past it the catalog degrades to a
    /// warned PARTIAL tree instead of failing.
    pub max_retained_nodes: usize,
    /// Oversize threshold for the file set (same warning as the hierarchy).
    pub max_files: usize,
    /// RM-17: top-level module NAME to elaborate from. `None` (the default) is
    /// Auto: slang picks every uninstantiated module as a root. A name slang
    /// cannot elaborate yields [`LintFailure::UnknownTop`] with THAT name.
    pub top: Option<String>,
}

/// Default wall-clock budget for the catalog's `slang` child.
///
/// A whole-project elaboration of a thousand files is not a 10 s lint; the
/// hierarchy's 30 s default was tuned for one design root. The parse itself is
/// not covered (it only starts once the child has exited).
pub const DEFAULT_CATALOG_TIMEOUT: Duration = Duration::from_secs(120);

impl Default for CatalogOptions {
    fn default() -> Self {
        Self {
            slang_binary: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: DEFAULT_CATALOG_TIMEOUT,
            max_json_bytes: crate::hierarchy::DEFAULT_MAX_JSON_BYTES,
            max_retained_nodes: 0,
            max_files: crate::hierarchy::DEFAULT_MAX_FILES,
            top: None,
        }
    }
}

/// Elaborate `files` and build the module catalog.
///
/// A project-wide elaboration (`--top` is never passed): every uninstantiated
/// module becomes a design root, which is what makes `is_top` and
/// "defined but never instantiated" observable.
///
/// The document is STREAMED ([`crate::ast_prune`]): the raw JSON is never held
/// in memory, so a dump far above the hierarchy's 256 MiB AST cap is a normal
/// project, not a failure. The walk keeps only module bodies, ports and
/// instantiation sites; a design whose KEPT node count exceeds
/// [`CatalogOptions::max_retained_nodes`] degrades to a warned partial catalog
/// instead of failing or growing without bound.
#[must_use]
pub fn build_catalog(files: &[PathBuf], options: &CatalogOptions) -> ModuleCatalog {
    let run_options = HierarchyOptions {
        slang_binary: options.slang_binary.clone(),
        top: options.top.clone().filter(|top| !top.trim().is_empty()),
        include_dirs: options.include_dirs.clone(),
        defines: options.defines.clone(),
        timeout: options.timeout,
        // The streaming sink owns the size policy; the hierarchy's whole-text
        // cap must not fire before the sink can read the file.
        max_json_bytes: usize::MAX,
        max_files: options.max_files,
        ..HierarchyOptions::default()
    };
    let node_budget = if options.max_retained_nodes == 0 {
        crate::ast_prune::DEFAULT_MAX_RETAINED_NODES
    } else {
        u64::try_from(options.max_retained_nodes).unwrap_or(u64::MAX)
    };
    /// What the big-stack walk thread hands back (never the deep document
    /// itself: the parsed `Value` is dropped inside that thread).
    struct CatalogScan {
        modules: Vec<CatalogModule>,
        retained_fully: bool,
        bytes_read: u64,
        reachable_files: Vec<PathBuf>,
        outside_modules: Vec<String>,
    }

    let (report, scanned) = run_ast_json(files, &run_options, |path, exit_code, stderr| {
        // RM-17: slang rejects a name it cannot elaborate with a named error
        // ("'<name>' is not a valid top-level module") and still writes a tiny
        // AST document. That is NOT a design: report the NAME.
        if let Some(top) = options
            .top
            .as_deref()
            .map(str::trim)
            .filter(|top| !top.is_empty())
        {
            if exit_code != Some(0) && stderr.contains("is not a valid top-level module") {
                return Err(LintFailure::UnknownTop {
                    name: top.to_string(),
                });
            }
        }
        let handle = std::fs::File::open(path).map_err(|error| LintFailure::Io {
            message: format!("opening {} failed: {error}", path.display()),
        })?;
        // U9d: measure the document's own nesting FIRST, with an iterative
        // streaming scan (O(1) memory, early exit past the cap), and refuse a
        // document the walk cannot read safely. Only a document within the cap
        // is parsed with serde_json's 128-value guard disabled — the recursion
        // depth is then proven bounded by the measured number, never by the
        // document alone.
        let measured = crate::ast_prune::measure_nesting(
            std::io::BufReader::new(&handle),
            crate::AST_JSON_MAX_NESTING,
        )
        .map_err(|error| LintFailure::Io {
            message: format!("reading {} failed: {error}", path.display()),
        })?;
        if measured.is_none() {
            return Err(LintFailure::AstJsonNestedTooDeep {
                measured: Some(crate::AST_JSON_MAX_NESTING + 1),
            });
        }
        drop(handle);
        // EVERYTHING that walks the parsed `Value` runs on the dedicated
        // big-stack thread, not just the parse: the RM-4 path rewrite, the
        // catalog walk (`collect_bodies` / `collect_instance_paths`) and the
        // deep Value's own recursive `Drop` all recurse once per JSON level, so
        // a document just below the cap would overflow a normal 8 MiB thread
        // after a successful parse. The thread returns only shallow results.
        let owned_path = path.to_path_buf();
        let root = project_root(files);
        let run_top = options.top.clone().filter(|top| !top.trim().is_empty());
        let walk = std::thread::Builder::new()
            .name("vd-catalog-walk".to_string())
            .stack_size(crate::ast_prune::WALK_STACK_BYTES)
            .spawn(move || -> Result<CatalogScan, LintFailure> {
                let reader = std::fs::File::open(&owned_path).map_err(|error| LintFailure::Io {
                    message: format!("opening {} failed: {error}", owned_path.display()),
                })?;
                let mut pruned =
                    crate::ast_prune::prune_json(std::io::BufReader::new(reader), node_budget)
                        .map_err(|message| LintFailure::InvalidAstJson { message })?;
                absolutize_ast(&mut pruned.document, &root);
                let reachable_files = reachable_source_files(&pruned.document);
                let modules = match parse_catalog_value(&pruned.document) {
                    Ok(modules) => modules,
                    Err(message) => {
                        // With an explicit top, an empty elaboration means the
                        // NAME was not elaboratable in this design.
                        if let Some(top) = run_top.as_deref() {
                            return Err(LintFailure::UnknownTop {
                                name: top.to_string(),
                            });
                        }
                        if pruned.retained_fully {
                            return Err(LintFailure::InvalidAstJson { message });
                        }
                        // A partial walk can legitimately miss the design root:
                        // keep the (possibly empty) catalog and let the warning
                        // explain the result instead of a hard failure.
                        Vec::new()
                    }
                };
                // RM-17: module definitions the elaboration did not reach.
                let outside_modules = if run_top.is_some() {
                    let declared = declared_module_names(&pruned.document);
                    let catalogued: std::collections::HashSet<&str> =
                        modules.iter().map(|module| module.name.as_str()).collect();
                    declared
                        .into_iter()
                        .filter(|name| !catalogued.contains(name.as_str()))
                        .collect()
                } else {
                    Vec::new()
                };
                let scan = CatalogScan {
                    modules,
                    retained_fully: pruned.retained_fully,
                    bytes_read: pruned.bytes_read,
                    reachable_files,
                    outside_modules,
                };
                drop(pruned);
                Ok(scan)
            })
            .map_err(|error| LintFailure::Io {
                message: format!("starting the catalog walk thread failed: {error}"),
            })?;
        walk.join().map_err(|_| LintFailure::Io {
            message: "the catalog walk thread panicked while reading the AST JSON".to_string(),
        })?
    });
    let mut catalog = ModuleCatalog {
        tool: report.tool,
        modules: Vec::new(),
        elapsed: report.elapsed,
        peak_rss_kb: report.peak_rss_kb,
        warning: None,
        top: options.top.clone().filter(|top| !top.trim().is_empty()),
        reachable_files: Vec::new(),
        outside_modules: Vec::new(),
        failure: report.failure,
    };
    if catalog.failure.is_some() {
        return catalog;
    }
    let Some(scan) = scanned else {
        catalog.failure = Some(LintFailure::Io {
            message: "the elaboration produced no AST JSON".to_string(),
        });
        return catalog;
    };
    let CatalogScan {
        modules,
        retained_fully,
        bytes_read,
        reachable_files,
        outside_modules,
    } = scan;
    catalog.modules = modules;
    catalog.reachable_files = reachable_files;
    catalog.outside_modules = outside_modules;
    let mut warnings: Vec<String> = Vec::new();
    if !retained_fully {
        warnings.push(format!(
            "the elaborated design exceeded the catalog's {node_budget} node retention budget: \
             the module tree is partial ({} module(s) kept)",
            catalog.modules.len()
        ));
    }
    if options.max_json_bytes > 0 && bytes_read > options.max_json_bytes as u64 {
        warnings.push(format!(
            "the elaborated AST JSON is {} MiB (over the {} MiB size threshold)",
            bytes_read / (1024 * 1024),
            options.max_json_bytes / (1024 * 1024)
        ));
    }
    if let Some(top) = catalog.top.as_deref() {
        if !catalog.outside_modules.is_empty() {
            warnings.push(format!(
                "{} module(s) outside the top {top} are not in this tree",
                catalog.outside_modules.len()
            ));
        }
    }
    if files.len() > options.max_files {
        warnings.push(format!(
            "the project has {} files (limit {}): the module tree may be slow",
            files.len(),
            options.max_files
        ));
    }
    catalog.warning = if warnings.is_empty() {
        None
    } else {
        Some(warnings.join("; "))
    };
    catalog
}

/// Every source file of an ELABORATED definition (module or interface), sorted
/// and deduplicated. RM-17: with an explicit top this is the reachable set.
fn reachable_source_files(document: &Value) -> Vec<PathBuf> {
    fn walk(value: &Value, out: &mut std::collections::BTreeSet<PathBuf>) {
        match value {
            Value::Object(map) => {
                if map.get("kind").and_then(Value::as_str) == Some("InstanceBody") {
                    if let Some(file) = map.get("source_file").and_then(Value::as_str) {
                        if !file.is_empty() {
                            out.insert(PathBuf::from(file));
                        }
                    }
                }
                for child in map.values() {
                    walk(child, out);
                }
            }
            Value::Array(items) => {
                for item in items {
                    walk(item, out);
                }
            }
            _ => {}
        }
    }
    let mut files = std::collections::BTreeSet::new();
    walk(document, &mut files);
    files.into_iter().collect()
}

/// Names of the MODULE definitions the document declares, from the top-level
/// `definitions` array (interfaces, packages and programs are excluded by their
/// `definitionKind`). RM-17: compared with the catalogued modules to report
/// what an explicit top leaves outside.
fn declared_module_names(document: &Value) -> Vec<String> {
    let mut names = std::collections::BTreeSet::new();
    if let Some(definitions) = document.get("definitions").and_then(Value::as_array) {
        for entry in definitions {
            if entry.get("kind").and_then(Value::as_str) != Some("Definition") {
                continue;
            }
            if entry.get("definitionKind").and_then(Value::as_str) != Some("Module") {
                continue;
            }
            if let Some(name) = entry.get("name").and_then(Value::as_str) {
                if !name.is_empty() {
                    names.insert(name.to_string());
                }
            }
        }
    }
    names.into_iter().collect()
}

/// Parse one `--ast-json` document into the catalog's module list.
///
/// # Errors
///
/// Returns the reason when the document is malformed or carries no elaborated
/// root instance (the two cases the caller must report, not paper over).
pub fn parse_catalog_json(text: &str) -> Result<Vec<CatalogModule>, String> {
    parse_catalog_json_in(text, None)
}

/// [`parse_catalog_json`] for a document whose paths are still relative to the
/// directory the tool ran in (the streaming [`build_catalog`] path applies the
/// same normalisation). `None` keeps the document's spelling.
///
/// # Errors
///
/// Same as [`parse_catalog_json`].
pub fn parse_catalog_json_in(
    text: &str,
    tool_dir: Option<&std::path::Path>,
) -> Result<Vec<CatalogModule>, String> {
    let mut document: Value = serde_json::from_str(text)
        .map_err(|error| format!("the AST JSON is malformed: {error}"))?;
    if let Some(tool_dir) = tool_dir {
        absolutize_ast(&mut document, tool_dir);
    }
    parse_catalog_value(&document)
}

/// The catalog walk over an already-parsed document.
///
/// [`parse_catalog_json`] feeds it a whole-string parse; `build_catalog` feeds
/// it the streaming prune of the same shape, so both paths share one walk.
pub(crate) fn parse_catalog_value(document: &Value) -> Result<Vec<CatalogModule>, String> {
    let design = document
        .get("design")
        .ok_or_else(|| "the AST JSON has no `design` object".to_string())?;
    let roots: Vec<&Value> = design
        .get("members")
        .and_then(Value::as_array)
        .map(|members| {
            members
                .iter()
                .filter(|member| node_kind(member) == "Instance")
                .collect()
        })
        .unwrap_or_default();
    if roots.is_empty() {
        return Err("the AST JSON has no elaborated top instance".to_string());
    }

    let bodies = Bodies::index(document);
    let definition_kinds = definition_kinds(document);

    // Elaborated path of every instance, keyed by AST address: the catalog is
    // definition-keyed, but a child edge still points at the place the user
    // would jump to, so `path` is the real hierarchical path.
    let mut paths: HashMap<u64, String> = HashMap::new();
    for root in &roots {
        let name = root.get("name").and_then(Value::as_str).unwrap_or("");
        collect_instance_paths(root, name, &bodies, &mut paths);
    }

    // Module bodies: one per definition, sorted by name for determinism.
    let mut all_bodies: Vec<&Value> = bodies.by_address.values().copied().collect();
    all_bodies.sort_by_key(|body| {
        body.get("name")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string()
    });
    let mut module_bodies: BTreeMap<String, &Value> = BTreeMap::new();
    for body in all_bodies {
        let Some(name) = body.get("name").and_then(Value::as_str) else {
            continue;
        };
        if name.is_empty() || !is_module_definition(body, &definition_kinds) {
            continue;
        }
        module_bodies.entry(name.to_string()).or_insert(body);
    }

    let mut modules: Vec<CatalogModule> = Vec::with_capacity(module_bodies.len());
    // ONE byte->character conversion for the whole catalog (see `Columns`): the
    // tool reports UTF-8 byte columns, the editor counts UTF-16 units.
    let mut columns = Columns::new();
    for (name, body) in &module_bodies {
        let (file, line, column) = catalog_position(body, &mut columns);
        let members: Vec<&Value> = body
            .get("members")
            .and_then(Value::as_array)
            .map(|members| members.iter().collect())
            .unwrap_or_default();
        let ports = ports_of(&members, &mut columns);
        let children = children_of(
            name,
            &members,
            &bodies,
            &definition_kinds,
            &paths,
            &mut columns,
        );
        modules.push(CatalogModule {
            name: name.clone(),
            file,
            line,
            column,
            is_top: false,
            ports,
            children,
        });
    }

    // `is_top` is a property of the whole design, not of one module: a module is
    // a top exactly when no other module instantiates it.
    let instantiated: HashSet<String> = modules
        .iter()
        .flat_map(|module| module.children.iter())
        .map(|child| child.module.clone())
        .collect();
    for module in &mut modules {
        module.is_top = !instantiated.contains(&module.name);
    }
    Ok(modules)
}

/// The declaration kind of every `Definition` node, keyed by AST address.
///
/// `slang` writes them in the document's top-level `definitions` array; walking
/// the whole document also covers a layout that inlines the definitions, and an
/// address with no entry is treated as a module by the caller.
fn definition_kinds(document: &Value) -> HashMap<u64, String> {
    let mut kinds = HashMap::new();
    collect_definition_kinds(document, &mut kinds);
    kinds
}

fn collect_definition_kinds(value: &Value, out: &mut HashMap<u64, String>) {
    match value {
        Value::Object(map) => {
            if map.get("kind").and_then(Value::as_str) == Some("Definition") {
                if let Some(address) = map.get("addr").and_then(Value::as_u64) {
                    let kind = map
                        .get("definitionKind")
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string();
                    out.insert(address, kind);
                }
            }
            for child in map.values() {
                collect_definition_kinds(child, out);
            }
        }
        Value::Array(items) => {
            for item in items {
                collect_definition_kinds(item, out);
            }
        }
        _ => {}
    }
}

/// Whether one `InstanceBody` declares a module (as opposed to an interface,
/// package or program).
fn is_module_definition(body: &Value, kinds: &HashMap<u64, String>) -> bool {
    let Some(reference) = body.get("definition").and_then(Value::as_str) else {
        return true;
    };
    let Some(address) = reference
        .split_whitespace()
        .next()
        .and_then(|token| token.parse::<u64>().ok())
    else {
        return true;
    };
    match kinds.get(&address) {
        Some(kind) => kind == "Module",
        None => true,
    }
}

/// Elaborated path of every instance in the design, keyed by AST address.
///
/// The first occurrence wins: a definition-keyed child edge is one entry per
/// source position, and the first elaboration is the canonical path.
fn collect_instance_paths(
    instance: &Value,
    path: &str,
    bodies: &Bodies<'_>,
    out: &mut HashMap<u64, String>,
) {
    if let Some(address) = instance.get("addr").and_then(Value::as_u64) {
        out.entry(address).or_insert_with(|| path.to_string());
    }
    let Some(body) = bodies.of(instance) else {
        return;
    };
    let members: Vec<&Value> = body
        .get("members")
        .and_then(Value::as_array)
        .map(|members| members.iter().collect())
        .unwrap_or_default();
    let mut scoped: Vec<(&Value, String)> = Vec::new();
    scope_instances(&members, path, &mut scoped);
    for (child, child_path) in scoped {
        collect_instance_paths(child, &child_path, bodies, out);
    }
}

/// Ports of one module definition, in declaration order.
///
/// Only `Port` and `InterfacePort` are ports: `slang` also lists a `Net` /
/// `Variable` with the same name next to each port, and a `ModportPort` lives
/// inside an interface's modport, not in the module's port list.
/// One catalog position: the tool's 0-based line and UTF-8 BYTE column, with the
/// column converted to the editor's 0-based UTF-16 unit (see
/// [`crate::tool_paths::Columns`]). Empty when the node carries no position.
fn catalog_position(value: &Value, columns: &mut Columns) -> (PathBuf, u32, u32) {
    let (file, line, column) = symbol_position(value).unwrap_or_default();
    let column = columns.char_column(&file, line, column);
    (file, line, column)
}

fn ports_of(members: &[&Value], columns: &mut Columns) -> Vec<ModulePort> {
    let mut ports = Vec::new();
    for member in members {
        match node_kind(member) {
            "Port" => {
                let Some(name) = member.get("name").and_then(Value::as_str) else {
                    continue;
                };
                let direction = member
                    .get("direction")
                    .and_then(Value::as_str)
                    .unwrap_or("");
                let (file, line, column) = catalog_position(member, columns);
                ports.push(ModulePort {
                    name: name.to_string(),
                    direction: PortDirection::from_ast(direction),
                    type_name: member
                        .get("type")
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string(),
                    file,
                    line,
                    column,
                });
            }
            "InterfacePort" => {
                let Some(name) = member.get("name").and_then(Value::as_str) else {
                    continue;
                };
                let (file, line, column) = catalog_position(member, columns);
                ports.push(ModulePort {
                    name: name.to_string(),
                    direction: PortDirection::InOut,
                    type_name: member
                        .get("interfaceDef")
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string(),
                    file,
                    line,
                    column,
                });
            }
            _ => {}
        }
    }
    ports
}

/// Instantiations inside one module definition, one per source position.
///
/// A generate-loop iteration produces one `Instance` per iteration at the same
/// source position; those collapse onto one edge (the parent definition is the
/// same edge) and keep the first elaborated path.
fn children_of(
    module: &str,
    members: &[&Value],
    bodies: &Bodies<'_>,
    kinds: &HashMap<u64, String>,
    paths: &HashMap<u64, String>,
    columns: &mut Columns,
) -> Vec<ModuleInstance> {
    let mut scoped: Vec<(&Value, String)> = Vec::new();
    scope_instances(members, "", &mut scoped);
    let mut children = Vec::new();
    let mut seen: HashSet<(String, String, u32, u32)> = HashSet::new();
    for (instance, relative) in scoped {
        let Some(body) = bodies.of(instance) else {
            continue;
        };
        if !is_module_definition(body, kinds) {
            continue;
        }
        let Some(child_module) = body.get("name").and_then(Value::as_str) else {
            continue;
        };
        let (file, line, column) = catalog_position(instance, columns);
        let key = (
            child_module.to_string(),
            file.to_string_lossy().into_owned(),
            line,
            column,
        );
        if !seen.insert(key) {
            continue;
        }
        let path = instance
            .get("addr")
            .and_then(Value::as_u64)
            .and_then(|address| paths.get(&address).cloned())
            .unwrap_or_else(|| join_path(module, &relative));
        children.push(ModuleInstance {
            module: child_module.to_string(),
            path,
            file,
            line,
            column,
        });
    }
    children
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direction_spelling_round_trips() {
        assert_eq!(PortDirection::from_ast("In"), PortDirection::In);
        assert_eq!(PortDirection::from_ast("Out"), PortDirection::Out);
        assert_eq!(PortDirection::from_ast("InOut"), PortDirection::InOut);
        assert_eq!(PortDirection::from_ast("Ref"), PortDirection::Ref);
        assert_eq!(PortDirection::In.as_str(), "In");
        assert_eq!(PortDirection::Ref.as_str(), "Ref");
    }

    #[test]
    fn an_empty_document_is_reported_not_silently_empty() {
        let missing_design = parse_catalog_json("{}");
        assert!(missing_design.is_err());
        let malformed = parse_catalog_json("not json");
        assert!(malformed.is_err());
    }

    #[test]
    fn the_streaming_prune_matches_the_whole_document_parse() {
        // The two document paths must agree node for node: `build_catalog` uses
        // the streaming prune, while the captured-document test and any caller
        // of `parse_catalog_json` use a whole-string parse.
        let text = include_str!("../tests/fixtures/catalog/ast.json");
        let whole = parse_catalog_json(text)
            .unwrap_or_else(|error| panic!("whole-document parse: {error}"));
        let pruned = crate::ast_prune::prune_json(text.as_bytes(), u64::MAX)
            .unwrap_or_else(|error| panic!("streaming prune: {error}"));
        assert!(pruned.retained_fully, "the fixture fits any budget");
        let streamed = parse_catalog_value(&pruned.document)
            .unwrap_or_else(|error| panic!("streamed parse: {error}"));
        assert_eq!(whole, streamed);
    }
}
