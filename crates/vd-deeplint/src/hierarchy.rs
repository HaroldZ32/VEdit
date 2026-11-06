//! Project-wide instance hierarchy and signal driver/load (T7).
//!
//! The spike (`evidence/w2-spike/`) proved the path: `slang --top <top>
//! --ast-json <file> --ast-json-source-info` elaborates the project file set and
//! serialises the *elaborated* design. This module runs that command and turns
//! the JSON into the two answers the UI needs:
//!
//! * [`InstanceNode`] — every elaborated instance with its hierarchical path
//!   (`top.u0`, `top.u1`), its module definition and the **instantiation site**
//!   (`file:line:column`), which is what a double-click jumps to (AC-3.1);
//! * [`SignalInfo`] — for every net of every instance, the drivers and loads with
//!   their exact positions, plus the driver chain that continues inside a child
//!   instance (`top.link ← top.u0.q ← top.u0.state ← always_ff`, AC-3.2).
//!
//! # Shape notes that cost the spike time (they are load-bearing here)
//!
//! * **Only the cwd decides whether paths resolve**: the child runs with
//!   `current_dir = project_root` (the first file's root); with any other cwd the
//!   `source_file` values degrade to a sibling of the project root.
//! * **Instance bodies are deduplicated**: the second instance of one module
//!   serialises `"body": "<addr> <name>"` instead of a body copy, so the walker
//!   resolves bodies by address ([`Bodies`]).
//! * Positions come 1-based from slang; this module exposes **0-based** lines and
//!   columns, matching `VdDiagnostic` and the editor's model.
//!
//! # Bounds (AC-3.5)
//!
//! The run is bounded by [`HierarchyOptions::timeout`] and
//! [`HierarchyOptions::max_json_bytes`]. A project above the declared size limits
//! still runs, but the report carries a [`HierarchyReport::warning`] the UI must
//! show, so an oversized project is never silently slow.

use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use serde_json::Value;

use crate::binary::{
    resolve_binary_for_current_process, sanitize_tool_child_env, BinaryResolution,
};
use crate::LintFailure;

/// Default budget for one elaboration run.
pub const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);
/// Default cap for the `--ast-json` file.
pub const DEFAULT_MAX_JSON_BYTES: usize = 256 * 1024 * 1024;
/// Projects larger than this many files are reported as oversized (AC-3.5).
pub const DEFAULT_MAX_FILES: usize = 1_000;
/// Designs with more instances than this are reported as oversized (AC-3.5).
pub const DEFAULT_MAX_INSTANCES: usize = 10_000;
/// How often the child is checked for exit / timeout. Elaboration runs are short
/// (milliseconds on the spike fixture), so the peak-RSS sample must be taken
/// often enough to catch the child while it is alive.
const POLL_INTERVAL: Duration = Duration::from_millis(1);

/// Options for one elaboration run.
#[derive(Debug, Clone)]
pub struct HierarchyOptions {
    /// Explicit slang binary (tests, packaging).
    pub slang_binary: Option<PathBuf>,
    /// Top module; `None` lets slang pick the roots.
    pub top: Option<String>,
    /// Project include dirs (`-I`).
    pub include_dirs: Vec<PathBuf>,
    /// Project defines (`-D`).
    pub defines: Vec<String>,
    /// Wall-clock budget for the run.
    pub timeout: Duration,
    /// Cap for the AST JSON file.
    pub max_json_bytes: usize,
    /// Oversize threshold for the file set (AC-3.5).
    pub max_files: usize,
    /// Oversize threshold for the instance count (AC-3.5).
    pub max_instances: usize,
}

impl Default for HierarchyOptions {
    fn default() -> Self {
        Self {
            slang_binary: None,
            top: None,
            include_dirs: Vec::new(),
            defines: Vec::new(),
            timeout: DEFAULT_TIMEOUT,
            max_json_bytes: DEFAULT_MAX_JSON_BYTES,
            max_files: DEFAULT_MAX_FILES,
            max_instances: DEFAULT_MAX_INSTANCES,
        }
    }
}

/// One elaborated instance.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstanceNode {
    /// Hierarchical path (`top`, `top.u0`, …).
    pub path: String,
    /// Module name of the instance body.
    pub module: String,
    /// File of the instantiation site.
    pub file: PathBuf,
    /// 0-based line of the instantiation site.
    pub line: u32,
    /// 0-based column of the instantiation site.
    pub column: u32,
}

/// Why an endpoint drives or loads a signal.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EndpointKind {
    /// An instance output port bound to the net.
    InstanceOutputPort,
    /// An instance input port reading the net.
    InstanceInputPort,
    /// A continuous assignment (`assign lhs = rhs`).
    ContinuousAssign,
    /// A procedural assignment inside an `always_*` / `initial` block.
    Procedural,
}

impl EndpointKind {
    /// Stable label for the UI and the C ABI.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::InstanceOutputPort => "instance-output-port",
            Self::InstanceInputPort => "instance-input-port",
            Self::ContinuousAssign => "continuous-assign",
            Self::Procedural => "procedural",
        }
    }

    /// Stable numeric code for the C ABI.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::InstanceOutputPort => 0,
            Self::InstanceInputPort => 1,
            Self::ContinuousAssign => 2,
            Self::Procedural => 3,
        }
    }
}

/// One driver or load: where and by what.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Endpoint {
    /// Hierarchical path of the driving/loading symbol (`top.u0.q`).
    pub path: String,
    /// File of the position.
    pub file: PathBuf,
    /// 0-based line.
    pub line: u32,
    /// 0-based column.
    pub column: u32,
    /// What kind of construct this is.
    pub kind: EndpointKind,
    /// Extra label (`AlwaysFF`, `assign`, port name …).
    pub detail: String,
    /// For [`EndpointKind::ContinuousAssign`] drivers: the net the assignment
    /// reads (the next hop of the driver chain).
    pub source: Option<String>,
}

/// Everything the service knows about one net.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SignalInfo {
    /// Hierarchical path of the net (`top.link`).
    pub path: String,
    /// `Net`, `Variable` or `Port` as slang serialises it.
    pub kind: String,
    /// Declaration position, when slang serialises one.
    pub declaration: Option<(PathBuf, u32, u32)>,
    /// Constructs that drive the net.
    pub drivers: Vec<Endpoint>,
    /// Constructs that read the net.
    pub loads: Vec<Endpoint>,
    /// Driver chain that continues inside a child instance (AC-3.2).
    pub chain: Vec<Endpoint>,
}

/// One elaboration result.
#[derive(Debug, Clone)]
pub struct HierarchyReport {
    /// The tool that ran (resolved binary's name).
    pub tool: String,
    /// The top that was requested (`None` = slang picked the roots).
    pub top: Option<String>,
    /// Instances, parents first.
    pub instances: Vec<InstanceNode>,
    /// Nets, in instance order.
    pub signals: Vec<SignalInfo>,
    /// Wall-clock time of the run.
    pub elapsed: Duration,
    /// Peak RSS of the child in KiB (sampled from `/proc/<pid>/status`).
    pub peak_rss_kb: u64,
    /// Set when the project exceeded the declared size limits (AC-3.5).
    pub warning: Option<String>,
    /// `None` on success; a typed failure otherwise.
    pub failure: Option<LintFailure>,
}

impl HierarchyReport {
    /// One-line summary for the status bar.
    #[must_use]
    pub fn summary(&self) -> String {
        match &self.failure {
            Some(failure) => failure.user_message(),
            None => format!(
                "{} instance(s), {} net(s), {} ms, {:.1} MB",
                self.instances.len(),
                self.signals.len(),
                self.elapsed.as_millis(),
                self.peak_rss_kb as f64 / 1024.0
            ),
        }
    }

    /// The instance under `path`, if any.
    #[must_use]
    pub fn instance(&self, path: &str) -> Option<&InstanceNode> {
        self.instances.iter().find(|node| node.path == path)
    }

    /// The net under `path`, if any.
    #[must_use]
    pub fn signal(&self, path: &str) -> Option<&SignalInfo> {
        self.signals.iter().find(|signal| signal.path == path)
    }
}

fn empty_report(top: Option<String>) -> HierarchyReport {
    HierarchyReport {
        tool: "slang".to_string(),
        top,
        instances: Vec::new(),
        signals: Vec::new(),
        elapsed: Duration::ZERO,
        peak_rss_kb: 0,
        warning: None,
        failure: None,
    }
}

/// Elaborate `files` and return the hierarchy + driver/load report.
#[must_use]
pub fn elaborate(files: &[PathBuf], options: &HierarchyOptions) -> HierarchyReport {
    elaborate_impl(files, options, None)
}

/// Elaborate `files` and hand the raw `--ast-json` document to a sink instead of
/// parsing it into the hierarchy's instance/net lists.
///
/// The module catalog needs the same `slang` invocation, binary resolution, cwd
/// rule, timeout and size bounds as [`elaborate`] but a different interpretation
/// of the document, and it must not pay for the hierarchy parse first. One
/// runner keeps the two products from drifting on how the tool is invoked; the
/// returned report still carries the tool name, the elapsed time, the peak RSS
/// and the typed failure, only with empty `instances`/`signals`.
pub(crate) fn run_ast_json(
    files: &[PathBuf],
    options: &HierarchyOptions,
) -> (HierarchyReport, Option<String>) {
    let mut captured: Option<String> = None;
    let report = {
        let mut sink = |text: &str| captured = Some(text.to_string());
        elaborate_impl(files, options, Some(&mut sink))
    };
    (report, captured)
}

fn elaborate_impl(
    files: &[PathBuf],
    options: &HierarchyOptions,
    mut on_json: Option<&mut dyn FnMut(&str)>,
) -> HierarchyReport {
    let started = Instant::now();
    let mut report = empty_report(options.top.clone());
    if files.is_empty() {
        report.failure = Some(LintFailure::Io {
            message: "the project file set is empty: nothing to elaborate".to_string(),
        });
        return report;
    }
    let binary = resolve_binary_for_current_process(options.slang_binary.as_deref());
    let Some(chosen) = binary.chosen.clone() else {
        report.failure = Some(LintFailure::BinaryNotFound {
            attempts: binary.attempts.clone(),
        });
        return report;
    };
    report.tool = chosen
        .file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_else(|| "slang".to_string());

    let temp = match tempfile::Builder::new().prefix("vd-hier-").tempdir() {
        Ok(temp) => temp,
        Err(error) => {
            report.failure = Some(LintFailure::Io {
                message: format!("creating a temporary directory failed: {error}"),
            });
            return report;
        }
    };
    let json_path = temp.path().join("ast.json");

    let mut command = Command::new(&chosen);
    sanitize_tool_child_env(&mut command);
    if let Some(top) = &options.top {
        command.arg("--top").arg(top);
    }
    command
        .arg("--ast-json")
        .arg(&json_path)
        .arg("--ast-json-source-info");
    for directory in &options.include_dirs {
        let mut value = std::ffi::OsString::from("-I");
        value.push(directory.as_os_str());
        command.arg(value);
    }
    for define in &options.defines {
        let define = define.trim();
        if !define.is_empty() {
            let mut value = std::ffi::OsString::from("-D");
            value.push(define);
            command.arg(value);
        }
    }
    command
        .args(files)
        .current_dir(project_root(files))
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(error) => {
            report.failure = Some(LintFailure::Spawn {
                message: format!("{}: {error}", chosen.display()),
            });
            return report;
        }
    };

    let mut peak_rss_kb = 0_u64;
    let wait_started = Instant::now();
    let outcome = loop {
        if let Ok(status) = read_peak_rss(child.id()) {
            peak_rss_kb = peak_rss_kb.max(status);
        }
        match child.try_wait() {
            Ok(Some(status)) => break Ok(status.code()),
            Ok(None) => {}
            Err(error) => break Err(format!("waiting for slang failed: {error}")),
        }
        if wait_started.elapsed() >= options.timeout {
            let _ = child.kill();
            let _ = child.wait();
            report.failure = Some(LintFailure::Timeout {
                after: options.timeout,
            });
            report.elapsed = started.elapsed();
            report.peak_rss_kb = peak_rss_kb;
            return report;
        }
        std::thread::sleep(POLL_INTERVAL);
    };
    report.peak_rss_kb = peak_rss_kb;

    let mut stderr = String::new();
    if let Some(mut handle) = child.stderr.take() {
        let _ = handle.read_to_string(&mut stderr);
    }
    let exit_code = match outcome {
        Err(message) => {
            report.failure = Some(LintFailure::Io { message });
            report.elapsed = started.elapsed();
            return report;
        }
        Ok(code) => code,
    };
    // A nonzero exit WITH an AST JSON is a normal result: slang exits 1 when it
    // reported errors but still wrote the elaborated tree (the crate rule in
    // lib.rs). Only a run that produced no usable file is MissingOutput, and
    // then `stderr` carries the reason (e.g. the loader's GLIBC message).
    let metadata = match std::fs::metadata(&json_path) {
        Ok(metadata) => metadata,
        Err(_) if exit_code != Some(0) => {
            report.failure = Some(missing_output(exit_code, &stderr));
            report.elapsed = started.elapsed();
            return report;
        }
        Err(error) => {
            report.failure = Some(LintFailure::Io {
                message: format!("stat {} failed: {error}", json_path.display()),
            });
            report.elapsed = started.elapsed();
            return report;
        }
    };
    if metadata.len() == 0 && exit_code != Some(0) {
        report.failure = Some(missing_output(exit_code, &stderr));
        report.elapsed = started.elapsed();
        return report;
    }
    if metadata.len() > options.max_json_bytes as u64 {
        report.failure = Some(LintFailure::OutputTooLarge {
            limit: options.max_json_bytes,
        });
        report.elapsed = started.elapsed();
        return report;
    }
    let text = match std::fs::read_to_string(&json_path) {
        Ok(text) => text,
        Err(error) => {
            report.failure = Some(LintFailure::Io {
                message: format!("reading {} failed: {error}", json_path.display()),
            });
            report.elapsed = started.elapsed();
            return report;
        }
    };
    if let Some(sink) = on_json.as_mut() {
        // The catalog interprets the document itself; parsing it into the
        // hierarchy's instance/net lists here would be wasted work (and memory)
        // on a large project.
        sink(&text);
    } else {
        match parse_design(&text) {
            Ok(design) => {
                report.instances = design.instances;
                report.signals = design.signals;
                if files.len() > options.max_files {
                    report.warning = Some(format!(
                        "the project has {} files (limit {}): elaboration may take longer than usual",
                        files.len(),
                        options.max_files
                    ));
                } else if report.instances.len() > options.max_instances {
                    report.warning = Some(format!(
                        "the design has {} instances (limit {}): the module tree may be slow",
                        report.instances.len(),
                        options.max_instances
                    ));
                }
            }
            Err(message) => {
                report.failure = Some(if exit_code != Some(0) && text.trim().is_empty() {
                    missing_output(exit_code, &stderr)
                } else {
                    LintFailure::InvalidJson { message, stderr }
                });
            }
        }
    }
    report.elapsed = started.elapsed();
    report
}

/// The failure for a child that exited without a usable AST JSON.
fn missing_output(exit_code: Option<i32>, stderr: &str) -> LintFailure {
    LintFailure::MissingOutput {
        exit_code,
        stderr: stderr.to_string(),
    }
}

/// The directory the child runs in: the first file's parent (the project root
/// as the caller handed it in). Any other cwd makes slang's `source_file`
/// values relative to the wrong base.
fn project_root(files: &[PathBuf]) -> PathBuf {
    files
        .first()
        .and_then(|file| file.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

#[cfg(target_os = "linux")]
fn read_peak_rss(pid: u32) -> Result<u64, std::io::Error> {
    let status = std::fs::read_to_string(format!("/proc/{pid}/status"))?;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("VmHWM:") {
            let value = rest.split_whitespace().next().unwrap_or("0");
            return Ok(value.parse().unwrap_or(0));
        }
    }
    Ok(0)
}

#[cfg(not(target_os = "linux"))]
fn read_peak_rss(_pid: u32) -> Result<u64, std::io::Error> {
    Ok(0)
}

/// The parsed design (internal: what [`parse_design`] returns).
struct Design {
    instances: Vec<InstanceNode>,
    signals: Vec<SignalInfo>,
}

/// Bodies keyed by slug address, for the deduplicated `"body": "<addr> name"`.
pub(crate) struct Bodies<'a> {
    pub(crate) by_address: HashMap<u64, &'a Value>,
}

impl<'a> Bodies<'a> {
    pub(crate) fn index(root: &'a Value) -> Self {
        let mut by_address = HashMap::new();
        collect_bodies(root, &mut by_address);
        Self { by_address }
    }

    pub(crate) fn of(&self, instance: &'a Value) -> Option<&'a Value> {
        match instance.get("body") {
            Some(Value::Object(_)) => instance.get("body"),
            Some(Value::String(reference)) => {
                let address = reference.split_whitespace().next()?.parse::<u64>().ok()?;
                self.by_address.get(&address).copied()
            }
            _ => None,
        }
    }
}

fn collect_bodies<'a>(value: &'a Value, out: &mut HashMap<u64, &'a Value>) {
    match value {
        Value::Object(map) => {
            if map.get("kind").and_then(Value::as_str) == Some("InstanceBody") {
                if let Some(address) = map.get("addr").and_then(Value::as_u64) {
                    out.insert(address, value);
                }
            }
            for child in map.values() {
                collect_bodies(child, out);
            }
        }
        Value::Array(items) => {
            for item in items {
                collect_bodies(item, out);
            }
        }
        _ => {}
    }
}

pub(crate) fn symbol_name(value: &Value) -> Option<String> {
    let text = match value {
        Value::Object(map) => map.get("symbol").and_then(Value::as_str)?,
        Value::String(text) => text.as_str(),
        _ => return None,
    };
    text.split_once(' ').map_or_else(
        || Some(text.to_string()),
        |(_, name)| Some(name.to_string()),
    )
}

fn text_field<'a>(value: &'a Value, key: &str) -> Option<&'a str> {
    value.get(key).and_then(Value::as_str)
}

fn number_field(value: &Value, key: &str) -> Option<u32> {
    value
        .get(key)
        .and_then(Value::as_u64)
        .and_then(|number| u32::try_from(number).ok())
}

/// A position starting at `prefix` (`source_file`, `source_line`, `source_column`
/// or the `_start` flavour used by expressions).
fn position(value: &Value, prefix: &str) -> Option<(PathBuf, u32, u32)> {
    let file = text_field(value, &format!("{prefix}_file"))?;
    let line = number_field(value, &format!("{prefix}_line"))?;
    let column = number_field(value, &format!("{prefix}_column"))?;
    Some((
        PathBuf::from(file),
        line.saturating_sub(1),
        column.saturating_sub(1),
    ))
}

pub(crate) fn symbol_position(value: &Value) -> Option<(PathBuf, u32, u32)> {
    position(value, "source").filter(|_| value.get("source_line").is_some())
}

/// Expression nodes serialise their span as `source_file_start` /
/// `source_line_start` / `source_column_start` (suffix last), unlike symbols.
fn ranged_position(value: &Value, suffix: &str) -> Option<(PathBuf, u32, u32)> {
    let file = text_field(value, &format!("source_file_{suffix}"))?;
    let line = number_field(value, &format!("source_line_{suffix}"))?;
    let column = number_field(value, &format!("source_column_{suffix}"))?;
    Some((
        PathBuf::from(file),
        line.saturating_sub(1),
        column.saturating_sub(1),
    ))
}

fn expr_position(value: &Value) -> Option<(PathBuf, u32, u32)> {
    symbol_position(value)
        .or_else(|| ranged_position(value, "start"))
        .or_else(|| ranged_position(value, "end"))
}

fn endpoint(
    path: String,
    kind: EndpointKind,
    detail: String,
    at: Option<(PathBuf, u32, u32)>,
) -> Endpoint {
    let (file, line, column) = at.unwrap_or_else(|| (PathBuf::new(), 0, 0));
    Endpoint {
        path,
        file,
        line,
        column,
        kind,
        detail,
        source: None,
    }
}

/// The net an output-port connection writes, or a `NamedValue` expression.
///
/// A port may be bound to a bit/part select (`b[g]`, `b[3:0]`, `s.field`); the
/// endpoint then belongs to the base net, which is what the editor jumps to.
/// Without this, every connection inside a `generate` loop — where binding a
/// bus element is the normal shape — would be silently dropped.
fn expr_net(expr: &Value) -> Option<String> {
    match expr.get("kind").and_then(Value::as_str) {
        Some("NamedValue") => symbol_name(expr),
        Some("Assignment") => expr.get("left").and_then(expr_net),
        Some("ElementSelect" | "RangeSelect" | "MemberAccess" | "Parenthesized" | "Conversion") => {
            expr.get("value").and_then(expr_net)
        }
        _ => None,
    }
}

fn collect_named_values<'a>(value: &'a Value, out: &mut Vec<&'a Value>) {
    match value {
        Value::Object(map) => {
            if map.get("kind").and_then(Value::as_str) == Some("NamedValue") {
                out.push(value);
            }
            for child in map.values() {
                collect_named_values(child, out);
            }
        }
        Value::Array(items) => {
            for item in items {
                collect_named_values(item, out);
            }
        }
        _ => {}
    }
}

fn collect_assignments<'a>(value: &'a Value, out: &mut Vec<&'a Value>) {
    match value {
        Value::Object(map) => {
            if map.get("kind").and_then(Value::as_str) == Some("Assignment") {
                out.push(value);
            }
            for child in map.values() {
                collect_assignments(child, out);
            }
        }
        Value::Array(items) => {
            for item in items {
                collect_assignments(item, out);
            }
        }
        _ => {}
    }
}

/// The `kind` tag every slang AST node carries.
pub(crate) fn node_kind(value: &Value) -> &str {
    value.get("kind").and_then(Value::as_str).unwrap_or("")
}

/// Join a scope path with a child segment.
///
/// `[i]` segments attach without a dot (`top.g_leaf[0]`, `top.u_arr[3]`), which
/// is the hierarchical name slang itself prints for an unnamed loop iteration or
/// an instance-array element; every other segment is a member name.
pub(crate) fn join_path(parent: &str, segment: &str) -> String {
    if segment.starts_with('[') {
        format!("{parent}{segment}")
    } else if parent.is_empty() {
        segment.to_string()
    } else {
        format!("{parent}.{segment}")
    }
}

/// Every instance inside one lexical scope, with the hierarchical path it must
/// get — including the instances that live inside generate constructs and
/// instance arrays.
///
/// This is the walk that makes a real design visible: `slang` serialises a
/// `generate for` as `GenerateBlockArray` → `GenerateBlock` (`LoopIteration`,
/// empty `name`, a `constructIndex`) → the instantiated `Instance`, so a walker
/// that only looks at direct `Instance` members of a body sees the module tree
/// of a toy design and nothing else. Members of kinds not listed here cannot
/// hold elaborated instances (ports, nets, parameters, procedures, genvars, …),
/// so they are not descended into.
pub(crate) fn scope_instances<'a>(
    members: &[&'a Value],
    scope: &str,
    out: &mut Vec<(&'a Value, String)>,
) {
    for &member in members {
        match node_kind(member) {
            "Instance" => {
                let name = member.get("name").and_then(Value::as_str).unwrap_or("");
                out.push((member, join_path(scope, name)));
            }
            "InstanceArray" => {
                let name = member.get("name").and_then(Value::as_str).unwrap_or("");
                let base = join_path(scope, name);
                if let Some(items) = member.get("members").and_then(Value::as_array) {
                    for (index, item) in items.iter().enumerate() {
                        out.push((item, join_path(&base, &format!("[{index}]"))));
                    }
                }
            }
            "GenerateBlockArray" => {
                let name = member.get("name").and_then(Value::as_str).unwrap_or("");
                let base = if name.is_empty() {
                    scope.to_string()
                } else {
                    join_path(scope, name)
                };
                if let Some(items) = member.get("members").and_then(Value::as_array) {
                    let blocks: Vec<&Value> = items.iter().collect();
                    push_generate_blocks(&blocks, &base, out);
                }
            }
            "GenerateBlock" => {
                let blocks = [member];
                push_generate_blocks(&blocks, scope, out);
            }
            _ => {}
        }
    }
}

/// The instances of one or more `GenerateBlock`s: a labelled block uses its
/// name (`g_if`, `g_case`), an unnamed loop iteration its `constructIndex`
/// (`[0]`, `[1]`, …, so two iterations never share a path).
fn push_generate_blocks<'a>(
    blocks: &[&'a Value],
    prefix: &str,
    out: &mut Vec<(&'a Value, String)>,
) {
    for block in blocks {
        let name = block.get("name").and_then(Value::as_str).unwrap_or("");
        let segment = if name.is_empty() {
            match block.get("constructIndex").and_then(Value::as_u64) {
                Some(index) => format!("[{index}]"),
                None => "[]".to_string(),
            }
        } else {
            name.to_string()
        };
        let path = join_path(prefix, &segment);
        if let Some(members) = block.get("members").and_then(Value::as_array) {
            let members: Vec<&Value> = members.iter().collect();
            scope_instances(&members, &path, out);
        }
    }
}

/// Parse the `--ast-json` document into the report's two lists.
fn parse_design(text: &str) -> Result<Design, String> {
    let document: Value = serde_json::from_str(text)
        .map_err(|error| format!("the AST JSON is malformed: {error}"))?;
    let design = document
        .get("design")
        .ok_or_else(|| "the AST JSON has no `design` object".to_string())?;
    // A whole-project elaboration (no `--top`) serialises EVERY design root as an
    // `Instance` member of `design`; taking only the first one answers for one
    // root out of a thousand and silently truncates the report.
    let roots: Vec<&Value> = design
        .get("members")
        .and_then(Value::as_array)
        .map(|members| {
            members
                .iter()
                .filter(|member| node_kind(member) == "Instance")
                .collect()
        })
        .unwrap_or_default();
    if roots.is_empty() {
        return Err("the AST JSON has no elaborated top instance".to_string());
    }

    let bodies = Bodies::index(&document);
    let mut instances = Vec::new();
    let mut signals = Vec::new();
    for root in roots {
        let name = root.get("name").and_then(Value::as_str).unwrap_or("");
        walk_instance(root, name, &bodies, &mut instances, &mut signals);
    }
    link_chains(&mut signals);
    Ok(Design { instances, signals })
}

fn walk_instance<'a>(
    instance: &'a Value,
    path: &str,
    bodies: &Bodies<'a>,
    instances: &mut Vec<InstanceNode>,
    signals: &mut Vec<SignalInfo>,
) {
    let Some(body) = bodies.of(instance) else {
        return;
    };
    let module = body.get("name").and_then(Value::as_str).unwrap_or("");
    let (file, line, column) = symbol_position(instance).unwrap_or_default();
    instances.push(InstanceNode {
        path: path.to_string(),
        module: module.to_string(),
        file,
        line,
        column,
    });

    let members: Vec<&Value> = body
        .get("members")
        .and_then(Value::as_array)
        .map(|members| members.iter().collect())
        .unwrap_or_default();

    // Every instance of this scope, generate constructs and instance arrays
    // included. Used twice: their port connections bind to THIS scope's nets,
    // and each one is walked as a child below.
    let mut scoped: Vec<(&Value, String)> = Vec::new();
    scope_instances(&members, path, &mut scoped);

    let mut local: Vec<SignalInfo> = Vec::new();
    for member in &members {
        let kind = member.get("kind").and_then(Value::as_str).unwrap_or("");
        if !matches!(kind, "Net" | "Variable" | "Port") {
            continue;
        }
        let Some(name) = member.get("name").and_then(Value::as_str) else {
            continue;
        };
        if name.is_empty()
            || local
                .iter()
                .any(|signal| signal.path == format!("{path}.{name}"))
        {
            continue;
        }
        local.push(SignalInfo {
            path: format!("{path}.{name}"),
            kind: kind.to_string(),
            declaration: symbol_position(member),
            drivers: Vec::new(),
            loads: Vec::new(),
            chain: Vec::new(),
        });
    }

    // Instance port connections (instances inside generate blocks included).
    for (member, child_path) in &scoped {
        let member: &Value = member;
        let Some(Value::Array(connections)) = member.get("connections") else {
            continue;
        };
        for connection in connections {
            let port = connection.get("port").cloned().unwrap_or(Value::Null);
            let direction = port
                .get("direction")
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_ascii_lowercase();
            let port_name = port.get("name").and_then(Value::as_str).unwrap_or("");
            let expr = connection.get("expr").cloned().unwrap_or(Value::Null);
            let at = expr_position(&expr).or_else(|| symbol_position(member));
            let target = format!("{child_path}.{port_name}");
            if direction == "out" || direction == "inout" {
                if let Some(net) = expr_net(&expr) {
                    if let Some(signal) = local
                        .iter_mut()
                        .find(|signal| signal.path == format!("{path}.{net}"))
                    {
                        signal.drivers.push(endpoint(
                            target.clone(),
                            EndpointKind::InstanceOutputPort,
                            format!("port {port_name} (out)"),
                            at.clone(),
                        ));
                    }
                }
            }
            if direction == "in" || direction == "inout" {
                let mut names = Vec::new();
                collect_named_values(&expr, &mut names);
                for name_value in names {
                    let Some(net) = symbol_name(name_value) else {
                        continue;
                    };
                    if let Some(signal) = local
                        .iter_mut()
                        .find(|signal| signal.path == format!("{path}.{net}"))
                    {
                        signal.loads.push(endpoint(
                            target.clone(),
                            EndpointKind::InstanceInputPort,
                            format!("port {port_name} (in)"),
                            at.clone(),
                        ));
                    }
                }
            }
        }
    }

    // Continuous assignments: LHS drives, RHS reads.
    for member in &members {
        if member.get("kind").and_then(Value::as_str) != Some("ContinuousAssign") {
            continue;
        }
        let Some(assignment) = member.get("assignment") else {
            continue;
        };
        let at = expr_position(assignment).or_else(|| symbol_position(member));
        let source_net = assignment.get("right").and_then(symbol_name);
        if let Some(left) = assignment.get("left").and_then(symbol_name) {
            if let Some(signal) = local
                .iter_mut()
                .find(|signal| signal.path == format!("{path}.{left}"))
            {
                let mut record = endpoint(
                    format!("{path}.{left}"),
                    EndpointKind::ContinuousAssign,
                    "assign".to_string(),
                    at.clone(),
                );
                record.source = source_net.map(|net| format!("{path}.{net}"));
                signal.drivers.push(record);
            }
        }
        let mut names = Vec::new();
        collect_named_values(assignment.get("right").unwrap_or(&Value::Null), &mut names);
        for name_value in names {
            let Some(right) = symbol_name(name_value) else {
                continue;
            };
            if let Some(signal) = local
                .iter_mut()
                .find(|signal| signal.path == format!("{path}.{right}"))
            {
                signal.loads.push(endpoint(
                    format!("{path}.{right}"),
                    EndpointKind::ContinuousAssign,
                    "assign source".to_string(),
                    at.clone(),
                ));
            }
        }
    }

    // Procedural blocks: any LHS inside drives.
    for member in &members {
        if member.get("kind").and_then(Value::as_str) != Some("ProceduralBlock") {
            continue;
        }
        let label = member
            .get("procedureKind")
            .and_then(Value::as_str)
            .unwrap_or("procedural");
        let mut found = Vec::new();
        if let Some(body) = member.get("body") {
            collect_assignments(body, &mut found);
        }
        for assignment in found {
            let Some(left) = assignment.get("left").and_then(symbol_name) else {
                continue;
            };
            let at = expr_position(assignment).or_else(|| symbol_position(member));
            if let Some(signal) = local
                .iter_mut()
                .find(|signal| signal.path == format!("{path}.{left}"))
            {
                signal.drivers.push(endpoint(
                    format!("{path}.{left}"),
                    EndpointKind::Procedural,
                    label.to_string(),
                    at,
                ));
            }
        }
    }

    signals.extend(local);

    // Children last, so the report is breadth-ordered by path.
    for (child, child_path) in &scoped {
        walk_instance(child, child_path, bodies, instances, signals);
    }
}

/// Attach the driver chain that continues inside a child instance.
///
/// Hops: parent net → (instance output port) → child net → (continuous assign,
/// following its `source`) → child net → terminal drivers (an `always_ff`, …).
fn link_chains(signals: &mut [SignalInfo]) {
    let index: HashMap<String, usize> = signals
        .iter()
        .enumerate()
        .map(|(index, signal)| (signal.path.clone(), index))
        .collect();
    let mut computed: Vec<(usize, Vec<Endpoint>)> = Vec::new();
    for (start, signal) in signals.iter().enumerate() {
        if signal.drivers.is_empty() {
            continue;
        }
        let mut chain: Vec<Endpoint> = Vec::new();
        let mut current = start;
        let mut seen = std::collections::HashSet::new();
        for _ in 0..8 {
            if !seen.insert(current) {
                break;
            }
            let Some(node) = signals.get(current) else {
                break;
            };
            // 1) instance output port writing this net
            if let Some(port) = node.drivers.iter().find(|driver| {
                driver.kind == EndpointKind::InstanceOutputPort && index.contains_key(&driver.path)
            }) {
                chain.push(port.clone());
                let Some(&next) = index.get(&port.path) else {
                    break;
                };
                current = next;
                continue;
            }
            // 2) continuous assignment whose source net is known
            if let Some(assign) = node.drivers.iter().find(|driver| {
                driver.kind == EndpointKind::ContinuousAssign
                    && driver
                        .source
                        .as_ref()
                        .is_some_and(|source| index.contains_key(source))
            }) {
                chain.push(assign.clone());
                let Some(source) = assign.source.as_ref() else {
                    break;
                };
                let Some(&next) = index.get(source) else {
                    break;
                };
                current = next;
                continue;
            }
            // 3) terminal: the drivers we have not recorded yet
            for driver in &node.drivers {
                if !chain.iter().any(|seen| seen == driver) {
                    chain.push(driver.clone());
                }
            }
            break;
        }
        if !chain.is_empty() {
            computed.push((start, chain));
        }
    }
    for (index, chain) in computed {
        if let Some(signal) = signals.get_mut(index) {
            signal.chain = chain;
        }
    }
}

/// Parse an `--ast-json` document that the caller already has.
///
/// This is the same parser the runner uses; it is public so a caller that keeps
/// the elaboration output (or a test fixture) can turn it into the report without
/// spawning the tool again.
///
/// # Errors
///
/// The text is not JSON, or it carries no elaborated design.
pub fn parse_ast_json(text: &str) -> Result<HierarchyReport, String> {
    let design = parse_design(text)?;
    let mut report = empty_report(None);
    report.instances = design.instances;
    report.signals = design.signals;
    Ok(report)
}

/// True when the run produced no instances at all.
#[must_use]
pub fn is_empty(report: &HierarchyReport) -> bool {
    report.failure.is_none() && report.instances.is_empty()
}

/// The binary resolution this crate would use (for diagnostics/evidence).
#[must_use]
pub fn resolution(options: &HierarchyOptions) -> BinaryResolution {
    resolve_binary_for_current_process(options.slang_binary.as_deref())
}
