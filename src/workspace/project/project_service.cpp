#include "project_service.h"

#include "vd_ffi.h"

#include <QByteArray>
#include <QStringLiteral>

namespace vedit::workspace::project {

namespace {

QStringList toList(const VdStringList &list)
{
    QStringList out;
    for (size_t index = 0; index < list.count; ++index) {
        const char *item = list.items[index];
        if (item != nullptr) {
            out.append(QString::fromUtf8(item));
        }
    }
    return out;
}

ExpansionPayload toPayload(const VdProjectInfo &info)
{
    ExpansionPayload payload;
    payload.files = toList(info.files);
    payload.includeDirs = toList(info.include_dirs);
    payload.defines = toList(info.defines);
    payload.diagnostics = toList(info.warnings);
    return payload;
}

} // namespace

ProjectService::ProjectService()
    : service_(vd_service_create())
{
}

ProjectService::~ProjectService()
{
    if (service_ != nullptr) {
        vd_service_destroy(service_);
    }
}

ProjectService::Start ProjectService::start(const QString &projectRoot, quint64 *job)
{
    if (service_ == nullptr || job == nullptr) {
        return Start::Rejected;
    }
    const QByteArray path = projectRoot.toUtf8();
    const quint64 id = static_cast<quint64>(vd_project_parse(service_, path.constData()));
    if (id == 0) {
        return Start::Rejected;
    }
    *job = id;
    return Start::Started;
}

ProjectService::Poll ProjectService::poll(quint64 job, ExpansionPayload *payload)
{
    if (service_ == nullptr || payload == nullptr) {
        return Poll::Misuse;
    }
    VdProjectInfo info{};
    const int status = static_cast<int>(vd_project_poll(service_, job, &info));
    switch (status) {
    case VD_STATUS_PENDING:
        return Poll::Pending;
    case VD_STATUS_READY: {
        *payload = toPayload(info);
        vd_project_info_free(&info);
        return Poll::Ready;
    }
    case VD_STATUS_FAILED:
        vd_project_info_free(&info);
        return Poll::Failed;
    default:
        vd_project_info_free(&info);
        return Poll::Misuse;
    }
}

QString ProjectService::lastError() const
{
    if (service_ == nullptr) {
        return QStringLiteral("the Rust project service is unavailable");
    }
    VdError error{};
    if (vd_service_error(service_, &error) != 1) {
        return QString();
    }
    return QString::fromUtf8(error.message);
}

void ProjectService::cancel(quint64 job)
{
    if (service_ != nullptr) {
        vd_job_cancel(service_, job);
    }
}

} // namespace vedit::workspace::project
