#include "vcs_toolchain.h"

#include <QCoreApplication>
#include <QWidget>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <cstdio>
#include <mutex>

namespace vedit::workspace::vcs {

namespace {

bool isExecutable(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

QString firstExecutable(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QString absolute = QFileInfo(candidate).absoluteFilePath();
        if (isExecutable(absolute)) {
            return absolute;
        }
    }
    return QString();
}

QString resolveWithSource(const QString &tool, QString *source)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QString found = bundledTool(appDir, tool);
    if (!found.isEmpty()) {
        *source = QStringLiteral("bundled");
        return found;
    }
    found = toolOnPath(tool);
    if (!found.isEmpty()) {
        *source = QStringLiteral("path");
        return found;
    }
    found = toolFromEnv(tool);
    if (!found.isEmpty()) {
        *source = QStringLiteral("env");
        return found;
    }
    *source = QStringLiteral("missing");
    return QString();
}

QString gitVersionFrom(const QString &raw)
{
    const int index = raw.indexOf(QStringLiteral("version "));
    return index >= 0 ? raw.mid(index + 8).trimmed() : raw;
}

} // namespace

QString bundledTool(const QString &appDir, const QString &tool)
{
    // Same candidates as crates/vd-vcs/src/toolchain.rs::bundled_candidates.
    // `appDir` is <appdir>/bin for a direct start, and the AppDir ROOT when the
    // product was started through the AppDir loader (applicationDirPath() then
    // names the loader's directory).
    return firstExecutable({
        appDir + QStringLiteral("/../usr/lib/vcs/bin/") + tool,
        appDir + QStringLiteral("/usr/lib/vcs/bin/") + tool,
        appDir + QLatin1Char('/') + tool,
        appDir + QStringLiteral("/bin/") + tool,
    });
}

QString toolOnPath(const QString &tool)
{
    const QString path = qEnvironmentVariable("PATH");
    QStringList candidates;
    const QStringList entries = path.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        candidates.append(entry + QLatin1Char('/') + tool);
    }
    return firstExecutable(candidates);
}

QString toolFromEnv(const QString &tool)
{
    const QString name = tool == QStringLiteral("git")  ? QStringLiteral("VEDIT_GIT")
        : tool == QStringLiteral("svn")                 ? QStringLiteral("VEDIT_SVN")
                                                        : QString();
    if (name.isEmpty()) {
        return QString();
    }
    const QString value = qEnvironmentVariable(name.toUtf8().constData());
    if (value.isEmpty()) {
        return QString();
    }
    const QString absolute = QFileInfo(value).absoluteFilePath();
    return isExecutable(absolute) ? absolute : QString();
}

QString toolVersion(const QString &path, const QStringList &arguments)
{
    if (path.isEmpty()) {
        return QString();
    }
    QProcess process;
    // The product runs with the AppDir's LD_LIBRARY_PATH; the bundled wrapper
    // starts with the HOST loader and re-execs the AppDir loader itself, so the
    // bundle must not leak into the shell that starts it.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
    process.setProcessEnvironment(environment);
    process.start(path, arguments);
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

ToolchainStatus detectToolchain()
{
    ToolchainStatus status;
    status.appDir = QCoreApplication::applicationDirPath();
    status.gitPath = resolveWithSource(QStringLiteral("git"), &status.gitSource);
    status.svnPath = resolveWithSource(QStringLiteral("svn"), &status.svnSource);
    if (!status.gitPath.isEmpty()) {
        status.gitVersion =
            gitVersionFrom(toolVersion(status.gitPath, {QStringLiteral("--version")}));
    }
    if (!status.svnPath.isEmpty()) {
        status.svnVersion = toolVersion(
            status.svnPath, {QStringLiteral("--version"), QStringLiteral("--quiet")});
    }
    return status;
}

QString toolchainStatusLine(const ToolchainStatus &status)
{
    QString source = QStringLiteral("missing");
    if (status.gitSource == status.svnSource) {
        source = status.gitSource;
    } else if (!status.gitPath.isEmpty() || !status.svnPath.isEmpty()) {
        source = QStringLiteral("mixed");
    }
    const QString missing = QStringLiteral("<missing>");
    return QStringLiteral(
               "VCS-TOOLCHAIN git=%1 version=%2 svn=%3 version=%4 source=%5 "
               "git_source=%6 svn_source=%7")
        .arg(status.gitPath.isEmpty() ? missing : status.gitPath,
             status.gitVersion.isEmpty() ? missing : status.gitVersion,
             status.svnPath.isEmpty() ? missing : status.svnPath,
             status.svnVersion.isEmpty() ? missing : status.svnVersion,
             source,
             status.gitSource,
             status.svnSource);
}

void applyToolchainProperties(QWidget *widget, const ToolchainStatus &status)
{
    if (widget == nullptr) {
        return;
    }
    widget->setProperty("veditVcsToolchain", toolchainStatusLine(status));
    widget->setProperty("veditVcsGitPath", status.gitPath);
    widget->setProperty("veditVcsGitVersion", status.gitVersion);
    widget->setProperty("veditVcsSvnPath", status.svnPath);
    widget->setProperty("veditVcsSvnVersion", status.svnVersion);
}

const ToolchainStatus &reportToolchainOnce()
{
    static ToolchainStatus status;
    static std::once_flag once;
    std::call_once(once, [] {
        status = detectToolchain();
        std::fprintf(stderr, "%s\n", qPrintable(toolchainStatusLine(status)));
        std::fflush(stderr);
    });
    return status;
}

} // namespace vedit::workspace::vcs
