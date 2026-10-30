//! Evidence helper: timeout + cancellation + no-zombie proof with fake binaries.
//!
//! Usage: `cargo run -p vd-deeplint --example runtime_probe`
//! Exit code 0 only when both scenarios behave as required.

use std::fs;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, Instant};

use vd_deeplint::{lint, LintOptions, LintService};

fn main() {
    let dir = std::env::temp_dir().join(format!("vd-deeplint-runtime-{}", std::process::id()));
    if let Err(error) = fs::create_dir_all(&dir) {
        eprintln!("FAIL: creating {}: {error}", dir.display());
        std::process::exit(1);
    }
    let pid_file = dir.join("pid");
    let script = dir.join("slow-slang");
    let body = format!(
        "#!/bin/sh\necho $$ > '{}'\nexec sleep 30\n",
        pid_file.display()
    );
    if let Err(error) = fs::write(&script, body) {
        eprintln!("FAIL: writing fake binary: {error}");
        std::process::exit(1);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        match fs::metadata(&script) {
            Ok(metadata) => {
                let mut permissions = metadata.permissions();
                permissions.set_mode(0o755);
                if let Err(error) = fs::set_permissions(&script, permissions) {
                    eprintln!("FAIL: chmod fake binary: {error}");
                    std::process::exit(1);
                }
            }
            Err(error) => {
                eprintln!("FAIL: stat fake binary: {error}");
                std::process::exit(1);
            }
        }
    }
    let fixture = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/undeclared.sv");

    let timeout_options = LintOptions {
        binary: Some(script.clone()),
        timeout: Duration::from_millis(300),
        ..LintOptions::default()
    };
    let started = Instant::now();
    let timed_out = lint(&fixture, &timeout_options);
    let timeout_elapsed = started.elapsed();
    let timeout_pid = read_pid(&pid_file);
    let timeout_reaped = timeout_pid.map(wait_until_gone).unwrap_or(false);
    println!(
        "TIMEOUT: elapsed_ms={} failure={:?} pid={:?} reaped={}",
        timeout_elapsed.as_millis(),
        timed_out.failure,
        timeout_pid,
        timeout_reaped
    );

    if let Err(error) = fs::remove_file(&pid_file) {
        eprintln!("note: could not remove stale pid file: {error}");
    }

    let service = LintService::new();
    let worker_service = service.clone();
    let mut cancel_options = timeout_options.clone();
    cancel_options.timeout = Duration::from_secs(30);
    let worker_fixture = fixture.clone();
    let worker = thread::spawn(move || worker_service.lint(&worker_fixture, &cancel_options));

    let deadline = Instant::now() + Duration::from_secs(2);
    while !pid_file.exists() && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    let cancel_pid = read_pid(&pid_file);
    service.cancel();
    let cancelled = match worker.join() {
        Ok(report) => report,
        Err(_) => {
            eprintln!("FAIL: lint worker panicked");
            std::process::exit(1);
        }
    };
    let cancel_reaped = cancel_pid.map(wait_until_gone).unwrap_or(false);
    println!(
        "CANCEL: failure={:?} pid={:?} reaped={}",
        cancelled.failure, cancel_pid, cancel_reaped
    );

    let timeout_ok = matches!(
        timed_out.failure,
        Some(vd_deeplint::LintFailure::Timeout { .. })
    ) && timeout_elapsed < Duration::from_secs(5)
        && timeout_reaped;
    let cancel_ok =
        matches!(cancelled.failure, Some(vd_deeplint::LintFailure::Cancelled)) && cancel_reaped;

    if let Err(error) = fs::remove_dir_all(&dir) {
        eprintln!("note: could not remove {}: {error}", dir.display());
    }

    println!(
        "RESULT: timeout={} cancel={}",
        pass(timeout_ok),
        pass(cancel_ok)
    );
    std::process::exit(if timeout_ok && cancel_ok { 0 } else { 1 });
}

fn pass(ok: bool) -> &'static str {
    if ok {
        "PASS"
    } else {
        "FAIL"
    }
}

fn read_pid(path: &Path) -> Option<u32> {
    let text = fs::read_to_string(path).ok()?;
    text.trim().parse::<u32>().ok()
}

fn wait_until_gone(pid: u32) -> bool {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        if !Path::new(&format!("/proc/{pid}")).exists() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        thread::sleep(Duration::from_millis(20));
    }
}
