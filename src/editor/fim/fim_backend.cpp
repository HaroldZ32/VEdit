#include "fim_backend.h"

#include <QElapsedTimer>
#include <QString>
#include <QThread>

#if defined(VEDIT_FIM_HAVE_FFI)
#include <vd_ffi.h>
#endif

namespace vedit::editor::fim {
namespace {

#if defined(VEDIT_FIM_HAVE_FFI)
constexpr int kPollBudgetMs = 15'000;

QString serviceError(VdService *service)
{
    VdError error{};
    if (service != nullptr && vd_service_error(service, &error) == 1) {
        return QString::fromUtf8(error.message);
    }
    return QStringLiteral("the Rust service reported no detail");
}

class RustBackend final : public Backend {
public:
    Reply complete(const QString &prefix, const QString &suffix) override
    {
        VdService *service = vd_service_create();
        if (service == nullptr) {
            return Reply{Reply::State::Error, QStringLiteral("FIM 服务不可用（无法创建 Rust 句柄）")};
        }
        const QByteArray prefixBytes = prefix.toUtf8();
        const QByteArray suffixBytes = suffix.toUtf8();
        const quint64 job = vd_fim_complete(service, prefixBytes.constData(),
                                            suffixBytes.constData());
        if (job == 0) {
            const QString error = serviceError(service);
            vd_service_destroy(service);
            return Reply{Reply::State::Error,
                         QStringLiteral("FIM 请求未被 Rust 服务接受：%1").arg(error)};
        }
        VdFimResult result{};
        QElapsedTimer timer;
        timer.start();
        qint32 status = VD_STATUS_PENDING;
        while (timer.elapsed() < kPollBudgetMs
               && (status = vd_fim_poll(service, job, &result)) == VD_STATUS_PENDING) {
            QThread::msleep(5);
        }
        Reply reply;
        if (status == VD_STATUS_READY) {
            reply.text = QString::fromUtf8(result.text != nullptr ? result.text : "");
            switch (result.state) {
            case 1:
                reply.state = Reply::State::Text;
                break;
            case 2:
                reply.state = Reply::State::Error;
                break;
            default:
                reply.state = Reply::State::Disabled;
                break;
            }
        } else if (status == VD_STATUS_FAILED) {
            reply.state = Reply::State::Error;
            reply.text = QStringLiteral("FIM 失败：%1").arg(serviceError(service));
        } else {
            reply.state = Reply::State::Error;
            reply.text = QStringLiteral("FIM 超时（%1 ms 无结果）").arg(kPollBudgetMs);
        }
        vd_fim_result_free(&result);
        vd_service_destroy(service);
        return reply;
    }
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
    return QStringLiteral("unavailable (no Rust service in this build)");
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
