// T117: prove the crash self-reporter works (never "written but unverified").
//
//   vedit-app-crash-reporter-selftest <work-dir>
//
// Each case runs in a forked child so the parent can inspect the report the
// handler wrote after a real signal. The shipped binary is covered separately by
// the `vedit-app-crash-report-shipped` ctest (it runs `vedit-app
// --crash-test=segv`), so this file also guards against the reporter silently
// degrading.
#include "crash_reporter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <csignal>
#include <cstdio>
#include <cstring>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", detail.toUtf8().constData());
    std::fflush(stdout);
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString findReport(const QString &stateRoot)
{
    const QDir directory(stateRoot + QStringLiteral("/vedit"));
    const QStringList files = directory.entryList(QStringList{QStringLiteral("crash-*.log")},
                                                  QDir::Files, QDir::Time);
    return files.isEmpty() ? QString() : directory.absoluteFilePath(files.first());
}

// Runs `signal` in a child; returns the report path (empty on failure). The child
// installs the reporter, records the canary event and the open file, then raises.
QString crashChild(const QString &stateRoot, int signal, int *waitStatus, const char *canary)
{
    const QByteArray state = stateRoot.toLocal8Bit();
    QDir().mkpath(stateRoot);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::setenv("XDG_STATE_HOME", state.constData(), 1);
        vedit::app::crash::install(QStringLiteral("crash-selftest"));
        vedit::app::crash::event("canary %s", canary != nullptr ? canary : "none");
        vedit::app::crash::setCurrentFile(QStringLiteral("/tmp/vedit-crash-selftest/design.sv"));
        // The report is the deliverable; do not litter the build dir with cores.
        struct rlimit limit {};
        limit.rlim_cur = 0;
        limit.rlim_max = 0;
        ::setrlimit(RLIMIT_CORE, &limit);
        ::raise(signal);
        ::_exit(97); // the signal did not kill us: the reporter is broken
    }
    if (pid < 0) {
        return {};
    }
    if (waitStatus != nullptr) {
        ::waitpid(pid, waitStatus, 0);
    } else {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
    return findReport(stateRoot);
}

void checkSegv(const QString &stateRoot)
{
    QDir(stateRoot).removeRecursively();
    int status = 0;
    const QString report = crashChild(stateRoot, SIGSEGV, &status, "segv-canary-42");
    check("crash.segv.signal-died", WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
          QStringLiteral("status=%1").arg(status));
    check("crash.segv.report-file", !report.isEmpty(), report);
    if (report.isEmpty()) {
        return;
    }
    const QString text = readText(report);
    check("crash.segv.signal-name", text.contains(QStringLiteral("signal=11 (SIGSEGV)")),
          text.left(80));
    check("crash.segv.si-code", text.contains(QStringLiteral("si_code=")));
    check("crash.segv.fault-address", text.contains(QStringLiteral("fault_address=0x")));
    check("crash.segv.build-info", text.contains(QStringLiteral("build=crash-selftest")));
    check("crash.segv.open-file", text.contains(QStringLiteral("/tmp/vedit-crash-selftest/design.sv")));
    check("crash.segv.events", text.contains(QStringLiteral("canary segv-canary-42")));
    check("crash.segv.backtrace", text.contains(QStringLiteral("backtrace:")));
    // -rdynamic + backtrace_symbols_fd: at least one frame names our code.
    check("crash.segv.symbolized", text.contains(QStringLiteral("vedit_crash_handler")),
          QStringLiteral("the backtrace must name the crash handler"));
    check("crash.segv.ends", text.contains(QStringLiteral("end")));
    std::printf("CRASH-REPORT %s\n", report.toUtf8().constData());
}

void checkAbort(const QString &stateRoot)
{
    QDir(stateRoot).removeRecursively();
    int status = 0;
    const QString report = crashChild(stateRoot, SIGABRT, &status, "abort-canary");
    check("crash.abort.signal-died", WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
          QStringLiteral("status=%1").arg(status));
    const QString text = report.isEmpty() ? QString() : readText(report);
    check("crash.abort.signal-name", text.contains(QStringLiteral("SIGABRT")));
    check("crash.abort.events", text.contains(QStringLiteral("abort-canary")));
}

void checkNormalExitIsQuiet(const QString &stateRoot)
{
    QDir(stateRoot).removeRecursively();
    QDir().mkpath(stateRoot);
    const QByteArray state = stateRoot.toLocal8Bit();
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::setenv("XDG_STATE_HOME", state.constData(), 1);
        vedit::app::crash::install(QStringLiteral("crash-selftest"));
        vedit::app::crash::event("normal-exit");
        ::_exit(0);
    }
    int status = 0;
    if (pid > 0) {
        ::waitpid(pid, &status, 0);
    }
    check("crash.normal-exit.code", WIFEXITED(status) && WEXITSTATUS(status) == 0,
          QStringLiteral("status=%1").arg(status));
    check("crash.normal-exit.no-report", findReport(stateRoot).isEmpty(),
          findReport(stateRoot));
}

} // namespace

int main(int argc, char **argv)
{
    const QString work = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("/tmp/vedit-crash-selftest");
    const QString stateRoot = work + QStringLiteral("/crash-reporter-state");
    std::printf("CRASH-SELFTEST start work=%s\n", work.toUtf8().constData());

    checkSegv(stateRoot + QStringLiteral("/segv"));
    checkAbort(stateRoot + QStringLiteral("/abort"));
    checkNormalExitIsQuiet(stateRoot + QStringLiteral("/normal"));

    std::printf("CRASH-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0, g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
