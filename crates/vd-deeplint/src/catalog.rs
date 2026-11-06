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
    join_path, node_kind, run_ast_json, scope_instances, symbol_position, Bodies,
};
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
        match &self.failure {
            Some(failure) => failure.user_message(),
            None => format!(
                "{} module(s), {} ms",
                self.modules.len(),
                self.elapsed.as_millis()
            ),
        }
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
    /// Wall-clock budget for the run.
    pub timeout: Duration,
    /// Cap for the `--ast-json` file.
    pub max_json_bytes: usize,
    /// Oversize threshold for the file set (same warning as the hierarchy).
    pub max_files: usize,
}

impl Default for CatalogOptions {
    fn default() -> Self {
        Self {
            slang_binary: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: crate::hierarchy::DEFAULT_TIMEOUT,
            max_json_bytes: crate::hierarchy::DEFAULT_MAX_JSON_BYTES,
            max_files: crate::hierarchy::DEFAULT_MAX_FILES,
        }
    }
}

/// Elaborate `files` and build the module catalog.
///
/// A project-wide elaboration (`--top` is never passed): every uninstantiated
/// module becomes a design root, which is what makes `is_top` and
/// "defined but never instantiated" observable.
#[must_use]
pub fn build_catalog(files: &[PathBuf], options: &CatalogOptions) -> ModuleCatalog {
    let run_options = HierarchyOptions {
        slang_binary: options.slang_binary.clone(),
        top: None,
        include_dirs: options.include_dirs.clone(),
        defines: options.defines.clone(),
        timeout: options.timeout,
        max_json_bytes: options.max_json_bytes,
        max_files: options.max_files,
        ..HierarchyOptions::default()
    };
    let (report, text) = run_ast_json(files, &run_options);
    let mut catalog = ModuleCatalog {
        tool: report.tool,
        modules: Vec::new(),
        elapsed: report.elapsed,
        peak_rss_kb: report.peak_rss_kb,
        warning: None,
        failure: report.failure,
    };
    if catalog.failure.is_some() {
        return catalog;
    }
    let Some(text) = text else {
        catalog.failure = Some(LintFailure::Io {
            message: "the elaboration produced no AST JSON".to_string(),
        });
        return catalog;
    };
    match parse_catalog_json(&text) {
        Ok(modules) => {
            catalog.modules = modules;
            if files.len() > options.max_files {
                catalog.warning = Some(format!(
                    "the project has {} files (limit {}): the module tree may be slow",
                    files.len(),
                    options.max_files
                ));
            }
        }
        Err(message) => {
            catalog.failure = Some(LintFailure::InvalidJson {
                message,
                stderr: String::new(),
            });
        }
    }
    catalog
}

/// Parse one `--ast-json` document into the catalog's module list.
///
/// # Errors
///
/// Returns the reason when the document is malformed or carries no elaborated
/// root instance (the two cases the caller must report, not paper over).
pub fn parse_catalog_json(text: &str) -> Result<Vec<CatalogModule>, String> {
    let document: Value = serde_json::from_str(text)
        .map_err(|error| format!("the AST JSON is malformed: {error}"))?;
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

    let bodies = Bodies::index(&document);
    let definition_kinds = definition_kinds(&document);

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
    for (name, body) in &module_bodies {
        let (file, line, column) = symbol_position(body).unwrap_or_default();
        let members: Vec<&Value> = body
            .get("members")
            .and_then(Value::as_array)
            .map(|members| members.iter().collect())
            .unwrap_or_default();
        let ports = ports_of(&members);
        let children = children_of(name, &members, &bodies, &definition_kinds, &paths);
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
fn ports_of(members: &[&Value]) -> Vec<ModulePort> {
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
                let (file, line, column) = symbol_position(member).unwrap_or_default();
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
                let (file, line, column) = symbol_position(member).unwrap_or_default();
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
        let (file, line, column) = symbol_position(instance).unwrap_or_default();
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
}
