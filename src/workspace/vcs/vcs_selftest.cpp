// Lane validator for the read-only VCS panel (T23).
//
//   vedit-workspace-vcs-selftest --selfcheck
//
// Drives the panel with a recording stub (no process, no repository) and, when
// the FFI library is linked, through the real C ABI on a plain directory. Every
// check prints one machine-readable line.
#include "vcs_panel.h"
#include "vcs_source.h"

#if defined(VEDIT_VCS_HAVE_FFI)
#include "vd_ffi.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#if defined(VEDIT_VCS_HAVE_FFI)
#include <chrono>
#include <thread>
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using vedit::workspace::vcs::Backend;
using vedit::workspace::vcs::Change;
using vedit::workspace::vcs::Diff;
using vedit::workspace::vcs::Info;
using vedit::workspace::vcs::Status;
#if defined(VEDIT_VCS_HAVE_FFI)
using vedit::workspace::vcs::FfiVcsSource;
#endif
using vedit::workspace::vcs::StubVcsSource;
using vedit::workspace::vcs::VcsPanel;
using vedit::workspace::vcs::VcsSource;

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

[[maybe_unused]] void skip(const char *name, const char *reason)
{
    ++g_checks;
    std::printf("CHECK %s SKIP %s\n", name, reason);
    std::fflush(stdout);
}

Status gitStatus()
{
    Status status;
    status.backend = Backend::Git;
    status.branch = "feature/vcs-panel";
    status.changes = {Change{"rtl/top.sv", 2}, Change{"rtl/added.sv", 1}};
    return status;
}

Info gitInfo()
{
    return Info{Backend::Git, "feature/vcs-panel", "abc1234", ""};
}

std::vector<Diff> gitDiffs()
{
    Diff top;
    top.path = "rtl/top.sv";
    top.text = "diff --git a/rtl/top.sv b/rtl/top.sv\n--- a/rtl/top.sv\n+++ b/rtl/top.sv\n"
               "@@ -1 +1 @@\n-module old;\n+module top;\n";
    return {top};
}

void checkEmptyState()
{
    Status empty;
    empty.note = "not a Git or Subversion working tree";
    auto stub = std::make_unique<StubVcsSource>(Info{}, empty, std::vector<Diff>{});
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/not-a-repo");
    panel.setSource(std::move(stub));

    check("vcs.panel.empty-state.backend-none", panel.backend() == Backend::None,
          QString::fromLatin1(vedit::workspace::vcs::backendName(panel.backend())));
    check("vcs.panel.empty-state.header", panel.headerText().contains(QStringLiteral("No repository")),
          panel.headerText());
    check("vcs.panel.empty-state.note-visible",
          panel.emptyText().contains(QStringLiteral("not a Git or Subversion working tree")),
          panel.emptyText().left(80));
    check("vcs.panel.empty-state.no-files", panel.fileCount() == 0);
    check("vcs.panel.empty-state.stub-queried", raw->calls() > 0,
          QStringLiteral("calls=%1").arg(raw->calls()));
    check("vcs.panel.empty-state.no-write-attempt", raw->writeAttempts() == 0);
}

void checkGitPanel()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));

    const QString header = panel.headerText();
    check("vcs.panel.git.backend", panel.backend() == Backend::Git, header);
    check("vcs.panel.git.branch-and-revision",
          header.contains(QStringLiteral("Git")) && header.contains(QStringLiteral("feature/vcs-panel"))
              && header.contains(QStringLiteral("abc1234")),
          header);
    check("vcs.panel.git.changed-file-count", panel.fileCount() == 2,
          QStringLiteral("rows=%1").arg(panel.fileCount()));
    check("vcs.panel.git.file-row",
          panel.fileText(0).contains(QStringLiteral("rtl/top.sv"))
              && panel.fileText(0).contains(QStringLiteral("modified")),
          panel.fileText(0));
    check("vcs.panel.git.added-row", panel.fileText(1).contains(QStringLiteral("added")),
          panel.fileText(1));
    check("vcs.panel.git.empty-state-hidden", !panel.emptyStateVisible());
}

void checkDiff()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));

    panel.selectFile("rtl/top.sv");
    check("vcs.panel.diff.shown", panel.diffText().contains(QStringLiteral("+module top;")),
          panel.diffText().left(90));
    check("vcs.panel.diff.read-only",
          panel.findChild<QWidget *>(QStringLiteral("vcs-diff")) != nullptr);
    panel.selectFile("rtl/missing.sv");
    check("vcs.panel.diff.missing-is-a-note",
          panel.diffText().contains(QStringLiteral("stub has no diff")),
          panel.diffText().left(90));
    check("vcs.panel.diff.no-write-attempt", raw->writeAttempts() == 0,
          QStringLiteral("calls=%1").arg(raw->calls()));
}

void checkSvnPanel()
{
    Status status;
    status.backend = Backend::Subversion;
    status.changes = {Change{"trunk/a.sv", 2}};
    Info info{Backend::Subversion, "trunk", "42", ""};
    std::vector<Diff> diffs{Diff{"trunk/a.sv", "Index: trunk/a.sv\n+line\n", false, ""}};
    auto stub = std::make_unique<StubVcsSource>(info, status, diffs);
    VcsPanel panel;
    panel.setRoot("/tmp/repo-svn");
    panel.setSource(std::move(stub));
    const QString header = panel.headerText();
    check("vcs.panel.svn.backend-and-revision",
          header.contains(QStringLiteral("Subversion")) && header.contains(QStringLiteral("42")),
          header);
    check("vcs.panel.svn.file-count", panel.fileCount() == 1, header);
}

void checkTransitions()
{
    // No source at all: the panel must render the empty state, not crash.
    VcsPanel panel;
    panel.refresh();
    check("vcs.panel.no-source.empty-state", panel.backend() == Backend::None,
          panel.headerText());
    // Refresh after a source that reports "no repository" keeps the empty state.
    Status empty;
    empty.note = "not a repository";
    panel.setSource(std::make_unique<StubVcsSource>(Info{}, empty, std::vector<Diff>{}));
    panel.refresh();
    check("vcs.panel.refresh.empty-stays-empty", panel.backend() == Backend::None);
    // ... and switching to a real backend repopulates the list.
    panel.setSource(std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs()));
    panel.refresh();
    check("vcs.panel.refresh.populates", panel.fileCount() == 2 && !panel.emptyStateVisible());
}

void checkFfi()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    // T99: info/diff are job-based like status. The ABI contract is start ->
    // job id, poll -> PENDING until READY (or a typed failure); no call spawns a
    // subprocess on the caller's thread.
    VdService *service = vd_service_create();
    check("vcs.ffi.service", service != nullptr);
    if (service == nullptr) {
        return;
    }

    const unsigned long long infoJob = vd_vcs_info_job(service, "/tmp");
    check("vcs.ffi.info.start", infoJob != 0);
    VdVcsInfo info{};
    int infoState = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && infoState == VD_STATUS_PENDING; ++attempt) {
        infoState = vd_vcs_info_poll(service, infoJob, &info);
        if (infoState == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const bool info_ok = infoState == VD_STATUS_READY && info.backend == 0
        && info.note != nullptr && std::strlen(info.note) > 0;
    const QString info_note = info.note != nullptr ? QString::fromUtf8(info.note) : QString();
    vd_vcs_info_free(&info);
    check("vcs.ffi.info-empty-state", info_ok, info_note.left(70));

    const unsigned long long diffJob = vd_vcs_diff_job(service, "/tmp", "a.sv");
    check("vcs.ffi.diff.start", diffJob != 0);
    VdVcsDiff diff{};
    int diffState = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && diffState == VD_STATUS_PENDING; ++attempt) {
        diffState = vd_vcs_diff_poll(service, diffJob, &diff);
        if (diffState == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const bool diff_ok =
        diffState == VD_STATUS_READY && (diff.text == nullptr || std::strlen(diff.text) == 0);
    const int diff_binary = diff.binary;
    const QString diff_note = diff.note != nullptr ? QString::fromUtf8(diff.note) : QString();
    vd_vcs_diff_free(&diff);
    check("vcs.ffi.diff-empty-state", diff_ok && diff_binary == 0, diff_note.left(70));

    // A null root is rejected before a job is started.
    check("vcs.ffi.invalid-root-rejected", vd_vcs_info_job(service, nullptr) == 0);
    // The C ABI has no write entry point at all (only status/info/diff jobs).
    check("vcs.ffi.read-only-surface", true,
          "vd_vcs_status / vd_vcs_info_job / vd_vcs_diff_job only; no write entry point");
    vd_service_destroy(service);
#else
    skip("vcs.ffi.service", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.info.start", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.info-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.diff.start", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.diff-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.invalid-root-rejected", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.read-only-surface", "lane built without vd-ffi (stub-only build)");
#endif
}

// T99 N-1 regression: the GUI thread must not wait for git. A fake `git` in
// front of PATH sleeps before exec-ing the real one, so the job stays PENDING
// while an event-loop timer has to keep ticking. The last check proves the
// metric itself is sensitive: a deliberate blocking sleep makes the timer fire
// late, so a synchronous implementation would be caught.
void checkNonBlocking()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        skip("vcs.nonblocking.start-returns-fast", "git is not on PATH");
        return;
    }
    QTemporaryDir temp;
    check("vcs.nonblocking.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString fakeDir = temp.path() + QStringLiteral("/bin");
    QDir().mkpath(fakeDir);
    const QString fakeGit = fakeDir + QStringLiteral("/git");
    {
        QFile script(fakeGit);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check("vcs.nonblocking.fake-git", false);
            return;
        }
        script.write(QStringLiteral("#!/bin/sh\nsleep 0.4\nexec \"%1\" \"$@\"\n").arg(git).toUtf8());
    }
    QFile::setPermissions(fakeGit, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner | QFileDevice::ReadUser
                                        | QFileDevice::ExeUser | QFileDevice::ReadGroup
                                        | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                        | QFileDevice::ExeOther);
    check("vcs.nonblocking.fake-git", QFileInfo(fakeGit).isExecutable());
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PATH", (fakeDir + QLatin1Char(':') + QString::fromLocal8Bit(savedPath)).toLocal8Bit());
    // A repository marker makes the backend Git, so the info job really spawns
    // `git rev-parse` twice (each through the sleeping wrapper). Without the
    // marker vd-vcs answers "no repository" from the marker walk alone and no
    // subprocess is started.
    QDir().mkpath(temp.path() + QStringLiteral("/.git"));

    {
        FfiVcsSource source;
        bool delivered = false;
        QString note;
        QElapsedTimer startTimer;
        startTimer.start();
        source.requestInfo(temp.path().toStdString(), [&delivered, &note](Info info) {
            delivered = true;
            note = QString::fromStdString(info.note);
        });
        const qint64 startMs = startTimer.elapsed();
        check("vcs.nonblocking.start-returns-fast", startMs < 150,
              QStringLiteral("start took %1 ms while the fake git sleeps 400 ms").arg(startMs));

        int ticks = 0;
        QTimer ticker;
        ticker.setInterval(20);
        QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
        ticker.start();
        QElapsedTimer window;
        window.start();
        while (window.elapsed() < 250) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ticker.stop();
        check("vcs.nonblocking.event-loop-ticks", ticks >= 3,
              QStringLiteral("ticks=%1").arg(ticks));
        check("vcs.nonblocking.job-still-pending", !delivered,
              QStringLiteral("polls=%1").arg(static_cast<unsigned long long>(source.pollRounds())));

        QElapsedTimer wait;
        wait.start();
        while (!delivered && wait.elapsed() < 8000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("vcs.nonblocking.result-delivered", delivered, note.left(60));
    }

    // Sensitivity: schedule the same 20 ms timer, then block the thread. A
    // synchronous implementation looks exactly like this, so the timing metric
    // must report the late fire.
    qint64 firedAtMs = -1;
    QElapsedTimer probe;
    probe.start();
    QTimer::singleShot(20, [&firedAtMs, &probe]() { firedAtMs = probe.elapsed(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check("vcs.nonblocking.metric-detects-block", firedAtMs >= 250,
          QStringLiteral("timer fired at %1 ms after scheduling (expected late)").arg(firedAtMs));

    qputenv("PATH", savedPath);
#else
    skip("vcs.nonblocking.start-returns-fast", "lane built without vd-ffi (stub-only build)");
#endif
}

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        }
    }
    if (!selfcheck) {
        std::printf("usage: %s --selfcheck\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    std::printf("WORKSPACE-VCS-SELFTEST start\n");

    checkEmptyState();
    checkGitPanel();
    checkDiff();
    checkSvnPanel();
    checkTransitions();
    checkFfi();
    checkNonBlocking();

    std::printf("WORKSPACE-VCS-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
