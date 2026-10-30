//! `--selftest` suites. Each suite drives the SAME production paths the user
//! drives: real tabs, the real `vd_editor::Document`, the real Verible LSP
//! client, the real SLANG binary and the real FIM service (which is disabled by
//! default). Nothing here fakes a lane: a missing binary makes the suite fail
//! instead of pretending.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context as _, Result};
use gtk::prelude::*;
use sourceview4::prelude::*;

use vd_editor::{Position, Range};

use crate::app::{App, Ui};
use crate::cli::{Options, SELFTEST_SUITES};
use crate::commands;
use crate::config_glue;
use crate::diagnostics::{self, DiagnosticSource, Severity, ViewDiagnostic};

/// One observed check.
struct Check {
    name: String,
    ok: bool,
    detail: String,
}

/// One suite's checks.
struct SuiteReport {
    suite: String,
    checks: Vec<Check>,
}

impl SuiteReport {
    fn new(name: &str) -> Self {
        Self {
            suite: name.to_string(),
            checks: Vec::new(),
        }
    }

    fn check(&mut self, name: &str, ok: bool, detail: impl Into<String>) {
        self.checks.push(Check {
            name: name.to_string(),
            ok,
            detail: detail.into(),
        });
    }

    fn ok(&self) -> bool {
        self.checks.iter().all(|check| check.ok)
    }
}

/// Root of the selftest fixtures.
///
/// Runtime-overridable so a packaged build can point at its payload
/// (`VEDIT_SELFTEST_FIXTURES=<share>/vedit/testdata`): the compile-time path is
/// only the source-tree fallback and MUST NOT be the only source, otherwise
/// `--selftest` can only ever pass inside the build directory.
fn fixture_root() -> PathBuf {
    if let Some(value) = std::env::var_os("VEDIT_SELFTEST_FIXTURES") {
        if !value.is_empty() {
            return PathBuf::from(value);
        }
    }
    // No compile-time path: `env!("CARGO_MANIFEST_DIR")` would embed the build
    // machine's absolute path in the shipped ELF. Discover the source-tree
    // layout at runtime instead; packaged builds set VEDIT_SELFTEST_FIXTURES.
    let relative = Path::new("crates/vd-app/tests/fixtures");
    if let Ok(cwd) = std::env::current_dir() {
        let candidate = cwd.join(relative);
        if candidate.is_dir() {
            return candidate;
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        for ancestor in exe.ancestors().skip(1).take(6) {
            let candidate = ancestor.join(relative);
            if candidate.is_dir() {
                return candidate;
            }
        }
    }
    PathBuf::from(relative)
}

/// Run a suite (or every suite with `all`).
///
/// # Errors
/// Returns an error when the window cannot start or any check failed.
pub fn run(suite: &str, opts: &Options) -> Result<()> {
    if suite == "list" {
        let mut names = SELFTEST_SUITES.to_vec();
        names.push("all");
        println!("{}", names.join("\n"));
        return Ok(());
    }
    let root = fixture_root();
    eprintln!(
        "vd-app selftest: fixture_root={} (VEDIT_SELFTEST_FIXTURES={:?})",
        root.display(),
        std::env::var_os("VEDIT_SELFTEST_FIXTURES")
    );
    let fixtures = Fixtures::create()?;
    let app = App::new(opts);
    app.register_and_activate()?;
    let ui = wait_for_ui(&app, Duration::from_secs(10))
        .ok_or_else(|| anyhow!("the GTK window did not start"))?;
    pump(200);

    let suites: Vec<&str> = if suite == "all" {
        SELFTEST_SUITES.to_vec()
    } else {
        vec![suite]
    };
    let mut reports = Vec::new();
    for name in suites {
        reports.push(run_suite(&ui, name, &fixtures));
    }

    if let Some(path) = &opts.screenshot {
        let (ok, detail) = match ui.capture_screenshot(path) {
            Ok(summary) => {
                eprintln!(
                    "vd-app: screenshot written to {} ({} bytes, {}x{}, {} sampled colours)",
                    path.display(),
                    summary.bytes,
                    summary.width,
                    summary.height,
                    summary.sampled_colours
                );
                // `capture_screenshot` already refuses to write a uniform image
                // (pixel samples checked before writing), which makes this an
                // independent of gdk-pixbuf loaders assertion: the clean room has
                // no PNG loader module, so reloading the file there would fail
                // for reasons unrelated to the image.
                let ok = summary.bytes > 1_000
                    && summary.sampled_colours >= 4
                    && summary.channel_range >= 8;
                (
                    ok,
                    format!(
                        "path={} bytes={} size={}x{} sampled_colours={} channel_range={}",
                        path.display(),
                        summary.bytes,
                        summary.width,
                        summary.height,
                        summary.sampled_colours,
                        summary.channel_range
                    ),
                )
            }
            Err(error) => (false, format!("capture failed: {error:#}")),
        };
        if let Some(first) = reports.first_mut() {
            first.checks.push(Check {
                name: "--screenshot produces a non-blank PNG".to_string(),
                ok,
                detail,
            });
        }
    }
    if opts.probe_hold_ms > 0 {
        pump(opts.probe_hold_ms);
    }

    let ok = reports.iter().all(SuiteReport::ok);
    let report = serde_json::json!({
        "app": crate::cli::APP_NAME,
        "version": env!("CARGO_PKG_VERSION"),
        "suite": suite,
        "ok": ok,
        "fixture_root": root.display().to_string(),
        "shortcuts": commands::SHORTCUTS.len(),
        "shortcut_table": commands::table_text(),
        "reports": reports.iter().map(|report| serde_json::json!({
            "suite": report.suite,
            "ok": report.ok(),
            "checks": report.checks.iter().map(|check| serde_json::json!({
                "name": check.name,
                "ok": check.ok,
                "detail": check.detail,
            })).collect::<Vec<_>>(),
        })).collect::<Vec<_>>(),
    });
    println!(
        "{}",
        serde_json::to_string_pretty(&report)
            .unwrap_or_else(|error| format!("{{\"error\":\"{error}\"}}"))
    );

    ui.window.close();
    pump(100);
    app.quit();
    fixtures.cleanup();
    if !ok {
        bail!("selftest '{suite}' failed; see the JSON report above");
    }
    Ok(())
}

fn run_suite(ui: &std::rc::Rc<Ui>, suite: &str, fixtures: &Fixtures) -> SuiteReport {
    match suite {
        "smoke" => smoke(ui, fixtures),
        "highlight" => highlight(ui, fixtures),
        "config" => config(ui),
        "lsp" => lsp(ui, fixtures),
        "slang" => slang(ui, fixtures),
        "fim" => fim(ui),
        other => {
            let mut report = SuiteReport::new(other);
            report.check("known suite", false, format!("unknown suite {other}"));
            report
        }
    }
}

fn smoke(ui: &std::rc::Rc<Ui>, fixtures: &Fixtures) -> SuiteReport {
    let mut report = SuiteReport::new("smoke");
    pump(300);
    report.check(
        "window realized",
        ui.window.is_realized(),
        format!("realized={}", ui.window.is_realized()),
    );
    report.check(
        "notebook has an initial tab",
        ui.notebook.n_pages() >= 1,
        format!("pages={}", ui.notebook.n_pages()),
    );

    let clean = ui.open_path(&fixtures.clean);
    match clean {
        Ok(tab) => {
            report.check(
                "open .sv applies the SystemVerilog language",
                tab.language
                    .borrow()
                    .as_deref()
                    .is_some_and(|language| language.contains("systemverilog")),
                format!("language={:?}", tab.language.borrow()),
            );
        }
        Err(error) => report.check("open clean fixture", false, format!("{error:#}")),
    }
    match ui.open_path(&fixtures.format_me) {
        Ok(tab) => {
            let page = ui.notebook.page_num(&tab.widget);
            let second_page = ui.state.tabs.borrow().len().saturating_sub(1) as u32;
            ui.notebook
                .set_current_page(Some(page.unwrap_or(second_page)));
            pump(20);
            report.check(
                "the current tab follows the opened file",
                ui.notebook.current_page() == page,
                format!(
                    "current={:?}, expected={:?}, titles={:?}",
                    ui.notebook.current_page(),
                    page,
                    ui.state
                        .tabs
                        .borrow()
                        .iter()
                        .map(|tab| tab.title())
                        .collect::<Vec<_>>()
                ),
            );
            let original = tab.text();
            tab.document
                .insert(Position::new(0, 0), "-- smoke edit\n")
                .unwrap_or_else(|error| panic!("insert failed: {error}"));
            report.check(
                "edit marks the document modified",
                tab.document.needs_close_confirmation(),
                "needs_close_confirmation=true",
            );
            if let Some(page) = ui.notebook.page_num(&tab.widget) {
                ui.notebook.set_current_page(Some(page));
                pump(20);
            }
            let can_undo = tab.document.can_undo();
            ui.window
                .activate_action("undo", None::<&gtk::glib::Variant>);
            pump(50);
            let status_after_win = ui.status_text();
            let mut restored_by_action = tab.text() == original;
            let mut dispatch = "undo";
            if !restored_by_action {
                ui.window
                    .activate_action("win.undo", None::<&gtk::glib::Variant>);
                pump(50);
                if tab.text() == original {
                    restored_by_action = true;
                    dispatch = "win.undo";
                }
            }
            let action_b = if restored_by_action {
                true
            } else {
                let undone = tab.document.undo();
                pump(20);
                report.check(
                    "Document::undo works directly (diagnostic)",
                    undone && tab.text() == original,
                    format!("undone={undone}, text_now={:?}", tab.text()),
                );
                undone && tab.text() == original
            };
            report.check(
                "undo shortcut restores the text through Document",
                restored_by_action,
                format!(
                    "restored={restored_by_action}, dispatch={dispatch}, action_fallback={action_b}, can_undo_before={can_undo}, status={status_after_win:?}"
                ),
            );
            let saved = fixtures.dir.join("smoke-saved.sv");
            match tab.save_as(&saved) {
                Ok(()) => report.check(
                    "save-as writes the document",
                    std::fs::read_to_string(&saved).is_ok_and(|text| text == tab.text()),
                    format!("path={}", saved.display()),
                ),
                Err(error) => report.check("save-as", false, format!("{error:#}")),
            }
        }
        Err(error) => report.check("open format fixture", false, format!("{error:#}")),
    }
    report.check(
        "several tabs are open",
        ui.notebook.n_pages() >= 2,
        format!("pages={}", ui.notebook.n_pages()),
    );

    // Problem panel + gutter marks share one model.
    if let Some(page) = ui
        .state
        .tabs
        .borrow()
        .iter()
        .find(|tab| tab.title() == "smoke-saved.sv")
        .and_then(|tab| ui.notebook.page_num(&tab.widget))
    {
        ui.notebook.set_current_page(Some(page));
        pump(20);
    }
    if let Some(tab) = ui.current_tab() {
        tab.store.borrow_mut().set_slang(vec![ViewDiagnostic {
            source: DiagnosticSource::Slang,
            severity: Severity::Error,
            message: "smoke diagnostic".to_string(),
            code: Some("Smoke".to_string()),
            range: Some(Range::new(Position::new(0, 0), Position::new(0, 1))),
        }]);
    }
    ui.refresh_panel();
    pump(20);
    report.check(
        "problem panel lists the diagnostic",
        !ui.panel_rows.borrow().is_empty() && ui.panel_revealer.reveals_child(),
        format!(
            "rows={}, panel_texts={:?}",
            ui.panel_rows.borrow().len(),
            ui.panel_rows
                .borrow()
                .iter()
                .map(ViewDiagnostic::panel_text)
                .collect::<Vec<_>>()
        ),
    );
    if let Some(tab) = ui.current_tab() {
        let all_marks = tab.buffer.source_marks_at_line(0, None);
        let categories: Vec<String> = all_marks
            .iter()
            .map(|mark| {
                mark.category()
                    .map_or_else(|| "?".to_string(), |category| category.to_string())
            })
            .collect();
        report.check(
            "gutter mark is placed at the diagnostic line",
            !all_marks.is_empty() && categories.iter().any(|category| category == "vedit-error"),
            format!(
                "all_marks={}, categories={categories:?}, worst={:?}, line_count={}",
                all_marks.len(),
                tab.store.borrow().worst_severity_per_line(),
                tab.document.line_count()
            ),
        );
    }

    // Shortcuts installed as window actions with the contract accelerators.
    let contract_ok = [
        "format",
        "slang",
        "definition",
        "references",
        "outline",
        "fim",
    ]
    .iter()
    .all(|id| commands::shortcut(id).is_some());
    report.check(
        "the contract shortcuts exist in the table",
        contract_ok,
        "format/slang/definition/references/outline/fim",
    );
    let actions = ui.window.list_actions();
    let expected_actions = [
        "format",
        "slang",
        "definition",
        "references",
        "outline",
        "fim",
    ];
    report.check(
        "shortcut actions are installed",
        expected_actions
            .iter()
            .all(|id| actions.iter().any(|action| action == id)),
        format!("actions={actions:?}"),
    );
    if let Some(application) = ui.window.application() {
        let accel = application.accels_for_action("win.format");
        let format_accel_ok = accel.iter().any(|value| {
            let text = value.to_string().to_lowercase();
            text.contains("shift")
                && text.contains('f')
                && (text.contains("primary") || text.contains("ctrl"))
        });
        report.check(
            "format accelerator is Ctrl+Shift+F",
            format_accel_ok,
            format!("accels={accel:?}"),
        );
    }

    // AC-04: goto-line moves the cursor (the Ctrl+G dialog calls the same path).
    if let Some(tab) = ui.current_tab() {
        let last_line = tab.document.line_count().saturating_sub(1);
        let jumped = ui.goto_line_now(1);
        pump(20);
        let at_first = tab.cursor() == Position::new(0, 0);
        let clamped = ui.goto_line_now(999);
        pump(20);
        let at_last = tab.cursor() == Position::new(last_line, 0);
        report.check(
            "goto-line moves the cursor and clamps to the last line",
            jumped && at_first && clamped && at_last,
            format!(
                "jumped={jumped}, at_first={at_first}, clamped={clamped}, at_last={at_last}, cursor={:?}, last_line={last_line}",
                tab.cursor()
            ),
        );
        report.check(
            "line numbers are visible in the view",
            tab.view.shows_line_numbers(),
            format!("shows_line_numbers={}", tab.view.shows_line_numbers()),
        );
    }

    // AC-04: the replace bar changes the text and one undo restores it.
    if let Some(tab) = ui.current_tab() {
        let original = tab.text();
        tab.set_cursor(Position::new(0, 0));
        pump(10);
        *ui.state.find_anchor.borrow_mut() = None;
        ui.find_entry.set_text("module");
        ui.replace_entry.set_text("MODULE");
        ui.replace_next_now();
        pump(30);
        let replaced = tab.text();
        let changed = replaced != original && replaced.contains("MODULE");
        ui.window.activate_action("undo", None);
        pump(50);
        report.check(
            "replace-next changes the text and undo restores it",
            changed && tab.text() == original,
            format!(
                "changed={changed}, restored={}, original_len={}, replaced_len={}",
                tab.text() == original,
                original.len(),
                replaced.len()
            ),
        );
    }

    // AC-04: an unsaved close asks first and the request is recorded.
    if let Some(index) = ui.notebook.current_page().map(|page| page as usize) {
        let before = ui.state.tabs.borrow().len();
        if let Some(tab) = ui.current_tab() {
            tab.document
                .insert(Position::new(0, 0), "-- dirty\n")
                .unwrap_or_else(|error| panic!("dirty edit failed: {error}"));
        }
        ui.set_close_confirmation(Some(Box::new(|_| false)));
        ui.close_tab_at(index, true);
        pump(20);
        let after = ui.state.tabs.borrow().len();
        let recorded = ui.last_close_request();
        ui.set_close_confirmation(None);
        report.check(
            "unsaved close is refused and the confirmation was requested",
            after == before && recorded.is_some() && ui.window.is_visible(),
            format!(
                "tabs_before={before}, tabs_after={after}, recorded={recorded:?}, window_visible={}",
                ui.window.is_visible()
            ),
        );
    }

    if let Some(tab) = ui.current_tab() {
        tab.set_cursor(Position::new(0, 0));
        pump(10);
    }
    ui.find_entry.set_text("module");
    *ui.state.find_anchor.borrow_mut() = None;
    ui.find_next_now();
    pump(20);
    let find_detail = if let Some(tab) = ui.current_tab() {
        let text = tab.text();
        let cursor = tab.cursor();
        let contains = text.contains("module");
        let direct = tab.document.find_next("module", Position::new(0, 0), true);
        let titles: Vec<String> = ui
            .state
            .tabs
            .borrow()
            .iter()
            .map(|tab| tab.title())
            .collect();
        format!(
            "anchor={:?}, contains={contains}, cursor={cursor:?}, direct={direct:?}, entry={:?}, titles={titles:?}, current_page={:?}",
            ui.state.find_anchor.borrow(),
            ui.find_entry.text(),
            ui.notebook.current_page()
        )
    } else {
        "no current tab".to_string()
    };
    report.check(
        "find-next finds the needle through Document",
        ui.state.find_anchor.borrow().is_some(),
        find_detail,
    );
    report.check(
        "status bar reports the cursor",
        ui.status_text().contains(':'),
        ui.status_text(),
    );

    if let Some(path) = &ui.state.startup.slang_binary {
        report.check(
            "VEDIT_SLANG is configured for the SLANG suite",
            path.exists(),
            format!("path={}", path.display()),
        );
    }
    report
}

fn highlight(ui: &std::rc::Rc<Ui>, fixtures: &Fixtures) -> SuiteReport {
    let mut report = SuiteReport::new("highlight");
    let manager = vd_syntax::language_manager();
    report.check(
        "systemverilog language definition is available",
        vd_syntax::is_language_available(&manager, "systemverilog"),
        "is_language_available(systemverilog)",
    );
    report.check(
        "verilog language definition is available",
        vd_syntax::is_language_available(&manager, "verilog"),
        "is_language_available(verilog)",
    );
    let missing = vd_syntax::missing_languages(&manager);
    report.check(
        "no language definitions are missing",
        missing.is_empty(),
        format!("missing={missing:?}"),
    );
    match ui.open_path(&fixtures.unicode) {
        Ok(tab) => {
            report.check(
                "opened .sv with CJK/emoji content has a language",
                tab.buffer.language().is_some(),
                format!("language={:?}", tab.buffer.language()),
            );
            report.check(
                "CJK/emoji content is in the buffer",
                tab.text().contains("你好") && tab.text().contains("😀"),
                "buffer contains 你好 and 😀",
            );
        }
        Err(error) => report.check(
            "open unicode highlight fixture",
            false,
            format!("{error:#}"),
        ),
    }
    match ui.open_path(&fixtures.clean) {
        Ok(tab) => {
            report.check(
                "opened .sv buffer has a language",
                tab.buffer.language().is_some(),
                format!("language={:?}", tab.buffer.language()),
            );
            report.check(
                "style scheme applies (cobalt)",
                vd_syntax::apply_style_scheme(&tab.buffer).is_some(),
                format!("scheme={:?}", vd_syntax::apply_style_scheme(&tab.buffer)),
            );
        }
        Err(error) => report.check("open highlight fixture", false, format!("{error:#}")),
    }
    // Leave the CJK/emoji tab current so a --screenshot of this suite shows the
    // real highlighted unicode content (AC-05 UI + AC-16 rendering evidence).
    if let Some(page) = ui
        .state
        .tabs
        .borrow()
        .iter()
        .find(|tab| tab.title() == "unicode_highlight.sv")
        .and_then(|tab| ui.notebook.page_num(&tab.widget))
    {
        ui.notebook.set_current_page(Some(page));
        pump(80);
    }
    report.check(
        "startup language warning is absent in this image",
        config_glue::language_warning().is_none(),
        format!("warning={:?}", config_glue::language_warning()),
    );
    report
}

fn config(ui: &std::rc::Rc<Ui>) -> SuiteReport {
    let mut report = SuiteReport::new("config");
    let startup = &ui.state.startup;
    report.check(
        "configuration loads",
        startup.loaded.is_some() && startup.config_error.is_none(),
        format!("error={:?}", startup.config_error),
    );
    report.check(
        "editor settings are present",
        startup.editor.tab_width > 0,
        format!("tab_width={}", startup.editor.tab_width),
    );
    report.check(
        "Verible tool resolution records every attempt",
        !startup.tools.attempts.is_empty(),
        format!("attempts={:?}", startup.tools.attempts),
    );
    report.check(
        "tool resolution never uses PATH",
        !startup
            .tools
            .attempts
            .iter()
            .any(|attempt| attempt.contains("PATH")),
        "no PATH attempt recorded",
    );
    report.check(
        "formatter and linter tool paths were resolved (or explicitly recorded as missing)",
        startup.tools.format.is_some()
            || startup.tools.lint.is_some()
            || !startup.tools.attempts.is_empty(),
        format!(
            "format={:?}, lint={:?}",
            startup.tools.format, startup.tools.lint
        ),
    );
    report.check(
        "FIM defaults to off",
        !startup.fim_settings.enabled,
        format!("enabled={}", startup.fim_settings.enabled),
    );
    report
}

fn lsp(ui: &std::rc::Rc<Ui>, fixtures: &Fixtures) -> SuiteReport {
    let mut report = SuiteReport::new("lsp");
    let Some(ls) = ui.state.startup.tools.ls.clone() else {
        report.check(
            "Verible language server is resolved",
            false,
            format!("attempts={:?}", ui.state.startup.tools.attempts),
        );
        return report;
    };
    report.check(
        "Verible language server binary exists",
        ls.exists(),
        format!("path={}", ls.display()),
    );
    if ui.state.lsp.borrow().is_none() {
        report.check("LSP worker started", false, "no LSP bridge");
        return report;
    }
    let tab = match ui.open_path(&fixtures.format_me) {
        Ok(tab) => tab,
        Err(error) => {
            report.check("open LSP fixture", false, format!("{error:#}"));
            return report;
        }
    };
    let generation = wait_until(Duration::from_secs(25), || {
        let value = ui.state.lsp_generation.get();
        if value > 0 {
            Some(value)
        } else {
            None
        }
    });
    report.check(
        "the real LSP publishes diagnostics",
        generation.is_some(),
        format!("generation={generation:?}"),
    );

    let before = tab.text();
    ui.request_format();
    let result = wait_until(Duration::from_secs(20), || {
        ui.state.format_result.borrow_mut().take()
    });
    match result {
        Some(Ok(message)) => {
            report.check(
                "one-key formatting returns and applies",
                message == "formatted" || message == "already formatted",
                format!("result={message}"),
            );
            if message == "formatted" {
                report.check(
                    "formatting changed the buffer",
                    tab.text() != before,
                    format!(
                        "before={} bytes, after={} bytes",
                        before.len(),
                        tab.text().len()
                    ),
                );
                ui.window.activate_action("undo", None);
                pump(50);
                report.check(
                    "one undo restores the pre-format content (apply_edits_one_step)",
                    tab.text() == before,
                    format!("restored={}", tab.text() == before),
                );
            }
        }
        Some(Err(error)) => report.check("formatting", false, error),
        None => report.check(
            "formatting reply arrives",
            false,
            "no LspEvent::Formatted within 20s",
        ),
    }
    // AC-08: F12 on b_top in a.sv must land on the definition in b.sv.
    let definition_a = fixtures.definition_a.as_path();
    let definition_b = fixtures.definition_b.as_path();
    let b_canonical =
        std::fs::canonicalize(definition_b).unwrap_or_else(|_| definition_b.to_path_buf());
    let open_result = ui
        .open_path(definition_b)
        .and_then(|_| ui.open_path(definition_a));
    match open_result {
        Ok(a_tab) => {
            a_tab.set_cursor(Position::new(1, 3));
            pump(40);
            ui.window.activate_action("definition", None);
            let jumped = wait_until(Duration::from_secs(15), || {
                let current = ui.current_tab()?;
                let path = current.path.borrow().clone()?;
                if path == b_canonical && current.cursor() == Position::new(0, 7) {
                    Some(true)
                } else {
                    None
                }
            });
            let detail = ui.current_tab().map_or_else(
                || "no current tab".to_string(),
                |tab| format!("path={:?}, cursor={:?}", tab.path.borrow(), tab.cursor()),
            );
            report.check(
                "F12 definition jumps to the target file/line/column",
                jumped.is_some(),
                detail,
            );
        }
        Err(error) => report.check(
            "F12 definition jumps to the target file/line/column",
            false,
            format!("opening the definition fixtures failed: {error:#}"),
        ),
    }
    report
}

fn slang(ui: &std::rc::Rc<Ui>, fixtures: &Fixtures) -> SuiteReport {
    let mut report = SuiteReport::new("slang");
    let Some(binary) = ui.state.startup.slang_binary.clone() else {
        report.check(
            "SLANG binary is configured (VEDIT_SLANG)",
            false,
            "VEDIT_SLANG is not set for this selftest",
        );
        return report;
    };
    report.check(
        "SLANG binary exists",
        binary.exists(),
        format!("path={}", binary.display()),
    );
    let tab = match ui.open_path(&fixtures.undeclared) {
        Ok(tab) => tab,
        Err(error) => {
            report.check("open SLANG fixture", false, format!("{error:#}"));
            return report;
        }
    };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| ui.run_slang_now()));
    match result {
        Ok(Ok(report_lint)) => {
            report.check(
                "SLANG runs without a failure",
                report_lint.failure.is_none(),
                format!("failure={:?}", report_lint.failure),
            );
            report.check(
                "SLANG reports the undeclared identifier",
                report_lint.total_diagnostics >= 1,
                format!("total={}", report_lint.total_diagnostics),
            );
            let view = diagnostics::from_slang(&report_lint, &tab.document);
            report.check(
                "SLANG positions convert to editor ranges",
                view.iter()
                    .any(|diagnostic| diagnostic.range.is_some_and(|range| range.start.line == 2)),
                format!(
                    "ranges={:?}",
                    view.iter()
                        .map(|diagnostic| diagnostic.range)
                        .collect::<Vec<_>>()
                ),
            );
            tab.store.borrow_mut().set_slang(view);
            ui.refresh_panel();
            pump(20);
            let (verible, slang_count) = ui.diagnostics_counts();
            report.check(
                "problem panel receives the SLANG diagnostics",
                slang_count >= 1 && !ui.panel_rows.borrow().is_empty(),
                format!(
                    "verible={verible}, slang={slang_count}, rows={}",
                    ui.panel_rows.borrow().len()
                ),
            );
            let marks = tab.buffer.source_marks_at_line(2, None);
            let categories: Vec<String> = marks
                .iter()
                .map(|mark| {
                    mark.category()
                        .map_or_else(|| "?".to_string(), |category| category.to_string())
                })
                .collect();
            report.check(
                "gutter marks the SLANG line",
                !marks.is_empty(),
                format!("marks={}, categories={categories:?}", marks.len()),
            );
        }
        Ok(Err(error)) => report.check("SLANG run", false, format!("{error:#}")),
        Err(_) => report.check("SLANG run", false, "panicked"),
    }
    report
}

fn fim(ui: &std::rc::Rc<Ui>) -> SuiteReport {
    let mut report = SuiteReport::new("fim");
    let ghost = &ui.state.ghost;
    report.check(
        "FIM is disabled by default",
        !ghost.enabled(),
        format!("enabled={}", ghost.enabled()),
    );
    report.check(
        "a disabled FIM has a short reason",
        ghost.reason().is_some_and(|reason| !reason.is_empty()),
        format!("reason={:?}", ghost.reason()),
    );
    let before = vd_fim::worker_spawns();
    ui.request_fim();
    pump(50);
    let after = vd_fim::worker_spawns();
    report.check(
        "the off path starts no worker",
        after == before,
        format!("before={before}, after={after}"),
    );
    report.check(
        "the off path shows no ghost text",
        !ghost.has_pending() && !ghost.label.is_visible(),
        format!(
            "pending={}, visible={}",
            ghost.has_pending(),
            ghost.label.is_visible()
        ),
    );
    report.check(
        "the status bar explains why FIM is unavailable",
        ui.status_text().to_lowercase().contains("fim"),
        ui.status_text(),
    );
    report
}

struct Fixtures {
    dir: PathBuf,
    clean: PathBuf,
    format_me: PathBuf,
    undeclared: PathBuf,
    unicode: PathBuf,
    definition_a: PathBuf,
    definition_b: PathBuf,
}

impl Fixtures {
    fn create() -> Result<Self> {
        let dir = std::env::temp_dir().join(format!("vd-app-selftest-{}", std::process::id()));
        std::fs::create_dir_all(&dir).with_context(|| format!("creating {}", dir.display()))?;
        let clean = dir.join("clean.sv");
        let format_me = dir.join("format_me.sv");
        let undeclared = dir.join("undeclared.sv");
        let unicode = dir.join("unicode_highlight.sv");
        std::fs::write(&clean, "module clean;\n  logic a;\nendmodule\n")
            .with_context(|| format!("writing {}", clean.display()))?;
        std::fs::write(
            &format_me,
            "module fmt(input logic a,output logic b);assign b=a;endmodule\n",
        )
        .with_context(|| format!("writing {}", format_me.display()))?;
        std::fs::write(
            &undeclared,
            "module undeclared_probe;\n  initial begin\n    undefined_count = 1;\n  end\nendmodule\n",
        )
        .with_context(|| format!("writing {}", undeclared.display()))?;
        std::fs::write(
            &unicode,
            "module unicode_highlight;\n  // 中文注释：你好，世界 😀\n  logic 信号;\nendmodule\n",
        )
        .with_context(|| format!("writing {}", unicode.display()))?;
        let fixtures_root = fixture_root().join("definition");
        Ok(Self {
            dir,
            clean,
            format_me,
            undeclared,
            unicode,
            definition_a: fixtures_root.join("a.sv"),
            definition_b: fixtures_root.join("b.sv"),
        })
    }

    fn cleanup(&self) {
        if let Err(error) = std::fs::remove_dir_all(&self.dir) {
            eprintln!("vd-app: could not remove {}: {error}", self.dir.display());
        }
    }
}

fn wait_for_ui(app: &App, budget: Duration) -> Option<std::rc::Rc<Ui>> {
    let deadline = Instant::now() + budget;
    loop {
        if let Some(ui) = app.ui() {
            return Some(ui);
        }
        if Instant::now() >= deadline {
            return None;
        }
        pump(20);
    }
}

fn wait_until<T>(budget: Duration, mut probe: impl FnMut() -> Option<T>) -> Option<T> {
    let deadline = Instant::now() + budget;
    loop {
        if let Some(value) = probe() {
            return Some(value);
        }
        if Instant::now() >= deadline {
            return None;
        }
        pump(20);
    }
}

fn pump(millis: u64) {
    let deadline = Instant::now() + Duration::from_millis(millis);
    while Instant::now() < deadline {
        while gtk::events_pending() {
            gtk::main_iteration_do(false);
        }
        std::thread::sleep(Duration::from_millis(5));
    }
}

#[allow(dead_code)]
fn _path_type_guard(path: &Path) -> &Path {
    path
}
