//! The child-process discipline shared by the one-file and whole-project runs.
//!
//! Both runners spawn one tool process, must never block forever on the pipes a
//! grandchild may inherit, must always kill and reap the child on timeout or
//! cancellation, and must name the Unix signal when the tool crashes instead of
//! exiting. Keeping that in ONE place is what stops the two runners from
//! drifting: the whole-project pass added in batch 20261110d reuses the exact
//! capture and reaping rules the per-file runner already proved.

use std::io::Read;
use std::process::Child;
use std::sync::mpsc::{self, Receiver};
use std::time::Duration;

/// Cap for captured stdout/stderr (each); longer streams are truncated.
const STREAM_CAPTURE_LIMIT: usize = 64 * 1024;

/// How long to wait for the output readers after the child was reaped. A
/// grandchild that inherited the pipe could otherwise block us for as long as it
/// lives; the run is already over, so the output is best-effort past this point.
const READER_GRACE: Duration = Duration::from_millis(250);

/// Read a child stream on its own thread, bounded by [`STREAM_CAPTURE_LIMIT`].
///
/// The flag is `true` when the stream was longer than the cap (or unreadable):
/// the caller appends its own truncation marker, so a bounded capture is never
/// silent.
pub(crate) fn spawn_reader(reader: impl Read + Send + 'static) -> Receiver<(String, bool)> {
    let (sender, receiver) = mpsc::channel();
    std::thread::spawn(move || {
        let result = read_capped(reader, STREAM_CAPTURE_LIMIT);
        if sender.send(result).is_err() {
            eprintln!("vd-deeplint: output reader lost its receiver");
        }
    });
    receiver
}

/// Receive one reader's capture, giving up after [`READER_GRACE`].
pub(crate) fn receive_capped(reader: &Receiver<(String, bool)>) -> (String, bool) {
    match reader.recv_timeout(READER_GRACE) {
        Ok(result) => result,
        Err(_) => (String::new(), true),
    }
}

pub(crate) fn read_capped(mut reader: impl Read, limit: usize) -> (String, bool) {
    let mut buffer = Vec::new();
    let mut truncated = false;
    let mut chunk = [0u8; 8192];
    loop {
        match reader.read(&mut chunk) {
            Ok(0) => break,
            Ok(read) => {
                let remaining = limit.saturating_sub(buffer.len());
                if remaining > 0 {
                    buffer.extend_from_slice(&chunk[..read.min(remaining)]);
                }
                if read > remaining {
                    truncated = true;
                }
            }
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => {
                truncated = true;
                break;
            }
        }
    }
    (String::from_utf8_lossy(&buffer).into_owned(), truncated)
}

/// Kill and reap the child; only a real failure is returned.
pub(crate) fn terminate(child: &mut Child) -> Result<(), String> {
    if let Err(error) = child.kill() {
        if error.kind() != std::io::ErrorKind::NotFound {
            return Err(format!("killing slang failed: {error}"));
        }
    }
    child
        .wait()
        .map(|_| ())
        .map_err(|error| format!("reaping slang failed: {error}"))
}

/// The Unix signal that terminated `status`, or `None` when the child exited.
/// `ExitStatus::code()` alone cannot tell the two apart.
#[cfg(unix)]
pub(crate) fn termination_signal(status: &std::process::ExitStatus) -> Option<i32> {
    use std::os::unix::process::ExitStatusExt;
    status.signal()
}

#[cfg(not(unix))]
pub(crate) fn termination_signal(_status: &std::process::ExitStatus) -> Option<i32> {
    None
}
