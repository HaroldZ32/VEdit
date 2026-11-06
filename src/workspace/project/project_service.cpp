#include "project_service.h"

#include "vd_ffi.h"

#include <QByteArray>
#include <QStringLiteral>

#include <vector>
#include <QCoreApplication>

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
    payload.relativeFiles = toList(info.relative_files);
    payload.includeDirs = toList(info.include_dirs);
    payload.defines = toList(info.defines);
    payload.diagnostics = toList(info.warnings);
    payload.missingVariables = toList(info.missing_variables);
    payload.skipped = static_cast<int>(info.skipped_lines);
    return payload;
}

// Borrowed view of a string list. The UTF-8 buffers are owned by `storage`,
// which the caller keeps alive for the whole call; `items` only points into it.
struct BorrowedList {
    std::vector<QByteArray> storage;
    std::vector<char *> items;
    VdStringList list{0, nullptr};

    explicit BorrowedList(const QStringList &values)
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

ProjectService::Start ProjectService::start(const QString &projectRoot, const QStringList &filelists,
                                            const QStringList &variables, const QString &settingsPath,
                                            quint64 *job)
{
    if (service_ == nullptr || job == nullptr) {
        return Start::Rejected;
    }
    const QByteArray path = projectRoot.toUtf8();
    const QByteArray settings = settingsPath.toUtf8();
    BorrowedList lists(filelists);
    BorrowedList table(variables);
    const quint64 id = static_cast<quint64>(vd_project_expand_v2(
        service_, path.constData(), &lists.list,
        settings.isEmpty() ? nullptr : settings.constData(), &table.list));
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
        return QCoreApplication::translate("VEdit", "the Rust project service is unavailable");
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
