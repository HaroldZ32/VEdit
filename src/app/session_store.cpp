#include "session_store.h"

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
#include <QCoreApplication>

namespace vedit::app {

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

// A non-negative integer field, or `fallback` when the value is absent, not a
// number, or negative (a negative offset is our "unknown" marker).
int offset(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return -1;
    }
    const int number = value.toInt(-1);
    return number >= 0 ? number : -1;
}

} // namespace

// ---------------------------------------------------------------------------
// SessionDocument
// ---------------------------------------------------------------------------

bool SessionDocument::hasPath() const
{
    return !path.isEmpty();
}

bool SessionDocument::hasCaret() const
{
    return caretLine >= 0 && caretColumn >= 0;
}

bool SessionDocument::hasScroll() const
{
    return scrollTop >= 0;
}

bool SessionDocument::isValid() const
{
    return hasPath();
}

QJsonObject SessionDocument::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), path);
    if (hasCaret()) {
        QJsonObject caret;
        caret.insert(QStringLiteral("line"), caretLine);
        caret.insert(QStringLiteral("column"), caretColumn);
        object.insert(QStringLiteral("caret"), caret);
    }
    if (hasScroll()) {
        object.insert(QStringLiteral("scroll"), scrollTop);
    }
    return object;
}

SessionDocument SessionDocument::fromJson(const QJsonObject &object)
{
    SessionDocument document;
    document.path = object.value(QStringLiteral("path")).toString();
    const QJsonObject caret = object.value(QStringLiteral("caret")).toObject();
    document.caretLine = offset(caret.value(QStringLiteral("line")));
    document.caretColumn = offset(caret.value(QStringLiteral("column")));
    if (document.caretLine < 0 || document.caretColumn < 0) {
        document.caretLine = -1;
        document.caretColumn = -1;
    }
    document.scrollTop = offset(object.value(QStringLiteral("scroll")));
    return document;
}

// ---------------------------------------------------------------------------
// SessionPane
// ---------------------------------------------------------------------------

bool SessionPane::isEmpty() const
{
    return paneId.isEmpty();
}

QJsonObject SessionPane::toJson() const
{
    QJsonObject leaf;
    leaf.insert(QStringLiteral("type"), QStringLiteral("leaf"));
    leaf.insert(QStringLiteral("pane"), paneId);
    leaf.insert(QStringLiteral("provider"), provider);
    leaf.insert(QStringLiteral("activeTab"), activeTab);
    QJsonArray documents;
    for (const SessionDocument &document : this->documents) {
        if (document.isValid()) {
            documents.append(document.toJson());
        }
    }
    leaf.insert(QStringLiteral("documents"), documents);
    return leaf;
}

SessionPane SessionPane::fromJson(const QJsonObject &leaf)
{
    SessionPane pane;
    pane.paneId = leaf.value(QStringLiteral("pane")).toString();
    pane.provider = leaf.value(QStringLiteral("provider")).toString();
    const int active = leaf.value(QStringLiteral("activeTab")).toInt(0);
    pane.activeTab = active >= 0 ? active : 0;
    const QJsonArray documents = leaf.value(QStringLiteral("documents")).toArray();
    for (const QJsonValue &value : documents) {
        if (!value.isObject()) {
            continue;
        }
        SessionDocument document = SessionDocument::fromJson(value.toObject());
        if (document.isValid()) {
            pane.documents.append(document);
        }
    }
    return pane;
}

// ---------------------------------------------------------------------------
// SessionState
// ---------------------------------------------------------------------------

bool SessionState::hasWindowState() const
{
    return !geometry.isEmpty() || !dockState.isEmpty();
}

bool SessionState::isValid() const
{
    return !theme.isEmpty() || !projectRoot.isEmpty() || !panels.isEmpty()
           || !editorLayout.isEmpty() || hasWindowState();
}

QJsonObject SessionState::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), kCurrentVersion);
    object.insert(QStringLiteral("project"), projectRoot);
    object.insert(QStringLiteral("theme"), theme);
    object.insert(QStringLiteral("geometry"), QString::fromLatin1(geometry.toBase64()));
    object.insert(QStringLiteral("dockState"), QString::fromLatin1(dockState.toBase64()));
    object.insert(QStringLiteral("panels"), QJsonArray::fromStringList(panels));
    object.insert(QStringLiteral("editorLayout"), editorLayout);
    return object;
}

SessionState SessionState::fromJson(const QJsonObject &object)
{
    SessionState state;
    state.version = object.value(QStringLiteral("version")).toInt(kCurrentVersion);
    if (state.version <= 0) {
        state.version = kCurrentVersion;
    }
    state.projectRoot = object.value(QStringLiteral("project")).toString();
    state.theme = object.value(QStringLiteral("theme")).toString();
    state.geometry =
        QByteArray::fromBase64(object.value(QStringLiteral("geometry")).toString().toLatin1());
    state.dockState =
        QByteArray::fromBase64(object.value(QStringLiteral("dockState")).toString().toLatin1());
    state.panels = stringList(object, "panels");
    state.editorLayout = object.value(QStringLiteral("editorLayout")).toObject();
    return state;
}

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

SessionState SessionStore::loadState(QString *error) const
{
    const QJsonObject object = load(error);
    return SessionState::fromJson(object);
}

bool SessionStore::saveState(const SessionState &state, QString *error) const
{
    return save(state.toJson(), error);
}

QJsonObject SessionStore::load(QString *error) const
{
    QFile file(path_);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot read session %1").arg(path_);
        }
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "invalid session %1: %2").arg(path_, parseError.errorString());
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
            *error = QCoreApplication::translate("VEdit", "cannot create session directory %1").arg(directory);
        }
        return false;
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot write session %1").arg(path_);
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot commit session %1").arg(path_);
        }
        return false;
    }
    return true;
}

} // namespace vedit::app
