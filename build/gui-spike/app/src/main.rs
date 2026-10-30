//! Throwaway GTK3 + GtkSourceView spike for T5.
//!
//! Proves on CentOS 7 (glibc 2.17): gtk-rs 0.18 links, a GtkWindow opens, and a
//! `.sv` buffer is highlighted by the upstream `systemverilog.lang` shipped with
//! the GtkSourceView 4.8 build. Also implements `--selftest` and `--screenshot`
//! so the clean offline container can assert behaviour without extra tooling.

use std::time::{Duration, Instant};

use cairo::{Context as CairoContext, Format as CairoFormat, ImageSurface};
use gdk::prelude::*;
use gtk::prelude::*;
use sourceview4::prelude::*;

/// GtkSourceView context classes that the spike probes on the fixture.
const PROBED_CLASSES: &[&str] = &[
    "keyword",
    "comment",
    "string",
    "number",
    "preprocessor",
    "type",
    "data_type",
    "function",
    "builtin",
    "constant",
    "identifier",
];

struct Opts {
    file: Option<String>,
    screenshot: Option<String>,
    selftest: bool,
    title: String,
}

fn usage() {
    println!(
        "vedit-gui-spike [--file PATH] [--screenshot PNG] [--selftest] [--title TITLE]\n\
         \n\
         --file       Verilog/SystemVerilog file to load (default: built-in fixture)\n\
         --screenshot capture the realized window to PATH (PNG) and exit\n\
         --selftest   assert language mapping + syntax classes on the loaded buffer, exit 0/1"
    );
}

fn parse_args() -> Opts {
    let mut opts = Opts {
        file: None,
        screenshot: None,
        selftest: false,
        title: "VEdit GTK3 spike".to_string(),
    };
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--file" => opts.file = args.next(),
            "--screenshot" => opts.screenshot = args.next(),
            "--selftest" => opts.selftest = true,
            "--title" => opts.title = args.next().unwrap_or_else(|| opts.title.clone()),
            "-h" | "--help" => {
                usage();
                std::process::exit(0);
            }
            other => {
                eprintln!("ERROR unknown argument: {other}");
                std::process::exit(2);
            }
        }
    }
    opts
}

/// Built-in fixture used when no --file is given (mirrors fixture/top.sv).
const FIXTURE: &str = r#"// COMMENT_TOKEN: VEdit gui-spike fixture
`define SPIKE_MACRO 8'd7

package pkg_spike;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
endpackage

interface bus_if #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
  modport master (output data, input clk);
endinterface

class spi_driver #(type T = int);
  rand int unsigned delay;
  constraint delay_c { delay inside {[1:5]}; }

  covergroup cg_delay;
    coverpoint delay { bins slow = {[4:5]}; }
  endgroup

  function new();
    cg_delay = new();
  endfunction
endclass

module spike_top (
  input  logic       clk,
  input  logic       rst_n,
  output logic [7:0] q
);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q <= 8'd42;  // NUMBER_TOKEN appears above
    end else begin
      $display("STRING_TOKEN value=%0d", q);
      q <= q + 1;
    end
  end

endmodule
"#;

fn language_id_for_path(path: &str) -> Option<&'static str> {
    let ext = std::path::Path::new(path)
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    match ext.as_str() {
        "v" | "vh" => Some("verilog"),
        "sv" | "svh" | "svi" => Some("systemverilog"),
        _ => None,
    }
}

fn language_manager() -> sourceview4::LanguageManager {
    let manager = sourceview4::LanguageManager::new();
    if let Ok(datadir) = std::env::var("VEDIT_GSV_DATADIR") {
        let specs = format!("{datadir}/language-specs");
        manager.set_search_path(&[specs.as_str()]);
        println!("LANGSPECS={specs}");
    } else {
        println!("LANGSPECS=<compiled-in default>");
    }
    manager
}

fn style_scheme_manager() -> sourceview4::StyleSchemeManager {
    let manager = sourceview4::StyleSchemeManager::new();
    if let Ok(datadir) = std::env::var("VEDIT_GSV_DATADIR") {
        let styles = format!("{datadir}/styles");
        manager.set_search_path(&[styles.as_str()]);
    }
    manager
}

/// Pump a main context until `cond` holds or the budget is spent (top level only).
fn pump_ctx(ctx: &glib::MainContext, mut cond: impl FnMut() -> bool, budget: Duration) -> bool {
    let start = Instant::now();
    loop {
        while ctx.iteration(false) {}
        if cond() {
            return true;
        }
        if start.elapsed() >= budget {
            return cond();
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Pump a main context for a fixed duration.
fn pump_for(ctx: &glib::MainContext, budget: Duration) {
    let start = Instant::now();
    while start.elapsed() < budget {
        while ctx.iteration(false) {}
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Pump the default main context until `cond` holds or the budget is spent.
fn pump_until(mut cond: impl FnMut() -> bool, budget: Duration) -> bool {
    let ctx = glib::MainContext::default();
    let start = Instant::now();
    loop {
        while ctx.iteration(false) {}
        if cond() {
            return true;
        }
        if start.elapsed() >= budget {
            return cond();
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Context classes GtkSourceView assigned to the character at `needle`+`delta`.
fn classes_at(
    buffer: &sourceview4::Buffer,
    content: &str,
    needle: &str,
    delta: i32,
) -> Vec<String> {
    let Some(byte_pos) = content.find(needle) else {
        return vec!["<needle-missing>".to_string()];
    };
    let char_off = content[..byte_pos].chars().count() as i32 + delta;
    let iter = buffer.iter_at_offset(char_off);
    PROBED_CLASSES
        .iter()
        .filter(|class| buffer.iter_has_context_class(&iter, class))
        .map(|class| (*class).to_string())
        .collect()
}

/// Names of the GtkTextTags GtkSourceView applied at `needle`+`delta`.
fn tags_at(buffer: &sourceview4::Buffer, content: &str, needle: &str, delta: i32) -> Vec<String> {
    let Some(byte_pos) = content.find(needle) else {
        return vec!["<needle-missing>".to_string()];
    };
    let char_off = content[..byte_pos].chars().count() as i32 + delta;
    let iter = buffer.iter_at_offset(char_off);
    let mut names: Vec<String> = iter
        .tags()
        .iter()
        .filter_map(|tag| tag.name())
        .map(|name| name.to_string())
        .collect();
    names.sort();
    names.dedup();
    names
}

/// Effective (foreground colour, weight) of the highest-priority styled tag at
/// `needle`+`delta`; None when no tag at that offset sets a foreground.
fn style_at(
    buffer: &sourceview4::Buffer,
    content: &str,
    needle: &str,
    delta: i32,
) -> (Option<String>, i32) {
    let Some(byte_pos) = content.find(needle) else {
        return (None, 0);
    };
    let char_off = content[..byte_pos].chars().count() as i32 + delta;
    let iter = buffer.iter_at_offset(char_off);
    let mut tags = iter.tags();
    tags.sort_by_key(|tag| std::cmp::Reverse(tag.priority()));
    for tag in tags {
        let fg: Option<gdk::RGBA> = tag.property("foreground-rgba");
        if let Some(fg) = fg {
            return (
                Some(format!(
                    "#{:02x}{:02x}{:02x}",
                    (fg.red() * 255.0).round() as u8,
                    (fg.green() * 255.0).round() as u8,
                    (fg.blue() * 255.0).round() as u8
                )),
                tag.weight(),
            );
        }
    }
    (None, 0)
}

/// Returns the process exit code: 0 = pass.
///
/// GtkSourceView does not expose `keyword` as a context class for this language
/// definition (the SV lang uses `style-ref="keyword"` on named contexts, not
/// `class="keyword"`), so the spike asserts the *rendered style* instead: the
/// foreground colour and weight of the highest-priority tag at the token. This is
/// what AC-05 actually demands — the construct categories must be visually
/// distinguishable.
fn run_selftest(buffer: &sourceview4::Buffer, content: &str, lang_id: &str) -> i32 {
    let ready = pump_until(
        || style_at(buffer, content, "module", 0).0.is_some(),
        Duration::from_secs(5),
    );

    // AC-05 construct -> probe needle.
    let constructs: &[(&str, &str)] = &[
        ("module", "module"),
        ("interface", "interface"),
        ("package", "package"),
        ("class", "class"),
        ("always_ff", "always_ff"),
        ("constraint", "constraint"),
        ("covergroup", "covergroup"),
        ("macro_define", "`define"),
        ("comment", "COMMENT_TOKEN"),
        ("string", "STRING_TOKEN"),
        ("number", "42"),
        ("system_task", "$display"),
        ("data_type", "logic"),
    ];

    println!("SELFTEST lang_id={lang_id}");
    println!("SELFTEST highlight_ready={ready}");
    let mut colors: Vec<(&str, String)> = Vec::new();
    let mut failed = 0;
    for (name, needle) in constructs {
        let classes = classes_at(buffer, content, needle, 0);
        let tags = tags_at(buffer, content, needle, 0);
        let (color, weight) = style_at(buffer, content, needle, 0);
        let ok = color.is_some();
        println!(
            "SELFTEST token {name} classes={classes:?} tags={tags:?} color={color:?} weight={weight} {}",
            if ok { "PASS" } else { "FAIL" }
        );
        if !ok {
            failed += 1;
        }
        if let Some(color) = color {
            colors.push((name, color));
        }
    }

    let color_of = |name: &str| -> Option<String> {
        colors
            .iter()
            .find(|(n, _)| *n == name)
            .map(|(_, c)| c.clone())
    };
    let keyword_color = color_of("module");

    let mut checks: Vec<(String, bool)> = Vec::new();
    checks.push((
        "lang_id==systemverilog".to_string(),
        lang_id == "systemverilog",
    ));
    checks.push(("highlight_ready".to_string(), ready));
    checks.push(("keyword_colored".to_string(), keyword_color.is_some()));
    // Every SystemVerilog keyword-family construct must share the keyword style.
    for name in [
        "interface",
        "package",
        "class",
        "always_ff",
        "constraint",
        "covergroup",
    ] {
        let same = color_of(name)
            .zip(keyword_color.clone())
            .map(|(a, b)| a == b);
        checks.push((format!("keyword_style_shared:{name}"), same == Some(true)));
    }
    // AC-05 token categories must be visually distinct from the keyword style.
    for (name, ok) in [
        ("comment", color_of("comment") != keyword_color),
        ("string", color_of("string") != keyword_color),
        ("number", color_of("number") != keyword_color),
        ("macro_define", color_of("macro_define") != keyword_color),
        ("data_type", color_of("data_type") != keyword_color),
    ] {
        checks.push((format!("distinct_from_keyword:{name}"), ok));
    }
    let distinct: std::collections::BTreeSet<&String> = colors.iter().map(|(_, c)| c).collect();
    checks.push(("distinct_style_colors>=5".to_string(), distinct.len() >= 5));

    for (name, ok) in checks {
        println!("CHECK {name} {}", if ok { "PASS" } else { "FAIL" });
        if !ok {
            failed += 1;
        }
    }
    println!("DISTINCT_COLORS={}", distinct.len());
    for (name, color) in &colors {
        println!("COLOR {name} {color}");
    }
    println!("SELFTEST {}", if failed == 0 { "PASS" } else { "FAIL" });
    if failed == 0 {
        0
    } else {
        1
    }
}

/// Render the GtkSourceView into a PNG with the widget's own draw handler.
///
/// `gdk_window_get_pixbuf` aborts on CentOS 7's cairo 1.15 when the window
/// surface carries mime data (measured: `cairo_surface_mark_dirty_rectangle`
/// assertion, SIGABRT). Drawing the widget into an image surface is deterministic
/// and needs no compositor.
fn capture(view: &sourceview4::View, path: &str) -> bool {
    let alloc = view.allocation();
    let width = alloc.width().max(1);
    let height = alloc.height().max(1);
    let surface = match ImageSurface::create(CairoFormat::Rgb24, width, height) {
        Ok(surface) => surface,
        Err(err) => {
            eprintln!("ERROR screenshot: image surface: {err}");
            return false;
        }
    };
    let cr = match CairoContext::new(&surface) {
        Ok(cr) => cr,
        Err(err) => {
            eprintln!("ERROR screenshot: cairo context: {err}");
            return false;
        }
    };
    view.draw(&cr);
    surface.flush();
    let mut png: Vec<u8> = Vec::new();
    if let Err(err) = surface.write_to_png(&mut png) {
        eprintln!("ERROR screenshot: encode PNG: {err}");
        return false;
    }
    match std::fs::write(path, &png) {
        Ok(()) => {
            println!(
                "SCREENSHOT path={path} width={width} height={height} bytes={}",
                png.len()
            );
            true
        }
        Err(err) => {
            eprintln!("ERROR screenshot write {path}: {err}");
            false
        }
    }
}

fn main() {
    let opts = parse_args();
    let started = Instant::now();

    if let Err(err) = gtk::init() {
        eprintln!("ERROR gtk_init failed: {err} (DISPLAY set? run under Xvfb)");
        std::process::exit(3);
    }
    println!(
        "GTK_VERSION={}.{}.{}",
        gtk::major_version(),
        gtk::minor_version(),
        gtk::micro_version()
    );
    if let Ok(exe) = std::env::current_exe() {
        println!("EXE={}", exe.display());
    }

    let path = opts
        .file
        .clone()
        .unwrap_or_else(|| "untitled.sv".to_string());
    let content = match &opts.file {
        Some(file) => match std::fs::read_to_string(file) {
            Ok(text) => text,
            Err(err) => {
                eprintln!("ERROR read {file}: {err}");
                std::process::exit(2);
            }
        },
        None => FIXTURE.to_string(),
    };

    let lang_mgr = language_manager();
    let lang_id = language_id_for_path(&path).unwrap_or("systemverilog");
    let language = lang_mgr.language(lang_id);
    match &language {
        Some(lang) => println!("LANG id={:?} name={:?}", lang.id(), lang.name()),
        None => println!("LANG id={lang_id} name=<not-found>"),
    }

    let buffer = sourceview4::Buffer::new(None::<&gtk::TextTagTable>);
    buffer.set_highlight_syntax(true);
    if let Some(lang) = &language {
        buffer.set_language(Some(lang));
    }
    buffer.set_text(&content);

    let style_mgr = style_scheme_manager();
    // Prefer upstream schemes that actually style `def:keyword` (measured: kate/classic
    // leave the SystemVerilog keyword style unstyled, Adwaita/cobalt do not).
    let scheme = ["Adwaita", "cobalt", "classic", "kate"]
        .iter()
        .find_map(|id| style_mgr.scheme(id));
    let scheme_name = scheme
        .as_ref()
        .and_then(|s| s.id())
        .map(|id| id.to_string())
        .unwrap_or_else(|| "<none>".to_string());
    if let Some(scheme) = &scheme {
        buffer.set_style_scheme(Some(scheme));
    }
    println!("STYLE_SCHEME={scheme_name}");

    // Read-only: the spike demonstrates highlighting, not editing behaviour.
    let view = sourceview4::View::with_buffer(&buffer);
    view.set_editable(false);
    view.set_show_line_numbers(true);
    view.set_highlight_current_line(false);
    view.set_monospace(true);

    let window = gtk::Window::new(gtk::WindowType::Toplevel);
    window.set_title(&opts.title);
    window.set_default_size(900, 700);
    window.connect_destroy(|_| gtk::main_quit());

    let mapped = std::rc::Rc::new(std::cell::Cell::new(false));
    {
        let mapped = mapped.clone();
        let started_c = started;
        window.connect_map(move |_| {
            mapped.set(true);
            println!("WINDOW_MAPPED_MS={}", started_c.elapsed().as_millis());
        });
    }

    let scroller = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroller.add(&view);
    window.add(&scroller);
    window.show_all();

    let headless = opts.selftest || opts.screenshot.is_some();
    if headless {
        let ctx = glib::MainContext::default();
        if !pump_ctx(&ctx, || mapped.get(), Duration::from_secs(10)) {
            eprintln!("ERROR window was not mapped within 10s");
            std::process::exit(5);
        }
        // Let the frame paint and the syntax highlighter finish its idle work.
        pump_for(&ctx, Duration::from_millis(700));
        let mut code = 0;
        if opts.selftest {
            code = run_selftest(&buffer, &content, lang_id);
        }
        if let Some(path) = &opts.screenshot {
            if !capture(&view, path) && code == 0 {
                code = 4;
            }
        }
        std::process::exit(code);
    }

    gtk::main();
}
