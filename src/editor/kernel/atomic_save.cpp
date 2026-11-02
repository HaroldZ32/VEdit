#include "atomic_save.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vedit::editor {
namespace {

std::atomic<int> g_tmpCounter{0};

QByteArray faultInjection()
{
    return qgetenv("VEDIT_ATOMIC_FAULT");
}

bool faultApplies(const char *stage)
{
    return faultInjection() == QByteArray(stage);
}

AtomicSaveResult failure(const QString &message)
{
    AtomicSaveResult result;
    result.ok = false;
    result.error = message;
    return result;
}

QString describe(const QString &step)
{
    return QStringLiteral("%1 failed: %2").arg(step, QString::fromLocal8Bit(std::strerror(errno)));
}

bool writeAll(int fd, const QByteArray &contents)
{
    qsizetype written = 0;
    while (written < contents.size()) {
        const ssize_t chunk = ::write(fd, contents.constData() + written,
                                      static_cast<size_t>(contents.size() - written));
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += chunk;
    }
    return true;
}

} // namespace

AtomicSaveResult atomicSaveText(const QString &targetPath, const QByteArray &contents)
{
    AtomicSaveResult result;

    const QFileInfo targetInfo(targetPath);
    const QDir directory = targetInfo.absoluteDir();
    if (!directory.exists()) {
        return failure(QStringLiteral("cannot save %1: directory %2 does not exist")
                           .arg(targetPath, directory.absolutePath()));
    }
    if (!QFileInfo(directory.absolutePath()).isWritable()) {
        return failure(QStringLiteral("cannot save %1: directory %2 is not writable")
                           .arg(targetPath, directory.absolutePath()));
    }

    const QString tmpPath = directory.absoluteFilePath(
        QStringLiteral(".%1.vedit-tmp-%2-%3")
            .arg(targetInfo.fileName())
            .arg(::getpid())
            .arg(g_tmpCounter.fetch_add(1, std::memory_order_relaxed)));

    mode_t mode = S_IRUSR | S_IWUSR;
    if (targetInfo.exists()) {
        struct stat existing {};
        if (::stat(QFile::encodeName(targetPath).constData(), &existing) == 0) {
            mode = existing.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        }
    }

    const QByteArray tmpEncoded = QFile::encodeName(tmpPath);
    int fd = ::open(tmpEncoded.constData(), O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd < 0) {
        return failure(QStringLiteral("cannot create the temporary file %1: %2")
                           .arg(tmpPath, QString::fromLocal8Bit(std::strerror(errno))));
    }

    const auto abandon = [&tmpPath, &tmpEncoded](const QString &message) {
        ::unlink(tmpEncoded.constData());
        AtomicSaveResult failed;
        failed.ok = false;
        failed.error = message;
        return failed;
    };

    if (faultApplies("write") || !writeAll(fd, contents)) {
        const QString message = faultApplies("write")
            ? QStringLiteral("atomic save: injected write failure")
            : describe(QStringLiteral("write"));
        ::close(fd);
        return abandon(message);
    }
    if (faultApplies("fsync") || ::fsync(fd) != 0) {
        const QString message = faultApplies("fsync")
            ? QStringLiteral("atomic save: injected fsync failure")
            : describe(QStringLiteral("fsync(file)"));
        ::close(fd);
        return abandon(message);
    }
    if (::close(fd) != 0) {
        return abandon(describe(QStringLiteral("close")));
    }

    if (faultApplies("rename")) {
        return abandon(QStringLiteral("atomic save: injected rename failure"));
    }
    if (::rename(tmpEncoded.constData(), QFile::encodeName(targetPath).constData()) != 0) {
        return abandon(describe(QStringLiteral("rename")));
    }

    // The rename is the commit point: from here on the target holds the new
    // bytes, so a directory fsync problem is a warning (see header).
    const QByteArray dirEncoded = QFile::encodeName(directory.absolutePath());
    const int dirFd = ::open(dirEncoded.constData(), O_RDONLY | O_DIRECTORY);
    if (dirFd >= 0) {
        if (faultApplies("fsync-dir")) {
            result.diagnostics.append(QStringLiteral("atomic save: injected directory fsync failure"));
            result.ok = true;
            result.diagnostics.append(QStringLiteral("directory fsync skipped by fault injection"));
        } else if (::fsync(dirFd) != 0) {
            if (errno == EINVAL || errno == ENOTSUP) {
                result.diagnostics.append(
                    QStringLiteral("directory fsync not supported here (%1); the rename is complete")
                        .arg(QString::fromLocal8Bit(std::strerror(errno))));
            } else {
                result.error = describe(QStringLiteral("fsync(directory)"));
                result.ok = false;
                ::close(dirFd);
                return result;
            }
        }
        ::close(dirFd);
    } else {
        result.diagnostics.append(QStringLiteral("cannot open the directory for fsync: %1")
                                      .arg(QString::fromLocal8Bit(std::strerror(errno))));
    }

    result.ok = true;
    return result;
}

} // namespace vedit::editor
