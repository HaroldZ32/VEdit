//! Scripted Verible LSP session used to produce `evidence/lsp/` transcripts.
//!
//! ```text
//! cargo run -p vd-lsp --example lsp-probe -- [WORKSPACE_DIR]
//! ```
//!
//! It prints: the exact server argv, the `initialize` request/result JSON, a real
//! diagnostics round trip with its latency and the **derived** lint rule, a
//! definition lookup, and a formatting edit set compared byte-for-byte with the
//! bundled formatter. A fixture that does not contain the optional files prints
//! an explicit `SKIP` line instead of failing.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{anyhow, Context, Result};
use lsp_types::{Position, Url};
use vd_lsp::{apply_text_edits, diagnostic_rule, LspClient, LspConfig};

fn binary_dir() -> PathBuf {
    std::env::var("VEDIT_VERIBLE_BIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../.mpd/recon/verible/bin")
        })
}

fn uri(path: &Path) -> Result<Url> {
    Url::from_file_path(path).map_err(|()| anyhow!("{} is not a file path", path.display()))
}

#[tokio::main]
async fn main() -> Result<()> {
    let root = match std::env::args().nth(1) {
        Some(argument) => PathBuf::from(argument),
        None => PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("fixtures/project"),
    };
    let root = std::fs::canonicalize(&root)
        .with_context(|| format!("workspace root {} does not exist", root.display()))?;
    let ls = binary_dir().join("verible-verilog-ls");
    let formatter = binary_dir().join("verible-verilog-format");
    if !ls.is_file() {
        return Err(anyhow!(
            "missing {} (set VEDIT_VERIBLE_BIN_DIR)",
            ls.display()
        ));
    }

    println!("workspace_root={}", root.display());
    println!("ls_path={}", ls.display());
    // One configuration source for both the printed argv and the real client:
    // the rules file is passed exactly when the project has one (T13 policy).
    let rules = root.join(".rules.verible_lint");
    let mut config = LspConfig::new(&ls, &root);
    if rules.is_file() {
        config = config.with_rules_config(&rules);
    }
    let rules_label = if rules.is_file() {
        rules.display().to_string()
    } else {
        "<none>".to_string()
    };
    println!("rules_file={rules_label}");
    println!(
        "server_argv={:?}",
        config
            .command_args()
            .iter()
            .map(|argument| argument.to_string_lossy().to_string())
            .collect::<Vec<_>>()
    );

    let mut client = LspClient::new(config);
    println!("lazy_start_running={}", client.is_running());

    let bad_path = root.join("bad_line_length.sv");
    let bad_uri = uri(&bad_path)?;
    let bad_text = std::fs::read_to_string(&bad_path)?;
    let since = client.diagnostics_generation();
    let started = Instant::now();
    client
        .open_document(bad_uri.clone(), "systemverilog", &bad_text)
        .await?;
    let (params, result) = client.initialize_exchange_json();
    println!("\n## initialize request");
    println!(
        "{}",
        serde_json::to_string_pretty(&params.unwrap_or_default())?
    );
    println!("\n## initialize result (capabilities)");
    println!(
        "{}",
        serde_json::to_string_pretty(&result.unwrap_or_default())?
    );

    let published = client
        .wait_for_diagnostics(&bad_uri, since, Duration::from_secs(3))
        .await?;
    println!(
        "\n## diagnostics: {} item(s) after {}ms",
        published.diagnostics.len(),
        started.elapsed().as_millis()
    );
    for diagnostic in &published.diagnostics {
        println!(
            "  {}:{} severity={:?} derived_rule={:?} raw_code={:?} message={}",
            diagnostic.range.start.line + 1,
            diagnostic.range.start.character + 1,
            diagnostic.severity,
            diagnostic_rule(diagnostic),
            diagnostic.code,
            diagnostic.message
        );
    }

    let a_path = root.join("a.sv");
    let b_path = root.join("b.sv");
    if a_path.is_file() && b_path.is_file() {
        let a_uri = uri(&a_path)?;
        client
            .open_document(
                a_uri.clone(),
                "systemverilog",
                std::fs::read_to_string(&a_path)?,
            )
            .await?;
        let b_uri = uri(&b_path)?;
        client
            .open_document(
                b_uri.clone(),
                "systemverilog",
                std::fs::read_to_string(&b_path)?,
            )
            .await?;
        let definition = client
            .definition(
                &a_uri,
                Position {
                    line: 1,
                    character: 3,
                },
            )
            .await?;
        println!(
            "\n## definition of b_top in a.sv -> {} location(s)",
            definition.len()
        );
        for location in &definition {
            println!(
                "  {}:{}:{}",
                location.uri.path(),
                location.range.start.line + 1,
                location.range.start.character + 1
            );
        }
    } else {
        println!(
            "\n## definition: SKIP — this project has no a.sv/b.sv pair (by design, not a failure)"
        );
    }

    let messy_path = root.join("messy.sv");
    if messy_path.is_file() {
        let messy_uri = uri(&messy_path)?;
        let messy_text = std::fs::read_to_string(&messy_path)?;
        client
            .open_document(messy_uri.clone(), "systemverilog", messy_text.clone())
            .await?;
        let edits = client.formatting(&messy_uri, 2, true).await?;
        let applied = apply_text_edits(&messy_text, &edits)?;
        let output = tokio::process::Command::new(&formatter)
            .arg(&messy_path)
            .output()
            .await
            .context("running verible-verilog-format")?;
        let expected = String::from_utf8(output.stdout)?;
        println!(
            "\n## formatting: {} edit(s), byte_equal_to_bundled_formatter={}",
            edits.len(),
            applied == expected
        );
    } else {
        println!("\n## formatting: SKIP — this project has no messy.sv (by design, not a failure)");
    }

    let pid = client.pid();
    client.shutdown().await?;
    let still_present = pid
        .map(|pid| Path::new(&format!("/proc/{pid}")).exists())
        .unwrap_or(false);
    println!(
        "\n## shutdown: running={} pid={pid:?} process_still_present={still_present}",
        client.is_running()
    );
    Ok(())
}
