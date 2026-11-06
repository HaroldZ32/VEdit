#include "fim_backend.h"

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QThread>

#if defined(VEDIT_FIM_HAVE_FFI)
#include <vd_ffi.h>
#include <QCoreApplication>
#endif

namespace vedit::editor::fim {
namespace {

#if defined(VEDIT_FIM_HAVE_FFI)
constexpr int kBlockingBudgetMs = 15'000;

QString serviceError(VdService *service)
{
    VdError error{};
    if (service != nullptr && vd_service_error(service, &error) == 1) {
        return QString::fromUtf8(error.message);
    }
    return QCoreApplication::translate("VEdit", "the Rust service reported no detail");
}

// Production adapter. T106: the request path is job-based and NON-BLOCKING —
// `start()` only creates the Rust job and returns its token; `poll()` asks the
// service once and never waits. The GUI thread (the editor binding) polls from
// a QTimer, so an unreachable endpoint can only make the suggestion late, never
// freeze the window. `complete()` stays as the blocking convenience call for the
// lane selftest and anything that explicitly wants to wait.
class RustBackend final : public Backend {
public:
    ~RustBackend() override
    {
        for (const quint64 token : inflight_.keys()) {
            releaseToken(token);
        }
    }

    bool supportsAsync() const override { return true; }

    Reply complete(const QString &prefix, const QString &suffix) override
    {
        QString error;
        const quint64 token = start(prefix, suffix, &error);
        if (token == 0) {
            return Reply{Reply::State::Error,
                         error.isEmpty() ? QCoreApplication::translate("VEdit", "FIM request was not accepted by the Rust service") : error};
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kBlockingBudgetMs) {
            const AsyncReply reply = poll(token);
            switch (reply.state) {
            case AsyncReply::State::Pending:
                QThread::msleep(5);
                continue;
            case AsyncReply::State::Text:
                return Reply{Reply::State::Text, reply.text};
            case AsyncReply::State::Disabled:
                return Reply{Reply::State::Disabled, reply.text};
            default:
                return Reply{Reply::State::Error, reply.text};
            }
        }
        cancel(token);
        return Reply{Reply::State::Error,
                     QCoreApplication::translate("VEdit", "FIM timed out (%1 ms without a result)").arg(kBlockingBudgetMs)};
    }

    quint64 start(const QString &prefix, const QString &suffix, QString *error) override
    {
        VdService *service = vd_service_create();
        if (service == nullptr) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "FIM service unavailable (could not create the Rust handle)");
            }
            return 0;
        }
        const QByteArray prefixBytes = prefix.toUtf8();
        const QByteArray suffixBytes = suffix.toUtf8();
        const quint64 job = vd_fim_complete(service, prefixBytes.constData(),
                                            suffixBytes.constData());
        if (job == 0) {
            const QString detail = serviceError(service);
            vd_service_destroy(service);
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "FIM request was not accepted by the Rust service: %1").arg(detail);
            }
            return 0;
        }
        const quint64 token = nextToken_++;
        inflight_.insert(token, Inflight{service, job});
        return token;
    }

    AsyncReply poll(quint64 request) override
    {
        auto it = inflight_.find(request);
        if (it == inflight_.end()) {
            return AsyncReply{AsyncReply::State::Stale, QString()};
        }
        VdFimResult result{};
        const qint32 status = vd_fim_poll(it->service, it->job, &result);
        if (status == VD_STATUS_PENDING) {
            return AsyncReply{AsyncReply::State::Pending, QString()};
        }
        AsyncReply reply;
        if (status == VD_STATUS_READY) {
            reply.text = QString::fromUtf8(result.text != nullptr ? result.text : "");
            switch (result.state) {
            case 1:
                reply.state = AsyncReply::State::Text;
                break;
            case 2:
                reply.state = AsyncReply::State::Error;
                break;
            default:
                reply.state = AsyncReply::State::Disabled;
                break;
            }
        } else if (status == VD_STATUS_FAILED) {
            reply.state = AsyncReply::State::Error;
            reply.text = QCoreApplication::translate("VEdit", "FIM failed: %1").arg(serviceError(it->service));
        } else {
            reply.state = AsyncReply::State::Error;
            reply.text = QCoreApplication::translate("VEdit", "FIM poll failed (state %1)").arg(status);
        }
        vd_fim_result_free(&result);
        releaseToken(request);
        return reply;
    }

    void cancel(quint64 request) override { releaseToken(request); }

private:
    struct Inflight {
        VdService *service = nullptr;
        quint64 job = 0;
    };

    void releaseToken(quint64 request)
    {
        auto it = inflight_.find(request);
        if (it == inflight_.end()) {
            return;
        }
        if (it->service != nullptr) {
            vd_service_destroy(it->service);
        }
        inflight_.erase(it);
    }

    QHash<quint64, Inflight> inflight_;
    quint64 nextToken_ = 1;
};
#endif // VEDIT_FIM_HAVE_FFI

} // namespace

std::unique_ptr<Backend> makeRustBackend()
{
#if defined(VEDIT_FIM_HAVE_FFI)
    return std::make_unique<RustBackend>();
#else
    return nullptr;
#endif
}

QString backendDescription()
{
#if defined(VEDIT_FIM_HAVE_FFI)
    return QStringLiteral("Rust service (vd_fim_complete -> vd-fim)");
#else
    return QCoreApplication::translate("VEdit", "unavailable (no Rust service in this build)");
#endif
}

Context contextAt(const QString &text, int zeroBasedLine, int zeroBasedColumn)
{
    Context context;
    if (text.isEmpty()) {
        return context;
    }
    const int line = qMax(0, zeroBasedLine);
    const int column = qMax(0, zeroBasedColumn);
    int offset = 0;
    int currentLine = 0;
    while (currentLine < line && offset < text.size()) {
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
        if (newline < 0) {
            offset = text.size();
            break;
        }
        offset = static_cast<int>(newline) + 1;
        ++currentLine;
    }
    const qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), offset);
    const int lineLength = static_cast<int>((lineEnd < 0 ? text.size() : lineEnd) - offset);
    const int clampedColumn = qMin(column, qMax(0, lineLength));
    const int cursor = qMin(offset + clampedColumn, static_cast<int>(text.size()));
    context.prefix = text.left(cursor);
    context.suffix = text.mid(cursor);
    return context;
}

} // namespace vedit::editor::fim
