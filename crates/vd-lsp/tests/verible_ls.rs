//! Integration tests against the real bundled `verible-verilog-ls`.
//!
//! Acceptance evidence for AC-06 (diagnostics), AC-07 (formatting byte
//! equality), AC-08 (navigation/symbols/rename) and AC-09 (lifecycle, restart,
//! no zombie). Requires the Verible binaries: `VEDIT_VERIBLE_BIN_DIR` or the
//! repository recon directory.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Result};
use lsp_types::{DocumentSymbolResponse, Position, Range, TextDocumentContentChangeEvent, Url};
use vd_lsp::{apply_text_edits, CancelHandle, LspClient, LspConfig};

const DIAGNOSTIC_BUDGET: Duration = Duration::from_secs(3);

fn binary(name: &str) -> PathBuf {
    let directory = std::env::var("VEDIT_VERIBLE_BIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../.mpd/recon/verible/bin")
        });
    let path = directory.join(name);
    assert!(
        path.is_file(),
        "missing Verible binary at {} — set VEDIT_VERIBLE_BIN_DIR",
        path.display()
    );
    path
}

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("fixtures")
        .join(name)
}

fn file_uri(path: &Path) -> Result<Url> {
    Url::from_file_path(path).map_err(|()| anyhow!("{} is not a file path", path.display()))
}

fn client_for(root: &Path) -> LspClient {
    LspClient::new(LspConfig::new(binary("verible-verilog-ls"), root))
}

async fn open(client: &mut LspClient, path: &Path) -> Result<Url> {
    let uri = file_uri(path)?;
    let text = std::fs::read_to_string(path)?;
    client
        .open_document(uri.clone(), "systemverilog", text)
        .await?;
    Ok(uri)
}

fn position_at_end(text: &str) -> Position {
    let line = text.matches('\n').count() as u32;
    let last_line = text.rsplit('\n').next().unwrap_or("");
    Position {
        line,
        character: last_line.encode_utf16().count() as u32,
    }
}

fn process_alive(pid: u32) -> bool {
    Path::new(&format!("/proc/{pid}")).exists()
}

fn wait_gone(pid: u32, budget: Duration) -> Result<()> {
    let deadline = Instant::now() + budget;
    while Instant::now() < deadline {
        if !process_alive(pid) {
            return Ok(());
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    bail!("process {pid} is still present after {budget:?} (zombie or leaked child)")
}

#[tokio::test]
async fn capabilities_are_the_measured_ten_and_start_is_lazy() -> Result<()> {
    let root = fixture("project");
    let mut client = client_for(&root);
    assert!(
        !client.is_running(),
        "lazy start: no process before the first call"
    );
    assert!(client.pid().is_none());

    let top = root.join("top.sv");
    open(&mut client, &top).await?;
    assert!(client.is_running());
    assert!(client.pid().is_some());

    let capabilities = client
        .server_capabilities()
        .ok_or_else(|| anyhow!("no server capabilities"))?;
    assert!(capabilities.definition_provider.is_some());
    assert!(capabilities.references_provider.is_some());
    assert!(capabilities.document_symbol_provider.is_some());
    assert!(capabilities.rename_provider.is_some());
    assert!(capabilities.document_highlight_provider.is_some());
    assert!(capabilities.document_formatting_provider.is_some());
    assert!(capabilities.document_range_formatting_provider.is_some());
    assert!(capabilities.code_action_provider.is_some());
    assert!(
        capabilities.completion_provider.is_none(),
        "verible has no completion; FIM is the only completion source"
    );
    assert!(capabilities.signature_help_provider.is_none());
    let sync = capabilities
        .text_document_sync
        .as_ref()
        .ok_or_else(|| anyhow!("no textDocumentSync"))?;
    let incremental = match sync {
        lsp_types::TextDocumentSyncCapability::Kind(kind) => {
            *kind == lsp_types::TextDocumentSyncKind::INCREMENTAL
        }
        lsp_types::TextDocumentSyncCapability::Options(options) => {
            options.change == Some(lsp_types::TextDocumentSyncKind::INCREMENTAL)
        }
    };
    assert!(incremental, "incremental sync expected, got {sync:?}");

    let (params, result) = client.initialize_exchange_json();
    assert!(
        params.is_some() && result.is_some(),
        "initialize transcript"
    );

    client.shutdown().await?;
    assert!(!client.is_running());
    Ok(())
}

#[tokio::test]
async fn diagnostics_arrive_within_three_seconds_and_clear_after_a_fix() -> Result<()> {
    let root = fixture("project");
    let mut client = client_for(&root);
    let path = root.join("bad_line_length.sv");
    let uri = file_uri(&path)?;
    let text = std::fs::read_to_string(&path)?;

    let since = client.diagnostics_generation();
    let started = Instant::now();
    client
        .open_document(uri.clone(), "systemverilog", text.clone())
        .await?;
    let published = client
        .wait_for_diagnostics(&uri, since, DIAGNOSTIC_BUDGET)
        .await?;
    let elapsed = started.elapsed();
    assert!(
        elapsed < DIAGNOSTIC_BUDGET,
        "diagnostics took {elapsed:?} (budget {DIAGNOSTIC_BUDGET:?})"
    );
    let line_violation = published.diagnostics.iter().find(|diagnostic| {
        diagnostic.range.start.line == 2 && diagnostic.message.to_lowercase().contains("length")
    });
    assert!(
        line_violation.is_some(),
        "expected a line-length diagnostic on line 2, got {:?}",
        published.diagnostics
    );

    let fixed =
        "// line-length violation fixture\nmodule bad_line_length;\n  wire ok;\nendmodule\n";
    let change = TextDocumentContentChangeEvent {
        range: Some(Range {
            start: Position {
                line: 0,
                character: 0,
            },
            end: position_at_end(&text),
        }),
        range_length: None,
        text: fixed.to_string(),
    };
    let since = client.diagnostics_generation();
    client.change_document(&uri, vec![change]).await?;
    let repaired = client
        .wait_for_diagnostics(&uri, since, DIAGNOSTIC_BUDGET)
        .await?;
    assert!(
        repaired.diagnostics.is_empty(),
        "the line-length diagnostic must disappear after the fix, got {:?}",
        repaired.diagnostics
    );
    let document = client
        .document(&uri)?
        .ok_or_else(|| anyhow!("document tracking lost bad_line_length.sv"))?;
    assert_eq!(document.text, fixed);
    assert_eq!(document.version, 2);
    client.shutdown().await?;
    Ok(())
}

#[tokio::test]
async fn definition_references_symbols_rename_and_highlight() -> Result<()> {
    let root = fixture("project");
    let mut client = client_for(&root);
    let a = root.join("a.sv");
    let b = root.join("b.sv");
    let a_uri = open(&mut client, &a).await?;
    let b_uri = open(&mut client, &b).await?;

    let definition_position = Position {
        line: 1,
        character: 3,
    };
    let locations = client.definition(&a_uri, definition_position).await?;
    assert!(
        locations.iter().any(|location| location.uri == b_uri),
        "definition of b_top must land in b.sv, got {locations:?}"
    );

    let symbol_position = Position {
        line: 0,
        character: 8,
    };
    let references = client.references(&b_uri, symbol_position, true).await?;
    assert!(
        references.iter().any(|location| location.uri == a_uri),
        "references of b_top must include a.sv, got {references:?}"
    );

    let symbols = client.document_symbols(&b_uri).await?;
    let names = symbol_names(&symbols);
    assert!(
        names.iter().any(|name| name == "b_top"),
        "document symbols must contain b_top, got {names:?}"
    );

    let edit = client
        .rename(&b_uri, symbol_position, "b_renamed")
        .await?
        .ok_or_else(|| anyhow!("rename returned no workspace edit"))?;
    let touches_a = edit
        .changes
        .as_ref()
        .is_some_and(|changes| changes.keys().any(|uri| uri == &a_uri));
    assert!(
        touches_a,
        "rename must also touch the instantiation in a.sv"
    );

    let highlights = client.document_highlight(&b_uri, symbol_position).await?;
    assert!(!highlights.is_empty(), "documentHighlight returned nothing");
    client.shutdown().await?;
    Ok(())
}

fn symbol_names(response: &Option<DocumentSymbolResponse>) -> Vec<String> {
    match response {
        Some(DocumentSymbolResponse::Flat(items)) => {
            items.iter().map(|symbol| symbol.name.clone()).collect()
        }
        Some(DocumentSymbolResponse::Nested(items)) => {
            items.iter().map(|symbol| symbol.name.clone()).collect()
        }
        None => Vec::new(),
    }
}

#[tokio::test]
async fn formatting_edits_reproduce_the_bundled_formatter_byte_for_byte() -> Result<()> {
    let root = fixture("project");
    let mut client = client_for(&root);
    let path = root.join("messy.sv");
    let uri = file_uri(&path)?;
    let text = std::fs::read_to_string(&path)?;
    client
        .open_document(uri.clone(), "systemverilog", text.clone())
        .await?;

    let edits = client.formatting(&uri, 2, true).await?;
    assert!(!edits.is_empty(), "formatting returned no edits");
    let applied = apply_text_edits(&text, &edits)?;

    let output = tokio::process::Command::new(binary("verible-verilog-format"))
        .arg(&path)
        .output()
        .await?;
    assert!(
        output.status.success(),
        "formatter failed: {:?}",
        output.status
    );
    let expected = String::from_utf8(output.stdout)?;
    assert_eq!(
        applied, expected,
        "LSP formatting must be byte-identical to the bundled verible-verilog-format"
    );

    let range = Range {
        start: Position {
            line: 2,
            character: 0,
        },
        end: Position {
            line: 4,
            character: 9,
        },
    };
    let range_edits = client.range_formatting(&uri, range, 2, true).await?;
    let _ = apply_text_edits(&text, &range_edits)?;
    let _ = client.code_actions(&uri, range, Vec::new()).await?;
    client.shutdown().await?;
    Ok(())
}

#[tokio::test]
async fn rules_config_is_passed_explicitly_and_changes_diagnostics() -> Result<()> {
    let plain_root = fixture("project");
    let mut plain = client_for(&plain_root);
    let plain_uri = open(&mut plain, &plain_root.join("bad_line_length.sv")).await?;
    let since = plain.diagnostics_generation();
    let published = plain
        .wait_for_diagnostics(&plain_uri, since, DIAGNOSTIC_BUDGET)
        .await?;
    assert!(
        published
            .diagnostics
            .iter()
            .any(|diagnostic| diagnostic.message.to_lowercase().contains("length")),
        "the default ruleset must report line-length"
    );
    plain.shutdown().await?;

    let rules_root = fixture("project_with_rules");
    let rules = rules_root.join(".rules.verible_lint");
    let config =
        LspConfig::new(binary("verible-verilog-ls"), &rules_root).with_rules_config(&rules);
    assert_eq!(
        config.command_args(),
        vec![std::ffi::OsString::from(format!(
            "--rules_config={}",
            rules.display()
        ))],
        "the rules file is passed explicitly and no search flag is added"
    );
    let mut with_rules = LspClient::new(config);
    let rules_uri = open(&mut with_rules, &rules_root.join("bad_line_length.sv")).await?;
    let since = with_rules.diagnostics_generation();
    let published = with_rules
        .wait_for_diagnostics(&rules_uri, since, DIAGNOSTIC_BUDGET)
        .await?;
    assert!(
        published
            .diagnostics
            .iter()
            .all(|diagnostic| !diagnostic.message.to_lowercase().contains("length")),
        "line-length must be disabled by the explicit rules file, got {:?}",
        published.diagnostics
    );
    with_rules.shutdown().await?;
    Ok(())
}

#[tokio::test]
async fn lazy_start_crash_restart_and_shutdown_leave_no_zombie() -> Result<()> {
    let root = fixture("project");
    let mut client = client_for(&root);
    assert!(!client.is_running());

    let top = root.join("top.sv");
    let uri = open(&mut client, &top).await?;
    let first_pid = client.pid().ok_or_else(|| anyhow!("no pid after start"))?;
    assert!(process_alive(first_pid));

    let status = std::process::Command::new("kill")
        .args(["-9", &first_pid.to_string()])
        .status()?;
    assert!(status.success(), "kill -9 failed");

    let deadline = Instant::now() + Duration::from_secs(3);
    while client.is_running() && Instant::now() < deadline {
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(
        !client.is_running(),
        "the client must notice the dead server"
    );

    let symbols = client.document_symbols(&uri).await?;
    assert!(client.restart_count() >= 1, "restart must be counted");
    let second_pid = client
        .pid()
        .ok_or_else(|| anyhow!("no pid after restart"))?;
    assert_ne!(second_pid, first_pid, "restart must use a new process");
    let document = client
        .document(&uri)?
        .ok_or_else(|| anyhow!("document lost across restart"))?;
    assert_eq!(document.text, std::fs::read_to_string(&top)?);
    assert!(symbols.is_some(), "restart must answer documentSymbol");

    client.shutdown().await?;
    assert!(!client.is_running());
    wait_gone(first_pid, Duration::from_secs(3))?;
    wait_gone(second_pid, Duration::from_secs(3))?;
    Ok(())
}

#[tokio::test]
async fn request_timeout_and_cancellation_are_enforced() -> Result<()> {
    let scratch = Scratch::new()?;
    // A file large enough that formatting takes well over the 1 ms budget.
    let mut large = String::new();
    for index in 0..20_000 {
        large.push_str(&format!("module m{index};endmodule\n"));
    }
    let large_path = scratch.path.join("large.sv");
    std::fs::write(&large_path, &large)?;

    let config = LspConfig::new(binary("verible-verilog-ls"), &scratch.path).with_timeouts(
        Duration::from_secs(10),
        Duration::from_millis(1),
        Duration::from_secs(5),
    );
    let mut client = LspClient::new(config);
    let uri = open(&mut client, &large_path).await?;

    let timeout = client.formatting(&uri, 2, true).await;
    let message = timeout
        .err()
        .map(|error| error.to_string())
        .unwrap_or_default();
    assert!(
        message.contains("timed out"),
        "expected a request timeout, got `{message}`"
    );

    let (handle, mut token) = CancelHandle::new();
    handle.cancel();
    let cancelled = client
        .definition_cancellable(
            &uri,
            Position {
                line: 0,
                character: 8,
            },
            &mut token,
        )
        .await;
    let message = cancelled
        .err()
        .map(|error| error.to_string())
        .unwrap_or_default();
    assert!(
        message.contains("cancelled"),
        "expected a cancellation, got `{message}`"
    );
    client.shutdown().await?;
    Ok(())
}

struct Scratch {
    path: PathBuf,
}

impl Scratch {
    fn new() -> Result<Self> {
        let path = std::env::temp_dir().join(format!(
            "vd-lsp-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&path)?;
        Ok(Self { path })
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        if let Err(error) = std::fs::remove_dir_all(&self.path) {
            eprintln!(
                "could not remove scratch dir {}: {error}",
                self.path.display()
            );
        }
    }
}

#[tokio::test]
async fn initialize_timeout_is_enforced_and_the_child_is_reaped() -> Result<()> {
    use std::os::unix::fs::PermissionsExt;

    let scratch = Scratch::new()?;
    let script = scratch.path.join("hanging-ls.sh");
    std::fs::write(&script, "#!/bin/sh\nexec sleep 30\n")?;
    let mut permissions = std::fs::metadata(&script)?.permissions();
    permissions.set_mode(0o755);
    std::fs::set_permissions(&script, permissions)?;

    let config = LspConfig::new(&script, &scratch.path).with_timeouts(
        Duration::from_millis(300),
        Duration::from_secs(1),
        Duration::from_millis(500),
    );
    let mut client = LspClient::new(config);
    let uri = file_uri(&scratch.path.join("x.sv"))?;
    let error = client
        .open_document(uri, "systemverilog", "module x; endmodule\n")
        .await;
    let message = error
        .err()
        .map(|error| error.to_string())
        .unwrap_or_default();
    assert!(
        message.contains("initialize timed out"),
        "expected an initialize timeout, got `{message}`"
    );
    let pid = client.pid();
    client.shutdown().await?;
    if let Some(pid) = pid {
        wait_gone(pid, Duration::from_secs(3))?;
    }
    Ok(())
}
