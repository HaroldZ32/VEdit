//! T8 real-tool transport evidence (batch 20261110b).
//!
//! The engine's OWN argv (`command_args_for`) is executed against the pinned
//! real tools, and the OUTCOME is measured: a patch mode leaves the file's
//! sha256 untouched, `inplace` rewrites it, `generate-waiver` writes the waiver
//! document, slang's `--waiver-file <toml>` really suppresses a finding, and an
//! unknown waiver key is refused by the tool exactly as the product-side
//! validator refuses it.
//!
//! Binaries follow the house pattern of `real_tools.rs`: `VEDIT_SLANG` or
//! `build/slang/out/slang`, `VEDIT_VERIBLE_LINT` or
//! `.mpd/recon/verible/bin/verible-verilog-lint`. A missing tool fails loudly
//! with the remediation command instead of skipping silently.
//!
//! Run explicitly for the evidence log:
//! `VEDIT_SLANG=$PWD/.mpd/recon/slang/slang
//!  VEDIT_VERIBLE_LINT=$PWD/.mpd/recon/verible/bin/verible-verilog-lint
//!  cargo +stable test -p vd-deeplint --test real_transport -- --nocapture`

use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use vd_deeplint::{command_args_for, lint_with, EngineOptions, LintEngine, VeribleAutofix};

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn slang_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SLANG") {
        return PathBuf::from(value);
    }
    let candidate = repo_root().join("build/slang/out/slang");
    assert!(
        candidate.is_file(),
        "slang artifact not found: set VEDIT_SLANG or run `bash build/slang/build.sh` \
         (looked at {})",
        candidate.display()
    );
    candidate
}

fn verible_binary() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_VERIBLE_LINT") {
        return PathBuf::from(value);
    }
    let candidate = repo_root().join(".mpd/recon/verible/bin/verible-verilog-lint");
    assert!(
        candidate.is_file(),
        "verible-verilog-lint not found: set VEDIT_VERIBLE_LINT or run \
         `bash scripts/fetch-verible.sh` (looked at {})",
        candidate.display()
    );
    candidate
}

/// The sha256 of one file, through the host's `sha256sum`.
fn sha256(path: &Path) -> String {
    let output = Command::new("sha256sum")
        .arg(path)
        .output()
        .unwrap_or_else(|error| panic!("sha256sum could not run: {error}"));
    assert!(
        output.status.success(),
        "sha256sum failed on {}",
        path.display()
    );
    String::from_utf8_lossy(&output.stdout)
        .split_whitespace()
        .next()
        .unwrap_or_else(|| panic!("sha256sum printed nothing for {}", path.display()))
        .to_string()
}

/// The argv as text (assertions compare this, so no `OsString` equality
/// subtleties can hide a mismatch).
fn rendered(args: &[OsString]) -> Vec<String> {
    args.iter()
        .map(|argument| argument.to_string_lossy().into_owned())
        .collect()
}

/// Print the exact argv the engine built (the evidence line).
fn report_argv(label: &str, binary: &Path, args: &[OsString]) {
    println!(
        "T8-ARGV {label}: {} {}",
        binary.display(),
        rendered(args).join(" ")
    );
}

/// Run the engine's argv against the real tool, from `dir`.
fn run(binary: &Path, args: &[OsString], dir: &Path) -> std::process::Output {
    Command::new(binary)
        .args(args)
        .current_dir(dir)
        .output()
        .unwrap_or_else(|error| panic!("{} could not start: {error}", binary.display()))
}

fn write(path: &Path, text: &str) {
    fs::write(path, text).unwrap_or_else(|error| panic!("write {}: {error}", path.display()));
}

/// A temporary directory that removes itself.
struct Scratch(tempfile::TempDir);

impl Scratch {
    fn new(label: &str) -> Self {
        Self(
            tempfile::Builder::new()
                .prefix(label)
                .tempdir()
                .unwrap_or_else(|error| panic!("temporary directory: {error}")),
        )
    }

    fn path(&self) -> &Path {
        self.0.path()
    }
}

#[test]
fn verible_autofix_patch_reports_the_diff_and_leaves_the_file_alone() {
    let scratch = Scratch::new("vd-t8-patch-");
    let file = scratch.path().join("fixme.sv");
    write(
        &file,
        "module fixme;\n  wire a;   \n  assign a = 0;\nendmodule\n",
    );
    let before = sha256(&file);
    let options = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_binary()),
        autofix: Some(VeribleAutofix::Patch),
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Verible, &file, None, &options);
    report_argv("autofix=patch", &verible_binary(), &args);
    assert!(
        rendered(&args).contains(&"--autofix=patch".to_string()),
        "the patch mode must be in argv: {args:?}"
    );
    let output = run(&verible_binary(), &args, scratch.path());
    let stdout = String::from_utf8_lossy(&output.stdout);
    let after = sha256(&file);
    println!(
        "T8-RESULT autofix=patch exit={:?} sha256 {} -> {}",
        output.status.code(),
        before,
        after
    );
    println!("T8-OUTPUT autofix=patch:\n{stdout}");
    assert_eq!(before, after, "patch must NOT modify the file");
    assert!(
        stdout.contains("--- a/") && stdout.contains("+++ b/"),
        "patch prints a unified diff: {stdout}"
    );
}

#[test]
fn verible_autofix_inplace_rewrites_the_file() {
    let scratch = Scratch::new("vd-t8-inplace-");
    let file = scratch.path().join("fixme.sv");
    write(
        &file,
        "module fixme;\n  wire a;   \n  assign a = 0;\nendmodule\n",
    );
    let before = sha256(&file);
    let options = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_binary()),
        autofix: Some(VeribleAutofix::Inplace),
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Verible, &file, None, &options);
    report_argv("autofix=inplace", &verible_binary(), &args);
    assert!(
        rendered(&args).contains(&"--autofix=inplace".to_string()),
        "{args:?}"
    );
    let output = run(&verible_binary(), &args, scratch.path());
    let after = sha256(&file);
    let text = fs::read_to_string(&file)
        .unwrap_or_else(|error| panic!("read {}: {error}", file.display()));
    println!(
        "T8-RESULT autofix=inplace exit={:?} sha256 {} -> {}",
        output.status.code(),
        before,
        after
    );
    println!("T8-FILE autofix=inplace:\n{text}");
    assert_ne!(before, after, "inplace must rewrite the file");
    assert!(
        text.contains("  wire a;\n") && !text.contains("wire a;   "),
        "the trailing spaces are fixed: {text:?}"
    );
}

#[test]
fn verible_autofix_generate_waiver_writes_the_registered_document() {
    let scratch = Scratch::new("vd-t8-waiver-");
    let file = scratch.path().join("waive.sv");
    write(
        &file,
        "module waive;\n  wire a;   \n  assign a = 0;\nendmodule\n",
    );
    let before = sha256(&file);
    let waiver_path = scratch.path().join("generated.waiver");
    let options = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_binary()),
        autofix: Some(VeribleAutofix::GenerateWaiver),
        autofix_output_file: Some(waiver_path.clone()),
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Verible, &file, None, &options);
    report_argv("autofix=generate-waiver", &verible_binary(), &args);
    let argv = rendered(&args);
    assert!(
        argv.contains(&"--autofix=generate-waiver".to_string()),
        "{args:?}"
    );
    assert!(
        argv.contains(&format!("--autofix_output_file={}", waiver_path.display())),
        "the output file rides in ONE argv element: {args:?}"
    );
    let output = run(&verible_binary(), &args, scratch.path());
    let after = sha256(&file);
    let written = fs::read_to_string(&waiver_path)
        .unwrap_or_else(|error| panic!("read {}: {error}", waiver_path.display()));
    println!(
        "T8-RESULT autofix=generate-waiver exit={:?} sha256 {} -> {}",
        output.status.code(),
        before,
        after
    );
    println!("T8-FILE generate-waiver:\n{written}");
    assert_eq!(before, after, "generate-waiver must not modify the source");
    assert!(
        written.contains("waive --rule=no-trailing-spaces") && written.contains("--line=2"),
        "the product writes real verible waivers: {written}"
    );
}

#[test]
fn the_generated_waiver_is_a_legal_line_the_same_tool_consumes() {
    // AC-5.2/AC-T8.5: generate -> register -> EFFECT. Step 1 writes the waiver
    // through the engine's argv; step 2 feeds the SAME file back through
    // `--waiver_files=` and asserts the finding really disappears.
    let scratch = Scratch::new("vd-t8-chain-");
    let file = scratch.path().join("waive.sv");
    write(
        &file,
        "module waive;\n  wire a;   \n  assign a = 0;\nendmodule\n",
    );
    let waiver_path = scratch.path().join("chain.waiver");

    let generate = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_binary()),
        autofix: Some(VeribleAutofix::GenerateWaiver),
        autofix_output_file: Some(waiver_path.clone()),
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Verible, &file, None, &generate);
    report_argv("generate-waiver (chain)", &verible_binary(), &args);
    let generated = run(&verible_binary(), &args, scratch.path());
    println!(
        "T8-RESULT chain generate exit={:?}",
        generated.status.code()
    );
    let written = fs::read_to_string(&waiver_path)
        .unwrap_or_else(|error| panic!("read {}: {error}", waiver_path.display()));
    println!("T8-FILE chain waiver:\n{written}");
    let lines: Vec<&str> = written.lines().map(str::trim).collect();
    assert_eq!(
        lines.len(),
        1,
        "one waiver line, not a transcript: {written:?}"
    );
    // The exact verible waiver grammar the ABI writer produces.
    let line = lines[0];
    assert!(
        line.starts_with("waive --rule=no-trailing-spaces --line=2 --location=\""),
        "the generated line is a legal verible waiver: {line:?}"
    );
    assert!(line.ends_with("waive.sv\""), "{line:?}");

    // Step 2: consume it through the engine's other waiver knob.
    let consume = EngineOptions {
        engine: LintEngine::Verible,
        verible_binary: Some(verible_binary()),
        waiver_files: vec![waiver_path.clone()],
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Verible, &file, None, &consume);
    report_argv("--waiver_files (chain)", &verible_binary(), &args);
    assert!(
        rendered(&args).contains(&format!("--waiver_files={}", waiver_path.display())),
        "ONE argv element: {args:?}"
    );
    let consumed = run(&verible_binary(), &args, scratch.path());
    let stdout = String::from_utf8_lossy(&consumed.stdout);
    println!(
        "T8-RESULT chain consume exit={:?} stdout={stdout:?}",
        consumed.status.code()
    );
    assert!(
        consumed.status.success(),
        "the tool accepts the generated waiver file"
    );
    assert!(
        !stdout.contains("no-trailing-spaces"),
        "the generated waiver really suppresses the finding: {stdout:?}"
    );
}

#[test]
fn the_interactive_autofix_values_keep_their_own_flag() {
    // The engine never rewrites a stored value: the product marks these two
    // unavailable (no TTY) in the page, and the transport still names exactly
    // what the user stored.
    for value in [
        VeribleAutofix::PatchInteractive,
        VeribleAutofix::InplaceInteractive,
    ] {
        let options = EngineOptions {
            engine: LintEngine::Verible,
            verible_binary: Some(verible_binary()),
            autofix: Some(value),
            ..EngineOptions::default()
        };
        let args = command_args_for(LintEngine::Verible, Path::new("f.sv"), None, &options);
        let expected = format!("--autofix={}", value.as_str());
        assert!(
            rendered(&args).contains(&expected),
            "{expected} must be transported: {args:?}"
        );
        assert!(value.is_interactive());
    }
}

#[test]
fn slang_waiver_file_suppresses_the_warning_and_unknown_keys_are_refused() {
    let scratch = Scratch::new("vd-t8-slang-waiver-");
    let file = scratch.path().join("slangprobe.sv");
    write(&file, "module slangprobe;\n  assign x = 1;\nendmodule\n");
    let waiver_path = scratch.path().join("waivers.toml");
    write(
        &waiver_path,
        "[[waivers]]\nfile = \"**/slangprobe.sv\"\ndiagnostic = \"implicit-net\"\n",
    );
    let json_path = scratch.path().join("baseline.json");
    let waived_json = scratch.path().join("waived.json");

    // Baseline: the finding is there, and it carries the TYPED option name that
    // the crate lifts from slang's own `optionName` (AC-5.8).
    let baseline_options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(slang_binary()),
        ..EngineOptions::default()
    };
    let report = lint_with(&file, &baseline_options);
    let finding = report
        .diagnostics
        .iter()
        .find(|diagnostic| diagnostic.code == "ImplicitNet")
        .unwrap_or_else(|| panic!("slang must report ImplicitNet: {:?}", report.diagnostics));
    println!(
        "T8-RESULT optionName: code={} option_name={:?} severity={:?}",
        finding.code, finding.option_name, finding.severity
    );
    assert_eq!(finding.option_name.as_deref(), Some("implicit-net"));

    // The real child, run with the engine's own argv, really suppresses it.
    let options = EngineOptions {
        engine: LintEngine::Slang,
        slang_binary: Some(slang_binary()),
        slang_waiver_file: Some(waiver_path.clone()),
        ..EngineOptions::default()
    };
    let args = command_args_for(LintEngine::Slang, &file, Some(&waived_json), &options);
    report_argv("slang --waiver-file", &slang_binary(), &args);
    let argv = rendered(&args);
    let has_flag = argv
        .windows(2)
        .any(|pair| pair[0] == "--waiver-file" && pair[1] == waiver_path.display().to_string());
    assert!(
        has_flag,
        "slang gets --waiver-file <toml> as TWO tokens: {args:?}"
    );
    let output = run(&slang_binary(), &args, scratch.path());
    println!(
        "T8-RESULT slang --waiver-file exit={:?} stderr={}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(
        output.status.success(),
        "slang must accept the waiver document"
    );
    let text = fs::read_to_string(&waived_json)
        .unwrap_or_else(|error| panic!("read {}: {error}", waived_json.display()));
    let waived: serde_json::Value =
        serde_json::from_str(&text).unwrap_or_else(|error| panic!("parse {text}: {error}"));
    let count = waived.as_array().map_or(0, Vec::len);
    println!("T8-RESULT waived diagnostics: {count}");
    assert_eq!(count, 0, "the waived warning must disappear: {text}");

    // Negative control: the SAME document with an unknown key is refused by the
    // product validator AND by slang itself (measured: exit 1).
    let bad_path = scratch.path().join("bad.toml");
    write(
        &bad_path,
        "[[waivers]]\nfile = \"**/slangprobe.sv\"\nbogus = \"x\"\n",
    );
    let accepted = vd_deeplint::validate_slang_waiver_file(&waiver_path)
        .unwrap_or_else(|error| panic!("the written document must validate: {error}"));
    println!("T8-RESULT product validator: {accepted} waiver block(s) accepted");
    let refused = vd_deeplint::validate_slang_waiver_file(&bad_path)
        .err()
        .unwrap_or_else(|| panic!("an unknown key must be refused by the product validator"));
    println!("T8-RESULT product validator refusal: {refused}");
    let missing = vd_deeplint::validate_slang_waiver_file(&scratch.path().join("missing.toml"))
        .err()
        .unwrap_or_else(|| panic!("a missing document must be refused"));
    println!("T8-RESULT missing document: {missing}");
    let bad_args = {
        let mut options = options.clone();
        options.slang_waiver_file = Some(bad_path.clone());
        command_args_for(LintEngine::Slang, &file, Some(&json_path), &options)
    };
    let bad_output = run(&slang_binary(), &bad_args, scratch.path());
    let bad_stderr = String::from_utf8_lossy(&bad_output.stderr);
    println!(
        "T8-RESULT slang unknown key: exit={:?} stderr={bad_stderr}",
        bad_output.status.code()
    );
    assert!(
        !bad_output.status.success() && bad_stderr.contains("unknown key"),
        "slang refuses an unknown key: {bad_stderr}"
    );
}
