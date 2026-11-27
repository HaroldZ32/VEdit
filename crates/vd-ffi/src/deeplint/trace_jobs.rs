//! Trace construction and retirement share one worker. The editor may poll
//! published indexes while the worker runs, but only the worker disposes of them.

use std::collections::BTreeMap;
use std::io;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};

use vd_deeplint::{elaborate_navigation, HierarchyOptions, LintFailure, TraceIndex};

// Bound all accepted slots together: queued work, active work, cached indexes
// and indexes waiting for retirement. Never enqueue a second disposal object.
const MAX_JOBS: usize = 4;

static RUNNER: OnceLock<io::Result<Arc<TraceRunner>>> = OnceLock::new();

type Jobs = BTreeMap<u64, Entry>;

pub(super) struct TraceRunner {
    jobs: Mutex<Jobs>,
    changed: Condvar,
}

pub(super) struct Request {
    pub(super) files: Vec<PathBuf>,
    pub(super) options: HierarchyOptions,
    pub(super) root: PathBuf,
}

pub(super) struct Slot {
    pub(super) result: Mutex<Option<Outcome>>,
    pub(super) cancel: AtomicBool,
}

pub(super) enum Outcome {
    Ready(TraceIndex, Option<Vec<PathBuf>>),
    Failed(LintFailure),
}

struct Entry {
    slot: Arc<Slot>,
    pending: Option<Request>,
}

enum Work {
    Build(Arc<Slot>, Request),
    Retire(u64, Arc<Slot>),
}

pub(super) fn get_or_start() -> io::Result<&'static TraceRunner> {
    RUNNER
        .get_or_init(|| {
            let runner = Arc::new(TraceRunner {
                jobs: Mutex::new(Jobs::new()),
                changed: Condvar::new(),
            });
            let worker = Arc::clone(&runner);
            std::thread::Builder::new()
                .name("vd-trace".to_owned())
                .spawn(move || worker.run())?;
            Ok(runner)
        })
        .as_deref()
        .map_err(|error| io::Error::new(error.kind(), error.to_string()))
}

pub(super) fn existing() -> Option<&'static TraceRunner> {
    RUNNER.get()?.as_deref().ok()
}

impl TraceRunner {
    pub(super) fn enqueue(&self, id: u64, request: Request) -> bool {
        let mut jobs = self.jobs.lock().unwrap_or_else(|error| error.into_inner());
        if jobs.len() >= MAX_JOBS {
            return false;
        }
        jobs.insert(
            id,
            Entry {
                slot: Arc::new(Slot {
                    result: Mutex::new(None),
                    cancel: AtomicBool::new(false),
                }),
                pending: Some(request),
            },
        );
        self.changed.notify_one();
        true
    }

    pub(super) fn find(&self, id: u64) -> Option<Arc<Slot>> {
        let jobs = self.jobs.lock().unwrap_or_else(|error| error.into_inner());
        let entry = jobs.get(&id)?;
        (!entry.slot.cancel.load(Ordering::SeqCst)).then(|| Arc::clone(&entry.slot))
    }

    pub(super) fn release(&self, id: u64) -> bool {
        let removed = {
            let mut jobs = self.jobs.lock().unwrap_or_else(|error| error.into_inner());
            let Some(entry) = jobs.get(&id) else {
                return false;
            };
            if entry.slot.cancel.swap(true, Ordering::SeqCst) {
                return false;
            }
            if entry.pending.is_some() {
                // No worker has started it, so there is no index to dispose of.
                jobs.remove(&id)
            } else {
                self.changed.notify_one();
                None
            }
        };
        drop(removed);
        true
    }

    fn run(&self) {
        loop {
            match self.next() {
                Work::Build(slot, request) => build(&slot, request),
                Work::Retire(id, slot) => self.retire(id, &slot),
            }
        }
    }

    fn retire(&self, id: u64, slot: &Slot) {
        // Take ownership even if a poll still holds another Arc to the slot.
        // Keep the slot charged until the index has actually been destroyed.
        let result = slot
            .result
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .take();
        drop(result);
        self.jobs
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .remove(&id);
    }

    fn next(&self) -> Work {
        let mut jobs = self.jobs.lock().unwrap_or_else(|error| error.into_inner());
        loop {
            if let Some((&id, entry)) = jobs
                .iter()
                .find(|(_, entry)| entry.slot.cancel.load(Ordering::SeqCst))
            {
                return Work::Retire(id, Arc::clone(&entry.slot));
            }
            if let Some(work) = jobs
                .values_mut()
                .find_map(|entry| Some(Work::Build(Arc::clone(&entry.slot), entry.pending.take()?)))
            {
                return work;
            }
            jobs = self
                .changed
                .wait(jobs)
                .unwrap_or_else(|error| error.into_inner());
        }
    }
}

fn build(slot: &Slot, request: Request) {
    let outcome = std::panic::catch_unwind(|| {
        let (mut report, dependencies) = elaborate_navigation(
            &request.files,
            &request.options,
            &request.root,
            &slot.cancel,
        );
        if let Some(failure) = report.failure.take() {
            return Outcome::Failed(failure);
        }
        match TraceIndex::from_report_cancellable(report, &slot.cancel) {
            Ok(index) => Outcome::Ready(index, Some(dependencies)),
            Err(failure) => Outcome::Failed(failure),
        }
    })
    .unwrap_or_else(|_| {
        Outcome::Failed(LintFailure::Io {
            message: "the signal trace worker panicked; its result was discarded".to_owned(),
        })
    });
    let mut result = slot
        .result
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    if !slot.cancel.load(Ordering::SeqCst) {
        *result = Some(outcome);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request() -> Request {
        Request {
            files: Vec::new(),
            options: HierarchyOptions::default(),
            root: PathBuf::from("/"),
        }
    }

    #[test]
    fn stale_work_is_retired_before_the_latest_request_and_never_fills_the_queue() {
        let runner = TraceRunner {
            jobs: Mutex::new(Jobs::new()),
            changed: Condvar::new(),
        };
        assert!(runner.enqueue(1, request()));
        let Work::Build(active, _) = runner.next() else {
            panic!("first request did not start")
        };
        for id in 2..100 {
            assert!(runner.enqueue(id, request()));
            assert!(runner.release(id));
            assert!(runner.find(id).is_none());
        }
        assert!(runner.enqueue(100, request()));
        assert!(runner.enqueue(101, request()));
        assert!(runner.enqueue(102, request()));
        assert!(!runner.enqueue(103, request()));

        // Model concurrent polling: release must never wait for the result lock.
        let mut reader = active
            .result
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ast = include_str!("../../../vd-deeplint/tests/fixtures/hierarchy/ast.json");
        let report = vd_deeplint::parse_ast_json(ast).unwrap_or_else(|error| panic!("{error}"));
        *reader = Some(Outcome::Ready(TraceIndex::from_report(report), None));
        std::thread::scope(|scope| {
            let (sent, received) = std::sync::mpsc::channel();
            let runner = &runner;
            scope.spawn(move || sent.send(runner.release(1)));
            let released = received.recv_timeout(std::time::Duration::from_secs(2));
            let still_cached = reader.is_some();
            drop(reader);
            assert_eq!(
                released,
                Ok(true),
                "release must not wait for the poll lock"
            );
            assert!(still_cached);
        });
        assert!(runner.find(1).is_none());
        assert!(!runner.release(1));

        let Work::Retire(1, stale) = runner.next() else {
            panic!("stale result was not retired first")
        };
        assert!(
            !runner.enqueue(103, request()),
            "retirement remains charged until disposal"
        );
        runner.retire(1, &stale);
        assert!(active
            .result
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .is_none());
        assert!(runner.enqueue(103, request()));
        let Work::Build(current, _) = runner.next() else {
            panic!("replacement did not start")
        };
        assert!(runner
            .find(100)
            .is_some_and(|slot| Arc::ptr_eq(&current, &slot)));
    }
}
