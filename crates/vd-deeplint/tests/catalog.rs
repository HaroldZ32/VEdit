//! Module catalog (ux-batch-2 A1).
//!
//! Two layers of evidence, in the hierarchy test's style:
//! * the parser, against a captured `--ast-json` document (deterministic, no
//!   tool and no filesystem race);
//! * the runner, against the REAL `slang` binary on the catalog fixture, where
//!   the top set, the port type/direction values and the child edges are the
//!   hand-written expectation, not the parser's own output.

use std::fs;
use std::path::{Path, PathBuf};

use vd_deeplint::{
    build_catalog, parse_catalog_json, CatalogModule, CatalogOptions, PortDirection,
};

fn fixture_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures")
}

fn catalog_fixture() -> PathBuf {
    fixture_dir().join("catalog")
}

fn ast_json() -> String {
    fs::read_to_string(catalog_fixture().join("ast.json"))
        .unwrap_or_else(|error| panic!("reading the captured AST: {error}"))
}

fn project_files(root: &Path) -> Vec<PathBuf> {
    let mut files: Vec<PathBuf> = fs::read_dir(root)
        .unwrap_or_else(|error| panic!("reading {}: {error}", root.display()))
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "sv"))
        .collect();
    files.sort();
    files
}

fn slang_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        return PathBuf::from(value);
    }
    let candidate = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/slang/out/slang");
    assert!(
        candidate.exists(),
        "slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh` (looked at {})",
        candidate.display()
    );
    candidate
}

fn module<'a>(modules: &'a [CatalogModule], name: &str) -> &'a CatalogModule {
    modules
        .iter()
        .find(|module| module.name == name)
        .unwrap_or_else(|| panic!("module {name} missing"))
}

fn names(modules: &[CatalogModule]) -> Vec<&str> {
    modules.iter().map(|module| module.name.as_str()).collect()
}

fn tops(modules: &[CatalogModule]) -> Vec<&str> {
    modules
        .iter()
        .filter(|module| module.is_top)
        .map(|module| module.name.as_str())
        .collect()
}

/// The hand-written expectation for `tests/fixtures/catalog/project`.
///
/// Modules (sorted by name): `consumer`, `orphan`, `reflink`, `shared`,
/// `top_a`, `top_b` — the interface `bus_if` is NOT a module. Tops are the
/// modules never instantiated: `orphan`, `top_a`, `top_b`.
fn assert_catalog_shape(modules: &[CatalogModule]) {
    assert_eq!(
        names(modules),
        vec!["consumer", "orphan", "reflink", "shared", "top_a", "top_b"],
        "the interface `bus_if` must not be catalogued as a module"
    );
    assert_eq!(tops(modules), vec!["orphan", "top_a", "top_b"]);
    for instantiated in ["shared", "consumer", "reflink"] {
        assert!(
            !module(modules, instantiated).is_top,
            "{instantiated} is instantiated and must not be a top"
        );
    }

    // Ports: name, direction, type — the exact values the ABI hands to the UI.
    let top_a = module(modules, "top_a");
    let ports: Vec<(&str, PortDirection, &str)> = top_a
        .ports
        .iter()
        .map(|port| (port.name.as_str(), port.direction, port.type_name.as_str()))
        .collect();
    assert_eq!(
        ports,
        vec![
            ("clk", PortDirection::In, "logic"),
            ("pad", PortDirection::InOut, "logic"),
            ("y", PortDirection::Out, "logic[3:0]"),
        ]
    );

    // An interface-typed port does not crash and is not dropped: it is
    // bidirectional by construction and its type is the interface name.
    let consumer = module(modules, "consumer");
    let interface_port = &consumer.ports[0];
    assert_eq!(interface_port.name, "bus");
    assert_eq!(interface_port.direction, PortDirection::InOut);
    assert_eq!(interface_port.type_name, "bus_if");
    assert!(interface_port.file.ends_with("consumer.sv"));

    // A `ref` port keeps its direction; the four-value ABI must not fold it
    // into InOut.
    assert_eq!(
        module(modules, "reflink").ports[0].direction,
        PortDirection::Ref
    );
    assert_eq!(module(modules, "reflink").ports[0].type_name, "logic");

    // Definition positions come from the `InstanceBody` (module name position),
    // 0-based.
    assert!(top_a.file.ends_with("top_a.sv"));
    assert_eq!((top_a.line, top_a.column), (0, 7));

    // Children: the same module (`shared`) is instantiated under two parents and
    // must appear in BOTH parents' children; the interface instance inside
    // `top_a` is not a module instantiation and must not appear.
    let mut top_a_children: Vec<(&str, &str)> = top_a
        .children
        .iter()
        .map(|child| (child.module.as_str(), child.path.as_str()))
        .collect();
    top_a_children.sort_unstable();
    assert_eq!(
        top_a_children,
        vec![
            ("consumer", "top_a.u_consumer"),
            ("reflink", "top_a.u_ref"),
            ("shared", "top_a.u_shared"),
        ]
    );
    let shared_edges: Vec<&str> = module(modules, "shared")
        .children
        .iter()
        .map(|child| child.path.as_str())
        .collect();
    assert!(
        shared_edges.is_empty(),
        "shared has no child instances: {shared_edges:?}"
    );
    for top in ["top_a", "top_b"] {
        let children: Vec<&str> = module(modules, top)
            .children
            .iter()
            .map(|child| child.module.as_str())
            .collect();
        assert!(
            children.contains(&"shared"),
            "{top} instantiates shared: {children:?}"
        );
    }
    // Two instances of the SAME module under one parent are two edges with
    // distinct paths, not one deduplicated child.
    let top_b_children: Vec<&str> = module(modules, "top_b")
        .children
        .iter()
        .map(|child| child.path.as_str())
        .collect();
    assert_eq!(
        top_b_children,
        vec!["top_b.u_shared", "top_b.u_shared_2"],
        "same-module multiple instantiations keep both edges"
    );
    // The instantiation site of `u_shared` inside top_a: line 4 (1-based),
    // column 10 (1-based) -> 0-based line 3, column 9.
    let shared_edge = module(modules, "top_a")
        .children
        .iter()
        .find(|child| child.module == "shared")
        .unwrap_or_else(|| panic!("top_a -> shared"));
    assert!(shared_edge.file.ends_with("top_a.sv"));
    assert_eq!((shared_edge.line, shared_edge.column), (3, 9));

    // The orphan is defined but instantiated nowhere.
    assert_eq!(module(modules, "orphan").children.len(), 0);
}

#[test]
fn the_captured_ast_yields_the_module_catalog() {
    let modules = parse_catalog_json(&ast_json()).unwrap_or_else(|error| panic!("parse: {error}"));
    assert_catalog_shape(&modules);
}

#[test]
fn the_real_slang_binary_matches_the_hand_written_catalog() {
    let project = catalog_fixture().join("project");
    let files = project_files(&project);
    assert!(!files.is_empty(), "the catalog fixture has no files");
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "the catalog run failed: {:?}",
        catalog.failure
    );
    assert!(catalog.tool.contains("slang"), "tool: {}", catalog.tool);
    assert_catalog_shape(&catalog.modules);
}

/// A generate loop and an instance array produce several elaborated instances at
/// the same source position; the definition-keyed catalog reports ONE child edge
/// per site, and a module instantiated under two parents keeps one entry whose
/// edge is listed under both parents.
#[test]
fn generate_instances_collapse_to_one_edge_per_site() {
    let project = fixture_dir().join("hierarchy/project");
    let files = project_files(&project);
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "the catalog run failed: {:?}",
        catalog.failure
    );
    assert_eq!(
        names(&catalog.modules),
        vec!["arr", "leaf", "mid", "top1", "top2"]
    );
    assert_eq!(tops(&catalog.modules), vec!["arr", "top1", "top2"]);

    let mid = module(&catalog.modules, "mid");
    let children: Vec<(&str, u32)> = mid
        .children
        .iter()
        .map(|child| (child.module.as_str(), child.line))
        .collect();
    // Two distinct generate sites (`g_leaf` loop and `g_if` block), each
    // collapsed from its elaborated copies to one edge; 0-based lines 5 and 9.
    assert_eq!(
        children,
        vec![("leaf", 5), ("leaf", 9)],
        "mid: {children:?}"
    );

    for top in ["top1", "top2"] {
        let children: Vec<&str> = module(&catalog.modules, top)
            .children
            .iter()
            .map(|child| child.module.as_str())
            .collect();
        assert_eq!(children, vec!["mid"], "{top}");
    }
    // `arr` instantiates mid as an array: one edge at the array's site.
    let arr_children: Vec<&str> = module(&catalog.modules, "arr")
        .children
        .iter()
        .map(|child| child.module.as_str())
        .collect();
    assert_eq!(arr_children, vec!["mid"]);
}
