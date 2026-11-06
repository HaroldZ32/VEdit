#pragma once

// Seam between the diagnostics controller and the Verible LSP service.
//
// The production implementation wraps the Rust C ABI (crates/vd-ffi/src/lsp.rs:
// `vd_lsp_session_*`); the headless self-test injects a fake so the A3c/A3d
// assertions (same effective file set, observable reparse state) run without a
// display and without starting a language server.

#include "diagnostics_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace vedit::workspace::diagnostics {

/// Which language server a session drives (T138/T11). The C ABI numbers must
/// match `vd_lsp_session_start_v2`'s `backend` argument.
enum class LspBackendKind {
    Verible = 0, ///< `verible-verilog-ls`
    Slang = 1,   ///< `slang-server`
};

/// One row of a backend capability table (`vd_lsp_backend_capabilities`).
struct CapabilityRow {
    QString action;    ///< `definition`, `hover`, `documentSymbol`, ...
    int supported = -1; ///< 1 supported, 0 unsupported, -1 unknown
    QString reason;    ///< why; never empty for an unsupported row
};

/// A backend's capability table plus how it was established.
struct BackendCapabilities {
    QString backend; ///< `verible` / `slang`
    QString basis;   ///< `measured` / `live` / `declared`
    QString source;  ///< binary version / live handshake
    QString note;    ///< the asymmetry sentence from the ABI
    QVector<CapabilityRow> rows;

    /// The row for `action`, or nullptr.
    const CapabilityRow *row(const QString &action) const;
    /// 1/0/-1; -1 also when the table has no such row (never claim support).
    int supportedFor(const QString &action) const;
    /// Why the action is (un)available; empty when the row says supported.
    QString reasonFor(const QString &action) const;
    bool isEmpty() const { return rows.isEmpty(); }
};

class LspSessionBackend {
public:
    virtual ~LspSessionBackend() = default;

    /// Start a session over exactly `files` (the effective file set). Returns
    /// false and fills `error` when the session cannot be created; the caller
    /// then reports a diagnosable degraded state, never a crash.
    virtual bool start(const QString &lsPath,
                       const QString &workspaceRoot,
                       const QString &rulesPath,
                       const QStringList &files,
                       QString *error) = 0;

    /// Request a cooperative shutdown (idempotent, non-blocking).
    virtual void stop() = 0;

    /// Observable session state (A3d); must be cheap and never block on I/O.
    virtual SessionSnapshot status() const = 0;

    virtual quint64 requestDefinition(const QString &file, int line, int character) = 0;
    virtual quint64 requestReferences(const QString &file, int line, int character) = 0;
    virtual quint64 requestSymbols(const QString &file) = 0;
    virtual quint64 requestRename(const QString &file,
                                  int line,
                                  int character,
                                  const QString &newName) = 0;
    virtual quint64 requestFormat(const QString &file) = 0;
    /// Diagnostics currently recorded for one file (`VD_LSP_REQ_DIAGNOSTICS`).
    virtual quint64 requestDiagnostics(const QString &file) = 0;
    /// `textDocument/hover` at a zero-based position (T16 / AC-3.5).
    virtual quint64 requestHover(const QString &file, int line, int character);
    /// `textDocument/signatureHelp` at a zero-based position (T16).
    virtual quint64 requestSignatureHelp(const QString &file, int line, int character);

    /// Replace the tracked text of `file` with the EDITOR BUFFER (a
    /// full-document didChange). Nothing is read from disk, so diagnostics can
    /// follow unsaved edits. Returns false with `error` filled when this backend
    /// cannot follow a buffer (the caller reports it instead of pretending).
    virtual bool changeBuffer(const QString &file, const QString &text, QString *error);

    /// Poll one request id; `pending` means "poll again later".
    virtual LspPolled poll(quint64 requestId) = 0;

    // --- T11: backend selection, capability table and argv echo ------------
    /// Select the server the NEXT start() drives, with the project's `-I`/`-D`
    /// and an explicit server path. Implementations stop any running session
    /// first; a fake ignores it. Default: only Verible, no include dirs/defines.
    virtual bool selectBackend(LspBackendKind kind,
                               const QString &serverPath,
                               const QStringList &includeDirs,
                               const QStringList &defines,
                               QString *error);
    virtual LspBackendKind backendKind() const { return LspBackendKind::Verible; }
    virtual QString backendName() const { return QStringLiteral("verible"); }
    /// The static capability table of the selected backend (no session needed).
    virtual BackendCapabilities staticCapabilities() const { return BackendCapabilities{}; }
    /// The live table after the handshake; equals the static table before it.
    virtual BackendCapabilities sessionCapabilities() const;
    /// The exact argv the next start() would use (AC-2.5's dump).
    virtual QString commandLine() const { return QString(); }
};

inline const CapabilityRow *BackendCapabilities::row(const QString &action) const
{
    for (const CapabilityRow &candidate : rows) {
        if (candidate.action == action) {
            return &candidate;
        }
    }
    return nullptr;
}

inline int BackendCapabilities::supportedFor(const QString &action) const
{
    const CapabilityRow *candidate = row(action);
    return candidate != nullptr ? candidate->supported : -1;
}

inline QString BackendCapabilities::reasonFor(const QString &action) const
{
    const CapabilityRow *candidate = row(action);
    return candidate != nullptr ? candidate->reason : QString();
}

inline bool LspSessionBackend::selectBackend(LspBackendKind kind,
                                             const QString &serverPath,
                                             const QStringList &includeDirs,
                                             const QStringList &defines,
                                             QString *error)
{
    (void)serverPath;
    (void)includeDirs;
    (void)defines;
    if (kind == LspBackendKind::Verible) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("this LSP backend cannot drive another server");
    }
    return false;
}

inline BackendCapabilities LspSessionBackend::sessionCapabilities() const
{
    return staticCapabilities();
}

inline quint64 LspSessionBackend::requestHover(const QString &file, int line, int character)
{
    (void)file;
    (void)line;
    (void)character;
    return 0;
}

inline quint64 LspSessionBackend::requestSignatureHelp(const QString &file, int line, int character)
{
    (void)file;
    (void)line;
    (void)character;
    return 0;
}

inline bool LspSessionBackend::changeBuffer(const QString &file, const QString &text,
                                            QString *error)
{
    (void)file;
    (void)text;
    if (error != nullptr) {
        *error = QStringLiteral("this LSP backend cannot follow editor buffers");
    }
    return false;
}

/// True when this build links the Rust service layer (the real backend exists).
bool ffiLspSessionBackendAvailable();

/// The real backend, or nullptr when the Rust service layer is not linked.
std::unique_ptr<LspSessionBackend> createFfiLspSessionBackend();

} // namespace vedit::workspace::diagnostics
