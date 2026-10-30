//! `--selftest` suites.
//!
//! A suite drives the REAL shell (`Shell::new`, the same activation handler,
//! the same command registry) and prints one JSON report on stdout. The exit
//! code is 0 only when every check passed, so a container or CI harness can
//! assert behaviour without extra tooling. `--probe-hold-ms` keeps the window
//! alive after the report so an external harness can query the X server.

use std::rc::Rc;
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};
use gtk::prelude::*;
use serde::Serialize;

use crate::app::Shell;
use crate::cli::{self, Options};
use crate::commands::CommandSpec;
use crate::document;

#[derive(Debug, Serialize)]
struct Check {
    name: &'static str,
    ok: bool,
    detail: String,
}

#[derive(Debug, Serialize)]
struct Report<'a> {
    suite: &'a str,
    result: &'a str,
    app_version: &'a str,
    gtk_version: String,
    glib_version: String,
    checks: Vec<Check>,
}

/// Entry point for `--selftest <suite>`.
///
/// # Errors
/// Returns an error for an unknown suite, when the shell cannot start (no
/// display), when the report cannot be serialized, or when a check failed.
pub fn run(suite: &str, opts: &Options) -> Result<()> {
    match suite {
        "list" => {
            for name in cli::SELFTEST_SUITES {
                println!("{name}");
            }
            Ok(())
        }
        "smoke" => run_smoke(opts),
        other => bail!("unknown selftest suite '{other}'"),
    }
}

fn check(name: &'static str, ok: bool, detail: impl Into<String>) -> Check {
    Check {
        name,
        ok,
        detail: detail.into(),
    }
}

fn run_smoke(opts: &Options) -> Result<()> {
    let shell = Shell::new(opts);
    shell.register_and_activate()?;

    let ui_slot = shell.ui_slot();
    let context = glib::MainContext::default();

    let mapped = pump_until(&context, Duration::from_secs(10), || {
        ui_slot
            .borrow()
            .as_ref()
            .is_some_and(|ui| ui.session.window.is_mapped())
    });
    // Let the first frame paint and the syntax highlighter run its idle pass.
    pump_for(&context, Duration::from_millis(300));

    let snapshot = {
        let slot = ui_slot.borrow();
        slot.as_ref().map(|ui| {
            (
                ui.session.window.title().map(|t| t.to_string()),
                ui.session.view.is_mapped(),
                ui.session.status.text().to_string(),
                ui.commands.specs().to_vec(),
                Rc::clone(&ui.session),
            )
        })
    };

    let title = snapshot
        .as_ref()
        .and_then(|s| s.0.clone())
        .unwrap_or_default();
    let editor_ok = snapshot.as_ref().is_some_and(|s| s.1);
    let status_text = snapshot.as_ref().map(|s| s.2.clone()).unwrap_or_default();
    let specs: Vec<CommandSpec> = snapshot.as_ref().map(|s| s.3.clone()).unwrap_or_default();
    let session = snapshot.as_ref().map(|s| Rc::clone(&s.4));

    let mut checks = Vec::new();
    checks.push(check(
        "window_created",
        snapshot.is_some(),
        if snapshot.is_some() {
            "activation created the main window".to_string()
        } else {
            "activation did not create a window".to_string()
        },
    ));
    checks.push(check(
        "window_mapped",
        mapped,
        if mapped {
            "window is mapped by the X server".to_string()
        } else {
            "window was not mapped within 10s".to_string()
        },
    ));
    checks.push(check(
        "window_title",
        title.contains(cli::APP_NAME),
        format!("title: {title:?}"),
    ));
    checks.push(check(
        "editor_widget",
        editor_ok,
        if editor_ok {
            "GtkSourceView widget is mapped in the window".to_string()
        } else {
            "editor widget is not mapped".to_string()
        },
    ));
    checks.push(check(
        "status_bar",
        !status_text.is_empty(),
        format!("status: {status_text:?}"),
    ));

    let expected: [(&str, &str); 3] = [
        ("open", "<Primary>o"),
        ("save", "<Primary>s"),
        ("quit", "<Primary>q"),
    ];
    let mut command_problems = Vec::new();
    for (id, accel) in expected {
        match specs.iter().find(|spec| spec.id == id) {
            Some(spec) if spec.accels.contains(&accel) => {}
            Some(_) => command_problems.push(format!("{id}: wrong accelerator")),
            None => command_problems.push(format!("{id}: not registered")),
        }
    }
    checks.push(check(
        "commands_registered",
        command_problems.is_empty(),
        if command_problems.is_empty() {
            // The label is part of the public registry API (menus/help will read
            // it); printing it here also keeps the API honest under -D warnings.
            let described: Vec<String> = specs
                .iter()
                .map(|spec| format!("{} ({})", spec.id, spec.label))
                .collect();
            format!("commands: {}", described.join(", "))
        } else {
            format!("problems: {}", command_problems.join("; "))
        },
    ));

    let actions: Vec<String> = shell
        .app()
        .list_actions()
        .iter()
        .map(|action| action.to_string())
        .collect();
    let actions_ok = ["open", "save", "quit"]
        .iter()
        .all(|id| actions.iter().any(|action| action == id));
    checks.push(check(
        "actions_installed",
        actions_ok,
        format!("application actions: {}", actions.join(", ")),
    ));

    let accels: Vec<String> = shell
        .app()
        .accels_for_action("app.open")
        .iter()
        .map(|accel| accel.to_string())
        .collect();
    checks.push(check(
        "accelerator_applied",
        accels.iter().any(|accel| accel == "<Primary>o"),
        format!("app.open accelerators: {}", accels.join(", ")),
    ));

    match &session {
        Some(session) => match document_roundtrip(&session.buffer) {
            Ok((ok, detail)) => checks.push(check("document_roundtrip", ok, detail)),
            Err(err) => checks.push(check("document_roundtrip", false, format!("{err:#}"))),
        },
        None => checks.push(check(
            "document_roundtrip",
            false,
            "no editor session to round-trip through",
        )),
    }

    let gtk_version = format!(
        "{}.{}.{}",
        gtk::major_version(),
        gtk::minor_version(),
        gtk::micro_version()
    );
    // glib 0.18 exposes only check_version(); per the GLib convention it
    // returns None when the runtime GLib IS compatible and a mismatch string
    // otherwise.
    let glib_mismatch = glib::check_version(2, 56, 0).map(|problem| problem.to_string());
    let glib_ok = glib_mismatch.is_none();
    let glib_detail = match &glib_mismatch {
        Some(problem) => format!("incompatible: {problem}"),
        None => ">= 2.56".to_string(),
    };
    checks.push(check(
        "toolkit_versions",
        !gtk_version.is_empty() && glib_ok,
        format!("gtk {gtk_version}; glib {glib_detail}"),
    ));

    let failed = checks.iter().filter(|check| !check.ok).count();
    let report = Report {
        suite: "smoke",
        result: if failed == 0 { "pass" } else { "fail" },
        app_version: env!("CARGO_PKG_VERSION"),
        gtk_version,
        glib_version: glib_detail,
        checks,
    };
    println!(
        "{}",
        serde_json::to_string(&report).context("serializing the selftest report")?
    );

    if opts.probe_hold_ms > 0 {
        pump_for(&context, Duration::from_millis(opts.probe_hold_ms));
    }

    if failed == 0 {
        Ok(())
    } else {
        bail!("selftest 'smoke': {failed} check(s) failed")
    }
}

/// Write a sample SV file, load it into the real buffer, save it back and
/// compare. Exercises the same helpers the Open/Save commands use.
fn document_roundtrip(buffer: &sourceview4::Buffer) -> Result<(bool, String)> {
    let dir = std::env::temp_dir().join(format!("vedit-selftest-{}", std::process::id()));
    std::fs::create_dir_all(&dir)
        .with_context(|| format!("creating test directory {}", dir.display()))?;
    let input = dir.join("input.sv");
    let output = dir.join("output.sv");
    let sample = "module selftest_top;\n  initial $display(\"selftest\");\nendmodule\n";
    std::fs::write(&input, sample).with_context(|| format!("writing {}", input.display()))?;
    document::load_into_buffer(buffer, &input)?;
    document::save_from_buffer(buffer, &output)?;
    let written = std::fs::read_to_string(&output)
        .with_context(|| format!("reading {}", output.display()))?;
    if let Err(err) = std::fs::remove_dir_all(&dir) {
        eprintln!("WARN: could not remove {}: {err}", dir.display());
    }
    Ok((
        written == sample,
        format!(
            "{} bytes round-tripped through {}",
            written.len(),
            output.display()
        ),
    ))
}

/// Pump the default main context until `condition` holds or the timeout expires.
fn pump_until(
    context: &glib::MainContext,
    timeout: Duration,
    mut condition: impl FnMut() -> bool,
) -> bool {
    let deadline = Instant::now() + timeout;
    loop {
        while context.iteration(false) {}
        if condition() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Pump the default main context for a fixed duration.
fn pump_for(context: &glib::MainContext, duration: Duration) {
    let deadline = Instant::now() + duration;
    while Instant::now() < deadline {
        while context.iteration(false) {}
        std::thread::sleep(Duration::from_millis(2));
    }
}
