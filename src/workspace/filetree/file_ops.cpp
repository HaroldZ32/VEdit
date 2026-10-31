#include "file_ops.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

namespace vedit::workspace {
namespace fileops {
namespace {

QString systemError()
{
    const int err = errno;
    if (err == 0) {
        return QString();
    }
    return QString::fromLocal8Bit(std::strerror(err));
}

QString describeTarget(const QString &name)
{
    return name.isEmpty() ? QStringLiteral("the entry") : QStringLiteral("'%1'").arg(name);
}

QString permissionHint(const QString &parentDirectory)
{
    if (parentDirectory.isEmpty() || QFileInfo(parentDirectory).isWritable()) {
        return QString();
    }
    return QStringLiteral("the folder '%1' is not writable").arg(parentDirectory);
}

} // namespace

FileOpResult FileOpResult::success(const QString &path)
{
    FileOpResult result;
    result.ok = true;
    result.path = path;
    return result;
}

FileOpResult FileOpResult::failure(const QString &path, const QString &error, const QString &detail)
{
    FileOpResult result;
    result.ok = false;
    result.path = path;
    result.error = error;
    result.detail = detail;
    return result;
}

bool isPlainEntryName(const QString &name, QString *error)
{
    const auto reject = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (name.isEmpty()) {
        return reject(QStringLiteral("Name must not be empty"));
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return reject(QStringLiteral("'%1' is a reserved name").arg(name));
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return reject(QStringLiteral("Name must not contain a path separator"));
    }
    if (name.contains(QChar(0))) {
        return reject(QStringLiteral("Name must not contain a NUL character"));
    }
    if (name.toUtf8().size() > 255) {
        return reject(QStringLiteral("Name is longer than the 255-byte filesystem limit"));
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

FileOpResult createFile(const QString &directory, const QString &name)
{
    QString invalid;
    if (!isPlainEntryName(name, &invalid)) {
        return FileOpResult::failure(QDir(directory).filePath(name), invalid);
    }

    const QString path = QDir(directory).filePath(name);
    if (QFileInfo::exists(path)) {
        return FileOpResult::failure(
            path, QStringLiteral("A file or folder named %1 already exists").arg(describeTarget(name)));
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        QString detail = file.errorString();
        const QString hint = permissionHint(directory);
        if (!hint.isEmpty()) {
            detail = detail.isEmpty() ? hint : QStringLiteral("%1 (%2)").arg(detail, hint);
        }
        return FileOpResult::failure(
            path, QStringLiteral("Could not create file %1").arg(describeTarget(name)), detail);
    }
    file.close();
    return FileOpResult::success(path);
}

FileOpResult createDirectory(const QString &directory, const QString &name)
{
    QString invalid;
    if (!isPlainEntryName(name, &invalid)) {
        return FileOpResult::failure(QDir(directory).filePath(name), invalid);
    }

    const QString path = QDir(directory).filePath(name);
    if (QFileInfo::exists(path)) {
        return FileOpResult::failure(
            path, QStringLiteral("A file or folder named %1 already exists").arg(describeTarget(name)));
    }

    errno = 0;
    if (!QDir(directory).mkdir(name)) {
        const QString detail = systemError();
        return FileOpResult::failure(
            path, QStringLiteral("Could not create folder %1").arg(describeTarget(name)), detail);
    }
    return FileOpResult::success(path);
}

FileOpResult renameEntry(const QString &path, const QString &newName)
{
    QString invalid;
    if (!isPlainEntryName(newName, &invalid)) {
        return FileOpResult::failure(path, invalid);
    }

    const QFileInfo info(path);
    if (!info.exists()) {
        return FileOpResult::failure(
            path, QStringLiteral("'%1' no longer exists").arg(info.fileName()));
    }

    const QString target = info.dir().filePath(newName);
    if (target == path) {
        return FileOpResult::success(path);
    }
    if (QFileInfo::exists(target)) {
        return FileOpResult::failure(
            path, QStringLiteral("A file or folder named %1 already exists").arg(describeTarget(newName)),
            QStringLiteral("target: %1").arg(target));
    }

    errno = 0;
    const bool moved = info.isDir() ? QDir().rename(path, target) : QFile::rename(path, target);
    if (!moved) {
        QString detail = systemError();
        const QString hint = permissionHint(info.dir().path());
        if (!hint.isEmpty()) {
            detail = detail.isEmpty() ? hint : QStringLiteral("%1 (%2)").arg(detail, hint);
        }
        return FileOpResult::failure(
            path,
            QStringLiteral("Could not rename %1 to %2")
                .arg(describeTarget(info.fileName()), describeTarget(newName)),
            detail);
    }
    return FileOpResult::success(target);
}

FileOpResult removeEntry(const QString &path, bool recursive)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return FileOpResult::failure(
            path, QStringLiteral("'%1' no longer exists").arg(info.fileName()));
    }

    const QString directory = info.dir().path();
    errno = 0;
    bool removed = false;
    if (info.isDir()) {
        removed = recursive ? QDir(path).removeRecursively() : QDir().rmdir(path);
    } else {
        removed = QFile::remove(path);
    }
    if (!removed) {
        QString detail = systemError();
        if (info.isDir() && !recursive && !QDir(path).isEmpty()) {
            detail = detail.isEmpty() ? QStringLiteral("the folder is not empty")
                                      : QStringLiteral("%1 (the folder is not empty)").arg(detail);
        }
        const QString hint = permissionHint(directory);
        if (!hint.isEmpty()) {
            detail = detail.isEmpty() ? hint : QStringLiteral("%1 (%2)").arg(detail, hint);
        }
        return FileOpResult::failure(
            path, QStringLiteral("Could not delete %1").arg(describeTarget(info.fileName())), detail);
    }
    return FileOpResult::success(path);
}

} // namespace fileops
} // namespace vedit::workspace
