#include "diagnostics_controller.h"

#include "../project/language_registry.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <utility>
#include <vector>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
#include "vd_ffi.h"
#include <QCoreApplication>
#endif

// UX batch 2 / U6: the lint-knob composition seam is header-only, so the
// diagnostics lane gains no link dependency on the settings library.
#if __has_include("../settings/lint_options.h")
#include "../settings/lint_options.h"
#define VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS 1
#endif

namespace vedit::workspace::diagnostics {

namespace {

/// Map the LSP backend's own phase onto the joint state vocabulary.
BackendState backendState(BackendState state)
{
    return state;
}

QString describeState(BackendState state)
{
    switch (state) {
    case BackendState::Idle:
        return QStringLiteral("idle");
    case BackendState::Reparsing:
        return QStringLiteral("reparsing");
    case BackendState::Ready:
        return QStringLiteral("ready");
    case BackendState::Degraded:
        return QStringLiteral("degraded");
    case BackendState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

/// C11: over this many files the pass says so explicitly (cancel stays usable).
constexpr int kLargePassWarningFiles = 1000;

/// batch-20261117 (AC-4): the ONE bound of the buffer-settle window.
///
/// After a didChange the controller keeps re-asking for diagnostics until the
/// server publishes a snapshot that is newer than the applied buffer. That
/// window is bounded: when it expires the round settles on whatever the server
/// currently has (the historical semantics), so a server that never publishes
/// cannot leave the panel in Reparsing forever. `refresh()` opens the window and
/// `collectLspDiagnostics()` accepts a poll at this bound; `finaliseState()`
/// keeps the round open until the same bound.
constexpr qint64 kBufferSettleMs = 2500;

/// batch-20261118 AC-2: how long the controller keeps re-asking for a document
/// the server has no diagnostics entry for. The re-ask exists so the analysis
/// the build context triggers (AC-1) is noticed even when it publishes after
/// the first request; after this bound the panel settles on the attributable
/// "the server did not analyse this file" state instead of asking forever.
constexpr qint64 kServerSilentReaskMs = 5000;

/// The ONE view a registered waiver file cannot reach, said out loud.
///
/// A waiver is a CLI-engine input: the project pass hands `--waiver_files`
/// (verible) / `--waiver-file` (slang) to the child. The language-server rows
/// come from `verible-verilog-ls` / the slang server, started through
/// `vd_lsp_session_start_v2`, whose C ABI has no waiver parameter at all (the
/// verible server itself WOULD accept `--waiver_files=`; the ABI cannot pass
/// it). So when a waiver is registered, the project pass is clean while the
/// language-server rows still carry the diagnostic.
///
/// The rule is "no silent disagreement": the status line names the side that
/// does not apply the waiver instead of showing two views that contradict each
/// other. Closing the gap needs an ABI `_v3` (follow-up card); it is NOT fixed
/// by dropping the waiver from the pass.
QString waiverScopeNote()
{
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS)
    const settings::LintValues &values = settings::currentLintValues();
    const bool waiverRegistered = !values.verible.waiverFiles.trimmed().isEmpty()
        || !values.slang.waiverFile.trimmed().isEmpty();
    if (!waiverRegistered) {
        return QString();
    }
    return QCoreApplication::translate(
        "VEdit",
        " · waiver: the registered waiver file is applied by the project pass only; "
        "the language-server rows do not apply it");
#else
    return QString();
#endif
}

int canonicalSeverity(int severity, DiagnosticOrigin origin)
{
    if (origin == DiagnosticOrigin::Cli) {
        // VdDiagnostic scale: 0 error, 1 warning, 2 note, 3 ignored, 4 unknown.
        switch (severity) {
        case 0:
            return 1; // error
        case 1:
            return 2; // warning
        case 2:
            return 3; // information
        case 3:
            return 4; // hint
        default:
            return 0; // unknown
        }
    }
    // LSP DiagnosticSeverity: 1 error, 2 warning, 3 information, 4 hint.
    return severity >= 1 && severity <= 4 ? severity : 0;
}

QString normalizedMessage(const QString &message)
{
    QString out = message.trimmed();
    out.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return out;
}

/// CLI entries reach the display layer in the C ABI's severity scale (0 error,
/// 1 warning, ...); the panel renders the LSP scale (1 error, 2 warning, ...).
/// BOTH CLI producers cross this conversion: the project-pass rows at display
/// time, the per-file rows when `collectLints()` stores them. A producer that
/// AC-6.4 (T15): the rows a LANE named for one file, across every source.
///
/// A lane is a lower-layer producer (VHDL's GHDL+VSG merge). The caller keeps
/// the per-source identity; this helper answers "did any lane evaluate `file`,
/// and with what". An EMPTY result means no lane spoke about the file - the
/// caller must not read that as "clean": a clean lane CLEARS its rows, so
/// absence is "no verdict", never "no findings".
QVector<DiagnosticEntry> laneRowsForFile(
    const QHash<QString, QVector<DiagnosticEntry>> &laneRows, const QString &file)
{
    QVector<DiagnosticEntry> rows;
    if (file.isEmpty()) {
        return rows;
    }
    for (auto it = laneRows.constBegin(); it != laneRows.constEnd(); ++it) {
        for (const DiagnosticEntry &entry : it.value()) {
            if (entry.file == file) {
                rows.append(entry);
            }
        }
    }
    return rows;
}

/// skips it paints a warning as an error (and an error as unknown).
DiagnosticEntry toDisplayEntry(const DiagnosticEntry &entry)
{
    DiagnosticEntry converted = entry;
    converted.severity = canonicalSeverity(entry.severity, DiagnosticOrigin::Cli);
    return converted;
}

} // namespace

QString diagnosticMergeKey(const DiagnosticEntry &entry, DiagnosticOrigin origin)
{
    // One normalized spelling per file: two producers (or the same producer
    // twice) may write the path with a different relative form. The old
    // expression returned `entry.file` in BOTH branches, so it never normalized
    // anything and the same file could be counted twice.
    QString path = entry.file;
    if (!path.isEmpty()) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!absolute.isEmpty()) {
            path = absolute;
        }
    }
    return QStringLiteral("%1|%2|%3|%4")
        .arg(path)
        .arg(entry.line)
        .arg(canonicalSeverity(entry.severity, origin))
        .arg(normalizedMessage(entry.message));
}

// LEGACY (R2): this is the pre-layering merge. It still marks `Both`, which is
// exactly the CLI+LSP stacking R2 forbids; nothing in the product calls it any
// more (the probe in evidence/w4-lint is its only consumer). Do not add a
// product call site: use mergedDiagnostics()/displayDiagnostics() instead.
QVector<DisplayDiagnostic> mergeDiagnostics(const QVector<DiagnosticEntry> &lsp,
                                            const QVector<DiagnosticEntry> &cli)
{
    QVector<DisplayDiagnostic> rows;
    QHash<QString, int> indexByKey;
    for (const DiagnosticEntry &entry : lsp) {
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Lsp);
        if (indexByKey.contains(key)) {
            continue; // the same engine reported it twice: still one row
        }
        DisplayDiagnostic row;
        row.entry = entry;
        row.origin = DiagnosticOrigin::Lsp;
        row.key = key;
        indexByKey.insert(key, rows.size());
        rows.append(row);
    }
    for (const DiagnosticEntry &entry : cli) {
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
        const auto found = indexByKey.constFind(key);
        if (found != indexByKey.constEnd()) {
            rows[found.value()].origin = DiagnosticOrigin::Both; // LSP entry kept
            continue;
        }
        DisplayDiagnostic row;
        row.entry = toDisplayEntry(entry);
        row.origin = DiagnosticOrigin::Cli;
        row.key = key;
        indexByKey.insert(key, rows.size());
        rows.append(row);
    }
    std::sort(rows.begin(), rows.end(), [](const DisplayDiagnostic &left, const DisplayDiagnostic &right) {
        if (left.entry.file != right.entry.file) {
            return left.entry.file < right.entry.file;
        }
        if (left.entry.line != right.entry.line) {
            return left.entry.line < right.entry.line;
        }
        return left.entry.severity < right.entry.severity;
    });
    return rows;
}

QString lintCancelledMessage(int done, int total)
{
    return QCoreApplication::translate("VEdit", "cancelled — %1/%2 file(s)").arg(done).arg(total);
}

DiagnosticsController::DiagnosticsController(std::unique_ptr<LspSessionBackend> lsp,
                                             std::unique_ptr<SlangBackend> slang)
    : lsp_(std::move(lsp))
    , slang_(std::move(slang))
{
}

DiagnosticsController::~DiagnosticsController()
{
    if (lsp_ && lspStarted_) {
        lsp_->stop();
        ++lspStops_;
    }
}

void DiagnosticsController::setConfiguration(const QString &lsPath,
                                             const QString &workspaceRoot,
                                             const QString &rulesPath)
{
    lsPath_ = lsPath;
    workspaceRoot_ = workspaceRoot;
    rulesPath_ = rulesPath;
}

void DiagnosticsController::setFileSet(const QStringList &files)
{
    // batch-20261116 L1.1: the PROJECT set and the REALTIME session are two
    // different sets, and confusing them was the defect: every file-set arrival
    // called reparse(), which initialized the language server over the WHOLE
    // project and then requested diagnostics for all of it every tick. A 20k-file
    // project therefore never became Ready and produced zero rows.
    //
    // This method records the project set only (the project-pass filter and the
    // per-file status marks read it). It must NOT touch a running realtime
    // session: at startup the project set can arrive AFTER the first
    // analyzeFile(), and restarting/clearing there would blank the editor.
    projectFiles_ = files;
    projectFileSet_ = QSet<QString>(files.begin(), files.end());
    if (!analysisFile_.isEmpty()) {
        // A realtime analysis is live: keep its session and set; only the
        // project-layer observations (done/pending marks) move. The project
        // membership itself is a structural change for the status map.
        invalidateView();
        refresh();
        return;
    }
    // No realtime analysis yet: this is a project-layer update, not a reparse.
    // There is deliberately no `files_ = files` and no startLsp()/startLints().
    files_.clear();
    report_ = FileSetReport{};
    report_.slangAvailable = slang_ && slang_->available();
    session_ = SessionSnapshot{};
    if (files.isEmpty()) {
        state_ = BackendState::Idle;
        session_.state = BackendState::Idle;
        session_.message = QCoreApplication::translate("VEdit", "no effective file set");
    } else {
        // L1.1's user-visible half: a project file set alone runs no server.
        state_ = BackendState::Idle;
        session_.state = BackendState::Idle;
        session_.message = QCoreApplication::translate(
                               "VEdit", "project file set: %1 file(s) — open a file to analyse")
                               .arg(files.size());
    }
    invalidateView();
}

void DiagnosticsController::setToolchainReport(const ToolchainReport &report)
{
    toolchain_ = report;
    if (!toolchain_.missing().isEmpty() && lastError_.isEmpty()) {
        lastError_ = toolchain_.userMessage();
    }
}

void DiagnosticsController::invalidateRealtimeSession()
{
    if (lsp_ && lspStarted_) {
        lsp_->stop();
        ++lspStops_;
    }
    lspStarted_ = false;
    navigation_ = 0;
    outlineRequest_ = 0;
    session_ = SessionSnapshot{};
}

void DiagnosticsController::restartRealtimeSession()
{
    // B1 (batch-20261116): the project-environment repin and every settings /
    // backend apply stop the LSP server through
    // `FfiLspSessionBackend::selectBackend()` (unconditional `stop()`), while
    // this controller's `lspStarted_`/`files_` are unchanged. `analyzeFile()`
    // would then see "same set, session started" and reuse a server that is
    // gone - the realtime LINT stays dead until the application restarts.
    //
    // Restart over the SAME realtime set, and KEEP the rendered lower rows: the
    // user keeps the last verdict until the fresh answer replaces it (no blank
    // editor on every `.f` import or settings save).
    if (analysisFile_.isEmpty()) {
        // Nothing analysed yet: the project set must still start no session.
        return;
    }
    if (files_.isEmpty()) {
        files_ = QStringList{analysisFile_};
    }
    invalidateRealtimeSession();
    lspDiagnosticsRequested_ = false;
    lspDiagnostics_.clear();
    lints_.clear();
    lastDiagnosticsRequestFiles_.clear();
    // A brand-new session has seen neither the editor buffer nor a symbol
    // request: reset those one-shot gates so refresh() pushes the buffer and
    // asks for the outline again. Diagnostics/outline themselves stay visible.
    bufferApplied_ = false;
    bufferAppliedMs_ = 0;
    bufferGenerationAtApply_ = 0;
    bufferDiagnosticsSeen_ = false;
    symbolsRequested_ = false;
    // AC-1/AC-2 (batch-20261118): a fresh server has received no build context
    // and has published nothing; both must be re-established.
    buildContextPushed_ = false;
    publishedDiagnosticsFiles_.clear();
    serverSilentSinceMs_ = 0;
    realtimeRequestFiles_.clear();
    lastError_.clear();
    session_ = SessionSnapshot{};
    report_ = FileSetReport{};
    report_.files = files_;
    report_.slangAvailable = false; // realtime file scope: the LSP is the fast path
    if (workspaceRoot_.isEmpty()) {
        workspaceRoot_ = QFileInfo(analysisFile_).absolutePath();
    }
    ++generation_;
    invalidateView();
    state_ = BackendState::Reparsing;
    session_.state = BackendState::Reparsing;
    session_.message = QCoreApplication::translate(
                           "VEdit", "restarting the realtime analysis over %1 file(s)")
                           .arg(files_.size());
    startLsp();
    refresh();
}

void DiagnosticsController::reparse()
{
    // batch-20261116 L1.1: a reparse restarts the REALTIME session - the set the
    // current analysis was started with (current file + level-1 closure) - never
    // the project set. `project.reparse` is a manual command; it is the one place
    // that drives BOTH backends (A3c) over that bounded set.
    ++generation_;
    if (lsp_ && lspStarted_) {
        lsp_->stop();
        ++lspStops_;
    }
    lspStarted_ = false;
    navigation_ = 0;
    outlineRequest_ = 0;
    lspDiagnosticsRequested_ = false;
    lints_.clear();
    lspDiagnostics_.clear();
    diagnostics_.clear();
    // N1: the "lower layer evaluated this file" marks describe the rows that
    // were just dropped. Keeping them would turn a file with errors into a green
    // dot (Clean) after the analysis is gone; the verdict must follow the rows.
    lowerAnalyzed_.clear();
    // RM-9: a reparse starts a NEW lower session, so every RETAINED lower
    // verdict is about the old session and is void. The project set keeps an
    // explicit "not analysed" state until the new session (or the pass) reports.
    retainedLowerRows_.clear();
    retainedLowerEvaluated_.clear();
    notAnalyzedReason_.clear();
    // RM-17b: every reparse clears the reasons above, which would erase the
    // ACTIVE scope's reasons, turning an excluded file green on the next paint.
    // Re-apply them from the ONE derivation (the excluded set).
    if (!topScopeTop_.isEmpty()) {
        const QString prefix = QStringLiteral("outside-top:");
        for (const QString &file : scopeExcluded_) {
            markNotAnalyzed(file, prefix + topScopeTop_);
        }
    }
    outline_.clear();
    lastError_.clear();
    invalidateView();
    session_ = SessionSnapshot{};
    // batch-20261117 (AC-4): a reparse starts a BRAND-NEW backend session, so its
    // diagnostics-generation counter restarts with it. The buffer settle floor
    // belongs to the session that recorded it: without this reset the new
    // session's counter could never clear a floor from the old one and the round
    // would sit through the whole settle window. Reset the one-shot gates so the
    // new session receives the editor buffer (same rule restartRealtimeSession()
    // already applies to a session it had to recreate).
    bufferApplied_ = false;
    bufferAppliedMs_ = 0;
    bufferGenerationAtApply_ = 0;
    bufferDiagnosticsSeen_ = false;
    report_ = FileSetReport{};
    if (files_.isEmpty() && !analysisFile_.isEmpty()) {
        files_ = QStringList{analysisFile_}; // the session of one file survives
    }
    report_.files = files_;
    report_.slangAvailable = slang_ && slang_->available();

    if (files_.isEmpty()) {
        state_ = BackendState::Idle;
        session_.state = BackendState::Idle;
        session_.message = QCoreApplication::translate(
            "VEdit", "no realtime file set — open a file to analyse");
        return;
    }

    state_ = BackendState::Reparsing;
    session_.state = BackendState::Reparsing;
    session_.message = QCoreApplication::translate("VEdit", "reparsing %1 file(s)").arg(files_.size());
    startLsp();
    startLints();
    refresh();
}

bool DiagnosticsController::analyzeFile(const QString &file, const QString &bufferText,
                                        bool bufferDirty)
{
    if (file.isEmpty()) {
        return false;
    }
    // Record the analysis request FIRST: the session start below already runs a
    // refresh(), and that refresh must see the buffer it is supposed to push.
    analysisFile_ = file;
    analysisText_ = bufferText;
    analysisDirty_ = bufferDirty;
    analysisInvalidated_ = false;
    lspDiagnosticsRequested_ = false;
    bufferApplied_ = false;
    bufferAppliedMs_ = 0;
    bufferGenerationAtApply_ = 0;
    symbolsRequested_ = false;
    ++analysisCount_;
    lastAnalysisAt_ = QDateTime::currentDateTime();

    // RM-9 retention rule (R2): an edit voids the file's verdict until the
    // analysis below lands. A dirty buffer's previous rows were computed on
    // older text, so they are dropped and the file shows the explicit
    // "not analysed / excluded" state - never a stale green. A clean file keeps
    // its previous verdict while the fresh one is in flight (no flicker), and a
    // file that has never been analysed gets a visible "pending" mark instead of
    // having no mark at all.
    if (bufferDirty) {
        clearRetainedRows(file);
        markNotAnalyzed(file, QStringLiteral("modified"));
    } else if (!retainedLowerEvaluated_.contains(file)) {
        markNotAnalyzed(file, QStringLiteral("pending"));
    }

    // batch-20261115 AC-5.1 / A61.3: the session covers a STABLE set - the union
    // of the level-1 module closures of every open document (`setRealtimeSession
    // Files`). Switching between documents inside that union must NOT stop and
    // start the language server: the session is reused and only the analysed
    // file's buffer/diagnostics move. A set the running session does not cover
    // still needs a (single) restart, because the server resolves modules across
    // the files it was opened with.
    //
    // batch-20261116 L1.1: the check is EXACT EQUALITY with the realtime desired
    // set, not "desired is a subset of whatever was opened". The old subset test
    // was satisfied by the PROJECT set (setFileSet assigned it to `files_`), so
    // no realtime session was ever started and refresh() requested diagnostics
    // for every project file on every tick.
    //
    // G1-a/G1-c (batch-20261120): the session set is the compiler input, so the
    // analysed document may not widen it. `activeDocumentJoinsProjectSession()`
    // is the watcher's answer to "is this document a golden member that the
    // catalog did not shadow?"; when it is false the session is `{file}` ALONE -
    // before this, an outside file was PREPENDED to the project session and a
    // duplicate loser was compiled next to the winner it lost to, which handed
    // `slang` a second authority over the same module (the reported defect).
    QStringList desired;
    if (!activeDocumentInSession_) {
        desired = QStringList{file};
    } else {
        desired = realtimeSessionFiles_;
        if (desired.isEmpty() || !desired.contains(file)) {
            desired.append(file);
        }
    }
    const auto sameSet = [](const QStringList &left, const QStringList &right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (const QString &candidate : right) {
            if (!left.contains(candidate)) {
                return false;
            }
        }
        return true;
    };
    const bool unionCovered = lspStarted_ && !files_.isEmpty() && sameSet(files_, desired)
        && runningProjectIndex_ == realtimeProjectIndex_;
    const bool sameFile = unionCovered && files_.contains(file);
    if (!sameFile || !lspStarted_) {
        // A file-scope analysis uses a session over EXACTLY the stable realtime
        // set; the project set is never a session set (L1.1).
        invalidateRealtimeSession();
        files_ = desired;
        lspDiagnosticsRequested_ = false;
        lastDiagnosticsRequestFiles_.clear();
        lints_.clear();
        lspDiagnostics_.clear();
        diagnostics_.clear();
        // N1: a new file-scope session replaces the previous analysis, so the
        // previous file's "evaluated" mark cannot survive it. Without this, a
        // file the LSP reported with errors becomes Clean (green) simply because
        // the user opened another file.
        lowerAnalyzed_.clear();
        outline_.clear();
        lastError_.clear();
        // AC-1/AC-2 (batch-20261118): a NEW session has no build context and
        // has published nothing for any file yet.
        buildContextPushed_ = false;
        publishedDiagnosticsFiles_.clear();
        serverSilentSinceMs_ = 0;
        session_ = SessionSnapshot{};
        report_ = FileSetReport{};
        report_.files = files_;
        report_.slangAvailable = false; // file scope: the LSP is the fast path
        // T118: a file opened WITHOUT a project still needs a workspace root for
        // the language server. Use the file's own directory (the project root is
        // kept when the product was started with --project).
        if (workspaceRoot_.isEmpty()) {
            workspaceRoot_ = QFileInfo(file).absolutePath();
        }
        ++generation_;
        state_ = BackendState::Reparsing;
        session_.state = BackendState::Reparsing;
        session_.message = QStringLiteral("analyzing %1").arg(QFileInfo(file).fileName());
        startLsp();
        refresh();
    } else {
        // Same file, running session: re-run the analysis on the new buffer.
        state_ = BackendState::Reparsing;
        refresh();
        if (session_.state == BackendState::Failed) {
            // Retry a live Slang server's context before applying the buffer.
            // A dead server instead restarts on the buffer change, then takes
            // its context in refresh(). Idle polling sends neither command.
            if (session_.serverPid != 0 && lsp_->backendKind() == LspBackendKind::Slang) {
                buildContextPushed_ = lsp_->pushBuildContext(nullptr);
            } else {
                lsp_->changeBuffer(file, bufferText, nullptr);
            }
        }
    }
    return lspStarted_;
}

void DiagnosticsController::setActiveDocumentJoinsProjectSession(bool joins)
{
    // G1-a/G1-c: one bit, decided by the watcher from the golden and shadowed
    // sets. A false value is consumed by the NEXT analyzeFile()/restart as the
    // `{file}` session (`realtime_file_scope.h` holds the rule itself); the
    // running session is not torn down here, because a watcher publishes this
    // on every tick and a session that is still correct must not be restarted
    // for a state it already satisfies.
    activeDocumentInSession_ = joins;
}

void DiagnosticsController::setRealtimeSessionFiles(const QStringList &files)
{
    QStringList normalized;
    normalized.reserve(files.size());
    for (const QString &file : files) {
        if (!file.isEmpty() && !normalized.contains(file)) {
            normalized.append(file);
        }
    }
    // Sorted: the union's ORDER must not differ between two switches that cover
    // the same documents, or the reuse check would restart for nothing.
    std::sort(normalized.begin(), normalized.end());
    realtimeSessionFiles_ = normalized;
}

void DiagnosticsController::setRealtimeRequestFiles(const QStringList &files)
{
    QStringList normalized;
    normalized.reserve(files.size());
    for (const QString &file : files) {
        if (!file.isEmpty() && !normalized.contains(file)) {
            normalized.append(file);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    // AC-5 (batch-20261118): the REQUEST scope changes when the ACTIVE tab
    // changes, so a new scope must be asked for even when the session set (the
    // union of the open documents) did not move at all.
    if (normalized != realtimeRequestFiles_) {
        lspDiagnosticsRequested_ = false;
        symbolsRequested_ = false;
        lastDiagnosticsRequestFiles_.clear();
    }
    realtimeRequestFiles_ = normalized;
}

void DiagnosticsController::invalidateBufferAnalysis(const QString &path)
{
    if (path.isEmpty() || path != analysisFile_ || analysisInvalidated_) {
        return;
    }
    analysisInvalidated_ = true;
    analysisText_.clear();
    outline_.clear();
    invalidateFile(path);
}

void DiagnosticsController::setDocumentAnalysisAvailability(const QString &path, bool available,
                                                            const QString &reason)
{
    if (path.isEmpty()) {
        documentPath_.clear();
        documentReason_.clear();
        documentAvailable_ = true;
        return;
    }
    documentPath_ = path;
    documentAvailable_ = available;
    documentReason_ = available ? QString() : reason;
    invalidateView();
}

QString DiagnosticsController::realtimeReason() const
{
    // batch-20261116 L1.2: a document with NO analysis path is the first reason.
    // The watcher publishes it before it returns (a `.f` manifest / a `.md` has
    // no analyzer; a file above the byte cap is never read), so the panel can
    // print one explicit line instead of the historical blank "No diagnostics".
    if (!documentPath_.isEmpty() && !documentAvailable_) {
        return documentReason_.isEmpty()
            ? QCoreApplication::translate("VEdit", "this document has no analyzer")
            : documentReason_;
    }
    if (analysisFile_.isEmpty()) {
        return QString();
    }
    // batch-20261118 AC-2/AC-3: a FAILED session is the first attributable
    // reason - the document was not analysed because the session died, and the
    // status line carries the server's own last words.
    if (lspStarted_ && session_.state == BackendState::Failed) {
        const QString detail = session_.message.isEmpty()
            ? QCoreApplication::translate("VEdit", "no reason was reported")
            : session_.message;
        return QCoreApplication::translate(
                   "VEdit", "the language server session failed — %1")
            .arg(detail);
    }
    // AC-2: the server produced neither diagnostics nor symbols for this file.
    // That is "not analysed", never "0 problems"; name the likely causes.
    if (serverSilent()) {
        return QCoreApplication::translate(
            "VEdit",
            "the language server did not analyse this file — no build context / no "
            "response / the session failed");
    }
    // RM-17b: exclusion by the user's own top scope is authoritative and must be
    // NAMED; silently dropping the request left the panel blank.
    if (isScopeExcluded(analysisFile_)) {
        const QString prefix = QStringLiteral("outside-top:");
        const QString stored = notAnalyzedReason_.value(analysisFile_);
        const QString top = stored.startsWith(prefix) ? stored.mid(prefix.size()) : topScopeTop_;
        return QCoreApplication::translate(
                   "VEdit", "not analysed: %1 is outside the active top scope %2")
            .arg(QFileInfo(analysisFile_).fileName(), top);
    }
    const QString stored = notAnalyzedReason_.value(analysisFile_);
    if (stored == QStringLiteral("modified")) {
        return QCoreApplication::translate(
            "VEdit", "not analysed yet: the editor buffer has unsaved changes");
    }
    if (stored == QStringLiteral("pending")) {
        return QCoreApplication::translate("VEdit", "analysis is starting");
    }
    if (stored == QStringLiteral("server-silent")) {
        return QCoreApplication::translate(
            "VEdit",
            "the language server did not analyse this file — no build context / no "
            "response / the session failed");
    }
    return QString();
}

bool DiagnosticsController::serverSilent() const
{
    // AC-2 (batch-20261118): the analysed document is "server silent" when the
    // watcher flagged it (no publishDiagnostics after the bounded settle) OR the
    // outline request came back with no symbols while no set was ever published
    // for it. Only a real publication clears the state.
    if (analysisFile_.isEmpty() || !lspStarted_ || session_.state != BackendState::Ready) {
        return false;
    }
    if (notAnalyzedReason_.value(analysisFile_) == QStringLiteral("server-silent")) {
        return true;
    }
    // A completed round with neither a diagnostics publication nor symbols.
    return state_ != BackendState::Reparsing && navigation_ == 0 && outlineRequest_ == 0
        && lspDiagnostics_.isEmpty() && outline_.isEmpty()
        && !publishedDiagnosticsFiles_.contains(analysisFile_);
}

void DiagnosticsController::startLsp()
{
    if (!lsp_) {
        report_.lspMessage = QCoreApplication::translate("VEdit", "no LSP backend in this build");
        lastError_ = report_.lspMessage;
        return;
    }
    if (lsPath_.isEmpty() || workspaceRoot_.isEmpty()) {
        if (!toolchain_.verible.found()) {
            report_.lspMessage = toolchain_.missingMessage(ToolKind::Verible);
            lastError_ = toolchain_.userMessage();
        } else {
            report_.lspMessage =
                QCoreApplication::translate("VEdit", "LSP backend is not configured (no workspace root)");
            lastError_ = report_.lspMessage;
        }
        return;
    }
    QString error;
    lsp_->setProjectIndex(realtimeProjectIndex_);
    if (lsp_->start(lsPath_, workspaceRoot_, rulesPath_, files_, &error)) {
        runningProjectIndex_ = realtimeProjectIndex_;
        lspStarted_ = true;
        lastRestartsSeen_ = 0;
        buildContextPushed_ = false;
        ++lspStarts_;
        report_.lspMessage = QCoreApplication::translate("VEdit", "session started over %1 file(s)").arg(files_.size());
    } else {
        lastError_ = error;
        report_.lspMessage = error;
    }
}

void DiagnosticsController::startLints()
{
    report_.slangRequested = 0;
    if (!slang_ || !report_.slangAvailable) {
        if (!toolchain_.slang.found()) {
            report_.slangMessage = toolchain_.missingMessage(ToolKind::Slang);
            if (lastError_.isEmpty()) {
                lastError_ = toolchain_.userMessage();
            }
        } else {
            report_.slangMessage = slang_ ? slang_->description() + QStringLiteral(" is unavailable")
                                          : QCoreApplication::translate("VEdit", "no SLANG backend in this build");
        }
        return;
    }
    for (const QString &file : files_) {
        // RM-17b ruling: the per-file layer skips excluded files ENTIRELY - no
        // request, so a zero-row answer can never be retained as Clean.
        if (isScopeExcluded(file)) {
            continue;
        }
        QString error;
        const int ticket = slang_->startLint(file, &error);
        if (ticket > 0) {
            lints_.append(LintTicket{ticket, file});
            ++report_.slangRequested;
        } else if (report_.slangMessage.isEmpty()) {
            report_.slangMessage = error;
        }
    }
}

void DiagnosticsController::clearLspResults()
{
    // Other sessions' retained verdicts and the independent project pass stay valid.
    for (const QString &file : publishedDiagnosticsFiles_) {
        clearRetainedRows(file);
        lowerAnalyzed_.remove(file);
    }
    diagnostics_.removeIf([this](const DiagnosticEntry &entry) {
        return publishedDiagnosticsFiles_.contains(entry.file);
    });
    if (!analysisInvalidated_ && !isScopeExcluded(analysisFile_)) {
        markNotAnalyzed(analysisFile_, QStringLiteral("pending"));
    }
    publishedDiagnosticsFiles_.clear();
    outline_.clear();
    // Drain old replies without applying them to the new process's view.
    for (DiagnosticTicket &ticket : lspDiagnostics_) {
        ticket.file.clear();
    }
    symbolsRequested_ = false;
    lspDiagnosticsRequested_ = false;
    bufferApplied_ = false;
    bufferDiagnosticsSeen_ = false;
    serverSilentSinceMs_ = 0;
}

void DiagnosticsController::sampleLspSession()
{
    if (!lsp_ || !lspStarted_) {
        return;
    }
    SessionSnapshot next = lsp_->status();
    next.state = backendState(next.state);
    const bool failed = next.state == BackendState::Failed;
    const bool stateChanged = next.state != session_.state;
    const bool restarted = next.state == BackendState::Ready && next.restarts != lastRestartsSeen_;
    const bool recovered = next.state == BackendState::Ready && session_.state == BackendState::Failed;
    const bool healthChanged = stateChanged || restarted || (failed &&
        (next.message != session_.message || next.restarts != session_.restarts));
    if (!healthChanged && state_ != BackendState::Reparsing) {
        return;
    }
    if ((failed && stateChanged) || restarted || recovered) {
        clearLspResults();
        state_ = BackendState::Reparsing;
    }
    if ((failed && stateChanged) || restarted) {
        buildContextPushed_ = false;
    }
    if (restarted) {
        lastRestartsSeen_ = next.restarts;
    }
    if (restarted || recovered) {
        lastError_.clear();
    }
    session_ = std::move(next);
    report_.lspFilesOpened = session_.filesOpened;
    report_.lspReceivedWholeSet = session_.filesOpened == files_.size()
        && session_.filesExpected == files_.size();
    if (healthChanged || !session_.message.isEmpty()) {
        report_.lspMessage = session_.message;
    }
    if (failed) {
        lastError_ = session_.message;
    }
    if (healthChanged) {
        invalidateView();
    }
}

void DiagnosticsController::refresh()
{
    // T8: the project pass is polled independently of the LSP reparse state -
    // a single-file diagnostic must never wait for the pass (AC-2.6).
    pollProjectPass();
    sampleLspSession();
    if (lsp_ && outlineRequest_ != 0
        && (analysisInvalidated_ || !symbolsRequested_ || outlineAnalysis_ != analysisCount_)
        && !lsp_->poll(outlineRequest_).pending) {
        outlineRequest_ = 0;
    }
    if (analysisInvalidated_ || state_ != BackendState::Reparsing) {
        // R2 / one count: a finished project pass (or an edited file falling
        // back) changes the merged view while the LSP is not reparsing. The
        // user-visible session count must follow it, not stay at the lower
        // layer's last number.
        // T9: a per-file edit moves the count too, so the combined token drives
        // this - never the structural revision alone.
        if (changeSerial_ != lastSessionMessageRevision_) {
            composeSessionMessage();
        }
        // batch-20261118 AC-2: a bounded re-ask for a document the server has
        // published nothing for. It runs OUTSIDE the analysis round, so the
        // "not analysed" state is visible immediately (a late publish - e.g.
        // right after the AC-1 build context - is still picked up).
        if (!analysisInvalidated_ && session_.state == BackendState::Ready) {
            pollServerSilence();
        } else {
            collectLspDiagnostics();
        }
        return;
    }

    if (lsp_ && lspStarted_) {
        if (session_.state == BackendState::Ready) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            // batch-20261118 AC-1: the project's include dirs / defines / build
            // files reach slang-server through `workspace/executeCommand` once
            // the session is Ready (and again after every re-selection, which
            // resets this flag). A rejection is a visible message, never
            // silence; a success re-arms both requests so the context's fresh
            // analysis is picked up.
            if (!buildContextPushed_) {
                QString contextError;
                if (lsp_->pushBuildContext(&contextError)) {
                    buildContextPushed_ = true;
                    lspDiagnosticsRequested_ = false;
                    symbolsRequested_ = false;
                    serverSilentSinceMs_ = 0;
                } else if (report_.lspMessage.isEmpty()) {
                    report_.lspMessage = contextError;
                }
            }
            // T118: push the EDITOR BUFFER into the session before asking for
            // diagnostics, so what gets linted is the text the user sees.
            if (!analysisFile_.isEmpty() && !bufferApplied_ && files_.contains(analysisFile_)
                && lspDiagnostics_.isEmpty() && outlineRequest_ == 0) {
                QString bufferError;
                if (lsp_->changeBuffer(analysisFile_, analysisText_, &bufferError)) {
                    bufferApplied_ = true;
                    bufferAppliedMs_ = now;
                    // batch-20261117 (AC-4): the status read at the top of this
                    // tick predates the didChange, so its generation is one
                    // command stale (a publish that raced in with the PRE-edit
                    // rows may already have moved it). Re-read AFTER the command
                    // and only then record the floor the settle window must
                    // clear: a poll at or below it is still the pre-edit set.
                    bufferGenerationAtApply_ = lsp_->status().generation;
                    bufferDiagnosticsSeen_ = false;
                    // Re-ask on the next tick: the server publishes the new
                    // diagnostics asynchronously and the request returns the
                    // CURRENT snapshot immediately.
                    lspDiagnosticsRequested_ = false;
                } else {
                    lspDiagnosticsRequested_ = true;
                    report_.lspMessage = bufferError;
                }
            }

            // verible publishes asynchronously: keep re-asking for the snapshot
            // after a buffer change until it arrives (bounded window), because a
            // request returns the CURRENT snapshot immediately.
            const bool bufferSettling =
                bufferApplied_ && !bufferDiagnosticsSeen_ && !analysisFile_.isEmpty()
                && now - bufferAppliedMs_ < kBufferSettleMs;
            if (bufferSettling) {
                lspDiagnosticsRequested_ = false;
            }
            const bool awaitingBuffer = !analysisFile_.isEmpty() && !bufferApplied_ && files_.contains(analysisFile_);
            if (!awaitingBuffer && !lspDiagnosticsRequested_ && lspDiagnostics_.isEmpty()) {
                lspDiagnosticsRequested_ = true;
                // batch-20261116 L1.1: the request set is the REALTIME session
                // set (current file + level-1 closure, bounded), never `files_`
                // as a stand-in for the project. The previous code looped over
                // the whole project because setFileSet() had put it in `files_`.
                lastDiagnosticsRequestFiles_.clear();
                // batch-20261118 AC-5: the REQUEST scope is the active
                // document's one-level closure, never the union of every open
                // tab. The session may still hold the union (reuse); only the
                // active closure is asked for and considered. An empty scope
                // keeps the historical session-wide request.
                const QStringList &requestScope =
                    realtimeRequestFiles_.isEmpty() ? files_ : realtimeRequestFiles_;
                for (const QString &file : requestScope) {
                    if (isScopeExcluded(file)) {
                        continue; // RM-17b: an excluded file is not requested
                    }
                    const quint64 id = lsp_->requestDiagnostics(file);
                    if (id != 0) {
                        lspDiagnostics_.append(DiagnosticTicket{id, file, analysisCount_, editSeq_.value(file)});
                        lastDiagnosticsRequestFiles_.append(file);
                    }
                }
                diagnosticsRequestCount_ +=
                    static_cast<quint64>(lastDiagnosticsRequestFiles_.size());
            }

            if (!awaitingBuffer && !analysisFile_.isEmpty() && !symbolsRequested_ && outlineRequest_ == 0) {
                outlineRequest_ = lsp_->requestSymbols(analysisFile_);
                outlineAnalysis_ = analysisCount_;
                symbolsRequested_ = outlineRequest_ != 0;
            }
        }
    }
    if (!lspStarted_ && report_.lspMessage.isEmpty()) {
        report_.lspMessage = QCoreApplication::translate("VEdit", "the LSP session did not start");
    }

    // The timer that requested a user action owns its reply. Automatic outline
    // polling must never consume definition, references, or formatting results.
    if (lsp_ && symbolsRequested_ && outlineRequest_ != 0) {
        const LspPolled polled = lsp_->poll(outlineRequest_);
        if (!polled.pending) {
            outlineRequest_ = 0;
            if (outlineAnalysis_ == analysisCount_ && polled.ready
                && polled.kind == ResponseKind::Symbols) {
                outline_ = polled.symbols;
            }
        }
    }
    collectLints();
    collectLspDiagnostics();
    finaliseState();
}

void DiagnosticsController::collectLints()
{
    if (lints_.isEmpty()) {
        return;
    }
    QVector<LintTicket> pending;
    for (const LintTicket &ticket : lints_) {
        if (isScopeExcluded(ticket.file)) {
            continue; // RM-17b: a scope change voids the in-flight lower result
        }
        LspPolled polled = slang_ ? slang_->pollLint(ticket.id) : LspPolled::failure(
                                            QCoreApplication::translate("VEdit", "no SLANG backend"));
        if (polled.pending) {
            pending.append(ticket);
            continue;
        }
        ++report_.slangCompleted;
        if (polled.failed) {
            ++report_.slangFailures;
            if (report_.slangMessage.isEmpty()) {
                report_.slangMessage = polled.error;
            }
            continue;
        }
        for (DiagnosticEntry entry : polled.diagnostics) {
            entry.file = ticket.file;
            if (entry.source.isEmpty()) {
                entry.source = slang_ ? slang_->description() : QStringLiteral("slang");
            }
            // T64: `SlangBackend::pollLint()` answers in the C ABI's VdDiagnostic
            // scale (0 error, 1 warning, ...), while the LSP leg below already
            // speaks the display scale (1 error, 2 warning, ...). Normalize the
            // CLI row HERE, at the collection boundary, so `diagnostics_` (and
            // the retained per-file store copied from it) carries ONE scale: the
            // panel, the squiggles, the file dot and the merge key then cannot
            // paint the same warning differently in the two layers.
            diagnostics_.append(toDisplayEntry(entry));
        }
        // RM-9: keep this file's rows in the retained per-file store (the SAME
        // rows the merged view reads) and lift the "pending" mark.
        retainLowerRows(ticket.file);
        clearNotAnalyzed(ticket.file);
    }
    lints_ = pending;
    if (!pending.isEmpty() || !diagnostics_.isEmpty()) {
        invalidateView();
    }
}

void DiagnosticsController::collectLspDiagnostics()
{
    if (lspDiagnostics_.isEmpty() || !lsp_) {
        return;
    }
    QVector<DiagnosticTicket> pending;
    for (const DiagnosticTicket &ticket : lspDiagnostics_) {
        LspPolled polled = lsp_->poll(ticket.id);
        if (polled.pending) {
            pending.append(ticket);
            continue;
        }
        if (analysisInvalidated_ || ticket.file.isEmpty() || ticket.analysis != analysisCount_
            || ticket.edit != editSeq_.value(ticket.file) || isScopeExcluded(ticket.file)
            || (ticket.file != analysisFile_ && notAnalyzedReason_.value(ticket.file) == QStringLiteral("modified"))) {
            continue;
        }
        if (polled.failed) {
            if (report_.lspMessage.isEmpty()) {
                report_.lspMessage = polled.error;
            }
            continue;
        }
        if (!polled.published) {
            // AC-2 (batch-20261118): the server has NO diagnostics entry for
            // this document. An empty answer here used to be recorded as a
            // clean verdict - the false "no problems reported" the user saw.
            // Keep the explicit not-analysed state and re-ask inside a bounded
            // window, so a publish that lands just after the build context was
            // accepted (AC-1) is still picked up.
            if (notAnalyzedReason_.value(ticket.file) != QStringLiteral("server-silent")) {
                markNotAnalyzed(ticket.file, QStringLiteral("server-silent"));
            }
            if (ticket.file == analysisFile_) {
                // The reply ends the buffer-settle wait. Only a publication
                // can establish a verdict.
                bufferDiagnosticsSeen_ = true;
                if (serverSilentSinceMs_ == 0) {
                    serverSilentSinceMs_ = QDateTime::currentMSecsSinceEpoch();
                }
            }
            continue;
        }
        if (bufferApplied_ && ticket.file == analysisFile_) {
            // batch-20261117 (AC-4): a poll answers the CURRENT server snapshot,
            // so the first completed poll after didChange may still carry the
            // PRE-edit rows. Accept it only once the session's diagnostics
            // generation moved past the floor recorded when the buffer was
            // applied, or once the settle window expired (the existing bound at
            // which the round settles on whatever the server has). Otherwise
            // DROP the ticket: the next tick sees bufferSettling and re-requests,
            // so a publish that lands later in the window is still picked up.
            const bool generationMoved = session_.generation > bufferGenerationAtApply_;
            const bool windowExpired =
                QDateTime::currentMSecsSinceEpoch() - bufferAppliedMs_ >= kBufferSettleMs;
            if (!generationMoved && !windowExpired) {
                continue;
            }
            bufferDiagnosticsSeen_ = true;
        }
        publishedDiagnosticsFiles_.insert(ticket.file);
        if (ticket.file == analysisFile_) {
            serverSilentSinceMs_ = 0;
        }
        // One analysis = one truth per file: replace the previous entries of
        // this file instead of appending, so re-requesting diagnostics after a
        // buffer edit cannot duplicate rows.
        for (int index = diagnostics_.size() - 1; index >= 0; --index) {
            if (diagnostics_.at(index).file == ticket.file) {
                diagnostics_.removeAt(index);
            }
        }
        for (DiagnosticEntry entry : polled.diagnostics) {
            entry.file = ticket.file;
            if (entry.source.isEmpty()) {
                entry.source = QStringLiteral("verible");
            }
            diagnostics_.append(entry);
        }
        // The lower layer really evaluated this file. A zero-row answer is a
        // CLEAN verdict, not "unknown" - that is the green dot for the file the
        // user is looking at, independent of any project pass.
        lowerAnalyzed_.insert(ticket.file);
        // RM-9: retain the verdict per file. Retaining is the ONLY new store:
        // it is fed here, from the rows the merged view already reads, so the
        // dot/count/hover never compute a second answer.
        retainLowerRows(ticket.file);
        clearNotAnalyzed(ticket.file);
        invalidateView();
    }
    lspDiagnostics_ = pending;
}

void DiagnosticsController::pollServerSilence()
{
    if (analysisInvalidated_ || analysisFile_.isEmpty() || !lspStarted_ || !lsp_) {
        return;
    }
    if (!lspDiagnostics_.isEmpty()) {
        collectLspDiagnostics();
        return;
    }
    if (notAnalyzedReason_.value(analysisFile_) != QStringLiteral("server-silent")) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (serverSilentSinceMs_ == 0 || now - serverSilentSinceMs_ > kServerSilentReaskMs) {
        return; // the window is over: the not-analysed state stands
    }
    const QStringList &scope = realtimeRequestFiles_.isEmpty() ? files_
                                                               : realtimeRequestFiles_;
    for (const QString &file : scope) {
        if (isScopeExcluded(file)) {
            continue;
        }
        const quint64 id = lsp_->requestDiagnostics(file);
        if (id != 0) {
            lspDiagnostics_.append(DiagnosticTicket{id, file, analysisCount_, editSeq_.value(file)});
        }
    }
    collectLspDiagnostics();
}

void DiagnosticsController::pollProjectPass()
{
    if (passJob_ == 0 || !passBackend_) {
        return;
    }
    LintPassSnapshot progress = passBackend_->progress(passJob_);
    if (progress.total <= 0) {
        progress.total = passFiles_;
    }
    if (progress.tool.isEmpty()) {
        progress.tool = passBackend_->toolName();
    }
    pass_ = progress;
    if (passFiles_ > kLargePassWarningFiles && progress.running()) {
        // C11: over the interactive budget stays visible for the whole pass.
        pass_.message = QStringLiteral("large pass: %1 files — cancel is available (%2/%3)")
                            .arg(passFiles_)
                            .arg(progress.done)
                            .arg(progress.total);
    }
    if (progress.running() || progress.phase == LintPhase::Idle) {
        return;
    }
    LspPolled result = passBackend_->poll(passJob_);
    if (result.pending) {
        return;
    }
    if (result.failed) {
        pass_.phase = LintPhase::Failed;
        if (pass_.tool.isEmpty()) {
            pass_.tool = passBackend_->toolName();
        }
        pass_.message = result.error;
        lastError_ = result.error;
    } else {
        // R2: the finished pass becomes the upper layer for exactly the files it
        // linted; rows of files it skipped (open documents) keep their previous
        // verdict until those files are edited.
        replaceUpperLayer(passLintedFiles_, result.diagnostics);
        if (progress.phase == LintPhase::Cancelled) {
            pass_.phase = LintPhase::Cancelled;
            pass_.message = lintCancelledMessage(pass_.done, pass_.total);
        } else {
            pass_.phase = LintPhase::Finished;
            // The Rust pass summary names every file whose engine failed and
            // why (e.g. "1 file(s) failed: bad.sv (terminated by signal 11)").
            // A finished pass is not automatically a CLEAN pass: overwriting
            // that line with "N/M file(s) scanned" would destroy the only
            // witness and show a crashed file as having no problems.
            if (progress.message.contains(QStringLiteral("file(s) failed"))) {
                pass_.message = progress.message;
            } else {
                pass_.message = QCoreApplication::translate("VEdit", "%1/%2 file(s) scanned — %3 CLI diagnostics")
                                    .arg(pass_.done)
                                    .arg(pass_.total)
                                    .arg(cliDiagnostics().size());
            }
        }
    }
    passRunning_ = false;
    passBackend_->release(passJob_);
    passJob_ = 0;
    notePassTerminal();
}

void DiagnosticsController::finaliseState()
{
    const bool lspPending = lspStarted_ && session_.state == BackendState::Reparsing;
    const bool lspFailed = lspStarted_ && session_.state == BackendState::Failed;
    // T118: a buffer edit is not "done" until the server published the new
    // diagnostics (bounded by the 1.5 s grace) - otherwise the state turns Ready
    // and the panel stops polling before the edit's diagnostics exist.
    const bool bufferPublishing =
        bufferApplied_ && !bufferDiagnosticsSeen_ && !analysisFile_.isEmpty()
        && QDateTime::currentMSecsSinceEpoch() - bufferAppliedMs_ <= kBufferSettleMs;
    const bool awaitingBuffer = lspStarted_ && session_.state == BackendState::Ready
        && !analysisFile_.isEmpty() && files_.contains(analysisFile_) && !bufferApplied_
        && !lspDiagnosticsRequested_;
    if (lspPending || !lints_.isEmpty()
        || (!lspFailed && (awaitingBuffer || bufferPublishing || outlineRequest_ != 0
                           || !lspDiagnostics_.isEmpty()))) {
        return;
    }

    const bool lspReady = !lspFailed && lspStarted_ && report_.lspReceivedWholeSet;
    const bool slangComplete =
        report_.slangAvailable && report_.slangRequested > 0
        && report_.slangCompleted >= report_.slangRequested && report_.slangFailures == 0;

    if (!lspReady && !slangComplete) {
        state_ = BackendState::Failed;
    } else if (!lspReady || !slangComplete) {
        state_ = BackendState::Degraded;
    } else {
        state_ = BackendState::Ready;
    }
    if (!toolchain_.missing().isEmpty() && lastError_.isEmpty()) {
        lastError_ = toolchain_.userMessage();
    }
    if (state_ == BackendState::Failed && lastError_.isEmpty()) {
        lastError_ = report_.lspMessage;
    }
    if (state_ == BackendState::Degraded && lastError_.isEmpty()) {
        lastError_ = lspReady ? report_.slangMessage : report_.lspMessage;
    }
    composeSessionMessage();
}

void DiagnosticsController::composeSessionMessage()
{
    // ONE count: the same merged view every other surface reads. The old code
    // wrote `diagnostics_.size()` (lower layer only) here while the panel
    // appended the merged count, so the status line and the header could
    // disagree by construction (R2: "不得两套各算一遍").
    // batch-20261115 AC-3.2: the status line is a REALTIME surface, so with an
    // active document it reports that document's rows - the same view the panel
    // and the squiggles show. Without one it keeps the whole merged count.
    const int mergedCount = analysisFile_.isEmpty()
        ? mergedDiagnosticCount()
        : static_cast<int>(mergedDiagnosticsForFile(analysisFile_).size());
    session_.diagnostics = mergedCount;
    lastSessionMessageRevision_ = changeSerial_;
    if (lspStarted_ && session_.state == BackendState::Failed) {
        return;
    }
    session_.message = QStringLiteral("%1: lsp %2/%3 file(s), diagnostics %4")
                           .arg(describeState(state_))
                           .arg(report_.lspFilesOpened)
                           .arg(files_.size())
                           .arg(mergedCount);
    // AC-5.8: a registered waiver is a CLI-engine input; the language-server
    // rows cannot carry one through the C ABI. Name that side (never a silent
    // disagreement between the pass and the server rows).
    session_.message += waiverScopeNote();
}

quint64 DiagnosticsController::requestDefinition(const QString &file, int line, int character)
{
    navigation_ = lsp_ ? lsp_->requestDefinition(file, line, character) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestReferences(const QString &file, int line, int character)
{
    navigation_ = lsp_ ? lsp_->requestReferences(file, line, character) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestSymbols(const QString &file)
{
    navigation_ = lsp_ ? lsp_->requestSymbols(file) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestRename(const QString &file,
                                             int line,
                                             int character,
                                             const QString &newName)
{
    if (!lsp_) {
        lastError_ = QStringLiteral("no LSP backend is installed");
        return 0;
    }
    // B5 (item 4): the capability gate belongs to the REQUEST path, not only to
    // the menu. A backend whose table says rename is unsupported refuses here,
    // with its own reason, so no caller can turn a greyed item into a request.
    BackendCapabilities capabilities = lsp_->sessionCapabilities();
    if (capabilities.isEmpty()) {
        capabilities = lsp_->staticCapabilities();
    }
    const CapabilityRow *row = capabilities.row(QStringLiteral("rename"));
    // A NULL row is as unsupported as a row that says 0: "unknown" must never
    // reach the server, or the UI-grey/request-refuse invariant has a hole.
    if (row == nullptr || row->supported != 1) {
        if (row == nullptr) {
            lastError_ = QStringLiteral("%1 has no rename capability row")
                             .arg(lsp_->backendName());
        } else {
            lastError_ = row->reason.isEmpty()
                ? QStringLiteral("%1 does not provide rename").arg(lsp_->backendName())
                : row->reason;
        }
        return 0;
    }
    lastError_.clear();
    navigation_ = lsp_->requestRename(file, line, character, newName);
    if (navigation_ == 0) {
        lastError_ = QStringLiteral("%1 refused the rename request").arg(lsp_->backendName());
    }
    return navigation_;
}

quint64 DiagnosticsController::requestFormat(const QString &file)
{
    navigation_ = lsp_ ? lsp_->requestFormat(file) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestHover(const QString &file, int line, int character)
{
    navigation_ = lsp_ ? lsp_->requestHover(file, line, character) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestSignatureHelp(const QString &file, int line, int character)
{
    navigation_ = lsp_ ? lsp_->requestSignatureHelp(file, line, character) : 0;
    return navigation_;
}

LspPolled DiagnosticsController::pollNavigation()
{
    if (!lsp_ || navigation_ == 0) {
        return LspPolled::failure(QCoreApplication::translate("VEdit", "no navigation request in flight"));
    }
    LspPolled polled = lsp_->poll(navigation_);
    if (polled.pending) {
        return polled;
    }
    if (polled.ready && polled.kind == ResponseKind::Symbols) {
        outline_ = polled.symbols;
    }
    navigation_ = 0;
    return polled;
}


// --- T8: project-wide lint pass -------------------------------------------

void DiagnosticsController::setProjectLintBackend(std::unique_ptr<ProjectLintBackend> backend)
{
    if (passRunning_ && passBackend_ && passJob_ != 0) {
        passBackend_->cancel(passJob_);
        passBackend_->release(passJob_);
    }
    passBackend_ = std::move(backend);
    passJob_ = 0;
    passRunning_ = false;
    if (!passBackend_) {
        pass_ = LintPassSnapshot{};
    }
}

void DiagnosticsController::setOpenFiles(const QStringList &files)
{
    openFiles_ = files;
}

bool DiagnosticsController::startProjectPass(const LintPassRequest &request, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (passRunning_) {
        if (error != nullptr) {
            *error = QStringLiteral("a project lint pass is already running");
        }
        return false;
    }
    // A-15: record the request BEFORE the backend checks. A first run that fails
    // at `resolve` (the tool is not installed yet) must still leave a valid
    // request behind, or Re-run — the documented recovery path — would refuse
    // with "no project lint pass has run yet" and the button would be a no-op.
    //
    // RM-11: this is the LAST boundary before the engine. The request builders
    // already ask the language registry, but a `.f` manifest / note / build
    // script that somehow arrives here must be EXCLUDED - never handed to a
    // HDL parser (the real-machine symptom was a syntax error on the filelist).
    // The rule stays positive: only a language with the Lint capability is a
    // candidate; everything else is recorded with the reason.
    passNonSource_.clear();
    QStringList requestedFiles;
    requestedFiles.reserve(request.files.size());
    for (const QString &file : request.files) {
        // RM-17: with a named top the pass covers only files reachable from it
        // (the SAME set the module tree derived and the dots show as excluded).
        if (isScopeExcluded(file)) {
            continue;
        }
        if (vedit::workspace::project::LanguageRegistry::instance().supports(
                file, vedit::workspace::project::Capability::Lint)) {
            requestedFiles.append(file);
        } else {
            passNonSource_.append(file);
        }
    }
    LintPassRequest effective = request;
    effective.files = requestedFiles;
    passRequest_ = effective;
    if (!passBackend_) {
        pass_ = LintPassSnapshot{};
        pass_.phase = LintPhase::Failed;
        pass_.stage = QStringLiteral("resolve");
        pass_.message = QStringLiteral("no lint backend is installed");
        if (error != nullptr) {
            *error = pass_.message;
        }
        return false;
    }
    if (!passBackend_->available()) {
        pass_ = LintPassSnapshot{};
        pass_.phase = LintPhase::Failed;
        pass_.tool = passBackend_->toolName();
        pass_.stage = QStringLiteral("resolve");
        pass_.message = QStringLiteral("%1: the lint tool could not be resolved").arg(pass_.tool);
        lastError_ = pass_.message;
        if (error != nullptr) {
            *error = pass_.message;
        }
        return false;
    }

    // C8: snapshot the open files NOW; later opens do not change this pass, but
    // their LSP diagnostics keep flowing (the pass never touches diagnostics_).
    const QStringList skipped = openFiles_;
    QStringList files;
    files.reserve(effective.files.size());
    for (const QString &file : effective.files) {
        if (!skipped.contains(file)) {
            files.append(file);
        }
    }
    passSkipped_ = skipped;
    passFiles_ = files.size();
    passLintedFiles_ = files;
    // RM-9: a skipped document is NOT "clean". The pass cannot read its unsaved
    // buffer from disk, so the file is explicitly excluded until a fresh lower
    // analysis (or the next pass) covers it. A fresh retained verdict still wins
    // in statusValueForFile(), so a dirty buffer the LSP just analysed keeps its
    // real red/yellow/green.
    for (const QString &file : skipped) {
        markNotAnalyzed(file, QStringLiteral("modified"));
    }
    passStartEditSeq_ = editCounter_;
    passAutomaticRequested_ = nextPassAutomatic_;
    nextPassAutomatic_ = false;
    if (files.isEmpty()) {
        pass_ = LintPassSnapshot{};
        pass_.phase = LintPhase::Finished;
        pass_.tool = passBackend_->toolName();
        pass_.message = passNonSource_.isEmpty()
            ? QStringLiteral(
                  "no files to lint (every requested file is a modified buffer; the language "
                  "server lints unsaved edits)")
            : QStringLiteral(
                  "no files to lint (%1 file(s) have no lint analyzer for their file type; "
                  "modified buffers are excluded)")
                  .arg(passNonSource_.size());
        lastError_ = pass_.message;
        if (error != nullptr) {
            *error = pass_.message;
        }
        return false;
    }

    LintPassRequest backendRequest = effective;
    backendRequest.files = files;
    QString startError;
    const quint64 job = passBackend_->start(backendRequest, &startError);
    if (job == 0) {
        pass_ = LintPassSnapshot{};
        pass_.phase = LintPhase::Failed;
        pass_.tool = passBackend_->toolName();
        pass_.stage = QStringLiteral("start");
        pass_.message = startError.isEmpty()
            ? QStringLiteral("%1: the pass could not be started").arg(pass_.tool)
            : startError;
        lastError_ = pass_.message;
        if (error != nullptr) {
            *error = pass_.message;
        }
        return false;
    }
    passJob_ = job;
    passRunning_ = true;
    pass_ = passBackend_->progress(job);
    if (pass_.total <= 0) {
        pass_.total = passFiles_;
    }
    if (pass_.tool.isEmpty()) {
        pass_.tool = passBackend_->toolName();
    }
    if (passFiles_ > kLargePassWarningFiles) {
        // C11: over the interactive budget, say so and keep cancel available.
        pass_.message = QStringLiteral("large pass: %1 files — cancel is available").arg(passFiles_);
    }
    return true;
}

bool DiagnosticsController::rerunProjectPass(QString *error)
{
    if (passRequest_.files.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no project lint pass has run yet");
        }
        return false;
    }
    return startProjectPass(passRequest_, error);
}

void DiagnosticsController::cancelProjectPass()
{
    if (!passRunning_ || passJob_ == 0 || !passBackend_) {
        return;
    }
    const quint64 job = passJob_;
    passBackend_->cancel(job);
    // True stop (AC-2.8): the job is released, no result is appended after this
    // point, the running flag is cleared and a rerun starts a fresh job.
    pass_.phase = LintPhase::Cancelled;
    pass_.message = lintCancelledMessage(pass_.done, pass_.total > 0 ? pass_.total : passFiles_);
    pass_.current.clear();
    passRunning_ = false;
    passJob_ = 0;
    passBackend_->release(job);
    notePassTerminal();
}

void DiagnosticsController::clearProjectDiagnostics()
{
    cliByFile_.clear();
    cliDiagnostics_.clear();
    cliFlatDirty_ = false;
    coveredFiles_.clear();
    lowerAnalyzed_.clear();
    // RM-9: the retained per-file store belongs to the project being left. A
    // switch must not paint the previous project's dots (the same rule RM-2
    // applies to the upper layer and to the analysis set).
    retainedLowerRows_.clear();
    retainedLowerEvaluated_.clear();
    notAnalyzedReason_.clear();
    projectFiles_.clear();
    projectFileSet_.clear();
    passSkipped_.clear();
    passLintedFiles_.clear();
    passFiles_ = 0;
    pass_ = LintPassSnapshot{};
    passRequest_ = LintPassRequest{};
    invalidateView();
}

QVector<DisplayDiagnostic> DiagnosticsController::displayDiagnostics() const
{
    ++workStats_.displayRebuilds;
    // R2: one layered view. A file with a live upper layer contributes ONLY its
    // project-pass rows (a clean covered file contributes none); every other
    // file contributes its lower (per-file/LSP) rows. `Both` can no longer
    // occur: stacking the two layers for one file is exactly what R2 forbids.
    QVector<DisplayDiagnostic> rows;
    QSet<QString> keys;
    for (auto bucket = cliByFile_.constBegin(); bucket != cliByFile_.constEnd(); ++bucket) {
        if (!coveredFiles_.contains(bucket.key())) {
            continue;
        }
        for (const DiagnosticEntry &entry : bucket.value()) {
            const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
            if (keys.contains(key)) {
                continue;
            }
            keys.insert(key);
            DisplayDiagnostic row;
            // The upper layer arrives in the C ABI's CLI scale (0 error, 1
            // warning, 2/3 note); every display surface speaks the LSP scale.
            // Convert ONCE, here, or a project-pass ERROR would be painted as
            // information.
            row.entry = toDisplayEntry(entry);
            row.origin = DiagnosticOrigin::Cli;
            row.key = key;
            rows.append(row);
        }
    }
    for (const DiagnosticEntry &entry : diagnostics_) {
        if (coveredFiles_.contains(entry.file)) {
            continue;
        }
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Lsp);
        if (keys.contains(key)) {
            continue;
        }
        keys.insert(key);
        DisplayDiagnostic row;
        // The lower layer is ALREADY in the display scale: the CLI leg was
        // normalized by collectLints() when it entered `diagnostics_`, and the
        // LSP leg arrives in the LSP scale. Converting again here would shift a
        // warning to information - the asymmetric bug T64 fixed.
        row.entry = entry;
        row.origin = DiagnosticOrigin::Lsp;
        row.key = key;
        rows.append(row);
    }
    // AC-6.4: the LANES' rows (the VHDL lane's GHDL+VSG merge). They are a
    // LOWER-layer source: a file the finished project pass covers keeps the
    // pass's verdict (one file, one truth), and the rows are ALREADY in the
    // display scale (origin Lsp is the scale marker here, not the producer -
    // `entry.source` names it: "ghdl" / "vsg").
    for (auto it = laneRows_.constBegin(); it != laneRows_.constEnd(); ++it) {
        for (const DiagnosticEntry &entry : it.value()) {
            if (coveredFiles_.contains(entry.file)) {
                continue;
            }
            const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Lsp);
            if (keys.contains(key)) {
                continue;
            }
            keys.insert(key);
            DisplayDiagnostic row;
            row.entry = entry;
            row.origin = DiagnosticOrigin::Lsp;
            row.key = key;
            rows.append(row);
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const DisplayDiagnostic &left, const DisplayDiagnostic &right) {
                  if (left.entry.file != right.entry.file) {
                      return left.entry.file < right.entry.file;
                  }
                  if (left.entry.line != right.entry.line) {
                      return left.entry.line < right.entry.line;
                  }
                  if (left.entry.column != right.entry.column) {
                      return left.entry.column < right.entry.column;
                  }
                  return left.entry.severity < right.entry.severity;
              });
    return rows;
}

bool DiagnosticsController::hasUpperLayer(const QString &file) const
{
    return coveredFiles_.contains(file);
}

QStringList DiagnosticsController::upperLayerFiles() const
{
    // T9: the covered SET is the answer; scanning the row store (and a
    // contains() per row) made this O(rows x files) on every 400 ms poll.
    QStringList files(coveredFiles_.begin(), coveredFiles_.end());
    files.sort();
    return files;
}

QVector<DiagnosticEntry> DiagnosticsController::upperLayerDiagnostics() const
{
    QVector<DiagnosticEntry> rows;
    for (auto bucket = cliByFile_.constBegin(); bucket != cliByFile_.constEnd(); ++bucket) {
        if (!coveredFiles_.contains(bucket.key())) {
            continue;
        }
        for (const DiagnosticEntry &entry : bucket.value()) {
            rows.append(toDisplayEntry(entry));
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
        if (left.file != right.file) {
            return left.file < right.file;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        return left.column < right.column;
    });
    return rows;
}

void DiagnosticsController::markFileEdited(const QString &file)
{
    if (file.isEmpty()) {
        return;
    }
    editSeq_.insert(file, ++editCounter_);
    invalidateBufferAnalysis(file);
    // T9: only a change of the FILE'S VISIBLE STATE is a view event. The first
    // keystroke of a covered file drops its upper layer and marks it modified;
    // every later keystroke changes only the edit sequence, which no surface
    // reads. Bumping the view for those would make every consumer rebuild the
    // project for a state that did not move.
    bool visibleChange = false;
    if (coveredFiles_.remove(file)) {
        // R2: an edited file's upper rows are void. Drop them from the store as
        // well, so a later pass cannot resurrect a stale row by dedupe key.
        dropUpperRows(file);
        visibleChange = true;
    }
    // RM-9 / R2: the edit voids the LOWER verdict too - it was computed on the
    // text from BEFORE this edit. Retaining it would keep a green dot alive on a
    // file whose verdict no longer describes its buffer. The file shows the
    // explicit "excluded" state until the next analysis lands (active buffer) or
    // the next pass covers it.
    if (retainedLowerEvaluated_.contains(file) || retainedLowerRows_.contains(file)) {
        clearRetainedRows(file);
        diagnostics_.removeIf([&file](const DiagnosticEntry &entry) { return entry.file == file; });
        lowerAnalyzed_.remove(file);
        visibleChange = true;
    }
    if (notAnalyzedReason_.value(file) != QStringLiteral("modified")) {
        markNotAnalyzed(file, QStringLiteral("modified"));
        visibleChange = true;
    }
    if (visibleChange) {
        // ONE file moved: the publisher repaints that file, never the project.
        invalidateFile(file);
    }
}

void DiagnosticsController::dropUpperRows(const QString &file)
{
    // T9: the upper store is per file, so this is O(that file's rows), not
    // O(the project's rows). The counter records the exact work for the
    // edit-jank benchmark.
    const auto bucket = cliByFile_.constFind(file);
    if (bucket == cliByFile_.constEnd()) {
        return;
    }
    workStats_.upperRowsScannedOnEdit += static_cast<quint64>(bucket.value().size());
    cliByFile_.erase(bucket);
    cliFlatDirty_ = true;
    invalidateFile(file);
}

/// The ONE classification rule for a set of already-collected rows: error
/// dominates, then warning, then clean. Shared by statusValueForFile() (one
/// file) and fileStatuses() (the whole map), so the dot and the map cannot
/// diverge. It is only called when a layer produced a verdict, so an empty row
/// set means CLEAN - exactly the green dot.
static int classifyStatusRows(const QVector<DiagnosticEntry> &rows, bool cliScale)
{
    bool error = false;
    bool warning = false;
    for (const DiagnosticEntry &entry : rows) {
        const int severity = cliScale ? canonicalSeverity(entry.severity, DiagnosticOrigin::Cli)
                                      : entry.severity;
        error = error || severity == 1;
        warning = warning || severity == 2;
    }
    if (error) {
        return static_cast<int>(vedit::common::FileStatus::Error);
    }
    if (warning) {
        return static_cast<int>(vedit::common::FileStatus::Warning);
    }
    return static_cast<int>(vedit::common::FileStatus::Clean);
}

QVector<DiagnosticEntry> DiagnosticsController::mergedDiagnostics() const
{
    ++workStats_.mergedRebuilds;
    QVector<DiagnosticEntry> rows;
    QSet<QString> keys;
    for (auto bucket = cliByFile_.constBegin(); bucket != cliByFile_.constEnd(); ++bucket) {
        if (!coveredFiles_.contains(bucket.key())) {
            continue;
        }
        for (const DiagnosticEntry &entry : bucket.value()) {
            const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
            if (keys.contains(key)) {
                continue;
            }
            keys.insert(key);
            rows.append(toDisplayEntry(entry));
        }
    }
    // LOWER: the RETAINED per-file verdicts, for every file the upper layer
    // does not cover. This is the same store the dots and the panel read, so a
    // file that is not the active document still contributes its real rows -
    // the defect this row fixes was exactly that it did not.
    for (auto it = retainedLowerRows_.constBegin(); it != retainedLowerRows_.constEnd(); ++it) {
        const QString &file = it.key();
        if (coveredFiles_.contains(file) || !retainedLowerEvaluated_.contains(file)) {
            continue;
        }
        for (const DiagnosticEntry &entry : it.value()) {
            const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Lsp);
            if (keys.contains(key)) {
                continue;
            }
            keys.insert(key);
            rows.append(entry);
        }
    }
    // AC-6.4: the LANES' rows, under the same rule as every other lower source
    // (a covered file keeps the pass's verdict). This is what makes the panel,
    // the session count and `statusValueForFile()` agree about a VHDL file.
    for (auto it = laneRows_.constBegin(); it != laneRows_.constEnd(); ++it) {
        for (const DiagnosticEntry &entry : it.value()) {
            if (coveredFiles_.contains(entry.file)) {
                continue;
            }
            const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Lsp);
            if (keys.contains(key)) {
                continue;
            }
            keys.insert(key);
            rows.append(entry);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
        if (left.file != right.file) {
            return left.file < right.file;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.column != right.column) {
            return left.column < right.column;
        }
        return left.severity < right.severity;
    });
    return rows;
}

QVector<DiagnosticEntry> DiagnosticsController::mergedDiagnosticsForFile(const QString &file) const
{
    QVector<DiagnosticEntry> rows;
    if (file.isEmpty()) {
        return rows;
    }
    const bool upper = coveredFiles_.contains(file);
    QSet<QString> keys;
    const auto take = [&](const DiagnosticEntry &entry, DiagnosticOrigin origin) {
        const QString key = diagnosticMergeKey(entry, origin);
        if (keys.contains(key)) {
            return;
        }
        keys.insert(key);
        rows.append(origin == DiagnosticOrigin::Cli ? toDisplayEntry(entry) : entry);
    };
    if (upper) {
        // T9: one bucket lookup, not a scan of every project row.
        for (const DiagnosticEntry &entry : cliByFile_.value(file)) {
            take(entry, DiagnosticOrigin::Cli);
        }
    } else if (!laneRowsForFile(laneRows_, file).isEmpty()) {
        // AC-6.4: a lane evaluated this file (GHDL+VSG). Its rows are already in
        // the display scale, so they are taken verbatim.
        for (const DiagnosticEntry &entry : laneRowsForFile(laneRows_, file)) {
            take(entry, DiagnosticOrigin::Lsp);
        }
    } else if (retainedLowerEvaluated_.contains(file)) {
        // The retained per-file verdict - the SAME store the dot reads. A file
        // edited after its last analysis has no retained verdict (markFileEdited
        // drops it), so it contributes no rows and no stale squiggle survives an
        // edit; it shows the explicit "not analysed / excluded" state instead.
        for (const DiagnosticEntry &entry : retainedLowerRows_.value(file)) {
            take(entry, DiagnosticOrigin::Lsp);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.column != right.column) {
            return left.column < right.column;
        }
        return left.severity < right.severity;
    });
    return rows;
}

void DiagnosticsController::setLaneDiagnostics(const QString &source,
                                               const QVector<DiagnosticEntry> &rows)
{
    if (source.isEmpty()) {
        return;
    }
    if (rows.isEmpty()) {
        // An empty set CLEARS the source: a re-lint that found nothing must not
        // leave the previous rows behind (they would read as a live verdict).
        if (laneRows_.remove(source) > 0) {
            invalidateView();
        }
        return;
    }
    // Same source => REPLACE (a lane re-analyses; it never stacks).
    laneRows_.insert(source, rows);
    invalidateView();
}

DiagnosticsController::FileStatus DiagnosticsController::statusForFile(const QString &file) const
{
    return static_cast<FileStatus>(statusValueForFile(file));
}

bool DiagnosticsController::isScopeExcluded(const QString &file) const
{
    return !topScopeTop_.isEmpty() && scopeExcluded_.contains(QDir::cleanPath(file));
}

int DiagnosticsController::statusValueForFile(const QString &file) const
{
    if (file.isEmpty()) {
        return static_cast<int>(FileStatus::Unknown);
    }
    // 0) RM-17b RULING: while a top is active, exclusion by the user's own
    //    instruction is AUTHORITATIVE for the dot. It outranks the upper project
    //    layer and the lower per-file verdict. This does not contradict RM-9
    //    ruling 1 (a fresh verdict beats a stale one for a dirty buffer): ruling
    //    1 resolves STALENESS, this resolves EXCLUSION by explicit instruction,
    //    and an instruction outranks any verdict.
    if (isScopeExcluded(file)) {
        return static_cast<int>(FileStatus::NotAnalyzed);
    }
    // 1) UPPER layer: the project pass covered this file and the file was not
    //    edited since. Its rows are the verdict (empty = clean).
    if (coveredFiles_.contains(file)) {
        // T9: the file's own bucket, not a scan of every project row.
        return classifyStatusRows(cliByFile_.value(file), true);
    }
    // 2a) AC-6.4: a LANE's verdict (the VHDL lane's GHDL+VSG merge). The rows are
    //     already in the display scale, so `cliScale` is false. An empty answer
    //     is "no verdict", never "clean": a clean lane CLEARS its rows, so only a
    //     file a lane actually named reaches this branch.
    const QVector<DiagnosticEntry> laneRows = laneRowsForFile(laneRows_, file);
    if (!laneRows.isEmpty()) {
        return classifyStatusRows(laneRows, false);
    }
    // 2) A FRESH lower verdict (analysed after the last edit). This wins over
    //    the "excluded" mark: the user's rule is that a file with a real
    //    verdict shows it, dirty buffer or not (RM-9 ruling 1).
    if (retainedLowerEvaluated_.contains(file)) {
        return classifyStatusRows(retainedLowerRows_.value(file), false);
    }
    // 3) No trustworthy verdict since the last edit, or excluded from the pass.
    if (notAnalyzedReason_.contains(file)) {
        return static_cast<int>(FileStatus::NotAnalyzed);
    }
    // 4) In the project set but nothing analysed it yet (queued / pass still
    //    running): the explicit visible "not analysed" mark, never "no dot".
    if (projectFileSet_.contains(file)) {
        return static_cast<int>(FileStatus::NotAnalyzed);
    }
    // 5) Not part of any analysed set: no dot at all.
    return static_cast<int>(FileStatus::Unknown);
}

QVector<DiagnosticEntry> DiagnosticsController::lowerRowsForFile(const QString &file) const
{
    if (!retainedLowerEvaluated_.contains(file)) {
        return {}; // no trustworthy verdict (never analysed, or edited since)
    }
    return retainedLowerRows_.value(file);
}

void DiagnosticsController::retainLowerRows(const QString &file)
{
    if (file.isEmpty()) {
        return;
    }
    QVector<DiagnosticEntry> rows;
    for (const DiagnosticEntry &entry : diagnostics_) {
        if (entry.file == file) {
            rows.append(entry);
        }
    }
    retainedLowerRows_.insert(file, rows);
    retainedLowerEvaluated_.insert(file);
    invalidateFile(file);
}

void DiagnosticsController::clearRetainedRows(const QString &file)
{
    retainedLowerRows_.remove(file);
    retainedLowerEvaluated_.remove(file);
    invalidateFile(file);
}

void DiagnosticsController::markNotAnalyzed(const QString &file, const QString &reason)
{
    if (file.isEmpty()) {
        return;
    }
    notAnalyzedReason_.insert(file, reason);
    invalidateFile(file);
}

void DiagnosticsController::clearNotAnalyzed(const QString &file)
{
    if (notAnalyzedReason_.remove(file) > 0) {
        invalidateFile(file);
    }
}

void DiagnosticsController::setTopScope(const QString &top, const QStringList &analysed,
                                        const QStringList &excluded)
{
    // `analysed` is the publisher's positive half; see the header: the ONE
    // derivation this controller stores is the EXCLUDED set (with a top, a
    // requested file is analysed exactly when it is not excluded).
    Q_UNUSED(analysed);
    // Clear ONLY the marks this seam set: a "modified" mark belongs to the
    // buffer rule and must survive a scope change.
    const QString prefix = QStringLiteral("outside-top:");
    QStringList stale;
    for (auto it = notAnalyzedReason_.constBegin(); it != notAnalyzedReason_.constEnd(); ++it) {
        if (it.value().startsWith(prefix)) {
            stale.append(it.key());
        }
    }
    for (const QString &file : stale) {
        clearNotAnalyzed(file);
    }
    topScopeTop_ = top;
    scopeExcluded_.clear();
    if (!top.isEmpty()) {
        for (const QString &file : excluded) {
            const QString clean = QDir::cleanPath(file);
            scopeExcluded_.insert(clean);
            markNotAnalyzed(clean, prefix + top);
        }
    }
    invalidateView();
}

const QVector<DiagnosticEntry> &DiagnosticsController::cliDiagnostics() const
{
    rebuildCliFlat();
    return cliDiagnostics_;
}

const QVector<DiagnosticEntry> &DiagnosticsController::upperDiagnostics() const
{
    rebuildCliFlat();
    return cliDiagnostics_;
}

void DiagnosticsController::rebuildCliFlat() const
{
    if (!cliFlatDirty_) {
        return;
    }
    cliFlatDirty_ = false;
    cliDiagnostics_.clear();
    cliDiagnostics_.reserve(static_cast<int>(cliByFile_.size()));
    for (auto it = cliByFile_.constBegin(); it != cliByFile_.constEnd(); ++it) {
        cliDiagnostics_ += it.value();
    }
}

void DiagnosticsController::invalidateFile(const QString &file)
{
    if (file.isEmpty()) {
        return;
    }
    pendingFileChanges_.insert(file);
    countDirtyFiles_.insert(file);
    ++fileViewRevision_;
    ++changeSerial_;
}

int DiagnosticsController::mergedRowCountForFile(const QString &file) const
{
    // Same layering and same key as mergedDiagnostics()/mergedDiagnosticsForFile().
    QSet<QString> keys;
    const auto take = [&keys](const DiagnosticEntry &entry, DiagnosticOrigin origin) {
        keys.insert(diagnosticMergeKey(entry, origin));
    };
    if (coveredFiles_.contains(file)) {
        for (const DiagnosticEntry &entry : cliByFile_.value(file)) {
            take(entry, DiagnosticOrigin::Cli);
        }
        return keys.size();
    }
    const QVector<DiagnosticEntry> lane = laneRowsForFile(laneRows_, file);
    if (!lane.isEmpty()) {
        for (const DiagnosticEntry &entry : lane) {
            take(entry, DiagnosticOrigin::Lsp);
        }
        return keys.size();
    }
    if (retainedLowerEvaluated_.contains(file)) {
        for (const DiagnosticEntry &entry : retainedLowerRows_.value(file)) {
            take(entry, DiagnosticOrigin::Lsp);
        }
    }
    return keys.size();
}

int DiagnosticsController::upperLayerDiagnosticCount() const
{
    int total = 0;
    for (const QString &file : coveredFiles_) {
        total += cliByFile_.value(file).size();
    }
    return total;
}

int DiagnosticsController::mergedDiagnosticCount() const
{
    if (countsAllDirty_) {
        countsAllDirty_ = false;
        countDirtyFiles_.clear();
        mergedCountByFile_.clear();
        mergedCountTotal_ = 0;
        QSet<QString> files(coveredFiles_.begin(), coveredFiles_.end());
        for (auto it = retainedLowerEvaluated_.constBegin();
             it != retainedLowerEvaluated_.constEnd(); ++it) {
            files.insert(*it);
        }
        for (auto it = laneRows_.constBegin(); it != laneRows_.constEnd(); ++it) {
            for (const DiagnosticEntry &entry : it.value()) {
                files.insert(entry.file);
            }
        }
        for (const QString &file : files) {
            const int count = mergedRowCountForFile(file);
            mergedCountByFile_.insert(file, count);
            mergedCountTotal_ += count;
        }
    } else if (!countDirtyFiles_.isEmpty()) {
        for (const QString &file : countDirtyFiles_) {
            const int previous = mergedCountByFile_.value(file, 0);
            const int count = mergedRowCountForFile(file);
            if (count == 0) {
                mergedCountByFile_.remove(file);
            } else {
                mergedCountByFile_.insert(file, count);
            }
            mergedCountTotal_ += count - previous;
        }
        countDirtyFiles_.clear();
    }
    return mergedCountTotal_;
}

LivePublish DiagnosticsController::takeLivePublish()
{
    LivePublish publish;
    publish.viewRevision = viewRevision_;
    publish.fileRevision = fileViewRevision_;
    if (viewRevision_ != publishedViewRevision_) {
        // STRUCTURAL: the whole view may differ. One rebuild, one map.
        publishedViewRevision_ = viewRevision_;
        publishedFileRevision_ = fileViewRevision_;
        pendingFileChanges_.clear();
        publish.full = true;
        publish.statuses = fileStatuses();
        publish.mergedCount = mergedDiagnosticCount();
        // The display ROWS are only needed by a caller that lists them (the
        // DIAG-ROW dump); computing them here would be a second full merge on
        // every structural change. -1 = not computed.
        publish.displayCount = -1;
        publish.upperCount = upperLayerDiagnosticCount();
        ++workStats_.fullPublishes;
        workStats_.statusEntriesPublished += static_cast<quint64>(publish.statuses.size());
        return publish;
    }
    if (fileViewRevision_ != publishedFileRevision_) {
        publishedFileRevision_ = fileViewRevision_;
        if (pendingFileChanges_.isEmpty()) {
            return publish; // only internal bookkeeping moved
        }
        // PER-FILE: publish just the files whose verdict moved.
        publish.changedFiles = QStringList(pendingFileChanges_.begin(), pendingFileChanges_.end());
        publish.changedFiles.sort();
        pendingFileChanges_.clear();
        for (const QString &file : publish.changedFiles) {
            publish.statuses.insert(file, statusValueForFile(file));
        }
        publish.mergedCount = mergedDiagnosticCount();
        publish.upperCount = upperLayerDiagnosticCount();
        ++workStats_.deltaPublishes;
        workStats_.statusEntriesPublished += static_cast<quint64>(publish.changedFiles.size());
    }
    return publish;
}

QHash<QString, int> DiagnosticsController::fileStatuses() const
{
    QHash<QString, int> statuses;
    const bool scopeActive = !topScopeTop_.isEmpty();
    // UPPER: every covered file is clean until one of its rows says otherwise;
    // the rows are classified in ONE pass, never per file over every row.
    // RM-17b: an excluded file is NOT covered for status purposes - the scope
    // outranks both layers here exactly as it does in statusValueForFile().
    for (const QString &file : coveredFiles_) {
        if (scopeActive && scopeExcluded_.contains(file)) {
            continue;
        }
        statuses.insert(file, static_cast<int>(FileStatus::Clean));
    }
    for (auto bucket = cliByFile_.constBegin(); bucket != cliByFile_.constEnd(); ++bucket) {
        const QString &file = bucket.key();
        if (scopeActive && scopeExcluded_.contains(file)) {
            continue;
        }
        if (!coveredFiles_.contains(file)) {
            continue;
        }
        for (const DiagnosticEntry &entry : bucket.value()) {
            const int severity = canonicalSeverity(entry.severity, DiagnosticOrigin::Cli);
            if (severity == 1) {
                statuses.insert(file, static_cast<int>(FileStatus::Error));
            } else if (severity == 2
                       && statuses.value(file, 0) != static_cast<int>(FileStatus::Error)) {
                statuses.insert(file, static_cast<int>(FileStatus::Warning));
            }
        }
    }
    // LOWER: the retained per-file store - the same rows mergedDiagnostics()
    // reads, so the dot map and the editor/panel rows cannot disagree.
    for (auto it = retainedLowerEvaluated_.constBegin();
         it != retainedLowerEvaluated_.constEnd(); ++it) {
        if (coveredFiles_.contains(*it)) {
            continue;
        }
        if (scopeActive && scopeExcluded_.contains(*it)) {
            continue; // the scope outranks a fresh lower verdict (RM-17b ruling)
        }
        statuses.insert(*it, classifyStatusRows(retainedLowerRows_.value(*it), false));
    }
    // EXCLUDED / PENDING: visible "not analysed" marks (state 4), never "clean".
    for (auto it = notAnalyzedReason_.constBegin(); it != notAnalyzedReason_.constEnd(); ++it) {
        if (!statuses.contains(it.key())) {
            statuses.insert(it.key(), static_cast<int>(FileStatus::NotAnalyzed));
        }
    }
    for (const QString &file : projectFiles_) {
        if (!statuses.contains(file)) {
            statuses.insert(file, static_cast<int>(FileStatus::NotAnalyzed));
        }
    }
    // RM-17b: the excluded set is inserted LAST, so no stale row/verdict can
    // leave an excluded file looking analysed - one derivation, no exceptions.
    if (scopeActive) {
        for (const QString &file : scopeExcluded_) {
            statuses.insert(file, static_cast<int>(FileStatus::NotAnalyzed));
        }
    }
    return statuses;
}

void DiagnosticsController::replaceUpperLayer(const QStringList &files,
                                              const QVector<DiagnosticEntry> &entries)
{
    const QSet<QString> linted(files.begin(), files.end());
    QVector<DiagnosticEntry> fresh;
    QSet<QString> keys;
    for (const DiagnosticEntry &entry : entries) {
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
        if (keys.contains(key)) {
            continue; // the engine repeated itself: one row
        }
        keys.insert(key);
        fresh.append(entry);
    }
    // T9: the store is per file. Files this pass did not look at (skipped as
    // open documents) keep their previous verdict; the linted files' rows are
    // replaced by the fresh ones, grouped by their own file.
    for (const QString &file : files) {
        cliByFile_.remove(file);
    }
    for (const DiagnosticEntry &entry : fresh) {
        cliByFile_[entry.file].append(entry);
    }
    cliFlatDirty_ = true;

    QSet<QString> live;
    for (const QString &file : coveredFiles_) {
        if (!linted.contains(file)) {
            live.insert(file);
        }
    }
    for (const QString &file : files) {
        // A file edited at/after the pass start may have been read before or
        // after the edit: the verdict is not trustworthy, so it is not covered.
        if (editSeq_.value(file, 0) <= passStartEditSeq_) {
            live.insert(file);
        }
    }
    coveredFiles_ = live;
    // RM-9: a fresh pass verdict supersedes an "excluded / not analysed" mark.
    // RM-17b: EXCEPT the scope's own mark - a pass that never linted an excluded
    // file must not clear the reason it was skipped, or the next pass would
    // silently turn the hollow state into a verdict (the defect this row fixes).
    for (const QString &file : live) {
        if (isScopeExcluded(file)) {
            continue;
        }
        clearNotAnalyzed(file);
    }
    invalidateView();
}

void DiagnosticsController::notePassTerminal()
{
    ++projectPassSerial_;
    lastPassAutomatic_ = passAutomaticRequested_;
}

// --- T8: the FFI-backed project pass (real tools) -------------------------

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
namespace {

/// Borrowed view of a QStringList for one C ABI call.
struct BorrowedStrings {
    std::vector<QByteArray> storage;
    std::vector<char *> items;
    VdStringList list{0, nullptr};

    explicit BorrowedStrings(const QStringList &values)
    {
        storage.reserve(static_cast<std::size_t>(values.size()));
        for (const QString &value : values) {
            storage.push_back(value.toUtf8());
        }
        items.reserve(storage.size());
        for (QByteArray &value : storage) {
            items.push_back(value.data());
        }
        list.count = items.size();
        list.items = items.empty() ? nullptr : items.data();
    }
};

LintPhase phaseOf(int phase)
{
    switch (phase) {
    case 0:
        return LintPhase::Queued;
    case 1:
        return LintPhase::Running;
    case 2:
        return LintPhase::Finished;
    case 3:
        return LintPhase::Cancelled;
    default:
        return LintPhase::Failed;
    }
}

QString textOrEmpty(char *text)
{
    return text != nullptr ? QString::fromUtf8(text) : QString();
}

class FfiProjectLintBackend final : public ProjectLintBackend {
public:
    FfiProjectLintBackend()
        : service_(vd_service_create())
    {
    }
    ~FfiProjectLintBackend() override
    {
        if (service_ != nullptr) {
            vd_service_destroy(service_);
        }
    }
    FfiProjectLintBackend(const FfiProjectLintBackend &) = delete;
    FfiProjectLintBackend &operator=(const FfiProjectLintBackend &) = delete;

    bool available() const override { return service_ != nullptr; }
    QString toolName() const override
    {
        return engine_ == 1 ? QStringLiteral("verible-verilog-lint") : QStringLiteral("slang");
    }

    quint64 start(const LintPassRequest &request, QString *error) override
    {
        engine_ = request.engine;
        if (service_ == nullptr) {
            if (error != nullptr) {
                *error = QStringLiteral("the Rust lint service is unavailable");
            }
            return 0;
        }
        BorrowedStrings files(request.files);
        BorrowedStrings includeDirs(request.includeDirs);
        BorrowedStrings defines(request.defines);
        QByteArray slang = request.slangPath.toUtf8();
        QByteArray verible = request.veriblePath.toUtf8();
        VdLintBatchOptions options{};
        options.engine = request.engine;
        options.slang_binary = request.slangPath.isEmpty() ? nullptr : slang.constData();
        options.verible_binary = request.veriblePath.isEmpty() ? nullptr : verible.constData();
        options.include_dirs = includeDirs.list;
        options.defines = defines.list;
        options.timeout_ms = static_cast<uint64_t>(request.timeoutMs);
        options.max_diagnostics = static_cast<size_t>(request.maxDiagnostics);
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS) && defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
        // UX batch 2 / U6: the Preferences Lint page's knobs and the rules-file
        // precedence reach the batch argv through the ONE composition seam. A
        // knob the selected engine ignores is never filled (R1). The storage
        // must live until vd_deeplint_batch_start() returns.
        settings::LintBatchAbiStorage abiStrings;
        const settings::LintJobOptions jobOptions = settings::buildLintJobOptions(
            settings::currentLintValues(),
            request.engine == 1 ? settings::LintBackend::Verible : settings::LintBackend::Slang,
            request.rulesPath, settings::currentDiscoveredRulesPath());
        settings::fillLintBatchOptions(jobOptions, &options, &abiStrings);
#else
        QByteArray rules = request.rulesPath.toUtf8();
        options.rules_path = request.rulesPath.isEmpty() ? nullptr : rules.constData();
#endif
        const quint64 job = static_cast<quint64>(vd_deeplint_batch_start(service_, &files.list, &options));
        if (job == 0 && error != nullptr) {
            *error = serviceError();
        }
        return job;
    }

    LintPassSnapshot progress(quint64 job) const override
    {
        LintPassSnapshot snapshot;
        snapshot.tool = toolName();
        if (service_ == nullptr) {
            snapshot.phase = LintPhase::Failed;
            snapshot.stage = QStringLiteral("resolve");
            snapshot.message = QStringLiteral("the Rust lint service is unavailable");
            return snapshot;
        }
        VdLintProgress raw{};
        const int status = vd_deeplint_batch_progress(service_, job, &raw);
        if (status != VD_STATUS_PENDING && status != VD_STATUS_READY) {
            snapshot.phase = LintPhase::Failed;
            snapshot.stage = QStringLiteral("progress");
            snapshot.message = serviceError();
            return snapshot;
        }
        snapshot.phase = phaseOf(raw.phase);
        snapshot.done = static_cast<int>(raw.done);
        snapshot.total = static_cast<int>(raw.total);
        snapshot.current = textOrEmpty(raw.current);
        if (!textOrEmpty(raw.tool).isEmpty()) {
            snapshot.tool = textOrEmpty(raw.tool);
        }
        snapshot.stage = textOrEmpty(raw.stage);
        snapshot.message = textOrEmpty(raw.message);
        return snapshot;
    }

    LspPolled poll(quint64 job) override
    {
        if (service_ == nullptr) {
            return LspPolled::failure(QStringLiteral("the Rust lint service is unavailable"));
        }
        VdDiagnostics raw{};
        const int status = vd_deeplint_batch_poll(service_, job, &raw);
        if (status == VD_STATUS_PENDING) {
            return LspPolled::pendingResult();
        }
        if (status == VD_STATUS_FAILED) {
            return LspPolled::failure(serviceError());
        }
        if (status != VD_STATUS_READY) {
            return LspPolled::failure(serviceError().isEmpty()
                                          ? QStringLiteral("the lint job is unknown to the service")
                                          : serviceError());
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        for (size_t index = 0; index < raw.count; ++index) {
            const VdDiagnostic &item = raw.items[index];
            DiagnosticEntry entry;
            entry.file = textOrEmpty(item.file);
            entry.line = static_cast<int>(item.line);
            entry.column = static_cast<int>(item.column);
            entry.endLine = static_cast<int>(item.end_line);
            entry.endColumn = static_cast<int>(item.end_column);
            entry.severity = item.severity;
            entry.code = textOrEmpty(item.code);
            // AC-5.8 / AC-N3: the typed slang warning option name travels with a
            // PROJECT-PASS row exactly like a per-file one (see
            // FfiSlangBackend::pollLint in ffi_backends.cpp). Without it the
            // upper layer - the only layer the panel shows for a file a finished
            // pass covered - could not offer the per-diagnostic slang waiver and
            // would fall back to a whole-file waiver, which AC-5.8 forbids.
            // `code` above stays what the producer sent: it is never substituted
            // for the option name.
            entry.optionName = textOrEmpty(item.option_name);
            entry.message = textOrEmpty(item.message);
            entry.source = QStringLiteral("cli:%1").arg(toolName());
            polled.diagnostics.append(entry);
        }
        vd_diagnostics_free(&raw);
        return polled;
    }

    void cancel(quint64 job) override
    {
        if (service_ != nullptr) {
            vd_deeplint_batch_cancel(service_, job);
        }
    }

    void release(quint64 job) override
    {
        if (service_ != nullptr) {
            vd_deeplint_batch_release(service_, job);
        }
    }

private:
    QString serviceError() const
    {
        VdError error{};
        if (service_ != nullptr) {
            vd_service_error(service_, &error);
        }
        return QString::fromUtf8(error.message);
    }

    VdService *service_ = nullptr;
    int engine_ = 0;
};

} // namespace

bool ffiProjectLintBackendAvailable()
{
    return true;
}

std::unique_ptr<ProjectLintBackend> createFfiProjectLintBackend()
{
    return std::make_unique<FfiProjectLintBackend>();
}
#else
bool ffiProjectLintBackendAvailable()
{
    return false;
}

std::unique_ptr<ProjectLintBackend> createFfiProjectLintBackend()
{
    return nullptr;
}
#endif

} // namespace vedit::workspace::diagnostics
