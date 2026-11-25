#include "vcs_toolchain.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QWidget>

#include <cstdio>
#include <future>
#include <mutex>

#if defined(VEDIT_VCS_HAVE_FFI)
#include "vd_ffi.h"
#endif

namespace vedit::workspace::vcs {

namespace {

#if defined(VEDIT_VCS_HAVE_FFI)
void probeTool(const char *tool, QString &path, QString &version, QString &source)
{
    char *rawPath = nullptr;
    VdEnvSnapshot snapshot{};
    VdEnvSnapshot launch{};
    const auto found = vd_host_vcs_probe(tool, &rawPath, &snapshot, &launch);
    if (found == 0) {
        source = QStringLiteral("missing");
        return;
    }
    path = QFile::decodeName(rawPath);
    vd_string_free(rawPath);
    source = found == 1 ? QStringLiteral("path") : QStringLiteral("env");

    QProcessEnvironment environment;
    for (size_t index = 0; index < snapshot.count; ++index) {
        const auto &row = snapshot.items[index];
        environment.insert(QString::fromLocal8Bit(row.name), QString::fromLocal8Bit(row.value));
    }
    vd_env_snapshot_free(&snapshot);
    if (launch.count == 0) {
        return;
    }
    const QString shell = QFile::decodeName(launch.items[0].name);
    QStringList arguments;
    for (size_t index = 1; index < launch.count; ++index) {
        arguments.append(QString::fromLocal8Bit(launch.items[index].name));
    }
    vd_env_snapshot_free(&launch);

    QProcess process;
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(shell, arguments);
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(200);
        return;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        version = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }
}
#endif

} // namespace

ToolchainStatus detectToolchain()
{
    ToolchainStatus status;
    status.appDir = QCoreApplication::applicationDirPath();
    status.gitSource = status.svnSource = QStringLiteral("missing");
#if defined(VEDIT_VCS_HAVE_FFI)
    probeTool("git", status.gitPath, status.gitVersion, status.gitSource);
    const int prefix = status.gitVersion.indexOf(QStringLiteral("version "));
    if (prefix >= 0) {
        status.gitVersion = status.gitVersion.mid(prefix + 8).trimmed();
    }
    probeTool("svn", status.svnPath, status.svnVersion, status.svnSource);
#endif
    return status;
}

QString toolchainStatusLine(const ToolchainStatus &status)
{
    const QString source = status.gitSource == status.svnSource
        ? status.gitSource : QStringLiteral("mixed");
    const QString missing = QStringLiteral("<missing>");
    return QStringLiteral(
               "VCS-TOOLCHAIN git=%1 version=%2 svn=%3 version=%4 source=%5 "
               "git_source=%6 svn_source=%7")
        .arg(status.gitPath.isEmpty() ? missing : status.gitPath,
             status.gitVersion.isEmpty() ? missing : status.gitVersion,
             status.svnPath.isEmpty() ? missing : status.svnPath,
             status.svnVersion.isEmpty() ? missing : status.svnVersion,
             source, status.gitSource, status.svnSource);
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

namespace {

const std::shared_future<ToolchainStatus> &toolchainProbe()
{
    static const auto probe = std::async(std::launch::async, detectToolchain).share();
    return probe;
}

} // namespace

void warmToolchainProbe()
{
    (void)toolchainProbe();
}

const ToolchainStatus &reportToolchainOnce()
{
    const ToolchainStatus &status = toolchainProbe().get();
    static std::once_flag once;
    std::call_once(once, [&status] {
        std::fprintf(stderr, "%s\n", qPrintable(toolchainStatusLine(status)));
        std::fflush(stderr);
    });
    return status;
}

} // namespace vedit::workspace::vcs
