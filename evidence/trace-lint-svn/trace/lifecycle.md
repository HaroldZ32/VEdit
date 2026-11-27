# Large trace cancellation and disposal

Baseline `0c24557` already streamed the compiler output and cached the trace graph. The FFI then spawned a separate worker for every start. Cancellation stopped elaboration but was unchecked during the post-elaboration source and UTF-16 index pass. Releasing a ready slot could destroy the entire cached index on the Qt thread when it held the last reference.

One trace worker now handles build and retirement. At most four trace slots are accepted per process, including pending, running, cached and retiring slots. The worker retires cancelled work before starting a replacement. Release makes the id unavailable immediately without taking the result mutex. The worker takes ownership of any cached result before destroying it, including when a concurrent poll still holds a reference to the slot. A released request that never started leaves the queue immediately. A fifth simultaneous live slot receives an explicit busy error. The ordinary hierarchy registry and one-shot polling retain their behavior.

The trace-specific column pass checks cancellation at each signal, each indexed position and between 64 KiB read chunks. Its loader skips non-regular source paths. UTF-8 and CRLF conversion share the existing column rules, and a new unit test crosses a chunk boundary inside a supplementary character. Like the rest of the compiler pipeline, this uses cooperative cancellation. A single blocking filesystem call can still finish before the worker observes a cancellation. The existing column cache can retain one source above its normal 32 MiB eviction threshold to preserve exact UTF-16 coordinates. The worker performs all source reads and cached-graph destruction.

## Rust checks

Run from the repository root with the pinned compiler in `VEDIT_SLANG` and a private Cargo target directory.

```sh
cargo +stable clippy --locked --offline -p vd-deeplint --all-targets -- -D warnings
cargo +stable clippy --locked --offline -p vd-ffi --lib -- -D warnings
cargo +stable test --locked --offline -p vd-deeplint --lib
cargo +stable test --locked --offline -p vd-ffi --lib deeplint::
```

The Rust suites passed 124 `vd-deeplint` unit tests and 10 FFI deep-lint and hierarchy tests. The worker test repeatedly queues and releases 98 stale snapshots, refuses a fifth live trace, verifies that release succeeds even while the result lock is held by a poll, and checks that a retired slot keeps its capacity charge until the worker takes and drops the result. [Rust log](trace-lifecycle-rust.log)

## Native test

The registered `vedit-signal-trace-oversize-selftest` extends the real slang test to cover an external file edit that invalidates a cached index, cancellation of a counted pending compiler PID, immediate restart, and closing with a newly cached large index. The wrapper uses `exec sleep 30` only to hold the cancelled job at a deterministic point. The first and replacement ASTs come from the pinned slang compiler. The wrapper checks the old PID has already exited when the replacement starts.

```sh
ctest --test-dir target/qt6-build -R '^vedit-signal-trace(-oversize)?-selftest$' -V
```

The independent reviewer ran both tests in the pinned Qt build. The regular test passed in 0.99 s and the oversize test passed in 4.33 s with a real 138,188,283-byte AST. Cached invalidation took 12 ms including filesystem notification delivery. The controlled pending compiler exited before the replacement started. The replacement returned to the exact driver position in 1,338 ms. The window destructor returned in 15 ms with the replacement index live. The initial cold action took 2,619 ms, and the 10 ms GUI timer fired 231 times with a longest observed interval of 115 ms. These are single-run observations and include work by other UI components. The tested binary SHA-256 is `aafd2fec060e51bfe6c65f550e75ba906efaac56ccc3d3cf0203e3dd78b32202`. [Native log](trace-lifecycle-qt.log)
