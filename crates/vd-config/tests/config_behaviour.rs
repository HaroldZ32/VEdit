//! End-to-end behaviour of the public configuration/project discovery API.

use std::path::{Path, PathBuf};

use anyhow::{bail, Result};
use vd_config::{render_table, to_json, Env, Loader, RootSource, Source, ValueView, WarningCode};

fn write(path: &Path, text: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(path, text)?;
    Ok(())
}

fn fixture() -> Result<(tempfile::TempDir, PathBuf, PathBuf, PathBuf)> {
    let temp = tempfile::tempdir()?;
    let home = temp.path().join("home");
    let project = temp.path().join("project");
    let exe_dir = temp.path().join("payload/usr/bin");
    std::fs::create_dir_all(&home)?;
    std::fs::create_dir_all(&project)?;
    std::fs::create_dir_all(&exe_dir)?;
    std::fs::create_dir_all(project.join(".git"))?;
    Ok((temp, home, project, exe_dir))
}

fn loader(home: &Path, project: &Path, exe_dir: &Path, extra: &[(&str, &str)]) -> Loader {
    let mut pairs: Vec<(String, String)> = vec![("HOME".to_string(), home.display().to_string())];
    for (key, value) in extra {
        pairs.push(((*key).to_string(), (*value).to_string()));
    }
    Loader::new(project)
        .with_env(Env::from_pairs(pairs))
        .with_exe_dir(exe_dir)
}

fn user_config(home: &Path) -> PathBuf {
    home.join(".config/vedit/vedit.toml")
}

fn project_config(project: &Path) -> PathBuf {
    project.join("vedit.toml")
}

fn entry<'a>(loaded: &'a vd_config::Loaded, key: &str) -> Result<&'a vd_config::Entry> {
    match loaded.entries.iter().find(|entry| entry.key == key) {
        Some(entry) => Ok(entry),
        None => bail!("missing provenance entry `{key}`"),
    }
}

#[test]
fn defaults_apply_when_nothing_is_configured() -> Result<()> {
    let (temp, home, project, exe_dir) = fixture()?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.config.ui.language, "zh-CN");
    assert_eq!(loaded.config.ui.theme, "cobalt");
    assert_eq!(loaded.config.editor.tab_width, 4);
    assert!(!loaded.config.fim.enabled);
    assert!(loaded.config.fim.endpoint.is_empty());
    assert!(loaded.config.fim.api_key.is_none());
    assert_eq!(loaded.project.root, std::path::absolute(&project)?);
    assert_eq!(loaded.project.root_source, RootSource::GitDir);

    let language = entry(&loaded, "ui.language")?;
    assert_eq!(language.source, Source::Default);
    assert_eq!(
        language.value,
        ValueView::Text {
            text: "zh-CN".to_string()
        }
    );
    assert!(loaded
        .warnings
        .iter()
        .all(|warning| warning.code != WarningCode::ConfigParseFailed));
    assert!(temp.path().exists());
    Ok(())
}

#[test]
fn the_user_layer_applies_from_the_xdg_config_home() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &user_config(&home),
        "[editor]\ntab_width = 2\n[ui]\ntheme = \"kate\"\n",
    )?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;
    assert_eq!(loaded.config.editor.tab_width, 2);
    assert_eq!(loaded.config.ui.theme, "kate");
    assert_eq!(entry(&loaded, "editor.tab_width")?.source, Source::User);
    Ok(())
}

#[test]
fn the_project_layer_overrides_the_user_layer_per_key_and_inherits_the_rest() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &user_config(&home),
        "[editor]\ntab_width = 2\ninsert_spaces = false\n",
    )?;
    write(&project_config(&project), "[editor]\ntab_width = 8\n")?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.config.editor.tab_width, 8, "project wins");
    assert!(!loaded.config.editor.insert_spaces, "user value inherited");

    let width = entry(&loaded, "editor.tab_width")?;
    assert_eq!(width.source, Source::Project);
    assert_eq!(width.overridden_by, Some(Source::User));
    assert_eq!(width.shadowed, vec![Source::Default, Source::User]);
    assert_eq!(entry(&loaded, "editor.insert_spaces")?.source, Source::User);
    Ok(())
}

#[test]
fn an_invalid_configuration_file_degrades_to_the_lower_layer() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(&user_config(&home), "[editor]\ntab_width = 2\n")?;
    write(&project_config(&project), "[editor\ntab_width = 8\n")?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.config.editor.tab_width, 2, "user layer survives");
    let warning = loaded
        .warnings
        .iter()
        .find(|warning| warning.code == WarningCode::ConfigParseFailed)
        .ok_or_else(|| anyhow::anyhow!("no config.parse_failed warning"))?;
    assert_eq!(
        warning.file.as_deref(),
        Some(project_config(&project).as_path())
    );
    assert!(warning.line.is_some());
    Ok(())
}

#[test]
fn an_unknown_key_is_ignored_while_the_rest_of_the_file_applies() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &project_config(&project),
        "[editor]\ntab_width = 6\nwibble = 1\n",
    )?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.config.editor.tab_width, 6);
    let warning = loaded
        .warnings
        .iter()
        .find(|warning| warning.code == WarningCode::ConfigUnknownKey)
        .ok_or_else(|| anyhow::anyhow!("no config.unknown_key warning"))?;
    assert!(warning.message.contains("editor.wibble"));
    Ok(())
}

#[test]
fn environment_variables_override_fim_settings_and_record_their_source() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &project_config(&project),
        "[fim]\nenabled = true\nendpoint = \"https://config.local/v1\"\nmodel = \"m1\"\n",
    )?;
    let loaded = loader(
        &home,
        &project,
        &exe_dir,
        &[
            ("VEDIT_FIM_ENDPOINT", "https://env.local/v1"),
            ("VEDIT_FIM_MODEL", "m2"),
        ],
    )
    .load()?;

    assert_eq!(loaded.config.fim.endpoint, "https://env.local/v1");
    assert_eq!(loaded.config.fim.model, "m2");
    assert!(loaded.config.fim.enabled, "config-only value survives");

    let endpoint = entry(&loaded, "fim.endpoint")?;
    assert_eq!(endpoint.source, Source::Env);
    assert_eq!(endpoint.overridden_by, Some(Source::Project));
    assert_eq!(entry(&loaded, "fim.enabled")?.source, Source::Project);
    Ok(())
}

#[test]
fn project_discovery_finds_filelists_rules_includes_defines_and_upward_rules() -> Result<()> {
    let (temp, home, project, exe_dir) = fixture()?;
    write(
        &project.join("top.f"),
        "rtl/a.sv\n+incdir+inc\n+define+FOO=1\n",
    )?;
    let parent_rules = temp.path().join(".rules.verible_lint");
    write(&parent_rules, "line-length: 120\n")?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.project.root, std::path::absolute(&project)?);
    assert_eq!(loaded.project.root_source, RootSource::GitDir);
    assert_eq!(loaded.project.filelists.len(), 1);
    assert_eq!(loaded.project.filelists[0].path, project.join("top.f"));
    assert_eq!(
        loaded.project.filelists[0].entries,
        vec![project.join("rtl/a.sv")]
    );
    assert_eq!(loaded.project.include_dirs, vec![project.join("inc")]);
    assert_eq!(loaded.project.defines.len(), 1);
    assert_eq!(loaded.project.defines[0].name, "FOO");
    assert_eq!(loaded.project.defines[0].value.as_deref(), Some("1"));
    assert_eq!(
        loaded.project.rules_path.as_deref(),
        Some(parent_rules.as_path())
    );
    assert_eq!(loaded.project.rules_search_stop, temp.path());
    Ok(())
}

#[test]
fn configured_filelists_and_project_settings_are_merged_with_discovered_ones() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(&project.join("auto.f"), "auto.sv\n")?;
    write(&project.join("extra.f"), "extra.sv\n")?;
    write(
        &project_config(&project),
        "[project]\nfilelists = [\"extra.f\"]\ninclude_dirs = [\"cfg_inc\"]\ndefines = [\"BAR\", \"BAZ=2\"]\n",
    )?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    let paths: Vec<PathBuf> = loaded
        .project
        .filelists
        .iter()
        .map(|filelist| filelist.path.clone())
        .collect();
    assert_eq!(paths, vec![project.join("auto.f"), project.join("extra.f")]);
    assert_eq!(loaded.project.include_dirs, vec![project.join("cfg_inc")]);
    assert!(loaded
        .project
        .defines
        .iter()
        .any(|define| define.name == "BAR" && define.value.is_none()));
    assert!(loaded
        .project
        .defines
        .iter()
        .any(|define| define.name == "BAZ" && define.value.as_deref() == Some("2")));
    Ok(())
}

#[test]
fn a_relative_configured_tool_path_is_resolved_against_the_project_root() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    let binary = project.join("tools/verible-verilog-ls");
    write(&binary, "#!/bin/sh\nexit 0\n")?;
    write(
        &project_config(&project),
        "[verible]\nls = \"tools/verible-verilog-ls\"\n",
    )?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.tools.ls.path.as_deref(), Some(binary.as_path()));
    assert_eq!(loaded.tools.ls.source, Some(vd_config::ToolSource::Config));
    let entry = entry(&loaded, "verible.ls")?;
    assert_eq!(
        entry.value,
        ValueView::Text {
            text: binary.display().to_string()
        }
    );
    Ok(())
}

#[test]
fn tool_resolution_records_every_attempt_even_on_success() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    let bundled = exe_dir
        .parent()
        .map(|usr| usr.join("lib/vedit/bin/verible-verilog-ls"))
        .ok_or_else(|| anyhow::anyhow!("no payload dir"))?;
    write(&bundled, "#!/bin/sh\nexit 0\n")?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    assert_eq!(loaded.tools.ls.path.as_deref(), Some(bundled.as_path()));
    assert_eq!(loaded.tools.ls.source, Some(vd_config::ToolSource::Bundled));
    assert!(!loaded.tools.ls.attempts.is_empty());
    assert!(loaded.tools.ls.error.is_none());

    assert!(loaded.tools.format.path.is_none());
    assert!(loaded.tools.format.attempts.len() >= 3);
    let error = loaded
        .tools
        .format
        .error
        .as_deref()
        .ok_or_else(|| anyhow::anyhow!("missing tool must carry an error"))?;
    assert!(error.contains("PATH is never consulted"));

    let json = to_json(&loaded);
    let attempts = json["tools"]["ls"]["attempts"]
        .as_array()
        .ok_or_else(|| anyhow::anyhow!("tools.ls.attempts is not an array"))?;
    assert!(!attempts.is_empty());
    Ok(())
}

#[test]
fn a_relative_xdg_variable_is_reported_and_the_default_is_used() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(&user_config(&home), "[editor]\ntab_width = 3\n")?;
    let loaded = loader(
        &home,
        &project,
        &exe_dir,
        &[("XDG_CONFIG_HOME", "relative/dir")],
    )
    .load()?;
    assert_eq!(loaded.config.editor.tab_width, 3, "default path still read");
    assert!(loaded
        .warnings
        .iter()
        .any(|warning| warning.code == WarningCode::XdgInvalid));
    Ok(())
}

#[test]
fn the_two_renderings_agree_and_stay_safe() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(&project_config(&project), "[editor]\ntab_width = 7\n")?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;
    let table = render_table(&loaded);
    let json = to_json(&loaded);
    assert_eq!(json["schema_version"], serde_json::json!(1));
    assert!(table.contains("editor.tab_width"));
    assert!(table.contains("source=project"));
    assert_eq!(json["config"]["editor"]["tab_width"], serde_json::json!(7));
    Ok(())
}
