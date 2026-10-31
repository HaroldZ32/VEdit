#pragma once

// Seam between the diagnostics controller and the SLANG deep-lint service.
//
// The production implementation wraps the Rust C ABI (crates/vd-ffi/src/
// deeplint.rs: `vd_deeplint_lint` / `vd_deeplint_poll`); the self-test injects a
// fake. The controller feeds this backend the **same** files it hands to the LSP
// session (A3c).

#include "diagnostics_types.h"

#include <QString>

#include <memory>

namespace vedit::workspace::diagnostics {

class SlangBackend {
public:
    virtual ~SlangBackend() = default;

    /// Whether a slang binary could be resolved at construction time. A false
    /// value makes the SLANG leg an explicit, diagnosable SKIP (never a pass).
    virtual bool available() const = 0;

    /// Human-readable backend identity for evidence ("slang (FFI)" / "fake").
    virtual QString description() const = 0;

    /// Start one lint run. Returns a ticket id, or 0 with `error` filled when
    /// the run could not even be queued (e.g. the binary is missing).
    virtual int startLint(const QString &file, QString *error) = 0;

    /// Poll one lint ticket; `pending` means "poll again later".
    virtual LspPolled pollLint(int ticket) = 0;
};

/// True when this build links the Rust service layer.
bool ffiSlangBackendAvailable();

/// The real backend, or nullptr when the Rust service layer is not linked.
///
/// `slangPath` is the path the toolchain resolver chose (bundled > PATH > env);
/// the backend passes it to the Rust service as an explicit override, so the
/// observable choice and the executed binary cannot diverge. An empty or
/// non-executable path makes the backend report itself unavailable, which the
/// controller surfaces as a diagnosable missing-tool state.
std::unique_ptr<SlangBackend> createFfiSlangBackend(const QString &slangPath = QString());

} // namespace vedit::workspace::diagnostics
