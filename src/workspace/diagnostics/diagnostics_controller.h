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
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace vedit::workspace::diagnostics {

/// Seam between the controller and the project-wide CLI lint pass (T8). The
/// production implementation wraps `vd_deeplint_batch_*` (crates/vd-ffi); the
/// evidence harness injects a fake so cancellation, failure reporting and the
/// 1k-file interactivity can be proven without a toolchain.
class ProjectLintBackend {
public:
    virtual ~ProjectLintBackend() = default;

    /// False when the engine cannot run at all (missing binary). The pass then
    /// fails with the tool name and stage instead of pretending to be clean.
    virtual bool available() const = 0;
    virtual QString toolName() const = 0;
    /// Start the pass over exactly `request.files`. Returns a job id, or 0 with
    /// `error` filled.
    virtual quint64 start(const LintPassRequest &request, QString *error) = 0;
    /// Cheap, non-blocking progress snapshot.
    virtual LintPassSnapshot progress(quint64 job) const = 0;
    /// Poll the pass: pending while it runs, ready with the CLI diagnostics once
    /// terminal (cancelled or finished), failed with `error` = tool + stage.
    virtual LspPolled poll(quint64 job) = 0;
    /// Cooperative cancel: stop after the current file, kill the child, start no
    /// later file (AC-2.8).
    virtual void cancel(quint64 job) = 0;
    /// Release the job and its bookkeeping (safe after terminal).
    virtual void release(quint64 job) = 0;
};

/// The real batch backend, or nullptr when this build links no Rust service.
bool ffiProjectLintBackendAvailable();
std::unique_ptr<ProjectLintBackend> createFfiProjectLintBackend();

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

    /// Advance the asynchronous state; cheap, non-blocking, idempotent. This
    /// also polls the project lint pass (T8) - the GUI only ever calls this.
    void refresh();

    // --- T8: project-wide lint pass (AC-2.6..2.14) -------------------------
    /// Install the pass backend (nullptr disables the pass). Ownership moves.
    void setProjectLintBackend(std::unique_ptr<ProjectLintBackend> backend);
    /// Files that currently hold an LSP session (C8). The pass SNAPSHOTS this
    /// list when it starts and skips those files; later changes do not affect
    /// the running pass (their diagnostics still come from the LSP session).
    void setOpenFiles(const QStringList &files);
    const QStringList &openFiles() const { return openFiles_; }
    /// Start the pass: `request.files` minus the open-file snapshot. Refuses
    /// when a pass is already running (cancel/rerun first).
    bool startProjectPass(const LintPassRequest &request, QString *error = nullptr);
    /// Start again with the last request (AC-2.8 rerun / A-15). The request is
    /// recorded before the tool is resolved, so a first run that failed with
    /// "tool could not be resolved" can still be re-run once the user installs
    /// it. False only when no request has ever been recorded.
    bool rerunProjectPass(QString *error = nullptr);
    /// True when the last request is still available to re-run. The strip uses
    /// it so it never offers a button that would do nothing (A-15).
    bool rerunAvailable() const { return !passRequest_.files.isEmpty(); }
    /// Cancel the running pass (idempotent). Existing diagnostics are NOT
    /// cleared; the terminal snapshot says `cancelled — N/M file(s)`
    /// (the zh catalogue renders `已取消 — N/M 文件`).
    void cancelProjectPass();
    /// Open Folder: drop every project-pass verdict and the recorded request so
    /// nothing from the previous project can be shown or re-run. The lower-layer
    /// rows are cleared by setFileSet() with the new project's file set.
    void clearProjectDiagnostics();
    bool projectPassRunning() const { return passRunning_; }
    LintPassSnapshot projectPass() const { return pass_; }
    /// Diagnostics collected by the CLI pass (AC-2.13 CLI side).
    const QVector<DiagnosticEntry> &cliDiagnostics() const { return cliDiagnostics_; }
    /// LSP session rows merged with the CLI pass rows (origin Lsp/Cli/Both).
    QVector<DisplayDiagnostic> displayDiagnostics() const;

    // --- R2 (ux-batch-2): ONE layered display view ------------------------
    // The project pass is the UPPER layer, the per-file/LSP result the LOWER
    // one. Every surface (editor squiggles/gutter, panel, hover, counts, the
    // file-tree dots) reads the view below, so no two surfaces can disagree and
    // the same file is never stacked or counted twice.
    /// True when `file` still carries a LIVE upper-layer (project pass) verdict.
    bool hasUpperLayer(const QString &file) const;
    /// Files whose upper layer is live (covered by the last finished pass and
    /// not edited since).
    QStringList upperLayerFiles() const;
    /// Mark `file` as edited after the pass: its upper rows are dropped and the
    /// lower layer applies again until the next project pass covers it.
    void markFileEdited(const QString &file);
    /// The live upper-layer rows only, in the DISPLAY severity scale (1 error,
    /// 2 warning, ...) - the ELAB list and the counts read this.
    QVector<DiagnosticEntry> upperLayerDiagnostics() const;
    /// The merged view: per file the live upper layer when there is one, the
    /// lower layer otherwise; one row per distinct finding.
    QVector<DiagnosticEntry> mergedDiagnostics() const;
    /// The merged view restricted to one file (editor/gutter/hover path).
    QVector<DiagnosticEntry> mergedDiagnosticsForFile(const QString &file) const;

    /// File-tree status dot, taken from the merged view.
    enum class FileStatus {
        Unknown = 0, ///< no layer ever reported this file: draw no dot
        Error = 1,   ///< at least one error-severity row (red)
        Warning = 2, ///< warnings only (yellow)
        Clean = 3,   ///< covered and clean (green)
    };
    FileStatus statusForFile(const QString &file) const;
    /// Every file with a known status, for the file tree's cached map.
    QHash<QString, int> fileStatuses() const;
    /// Bumped whenever the merged view changes: a repaint cache key, never a
    /// reason to rescan the project.
    quint64 viewRevision() const { return viewRevision_; }
    /// Bumped once per pass that reached a terminal phase; the shell reacts to
    /// a CHANGE of this value exactly once (the ELAB notice/dialog).
    quint64 projectPassSerial() const { return projectPassSerial_; }
    /// True when the last terminal pass was started by the automatic (save)
    /// trigger: that pass never opens a modal dialog (R3).
    bool lastPassWasAutomatic() const { return lastPassAutomatic_; }
    /// The next pass is the automatic (save-triggered) one. Consumed by
    /// startProjectPass() and recorded when the pass reaches a terminal phase.
    void setNextPassAutomatic(bool automatic) { nextPassAutomatic_ = automatic; }
    /// Rows of the upper layer as stored (before layering), for diagnosis.
    const QVector<DiagnosticEntry> &upperDiagnostics() const { return cliDiagnostics_; }
    /// True when the lower layer actually reported for `file` (even with zero
    /// rows): the green dot must not be limited to project-pass coverage.
    bool lowerLayerAnalyzed(const QString &file) const { return lowerAnalyzed_.contains(file); }
    /// The last request (rerun source) and the C8 snapshot it skipped.
    const LintPassRequest &lastProjectPassRequest() const { return passRequest_; }
    const QStringList &projectPassSkipped() const { return passSkipped_; }
    /// Files in the pass (request files minus the snapshot), for the status line.
    int projectPassFileCount() const { return passFiles_; }

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
    /// `textDocument/hover` at a zero-based position (T16 / AC-3.5). Poll it with
    /// `pollNavigation()`; a ready answer has `kind == Hover` and `hoverText`.
    quint64 requestHover(const QString &file, int line, int character);
    /// `textDocument/signatureHelp` at a zero-based position (T16).
    quint64 requestSignatureHelp(const QString &file, int line, int character);

    /// Poll the most recent navigation request. A ready `Symbols` response also
    /// updates outline().
    LspPolled pollNavigation();

private:
    void pollProjectPass();
    void finaliseProjectPass(bool cancelled);
    /// R2: drop the upper rows of `files` and install `entries` as their new
    /// upper layer; records which files keep a live verdict afterwards.
    void replaceUpperLayer(const QStringList &files, const QVector<DiagnosticEntry> &entries);
    void dropUpperRows(const QString &file);
    void notePassTerminal();
    void invalidateView() { ++viewRevision_; }

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
    /// ONE user-visible count: the session message is composed from the merged
    /// view, never from the lower-layer `diagnostics_` vector (R2).
    void composeSessionMessage();

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

    // T8 project pass state.
    std::unique_ptr<ProjectLintBackend> passBackend_;
    LintPassRequest passRequest_;
    QStringList openFiles_;
    QStringList passSkipped_;
    QVector<DiagnosticEntry> cliDiagnostics_;
    LintPassSnapshot pass_;
    quint64 passJob_ = 0;
    int passFiles_ = 0;
    bool passRunning_ = false;

    // R2 layering + R3 observability.
    QSet<QString> coveredFiles_;      ///< files whose upper layer is live
    QSet<QString> lowerAnalyzed_;     ///< files the CURRENT lower analysis evaluated; cleared with its rows
    QHash<QString, quint64> editSeq_; ///< file -> last edit sequence number
    quint64 editCounter_ = 0;
    quint64 passStartEditSeq_ = 0;
    QStringList passLintedFiles_; ///< effective files of the running pass
    quint64 viewRevision_ = 0;
    quint64 lastSessionMessageRevision_ = 0;
    quint64 projectPassSerial_ = 0;
    bool nextPassAutomatic_ = false;
    bool passAutomaticRequested_ = false;
    bool lastPassAutomatic_ = false;

    ToolchainReport toolchain_;
    QString lastError_;
    quint64 generation_ = 0;
    quint64 navigation_ = 0;
    bool lspStarted_ = false;
    bool lspDiagnosticsRequested_ = false;
};

} // namespace vedit::workspace::diagnostics
