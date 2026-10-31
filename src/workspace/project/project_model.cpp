#include "project_model.h"

#include <QFileInfo>
#include <QStringLiteral>

#include <utility>

namespace vedit::workspace::project {

namespace {

constexpr int kPollIntervalMs = 10;
constexpr int kJobTimeoutMs = 30000;

QString displayPath(const QString &root, const QString &path)
{
    if (root.isEmpty()) {
        return path;
    }
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (path.startsWith(prefix)) {
        return path.mid(prefix.size());
    }
    return path;
}

} // namespace

ProjectModel::ProjectModel(QString stateDirectory, QObject *parent)
    : QObject(parent)
    , store_(std::move(stateDirectory))
{
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &ProjectModel::pollOnce);
}

ProjectModel::~ProjectModel()
{
    pollTimer_.stop();
    if (job_ != 0) {
        service_.cancel(job_);
    }
}

bool ProjectModel::restoreFromDisk(QString *error)
{
    bool ok = false;
    QString loadError;
    ProjectState state = store_.load(&ok, &loadError);
    if (!ok) {
        if (error != nullptr) {
            *error = loadError;
        }
        return false;
    }
    state_ = state;
    restored_ = true;
    lastError_.clear();
    stateError_.clear();
    emit stateChanged();
    return true;
}

void ProjectModel::open(const QString &projectRoot)
{
    if (projectRoot.isEmpty()) {
        return;
    }
    const QString absolute = QFileInfo(projectRoot).absoluteFilePath();
    if (absolute != state_.root) {
        state_ = ProjectState{};
        state_.root = absolute;
        restored_ = false;
    }
    startExpansion();
}

void ProjectModel::refresh()
{
    if (state_.root.isEmpty()) {
        emit refreshFinished(false, QStringLiteral("no project is open"));
        return;
    }
    startExpansion();
}

bool ProjectModel::hasProject() const
{
    return state_.isValid();
}

bool ProjectModel::isPending() const
{
    return pending_;
}

bool ProjectModel::restoredFromDisk() const
{
    return restored_;
}

QString ProjectModel::projectRoot() const
{
    return state_.root;
}

QStringList ProjectModel::files() const
{
    return state_.files;
}

QStringList ProjectModel::displayFiles() const
{
    QStringList out;
    out.reserve(state_.files.size());
    for (const QString &file : state_.files) {
        out.append(displayPath(state_.root, file));
    }
    return out;
}

QStringList ProjectModel::includeDirs() const
{
    return state_.includeDirs;
}

QStringList ProjectModel::defines() const
{
    return state_.defines;
}

QStringList ProjectModel::diagnostics() const
{
    QStringList out = state_.diagnostics;
    if (!stateError_.isEmpty()) {
        out.append(QStringLiteral("state: %1").arg(stateError_));
    }
    if (!lastError_.isEmpty()) {
        out.append(QStringLiteral("service: %1").arg(lastError_));
    }
    return out;
}

QString ProjectModel::summary() const
{
    if (!hasProject()) {
        return QStringLiteral("No project open");
    }
    const QString name = QFileInfo(state_.root).fileName();
    const int count = state_.files.size();
    QString text = QStringLiteral("Project: %1 — %2 %3")
                       .arg(name.isEmpty() ? state_.root : name)
                       .arg(count)
                       .arg(count == 1 ? QStringLiteral("file") : QStringLiteral("files"));
    if (pending_) {
        text += QStringLiteral(" (expanding…)");
    } else if (!lastError_.isEmpty()) {
        text += QStringLiteral(" (refresh failed)");
    } else if (restored_) {
        text += QStringLiteral(" (restored from last session)");
    } else {
        text += QStringLiteral(" (live)");
    }
    return text;
}

QString ProjectModel::statePath() const
{
    return store_.path();
}

QString ProjectModel::lastError() const
{
    return lastError_;
}

void ProjectModel::startExpansion()
{
    if (pending_ && job_ != 0) {
        service_.cancel(job_);
        job_ = 0;
    }
    quint64 id = 0;
    if (service_.start(state_.root, &id) != ProjectService::Start::Started) {
        recordFailure(service_.lastError());
        return;
    }
    job_ = id;
    pending_ = true;
    lastError_.clear();
    deadline_.start();
    pollTimer_.start();
    emit stateChanged();
}

void ProjectModel::pollOnce()
{
    if (!pending_ || job_ == 0) {
        pollTimer_.stop();
        return;
    }
    if (deadline_.elapsed() > kJobTimeoutMs) {
        const quint64 stuck = job_;
        job_ = 0;
        service_.cancel(stuck);
        recordFailure(QStringLiteral("the project service did not answer in %1 ms").arg(kJobTimeoutMs));
        return;
    }

    ExpansionPayload payload;
    switch (service_.poll(job_, &payload)) {
    case ProjectService::Poll::Pending:
        return;
    case ProjectService::Poll::Ready: {
        job_ = 0;
        pending_ = false;
        pollTimer_.stop();
        adopt(payload);
        emit stateChanged();
        emit refreshFinished(true, QString());
        return;
    }
    case ProjectService::Poll::Failed: {
        recordFailure(service_.lastError());
        return;
    }
    case ProjectService::Poll::Misuse: {
        recordFailure(QStringLiteral("the project job was rejected by the service"));
        return;
    }
    }
}

void ProjectModel::adopt(const ExpansionPayload &payload)
{
    state_.files = payload.files;
    state_.includeDirs = payload.includeDirs;
    state_.defines = payload.defines;
    state_.diagnostics = payload.diagnostics;
    state_.capturedAt = ProjectState::nowUtc();
    restored_ = false;
    lastError_.clear();

    QString saveError;
    if (!store_.save(state_, &saveError)) {
        stateError_ = saveError;
    } else {
        stateError_.clear();
    }
}

void ProjectModel::recordFailure(const QString &error)
{
    lastError_ = error.isEmpty() ? QStringLiteral("the project service failed") : error;
    if (job_ != 0) {
        service_.cancel(job_);
        job_ = 0;
    }
    pending_ = false;
    pollTimer_.stop();
    emit stateChanged();
    emit refreshFinished(false, lastError_);
}

} // namespace vedit::workspace::project
