//! AC-4.1/4.2/4.5/4.6/4.7/4.8: shell environment import — capture, parse, diff,
//! masking and the layered selection.
//!
//! The capture tests are REAL process runs against the shells this host has
//! (`bash`/`sh` always; `zsh`/`csh`/`fish` only when installed, otherwise the
//! test reports SKIP instead of pretending).

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::AtomicBool;
use std::time::{Duration, Instant};

use vd_config::env::{
    capture, capture_argv, capture_with, diff, parse_capture_block, CaptureError, ShellInit,
    ShellKind, DEFAULT_STDERR_CAP,
};
use vd_config::{Env, EnvironmentSelection};

/// A scratch directory removed on drop.
struct TempDir(PathBuf);

impl TempDir {
    fn new(label: &str) -> Self {
        let serial = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|elapsed| elapsed.subsec_nanos())
            .unwrap_or(0);
        let path = std::env::temp_dir().join(format!(
            "vd-env-import-{}-{serial}-{label}",
            std::process::id()
        ));
        std::fs::create_dir_all(&path)
            .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
        Self(path)
    }

    fn path(&self) -> &Path {
        &self.0
    }

    fn file(&self, name: &str, text: &str) -> PathBuf {
        let path = self.0.join(name);
        std::fs::write(&path, text)
            .unwrap_or_else(|error| panic!("writing {} failed: {error}", path.display()));
        path
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        if let Err(error) = std::fs::remove_dir_all(&self.0) {
            eprintln!("WARN: could not remove {}: {error}", self.0.display());
        }
    }
}

/// Whether a shell is installed (the capture tests skip when it is not).
fn have_shell(name: &str) -> bool {
    Command::new(name)
        .arg("-c")
        .arg("true")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

fn init(shell: ShellKind, source_file: Option<&Path>) -> ShellInit {
    ShellInit {
        shell,
        source_file: source_file.map(Path::to_path_buf),
        timeout: Duration::from_secs(20),
        ..ShellInit::default()
    }
}

#[test]
fn a_nul_separated_block_keeps_spaces_and_newlines() {
    let mut block: Vec<u8> = Vec::new();
    block.extend_from_slice(b"PLAIN=value\0");
    block.extend_from_slice(b"WITH SPACE=one two\0");
    block.extend_from_slice(b"MULTI=line one\nline two\0");
    block.extend_from_slice(b"EQUALS=a=b=c\0");
    block.push(0);
    let mut warnings = Vec::new();
    let (entries, raw) = parse_capture_block(&block, &mut warnings)
        .unwrap_or_else(|error| panic!("parsing failed: {error}"));
    assert_eq!(entries.len(), 4);
    assert_eq!(entries[1].1, "one two");
    assert_eq!(
        entries[2].1, "line one\nline two",
        "a newline inside a value must survive"
    );
    assert_eq!(
        entries[3].1, "a=b=c",
        "only the FIRST '=' separates name and value"
    );
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(raw.last(), Some(&0), "the blob ends with a NUL");
}

#[test]
fn a_bare_entry_and_an_empty_block_are_handled() {
    let mut warnings = Vec::new();
    let (entries, _) = parse_capture_block(b"A=1\0garbage\0B=2\0", &mut warnings)
        .unwrap_or_else(|error| panic!("parsing failed: {error}"));
    assert_eq!(entries.len(), 2);
    assert_eq!(
        warnings.len(),
        1,
        "the bare entry is reported: {warnings:?}"
    );

    let mut warnings = Vec::new();
    assert_eq!(
        parse_capture_block(b"", &mut warnings),
        Err(CaptureError::NotAnEnvironment(
            "the capture produced no NAME=VALUE entry".to_string()
        ))
    );
}

#[test]
fn the_argv_of_each_shell_family_is_documented_and_stable() {
    let posix = capture_argv(&ShellInit {
        shell: ShellKind::Bash,
        source_file: Some(PathBuf::from("/tmp/a b/my rc.sh")),
        login: true,
        interactive: true,
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(posix[0], "bash");
    assert_eq!(posix[1], "-l");
    assert_eq!(posix[2], "-i");
    assert_eq!(posix[3], "-c");
    let script = posix[4].to_string_lossy();
    assert!(script.starts_with("set -a; . "), "{script}");
    assert!(
        script.contains("'/tmp/a b/my rc.sh'"),
        "quoted path: {script}"
    );
    assert!(script.contains("env -0"), "{script}");
    assert!(
        script.contains("exit $_vd_rc"),
        "the source status is reported: {script}"
    );

    let fish = capture_argv(&ShellInit {
        shell: ShellKind::Fish,
        source_file: Some(PathBuf::from("/tmp/rc.fish")),
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    let script = fish[2].to_string_lossy();
    assert!(script.contains("source '/tmp/rc.fish'"), "{script}");
    assert!(!script.contains("set -a"), "fish has no `set -a`: {script}");

    // A51: the csh family used to be the one capture that printed `env` (newline
    // separated) because "csh has no NUL form" was believed. That premise was
    // wrong: `env -0` is coreutils, independent of the shell that runs it, so the
    // csh family now emits the same NUL block as the others and a value holding a
    // newline survives. The status of `source` is still saved BEFORE env runs.
    let csh = capture_argv(&ShellInit {
        shell: ShellKind::Tcsh,
        source_file: Some(PathBuf::from("/tmp/rc.csh")),
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    let script = csh[2].to_string_lossy();
    assert!(script.contains("source '/tmp/rc.csh'"), "{script}");
    assert!(
        script.contains("env -0"),
        "the csh family captures the NUL form through coreutils env(1): {script}"
    );
    assert!(
        !script.contains("; env;"),
        "no newline-separated capture is left in the csh family: {script}"
    );
    assert!(
        script.contains("set _vd_rc=$status") && script.contains("exit $_vd_rc"),
        "the source status is saved before env runs and reported: {script}"
    );
    assert!(
        ShellKind::Tcsh.documented_behaviour().contains("env -0"),
        "the documented behaviour matches the argv: {}",
        ShellKind::Tcsh.documented_behaviour()
    );

    // The no-rc form is the same NUL read (no shell script at all).
    let csh_no_rc = capture_argv(&ShellInit {
        shell: ShellKind::Csh,
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(csh_no_rc[1], "-c");
    assert_eq!(
        csh_no_rc[2], "env -0",
        "a csh capture without an rc file is NUL-separated too"
    );

    // Custom: the caller's argv verbatim, no `-c` added.
    let custom = capture_argv(&ShellInit {
        shell: ShellKind::Custom,
        program: Some(PathBuf::from("/usr/bin/env")),
        extra_argv: vec!["-0".to_string()],
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(custom.len(), 2);
    assert_eq!(custom[1], "-0");

    // Custom without a program is refused before anything is spawned.
    assert!(capture_argv(&ShellInit {
        shell: ShellKind::Custom,
        ..ShellInit::default()
    })
    .is_err());
}

#[test]
fn a_real_bash_capture_reads_the_sourced_environment_and_masks_secrets() {
    if !have_shell("bash") {
        eprintln!("SKIP: no bash on this host");
        return;
    }
    let dir = TempDir::new("bash-capture");
    let rc = dir.file(
        "rc.sh",
        "export WIDTH=8\nexport WITH_SPACE='one two'\nexport MULTI='first\nsecond'\nexport GITHUB_TOKEN='super-secret-token'\n",
    );
    let outcome = capture(&init(ShellKind::Bash, Some(&rc)), None)
        .unwrap_or_else(|error| panic!("capture failed: {error}"));
    assert_eq!(outcome.exit_code, 0, "stderr: {:?}", outcome.stderr);
    let env = outcome.environment();
    assert_eq!(env.get("WIDTH"), Some("8"));
    assert_eq!(env.get("WITH_SPACE"), Some("one two"));
    assert_eq!(
        env.get("MULTI"),
        Some("first\nsecond"),
        "a newline inside a value survives the NUL-separated capture"
    );
    assert_eq!(env.get("GITHUB_TOKEN"), Some("super-secret-token"));
    assert_eq!(outcome.program, "bash");

    // The diff exposes only added/changed entries and masks the secret one.
    let process = Env::from_pairs([("KEEP", "same"), ("WIDTH", "4")]);
    let changes = diff(&process, &outcome.entries);
    let names: Vec<&str> = changes.iter().map(|change| change.name.as_str()).collect();
    assert!(names.contains(&"WITH_SPACE"), "{names:?}");
    assert!(
        !names.contains(&"KEEP"),
        "an unchanged variable is not offered: {names:?}"
    );
    let width = changes
        .iter()
        .find(|change| change.name == "WIDTH")
        .unwrap_or_else(|| panic!("WIDTH changed 4 -> 8"));
    assert_eq!(width.before.as_deref(), Some("4"));
    assert_eq!(width.after, "8");
    let token = changes
        .iter()
        .find(|change| change.name == "GITHUB_TOKEN")
        .unwrap_or_else(|| panic!("the token is an added variable"));
    assert!(token.is_masked(), "a *TOKEN* name is masked by default");
    assert_eq!(token.display_value(), "<masked>");
    assert!(
        !token.display_value().contains("super-secret-token"),
        "the masked display must never carry the value"
    );
}

/// E4b: the capture's own status bookkeeping must not be exported into the
/// captured environment. On the POSIX family the source status used to be saved
/// as `_vd_rc` WHILE `set -a` was still active, so the assignment was exported
/// and every POSIX capture offered `_vd_rc` to the Environment page as a newly
/// added variable (the user could even import it).
#[test]
fn a_posix_capture_does_not_export_its_own_bookkeeping_variables() {
    for shell in [ShellKind::Sh, ShellKind::Bash] {
        let Some(program) = shell.program() else {
            continue;
        };
        if !have_shell(program) {
            eprintln!("SKIP: no {program} on this host");
            continue;
        }
        let dir = TempDir::new("bookkeeping");
        let rc = dir.file("rc.sh", "export POSIX_MARKER=1\n");
        let outcome = capture(&init(shell, Some(&rc)), None)
            .unwrap_or_else(|error| panic!("{program} capture failed: {error}"));
        assert_eq!(outcome.exit_code, 0, "stderr: {:?}", outcome.stderr);
        assert_eq!(
            outcome.environment().get("POSIX_MARKER"),
            Some("1"),
            "the rc file really was sourced (a vacuous capture must not pass this test)"
        );
        let names: Vec<&str> = outcome
            .entries
            .iter()
            .map(|(name, _)| name.as_str())
            .collect();
        let leaked: Vec<&str> = names
            .iter()
            .copied()
            .filter(|name| name.starts_with("_vd_"))
            .collect();
        assert!(
            leaked.is_empty(),
            "{program}: the capture exported its own bookkeeping variable(s) {leaked:?}; \
             the reserved `_vd_` prefix must never reach the captured environment; \
             captured names: {names:?}"
        );
    }
}

#[test]
fn a_broken_rc_file_reports_a_nonzero_exit_and_the_stderr_tail() {
    if !have_shell("bash") {
        eprintln!("SKIP: no bash on this host");
        return;
    }
    // bash (unlike `sh`, which aborts when `.` fails) still runs the rest of the
    // script, so the outcome carries BOTH the failing status and the environment
    // — the shape the product reports for a broken rc file.
    let dir = TempDir::new("bash-broken");
    let missing = dir.path().join("does-not-exist.sh");
    let outcome = capture(&init(ShellKind::Bash, Some(&missing)), None)
        .unwrap_or_else(|error| panic!("the shell still ran: {error}"));
    assert_ne!(
        outcome.exit_code, 0,
        "a missing source file must be observable through the exit code"
    );
    let stderr = outcome.stderr.unwrap_or_default();
    assert!(
        stderr.contains("does-not-exist.sh") || !stderr.is_empty(),
        "the stderr tail explains the failure: {stderr:?}"
    );
    // E4b: the FAILURE path must not leak the bookkeeping variable either. The
    // status has to survive until `exit`, so it cannot be unset before the exit;
    // it is relayed through a variable that is no longer exported instead.
    let names: Vec<&str> = outcome
        .entries
        .iter()
        .map(|(name, _)| name.as_str())
        .collect();
    let leaked: Vec<&str> = names
        .iter()
        .copied()
        .filter(|name| name.starts_with("_vd_"))
        .collect();
    assert!(
        leaked.is_empty(),
        "a failed source must not export the capture's bookkeeping: {leaked:?}; \
         captured names: {names:?}"
    );
}

#[test]
fn the_stderr_tail_is_truncated_and_keeps_the_end() {
    if !have_shell("sh") {
        eprintln!("SKIP: no sh on this host");
        return;
    }
    let long = "x".repeat(4 * 1024);
    let dir = TempDir::new("stderr-tail");
    let rc = dir.file(
        "rc.sh",
        &format!("echo \"{long} FIRST-LINE\" >&2\necho LAST-LINE >&2\nexport OK=1\n"),
    );
    let outcome = capture_with(&init(ShellKind::Sh, Some(&rc)), None, 2048)
        .unwrap_or_else(|error| panic!("capture failed: {error}"));
    assert_eq!(outcome.environment().get("OK"), Some("1"));
    let stderr = outcome.stderr.unwrap_or_default();
    assert!(
        stderr.len() <= 2048,
        "the tail is capped: {} bytes",
        stderr.len()
    );
    assert!(
        stderr.contains("LAST-LINE"),
        "the END of stderr is kept: {stderr:?}"
    );
}

#[test]
fn a_capture_that_overruns_its_budget_is_killed_and_reported() {
    let dir = TempDir::new("timeout");
    let mut request = ShellInit {
        shell: ShellKind::Custom,
        program: Some(PathBuf::from("sh")),
        extra_argv: vec!["-c".to_string(), "sleep 30".to_string()],
        timeout: Duration::from_millis(300),
        ..ShellInit::default()
    };
    request.workdir = Some(dir.path().to_path_buf());
    let started = Instant::now();
    let result = capture(&request, None);
    let elapsed = started.elapsed();
    match result {
        Err(CaptureError::TimedOut { elapsed: reported }) => {
            assert!(reported >= Duration::from_millis(300), "{reported:?}");
        }
        other => panic!("expected a timeout, got {other:?}"),
    }
    assert!(
        elapsed < Duration::from_secs(10),
        "the kill must not wait for the child ({elapsed:?})"
    );
}

#[test]
fn a_cancel_stops_the_capture() {
    let cancel = AtomicBool::new(true);
    let request = ShellInit {
        shell: ShellKind::Custom,
        program: Some(PathBuf::from("sh")),
        extra_argv: vec!["-c".to_string(), "sleep 30".to_string()],
        timeout: Duration::from_secs(20),
        ..ShellInit::default()
    };
    let started = Instant::now();
    assert_eq!(
        capture(&request, Some(&cancel)),
        Err(CaptureError::Cancelled)
    );
    assert!(started.elapsed() < Duration::from_secs(10));
}

#[test]
fn the_selection_merges_process_then_global_then_project() {
    let process = Env::from_pairs([("PATH", "/usr/bin"), ("KEEP", "1"), ("LAYER", "process")]);
    let mut selection = EnvironmentSelection {
        global: vec![
            ("LAYER".to_string(), "global".to_string()),
            ("GLOBAL_ONLY".to_string(), "g".to_string()),
            ("SECRET_TOKEN".to_string(), "tok".to_string()),
        ],
        ..EnvironmentSelection::default()
    };
    selection.projects.insert(
        "/proj/a".to_string(),
        vec![("LAYER".to_string(), "project".to_string())],
    );
    let merged = selection.merged_environment(&process, Some(Path::new("/proj/a")));
    assert_eq!(merged.get("PATH"), Some("/usr/bin"), "process stays");
    assert_eq!(merged.get("KEEP"), Some("1"));
    assert_eq!(
        merged.get("LAYER"),
        Some("project"),
        "project wins over global, global over process"
    );
    assert_eq!(merged.get("GLOBAL_ONLY"), Some("g"));

    // Without a project root the project layer does not apply.
    let global_only = selection.merged_environment(&process, None);
    assert_eq!(global_only.get("LAYER"), Some("global"));
}

#[test]
fn the_selection_round_trips_and_a_broken_file_degrades() {
    let dir = TempDir::new("selection");
    let path = dir.path().join("environment.json");
    let selection = EnvironmentSelection {
        global: vec![("WIDTH".to_string(), "8".to_string())],
        projects: std::collections::BTreeMap::from([(
            "/proj/a".to_string(),
            vec![("MODE".to_string(), "sim".to_string())],
        )]),
    };
    selection
        .save(&path)
        .unwrap_or_else(|error| panic!("save failed: {error}"));
    let (loaded, warnings) = EnvironmentSelection::load(&path);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(loaded.global, selection.global);
    assert_eq!(
        loaded.projects.get("/proj/a"),
        selection.projects.get("/proj/a")
    );

    let leftovers: Vec<PathBuf> = std::fs::read_dir(dir.path())
        .map(|entries| {
            entries
                .filter_map(Result::ok)
                .map(|entry| entry.path())
                .filter(|path| {
                    path.file_name()
                        .map(|name| name.to_string_lossy().starts_with('.'))
                        .unwrap_or(false)
                })
                .collect()
        })
        .unwrap_or_default();
    assert!(
        leftovers.is_empty(),
        "no temporary file is left: {leftovers:?}"
    );

    // A missing file is simply empty; a broken file is empty plus a warning.
    let (missing, warnings) = EnvironmentSelection::load(&dir.path().join("nothing.json"));
    assert!(missing.global.is_empty());
    assert!(warnings.is_empty());
    let broken = dir.file("broken.json", "{not json");
    let (empty, warnings) = EnvironmentSelection::load(&broken);
    assert!(empty.global.is_empty());
    assert_eq!(warnings.len(), 1, "{warnings:?}");
    assert!(warnings[0].contains("not valid JSON"), "{warnings:?}");
}

#[test]
fn the_default_stderr_cap_is_documented() {
    // The ABI and the engine must agree on the same default (8 KiB tail).
    assert_eq!(DEFAULT_STDERR_CAP, 8 * 1024);
}
