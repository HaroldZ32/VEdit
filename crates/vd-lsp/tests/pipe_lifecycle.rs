//! T134: the "Broken pipe" crash path, headless.
//!
//! A language server that exits immediately leaves a closed stdin pipe. In the
//! product the C++ host ignores SIGPIPE (Rust is statically linked there, so the
//! Rust runtime's own ignore never runs), which turns the next write into an
//! `EPIPE` error. That error must surface as a typed failure — never a panic,
//! never a request that stays Pending, never a hang.
#![cfg(unix)]

use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use vd_lsp::{LspConfig, PollOutcome, SessionHandle, SessionPhase, SessionRequest};

fn fixture_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("fixtures/project")
}

#[test]
fn a_server_that_exits_immediately_is_a_typed_failure_not_a_panic() -> Result<()> {
    let root = fixture_root();
    let script = std::env::temp_dir().join(format!("vd-lsp-exiting-{}.sh", std::process::id()));
    std::fs::write(&script, "#!/bin/sh\nexit 0\n")?;
    std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755))?;

    let session = SessionHandle::with_budgets(
        LspConfig::new(&script, &root),
        vec![root.join("top.sv")],
        Duration::from_millis(300),
        Duration::from_secs(3),
    )?;

    let deadline = Instant::now() + Duration::from_secs(10);
    let status = loop {
        let status = session.status();
        match status.phase {
            SessionPhase::Failed | SessionPhase::Stopped => break status,
            SessionPhase::Ready => bail!("a server that exits immediately must not become Ready"),
            SessionPhase::Starting => {}
        }
        if Instant::now() >= deadline {
            bail!("the session neither failed nor stopped within 10 s: {status:?}");
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert!(
        !status.message.is_empty(),
        "the failure must carry a diagnosable message, got {status:?}"
    );

    // A request after the server died must be a typed failure, not Pending forever.
    if let Ok(request) = session.request(SessionRequest::DocumentSymbols {
        file: root.join("top.sv"),
    }) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            match session.poll(request) {
                PollOutcome::Failed(_) | PollOutcome::Unknown => break,
                PollOutcome::Pending => {}
                PollOutcome::Ready(_) => bail!("a dead server must not serve a request"),
            }
            if Instant::now() >= deadline {
                bail!("the request stayed Pending after the server died (dead-pipe write path)");
            }
            std::thread::sleep(Duration::from_millis(20));
        }
    }

    let _ = std::fs::remove_file(&script);
    assert!(
        session.wait_for_stop(),
        "the worker must stop and reap the dead child (no writes after teardown)"
    );
    Ok(())
}
