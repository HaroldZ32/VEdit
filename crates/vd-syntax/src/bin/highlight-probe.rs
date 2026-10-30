//! Headless-ish AC-05 probe: prove the highlighted token categories are distinct.
//!
//! This binary is the evidence producer for T11. It binds the upstream
//! `systemverilog` language definition and the fixed `cobalt` style scheme to a
//! `GtkSourceBuffer`, shows the buffer in a `GtkSourceView` and reads the
//! **rendered** style (style tag, foreground colour, weight) at every AC-05 token
//! in `tests/fixtures/ac05.sv`.
//!
//! ## Why a window
//!
//! Measured on GtkSourceView 4.6.1 (the pinned CentOS 7 build): a buffer alone,
//! with the language and the style scheme set, gets its context classes applied
//! (`comment`, `string`, ...) but **no styled tag at all** — every AC-05 token
//! reads `color=<none>` even after pumping the main context for 10 s. Realising
//! the view without showing it does not help either; only a shown (`show_all()`)
//! view produces the style tags. That is upstream behaviour, not a choice here,
//! which is why this evidence tool is allowed to create one window (the library
//! and every unit test stay window-free).
//!
//! Usage:
//!
//! ```text
//! xvfb-run -a cargo run -p vd-syntax --bin highlight-probe -- \
//!     --datadir /opt/gsv4/share/gtksourceview-4 [--fixture PATH] [--screenshot PNG]
//! ```
//!
//! `--datadir` points at the GtkSourceView data directory (the one containing
//! `language-specs/` and `styles/`); when omitted the manager keeps
//! GtkSourceView's own default search path (`$XDG_DATA_HOME` + `$XDG_DATA_DIRS`).
//!
//! Output: one `TOKEN` line per AC-05 construct (position, style id, colour,
//! weight, verdict), one `CHECK` line per assertion, `DISTINCT=<n>`, and a final
//! `PROBE PASS` / `PROBE FAIL`.
//!
//! Exit codes: `0` all checks pass, `1` a check failed, `2` setup error.
#![forbid(unsafe_code)]

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use gtk::prelude::*;
use sourceview4::prelude::*;

/// The language the product must bind for a `.sv` file.
const EXPECTED_LANGUAGE: &str = "systemverilog";

/// AC-05 construct → needle. The fixture guarantees the first occurrence of each
/// needle is the token under test.
const CONSTRUCTS: &[(&str, &str)] = &[
    ("module", "module"),
    ("interface", "interface"),
    ("package", "package"),
    ("class", "class"),
    ("always_ff", "always_ff"),
    ("constraint", "constraint"),
    ("covergroup", "covergroup"),
    ("macro_define", "`define"),
    ("comment", "COMMENT_TOKEN"),
    ("string", "STRING_LITERAL"),
    ("system_task", "$display"),
    ("number", "42"),
    ("data_type", "logic"),
];

/// Upstream `def:` style ids probed in addition to the ones the language
/// definitions declare, because a language maps several of its ids onto the same
/// `def:` style (`integer-number`/`real-number` → `def:number`, ...).
const COMMON_SCHEME_STYLE_IDS: &[&str] = &[
    "def:boolean",
    "def:builtin",
    "def:comment",
    "def:constant",
    "def:error",
    "def:function",
    "def:identifier",
    "def:keyword",
    "def:number",
    "def:operator",
    "def:preprocessor",
    "def:special-char",
    "def:string",
    "def:type",
];

/// Constructs that must share the keyword style.
const KEYWORD_FAMILY: &[&str] = &[
    "interface",
    "package",
    "class",
    "always_ff",
    "constraint",
    "covergroup",
];

/// Constructs that must be visually distinct from the keyword style.
const DISTINCT_FROM_KEYWORD: &[&str] =
    &["comment", "string", "number", "macro_define", "data_type"];

struct Options {
    fixture: PathBuf,
    datadir: Option<PathBuf>,
    screenshot: Option<PathBuf>,
}

fn parse_args() -> Result<Options, String> {
    let mut fixture = PathBuf::from(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/tests/fixtures/ac05.sv"
    ));
    let mut datadir = None;
    let mut screenshot = None;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--fixture" => {
                fixture = PathBuf::from(args.next().ok_or("--fixture needs a path")?);
            }
            "--datadir" => {
                datadir = Some(PathBuf::from(args.next().ok_or("--datadir needs a path")?));
            }
            "--screenshot" => {
                screenshot = Some(PathBuf::from(
                    args.next().ok_or("--screenshot needs a path")?,
                ));
            }
            other => return Err(format!("unknown argument '{other}'")),
        }
    }
    Ok(Options {
        fixture,
        datadir,
        screenshot,
    })
}

/// Character offset of the first occurrence of `needle`.
fn char_offset(content: &str, needle: &str) -> Option<i32> {
    let byte_position = content.find(needle)?;
    Some(content[..byte_position].chars().count() as i32)
}

/// 1-based line and column of the first occurrence of `needle`.
fn line_col(content: &str, needle: &str) -> Option<(usize, usize)> {
    let byte_position = content.find(needle)?;
    let before = &content[..byte_position];
    let line = before.matches('\n').count() + 1;
    let column = before
        .rsplit('\n')
        .next()
        .map_or(1, |tail| tail.chars().count() + 1);
    Some((line, column))
}

/// Rendered style at `needle`: style id, foreground colour, weight.
struct Style {
    colour: Option<String>,
    weight: i32,
}

fn style_at(buffer: &sourceview4::Buffer, content: &str, needle: &str) -> Style {
    let Some(offset) = char_offset(content, needle) else {
        return Style {
            colour: None,
            weight: 0,
        };
    };
    let iter = buffer.iter_at_offset(offset);
    let mut tags = iter.tags();
    tags.sort_by_key(|tag| std::cmp::Reverse(tag.priority()));
    for tag in &tags {
        let foreground: Option<gtk::gdk::RGBA> = tag.property("foreground-rgba");
        if let Some(colour) = foreground {
            return Style {
                colour: Some(format!(
                    "#{:02x}{:02x}{:02x}",
                    (colour.red() * 255.0).round() as u8,
                    (colour.green() * 255.0).round() as u8,
                    (colour.blue() * 255.0).round() as u8
                )),
                weight: tag.weight(),
            };
        }
    }
    Style {
        colour: None,
        weight: 0,
    }
}

/// Pump the default main context until `ready` holds or the budget is spent.
fn pump_until(mut ready: impl FnMut() -> bool, budget: Duration) -> bool {
    let context = gtk::glib::MainContext::default();
    let started = Instant::now();
    loop {
        while context.iteration(false) {}
        if ready() {
            return true;
        }
        if started.elapsed() >= budget {
            return ready();
        }
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Pump the default main context for a fixed duration (lets the frame paint).
fn pump_for(budget: Duration) {
    let context = gtk::glib::MainContext::default();
    let started = Instant::now();
    while started.elapsed() < budget {
        while context.iteration(false) {}
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// Render the view into a PPM (P6) raster with the widget's own draw handler.
///
/// PPM keeps the probe dependency-free: `cairo-rs` is pulled in by `gtk` without
/// the `png` feature, so `write_to_png` is unavailable, and adding a `cairo`
/// dependency to this crate would mean touching a manifest this lane does not
/// own. `evidence/highlight/render.sh` converts the PPM to PNG for review.
fn capture(view: &sourceview4::View, path: &Path) -> Result<(), String> {
    let mut surface = {
        let allocation = view.allocation();
        let width = allocation.width().max(1);
        let height = allocation.height().max(1);
        gtk::cairo::ImageSurface::create(gtk::cairo::Format::Rgb24, width, height)
            .map_err(|error| format!("image surface: {error}"))?
    };
    let context =
        gtk::cairo::Context::new(&surface).map_err(|error| format!("cairo context: {error}"))?;
    view.draw(&context);
    drop(context);
    surface.flush();

    let width = usize::try_from(surface.width()).map_err(|_| "negative width".to_string())?;
    let height = usize::try_from(surface.height()).map_err(|_| "negative height".to_string())?;
    let stride = usize::try_from(surface.stride()).map_err(|_| "negative stride".to_string())?;
    let data = surface
        .data()
        .map_err(|error| format!("surface data: {error}"))?;

    // cairo RGB24 is BGRx in memory (little-endian); P6 wants RGB triples.
    let mut ppm: Vec<u8> = Vec::with_capacity(width * height * 3 + 32);
    ppm.extend_from_slice(format!("P6\n{width} {height}\n255\n").as_bytes());
    for row in 0..height {
        let start = row * stride;
        for pixel in 0..width {
            let offset = start + pixel * 4;
            ppm.push(data[offset + 2]);
            ppm.push(data[offset + 1]);
            ppm.push(data[offset]);
        }
    }
    std::fs::write(path, &ppm).map_err(|error| format!("write {}: {error}", path.display()))?;
    println!(
        "SCREENSHOT path={} width={width} height={height} bytes={}",
        path.display(),
        ppm.len()
    );
    Ok(())
}

fn main() -> std::process::ExitCode {
    match run() {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("ERROR: {message}");
            std::process::ExitCode::from(1)
        }
    }
}

fn run() -> Result<(), String> {
    let options = parse_args()?;
    let content = std::fs::read_to_string(&options.fixture)
        .map_err(|error| format!("cannot read fixture {}: {error}", options.fixture.display()))?;

    // GtkSourceView refuses to construct any manager before GTK is initialised
    // ("Gtk has to be initialized before using GtkSourceView"), and `gtk::init()`
    // wraps `gtk_init_check()`, so a missing display is a clean error here.
    gtk::init()
        .map_err(|error| format!("gtk init failed: {error} (no DISPLAY? run under xvfb-run)"))?;
    println!(
        "GTK_VERSION={}.{}.{}",
        gtk::major_version(),
        gtk::minor_version(),
        gtk::micro_version()
    );

    let manager = vd_syntax::language_manager_with(options.datadir.as_deref());
    let search_path: Vec<String> = manager
        .search_path()
        .iter()
        .map(ToString::to_string)
        .collect();
    if search_path.is_empty() {
        println!("SEARCH_PATH=default");
    } else {
        println!("SEARCH_PATH={}", search_path.join(":"));
    }

    let language = manager.language(EXPECTED_LANGUAGE).ok_or_else(|| {
        format!("language '{EXPECTED_LANGUAGE}' not found (is GtkSourceView data installed?)")
    })?;
    println!(
        "LANG id={:?} name={:?}",
        language.id().map(|id| id.to_string()),
        language.name().map(|name| name.to_string())
    );

    println!(
        "LANGUAGES verilog={} systemverilog={} missing={:?}",
        vd_syntax::is_language_available(&manager, "verilog"),
        vd_syntax::is_language_available(&manager, EXPECTED_LANGUAGE),
        vd_syntax::missing_languages(&manager)
    );

    let buffer = sourceview4::Buffer::new(None::<&gtk::TextTagTable>);
    buffer.set_highlight_syntax(true);
    buffer.set_language(Some(&language));
    buffer.set_text(&content);
    let scheme_manager = vd_syntax::style_scheme_manager_with(options.datadir.as_deref());
    let scheme = vd_syntax::apply_style_scheme_with_manager(&scheme_manager, &buffer);
    println!("STYLE_SCHEME={}", scheme.as_deref().unwrap_or("<none>"));

    let view = sourceview4::View::with_buffer(&buffer);
    view.set_editable(false);
    view.set_show_line_numbers(true);
    view.set_highlight_current_line(false);
    view.set_monospace(true);

    let window = gtk::Window::new(gtk::WindowType::Toplevel);
    window.set_title("VEdit T11 highlight probe");
    window.set_default_size(900, 700);
    let scroller = gtk::ScrolledWindow::new(None::<&gtk::Adjustment>, None::<&gtk::Adjustment>);
    scroller.add(&view);
    window.add(&scroller);

    let mapped = std::rc::Rc::new(std::cell::Cell::new(false));
    {
        let mapped = mapped.clone();
        window.connect_map(move |_| mapped.set(true));
    }
    window.show_all();

    let mapped_ok = pump_until(|| mapped.get(), Duration::from_secs(10));
    println!("WINDOW_MAPPED={mapped_ok}");
    // Let the frame paint: style tags are applied by the displayed view.
    pump_for(Duration::from_millis(700));
    let ready = pump_until(
        || style_at(&buffer, &content, "module").colour.is_some(),
        Duration::from_secs(10),
    );

    // GtkSourceView 4.6.1 creates the style tags *unnamed* (measured: 14 tags in
    // the table, only the 3 context-class tags have names), so a style id has to
    // be derived. `GtkSourceLanguage` exposes the style ids its contexts use
    // (`systemverilog:keyword`, `verilog:comment`, ...); each is resolved against
    // the applied scheme directly and through its `def:<name>` mapping. The
    // rendered colour + weight stay the primary evidence — the id is a readable
    // label for it, and is reported as `<unmapped>` when it cannot be derived.
    let style_table: Vec<(String, String, bool)> = {
        let mut rows: Vec<(String, String, bool)> = Vec::new();
        let scheme_object = scheme.as_deref().and_then(|id| {
            sourceview4::prelude::StyleSchemeManagerExt::scheme(&scheme_manager, id)
        });
        if let Some(scheme_object) = scheme_object {
            let mut language_style_ids: Vec<String> = Vec::new();
            for language_id in ["systemverilog", "verilog"] {
                if let Some(language) = manager.language(language_id) {
                    language_style_ids.extend(language.style_ids().iter().map(ToString::to_string));
                }
            }
            language_style_ids.extend(COMMON_SCHEME_STYLE_IDS.iter().map(|id| (*id).to_string()));
            language_style_ids.sort_unstable();
            language_style_ids.dedup();
            for language_style_id in language_style_ids {
                let suffix = language_style_id
                    .rsplit(':')
                    .next()
                    .unwrap_or(language_style_id.as_str())
                    .to_string();
                let mut resolved = false;
                for candidate in [language_style_id.clone(), format!("def:{suffix}")] {
                    if let Some(style) = sourceview4::prelude::StyleSchemeExt::style(
                        &scheme_object,
                        candidate.as_str(),
                    ) {
                        if let Some(foreground) = style.foreground() {
                            rows.push((
                                candidate.clone(),
                                foreground.to_string().to_ascii_lowercase(),
                                style.is_bold(),
                            ));
                        }
                        resolved = true;
                        if candidate == language_style_id {
                            break;
                        }
                    }
                }
                if !resolved {
                    println!("STYLE_ID_UNRESOLVED {language_style_id}");
                }
            }
            rows.sort();
            rows.dedup();
        }
        rows
    };
    for (style_id, colour, bold) in &style_table {
        println!("STYLE_TABLE id={style_id} color={colour} bold={bold}");
    }

    let mut styles: Vec<(String, Option<String>)> = Vec::new();
    let mut failures = 0_usize;
    for (name, needle) in CONSTRUCTS {
        let style = style_at(&buffer, &content, needle);
        let position = line_col(&content, needle)
            .map(|(line, column)| format!("{line}:{column}"))
            .unwrap_or_else(|| "<missing>".to_string());
        let ok = style.colour.is_some();
        if !ok {
            failures += 1;
        }
        let style_ids = match &style.colour {
            Some(colour) => {
                let bold = style.weight >= 700;
                let mut matches: Vec<&str> = style_table
                    .iter()
                    .filter(|(_, style_colour, style_bold)| {
                        style_colour == colour && *style_bold == bold
                    })
                    .map(|(id, _, _)| id.as_str())
                    .collect();
                matches.sort_unstable();
                if matches.is_empty() {
                    "<unmapped>".to_string()
                } else {
                    matches.join("|")
                }
            }
            None => "<none>".to_string(),
        };
        println!(
            "TOKEN name={name} pos={position} style={style_ids} color={} weight={} {}",
            style.colour.as_deref().unwrap_or("<none>"),
            style.weight,
            if ok { "PASS" } else { "FAIL" }
        );
        styles.push(((*name).to_string(), style.colour));
    }

    let colour_of = |name: &str| -> Option<String> {
        styles
            .iter()
            .find(|(construct, _)| construct == name)
            .and_then(|(_, colour)| colour.clone())
    };
    let keyword_colour = colour_of("module");

    let check = |name: &str, ok: bool, failures: &mut usize| {
        if !ok {
            *failures += 1;
        }
        println!("CHECK {name} {}", if ok { "PASS" } else { "FAIL" });
    };

    check("window_mapped", mapped_ok, &mut failures);
    check("highlight_ready", ready, &mut failures);
    check(
        "language_bound==systemverilog",
        language.id().map(|id| id.to_string()).as_deref() == Some(EXPECTED_LANGUAGE),
        &mut failures,
    );
    check(
        "keyword_style_present",
        keyword_colour.is_some(),
        &mut failures,
    );
    for name in KEYWORD_FAMILY {
        let shared = colour_of(name).is_some() && colour_of(name) == keyword_colour;
        check(
            &format!("keyword_style_shared:{name}"),
            shared,
            &mut failures,
        );
    }
    for name in DISTINCT_FROM_KEYWORD {
        let colour = colour_of(name);
        check(
            &format!("distinct_from_keyword:{name}"),
            colour.is_some() && colour != keyword_colour,
            &mut failures,
        );
    }
    // Inheritance proof by rendering: `module` is a Verilog definition token and
    // `always_ff`/`constraint` are SystemVerilog-only tokens. Both being styled
    // in one systemverilog buffer means the inherited Verilog contexts are
    // active; a string search inside systemverilog.lang cannot show that.
    check(
        "inheritance:verilog_token_in_systemverilog",
        keyword_colour.is_some(),
        &mut failures,
    );
    // `$display` is styled with the keyword colour in cobalt (system-task style),
    // so it is asserted as *styled*, not as visually distinct from keywords.
    check(
        "styled:system_task",
        colour_of("system_task").is_some(),
        &mut failures,
    );
    check(
        "inheritance:systemverilog_only_token",
        colour_of("always_ff").is_some() && colour_of("constraint").is_some(),
        &mut failures,
    );

    let distinct: std::collections::BTreeSet<&str> = styles
        .iter()
        .filter_map(|(_, colour)| colour.as_deref())
        .collect();
    println!("DISTINCT={}", distinct.len());
    check(
        "distinct_style_colours>=6",
        distinct.len() >= 6,
        &mut failures,
    );

    if let Some(path) = &options.screenshot {
        if let Err(error) = capture(&view, path) {
            eprintln!("ERROR: screenshot: {error}");
            failures += 1;
        }
    }

    if failures == 0 {
        println!("PROBE PASS");
        Ok(())
    } else {
        Err(format!("PROBE FAIL failures={failures}"))
    }
}
