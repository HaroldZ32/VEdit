#include "syntax_directory.h"

#include <QCoreApplication>
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

QString syntaxDefinitionFileName(const QString &mode)
{
    return mode == QStringLiteral("Verilog") ? QStringLiteral("vedit-verilog.xml")
                                             : QStringLiteral("vedit-systemverilog.xml");
}

QString applicationSyntaxDefinitionDirectory()
{
    const QString fromEnvironment = qEnvironmentVariable("VEDIT_SYNTAX_DIR");
    if (!fromEnvironment.isEmpty()) {
        const QString resolved = resolveSyntaxDefinitionDirectory(fromEnvironment);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    // Bare build: the checkout's own definitions (the delivered AppRun exports
    // VEDIT_SYNTAX_DIR, so this is only the developer path).
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        const QString candidate = dir.filePath(QStringLiteral("resources/syntax"));
        if (QFileInfo(candidate + QStringLiteral("/vedit-systemverilog.xml")).isFile()) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

} // namespace vedit::syntax
