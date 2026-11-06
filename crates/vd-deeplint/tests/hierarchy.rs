//! Instance hierarchy + signal driver/load (T7).
//!
//! Two layers of evidence:
//! * the parser, against a captured `--ast-json` document (deterministic, no tool);
//! * the runner, against the real `slang` binary on the AC-3.0 fixture.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;

use vd_deeplint::{
    elaborate, parse_ast_json, EndpointKind, HierarchyOptions, InstanceNode, LintFailure,
};

// The process-spawning tests write an executable fake binary (the hang test) and
// run the real tool; running those bodies in parallel is the class of race the
// crate's `fake_binary.rs` documents (a child exec'ing a file another thread still
// has open for writing => ETXTBSY => `LintFailure::Spawn`, which is not a timeout).
// The bodies are serialised for the same reason. This does NOT retry or weaken an
// assertion: a run that does not time out still fails.
static SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn serial() -> std::sync::MutexGuard<'static, ()> {
    match SERIAL.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/hierarchy")
}

fn ast_json() -> String {
    fs::read_to_string(fixture_dir().join("ast.json"))
        .unwrap_or_else(|error| panic!("reading the AST fixture: {error}"))
}

fn spike_fixture() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../probes/slang-spike/fixture")
}

fn spike_files() -> Vec<PathBuf> {
    let mut files: Vec<PathBuf> = fs::read_dir(spike_fixture())
        .unwrap_or_else(|error| panic!("reading the spike fixture: {error}"))
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
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

fn paths(instances: &[InstanceNode]) -> Vec<&str> {
    instances.iter().map(|node| node.path.as_str()).collect()
}

#[test]
fn the_captured_ast_json_yields_the_elaborated_hierarchy() {
    let report = parse_ast_json(&ast_json()).unwrap_or_else(|error| panic!("parse: {error}"));
    assert_eq!(paths(&report.instances), vec!["top", "top.u0", "top.u1"]);
    let u0 = report
        .instance("top.u0")
        .unwrap_or_else(|| panic!("top.u0"));
    assert_eq!(u0.module, "mid");
    assert!(u0.file.ends_with("top.sv"), "{:?}", u0.file);
    assert_eq!(u0.line, 10, "0-based line of `mid u0 (...)`");
    let u1 = report
        .instance("top.u1")
        .unwrap_or_else(|| panic!("top.u1"));
    assert_eq!(u1.module, "mid");
    assert_eq!(u1.line, 11);
    // Both instances resolve to a real body despite slang deduplicating them.
    assert_eq!(
        report.instance("top.u0").map(|node| node.module.as_str()),
        Some("mid")
    );
    assert_eq!(
        report.instance("top.u1").map(|node| node.module.as_str()),
        Some("mid")
    );
}

#[test]
fn the_driven_and_loaded_signal_carries_exact_positions() {
    let report = parse_ast_json(&ast_json()).unwrap_or_else(|error| panic!("parse: {error}"));
    let link = report
        .signal("top.link")
        .unwrap_or_else(|| panic!("top.link"));
    assert_eq!(link.kind, "Variable");
    assert_eq!(link.drivers.len(), 1, "{:?}", link.drivers);
    assert_eq!(link.drivers[0].path, "top.u0.q");
    assert_eq!(link.drivers[0].kind, EndpointKind::InstanceOutputPort);
    assert!(link.drivers[0].file.ends_with("top.sv"));
    assert_eq!(
        link.drivers[0].line, 10,
        "0-based line of the u0 instantiation"
    );

    let load_paths: Vec<&str> = link.loads.iter().map(|load| load.path.as_str()).collect();
    assert!(load_paths.contains(&"top.u1.d"), "{load_paths:?}");
    assert!(link
        .loads
        .iter()
        .any(|load| load.kind == EndpointKind::ContinuousAssign));

    // The chain continues inside the child that drives the net.
    let chain: Vec<&str> = link.chain.iter().map(|step| step.path.as_str()).collect();
    assert_eq!(chain.first().copied(), Some("top.u0.q"), "{chain:?}");
    assert!(
        link.chain
            .iter()
            .any(|step| step.kind == EndpointKind::Procedural),
        "the chain ends at the always_ff: {chain:?}"
    );
    let always = link
        .chain
        .iter()
        .find(|step| step.kind == EndpointKind::Procedural)
        .unwrap_or_else(|| panic!("procedural driver"));
    assert!(always.file.ends_with("mid.sv"), "{:?}", always.file);
    // mid.sv: the `always_ff` is line 9 (1-based), its assignment line 10; the
    // endpoint carries the ASSIGNMENT (0-based line 9), which is what the editor
    // highlights.
    assert_eq!(always.line, 9, "0-based line of the procedural assignment");
    assert_eq!(always.detail, "AlwaysFF");
}

#[test]
fn the_real_tool_produces_the_same_hierarchy_within_the_bounds() {
    let _serial = serial();
    let files = spike_files();
    assert!(!files.is_empty(), "the spike fixture must exist");
    let options = HierarchyOptions {
        slang_binary: Some(slang_binary()),
        top: Some("top".to_string()),
        max_files: 1_000,
        max_instances: 10_000,
        ..HierarchyOptions::default()
    };
    let report = elaborate(&files, &options);
    assert!(report.failure.is_none(), "{:?}", report.failure);
    assert_eq!(paths(&report.instances), vec!["top", "top.u0", "top.u1"]);
    let link = report
        .signal("top.link")
        .unwrap_or_else(|| panic!("top.link"));
    assert_eq!(link.drivers[0].path, "top.u0.q");
    assert!(link.loads.iter().any(|load| load.path == "top.u1.d"));
    // AC-3.5 kill bounds, measured per run.
    assert!(
        report.elapsed <= Duration::from_secs(30),
        "elaboration took {:?}",
        report.elapsed
    );
    assert!(report.peak_rss_kb > 0, "the child's peak RSS is sampled");
    assert!(
        report.peak_rss_kb <= 1024 * 1024,
        "peak RSS {} KiB exceeds 1 GiB",
        report.peak_rss_kb
    );
    assert!(report.warning.is_none(), "{:?}", report.warning);
    assert!(
        report.summary().contains("instance(s)"),
        "{}",
        report.summary()
    );
    println!(
        "VD_HIER_REPORT_JSON={}",
        serde_json::json!({
            "tool": report.tool,
            "top": report.top,
            "elapsed_ms": report.elapsed.as_millis(),
            "peak_rss_kb": report.peak_rss_kb,
            "warning": report.warning,
            "instances": report.instances.iter().map(|node| serde_json::json!({
                "path": node.path,
                "module": node.module,
                "file": node.file.to_string_lossy(),
                "line": node.line,
                "column": node.column,
            })).collect::<Vec<_>>(),
            "signals": report.signals.iter().map(|signal| serde_json::json!({
                "path": signal.path,
                "kind": signal.kind,
                "drivers": signal.drivers.iter().map(|endpoint| serde_json::json!({
                    "path": endpoint.path,
                    "file": endpoint.file.to_string_lossy(),
                    "line": endpoint.line,
                    "column": endpoint.column,
                    "kind": endpoint.kind.as_str(),
                    "detail": endpoint.detail,
                })).collect::<Vec<_>>(),
                "loads": signal.loads.iter().map(|endpoint| serde_json::json!({
                    "path": endpoint.path,
                    "file": endpoint.file.to_string_lossy(),
                    "line": endpoint.line,
                    "column": endpoint.column,
                    "kind": endpoint.kind.as_str(),
                })).collect::<Vec<_>>(),
                "chain": signal.chain.iter().map(|endpoint| serde_json::json!({
                    "path": endpoint.path,
                    "line": endpoint.line,
                    "kind": endpoint.kind.as_str(),
                    "detail": endpoint.detail,
                })).collect::<Vec<_>>(),
            })).collect::<Vec<_>>(),
        })
    );
}

#[test]
fn an_oversized_project_sets_the_warning_instead_of_failing_silently() {
    let _serial = serial();
    let files = spike_files();
    let options = HierarchyOptions {
        slang_binary: Some(slang_binary()),
        top: Some("top".to_string()),
        max_files: 1,
        ..HierarchyOptions::default()
    };
    let report = elaborate(&files, &options);
    assert!(report.failure.is_none(), "{:?}", report.failure);
    let warning = report
        .warning
        .unwrap_or_else(|| panic!("a warning is expected"));
    assert!(warning.contains("limit"), "{warning}");
    assert!(!report.instances.is_empty(), "the run still produced data");
}

/// The contract's input half: the project file set plus `-I`/`-D` must reach the
/// tool as argv (a fake `slang` records its own arguments and exits).
#[cfg(unix)]
#[test]
fn the_elaboration_command_passes_the_files_include_dirs_and_defines() {
    let _serial = serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-hier-argv-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let script = dir.path().join("slang");
    fs::write(
        &script,
        "#!/bin/sh\nprintf '%s\\n' \"$@\" > args.txt\nexit 1\n",
    )
    .unwrap_or_else(|error| panic!("write: {error}"));
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&script)
            .unwrap_or_else(|error| panic!("stat: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&script, permissions).unwrap_or_else(|error| panic!("chmod: {error}"));
    }
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n").unwrap_or_else(|error| panic!("write: {error}"));
    let include = dir.path().join("include");
    let options = HierarchyOptions {
        slang_binary: Some(script),
        top: Some("top".to_string()),
        include_dirs: vec![include.clone()],
        defines: vec!["WIDTH=8".to_string(), "SIM".to_string()],
        timeout: Duration::from_secs(5),
        ..HierarchyOptions::default()
    };
    let report = elaborate(&[file], &options);
    assert!(
        report.failure.is_some(),
        "a tool that exits 1 must be reported as a failure"
    );
    let argv = fs::read_to_string(dir.path().join("args.txt"))
        .unwrap_or_else(|error| panic!("the fake slang did not record its argv: {error}"));
    let lines: Vec<&str> = argv.lines().collect();
    assert!(
        lines
            .iter()
            .any(|line| *line == format!("-I{}", include.display())),
        "-I<dir> must be one argv token; argv:\n{argv}"
    );
    assert!(
        lines.contains(&"-DWIDTH=8"),
        "-D<define> must be one argv token; argv:\n{argv}"
    );
    assert!(
        lines.contains(&"-DSIM"),
        "every define is passed; argv:\n{argv}"
    );
    assert!(
        lines.contains(&"--ast-json-source-info"),
        "source positions are requested; argv:\n{argv}"
    );
    assert!(
        lines.iter().any(|line| line.ends_with("top.sv")),
        "the project file set is passed; argv:\n{argv}"
    );
    assert!(lines.contains(&"top"), "--top is passed; argv:\n{argv}");
}

#[cfg(unix)]
#[test]
fn a_hanging_elaboration_times_out_and_is_reaped() {
    let _serial = serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-hier-timeout-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let script = dir.path().join("slang");
    fs::write(&script, "#!/bin/sh\nexec sleep 30\n")
        .unwrap_or_else(|error| panic!("write: {error}"));
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&script)
            .unwrap_or_else(|error| panic!("stat: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&script, permissions).unwrap_or_else(|error| panic!("chmod: {error}"));
    }
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n").unwrap_or_else(|error| panic!("write: {error}"));
    let options = HierarchyOptions {
        slang_binary: Some(script),
        top: Some("top".to_string()),
        timeout: Duration::from_millis(200),
        ..HierarchyOptions::default()
    };
    let report = elaborate(&[file], &options);
    match report.failure {
        Some(LintFailure::Timeout { .. }) => {}
        // Show enough to diagnose a recurrence (which failure variant, which tool,
        // how long it took) instead of a bare variant name.
        other => panic!(
            "expected a timeout, got {other:?} (tool={}, elapsed={:?}, peak_rss={} KiB)",
            report.tool, report.elapsed, report.peak_rss_kb
        ),
    }
    assert!(!Path::new(&spike_fixture()).exists() || true);
}

/// The crate's documented rule (lib.rs): slang exits 1 when it reported errors
/// but still wrote the AST; that is a normal result. Before the fix every
/// nonzero exit returned MissingOutput and threw the valid tree away.
#[cfg(unix)]
#[test]
fn a_nonzero_exit_with_a_parseable_ast_json_is_a_normal_result() {
    let _serial = serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-hier-exit1-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let script = dir.path().join("slang");
    let ast = fixture_dir().join("ast.json");
    fs::write(
        &script,
        format!(
            "#!/bin/sh\nprev=\"\"\nout=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--ast-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\ncat '{}' > \"$out\"\necho 'fake slang: 1 error reported' >&2\nexit 1\n",
            ast.display()
        ),
    )
    .unwrap_or_else(|error| panic!("write: {error}"));
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&script)
            .unwrap_or_else(|error| panic!("stat: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&script, permissions).unwrap_or_else(|error| panic!("chmod: {error}"));
    }
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n").unwrap_or_else(|error| panic!("write: {error}"));
    let options = HierarchyOptions {
        slang_binary: Some(script),
        top: Some("top".to_string()),
        ..HierarchyOptions::default()
    };
    let report = elaborate(&[file], &options);
    assert!(
        report.failure.is_none(),
        "a nonzero exit with valid AST JSON is a normal result: {:?}",
        report.failure
    );
    assert_eq!(paths(&report.instances), vec!["top", "top.u0", "top.u1"]);
}

/// The visible half of the module-tree failure: a child that dies before it can
/// write JSON must surface its own stderr (the loader's `GLIBC_2.35` line)
/// instead of only "produced no diagnostics".
#[cfg(unix)]
#[test]
fn a_child_that_dies_without_output_reports_its_stderr_line() {
    let _serial = serial();
    let dir = tempfile::Builder::new()
        .prefix("vd-hier-stderr-")
        .tempdir()
        .unwrap_or_else(|error| panic!("tempdir: {error}"));
    let script = dir.path().join("slang");
    fs::write(
        &script,
        "#!/bin/sh\necho 'version GLIBC_2.35 not found (required by libc.so.6)' >&2\nexit 1\n",
    )
    .unwrap_or_else(|error| panic!("write: {error}"));
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&script)
            .unwrap_or_else(|error| panic!("stat: {error}"))
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&script, permissions).unwrap_or_else(|error| panic!("chmod: {error}"));
    }
    let file = dir.path().join("top.sv");
    fs::write(&file, "module top;\nendmodule\n").unwrap_or_else(|error| panic!("write: {error}"));
    let options = HierarchyOptions {
        slang_binary: Some(script),
        top: Some("top".to_string()),
        ..HierarchyOptions::default()
    };
    let report = elaborate(&[file], &options);
    match &report.failure {
        Some(LintFailure::MissingOutput { exit_code, .. }) => {
            assert_eq!(*exit_code, Some(1));
            assert!(
                report.summary().contains("GLIBC_2.35"),
                "the child's stderr line must be user-visible: {}",
                report.summary()
            );
        }
        other => panic!("expected MissingOutput, got {other:?}"),
    }
}

// --- T46 regressions: multiple roots, generate constructs, instance arrays ----
//
// The scale measurement found that the service returned 5 instances where the
// elaborated design has 11 000: `parse_design` walked only the first `Instance`
// of `design.members`, and `walk_instance` only descended into direct `Instance`
// members, so every instance inside a generate construct was invisible. These
// two tests pin the ground truth on a small real project
// (`tests/fixtures/hierarchy/project/`) plus its captured AST, and the third
// re-elaborates the same project with the real tool.

fn captured_multi_root_ast() -> String {
    fs::read_to_string(fixture_dir().join("multi-root-generate.json"))
        .unwrap_or_else(|error| panic!("reading multi-root-generate.json: {error}"))
}

fn multi_root_project_files() -> Vec<PathBuf> {
    let project = fixture_dir().join("project");
    let mut files: Vec<PathBuf> = fs::read_dir(&project)
        .unwrap_or_else(|error| panic!("reading {}: {error}", project.display()))
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.extension().is_some_and(|extension| extension == "sv"))
        .collect();
    files.sort();
    files
}

#[test]
fn every_design_root_is_elaborated_not_only_the_first() {
    let report =
        parse_ast_json(&captured_multi_root_ast()).unwrap_or_else(|error| panic!("parse: {error}"));
    let roots: Vec<&str> = report
        .instances
        .iter()
        .filter(|node| !node.path.contains('.'))
        .map(|node| node.path.as_str())
        .collect();
    // Three modules are elaborated as design roots (an instance array, and two
    // plain tops); the old walker returned the first one only.
    assert_eq!(roots, vec!["arr", "top1", "top2"]);
    // 19 elaborated instances: arr + its 2 array elements (each a `mid`) with
    // 3 generate instances each, and top1/top2 with u_mid + 3 generate
    // instances each. The captured AST serialises `mid`'s body once (slang
    // deduplicates bodies by address); the walker must expand it per instance.
    assert_eq!(report.instances.len(), 19, "{:?}", paths(&report.instances));
}

#[test]
fn generate_constructs_and_instance_arrays_are_walked() {
    let report =
        parse_ast_json(&captured_multi_root_ast()).unwrap_or_else(|error| panic!("parse: {error}"));

    let generated = report
        .instance("top1.u_mid.g_leaf[1].u")
        .unwrap_or_else(|| panic!("top1.u_mid.g_leaf[1].u was not elaborated"));
    assert_eq!(generated.module, "leaf");
    assert!(generated.file.ends_with("mid.sv"), "{:?}", generated.file);
    assert_eq!(
        generated.line, 5,
        "0-based line of the instantiation in the loop"
    );

    // An `if` generate block keeps its label; a loop iteration uses its index so
    // two iterations never share a path.
    assert!(report.instance("top1.u_mid.g_if.u_if").is_some());
    assert!(report.instance("top1.u_mid.g_leaf[0].u").is_some());
    assert!(report.instance("top1.u_mid.g_leaf[1].u").is_some());

    // The instance array contributes one instance per element.
    assert_eq!(
        report
            .instance("arr.u_arr[0]")
            .map(|node| node.module.as_str()),
        Some("mid")
    );
    assert_eq!(
        report
            .instance("arr.u_arr[1]")
            .map(|node| node.module.as_str()),
        Some("mid")
    );
}

#[test]
fn ports_inside_a_generate_block_reach_the_parent_signal() {
    let report =
        parse_ast_json(&captured_multi_root_ast()).unwrap_or_else(|error| panic!("parse: {error}"));
    // `t[g]` binds through a bit select; the endpoint belongs to the base net
    // `top1.u_mid.t`, so the signal must list both generated leaves as drivers.
    let t = report
        .signal("top1.u_mid.t")
        .unwrap_or_else(|| panic!("top1.u_mid.t"));
    let drivers: Vec<&str> = t
        .drivers
        .iter()
        .map(|driver| driver.path.as_str())
        .collect();
    assert!(drivers.contains(&"top1.u_mid.g_leaf[0].u.b"), "{drivers:?}");
    assert!(drivers.contains(&"top1.u_mid.g_leaf[1].u.b"), "{drivers:?}");

    let a = report
        .signal("top1.u_mid.a")
        .unwrap_or_else(|| panic!("top1.u_mid.a"));
    let loads: Vec<&str> = a.loads.iter().map(|load| load.path.as_str()).collect();
    assert!(loads.contains(&"top1.u_mid.g_leaf[0].u.a"), "{loads:?}");
    assert!(loads.contains(&"top1.u_mid.g_if.u_if.a"), "{loads:?}");
}

#[test]
fn the_real_tool_elaborates_every_root_and_generate_instantiation() {
    let _serial = serial();
    let files = multi_root_project_files();
    assert!(
        !files.is_empty(),
        "the regression project must ship sources"
    );
    let options = HierarchyOptions {
        slang_binary: Some(slang_binary()),
        top: None,
        ..HierarchyOptions::default()
    };
    let report = elaborate(&files, &options);
    assert!(report.failure.is_none(), "{:?}", report.failure);
    assert_eq!(
        report.instances.len(),
        19,
        "elaborated instance count (see `path`): {:?}",
        paths(&report.instances)
    );
    let roots: Vec<&str> = report
        .instances
        .iter()
        .filter(|node| !node.path.contains('.'))
        .map(|node| node.path.as_str())
        .collect();
    assert_eq!(roots, vec!["arr", "top1", "top2"]);
    assert!(
        report.instance("top2.u_mid.g_leaf[1].u").is_some(),
        "generate instances are present: {:?}",
        paths(&report.instances)
    );
    assert!(
        report
            .signal("top2.u_mid.t")
            .is_some_and(|signal| !signal.drivers.is_empty()),
        "the generate-block output drives the parent net"
    );
}
