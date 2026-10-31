#include "session_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringLiteral>

namespace vedit::app {

SessionStore::SessionStore(QString filePath)
    : path_(std::move(filePath))
{
}

QString SessionStore::path() const
{
    return path_;
}

QString SessionStore::defaultFilePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString directory = base.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/vedit") : base;
    return directory + QStringLiteral("/session.json");
}

QJsonObject SessionStore::load(QString *error) const
{
    QFile file(path_);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read session %1").arg(path_);
        }
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid session %1: %2").arg(path_, parseError.errorString());
        }
        return {};
    }
    return document.object();
}

bool SessionStore::save(const QJsonObject &object, QString *error) const
{
    const QString directory = QFileInfo(path_).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create session directory %1").arg(directory);
        }
        return false;
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write session %1").arg(path_);
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot commit session %1").arg(path_);
        }
        return false;
    }
    return true;
}

} // namespace vedit::app
