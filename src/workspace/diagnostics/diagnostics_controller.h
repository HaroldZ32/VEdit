#pragma once

// Diagnostics controller: one realtime session, two backends (Verible LSP +
// SLANG), one observable reparse state.
//
//   * batch-20261116 (L1.1): the PROJECT file set and the REALTIME session are
//     separate. `setFileSet()` records the project set for the project-pass
//     filter and the per-file status marks; it never initializes a language
//     server over it. `analyzeFile()` opens the realtime session over the
//     current file plus its one-level module closure (bounded), and only that
//     set is ever requested for diagnostics.
//   * A3c (manual reparse): `reparse()` hands the SAME realtime set to the LSP
//     session and to the SLANG backend; fileSetReport() proves it.
//   * A3d: reparse()/restartRealtimeSession()/a new file-scope session move the
//     state to Reparsing and refresh() advances it to Ready / Degraded / Failed;
//     the snapshot carries the counts. setFileSet() alone starts nothing.
//   * The five navigation entry points (definition, references, symbols,
//     rename, format) are polled requests — nothing here blocks the UI thread.
//
// The controller owns its backends, so the self-test injects fakes without an
// FFI library or a language server.

#include "diagnostics_types.h"
#include "file_status.h" // src/common: the ONE status vocabulary (RM-9)
#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>
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

/// What the 400 ms live publisher must republish (batch 20261110d, T9).
///
/// A STRUCTURAL change (a finished project pass, a lane update, a scope change)
/// returns `full` with the whole status map, exactly like the pre-T9 publisher.
/// A PER-FILE change (one document edited, one file's retained verdict replaced)
/// returns only `changedFiles`; the consumers then update those entries instead
/// of rebuilding the project. `viewRevision`/`fileRevision` are the two tokens a
/// consumer compares; the counts are the cheap exact numbers, never a merged-view
/// rebuild (the pre-T9 publisher sorted the whole merged view just to count it).
struct LivePublish {
    /// True when `statuses` is the whole map (a structural change).
    bool full = false;
    /// Structural token, monotone. 0 means "nothing to publish yet".
    quint64 viewRevision = 0;
    /// Per-file token, monotone; moves when `changedFiles` is non-empty.
    quint64 fileRevision = 0;
    /// The whole status map when `full` (file -> FileStatus value).
    QHash<QString, int> statuses;
    /// The files whose per-file verdict changed when `!full`.
    QStringList changedFiles;
    /// Exact merged-view row count (upper + lower + lanes, de-duplicated).
    int mergedCount = 0;
    /// Display-view row count; -1 = not computed (the caller that needs the
    /// rows computes them once and reads their size).
    int displayCount = -1;
    /// Exact live upper-layer row count.
    int upperCount = 0;
};

/// Exact counters of the per-keystroke view path (observability + the edit-jank
/// benchmark). Cheap increments only; never a reason to scan anything.
struct DiagnosticsWorkStats {
    /// Upper-layer rows walked by the LAST edit invalidation (the O(rows) cost
    /// T9 removes; pre-T9 it is the whole project's row count).
    quint64 upperRowsScannedOnEdit = 0;
    /// Full merged-view rebuilds (merge + sort).
    quint64 mergedRebuilds = 0;
    /// Full display-view rebuilds (merge + sort).
    quint64 displayRebuilds = 0;
    /// `takeLivePublish` calls that returned the whole status map.
    quint64 fullPublishes = 0;
    /// `takeLivePublish` calls that returned a per-file delta.
    quint64 deltaPublishes = 0;
    /// Status entries a consumer had to apply across those publishes.
    quint64 statusEntriesPublished = 0;
};

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

    /// Replace the PROJECT file set (batch-20261116 L1.1): it feeds the project
    /// pass, the per-file status marks and `projectFiles()`; it never starts or
    /// stops the realtime session.
    void setFileSet(const QStringList &files);
    const QStringList &fileSet() const { return files_; }

    /// Restart the REALTIME (lower-layer) session over the CURRENT realtime set
    /// and keep the rendered lower rows until the fresh answer lands.
    ///
    /// Needed when the LSP backend was stopped *under* this controller - the
    /// project-environment repin (`BackendSwitchState::setProjectEnvironment`)
    /// and a settings/backend apply both call
    /// `FfiLspSessionBackend::selectBackend()`, which stops the server
    /// unconditionally, while `lspStarted_`/`files_` stay as they were. Without
    /// this call the next `analyzeFile()` reuses a session that is gone and the
    /// realtime LINT stays dead (batch-20261116 B1). No-op when no document has
    /// been analysed yet (the project set must still start nothing).
    void restartRealtimeSession();
    /// Stop using the current server before the watcher adopts new project inputs.
    void invalidateRealtimeSession();

    /// Restart the reparse for the current set (manual `project.reparse`): stops
    /// the session, clears the rendered lower rows and re-runs both backends.
    void reparse();

    /// File-scope analysis that follows the EDITOR BUFFER (T118): start (or
    /// reuse) a session over exactly `file`, replace the tracked document text
    /// with `bufferText` (a didChange; nothing is read from disk) and collect
    /// diagnostics + the outline for that file. `bufferDirty` only labels the
    /// source in the UI/status. Returns false when no session could be started.
    bool analyzeFile(const QString &file, const QString &bufferText, bool bufferDirty);
    /// batch-20261115 AC-5.1 (A61.3): the STABLE set a file-scope session covers
    /// = the union of the level-1 module closures of every OPEN document. A tab
    /// switch inside that union reuses the running session instead of stopping
    /// and starting a language server. Empty keeps the historical
    /// "session over exactly the analysed file" behaviour.
    void setRealtimeSessionFiles(const QStringList &files);
    const QStringList &realtimeSessionFiles() const { return realtimeSessionFiles_; }
    /// G1-a / G1-c: may the NEXT analysed document join the session above?
    ///
    /// The language server is started over this controller's session, and that
    /// file list IS the compiler input (`vd_lsp_session_start_v2` writes one
    /// build context from it). `analyzeFile()` therefore may not widen it with
    /// the active document: a document outside the golden project file set, or
    /// one the catalog shadowed as a duplicate loser, is analysed over `{file}`
    /// ALONE (G1-a / G1-c). The watcher decides membership (it owns the golden
    /// and shadowed sets, `realtime_file_scope.h`) and publishes the answer
    /// here; the default `true` keeps a bare controller - no watcher, no project
    /// - bit-for-bit at its historical behaviour, where the caller's session set
    /// is trusted as given.
    void setActiveDocumentJoinsProjectSession(bool joins);
    bool activeDocumentJoinsProjectSession() const { return activeDocumentInSession_; }
    void setRealtimeProjectIndex(const QStringList &files) { realtimeProjectIndex_ = files; }
    /// batch-20261118 AC-5: the REQUEST scope - the active document's one-level
    /// closure. The session may still cover the union of the open documents
    /// (reuse); only this set is asked for diagnostics and considered. Empty
    /// keeps the historical "request exactly the session set" behaviour.
    void setRealtimeRequestFiles(const QStringList &files);
    const QStringList &realtimeRequestFiles() const { return realtimeRequestFiles_; }
    /// batch-20261118 AC-2: true when the server produced NEITHER a
    /// `publishDiagnostics` set NOR outline symbols for the analysed document.
    /// The panel prints the attributable state (never "no problems reported").
    bool serverSilent() const;
    /// batch-20261116 L1.1: the file set the RUNNING realtime LSP session was
    /// opened with - the current file plus its one-level closure, never the
    /// project set. `fileSetReport().files` carries the same list.
    const QStringList &realtimeSessionSet() const { return files_; }
    /// L1.1 probe: the files the LAST diagnostics request was issued for. Every
    /// entry is in the realtime set; the project set can never appear here.
    const QStringList &lastDiagnosticsRequestFiles() const { return lastDiagnosticsRequestFiles_; }
    /// Total file requests issued by refresh() since construction.
    quint64 diagnosticsRequestCount() const { return diagnosticsRequestCount_; }

    /// batch-20261116 L1.2: the document the EDITOR currently shows when this
    /// build has no analysis path for it (no analyzer for the file type, above
    /// the byte cap, an empty buffer). The watcher publishes the reason BEFORE
    /// it returns, so the panel can name it instead of showing a blank
    /// "no diagnostics". `available = true` clears the reason.
    void setDocumentAnalysisAvailability(const QString &path, bool available,
                                         const QString &reason);
    const QString &documentAnalysisPath() const { return documentPath_; }
    bool documentAnalysisAvailable() const { return documentAvailable_; }
    const QString &documentAnalysisReason() const { return documentReason_; }
    /// L1.2: the ONE line explaining why the realtime surface has nothing for
    /// the document in front of the user (no analyzer / byte cap / top-scope
    /// exclusion / pending). Empty when the document IS analysed - a clean file
    /// keeps its ordinary "no problems reported" state.
    QString realtimeReason() const;
    /// AC-2 (batch-20261118): the bounded re-ask for a document the server has
    /// published nothing for. Runs from refresh() while no analysis round is in
    /// flight, so it never delays the terminal state.
    void pollServerSilence();
    /// Probe counters (AC-5.1): how often this controller started/stopped an
    /// LSP session. A session that is reused does not move them.
    quint64 lspStartCount() const { return lspStarts_; }
    quint64 lspStopCount() const { return lspStops_; }
    /// The file whose diagnostics the realtime surfaces (panel, counts, badge)
    /// show. Empty = no filter (the historical whole-view behaviour).
    QString realtimeDisplayFile() const { return analysisFile_; }
    /// The LOWER (per-file/LSP) rows of ONE file - the realtime layer, without
    /// the whole-project rows. The Outline panel reads this (AC-4.3: the project
    /// rows belong to the Project Lint dock, not to the Outline).
    QVector<DiagnosticEntry> lowerRowsForFile(const QString &file) const;
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
    /// RM-11: paths a lint request carried that this build has NO lint analyzer
    /// for (a `.f` manifest, a `.md`, a `.tcl`, ...). They are excluded from the
    /// pass - never parsed as HDL - and kept here with the recorded reason
    /// "no lint analyzer for this file type".
    const QStringList &projectPassNonSource() const { return passNonSource_; }
    LintPassSnapshot projectPass() const { return pass_; }
    /// Diagnostics collected by the CLI pass (AC-2.13 CLI side).
    /// The upper-layer rows as a flat list (the pre-T9 shape). The rows are
    /// stored per file; this rebuilds the flat cache only when asked (T9).
    const QVector<DiagnosticEntry> &cliDiagnostics() const;
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

    // --- rows produced by a WORKSPACE LANE (AC-6.4) -----------------------
    // A lane owns its own tools (the VHDL lane runs GHDL + VSG), but this
    // controller owns the ONE display view: the panel, the counts, the hover and
    // the file-tree dots all read it, so a lane's rows must land HERE or they
    // would need a second, disagreeing view.
    //
    // Semantics, spelled out:
    //   * a second call with the same `source` REPLACES that source's rows
    //     (lanes re-analyse; they never stack);
    //   * an EMPTY `rows` CLEARS that source (a re-lint that found nothing must
    //     remove the old rows);
    //   * lane rows are the LOWER layer: a file the finished project pass covers
    //     keeps the pass's verdict — one file, one truth (R2), no stacking;
    //   * rows are already in the DISPLAY scale (1 error, 2 warning, 3 info,
    //     4 hint, 0 unknown); they are never re-mapped, and `entry.source`
    //     ("ghdl" / "vsg") is what names the producer per row.
    //
    // AUTHORIZATION (batch 20261110c, cell cell-20261112T003245505Z, card T15):
    // this header was added to the card's write scope by the captain explicitly,
    // because the pane's row source is `displayDiagnostics()` and there was no
    // injection point. ONE method and ONE private member were added; nothing
    // else in this class changed.
    void setLaneDiagnostics(const QString &source, const QVector<DiagnosticEntry> &rows);

    /// File status, taken from the merged view. The values (and the colours
    /// every surface paints) live in src/common/file_status.h: one vocabulary
    /// for the controller, the file-tree dot and the Project panel's list.
    using FileStatus = ::vedit::common::FileStatus;
    FileStatus statusForFile(const QString &file) const;
    /// Every file with a known status, for the file tree's cached map. Built
    /// from the SAME per-file derivation as statusForFile() (one source), so a
    /// dot and a list row can never disagree.
    QHash<QString, int> fileStatuses() const;
    /// RM-17: the top scope the module tree derived from the SAME catalog run
    /// that produced its tree (see src/workspace/top_scope.h).
    ///
    /// `top` empty = Auto: the whole project is linted and the marks this call
    /// set are cleared. With a top: the NEXT project pass lints only files that
    /// are NOT excluded, and every `excluded` file keeps the visible NotAnalyzed
    /// mark with a reason that names the top (never green, never "no mark").
    ///
    /// RULING (RM-17b): while a top is active, exclusion by the user's own
    /// instruction is AUTHORITATIVE for the dot and for whether any layer may
    /// analyse the file - it outranks the upper project layer AND the lower
    /// per-file layer. This does NOT contradict RM-9 ruling 1 (a fresh verdict
    /// beats a stale one for a dirty buffer): that ruling resolves STALENESS,
    /// this one resolves EXCLUSION, and an instruction outranks a verdict.
    /// `analysed` is accepted for the seam and documented in the publisher; it
    /// is deliberately NOT stored: with a top, a requested file is analysed
    /// exactly when it is not excluded, so a second list could only drift.
    void setTopScope(const QString &top, const QStringList &analysed,
                     const QStringList &excluded);

    /// RM-9: the files of the CURRENT project set. A file of this set with no
    /// verdict yet is NotAnalyzed (a visible "not analysed" mark), never
    /// "no dot": "nothing" is indistinguishable from "not a file" for the user.
    const QStringList &projectFiles() const { return projectFiles_; }
    /// Bumped whenever the merged view changes: a repaint cache key, never a
    /// reason to rescan the project.
    quint64 viewRevision() const { return viewRevision_; }
    /// Per-file token: moves on every change that affects ONE file's verdict
    /// (an edit invalidating its upper layer, a retained lower verdict, a
    /// per-file "not analysed" mark). A consumer that compares this token and
    /// repaints only the reported files never rebuilds the project (T9).
    quint64 fileViewRevision() const { return fileViewRevision_; }
    /// ONE call for the 400 ms live publisher: the whole status map after a
    /// structural change, else the files whose verdict moved. Also returns the
    /// cheap exact counts, so a status line never rebuilds the merged view.
    LivePublish takeLivePublish();
    /// Moves on EVERY change (structural or per-file). A consumer that only
    /// needs "did anything change?" compares this one number (T9).
    quint64 changeSerial() const { return changeSerial_; }
    /// Exact merged-view row count, maintained per file (T9: never a rebuild).
    int mergedDiagnosticCount() const;
    /// Exact live upper-layer row count (covered files only).
    int upperLayerDiagnosticCount() const;
    /// Exact counters behind the per-keystroke path (tests / the edit-jank
    /// benchmark). Rolling numbers, no allocation, no scan.
    const DiagnosticsWorkStats &workStats() const { return workStats_; }
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
    const QVector<DiagnosticEntry> &upperDiagnostics() const;
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
    /// Reparse counter; changes on every reparse()/restartRealtimeSession() and
    /// on every new file-scope session (setFileSet() alone does not move it).
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
    /// A STRUCTURAL change: the whole view may differ (pass, file set, lanes).
    void invalidateView()
    {
        ++viewRevision_;
        ++changeSerial_;
        countsAllDirty_ = true;
    }
    /// A per-file change: only `file`'s verdict moved (T9). The publisher can
    /// repaint that file instead of rebuilding the project.
    void invalidateFile(const QString &file);
    /// The merged-view row count of ONE file, without sorting (T9 counts).
    int mergedRowCountForFile(const QString &file) const;
    /// Rebuild the flat `cliDiagnostics_` cache from the per-file store.
    void rebuildCliFlat() const;

    // --- RM-9: retained per-file verdict store ---------------------------
    /// RM-17b ruling: true while a top is active and `file` is outside it.
    /// Consulted FIRST by statusValueForFile()/fileStatuses(), by the project
    /// pass filter, by the per-file start paths and by replaceUpperLayer(): the
    /// excluded set outranks both the upper and the lower layer (exclusion by
    /// instruction, never overridden by a verdict).
    bool isScopeExcluded(const QString &file) const;
    /// ONE per-file derivation, used by statusForFile() AND fileStatuses():
    /// active-scope exclusion > upper layer > fresh lower verdict > mark >
    /// pending (project set). 0 = no dot (not part of any analysed set).
    int statusValueForFile(const QString &file) const;
    /// The lower-layer rows of `file` that are still trustworthy (empty when
    /// the file was edited after its last analysis or never analysed).
    /// Keep `file`'s CURRENT rows after a lower-layer collection. This is the
    /// single point where the retained store is fed: the same rows the merged
    /// view reads, so the dot/panel/count cannot disagree with the editor.
    void retainLowerRows(const QString &file);
    void clearRetainedRows(const QString &file);
    /// A visible "no verdict yet" state: kept until a fresh lower verdict lands
    /// or the pass covers the file. `reason` is a stable code ("pending" for
    /// queued work, "modified" for a dirty buffer excluded from the pass).
    void markNotAnalyzed(const QString &file, const QString &reason);
    void clearNotAnalyzed(const QString &file);

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
    QStringList passNonSource_; ///< RM-11: excluded - no lint analyzer for the file type
    /// T9: the upper layer is stored PER FILE, so dropping one file's rows and
    /// reading one file's verdict are O(that file), not O(the project). The flat
    /// vector below is a lazily rebuilt cache for the public accessors.
    QHash<QString, QVector<DiagnosticEntry>> cliByFile_;
    mutable QVector<DiagnosticEntry> cliDiagnostics_;
    mutable bool cliFlatDirty_ = false;
    /// Merged-view count cache: per file + total, recomputed only for the files
    /// that changed (the merge key contains the file, so the counts add up).
    mutable QHash<QString, int> mergedCountByFile_;
    mutable int mergedCountTotal_ = 0;
    mutable QSet<QString> countDirtyFiles_;
    mutable bool countsAllDirty_ = true;
    quint64 changeSerial_ = 0;
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
    quint64 fileViewRevision_ = 0;
    /// What takeLivePublish() already handed out; a consumer that calls it twice
    /// with no change in between gets an empty answer (never a duplicate full
    /// map). See the T9 comment on the method.
    quint64 publishedViewRevision_ = std::numeric_limits<quint64>::max();
    quint64 publishedFileRevision_ = std::numeric_limits<quint64>::max();
    /// Files whose per-file verdict changed since the last takeLivePublish().
    QSet<QString> pendingFileChanges_;
    mutable DiagnosticsWorkStats workStats_;
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

    // RM-9: the retained per-file verdict store. `diagnostics_` below stays the
    // CURRENT analysis' display rows; these survive a tab switch so a file that
    // is not the active document still carries its verdict (the user-visible
    // defect: "只在正在打开/聚焦的文件上显示"). Fed from the same collection
    // points that feed the merged view - there is no second merge.
    QStringList projectFiles_; ///< the project set (setFileSet; NOT files_)
    /// AC-5.1: the stable session set the watcher computed (empty = per-file).
    QStringList realtimeSessionFiles_;
    QStringList realtimeProjectIndex_;
    QStringList runningProjectIndex_;
    /// G1-a/G1-c: false = the active document is analysed alone; the session
    /// above must not be widened with it. `true` by default (see the setter).
    bool activeDocumentInSession_ = true;
    /// AC-5 (batch-20261118): the active closure the REQUEST loop is scoped to.
    QStringList realtimeRequestFiles_;
    /// AC-1: the Ready slang session already received this session's build
    /// context. Reset on every restart/re-selection.
    bool buildContextPushed_ = false;
    /// AC-3: the last restart count this controller acted on. A change means a
    /// NEW server process with no build context and no published sets.
    int lastRestartsSeen_ = 0;
    /// AC-2: a bounded re-ask window while the server has published nothing for
    /// the analysed document (a late publish - e.g. right after the build
    /// context arrives - is still picked up).
    qint64 serverSilentSinceMs_ = 0;
    /// AC-2: documents the server really published a diagnostics set for. Only
    /// these may be read as "0 problems"; the rest are "not analysed".
    QSet<QString> publishedDiagnosticsFiles_;
    quint64 lspStarts_ = 0;
    quint64 lspStops_ = 0;
    QSet<QString> projectFileSet_; ///< O(1) membership for the per-row status
    QHash<QString, QVector<DiagnosticEntry>> retainedLowerRows_;
    QSet<QString> retainedLowerEvaluated_;
    QHash<QString, QString> notAnalyzedReason_;
    // batch-20261116 L1.1: observability of the diagnostics request path. The
    // list is always a subset of `files_` (the realtime set); it exists so a
    // product-level test can prove the project set never becomes the request
    // set, without reading the LSP backend's internals.
    QStringList lastDiagnosticsRequestFiles_;
    quint64 diagnosticsRequestCount_ = 0;
    // batch-20261116 L1.2: the watcher's verdict on the document it is showing.
    // `available = false` means no realtime analysis is possible at all, and
    // `documentReason_` is the one user-visible line naming why.
    QString documentPath_;
    QString documentReason_;
    bool documentAvailable_ = true;
    // RM-17: the top scope (empty top = Auto). The pass filter and the dot
    // derivation both read this, so the exclusion set cannot differ.
    QString topScopeTop_;
    QSet<QString> scopeExcluded_; ///< clean paths the ACTIVE top excludes
    /// AC-6.4 (T15): rows a workspace LANE produced, keyed by its source name.
    /// See setLaneDiagnostics(): replace-per-source, empty clears, lower layer.
    QHash<QString, QVector<DiagnosticEntry>> laneRows_;
};

} // namespace vedit::workspace::diagnostics
