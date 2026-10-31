//! A3c/A3d at the client level: the effective fixture file set is opened on the
//! real bundled Verible language server, and the session reports observable
//! state after the set is handed in and after it is replaced.
//!
//! The expected file set is the checked-in `tests/fixtures/verilog-project/
//! expected/files.txt` (produced by the project model's `.f` semantics and
//! verified by `scripts/verify-fixture.sh`). The server binary comes from
//! `VEDIT_VERIBLE_BIN_DIR` or the repository recon directory; when it is absent
//! the test prints an explicit `SKIP:` line and returns successfully — it never
//! pretends the interaction happened.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use vd_lsp::{LspConfig, PollOutcome, SessionRequest, SessionResponse, SessionStatus};

const DIAGNOSTICS_BUDGET: Duration = Duration::from_secs(5);
const START_BUDGET: Duration = Duration::from_secs(45);
const REQUEST_BUDGET: Duration = Duration::from_secs(20);

fn binary() -> Option<PathBuf> {
    let directory = std::env::var("VEDIT_VERIBLE_BIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../.mpd/recon/verible/bin")
        });
    let path = directory.join("verible-verilog-ls");
    if path.is_file() {
        Some(path)
    } else {
        None
    }
}

fn fixture_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/verilog-project")
}

fn effective_files() -> Result<Vec<PathBuf>> {
    let root = fixture_root();
    let expected = std::fs::read_to_string(root.join("expected/files.txt"))?;
    Ok(expected
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(|relative| root.join(relative))
        .collect())
}

fn wait_ready(session: &vd_lsp::SessionHandle, budget: Duration) -> Result<SessionStatus> {
    let deadline = Instant::now() + budget;
    loop {
        let status = session.status();
        match status.phase {
            vd_lsp::SessionPhase::Ready => return Ok(status),
            vd_lsp::SessionPhase::Failed => bail!("session failed: {}", status.message),
            vd_lsp::SessionPhase::Starting => {}
            vd_lsp::SessionPhase::Stopped => bail!("session stopped before it became ready"),
        }
        if Instant::now() >= deadline {
            bail!("session did not become ready within {budget:?}: {status:?}");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn poll_response(
    session: &vd_lsp::SessionHandle,
    request: SessionRequest,
    budget: Duration,
) -> Result<SessionResponse> {
    let id = session.request(request)?;
    let deadline = Instant::now() + budget;
    loop {
        match session.poll(id) {
            PollOutcome::Ready(response) => return Ok(response),
            PollOutcome::Failed(message) => bail!("request failed: {message}"),
            PollOutcome::Pending => {}
            PollOutcome::Unknown => bail!("request id {id} vanished before it was answered"),
        }
        if Instant::now() >= deadline {
            bail!("request {id} was not answered within {budget:?}");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

/// Zero-based line and UTF-16 character of the first occurrence of `needle`.
fn position_of(text: &str, needle: &str) -> Result<(u32, u32)> {
    let Some(offset) = text.find(needle) else {
        bail!("{needle:?} is not in the fixture text");
    };
    let before = &text[..offset];
    let line = before.matches('\n').count() as u32;
    let line_prefix = before.rsplit('\n').next().unwrap_or("");
    let character = line_prefix.encode_utf16().count() as u32;
    Ok((line, character))
}

fn open_session(ls: &Path, files: &[PathBuf]) -> Result<vd_lsp::SessionHandle> {
    let root = fixture_root();
    let config = LspConfig::new(ls, &root);
    vd_lsp::SessionHandle::with_budgets(
        config,
        files.to_vec(),
        DIAGNOSTICS_BUDGET,
        Duration::from_secs(5),
    )
}

#[test]
fn effective_file_set_is_opened_and_navigation_outline_format_answer() -> Result<()> {
    let Some(ls) = binary() else {
        println!(
            "SKIP: verible-verilog-ls is not available (set VEDIT_VERIBLE_BIN_DIR or run scripts/fetch-verible.sh)"
        );
        return Ok(());
    };
    let files = effective_files()?;
    assert_eq!(
        files.len(),
        7,
        "the fixture expectation is seven source files"
    );
    let session = open_session(&ls, &files)?;
    let status = wait_ready(&session, START_BUDGET)?;
    assert_eq!(
        status.files_expected,
        files.len(),
        "the session must report the set size it was handed"
    );
    assert_eq!(
        status.files_opened,
        files.len(),
        "every file in the effective set must be opened: {:?}",
        status.skipped
    );
    assert!(
        status.skipped.is_empty(),
        "no file may be skipped: {:?}",
        status.skipped
    );
    assert!(
        status.server_pid.is_some(),
        "a server process must be running"
    );

    let top = fixture_root().join("rtl/top.sv");
    let text = std::fs::read_to_string(&top)?;

    let outline = poll_response(
        &session,
        SessionRequest::DocumentSymbols { file: top.clone() },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Symbols(symbols) = outline else {
        bail!("documentSymbols returned the wrong response kind");
    };
    let names: Vec<&str> = symbols.iter().map(|symbol| symbol.name.as_str()).collect();
    assert!(
        names.contains(&"top"),
        "the outline must contain the top module, got {names:?}"
    );

    let (line, character) = position_of(&text, "alu u_alu")?;
    let definition = poll_response(
        &session,
        SessionRequest::Definition {
            file: top.clone(),
            line,
            character,
        },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Locations(definition) = definition else {
        bail!("definition returned the wrong response kind");
    };
    println!("OBSERVED definition locations={}", definition.len());
    assert!(
        !definition.is_empty(),
        "go-to-definition on the alu instance must resolve to at least one location"
    );

    let references = poll_response(
        &session,
        SessionRequest::References {
            file: top.clone(),
            line,
            character,
        },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Locations(references) = references else {
        bail!("references returned the wrong response kind");
    };
    println!("OBSERVED reference locations={}", references.len());

    let rename = poll_response(
        &session,
        SessionRequest::Rename {
            file: top.clone(),
            line,
            character,
            new_name: "alu_renamed".to_string(),
        },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Edits(rename) = rename else {
        bail!("rename returned the wrong response kind");
    };
    println!("OBSERVED rename edits={}", rename.len());

    let formatting = poll_response(
        &session,
        SessionRequest::Formatting { file: top.clone() },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Edits(edits) = formatting else {
        bail!("formatting returned the wrong response kind");
    };
    println!("OBSERVED formatting edits={}", edits.len());

    let diagnostics = poll_response(
        &session,
        SessionRequest::Diagnostics {
            file: fixture_root().join("rtl/alu_pkg.sv"),
        },
        REQUEST_BUDGET,
    )?;
    let SessionResponse::Diagnostics(diagnostics) = diagnostics else {
        bail!("diagnostics returned the wrong response kind");
    };
    println!("OBSERVED alu_pkg.sv diagnostics={}", diagnostics.len());

    let pid = status.server_pid;
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    if let Some(pid) = pid {
        assert!(
            !Path::new(&format!("/proc/{pid}")).exists(),
            "server pid {pid} is still present after the session stopped"
        );
    }
    Ok(())
}

#[test]
fn replacing_the_file_set_changes_the_observable_opened_count() -> Result<()> {
    let Some(ls) = binary() else {
        println!(
            "SKIP: verible-verilog-ls is not available (set VEDIT_VERIBLE_BIN_DIR or run scripts/fetch-verible.sh)"
        );
        return Ok(());
    };
    let files = effective_files()?;
    for (label, subset) in [("full", files.clone()), ("reduced", files[..3].to_vec())] {
        let session = open_session(&ls, &subset)?;
        let status = wait_ready(&session, START_BUDGET)?;
        println!(
            "OBSERVED set={label} expected={} opened={} diagnostics={}",
            status.files_expected, status.files_opened, status.diagnostics
        );
        assert_eq!(status.files_expected, subset.len(), "{label}: set size");
        assert_eq!(status.files_opened, subset.len(), "{label}: opened count");
        assert!(
            session.wait_for_stop(),
            "{label}: session must stop cleanly"
        );
    }
    Ok(())
}
