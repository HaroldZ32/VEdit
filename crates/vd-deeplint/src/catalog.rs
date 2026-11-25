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
//! # Duplicate module definitions (batch-20261118 B)
//!
//! Two files may declare the same module name. `slang` reports
//! `duplicate definition of '<name>'`, **exits non-zero** and still writes the
//! whole AST JSON — but that document carries BOTH `definitions` entries while
//! the elaborated design tree (and therefore the only `InstanceBody`) belongs to
//! the declaration it processed last. The catalog must neither inherit that
//! accident nor pick one silently:
//!
//! * the adopted declaration is the FIRST one in the caller's file-set order
//!   ([`parse_catalog_json_with_files`]; `build_catalog` passes the project file
//!   set in `.f` order), then the lowest `(source_line, source_column)` inside
//!   one file, then the file's spelling. Nothing about the choice depends on a
//!   `HashMap`'s iteration order,
//! * every collision produces exactly one warning naming the files involved and
//!   the adopted declaration (the sentence the module tree shows in its summary
//!   line). A document that carries fewer than two `definitions` entries but
//!   several bodies of one name (an older tool, a hand-written document) is
//!   reported too: that choice is never silent (review F1),
//! * the preference key ends with the body's OWN AST address, so two bodies that
//!   carry neither a `definition` reference nor a source position still have a
//!   total order and the result never depends on `Bodies::by_address`'s
//!   `HashMap` iteration order (review F1),
//! * when the document carries the winning declaration but NOT its body, the
//!   module is reported at the declaration's position with no ports and no
//!   children — never the losing body's data under the winner's name. A SECOND
//!   warning says that the port data is unavailable, so an empty port list is
//!   distinguishable from "this module has no ports" (review F4),
//! * a `duplicate definition` diagnostic `slang` printed is never lost to its
//!   non-zero exit: a name the document resolved is explained by that warning,
//!   and any other is quoted verbatim in the catalog warning (B-5).
//!
//! # The losing file is published, not left to the tool (G1-c)
//!
//! Warning about the pick is not enough: the compiler still SEES both files, so
//! `slang` applies its own duplicate rule and its answer (which body to
//! elaborate) can disagree with the catalog's. The catalog therefore publishes
//! [`ModuleCatalog::shadowed_files`]: the files whose EVERY module declaration
//! lost the duplicate pick. A file that still declares a winning or unique
//! module — or an interface/package/program — is deliberately NOT published, and
//! the duplicate warning is all it gets: excluding such a file would drop the
//! definitions it alone provides. Together with
//! [`ModuleCatalog::shadowed_warning`] (the sentence saying the compiler will no
//! longer see those files) this keeps ONE duplicate-definition authority: the
//! catalog's, not `slang`'s.
//!
//! # Definitions outside the file set (G1-b)
//!
//! `slang` parses `` `include ``d files as part of the compilation unit, so the
//! AST document carries `Definition` nodes whose `source_file` is NOT one of the
//! files the caller asked for. Such a declaration must never become a module's
//! definition location (the module tree would jump into an include file): every
//! [`DefinitionInfo`] records whether its file is in the caller's file set, and
//! only in-set declarations are adopted. An `InstanceBody` from an outside file
//! is filtered by the same rule, so port/child data never leaks in from an
//! include either.
//!
//! A module whose EVERY definition lives outside the set is excluded from
//! `modules` and summarised in ONE warning (`N module(s) are defined only in
//! files outside the file list …`), at most three examples named. When a name
//! has both in-set and outside declarations, the in-set one is adopted and the
//! outside ones join the ordinary duplicate warning. A caller with NO file set
//! (`parse_catalog_json`) cannot decide, so the old collect-everything
//! behaviour is kept there. A caller that DOES supply a set is taken at its
//! word: when the set shares no path with the document at all, every module is
//! outside it and the result is an empty catalog plus that same summary warning.
//! The boundary is never inferred from the document's content — a `.f` that
//! lists only a compile-unit wrapper whose modules all come from `` `include ``d
//! files is a real project shape, not a reason to collect everything again.
//!
//! # Positions
//!
//! 0-based lines and columns, like the hierarchy report and `VdDiagnostic`.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use serde_json::Value;

use crate::hierarchy::{
    join_path, node_kind, project_root, run_ast_json_cancellable, scope_instances, symbol_position,
    Bodies,
};
use crate::tool_paths::{absolutize_ast, lexical, Columns};
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
    /// Source file to compile when the declaration was reached through an include.
    pub compilation_unit: Option<PathBuf>,
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
    /// The warning line for the UI: the oversize/partial notes (same rule as
    /// the hierarchy report) plus one sentence per duplicate-module collision
    /// and, when the adopted declaration has no elaborated body, a second
    /// sentence saying its port data is unavailable (batch-20261118 B, review
    /// F4). `None` when the run has nothing to explain.
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
    /// G1-c: the files the compiler input no longer needs — every module
    /// declaration of the file lost the duplicate pick to a definition in
    /// another file, so the file contributes nothing the catalog does not
    /// already answer from the winner. Sorted, in the document's spelling
    /// (absolute on the product path). A file that still declares a winning or
    /// unique module, or an interface/package/program, is deliberately NOT
    /// listed: dropping it would remove a definition from the design, and the
    /// duplicate warning already names the collision.
    ///
    /// This is the list a consumer may subtract from the file set it hands to a
    /// compiler, so `slang`'s own duplicate rule cannot pick a second winner
    /// (G1-c). Empty when nothing is shadowed.
    pub shadowed_files: Vec<PathBuf>,
    /// G1-c: the user-facing sentence naming [`Self::shadowed_files`] and saying
    /// what happens to them ("excluded from the compiler input because every
    /// module it defines lost the duplicate pick"), bounded to three files plus
    /// `+N more`. `None` when nothing is shadowed.
    ///
    /// It is a SEPARATE channel on purpose: the duplicate sentence of B-2 is
    /// frozen byte-for-byte (contract §5 B-2 and its tests assert it verbatim),
    /// so this one is published beside it and is meant to be shown right AFTER
    /// it, never merged into [`Self::warning`].
    pub shadowed_warning: Option<String>,
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
            LintFailure::MissingOutput {
                exit_code, stderr, ..
            } => {
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
    /// Cooperative cancellation: raising this flag from another thread kills the
    /// `slang` child (and stops the run before the streamed parse), so the
    /// result is a [`LintFailure::Cancelled`] catalog instead of a job that keeps
    /// burning CPU after its consumer is gone. `None` (the default) is the old
    /// run-to-completion behaviour.
    pub cancel: Option<Arc<AtomicBool>>,
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
            cancel: None,
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
    build_catalog_cancellable(files, options, options.cancel.as_deref())
}

/// [`build_catalog`] with a cancellation flag the caller can raise from another
/// thread (the UI's Cancel button and the job teardown path).
///
/// Raising the flag kills the `slang` child inside
/// [`crate::hierarchy::run_tool_once`] and the run reports
/// [`LintFailure::Cancelled`] — a real cancellation, not a release of the
/// caller's handle. The streamed parse that follows a successful child is not
/// coverable this way (it is bounded by the node budget instead); the flag is
/// checked before the walk starts so a cancel that lands during the child's
/// exit still stops the run.
#[must_use]
pub fn build_catalog_cancellable(
    files: &[PathBuf],
    options: &CatalogOptions,
    cancel: Option<&AtomicBool>,
) -> ModuleCatalog {
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
        /// D2: one warning per duplicate-module collision, module-name order,
        /// plus an "unavailable ports" sentence when the adopted declaration
        /// has no body (review F4). G1-b prepends the summary of the modules
        /// excluded because every definition of them lies outside the file set.
        duplicate_warnings: Vec<String>,
        /// The module names those warnings explain (B-5 coverage).
        duplicate_modules: Vec<String>,
        /// B-5: the `duplicate definition` diagnostics `slang` printed, as
        /// `(name, verbatim line)`. Kept even when the child exited non-zero.
        slang_duplicates: Vec<(String, String)>,
        /// G1-c: the files every module declaration of which lost the duplicate
        /// pick, plus the sentence naming them.
        shadowed_files: Vec<PathBuf>,
        shadowed_warning: Option<String>,
        retained_fully: bool,
        bytes_read: u64,
        reachable_files: Vec<PathBuf>,
        outside_modules: Vec<String>,
    }

    let (report, scanned) = run_ast_json_cancellable(
        files,
        &run_options,
        |path, exit_code, stderr, trace| {
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
            // A cancel that landed while the child was exiting stops the run here:
            // the parse of a whole-project document must not be spent on an answer
            // nobody is waiting for any more.
            if cancel.is_some_and(|flag| flag.load(Ordering::SeqCst)) {
                return Err(LintFailure::Cancelled);
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
            // D2: the file set ORDER is the duplicate-module tie-break, so the walk
            // owns it (a list of paths, cloned once per run).
            let include_units = compiler_include_units(trace, files);
            let mut ordered_files = files.to_vec();
            ordered_files.extend(include_units.keys().cloned());
            // B-5: a non-zero exit WITH the JSON is a normal result (see the module
            // docs); the diagnostics that came with it are read before the walk so
            // they cannot be lost when the walk returns.
            let slang_duplicates = duplicate_definition_lines(stderr);
            let walk = std::thread::Builder::new()
                .name("vd-catalog-walk".to_string())
                .stack_size(crate::ast_prune::WALK_STACK_BYTES)
                .spawn(move || -> Result<CatalogScan, LintFailure> {
                    let reader =
                        std::fs::File::open(&owned_path).map_err(|error| LintFailure::Io {
                            message: format!("opening {} failed: {error}", owned_path.display()),
                        })?;
                    let mut pruned =
                        crate::ast_prune::prune_json(std::io::BufReader::new(reader), node_budget)
                            .map_err(|message| LintFailure::InvalidAstJson { message })?;
                    absolutize_ast(&mut pruned.document, &root);
                    let reachable_files = reachable_source_files(&pruned.document);
                    let parsed =
                        match parse_catalog_value_in(&pruned.document, Some(&ordered_files)) {
                            Ok(parsed) => parsed,
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
                                ParsedCatalog {
                                    modules: Vec::new(),
                                    warnings: Vec::new(),
                                    duplicate_modules: Vec::new(),
                                    shadowed_files: Vec::new(),
                                    shadowed_warning: None,
                                }
                            }
                        };
                    let ParsedCatalog {
                        modules,
                        warnings: duplicate_warnings,
                        duplicate_modules,
                        shadowed_files,
                        shadowed_warning,
                    } = parsed;
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
                    let modules = modules
                        .into_iter()
                        .map(|mut module| {
                            module.compilation_unit =
                                include_units.get(&real_path(&module.file)).cloned();
                            module
                        })
                        .collect();
                    let scan = CatalogScan {
                        modules,
                        duplicate_warnings,
                        duplicate_modules,
                        slang_duplicates,
                        shadowed_files,
                        shadowed_warning,
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
        },
        cancel,
        true,
    );
    let mut catalog = ModuleCatalog {
        tool: report.tool,
        modules: Vec::new(),
        elapsed: report.elapsed,
        peak_rss_kb: report.peak_rss_kb,
        warning: None,
        top: options.top.clone().filter(|top| !top.trim().is_empty()),
        reachable_files: Vec::new(),
        outside_modules: Vec::new(),
        shadowed_files: Vec::new(),
        shadowed_warning: None,
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
        duplicate_warnings,
        duplicate_modules,
        slang_duplicates,
        shadowed_files,
        shadowed_warning,
        retained_fully,
        bytes_read,
        reachable_files,
        outside_modules,
    } = scan;
    catalog.modules = modules;
    catalog.reachable_files = reachable_files;
    catalog.outside_modules = outside_modules;
    catalog.shadowed_files = shadowed_files;
    catalog.shadowed_warning = shadowed_warning;
    // D2 first: a duplicate module explains a tree that looks WRONG, which is
    // more urgent than a long run.
    let mut warnings: Vec<String> = duplicate_warnings;
    // B-5: every `duplicate definition` diagnostic slang printed is either
    // explained above (the document carried both declarations) or quoted here.
    // A non-zero exit must never make the tool's own sentence disappear.
    for (name, line) in slang_duplicates {
        if !duplicate_modules.contains(&name) {
            warnings.push(format!("slang reported: {line}"));
        }
    }
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
fn compiler_include_units(trace: &str, files: &[PathBuf]) -> BTreeMap<PathBuf, PathBuf> {
    let ranked: HashMap<_, _> = files
        .iter()
        .enumerate()
        .rev()
        .map(|(rank, file)| (real_path(file), rank))
        .collect();
    let mut owners = BTreeMap::<PathBuf, PathBuf>::new();
    let mut unit = None;
    for line in trace.lines() {
        let Some((kind, path)) = line
            .strip_prefix("Parsing ")
            .and_then(|line| line.split_once(" file '"))
        else {
            continue;
        };
        let Some(path) = path.strip_suffix("'.") else {
            continue;
        };
        let path = real_path(Path::new(path));
        if kind != "include" {
            unit = ranked.get_key_value(&path).map(|(path, _)| path);
        } else if let Some(owner) = unit.filter(|_| !ranked.contains_key(&path)) {
            owners
                .entry(path)
                .and_modify(|existing| {
                    if ranked.get(owner) < ranked.get(existing) {
                        *existing = owner.clone();
                    }
                })
                .or_insert_with(|| owner.clone());
        }
    }
    owners
}

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
    parse_catalog_value_in(&document, None).map(|parsed| parsed.modules)
}

/// One catalog walk's result: the modules AND the warnings the walk itself
/// produced.
#[derive(Debug, Clone)]
pub struct ParsedCatalog {
    /// Modules sorted by name (deterministic).
    pub modules: Vec<CatalogModule>,
    /// One warning per cross-file duplicate-module collision, in module-name
    /// order, plus the "ports unavailable" note when the adopted declaration has
    /// no body (review F4). A collision confined to ONE file warns nothing
    /// (R1-a); `duplicate_modules` still records it. A caller that supplied a
    /// file set also gets one leading sentence when modules were excluded
    /// because every declaration of them lives outside that set (G1-b).
    pub warnings: Vec<String>,
    /// The module names the document itself showed as collisions, in the same
    /// order: a caller that also holds the tool's stderr uses this to tell "the
    /// document showed this collision" from "the tool reported one the document
    /// did not" (B-5). A collision intentionally left silent (R1-a) is recorded
    /// here too, so the tool's own duplicate line is not quoted for it.
    pub duplicate_modules: Vec<String>,
    /// G1-c: the files whose every module declaration lost the duplicate pick,
    /// sorted (see [`ModuleCatalog::shadowed_files`]).
    pub shadowed_files: Vec<PathBuf>,
    /// G1-c: the sentence naming [`Self::shadowed_files`]
    /// (see [`ModuleCatalog::shadowed_warning`]); `None` when nothing is
    /// shadowed.
    pub shadowed_warning: Option<String>,
}

/// [`parse_catalog_json`] with the ordered project file set.
///
/// The file set's order is the duplicate-module tie-break (batch-20261118 B):
/// when more than one file declares the same module name, the definition whose
/// file comes FIRST in `files` is adopted, and every collision is reported in
/// [`ParsedCatalog::warnings`] with the files involved and the adopted
/// declaration. The result never depends on a `HashMap`'s iteration order.
///
/// The file set is also the boundary G1-b needs: a declaration (or elaborated
/// body) whose `source_file` is outside `files` — an `` `include ``d file — is
/// never adopted, and a module defined ONLY there is excluded with one summary
/// warning. The set is the caller's own fact, so a non-empty set is authoritative
/// even when NO path of this document resolves into it: the catalog is then
/// empty (every module is outside the set) and the warning says so, instead of
/// silently collecting the whole document.
///
/// An empty `files` slice is legal (a caller with no project file set): the
/// adoption then orders by the document's own file spelling and position, which
/// is still deterministic for one document, and every definition is kept
/// because membership cannot be decided without a set.
///
/// # Errors
///
/// Same as [`parse_catalog_json`].
pub fn parse_catalog_json_with_files(
    text: &str,
    files: &[PathBuf],
) -> Result<ParsedCatalog, String> {
    let document: Value = serde_json::from_str(text)
        .map_err(|error| format!("the AST JSON is malformed: {error}"))?;
    parse_catalog_value_in(&document, (!files.is_empty()).then_some(files))
}

/// The catalog walk over an already-parsed document and the caller's file-set
/// boundary.
///
/// [`parse_catalog_json_with_files`] feeds it a whole-string parse;
/// `build_catalog` feeds it the streaming prune of the same shape, so both paths
/// share one walk — and one duplicate-module rule (batch-20261118 B).
///
/// `files` is the BOUNDARY as the caller declared it, which is why it is an
/// `Option` and not a slice: `None` means "this caller has no file set"
/// ([`parse_catalog_json`]), while `Some(&[])` means "the caller's set is empty"
/// and therefore contains nothing. `build_catalog` always passes `Some`, so the
/// product path can never regain a collect-everything fallback (G1-b review,
/// batch-20261119).
pub(crate) fn parse_catalog_value_in(
    document: &Value,
    files: Option<&[PathBuf]>,
) -> Result<ParsedCatalog, String> {
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
    let file_order = FileOrder::new(files);
    let definitions = definition_index(document, &file_order);
    // G1-b: the file set is the boundary the CALLER declared, never something
    // inferred from the document's content. A declared set is authoritative even
    // when no path of this document resolves into it: a `.f` may list only a
    // compile-unit wrapper while every module is defined in an `include`d file,
    // and that is the real "all definitions live outside the file list" case —
    // it excludes them (with the summary warning) instead of silently collecting
    // everything again. `parse_catalog_json` is the no-boundary channel: it
    // passes `None` (no set at all), and [`FileOrder::contains`] then answers
    // true for every file because membership cannot be decided without a set.
    let file_set_applies = files.is_some();
    let display_root = common_directory(files.unwrap_or(&[]));

    // Elaborated path of every instance, keyed by AST address: the catalog is
    // definition-keyed, but a child edge still points at the place the user
    // would jump to, so `path` is the real hierarchical path.
    let mut paths: HashMap<u64, String> = HashMap::new();
    for root in &roots {
        let name = root.get("name").and_then(Value::as_str).unwrap_or("");
        collect_instance_paths(root, name, &bodies, &mut paths);
    }

    // D2: every module DECLARATION, grouped by name. The document carries one
    // entry per declaration even when the elaboration produced a body for only
    // the last of them (slang 12.1.0 on a duplicate definition), so the
    // collision is visible here and nowhere else.
    let mut declarations: BTreeMap<String, Vec<DeclarationCandidate<'_>>> = BTreeMap::new();
    for info in definitions.values() {
        if info.kind != "Module" || info.name.is_empty() {
            continue;
        }
        declarations
            .entry(info.name.clone())
            .or_default()
            .push(DeclarationCandidate {
                info,
                order: preference_order(&file_order, &info.file, info.line, info.column, info.addr),
            });
    }
    for group in declarations.values_mut() {
        group.sort_by(|left, right| left.order.cmp(&right.order));
    }

    // Candidate bodies, grouped by module name: the BTreeMap is what keeps the
    // module list (and its warnings) in module-name order, as before.
    let mut body_groups: BTreeMap<String, Vec<BodyCandidate<'_>>> = BTreeMap::new();
    // G1-b: bodies the caller's file set does not contain (an `include`d file),
    // kept only so the exclusion summary can name an example file for a module
    // that has no declaration at all. They are never catalogued.
    let mut outside_body_files: BTreeMap<String, PathBuf> = BTreeMap::new();
    for body in bodies.by_address.values().copied() {
        if !is_module_definition(body, &definitions) {
            continue;
        }
        let Some(name) = body.get("name").and_then(Value::as_str) else {
            continue;
        };
        if name.is_empty() {
            continue;
        }
        let definition = body
            .get("definition")
            .and_then(Value::as_str)
            .and_then(definition_address);
        let (file, line, column) = symbol_position(body).unwrap_or_default();
        if file_set_applies && !file_order.contains(&file) {
            outside_body_files.entry(name.to_string()).or_insert(file);
            continue;
        }
        // `Bodies::by_address` only indexes a body that carries an `addr`, and
        // that address is the map key, so it is unique among the candidates.
        let body_addr = body.get("addr").and_then(Value::as_u64).unwrap_or(0);
        body_groups
            .entry(name.to_string())
            .or_default()
            .push(BodyCandidate {
                body,
                definition,
                order: (
                    preference_order(&file_order, &file, line, column, definition.unwrap_or(0)),
                    body_addr,
                ),
            });
    }
    for group in body_groups.values_mut() {
        group.sort_by(|left, right| left.order.cmp(&right.order));
    }

    // G1-b: every module the catalog can still describe — the in-set bodies,
    // plus a name declared INSIDE the set whose only body came from an outside
    // file (it is then reported at its declaration, with its port data
    // unavailable, never with the outside body's data).
    let mut names: BTreeSet<String> = body_groups.keys().cloned().collect();
    for (name, group) in &declarations {
        if outside_body_files.contains_key(name)
            && group.iter().any(|candidate| candidate.info.in_file_set)
        {
            names.insert(name.clone());
        }
    }

    let mut parsed = ParsedCatalog {
        modules: Vec::with_capacity(names.len()),
        warnings: Vec::new(),
        duplicate_modules: Vec::new(),
        shadowed_files: Vec::new(),
        shadowed_warning: None,
    };
    // ONE byte->character conversion for the whole catalog (see `Columns`): the
    // tool reports UTF-8 byte columns, the editor counts UTF-16 units.
    let mut columns = Columns::new();
    for name in &names {
        let declared = declarations.get(name).map_or(&[][..], Vec::as_slice);
        let candidates = body_groups.get(name).map_or(&[][..], Vec::as_slice);
        // G1-b: only a declaration INSIDE the caller's file set can be the
        // module's definition location. `declared` is already in preference
        // order, so the first in-set entry is the adopted one; an outside
        // declaration stays in `declared` (and therefore in the duplicate
        // warning) but is never adopted.
        let winner = declared.iter().find(|candidate| candidate.info.in_file_set);
        // The body of the ADOPTED declaration when the elaboration produced it;
        // otherwise the best-ranked body, whose data is then not used at all.
        let selected = winner
            .and_then(|winner| {
                candidates
                    .iter()
                    .find(|candidate| candidate.definition == Some(winner.info.addr))
            })
            .or_else(|| candidates.first());
        // A body with no `definition` reference cannot be tied to a declaration
        // (an older tool, a hand-written document): the document does not say
        // which declaration it is, so its own position/ports are used.
        let adopted_body = match (winner, selected) {
            (None, Some(_)) => true,
            (Some(winner), Some(selected)) => selected
                .definition
                .is_none_or(|address| address == winner.info.addr),
            // No body at all: the in-set declaration is the only source.
            (_, None) => false,
        };
        // Review F4: the winning declaration exists in the document but the
        // elaboration produced no body for it, or the only body came from a
        // file outside the set (G1-b). The empty port list then means the data
        // is UNAVAILABLE, not that the module has no ports.
        let bodiless_winner = match (winner, adopted_body) {
            (Some(winner), false) => Some(winner),
            _ => None,
        };
        let (file, line, column) = match (winner, selected) {
            (Some(_), Some(selected)) if adopted_body => {
                catalog_position(selected.body, &mut columns)
            }
            // The document carries the winning declaration but not its body: the
            // module is reported at the DECLARATION's position with no ports and
            // no children, never with the losing body's data under its name.
            (Some(winner), _) => {
                let info = winner.info;
                let column = columns.char_column(&info.file, info.line, info.column);
                (info.file.clone(), info.line, column)
            }
            (None, Some(selected)) => catalog_position(selected.body, &mut columns),
            (None, None) => continue,
        };
        let members: Vec<&Value> = match selected {
            Some(candidate) => body_members(candidate.body),
            None => Vec::new(),
        };
        let (ports, children) = if adopted_body {
            (
                ports_of(&members, &mut columns),
                children_of(name, &members, &bodies, &definitions, &paths, &mut columns),
            )
        } else {
            (Vec::new(), Vec::new())
        };
        if declared.len() > 1 && winner.is_some() {
            // R1-a: the name is still recorded as a collision the document
            // itself shows (§B-5 then keeps the tool's per-name diagnostic out
            // of the summary), but a same-file repeat produces no sentence.
            parsed.duplicate_modules.push(name.clone());
            if let Some(warning) = duplicate_module_warning(name, declared, display_root.as_deref())
            {
                parsed.warnings.push(warning);
            }
        } else if candidates.len() > 1 {
            // Review F1: the document may carry several bodies of one name with
            // fewer than two `definitions` entries (an older tool, a hand-written
            // document). The order is total now, but the choice must not be
            // silent either — it is the same B-2 sentence the declaration path
            // uses, built from the bodies' own positions.
            parsed.duplicate_modules.push(name.clone());
            if let Some(warning) = duplicate_body_warning(name, candidates, display_root.as_deref())
            {
                parsed.warnings.push(warning);
            }
        }
        if let Some(winner) = bodiless_winner {
            parsed
                .warnings
                .push(no_body_warning(name, winner.info, display_root.as_deref()));
        }
        parsed.modules.push(CatalogModule {
            name: name.clone(),
            file,
            compilation_unit: None,
            line,
            column,
            is_top: false,
            ports,
            children,
        });
    }

    // G1-b: a module with NO definition inside the caller's file set (the
    // `include` case) is not this project's module: it is excluded from
    // `modules` and reported in ONE bounded summary sentence. `outside_samples`
    // covers both the declarations and the bodies filtered above.
    if file_set_applies {
        let mut outside_samples: BTreeMap<String, PathBuf> = BTreeMap::new();
        for (name, group) in &declarations {
            if group.iter().all(|candidate| !candidate.info.in_file_set) {
                outside_samples
                    .entry(name.clone())
                    .or_insert_with(|| group[0].info.file.clone());
            }
        }
        for (name, file) in &outside_body_files {
            outside_samples
                .entry(name.clone())
                .or_insert_with(|| file.clone());
        }
        let kept: HashSet<&str> = parsed
            .modules
            .iter()
            .map(|module| module.name.as_str())
            .collect();
        let excluded: Vec<(String, PathBuf)> = outside_samples
            .into_iter()
            .filter(|(name, _file)| !kept.contains(name.as_str()))
            .collect();
        if !excluded.is_empty() {
            parsed.warnings.insert(
                0,
                outside_file_set_warning(&excluded, display_root.as_deref()),
            );
        }
    }

    // `is_top` is a property of the whole design, not of one module: a module is
    // a top exactly when no other module instantiates it.
    let instantiated: HashSet<String> = parsed
        .modules
        .iter()
        .flat_map(|module| module.children.iter())
        .map(|child| child.module.clone())
        .collect();
    for module in &mut parsed.modules {
        module.is_top = !instantiated.contains(&module.name);
    }
    // G1-c: computed from the same preference order the adoption used, so the
    // list can never disagree with the winner the catalog picked.
    let (shadowed_files, shadowed_warning) =
        shadowed_file_set(&definitions, &declarations, display_root.as_deref());
    parsed.shadowed_files = shadowed_files;
    parsed.shadowed_warning = shadowed_warning;
    Ok(parsed)
}

/// One `Definition` node of the document: the declaration it proves exists,
/// whether or not the elaboration produced a body for it (batch-20261118 B).
#[derive(Debug, Clone)]
struct DefinitionInfo {
    /// AST address the body's `definition` reference points at.
    addr: u64,
    /// Declared name (`uart`).
    name: String,
    /// `definitionKind` verbatim (`Module`, `Interface`, `Package`, …).
    kind: String,
    /// Source file of the declaration.
    file: PathBuf,
    /// 0-based line of the declaration.
    line: u32,
    /// 0-based BYTE column of the declaration (see [`Columns`]).
    column: u32,
    /// Whether the declaration's file belongs to the caller's file set (G1-b).
    /// A caller with NO file set cannot decide, so every declaration counts as
    /// inside there and the old collect-everything behaviour is preserved.
    in_file_set: bool,
}

/// The declaration of every `Definition` node, keyed by AST address.
///
/// `slang` writes them in the document's top-level `definitions` array; walking
/// the whole document also covers a layout that inlines the definitions, and an
/// address with no entry is treated as a module by the caller. This is the ONE
/// place a duplicate module NAME can be seen: the elaboration keeps a body for
/// only the declaration it processed last.
fn definition_index(document: &Value, file_order: &FileOrder<'_>) -> HashMap<u64, DefinitionInfo> {
    let mut definitions = HashMap::new();
    collect_definitions(document, file_order, &mut definitions);
    definitions
}

fn collect_definitions(
    value: &Value,
    file_order: &FileOrder<'_>,
    out: &mut HashMap<u64, DefinitionInfo>,
) {
    match value {
        Value::Object(map) => {
            if map.get("kind").and_then(Value::as_str) == Some("Definition") {
                if let Some(address) = map.get("addr").and_then(Value::as_u64) {
                    let (file, line, column) = symbol_position(value).unwrap_or_default();
                    let in_file_set = file_order.contains(&file);
                    out.insert(
                        address,
                        DefinitionInfo {
                            addr: address,
                            name: map
                                .get("name")
                                .and_then(Value::as_str)
                                .unwrap_or("")
                                .to_string(),
                            kind: map
                                .get("definitionKind")
                                .and_then(Value::as_str)
                                .unwrap_or("")
                                .to_string(),
                            file,
                            line,
                            column,
                            in_file_set,
                        },
                    );
                }
            }
            for child in map.values() {
                collect_definitions(child, file_order, out);
            }
        }
        Value::Array(items) => {
            for item in items {
                collect_definitions(item, file_order, out);
            }
        }
        _ => {}
    }
}

/// The AST address inside a `"<addr> <name>"` reference (`definition`, `body`, …).
fn definition_address(reference: &str) -> Option<u64> {
    reference.split_whitespace().next()?.parse::<u64>().ok()
}

/// Whether one `InstanceBody` declares a module (as opposed to an interface,
/// package or program).
fn is_module_definition(body: &Value, definitions: &HashMap<u64, DefinitionInfo>) -> bool {
    let Some(reference) = body.get("definition").and_then(Value::as_str) else {
        return true;
    };
    let Some(address) = definition_address(reference) else {
        return true;
    };
    match definitions.get(&address) {
        Some(info) => info.kind == "Module",
        None => true,
    }
}

/// The `members` array of one body, in declaration order.
fn body_members(body: &Value) -> Vec<&Value> {
    body.get("members")
        .and_then(Value::as_array)
        .map(|members| members.iter().collect())
        .unwrap_or_default()
}

/// The D2 preference key of one candidate definition.
///
/// Components, in order:
///
/// 1. the file's FIRST place in the caller's file set — the definition whose
///    file comes first wins, which is the order the user's `.f` declares;
/// 2. the file's spelling — reachable only when both candidates are outside the
///    file set (an include file, or a caller with no file set), and then it is
///    the document's spelling that decides, never a map's iteration order;
/// 3. the definition's own `(source_line, source_column)`, 0-based, which is the
///    contract's same-file rule and, for a file outside the set, a stable
///    second key;
/// 4. the AST address, which separates two candidates the document spells
///    identically. It is read out of the document itself, so the same input
///    always yields the same order (B-3).
type PreferenceOrder = (usize, String, u32, u32, u64);

/// The D2 preference key of one candidate definition BODY: the declaration key
/// plus the body's OWN AST address.
///
/// The address is what makes the key a TOTAL order (review F1): a body that
/// carries neither a `definition` reference nor a source position otherwise ties
/// with every other such body, and a stable sort would then keep the iteration
/// order of `Bodies::by_address` — a `HashMap`. `Bodies::index` only stores a
/// body that has an `addr`, and that address is the map key, so the last
/// component is unique among the candidates of one walk.
type BodyOrder = (PreferenceOrder, u64);

fn preference_order(
    order: &FileOrder<'_>,
    file: &Path,
    line: u32,
    column: u32,
    address: u64,
) -> PreferenceOrder {
    (
        order.rank(file),
        file.to_string_lossy().into_owned(),
        line,
        column,
        address,
    )
}

/// One module DECLARATION with its D2 preference key.
struct DeclarationCandidate<'a> {
    info: &'a DefinitionInfo,
    order: PreferenceOrder,
}

/// One candidate definition BODY with its D2 preference key.
struct BodyCandidate<'a> {
    body: &'a Value,
    /// The declaration this body belongs to, from its `definition` reference.
    definition: Option<u64>,
    order: BodyOrder,
}

/// The caller's ordered file set with the three lookups the D2 rule needs.
///
/// A project has thousands of files and every candidate definition asks for its
/// place, so the spellings that hit in practice (the exact path, the lexically
/// normalised one and the real path) are MAPS, and only the trailing-component
/// fallback scans the set — it is reachable for a file the set does not name (an
/// include file) or when the tool and the caller disagree on the prefix.
///
/// The real-path map exists because `absolutize_ast` is deliberately LEXICAL:
/// with a symlinked project root one side can spell the link while the other
/// reports the target, and a lexical comparison would then call an in-set
/// definition external. [`real_path`] resolves both directions to one key.
struct FileOrder<'a> {
    /// The caller's ordered set, `None` when it declared none: the G1-b boundary
    /// IS this option, so no walk can turn an empty set into "collect
    /// everything" by inspecting the document (review G1-b, batch-20261119).
    files: Option<&'a [PathBuf]>,
    exact: HashMap<&'a Path, usize>,
    normalized: HashMap<PathBuf, usize>,
    canonical: HashMap<PathBuf, usize>,
}

impl<'a> FileOrder<'a> {
    fn new(files: Option<&'a [PathBuf]>) -> Self {
        let members = files.unwrap_or(&[]);
        let mut exact = HashMap::with_capacity(members.len());
        let mut normalized = HashMap::with_capacity(members.len());
        let mut canonical = HashMap::with_capacity(members.len());
        for (index, file) in members.iter().enumerate() {
            // `entry(..).or_insert(..)` keeps the FIRST place, which is the order
            // the caller spelled (a file named twice keeps its first spot).
            exact.entry(file.as_path()).or_insert(index);
            let normalized_file = lexical(file);
            canonical
                .entry(real_path(&normalized_file))
                .or_insert(index);
            normalized.entry(normalized_file).or_insert(index);
        }
        Self {
            files,
            exact,
            normalized,
            canonical,
        }
    }

    /// Whether `file` belongs to the caller's file set (G1-b).
    ///
    /// A caller with NO file set (`None`) cannot decide membership, so every
    /// file counts as inside: `parse_catalog_json` keeps collecting every
    /// definition, which is its documented behaviour. With a set — INCLUDING an
    /// empty `Some(&[])` — membership is exactly [`Self::rank`]'s answer, so the
    /// spelling matches below count as inside too and an empty declared set
    /// contains nothing.
    fn contains(&self, file: &Path) -> bool {
        self.files.is_none() || self.rank(file) != usize::MAX
    }

    /// The index of the FIRST file-set entry that spells `file`, or `usize::MAX`
    /// when the set does not name it (which sorts it after every member).
    ///
    /// Four spellings are accepted, in this order of confidence: the exact path,
    /// the lexically normalised path (`.`/`..` collapsed, no symlink resolution —
    /// see [`crate::tool_paths::absolutize_ast`]), the real path
    /// (`std::fs::canonicalize`, so a symlinked root matches its target on either
    /// side), and a trailing-component match (the tool reports paths relative to
    /// its own cwd while the caller's file set is absolute, or the reverse).
    fn rank(&self, file: &Path) -> usize {
        if file.as_os_str().is_empty() {
            return usize::MAX;
        }
        if let Some(index) = self.exact.get(file) {
            return *index;
        }
        let normalized = lexical(file);
        if let Some(index) = self.normalized.get(&normalized) {
            return *index;
        }
        if let Some(index) = self.canonical.get(&real_path(&normalized)) {
            return *index;
        }
        self.files
            .unwrap_or(&[])
            .iter()
            .position(|entry| entry.ends_with(&normalized) || normalized.ends_with(entry))
            .unwrap_or(usize::MAX)
    }
}

/// The real path of `path` — `std::fs::canonicalize`, which resolves symlinks
/// and collapses `.`/`..` — with the lexical form as the fallback.
///
/// The fallback covers what `canonicalize` cannot answer: a path the document
/// mentions but which does not exist (a synthetic AST, a source deleted since
/// the tool ran). Such a path still gets a comparable spelling instead of an
/// error, and the boundary decision then rests on the spellings only.
fn real_path(path: &Path) -> PathBuf {
    std::fs::canonicalize(path).unwrap_or_else(|_| lexical(path))
}

/// The longest directory prefix the whole file set shares: the root a warning
/// should name paths relative to. `None` when the set shares no directory at all
/// (or is empty), in which case the document's own spelling is used.
fn common_directory(files: &[PathBuf]) -> Option<PathBuf> {
    let mut common: Option<Vec<std::ffi::OsString>> = None;
    for file in files {
        let parent = file.parent()?;
        let components: Vec<std::ffi::OsString> = parent
            .components()
            .map(|component| component.as_os_str().to_os_string())
            .collect();
        common = Some(match common {
            None => components,
            Some(current) => current
                .into_iter()
                .zip(components)
                .take_while(|(left, right)| left == right)
                .map(|(left, _)| left)
                .collect(),
        });
        if common.as_ref().is_some_and(Vec::is_empty) {
            return None;
        }
    }
    let components = common?;
    let mut root = PathBuf::new();
    for component in components {
        root.push(component);
    }
    Some(root)
}

/// A document path as a warning should read it: relative to the project root
/// when it lives under one, otherwise the document's own spelling.
fn display_path(file: &Path, root: Option<&Path>) -> String {
    if file.as_os_str().is_empty() {
        return "<unknown>".to_string();
    }
    root.and_then(|root| file.strip_prefix(root).ok())
        .filter(|relative| !relative.as_os_str().is_empty())
        .unwrap_or(file)
        .to_string_lossy()
        .into_owned()
}

/// One candidate a duplicate warning can name: the file it was written in (empty
/// when the document carries no position at all) and the AST address that
/// identifies it when there is no file to name (review F1).
struct DuplicateSite {
    file: PathBuf,
    addr: u64,
}

/// The D2 warning for one name declared more than once: the DISTINCT files
/// involved and the declaration the catalog adopted.
///
/// `declarations` is in preference order, so `[0]` is the adopted declaration.
/// `None` when every declaration lives in the same file (R1-a): that file and
/// its ports leave no "which one" question to answer.
fn duplicate_module_warning(
    name: &str,
    declarations: &[DeclarationCandidate<'_>],
    root: Option<&Path>,
) -> Option<String> {
    let sites: Vec<DuplicateSite> = declarations
        .iter()
        .map(|candidate| DuplicateSite {
            file: candidate.info.file.clone(),
            addr: candidate.info.addr,
        })
        .collect();
    duplicate_site_warning(name, &sites, root)
}

/// The B-2 warning for several BODIES of one name with fewer than two
/// `definitions` entries (review F1). The sentence is the declaration one built
/// from each body's own source position; a body without any position is named by
/// its AST address instead of the unusable `<unknown>`. `None` when every body
/// lives in the same file (R1-a).
fn duplicate_body_warning(
    name: &str,
    candidates: &[BodyCandidate<'_>],
    root: Option<&Path>,
) -> Option<String> {
    let sites: Vec<DuplicateSite> = candidates
        .iter()
        .map(|candidate| {
            let (file, _line, _column) = symbol_position(candidate.body).unwrap_or_default();
            let (_, body_addr) = candidate.order;
            DuplicateSite {
                file,
                addr: body_addr,
            }
        })
        .collect();
    duplicate_site_warning(name, &sites, root)
}

/// The one sentence every duplicate-module path ends in, aggregated by the path
/// the panel shows (R1-a/R1-b).
///
/// * `None` when every site resolves to the SAME `display_path`: one file, one
///   set of ports, no ambiguity — and a file that declares a module ninety-six
///   times can no longer flood the module tree's summary with ninety-six
///   identical absolute paths. The old `defined twice in … (lines A and B)`
///   sentence is silenced by the same rule.
/// * two distinct files keep the B-2 sentence byte for byte (`defined in both A
///   and B; using A`), which the contract and the existing tests depend on.
/// * three or more distinct files name at most THREE files, folding the rest
///   into `+N more`, so the sentence is bounded by the name, not the project.
fn duplicate_site_warning(
    name: &str,
    sites: &[DuplicateSite],
    root: Option<&Path>,
) -> Option<String> {
    let adopted = &sites[0];
    let adopted_file = display_path(&adopted.file, root);
    if adopted.file.as_os_str().is_empty() {
        // A degenerate document with no source position still must be told
        // apart: the AST addresses are read out of the document itself, so the
        // sentence stays deterministic (review F1).
        let others: Vec<String> = sites[1..]
            .iter()
            .map(|site| format!("AST address {}", site.addr))
            .collect();
        return Some(format!(
            "module '{name}' has {} definitions with no source position; using the definition \
             at AST address {} (also at {})",
            sites.len(),
            adopted.addr,
            others.join(", ")
        ));
    }
    // Aggregate on the DISPLAYED path, not the raw `PathBuf`: the same file can
    // reach the document with two spellings (`/root/stdcell.v` and `./stdcell.v`)
    // and only the shown path is what the user would see twice.
    let mut other_files: Vec<String> = Vec::new();
    for site in &sites[1..] {
        let shown = display_path(&site.file, root);
        if shown != adopted_file && !other_files.contains(&shown) {
            other_files.push(shown);
        }
    }
    if other_files.is_empty() {
        // R1-a: every declaration is in the adopted file. Nothing to choose.
        return None;
    }
    if other_files.len() == 1 {
        // R1-b: exactly two distinct files keep the B-2 sentence verbatim.
        let other_file = &other_files[0];
        return Some(format!(
            "module '{name}' is defined in both {other_file} and {adopted_file}; \
             using {adopted_file}"
        ));
    }
    // R1-b: name at most three distinct files, then `+N more`. The count is
    // still the number of DECLARATIONS (the old sentence's meaning), while the
    // list is bounded by distinct files.
    const MAX_LISTED_FILES: usize = 3;
    let distinct_files = other_files.len() + 1;
    let mut listed: Vec<&str> = other_files
        .iter()
        .take(MAX_LISTED_FILES)
        .map(String::as_str)
        .collect();
    if listed.len() < MAX_LISTED_FILES {
        // Keep the old spelling's shape: the adopted file closes the list.
        listed.push(adopted_file.as_str());
    }
    let mut files = listed.join(", ");
    if distinct_files > MAX_LISTED_FILES {
        files.push_str(&format!(", +{} more", distinct_files - MAX_LISTED_FILES));
    }
    Some(format!(
        "module '{name}' is defined {} times ({files}); using {adopted_file}",
        sites.len()
    ))
}

/// G1-c: the files whose EVERY module declaration lost the duplicate pick, plus
/// the sentence naming them.
///
/// `definitions` is every `Definition` node of the document, `declarations`
/// groups the MODULE ones by name in D2 preference order (so a group's first
/// in-set entry is the declaration the catalog adopted).
///
/// A file is returned only when BOTH hold:
///
/// * at least one of its declarations lost to a declaration in ANOTHER file, and
/// * every definition the file declares is such a loser — no winning and no
///   unique module, and no interface/package/program either.
///
/// The second half is the safety boundary. A file that still declares something
/// the compiler must see is never returned, because excluding it would remove
/// that definition from the design entirely (the B-2 warning already names the
/// collision, so the user is still told). Three shapes fall on the keeping side
/// by construction rather than by a special case:
///
/// * a repeat inside ONE file: the adopted declaration is in that file, so the
///   file holds a winner and is kept — a file can never shadow itself;
/// * a file whose declarations all live outside the caller's file set: it is not
///   part of the compiler input to begin with (G1-b);
/// * a module definition the walk cannot group (an empty name, an older
///   document): it is not a duplicate pick, so the file is kept.
///
/// The identity key is the lexically normalised path (the same normalisation
/// [`FileOrder`] uses), so two spellings of one file cannot split its verdict;
/// the path published is the document's own spelling.
fn shadowed_file_set(
    definitions: &HashMap<u64, DefinitionInfo>,
    declarations: &BTreeMap<String, Vec<DeclarationCandidate<'_>>>,
    root: Option<&Path>,
) -> (Vec<PathBuf>, Option<String>) {
    // The files with at least one losing declaration, key -> published path.
    let mut losing: BTreeMap<PathBuf, PathBuf> = BTreeMap::new();
    // The files that must stay in the compiler input.
    let mut keeping: HashSet<PathBuf> = HashSet::new();
    for info in definitions.values() {
        if !info.in_file_set || info.file.as_os_str().is_empty() {
            continue;
        }
        let key = lexical(&info.file);
        let Some(group) = declarations.get(&info.name) else {
            keeping.insert(key);
            continue;
        };
        // The adopted declaration of this name: the first in-set candidate.
        let adopted = group
            .iter()
            .find(|candidate| candidate.info.in_file_set)
            .map(|candidate| candidate.info);
        let lost = info.kind == "Module"
            && group.len() > 1
            && adopted
                .is_some_and(|winner| winner.addr != info.addr && lexical(&winner.file) != key);
        if lost {
            losing.entry(key).or_insert_with(|| info.file.clone());
        } else {
            keeping.insert(key);
        }
    }
    let shadowed: Vec<PathBuf> = losing
        .into_iter()
        .filter(|(key, _file)| !keeping.contains(key))
        .map(|(_key, file)| file)
        .collect();
    let warning = shadowed_warning(&shadowed, root);
    (shadowed, warning)
}

/// G1-c: the sentence the module tree shows AFTER the duplicate warnings, saying
/// what happens to the files they name.
///
/// `None` when nothing is shadowed. One file is named directly; several are
/// counted and listed up to three (then `+N more`), the same bound
/// [`duplicate_site_warning`] uses, so a project-wide duplicate cannot turn the
/// summary line into a file dump.
fn shadowed_warning(shadowed: &[PathBuf], root: Option<&Path>) -> Option<String> {
    let names: Vec<String> = shadowed
        .iter()
        .map(|file| display_path(file, root))
        .collect();
    match names.as_slice() {
        [] => None,
        [only] => Some(format!(
            "{only} is excluded from the compiler input because every module it defines lost \
             the duplicate pick"
        )),
        many => {
            const MAX_LISTED_FILES: usize = 3;
            let listed: Vec<&str> = many
                .iter()
                .take(MAX_LISTED_FILES)
                .map(String::as_str)
                .collect();
            let mut files = listed.join(", ");
            if many.len() > MAX_LISTED_FILES {
                files.push_str(&format!(", +{} more", many.len() - MAX_LISTED_FILES));
            }
            Some(format!(
                "{} files are excluded from the compiler input because every module they define \
                 lost the duplicate pick ({files})",
                many.len()
            ))
        }
    }
}

/// G1-b: ONE bounded sentence for the modules the walk excluded because every
/// definition of them lives outside the caller's file set (typically a
/// `` `include ``d file). At most THREE examples are named as `'name' in path`;
/// the rest are folded into `+N more`, so the sentence is bounded by the module
/// count, not the project size.
fn outside_file_set_warning(excluded: &[(String, PathBuf)], root: Option<&Path>) -> String {
    const MAX_LISTED: usize = 3;
    let examples: Vec<String> = excluded
        .iter()
        .take(MAX_LISTED)
        .map(|(name, file)| format!("'{name}' in {}", display_path(file, root)))
        .collect();
    let mut listed = examples.join(", ");
    if excluded.len() > MAX_LISTED {
        listed.push_str(&format!(", +{} more", excluded.len() - MAX_LISTED));
    }
    format!(
        "{} module(s) are defined only in files outside the file list (e.g. {listed}); \
         they are excluded",
        excluded.len()
    )
}

/// Review F4: the adopted declaration is in the document but the elaboration
/// produced no body for it, so the module's empty port list means the data is
/// UNAVAILABLE — not that the module has no ports.
///
/// This is a SECOND warning, after the B-2 sentence, whose text stays
/// byte-identical.
fn no_body_warning(name: &str, info: &DefinitionInfo, root: Option<&Path>) -> String {
    format!(
        "ports of module '{name}' are unavailable: the adopted definition in {} (line {}) \
         has no body in the elaborated AST",
        display_path(&info.file, root),
        info.line + 1
    )
}

/// The `duplicate definition of '<name>'` diagnostics of one child's stderr, as
/// `(name, verbatim line)` in the order they were printed, one entry per NAME.
///
/// `slang` exits non-zero when it reported one of these but still writes the
/// whole AST JSON (12.1.0 does exactly that), and the crate rule is that a
/// non-zero exit WITH a usable document is a normal result. B-5 forbids letting
/// the tool's own sentence vanish with the exit code, so it is read here and
/// either explained by a D2 warning or quoted in the catalog warning.
fn duplicate_definition_lines(stderr: &str) -> Vec<(String, String)> {
    let mut seen: HashSet<String> = HashSet::new();
    let mut found = Vec::new();
    for raw in stderr.lines() {
        let line = raw.trim();
        let Some(rest) = line.split("duplicate definition of '").nth(1) else {
            continue;
        };
        let Some((name, _)) = rest.split_once('\'') else {
            continue;
        };
        if name.is_empty() || !seen.insert(name.to_string()) {
            continue;
        }
        found.push((name.to_string(), line.to_string()));
    }
    found
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
    definitions: &HashMap<u64, DefinitionInfo>,
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
        if !is_module_definition(body, definitions) {
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
        let streamed = parse_catalog_value_in(&pruned.document, None)
            .unwrap_or_else(|error| panic!("streamed parse: {error}"));
        assert_eq!(whole, streamed.modules);
        assert!(
            streamed.warnings.is_empty(),
            "a clean document has no duplicate warning: {:?}",
            streamed.warnings
        );
    }
}
