//! The product-wide tool-resolution order for the lint binaries (T8 / T24-F1).
//!
//! This is the cross-crate contract: the seven stage labels here must equal
//! `vd_config::provenance::ResolutionStage::ORDER`'s labels, and the resolver
//! must probe them in that sequence for both engines. `vd-deeplint` does not
//! depend on `vd-config` (a dependency would belong to the workspace owner), so
//! `the_two_crates_declare_the_same_order` reads the other crate's source file
//! and pins the two written orders together.
//!
//! Integration form on purpose: it exercises only the public API, and it can run
//! while other lanes' `#[cfg(test)]` modules are temporarily broken.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use vd_deeplint::binary::{
    resolve_binary_with_imported, resolve_verible_lint_with_imported, AttemptOutcome,
    AttemptSource, STAGE_ORDER,
};

static DIRS: AtomicU64 = AtomicU64::new(0);

/// `Result::unwrap`/`expect` are denied workspace-wide, tests included.
fn must<T, E: std::fmt::Display>(result: Result<T, E>, what: &str) -> T {
    result.unwrap_or_else(|error| panic!("{what}: {error}"))
}

/// The same discipline for an `Option` (a temp path is UTF-8 on Linux).
fn utf8(path: &Path) -> &str {
    path.to_str()
        .unwrap_or_else(|| panic!("{} is not UTF-8", path.display()))
}

struct TempDir(PathBuf);

impl TempDir {
    fn new(label: &str) -> Self {
        let serial = DIRS.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!(
            "vd-deeplint-order-{}-{serial}-{label}",
            std::process::id()
        ));
        must(fs::create_dir_all(&path), "create temp dir");
        Self(path)
    }

    fn script(&self, name: &str) -> PathBuf {
        let path = self.0.join(name);
        if let Some(parent) = path.parent() {
            must(fs::create_dir_all(parent), "create parent");
        }
        must(
            fs::write(&path, "#!/bin/sh\nexit 0\n"),
            "write the fake tool",
        );
        make_executable(&path);
        path
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[cfg(unix)]
fn make_executable(path: &Path) {
    use std::os::unix::fs::PermissionsExt;
    let mut permissions = must(fs::metadata(path), "metadata").permissions();
    permissions.set_mode(0o755);
    must(fs::set_permissions(path, permissions), "chmod");
}

#[cfg(not(unix))]
fn make_executable(_path: &Path) {}

/// The `Self::Variant` names in a block, in source order.
fn variant_names(block: &str) -> Vec<String> {
    block
        .match_indices("Self::")
        .map(|(index, _)| {
            block[index + "Self::".len()..]
                .chars()
                .take_while(|character| character.is_ascii_alphanumeric() || *character == '_')
                .collect::<String>()
        })
        .filter(|name| !name.is_empty())
        .collect()
}

#[test]
fn the_two_crates_declare_the_same_order() {
    let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("../vd-config/src/provenance.rs");
    let text = must(
        fs::read_to_string(&source),
        "read vd-config's provenance.rs",
    );

    // `ResolutionStage::ORDER` — the written probe order.
    let order_at = text
        .find("pub const ORDER: [Self; 7] = [")
        .unwrap_or_else(|| panic!("vd-config must declare ResolutionStage::ORDER"));
    let after_order = &text[order_at..];
    let order_end = after_order
        .find("];")
        .unwrap_or_else(|| panic!("the ORDER array must close with `];`"));
    let order = variant_names(&after_order[..order_end]);
    assert_eq!(
        order.len(),
        7,
        "the written order has seven stages: {order:?}"
    );

    // `ResolutionStage::as_str` — the label each stage spells. The file has
    // more than one `as_str`, so the search starts AT the stage enum.
    let labels_at = text[order_at..]
        .find("pub fn as_str(self) -> &'static str {")
        .map(|offset| order_at + offset)
        .unwrap_or_else(|| panic!("vd-config must map a stage to its label"));
    let after_labels = &text[labels_at..];
    let labels_end = after_labels
        .find("\n    }")
        .unwrap_or_else(|| panic!("the as_str body must close"));
    let mut labels: Vec<(String, String)> = Vec::new();
    for line in after_labels[..labels_end].lines() {
        let line = line.trim();
        let Some(rest) = line.strip_prefix("Self::") else {
            continue;
        };
        let Some((variant, value)) = rest.split_once(" => ") else {
            continue;
        };
        let value = value.trim().trim_end_matches(',').trim();
        let Some(label) = value
            .strip_prefix('"')
            .and_then(|label| label.strip_suffix('"'))
        else {
            continue;
        };
        labels.push((variant.trim().to_string(), label.to_string()));
    }
    let mapped: Vec<String> = order
        .iter()
        .map(|variant| {
            labels
                .iter()
                .find(|(name, _)| name == variant)
                .map(|(_, label)| label.clone())
                .unwrap_or_else(|| panic!("{variant} has no as_str label"))
        })
        .collect();
    assert_eq!(
        mapped,
        STAGE_ORDER.to_vec(),
        "the lint resolver's STAGE_ORDER must equal ResolutionStage::ORDER label for label"
    );
}

#[test]
fn stage_labels_match_the_product_order() {
    assert_eq!(
        STAGE_ORDER,
        [
            "setting",
            "bundled",
            "local",
            "imported-project",
            "imported-global",
            "PATH",
            "env"
        ]
    );
    let sources = [
        AttemptSource::ExplicitOption,
        AttemptSource::ExeRelativeRootBin,
        AttemptSource::LocalCheckout,
        AttemptSource::ImportedProject,
        AttemptSource::ImportedGlobal,
        AttemptSource::Path,
        AttemptSource::Environment,
    ];
    for (index, source) in sources.iter().enumerate() {
        assert_eq!(
            source.stage(),
            index,
            "{} is at the wrong stage",
            source.as_str()
        );
        assert_eq!(source.stage_label(), STAGE_ORDER[index]);
    }
    assert_eq!(AttemptSource::EnvironmentVerible.stage(), 6);
    assert_eq!(AttemptSource::ExeDirectoryVerible.stage(), 1);
    // Every bundled spelling is stage 1: the four paths of the payload.
    for source in [
        AttemptSource::ExeRelativePackageLib,
        AttemptSource::ExeRelativeBin,
        AttemptSource::ExeRelativeRootBin,
        AttemptSource::ExeDirectory,
        AttemptSource::ExeRelativePackageLibVerible,
        AttemptSource::ExeRelativeBinVerible,
        AttemptSource::ExeRelativeRootBinVerible,
        AttemptSource::ExeDirectoryVerible,
    ] {
        assert_eq!(source.stage(), 1, "{}", source.as_str());
    }
}

#[test]
fn bundled_payload_beats_the_environment_variable() {
    let dir = TempDir::new("bundled");
    let bundled = dir.script("bin/slang");
    let env = dir.script("slang-env");
    let resolution = resolve_binary_with_imported(
        None,
        &Default::default(),
        &Default::default(),
        None,
        Some(env.as_os_str()),
        &dir.0,
    );
    assert_eq!(
        resolution.chosen,
        Some(bundled),
        "bundled must win over env"
    );
    let env_attempt = resolution
        .attempts
        .iter()
        .find(|attempt| attempt.source == AttemptSource::Environment)
        .unwrap_or_else(|| panic!("the env stage must be recorded"));
    assert_eq!(env_attempt.outcome, AttemptOutcome::Found);
    assert_eq!(env_attempt.source.stage(), 6);
}

#[test]
fn path_is_consulted_after_bundled_and_local() {
    let dir = TempDir::new("path");
    let path_dir = dir.0.join("on-path");
    must(fs::create_dir_all(&path_dir), "create path dir");
    let binary = path_dir.join("slang");
    must(fs::write(&binary, "#!/bin/sh\nexit 0\n"), "write");
    make_executable(&binary);
    let exe_dir = dir.0.join("absent");
    must(fs::create_dir_all(&exe_dir), "create exe dir");
    let resolution = resolve_binary_with_imported(
        None,
        &Default::default(),
        &Default::default(),
        Some(utf8(&path_dir)),
        None,
        &exe_dir,
    );
    assert_eq!(resolution.chosen, Some(binary));
    let path_attempt = resolution
        .attempts
        .iter()
        .find(|attempt| attempt.source == AttemptSource::Path)
        .unwrap_or_else(|| panic!("the PATH stage must be recorded"));
    assert_eq!(path_attempt.outcome, AttemptOutcome::Found);
    assert_eq!(path_attempt.source.stage(), 5);
}

#[test]
fn local_checkout_beats_path_and_environment() {
    let dir = TempDir::new("local");
    let root = dir.0.join("repo");
    must(fs::create_dir_all(root.join("scripts")), "create scripts");
    must(
        fs::write(root.join("scripts/fetch-verible.sh"), "marker"),
        "write the checkout marker",
    );
    let checkout = root.join("build/slang/out/slang");
    must(fs::create_dir_all(root.join("build/slang/out")), "out dir");
    must(fs::write(&checkout, "#!/bin/sh\nexit 0\n"), "write");
    make_executable(&checkout);
    let path_dir = dir.0.join("on-path");
    must(fs::create_dir_all(&path_dir), "path dir");
    let on_path = path_dir.join("slang");
    must(fs::write(&on_path, "#!/bin/sh\nexit 0\n"), "write");
    make_executable(&on_path);
    let env = dir.script("slang-env");
    let exe_dir = root.join("target/debug");
    must(fs::create_dir_all(&exe_dir), "exe dir");
    let resolution = resolve_binary_with_imported(
        None,
        &Default::default(),
        &Default::default(),
        Some(utf8(&path_dir)),
        Some(env.as_os_str()),
        &exe_dir,
    );
    assert_eq!(resolution.chosen, Some(checkout), "local checkout wins");
    assert_eq!(
        resolution
            .attempts
            .iter()
            .find(|attempt| attempt.outcome == AttemptOutcome::Found)
            .map(|attempt| attempt.source),
        Some(AttemptSource::LocalCheckout)
    );
}

#[test]
fn imported_project_beats_imported_global_and_path() {
    let dir = TempDir::new("imported");
    let exe_dir = dir.0.join("absent");
    must(fs::create_dir_all(&exe_dir), "exe dir");
    let project_bin = dir.script("slang-project");
    let global_bin = dir.script("slang-global");
    let mut project = std::collections::BTreeMap::new();
    project.insert(
        "VEDIT_SLANG".to_string(),
        project_bin.to_string_lossy().into_owned(),
    );
    let mut global = std::collections::BTreeMap::new();
    global.insert(
        "VEDIT_SLANG".to_string(),
        global_bin.to_string_lossy().into_owned(),
    );
    let resolution =
        resolve_binary_with_imported(None, &project, &global, Some(utf8(&dir.0)), None, &exe_dir);
    assert_eq!(resolution.chosen, Some(project_bin));
    assert_eq!(
        resolution
            .attempts
            .iter()
            .find(|attempt| attempt.outcome == AttemptOutcome::Found)
            .map(|attempt| attempt.source),
        Some(AttemptSource::ImportedProject)
    );
}

#[test]
fn attempts_are_recorded_in_product_order() {
    let dir = TempDir::new("order");
    let exe_dir = dir.0.join("absent");
    must(fs::create_dir_all(&exe_dir), "exe dir");
    let mut project = std::collections::BTreeMap::new();
    project.insert("VEDIT_SLANG".to_string(), "/nowhere/project".to_string());
    let mut global = std::collections::BTreeMap::new();
    global.insert("VEDIT_SLANG".to_string(), "/nowhere/global".to_string());
    let resolution = resolve_binary_with_imported(
        None,
        &project,
        &global,
        Some("/nowhere/bin"),
        None,
        &exe_dir,
    );
    let stages: Vec<usize> = resolution
        .attempts
        .iter()
        .map(|attempt| attempt.source.stage())
        .collect();
    let mut sorted = stages.clone();
    sorted.sort_unstable();
    assert_eq!(stages, sorted, "attempts must be probed in STAGE_ORDER");
}

#[test]
fn verible_walks_the_same_order() {
    let dir = TempDir::new("verible");
    let path_dir = dir.0.join("on-path");
    must(fs::create_dir_all(&path_dir), "path dir");
    let binary = path_dir.join("verible-verilog-lint");
    must(fs::write(&binary, "#!/bin/sh\nexit 0\n"), "write");
    make_executable(&binary);
    let exe_dir = dir.0.join("absent");
    must(fs::create_dir_all(&exe_dir), "exe dir");
    let resolution = resolve_verible_lint_with_imported(
        None,
        &Default::default(),
        &Default::default(),
        Some(utf8(&path_dir)),
        None,
        &exe_dir,
    );
    assert_eq!(resolution.chosen, Some(binary));
    assert_eq!(
        resolution
            .attempts
            .iter()
            .find(|attempt| attempt.outcome == AttemptOutcome::Found)
            .map(|attempt| attempt.source),
        Some(AttemptSource::Path)
    );
}
