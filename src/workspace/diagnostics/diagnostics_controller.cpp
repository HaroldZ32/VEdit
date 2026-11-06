#include "diagnostics_controller.h"

#include <QDateTime>
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

/// CLI entries reach the display layer in the C ABI's severity scale; the panel
/// renders the LSP scale (1 error, 2 warning...), so convert once.
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
    files_ = files;
    reparse();
}

void DiagnosticsController::setToolchainReport(const ToolchainReport &report)
{
    toolchain_ = report;
    if (!toolchain_.missing().isEmpty() && lastError_.isEmpty()) {
        lastError_ = toolchain_.userMessage();
    }
}

void DiagnosticsController::reparse()
{
    ++generation_;
    if (lsp_ && lspStarted_) {
        lsp_->stop();
    }
    lspStarted_ = false;
    lspDiagnosticsRequested_ = false;
    lints_.clear();
    lspDiagnostics_.clear();
    diagnostics_.clear();
    // N1: the "lower layer evaluated this file" marks describe the rows that
    // were just dropped. Keeping them would turn a file with errors into a green
    // dot (Clean) after the analysis is gone; the verdict must follow the rows.
    lowerAnalyzed_.clear();
    outline_.clear();
    lastError_.clear();
    invalidateView();
    session_ = SessionSnapshot{};
    report_ = FileSetReport{};
    report_.files = files_;
    report_.slangAvailable = slang_ && slang_->available();

    if (files_.isEmpty()) {
        state_ = BackendState::Idle;
        session_.state = BackendState::Idle;
        session_.message = QCoreApplication::translate("VEdit", "no effective file set");
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
    lspDiagnosticsRequested_ = false;
    bufferApplied_ = false;
    bufferAppliedMs_ = 0;
    bufferGenerationAtApply_ = 0;
    symbolsRequested_ = false;
    ++analysisCount_;
    lastAnalysisAt_ = QDateTime::currentDateTime();

    const bool sameFile = files_.size() == 1 && files_.first() == file;
    if (!sameFile || !lspStarted_) {
        // A file-scope analysis uses a session over exactly this file: the
        // project-scope session (if any) is replaced, and `project.reparse`
        // restores the project file set.
        if (lsp_ && lspStarted_) {
            lsp_->stop();
        }
        files_ = QStringList{file};
        lspStarted_ = false;
        lspDiagnosticsRequested_ = false;
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
        session_.state = BackendState::Reparsing;
        refresh();
    }
    return lspStarted_;
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
    if (lsp_->start(lsPath_, workspaceRoot_, rulesPath_, files_, &error)) {
        lspStarted_ = true;
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

void DiagnosticsController::refresh()
{
    // T8: the project pass is polled independently of the LSP reparse state -
    // a single-file diagnostic must never wait for the pass (AC-2.6).
    pollProjectPass();
    if (state_ != BackendState::Reparsing) {
        // R2 / one count: a finished project pass (or an edited file falling
        // back) changes the merged view while the LSP is not reparsing. The
        // user-visible session count must follow it, not stay at the lower
        // layer's last number.
        if (viewRevision_ != lastSessionMessageRevision_) {
            composeSessionMessage();
        }
        return;
    }

    if (lsp_ && lspStarted_) {
        session_ = lsp_->status();
        session_.state = backendState(session_.state);
        report_.lspFilesOpened = session_.filesOpened;
        report_.lspReceivedWholeSet =
            session_.filesOpened == files_.size() && session_.filesExpected == files_.size();
        if (!session_.message.isEmpty()) {
            report_.lspMessage = session_.message;
        }
        if (session_.state == BackendState::Ready) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            // T118: push the EDITOR BUFFER into the session before asking for
            // diagnostics, so what gets linted is the text the user sees.
            if (!analysisFile_.isEmpty() && !bufferApplied_ && files_.contains(analysisFile_)) {
                QString bufferError;
                if (lsp_->changeBuffer(analysisFile_, analysisText_, &bufferError)) {
                    bufferApplied_ = true;
                    bufferAppliedMs_ = now;
                    bufferGenerationAtApply_ = session_.generation;
                    bufferDiagnosticsSeen_ = false;
                    // Re-ask on the next tick: the server publishes the new
                    // diagnostics asynchronously and the request returns the
                    // CURRENT snapshot immediately.
                    lspDiagnosticsRequested_ = false;
                } else if (report_.lspMessage.isEmpty()) {
                    report_.lspMessage = bufferError;
                }
            }

            // verible publishes asynchronously: keep re-asking for the snapshot
            // after a buffer change until it arrives (bounded window), because a
            // request returns the CURRENT snapshot immediately.
            const bool bufferSettling =
                bufferApplied_ && !bufferDiagnosticsSeen_ && !analysisFile_.isEmpty()
                && now - bufferAppliedMs_ < 2500;
            if (bufferSettling) {
                lspDiagnosticsRequested_ = false;
            }
            if (!lspDiagnosticsRequested_) {
                lspDiagnosticsRequested_ = true;
                for (const QString &file : files_) {
                    const quint64 id = lsp_->requestDiagnostics(file);
                    if (id != 0) {
                        lspDiagnostics_.append(DiagnosticTicket{id, file});
                    }
                }
            }

            if (!analysisFile_.isEmpty() && !symbolsRequested_) {
                navigation_ = lsp_->requestSymbols(analysisFile_);
                symbolsRequested_ = navigation_ != 0;
            }
        }
    }
    if (!lspStarted_ && report_.lspMessage.isEmpty()) {
        report_.lspMessage = QCoreApplication::translate("VEdit", "the LSP session did not start");
    }

    if (navigation_ != 0) {
        pollNavigation();
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
            diagnostics_.append(entry);
        }
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
        if (polled.failed) {
            if (report_.lspMessage.isEmpty()) {
                report_.lspMessage = polled.error;
            }
            continue;
        }
        if (bufferApplied_ && ticket.file == analysisFile_) {
            bufferDiagnosticsSeen_ = true;
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
        invalidateView();
    }
    lspDiagnostics_ = pending;
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
            pass_.message = QCoreApplication::translate("VEdit", "%1/%2 file(s) scanned — %3 CLI diagnostics")
                                .arg(pass_.done)
                                .arg(pass_.total)
                                .arg(cliDiagnostics_.size());
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
        && QDateTime::currentMSecsSinceEpoch() - bufferAppliedMs_ <= 2500;
    if (lspPending || bufferPublishing || !lints_.isEmpty() || !lspDiagnostics_.isEmpty()) {
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
    const int mergedCount = mergedDiagnostics().size();
    session_.diagnostics = mergedCount;
    session_.message = QStringLiteral("%1: lsp %2/%3 file(s), diagnostics %4")
                           .arg(describeState(state_))
                           .arg(report_.lspFilesOpened)
                           .arg(files_.size())
                           .arg(mergedCount);
    lastSessionMessageRevision_ = viewRevision_;
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
    passRequest_ = request;
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
    files.reserve(request.files.size());
    for (const QString &file : request.files) {
        if (!skipped.contains(file)) {
            files.append(file);
        }
    }
    passSkipped_ = skipped;
    passFiles_ = files.size();
    passLintedFiles_ = files;
    passStartEditSeq_ = editCounter_;
    passAutomaticRequested_ = nextPassAutomatic_;
    nextPassAutomatic_ = false;
    if (files.isEmpty()) {
        pass_ = LintPassSnapshot{};
        pass_.phase = LintPhase::Finished;
        pass_.tool = passBackend_->toolName();
        pass_.message = QStringLiteral(
            "no files to lint (every requested file is a modified buffer; the language "
            "server lints unsaved edits)");
        lastError_ = pass_.message;
        if (error != nullptr) {
            *error = pass_.message;
        }
        return false;
    }

    LintPassRequest backendRequest = request;
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
    cliDiagnostics_.clear();
    coveredFiles_.clear();
    lowerAnalyzed_.clear();
    passSkipped_.clear();
    passLintedFiles_.clear();
    passFiles_ = 0;
    pass_ = LintPassSnapshot{};
    passRequest_ = LintPassRequest{};
    invalidateView();
}

QVector<DisplayDiagnostic> DiagnosticsController::displayDiagnostics() const
{
    // R2: one layered view. A file with a live upper layer contributes ONLY its
    // project-pass rows (a clean covered file contributes none); every other
    // file contributes its lower (per-file/LSP) rows. `Both` can no longer
    // occur: stacking the two layers for one file is exactly what R2 forbids.
    QVector<DisplayDiagnostic> rows;
    QSet<QString> keys;
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (!coveredFiles_.contains(entry.file)) {
            continue;
        }
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
        if (keys.contains(key)) {
            continue;
        }
        keys.insert(key);
        DisplayDiagnostic row;
        // The upper layer arrives in the C ABI's CLI scale (0 error, 1 warning,
        // 2/3 note); every display surface speaks the LSP scale. Convert ONCE,
        // here, or a project-pass ERROR would be painted as information.
        row.entry = toDisplayEntry(entry);
        row.origin = DiagnosticOrigin::Cli;
        row.key = key;
        rows.append(row);
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
        row.entry = entry;
        row.origin = DiagnosticOrigin::Lsp;
        row.key = key;
        rows.append(row);
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
    QStringList files;
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (coveredFiles_.contains(entry.file) && !files.contains(entry.file)) {
            files.append(entry.file);
        }
    }
    // A covered file with no rows (a clean verdict) is still an upper-layer file.
    for (const QString &file : coveredFiles_) {
        if (!files.contains(file)) {
            files.append(file);
        }
    }
    files.sort();
    return files;
}

QVector<DiagnosticEntry> DiagnosticsController::upperLayerDiagnostics() const
{
    QVector<DiagnosticEntry> rows;
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (coveredFiles_.contains(entry.file)) {
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
    if (coveredFiles_.remove(file)) {
        // R2: an edited file's upper rows are void. Drop them from the store as
        // well, so a later pass cannot resurrect a stale row by dedupe key.
        dropUpperRows(file);
        invalidateView();
    }
}

void DiagnosticsController::dropUpperRows(const QString &file)
{
    for (int index = cliDiagnostics_.size() - 1; index >= 0; --index) {
        if (cliDiagnostics_.at(index).file == file) {
            cliDiagnostics_.removeAt(index);
        }
    }
}

QVector<DiagnosticEntry> DiagnosticsController::mergedDiagnostics() const
{
    QVector<DiagnosticEntry> rows;
    QSet<QString> keys;
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (!coveredFiles_.contains(entry.file)) {
            continue;
        }
        const QString key = diagnosticMergeKey(entry, DiagnosticOrigin::Cli);
        if (keys.contains(key)) {
            continue;
        }
        keys.insert(key);
        rows.append(toDisplayEntry(entry));
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
        rows.append(entry);
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
        for (const DiagnosticEntry &entry : cliDiagnostics_) {
            if (entry.file == file) {
                take(entry, DiagnosticOrigin::Cli);
            }
        }
    } else {
        for (const DiagnosticEntry &entry : diagnostics_) {
            if (entry.file == file) {
                take(entry, DiagnosticOrigin::Lsp);
            }
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

DiagnosticsController::FileStatus DiagnosticsController::statusForFile(const QString &file) const
{
    if (file.isEmpty()) {
        return FileStatus::Unknown;
    }
    bool any = false;
    bool error = false;
    bool warning = false;
    const auto scan = [&](const DiagnosticEntry &entry, bool cliScale) {
        if (entry.file != file) {
            return;
        }
        any = true;
        const int severity = cliScale ? canonicalSeverity(entry.severity, DiagnosticOrigin::Cli)
                                      : entry.severity;
        error = error || severity == 1;
        warning = warning || severity == 2;
    };
    if (coveredFiles_.contains(file)) {
        for (const DiagnosticEntry &entry : cliDiagnostics_) {
            scan(entry, true);
        }
        // The pass evaluated this file and found nothing: that is CLEAN, and it
        // is exactly the green dot (an un-evaluated file must stay dot-less).
        if (!any) {
            return FileStatus::Clean;
        }
    } else {
        for (const DiagnosticEntry &entry : diagnostics_) {
            scan(entry, false);
        }
        if (!any) {
            // The lower layer evaluated this file and reported nothing: clean,
            // exactly like a covered-and-clean project file. A file no layer
            // ever looked at stays dot-less.
            return lowerAnalyzed_.contains(file) ? FileStatus::Clean : FileStatus::Unknown;
        }
    }
    if (error) {
        return FileStatus::Error;
    }
    if (warning) {
        return FileStatus::Warning;
    }
    return FileStatus::Clean;
}

QHash<QString, int> DiagnosticsController::fileStatuses() const
{
    QHash<QString, int> statuses;
    const auto classify = [&statuses](const DiagnosticEntry &entry, bool cliScale) {
        const int current = statuses.value(entry.file, 0);
        const int severity = cliScale ? canonicalSeverity(entry.severity, DiagnosticOrigin::Cli)
                                      : entry.severity;
        if (severity == 1) {
            statuses.insert(entry.file, 1); // error dominates
        } else if (severity == 2 && current != 1) {
            statuses.insert(entry.file, 2);
        } else if (current == 0) {
            statuses.insert(entry.file, 3); // covered with a non-error row: clean
        }
    };
    for (const QString &file : coveredFiles_) {
        statuses.insert(file, 3); // clean until a row says otherwise
    }
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (coveredFiles_.contains(entry.file)) {
            classify(entry, true);
        }
    }
    for (const DiagnosticEntry &entry : diagnostics_) {
        if (coveredFiles_.contains(entry.file)) {
            continue;
        }
        // The lower layer only exists for files it actually reports on: a file
        // the LSP never mentioned stays dot-less (0 absent from the map).
        classify(entry, false);
    }
    for (const QString &file : lowerAnalyzed_) {
        if (!statuses.contains(file) && !coveredFiles_.contains(file)) {
            statuses.insert(file, 3); // evaluated clean by the lower layer
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
    QVector<DiagnosticEntry> kept;
    kept.reserve(cliDiagnostics_.size() + fresh.size());
    for (const DiagnosticEntry &entry : cliDiagnostics_) {
        if (!linted.contains(entry.file)) {
            // This pass did not look at the file (it was skipped as an open
            // document): its previous verdict stands until it is edited.
            kept.append(entry);
        }
    }
    kept += fresh;
    cliDiagnostics_ = kept;

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
