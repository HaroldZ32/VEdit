//! The EDITOR BUFFER path (T118): diagnostics must follow the text the user sees,
//! not just the bytes on disk.
//!
//! Real `verible-verilog-ls`, no fake server. The scenario is the reported defect:
//! open a valid file, then DELETE ONE COMMA inside the editor buffer (without
//! saving) and require a syntax diagnostic for the buffer text; the file on disk
//! is asserted to be unchanged, which is what proves the lint ran on the buffer.
//!
//! Requires the Verible binaries: `VEDIT_VERIBLE_BIN_DIR` or the repository recon
//! directory (`scripts/fetch-verible.sh`).

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Result};
use vd_lsp::{
    LspConfig, PollOutcome, SessionDiagnostic, SessionHandle, SessionPhase, SessionRequest,
    SessionResponse,
};

fn binary(name: &str) -> PathBuf {
    let directory = std::env::var("VEDIT_VERIBLE_BIN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../.mpd/recon/verible/bin")
        });
    directory.join(name)
}

const GOOD: &str =
    "module top;\n  logic a, b;\n  always_comb begin\n    a = b;\n  end\nendmodule\n";
const MISSING_COMMA: &str =
    "module top;\n  logic a b;\n  always_comb begin\n    a = b;\n  end\nendmodule\n";

struct Scratch {
    path: PathBuf,
}

impl Scratch {
    fn new(tag: &str) -> Result<Self> {
        let path = std::env::temp_dir().join(format!(
            "vd-lsp-buffer-{tag}-{}-{}",
            std::process::id(),
            Instant::now().elapsed().as_nanos()
                + std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_nanos())
                    .unwrap_or(0)
        ));
        std::fs::create_dir_all(&path)?;
        Ok(Self { path })
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

fn diagnostics_of(session: &SessionHandle, file: &Path) -> Result<Vec<SessionDiagnostic>> {
    let id = session.request(SessionRequest::Diagnostics {
        file: file.to_path_buf(),
    })?;
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        match session.poll(id) {
            PollOutcome::Pending => {}
            PollOutcome::Ready(SessionResponse::Diagnostics(diagnostics)) => {
                return Ok(diagnostics);
            }
            PollOutcome::Ready(other) => bail!("unexpected response: {other:?}"),
            PollOutcome::Failed(error) => bail!("diagnostics request failed: {error}"),
            PollOutcome::Unknown => bail!("diagnostics request was consumed unexpectedly"),
        }
        if Instant::now() >= deadline {
            bail!("diagnostics request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn wait_for<F>(budget: Duration, mut probe: F) -> Result<()>
where
    F: FnMut() -> Result<bool>,
{
    let deadline = Instant::now() + budget;
    loop {
        if probe()? {
            return Ok(());
        }
        if Instant::now() >= deadline {
            bail!("condition not met within {budget:?}");
        }
        std::thread::sleep(Duration::from_millis(40));
    }
}

fn session_over(root: &Path, file: &Path) -> Result<SessionHandle> {
    let config = LspConfig::new(binary("verible-verilog-ls"), root);
    SessionHandle::start(config, vec![file.to_path_buf()])
}

#[test]
fn buffer_edit_without_saving_is_diagnosed() -> Result<()> {
    let ls = binary("verible-verilog-ls");
    if !ls.is_file() {
        eprintln!(
            "SKIP: verible-verilog-ls is not available at {} (set VEDIT_VERIBLE_BIN_DIR or run scripts/fetch-verible.sh)",
            ls.display()
        );
        return Ok(());
    }
    let scratch = Scratch::new("comma")?;
    let file = scratch.path.join("top.sv");
    std::fs::write(&file, GOOD)?;

    let session = session_over(&scratch.path, &file)?;
    wait_for(Duration::from_secs(20), || {
        Ok(session.status().phase == SessionPhase::Ready)
    })?;
    let initial = diagnostics_of(&session, &file)?;
    eprintln!(
        "initial diagnostics for the saved good file: {}",
        initial.len()
    );

    // The user deletes one comma IN THE BUFFER; nothing is written to disk.
    session.change_document(&file, MISSING_COMMA.to_string())?;
    wait_for(Duration::from_secs(20), || {
        let current = diagnostics_of(&session, &file)?;
        if current.iter().any(|diagnostic| {
            diagnostic.message.to_lowercase().contains("syntax") || diagnostic.severity == Some(1)
        }) {
            Ok(true)
        } else {
            Ok(false)
        }
    })?;
    let after_break = diagnostics_of(&session, &file)?;
    eprintln!(
        "diagnostics after the buffer-only comma removal: {} -> {:?}",
        after_break.len(),
        after_break
            .iter()
            .map(|diagnostic| diagnostic.message.clone())
            .collect::<Vec<_>>()
    );
    assert!(
        !after_break.is_empty(),
        "the buffer syntax error produced no diagnostic"
    );

    // Proof that the lint ran on the BUFFER: the file on disk never changed.
    assert_eq!(
        std::fs::read_to_string(&file)?,
        GOOD,
        "disk file was modified"
    );

    // Putting the comma back in the buffer must clear the syntax error.
    session.change_document(&file, GOOD.to_string())?;
    wait_for(Duration::from_secs(20), || {
        Ok(diagnostics_of(&session, &file)?.len() < after_break.len())
    })?;
    let restored = diagnostics_of(&session, &file)?;
    eprintln!("diagnostics after restoring the buffer: {}", restored.len());
    assert!(
        restored.len() < after_break.len(),
        "restoring the buffer did not reduce the diagnostics"
    );

    session.stop();
    Ok(())
}

#[test]
fn change_opens_a_document_the_session_did_not_have() -> Result<()> {
    let ls = binary("verible-verilog-ls");
    if !ls.is_file() {
        eprintln!("SKIP: verible-verilog-ls is not available");
        return Ok(());
    }
    let scratch = Scratch::new("opens")?;
    let first = scratch.path.join("first.sv");
    let second = scratch.path.join("second.sv");
    std::fs::write(&first, GOOD)?;
    std::fs::write(&second, GOOD)?;

    let session = session_over(&scratch.path, &first)?;
    wait_for(Duration::from_secs(20), || {
        Ok(session.status().phase == SessionPhase::Ready)
    })?;
    session.change_document(&second, MISSING_COMMA.to_string())?;
    wait_for(Duration::from_secs(20), || {
        Ok(!diagnostics_of(&session, &second)?.is_empty())
    })?;
    assert!(
        !diagnostics_of(&session, &second)?.is_empty(),
        "changing a file that was not in the set did not open and lint it"
    );
    let error_message = diagnostics_of(&session, &second)?
        .iter()
        .map(|diagnostic| diagnostic.message.clone())
        .collect::<Vec<_>>()
        .join(" | ");
    eprintln!("closed-set file diagnostics: {error_message}");
    session.stop();
    Ok(())
}

#[allow(dead_code)]
fn unused_anyhow() -> anyhow::Error {
    anyhow!("unused")
}
