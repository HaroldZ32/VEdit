//! A missing Verible binary degrades to a diagnosable `Failed` session: no
//! panic, no stuck request, and a status message that names the problem.
//!
//! This is the "verible 缺失" path required by the task. It runs with no server
//! binary at all, so it is part of every clean checkout's test run.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use vd_lsp::{LspConfig, SessionHandle, SessionPhase, SessionRequest};

fn fixture_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("fixtures/project")
}

#[test]
fn missing_server_binary_fails_diagnosably_and_never_hangs() -> Result<()> {
    let root = fixture_root();
    let top = root.join("top.sv");
    let missing = root.join("this-verible-binary-does-not-exist");
    assert!(!missing.exists(), "the fixture must not contain a fake binary");

    let session = SessionHandle::with_budgets(
        LspConfig::new(&missing, &root),
        vec![top],
        Duration::from_millis(500),
        Duration::from_secs(5),
    )?;

    let deadline = Instant::now() + Duration::from_secs(20);
    let status = loop {
        let status = session.status();
        match status.phase {
            SessionPhase::Failed => break status,
            SessionPhase::Starting => {}
            other => bail!("expected a Failed session, got {other:?} ({})", status.message),
        }
        if Instant::now() >= deadline {
            bail!("the session did not reach Failed within 20 s: {status:?}");
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert!(
        status.message.contains("cannot open"),
        "the failure message must explain what could not be opened: {}",
        status.message
    );
    assert!(
        status.message.contains("this-verible-binary-does-not-exist")
            || status.message.to_lowercase().contains("no such file"),
        "the failure message must name the missing binary or the OS error: {}",
        status.message
    );

    // A request after the failure is answered with a typed failure, not Pending.
    let request = session.request(SessionRequest::DocumentSymbols {
        file: root.join("top.sv"),
    })?;
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        match session.poll(request) {
            vd_lsp::PollOutcome::Failed(message) => {
                assert!(
                    message.contains("cannot open"),
                    "the request failure must carry the session reason: {message}"
                );
                break;
            }
            vd_lsp::PollOutcome::Pending => {}
            vd_lsp::PollOutcome::Unknown => bail!("the request id vanished"),
            vd_lsp::PollOutcome::Ready(_) => bail!("a failed session must not serve requests"),
        }
        if Instant::now() >= deadline {
            bail!("a request on a failed session stayed pending");
        }
        std::thread::sleep(Duration::from_millis(20));
    }

    assert!(session.wait_for_stop(), "the failed session must stop cleanly");
    Ok(())
}
