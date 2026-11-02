#include "fim_inline_completion.h"

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
    status_ = backend_ != nullptr ? QStringLiteral("FIM 未启用（默认关闭）")
                                  : QStringLiteral("FIM 不可用（未链接 Rust 服务）");
}

void InlineCompletion::setBackend(Backend *backend)
{
    cancelRequest(inFlight_);
    backend_ = backend;
    pending_ = false;
    suggestion_.clear();
    status_ = backend_ != nullptr ? QStringLiteral("FIM 未启用（默认关闭）")
                                  : QStringLiteral("FIM 不可用（未链接 Rust 服务）");
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
        status_ = QStringLiteral("FIM 超时（%1 ms 无结果）").arg(kBlockingBudgetMs);
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
        status_ = QStringLiteral("FIM 不可用（未链接 Rust 服务；未发出请求）");
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
        status_ = error.isEmpty() ? QStringLiteral("FIM 请求未被 Rust 服务接受") : error;
        return 0;
    }
    inFlight_ = token;
    status_ = QStringLiteral("FIM 请求中…");
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
        status_ = QStringLiteral("FIM 不可用（未链接 Rust 服务）");
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
        status_ = pending_ ? QStringLiteral("FIM 建议就绪") : QStringLiteral("FIM 返回空建议");
        break;
    case AsyncReply::State::Disabled:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 未启用（默认关闭）") : reply.text;
        break;
    case AsyncReply::State::Error:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 失败（无详情）") : reply.text;
        break;
    case AsyncReply::State::Stale:
        status_ = QStringLiteral("FIM 请求已失效");
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
    status_ = QStringLiteral("FIM 请求已取消");
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
        status_ = pending_ ? QStringLiteral("FIM 建议就绪") : QStringLiteral("FIM 返回空建议");
        return;
    case Reply::State::Disabled:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 未启用（默认关闭）") : reply.text;
        return;
    case Reply::State::Error:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 失败（无详情）") : reply.text;
        return;
    }
    status_ = QStringLiteral("FIM 未知状态");
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
    status_ = QStringLiteral("FIM 建议已作废（继续输入）");
}

QString InlineCompletion::accept()
{
    if (!pending_) {
        return {};
    }
    const QString accepted = suggestion_;
    suggestion_.clear();
    pending_ = false;
    status_ = QStringLiteral("FIM 已接受建议");
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
            status_ = QStringLiteral("FIM 已取消");
        }
        return;
    }
    suggestion_.clear();
    pending_ = false;
    status_ = QStringLiteral("FIM 已取消");
}

} // namespace vedit::editor::fim
