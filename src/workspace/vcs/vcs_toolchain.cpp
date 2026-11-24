#include "vcs_toolchain.h"

#include <QCoreApplication>
#include <QWidget>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vedit::workspace::vcs {

namespace {

// batch-20261110d — the two version probes used to run INSIDE installPanels(),
// on the GUI thread, with a 5 s wait + 1 s kill wait each: up to 12 s of frozen
// window before the first paint. They now run on one background thread that is
// started when the QApplication is constructed, so their cost overlaps Qt's own
// initialisation and the shell construction. `reportToolchainOnce()` joins that
// thread (the join is the only remaining wait); a tool that never answers is
// bounded here, and the line then names the missing version instead of lying.
constexpr int kVersionProbeMs = 1500;
constexpr int kVersionProbeKillMs = 200;

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
    // batch-20261110d: stdin is /dev/null and the wait is bounded — the probe
    // runs on a worker thread now (see the background probe below), but a tool
    // that blocks must still bound how long installPanels() may wait for it.
    // The old bound was 5 s + a 1 s kill wait PER TOOL, all of it on the GUI
    // thread; 1.5 s is ~40x a healthy `git --version` (measured 10-40 ms).
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(path, arguments);
    if (!process.waitForFinished(kVersionProbeMs)) {
        process.kill();
        process.waitForFinished(kVersionProbeKillMs);
        return QString();
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

namespace {

// batch-20261110d — where the version probes run. `detectToolchain()` used to be
// called from installPanels() on the GUI thread; the two children (`git
// --version`, `svn --version --quiet`) therefore delayed the first paint by up
// to 6 s each. The probe now runs on ONE background thread that the
// Q_COREAPP_STARTUP_FUNCTION hook below starts as soon as the QApplication
// exists, so both children overlap Qt's own initialisation and the shell
// construction. `reportToolchainOnce()` joins that thread: in the measured
// launcher (see evidence/batch-20261110d/startup/) the probes finish BEFORE
// installPanels() is reached, so the join costs nothing; if a tool is slow the
// wait is bounded by kVersionProbeMs instead of extending the startup path.
ToolchainStatus detectToolchainAt(const QString &appDir)
{
    ToolchainStatus status;
    status.appDir = appDir;
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

QString probeApplicationDirectory()
{
    // The probe starts while QApplication is still being constructed, where
    // applicationDirPath() is not yet a stable answer. /proc/self/exe names the
    // SAME directory in both launch forms: <AppDir>/bin for a direct start, and
    // the AppDir ROOT when AppRun enters through the bundled loader (which is
    // exactly what applicationDirPath() reports there too, see
    // packaging/runtime/AppRun).
    const QString executable = QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget();
    if (!executable.isEmpty()) {
        return QFileInfo(executable).absolutePath();
    }
    return QCoreApplication::applicationDirPath();
}

struct BackgroundProbe {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    ToolchainStatus status;
    std::thread worker;

    // Join in the destructor: a process that exits between the hook and the
    // first reportToolchainOnce() must not tear the statics down under a running
    // probe (the join is bounded by the probe's own wait, see toolVersion()).
    ~BackgroundProbe()
    {
        if (worker.joinable()) {
            worker.join();
        }
    }
};

BackgroundProbe &backgroundProbe()
{
    static BackgroundProbe probe;
    return probe;
}

void probeInBackground()
{
    const ToolchainStatus detected = detectToolchainAt(probeApplicationDirectory());
    BackgroundProbe &probe = backgroundProbe();
    {
        std::lock_guard<std::mutex> lock(probe.mutex);
        probe.status = detected;
        probe.done = true;
    }
    probe.ready.notify_all();
}

} // namespace

ToolchainStatus detectToolchain()
{
    return detectToolchainAt(QCoreApplication::applicationDirPath());
}

void warmToolchainProbe()
{
    // Started once per process, from the QCoreApplication startup hook below —
    // i.e. BEFORE main() reaches installPanels().
    static std::once_flag once;
    std::call_once(once, [] {
        BackgroundProbe &probe = backgroundProbe();
        probe.worker = std::thread(probeInBackground);
    });
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
    warmToolchainProbe();
    BackgroundProbe &probe = backgroundProbe();
    {
        std::unique_lock<std::mutex> lock(probe.mutex);
        probe.ready.wait(lock, [&probe] { return probe.done; });
    }
    // One `VCS-TOOLCHAIN ...` line per process, printed by the lane's one init
    // call as before — the difference is only WHERE the two children ran.
    static std::once_flag once;
    std::call_once(once, [&probe] {
        std::fprintf(stderr, "%s\n", qPrintable(toolchainStatusLine(probe.status)));
        std::fflush(stderr);
    });
    return probe.status;
}

} // namespace vedit::workspace::vcs

namespace {

// batch-20261110d: the hook is what moves the probes off the startup path; it
// only starts the thread (no I/O, no wait). Qualified names cannot go into
// Q_COREAPP_STARTUP_FUNCTION (its implementation token-pastes the name).
void startVcsToolchainProbe()
{
    if (!qEnvironmentVariableIsSet("VEDIT_STARTUP_WELCOME_CHILD")) {
        vedit::workspace::vcs::warmToolchainProbe();
    }
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(startVcsToolchainProbe)
