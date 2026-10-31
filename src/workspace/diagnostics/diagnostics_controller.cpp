#include "diagnostics_controller.h"

#include <utility>

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

} // namespace

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
    outline_.clear();
    lastError_.clear();
    session_ = SessionSnapshot{};
    report_ = FileSetReport{};
    report_.files = files_;
    report_.slangAvailable = slang_ && slang_->available();

    if (files_.isEmpty()) {
        state_ = BackendState::Idle;
        session_.state = BackendState::Idle;
        session_.message = QStringLiteral("no effective file set");
        return;
    }

    state_ = BackendState::Reparsing;
    session_.state = BackendState::Reparsing;
    session_.message = QStringLiteral("reparsing %1 file(s)").arg(files_.size());
    startLsp();
    startLints();
    refresh();
}

void DiagnosticsController::startLsp()
{
    if (!lsp_) {
        report_.lspMessage = QStringLiteral("no LSP backend in this build");
        lastError_ = report_.lspMessage;
        return;
    }
    if (lsPath_.isEmpty() || workspaceRoot_.isEmpty()) {
        if (!toolchain_.verible.found()) {
            report_.lspMessage = toolchain_.missingMessage(ToolKind::Verible);
            lastError_ = toolchain_.userMessage();
        } else {
            report_.lspMessage =
                QStringLiteral("LSP backend is not configured (no workspace root)");
            lastError_ = report_.lspMessage;
        }
        return;
    }
    QString error;
    if (lsp_->start(lsPath_, workspaceRoot_, rulesPath_, files_, &error)) {
        lspStarted_ = true;
        report_.lspMessage = QStringLiteral("session started over %1 file(s)").arg(files_.size());
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
                                          : QStringLiteral("no SLANG backend in this build");
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
    if (state_ != BackendState::Reparsing) {
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
        if (session_.state == BackendState::Ready && !lspDiagnosticsRequested_) {
            lspDiagnosticsRequested_ = true;
            for (const QString &file : files_) {
                const quint64 id = lsp_->requestDiagnostics(file);
                if (id != 0) {
                    lspDiagnostics_.append(DiagnosticTicket{id, file});
                }
            }
        }
    }
    if (!lspStarted_ && report_.lspMessage.isEmpty()) {
        report_.lspMessage = QStringLiteral("the LSP session did not start");
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
                                            QStringLiteral("no SLANG backend"));
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
        for (DiagnosticEntry entry : polled.diagnostics) {
            entry.file = ticket.file;
            if (entry.source.isEmpty()) {
                entry.source = QStringLiteral("verible");
            }
            diagnostics_.append(entry);
        }
    }
    lspDiagnostics_ = pending;
}

void DiagnosticsController::finaliseState()
{
    const bool lspPending = lspStarted_ && session_.state == BackendState::Reparsing;
    const bool lspFailed = lspStarted_ && session_.state == BackendState::Failed;
    if (lspPending || !lints_.isEmpty() || !lspDiagnostics_.isEmpty()) {
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
    session_.message = QStringLiteral("%1: lsp %2/%3 file(s), diagnostics %4")
                           .arg(describeState(state_))
                           .arg(report_.lspFilesOpened)
                           .arg(files_.size())
                           .arg(diagnostics_.size());
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
    navigation_ = lsp_ ? lsp_->requestRename(file, line, character, newName) : 0;
    return navigation_;
}

quint64 DiagnosticsController::requestFormat(const QString &file)
{
    navigation_ = lsp_ ? lsp_->requestFormat(file) : 0;
    return navigation_;
}

LspPolled DiagnosticsController::pollNavigation()
{
    if (!lsp_ || navigation_ == 0) {
        return LspPolled::failure(QStringLiteral("no navigation request in flight"));
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

} // namespace vedit::workspace::diagnostics
