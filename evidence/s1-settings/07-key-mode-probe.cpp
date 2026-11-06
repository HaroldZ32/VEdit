// Falsification control for T10's key-file acceptance test:
//   old path  = QFile::open (0666 & ~umask), then chmod
//   new path  = POSIX open(O_CREAT|O_EXCL|O_WRONLY, 0600), verified with fstat
// Run under `umask 000`: the old path must show group/other write, the new one
// must be 0600 at creation.
#include <QFile>
#include <QString>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
    const QString oldPath = QStringLiteral("old-way-key");
    const QString newPath = QStringLiteral("new-way-key");
    QFile::remove(oldPath);
    QFile::remove(newPath);

    QFile file(oldPath);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    struct stat oldStat {};
    ::stat(oldPath.toUtf8().constData(), &oldStat);
    std::printf("OLD opened=%d mode-before-chmod=%04o\n", opened ? 1 : 0,
                static_cast<unsigned>(oldStat.st_mode & 07777));
    file.close();
    QFile::remove(oldPath);

    const int fd = ::open(newPath.toUtf8().constData(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    struct stat newStat {};
    ::fstat(fd, &newStat);
    std::printf("NEW fd=%d mode-at-creation=%04o\n", fd,
                static_cast<unsigned>(newStat.st_mode & 07777));
    ::close(fd);
    QFile::remove(newPath);
    return 0;
}
