//! batch-20261118 AC-1: the project build context reaches slang-server.
//!
//! slang-server 0.3.1 ignores argv `-I`/`-D` (measured: `--help` has no such
//! option and argv values never arrive). The only working channel is
//! `workspace/executeCommand`; this test drives the REAL server over a synthetic
//! project whose `inc/defs.svh` is OUTSIDE the analysed file's directory:
//!
//! 1. before the context push the include cannot be resolved (the server
//!    publishes `'defs.svh': No such file or directory` and the macro is
//!    unknown);
//! 2. after `SessionHandle::set_build_context` the include resolves (that error
//!    is gone), the outline carries the module, and deleting one semicolon
//!    produces a real `expected ';'` diagnostic.
//!
//! The test SKIPs when no slang-server is resolvable, exactly like the verible
//! tests skip without a binary: a checkout is not expected to ship the server.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use vd_lsp::{
    LspBackend, LspConfig, PollOutcome, SessionDiagnostic, SessionHandle, SessionPhase,
    SessionRequest, SessionResponse,
};

/// Resolve slang-server: `VEDIT_SLANG_SERVER`, or the AppDir wrapper this
/// repository builds (whose bundled loader runs on the CentOS 7 test image too).
fn slang_server() -> Option<PathBuf> {
    if let Ok(value) = std::env::var("VEDIT_SLANG_SERVER") {
        let path = PathBuf::from(value);
        if path.is_file() {
            return Some(path);
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let bundled = root.join("packaging/runtime/out/AppDir/bin/slang-server");
    if bundled.is_file() {
        return Some(bundled);
    }
    let raw = root.join("packaging/runtime/out/AppDir/usr/lib/lsp/slang-server");
    if raw.is_file() {
        return Some(raw);
    }
    None
}

/// A unique scratch project, removed on drop.
struct Project {
    root: PathBuf,
}

impl Project {
    fn new(tag: &str) -> Result<Self> {
        Self::named("vedit-ac1", tag)
    }

    /// A project whose ROOT path contains a space (H1: a legal checkout path
    /// such as `/home/user/My Projects/…`).
    fn with_space_in_root(tag: &str) -> Result<Self> {
        Self::named("vedit ac1", tag)
    }

    fn named(prefix: &str, tag: &str) -> Result<Self> {
        let root = std::env::temp_dir().join(format!(
            "{prefix}-{tag}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|value| value.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(root.join("design/rtl"))?;
        std::fs::create_dir_all(root.join("inc"))?;
        std::fs::write(root.join("inc/defs.svh"), "`define WIDTH 8\n")?;
        std::fs::write(
            root.join("design/rtl/top.sv"),
            "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data;\nendmodule\n",
        )?;
        Ok(Self { root })
    }

    fn workspace(&self) -> PathBuf {
        self.root.join("design")
    }

    fn top(&self) -> PathBuf {
        self.root.join("design/rtl/top.sv")
    }

    fn inc(&self) -> PathBuf {
        self.root.join("inc")
    }
}

impl Drop for Project {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.root);
    }
}

fn wait_ready(session: &SessionHandle, budget: Duration) -> Result<vd_lsp::SessionStatus> {
    let deadline = Instant::now() + budget;
    loop {
        let status = session.status();
        match status.phase {
            SessionPhase::Ready => return Ok(status),
            SessionPhase::Failed => bail!("the session failed: {}", status.message),
            SessionPhase::Starting => {}
            SessionPhase::Stopped => bail!("the session stopped before Ready"),
        }
        if Instant::now() >= deadline {
            bail!("the session did not reach Ready within {budget:?}");
        }
        std::thread::sleep(Duration::from_millis(25));
    }
}

/// Ask for the current diagnostics rows of `file`, bounded.
fn diagnostics_of(
    session: &SessionHandle,
    file: &Path,
    budget: Duration,
) -> Result<(Vec<SessionDiagnostic>, bool)> {
    let id = session.request(SessionRequest::Diagnostics {
        file: file.to_path_buf(),
    })?;
    let deadline = Instant::now() + budget;
    loop {
        match session.poll(id) {
            PollOutcome::Pending => {}
            PollOutcome::Ready(SessionResponse::Diagnostics { rows, published }) => {
                return Ok((rows, published));
            }
            PollOutcome::Ready(other) => bail!("unexpected response: {other:?}"),
            PollOutcome::Failed(error) => bail!("diagnostics request failed: {error}"),
            PollOutcome::Unknown => bail!("the diagnostics request id vanished"),
        }
        if Instant::now() >= deadline {
            bail!("the diagnostics request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn messages(rows: &[SessionDiagnostic]) -> String {
    rows.iter()
        .map(|row| row.message.clone())
        .collect::<Vec<_>>()
        .join(" | ")
}

/// Zero-based line and UTF-16 character of the first occurrence of `needle`.
fn position_of(text: &str, needle: &str) -> Result<(u32, u32)> {
    let Some(offset) = text.find(needle) else {
        bail!("{needle:?} is not in the fixture text");
    };
    let before = &text[..offset];
    let line = before.matches('\n').count() as u32;
    let line_prefix = before.rsplit('\n').next().unwrap_or("");
    let character = line_prefix.encode_utf16().count() as u32;
    Ok((line, character))
}

/// Request the outline once and flatten it to `name (detail), ...`.
fn symbol_summary(session: &SessionHandle, file: &Path, budget: Duration) -> Result<String> {
    let id = session.request(SessionRequest::DocumentSymbols {
        file: file.to_path_buf(),
    })?;
    let deadline = Instant::now() + budget;
    loop {
        match session.poll(id) {
            PollOutcome::Pending => {}
            PollOutcome::Ready(SessionResponse::Symbols(nodes)) => {
                return Ok(nodes
                    .iter()
                    .map(|node| {
                        format!("{} ({})", node.name, node.detail.as_deref().unwrap_or("-"))
                    })
                    .collect::<Vec<_>>()
                    .join(", "));
            }
            PollOutcome::Ready(other) => bail!("unexpected symbol response: {other:?}"),
            PollOutcome::Failed(error) => bail!("symbol request failed: {error}"),
            PollOutcome::Unknown => bail!("the symbol request id vanished"),
        }
        if Instant::now() >= deadline {
            bail!("the symbol request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

/// Every regular file under `root`, path-sorted, with its bytes: the before/after
/// witness for "the build context does not write into the project".
fn snapshot(root: &Path) -> Result<Vec<(PathBuf, Vec<u8>)>> {
    fn walk(directory: &Path, files: &mut Vec<(PathBuf, Vec<u8>)>) -> Result<()> {
        for entry in std::fs::read_dir(directory)? {
            let entry = entry?;
            let path = entry.path();
            let metadata = std::fs::symlink_metadata(&path)?;
            if metadata.is_dir() {
                walk(&path, files)?;
            } else if metadata.is_file() {
                files.push((path.clone(), std::fs::read(&path)?));
            }
        }
        Ok(())
    }
    let mut files = Vec::new();
    walk(root, &mut files)?;
    files.sort_by(|left, right| left.0.cmp(&right.0));
    Ok(files)
}

#[test]
fn include_context_reaches_slang_server_and_a_missing_semicolon_is_reported() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::new("build-context")?;
    let top = project.top();

    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;

    // --- 1. BEFORE: the include lives outside the file's directory -------------
    let (before, published) = diagnostics_of(&session, &top, Duration::from_secs(10))?;
    assert!(
        published,
        "the server must have published a set for the opened file"
    );
    assert!(
        before
            .iter()
            .any(|row| row.message.contains("No such file or directory")),
        "before the push the include must be unresolved; got: {}",
        messages(&before)
    );
    println!("OBSERVED before rows: {}", messages(&before));

    // --- 2. Push the project build context through workspace/executeCommand ---
    session.set_build_context(vec![project.inc()], vec!["WIDTH=8".to_string()])?;

    // The server re-analyses after `slang.setBuildFile`; the snapshot is
    // replaced when its publish lands, so poll rather than assume.
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut after = Vec::new();
    while Instant::now() < deadline {
        let (rows, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        let unresolved = rows.iter().any(|row| {
            row.message.contains("No such file or directory")
                || row.message.contains("unknown macro")
        });
        after = rows;
        if !unresolved {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        !after
            .iter()
            .any(|row| row.message.contains("No such file or directory")),
        "after the push the include must resolve; got: {}",
        messages(&after)
    );
    assert!(
        !after
            .iter()
            .any(|row| row.message.contains("unknown macro")),
        "after the push the project define must be known; got: {}",
        messages(&after)
    );
    println!("OBSERVED after rows: {}", messages(&after));

    // --- 3. The outline really has the module --------------------------------
    let symbols = session.request(SessionRequest::DocumentSymbols { file: top.clone() })?;
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        match session.poll(symbols) {
            PollOutcome::Pending => {}
            PollOutcome::Ready(SessionResponse::Symbols(nodes)) => {
                assert!(
                    !nodes.is_empty(),
                    "the outline must carry the module after the context arrived"
                );
                let summary = nodes
                    .iter()
                    .map(|node| {
                        format!("{} ({})", node.name, node.detail.as_deref().unwrap_or("-"))
                    })
                    .collect::<Vec<_>>()
                    .join(", ");
                println!("OBSERVED symbols: {summary}");
                // F1: the define must reach the server through the generated
                // file list. `[`WIDTH-1:0]` resolves to `[8-1:0]` only when
                // `WIDTH=8` is known, so this is the define channel's effect,
                // not merely "the include resolved".
                assert!(
                    nodes.iter().any(|node| node
                        .detail
                        .as_deref()
                        .is_some_and(|detail| detail.contains("8-1:0"))),
                    "the project define must reach the server (data as [8-1:0]); got: {summary}"
                );
                break;
            }
            PollOutcome::Ready(other) => bail!("unexpected symbol response: {other:?}"),
            PollOutcome::Failed(error) => bail!("symbol request failed: {error}"),
            PollOutcome::Unknown => bail!("the symbol request id vanished"),
        }
        if Instant::now() >= deadline {
            bail!("the symbol request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    }

    // --- 4. Delete one semicolon: the server reports `expected ';'` ----------
    let broken = "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data\nendmodule\n";
    session.change_document(&top, broken.to_string())?;
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut malformed = Vec::new();
    while Instant::now() < deadline {
        let (rows, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        malformed = rows;
        if malformed
            .iter()
            .any(|row| row.message.contains("expected ';'"))
        {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        malformed
            .iter()
            .any(|row| row.message.contains("expected ';'")),
        "a deleted semicolon must be reported as expected ';'; got: {}",
        messages(&malformed)
    );
    println!("OBSERVED malformed rows: {}", messages(&malformed));

    assert!(session.wait_for_stop(), "the session must stop cleanly");
    Ok(())
}

/// batch-20261118 F1 (High): the build-context push must not write into the
/// user's project.
///
/// The shipped slang-server 0.3.1 persists `<workspace>/.slang/local/server.json`
/// when a client sends `slang.addDefine` (and keeps applying those defines to
/// later sessions). The session therefore delivers the defines inside the
/// generated file list, pushed with ONE `slang.setBuildFile`: this test proves
/// the project tree is byte-identical before and after, with no `.slang/`.
#[test]
fn the_build_context_push_does_not_write_into_the_project() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::new("no-project-write")?;
    let top = project.top();
    let project_filelist = project.root.join("files.f");
    std::fs::write(
        &project_filelist,
        format!("+incdir+{}\n", project.inc().display()),
    )?;
    let before = snapshot(&project.root)?;

    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;
    session.set_build_context(vec![project.inc()], vec!["WIDTH=8".to_string()])?;
    // Bound the wait on the push's EFFECT (the pushed context resolves the
    // out-of-directory include), not on a fixed sleep: the snapshot below must
    // describe a session that really received the context.
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut resolved = false;
    while Instant::now() < deadline {
        let (rows, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        if !rows
            .iter()
            .any(|row| row.message.contains("No such file or directory"))
        {
            resolved = true;
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        resolved,
        "the pushed build context must resolve the include"
    );
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    drop(session);

    let after = snapshot(&project.root)?;
    assert!(
        !project.root.join(".slang").exists() && !project.workspace().join(".slang").exists(),
        "the project must not gain a .slang/ directory"
    );
    assert_eq!(
        before.len(),
        after.len(),
        "the project file set must not change: before={:?} after={:?}",
        before.iter().map(|(path, _)| path).collect::<Vec<_>>(),
        after.iter().map(|(path, _)| path).collect::<Vec<_>>()
    );
    for ((before_path, before_bytes), (after_path, after_bytes)) in before.iter().zip(&after) {
        assert_eq!(
            before_path, after_path,
            "the project file set must not change"
        );
        assert_eq!(
            before_bytes,
            after_bytes,
            "{} must not be modified by the build-context push",
            before_path.display()
        );
    }
    println!(
        "OBSERVED project-unchanged files={} no-.slang=PASS",
        after.len()
    );
    Ok(())
}

/// batch-20261118 F1: defines with NO include dirs and NO build file still
/// reach the server (the product's settings-only define case). The generated
/// file list then carries `-D<name>[=<value>]` lines alone.
#[test]
fn defines_alone_reach_the_server_through_the_generated_file() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::new("defines-only")?;
    let top = project.top();
    // No `include: the width depends on the project define alone.
    std::fs::write(&top, "module top;\n  logic [`WIDTH-1:0] data;\nendmodule\n")?;
    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;
    let (before, _) = diagnostics_of(&session, &top, Duration::from_secs(10))?;
    assert!(
        messages(&before).contains("unknown macro"),
        "before the push the macro must be unknown; got: {}",
        messages(&before)
    );
    session.set_build_context(vec![], vec!["WIDTH=8".to_string()])?;
    // The define must take effect through the generated file: the macro error
    // disappears from the published diagnostics...
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut rows;
    loop {
        let (current, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        rows = current;
        if !messages(&rows).contains("unknown macro") {
            break;
        }
        if Instant::now() >= deadline {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        !messages(&rows).contains("unknown macro"),
        "the defines-only build context must reach the server; got: {}",
        messages(&rows)
    );
    // ...and the symbol carries the width it produced.
    std::thread::sleep(Duration::from_millis(500));
    let symbols = session.request(SessionRequest::DocumentSymbols { file: top.clone() })?;
    let deadline = Instant::now() + Duration::from_secs(10);
    let width;
    loop {
        match session.poll(symbols) {
            PollOutcome::Pending => {}
            PollOutcome::Ready(SessionResponse::Symbols(nodes)) => {
                width = nodes
                    .iter()
                    .map(|node| {
                        format!("{} ({})", node.name, node.detail.as_deref().unwrap_or("-"))
                    })
                    .collect::<Vec<_>>()
                    .join(", ");
                break;
            }
            PollOutcome::Ready(other) => bail!("unexpected symbol response: {other:?}"),
            PollOutcome::Failed(error) => bail!("symbol request failed: {error}"),
            PollOutcome::Unknown => bail!("the symbol request id vanished"),
        }
        if Instant::now() >= deadline {
            bail!("the symbol request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    println!("OBSERVED defines-only symbols: {width}");
    assert!(
        width.contains("8-1:0"),
        "a define without include dirs/build files must reach the server; got: {width}"
    );
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    Ok(())
}

/// batch-20261118 H1 (High): a legal project path containing whitespace.
///
/// The generated file list used to write `+incdir+/tmp/…/proj with space/inc`
/// unquoted, so the parser split it at the space: the include directory was
/// reported missing and the include stayed unresolved. The argument is now one
/// quoted token, so the include resolves and the define still reaches the
/// server.
#[test]
fn an_include_path_with_a_space_resolves_and_keeps_the_define() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::with_space_in_root("include-space")?;
    assert!(
        project.inc().display().to_string().contains(' '),
        "the fixture must really carry a space in the include path: {}",
        project.inc().display()
    );
    let top = project.top();
    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;
    let (before, _) = diagnostics_of(&session, &top, Duration::from_secs(10))?;
    assert!(
        messages(&before).contains("No such file or directory"),
        "the fixture must start unresolved; got: {}",
        messages(&before)
    );

    session.set_build_context(vec![project.inc()], vec!["WIDTH=8".to_string()])?;
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut after = Vec::new();
    while Instant::now() < deadline {
        let (rows, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        after = rows;
        if !messages(&after).contains("No such file or directory") {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        !messages(&after).contains("No such file or directory"),
        "a space in the include path must not break the include; got: {}",
        messages(&after)
    );
    assert!(
        !messages(&after).contains("unknown macro"),
        "the define must reach the server together with the include; got: {}",
        messages(&after)
    );
    println!("OBSERVED space-path rows: {}", messages(&after));

    let deadline = Instant::now() + Duration::from_secs(10);
    let mut summary = String::new();
    while Instant::now() < deadline {
        summary = symbol_summary(&session, &top, Duration::from_secs(5))?;
        if summary.contains("8-1:0") {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    println!("OBSERVED space-path symbols: {summary}");
    assert!(
        summary.contains("8-1:0"),
        "the space-path context must still produce data as [8-1:0]; got: {summary}"
    );
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    Ok(())
}

/// batch-20261118 H1 (High): a define VALUE containing whitespace.
///
/// The generated file list used to write `-DWIDTH=8 + 4` unquoted; the parser
/// kept `WIDTH=8` and dropped the rest, so the outline showed `logic [8-1:0]`
/// with no error. The whole define is now one quoted token, so the server sees
/// `WIDTH=8 + 4` and the symbol is `logic [8 + 4-1:0]`.
#[test]
fn a_define_value_with_spaces_reaches_the_server_untruncated() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::new("define-multitoken")?;
    let top = project.top();
    // No include: the width depends on the multi-token define alone.
    std::fs::write(&top, "module top;\n  logic [`WIDTH-1:0] data;\nendmodule\n")?;
    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;
    let (before, _) = diagnostics_of(&session, &top, Duration::from_secs(10))?;
    assert!(
        messages(&before).contains("unknown macro"),
        "before the push the macro must be unknown; got: {}",
        messages(&before)
    );

    session.set_build_context(vec![], vec!["WIDTH=8 + 4".to_string()])?;
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut rows = Vec::new();
    while Instant::now() < deadline {
        let (current, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        rows = current;
        if !messages(&rows).contains("unknown macro") {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        !messages(&rows).contains("unknown macro"),
        "the multi-token define must reach the server; got: {}",
        messages(&rows)
    );
    println!("OBSERVED multi-token define rows: {}", messages(&rows));

    let deadline = Instant::now() + Duration::from_secs(10);
    let mut summary = String::new();
    while Instant::now() < deadline {
        summary = symbol_summary(&session, &top, Duration::from_secs(5))?;
        if summary.contains("8 + 4-1:0") {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    println!("OBSERVED multi-token define symbols: {summary}");
    assert!(
        summary.contains("8 + 4-1:0"),
        "the define must not be truncated to its first token; got: {summary}"
    );
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    Ok(())
}

/// batch-20261118 AC-3: a child that exits after Ready is a SESSION state, and
/// the bounded lazy restart brings the session back (MAX_RESTARTS = 3).
#[test]
fn a_child_that_exits_after_ready_marks_the_session_failed_and_restarts() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    let project = Project::new("child-exit")?;
    let top = project.top();
    let config = LspConfig::new(&server, project.workspace()).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    let ready = wait_ready(&session, Duration::from_secs(20))?;
    let pid = ready
        .server_pid
        .ok_or_else(|| anyhow::anyhow!("the Ready session must report a server pid"))?;
    println!("OBSERVED session ready pid={pid}");

    // Kill the child out from under the session, exactly like a crash.
    let killed = std::process::Command::new("kill")
        .args(["-9", &pid.to_string()])
        .status()?;
    assert!(
        killed.success(),
        "the fixture must be able to kill the server child"
    );

    let deadline = Instant::now() + Duration::from_secs(10);
    let failed = loop {
        let status = session.status();
        if status.phase == SessionPhase::Failed {
            break status;
        }
        if Instant::now() >= deadline {
            bail!(
                "the session did not report Failed after its child died: {:?} ({})",
                status.phase,
                status.message
            );
        }
        std::thread::sleep(Duration::from_millis(50));
    };
    println!("OBSERVED failed message: {}", failed.message);
    assert!(
        failed.message.contains("exited after Ready") || failed.message.contains("failed"),
        "the Failed message must name the exit: {}",
        failed.message
    );
    assert!(
        failed.message.contains("restart"),
        "the Failed message must carry the restart count: {}",
        failed.message
    );

    // The next command restarts the server lazily (bounded by MAX_RESTARTS).
    session.change_document(
        &top,
        "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data;\nendmodule\n".to_string(),
    )?;
    let deadline = Instant::now() + Duration::from_secs(20);
    loop {
        let status = session.status();
        if status.phase == SessionPhase::Ready && status.restarts >= 1 {
            println!(
                "OBSERVED recovered restarts={} message={}",
                status.restarts, status.message
            );
            break;
        }
        if status.phase == SessionPhase::Failed && status.restarts > vd_lsp::MAX_RESTARTS {
            bail!(
                "the session gave up beyond MAX_RESTARTS: {}",
                status.message
            );
        }
        if Instant::now() >= deadline {
            bail!(
                "the session did not recover after the lazy restart: {:?} ({})",
                status.phase,
                status.message
            );
        }
        std::thread::sleep(Duration::from_millis(50));
    }

    assert!(session.wait_for_stop(), "the session must stop cleanly");
    Ok(())
}

/// AC-1 regression: the SAME-DIRECTORY include case (the 8051 fixture shape -
/// the macro header sits next to the source) must keep working WITHOUT any
/// context push; the change only adds a channel, it must not break this.
#[test]
fn a_same_directory_include_still_resolves_and_reports_a_missing_semicolon() -> Result<()> {
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        return Ok(());
    };
    // Same directory for the header and the source, as in the 8051 project.
    let root = std::env::temp_dir().join(format!(
        "vedit-ac1-same-dir-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|value| value.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(root.join("rtl"))?;
    std::fs::write(root.join("rtl/defs.svh"), "`define WIDTH 8\n")?;
    std::fs::write(
        root.join("rtl/top.sv"),
        "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data;\nendmodule\n",
    )?;
    let top = root.join("rtl/top.sv");
    let config = LspConfig::new(&server, &root).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        vec![top.clone()],
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;

    let (rows, published) = diagnostics_of(&session, &top, Duration::from_secs(10))?;
    assert!(published, "the same-directory file must be analysed");
    assert!(
        !messages(&rows).contains("No such file or directory"),
        "a same-directory include must resolve without any context push; got: {}",
        messages(&rows)
    );
    println!("OBSERVED same-dir rows: {}", messages(&rows));

    session.change_document(
        &top,
        "`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data\nendmodule\n".to_string(),
    )?;
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut malformed = Vec::new();
    while Instant::now() < deadline {
        let (rows, _) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        malformed = rows;
        if malformed
            .iter()
            .any(|row| row.message.contains("expected ';'"))
        {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(
        malformed
            .iter()
            .any(|row| row.message.contains("expected ';'")),
        "the same-directory case must still report expected ';'; got: {}",
        messages(&malformed)
    );
    println!(
        "OBSERVED same-dir missing semicolon: {}",
        messages(&malformed)
    );
    assert!(session.wait_for_stop(), "the session must stop cleanly");
    let _ = std::fs::remove_dir_all(&root);
    Ok(())
}

/// batch-20261119: realtime lint compiles the SESSION's file set, not the
/// project's imported file list.
///
/// `rtl/uart.v` (port `a` - the definition the module tree picked) and
/// `sim/uart.v` (port `b`) both define `module uart`, and the project's own `.f`
/// lists both. The generated context must name exactly the session files
/// (`rtl/top.v` + `rtl/uart.v`), one absolute path per line, and must never
/// nest the project `.f` with `-f`: nesting it made slang-server compile the
/// whole project, so slang's own last-processed rule could put the `sim`
/// definition in force and report the realtime "module has no such port" false
/// positive this test locks out.
///
/// The byte-level half always runs; the server half SKIPs (like every test in
/// this file) when no slang-server is resolvable.
#[test]
fn the_build_context_compiles_the_session_file_set_not_the_project_filelist() -> Result<()> {
    let root = std::env::temp_dir().join(format!(
        "vedit-lint-scope-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|value| value.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(root.join("rtl"))?;
    std::fs::create_dir_all(root.join("sim"))?;
    std::fs::write(
        root.join("rtl/uart.v"),
        "module uart(input wire a, output wire y);\n  assign y = a;\nendmodule\n",
    )?;
    std::fs::write(
        root.join("sim/uart.v"),
        "module uart(input wire b, output wire y);\n  assign y = b;\nendmodule\n",
    )?;
    let top_text = "module top;\n  wire y;\n  uart u(.a(1'b0), .y(y));\nendmodule\n";
    std::fs::write(root.join("rtl/top.v"), top_text)?;
    let project_filelist = root.join("files.f");
    std::fs::write(
        &project_filelist,
        format!(
            "{}\n{}\n",
            root.join("rtl/uart.v").display(),
            root.join("sim/uart.v").display()
        ),
    )?;

    let top = root.join("rtl/top.v");
    let rtl_uart = root.join("rtl/uart.v");
    let session_files = vec![top.clone(), rtl_uart.clone()];

    // --- 1. the generated context text (no server needed) ------------------
    let text = vd_lsp::build_context_text(&[], &[], &session_files)?;
    assert_eq!(
        text,
        format!("\"{}\"\n\"{}\"\n", top.display(), rtl_uart.display()),
        "the context is a file list of the SESSION files, one absolute path per line"
    );
    assert!(
        text.contains(&top.display().to_string()),
        "the context must carry the session's current file: {text:?}"
    );
    assert!(
        text.contains(&rtl_uart.display().to_string()),
        "the context must carry the session's closure file: {text:?}"
    );
    assert!(
        !text.contains("-f "),
        "a project file list must never be nested: {text:?}"
    );
    assert!(
        !text.contains(&project_filelist.display().to_string()),
        "the project's imported .f must not be named: {text:?}"
    );
    assert!(
        !text.contains("sim/uart.v"),
        "the project's other uart definition must not be listed: {text:?}"
    );
    println!("OBSERVED context text: {text:?}");

    // --- 2. the real server agrees with the session's choice ---------------
    let Some(server) = slang_server() else {
        eprintln!("SKIP: no slang-server binary (set VEDIT_SLANG_SERVER)");
        let _ = std::fs::remove_dir_all(&root);
        return Ok(());
    };
    let config = LspConfig::new(&server, &root).with_backend(LspBackend::Slang);
    let session = SessionHandle::with_budgets(
        config,
        session_files,
        Duration::from_secs(5),
        Duration::from_secs(10),
    )?;
    wait_ready(&session, Duration::from_secs(20))?;
    session.set_build_context(vec![], vec![])?;

    // The rows for the instantiation must never mention the other definition's
    // port. `No such file or directory` / `unknown macro` are unrelated.
    let deadline = Instant::now() + Duration::from_secs(15);
    let mut rows = Vec::new();
    while Instant::now() < deadline {
        let (current, published) = diagnostics_of(&session, &top, Duration::from_secs(5))?;
        rows = current;
        if published {
            break;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let observed = messages(&rows);
    println!("OBSERVED session-scope rows: {observed}");
    for forbidden in [
        "is not a port",
        "no such port",
        "unknown port",
        "duplicate definition",
    ] {
        assert!(
            !observed.contains(forbidden),
            "the other directory's uart must not be compiled ({forbidden:?}): {observed}"
        );
    }

    // Go-to-definition is the structural witness: the module the server has in
    // force must be the file the module tree selected.
    let (line, character) = position_of(top_text, "uart u(")?;
    let id = session.request(SessionRequest::Definition {
        file: top.clone(),
        line,
        character: character + 1,
    })?;
    let deadline = Instant::now() + Duration::from_secs(15);
    let locations = loop {
        match session.poll(id) {
            PollOutcome::Ready(SessionResponse::Locations(rows)) => break rows,
            PollOutcome::Ready(other) => bail!("unexpected definition response: {other:?}"),
            PollOutcome::Failed(error) => bail!("definition request failed: {error}"),
            PollOutcome::Pending | PollOutcome::Unknown => {}
        }
        if Instant::now() >= deadline {
            bail!("the definition request did not settle");
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    let target = locations
        .iter()
        .filter_map(|location| location.uri.to_file_path().ok())
        .find(|path| path.ends_with("uart.v"))
        .ok_or_else(|| anyhow::anyhow!("no definition location for uart: {locations:?}"))?;
    assert_eq!(
        target.canonicalize()?,
        rtl_uart.canonicalize()?,
        "the definition must be the session's rtl/uart.v, not the project's sim/uart.v"
    );
    println!("OBSERVED uart definition: {}", target.display());

    assert!(session.wait_for_stop(), "the session must stop cleanly");
    let _ = std::fs::remove_dir_all(&root);
    Ok(())
}
