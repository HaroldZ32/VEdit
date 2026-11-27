#![cfg(unix)]

#[path = "fixtures/session_health_server.rs"]
mod server;

use std::os::unix::fs::DirBuilderExt;
use std::path::PathBuf;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use anyhow::{bail, Context, Result};
use vd_lsp::{
    LspBackend, LspConfig, PollOutcome, SessionHandle, SessionPhase, SessionRequest,
    SessionResponse, SessionStatus, MAX_RESTARTS,
};

const BUDGET: Duration = Duration::from_secs(5);

struct Project(PathBuf);

impl Project {
    fn new(name: &str) -> Result<Self> {
        let nonce = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
        let pid = std::process::id();
        let path = std::env::temp_dir().join(format!("vd-lsp-health-{name}-{pid}-{nonce}"));
        std::fs::DirBuilder::new().mode(0o700).create(&path)?;
        let project = Self(path);
        std::fs::write(project.file(), "module top; endmodule\n")?;
        Ok(project)
    }

    fn file(&self) -> PathBuf {
        self.0.join("top.sv")
    }

    fn start(&self, mode: &str) -> Result<SessionHandle> {
        // Keep libtest stdout off the LSP pipe. The shell execs the same test
        // binary in server mode, so the reported child PID is the wire peer's.
        let config = LspConfig::new("/bin/sh", &self.0)
            .with_backend(LspBackend::Slang)
            .with_extra_arg("-c")
            .with_extra_arg(concat!(
                "exec 3>&1 1>/dev/null; exec \"$0\" --exact ",
                "startup_failure_includes_child_stderr --nocapture"
            ))
            .with_extra_arg(std::env::current_exe()?)
            .with_environment([(server::MODE.to_string(), mode.to_string())].into())
            .with_timeouts(BUDGET, BUDGET, Duration::from_millis(500));
        SessionHandle::with_budgets(config, vec![self.file()], BUDGET, BUDGET)
    }

    fn events(&self) -> Result<String> {
        match std::fs::read_to_string(self.0.join(server::EVENTS)) {
            Ok(mut events) => {
                // A concurrent append may leave the last record incomplete.
                events.truncate(events.rfind('\n').map_or(0, |end| end + 1));
                Ok(events)
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(String::new()),
            Err(error) => Err(error.into()),
        }
    }

    fn pid(&self, launch: usize) -> Result<u32> {
        wait_for("fixture launch", || {
            self.events()?
                .lines()
                .filter_map(|line| line.strip_prefix("spawn "))
                .nth(launch)
                .map(str::parse)
                .transpose()
                .context("invalid fixture PID")
        })
    }

    fn crash(&self, session: &SessionHandle, pid: u32) -> Result<SessionStatus> {
        std::fs::write(self.0.join(format!("exit-{pid}")), "")?;
        let failed = wait_status(session, "idle exit", |status| {
            status.phase == SessionPhase::Failed && status.message.contains("exited after Ready")
        })?;
        assert!(failed.message.contains(server::IDLE_STDERR), "{failed:?}");
        Ok(failed)
    }

    fn change(&self, session: &SessionHandle) -> Result<()> {
        session.change_document(&self.file(), server::UNSAVED.to_string())
    }

    fn push_context(&self, session: &SessionHandle) -> Result<()> {
        session.set_build_context(vec![self.0.clone()], vec![server::DEFINE.to_string()])
    }
}

impl Drop for Project {
    fn drop(&mut self) {
        if let Err(error) = std::fs::remove_dir_all(&self.0) {
            eprintln!("fixture cleanup failed: {error}");
        }
    }
}

fn wait_for<T>(what: &str, mut poll: impl FnMut() -> Result<Option<T>>) -> Result<T> {
    let deadline = Instant::now() + BUDGET;
    loop {
        if let Some(value) = poll()? {
            return Ok(value);
        }
        if Instant::now() >= deadline {
            bail!("timed out waiting for {what}");
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

fn wait_status(
    session: &SessionHandle,
    what: &str,
    ready: impl Fn(&SessionStatus) -> bool,
) -> Result<SessionStatus> {
    wait_for(what, || {
        let status = session.status();
        Ok(ready(&status).then_some(status))
    })
    .with_context(|| format!("last status: {:?}", session.status()))
}

fn ready(session: &SessionHandle, restarts: u32) -> Result<SessionStatus> {
    wait_status(session, "Ready with the expected restart count", |status| {
        status.phase == SessionPhase::Ready && status.restarts == restarts
    })
}

fn wire_barrier(project: &Project, session: &SessionHandle) -> Result<()> {
    let id = session.request(SessionRequest::DocumentSymbols {
        file: project.file(),
    })?;
    wait_for("document symbols reply", || match session.poll(id) {
        PollOutcome::Pending => Ok(None),
        PollOutcome::Ready(SessionResponse::Symbols(nodes)) if nodes.is_empty() => Ok(Some(())),
        other => bail!("unexpected request outcome: {other:?}"),
    })
}

#[test]
fn idle_exit_and_recovery_publish_the_current_pid() -> Result<()> {
    let project = Project::new("pid")?;
    let session = project.start("normal")?;
    let first = project.pid(0)?;
    assert_eq!(ready(&session, 0)?.server_pid, Some(first));
    let failed = project.crash(&session, first)?;
    assert_eq!(failed.restarts, 0);
    project.change(&session)?;
    let recovered = ready(&session, 1)?;
    let replacement = project.pid(1)?;
    assert_ne!(first, replacement);
    println!(
        "pid: original={first} failed={:?} replacement={replacement} public={:?}",
        failed.server_pid, recovered.server_pid
    );
    println!("idle failure: {}", failed.message);
    assert_eq!(
        recovered.server_pid,
        Some(replacement),
        "Ready must publish the replacement PID"
    );
    assert_eq!(
        failed.server_pid, None,
        "an exited server has no running PID"
    );
    assert!(session.wait_for_stop());
    Ok(())
}

#[test]
fn idle_exit_stops_after_three_restarts() -> Result<()> {
    assert_eq!(MAX_RESTARTS, 3);
    let project = Project::new("limit")?;
    let session = project.start("normal")?;
    for restarts in 0..=MAX_RESTARTS {
        ready(&session, restarts)?;
        let failed = project.crash(&session, project.pid(usize::try_from(restarts)?)?)?;
        assert_eq!(failed.restarts, restarts);
        assert!(failed
            .message
            .contains(&format!("{restarts}/{MAX_RESTARTS}")));
        project.change(&session)?;
    }
    let final_status = wait_status(&session, "restart refusal", |status| {
        status.phase == SessionPhase::Failed && status.message.contains("refusing to restart")
    })?;
    let launches = project
        .events()?
        .lines()
        .filter(|line| line.starts_with("spawn "))
        .count();
    println!(
        "restart limit: launches={launches} public={} message={}",
        final_status.restarts, final_status.message
    );
    assert_eq!(launches, 4);
    assert_eq!(final_status.restarts, MAX_RESTARTS);
    assert!(final_status.message.contains("3/3 restart(s)"));
    assert!(session.wait_for_stop());
    Ok(())
}

#[test]
fn startup_failure_includes_child_stderr() -> Result<()> {
    if let Ok(mode) = std::env::var(server::MODE) {
        return server::run(&mode);
    }
    let project = Project::new("startup")?;
    let session = project.start("startup-failure")?;
    let failed = wait_status(&session, "startup failure", |status| {
        status.phase == SessionPhase::Failed
    })?;
    println!("startup failure: {}", failed.message);
    assert!(failed.message.contains("initialize failed"), "{failed:?}");
    assert!(
        failed.message.contains(server::STARTUP_STDERR),
        "{failed:?}"
    );
    assert_eq!(failed.server_pid, None);
    assert!(session.wait_for_stop());
    Ok(())
}

#[test]
fn restart_reopens_the_buffer_and_the_caller_reapplies_build_context_once() -> Result<()> {
    let project = Project::new("context")?;
    let session = project.start("normal")?;
    ready(&session, 0)?;
    let first = project.pid(0)?;
    project.push_context(&session)?;
    project.change(&session)?;
    wire_barrier(&project, &session)?;
    let events = project.events()?;
    assert!(
        events
            .lines()
            .any(|line| line == format!("change {first} unsaved")),
        "{events}"
    );
    project.crash(&session, first)?;
    project.change(&session)?;
    ready(&session, 1)?;
    let replacement = project.pid(1)?;
    wire_barrier(&project, &session)?;
    let before = project.events()?;
    assert!(
        before
            .lines()
            .any(|line| line == format!("open {replacement} unsaved")),
        "{before}"
    );
    let count = |events: &str, pid| {
        let event = format!("context {pid}");
        events.lines().filter(|line| *line == event).count()
    };
    assert_eq!(count(&before, first), 1, "{before}");
    assert_eq!(count(&before, replacement), 0, "{before}");
    project.push_context(&session)?;
    wire_barrier(&project, &session)?;
    assert_eq!(count(&project.events()?, replacement), 1);
    println!("context: initial=1 replacement_before_caller_push=0 replacement_after_caller_push=1; unsaved didOpen replayed");
    assert!(session.wait_for_stop());
    Ok(())
}
