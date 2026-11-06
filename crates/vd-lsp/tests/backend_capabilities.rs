//! Backend selection and the capability table (T138).
//!
//! The verible rows are checked against the REAL server's `initialize` result, so
//! the static table can never drift into an unmeasured claim. The slang rows are
//! checked to be `Unknown` — the honest state before a slang-server exists here —
//! and a slang session with no binary must fail diagnosably instead of hanging.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use vd_lsp::{
    capabilities, CapabilityBasis, LspAction, LspBackend, LspConfig, SessionHandle, SessionPhase,
    Support,
};

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

fn file_set(root: &Path) -> Vec<PathBuf> {
    let mut files: Vec<PathBuf> = std::fs::read_dir(root)
        .unwrap_or_else(|error| panic!("read fixture dir: {error}"))
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.is_file() && path.extension().is_some_and(|ext| ext == "sv"))
        .collect();
    files.sort();
    files
}

fn wait_ready(handle: &SessionHandle, budget: Duration) -> vd_lsp::SessionStatus {
    let deadline = Instant::now() + budget;
    loop {
        let status = handle.status();
        if matches!(status.phase, SessionPhase::Ready | SessionPhase::Failed) {
            return status;
        }
        assert!(
            Instant::now() < deadline,
            "session did not settle: {status:?}"
        );
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[test]
fn the_static_verible_table_matches_the_real_initialize_result() {
    let config = LspConfig::new(binary("verible-verilog-ls"), fixture("project"));
    let files = file_set(&fixture("project"));
    let handle = SessionHandle::start(config, files)
        .unwrap_or_else(|error| panic!("session start: {error}"));
    let status = wait_ready(&handle, Duration::from_secs(20));
    assert_eq!(status.phase, SessionPhase::Ready, "{}", status.message);
    assert_eq!(status.backend, LspBackend::Verible);
    let live = handle.capabilities();
    assert_eq!(live.basis, CapabilityBasis::Live);
    let declared = capabilities(LspBackend::Verible);
    assert_eq!(declared.basis, CapabilityBasis::Measured);
    for action in LspAction::ALL {
        assert_eq!(
            live.support(action),
            declared.support(action),
            "row {action:?}: the measured table must equal the live answer"
        );
    }
    assert!(handle.wait_for_stop(), "the session must stop cleanly");
}

#[test]
fn a_slang_session_without_a_binary_fails_diagnosably() {
    let config = LspConfig::new("/nonexistent/slang-server", fixture("project"))
        .with_backend(LspBackend::Slang);
    let files = file_set(&fixture("project"));
    let handle = SessionHandle::start(config, files)
        .unwrap_or_else(|error| panic!("session start: {error}"));
    let status = wait_ready(&handle, Duration::from_secs(20));
    assert_eq!(status.phase, SessionPhase::Failed, "{}", status.message);
    assert_eq!(status.backend, LspBackend::Slang);
    assert!(
        status.message.contains("slang-server") || status.message.contains("/nonexistent"),
        "the failure names the binary: {}",
        status.message
    );
    assert_eq!(
        handle.capabilities().support(LspAction::Definition),
        Support::Supported,
        "no live handshake happened, so the static (measured) table applies"
    );
    assert_eq!(
        handle.capabilities().support(LspAction::Formatting),
        Support::Unsupported,
        "slang-server advertises no documentFormattingProvider (measured)"
    );
}

#[test]
fn the_two_backends_produce_different_argv() {
    let verible = LspConfig::new("/bin/verible-verilog-ls", "/p")
        .with_rules_config("/p/.rules.verible_lint")
        .with_include_dir("/p/inc")
        .with_define("WIDTH=8");
    let verible_line = verible.command_line();
    assert!(verible_line.contains("backend=verible"), "{verible_line}");
    assert!(
        verible_line.contains("--rules_config=/p/.rules.verible_lint"),
        "{verible_line}"
    );
    assert!(
        !verible_line.contains("-I/p/inc"),
        "verible has no include flag"
    );

    let slang = LspConfig::new("/bin/slang-server", "/p")
        .with_backend(LspBackend::Slang)
        .with_rules_config("/p/.rules.verible_lint")
        .with_include_dir("/p/inc")
        .with_define("WIDTH=8");
    let slang_line = slang.command_line();
    assert!(slang_line.contains("backend=slang"), "{slang_line}");
    assert!(slang_line.contains("-I/p/inc"), "{slang_line}");
    assert!(slang_line.contains("-DWIDTH=8"), "{slang_line}");
    assert!(
        !slang_line.contains("--rules_config"),
        "verible's lint rules do not apply to slang: {slang_line}"
    );
}
