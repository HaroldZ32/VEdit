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

#include <memory>

namespace vedit::workspace::diagnostics {

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

    /// Replace the tracked text of `file` with the EDITOR BUFFER (a
    /// full-document didChange). Nothing is read from disk, so diagnostics can
    /// follow unsaved edits. Returns false with `error` filled when this backend
    /// cannot follow a buffer (the caller reports it instead of pretending).
    virtual bool changeBuffer(const QString &file, const QString &text, QString *error);

    /// Poll one request id; `pending` means "poll again later".
    virtual LspPolled poll(quint64 requestId) = 0;
};

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
