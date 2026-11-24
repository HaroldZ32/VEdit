//! batch-20261115 AC-1.1/1.2/1.4/1.5: the automatic csh-family capture policy,
//! the csh quoting fix and the global-layer merge.
//!
//! The quoting tests run REAL `csh`/`tcsh` children (skipped with an explicit
//! message when the host has neither) against a temp directory whose name holds
//! the characters the old POSIX quoting got wrong.

use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;

use vd_config::env::{
    auto_capture_entries, auto_capture_path, auto_capture_plan, capture, capture_argv,
    detect_shell, AutoCapturePlan, AutoCaptureRecord, AutoCaptureSkip, EnvironmentSelection,
    ShellInit, ShellKind, AUTO_CAPTURE_FILE_NAME, AUTO_CAPTURE_SKIP_NAMES,
};
use vd_config::Env;

/// A scratch directory removed on drop.
struct TempDir(PathBuf);

impl TempDir {
    fn new(label: &str) -> Self {
        let serial = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|elapsed| elapsed.subsec_nanos())
            .unwrap_or(0);
        let path = std::env::temp_dir().join(format!(
            "vd-env-auto-{}-{serial}-{label}",
            std::process::id()
        ));
        std::fs::create_dir_all(&path)
            .unwrap_or_else(|error| panic!("creating {} failed: {error}", path.display()));
        Self(path)
    }

    fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn shell_available(program: &str) -> bool {
    std::process::Command::new(program)
        .arg("-c")
        .arg("true")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

/// The csh-family quoting must survive the characters the POSIX form breaks on.
///
/// The old form escaped only `'`. Measured on csh/tcsh 6.24: a `!` is expanded by
/// history substitution even inside single quotes, so `source '/tmp/bang!x'` fails
/// with "Event not found" — the captured environment would silently be the empty
/// shell's. The fix escapes `!` as `\!`; this test proves it by SOURCING such a
/// file for real.
#[test]
fn csh_family_capture_sources_a_path_with_apostrophe_and_bang() {
    for (kind, program) in [(ShellKind::Csh, "csh"), (ShellKind::Tcsh, "tcsh")] {
        if !shell_available(program) {
            eprintln!("SKIP: {program} is not installed on this host");
            continue;
        }
        let root = TempDir::new("quote");
        // A directory name carrying the three characters that matter: space,
        // apostrophe and history bang.
        let directory = root.path().join("it's a bang! dir");
        std::fs::create_dir_all(&directory)
            .unwrap_or_else(|error| panic!("creating {} failed: {error}", directory.display()));
        let rc = directory.join("probe.csh");
        std::fs::write(&rc, "setenv VD_CAPTURE_QUOTE_PROBE quoted-ok\n")
            .unwrap_or_else(|error| panic!("writing {} failed: {error}", rc.display()));

        let init = ShellInit {
            shell: kind,
            source_file: Some(rc.clone()),
            ..ShellInit::default()
        };
        let outcome = capture(&init, Some(&AtomicBool::new(false)))
            .unwrap_or_else(|error| panic!("{program} capture failed: {error}"));
        assert_eq!(
            outcome.exit_code, 0,
            "{program} rejected the quoted path; stderr={:?}",
            outcome.stderr
        );
        let entries = outcome.environment().to_pairs();
        assert!(
            entries
                .iter()
                .any(|(name, value)| name == "VD_CAPTURE_QUOTE_PROBE" && value == "quoted-ok"),
            "{program} did not source the quoted rc file"
        );
    }
}

/// The POSIX and fish quoting is unchanged (the csh fix must not move them).
#[test]
fn posix_quoting_stays_the_posix_form() {
    let bump = capture_argv(&ShellInit {
        shell: ShellKind::Bash,
        source_file: Some(PathBuf::from("/tmp/it's here/rc.sh")),
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    let script = bump[2].to_string_lossy();
    assert!(
        script.contains("'/tmp/it'\\''s here/rc.sh'"),
        "the POSIX form must not gain the csh `\\!` rule: {script}"
    );
}

/// The csh script text keeps the shape the older test asserted, plus the bang
/// escape.
#[test]
fn csh_script_escapes_a_bang_and_keeps_the_apostrophe_rule() {
    let argv = capture_argv(&ShellInit {
        shell: ShellKind::Tcsh,
        source_file: Some(PathBuf::from("/tmp/bang! dir/rc.csh")),
        ..ShellInit::default()
    })
    .unwrap_or_else(|error| panic!("{error}"));
    let script = argv[2].to_string_lossy();
    assert!(
        script.contains("source '/tmp/bang\\! dir/rc.csh'"),
        "a history bang must be escaped inside the csh word: {script}"
    );
    assert!(
        script.contains("set _vd_rc=$status") && script.contains("exit $_vd_rc"),
        "the source status is still saved before env runs: {script}"
    );
}

#[test]
fn detect_shell_reports_one_reason_instead_of_guessing() {
    let executable = |path: &Path| path == Path::new("/usr/bin/tcsh");

    assert_eq!(
        detect_shell(Some("/usr/bin/tcsh"), executable),
        Ok(vd_config::env::DetectedShell {
            kind: ShellKind::Tcsh,
            program: PathBuf::from("/usr/bin/tcsh"),
        })
    );
    // A login-shell spelling and a bare name are the same family.
    assert_eq!(
        ShellKind::detect("-tcsh"),
        Some(ShellKind::Tcsh),
        "a login-shell spelling still resolves"
    );
    assert_eq!(ShellKind::detect("bash"), Some(ShellKind::Bash));
    assert_eq!(ShellKind::detect("/bin/dash"), Some(ShellKind::Sh));
    assert_eq!(ShellKind::detect("nu"), None);

    // Each unusable `$SHELL` produces ONE sentence, never a guess and never a
    // panic (AC-1.2). The tests use `match` because the workspace lint denies
    // `unwrap`/`unwrap_err` in tests too.
    let diagnostic = |value: Option<&str>, executable: &dyn Fn(&Path) -> bool| match detect_shell(
        value, executable,
    ) {
        Ok(shell) => panic!("expected a diagnostic, got a capture plan for {shell:?}"),
        Err(diagnostic) => diagnostic,
    };
    let missing = diagnostic(None, &executable);
    assert!(missing.contains("$SHELL is not set"), "{missing}");
    let empty = diagnostic(Some("   "), &executable);
    assert!(empty.contains("$SHELL is not set"), "{empty}");
    let not_executable = diagnostic(Some("/bin/csh"), &executable);
    assert!(
        not_executable.contains("not an executable file"),
        "{not_executable}"
    );
    let unknown = diagnostic(Some("/usr/bin/unknown-sh"), &|_| true);
    assert!(
        unknown.contains("does not support"),
        "an unknown family is named instead of guessed: {unknown}"
    );
}

#[test]
fn auto_capture_plan_only_runs_for_the_csh_family_once() {
    let executable = |_path: &Path| true;

    match auto_capture_plan(Some("/bin/tcsh"), executable, false) {
        AutoCapturePlan::Capture(shell) => {
            assert_eq!(shell.kind, ShellKind::Tcsh);
            assert_eq!(shell.program, PathBuf::from("/bin/tcsh"));
        }
        other => panic!("expected a capture plan, got {other:?}"),
    }

    assert_eq!(
        auto_capture_plan(Some("/bin/tcsh"), executable, true),
        AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::AlreadyRecorded,
            diagnostic: None
        },
        "a recorded capture is never repeated (AC-1.1: no overwrite of user choices)"
    );
    assert_eq!(
        auto_capture_plan(Some("/bin/bash"), executable, false),
        AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::NotCshFamily,
            diagnostic: None
        },
        "contract §0#2 scopes the automatic capture to the csh family"
    );
    match auto_capture_plan(None, executable, false) {
        AutoCapturePlan::Skip {
            reason: AutoCaptureSkip::ShellUnusable,
            diagnostic: Some(diagnostic),
        } => assert!(diagnostic.contains("$SHELL is not set"), "{diagnostic}"),
        other => panic!("a missing SHELL must produce ONE diagnostic, got {other:?}"),
    }
    assert_eq!(
        AutoCaptureSkip::AlreadyRecorded.reason(),
        "already-recorded"
    );
    assert_eq!(AutoCaptureSkip::ShellUnusable.reason(), "shell-unusable");
    assert_eq!(AutoCaptureSkip::NotCshFamily.reason(), "shell-not-csh");
}

#[test]
fn auto_capture_imports_only_what_the_shell_changed() {
    let process = Env::from_pairs([("PATH", "/usr/bin"), ("HOME", "/home/alice")]);
    let captured = vec![
        ("PATH".to_string(), "/usr/bin:/rc/bin".to_string()),
        ("HOME".to_string(), "/home/alice".to_string()),
        ("VD_FROM_RC".to_string(), "yes".to_string()),
        ("_".to_string(), "/usr/bin/env".to_string()),
        ("SHLVL".to_string(), "2".to_string()),
        ("PWD".to_string(), "/tmp".to_string()),
    ];
    let (entries, skipped) = auto_capture_entries(&process, &captured);
    assert_eq!(
        entries,
        vec![
            ("PATH".to_string(), "/usr/bin:/rc/bin".to_string()),
            ("VD_FROM_RC".to_string(), "yes".to_string()),
        ],
        "an unchanged value is not the shell's contribution; bookkeeping names are dropped"
    );
    assert_eq!(skipped, 3, "three bookkeeping rows were dropped");
    assert_eq!(AUTO_CAPTURE_SKIP_NAMES.len(), 4);
}

#[test]
fn auto_capture_merge_never_overwrites_a_user_choice() {
    let mut selection = EnvironmentSelection {
        global: vec![("PATH".to_string(), "/user/chose/this".to_string())],
        projects: Default::default(),
    };
    let added = selection.add_global_missing(&[
        ("PATH".to_string(), "/rc/bin".to_string()),
        ("VD_FROM_RC".to_string(), "yes".to_string()),
    ]);
    assert_eq!(added, 1, "only the missing name is added");
    assert_eq!(
        selection
            .global
            .iter()
            .find(|(name, _)| name == "PATH")
            .map(|(_, value)| value.as_str()),
        Some("/user/chose/this"),
        "the user's PATH survives; the capture refills nothing"
    );

    // AC-1.4: the global layer applies with NO project root at all.
    let merged = selection.merged_for(None);
    assert!(
        merged
            .iter()
            .any(|(name, value)| name == "VD_FROM_RC" && value == "yes"),
        "a captured global entry applies without a project root: {merged:?}"
    );
}

#[test]
fn the_marker_sits_beside_the_selection_and_round_trips() {
    let root = TempDir::new("marker");
    let selection_path = root.path().join("environment.json");
    assert_eq!(
        auto_capture_path(&selection_path),
        root.path().join(AUTO_CAPTURE_FILE_NAME),
        "the marker is a sibling of environment.json (the Settings page rewrites that file)"
    );
    let marker = auto_capture_path(&selection_path);
    assert_eq!(AutoCaptureRecord::load(&marker), None, "no marker yet");

    let record = AutoCaptureRecord {
        shell: "tcsh".to_string(),
        program: "/usr/bin/tcsh".to_string(),
        captured_at: 1_763_000_000,
        captured: 59,
        imported: 2,
    };
    record
        .save(&marker)
        .unwrap_or_else(|error| panic!("writing the marker failed: {error}"));
    assert_eq!(AutoCaptureRecord::load(&marker), Some(record));
    let text = std::fs::read_to_string(&marker).unwrap_or_default();
    assert!(
        !text.contains("VD_FROM_RC"),
        "the marker holds counts and the shell identity, never a variable: {text}"
    );

    std::fs::write(&marker, "{ not json").unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(
        AutoCaptureRecord::load(&marker),
        None,
        "a broken marker is ignored (the capture simply runs again)"
    );
}

/// AC-1.1 end to end at the library level: a real csh capture, the changed-only
/// filter, the global merge, and the same selection applying with no project.
#[test]
fn real_csh_capture_feeds_the_global_layer() {
    if !shell_available("csh") {
        eprintln!("SKIP: csh is not installed on this host");
        return;
    }
    let root = TempDir::new("global-layer");
    let rc = root.path().join("capture.csh");
    std::fs::write(
        &rc,
        "setenv VD_GLOBAL_PROBE from-rc\nsetenv PATH /rc/only/bin:$PATH\n",
    )
    .unwrap_or_else(|error| panic!("writing {} failed: {error}", rc.display()));

    let outcome = capture(
        &ShellInit {
            shell: ShellKind::Csh,
            source_file: Some(rc),
            ..ShellInit::default()
        },
        Some(&AtomicBool::new(false)),
    )
    .unwrap_or_else(|error| panic!("csh capture failed: {error}"));
    assert_eq!(outcome.exit_code, 0, "stderr={:?}", outcome.stderr);

    // Pretend the product already has this process environment.
    let process = Env::from_pairs([
        ("PATH", "/usr/bin:/bin"),
        ("HOME", "/home/alice"),
        ("_", "/usr/bin/zed"),
    ]);
    let captured = outcome.environment().to_pairs();
    let (entries, _skipped) = auto_capture_entries(&process, &captured);
    assert!(
        entries
            .iter()
            .any(|(name, value)| name == "VD_GLOBAL_PROBE" && value == "from-rc"),
        "the rc-file variable is the shell's contribution: {entries:?}"
    );
    assert!(
        !entries.iter().any(|(name, _)| name == "_"),
        "shell bookkeeping is not imported: {entries:?}"
    );

    let mut selection = EnvironmentSelection::default();
    let added = selection.add_global_missing(&entries);
    assert_eq!(added, entries.len());

    // AC-1.4: merged with no project root, the global entries still apply.
    let merged = selection.merged_for(None);
    assert!(merged
        .iter()
        .any(|(name, value)| name == "VD_GLOBAL_PROBE" && value == "from-rc"));
    assert!(merged.iter().any(|(name, _)| name == "PATH"));

    // And a project layer still wins over the global one (unchanged semantics).
    let mut with_project = selection.clone();
    with_project.projects.insert(
        "/work/p".to_string(),
        vec![("VD_GLOBAL_PROBE".to_string(), "project-wins".to_string())],
    );
    let merged = with_project.merged_for(Some(Path::new("/work/p")));
    assert!(merged
        .iter()
        .any(|(name, value)| name == "VD_GLOBAL_PROBE" && value == "project-wins"));
}
