#pragma once

// Diagnostics controller: one effective file set, two backends (Verible LSP +
// SLANG), one observable reparse state.
//
//   * A3c: setFileSet() hands the **same** QStringList to the LSP session and
//     to the SLANG backend (one lint run per file); fileSetReport() proves it.
//   * A3d: setFileSet()/reparse() move the state to Reparsing and refresh()
//     advances it to Ready / Degraded / Failed; the snapshot carries the counts.
//   * The five navigation entry points (definition, references, symbols,
//     rename, format) are polled requests — nothing here blocks the UI thread.
//
// The controller owns its backends, so the self-test injects fakes without an
// FFI library or a language server.

#include "diagnostics_types.h"
#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace vedit::workspace::diagnostics {

class DiagnosticsController {
public:
    DiagnosticsController(std::unique_ptr<LspSessionBackend> lsp,
                          std::unique_ptr<SlangBackend> slang);
    ~DiagnosticsController();

    DiagnosticsController(const DiagnosticsController &) = delete;
    DiagnosticsController &operator=(const DiagnosticsController &) = delete;

    /// Configure where the language server starts (workspace root, optional
    /// `.rules.verible_lint`). Does not start anything.
    void setConfiguration(const QString &lsPath,
                          const QString &workspaceRoot,
                          const QString &rulesPath = QString());

    /// Record the resolved toolchain (observable source/missing).
    void setToolchainReport(const ToolchainReport &report);
    const ToolchainReport &toolchainReport() const { return toolchain_; }

    const QString &lsPath() const { return lsPath_; }
    const QString &workspaceRoot() const { return workspaceRoot_; }
    const QString &rulesPath() const { return rulesPath_; }

    /// Replace the effective file set and start a reparse (A3c/A3d).
    void setFileSet(const QStringList &files);
    const QStringList &fileSet() const { return files_; }

    /// Restart the reparse for the current set.
    void reparse();

    /// File-scope analysis that follows the EDITOR BUFFER (T118): start (or
    /// reuse) a session over exactly `file`, replace the tracked document text
    /// with `bufferText` (a didChange; nothing is read from disk) and collect
    /// diagnostics + the outline for that file. `bufferDirty` only labels the
    /// source in the UI/status. Returns false when no session could be started.
    bool analyzeFile(const QString &file, const QString &bufferText, bool bufferDirty);
    /// How many file-scope analyses ran (observable in the status line/tests).
    quint64 analysisCount() const { return analysisCount_; }
    QString analyzeFileName() const { return analysisFile_; }
    bool analyzeWasDirty() const { return analysisDirty_; }
    QDateTime lastAnalysisAt() const { return lastAnalysisAt_; }

    /// Advance the asynchronous state; cheap, non-blocking, idempotent.
    void refresh();

    /// Joint observable state (A3d).
    BackendState state() const { return state_; }
    /// LSP session snapshot (counts, pid, generation, skipped files).
    const SessionSnapshot &session() const { return session_; }
    /// A3c report: which files reached which backend.
    const FileSetReport &fileSetReport() const { return report_; }
    /// Diagnostics collected from both backends.
    const QVector<DiagnosticEntry> &diagnostics() const { return diagnostics_; }
    /// Outline entries from the last successful `requestSymbols`.
    const QVector<OutlineSymbol> &outline() const { return outline_; }
    /// Last diagnosable error (degradation path), empty when healthy.
    const QString &lastError() const { return lastError_; }
    /// Reparse counter; changes on every setFileSet()/reparse().
    quint64 generation() const { return generation_; }

    quint64 requestDefinition(const QString &file, int line, int character);
    quint64 requestReferences(const QString &file, int line, int character);
    quint64 requestSymbols(const QString &file);
    quint64 requestRename(const QString &file,
                          int line,
                          int character,
                          const QString &newName);
    quint64 requestFormat(const QString &file);

    /// Poll the most recent navigation request. A ready `Symbols` response also
    /// updates outline().
    LspPolled pollNavigation();

private:
    struct LintTicket {
        int id = 0;
        QString file;
    };
    struct DiagnosticTicket {
        quint64 id = 0;
        QString file;
    };

    void startLsp();
    void startLints();
    void collectLints();
    void collectLspDiagnostics();
    void finaliseState();

    std::unique_ptr<LspSessionBackend> lsp_;
    std::unique_ptr<SlangBackend> slang_;

    QString lsPath_;
    QString workspaceRoot_;
    QString rulesPath_;
    QStringList files_;

    BackendState state_ = BackendState::Idle;
    SessionSnapshot session_;
    FileSetReport report_;
    QVector<DiagnosticEntry> diagnostics_;
    QVector<OutlineSymbol> outline_;
    QVector<LintTicket> lints_;
    QVector<DiagnosticTicket> lspDiagnostics_;

    // File-scope analysis state (T118): one file, the buffer text it was asked
    // to analyze, and the bookkeeping that lets refresh() apply the buffer
    // before it collects the diagnostics.
    QString analysisFile_;
    QString analysisText_;
    bool analysisDirty_ = false;
    bool bufferApplied_ = false;
    qint64 bufferAppliedMs_ = 0;
    quint64 bufferGenerationAtApply_ = 0;
    bool bufferDiagnosticsSeen_ = false;
    bool symbolsRequested_ = false;
    quint64 analysisCount_ = 0;
    QDateTime lastAnalysisAt_;

    ToolchainReport toolchain_;
    QString lastError_;
    quint64 generation_ = 0;
    quint64 navigation_ = 0;
    bool lspStarted_ = false;
    bool lspDiagnosticsRequested_ = false;
};

} // namespace vedit::workspace::diagnostics
