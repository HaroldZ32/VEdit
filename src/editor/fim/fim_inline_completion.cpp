#include "fim_inline_completion.h"

namespace vedit::editor::fim {

InlineCompletion::InlineCompletion(Backend *backend)
    : backend_(backend)
{
    status_ = backend_ != nullptr ? QStringLiteral("FIM 未启用（默认关闭）")
                                  : QStringLiteral("FIM 不可用（未链接 Rust 服务）");
}

void InlineCompletion::setBackend(Backend *backend)
{
    backend_ = backend;
    cancel();
    status_ = backend_ != nullptr ? QStringLiteral("FIM 未启用（默认关闭）")
                                  : QStringLiteral("FIM 不可用（未链接 Rust 服务）");
}

bool InlineCompletion::request(const QString &text, int zeroBasedLine, int zeroBasedColumn)
{
    ++requests_;
    suggestion_.clear();
    pending_ = false;
    if (backend_ == nullptr) {
        status_ = QStringLiteral("FIM 不可用（未链接 Rust 服务；未发出请求）");
        return false;
    }
    const Context context = contextAt(text, zeroBasedLine, zeroBasedColumn);
    const Reply reply = backend_->complete(context.prefix, context.suffix);
    switch (reply.state) {
    case Reply::State::Text:
        suggestion_ = reply.text;
        pending_ = !suggestion_.isEmpty();
        status_ = pending_ ? QStringLiteral("FIM 建议就绪") : QStringLiteral("FIM 返回空建议");
        return pending_;
    case Reply::State::Disabled:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 未启用（默认关闭）") : reply.text;
        return false;
    case Reply::State::Error:
        status_ = reply.text.isEmpty() ? QStringLiteral("FIM 失败（无详情）") : reply.text;
        return false;
    }
    status_ = QStringLiteral("FIM 未知状态");
    return false;
}

void InlineCompletion::onTextEdited()
{
    if (!pending_) {
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
    if (!pending_) {
        return;
    }
    suggestion_.clear();
    pending_ = false;
    status_ = QStringLiteral("FIM 已取消");
}

} // namespace vedit::editor::fim
