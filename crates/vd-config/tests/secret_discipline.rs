//! AC-15: secret material never reaches a log line, a rendering, `Debug` output
//! or a warning message.

use std::path::{Path, PathBuf};

use anyhow::{bail, Result};
use vd_config::{render_table, to_json, Env, Loader, Source, ValueView};

const ENV_SECRET: &str = "test-env-secret-987654";
const CONFIG_SECRET: &str = "test-config-secret-123456";
const BROKEN_SECRET: &str = "test-broken-secret-555555";

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

fn assert_secret_free(rendering: &str, label: &str, secrets: &[&str]) {
    for secret in secrets {
        assert!(
            !rendering.contains(secret),
            "{label} leaked secret material `{secret}`"
        );
    }
}

#[test]
fn a_configured_and_an_environment_secret_never_leave_the_config_layer() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &project.join("vedit.toml"),
        &format!(
            "[fim]\nenabled = true\nendpoint = \"https://internal.example/v1\"\nmodel = \"m\"\napi_key = \"{CONFIG_SECRET}\"\n"
        ),
    )?;
    let loaded = loader(
        &home,
        &project,
        &exe_dir,
        &[("VEDIT_FIM_API_KEY", ENV_SECRET)],
    )
    .load()?;

    let key = loaded
        .config
        .fim
        .api_key
        .as_ref()
        .ok_or_else(|| anyhow::anyhow!("the key must be configured"))?;
    assert_eq!(key.expose_secret(), ENV_SECRET, "environment wins");

    let table = render_table(&loaded);
    let json = serde_json::to_string(&to_json(&loaded))?;
    let debug_loaded = format!("{loaded:?}");
    let debug_config = format!("{:?}", loaded.config);
    let debug_key = format!("{key:?}");
    let warning_json = serde_json::to_string(&loaded.warnings)?;
    for (label, rendering) in [
        ("table", table.as_str()),
        ("json", json.as_str()),
        ("debug(Loaded)", debug_loaded.as_str()),
        ("debug(Config)", debug_config.as_str()),
        ("debug(ApiKey)", debug_key.as_str()),
        ("warnings", warning_json.as_str()),
    ] {
        assert_secret_free(rendering, label, &[ENV_SECRET, CONFIG_SECRET]);
    }

    let entry = loaded
        .entries
        .iter()
        .find(|entry| entry.key == "fim.api_key")
        .ok_or_else(|| anyhow::anyhow!("missing fim.api_key provenance entry"))?;
    assert_eq!(entry.value, ValueView::Redacted { set: true });
    assert_eq!(entry.source, Source::Env);
    assert!(table.contains("<redacted>"));
    assert_eq!(
        to_json(&loaded)["config"]["fim"]["api_key"],
        serde_json::json!("<redacted>")
    );
    Ok(())
}

#[test]
fn a_rejected_configuration_file_never_quotes_its_secret_in_the_warning() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    write(
        &project.join("vedit.toml"),
        &format!("[fim]\napi_key = \"{BROKEN_SECRET}\n"),
    )?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;

    let warning = loaded
        .warnings
        .iter()
        .find(|warning| warning.code == vd_config::WarningCode::ConfigParseFailed)
        .ok_or_else(|| anyhow::anyhow!("expected a config.parse_failed warning"))?;
    assert_secret_free(&warning.message, "warning message", &[BROKEN_SECRET]);
    let table = render_table(&loaded);
    let json = serde_json::to_string(&to_json(&loaded))?;
    assert_secret_free(&table, "table", &[BROKEN_SECRET]);
    assert_secret_free(&json, "json", &[BROKEN_SECRET]);
    assert!(
        loaded.config.fim.api_key.is_none(),
        "rejected file applies nothing"
    );
    Ok(())
}

#[test]
fn the_default_configuration_carries_no_endpoint_or_key() -> Result<()> {
    let (_temp, home, project, exe_dir) = fixture()?;
    let loaded = loader(&home, &project, &exe_dir, &[]).load()?;
    assert!(!loaded.config.fim.enabled);
    assert!(loaded.config.fim.endpoint.is_empty());
    assert!(loaded.config.fim.model.is_empty());
    assert!(loaded.config.fim.api_key.is_none());
    let table = render_table(&loaded);
    assert!(table.contains("fim.enabled"));
    assert!(
        !table.contains("fim.api_key"),
        "no secret key is reported unset by default"
    );
    let json = to_json(&loaded);
    assert!(json["config"]["fim"].get("api_key").is_none());
    Ok(())
}

#[test]
fn the_secret_is_only_reachable_through_the_explicit_accessor() -> Result<()> {
    use vd_config::ApiKey;

    let key = ApiKey::new(ENV_SECRET);
    assert_eq!(key.expose_secret(), ENV_SECRET);
    assert_secret_free(&format!("{key:?}"), "Debug", &[ENV_SECRET]);
    let serialized = serde_json::to_string(&key)?;
    assert_secret_free(&serialized, "Serialize", &[ENV_SECRET]);
    assert_eq!(serialized, "\"<redacted>\"");
    if serialized.contains(ENV_SECRET) {
        bail!("serialization leaked the secret");
    }
    Ok(())
}
