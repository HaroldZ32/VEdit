//! Integration tests for the process-level behaviour, using fake `slang`
//! shell scripts. These cover the failure model, the limits, cancellation and
//! the no-zombie guarantee without depending on the real slang artifact.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use vd_deeplint::{
    AttemptOutcome, AttemptSource, EditorPosition, EditorRange, LintOptions, LintReport,
    LintService, Severity,
};

static DIRS: AtomicU64 = AtomicU64::new(0);

// The tests write executable fake binaries and spawn them; running them in
// parallel can hit ETXTBSY (another thread still holds a write descriptor when
// a child execs the file). Serialise the test bodies.
static SERIAL: std::sync::Mutex<()> = std::sync::Mutex::new(());
fn serial() -> std::sync::MutexGuard<'static, ()> {
    match SERIAL.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

struct TestDir {
    path: PathBuf,
}

impl TestDir {
    fn new(label: &str) -> Self {
        let serial = DIRS.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!(
            "vd-deeplint-fake-{}-{serial}-{label}",
            std::process::id()
        ));
        fs::create_dir_all(&path)
            .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
        Self { path }
    }

    fn file(&self, name: &str) -> PathBuf {
        self.path.join(name)
    }

    fn script(&self, name: &str, body: &str) -> PathBuf {
        let path = self.file(name);
        fs::write(&path, body)
            .unwrap_or_else(|error| panic!("writing {} failed: {error}", path.display()));
        set_executable(&path);
        path
    }
}

impl Drop for TestDir {
    fn drop(&mut self) {
        if let Err(error) = fs::remove_dir_all(&self.path) {
            eprintln!("WARN: could not remove {}: {error}", self.path.display());
        }
    }
}

#[cfg(unix)]
fn set_executable(path: &Path) {
    use std::os::unix::fs::PermissionsExt;
    if let Ok(metadata) = fs::metadata(path) {
        let mut permissions = metadata.permissions();
        permissions.set_mode(0o755);
        if let Err(error) = fs::set_permissions(path, permissions) {
            panic!("chmod {} failed: {error}", path.display());
        }
    }
}

#[cfg(not(unix))]
fn set_executable(_path: &Path) {}

fn fixture(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures")
        .join(name)
}

fn base_options(binary: PathBuf) -> LintOptions {
    LintOptions {
        binary: Some(binary),
        timeout: Duration::from_millis(500),
        max_json_bytes: 1024 * 1024,
        max_diagnostics: 500,
    }
}

/// A fake slang that writes the given JSON array to the `--diag-json` path.
fn json_script(dir: &TestDir, name: &str, json: &str, exit_code: i32) -> PathBuf {
    let escaped = json.replace('\\', "\\\\").replace('\'', "'\\''");
    dir.script(
        name,
        &format!(
            "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '%s' '{escaped}' > \"$out\"\nexit {exit_code}\n"
        ),
    )
}

fn slumber_script(dir: &TestDir, name: &str, pid_file: &Path) -> PathBuf {
    dir.script(
        name,
        &format!(
            "#!/bin/sh\necho $$ > '{}'\nexec sleep 30\n",
            pid_file.display()
        ),
    )
}

fn wait_for_process_gone(pid: u32) -> bool {
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

fn read_pid(path: &Path) -> u32 {
    let text =
        fs::read_to_string(path).unwrap_or_else(|error| panic!("reading pid file failed: {error}"));
    text.trim()
        .parse::<u32>()
        .unwrap_or_else(|error| panic!("bad pid file: {error}"))
}

fn fake_diagnostic_json() -> String {
    // One diagnostic on line 3, byte columns 5..20 of `undeclared.sv`
    // (the identifier starts after four spaces and is 15 bytes long).
    concat!(
        "[{\"severity\":\"error\",",
        "\"message\":\"use of undeclared identifier 'undefined_count'\",",
        "\"code\":\"UndeclaredIdentifier\",",
        "\"location\":\"tests/fixtures/undeclared.sv:3:5\",",
        "\"ranges\":[{\"start\":{\"file\":\"f.sv\",\"line\":3,\"column\":5},",
        "\"end\":{\"file\":\"f.sv\",\"line\":3,\"column\":20}}],",
        "\"symbolPath\":\"$unit\"}]"
    )
    .to_string()
}

fn fake_three_diagnostics_json() -> String {
    let one = fake_diagnostic_json();
    let mut elements = Vec::new();
    for _ in 0..3 {
        elements.push(one[1..one.len() - 1].to_string());
    }
    format!("[{}]", elements.join(","))
}

#[test]
fn clean_file_is_a_success_with_no_diagnostics() {
    let _serial = serial();
    let dir = TestDir::new("clean");
    let binary = json_script(&dir, "slang", "[]", 0);
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.total_diagnostics, 0);
    assert_eq!(report.exit_code, Some(0));
    assert_eq!(report.summary(), "no diagnostics");
}

#[test]
fn diagnostics_are_parsed_and_converted_to_utf16_positions() {
    let _serial = serial();
    let dir = TestDir::new("diag");
    let json = fake_diagnostic_json();
    let binary = json_script(&dir, "slang", &json, 1);
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(
        report.exit_code,
        Some(1),
        "slang reports errors with exit 1"
    );
    assert_eq!(report.total_diagnostics, 1);
    let diagnostic = &report.diagnostics[0];
    assert_eq!(diagnostic.severity, Severity::Error);
    assert_eq!(diagnostic.code, "UndeclaredIdentifier");
    assert_eq!(diagnostic.symbol_path.as_deref(), Some("$unit"));
    assert_eq!(
        diagnostic.editor_range,
        Some(EditorRange {
            start: EditorPosition {
                line: 2,
                character: 4
            },
            end: EditorPosition {
                line: 2,
                character: 19
            },
        })
    );
    assert!(diagnostic.conversion_note.is_none());
    assert_eq!(report.error_count(), 1);
    assert_eq!(report.warning_count(), 0);
}

#[test]
fn diagnostics_are_capped_and_the_total_is_reported() {
    let _serial = serial();
    let dir = TestDir::new("cap");
    let json = fake_three_diagnostics_json();
    let binary = json_script(&dir, "slang", &json, 1);
    let mut options = base_options(binary);
    options.max_diagnostics = 2;
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &options);
    assert!(report.is_success(), "{:?}", report.failure);
    assert_eq!(report.diagnostics.len(), 2);
    assert_eq!(report.total_diagnostics, 3);
    assert!(report.truncated);
    assert!(report.summary().contains("3"));
}

#[test]
fn malformed_json_is_a_typed_failure() {
    let _serial = serial();
    let dir = TestDir::new("bad-json");
    let binary = json_script(&dir, "slang", "{ this is not json", 1);
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::InvalidJson { .. }) => {}
        other => panic!("expected InvalidJson, got {other:?}"),
    }
    assert!(report.summary().to_lowercase().contains("malformed"));
}

#[test]
fn nonzero_exit_without_json_reports_stderr() {
    let _serial = serial();
    let dir = TestDir::new("no-json");
    let binary = dir.script(
        "slang",
        "#!/bin/sh\necho 'slang: internal error 42' >&2\nexit 7\n",
    );
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::MissingOutput {
            exit_code, stderr, ..
        }) => {
            assert_eq!(*exit_code, Some(7));
            assert!(stderr.contains("internal error 42"));
        }
        other => panic!("expected MissingOutput, got {other:?}"),
    }
    assert!(report.summary().contains('7'));
}

#[test]
fn oversized_json_is_rejected_before_parsing() {
    let _serial = serial();
    let dir = TestDir::new("huge");
    let binary = dir.script(
        "slang",
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nhead -c 4096 /dev/zero | tr '\\0' ' ' > \"$out\"\nexit 1\n",
    );
    let mut options = base_options(binary);
    options.max_json_bytes = 256;
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &options);
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::OutputTooLarge { limit }) => assert_eq!(*limit, 256),
        other => panic!("expected OutputTooLarge, got {other:?}"),
    }
}

#[test]
fn timeout_kills_and_reaps_the_child() {
    let _serial = serial();
    let dir = TestDir::new("timeout");
    let pid_file = dir.file("pid");
    let binary = slumber_script(&dir, "slang", &pid_file);
    let mut options = base_options(binary);
    options.timeout = Duration::from_millis(250);

    let started = Instant::now();
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &options);
    let elapsed = started.elapsed();

    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::Timeout { after }) => {
            assert_eq!(*after, Duration::from_millis(250))
        }
        other => panic!("expected Timeout, got {other:?}"),
    }
    assert!(elapsed < Duration::from_secs(5), "timeout took {elapsed:?}");
    let pid = read_pid(&pid_file);
    assert_eq!(report.child_pid, Some(pid));
    assert!(
        wait_for_process_gone(pid),
        "child {pid} is still alive (zombie?)"
    );
}

#[test]
fn cancel_kills_and_reaps_the_child() {
    let _serial = serial();
    let dir = TestDir::new("cancel");
    let pid_file = dir.file("pid");
    let binary = slumber_script(&dir, "slang", &pid_file);
    let mut options = base_options(binary);
    options.timeout = Duration::from_secs(30);

    let service = LintService::new();
    let worker_service = service.clone();
    let file = fixture("undeclared.sv");
    let worker = thread::spawn(move || worker_service.lint(&file, &options));

    // Wait until the fake binary has recorded its pid, then cancel.
    let deadline = Instant::now() + Duration::from_secs(2);
    while !pid_file.exists() && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    let pid = read_pid(&pid_file);
    service.cancel();
    let report = worker
        .join()
        .unwrap_or_else(|panic| std::panic::resume_unwind(panic));

    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::Cancelled) => {}
        other => panic!("expected Cancelled, got {other:?}"),
    }
    assert!(
        wait_for_process_gone(pid),
        "child {pid} is still alive (zombie?)"
    );
}

#[test]
fn a_new_run_cancels_the_previous_one() {
    let _serial = serial();
    let dir = TestDir::new("replace");
    let pid_file = dir.file("pid");
    let slow = slumber_script(&dir, "slow-slang", &pid_file);
    let fast = json_script(&dir, "fast-slang", "[]", 0);

    let service = LintService::new();
    let worker_service = service.clone();
    let file = fixture("undeclared.sv");
    let mut slow_options = base_options(slow);
    slow_options.timeout = Duration::from_secs(30);
    let slow_file = file.clone();
    let worker = thread::spawn(move || worker_service.lint(&slow_file, &slow_options));

    let deadline = Instant::now() + Duration::from_secs(2);
    while !pid_file.exists() && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    let pid = read_pid(&pid_file);

    let report = service.lint(&file, &base_options(fast));
    assert!(report.is_success(), "{:?}", report.failure);

    let first = worker
        .join()
        .unwrap_or_else(|panic| std::panic::resume_unwind(panic));
    match &first.failure {
        Some(vd_deeplint::LintFailure::Cancelled) => {}
        other => panic!("expected the first run to be Cancelled, got {other:?}"),
    }
    assert!(wait_for_process_gone(pid), "old child {pid} is still alive");
}

#[test]
fn missing_binary_lists_every_attempt() {
    let _serial = serial();
    let dir = TestDir::new("missing-bin");
    let mut options = base_options(dir.file("does-not-exist"));
    options.timeout = Duration::from_millis(100);
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &options);
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::BinaryNotFound { attempts }) => {
            assert!(!attempts.is_empty());
            assert_eq!(attempts[0].source, AttemptSource::ExplicitOption);
            assert_eq!(attempts[0].outcome, AttemptOutcome::NotFound);
        }
        other => panic!("expected BinaryNotFound, got {other:?}"),
    }
    assert!(report.summary().contains("VEDIT_SLANG"));
}

#[test]
fn non_executable_binary_is_not_found() {
    let _serial = serial();
    let dir = TestDir::new("noexec");
    let binary = dir.file("slang");
    fs::write(&binary, "not a program").unwrap_or_else(|error| panic!("write failed: {error}"));
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(metadata) = fs::metadata(&binary) {
            let mut permissions = metadata.permissions();
            permissions.set_mode(0o644);
            if let Err(error) = fs::set_permissions(&binary, permissions) {
                panic!("chmod failed: {error}");
            }
        }
    }
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::BinaryNotFound { attempts }) => {
            assert_eq!(attempts[0].outcome, AttemptOutcome::NotExecutable);
        }
        other => panic!("expected BinaryNotFound, got {other:?}"),
    }
}

#[test]
fn executable_with_a_missing_interpreter_is_a_spawn_failure() {
    let _serial = serial();
    let dir = TestDir::new("bad-interpreter");
    // Executable bit set (so resolution picks it) but the shebang points at a
    // non-existent interpreter: exec fails and must surface as Spawn, not as a
    // crash or a pile of downstream errors.
    let binary = dir.script("slang", "#!/nonexistent/interpreter\nexit 0\n");
    let report = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(!report.is_success());
    match &report.failure {
        Some(vd_deeplint::LintFailure::Spawn { .. }) => {}
        other => panic!("expected Spawn, got {other:?}"),
    }
}

#[test]
fn report_captures_stderr_of_a_successful_run() {
    let _serial = serial();
    let dir = TestDir::new("stderr");
    let binary = dir.script(
        "slang",
        "#!/bin/sh\nprev=\"\"\nfor arg in \"$@\"; do\n  if [ \"$prev\" = \"--diag-json\" ]; then out=\"$arg\"; fi\n  prev=\"$arg\"\ndone\nprintf '[]' > \"$out\"\necho 'a warning-free run' >&2\nexit 0\n",
    );
    let report: LintReport = vd_deeplint::lint(&fixture("undeclared.sv"), &base_options(binary));
    assert!(report.is_success());
    assert!(report.stderr.contains("warning-free"));
}
