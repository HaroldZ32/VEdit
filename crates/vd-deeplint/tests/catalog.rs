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
    build_catalog, parse_catalog_json, parse_catalog_json_in, CatalogModule, CatalogOptions,
    PortDirection,
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

/// One spawning catalog test at a time.
///
/// The fake `slang` scripts are written to disk and then exec'd. A `fork` in
/// ANOTHER test thread can inherit a script's write descriptor for a moment;
/// exec'ing that script while such a child still holds the descriptor fails
/// with `ETXTBSY` ("Text file busy"). The repo has seen this race before (see
/// the comment in `tests/hierarchy.rs`); with several catalog tests spawning
/// tools in parallel it became reachable, so the spawning tests serialize. This
/// is test-only synchronization: no assertion changed.
static SPAWN_SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn spawn_serial() -> std::sync::MutexGuard<'static, ()> {
    match SPAWN_SERIAL.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
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
    let _serial = spawn_serial();
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
    let _serial = spawn_serial();
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

/// Write the realistic project shape: `top` -> `mids` x `mid_i` -> `leaves` x
/// `leaf_{i}_{j}` (depth 3, all 1201 files small). Used by the completeness
/// test: a streaming parser that silently drops the last N definitions would
/// still return a catalogue — just a SHORT one, which the count catches.
fn write_tree_project(root: &Path, mids: usize, leaves: usize) {
    let mut top = String::from("module top (input logic clk, output logic q);\n");
    for mid in 0..mids {
        top.push_str(&format!("  mid_{mid} u_mid_{mid} (.clk(clk), .q());\n"));
    }
    top.push_str("  assign q = 1'b0;\nendmodule\n");
    fs::write(root.join("top.sv"), top).unwrap_or_else(|error| panic!("writing top.sv: {error}"));

    for mid in 0..mids {
        let mut body = format!("module mid_{mid} (input logic clk, output logic q);\n");
        for leaf in 0..leaves {
            let index = mid * leaves + leaf;
            body.push_str(&format!(
                "  leaf_{index} u_leaf_{leaf} (.clk(clk), .q());\n"
            ));
        }
        body.push_str("  assign q = 1'b0;\nendmodule\n");
        fs::write(root.join(format!("mid_{mid}.sv")), body)
            .unwrap_or_else(|error| panic!("writing mid_{mid}.sv: {error}"));
        for leaf in 0..leaves {
            let index = mid * leaves + leaf;
            fs::write(
                root.join(format!("leaf_{index}.sv")),
                format!(
                    "module leaf_{index} (input logic clk, output logic q);\n  assign q = clk;\nendmodule\n"
                ),
            )
            .unwrap_or_else(|error| panic!("writing leaf_{index}.sv: {error}"));
        }
    }
}

#[test]
fn a_1201_file_tree_is_catalogued_completely() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    write_tree_project(dir.path(), 40, 29);
    let files = project_files(dir.path());
    assert_eq!(files.len(), 1201, "the synthetic project has 1201 files");

    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            // The point of this test is completeness; the file-count oversize
            // warning is exercised separately by the 1176-file real run.
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "the 1201-file catalog failed: {:?}",
        catalog.failure
    );
    assert!(
        catalog.warning.is_none(),
        "a complete catalog must not carry a warning: {:?}",
        catalog.warning
    );
    assert_eq!(
        catalog.modules.len(),
        1201,
        "every one of the 1201 definitions must be catalogued (a streaming parser that drops the tail shows up here)"
    );

    let top = module(&catalog.modules, "top");
    assert!(top.is_top, "top is never instantiated");
    assert_eq!(top.children.len(), 40, "top wires 40 mids");
    assert_eq!(module(&catalog.modules, "mid_0").children.len(), 29);
    assert_eq!(module(&catalog.modules, "mid_39").children.len(), 29);
    assert!(!module(&catalog.modules, "leaf_0").is_top);
    assert!(!module(&catalog.modules, "leaf_1159").is_top);
}

#[test]
fn an_over_budget_document_degrades_to_a_warning_not_a_failure() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let fake = dir.path().join("fake-slang");
    let captured = catalog_fixture().join("ast.json");
    let script = format!(
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--ast-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncp '{}' \"$out\"\nexit 0\n",
        captured.display()
    );
    fs::write(&fake, script).unwrap_or_else(|error| panic!("writing the fake tool: {error}"));
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&fake)
            .unwrap_or_else(|error| panic!("stat the fake tool: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&fake, permissions)
            .unwrap_or_else(|error| panic!("chmod the fake tool: {error}"));
    }

    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            // One KEPT structural node: the first Instance survives, its body
            // and every later structural node are dropped.
            max_retained_nodes: 1,
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "an over-budget document must not fail the whole catalog: {:?}",
        catalog.failure
    );
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the partial catalog must carry a warning"));
    assert!(
        warning.contains("retention budget"),
        "the warning must name the budget and the partial result: {warning}"
    );
}

/// A fake `slang` that copies `source` to the `--ast-json` path and exits 0.
fn fake_slang_copying(dir: &Path, source: &Path) -> PathBuf {
    let fake = dir.join("fake-slang");
    let script = format!(
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--ast-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncp '{}' \"$out\"\nexit 0\n",
        source.display()
    );
    fs::write(&fake, script).unwrap_or_else(|error| panic!("writing the fake tool: {error}"));
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&fake)
            .unwrap_or_else(|error| panic!("stat the fake tool: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&fake, permissions)
            .unwrap_or_else(|error| panic!("chmod the fake tool: {error}"));
    }
    fake
}

#[test]
fn the_streamed_catalog_matches_the_whole_string_parse() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let fake = fake_slang_copying(dir.path(), &catalog_fixture().join("ast.json"));
    let files = project_files(&catalog_fixture().join("project"));
    let streamed = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        streamed.failure.is_none(),
        "the streamed catalog failed: {:?}",
        streamed.failure
    );
    // The producer resolves slang's relative `source_file` values against the
    // directory the tool ran in (the parent of the first file, see
    // `tool_paths`). The streaming path knows that directory; the whole-string
    // entry point needs it handed in, otherwise this parity check would compare
    // different path spellings instead of different interpretations.
    let tool_dir = files
        .first()
        .and_then(|file| file.parent())
        .unwrap_or_else(|| panic!("the fixture file has no parent directory"));
    let expected = parse_catalog_json_in(&ast_json(), Some(tool_dir))
        .unwrap_or_else(|error| panic!("whole-string parse: {error}"));
    assert_eq!(
        streamed.modules, expected,
        "the streaming prune must produce exactly the catalog the whole-string parse produces"
    );
    assert!(
        streamed.warning.is_none(),
        "the 44 KiB fixture is under every threshold"
    );
}

#[test]
fn a_document_over_the_size_threshold_is_warned_not_rejected() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let fake = fake_slang_copying(dir.path(), &catalog_fixture().join("ast.json"));
    let files = project_files(&catalog_fixture().join("project"));
    // The old behaviour: anything above this "cap" failed the whole catalog
    // (`lint diagnostics exceeded the N byte limit`). Now it is a size note.
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            max_json_bytes: 1024,
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "a document above the size threshold must not fail the catalog: {:?}",
        catalog.failure
    );
    assert_catalog_shape(&catalog.modules);
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the size threshold must be reported"));
    assert!(
        warning.contains("size threshold") && !warning.contains("partial"),
        "the size note must not claim the catalog is partial: {warning}"
    );
}

/// Real-scale run (U9a): the generated 1176-file project whose `--ast-json`
/// document is FAR above the hierarchy's 256 MiB AST cap.
///
/// Ignored by default: it needs the generated fixture (see
/// `evidence/ux-batch-2/u9a-catalog-scale/gen_design.py`) and spends seconds in
/// `slang`. Run it deliberately:
///
/// ```text
/// VEDIT_U9A_BIGDIR=evidence/ux-batch-2/u9a-catalog-scale/bigdesign-med \
///   cargo test -p vd-deeplint --test catalog -- --ignored --nocapture
/// ```
#[test]
#[ignore = "needs VEDIT_U9A_BIGDIR (a generated 1176-file project whose AST JSON is > 256 MiB)"]
fn the_1176_file_design_yields_a_full_catalog() {
    let _serial = spawn_serial();
    let root = std::env::var("VEDIT_U9A_BIGDIR")
        .unwrap_or_else(|_| panic!("set VEDIT_U9A_BIGDIR to the generated project root"));
    let files = project_files(Path::new(&root));
    assert!(
        files.len() >= 1000,
        "the generated project has {} files; expected the 1176-file design",
        files.len()
    );
    let started = std::time::Instant::now();
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            ..CatalogOptions::default()
        },
    );
    let tops = catalog
        .modules
        .iter()
        .filter(|module| module.is_top)
        .count();
    // Our own high-water mark: /usr/bin/time on a test binary reports the
    // CHILD (slang) maximum too, which hides what the catalog process itself
    // needed. VmHWM is the kernel's own number and needs no unsafe code.
    let self_peak_rss_kb = std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|status| {
            status
                .lines()
                .find_map(|line| line.strip_prefix("VmHWM:"))
                .and_then(|rest| rest.split_whitespace().next()?.parse::<u64>().ok())
        })
        .unwrap_or(0);
    println!(
        "big-catalog: files={} modules={} tops={} service_elapsed_ms={} slang_peak_rss_kb={} self_peak_rss_kb={} wall_ms={} warning={:?} failure={:?}",
        files.len(),
        catalog.modules.len(),
        tops,
        catalog.elapsed.as_millis(),
        catalog.peak_rss_kb,
        self_peak_rss_kb,
        started.elapsed().as_millis(),
        catalog.warning,
        catalog.failure
    );
    assert!(
        catalog.failure.is_none(),
        "a real-scale catalog must not fail: {:?}",
        catalog.failure
    );
    assert!(
        catalog.modules.len() >= 1000,
        "the module tree must cover the design, got {} module(s)",
        catalog.modules.len()
    );
    let warning = catalog.warning.as_deref().unwrap_or("");
    assert!(
        !warning.contains("partial"),
        "the module tree must be complete, not truncated: {warning}"
    );
}

#[test]
fn a_catalog_failure_never_blames_lint_diagnostics() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    // A syntactically valid document with no elaborated design: the catalog
    // cannot answer, and its reason must be about the AST JSON, not lint.
    let broken = dir.path().join("broken.json");
    fs::write(&broken, "{\"design\": {}}").unwrap_or_else(|error| panic!("broken JSON: {error}"));
    let fake = fake_slang_copying(dir.path(), &broken);
    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    let failure = catalog
        .failure
        .as_ref()
        .unwrap_or_else(|| panic!("a document without a design must fail the catalog"));
    let message = catalog
        .failure_message()
        .unwrap_or_else(|| panic!("the failure must have a message"));
    assert!(
        message.contains("AST JSON"),
        "the message must name the artifact: {message}"
    );
    assert!(
        !message.contains("lint") && !message.contains("diagnostics"),
        "the catalog must not borrow the lint vocabulary: {message}"
    );
    // The Display form stays available for logs and is equally truthful.
    let rendered = failure.to_string();
    assert!(
        !rendered.contains("diagnostics"),
        "the Display form must not blame diagnostics either: {rendered}"
    );
}

/// An AST document with `levels` nested instances.
///
/// Each instance costs exactly three JSON levels (`Instance` object ->
/// `body` object -> `members` array), so the document's deepest container is at
/// `3 * (levels - 1) + 6` (the `design` member prefix adds 3).
fn synthetic_instance_chain(levels: usize) -> String {
    let mut document = String::with_capacity(levels * 320 + 256);
    document.push_str("{\"design\":{\"name\":\"$root\",\"kind\":\"Root\",\"addr\":1,\"members\":[");
    for index in 0..levels {
        document.push_str(&format!(
            "{{\"name\":\"m{index}\",\"kind\":\"Instance\",\"source_file\":\"top.sv\",\"source_line\":1,\"source_column\":1,\"addr\":{},\"body\":{{\"name\":\"m{index}\",\"kind\":\"InstanceBody\",\"source_file\":\"top.sv\",\"source_line\":1,\"source_column\":1,\"addr\":{},\"members\":[",
            10 + index * 2,
            11 + index * 2
        ));
    }
    for _ in 0..levels {
        document.push_str("]}}");
    }
    document.push_str("]},\"definitions\":[]}");
    document
}

fn catalog_for_document(dir: &Path, document: &str) -> vd_deeplint::ModuleCatalog {
    let path = dir.join("synthetic.json");
    fs::write(&path, document).unwrap_or_else(|error| panic!("writing the synthetic AST: {error}"));
    let fake = fake_slang_copying(dir, &path);
    let files = project_files(&catalog_fixture().join("project"));
    build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    )
}

#[test]
fn a_chain_just_past_serde_json_s_default_guard_is_catalogued() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    // 42 nested instances put the document at 129 JSON levels: past serde_json's
    // 128-value default guard, i.e. the SMALLEST design depth that used to fail
    // (one instance level = 3 JSON levels). U9d must parse it instead of
    // returning an empty catalog.
    let document = synthetic_instance_chain(42);
    let catalog = catalog_for_document(dir.path(), &document);
    assert!(
        catalog.failure.is_none(),
        "a normal-depth design must be catalogued: {:?}",
        catalog.failure
    );
    assert_eq!(
        catalog.modules.len(),
        42,
        "every nested instance must appear in the module tree"
    );
}

#[test]
fn a_chain_just_below_the_nesting_cap_completes_on_the_walk_thread() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    // 3000 instances -> 9003 JSON levels, just below the 10,000-level cap. The
    // walk runs on the dedicated 64 MiB-stack thread: if that stack were too
    // small this test would die (abort), not merely fail. Measured proof that
    // the verified-bound unlock is stack-safe.
    let document = synthetic_instance_chain(3000);
    let catalog = catalog_for_document(dir.path(), &document);
    assert!(
        catalog.failure.is_none(),
        "a document just below the cap must be parsed: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 3000);
}

#[test]
fn a_document_above_the_nesting_cap_is_refused_with_its_depth() {
    let _serial = spawn_serial();
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    // 3400 instances -> 10,203 JSON levels, past the cap. The catalog must
    // refuse it by NAME and depth instead of risking its stack.
    let document = synthetic_instance_chain(3400);
    let catalog = catalog_for_document(dir.path(), &document);
    let failure = catalog
        .failure
        .as_ref()
        .unwrap_or_else(|| panic!("a document past the cap must be refused"));
    match failure {
        vd_deeplint::LintFailure::AstJsonNestedTooDeep { measured } => {
            assert_eq!(
                *measured,
                Some(vd_deeplint::AST_JSON_MAX_NESTING + 1),
                "the failure reports the measured lower bound"
            );
        }
        other => panic!("expected AstJsonNestedTooDeep, got {other:?}"),
    }
    let message = catalog
        .failure_message()
        .unwrap_or_else(|| panic!("the failure must have a message"));
    assert!(
        message.contains(&(vd_deeplint::AST_JSON_MAX_NESTING + 1).to_string())
            && message.contains(&vd_deeplint::AST_JSON_MAX_NESTING.to_string()),
        "the message names the measured depth and the cap: {message}"
    );
    assert!(
        !message.contains("invalid") && !message.contains("malformed"),
        "a valid but too-deep document must not be called invalid: {message}"
    );
    assert!(
        !message.contains("diagnostics") && !message.contains("lint"),
        "the nesting limit must not borrow the lint vocabulary: {message}"
    );
}

// --- RM-4: the paths and columns the catalog hands to the editor ------------

fn write_source(path: &Path, text: &str) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .unwrap_or_else(|error| panic!("mkdir {}: {error}", parent.display()));
    }
    fs::write(path, text).unwrap_or_else(|error| panic!("write {}: {error}", path.display()));
}

/// The real-machine shape: the opened folder holds the sources in
/// subdirectories (`rtl/`, `tb/`). `slang` reports `source_file` relative to the
/// directory ITS process ran in (the parent of the first file), so a catalog
/// that hands that relative path to the editor - or resolves it against the
/// folder root - loses the `rtl/` segment. The editor then cannot open the file
/// and EVERY jump is refused (RM-4).
#[test]
fn sources_in_subdirectories_get_the_real_absolute_path() {
    let _serial = spawn_serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-catalog-nested-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = dir.path();
    write_source(
        &root.join("rtl/child.sv"),
        "module child (input logic clk, output logic [3:0] y);\n  assign y = 4'b0;\nendmodule\n",
    );
    write_source(
        &root.join("rtl/top.sv"),
        "module top (input logic clk, output logic [3:0] y);\n  child u_child (.clk(clk), .y(y));\nendmodule\n",
    );
    write_source(
        &root.join("tb/tb.sv"),
        "module tb;\n  logic clk;\n  logic [3:0] y;\n  top u_top (.clk(clk), .y(y));\nendmodule\n",
    );
    // The FIRST file lives in a subdirectory, which is what makes the tool's cwd
    // (and therefore its relative report) differ from the folder root.
    let files = vec![
        root.join("rtl/child.sv"),
        root.join("rtl/top.sv"),
        root.join("tb/tb.sv"),
    ];
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
    let canonical = |path: &Path| {
        fs::canonicalize(path)
            .unwrap_or_else(|error| panic!("canonicalise {}: {error}", path.display()))
            .display()
            .to_string()
    };
    let expect = |relative: &str| {
        fs::canonicalize(root.join(relative))
            .unwrap_or_else(|error| panic!("canonicalise {relative}: {error}"))
            .display()
            .to_string()
    };
    // Every position the editor may jump to must be an ABSOLUTE, existing path.
    for module in &catalog.modules {
        assert!(
            Path::new(&module.file).is_absolute(),
            "{}: definition file is not absolute: {}",
            module.name,
            module.file.display()
        );
        assert!(
            Path::new(&module.file).is_file(),
            "{}: definition file does not exist: {}",
            module.name,
            module.file.display()
        );
        for port in &module.ports {
            assert!(
                Path::new(&port.file).is_file(),
                "{} port {}: file does not exist: {}",
                module.name,
                port.name,
                port.file.display()
            );
        }
        for instance in &module.children {
            assert!(
                Path::new(&instance.file).is_file(),
                "{} -> {}: instantiation file does not exist: {}",
                module.name,
                instance.module,
                instance.file.display()
            );
        }
    }
    // The exact files, so a directory swap cannot pass by accident.
    assert_eq!(
        canonical(&module(&catalog.modules, "child").file),
        expect("rtl/child.sv")
    );
    assert_eq!(
        canonical(&module(&catalog.modules, "top").file),
        expect("rtl/top.sv")
    );
    assert_eq!(
        canonical(&module(&catalog.modules, "tb").file),
        expect("tb/tb.sv")
    );
    let top_children = &module(&catalog.modules, "top").children;
    assert_eq!(top_children[0].module, "child");
    assert_eq!(canonical(&top_children[0].file), expect("rtl/top.sv"));
    let tb_children = &module(&catalog.modules, "tb").children;
    assert_eq!(tb_children[0].module, "top");
    assert_eq!(
        canonical(&tb_children[0].file),
        expect("tb/tb.sv"),
        "a source in ANOTHER subdirectory must resolve to its own directory"
    );
}

/// The reviewer's fixture: a SHORT CJK prefix on a LONG line. slang's column is
/// a UTF-8 byte column (47 for character 43); it stays INSIDE the line, so an
/// overflow heuristic cannot catch it. The catalog must hand the editor's unit
/// (UTF-16 / character), or the jump is delivered four columns past the
/// identifier.
#[test]
fn a_cjk_prefix_does_not_shift_the_reported_column() {
    let _serial = spawn_serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-catalog-cjk-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let root = dir.path();
    // Verbatim from the review fixture: `clk` starts at character 42 and byte 46
    // (0-based); `y` at character 168 / byte 172.
    write_source(
        &root.join("child.sv"),
        "module child (/* 中文 */ input  logic       clk, /* padding padding padding padding padding padding padding padding padding padding padding padding */ output logic [3:0] y);\n  assign y = 4'b0;\nendmodule\n",
    );
    write_source(
        &root.join("top.sv"),
        "module top (input  logic       clk,\n            output logic [3:0] y);\n  child u_child (\n      .clk (clk),\n      .y   (y)\n  );\nendmodule\n",
    );
    let files = vec![root.join("child.sv"), root.join("top.sv")];
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
    let child = module(&catalog.modules, "child");
    let clk = child
        .ports
        .iter()
        .find(|port| port.name == "clk")
        .unwrap_or_else(|| panic!("the clk port is catalogued"));
    assert_eq!(clk.line, 0);
    assert_eq!(
        clk.column, 42,
        "a short CJK prefix must not shift the column: got {} (char) vs 46 (byte)",
        clk.column
    );
    let y = child
        .ports
        .iter()
        .find(|port| port.name == "y")
        .unwrap_or_else(|| panic!("the y port is catalogued"));
    assert_eq!(y.column, 168, "the second port converts too");
}

/// U9d boundary, end to end: a REAL slang design whose instance chain is 42
/// deep. One instance level costs three JSON levels, so this is the SMALLEST
/// design depth that trips serde_json's default 128-value guard — the case that
/// used to produce an empty catalog and that must never regress.
///
/// Ignored by default: needs the generated fixture (see
/// `evidence/ux-batch-2/u9d-depth/README.md`). Run it deliberately with
/// `VEDIT_U9D_BOUNDARY_DIR=...`.
#[test]
#[ignore = "needs VEDIT_U9D_BOUNDARY_DIR (the generated ~42-deep chain project)"]
fn the_boundary_depth_design_below_the_default_guard_is_catalogued() {
    let _serial = spawn_serial();
    let root = std::env::var("VEDIT_U9D_BOUNDARY_DIR")
        .unwrap_or_else(|_| panic!("set VEDIT_U9D_BOUNDARY_DIR to the generated chain project"));
    let files = project_files(Path::new(&root));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            ..CatalogOptions::default()
        },
    );
    println!(
        "u9d-boundary: files={} modules={} tops={} elapsed_ms={} warning={:?} failure={:?}",
        files.len(),
        catalog.modules.len(),
        catalog
            .modules
            .iter()
            .filter(|module| module.is_top)
            .count(),
        catalog.elapsed.as_millis(),
        catalog.warning,
        catalog.failure
    );
    assert!(
        catalog.failure.is_none(),
        "the boundary-depth design must be catalogued: {:?}",
        catalog.failure
    );
    assert_eq!(
        catalog.modules.len(),
        files.len(),
        "every module of the chain must appear"
    );
}

/// RM-17: an explicit top selects the hierarchy; modules outside it are
/// REPORTED (never silently dropped) and the reachable file set is derived from
/// the SAME elaboration — the lint scope and the status dots consume it.
#[test]
fn an_explicit_top_selects_the_hierarchy_and_reports_what_it_leaves_out() {
    let _serial = spawn_serial();
    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            top: Some("top_a".to_string()),
            max_json_bytes: 0,
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "top=top_a must be elaboratable: {:?}",
        catalog.failure
    );
    assert_eq!(
        names(&catalog.modules),
        vec!["consumer", "reflink", "shared", "top_a"],
        "the tree holds the modules under top_a only (the interface is not a module)"
    );
    assert_eq!(
        catalog.outside_modules,
        vec!["orphan".to_string(), "top_b".to_string()],
        "modules outside the top are NAMED, not silently dropped"
    );
    let reachable: Vec<String> = catalog
        .reachable_files
        .iter()
        .map(|path| {
            path.file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned()
        })
        .collect();
    assert_eq!(
        reachable,
        vec![
            "bus.sv",
            "consumer.sv",
            "reflink.sv",
            "shared.sv",
            "top_a.sv"
        ],
        "the reachable set includes the interface file bus.sv (an elaborated body)"
    );
    let warning = catalog.warning.unwrap_or_default();
    assert!(
        warning.contains("2 module(s) outside the top top_a"),
        "the tree line names the top and the count: {warning}"
    );
}

#[test]
fn auto_still_catalogues_every_root() {
    let _serial = spawn_serial();
    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            max_json_bytes: 0,
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    assert!(catalog.failure.is_none(), "{:?}", catalog.failure);
    assert_eq!(
        names(&catalog.modules),
        vec!["consumer", "orphan", "reflink", "shared", "top_a", "top_b"],
        "Auto keeps today's all-tops behaviour"
    );
    assert!(
        catalog.outside_modules.is_empty(),
        "nothing is outside in Auto"
    );
    assert!(catalog.top.is_none());
}

#[test]
fn an_unknown_top_is_a_named_error_not_an_empty_tree() {
    let _serial = spawn_serial();
    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            top: Some("no_such_module".to_string()),
            max_json_bytes: 0,
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    match &catalog.failure {
        Some(vd_deeplint::LintFailure::UnknownTop { name }) => {
            assert_eq!(name, "no_such_module");
        }
        other => panic!("expected UnknownTop, got {other:?}"),
    }
    let message = catalog
        .failure_message()
        .unwrap_or_else(|| panic!("the failure must have a message"));
    assert!(
        message.contains("'no_such_module'") && message.contains("not a valid top-level module"),
        "the error names the module and the reason: {message}"
    );
    assert!(
        !message.contains("invalid") && !message.contains("malformed"),
        "the design is not invalid, the NAME is unknown: {message}"
    );
}

/// RM-17 measurement, real scale: the same 1176-file project catalogued with
/// Auto and with a named TOP. Prints the file counts and wall times; the lint
/// scope is `reachable`, derived from THIS run.
///
/// Ignored by default: needs the generated fixture (see
/// `evidence/ux-batch-2/rm17-top/README.md`).
#[test]
#[ignore = "needs VEDIT_U9A_BIGDIR + VEDIT_U9A_TOP"]
fn the_top_selects_a_segment_of_a_real_scale_design() {
    let _serial = spawn_serial();
    let root = std::env::var("VEDIT_U9A_BIGDIR")
        .unwrap_or_else(|_| panic!("set VEDIT_U9A_BIGDIR to the generated project"));
    let top = std::env::var("VEDIT_U9A_TOP")
        .unwrap_or_else(|_| panic!("set VEDIT_U9A_TOP to a top module of that project"));
    let files = project_files(Path::new(&root));
    let started = std::time::Instant::now();
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            top: Some(top.clone()),
            max_json_bytes: 0,
            max_files: usize::MAX,
            ..CatalogOptions::default()
        },
    );
    let wall = started.elapsed();
    println!(
        "u9a-top: files={} top={} modules={} reachable_files={} outside_modules={} \
         elapsed_ms={} wall_ms={} peak_rss_kb={} failure={:?}",
        files.len(),
        top,
        catalog.modules.len(),
        catalog.reachable_files.len(),
        catalog.outside_modules.len(),
        catalog.elapsed.as_millis(),
        wall.as_millis(),
        catalog.peak_rss_kb,
        catalog.failure
    );
    assert!(
        catalog.failure.is_none(),
        "the named top must be elaboratable: {:?}",
        catalog.failure
    );
    assert!(
        catalog.modules.len() < files.len(),
        "a top must select a SUBSET of the project"
    );
    assert!(
        !catalog.outside_modules.is_empty(),
        "the modules outside the top are reported"
    );
}
