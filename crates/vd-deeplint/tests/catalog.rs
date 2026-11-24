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
    build_catalog, parse_catalog_json, parse_catalog_json_in, parse_catalog_json_with_files,
    CatalogModule, CatalogOptions, PortDirection,
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

/// Catalogue a synthetic AST through the REAL product path (`build_catalog`), so
/// the streaming prune and the big-stack walk thread are the ones under test.
///
/// G1-b: the file set is a caller fact and is never inferred from the document,
/// so the set has to name the file the document puts its instances in. The
/// synthetic chain spells every position as `top.sv`; passing an unrelated set
/// (as this helper used to) would now exclude every definition and make the
/// depth assertions below vacuous.
fn catalog_for_document(dir: &Path, document: &str) -> vd_deeplint::ModuleCatalog {
    let path = dir.join("synthetic.json");
    fs::write(&path, document).unwrap_or_else(|error| panic!("writing the synthetic AST: {error}"));
    let fake = fake_slang_copying(dir, &path);
    let top = dir.join("top.sv");
    fs::write(&top, "module top; endmodule\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", top.display()));
    build_catalog(
        std::slice::from_ref(&top),
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

// ---------------------------------------------------------------------------
// batch-20261118 B: a duplicate module name is resolved DETERMINISTICALLY and
// reported, never silently. The fixture is the real `slang 12.1.0` document of
// two files that both declare `module uart` (see
// `tests/fixtures/duplicate-module/README.md`): `definitions` carries both
// declarations with their own `source_file`, while the design tree (and the
// only `InstanceBody`) belongs to `sim/uart.v`, the file slang processed LAST.
// ---------------------------------------------------------------------------

fn duplicate_module_ast() -> String {
    fs::read_to_string(fixture_dir().join("duplicate-module/ast.json"))
        .unwrap_or_else(|error| panic!("reading the captured duplicate AST: {error}"))
}

fn file_set(order: [&str; 2]) -> Vec<PathBuf> {
    order.into_iter().map(PathBuf::from).collect()
}

#[test]
fn a_duplicate_module_is_adopted_from_the_first_file_of_the_file_set() {
    let parsed = parse_catalog_json_with_files(
        &duplicate_module_ast(),
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));

    // B-1: the adopted definition is the FIRST file of the file set — not the
    // body the elaboration happened to keep (`sim/uart.v`).
    assert_eq!(parsed.modules.len(), 1, "one catalog entry per module name");
    let uart = &parsed.modules[0];
    assert_eq!(uart.name, "uart");
    assert!(
        uart.file.ends_with("rtl/uart.v"),
        "the first file in the set wins: {}",
        uart.file.display()
    );
    // The winning declaration's BODY is not in the document (slang serialised
    // the losing one), so no port of `sim/uart.v` may be shown under the winner's
    // name: empty ports are the honest answer.
    assert!(
        uart.ports.is_empty(),
        "the losing file's ports must not be attributed to the winner: {:?}",
        uart.ports
    );

    // B-2: the FIRST warning names BOTH files and the adopted declaration, and
    // its sentence stays byte-identical. F4: a SECOND warning says WHY the
    // adopted module has no ports (the elaboration kept only the losing body).
    assert_eq!(parsed.duplicate_modules, vec!["uart".to_string()]);
    assert_eq!(
        parsed.warnings[0],
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
    );
    assert_eq!(
        parsed.warnings[1],
        "ports of module 'uart' are unavailable: the adopted definition in rtl/uart.v \
         (line 1) has no body in the elaborated AST"
    );
    assert_eq!(parsed.warnings.len(), 2, "{:?}", parsed.warnings);
}

#[test]
fn the_file_set_order_decides_not_the_map_iteration_order() {
    // The exact same document, the opposite file-set order: the OTHER
    // declaration must be adopted. `Bodies::by_address` is a HashMap, so any
    // answer that ignores the file set would flip with the hash seed instead.
    let reversed = parse_catalog_json_with_files(
        &duplicate_module_ast(),
        &file_set(["sim/uart.v", "rtl/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));

    let uart = &reversed.modules[0];
    assert!(
        uart.file.ends_with("sim/uart.v"),
        "the first file of THIS set wins: {}",
        uart.file.display()
    );
    // Here the adopted declaration's body IS the elaborated one, so its ports
    // are catalogued (and they are `sim/uart.v`'s, never `rtl`'s).
    let ports: Vec<(&str, PortDirection)> = uart
        .ports
        .iter()
        .map(|port| (port.name.as_str(), port.direction))
        .collect();
    assert_eq!(
        ports,
        vec![("clk", PortDirection::In), ("done", PortDirection::Out)]
    );
    assert_eq!(
        reversed.warnings,
        vec![
            "module 'uart' is defined in both rtl/uart.v and sim/uart.v; using sim/uart.v"
                .to_string()
        ]
    );
}

#[test]
fn the_duplicate_resolution_is_reproducible() {
    // B-3: the same document and the same file-set order give byte-identical
    // modules and warning text on every run.
    let text = duplicate_module_ast();
    let files = file_set(["rtl/uart.v", "sim/uart.v"]);
    let first = parse_catalog_json_with_files(&text, &files)
        .unwrap_or_else(|error| panic!("parse: {error}"));
    for run in 0..4 {
        let again = parse_catalog_json_with_files(&text, &files)
            .unwrap_or_else(|error| panic!("parse (run {run}): {error}"));
        assert_eq!(
            again.modules, first.modules,
            "run {run} changed the modules"
        );
        assert_eq!(
            again.warnings, first.warnings,
            "run {run} changed the warning"
        );
    }
    // A caller with NO file set still gets a deterministic answer (the document
    // spelling orders it), and the same warning shape.
    let without_files = parse_catalog_json_with_files(&text, &[])
        .unwrap_or_else(|error| panic!("parse without a file set: {error}"));
    let repeated = parse_catalog_json_with_files(&text, &[])
        .unwrap_or_else(|error| panic!("parse without a file set: {error}"));
    assert_eq!(without_files.modules, repeated.modules);
    assert_eq!(without_files.warnings, repeated.warnings);
    assert!(
        without_files.warnings[0].contains("; using rtl/uart.v"),
        "the document's own spelling decides when no file set is given: {:?}",
        without_files.warnings
    );
}

/// Two declarations of one name inside ONE file: the contract's same-file rule
/// is `(source_line, source_column)` ascending. The body belongs to the SECOND
/// declaration, so the first one must win without borrowing its ports.
const SAME_FILE_DUPLICATE: &str = r#"{
  "design": {
    "name": "$root",
    "kind": "Root",
    "addr": 1,
    "members": [
      {
        "name": "uart",
        "kind": "Instance",
        "addr": 2,
        "body": {
          "name": "uart",
          "kind": "InstanceBody",
          "source_file": "rtl/uart.v",
          "source_line": 9,
          "source_column": 8,
          "addr": 3,
          "definition": "11 uart",
          "members": [
            {
              "name": "done",
              "kind": "Port",
              "source_file": "rtl/uart.v",
              "source_line": 9,
              "source_column": 20,
              "addr": 4,
              "type": "logic",
              "direction": "Out"
            }
          ]
        }
      }
    ]
  },
  "definitions": [
    {
      "name": "uart",
      "kind": "Definition",
      "source_file": "rtl/uart.v",
      "source_line": 1,
      "source_column": 8,
      "addr": 10,
      "definitionKind": "Module"
    },
    {
      "name": "uart",
      "kind": "Definition",
      "source_file": "rtl/uart.v",
      "source_line": 9,
      "source_column": 8,
      "addr": 11,
      "definitionKind": "Module"
    }
  ]
}"#;

#[test]
fn two_definitions_in_one_file_are_ordered_by_position() {
    let parsed = parse_catalog_json_with_files(SAME_FILE_DUPLICATE, &[PathBuf::from("rtl/uart.v")])
        .unwrap_or_else(|error| panic!("parse: {error}"));
    let uart = &parsed.modules[0];
    assert_eq!(
        (uart.line, uart.column),
        (0, 7),
        "the 0-based position of the FIRST declaration (1:8 in the file)"
    );
    assert!(
        uart.ports.is_empty(),
        "the second declaration's ports must not be used: {:?}",
        uart.ports
    );
    // R1-a (contract revision 2026-11-19): both declarations are in ONE file,
    // so the collision no longer produces a duplicate sentence at all — the old
    // `defined twice in uart.v (lines 1 and 9)` wording is retired by the same
    // rule that keeps a 96-declaration file out of the module-tree summary.
    assert_eq!(
        parsed.duplicate_modules,
        vec!["uart".to_string()],
        "the collision is still recorded, so the tool's per-name duplicate line stays out"
    );
    assert_eq!(
        parsed.warnings,
        vec![
            // F4: the winner is the FIRST declaration and has no body in the
            // document, so the remaining warning says the port data is unavailable.
            "ports of module 'uart' are unavailable: the adopted definition in uart.v (line 1) \
             has no body in the elaborated AST"
                .to_string(),
        ]
    );
}

/// Review F1: two bodies of one name with NEITHER a `definition` reference NOR
/// any source position. Every earlier component of the preference key is
/// identical, so only the body's own AST address can decide; without it the
/// stable sort kept `Bodies::by_address`'s `HashMap` iteration order and the
/// probe saw two different port lists (500 runs).
const BODIES_WITHOUT_DECLARATION: &str = r#"{
  "design": {"name": "$root", "kind": "Root", "addr": 1, "members": [
    {"name": "uart", "kind": "Instance", "addr": 2,
     "body": {"name": "uart", "kind": "InstanceBody", "addr": 3,
              "members": [{"name": "din", "kind": "Port", "addr": 4, "direction": "In", "type": "logic"}]}},
    {"name": "uart", "kind": "Instance", "addr": 5,
     "body": {"name": "uart", "kind": "InstanceBody", "addr": 6,
              "members": [{"name": "dout", "kind": "Port", "addr": 7, "direction": "Out", "type": "logic"}]}}
  ]}
}"#;

#[test]
fn bodies_without_a_declaration_are_totally_ordered_by_address_and_reported() {
    let mut expected: Option<(Vec<CatalogModule>, Vec<String>)> = None;
    for run in 0..500 {
        let parsed = parse_catalog_json_with_files(BODIES_WITHOUT_DECLARATION, &[])
            .unwrap_or_else(|error| panic!("parse (run {run}): {error}"));
        assert_eq!(parsed.modules.len(), 1, "run {run}");
        let ports: Vec<&str> = parsed.modules[0]
            .ports
            .iter()
            .map(|port| port.name.as_str())
            .collect();
        assert_eq!(
            ports,
            vec!["din"],
            "the LOWEST AST address must win on every run (run {run})"
        );
        assert!(
            !parsed.warnings.is_empty(),
            "a same-name body multiplicity must never be silent (run {run})"
        );
        match &expected {
            None => {
                // No position is available, so the warning names the AST
                // addresses instead of the unusable `<unknown>`.
                assert_eq!(
                    parsed.warnings[0],
                    "module 'uart' has 2 definitions with no source position; using the \
                     definition at AST address 3 (also at AST address 6)"
                );
                expected = Some((parsed.modules, parsed.warnings));
            }
            Some((modules, warnings)) => {
                assert_eq!(&parsed.modules, modules, "run {run} changed the choice");
                assert_eq!(&parsed.warnings, warnings, "run {run} changed the warning");
            }
        }
    }
    assert!(
        expected.is_some(),
        "the degeneracy probe must have observed at least one run"
    );
}

/// Review F1: the same-name multiplicity is reported even when the document has
/// no `definitions` entries at all, and the bodies' own positions order them.
const POSITIONED_BODIES_WITHOUT_DECLARATION: &str = r#"{
  "design": {"name": "$root", "kind": "Root", "addr": 1, "members": [
    {"name": "uart", "kind": "Instance", "addr": 2,
     "body": {"name": "uart", "kind": "InstanceBody", "addr": 3,
              "source_file": "rtl/uart.v", "source_line": 20, "source_column": 1,
              "members": [{"name": "dout", "kind": "Port", "addr": 4, "direction": "Out", "type": "logic"}]}},
    {"name": "uart", "kind": "Instance", "addr": 5,
     "body": {"name": "uart", "kind": "InstanceBody", "addr": 6,
              "source_file": "rtl/uart.v", "source_line": 10, "source_column": 1,
              "members": [{"name": "din", "kind": "Port", "addr": 7, "direction": "In", "type": "logic"}]}}
  ]}
}"#;

#[test]
fn positioned_bodies_without_a_declaration_in_one_file_are_silent() {
    // Contract revision R1-a: two BODIES in one file are the same
    // ambiguity-free case as two DECLARATIONS in one file, so the old
    // `defined twice in rtl/uart.v (lines 10 and 20)` sentence is retired.
    let parsed = parse_catalog_json_with_files(POSITIONED_BODIES_WITHOUT_DECLARATION, &[])
        .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(parsed.modules.len(), 1);
    let ports: Vec<&str> = parsed.modules[0]
        .ports
        .iter()
        .map(|port| port.name.as_str())
        .collect();
    assert_eq!(ports, vec!["din"], "the lower source line wins");
    assert_eq!(
        parsed.duplicate_modules,
        vec!["uart".to_string()],
        "the collision is still recorded for B-5 coverage"
    );
    assert!(
        parsed.warnings.is_empty(),
        "R1-a: the bodies' own positions are in ONE file, so nothing is reported: {:?}",
        parsed.warnings
    );
}

// ---------------------------------------------------------------------------
// batch-20261119 R1: duplicate-module warnings are aggregated by the path the
// panel shows. One file that declares a name many times is silent; several
// files list each file once and stop after three (`+N more`).
// ---------------------------------------------------------------------------

/// A minimal catalog document: one module name declared at every `(file, line)`
/// in `declarations`, plus ONE elaborated body for the FIRST declaration (so no
/// F4 "ports unavailable" note mixes into the warnings under test).
///
/// `declarations` must be in preference order: the body's `definition` reference
/// points at the first entry's AST address (`100 + index`).
fn duplicate_sites_document(name: &str, declarations: &[(String, u32)]) -> String {
    let definitions: Vec<String> = declarations
        .iter()
        .enumerate()
        .map(|(index, (file, line))| {
            let addr = 100 + u64::try_from(index)
                .unwrap_or_else(|error| panic!("definition index: {error}"));
            // `kind` FIRST: the streaming prune refuses a retained node whose
            // position fields (`addr`, `source_*`) precede its `kind`.
            format!(
                r#"{{"kind":"Definition","name":"{name}","source_file":"{file}","source_line":{line},"source_column":8,"addr":{addr},"definitionKind":"Module"}}"#
            )
        })
        .collect();
    let definitions = definitions.join(",");
    let (adopted_file, adopted_line) = &declarations[0];
    format!(
        r#"{{"design":{{"kind":"Root","name":"$root","addr":1,"members":[{{"kind":"Instance","name":"{name}","addr":2,"body":{{"kind":"InstanceBody","name":"{name}","source_file":"{adopted_file}","source_line":{adopted_line},"source_column":8,"addr":3,"definition":"100 {name}","members":[{{"kind":"Port","name":"clk","source_file":"{adopted_file}","source_line":{adopted_line},"source_column":20,"addr":4,"type":"logic","direction":"In"}}]}}}}]}},"definitions":[{definitions}]}}"#
    )
}

#[test]
fn ninety_six_same_file_declarations_produce_no_duplicate_warning() {
    // R1-a: the user's real panel showed one `defined 96 times (/abs/…,
    // /abs/…, …)` row carrying 96 identical paths. The same file is ONE source
    // of truth, so the whole duplicate sentence — including the old
    // `defined twice in … (lines A and B)` form — disappears.
    let declarations: Vec<(String, u32)> = (0..96)
        .map(|index| ("stdcell.v".to_string(), 1 + index as u32 * 10))
        .collect();
    let parsed =
        parse_catalog_json_with_files(&duplicate_sites_document("ANDX2", &declarations), &[])
            .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(parsed.modules.len(), 1);
    assert_eq!(
        parsed.duplicate_modules,
        vec!["ANDX2".to_string()],
        "the collision stays recorded, so the tool's per-name duplicate line is not quoted"
    );
    assert!(
        parsed.warnings.is_empty(),
        "R1-a: 96 declarations in ONE file are not an ambiguity to report: {:?}",
        parsed.warnings
    );
}

#[test]
fn three_declarations_in_one_file_plus_one_elsewhere_name_the_file_once() {
    // R1-b: the file that declares `uart` three times is listed ONCE, the
    // sentence keeps the B-2 shape, and only one warning is produced.
    let declarations = vec![
        ("rtl/uart.v".to_string(), 1),
        ("sim/uart.v".to_string(), 1),
        ("sim/uart.v".to_string(), 10),
        ("sim/uart.v".to_string(), 20),
    ];
    let parsed = parse_catalog_json_with_files(
        &duplicate_sites_document("uart", &declarations),
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        parsed.warnings,
        vec![
            "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
                .to_string()
        ],
        "three declarations in sim/uart.v must not repeat that path"
    );
    assert_eq!(
        parsed.warnings[0].matches("sim/uart.v").count(),
        1,
        "the repeated file is named exactly once: {}",
        parsed.warnings[0]
    );
    assert!(
        parsed.warnings[0].contains("; using rtl/uart.v"),
        "the adopted declaration is still named: {}",
        parsed.warnings[0]
    );
}

#[test]
fn two_distinct_files_keep_the_b2_sentence_byte_identical() {
    // R1-b: the aggregation must not reword the 2-file sentence that contract
    // B-2 and `a_duplicate_module_is_adopted_from_the_first_file_of_the_file_set`
    // depend on.
    let parsed = parse_catalog_json_with_files(
        &duplicate_module_ast(),
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        parsed.warnings[0],
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
    );
}

#[test]
fn one_hundred_distinct_files_are_folded_to_three_plus_more() {
    // R1-b: the list is bounded by DISTINCT FILES, not by the declaration
    // count: three files, then `+N more`.
    let declarations: Vec<(String, u32)> = (0..100)
        .map(|index| (format!("rtl/block_{index:03}.v"), 1))
        .collect();
    let parsed =
        parse_catalog_json_with_files(&duplicate_sites_document("ANDX2", &declarations), &[])
            .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(parsed.warnings.len(), 1);
    let warning = &parsed.warnings[0];
    assert!(
        warning.contains("(rtl/block_001.v, rtl/block_002.v, rtl/block_003.v, +97 more)"),
        "three files are listed and the other 97 are folded: {warning}"
    );
    assert!(
        warning.ends_with("; using rtl/block_000.v"),
        "the adopted file closes the sentence: {warning}"
    );
    assert!(
        warning.len() < 300,
        "the sentence is bounded by the file cap: {} chars: {warning}",
        warning.len()
    );
    assert_eq!(
        warning.matches(".v").count(),
        4,
        "three listed files plus the adopted one, never 100: {warning}"
    );
}

#[test]
fn a_same_file_duplicate_does_not_reach_the_summary_even_when_slang_reports_it() {
    // R1-a end to end: slang exits 1 and prints `duplicate definition` for a
    // module its own file declares 96 times. The catalog must produce NO
    // duplicate warning at all — neither the D2 sentence nor the tool's line
    // quoted back in its place (B-5 only covers collisions the document does
    // NOT show).
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let declarations: Vec<(String, u32)> = (0..96)
        .map(|index| ("stdcell.v".to_string(), 1 + index as u32 * 10))
        .collect();
    let document_path = project.path().join("duplicate-ast.json");
    fs::write(
        &document_path,
        duplicate_sites_document("ANDX2", &declarations),
    )
    .unwrap_or_else(|error| panic!("writing the document: {error}"));
    fs::write(
        project.path().join("stdcell.v"),
        "module ANDX2 (input clk);\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("writing the source fixture: {error}"));
    let stderr = "stdcell.v:1:8: error: duplicate definition of 'ANDX2' [-Wduplicate-definition]";
    let fake = fake_slang_reporting(project.path(), &document_path, stderr, 1);
    let catalog = build_catalog(
        &[project.path().join("stdcell.v")],
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "a non-zero exit with a usable document is a normal result: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 1, "one entry per module name");
    assert_eq!(
        catalog.warning, None,
        "R1-a: neither a duplicate sentence nor the tool's own line may reach the summary"
    );
}

/// A fake `slang` that writes `document` to the `--ast-json` path, prints
/// `stderr` and exits with `exit_code` — the "non-zero exit WITH a usable
/// document" shape `slang 12.1.0` produces on a duplicate definition (B-5).
fn fake_slang_reporting(dir: &Path, document: &Path, stderr: &str, exit_code: i32) -> PathBuf {
    let fake = dir.join("fake-slang-duplicate");
    let script = format!(
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--ast-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncp '{}' \"$out\"\ncat >&2 <<'VD_STDERR_EOF'\n{}\nVD_STDERR_EOF\nexit {}\n",
        document.display(),
        stderr,
        exit_code
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
fn a_non_zero_exit_with_usable_json_keeps_the_duplicate_warning() {
    // B-5: slang exits 1 on a duplicate definition and still writes the whole
    // AST JSON. The crate rule is that this is a NORMAL result — and the
    // duplicate must survive it as the B-2 warning, not vanish with the exit
    // code or a log line.
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let rtl = project.path().join("rtl");
    let sim = project.path().join("sim");
    for directory in [&rtl, &sim] {
        fs::create_dir_all(directory)
            .unwrap_or_else(|error| panic!("creating {}: {error}", directory.display()));
    }
    fs::write(rtl.join("uart.v"), "module uart (input clk);\nendmodule\n")
        .unwrap_or_else(|error| panic!("writing the fixture: {error}"));
    fs::write(
        sim.join("uart.v"),
        "module uart (output done);\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("writing the fixture: {error}"));
    // The captured document is written with ABSOLUTE paths: `build_catalog` runs
    // the tool with `cwd = project_root(files)` and absolutises the document
    // against it, so a document captured in another tree would be rewritten to
    // the wrong files.
    let document = duplicate_module_ast()
        .replace("rtl/uart.v", &rtl.join("uart.v").display().to_string())
        .replace("sim/uart.v", &sim.join("uart.v").display().to_string());
    let document_path = project.path().join("duplicate-ast.json");
    fs::write(&document_path, document)
        .unwrap_or_else(|error| panic!("writing the document: {error}"));
    let stderr = "sim/uart.v:1:8: error: duplicate definition of 'uart' [-Wduplicate-definition]\n\
                  rtl/uart.v:1:8: note: previous definition here";
    let fake = fake_slang_reporting(project.path(), &document_path, stderr, 1);
    let files = vec![rtl.join("uart.v"), sim.join("uart.v")];
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "a non-zero exit with a usable document is a normal catalog result: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 1);
    assert!(catalog.modules[0].file.ends_with("rtl/uart.v"));
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the duplicate must be reported"));
    assert_eq!(
        warning,
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v; \
         ports of module 'uart' are unavailable: the adopted definition in rtl/uart.v \
         (line 1) has no body in the elaborated AST",
        "the B-2 sentence must be the warning the UI receives FIRST and byte-identical; \
         F4 appends the reason the winner has no ports"
    );
}

#[test]
fn the_product_path_publishes_the_shadowed_file_with_its_sentence() {
    // G1-c end to end: `build_catalog` is what the C ABI and the module tree
    // consume, so the list and its sentence must survive the streaming walk, not
    // only the whole-string parse.
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let rtl = project.path().join("rtl");
    let sim = project.path().join("sim");
    for directory in [&rtl, &sim] {
        fs::create_dir_all(directory)
            .unwrap_or_else(|error| panic!("creating {}: {error}", directory.display()));
    }
    fs::write(rtl.join("uart.v"), "module uart (input clk);\nendmodule\n")
        .unwrap_or_else(|error| panic!("writing the fixture: {error}"));
    fs::write(
        sim.join("uart.v"),
        "module uart (output done);\nendmodule\n",
    )
    .unwrap_or_else(|error| panic!("writing the fixture: {error}"));
    let document = duplicate_module_ast()
        .replace("rtl/uart.v", &rtl.join("uart.v").display().to_string())
        .replace("sim/uart.v", &sim.join("uart.v").display().to_string());
    let document_path = project.path().join("duplicate-ast.json");
    fs::write(&document_path, document)
        .unwrap_or_else(|error| panic!("writing the document: {error}"));
    let stderr = "sim/uart.v:1:8: error: duplicate definition of 'uart' [-Wduplicate-definition]\n\
                  rtl/uart.v:1:8: note: previous definition here";
    let fake = fake_slang_reporting(project.path(), &document_path, stderr, 1);
    let files = vec![rtl.join("uart.v"), sim.join("uart.v")];
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "the catalog must succeed: {:?}",
        catalog.failure
    );
    assert_eq!(
        catalog.shadowed_files,
        vec![sim.join("uart.v")],
        "the losing file is published with the path the caller knows"
    );
    assert_eq!(
        catalog.shadowed_warning.as_deref(),
        Some(
            "sim/uart.v is excluded from the compiler input because every module it defines \
             lost the duplicate pick"
        ),
        "the sentence names the file relative to the project root, like the duplicate one"
    );
    // The frozen B-2/F4 warning is untouched: the new sentence is its own field.
    assert_eq!(
        catalog.warning.as_deref(),
        Some(
            "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v; \
             ports of module 'uart' are unavailable: the adopted definition in rtl/uart.v \
             (line 1) has no body in the elaborated AST"
        )
    );
}

#[test]
fn a_duplicate_the_document_does_not_show_is_still_reported() {
    // B-5's other half: slang reported a duplicate that the AST JSON does not
    // show as two declarations (here: the captured single-definition catalog
    // document). Its sentence must be quoted, never dropped.
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let captured = catalog_fixture().join("ast.json");
    let stderr = "sim/uart.v:1:8: error: duplicate definition of 'uart' [-Wduplicate-definition]";
    let fake = fake_slang_reporting(project.path(), &captured, stderr, 1);
    let files = project_files(&catalog_fixture().join("project"));
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "a non-zero exit with a usable document is a normal catalog result: {:?}",
        catalog.failure
    );
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the tool's duplicate diagnostic must survive"));
    assert!(
        warning.contains(
            "slang reported: sim/uart.v:1:8: error: duplicate definition of 'uart' \
             [-Wduplicate-definition]"
        ),
        "the verbatim diagnostic must be quoted: {warning}"
    );
    assert!(
        !warning.contains("is defined in both"),
        "the document has no duplicate declaration, so no D2 sentence may be invented: {warning}"
    );
}

#[test]
fn the_real_slang_binary_resolves_a_duplicate_module_deterministically() {
    // B-4 against the REAL tool: both files are fed to slang, which exits 1
    // (`duplicate definition`) and still writes a parseable document. The
    // catalog must adopt the first file of the file set and warn with both.
    let _serial = spawn_serial();
    let project = fixture_dir().join("duplicate-module/project");
    let files = vec![project.join("rtl/uart.v"), project.join("sim/uart.v")];
    let catalog = build_catalog(
        &files,
        &CatalogOptions {
            slang_binary: Some(slang_binary()),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "slang exits 1 and still writes the AST JSON: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 1, "one entry per module name");
    let uart = &catalog.modules[0];
    assert!(
        uart.file.ends_with("rtl/uart.v"),
        "the first file of the file set wins: {}",
        uart.file.display()
    );
    let warning = catalog.warning.as_deref().unwrap_or_default();
    assert!(
        warning.contains(
            "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
        ),
        "the real run must produce the B-2 sentence: {warning:?}"
    );
}

// ---------------------------------------------------------------------------
// G1-b (batch-20261119): `slang` parses an `include`d file as part of the
// compilation unit, so the AST carries definitions whose `source_file` is NOT
// one of the files the caller asked for. Such a declaration must never become
// the module's definition location; it is excluded from the catalog and named
// in one bounded summary warning.
// ---------------------------------------------------------------------------

/// One module name declared in BOTH an in-set file (`rtl/uart.v`) and an
/// outside one (`inc/defs.v`, the `include`), with the only elaborated BODY in
/// the outside file — the shape `slang` produces when the include is processed
/// last. The in-set declaration must win without borrowing that body's ports.
const DECLARED_INSIDE_AND_OUTSIDE: &str = r#"{
  "design": {"name": "$root", "kind": "Root", "addr": 1, "members": [
    {"name": "uart", "kind": "Instance", "addr": 2,
     "body": {"name": "uart", "kind": "InstanceBody",
              "source_file": "inc/defs.v", "source_line": 1, "source_column": 8,
              "addr": 3, "definition": "101 uart",
              "members": [
                {"name": "clk", "kind": "Port",
                 "source_file": "inc/defs.v", "source_line": 1,
                 "source_column": 20, "addr": 4,
                 "type": "logic", "direction": "In"}
              ]}}
  ]},
  "definitions": [
    {"kind": "Definition", "name": "uart", "definitionKind": "Module",
     "source_file": "rtl/uart.v", "source_line": 1, "source_column": 8, "addr": 100},
    {"kind": "Definition", "name": "uart", "definitionKind": "Module",
     "source_file": "inc/defs.v", "source_line": 1, "source_column": 8, "addr": 101}
  ]
}"#;

#[test]
fn an_included_definition_never_becomes_the_module_location() {
    let parsed = parse_catalog_json_with_files(
        DECLARED_INSIDE_AND_OUTSIDE,
        &file_set(["rtl/uart.v", "sim/top.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));

    // G1-b: the module is located at the IN-SET declaration, never in the
    // include file the elaborated body came from.
    assert_eq!(parsed.modules.len(), 1, "one catalog entry per module name");
    let uart = &parsed.modules[0];
    assert!(
        uart.file.ends_with("rtl/uart.v"),
        "the in-set declaration must win: {}",
        uart.file.display()
    );
    assert_eq!(
        (uart.line, uart.column),
        (0, 7),
        "the adopted position is the in-set declaration's own (1:8)"
    );
    assert!(
        uart.ports.is_empty(),
        "the outside body's ports must not be attributed to the winner: {:?}",
        uart.ports
    );

    // The outside declaration still counts in the SAME duplicate warning, and
    // the reason the winner has no ports is stated (review F4).
    assert_eq!(
        parsed.warnings,
        vec![
            "module 'uart' is defined in both inc/defs.v and rtl/uart.v; using rtl/uart.v"
                .to_string(),
            "ports of module 'uart' are unavailable: the adopted definition in rtl/uart.v \
             (line 1) has no body in the elaborated AST"
                .to_string(),
        ],
        "the outside declaration is named by the duplicate sentence, not silently dropped"
    );
    assert!(
        !parsed.warnings[0].contains("outside the file list"),
        "a module that IS declared inside the file set is not excluded: {:?}",
        parsed.warnings
    );
}

/// `uart` is declared and elaborated ONLY in `inc/defs.v` (outside the file
/// set); `top` lives entirely in `rtl/top.v` (inside it), so the catalog is not
/// simply emptied by the exclusion.
const DECLARED_ONLY_OUTSIDE: &str = r#"{
  "design": {"name": "$root", "kind": "Root", "addr": 1, "members": [
    {"name": "uart", "kind": "Instance", "addr": 2,
     "body": {"name": "uart", "kind": "InstanceBody",
              "source_file": "inc/defs.v", "source_line": 1, "source_column": 8,
              "addr": 3, "definition": "101 uart",
              "members": [
                {"name": "clk", "kind": "Port",
                 "source_file": "inc/defs.v", "source_line": 1,
                 "source_column": 20, "addr": 4,
                 "type": "logic", "direction": "In"}
              ]}},
    {"name": "top", "kind": "Instance", "addr": 10,
     "body": {"name": "top", "kind": "InstanceBody",
              "source_file": "rtl/top.v", "source_line": 1, "source_column": 8,
              "addr": 11, "definition": "110 top", "members": []}}
  ]},
  "definitions": [
    {"kind": "Definition", "name": "uart", "definitionKind": "Module",
     "source_file": "inc/defs.v", "source_line": 1, "source_column": 8, "addr": 101},
    {"kind": "Definition", "name": "top", "definitionKind": "Module",
     "source_file": "rtl/top.v", "source_line": 1, "source_column": 8, "addr": 110}
  ]
}"#;

#[test]
fn a_module_defined_only_outside_the_file_list_is_excluded_and_summarised() {
    let parsed =
        parse_catalog_json_with_files(DECLARED_ONLY_OUTSIDE, &[PathBuf::from("rtl/top.v")])
            .unwrap_or_else(|error| panic!("parse: {error}"));

    assert_eq!(
        names(&parsed.modules),
        vec!["top"],
        "uart is defined only in inc/defs.v, so it is not this project's module"
    );
    assert_eq!(
        parsed.warnings,
        vec![
            "1 module(s) are defined only in files outside the file list (e.g. 'uart' in \
             inc/defs.v); they are excluded"
                .to_string(),
        ],
        "the exclusion is reported once, with the outside file named"
    );
    assert!(
        !parsed.duplicate_modules.iter().any(|name| name == "uart"),
        "an excluded module is not a duplicate-module choice: {:?}",
        parsed.duplicate_modules
    );
}

#[test]
fn without_a_file_set_every_definition_is_still_collected() {
    // G1-b must not change `parse_catalog_json`: with no file set the caller
    // cannot decide membership, so the old collect-everything behaviour stays.
    let parsed = parse_catalog_json_with_files(DECLARED_ONLY_OUTSIDE, &[])
        .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        names(&parsed.modules),
        vec!["top", "uart"],
        "no file set means no exclusion"
    );
    assert_eq!(
        module(&parsed.modules, "uart").file,
        PathBuf::from("inc/defs.v"),
        "the document's own spelling is still what is catalogued"
    );
    assert!(
        parsed.warnings.is_empty(),
        "no file set, so no exclusion warning: {:?}",
        parsed.warnings
    );

    let via_public = parse_catalog_json(DECLARED_ONLY_OUTSIDE)
        .unwrap_or_else(|error| panic!("parse_catalog_json: {error}"));
    assert_eq!(via_public, parsed.modules);
}

/// A catalog document for `build_catalog`'s end-to-end path: `top` is declared
/// and elaborated in `inside` (the file set), `uart` only in `outside` (the
/// `include`d file). Paths are ABSOLUTE because `build_catalog` absolutises the
/// document against the project root.
fn outside_file_set_document(inside: &Path, outside: &Path) -> String {
    format!(
        r#"{{"design":{{"name":"$root","kind":"Root","addr":1,"members":[
  {{"name":"top","kind":"Instance","addr":2,"body":{{"name":"top","kind":"InstanceBody","source_file":"{inside}","source_line":1,"source_column":8,"addr":3,"definition":"110 top","members":[]}}}}
]}},"definitions":[
  {{"kind":"Definition","name":"uart","definitionKind":"Module","source_file":"{outside}","source_line":1,"source_column":8,"addr":101}},
  {{"kind":"Definition","name":"top","definitionKind":"Module","source_file":"{inside}","source_line":1,"source_column":8,"addr":110}}
]}}"#,
        inside = inside.display(),
        outside = outside.display()
    )
}

#[test]
fn the_exclusion_reaches_the_catalog_summary_end_to_end() {
    // G1-b's warning must be the sentence the module tree's `summaryText()`
    // shows, i.e. it has to survive `build_catalog`'s walk (not just the
    // parser), so assert on `ModuleCatalog::warning`.
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let rtl = project.path().join("rtl");
    let inc = project.path().join("inc");
    for directory in [&rtl, &inc] {
        fs::create_dir_all(directory)
            .unwrap_or_else(|error| panic!("creating {}: {error}", directory.display()));
    }
    let inside = rtl.join("top.v");
    let outside = inc.join("defs.vh");
    fs::write(&inside, "module top; endmodule\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", inside.display()));
    fs::write(&outside, "module uart; endmodule\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", outside.display()));
    let document_path = project.path().join("ast.json");
    fs::write(&document_path, outside_file_set_document(&inside, &outside))
        .unwrap_or_else(|error| panic!("writing the document: {error}"));
    let fake = fake_slang_copying(project.path(), &document_path);
    let catalog = build_catalog(
        &[inside],
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );
    assert!(
        catalog.failure.is_none(),
        "the outside declaration is not a failure: {:?}",
        catalog.failure
    );
    assert_eq!(catalog.modules.len(), 1, "uart must be excluded");
    assert_eq!(catalog.modules[0].name, "top");
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the exclusion must reach the catalog summary"));
    assert!(
        warning.contains("1 module(s) are defined only in files outside the file list"),
        "the summary sentence must reach `ModuleCatalog::warning`: {warning}"
    );
    assert!(
        warning.contains("'uart' in") && warning.contains("defs.vh"),
        "the outside module and file must be named: {warning}"
    );
    assert!(
        warning.contains("they are excluded"),
        "the sentence must say what happened: {warning}"
    );
}

/// G1-b, the case the review called a Blocker: the file set is the caller's
/// fact, so a set the document shares NO path with excludes every module and
/// says so. This is the real `.f`-lists-only-the-wrapper shape — the compile
/// unit is in the list, every module definition comes from `` `include ``d
/// files — and it must never silently fall back to collecting the whole
/// document.
#[test]
fn a_file_set_that_shares_no_path_with_the_document_excludes_every_module() {
    let _serial = spawn_serial();
    let project = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let rtl = project.path().join("rtl");
    let inc = project.path().join("inc");
    for directory in [&rtl, &inc] {
        fs::create_dir_all(directory)
            .unwrap_or_else(|error| panic!("creating {}: {error}", directory.display()));
    }
    let wrapper = rtl.join("wrapper.v");
    let included = inc.join("defs.vh");
    fs::write(&wrapper, "`include \"defs.vh\"\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", wrapper.display()));
    fs::write(&included, "module uart; endmodule\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", included.display()));
    // The document mentions ONLY the include file: no declaration and no body
    // resolves into the caller's set (`wrapper.v` compiles, `defs.vh` defines).
    let document_path = project.path().join("ast.json");
    fs::write(&document_path, single_module_document(&included))
        .unwrap_or_else(|error| panic!("writing the document: {error}"));
    let fake = fake_slang_copying(project.path(), &document_path);
    let catalog = build_catalog(
        std::slice::from_ref(&wrapper),
        &CatalogOptions {
            slang_binary: Some(fake),
            ..CatalogOptions::default()
        },
    );

    assert!(
        catalog.failure.is_none(),
        "an all-outside design is a result, not a failure: {:?}",
        catalog.failure
    );
    assert!(
        catalog.modules.is_empty(),
        "nothing in the file set is a definition, so nothing may be catalogued: {:?}",
        names(&catalog.modules)
    );
    let warning = catalog
        .warning
        .as_deref()
        .unwrap_or_else(|| panic!("the exclusion must reach the catalog summary"));
    assert!(
        warning.contains("1 module(s) are defined only in files outside the file list"),
        "the zero-intersection case must report the exclusion, not swallow it: {warning}"
    );
    assert!(
        warning.contains("'top' in") && warning.contains("defs.vh"),
        "the excluded module and its outside file must be named: {warning}"
    );
}

/// One module `top`, declared and elaborated in `file` (an absolute path).
fn single_module_document(file: &Path) -> String {
    format!(
        r#"{{"design":{{"name":"$root","kind":"Root","addr":1,"members":[
  {{"name":"top","kind":"Instance","addr":2,"body":{{"name":"top","kind":"InstanceBody","source_file":"{file}","source_line":1,"source_column":8,"addr":3,"definition":"110 top","members":[]}}}}
]}},"definitions":[
  {{"kind":"Definition","name":"top","definitionKind":"Module","source_file":"{file}","source_line":1,"source_column":8,"addr":110}}
]}}"#,
        file = file.display()
    )
}

/// G1-b and symlinks: `tool_paths::absolutize_ast` is deliberately LEXICAL, so
/// the project root can be spelled through a symlink while the document reports
/// the target (or the reverse). `FileOrder` canonicalises BOTH sides, so the
/// definition is still inside the caller's set — without it `rank` would answer
/// `usize::MAX` and a real in-project definition would be excluded as external.
#[cfg(unix)]
#[test]
fn a_symlinked_root_spelling_is_still_inside_the_file_set() {
    let dir = tempfile::tempdir().unwrap_or_else(|error| panic!("tempdir: {error}"));
    let real = dir.path().join("real");
    fs::create_dir_all(&real)
        .unwrap_or_else(|error| panic!("creating {}: {error}", real.display()));
    let real_top = real.join("top.v");
    fs::write(&real_top, "module top; endmodule\n")
        .unwrap_or_else(|error| panic!("writing {}: {error}", real_top.display()));
    let link = dir.path().join("linked");
    std::os::unix::fs::symlink(&real, &link)
        .unwrap_or_else(|error| panic!("symlinking {}: {error}", link.display()));
    let linked_top = link.join("top.v");

    // The document reports the REAL path, the caller's set spells the LINK.
    let parsed = parse_catalog_json_with_files(
        &single_module_document(&real_top),
        std::slice::from_ref(&linked_top),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        names(&parsed.modules),
        vec!["top"],
        "a symlink spelling of the project root is still the same file"
    );
    assert!(
        parsed.warnings.is_empty(),
        "the definition is inside the set, so nothing is excluded: {:?}",
        parsed.warnings
    );

    // The reverse: the set spells the REAL path, the document reports the LINK.
    let parsed = parse_catalog_json_with_files(
        &single_module_document(&linked_top),
        std::slice::from_ref(&real_top),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(names(&parsed.modules), vec!["top"]);
    assert!(
        parsed.warnings.is_empty(),
        "the reverse spelling is inside the set too: {:?}",
        parsed.warnings
    );
}

// ---------------------------------------------------------------------------
// G1-c: the LOSING definition file is PUBLISHED, so a consumer can leave it out
// of the compiler input — the duplicate pick then stays the catalog's alone and
// `slang`'s own duplicate rule can never become a second authority. The safety
// half is the important one: a file that still declares anything the compiler
// must see is KEPT and only warned about.
// ---------------------------------------------------------------------------

/// G1-c's safety boundary: the losing file ALSO declares a module of its own
/// (`fifo`) that no other file declares. Excluding `sim/uart.v` would delete
/// `fifo` from the design, so the file must stay in the input.
const LOSER_WITH_A_UNIQUE_MODULE: &str = r#"{
  "design": {
    "name": "$root",
    "kind": "Root",
    "addr": 1,
    "members": [
      {"name": "uart", "kind": "Instance", "addr": 2,
       "body": {"name": "uart", "kind": "InstanceBody", "source_file": "sim/uart.v",
                "source_line": 1, "source_column": 8, "addr": 3, "definition": "11 uart",
                "members": []}},
      {"name": "fifo", "kind": "Instance", "addr": 4,
       "body": {"name": "fifo", "kind": "InstanceBody", "source_file": "sim/uart.v",
                "source_line": 6, "source_column": 8, "addr": 5, "definition": "12 fifo",
                "members": []}}
    ]
  },
  "definitions": [
    {"name": "uart", "kind": "Definition", "source_file": "rtl/uart.v", "source_line": 1,
     "source_column": 8, "addr": 10, "definitionKind": "Module"},
    {"name": "uart", "kind": "Definition", "source_file": "sim/uart.v", "source_line": 1,
     "source_column": 8, "addr": 11, "definitionKind": "Module"},
    {"name": "fifo", "kind": "Definition", "source_file": "sim/uart.v", "source_line": 6,
     "source_column": 8, "addr": 12, "definitionKind": "Module"}
  ]
}"#;

/// G1-c's second safety half: a losing file that ALSO declares a package. A
/// package is not a module, but the compiler still needs it, so the file is kept.
const LOSER_WITH_A_PACKAGE: &str = r#"{
  "design": {
    "name": "$root",
    "kind": "Root",
    "addr": 1,
    "members": [
      {"name": "uart", "kind": "Instance", "addr": 2,
       "body": {"name": "uart", "kind": "InstanceBody", "source_file": "sim/uart.v",
                "source_line": 1, "source_column": 8, "addr": 3, "definition": "11 uart",
                "members": []}}
    ]
  },
  "definitions": [
    {"name": "uart", "kind": "Definition", "source_file": "rtl/uart.v", "source_line": 1,
     "source_column": 8, "addr": 10, "definitionKind": "Module"},
    {"name": "uart", "kind": "Definition", "source_file": "sim/uart.v", "source_line": 1,
     "source_column": 8, "addr": 11, "definitionKind": "Module"},
    {"name": "cfg_pkg", "kind": "Definition", "source_file": "sim/uart.v", "source_line": 4,
     "source_column": 8, "addr": 12, "definitionKind": "Package"}
  ]
}"#;

#[test]
fn a_losing_file_with_no_other_module_is_published_as_shadowed() {
    let parsed = parse_catalog_json_with_files(
        &duplicate_module_ast(),
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    // The loser declares `uart` only, so the compiler input no longer needs it.
    assert_eq!(
        parsed.shadowed_files,
        vec![PathBuf::from("sim/uart.v")],
        "the file whose every module lost the pick is published"
    );
    assert_eq!(
        parsed.shadowed_warning.as_deref(),
        Some(
            "sim/uart.v is excluded from the compiler input because every module it defines \
             lost the duplicate pick"
        ),
        "the consequence must be spelled out, not left to the tool's own error"
    );
    // G1-c must not disturb B-1/B-2: the pick and its frozen sentence stand.
    assert!(parsed.modules[0].file.ends_with("rtl/uart.v"));
    assert_eq!(
        parsed.warnings[0],
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
    );

    // The list follows the PICK, never the document: the opposite file-set order
    // shadows the other file (and would shadow nothing if it were hard-coded).
    let reversed = parse_catalog_json_with_files(
        &duplicate_module_ast(),
        &file_set(["sim/uart.v", "rtl/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        reversed.shadowed_files,
        vec![PathBuf::from("rtl/uart.v")],
        "the losing file is the one that did not win THIS file set"
    );
    assert_eq!(
        reversed.shadowed_warning.as_deref(),
        Some(
            "rtl/uart.v is excluded from the compiler input because every module it defines \
             lost the duplicate pick"
        )
    );
}

#[test]
fn a_losing_file_that_also_declares_a_unique_module_is_kept_and_only_warned() {
    let parsed = parse_catalog_json_with_files(
        LOSER_WITH_A_UNIQUE_MODULE,
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    // `sim/uart.v` lost `uart` but is the ONLY definition of `fifo`: excluding it
    // would lose a module, so it must not be published as shadowed.
    assert!(
        parsed.shadowed_files.is_empty(),
        "a file that still defines a module must stay in the compiler input: {:?}",
        parsed.shadowed_files
    );
    assert!(
        parsed.shadowed_warning.is_none(),
        "no exclusion may be announced for a file that is kept: {:?}",
        parsed.shadowed_warning
    );
    // The collision is still reported — the user is told, the file is kept.
    assert_eq!(
        parsed.warnings[0],
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v",
        "the duplicate warning must survive the safety boundary: {:?}",
        parsed.warnings
    );
    assert_eq!(
        names(&parsed.modules),
        vec!["fifo", "uart"],
        "both modules are still catalogued"
    );
}

#[test]
fn a_repeat_inside_one_file_is_never_shadowed() {
    let parsed = parse_catalog_json_with_files(SAME_FILE_DUPLICATE, &[PathBuf::from("rtl/uart.v")])
        .unwrap_or_else(|error| panic!("parse: {error}"));
    // Both declarations are in this very file: it cannot be dropped, and it is
    // not "a file that lost to another file" either.
    assert!(
        parsed.shadowed_files.is_empty(),
        "a same-file repeat never shadows the file: {:?}",
        parsed.shadowed_files
    );
    assert!(parsed.shadowed_warning.is_none());
    assert_eq!(
        parsed.duplicate_modules,
        vec!["uart".to_string()],
        "the collision is still recorded (R1-a) even though nothing is excluded"
    );
}

#[test]
fn a_project_without_duplicates_publishes_no_shadowed_file() {
    let files = project_files(&catalog_fixture().join("project"));
    let parsed = parse_catalog_json_with_files(&ast_json(), &files)
        .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(names(&parsed.modules).len(), 6, "the fixture is catalogued");
    assert!(
        parsed.duplicate_modules.is_empty(),
        "the fixture declares every module once: {:?}",
        parsed.duplicate_modules
    );
    assert!(
        parsed.shadowed_files.is_empty(),
        "with no duplicate pick there is nothing shadowed: {:?}",
        parsed.shadowed_files
    );
    assert!(parsed.shadowed_warning.is_none());
}

#[test]
fn a_losing_file_that_also_declares_a_package_is_kept() {
    let parsed = parse_catalog_json_with_files(
        LOSER_WITH_A_PACKAGE,
        &file_set(["rtl/uart.v", "sim/uart.v"]),
    )
    .unwrap_or_else(|error| panic!("parse: {error}"));
    // `sim/uart.v` also declares `cfg_pkg`. A package is not a module, but the
    // compiler still needs it, so the file is kept (the conservative side of the
    // safety boundary: only files that contribute NOTHING are published).
    assert!(
        parsed.shadowed_files.is_empty(),
        "a file that declares a package must stay in the compiler input: {:?}",
        parsed.shadowed_files
    );
    assert!(parsed.shadowed_warning.is_none());
    assert_eq!(
        parsed.warnings[0],
        "module 'uart' is defined in both sim/uart.v and rtl/uart.v; using rtl/uart.v"
    );
}

#[test]
fn many_shadowed_files_are_counted_and_listed_up_to_three_then_folded() {
    // R1-b's bound applies to the new sentence too: a project-wide duplicate
    // must not turn the summary into a file dump.
    let declarations: Vec<(String, u32)> = (0..5)
        .map(|index| (format!("rtl/block_{index:03}.v"), 1))
        .collect();
    let files: Vec<PathBuf> = declarations
        .iter()
        .map(|(file, _line)| PathBuf::from(file))
        .collect();
    let parsed =
        parse_catalog_json_with_files(&duplicate_sites_document("ANDX2", &declarations), &files)
            .unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(
        parsed.shadowed_files,
        vec![
            PathBuf::from("rtl/block_001.v"),
            PathBuf::from("rtl/block_002.v"),
            PathBuf::from("rtl/block_003.v"),
            PathBuf::from("rtl/block_004.v"),
        ],
        "every losing file is published, not only the ones the sentence names"
    );
    assert_eq!(
        parsed.shadowed_warning.as_deref(),
        Some(
            "4 files are excluded from the compiler input because every module they define lost \
             the duplicate pick (block_001.v, block_002.v, block_003.v, +1 more)"
        )
    );
}
