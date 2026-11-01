#pragma once

// Crash self-reporting for the product (task T117).
//
// The real-machine verification reported one SIGSEGV ("段错误(吐核)") and left
// NOTHING behind: no core (the VM does not produce one) and no log. This module
// makes the NEXT crash collectable: a signal handler writes a self-contained
// report to
//
//   $XDG_STATE_HOME/vedit/crash-<pid>-<unix-time>.log
//   (~/.local/state/vedit/... when XDG_STATE_HOME is unset)
//
// with the signal name/number, si_code, fault address, a symbolized backtrace,
// the file that was open, the last internal events (a ring buffer filled by
// `event()`), and the build identity (HEAD, build time, AppImage path + sha256).
//
// The handler is async-signal-safe on purpose: no malloc, no Qt, only `open`,
// `write`, `time`, `backtrace_symbols_fd` and friends on a pre-opened code path.
// Normal execution is unaffected apart from the ring-buffer bookkeeping; after a
// report is written the default action is restored and the signal re-raised so
// the process still dies (and still cores when the system is configured to).
#include <QString>

namespace vedit::app::crash {

/// Install the handlers (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT), enable core
/// dumps for this process when the kernel allows it, and capture the build
/// identity. `buildInfo` is a short one-line stamp (HEAD + build time); the
/// AppImage path/sha256 is discovered here too. Safe to call once, early.
void install(const QString &buildInfo);

/// Record an internal event for the ring buffer (normal-time formatting).
/// Keep the message short (it is truncated to one slot).
void event(const char *format, ...);

/// Remember which file the editor has open (path + size), shown in the report.
void setCurrentFile(const QString &path);
void clearCurrentFile();

/// The directory the reports are written to (created by install()); empty when
/// install() has not run.
QString reportDirectory();

} // namespace vedit::app::crash
