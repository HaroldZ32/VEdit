#include "fim_inline_completion.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

namespace vedit::editor::fim {
namespace {

// The blocking convenience call's budget (the async path's budget belongs to
// its caller: the editor binding uses a QTimer and never blocks).
constexpr int kBlockingBudgetMs = 15'000;

} // namespace

InlineCompletion::InlineCompletion(Backend *backend)
    : backend_(backend)
{
    status_ = backend_ != nullptr ? QCoreApplication::translate("VEdit", "FIM disabled (off by default)")
                                  : QCoreApplication::translate("VEdit", "FIM unavailable (no Rust service linked)");
}

void InlineCompletion::setBackend(Backend *backend)
{
    cancelRequest(inFlight_);
    backend_ = backend;
    pending_ = false;
    suggestion_.clear();
    status_ = backend_ != nullptr ? QCoreApplication::translate("VEdit", "FIM disabled (off by default)")
                                  : QCoreApplication::translate("VEdit", "FIM unavailable (no Rust service linked)");
}

bool InlineCompletion::request(const QString &text, int zeroBasedLine, int zeroBasedColumn)
{
    const quint64 job = requestJob(text, zeroBasedLine, zeroBasedColumn);
    if (job == 0) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (inFlight_ != 0 && timer.elapsed() < kBlockingBudgetMs) {
        if (pollRequest(job)) {
            break;
        }
        QThread::msleep(5);
    }
    if (inFlight_ != 0) {
        cancelRequest(job);
        status_ = QCoreApplication::translate("VEdit", "FIM timed out (%1 ms without a result)").arg(kBlockingBudgetMs);
        return false;
    }
    return pending_;
}

quint64 InlineCompletion::requestJob(const QString &text, int zeroBasedLine, int zeroBasedColumn)
{
    ++requests_;
    if (inFlight_ != 0) {
        cancelRequest(inFlight_);
    }
    inFlight_ = 0;
    immediateDone_ = false;
    suggestion_.clear();
    pending_ = false;
    if (backend_ == nullptr) {
        status_ = QCoreApplication::translate("VEdit", "FIM unavailable (no Rust service linked; no request was sent)");
        return 0;
    }
    const Context context = contextAt(text, zeroBasedLine, zeroBasedColumn);
    if (!backend_->supportsAsync()) {
        // Test/legacy backend: complete now and let the first poll report it.
        applyReply(backend_->complete(context.prefix, context.suffix));
        inFlight_ = ++nextToken_;
        immediateDone_ = true;
        return inFlight_;
    }
    QString error;
    const quint64 token = backend_->start(context.prefix, context.suffix, &error);
    if (token == 0) {
        status_ = error.isEmpty() ? QCoreApplication::translate("VEdit", "FIM request was not accepted by the Rust service") : error;
        return 0;
    }
    inFlight_ = token;
    status_ = QCoreApplication::translate("VEdit", "FIM request in flight…");
    return token;
}

bool InlineCompletion::pollRequest(quint64 job)
{
    if (job == 0 || job != inFlight_) {
        return true; // stale: the job was cancelled or already consumed
    }
    if (immediateDone_) {
        finishJob();
        return true;
    }
    if (backend_ == nullptr) {
        status_ = QCoreApplication::translate("VEdit", "FIM unavailable (no Rust service linked)");
        finishJob();
        return true;
    }
    const AsyncReply reply = backend_->poll(job);
    switch (reply.state) {
    case AsyncReply::State::Pending:
        return false;
    case AsyncReply::State::Text:
        suggestion_ = reply.text;
        pending_ = !suggestion_.isEmpty();
        status_ = pending_ ? QCoreApplication::translate("VEdit", "FIM suggestion ready") : QCoreApplication::translate("VEdit", "FIM returned an empty suggestion");
        break;
    case AsyncReply::State::Disabled:
        status_ = reply.text.isEmpty() ? QCoreApplication::translate("VEdit", "FIM disabled (off by default)") : reply.text;
        break;
    case AsyncReply::State::Error:
        status_ = reply.text.isEmpty() ? QCoreApplication::translate("VEdit", "FIM failed (no details)") : reply.text;
        break;
    case AsyncReply::State::Stale:
        status_ = QCoreApplication::translate("VEdit", "FIM request went stale");
        break;
    }
    finishJob();
    return true;
}

void InlineCompletion::cancelRequest(quint64 job)
{
    if (job == 0 || job != inFlight_) {
        return;
    }
    if (backend_ != nullptr && !immediateDone_) {
        backend_->cancel(job);
    }
    finishJob();
    suggestion_.clear();
    pending_ = false;
    status_ = QCoreApplication::translate("VEdit", "FIM request cancelled");
}

void InlineCompletion::finishJob()
{
    inFlight_ = 0;
    immediateDone_ = false;
}

void InlineCompletion::applyReply(const Reply &reply)
{
    switch (reply.state) {
    case Reply::State::Text:
        suggestion_ = reply.text;
        pending_ = !suggestion_.isEmpty();
        status_ = pending_ ? QCoreApplication::translate("VEdit", "FIM suggestion ready") : QCoreApplication::translate("VEdit", "FIM returned an empty suggestion");
        return;
    case Reply::State::Disabled:
        status_ = reply.text.isEmpty() ? QCoreApplication::translate("VEdit", "FIM disabled (off by default)") : reply.text;
        return;
    case Reply::State::Error:
        status_ = reply.text.isEmpty() ? QCoreApplication::translate("VEdit", "FIM failed (no details)") : reply.text;
        return;
    }
    status_ = QCoreApplication::translate("VEdit", "FIM unknown state");
}

void InlineCompletion::onTextEdited()
{
    const bool hadJob = inFlight_ != 0;
    if (hadJob) {
        cancelRequest(inFlight_);
    }
    if (!pending_ && !hadJob) {
        return;
    }
    pending_ = false;
    suggestion_.clear();
    status_ = QCoreApplication::translate("VEdit", "FIM suggestion invalidated (typing continued)");
}

QString InlineCompletion::accept()
{
    if (!pending_) {
        return {};
    }
    const QString accepted = suggestion_;
    suggestion_.clear();
    pending_ = false;
    status_ = QCoreApplication::translate("VEdit", "FIM suggestion accepted");
    return accepted;
}

void InlineCompletion::cancel()
{
    const bool hadJob = inFlight_ != 0;
    if (hadJob) {
        cancelRequest(inFlight_);
    }
    if (!pending_) {
        if (!hadJob) {
            status_ = QCoreApplication::translate("VEdit", "FIM cancelled");
        }
        return;
    }
    suggestion_.clear();
    pending_ = false;
    status_ = QCoreApplication::translate("VEdit", "FIM cancelled");
}

} // namespace vedit::editor::fim
