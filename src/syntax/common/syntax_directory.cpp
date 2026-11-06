#include "syntax_directory.h"

#include <QDir>
#include <QFileInfo>

namespace vedit::syntax {

QString resolveSyntaxDefinitionDirectory(const QString &dataRootOrSyntaxDir)
{
    const QString value = dataRootOrSyntaxDir.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    const QFileInfo info(value);
    if (!info.isDir()) {
        return {};
    }
    // T84 normalisation: prefer the data root (`<value>/syntax`); if that child is
    // missing but the basename is `syntax`, the caller passed the subdirectory.
    QString dataRoot = info.absoluteFilePath();
    if (!QDir(dataRoot + QStringLiteral("/syntax")).exists()) {
        if (info.fileName() == QLatin1String("syntax") && info.dir().exists()) {
            dataRoot = info.dir().absolutePath();
        }
    }
    const QString definitions = dataRoot + QStringLiteral("/syntax");
    if (!QDir(definitions).exists()) {
        return {};
    }
    return definitions;
}

} // namespace vedit::syntax
