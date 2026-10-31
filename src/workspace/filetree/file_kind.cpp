#include "file_kind.h"

#include <QFileInfo>

namespace vedit::workspace {

FileKind fileKindForPath(const QString &path, bool isDirectory)
{
    if (isDirectory) {
        return FileKind::Directory;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("v") || suffix == QLatin1String("vh")) {
        return FileKind::Verilog;
    }
    if (suffix == QLatin1String("sv") || suffix == QLatin1String("svh")) {
        return FileKind::SystemVerilog;
    }
    if (suffix == QLatin1String("f") || suffix == QLatin1String("filelist")) {
        return FileKind::FileList;
    }
    return FileKind::Other;
}

QString fileKindId(FileKind kind)
{
    switch (kind) {
    case FileKind::Directory:
        return QStringLiteral("directory");
    case FileKind::Verilog:
        return QStringLiteral("verilog");
    case FileKind::SystemVerilog:
        return QStringLiteral("systemverilog");
    case FileKind::FileList:
        return QStringLiteral("filelist");
    case FileKind::Other:
        break;
    }
    return QStringLiteral("other");
}

QString fileKindLabel(FileKind kind)
{
    switch (kind) {
    case FileKind::Directory:
        return QStringLiteral("Folder");
    case FileKind::Verilog:
        return QStringLiteral("Verilog");
    case FileKind::SystemVerilog:
        return QStringLiteral("SystemVerilog");
    case FileKind::FileList:
        return QStringLiteral("File list");
    case FileKind::Other:
        break;
    }
    return QStringLiteral("File");
}

} // namespace vedit::workspace
