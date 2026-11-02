#include "project_state.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringLiteral>

#include <utility>

namespace vedit::workspace::project {

namespace {

QStringList stringList(const QJsonObject &object, const char *key)
{
    QStringList out;
    const QJsonArray array = object.value(QString::fromLatin1(key)).toArray();
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            out.append(value.toString());
        }
    }
    return out;
}

QJsonArray toArray(const QStringList &values)
{
    return QJsonArray::fromStringList(values);
}

} // namespace

bool ProjectState::isValid() const
{
    return !root.isEmpty();
}

QJsonObject ProjectState::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("projectRoot"), root);
    object.insert(QStringLiteral("capturedAtUtc"), capturedAt);
    object.insert(QStringLiteral("files"), toArray(files));
    object.insert(QStringLiteral("includeDirs"), toArray(includeDirs));
    object.insert(QStringLiteral("defines"), toArray(defines));
    object.insert(QStringLiteral("diagnostics"), toArray(diagnostics));
    return object;
}

ProjectState ProjectState::fromJson(const QJsonObject &object)
{
    ProjectState state;
    state.root = object.value(QStringLiteral("projectRoot")).toString();
    state.capturedAt = object.value(QStringLiteral("capturedAtUtc")).toString();
    state.files = stringList(object, "files");
    state.includeDirs = stringList(object, "includeDirs");
    state.defines = stringList(object, "defines");
    state.diagnostics = stringList(object, "diagnostics");
    return state;
}

QString ProjectState::nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

ProjectStateStore::ProjectStateStore(QString directory)
    : directory_(std::move(directory))
{
}

QString ProjectStateStore::fileName()
{
    return QStringLiteral("project-model.json");
}

QString ProjectStateStore::path() const
{
    QString directory = directory_;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (directory.isEmpty()) {
        return QString();
    }
    return QDir(directory).filePath(fileName());
}

ProjectState ProjectStateStore::load(bool *ok, QString *error) const
{
    const auto fail = [ok, error](const QString &reason) {
        if (ok != nullptr) {
            *ok = false;
        }
        if (error != nullptr) {
            *error = reason;
        }
        return ProjectState{};
    };

    const QString filePath = path();
    if (filePath.isEmpty()) {
        return fail(QStringLiteral("no state directory is available"));
    }
    QFile file(filePath);
    if (!file.exists()) {
        return fail(QString());
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("cannot read %1: %2").arg(filePath, file.errorString()));
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("%1 is not a project state file: %2")
                        .arg(filePath, parseError.errorString()));
    }
    const ProjectState state = ProjectState::fromJson(document.object());
    if (!state.isValid()) {
        return fail(QStringLiteral("%1 carries no project root").arg(filePath));
    }
    if (ok != nullptr) {
        *ok = true;
    }
    if (error != nullptr) {
        error->clear();
    }
    return state;
}

bool ProjectStateStore::save(const ProjectState &state, QString *error) const
{
    const QString filePath = path();
    if (filePath.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no state directory is available");
        }
        return false;
    }
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1").arg(QFileInfo(filePath).absolutePath());
        }
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2").arg(filePath, file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(state.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot commit %1: %2").arg(filePath, file.errorString());
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace vedit::workspace::project
