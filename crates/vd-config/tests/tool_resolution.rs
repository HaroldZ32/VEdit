//! Source-assertion matrix for the product-wide tool-resolution order.
//!
//! One order, every tool class: **explicit setting -> bundled -> imported
//! project -> imported global -> PATH -> env**. Each case stages a set of
//! sources and asserts which one wins, so a class that silently changes order
//! (or a new class added to `ToolClass::all()`) fails here instead of in the
//! field.

use std::collections::BTreeMap;
use std::error::Error;
use std::path::{Path, PathBuf};

use vd_config::provenance::ResolutionStage;
use vd_config::tools::{resolve_tool, ImportedVars, ToolClass, ToolSource};
use vd_config::{Config, Env};

fn write_executable(path: &Path) -> Result<(), Box<dyn Error>> {
    use std::os::unix::fs::PermissionsExt;
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(path, "#!/bin/sh\nexit 0\n")?;
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755))?;
    Ok(())
}

fn no_env() -> Env {
    Env::from_pairs(Vec::<(String, String)>::new())
}

fn imported(project: Option<String>, global: Option<String>, env_var: &str) -> ImportedVars {
    let mut project_map = BTreeMap::new();
    if let Some(value) = project {
        project_map.insert(env_var.to_string(), value);
    }
    let mut global_map = BTreeMap::new();
    if let Some(value) = global {
        global_map.insert(env_var.to_string(), value);
    }
    ImportedVars::from_maps(project_map, global_map)
}

/// A staged fixture: one directory per source, each holding a class binary.
struct Fixture {
    temp: tempfile::TempDir,
}

impl Fixture {
    fn new() -> Result<Self, Box<dyn Error>> {
        let temp = tempfile::tempdir()?;
        // The AppDir layout: `<root>/AppDir` is the executable directory and
        // `<root>/AppDir/bin/<tool>` is the bundled payload.
        std::fs::create_dir_all(temp.path().join("AppDir"))?;
        Ok(Self { temp })
    }

    fn root(&self) -> &Path {
        self.temp.path()
    }

    /// An executable directory with NO bundled neighbour: four levels deep, so
    /// `../lib/vedit/bin` and `../bin` cannot resolve to the fixture's own dirs.
    fn absent_exe_dir(&self) -> PathBuf {
        self.root().join("absent/exe")
    }

    fn bundled_exe_dir(&self) -> PathBuf {
        self.root().join("AppDir")
    }

    /// Install `class`'s binary under `<root>/<source>/` and return that dir.
    fn install(&self, source: &str, class: ToolClass) -> Result<PathBuf, Box<dyn Error>> {
        let dir = self.root().join(source);
        write_executable(&dir.join(class.binary_name()))?;
        Ok(dir)
    }
}

/// Which sources a case stages. `expected` is DECLARED by hand, never derived
/// from the flags, so the assertion compares the declared order with the
/// implementation.
struct Case {
    label: &'static str,
    setting: bool,
    bundled: bool,
    imported_project: bool,
    imported_global: bool,
    on_path: bool,
    in_env: bool,
    expected: Option<ToolSource>,
}

fn has_setting_key(class: ToolClass) -> bool {
    matches!(
        class,
        ToolClass::VeribleLs | ToolClass::VeribleFormat | ToolClass::VeribleLint
    )
}

fn set_setting(config: &mut Config, class: ToolClass, path: PathBuf) {
    match class {
        ToolClass::VeribleLs => config.verible.ls = Some(path),
        ToolClass::VeribleFormat => config.verible.format = Some(path),
        ToolClass::VeribleLint => config.verible.lint = Some(path),
        _ => unreachable!("only the Verible trio has a configuration key"),
    }
}

fn assert_stage_order(attempts: &[vd_config::tools::Attempt]) {
    let stages: Vec<ResolutionStage> = attempts.iter().map(|attempt| attempt.stage()).collect();
    let mut sorted = stages.clone();
    sorted.sort();
    assert_eq!(
        stages, sorted,
        "attempts must be probed in ResolutionStage::ORDER"
    );
}

/// The matrix: for every tool class, each source wins exactly when it is the
/// first staged one, and the chosen source and path are reported.
#[test]
fn every_tool_class_follows_one_resolution_order() -> Result<(), Box<dyn Error>> {
    let fixture = Fixture::new()?;
    let mut failures: Vec<String> = Vec::new();

    for class in ToolClass::all() {
        let name = class.binary_name();
        let env_var = class.env_var();
        let setting_supported = has_setting_key(class);

        let bundled_dir = fixture.bundled_exe_dir();
        let bundled_path = bundled_dir.join("bin").join(name);
        write_executable(&bundled_path)?;
        let setting_path = fixture.install("setting", class)?.join(name);
        let project_path = fixture.install("imported-project", class)?.join(name);
        let global_path = fixture.install("imported-global", class)?.join(name);
        let path_path = fixture.install("path", class)?.join(name);
        let env_path = fixture.install("env", class)?.join(name);

        let cases = [
            Case {
                label: "explicit setting wins over everything",
                setting: true,
                bundled: true,
                imported_project: true,
                imported_global: true,
                on_path: true,
                in_env: true,
                expected: if setting_supported {
                    Some(ToolSource::Config)
                } else {
                    Some(ToolSource::Bundled)
                },
            },
            Case {
                label: "bundled wins over imported vars, PATH and env",
                setting: false,
                bundled: true,
                imported_project: true,
                imported_global: true,
                on_path: true,
                in_env: true,
                expected: Some(ToolSource::Bundled),
            },
            Case {
                label: "imported project wins over imported global, PATH and env",
                setting: false,
                bundled: false,
                imported_project: true,
                imported_global: true,
                on_path: true,
                in_env: true,
                expected: Some(ToolSource::ImportedProject),
            },
            Case {
                label: "imported global wins over PATH and env",
                setting: false,
                bundled: false,
                imported_project: false,
                imported_global: true,
                on_path: true,
                in_env: true,
                expected: Some(ToolSource::ImportedGlobal),
            },
            Case {
                label: "PATH wins over env",
                setting: false,
                bundled: false,
                imported_project: false,
                imported_global: false,
                on_path: true,
                in_env: true,
                expected: Some(ToolSource::Path),
            },
            Case {
                label: "env is the last resort",
                setting: false,
                bundled: false,
                imported_project: false,
                imported_global: false,
                on_path: false,
                in_env: true,
                expected: Some(ToolSource::Env),
            },
            Case {
                label: "nothing staged reports missing",
                setting: false,
                bundled: false,
                imported_project: false,
                imported_global: false,
                on_path: false,
                in_env: false,
                expected: None,
            },
        ];

        for case in cases {
            let mut config = Config::default();
            if case.setting && setting_supported {
                set_setting(&mut config, class, setting_path.clone());
            }
            let exe_dir = if case.bundled {
                bundled_dir.clone()
            } else {
                fixture.absent_exe_dir()
            };
            let imported_vars = imported(
                case.imported_project
                    .then(|| project_path.display().to_string()),
                case.imported_global
                    .then(|| global_path.display().to_string()),
                env_var,
            );
            let mut pairs: Vec<(&str, String)> = Vec::new();
            if case.on_path {
                pairs.push(("PATH", fixture.root().join("path").display().to_string()));
            }
            if case.in_env {
                pairs.push((env_var, env_path.display().to_string()));
            }
            let env = if pairs.is_empty() {
                no_env()
            } else {
                Env::from_pairs(pairs)
            };

            let resolved = resolve_tool(class, &config, &env, &imported_vars, &exe_dir);
            if resolved.source != case.expected {
                failures.push(format!(
                    "{name} / {}: expected source {:?}, got {:?}",
                    case.label, case.expected, resolved.source
                ));
            }
            let wanted = resolved.source.map(|source| match source {
                ToolSource::Config => setting_path.clone(),
                ToolSource::Bundled => bundled_path.clone(),
                ToolSource::ImportedProject => project_path.clone(),
                ToolSource::ImportedGlobal => global_path.clone(),
                ToolSource::Path => path_path.clone(),
                ToolSource::Env => env_path.clone(),
            });
            match wanted {
                Some(wanted) => {
                    let found = resolved
                        .path
                        .as_deref()
                        .map(|path| path.canonicalize().unwrap_or_else(|_| path.to_path_buf()));
                    let wanted = wanted
                        .canonicalize()
                        .unwrap_or_else(|_| wanted.to_path_buf());
                    if found.as_deref() != Some(wanted.as_path()) {
                        failures.push(format!(
                            "{name} / {}: expected path {}, got {:?}",
                            case.label,
                            wanted.display(),
                            resolved.path
                        ));
                    }
                }
                None => {
                    if resolved.error.is_none() {
                        failures.push(format!("{name} / {}: expected an error", case.label));
                    }
                }
            }
            assert_stage_order(&resolved.attempts);
        }
    }

    assert!(
        failures.is_empty(),
        "resolution-order matrix failed:\n{}",
        failures.join("\n")
    );
    Ok(())
}

/// The written order is one declaration: the stages are distinct, declared in
/// precedence order, and every `ToolSource` maps onto it.
#[test]
fn the_written_order_is_declared_once() {
    assert_eq!(ResolutionStage::ORDER.len(), 7);
    let names: Vec<&str> = ResolutionStage::ORDER
        .iter()
        .map(|stage| stage.as_str())
        .collect();
    assert_eq!(
        names,
        vec![
            "setting",
            "bundled",
            "local",
            "imported-project",
            "imported-global",
            "PATH",
            "env"
        ],
        "the full seven-stage order, LocalCheckout included"
    );
    for (index, stage) in ResolutionStage::ORDER.iter().enumerate() {
        assert_eq!(
            stage.precedence(),
            index,
            "stage {} is not at its declared position",
            stage.as_str()
        );
        assert!(!stage.as_str().is_empty(), "every stage needs a label");
    }
    let sources = [
        ToolSource::Config,
        ToolSource::Bundled,
        ToolSource::ImportedProject,
        ToolSource::ImportedGlobal,
        ToolSource::Path,
        ToolSource::Env,
    ];
    for (index, source) in sources.iter().enumerate() {
        assert!(
            ResolutionStage::ORDER.contains(&source.stage()),
            "{source:?} maps outside the declared order"
        );
        if index > 0 {
            assert!(
                sources[index - 1].stage() < source.stage(),
                "{source:?} is declared out of precedence order"
            );
        }
    }
}

/// A missing explicit setting falls through and its failed attempt stays visible
/// (a stale `[verible]` path must not silently hide the bundled payload).
#[test]
fn a_missing_explicit_setting_falls_through_and_is_recorded() -> Result<(), Box<dyn Error>> {
    let fixture = Fixture::new()?;
    let class = ToolClass::VeribleLs;
    let bundled = fixture.bundled_exe_dir().join("bin/verible-verilog-ls");
    write_executable(&bundled)?;

    let mut config = Config::default();
    config.verible.ls = Some(fixture.root().join("does-not-exist"));
    let resolved = resolve_tool(
        class,
        &config,
        &no_env(),
        &ImportedVars::default(),
        &fixture.bundled_exe_dir(),
    );
    assert_eq!(resolved.source, Some(ToolSource::Bundled));
    assert_eq!(resolved.path.as_deref(), Some(bundled.as_path()));
    assert_eq!(resolved.attempts[0].source, ToolSource::Config);
    assert!(!resolved.attempts[0].found);
    Ok(())
}

/// The error for a class with no explicit setting still prints the stages it
/// actually probed, in order, for every attempted path.
#[test]
fn a_missing_vhdl_tool_explains_the_probed_stages() -> Result<(), Box<dyn Error>> {
    let fixture = Fixture::new()?;
    let class = ToolClass::VhdlLs;
    // A PATH directory and an env override that both miss: the class is missing
    // through the last stages, so the error must name all of them.
    let empty_path_dir = fixture.root().join("empty-path");
    std::fs::create_dir_all(&empty_path_dir)?;
    let env = Env::from_pairs([
        ("PATH", empty_path_dir.display().to_string()),
        (
            "VEDIT_VHDL_LS",
            fixture.root().join("missing/vhdl_ls").display().to_string(),
        ),
    ]);
    let resolved = resolve_tool(
        class,
        &Config::default(),
        &env,
        &ImportedVars::default(),
        &fixture.absent_exe_dir(),
    );
    assert!(resolved.path.is_none());
    let Some(error) = resolved.error else {
        return Err("a missing tool must carry an error".into());
    };
    assert!(error.contains("vhdl_ls"));
    assert!(
        error.contains("bundled -> PATH -> env"),
        "stages are printed in probe order: {error}"
    );
    for attempt in &resolved.attempts {
        assert!(
            error.contains(&attempt.path.display().to_string()),
            "every attempted path is listed: {}",
            attempt.path.display()
        );
    }
    Ok(())
}

/// A candidate that exists but is not executable must NOT be accepted: it would
/// shadow the later, runnable copy of the same tool on `PATH`.
#[test]
fn a_non_executable_path_candidate_never_shadows_an_executable_one() -> Result<(), Box<dyn Error>> {
    let fixture = Fixture::new()?;
    let class = ToolClass::VeribleLint;
    let shadow_dir = fixture.root().join("shadow");
    std::fs::create_dir_all(&shadow_dir)?;
    let shadow = shadow_dir.join(class.binary_name());
    std::fs::write(&shadow, "not executable\n")?;
    let real_dir = fixture.install("real", class)?;
    let real = real_dir.join(class.binary_name());
    let env = Env::from_pairs([(
        "PATH",
        format!("{}:{}", shadow_dir.display(), real_dir.display()),
    )]);
    let resolved = resolve_tool(
        class,
        &Config::default(),
        &env,
        &ImportedVars::default(),
        &fixture.absent_exe_dir(),
    );
    assert_eq!(resolved.source, Some(ToolSource::Path));
    assert_eq!(resolved.path.as_deref(), Some(real.as_path()));
    let Some(attempt) = resolved
        .attempts
        .iter()
        .find(|attempt| attempt.path == shadow)
    else {
        return Err("the shadowing candidate must be recorded as an attempt".into());
    };
    assert!(!attempt.found, "a non-executable file is not a hit");
    Ok(())
}
