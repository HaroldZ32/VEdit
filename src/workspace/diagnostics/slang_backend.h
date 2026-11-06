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

/// Which CLI lint engine a run uses (T138/T11). The numbers match
/// `vd_deeplint_lint_v2`'s `engine` argument.
enum class LintEngineKind {
    Slang = 0,        ///< `slang --lint-only`
    VeribleLint = 1,  ///< `verible-verilog-lint`
};

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

    // --- T11: engine selection and the project's -I/-D ---------------------
    /// Select the engine, its binaries, the project's include dirs/defines and
    /// the verible rules file for runs started AFTER this call.
    virtual void selectEngine(LintEngineKind engine,
                              const QString &slangPath,
                              const QString &veribleLintPath,
                              const QStringList &includeDirs,
                              const QStringList &defines,
                              const QString &rulesPath);
    virtual LintEngineKind engineKind() const { return LintEngineKind::Slang; }
    virtual QString engineName() const { return QStringLiteral("slang"); }
};

inline void SlangBackend::selectEngine(LintEngineKind engine,
                                       const QString &slangPath,
                                       const QString &veribleLintPath,
                                       const QStringList &includeDirs,
                                       const QStringList &defines,
                                       const QString &rulesPath)
{
    (void)engine;
    (void)slangPath;
    (void)veribleLintPath;
    (void)includeDirs;
    (void)defines;
    (void)rulesPath;
}

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
