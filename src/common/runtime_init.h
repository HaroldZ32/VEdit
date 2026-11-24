#pragma once

// The ONE process-level runtime entry point shared by every C++ program in this
// product that links the Rust service layer statically (AGENT.md §T134).
//
// Rust is STATICALLY LINKED into this C++ host (`crates/vd-ffi` -> vd-lsp,
// vd-config, ...). Rust's runtime startup — the part of a Rust binary that
// installs process-wide defaults — does NOT run in such a host. The known
// consequence is SIGPIPE: a Rust binary ignores it at startup, while here it
// stays at its default (terminate), so a write to a child that already died
// (the verible LSP pipe while switching files, a rename job's child, ...) kills
// the process with "Broken pipe" before any error path runs. Ignoring the
// signal makes the write fail with EPIPE instead, which the Rust side records
// as a state.
//
// Every entry point that links Rust — the product (`src/app/main.cpp`), the
// ctest binaries, one-off probes — must call this function as the FIRST
// statement of main(): before Qt starts, before the first FFI call and before
// any child process exists. Header-only and Qt-free on purpose, so a workspace
// lane can use it in a standalone build (`cmake -S src/workspace/<lane>`)
// without linking src/app and without a second source of truth.
//
// Do not add SA_RESETHAND: the ignore must stay for the whole process.

#include <csignal>

/// Install the process-wide defaults a Rust binary would have installed at its
/// own runtime startup. Idempotent; call it once, first thing in main().
inline void vedit_runtime_init()
{
    std::signal(SIGPIPE, SIG_IGN);
    // Future Rust process-level defaults that a statically linked host does not
    // inherit (e.g. stack-overflow handling) go HERE, so every program that
    // calls this one entry point inherits them without touching N mains.
}
