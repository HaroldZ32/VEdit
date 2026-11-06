// Lane validator for the read-only VCS panel (T23).
//
//   vedit-workspace-vcs-selftest --selfcheck
//
// Drives the panel with a recording stub (no process, no repository) and, when
// the FFI library is linked, through the real C ABI on a plain directory. Every
// check prints one machine-readable line.
#include "vcs_history.h"
#include "vcs_panel.h"
#include "vcs_source.h"
#include "vcs_toolchain.h"

#if defined(VEDIT_VCS_HAVE_FFI)
#include "vd_ffi.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTranslator>
#include <QWidget>

#include <chrono>
#include <functional>
#include <thread>

#include <cctype>
#include <cstdio>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(VEDIT_VCS_HAVE_FFI)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <mutex>

#include <chrono>
#include <thread>
#endif

using vedit::workspace::vcs::Backend;
using vedit::workspace::vcs::Change;
using vedit::workspace::vcs::Diff;
using vedit::workspace::vcs::Info;
using vedit::workspace::vcs::RefEntry;
using vedit::workspace::vcs::RefKind;
using vedit::workspace::vcs::RemoteEntry;
using vedit::workspace::vcs::Status;
using vedit::workspace::vcs::BlameLine;
using vedit::workspace::vcs::CommitEntry;
using vedit::workspace::vcs::FileStat;
using vedit::workspace::vcs::GitOperation;
using vedit::workspace::vcs::HistoryData;
using vedit::workspace::vcs::HistoryKind;
using vedit::workspace::vcs::HistoryRequest;
using vedit::workspace::vcs::PanelAction;
using vedit::workspace::vcs::capabilityFor;
using vedit::workspace::vcs::capabilityReport;
using vedit::workspace::vcs::capabilitiesFor;
using vedit::workspace::vcs::disabledActionReasons;
using vedit::workspace::vcs::WorkspaceData;
using vedit::workspace::vcs::WorkspaceDialog;
using vedit::workspace::vcs::WorkspaceEntry;
using vedit::workspace::vcs::WorkspaceKind;
using vedit::workspace::vcs::WorkspaceRequest;
using vedit::workspace::vcs::WriteOp;
using vedit::workspace::vcs::WriteRequest;
using vedit::workspace::vcs::WriteResult;
using vedit::workspace::vcs::writeOpIsIrreversible;
using vedit::workspace::vcs::writeOpIsNetwork;
using vedit::workspace::vcs::writeResultNeedsCredentials;
#if defined(VEDIT_VCS_HAVE_FFI)
using vedit::workspace::vcs::FfiVcsSource;
#endif
using vedit::workspace::vcs::StubVcsSource;
using vedit::workspace::vcs::VcsHistoryPanel;
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
    // Batch 4a: the info read carries the ref listing, the remotes and the
    // upstream, so the stub reports a realistic set (one current branch, one
    // other branch, one tag, one remote-tracking branch, one remote).
    Info info;
    info.backend = Backend::Git;
    info.branch = "feature/vcs-panel";
    info.revision = "abc1234";
    info.upstream = "origin/feature/vcs-panel";
    info.refs = {
        RefEntry{"main", RefKind::Branch, "1234abc", false},
        RefEntry{"feature/vcs-panel", RefKind::Branch, "abc1234", true},
        RefEntry{"v1.0", RefKind::Tag, "abc1234", false},
        RefEntry{"origin/main", RefKind::RemoteBranch, "1234abc", false},
    };
    info.remotes = {RemoteEntry{"origin", "https://example.com/vedit.git"}};
    return info;
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
    empty.note = QCoreApplication::translate("VEdit", "not a Git or Subversion working tree")
                    .toStdString();
    auto stub = std::make_unique<StubVcsSource>(Info{}, empty, std::vector<Diff>{});
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/not-a-repo");
    panel.setSource(std::move(stub));

    check("vcs.panel.empty-state.backend-none", panel.backend() == Backend::None,
          vedit::workspace::vcs::backendName(panel.backend()));
    check("vcs.panel.empty-state.header", panel.headerText().contains(QCoreApplication::translate("VEdit", "No repository")),
          panel.headerText());
    check("vcs.panel.empty-state.note-visible",
          panel.emptyText().contains(QCoreApplication::translate("VEdit", "not a Git or Subversion working tree")),
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
    Info info;
    info.backend = Backend::Subversion;
    info.branch = "trunk";
    info.revision = "42";
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

#if defined(VEDIT_VCS_HAVE_FFI)
// Runs one command in `workingDirectory`; false on a non-zero exit or timeout.
bool runProcess(const QString &program, const QStringList &arguments,
                const QString &workingDirectory, QString *output = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(program, arguments);
    if (!process.waitForFinished(15000)) {
        return false;
    }
    if (output != nullptr) {
        *output = QString::fromLocal8Bit(process.readAllStandardOutput());
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool errorCode(VdService *service, int code)
{
    VdError error{};
    return vd_service_error(service, &error) == 1 && error.code == code;
}
#endif

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

    // Batch 3a replaces the old "no write entry point at all" claim with the
    // stronger contract: reads are unchanged, and writes are a SEPARATE
    // job/poll/free trio driven by a typed, default-deny operation list.
    //   1. an operation code that is not listed is refused before a job exists;
    //   2. a LISTED operation outside a working tree starts a job that fails
    //      with a typed error — the request is refused, nothing is spawned;
    //   3. poll misuse (null out) and free are null-safe.
    VdVcsWriteRequest unknown{};
    unknown.op = 999;
    check("vcs.ffi.write.unknown-op-refused", vd_vcs_write_job(service, &unknown) == 0);
    check("vcs.ffi.write.unknown-op-typed-error", errorCode(service, VD_ERR_INVALID_ARGUMENT));
    check("vcs.ffi.write.null-request-refused", vd_vcs_write_job(service, nullptr) == 0);

    QTemporaryDir writeTemp;
    check("vcs.ffi.write.temp-dir", writeTemp.isValid());
    const QByteArray writeRoot = writeTemp.path().toUtf8();
    const char *writePaths[] = {"rtl/a.sv"};
    VdVcsWriteRequest noRepo{};
    noRepo.op = VD_VCS_WRITE_GIT_STAGE; // a LISTED operation
    noRepo.root = writeRoot.constData();
    noRepo.count = 1;
    noRepo.paths = writePaths;
    const unsigned long long noRepoJob = vd_vcs_write_job(service, &noRepo);
    check("vcs.ffi.write.listed-op-starts-a-job", noRepoJob != 0);
    VdVcsWriteResult noRepoResult{};
    int noRepoState = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 2000 && noRepoState == VD_STATUS_PENDING; ++attempt) {
        noRepoState = vd_vcs_write_poll(service, noRepoJob, &noRepoResult);
        if (noRepoState == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    check("vcs.ffi.write.norepo-default-deny", noRepoState == VD_STATUS_FAILED);
    check("vcs.ffi.write.norepo-typed-error", errorCode(service, VD_ERR_INVALID_ARGUMENT));
    check("vcs.ffi.write.poll-null-out-misuse",
          vd_vcs_write_poll(service, noRepoJob, nullptr) == -1);
    vd_vcs_write_free(&noRepoResult);
    vd_vcs_write_free(nullptr);
    check("vcs.ffi.write.free-null-safe", noRepoResult.stdout_text == nullptr);
    vd_service_destroy(service);
#else
    skip("vcs.ffi.service", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.info.start", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.info-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.diff.start", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.diff-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.invalid-root-rejected", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.unknown-op-refused", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.unknown-op-typed-error", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.null-request-refused", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.temp-dir", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.listed-op-starts-a-job", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.norepo-default-deny", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.norepo-typed-error", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.poll-null-out-misuse", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.write.free-null-safe", "lane built without vd-ffi (stub-only build)");
#endif
}

// AC-3.0: the READ jobs must really return status / branch+revision / diff from
// a repository on the git actually installed here (the Qt6 image carries the
// current git; the pinned Rust image carries the delivery baseline 1.8.3.1, where
// the same Rust tests run). Two repositories prove the working directory is part
// of the seam, and a before/after HEAD+porcelain comparison proves the read path
// executed no write.
void checkReadCompat()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        skip("vcs.read.compat.git-available", "git is not on PATH");
        skip("vcs.read.compat.fixture-committed", "git is not on PATH");
        skip("vcs.read.compat.second-repo", "git is not on PATH");
        skip("vcs.read.compat.status", "git is not on PATH");
        skip("vcs.read.compat.cwd-isolation", "git is not on PATH");
        skip("vcs.read.compat.info", "git is not on PATH");
        skip("vcs.read.compat.diff", "git is not on PATH");
        skip("vcs.read.compat.no-write-executed", "git is not on PATH");
        return;
    }
    check("vcs.read.compat.git-available", true, git);

    QTemporaryDir temp;
    check("vcs.read.compat.fixture", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString repoA = temp.path() + QStringLiteral("/repo-a");
    const QString repoB = temp.path() + QStringLiteral("/repo-b");
    QDir().mkpath(repoA + QStringLiteral("/rtl"));
    QDir().mkpath(repoB + QStringLiteral("/rtl"));
    {
        QFile fileA(repoA + QStringLiteral("/rtl/a.sv"));
        if (!fileA.open(QIODevice::WriteOnly)) {
            check("vcs.read.compat.fixture-committed", false);
            return;
        }
        fileA.write("module a;\nendmodule\n");
    }
    {
        QFile fileB(repoB + QStringLiteral("/rtl/b.sv"));
        if (!fileB.open(QIODevice::WriteOnly)) {
            check("vcs.read.compat.fixture-committed", false);
            return;
        }
        fileB.write("module b;\nendmodule\n");
    }
    const bool committed = runProcess(git, {"init", "-q"}, repoA)
        && runProcess(git, {"config", "user.email", "compat@example.com"}, repoA)
        && runProcess(git, {"config", "user.name", "VEdit Compat"}, repoA)
        && runProcess(git, {"add", "rtl/a.sv"}, repoA)
        && runProcess(git, {"commit", "-m", "compat fixture"}, repoA);
    check("vcs.read.compat.fixture-committed", committed);
    if (!committed) {
        return;
    }
    // The second repository is a separate working copy with its own commit: its
    // status must report its own modified file, never repository A's one.
    const bool committedB = runProcess(git, {"init", "-q"}, repoB)
        && runProcess(git, {"config", "user.email", "compat@example.com"}, repoB)
        && runProcess(git, {"config", "user.name", "VEdit Compat"}, repoB)
        && runProcess(git, {"add", "rtl/b.sv"}, repoB)
        && runProcess(git, {"commit", "-m", "compat fixture b"}, repoB);
    check("vcs.read.compat.second-repo", committedB);
    if (!committedB) {
        return;
    }
    // Dirty both repositories.
    {
        QFile fileA(repoA + QStringLiteral("/rtl/a.sv"));
        if (!fileA.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check("vcs.read.compat.fixture-dirty", false);
            return;
        }
        fileA.write("module a;\n// changed\nendmodule\n");
    }
    {
        QFile fileB(repoB + QStringLiteral("/rtl/b.sv"));
        if (!fileB.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            check("vcs.read.compat.fixture-dirty", false);
            return;
        }
        fileB.write("module b;\n// changed-b\nendmodule\n");
    }
    QString headBefore;
    QString porcelainBefore;
    runProcess(git, {"rev-parse", "HEAD"}, repoA, &headBefore);
    runProcess(git, {"status", "--porcelain"}, repoA, &porcelainBefore);

    VdService *service = vd_service_create();
    check("vcs.read.compat.service", service != nullptr);
    if (service == nullptr) {
        return;
    }
    const QByteArray rootA = repoA.toUtf8();
    const QByteArray rootB = repoB.toUtf8();

    VdVcsStatus statusA{};
    int stateA = VD_STATUS_PENDING;
    const unsigned long long statusJobA = vd_vcs_status(service, rootA.constData());
    for (int attempt = 0; attempt < 4000 && stateA == VD_STATUS_PENDING; ++attempt) {
        stateA = vd_vcs_poll(service, statusJobA, &statusA);
        if (stateA == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const QString branchA = statusA.branch != nullptr ? QString::fromUtf8(statusA.branch) : QString();
    const QString pathA = statusA.count > 0 && statusA.changes != nullptr
            && statusA.changes[0].path != nullptr
        ? QString::fromUtf8(statusA.changes[0].path)
        : QString();
    check("vcs.read.compat.status",
          stateA == VD_STATUS_READY && statusA.backend == 1 && statusA.count == 1
              && pathA == QStringLiteral("rtl/a.sv") && !branchA.isEmpty(),
          QStringLiteral("state=%1 branch=%2 count=%3 path=%4")
              .arg(stateA).arg(branchA).arg(statusA.count).arg(pathA));
    vd_vcs_status_free(&statusA);

    VdVcsStatus statusB{};
    int stateB = VD_STATUS_PENDING;
    const unsigned long long statusJobB = vd_vcs_status(service, rootB.constData());
    for (int attempt = 0; attempt < 4000 && stateB == VD_STATUS_PENDING; ++attempt) {
        stateB = vd_vcs_poll(service, statusJobB, &statusB);
        if (stateB == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const QString pathB = statusB.count > 0 && statusB.changes != nullptr
            && statusB.changes[0].path != nullptr
        ? QString::fromUtf8(statusB.changes[0].path)
        : QString();
    check("vcs.read.compat.cwd-isolation",
          stateB == VD_STATUS_READY && statusB.backend == 1 && statusB.count == 1
              && pathB == QStringLiteral("rtl/b.sv"),
          QStringLiteral("state=%1 count=%2 path=%3").arg(stateB).arg(statusB.count).arg(pathB));
    vd_vcs_status_free(&statusB);

    VdVcsInfo infoA{};
    int infoState = VD_STATUS_PENDING;
    const unsigned long long infoJobA = vd_vcs_info_job(service, rootA.constData());
    for (int attempt = 0; attempt < 4000 && infoState == VD_STATUS_PENDING; ++attempt) {
        infoState = vd_vcs_info_poll(service, infoJobA, &infoA);
        if (infoState == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const QString revisionA = infoA.revision != nullptr ? QString::fromUtf8(infoA.revision) : QString();
    check("vcs.read.compat.info",
          infoState == VD_STATUS_READY && infoA.backend == 1 && !revisionA.isEmpty(), revisionA);
    vd_vcs_info_free(&infoA);

    VdVcsDiff diffA{};
    int diffState = VD_STATUS_PENDING;
    const unsigned long long diffJobA = vd_vcs_diff_job(service, rootA.constData(), "rtl/a.sv");
    for (int attempt = 0; attempt < 4000 && diffState == VD_STATUS_PENDING; ++attempt) {
        diffState = vd_vcs_diff_poll(service, diffJobA, &diffA);
        if (diffState == VD_STATUS_PENDING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const QString diffText = diffA.text != nullptr ? QString::fromUtf8(diffA.text) : QString();
    check("vcs.read.compat.diff",
          diffState == VD_STATUS_READY && diffText.contains(QStringLiteral("+// changed")),
          diffText.left(60));
    vd_vcs_diff_free(&diffA);
    vd_service_destroy(service);

    QString headAfter;
    QString porcelainAfter;
    runProcess(git, {"rev-parse", "HEAD"}, repoA, &headAfter);
    runProcess(git, {"status", "--porcelain"}, repoA, &porcelainAfter);
    check("vcs.read.compat.no-write-executed",
          !headBefore.isEmpty() && headBefore == headAfter && porcelainBefore == porcelainAfter,
          QStringLiteral("head_same=%1 porcelain_same=%2")
              .arg(headBefore == headAfter ? 1 : 0)
              .arg(porcelainBefore == porcelainAfter ? 1 : 0));
#else
    skip("vcs.read.compat.git-available", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.fixture", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.fixture-committed", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.second-repo", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.service", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.status", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.cwd-isolation", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.info", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.diff", "lane built without vd-ffi (stub-only build)");
    skip("vcs.read.compat.no-write-executed", "lane built without vd-ffi (stub-only build)");
#endif
}

// Batch 6a (rev3): the product bundles git/svn, and the lane resolves them as
// bundled > PATH > env (the same order as crates/vd-vcs/src/toolchain.rs). The
// line is printed once at init; the production panel factory copies the values
// into properties for the acceptance probes.
void checkToolchain()
{
    const vedit::workspace::vcs::ToolchainStatus &status =
        vedit::workspace::vcs::reportToolchainOnce();
    const QString line = vedit::workspace::vcs::toolchainStatusLine(status);
    check("vcs.toolchain.line-shape",
          line.startsWith(QStringLiteral("VCS-TOOLCHAIN ")) && line.contains(QStringLiteral("git="))
              && line.contains(QStringLiteral("svn=")) && line.contains(QStringLiteral("source=")),
          line.left(150));
    const QStringList known{QStringLiteral("bundled"), QStringLiteral("path"),
                            QStringLiteral("env"), QStringLiteral("missing")};
    check("vcs.toolchain.sources-known",
          known.contains(status.gitSource) && known.contains(status.svnSource),
          QStringLiteral("git_source=%1 svn_source=%2").arg(status.gitSource, status.svnSource));
    check("vcs.toolchain.appdir", !status.appDir.isEmpty(), status.appDir);
    if (status.gitPath.isEmpty()) {
        skip("vcs.toolchain.git-version", "git is not available in this environment");
    } else {
        check("vcs.toolchain.git-version", !status.gitVersion.isEmpty(),
              QStringLiteral("%1 -> %2").arg(status.gitPath, status.gitVersion));
    }
    if (status.svnPath.isEmpty()) {
        skip("vcs.toolchain.svn-version", "svn is not available in this environment");
    } else {
        check("vcs.toolchain.svn-version", !status.svnVersion.isEmpty(),
              QStringLiteral("%1 -> %2").arg(status.svnPath, status.svnVersion));
    }
    // Batch 7 (NIT from the 6a review): the production factory exposes the chosen
    // toolchain as widget properties; the SAME helper must do it on a plain panel
    // (asserted here without an ApplicationShell).
    {
        VcsPanel propertyPanel;
        vedit::workspace::vcs::applyToolchainProperties(&propertyPanel, status);
        const QString toolchainProperty =
            propertyPanel.property("veditVcsToolchain").toString();
        check("vcs.toolchain.panel-properties",
              toolchainProperty.startsWith(QStringLiteral("VCS-TOOLCHAIN "))
                  && propertyPanel.property("veditVcsGitPath").toString() == status.gitPath
                  && propertyPanel.property("veditVcsGitVersion").toString() == status.gitVersion
                  && propertyPanel.property("veditVcsSvnPath").toString() == status.svnPath
                  && propertyPanel.property("veditVcsSvnVersion").toString() == status.svnVersion,
              toolchainProperty.left(140));
    }
    // The bundled layout must be preferred when it exists. A fake AppDir proves
    // the rule without depending on the real package being installed here.
    QTemporaryDir fake;
    check("vcs.toolchain.fake-appdir", fake.isValid());
    if (!fake.isValid()) {
        return;
    }
    const QString fakeAppDir = fake.path() + QStringLiteral("/bin");
    QDir().mkpath(fakeAppDir);
    QDir().mkpath(fake.path() + QStringLiteral("/usr/lib/vcs/bin"));
    const QString fakeTool = fake.path() + QStringLiteral("/usr/lib/vcs/bin/git");
    QFile tool(fakeTool);
    bool written = tool.open(QIODevice::WriteOnly);
    if (written) {
        tool.write("#!/bin/sh\nexit 0\n");
        tool.close();
        written = QFile::setPermissions(fakeTool, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner | QFileDevice::ReadUser
                                                     | QFileDevice::ExeUser | QFileDevice::ReadGroup
                                                     | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                                     | QFileDevice::ExeOther);
    }
    const QString found =
        vedit::workspace::vcs::bundledTool(fakeAppDir, QStringLiteral("git"));
    check("vcs.toolchain.bundled-layout",
          written && found == QFileInfo(fakeTool).absoluteFilePath(), found);
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

    // Batch 3a: a WRITE job goes through the same registry and the same event
    // loop. The listed operation really reaches the tool — the job stays
    // PENDING while the fake git sleeps 400 ms — so this is the "write path is
    // asynchronous, the GUI only polls" check (AC-3.2).
    {
        VdService *writeService = vd_service_create();
        check("vcs.write.service", writeService != nullptr);
        if (writeService != nullptr) {
            const QByteArray writeRoot = temp.path().toUtf8();
            const char *writePaths[] = {"rtl/top.sv"};
            VdVcsWriteRequest writeRequest{};
            writeRequest.op = VD_VCS_WRITE_GIT_STAGE; // a listed write operation
            writeRequest.root = writeRoot.constData();
            writeRequest.count = 1;
            writeRequest.paths = writePaths;

            QElapsedTimer writeTimer;
            writeTimer.start();
            const unsigned long long writeJob = vd_vcs_write_job(writeService, &writeRequest);
            const qint64 writeStartMs = writeTimer.elapsed();
            check("vcs.write.start-returns-fast", writeJob != 0 && writeStartMs < 150,
                  QStringLiteral("start took %1 ms while the fake git sleeps 400 ms")
                      .arg(writeStartMs));

            int writeTicks = 0;
            QTimer writeTicker;
            writeTicker.setInterval(20);
            QObject::connect(&writeTicker, &QTimer::timeout, [&writeTicks]() { ++writeTicks; });
            writeTicker.start();
            VdVcsWriteResult writeResult{};
            int writeState = VD_STATUS_PENDING;
            int polls = 0;
            bool pendingWhileTicking = false;
            QElapsedTimer writeWindow;
            writeWindow.start();
            while (writeWindow.elapsed() < 250) {
                writeState = vd_vcs_write_poll(writeService, writeJob, &writeResult);
                ++polls;
                if (writeState != VD_STATUS_PENDING) {
                    break;
                }
                pendingWhileTicking = true;
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            writeTicker.stop();
            check("vcs.write.whitelisted-op-runs", polls > 0 && pendingWhileTicking,
                  QStringLiteral("polls=%1 state=%2").arg(polls).arg(writeState));
            check("vcs.write.event-loop-ticks", writeTicks >= 3,
                  QStringLiteral("ticks=%1").arg(writeTicks));

            QElapsedTimer writeWait;
            writeWait.start();
            while (writeState == VD_STATUS_PENDING && writeWait.elapsed() < 8000) {
                writeState = vd_vcs_write_poll(writeService, writeJob, &writeResult);
                if (writeState == VD_STATUS_PENDING) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            const QString writeReason = writeResult.stderr_text != nullptr
                ? QString::fromUtf8(writeResult.stderr_text)
                : (writeResult.note != nullptr ? QString::fromUtf8(writeResult.note) : QString());
            const int writeOp = writeResult.op;
            vd_vcs_write_free(&writeResult);
            check("vcs.write.result-delivered",
                  writeState == VD_STATUS_READY && writeOp == VD_VCS_WRITE_GIT_STAGE,
                  writeReason.left(60));
            vd_service_destroy(writeService);
        } else {
            skip("vcs.write.start-returns-fast", "vd_service_create returned null");
            skip("vcs.write.whitelisted-op-runs", "vd_service_create returned null");
            skip("vcs.write.event-loop-ticks", "vd_service_create returned null");
            skip("vcs.write.result-delivered", "vd_service_create returned null");
        }
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

// ==========================================================================
// Batch 3b (AC-3.3 / AC-3.4 / AC-3.5 / AC-3.7)
// ==========================================================================

using vedit::workspace::vcs::CommitDialog;

[[maybe_unused]] bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool ok = file.write(text.toUtf8()) >= 0;
    file.flush();
    return ok;
}

// Pumps the event loop for `milliseconds`, so QTimer-driven jobs (the FFI poll,
// the coalesced refresh) really advance.
[[maybe_unused]] void pump(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

[[maybe_unused]] bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// Drives ONE real modal dialog from a timer: the handler runs inside `exec()` (a
// nested event loop) as soon as the dialog with `objectName` is visible, which is
// exactly how a user answers it. A driver that never sees its dialog leaves the
// probe blocked in `exec()` — the ctest TIMEOUT turns that into a visible failure
// instead of a silently passing check, which is why no assertion here is skipped.
class ModalDriver {
public:
    ModalDriver(QString objectName, std::function<void(QDialog *)> handler)
        : object_name_(std::move(objectName))
        , handler_(std::move(handler))
    {
        timer_ = new QTimer();
        timer_->setInterval(20);
        QObject::connect(timer_, &QTimer::timeout, timer_, [this]() { tick(); });
        timer_->start();
    }

    ~ModalDriver()
    {
        if (timer_ != nullptr) {
            timer_->stop();
            timer_->deleteLater();
        }
    }

    ModalDriver(const ModalDriver &) = delete;
    ModalDriver &operator=(const ModalDriver &) = delete;

    int driven() const { return driven_; }
    int ticks() const { return ticks_; }
    QPointer<QDialog> dialog() const { return dialog_; }
    bool timedOut() const { return ticks_ > 400 && driven_ == 0; }

private:
    void tick()
    {
        ++ticks_;
        if (driven_ > 0) {
            return;
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<QDialog *>(widget);
            if (candidate == nullptr || candidate->objectName() != object_name_
                || !candidate->isVisible()) {
                continue;
            }
            ++driven_;
            dialog_ = candidate;
            handler_(candidate);
            return;
        }
    }

    QString object_name_;
    std::function<void(QDialog *)> handler_;
    QTimer *timer_ = nullptr;
    int ticks_ = 0;
    int driven_ = 0;
    QPointer<QDialog> dialog_;
};

// AC-3.4: the SourceGit-style three-column baseline and the operation bar exist,
// and the seam's write codes match the C ABI one for one.
void checkThreeColumnLayout()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));

    check("vcs.layout.splitter",
          panel.findChild<QWidget *>(QStringLiteral("vcs-columns")) != nullptr);
    for (const char *name : {"vcs-column-refs", "vcs-column-changes", "vcs-column-diff"}) {
        check(name, panel.findChild<QWidget *>(QString::fromLatin1(name)) != nullptr);
    }
    check("vcs.layout.refs-column",
          panel.refTexts().contains(QStringLiteral("Branches"))
              && panel.refTexts().contains(QStringLiteral("Tags"))
              && panel.refTexts().contains(QStringLiteral("* feature/vcs-panel")),
          panel.refTexts().join(QStringLiteral(" | ")));
    check("vcs.layout.changes-and-staged-columns",
          panel.findChild<QWidget *>(QStringLiteral("vcs-changes")) != nullptr
              && panel.findChild<QWidget *>(QStringLiteral("vcs-staged")) != nullptr,
          QStringLiteral("changes=%1 staged=%2").arg(panel.changeCount()).arg(panel.stagedCount()));
    check("vcs.layout.changes-list", panel.changeCount() == 2 && panel.stagedCount() == 0,
          QStringLiteral("changes=%1 staged=%2").arg(panel.changeCount()).arg(panel.stagedCount()));

    const QStringList actions = panel.actionNames();
    check("vcs.layout.action-bar",
          actions
              == QStringList{QStringLiteral("vcs-action-refresh"),
                             QStringLiteral("vcs-action-stage"),
                             QStringLiteral("vcs-action-unstage"),
                             QStringLiteral("vcs-action-discard"),
                             QStringLiteral("vcs-action-commit"),
                             QStringLiteral("vcs-action-fetch"),
                             QStringLiteral("vcs-action-pull"),
                             QStringLiteral("vcs-action-push"),
                             QStringLiteral("vcs-action-branch"),
                             QStringLiteral("vcs-action-tag"),
                             QStringLiteral("vcs-action-remote"),
                             QStringLiteral("vcs-action-workspace"),
                             QStringLiteral("vcs-action-remove")},
          actions.join(QStringLiteral(",")));
    check("vcs.layout.action-bar-gated",
          !panel.stageSelected() || panel.lastWriteOk(),
          QStringLiteral("actions are gated on a selection"));
#if defined(VEDIT_VCS_HAVE_FFI)
    check("vcs.seam.write-op-codes",
          static_cast<int>(WriteOp::GitStage) == VD_VCS_WRITE_GIT_STAGE
              && static_cast<int>(WriteOp::GitUnstage) == VD_VCS_WRITE_GIT_UNSTAGE
              && static_cast<int>(WriteOp::GitDiscard) == VD_VCS_WRITE_GIT_DISCARD
              && static_cast<int>(WriteOp::GitCommit) == VD_VCS_WRITE_GIT_COMMIT
              && static_cast<int>(WriteOp::GitFetch) == VD_VCS_WRITE_GIT_FETCH
              && static_cast<int>(WriteOp::GitPull) == VD_VCS_WRITE_GIT_PULL
              && static_cast<int>(WriteOp::GitPush) == VD_VCS_WRITE_GIT_PUSH
              && static_cast<int>(WriteOp::GitBranchCreate) == VD_VCS_WRITE_GIT_BRANCH_CREATE
              && static_cast<int>(WriteOp::GitBranchSwitch) == VD_VCS_WRITE_GIT_BRANCH_SWITCH
              && static_cast<int>(WriteOp::GitBranchDelete) == VD_VCS_WRITE_GIT_BRANCH_DELETE
              && static_cast<int>(WriteOp::GitBranchRename) == VD_VCS_WRITE_GIT_BRANCH_RENAME
              && static_cast<int>(WriteOp::GitTagCreate) == VD_VCS_WRITE_GIT_TAG_CREATE
              && static_cast<int>(WriteOp::GitTagDelete) == VD_VCS_WRITE_GIT_TAG_DELETE
              && static_cast<int>(WriteOp::GitCheckout) == VD_VCS_WRITE_GIT_CHECKOUT
              && static_cast<int>(WriteOp::GitRemoteAdd) == VD_VCS_WRITE_GIT_REMOTE_ADD
              && static_cast<int>(WriteOp::GitRemoteRemove) == VD_VCS_WRITE_GIT_REMOTE_REMOVE
              && static_cast<int>(WriteOp::GitRemoteSetUrl) == VD_VCS_WRITE_GIT_REMOTE_SET_URL
              && static_cast<int>(WriteOp::GitSetUpstream) == VD_VCS_WRITE_GIT_SET_UPSTREAM
              && static_cast<int>(WriteOp::SvnRevert) == VD_VCS_WRITE_SVN_REVERT,
          QStringLiteral("stage=%1 fetch=%2 push=%3 remote-add=%4 set-upstream=%5")
              .arg(static_cast<int>(WriteOp::GitStage))
              .arg(static_cast<int>(WriteOp::GitFetch))
              .arg(static_cast<int>(WriteOp::GitPush))
              .arg(static_cast<int>(WriteOp::GitRemoteAdd))
              .arg(static_cast<int>(WriteOp::GitSetUpstream)));
#else
    skip("vcs.seam.write-op-codes", "lane built without vd-ffi (no C ABI op codes to compare)");
#endif
}

// AC-3.4: empty / loading / error / ready are four DISTINCT visible states.
void checkFourStates()
{
    // ready
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        VcsPanel panel;
        panel.setRoot("/tmp/repo-git");
        panel.setSource(std::move(stub));
        check("vcs.state.ready",
              !panel.emptyStateVisible() && !panel.errorStateVisible()
                  && !panel.loadingStateVisible() && panel.changeCount() == 2,
              panel.headerText());
    }
    // empty
    {
        Status empty;
        empty.note = "not a repository";
        auto stub = std::make_unique<StubVcsSource>(Info{}, empty, std::vector<Diff>{});
        VcsPanel panel;
        panel.setRoot("/tmp/not-a-repo");
        panel.setSource(std::move(stub));
        check("vcs.state.empty",
              panel.emptyStateVisible() && !panel.errorStateVisible()
                  && !panel.loadingStateVisible() && panel.changeCount() == 0,
              panel.emptyText().left(60));
    }
    // error
    {
        Status broken;
        broken.backend = Backend::Git;
        broken.error = true;
        broken.note = "git: fatal: not a repository";
        auto stub = std::make_unique<StubVcsSource>(Info{}, broken, std::vector<Diff>{});
        VcsPanel panel;
        panel.setRoot("/tmp/broken-repo");
        panel.setSource(std::move(stub));
        check("vcs.state.error",
              panel.errorStateVisible() && !panel.emptyStateVisible()
                  && panel.errorText().contains(QStringLiteral("not a repository")),
              panel.errorText().left(80));
    }
    // loading: the stub defers its answer, so the state is observable.
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        auto *raw = stub.get();
        raw->setDeferStatus(true);
        VcsPanel panel;
        panel.setRoot("/tmp/repo-git");
        panel.setSource(std::move(stub));
        check("vcs.state.loading",
              panel.loadingStateVisible() && !panel.emptyStateVisible() && raw->statusPending(),
              panel.loadingText());
        raw->flushStatus();
        check("vcs.state.loading-clears",
              !panel.loadingStateVisible() && panel.changeCount() == 2,
              QStringLiteral("changes=%1").arg(panel.changeCount()));
    }
}

// AC-3.3 (UI half): every write is confirmed where the contract says so, and a
// declined confirmation writes NOTHING. The stub records the requests, so the
// assertion is on what reached the seam, not on a rendered message.
void checkWriteConfirmations()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));
    panel.selectFile("rtl/top.sv");

    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.write.discard-confirm-declines", !panel.discardSelected());
    check("vcs.write.discard-confirm-zero-write", raw->writeAttempts() == 0,
          QStringLiteral("writes=%1").arg(raw->writeAttempts()));

    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.write.discard-requested", panel.discardSelected());
    check("vcs.write.discard-op-and-path",
          raw->writes().size() == 1 && raw->writes().back().op == WriteOp::GitDiscard
              && raw->writes().back().paths.size() == 1
              && raw->writes().back().paths.front() == "rtl/top.sv",
          QStringLiteral("writes=%1 op=%2")
              .arg(raw->writes().size())
              .arg(raw->writes().empty() ? -1 : static_cast<int>(raw->writes().back().op)));
    check("vcs.write.discard-reported",
          panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::GitDiscard,
          panel.messageText());

    check("vcs.write.stage-requested", panel.stageSelected());
    check("vcs.write.stage-op",
          raw->writes().size() == 2 && raw->writes().back().op == WriteOp::GitStage);
    check("vcs.write.stage-moves-the-row",
          panel.stagedCount() == 1 && panel.changeCount() == 1
              && panel.stagedText(0).contains(QStringLiteral("rtl/top.sv")),
          QStringLiteral("changes=%1 staged=%2").arg(panel.changeCount()).arg(panel.stagedCount()));

    check("vcs.write.unstage-requested", panel.unstageSelected());
    check("vcs.write.unstage-op",
          raw->writes().size() == 3 && raw->writes().back().op == WriteOp::GitUnstage);
    check("vcs.write.unstage-restores-the-row",
          panel.stagedCount() == 0 && panel.changeCount() == 2,
          QStringLiteral("changes=%1 staged=%2").arg(panel.changeCount()).arg(panel.stagedCount()));

    const std::size_t beforeEmpty = raw->writes().size();
    check("vcs.write.commit-requires-a-message", !panel.commit(QString(), QString(), false));
    check("vcs.write.commit-empty-is-zero-write", raw->writes().size() == beforeEmpty,
          QStringLiteral("writes=%1").arg(raw->writes().size()));

    check("vcs.write.commit-requested",
          panel.commit(QStringLiteral("fixture: add rtl"), QStringLiteral("VEdit Test"), false));
    check("vcs.write.commit-carries-message-author",
          raw->writes().size() == beforeEmpty + 1
              && raw->writes().back().op == WriteOp::GitCommit
              && raw->writes().back().message == "fixture: add rtl"
              && raw->writes().back().author == "VEdit Test"
              && !raw->writes().back().amend);

    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const std::size_t beforeAmend = raw->writes().size();
    check("vcs.write.amend-confirm-declines", !panel.commit(QStringLiteral("rewrite"), QString(), true));
    check("vcs.write.amend-confirm-zero-write", raw->writes().size() == beforeAmend,
          QStringLiteral("writes=%1").arg(raw->writes().size()));

    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.write.amend-requested", panel.commit(QStringLiteral("rewrite"), QString(), true));
    check("vcs.write.amend-op-and-flag",
          raw->writes().size() == beforeAmend + 1 && raw->writes().back().op == WriteOp::GitCommit
              && raw->writes().back().amend
              && raw->writes().back().message == "rewrite",
          QStringLiteral("writes=%1").arg(raw->writes().size()));

    // A failed write surfaces the real reason instead of a success line.
    raw->setWriteOutcome(false, "fatal: pathspec 'rtl/top.sv' did not match any files");
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.write.failure-is-reported", panel.discardSelected());
    check("vcs.write.failure-reason-visible",
          !panel.lastWriteOk() && panel.errorStateVisible()
              && panel.errorText().contains(QStringLiteral("did not match any files")),
          panel.errorText().left(90));
}

// AC-3.3 (real repository): stage / unstage / discard / commit / amend really run
// `git`, asserted with `git status --porcelain` and `git log`. The fixture is a
// fresh QTemporaryDir; the check asserts it is NOT the VEdit working tree.
void checkGitWriteE2E()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        for (const char *name : {"vcs.write.e2e.git", "vcs.write.e2e.fixture",
                                 "vcs.write.e2e.not-worktree", "vcs.write.e2e.status-first",
                                 "vcs.write.e2e.stage-ok", "vcs.write.e2e.staged-porcelain",
                                 "vcs.write.e2e.staged-column", "vcs.write.e2e.unstage-ok",
                                 "vcs.write.e2e.unstaged-porcelain", "vcs.write.e2e.discard-cancel",
                                 "vcs.write.e2e.discard-cancel-zero-write", "vcs.write.e2e.discard-ok",
                                 "vcs.write.e2e.discarded-porcelain", "vcs.write.e2e.commit-ok",
                                 "vcs.write.e2e.commit-subject", "vcs.write.e2e.commit-author",
                                 "vcs.write.e2e.amend-ok", "vcs.write.e2e.amend-subject",
                                 "vcs.write.e2e.amend-rewrote-one-commit"}) {
            skip(name, "git is not on PATH");
        }
        return;
    }
    check("vcs.write.e2e.git", true, git);

    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/write-repo");
    QDir().mkpath(repo + QStringLiteral("/rtl"));
    const bool fixture =
        writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, repo)
        && runProcess(git, {"config", "user.email", "e2e@example.com"}, repo)
        && runProcess(git, {"config", "user.name", "VEdit E2E"}, repo)
        && runProcess(git, {"add", "rtl/a.sv"}, repo)
        && runProcess(git, {"commit", "-m", "fixture base"}, repo);
    check("vcs.write.e2e.fixture", temp.isValid() && fixture, repo);
    if (!fixture) {
        return;
    }
    check("vcs.write.e2e.not-worktree",
          QFileInfo(repo).canonicalFilePath() != QFileInfo(QDir::currentPath()).canonicalFilePath()
              && repo.startsWith(QDir::tempPath()),
          QStringLiteral("fixture=%1 cwd=%2").arg(repo, QDir::currentPath()));

    writeText(repo + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\n// e2e change\nendmodule\n"));

    VcsPanel panel;
    panel.setRoot(repo.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    const bool firstStatus = waitUntil([&]() { return panel.fileCount() == 1; }, 20000);
    check("vcs.write.e2e.status-first", firstStatus,
          QStringLiteral("files=%1").arg(panel.fileCount()));
    panel.selectFile("rtl/a.sv");
    waitUntil([&]() { return panel.diffText().contains(QStringLiteral("e2e change")); }, 20000);

    const auto writeDone = [&panel]() {
        return waitUntil([&panel]() { return !panel.writeInFlight(); }, 20000);
    };

    // --- stage ------------------------------------------------------------
    check("vcs.write.e2e.stage-requested", panel.stageSelected());
    writeDone();
    check("vcs.write.e2e.stage-ok", panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::GitStage,
          panel.messageText());
    QString porcelain;
    runProcess(git, {"status", "--porcelain"}, repo, &porcelain);
    check("vcs.write.e2e.staged-porcelain",
          porcelain.contains(QStringLiteral("M  rtl/a.sv")), porcelain.trimmed());
    const bool stagedColumn = waitUntil([&]() { return panel.stagedCount() == 1; }, 6000);
    check("vcs.write.e2e.staged-column", stagedColumn,
          QStringLiteral("staged=%1").arg(panel.stagedCount()));

    // --- unstage ----------------------------------------------------------
    check("vcs.write.e2e.unstage-requested", panel.unstageSelected());
    writeDone();
    runProcess(git, {"status", "--porcelain"}, repo, &porcelain);
    check("vcs.write.e2e.unstage-ok", panel.lastWriteOk());
    check("vcs.write.e2e.unstaged-porcelain",
          porcelain.contains(QStringLiteral(" M rtl/a.sv")), porcelain.trimmed());

    // --- discard: a declined confirmation must write nothing ---------------
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.write.e2e.discard-cancel", !panel.discardSelected());
    runProcess(git, {"status", "--porcelain"}, repo, &porcelain);
    check("vcs.write.e2e.discard-cancel-zero-write",
          porcelain.contains(QStringLiteral(" M rtl/a.sv")), porcelain.trimmed());

    // --- discard: approved really reverts the working tree -----------------
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.write.e2e.discard-requested", panel.discardSelected());
    writeDone();
    runProcess(git, {"status", "--porcelain"}, repo, &porcelain);
    check("vcs.write.e2e.discard-ok", panel.lastWriteOk(), panel.messageText());
    check("vcs.write.e2e.discarded-porcelain", porcelain.trimmed().isEmpty(), porcelain.trimmed());

    // --- commit with message + author -------------------------------------
    writeText(repo + QStringLiteral("/rtl/new.sv"), QStringLiteral("module new;\nendmodule\n"));
    panel.refresh();
    waitUntil([&]() { return panel.fileCount() == 1; }, 20000);
    panel.selectFile("rtl/new.sv");
    check("vcs.write.e2e.stage-new", panel.stageSelected());
    writeDone();
    check("vcs.write.e2e.stage-new-ok", panel.lastWriteOk(),
          panel.errorStateVisible() ? panel.errorText() : panel.messageText());
    check("vcs.write.e2e.commit-requested",
          panel.commit(QStringLiteral("fixture: add new.sv"),
                       QStringLiteral("VEdit Test <test@vedit.local>"), false));
    writeDone();
    check("vcs.write.e2e.commit-ok", panel.lastWriteOk(),
          panel.errorStateVisible() ? panel.errorText() : panel.messageText());
    QString subject;
    runProcess(git, {"log", "-1", "--pretty=%s"}, repo, &subject);
    check("vcs.write.e2e.commit-subject",
          subject.trimmed() == QStringLiteral("fixture: add new.sv"), subject.trimmed());
    QString author;
    runProcess(git, {"log", "-1", "--pretty=%an <%ae>"}, repo, &author);
    check("vcs.write.e2e.commit-author",
          author.trimmed() == QStringLiteral("VEdit Test <test@vedit.local>"), author.trimmed());

    // --- amend rewrites the message, not the commit count ------------------
    QString countBefore;
    runProcess(git, {"rev-list", "--count", "HEAD"}, repo, &countBefore);
    check("vcs.write.e2e.amend-requested",
          panel.commit(QStringLiteral("fixture: add new.sv (amended)"),
                       QStringLiteral("VEdit Test <test@vedit.local>"), true));
    writeDone();
    check("vcs.write.e2e.amend-ok", panel.lastWriteOk(), panel.messageText());
    runProcess(git, {"log", "-1", "--pretty=%s"}, repo, &subject);
    check("vcs.write.e2e.amend-subject",
          subject.trimmed() == QStringLiteral("fixture: add new.sv (amended)"), subject.trimmed());
    QString countAfter;
    runProcess(git, {"rev-list", "--count", "HEAD"}, repo, &countAfter);
    check("vcs.write.e2e.amend-rewrote-one-commit",
          countBefore.trimmed() == countAfter.trimmed(), countBefore.trimmed() + " -> " + countAfter.trimmed());
#else
    for (const char *name : {"vcs.write.e2e.git", "vcs.write.e2e.fixture",
                             "vcs.write.e2e.not-worktree", "vcs.write.e2e.status-first",
                             "vcs.write.e2e.stage-ok", "vcs.write.e2e.staged-porcelain",
                             "vcs.write.e2e.staged-column", "vcs.write.e2e.unstage-ok",
                             "vcs.write.e2e.unstaged-porcelain", "vcs.write.e2e.discard-cancel",
                             "vcs.write.e2e.discard-cancel-zero-write", "vcs.write.e2e.discard-ok",
                             "vcs.write.e2e.discarded-porcelain", "vcs.write.e2e.commit-ok",
                             "vcs.write.e2e.commit-subject", "vcs.write.e2e.commit-author",
                             "vcs.write.e2e.amend-ok", "vcs.write.e2e.amend-subject",
                             "vcs.write.e2e.amend-rewrote-one-commit"}) {
        skip(name, "lane built without vd-ffi (stub-only build)");
    }
#endif
}

// AC-3.5: a save, a finished write and a window activation all refresh the
// panel, and a burst inside one window collapses to ONE refresh.
// ==========================================================================
// Batch 5b (AC-5.2 / AC-5.4): stash, worktrees and submodules
// ==========================================================================

WorkspaceEntry stashEntry(const QString &ref, const QString &subject)
{
    WorkspaceEntry entry;
    entry.refName = ref.toStdString();
    entry.subject = subject.toStdString();
    entry.hash = "1111111111111111111111111111111111111111";
    return entry;
}

WorkspaceEntry worktreeEntry(const QString &path, const QString &branch, bool main,
                             bool detached = false)
{
    WorkspaceEntry entry;
    entry.path = path.toStdString();
    entry.branch = branch.toStdString();
    entry.hash = "1111111";
    entry.mainWorktree = main;
    entry.detached = detached;
    return entry;
}

void checkWorkspaceStub()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    WorkspaceData stashes;
    stashes.kind = WorkspaceKind::Stashes;
    stashes.backend = Backend::Git;
    stashes.entries = {stashEntry(QStringLiteral("stash@{0}"), QStringLiteral("On main: first"))};
    stub->setWorkspaceData(WorkspaceKind::Stashes, stashes);
    WorkspaceData worktrees;
    worktrees.kind = WorkspaceKind::Worktrees;
    worktrees.backend = Backend::Git;
    worktrees.entries = {worktreeEntry(QStringLiteral("/repo"), QStringLiteral("main"), true),
                         worktreeEntry(QStringLiteral("/repo/wt with space"),
                                       QStringLiteral("feature/x"), false)};
    stub->setWorkspaceData(WorkspaceKind::Worktrees, worktrees);
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });

    // --- the action-bar entry point exists and is usable -------------------
    auto *workspaceButton = panel.findChild<QPushButton *>(QStringLiteral("vcs-action-workspace"));
    check("vcs.workspace.button-present", workspaceButton != nullptr);
    check("vcs.workspace.button-enabled", workspaceButton != nullptr && workspaceButton->isEnabled());

    // --- the two listings are requested and rendered -----------------------
    panel.refreshWorkspace();
    const std::vector<WorkspaceRequest> &requests = raw->workspaceRequests();
    check("vcs.workspace.stub-requests", requests.size() == 2,
          QStringLiteral("requests=%1").arg(requests.size()));
    check("vcs.workspace.stub-stash-request",
          requests.size() == 2 && requests[0].kind == WorkspaceKind::Stashes
              && requests[0].root == "/tmp/repo-git",
          requests.empty() ? QStringLiteral("no request")
                           : QString::fromStdString(requests[0].root));
    check("vcs.workspace.stub-worktree-request",
          requests.size() == 2 && requests[1].kind == WorkspaceKind::Worktrees);
    check("vcs.workspace.stashes-visible", panel.stashCount() == 1, panel.stashText(0));
    check("vcs.workspace.stash-text",
          panel.stashText(0).contains(QStringLiteral("stash@{0}"))
              && panel.stashText(0).contains(QStringLiteral("first")),
          panel.stashText(0));
    check("vcs.workspace.worktrees-visible", panel.worktreeCount() == 2,
          QStringLiteral("rows=%1").arg(panel.worktreeCount()));
    check("vcs.workspace.worktree-text-with-space",
          panel.worktreeText(1).contains(QStringLiteral("/repo/wt with space"))
              && panel.worktreeText(1).contains(QStringLiteral("feature/x")),
          panel.worktreeText(1));
    check("vcs.workspace.status-text",
          panel.workspaceStatusText().contains(QStringLiteral("1"))
              && panel.workspaceStatusText().contains(QStringLiteral("2")),
          panel.workspaceStatusText());
    check("vcs.workspace.out-of-range-row", panel.stashText(7).isEmpty() && panel.worktreeText(-1).isEmpty());

    // --- the exact write requests ------------------------------------------
    check("vcs.workspace.stash-save-requested", panel.stashSave(QStringLiteral("wip message")));
    check("vcs.workspace.stash-save-request",
          raw->writes().back().op == WriteOp::GitStashSave
              && raw->writes().back().message == "wip message",
          QString::fromStdString(raw->writes().back().message));
    check("vcs.workspace.stash-save-empty-message", panel.stashSave());
    check("vcs.workspace.stash-save-empty-request",
          raw->writes().back().op == WriteOp::GitStashSave
              && raw->writes().back().message.empty());
    check("vcs.workspace.stash-pop-requested", panel.stashPop(QStringLiteral("stash@{0}")));
    check("vcs.workspace.stash-pop-request",
          raw->writes().back().op == WriteOp::GitStashPop
              && raw->writes().back().target == "stash@{0}",
          QString::fromStdString(raw->writes().back().target));
    check("vcs.workspace.stash-apply-requested", panel.stashApply(QStringLiteral("stash@{1}")));
    check("vcs.workspace.stash-apply-request",
          raw->writes().back().op == WriteOp::GitStashApply
              && raw->writes().back().target == "stash@{1}");
    check("vcs.workspace.worktree-add-requested",
          panel.worktreeAdd(QStringLiteral("../wt-new"), QStringLiteral("feature/x")));
    check("vcs.workspace.worktree-add-request",
          raw->writes().back().op == WriteOp::GitWorktreeAdd
              && raw->writes().back().target == "../wt-new"
              && raw->writes().back().name == "feature/x",
          QString::fromStdString(raw->writes().back().target));
    check("vcs.workspace.submodule-init-requested", panel.submoduleInit());
    check("vcs.workspace.submodule-init-request",
          raw->writes().back().op == WriteOp::GitSubmoduleInit
              && raw->writes().back().target.empty() && raw->writes().back().paths.empty());
    check("vcs.workspace.submodule-update-requested", panel.submoduleUpdate());
    check("vcs.workspace.submodule-update-request",
          raw->writes().back().op == WriteOp::GitSubmoduleUpdate);

    // --- AC-5.3: dangerous operations confirm twice, cancel = zero writes ---
    const std::size_t beforeDrop = raw->writes().size();
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const int callsBeforeDrop = panel.confirmCalls();
    check("vcs.workspace.stash-drop-declined",
          !panel.stashDrop(QStringLiteral("stash@{0}")));
    check("vcs.workspace.stash-drop-declined-zero-write", raw->writes().size() == beforeDrop,
          QStringLiteral("writes=%1").arg(raw->writes().size()));
    check("vcs.workspace.stash-drop-first-confirm",
          panel.confirmCalls() == callsBeforeDrop + 1);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.workspace.stash-drop-requested", panel.stashDrop(QStringLiteral("stash@{0}")));
    check("vcs.workspace.stash-drop-request",
          raw->writes().back().op == WriteOp::GitStashDrop
              && raw->writes().back().target == "stash@{0}");
    check("vcs.workspace.stash-drop-double-confirmed",
          panel.confirmCalls() == callsBeforeDrop + 3,
          QStringLiteral("confirm calls=%1").arg(panel.confirmCalls()));

    const std::size_t beforeRemove = raw->writes().size();
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const int callsBeforeRemove = panel.confirmCalls();
    check("vcs.workspace.worktree-remove-declined",
          !panel.worktreeRemove(QStringLiteral("../wt-new")));
    check("vcs.workspace.worktree-remove-declined-zero-write",
          raw->writes().size() == beforeRemove);
    check("vcs.workspace.worktree-remove-first-confirm",
          panel.confirmCalls() == callsBeforeRemove + 1);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.workspace.worktree-remove-requested",
          panel.worktreeRemove(QStringLiteral("../wt-new")));
    check("vcs.workspace.worktree-remove-request",
          raw->writes().back().op == WriteOp::GitWorktreeRemove
              && raw->writes().back().target == "../wt-new");
    check("vcs.workspace.worktree-remove-double-confirmed",
          panel.confirmCalls() == callsBeforeRemove + 3);
    // NIT from the 5b/6b reviews: accept the first question, decline the second.
    {
        int step = 0;
        panel.setConfirmProvider(
            [&step](const QString &, const QString &) { return ++step == 1; });
        const std::size_t beforeSecondStep = raw->writes().size();
        const int callsBeforeSecondStep = panel.confirmCalls();
        check("vcs.workspace.worktree-remove-second-step-declined",
              !panel.worktreeRemove(QStringLiteral("../wt-new")));
        check("vcs.workspace.worktree-remove-second-step-zero-write",
              raw->writes().size() == beforeSecondStep);
        check("vcs.workspace.worktree-remove-second-step-two-confirms",
              panel.confirmCalls() == callsBeforeSecondStep + 2);
        panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    }

    // --- refusals write nothing -------------------------------------------
    const std::size_t beforeRefusal = raw->writes().size();
    check("vcs.workspace.worktree-add-empty-path-refused", !panel.worktreeAdd(QString()));
    check("vcs.workspace.worktree-add-empty-path-zero-write",
          raw->writes().size() == beforeRefusal);
    check("vcs.workspace.worktree-add-option-path-refused",
          !panel.worktreeAdd(QStringLiteral("--force")));
    check("vcs.workspace.worktree-add-option-path-zero-write",
          raw->writes().size() == beforeRefusal);
    check("vcs.workspace.worktree-remove-empty-refused", !panel.worktreeRemove(QString()));

    // --- a failed listing is visible with the backend's real reason --------
    auto failing = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    failing->setWorkspaceData(WorkspaceKind::Stashes, stashes);
    failing->setWorkspaceError(WorkspaceKind::Worktrees,
                               "git: 'worktree' is not a git command. See 'git --help'.");
    VcsPanel broken;
    broken.setRoot("/tmp/repo-git");
    broken.setSource(std::move(failing));
    broken.refreshWorkspace();
    check("vcs.workspace.error-visible",
          broken.workspaceErrorText().contains(QStringLiteral("not a git command")),
          broken.workspaceErrorText());
    check("vcs.workspace.error-status-text",
          broken.workspaceStatusText().contains(QStringLiteral("not a git command")));
}

void checkWorkspaceDialog()
{
    WorkspaceData stashes;
    stashes.entries = {stashEntry(QStringLiteral("stash@{0}"), QStringLiteral("On main: one")),
                       stashEntry(QStringLiteral("stash@{1}"), QStringLiteral("On main: two"))};
    WorkspaceData worktrees;
    worktrees.entries = {worktreeEntry(QStringLiteral("/repo"), QStringLiteral("main"), true),
                         worktreeEntry(QStringLiteral("/wt"), QString(), false, true)};

    WorkspaceDialog dialog;
    dialog.setFields(QStringLiteral("message"), QStringLiteral("../wt-new"),
                     QStringLiteral("feature/x"));
    dialog.setStashes(stashes.entries);
    dialog.setWorktrees(worktrees.entries);
    dialog.setStatusText(QStringLiteral("2 stash(es), 2 worktree(s)"));
    check("vcs.workspace.dialog.fields",
          dialog.stashMessage() == QStringLiteral("message")
              && dialog.worktreePath() == QStringLiteral("../wt-new")
              && dialog.worktreeBranch() == QStringLiteral("feature/x"));
    auto *stashList = dialog.findChild<QListWidget *>(QStringLiteral("vcs-workspace-stashes"));
    auto *worktreeList = dialog.findChild<QListWidget *>(QStringLiteral("vcs-workspace-worktrees"));
    check("vcs.workspace.dialog.stash-list", stashList != nullptr && stashList->count() == 2,
          stashList == nullptr ? QStringLiteral("missing") : QStringLiteral("rows=2"));
    check("vcs.workspace.dialog.worktree-list",
          worktreeList != nullptr && worktreeList->count() == 2);
    check("vcs.workspace.dialog.selected-stash", dialog.selectedStash() == QStringLiteral("stash@{0}"),
          dialog.selectedStash());
    check("vcs.workspace.dialog.selected-worktree",
          dialog.selectedWorktree() == QStringLiteral("/repo"), dialog.selectedWorktree());
    check("vcs.workspace.dialog.detached-row",
          worktreeList != nullptr
              && worktreeList->item(1)->text().contains(QCoreApplication::translate("VEdit", "detached")),
          worktreeList == nullptr ? QString() : worktreeList->item(1)->text());
    // The dialog's Close button must not turn the dialog into an action.
    check("vcs.workspace.dialog.no-action-by-default", dialog.action() == WorkspaceDialog::Action::None);
    // A REAL click on the Save stash button selects that action and accepts.
    auto *saveButton = dialog.findChild<QPushButton *>(QStringLiteral("vcs-workspace-stash-save"));
    check("vcs.workspace.dialog.save-button", saveButton != nullptr);
    if (saveButton != nullptr) {
        saveButton->click();
        check("vcs.workspace.dialog.click-selects-action",
              dialog.action() == WorkspaceDialog::Action::StashSave);
    }
    auto *dropButton = dialog.findChild<QPushButton *>(QStringLiteral("vcs-workspace-stash-drop"));
    if (dropButton != nullptr) {
        dropButton->click();
        check("vcs.workspace.dialog.drop-action",
              dialog.action() == WorkspaceDialog::Action::StashDrop);
    }
}

#if defined(VEDIT_VCS_HAVE_FFI)
void checkWorkspaceE2E()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        for (const char *name : {"vcs.workspace.e2e.git", "vcs.workspace.e2e.fixture",
                                 "vcs.workspace.e2e.stash-save", "vcs.workspace.e2e.stash-list",
                                 "vcs.workspace.e2e.stash-apply", "vcs.workspace.e2e.stash-drop",
                                 "vcs.workspace.e2e.stash-pop", "vcs.workspace.e2e.worktree-add",
                                 "vcs.workspace.e2e.worktree-list", "vcs.workspace.e2e.worktree-dirty",
                                 "vcs.workspace.e2e.worktree-remove",
                                 "vcs.workspace.e2e.submodule-init",
                                 "vcs.workspace.e2e.submodule-update"}) {
            skip(name, "git is not on PATH");
        }
        return;
    }
    check("vcs.workspace.e2e.git", true, git);
    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/workspace-repo");
    QDir().mkpath(repo + QStringLiteral("/rtl"));
    const bool fixture =
        writeText(repo + QStringLiteral("/rtl/a.sv"),
                  QStringLiteral("module a;\nbase\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, repo)
        && runProcess(git, {"config", "user.email", "ws@example.com"}, repo)
        && runProcess(git, {"config", "user.name", "VEdit Workspace"}, repo)
        && runProcess(git, {"add", "rtl/a.sv"}, repo)
        && runProcess(git, {"commit", "-m", "workspace base"}, repo);
    check("vcs.workspace.e2e.fixture", temp.isValid() && fixture, repo);
    if (!fixture) {
        return;
    }
    check("vcs.workspace.e2e.not-worktree",
          repo.startsWith(QDir::tempPath())
              && QFileInfo(repo).canonicalFilePath()
                  != QFileInfo(QDir::currentPath()).canonicalFilePath(),
          QStringLiteral("fixture=%1 cwd=%2").arg(repo, QDir::currentPath()));

    writeText(repo + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\nstashed change\nendmodule\n"));
    VcsPanel panel;
    panel.setRoot(repo.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    const auto writeDone = [&panel]() {
        return waitUntil([&panel]() { return !panel.writeInFlight(); }, 30000);
    };
    waitUntil([&panel]() { return !panel.headerText().isEmpty(); }, 30000);

    // --- stash save → list → apply → drop → pop ---------------------------
    check("vcs.workspace.e2e.stash-save-requested",
          panel.stashSave(QStringLiteral("e2e stash one")));
    writeDone();
    check("vcs.workspace.e2e.stash-save",
          panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::GitStashSave,
          panel.messageText());
    QString listing;
    runProcess(git, {"stash", "list"}, repo, &listing);
    check("vcs.workspace.e2e.stash-save-git",
          listing.contains(QStringLiteral("e2e stash one")), listing.trimmed());
    QString strContent;
    panel.refreshWorkspace();
    const bool stashListed =
        waitUntil([&panel]() { return panel.stashCount() == 1; }, 20000);
    check("vcs.workspace.e2e.stash-list", stashListed,
          QStringLiteral("stashes=%1").arg(panel.stashCount()));
    const QString stashRef = stashListed
        ? QString::fromStdString(panel.stashes().entries[0].refName)
        : QStringLiteral("stash@{0}");
    check("vcs.workspace.e2e.stash-ref",
          stashRef == QStringLiteral("stash@{0}"), stashRef);

    check("vcs.workspace.e2e.stash-apply-requested", panel.stashApply(stashRef));
    writeDone();
    check("vcs.workspace.e2e.stash-apply", panel.lastWriteOk(), panel.messageText());
    QString content;
    {
        QFile file(repo + QStringLiteral("/rtl/a.sv"));
        if (file.open(QIODevice::ReadOnly)) {
            content = QString::fromUtf8(file.readAll());
        }
    }
    check("vcs.workspace.e2e.stash-apply-kept",
          content.contains(QStringLiteral("stashed change")), content.trimmed());
    panel.refreshWorkspace();
    check("vcs.workspace.e2e.stash-apply-count",
          waitUntil([&panel]() { return panel.stashCount() == 1; }, 20000));

    check("vcs.workspace.e2e.stash-drop-requested", panel.stashDrop(stashRef));
    writeDone();
    check("vcs.workspace.e2e.stash-drop", panel.lastWriteOk(), panel.messageText());
    panel.refreshWorkspace();
    check("vcs.workspace.e2e.stash-drop-count",
          waitUntil([&panel]() { return panel.stashCount() == 0; }, 20000));

    // A second save/pop proves pop restores AND drops in one operation.
    writeText(repo + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\npopped change\nendmodule\n"));
    check("vcs.workspace.e2e.stash-save-two", panel.stashSave(QStringLiteral("e2e stash two")));
    writeDone();
    check("vcs.workspace.e2e.stash-pop-requested", panel.stashPop());
    writeDone();
    check("vcs.workspace.e2e.stash-pop", panel.lastWriteOk(), panel.messageText());
    strContent.clear();
    runProcess(git, {"stash", "list"}, repo, &strContent);
    check("vcs.workspace.e2e.stash-pop-dropped", strContent.trimmed().isEmpty(),
          strContent.trimmed());
    QString popped;
    {
        QFile file(repo + QStringLiteral("/rtl/a.sv"));
        if (file.open(QIODevice::ReadOnly)) {
            popped = QString::fromUtf8(file.readAll());
        }
    }
    check("vcs.workspace.e2e.stash-pop-content",
          popped.contains(QStringLiteral("popped change")), popped.trimmed());
    // Clean the working tree for the worktree fixture below (the pop left the
    // applied change in place, which is what the check above just proved).
    runProcess(git, {"checkout", "--", "."}, repo);

    // --- worktree add → list → remove (git 2.5+; the delivery baseline has no
    // `worktree` command, so its absence is an explicit SKIP with the reason) --
    QString worktreeProbe;
    const bool supportsWorktree =
        runProcess(git, {"worktree", "list"}, repo, &worktreeProbe);
    if (!supportsWorktree) {
        skip("vcs.workspace.e2e.worktree-add",
             "this git has no 'worktree' command (the delivery baseline, git 1.8.3.1)");
        skip("vcs.workspace.e2e.worktree-list", "no 'worktree' command");
        skip("vcs.workspace.e2e.worktree-dirty", "no 'worktree' command");
        skip("vcs.workspace.e2e.worktree-remove", "no 'worktree' command");
    } else {
        // A clean tree is required before a worktree can be added: the checks
        // above left the tree clean (the pop check restored it), and `git
        // checkout -- .` is repeated here so the fixture never assumes it.
        runProcess(git, {"checkout", "--", "."}, repo);
        runProcess(git, {"branch", "feature/ws"}, repo);
        const QString linked = temp.path() + QStringLiteral("/linked worktree");
        check("vcs.workspace.e2e.worktree-add-requested",
              panel.worktreeAdd(linked, QStringLiteral("feature/ws")));
        writeDone();
        check("vcs.workspace.e2e.worktree-add",
              panel.lastWriteOk() && QFileInfo(linked + QStringLiteral("/rtl/a.sv")).isFile(),
              panel.messageText());
        panel.refreshWorkspace();
        check("vcs.workspace.e2e.worktree-list",
              waitUntil([&panel]() { return panel.worktreeCount() == 2; }, 20000),
              QStringLiteral("worktrees=%1").arg(panel.worktreeCount()));
        check("vcs.workspace.e2e.worktree-list-text",
              panel.worktreeText(1).contains(QStringLiteral("linked worktree"))
                  && panel.worktreeText(1).contains(QStringLiteral("feature/ws")),
              panel.worktreeText(1));
        // A dirty worktree is refused by git itself (no --force is ever passed).
        writeText(linked + QStringLiteral("/rtl/untracked.sv"),
                  QStringLiteral("module u;\nendmodule\n"));
        check("vcs.workspace.e2e.worktree-remove-requested", panel.worktreeRemove(linked));
        writeDone();
        check("vcs.workspace.e2e.worktree-dirty",
              !panel.lastWriteOk() && QFileInfo(linked).isDir(),
              panel.messageText());
        QFile::remove(linked + QStringLiteral("/rtl/untracked.sv"));
        check("vcs.workspace.e2e.worktree-remove-requested-two", panel.worktreeRemove(linked));
        writeDone();
        check("vcs.workspace.e2e.worktree-remove",
              panel.lastWriteOk() && !QFileInfo(linked).exists(), panel.messageText());
        panel.refreshWorkspace();
        check("vcs.workspace.e2e.worktree-remove-count",
              waitUntil([&panel]() { return panel.worktreeCount() == 1; }, 20000));
    }

    // --- submodules: a LOCAL fixture (no network) --------------------------
    // git 2.53 refuses `file://` submodule clones by default (CVE-2022-39253) and
    // reads the override from the PROCESS config, never from the repository, so
    // only the FIXTURE opens it — the product keeps the default policy.
    const QString source = temp.path() + QStringLiteral("/submodule-source");
    QDir().mkpath(source);
    const bool submoduleFixture =
        writeText(source + QStringLiteral("/sub.txt"), QStringLiteral("submodule payload\n"))
        && runProcess(git, {"init", "-q"}, source)
        && runProcess(git, {"config", "user.email", "sub@example.com"}, source)
        && runProcess(git, {"config", "user.name", "VEdit Sub"}, source)
        && runProcess(git, {"add", "sub.txt"}, source)
        && runProcess(git, {"commit", "-m", "submodule base"}, source)
        && runProcess(git, {"-c", "protocol.file.allow=always", "submodule", "add", source, "sub"},
                      repo)
        && runProcess(git, {"commit", "-m", "add submodule"}, repo);
    const QString clone = temp.path() + QStringLiteral("/submodule-clone");
    const bool cloned = submoduleFixture
        && runProcess(git, {"clone", "-q", repo, clone}, temp.path());
    if (!cloned) {
        skip("vcs.workspace.e2e.submodule-init", "git could not build the local submodule fixture");
        skip("vcs.workspace.e2e.submodule-update", "git could not build the local submodule fixture");
    } else {
        const QByteArray previousCount = qgetenv("GIT_CONFIG_COUNT");
        qputenv("GIT_CONFIG_COUNT", "1");
        qputenv("GIT_CONFIG_KEY_0", "protocol.file.allow");
        qputenv("GIT_CONFIG_VALUE_0", "always");
        VcsPanel submodulePanel;
        submodulePanel.setRoot(clone.toStdString());
        submodulePanel.setSource(std::make_unique<FfiVcsSource>());
        waitUntil([&submodulePanel]() { return !submodulePanel.headerText().isEmpty(); }, 30000);
        const auto submoduleDone = [&submodulePanel]() {
            return waitUntil([&submodulePanel]() { return !submodulePanel.writeInFlight(); }, 60000);
        };
        check("vcs.workspace.e2e.submodule-init-requested", submodulePanel.submoduleInit());
        submoduleDone();
        check("vcs.workspace.e2e.submodule-init", submodulePanel.lastWriteOk(),
              submodulePanel.messageText());
        check("vcs.workspace.e2e.submodule-update-requested", submodulePanel.submoduleUpdate());
        submoduleDone();
        check("vcs.workspace.e2e.submodule-update",
              submodulePanel.lastWriteOk()
                  && QFileInfo(clone + QStringLiteral("/sub/sub.txt")).isFile(),
              submodulePanel.messageText());
        QString status;
        runProcess(git, {"submodule", "status"}, clone, &status);
        check("vcs.workspace.e2e.submodule-in-sync",
              !status.isEmpty() && status.at(0) == QLatin1Char(' '), status.trimmed());
        if (previousCount.isEmpty()) {
            qunsetenv("GIT_CONFIG_COUNT");
        } else {
            qputenv("GIT_CONFIG_COUNT", previousCount);
        }
        qunsetenv("GIT_CONFIG_KEY_0");
        qunsetenv("GIT_CONFIG_VALUE_0");
    }
}
#else
void checkWorkspaceE2E()
{
    skip("vcs.workspace.e2e.stash-save", "lane built without vd-ffi (stub-only build)");
}
#endif

// ==========================================================================
// Batch 6b (AC-6.2 / AC-6.3 / AC-6.5): the Subversion backend and the
// per-backend capability table
// ==========================================================================

Info svnInfo()
{
    Info info;
    info.backend = Backend::Subversion;
    info.branch = "trunk";
    info.revision = "42";
    return info;
}

Status svnStatus()
{
    Status status;
    status.backend = Backend::Subversion;
    status.changes = {Change{"rtl/top.sv", 2}, Change{"rtl/new.sv", 1}};
    return status;
}

std::vector<Diff> svnDiffs()
{
    Diff diff;
    diff.path = "rtl/top.sv";
    diff.text = "Index: rtl/top.sv\n--- rtl/top.sv (revision 41)\n+++ rtl/top.sv (working copy)\n"
                "-module old;\n+module top;\n";
    return {diff};
}

void checkSvnCapabilities()
{
    // --- the table itself (AC-6.5: the 1.14.5 capability set) --------------
    const auto svnTable = capabilitiesFor(Backend::Subversion);
    check("vcs.capability.svn.table-size", svnTable.size() == 17,
          QStringLiteral("rows=%1").arg(svnTable.size()));
    for (const PanelAction action : {PanelAction::Add, PanelAction::Revert, PanelAction::Commit,
                                     PanelAction::Remove, PanelAction::Update,
                                     PanelAction::Refresh}) {
        check(qPrintable(QStringLiteral("vcs.capability.svn.%1-supported")
                             .arg(QString::fromLatin1(panelActionToken(action)))),
              capabilityFor(Backend::Subversion, action).supported);
    }
    for (const PanelAction action : {PanelAction::Unstage, PanelAction::Fetch, PanelAction::Pull,
                                     PanelAction::Push, PanelAction::Branch, PanelAction::Tag,
                                     PanelAction::Remote, PanelAction::Workspace,
                                     PanelAction::Continue, PanelAction::Abort,
                                     PanelAction::History}) {
        const auto row = capabilityFor(Backend::Subversion, action);
        check(qPrintable(QStringLiteral("vcs.capability.svn.%1-unsupported")
                             .arg(QString::fromLatin1(panelActionToken(action)))),
              !row.supported);
        check(qPrintable(QStringLiteral("vcs.capability.svn.%1-reason")
                             .arg(QString::fromLatin1(panelActionToken(action)))),
              !row.reason.isEmpty());
    }
    // The reasons are REAL properties of Subversion, not "not implemented".
    check("vcs.capability.svn.unstage-reason",
          capabilityFor(Backend::Subversion, PanelAction::Unstage).reason.contains(
              QStringLiteral("staging area")),
          capabilityFor(Backend::Subversion, PanelAction::Unstage).reason);
    check("vcs.capability.svn.workspace-reason",
          capabilityFor(Backend::Subversion, PanelAction::Workspace).reason.contains(
              QStringLiteral("Git concepts")),
          capabilityFor(Backend::Subversion, PanelAction::Workspace).reason);
    check("vcs.capability.svn.remote-reason",
          capabilityFor(Backend::Subversion, PanelAction::Remote).reason.contains(
              QStringLiteral("URL")),
          capabilityFor(Backend::Subversion, PanelAction::Remote).reason);
    check("vcs.capability.svn.history-reason",
          capabilityFor(Backend::Subversion, PanelAction::History).reason.contains(
              QStringLiteral("accepted read set")),
          capabilityFor(Backend::Subversion, PanelAction::History).reason);
    check("vcs.capability.svn.disabled-count",
          disabledActionReasons(Backend::Subversion).size() == 11,
          QStringLiteral("disabled=%1").arg(disabledActionReasons(Backend::Subversion).size()));
    check("vcs.capability.svn.report",
          capabilityReport(Backend::Subversion).startsWith(QStringLiteral("svn refresh=yes add=yes"))
              && capabilityReport(Backend::Subversion).contains(QStringLiteral(" unstage=no(")),
          capabilityReport(Backend::Subversion).left(120));

    // Git keeps its full surface; Remove/Update are Subversion's spellings.
    check("vcs.capability.git.table-size", capabilitiesFor(Backend::Git).size() == 17);
    check("vcs.capability.git.unstage-supported",
          capabilityFor(Backend::Git, PanelAction::Unstage).supported);
    check("vcs.capability.git.remove-unsupported",
          !capabilityFor(Backend::Git, PanelAction::Remove).supported
              && capabilityFor(Backend::Git, PanelAction::Remove).reason.contains(
                  QStringLiteral("deleted file is staged")),
          capabilityFor(Backend::Git, PanelAction::Remove).reason);
    check("vcs.capability.git.update-unsupported",
          !capabilityFor(Backend::Git, PanelAction::Update).supported);
    // No repository: only Refresh.
    const auto noneTable = capabilitiesFor(Backend::None);
    int noneSupported = 0;
    for (const auto &row : noneTable) {
        noneSupported += row.supported ? 1 : 0;
    }
    check("vcs.capability.none.only-refresh", noneSupported == 1);

    // --- the panel renders the SAME three columns with SVN verbs -----------
    auto stub = std::make_unique<StubVcsSource>(svnInfo(), svnStatus(), svnDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-svn");
    panel.setSource(std::move(stub));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.svn.panel.backend", panel.backend() == Backend::Subversion);
    check("vcs.svn.panel.header", panel.headerText().contains(QStringLiteral("Subversion")),
          panel.headerText());
    check("vcs.svn.panel.three-columns", panel.fileCount() == 2,
          QStringLiteral("files=%1").arg(panel.fileCount()));
    check("vcs.svn.panel.add-label",
          panel.actionLabel(QStringLiteral("vcs-action-stage")) == QStringLiteral("Add"),
          panel.actionLabel(QStringLiteral("vcs-action-stage")));
    check("vcs.svn.panel.revert-label",
          panel.actionLabel(QStringLiteral("vcs-action-discard")).startsWith(
              QStringLiteral("Revert")),
          panel.actionLabel(QStringLiteral("vcs-action-discard")));
    check("vcs.svn.panel.update-label",
          panel.actionLabel(QStringLiteral("vcs-action-pull")) == QStringLiteral("Update"));
    check("vcs.svn.panel.remove-label",
          panel.actionLabel(QStringLiteral("vcs-action-remove")).startsWith(
              QStringLiteral("Remove")));
    check("vcs.svn.panel.capability-line",
          panel.capabilitySummaryText().contains(QStringLiteral("Subversion"))
              && panel.capabilitySummaryText().contains(QStringLiteral("add, commit, revert")),
          panel.capabilitySummaryText());
    check("vcs.svn.panel.staged-column-hidden", !panel.stagedColumnVisible());
    // A file-scoped action is enabled only with a selection, exactly like Git.
    check("vcs.svn.panel.enabled.add-before-selection",
          !panel.actionEnabled(QStringLiteral("vcs-action-stage")));
    panel.selectFile("rtl/top.sv");
    check("vcs.svn.panel.enabled.add",
          panel.actionEnabled(QStringLiteral("vcs-action-stage")));
    check("vcs.svn.panel.enabled.commit",
          panel.actionEnabled(QStringLiteral("vcs-action-commit")));
    check("vcs.svn.panel.enabled.update",
          panel.actionEnabled(QStringLiteral("vcs-action-pull")));
    check("vcs.svn.panel.disabled.unstage",
          !panel.actionEnabled(QStringLiteral("vcs-action-unstage")));
    check("vcs.svn.panel.unstage-reason",
          panel.actionToolTip(QStringLiteral("vcs-action-unstage")).contains(
              QStringLiteral("staging area")),
          panel.actionToolTip(QStringLiteral("vcs-action-unstage")));
    check("vcs.svn.panel.disabled.workspace",
          !panel.actionEnabled(QStringLiteral("vcs-action-workspace")));
    check("vcs.svn.panel.workspace-reason",
          panel.actionToolTip(QStringLiteral("vcs-action-workspace")).contains(
              QStringLiteral("Git concepts")),
          panel.actionToolTip(QStringLiteral("vcs-action-workspace")));
    check("vcs.svn.panel.disabled.fetch-push-branch-tag-remote",
          !panel.actionEnabled(QStringLiteral("vcs-action-fetch"))
              && !panel.actionEnabled(QStringLiteral("vcs-action-push"))
              && !panel.actionEnabled(QStringLiteral("vcs-action-branch"))
              && !panel.actionEnabled(QStringLiteral("vcs-action-tag"))
              && !panel.actionEnabled(QStringLiteral("vcs-action-remote")));
    check("vcs.svn.panel.disabled.continue-abort",
          !panel.actionEnabled(QStringLiteral("vcs-action-continue"))
              && !panel.actionEnabled(QStringLiteral("vcs-action-abort")));
    check("vcs.svn.panel.diff", panel.diffText().contains(QStringLiteral("+module top;")),
          panel.diffText().left(120));

    // --- the SAME request path as Git (AC-6.3) ----------------------------
    check("vcs.svn.write.add-requested", panel.stageSelected());
    check("vcs.svn.write.add-request",
          raw->writes().back().op == WriteOp::SvnAdd
              && raw->writes().back().paths.size() == 1
              && raw->writes().back().paths.front() == "rtl/top.sv",
          QString::fromStdString(raw->writes().back().paths.empty()
                                     ? std::string()
                                     : raw->writes().back().paths.front()));

    check("vcs.svn.write.commit-requested",
          panel.commit(QStringLiteral("svn fixture message"), QString(), false));
    check("vcs.svn.write.commit-request",
          raw->writes().back().op == WriteOp::SvnCommit
              && raw->writes().back().message == "svn fixture message"
              && raw->writes().back().paths.empty(),
          QString::fromStdString(raw->writes().back().message));
    const std::size_t beforeAmend = raw->writes().size();
    check("vcs.svn.write.amend-refused",
          !panel.commit(QStringLiteral("another message"), QString(), true));
    check("vcs.svn.write.amend-zero-write", raw->writes().size() == beforeAmend);
    check("vcs.svn.write.amend-reason",
          panel.messageText().contains(QStringLiteral("no amend")), panel.messageText());

    check("vcs.svn.write.update-requested", panel.pull());
    check("vcs.svn.write.update-request",
          raw->writes().back().op == WriteOp::SvnUpdate
              && raw->writes().back().paths.empty());

    // revert and remove are destructive: a declined first step writes nothing.
    const std::size_t beforeRevert = raw->writes().size();
    const int revertCalls = panel.confirmCalls();
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.svn.write.revert-declined", !panel.discardSelected());
    check("vcs.svn.write.revert-declined-zero-write", raw->writes().size() == beforeRevert);
    check("vcs.svn.write.revert-first-confirm",
          panel.confirmCalls() == revertCalls + 1);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.svn.write.revert-requested", panel.discardSelected());
    check("vcs.svn.write.revert-request",
          raw->writes().back().op == WriteOp::SvnRevert
              && raw->writes().back().paths.size() == 1);
    check("vcs.svn.write.revert-double-confirmed",
          panel.confirmCalls() == revertCalls + 3);

    const std::size_t beforeRemove = raw->writes().size();
    const int removeCalls = panel.confirmCalls();
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.svn.write.remove-declined", !panel.removeSelected());
    check("vcs.svn.write.remove-declined-zero-write", raw->writes().size() == beforeRemove);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.svn.write.remove-requested", panel.removeSelected());
    check("vcs.svn.write.remove-request",
          raw->writes().back().op == WriteOp::SvnRemove
              && raw->writes().back().paths.front() == "rtl/top.sv");
    check("vcs.svn.write.remove-double-confirmed",
          panel.confirmCalls() == removeCalls + 3);

    // Unstage is not a Subversion action: it refuses with the real reason.
    const std::size_t beforeUnstage = raw->writes().size();
    check("vcs.svn.write.unstage-refused", !panel.unstageSelected());
    check("vcs.svn.write.unstage-zero-write", raw->writes().size() == beforeUnstage);
    check("vcs.svn.write.unstage-message",
          panel.messageText().contains(QStringLiteral("staging area")), panel.messageText());

    // --- Git keeps its own verbs, same widget (parity control) ------------
    auto gitStub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    VcsPanel gitPanel;
    gitPanel.setRoot("/tmp/repo-git");
    gitPanel.setSource(std::move(gitStub));
    check("vcs.git.panel.stage-label",
          gitPanel.actionLabel(QStringLiteral("vcs-action-stage")) == QStringLiteral("Stage"));
    check("vcs.git.panel.staged-column-visible", gitPanel.stagedColumnVisible());
    check("vcs.git.panel.remove-disabled",
          !gitPanel.actionEnabled(QStringLiteral("vcs-action-remove")));
    check("vcs.git.panel.remove-reason",
          gitPanel.actionToolTip(QStringLiteral("vcs-action-remove")).contains(
              QStringLiteral("deleted file is staged")),
          gitPanel.actionToolTip(QStringLiteral("vcs-action-remove")));
    check("vcs.git.panel.capability-line",
          gitPanel.capabilitySummaryText().contains(QStringLiteral("Git")));
}

#if defined(VEDIT_VCS_HAVE_FFI)
void checkSvnE2E()
{
    const QString svn = qEnvironmentVariable("VEDIT_SVN_BIN");
    const QString svnadmin = qEnvironmentVariable("VEDIT_SVNADMIN");
    if (svn.isEmpty() || svnadmin.isEmpty() || !QFileInfo(svn).isExecutable()
        || !QFileInfo(svnadmin).isExecutable()) {
        for (const char *name : {"vcs.svn.e2e.tools", "vcs.svn.e2e.fixture", "vcs.svn.e2e.not-worktree",
                                 "vcs.svn.e2e.backend", "vcs.svn.e2e.header", "vcs.svn.e2e.changed-row",
                                 "vcs.svn.e2e.add-request", "vcs.svn.e2e.add-ok",
                                 "vcs.svn.e2e.commit-ok", "vcs.svn.e2e.revision-1",
                                 "vcs.svn.e2e.diff", "vcs.svn.e2e.revert-ok",
                                 "vcs.svn.e2e.revert-restored", "vcs.svn.e2e.update-ok",
                                 "vcs.svn.e2e.update-file", "vcs.svn.e2e.update-row",
                                 "vcs.svn.e2e.remove-ok",
                                 "vcs.svn.e2e.remove-committed", "vcs.svn.e2e.revision-3",
                                 "vcs.svn.e2e.no-cannot-run"}) {
            skip(name, "set VEDIT_SVN_BIN and VEDIT_SVNADMIN to run the real Subversion E2E");
        }
        return;
    }
    check("vcs.svn.e2e.tools", true, svn + QStringLiteral(" + ") + svnadmin);
    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/repo");
    const QString wc = temp.path() + QStringLiteral("/wc");
    const QString wc2 = temp.path() + QStringLiteral("/wc2");
    const bool created = runProcess(svnadmin, {"create", repo}, temp.path());
    const QString url = QStringLiteral("file://") + repo;
    const bool checkedOut = created
        && runProcess(svn, {"checkout", "-q", url, wc}, temp.path())
        && runProcess(svn, {"checkout", "-q", url, wc2}, temp.path());
    check("vcs.svn.e2e.fixture", temp.isValid() && checkedOut, wc);
    if (!checkedOut) {
        return;
    }
    check("vcs.svn.e2e.not-worktree",
          wc.startsWith(QDir::tempPath())
              && QFileInfo(wc + QStringLiteral("/.svn")).isDir(),
          wc);
    // The product's resolver spawns the bundled client: no svn on PATH, so the
    // documented env fallback is what the read/write runners use.
    const QByteArray previousSvn = qgetenv("VEDIT_SVN");
    qputenv("VEDIT_SVN", svn.toUtf8());
    writeText(wc + QStringLiteral("/a.sv"), QStringLiteral("module a;\nendmodule\n"));

    VcsPanel panel;
    panel.setRoot(wc.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    const bool ready = waitUntil([&panel]() {
        return panel.backend() == Backend::Subversion && panel.fileCount() >= 1;
    }, 30000);
    check("vcs.svn.e2e.backend",
          ready && panel.backend() == Backend::Subversion, panel.headerText());
    // The header is set by the INFO read, which arrives after the status read.
    const bool infoReady = waitUntil([&panel]() {
        return panel.headerText().contains(QStringLiteral("Subversion"));
    }, 30000);
    check("vcs.svn.e2e.header",
          infoReady && panel.headerText().contains(QStringLiteral("repo"))
              && panel.headerText().contains(QStringLiteral("@ ")),
          panel.headerText());
    check("vcs.svn.e2e.no-cannot-run",
          !panel.headerText().contains(QStringLiteral("cannot run svn"))
              && !panel.emptyText().contains(QStringLiteral("cannot run svn")),
          panel.headerText());
    check("vcs.svn.e2e.changed-row",
          panel.fileText(0).contains(QStringLiteral("a.sv")), panel.fileText(0));
    const auto writeDone = [&panel]() {
        return waitUntil([&panel]() { return !panel.writeInFlight(); }, 30000);
    };

    panel.selectFile("a.sv");
    check("vcs.svn.e2e.add-request", panel.stageSelected());
    writeDone();
    check("vcs.svn.e2e.add-ok",
          panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::SvnAdd, panel.messageText());
    check("vcs.svn.e2e.commit-request",
          panel.commit(QStringLiteral("svn e2e base"), QString(), false));
    writeDone();
    check("vcs.svn.e2e.commit-ok",
          panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::SvnCommit, panel.messageText());
    QString log;
    runProcess(svn, {"log", "-q", url}, temp.path(), &log);
    check("vcs.svn.e2e.revision-1", log.contains(QStringLiteral("r1")), log.trimmed());

    // A modification is visible through the SAME diff read the Git panel uses.
    writeText(wc + QStringLiteral("/a.sv"), QStringLiteral("module a;\n// svn change\nendmodule\n"));
    panel.refresh();
    waitUntil([&panel]() { return panel.fileCount() >= 1; }, 20000);
    panel.selectFile("a.sv");
    const bool diffShown = waitUntil(
        [&panel]() { return panel.diffText().contains(QStringLiteral("// svn change")); }, 20000);
    check("vcs.svn.e2e.diff", diffShown, panel.diffText().left(160));

    // Revert (the SVN spelling of Discard) restores the committed content.
    check("vcs.svn.e2e.revert-request", panel.discardSelected());
    writeDone();
    check("vcs.svn.e2e.revert-ok", panel.lastWriteOk(), panel.messageText());
    QFile reverted(wc + QStringLiteral("/a.sv"));
    QString revertedText;
    if (reverted.open(QIODevice::ReadOnly)) {
        revertedText = QString::fromUtf8(reverted.readAll());
    }
    check("vcs.svn.e2e.revert-restored",
          revertedText.contains(QStringLiteral("endmodule"))
              && !revertedText.contains(QStringLiteral("// svn change")),
          revertedText.trimmed());

    // Another working copy commits; Update (the SVN spelling of Pull) brings it in.
    writeText(wc2 + QStringLiteral("/b.sv"), QStringLiteral("module b;\nendmodule\n"));
    runProcess(svn, {"add", "b.sv"}, wc2);
    runProcess(svn, {"commit", "-q", "-m", "svn e2e second"}, wc2);
    check("vcs.svn.e2e.update-request", panel.pull());
    writeDone();
    check("vcs.svn.e2e.update-ok", panel.lastWriteOk(), panel.messageText());
    // An UPDATED file is CLEAN: it is not a change row (SVN and Git agree).
    // What proves the update is the file on disk and the working-copy revision
    // the header shows (r2 after the second working copy's commit).
    check("vcs.svn.e2e.update-file", QFileInfo(wc + QStringLiteral("/b.sv")).isFile(),
          wc + QStringLiteral("/b.sv"));
    panel.refresh();
    check("vcs.svn.e2e.update-row",
          waitUntil([&panel]() { return panel.headerText().contains(QStringLiteral("@ 2")); },
                    20000),
          panel.headerText());

    // Remove + Commit: the Subversion-only action, through the same seam.
    panel.selectFile("b.sv");
    check("vcs.svn.e2e.remove-request", panel.removeSelected());
    writeDone();
    check("vcs.svn.e2e.remove-ok",
          panel.lastWriteOk() && panel.lastWriteOp() == WriteOp::SvnRemove, panel.messageText());
    check("vcs.svn.e2e.remove-commit-request",
          panel.commit(QStringLiteral("svn e2e remove"), QString(), false));
    writeDone();
    check("vcs.svn.e2e.remove-committed",
          panel.lastWriteOk() && !QFileInfo(wc + QStringLiteral("/b.sv")).exists(),
          panel.messageText());
    log.clear();
    runProcess(svn, {"log", "-q", url}, temp.path(), &log);
    check("vcs.svn.e2e.revision-3", log.contains(QStringLiteral("r3")), log.trimmed());

    if (previousSvn.isEmpty()) {
        qunsetenv("VEDIT_SVN");
    } else {
        qputenv("VEDIT_SVN", previousSvn);
    }
}
#else
void checkSvnE2E()
{
    skip("vcs.svn.e2e.backend", "lane built without vd-ffi (stub-only build)");
}
#endif

void checkAutoRefresh()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        for (const char *name : {"vcs.refresh.fixture", "vcs.refresh.initial",
                                 "vcs.refresh.diff-before", "vcs.refresh.save-detected",
                                 "vcs.refresh.save-state-after", "vcs.refresh.new-file-detected",
                                 "vcs.refresh.activation-detected", "vcs.refresh.activation-reason",
                                 "vcs.refresh.write-detected", "vcs.refresh.write-reason",
                                 "vcs.refresh.coalesce-requests", "vcs.refresh.coalesce-merged",
                                 "vcs.refresh.coalesce-single", "vcs.refresh.coalesce-no-storm"}) {
            skip(name, "git is not on PATH");
        }
        return;
    }
    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/refresh-repo");
    QDir().mkpath(repo + QStringLiteral("/rtl"));
    const bool fixture =
        writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, repo)
        && runProcess(git, {"config", "user.email", "auto@example.com"}, repo)
        && runProcess(git, {"config", "user.name", "VEdit Auto"}, repo)
        && runProcess(git, {"add", "rtl/a.sv"}, repo)
        && runProcess(git, {"commit", "-m", "refresh fixture"}, repo)
        && writeText(repo + QStringLiteral("/rtl/a.sv"),
                     QStringLiteral("module a;\n// change-1\nendmodule\n"));
    check("vcs.refresh.fixture", temp.isValid() && fixture, repo);
    if (!fixture) {
        return;
    }

    VcsPanel panel;
    panel.setRoot(repo.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    const bool initialStatus =
        waitUntil([&]() { return panel.backend() == Backend::Git && panel.fileCount() == 1; }, 20000);
    check("vcs.refresh.initial", initialStatus,
          QStringLiteral("backend=%1 files=%2")
              .arg(static_cast<int>(panel.backend()))
              .arg(panel.fileCount()));
    panel.selectFile("rtl/a.sv");
    const bool diffBefore =
        waitUntil([&]() { return panel.diffText().contains(QStringLiteral("change-1")); }, 20000);
    check("vcs.refresh.diff-before", diffBefore, panel.diffText().left(60));

    // (1) saving a file the panel already watches (its status lists it).
    const int beforeSave = panel.refreshCount();
    check("vcs.refresh.save-written",
          writeText(repo + QStringLiteral("/rtl/a.sv"),
                    QStringLiteral("module a;\n// change-2\nendmodule\n")));
    const bool saveDetected = waitUntil([&]() { return panel.refreshCount() > beforeSave; }, 6000);
    check("vcs.refresh.save-detected", saveDetected,
          QStringLiteral("refreshes %1 -> %2 (%3)")
              .arg(beforeSave)
              .arg(panel.refreshCount())
              .arg(panel.lastRefreshReason()));
    const bool saveStateAfter =
        waitUntil([&]() { return panel.diffText().contains(QStringLiteral("change-2")); }, 20000);
    check("vcs.refresh.save-state-after", saveStateAfter, panel.diffText().left(60));

    // (2) a NEW file changes the watched directory listing.
    const int beforeNewFile = panel.refreshCount();
    check("vcs.refresh.new-file-written",
          writeText(repo + QStringLiteral("/rtl/b.sv"), QStringLiteral("module b;\nendmodule\n")));
    const bool newFileDetected = waitUntil(
        [&]() { return panel.fileCount() == 2 && panel.refreshCount() > beforeNewFile; }, 6000);
    check("vcs.refresh.new-file-detected", newFileDetected,
          QStringLiteral("files=%1 refreshes=%2").arg(panel.fileCount()).arg(panel.refreshCount()));

    // (3) the window regaining activation.
    pump(50);
    const int beforeActivation = panel.refreshCount();
    {
        QEvent activation(QEvent::WindowActivate);
        QCoreApplication::sendEvent(panel.window(), &activation);
    }
    const bool activationDetected =
        waitUntil([&]() { return panel.refreshCount() > beforeActivation; }, 6000);
    check("vcs.refresh.activation-detected", activationDetected,
          QStringLiteral("refreshes %1 -> %2").arg(beforeActivation).arg(panel.refreshCount()));
    check("vcs.refresh.activation-reason",
          panel.lastRefreshReason() == QStringLiteral("activated"), panel.lastRefreshReason());

    // (4) a finished write refreshes through the same coalescer.
    pump(50);
    panel.selectFile("rtl/a.sv");
    const int beforeWrite = panel.refreshCount();
    check("vcs.refresh.write-requested", panel.stageSelected());
    const bool writeCompleted = waitUntil([&]() { return !panel.writeInFlight(); }, 20000);
    check("vcs.refresh.write-completed", writeCompleted,
          panel.lastWriteOk() ? QStringLiteral("ok") : QStringLiteral("failed"));
    const bool writeDetected = waitUntil([&]() { return panel.refreshCount() > beforeWrite; }, 6000);
    check("vcs.refresh.write-detected", writeDetected,
          QStringLiteral("refreshes %1 -> %2").arg(beforeWrite).arg(panel.refreshCount()));
    check("vcs.refresh.write-reason", panel.lastRefreshReason() == QStringLiteral("write"),
          panel.lastRefreshReason());

    // (5) coalescing: five triggers inside one window -> ONE refresh.
    panel.flushRefreshForProbe();
    pump(60);
    const int requestsBefore = panel.refreshRequestCount();
    const int mergedBefore = panel.mergedRefreshCount();
    const int refreshesBefore = panel.refreshCount();
    for (int index = 0; index < 5; ++index) {
        panel.noteFileSaved(QStringLiteral("rtl/a.sv"));
    }
    check("vcs.refresh.coalesce-requests", panel.refreshRequestCount() - requestsBefore == 5,
          QStringLiteral("requests=%1").arg(panel.refreshRequestCount() - requestsBefore));
    check("vcs.refresh.coalesce-merged", panel.mergedRefreshCount() - mergedBefore == 4,
          QStringLiteral("merged=%1").arg(panel.mergedRefreshCount() - mergedBefore));
    check("vcs.refresh.coalesce-single",
          waitUntil([&]() { return panel.refreshCount() > refreshesBefore; }, 4000));
    pump(1300);
    check("vcs.refresh.coalesce-no-storm", panel.refreshCount() - refreshesBefore == 1,
          QStringLiteral("5 triggers -> %1 refresh(es) in %2 ms")
              .arg(panel.refreshCount() - refreshesBefore)
              .arg(panel.coalesceWindowMs()));
#else
    for (const char *name : {"vcs.refresh.fixture", "vcs.refresh.initial",
                             "vcs.refresh.diff-before", "vcs.refresh.save-detected",
                             "vcs.refresh.save-state-after", "vcs.refresh.new-file-detected",
                             "vcs.refresh.activation-detected", "vcs.refresh.activation-reason",
                             "vcs.refresh.write-detected", "vcs.refresh.write-reason",
                             "vcs.refresh.coalesce-requests", "vcs.refresh.coalesce-merged",
                             "vcs.refresh.coalesce-single", "vcs.refresh.coalesce-no-storm"}) {
        skip(name, "lane built without vd-ffi (stub-only build)");
    }
#endif
}

// AC-3.4: closing and reopening the panel must not crash — including while a
// real asynchronous job is still polling.
void checkCloseReopen()
{
    for (int round = 0; round < 3; ++round) {
        auto *panel = new VcsPanel();
        panel->setRoot("/tmp/repo-git");
        panel->setSource(std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs()));
        panel->resize(520, 360);
        panel->show();
        QCoreApplication::processEvents();
        panel->refresh();
        QCoreApplication::processEvents();
        panel->close();
        delete panel;
    }
    check("vcs.panel.close-reopen.no-crash", true,
          QStringLiteral("3 show/refresh/close/destroy cycles"));
#if defined(VEDIT_VCS_HAVE_FFI)
    {
        QTemporaryDir temp;
        auto *panel = new VcsPanel();
        panel->setRoot(temp.path().toStdString());
        panel->setSource(std::make_unique<FfiVcsSource>());
        panel->refresh();
        pump(20); // the job is in flight when the panel goes away
        delete panel;
        pump(100);
    }
    check("vcs.panel.close-reopen.pending-job", true,
          QStringLiteral("destroyed with a status job pending"));
#else
    skip("vcs.panel.close-reopen.pending-job", "lane built without vd-ffi (stub-only build)");
#endif
}

// ==========================================================================
// Batch 4a (AC-4.1 / AC-4.2 / AC-4.4 / AC-4.5)
// ==========================================================================

// The refs column and every batch-4a operation, driven through the recording
// stub: each check asserts the EXACT request the panel issued, so a wrong
// argument cannot hide behind a green result.
void checkRefsAndRefOperations()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });

    const QStringList refs = panel.refTexts();
    const QString joined = refs.join(QStringLiteral(" | "));
    check("vcs.refs.sections",
          refs.contains(QStringLiteral("Branches")) && refs.contains(QStringLiteral("Tags"))
              && refs.contains(QStringLiteral("Remote branches"))
              && refs.contains(QStringLiteral("Remotes")),
          joined);
    check("vcs.refs.current-branch-marked",
          refs.contains(QStringLiteral("* feature/vcs-panel")) && refs.contains(QStringLiteral("main")),
          joined);
    check("vcs.refs.tag-listed", refs.contains(QStringLiteral("v1.0")), joined);
    check("vcs.refs.remote-branch-listed", refs.contains(QStringLiteral("origin/main")), joined);
    check("vcs.refs.remote-url-listed",
          refs.contains(QStringLiteral("origin  https://example.com/vedit.git")), joined);
    check("vcs.refs.upstream-in-header",
          panel.headerText().contains(QStringLiteral("\u2192 origin/feature/vcs-panel")),
          panel.headerText());
    check("vcs.refs.row-count", panel.refRowCount() == 5,
          QStringLiteral("rows=%1").arg(panel.refRowCount()));
    check("vcs.refs.kind-codes",
          static_cast<int>(RefKind::Branch) == 1 && static_cast<int>(RefKind::Tag) == 2
              && static_cast<int>(RefKind::RemoteBranch) == 3
              && static_cast<int>(RefKind::Other) == 4,
          QStringLiteral("branch=%1 tag=%2 remote=%3")
              .arg(static_cast<int>(RefKind::Branch))
              .arg(static_cast<int>(RefKind::Tag))
              .arg(static_cast<int>(RefKind::RemoteBranch)));
    check("vcs.seam.network-op-set",
          writeOpIsNetwork(WriteOp::GitFetch) && writeOpIsNetwork(WriteOp::GitPull)
              && writeOpIsNetwork(WriteOp::GitPush) && !writeOpIsNetwork(WriteOp::GitCommit)
              && !writeOpIsNetwork(WriteOp::GitBranchCreate));
    check("vcs.seam.irreversible-branch-tag",
          writeOpIsIrreversible(WriteOp::GitBranchDelete)
              && writeOpIsIrreversible(WriteOp::GitTagDelete)
              && !writeOpIsIrreversible(WriteOp::GitBranchSwitch)
              && !writeOpIsIrreversible(WriteOp::GitRemoteRemove));

    const auto lastWrite = [raw]() -> const WriteRequest & { return raw->writes().back(); };
    const auto writes = [raw]() { return raw->writes().size(); };

    // --- fetch / pull / push ---------------------------------------------
    check("vcs.op.fetch-requested", panel.fetch());
    check("vcs.op.fetch-request",
          writes() == 1 && lastWrite().op == WriteOp::GitFetch && lastWrite().target == "origin",
          QStringLiteral("op=%1 target=%2")
              .arg(static_cast<int>(lastWrite().op))
              .arg(QString::fromStdString(lastWrite().target)));

    check("vcs.op.pull-requested", panel.pull());
    check("vcs.op.pull-uses-the-configured-upstream",
          writes() == 2 && lastWrite().op == WriteOp::GitPull && lastWrite().name.empty()
              && lastWrite().target.empty(),
          QStringLiteral("op=%1 name=%2 target=%3")
              .arg(static_cast<int>(lastWrite().op))
              .arg(QString::fromStdString(lastWrite().name))
              .arg(QString::fromStdString(lastWrite().target)));

    check("vcs.op.push-requested", panel.push());
    check("vcs.op.push-request",
          writes() == 3 && lastWrite().op == WriteOp::GitPush
              && lastWrite().target == "origin" && lastWrite().name == "feature/vcs-panel"
              && !lastWrite().set_upstream,
          QStringLiteral("op=%1 target=%2 name=%3 up=%4")
              .arg(static_cast<int>(lastWrite().op))
              .arg(QString::fromStdString(lastWrite().target))
              .arg(QString::fromStdString(lastWrite().name))
              .arg(lastWrite().set_upstream));

    // A branch with no upstream publishes with --set-upstream on its first push.
    {
        Info noUpstream = gitInfo();
        noUpstream.upstream.clear();
        raw->setInfo(noUpstream);
        panel.refresh();
        check("vcs.op.push-first-publishes", panel.push());
        check("vcs.op.push-first-sets-upstream",
              writes() == 4 && lastWrite().op == WriteOp::GitPush && lastWrite().set_upstream
                  && lastWrite().target == "origin",
              QStringLiteral("up=%1 target=%2")
                  .arg(lastWrite().set_upstream)
                  .arg(QString::fromStdString(lastWrite().target)));
        // No remote configured and no upstream: the panel refuses rather than
        // issuing `git push <branch>`, which git would read as a REMOTE name.
        Info noRemote = gitInfo();
        noRemote.upstream.clear();
        noRemote.remotes.clear();
        raw->setInfo(noRemote);
        panel.refresh();
        const std::size_t beforeNoRemote = writes();
        check("vcs.op.push-no-remote-refused", !panel.push());
        check("vcs.op.push-no-remote-zero-write", writes() == beforeNoRemote,
              QStringLiteral("writes=%1").arg(writes()));
        raw->setInfo(gitInfo());
        panel.refresh();
    }

    // --- branches ---------------------------------------------------------
    check("vcs.op.branch-create-requested", panel.createBranch("feature/new", "main"));
    check("vcs.op.branch-create-request",
          writes() == 5 && lastWrite().op == WriteOp::GitBranchCreate
              && lastWrite().name == "feature/new" && lastWrite().target == "main",
          QStringLiteral("name=%1 start=%2").arg(QString::fromStdString(lastWrite().name),
                                                 QString::fromStdString(lastWrite().target)));

    check("vcs.op.branch-create-switch-requested", panel.createBranch("feature/chain", {}, true));
    check("vcs.op.branch-create-switch-is-a-chain",
          writes() == 7 && raw->writes()[5].op == WriteOp::GitBranchCreate
              && raw->writes()[6].op == WriteOp::GitBranchSwitch
              && raw->writes()[6].name == "feature/chain",
          QStringLiteral("writes=%1 last-op=%2")
              .arg(writes())
              .arg(static_cast<int>(lastWrite().op)));

    check("vcs.op.branch-switch-requested", panel.switchBranch("main"));
    check("vcs.op.branch-switch-request",
          writes() == 8 && lastWrite().op == WriteOp::GitBranchSwitch && lastWrite().name == "main",
          QString::fromStdString(lastWrite().name));

    check("vcs.op.branch-rename-requested", panel.renameBranch("old-name", "new-name"));
    check("vcs.op.branch-rename-request",
          writes() == 9 && lastWrite().op == WriteOp::GitBranchRename
              && lastWrite().name == "old-name" && lastWrite().target == "new-name",
          QStringLiteral("%1 -> %2").arg(QString::fromStdString(lastWrite().name),
                                         QString::fromStdString(lastWrite().target)));

    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.op.branch-delete-declined", !panel.deleteBranch("main"));
    check("vcs.op.branch-delete-declined-zero-write", writes() == 9,
          QStringLiteral("writes=%1").arg(writes()));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.op.branch-delete-requested", panel.deleteBranch("main"));
    check("vcs.op.branch-delete-request",
          writes() == 10 && lastWrite().op == WriteOp::GitBranchDelete && lastWrite().name == "main"
              && lastWrite().paths.empty(),
          QString::fromStdString(lastWrite().name));

    // --- tags -------------------------------------------------------------
    check("vcs.op.tag-create-requested", panel.createTag("v2.0", "abc1234"));
    check("vcs.op.tag-create-request",
          writes() == 11 && lastWrite().op == WriteOp::GitTagCreate && lastWrite().name == "v2.0"
              && lastWrite().target == "abc1234",
          QStringLiteral("name=%1 target=%2").arg(QString::fromStdString(lastWrite().name),
                                                  QString::fromStdString(lastWrite().target)));
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.op.tag-delete-declined", !panel.deleteTag("v1.0"));
    check("vcs.op.tag-delete-declined-zero-write", writes() == 11,
          QStringLiteral("writes=%1").arg(writes()));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.op.tag-delete-requested", panel.deleteTag("v1.0"));
    check("vcs.op.tag-delete-request",
          writes() == 12 && lastWrite().op == WriteOp::GitTagDelete && lastWrite().name == "v1.0",
          QString::fromStdString(lastWrite().name));

    check("vcs.op.checkout-requested", panel.checkoutRef("v1.0"));
    check("vcs.op.checkout-request",
          writes() == 13 && lastWrite().op == WriteOp::GitCheckout && lastWrite().target == "v1.0",
          QString::fromStdString(lastWrite().target));

    // --- remotes / upstream ----------------------------------------------
    check("vcs.op.remote-add-requested", panel.addRemote("upstream", "https://example.com/upstream.git"));
    check("vcs.op.remote-add-request",
          writes() == 14 && lastWrite().op == WriteOp::GitRemoteAdd
              && lastWrite().name == "upstream" && lastWrite().url == "https://example.com/upstream.git",
          QStringLiteral("name=%1 url=%2").arg(QString::fromStdString(lastWrite().name),
                                               QString::fromStdString(lastWrite().url)));
    check("vcs.op.remote-set-url-requested", panel.setRemoteUrl("origin", "https://example.com/moved.git"));
    check("vcs.op.remote-set-url-request",
          writes() == 15 && lastWrite().op == WriteOp::GitRemoteSetUrl
              && lastWrite().name == "origin" && lastWrite().url == "https://example.com/moved.git",
          QString::fromStdString(lastWrite().url));
    check("vcs.op.set-upstream-requested", panel.setUpstream("feature/vcs-panel", "origin/feature/vcs-panel"));
    check("vcs.op.set-upstream-request",
          writes() == 16 && lastWrite().op == WriteOp::GitSetUpstream
              && lastWrite().name == "feature/vcs-panel"
              && lastWrite().target == "origin/feature/vcs-panel",
          QStringLiteral("name=%1 target=%2").arg(QString::fromStdString(lastWrite().name),
                                                  QString::fromStdString(lastWrite().target)));

    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.op.remote-remove-declined", !panel.removeRemote("upstream"));
    check("vcs.op.remote-remove-declined-zero-write", writes() == 16,
          QStringLiteral("writes=%1").arg(writes()));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.op.remote-remove-requested", panel.removeRemote("upstream"));
    check("vcs.op.remote-remove-request",
          writes() == 17 && lastWrite().op == WriteOp::GitRemoteRemove && lastWrite().name == "upstream",
          QString::fromStdString(lastWrite().name));

    // --- rejected arguments never become a request ------------------------
    check("vcs.op.branch-create-empty-refused", !panel.createBranch("   "));
    check("vcs.op.tag-create-empty-refused", !panel.createTag(QString()));
    check("vcs.op.remote-add-empty-refused", !panel.addRemote("origin", QString()));
    check("vcs.op.invalid-args-zero-write", writes() == 17,
          QStringLiteral("writes=%1").arg(writes()));

    // --- credentials (AC-4.5): retry once with the entered secret ----------
    {
        Info secretUrl = gitInfo();
        secretUrl.remotes = {RemoteEntry{"origin", "https://alice:tok-9f8e7d@example.com/vedit.git"}};
        raw->setInfo(secretUrl);
        panel.refresh();
        raw->setWriteOutcome(false, "fatal: could not read Username for 'https://example.com': "
                                    "terminal prompts disabled");
        raw->setWriteStderr("fatal: could not read Username for 'https://example.com': terminal prompts disabled");
        int prompts = 0;
        panel.setCredentialProvider([&prompts](const QString &prompt, QString &user, QString &password) {
            ++prompts;
            check("vcs.cred.prompt-is-sanitized",
                  prompt.contains(QStringLiteral("https://example.com/vedit.git"))
                      && !prompt.contains(QStringLiteral("tok-9f8e7d")),
                  prompt);
            user = QStringLiteral("alice");
            password = QStringLiteral("stub-secret-123");
            return true;
        });
        const std::size_t beforeRetry = writes();
        check("vcs.cred.push-issued", panel.push());
        check("vcs.cred.prompted-once", prompts == 1 && panel.credentialPromptCount() == 1,
              QStringLiteral("prompts=%1").arg(panel.credentialPromptCount()));
        check("vcs.cred.retried-with-credentials",
              writes() == beforeRetry + 2 && lastWrite().op == WriteOp::GitPush
                  && lastWrite().credential_user == "alice"
                  && lastWrite().credential_password == "stub-secret-123",
              QStringLiteral("writes=%1 user=%2").arg(writes()).arg(
                  QString::fromStdString(lastWrite().credential_user)));
        check("vcs.cred.secret-never-rendered",
              !panel.messageText().contains(QStringLiteral("stub-secret-123"))
                  && !panel.errorText().contains(QStringLiteral("stub-secret-123"))
                  && !panel.lastCredentialPrompt().contains(QStringLiteral("stub-secret-123")),
              panel.errorText().left(120));
        check("vcs.cred.retry-only-once",
              writes() == beforeRetry + 2 && prompts == 1 && panel.credentialPromptCount() == 1,
              QStringLiteral("writes=%1 prompts=%2").arg(writes()).arg(prompts));

        // Cancel aborts the operation and reports git's real reason.
        panel.setConfirmProvider([](const QString &, const QString &) { return true; });
        panel.setCredentialProvider([&prompts](const QString &, QString &, QString &) {
            ++prompts;
            return false;
        });
        const std::size_t beforeCancel = writes();
        check("vcs.cred.cancel-push-issued", panel.push());
        check("vcs.cred.cancel-no-retry", writes() == beforeCancel + 1,
              QStringLiteral("writes=%1").arg(writes()));
        check("vcs.cred.cancel-reports-real-reason",
              panel.errorText().contains(QStringLiteral("terminal prompts disabled")),
              panel.errorText().left(120));
        check("vcs.cred.cancel-says-aborted",
              panel.messageText().contains(QStringLiteral("cancelled")),
              panel.messageText());

        // Batch 7 (NIT from the 4a review): the DEFAULT headless behaviour must be
        // asserted — with the probe seam NOT enabled, no dialog is driven and the
        // operation is refused with no retry.
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            panel.setCredentialProvider({});
            raw->setWriteOutcome(false,
                                 "fatal: Authentication failed for 'https://example.com/vedit.git'");
            raw->setWriteStderr(
                "fatal: Authentication failed for 'https://example.com/vedit.git'");
            const std::size_t beforeHeadless = writes();
            const int headlessPrompts = panel.credentialPromptCount();
            const int headlessModals = panel.modalCredentialDialogs();
            check("vcs.cred.headless-push-issued", panel.push());
            check("vcs.cred.headless-default-refusal",
                  panel.credentialPromptCount() == headlessPrompts + 1,
                  QStringLiteral("prompts=%1").arg(panel.credentialPromptCount()));
            check("vcs.cred.headless-no-modal",
                  panel.modalCredentialDialogs() == headlessModals,
                  QStringLiteral("modals=%1").arg(panel.modalCredentialDialogs()));
            check("vcs.cred.headless-no-retry", writes() == beforeHeadless + 1,
                  QStringLiteral("writes=%1").arg(writes()));
            check("vcs.cred.headless-says-aborted",
                  panel.messageText().contains(QStringLiteral("nothing was written"))
                      || panel.messageText().contains(QStringLiteral("cancelled")),
                  panel.messageText());
        } else {
            skip("vcs.cred.headless-push-issued", "not the offscreen platform");
            skip("vcs.cred.headless-default-refusal", "not the offscreen platform");
            skip("vcs.cred.headless-no-modal", "not the offscreen platform");
            skip("vcs.cred.headless-no-retry", "not the offscreen platform");
            skip("vcs.cred.headless-says-aborted", "not the offscreen platform");
        }

        // A non-authentication failure never asks for credentials.
        const int promptBefore = panel.credentialPromptCount();
        raw->setWriteOutcome(false, "fatal: not a git repository (or any of the parent directories)");
        raw->setWriteStderr("fatal: not a git repository (or any of the parent directories)");
        check("vcs.cred.non-auth-failure-issued", panel.push());
        check("vcs.cred.non-auth-failure-no-prompt",
              panel.credentialPromptCount() == promptBefore,
              QStringLiteral("prompts=%1").arg(panel.credentialPromptCount()));
        check("vcs.cred.classifier",
              writeResultNeedsCredentials(WriteResult{WriteOp::GitPush, false, {},
                                                      "fatal: Authentication failed for 'https://x'", {}})
                  && !writeResultNeedsCredentials(WriteResult{WriteOp::GitPush, false, {},
                                                              "fatal: not a git repository", {}})
                  && !writeResultNeedsCredentials(WriteResult{WriteOp::GitPush, true, {}, "terminal prompts disabled", {}}));
        raw->setInfo(gitInfo());
        panel.refresh();
    }
}

#if defined(VEDIT_VCS_HAVE_FFI)
namespace {

// A minimal HTTP server that answers EVERY request with 401 + a Basic
// challenge. That is exactly the situation "no credential helper and no SSH"
// creates for a network operation: git asks the transport, the transport asks
// for credentials, and with GIT_TERMINAL_PROMPT=0 git fails with
// "could not read Username …: terminal prompts disabled". POSIX sockets keep the
// lane independent of a Qt network module.
struct FakeAuthServer {
    int listen_fd = -1;
    int port = 0;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::atomic<int> requests{0};
    std::atomic<int> authorized{0};
    std::mutex mutex;
    std::string last_authorization;

    bool start()
    {
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            return false;
        }
        int one = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            ::close(listen_fd);
            listen_fd = -1;
            return false;
        }
        socklen_t length = sizeof(address);
        if (::getsockname(listen_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0
            || ::listen(listen_fd, 8) != 0) {
            ::close(listen_fd);
            listen_fd = -1;
            return false;
        }
        port = ntohs(address.sin_port);
        worker = std::thread([this]() { serve(); });
        return true;
    }

    void serve()
    {
        while (!stopping.load()) {
            pollfd descriptor{};
            descriptor.fd = listen_fd;
            descriptor.events = POLLIN;
            const int ready = ::poll(&descriptor, 1, 100);
            if (ready <= 0) {
                continue;
            }
            const int client = ::accept(listen_fd, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            std::string request;
            char buffer[4096];
            while (request.find("\r\n\r\n") == std::string::npos) {
                const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
                if (got <= 0) {
                    break;
                }
                request.append(buffer, static_cast<std::size_t>(got));
            }
            ++requests;
            std::string lowered = request;
            for (char &character : lowered) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            const std::size_t header = lowered.find("authorization:");
            if (header != std::string::npos) {
                const std::size_t start = request.find_first_not_of(' ', header + 14);
                std::size_t end = request.find("\r\n", start);
                if (end == std::string::npos) {
                    end = request.size();
                }
                if (start != std::string::npos && end > start) {
                    ++authorized;
                    std::lock_guard<std::mutex> lock(mutex);
                    last_authorization = request.substr(start, end - start);
                }
            }
            static const char kResponse[] =
                "HTTP/1.1 401 Unauthorized\r\n"
                "WWW-Authenticate: Basic realm=\"vedit-e2e\"\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n\r\n";
            ::send(client, kResponse, sizeof(kResponse) - 1, 0);
            ::close(client);
        }
    }

    std::string lastAuthorization()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return last_authorization;
    }

    void shutdown()
    {
        stopping.store(true);
        if (worker.joinable()) {
            worker.join();
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
            listen_fd = -1;
        }
    }
};

std::string base64Encode(const std::string &value)
{
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t index = 0;
    while (index + 2 < value.size()) {
        const auto triple = (static_cast<unsigned>(static_cast<unsigned char>(value[index])) << 16)
            | (static_cast<unsigned>(static_cast<unsigned char>(value[index + 1])) << 8)
            | static_cast<unsigned>(static_cast<unsigned char>(value[index + 2]));
        out.push_back(kTable[(triple >> 18) & 0x3Fu]);
        out.push_back(kTable[(triple >> 12) & 0x3Fu]);
        out.push_back(kTable[(triple >> 6) & 0x3Fu]);
        out.push_back(kTable[triple & 0x3Fu]);
        index += 3;
    }
    const std::size_t rest = value.size() - index;
    if (rest == 1) {
        const auto chunk = static_cast<unsigned>(static_cast<unsigned char>(value[index])) << 16;
        out.push_back(kTable[(chunk >> 18) & 0x3Fu]);
        out.push_back(kTable[(chunk >> 12) & 0x3Fu]);
        out += "==";
    } else if (rest == 2) {
        const auto chunk = (static_cast<unsigned>(static_cast<unsigned char>(value[index])) << 16)
            | (static_cast<unsigned>(static_cast<unsigned char>(value[index + 1])) << 8);
        out.push_back(kTable[(chunk >> 18) & 0x3Fu]);
        out.push_back(kTable[(chunk >> 12) & 0x3Fu]);
        out.push_back(kTable[(chunk >> 6) & 0x3Fu]);
        out.push_back('=');
    }
    return out;
}

// True when any regular file below `root` (hidden files included) contains
// `needle`. Used for the "credentials never reach the disk" assertion.
bool directoryContainsText(const QString &root, const QByteArray &needle)
{
    QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (file.readAll().contains(needle)) {
            return true;
        }
    }
    return false;
}

int countLinesContaining(const QString &text, const QString &needle)
{
    int count = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.contains(needle)) {
            ++count;
        }
    }
    return count;
}

} // namespace
#endif

// AC-4.1 / AC-4.2 / AC-4.4 / AC-4.5 on REAL repositories: every operation runs
// through the panel into a REAL bare repository under QTemporaryDir, and every
// claim is asserted with `git for-each-ref`, `git log`, `git remote`, the
// working-tree files and a real 401 transport for the credential path.
void checkGitRemoteE2E()
{
#if defined(VEDIT_VCS_HAVE_FFI)
    const QStringList names = {
        "vcs.remote.e2e.git",
        "vcs.remote.e2e.fixture",
        "vcs.remote.e2e.not-worktree",
        "vcs.remote.e2e.info-ready",
        "vcs.remote.e2e.remote-add-requested",
        "vcs.remote.e2e.remote-add-reported",
        "vcs.remote.e2e.remote-reported",
        "vcs.remote.e2e.branch-known",
        "vcs.remote.e2e.push-requested",
        "vcs.remote.e2e.push-ok",
        "vcs.remote.e2e.push-landed-in-bare",
        "vcs.remote.e2e.upstream-established",
        "vcs.remote.e2e.push2-ok",
        "vcs.remote.e2e.push2-landed",
        "vcs.remote.e2e.clone",
        "vcs.remote.e2e.pull-ok",
        "vcs.remote.e2e.pull-moved-worktree",
        "vcs.remote.e2e.pull-file-content",
        "vcs.remote.e2e.fetch-ok",
        "vcs.remote.e2e.fetch-updated-tracking",
        "vcs.remote.e2e.fetch-left-worktree",
        "vcs.remote.e2e.pull-diverged-fails",
        "vcs.remote.e2e.pull-diverged-reason",
        "vcs.remote.e2e.recovery-requested",
        "vcs.remote.e2e.recovered-after-failure",
        "vcs.remote.e2e.branch-create-ok",
        "vcs.remote.e2e.branch-created",
        "vcs.remote.e2e.branch-switched",
        "vcs.remote.e2e.branch-rename-ok",
        "vcs.remote.e2e.branch-renamed",
        "vcs.remote.e2e.branch-switch-back",
        "vcs.remote.e2e.branch-delete-declined",
        "vcs.remote.e2e.branch-still-present",
        "vcs.remote.e2e.branch-delete-requested",
        "vcs.remote.e2e.branch-deleted",
        "vcs.remote.e2e.tag-create-ok",
        "vcs.remote.e2e.tag-created",
        "vcs.remote.e2e.tag-delete-declined",
        "vcs.remote.e2e.tag-still-present",
        "vcs.remote.e2e.tag-delete-requested",
        "vcs.remote.e2e.tag-deleted",
        "vcs.remote.e2e.detached-tag-created",
        "vcs.remote.e2e.checkout-detached",
        "vcs.remote.e2e.checkout-detached-head",
        "vcs.remote.e2e.checkout-back",
        "vcs.remote.e2e.refs-match-for-each-ref",
        "vcs.remote.e2e.current-ref-flagged",
        "vcs.remote.e2e.remote-set-url-ok",
        "vcs.remote.e2e.remote-url-changed",
        "vcs.remote.e2e.remote-set-url-back",
        "vcs.remote.e2e.unreachable-set-url",
        "vcs.remote.e2e.unreachable-fetch-issued",
        "vcs.remote.e2e.unreachable-fails",
        "vcs.remote.e2e.unreachable-recovered",
        "vcs.remote.e2e.unreachable-recovered-fetch",
        "vcs.remote.e2e.upstream-set-ok",
        "vcs.remote.e2e.upstream-set",
        "vcs.remote.e2e.remote-remove-declined",
        "vcs.remote.e2e.remote-still-configured",
        "vcs.remote.e2e.remote-remove-requested",
        "vcs.remote.e2e.remote-removed",
        "vcs.cred.e2e.server",
        "vcs.cred.e2e.remote-add-ok",
        "vcs.cred.e2e.remote-read",
        "vcs.cred.e2e.prompt-content",
        "vcs.cred.e2e.push-requested",
        "vcs.cred.e2e.prompted",
        "vcs.cred.e2e.transport-received-credentials",
        "vcs.cred.e2e.op-fails-honestly",
        "vcs.cred.e2e.secret-not-in-ui",
        "vcs.cred.e2e.secret-not-in-argv",
        "vcs.cred.e2e.helper-not-in-argv-value",
        "vcs.cred.e2e.cancel-push-requested",
        "vcs.cred.e2e.cancel-prompted-once",
        "vcs.cred.e2e.cancel-no-authorization",
        "vcs.cred.e2e.cancel-real-reason",
        "vcs.cred.e2e.cancel-says-aborted",
        "vcs.cred.e2e.modal-fixture",
        "vcs.cred.e2e.modal-push-requested",
        "vcs.cred.e2e.modal-dialog-opened",
        "vcs.cred.e2e.modal-exactly-one-authorization",
        "vcs.cred.e2e.modal-secret-cleared",
        "vcs.cred.e2e.modal-secret-not-in-ui",
        "vcs.cred.e2e.modal-dialog-destroyed",
        "vcs.cred.e2e.secret-not-on-disk",
    };
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        for (const QString &name : names) {
            skip(name.toUtf8().constData(), "git is not on PATH");
        }
        return;
    }
    check("vcs.remote.e2e.git", true, git);

    QTemporaryDir temp;
    const QString base = temp.path();
    const QString bare = base + QStringLiteral("/origin.git");
    const QString work = base + QStringLiteral("/work");
    QDir().mkpath(work + QStringLiteral("/rtl"));
    const bool fixture =
        temp.isValid() && QDir().mkpath(base)
        && writeText(work + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, work)
        && runProcess(git, {"config", "user.email", "remote-e2e@example.com"}, work)
        && runProcess(git, {"config", "user.name", "VEdit Remote E2E"}, work)
        && runProcess(git, {"add", "rtl/a.sv"}, work)
        && runProcess(git, {"commit", "-q", "-m", "fixture base"}, work)
        && runProcess(git, {"init", "--bare", "-q", bare}, base);
    check("vcs.remote.e2e.fixture", fixture, base);
    if (!fixture) {
        return;
    }
    check("vcs.remote.e2e.not-worktree",
          QFileInfo(work).canonicalFilePath() != QFileInfo(QDir::currentPath()).canonicalFilePath()
              && work.startsWith(QDir::tempPath()),
          QStringLiteral("fixture=%1 cwd=%2").arg(work, QDir::currentPath()));

    VcsPanel panel;
    panel.setRoot(work.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    const auto writeDone = [](VcsPanel &target) {
        return waitUntil([&target]() { return !target.writeInFlight(); }, 40000);
    };
    const auto refList = [&git](const QString &repo, const QString &pattern) {
        QString output;
        runProcess(git, {"for-each-ref", "--format=%(refname:short)", pattern}, repo, &output);
        return output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    };
    const auto headOf = [&git](const QString &repo, const QString &ref) {
        QString output;
        runProcess(git, {"rev-parse", ref}, repo, &output);
        return output.trimmed();
    };

    const bool infoReady = waitUntil([&panel]() { return !panel.refs().empty(); }, 40000);
    check("vcs.remote.e2e.info-ready", infoReady,
          QStringLiteral("refs=%1").arg(panel.refs().size()));

    // --- remote add -------------------------------------------------------
    check("vcs.remote.e2e.remote-add-requested", panel.addRemote("origin", bare));
    writeDone(panel);
    check("vcs.remote.e2e.remote-add-reported", panel.lastWriteOk(),
          panel.errorText().left(160));
    QString remotesText;
    runProcess(git, {"remote", "-v"}, work, &remotesText);
    check("vcs.remote.e2e.remote-add-reported", panel.lastWriteOk(), panel.errorText().left(160));
    panel.refresh();
    const bool remotesRead = waitUntil([&panel]() { return !panel.remotes().empty(); }, 40000);
    check("vcs.remote.e2e.remote-reported",
          remotesText.contains(bare) && remotesRead
              && panel.remotes().size() == 1
              && QString::fromStdString(panel.remotes().front().name) == QStringLiteral("origin"),
          remotesText.trimmed());

    QString branch;
    runProcess(git, {"symbolic-ref", "--short", "HEAD"}, work, &branch);
    branch = branch.trimmed();
    check("vcs.remote.e2e.branch-known", !branch.isEmpty(), branch);

    // --- push (first push publishes and tracks the branch) ----------------
    check("vcs.remote.e2e.push-requested", panel.push());
    writeDone(panel);
    check("vcs.remote.e2e.push-ok", panel.lastWriteOk(), panel.errorText().left(160));
    QString bareLog;
    runProcess(git, {"log", "--format=%s", branch}, bare, &bareLog);
    check("vcs.remote.e2e.push-landed-in-bare", panel.lastWriteOk() && bareLog.contains(QStringLiteral("fixture base")),
          bareLog.trimmed());
    panel.refresh();
    const bool upstreamRead = waitUntil([&panel]() { return !panel.upstream().isEmpty(); }, 40000);
    check("vcs.remote.e2e.upstream-established",
          upstreamRead && panel.upstream() == QStringLiteral("origin/") + branch, panel.upstream());

    // --- second commit, second push --------------------------------------
    writeText(work + QStringLiteral("/rtl/b.sv"), QStringLiteral("module b;\nendmodule\n"));
    runProcess(git, {"add", "rtl/b.sv"}, work);
    runProcess(git, {"commit", "-q", "-m", "second commit"}, work);
    check("vcs.remote.e2e.push2-ok", panel.push());
    writeDone(panel);
    runProcess(git, {"log", "--format=%s", branch}, bare, &bareLog);
    check("vcs.remote.e2e.push2-landed",
          panel.lastWriteOk() && bareLog.contains(QStringLiteral("second commit")),
          panel.errorText().left(160) + QStringLiteral(" | ") + bareLog.trimmed());

    // --- pull a real fast-forward ----------------------------------------
    const QString work2 = base + QStringLiteral("/work2");
    const bool cloned = runProcess(git, {"clone", "-q", bare, work2}, base)
        && runProcess(git, {"config", "user.email", "remote-e2e@example.com"}, work2)
        && runProcess(git, {"config", "user.name", "VEdit Remote E2E"}, work2);
    check("vcs.remote.e2e.clone", cloned, work2);
    if (!cloned) {
        return;
    }
    writeText(work2 + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\n// from work2\nendmodule\n"));
    runProcess(git, {"commit", "-q", "-am", "work2 commit"}, work2);
    const QString work2Head = headOf(work2, "HEAD");
    runProcess(git, {"push", "-q", "origin", branch}, work2);

    check("vcs.remote.e2e.pull-ok", panel.pull());
    writeDone(panel);
    const QString workHeadAfterPull = headOf(work, "HEAD");
    check("vcs.remote.e2e.pull-moved-worktree",
          panel.lastWriteOk() && !work2Head.isEmpty() && workHeadAfterPull == work2Head,
          QStringLiteral("work=%1 work2=%2").arg(workHeadAfterPull, work2Head));
    QFile pulledFile(work + QStringLiteral("/rtl/a.sv"));
    const bool pulledOpened = pulledFile.open(QIODevice::ReadOnly);
    check("vcs.remote.e2e.pull-file-content",
          pulledOpened && pulledFile.readAll().contains("from work2"),
          pulledOpened ? QStringLiteral("file updated") : QStringLiteral("cannot read the file"));

    // --- fetch moves the remote-tracking ref, not the working tree --------
    writeText(work2 + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\n// from work2 third\nendmodule\n"));
    runProcess(git, {"commit", "-q", "-am", "work2 commit 3"}, work2);
    const QString work2Head2 = headOf(work2, "HEAD");
    runProcess(git, {"push", "-q", "origin", branch}, work2);
    const QString workHeadBeforeFetch = headOf(work, "HEAD");
    check("vcs.remote.e2e.fetch-ok", panel.fetch());
    writeDone(panel);
    const QString trackingHead = headOf(work, QStringLiteral("origin/") + branch);
    check("vcs.remote.e2e.fetch-updated-tracking",
          panel.lastWriteOk() && trackingHead == work2Head2,
          QStringLiteral("tracking=%1 work2=%2").arg(trackingHead, work2Head2));
    check("vcs.remote.e2e.fetch-left-worktree", headOf(work, "HEAD") == workHeadBeforeFetch,
          headOf(work, "HEAD"));

    // --- a non-fast-forward pull fails with git's reason, no crash --------
    writeText(work + QStringLiteral("/rtl/c.sv"), QStringLiteral("module c;\nendmodule\n"));
    runProcess(git, {"add", "rtl/c.sv"}, work);
    runProcess(git, {"commit", "-q", "-m", "local only"}, work);
    writeText(work2 + QStringLiteral("/rtl/a.sv"),
              QStringLiteral("module a;\n// work2 fourth\nendmodule\n"));
    runProcess(git, {"commit", "-q", "-am", "work2 commit 4"}, work2);
    runProcess(git, {"push", "-q", "origin", branch}, work2);
    check("vcs.remote.e2e.pull-diverged-fails", panel.pull());
    writeDone(panel);
    check("vcs.remote.e2e.pull-diverged-reason",
          !panel.lastWriteOk() && panel.errorStateVisible() && !panel.errorText().isEmpty(),
          panel.errorText().left(160));
    // The panel recovered: a fetch on the diverged tree still works.
    check("vcs.remote.e2e.recovery-requested", panel.fetch());
    writeDone(panel);
    check("vcs.remote.e2e.recovered-after-failure", panel.lastWriteOk(), panel.errorText().left(160));
    runProcess(git, {"reset", "--hard", QStringLiteral("origin/") + branch}, work);

    // --- branches: create / switch / rename / delete ----------------------
    check("vcs.remote.e2e.branch-create-ok", panel.createBranch("feature/e2e", branch));
    writeDone(panel);
    QStringList heads = refList(work, "refs/heads");
    check("vcs.remote.e2e.branch-created",
          panel.lastWriteOk() && heads.contains(QStringLiteral("feature/e2e")),
          heads.join(QStringLiteral(",")));
    check("vcs.remote.e2e.branch-switched", panel.switchBranch("feature/e2e"));
    writeDone(panel);
    QString headBranch;
    runProcess(git, {"symbolic-ref", "--short", "HEAD"}, work, &headBranch);
    check("vcs.remote.e2e.branch-switched",
          panel.lastWriteOk() && headBranch.trimmed() == QStringLiteral("feature/e2e"),
          headBranch.trimmed());
    check("vcs.remote.e2e.branch-rename-ok", panel.renameBranch("feature/e2e", "feature/renamed"));
    writeDone(panel);
    heads = refList(work, "refs/heads");
    check("vcs.remote.e2e.branch-renamed",
          panel.lastWriteOk() && heads.contains(QStringLiteral("feature/renamed"))
              && !heads.contains(QStringLiteral("feature/e2e")),
          heads.join(QStringLiteral(",")));
    // Switch back to the base branch and delete the renamed one.
    check("vcs.remote.e2e.branch-switch-back", panel.switchBranch(branch));
    writeDone(panel);
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.remote.e2e.branch-delete-declined", !panel.deleteBranch("feature/renamed"));
    heads = refList(work, "refs/heads");
    check("vcs.remote.e2e.branch-still-present",
          heads.contains(QStringLiteral("feature/renamed")), heads.join(QStringLiteral(",")));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.remote.e2e.branch-delete-requested", panel.deleteBranch("feature/renamed"));
    writeDone(panel);
    heads = refList(work, "refs/heads");
    check("vcs.remote.e2e.branch-deleted",
          panel.lastWriteOk() && !heads.contains(QStringLiteral("feature/renamed")),
          heads.join(QStringLiteral(",")));

    // --- tags: create / delete (with confirmation) ------------------------
    check("vcs.remote.e2e.tag-create-ok", panel.createTag("v-e2e", "HEAD"));
    writeDone(panel);
    QStringList tags = refList(work, "refs/tags");
    check("vcs.remote.e2e.tag-created",
          panel.lastWriteOk() && tags.contains(QStringLiteral("v-e2e")),
          tags.join(QStringLiteral(",")));
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.remote.e2e.tag-delete-declined", !panel.deleteTag("v-e2e"));
    check("vcs.remote.e2e.tag-still-present", refList(work, "refs/tags").contains(QStringLiteral("v-e2e")),
          refList(work, "refs/tags").join(QStringLiteral(",")));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.remote.e2e.tag-delete-requested", panel.deleteTag("v-e2e"));
    writeDone(panel);
    tags = refList(work, "refs/tags");
    check("vcs.remote.e2e.tag-deleted",
          panel.lastWriteOk() && !tags.contains(QStringLiteral("v-e2e")),
          tags.join(QStringLiteral(",")));

    // --- checkout a ref (detached HEAD) -----------------------------------
    check("vcs.remote.e2e.detached-tag-created", panel.createTag("v-detached", "HEAD"));
    writeDone(panel);
    check("vcs.remote.e2e.checkout-detached", panel.checkoutRef("v-detached"));
    writeDone(panel);
    QString detached;
    runProcess(git, {"rev-parse", "--abbrev-ref", "HEAD"}, work, &detached);
    check("vcs.remote.e2e.checkout-detached-head",
          panel.lastWriteOk() && detached.trimmed() == QStringLiteral("HEAD"), detached.trimmed());
    check("vcs.remote.e2e.checkout-back", panel.switchBranch(branch));
    writeDone(panel);

    // --- the refs column matches `git for-each-ref` -----------------------
    panel.refresh();
    const bool refsRead = waitUntil(
        [&panel]() {
            for (const RefEntry &ref : panel.refs()) {
                if (ref.kind == RefKind::Tag && ref.name == "v-detached") {
                    return true;
                }
            }
            return false;
        },
        40000);
    QStringList panelBranches;
    QStringList panelTags;
    QStringList currentRefs;
    for (const RefEntry &ref : panel.refs()) {
        if (ref.kind == RefKind::Branch) {
            panelBranches << QString::fromStdString(ref.name);
        } else if (ref.kind == RefKind::Tag) {
            panelTags << QString::fromStdString(ref.name);
        }
        if (ref.current) {
            currentRefs << QString::fromStdString(ref.name);
        }
    }
    panelBranches.sort();
    panelTags.sort();
    QStringList realBranches = refList(work, "refs/heads");
    QStringList realTags = refList(work, "refs/tags");
    realBranches.sort();
    realTags.sort();
    check("vcs.remote.e2e.refs-match-for-each-ref",
          refsRead && panelBranches == realBranches && panelTags == realTags,
          QStringLiteral("panel=%1|%2 git=%3|%4")
              .arg(panelBranches.join(QStringLiteral(",")), panelTags.join(QStringLiteral(",")),
                   realBranches.join(QStringLiteral(",")), realTags.join(QStringLiteral(","))));
    check("vcs.remote.e2e.current-ref-flagged",
          currentRefs == QStringList{branch},
          currentRefs.join(QStringLiteral(",")));

    // --- remote set-url / remove -----------------------------------------
    const QString otherUrl = base + QStringLiteral("/other-origin.git");
    check("vcs.remote.e2e.remote-set-url-ok", panel.setRemoteUrl("origin", otherUrl));
    writeDone(panel);
    QString configuredUrl;
    runProcess(git, {"remote", "get-url", "origin"}, work, &configuredUrl);
    check("vcs.remote.e2e.remote-url-changed",
          panel.lastWriteOk() && configuredUrl.trimmed() == otherUrl, configuredUrl.trimmed());
    check("vcs.remote.e2e.remote-set-url-back", panel.setRemoteUrl("origin", bare));
    writeDone(panel);

    // --- an unreachable remote reports git's real reason, no crash --------
    const QString missingUrl = base + QStringLiteral("/missing-origin.git");
    check("vcs.remote.e2e.unreachable-set-url", panel.setRemoteUrl("origin", missingUrl));
    writeDone(panel);
    check("vcs.remote.e2e.unreachable-fetch-issued", panel.fetch());
    writeDone(panel);
    check("vcs.remote.e2e.unreachable-fails",
          !panel.lastWriteOk() && panel.errorStateVisible()
              && (panel.errorText().contains(QStringLiteral("does not appear to be a git repository"))
                  || panel.errorText().contains(QStringLiteral("Could not read from remote repository"))),
          panel.errorText().left(200));
    check("vcs.remote.e2e.unreachable-recovered", panel.setRemoteUrl("origin", bare));
    writeDone(panel);
    check("vcs.remote.e2e.unreachable-recovered-fetch", panel.lastWriteOk(), panel.errorText().left(160));
    // set the upstream explicitly, then read it back with git
    check("vcs.remote.e2e.upstream-set-ok", panel.setUpstream(branch, QStringLiteral("origin/") + branch));
    writeDone(panel);
    QString upstreamRef;
    runProcess(git, {"rev-parse", "--abbrev-ref", branch + QStringLiteral("@{u}")}, work, &upstreamRef);
    check("vcs.remote.e2e.upstream-set",
          panel.lastWriteOk() && upstreamRef.trimmed() == QStringLiteral("origin/") + branch,
          upstreamRef.trimmed());
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.remote.e2e.remote-remove-declined", !panel.removeRemote("origin"));
    QString remotesAfterDecline;
    runProcess(git, {"remote"}, work, &remotesAfterDecline);
    check("vcs.remote.e2e.remote-still-configured",
          remotesAfterDecline.contains(QStringLiteral("origin")), remotesAfterDecline.trimmed());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.remote.e2e.remote-remove-requested", panel.removeRemote("origin"));
    writeDone(panel);
    QString remotesAfter;
    runProcess(git, {"remote"}, work, &remotesAfter);
    check("vcs.remote.e2e.remote-removed",
          panel.lastWriteOk() && remotesAfter.trimmed().isEmpty(), remotesAfter.trimmed());

    // ======================================================================
    // Credential path (AC-4.5) against a REAL transport that answers 401.
    // ======================================================================
    const QString creds = base + QStringLiteral("/creds");
    QDir().mkpath(creds + QStringLiteral("/rtl"));
    const bool credFixture =
        writeText(creds + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, creds)
        && runProcess(git, {"config", "user.email", "remote-e2e@example.com"}, creds)
        && runProcess(git, {"config", "user.name", "VEdit Remote E2E"}, creds)
        && runProcess(git, {"add", "rtl/a.sv"}, creds)
        && runProcess(git, {"commit", "-q", "-m", "credential fixture"}, creds);
    FakeAuthServer server;
    const bool serverUp = credFixture && server.start();
    check("vcs.cred.e2e.server", serverUp, QStringLiteral("port=%1").arg(server.port));
    if (!serverUp) {
        server.shutdown();
        return;
    }
    const QString remoteUrl = QStringLiteral("http://127.0.0.1:%1/repo.git").arg(server.port);
    VcsPanel credPanel;
    credPanel.setRoot(creds.toStdString());
    credPanel.setSource(std::make_unique<FfiVcsSource>());
    credPanel.setConfirmProvider([](const QString &, const QString &) { return true; });
    waitUntil([&credPanel]() { return !credPanel.refs().empty(); }, 40000);
    check("vcs.cred.e2e.remote-add-ok", credPanel.addRemote("origin", remoteUrl));
    const auto credDone = [](VcsPanel &target) {
        return waitUntil([&target]() { return !target.writeInFlight(); }, 40000);
    };
    credDone(credPanel);
    // The push takes the remote from the info read: wait for that read, otherwise
    // the request would carry no remote and git would never reach the transport.
    credPanel.refresh();
    const bool credRemoteRead = waitUntil([&credPanel]() { return !credPanel.remotes().empty(); }, 40000);
    check("vcs.cred.e2e.remote-read", credRemoteRead,
          QStringLiteral("remotes=%1").arg(credPanel.remotes().size()));

    // A `git` wrapper on PATH records every argv the backend spawns, so the
    // "the secret is never in argv" claim is observed rather than assumed.
    const QString shimDir = base + QStringLiteral("/shim");
    const QString argvLog = base + QStringLiteral("/git-argv.log");
    QDir().mkpath(shimDir);
    QFile::remove(argvLog);
    const bool shimWritten = writeText(
        shimDir + QStringLiteral("/git"),
        QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$VD_ARGV_LOG\"\nexec %1 \"$@\"\n").arg(git));
    QFile::setPermissions(shimDir + QStringLiteral("/git"),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QByteArray savedPath = qgetenv("PATH");
    qputenv("VD_ARGV_LOG", argvLog.toUtf8());
    if (shimWritten) {
        qputenv("PATH", (shimDir + QLatin1Char(':') + QString::fromLocal8Bit(savedPath)).toUtf8());
    }

    const QString password = QStringLiteral("e2e-secret-4f2a9c");
    const QString user = QStringLiteral("e2e-user");
    int prompts = 0;
    credPanel.setCredentialProvider([&prompts, &password, &user](const QString &prompt, QString &userOut,
                                                                 QString &passwordOut) {
        ++prompts;
        check("vcs.cred.e2e.prompt-content",
              prompt.contains(QStringLiteral("127.0.0.1")) && !prompt.contains(password), prompt);
        userOut = user;
        passwordOut = password;
        return true;
    });
    check("vcs.cred.e2e.push-requested", credPanel.push());
    credDone(credPanel);
    check("vcs.cred.e2e.prompted",
          prompts == 1 && credPanel.credentialPromptCount() == 1,
          QStringLiteral("prompts=%1").arg(credPanel.credentialPromptCount()));
    const QString expectedAuthorization =
        QStringLiteral("Basic ") + QString::fromLatin1(base64Encode((user + QLatin1Char(':') + password).toStdString()).c_str());
    check("vcs.cred.e2e.transport-received-credentials",
          server.authorized.load() >= 1
              && QString::fromStdString(server.lastAuthorization()) == expectedAuthorization,
          QStringLiteral("authorized=%1 requests=%2")
              .arg(server.authorized.load())
              .arg(server.requests.load()));
    check("vcs.cred.e2e.op-fails-honestly",
          !credPanel.lastWriteOk() && !credPanel.errorText().isEmpty(),
          credPanel.errorText().left(160));
    check("vcs.cred.e2e.secret-not-in-ui",
          !credPanel.errorText().contains(password) && !credPanel.messageText().contains(password)
              && !credPanel.lastCredentialPrompt().contains(password),
          credPanel.errorText().left(160));

    QString argvText;
    {
        QFile argvFile(argvLog);
        if (argvFile.open(QIODevice::ReadOnly)) {
            argvText = QString::fromLocal8Bit(argvFile.readAll());
        }
    }
    check("vcs.cred.e2e.secret-not-in-argv",
          shimWritten && argvText.contains(QStringLiteral("push")) && !argvText.contains(password),
          QStringLiteral("lines=%1").arg(argvText.count(QLatin1Char('\n'))));
    check("vcs.cred.e2e.helper-not-in-argv-value",
          !argvText.contains(password) && argvText.contains(QStringLiteral("credential.helper")),
          QStringLiteral("the retry configured the helper without carrying the secret"));

    // --- cancel: no retry, no credentials on the wire, real reason ---------
    const int pushLinesBefore = countLinesContaining(argvText, QStringLiteral("push"));
    const int authorizedBefore = server.authorized.load();
    VcsPanel cancelPanel;
    cancelPanel.setRoot(creds.toStdString());
    cancelPanel.setSource(std::make_unique<FfiVcsSource>());
    cancelPanel.setConfirmProvider([](const QString &, const QString &) { return true; });
    waitUntil([&cancelPanel]() { return !cancelPanel.refs().empty(); }, 40000);
    cancelPanel.refresh();
    waitUntil([&cancelPanel]() { return !cancelPanel.remotes().empty(); }, 40000);
    int cancelPrompts = 0;
    cancelPanel.setCredentialProvider([&cancelPrompts](const QString &, QString &, QString &) {
        ++cancelPrompts;
        return false;
    });
    check("vcs.cred.e2e.cancel-push-requested", cancelPanel.push());
    credDone(cancelPanel);
    {
        QFile argvFile(argvLog);
        argvText.clear();
        if (argvFile.open(QIODevice::ReadOnly)) {
            argvText = QString::fromLocal8Bit(argvFile.readAll());
        }
    }
    check("vcs.cred.e2e.cancel-prompted-once",
          cancelPrompts == 1 && cancelPanel.credentialPromptCount() == 1,
          QStringLiteral("prompts=%1").arg(cancelPanel.credentialPromptCount()));
    check("vcs.cred.e2e.cancel-no-authorization",
          server.authorized.load() == authorizedBefore
              && countLinesContaining(argvText, QStringLiteral("push")) == pushLinesBefore + 1,
          QStringLiteral("authorized=%1 push-lines=%2")
              .arg(server.authorized.load())
              .arg(countLinesContaining(argvText, QStringLiteral("push")) - pushLinesBefore));
    check("vcs.cred.e2e.cancel-real-reason",
          cancelPanel.errorText().contains(QStringLiteral("terminal prompts disabled"))
              || cancelPanel.errorText().contains(QStringLiteral("could not read Username")),
          cancelPanel.errorText().left(200));
    check("vcs.cred.e2e.cancel-says-aborted",
          cancelPanel.messageText().contains(QStringLiteral("cancelled")),
          cancelPanel.messageText());

    // --- the REAL modal dialog on the wire: exactly one Authorization ------
    // The same 401 transport, answered by the real CredentialDialog driven from a
    // QTimer: the dialog opens with a masked password field, the retry carries the
    // credentials, and the transport sees EXACTLY ONE authorized request.
    {
        const QString creds2 = base + QStringLiteral("/creds2");
        QDir().mkpath(creds2 + QStringLiteral("/rtl"));
        const bool fixture2 =
            writeText(creds2 + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))
            && runProcess(git, {"init", "-q"}, creds2)
            && runProcess(git, {"config", "user.email", "remote-e2e@example.com"}, creds2)
            && runProcess(git, {"config", "user.name", "VEdit Remote E2E"}, creds2)
            && runProcess(git, {"add", "rtl/a.sv"}, creds2)
            && runProcess(git, {"commit", "-q", "-m", "modal credential fixture"}, creds2);
        check("vcs.cred.e2e.modal-fixture", fixture2, creds2);
        const int authorizedBeforeModal = server.authorized.load();
        if (fixture2) {
            VcsPanel modalPanel;
            modalPanel.setRoot(creds2.toStdString());
            modalPanel.setSource(std::make_unique<FfiVcsSource>());
            modalPanel.setModalCredentialDialogForProbe(true);
            waitUntil([&modalPanel]() { return !modalPanel.refs().empty(); }, 40000);
            modalPanel.addRemote("origin", remoteUrl);
            credDone(modalPanel);
            modalPanel.refresh();
            waitUntil([&modalPanel]() { return !modalPanel.remotes().empty(); }, 40000);
            bool opened = false;
            bool masked = false;
            ModalDriver driver(QStringLiteral("vcs-credential-dialog"), [&](QDialog *dialog) {
                opened = dialog->isVisible();
                auto *passwordField =
                    dialog->findChild<QLineEdit *>(QStringLiteral("vcs-credential-password"));
                auto *userField = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-credential-user"));
                masked = passwordField != nullptr && passwordField->echoMode() == QLineEdit::Password;
                if (userField != nullptr) {
                    userField->setText(user);
                }
                if (passwordField != nullptr) {
                    passwordField->setText(password);
                }
                dialog->accept();
            });
            check("vcs.cred.e2e.modal-push-requested", modalPanel.push());
            credDone(modalPanel);
            check("vcs.cred.e2e.modal-dialog-opened", opened && masked && driver.driven() == 1,
                  QStringLiteral("driven=%1 ticks=%2").arg(driver.driven()).arg(driver.ticks()));
            check("vcs.cred.e2e.modal-exactly-one-authorization",
                  server.authorized.load() == authorizedBeforeModal + 1,
                  QStringLiteral("authorized %1 -> %2")
                      .arg(authorizedBeforeModal)
                      .arg(server.authorized.load()));
            check("vcs.cred.e2e.modal-secret-cleared", modalPanel.credentialSecretCleared());
            check("vcs.cred.e2e.modal-secret-not-in-ui",
                  !modalPanel.errorText().contains(password) && !modalPanel.messageText().contains(password),
                  modalPanel.errorText().left(160));
            check("vcs.cred.e2e.modal-dialog-destroyed", driver.dialog() == nullptr);
        }
    }

    // --- the secret never reached the disk --------------------------------
    check("vcs.cred.e2e.secret-not-on-disk",
          !directoryContainsText(base, password.toUtf8())
              && !directoryContainsText(creds + QStringLiteral("/.git"), password.toUtf8()),
          QStringLiteral("scanned %1").arg(base));
    qputenv("PATH", savedPath);
    qunsetenv("VD_ARGV_LOG");
    server.shutdown();
#else
    for (const char *name : {"vcs.remote.e2e.fixture", "vcs.cred.e2e.server"}) {
        skip(name, "lane built without vd-ffi (stub-only build)");
    }
#endif
}

// ==========================================================================
// Batch 4a repair (F1 / F2 / F3): the REAL modal dialogs, driven headlessly
// ==========================================================================

using vedit::workspace::vcs::BranchDialog;
using vedit::workspace::vcs::CredentialDialog;
using vedit::workspace::vcs::RemoteDialog;
using vedit::workspace::vcs::TagDialog;

// F2: the credential dialog is not only a provider seam — the real QDialog is
// opened, its password field is a masked field, and cancel really aborts.
// F3: the branch/tag/remote dialogs prefill from the selection, accept into the
// documented request, reject without a write, and the panel refuses invalid
// names before a job exists.
void checkModalDialogs()
{
    const QString authFailure =
        "fatal: could not read Username for 'https://example.com': terminal prompts disabled";
    const QString secretMod = QStringLiteral("modal-secret-987");

    // --- F2, accept: the real modal credential dialog drives a retry ---------
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        auto *raw = stub.get();
        VcsPanel panel;
        panel.setRoot("/tmp/repo-git");
        panel.setSource(std::move(stub));
        panel.setModalCredentialDialogForProbe(true);
        raw->setWriteOutcome(false, authFailure.toStdString());
        raw->setWriteStderr(authFailure.toStdString());

        bool opened = false;
        bool credentialDialog = false;
        bool passwordMasked = false;
        bool hasPromptLabel = false;
        ModalDriver driver(QStringLiteral("vcs-credential-dialog"), [&](QDialog *dialog) {
            opened = dialog->isVisible();
            credentialDialog = qobject_cast<CredentialDialog *>(dialog) != nullptr;
            auto *password = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-credential-password"));
            auto *user = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-credential-user"));
            passwordMasked = password != nullptr && password->echoMode() == QLineEdit::Password;
            hasPromptLabel = dialog->findChild<QLabel *>(QStringLiteral("vcs-credential-prompt")) != nullptr;
            if (user != nullptr) {
                user->setText(QStringLiteral("modal-user"));
            }
            if (password != nullptr) {
                password->setText(secretMod);
            }
            dialog->accept();
        });
        const std::size_t before = raw->writes().size();
        check("vcs.modal.credential-push-issued", panel.push());
        check("vcs.modal.credential-dialog-opened", opened && credentialDialog && !driver.timedOut(),
              QStringLiteral("driven=%1 ticks=%2").arg(driver.driven()).arg(driver.ticks()));
        check("vcs.modal.credential-prompt-shown", hasPromptLabel);
        check("vcs.modal.credential-password-masked", passwordMasked);
        check("vcs.modal.credential-retried",
              raw->writes().size() == before + 2 && raw->writes().back().op == WriteOp::GitPush
                  && raw->writes().back().credential_user == "modal-user"
                  && raw->writes().back().credential_password == secretMod.toStdString(),
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        check("vcs.modal.credential-dialog-destroyed", driver.dialog() == nullptr);
        check("vcs.modal.credential-secret-cleared", panel.credentialSecretCleared());
        check("vcs.modal.credential-counted",
              panel.modalCredentialDialogs() == 1 && panel.modalDialogCount() == 1,
              QStringLiteral("credential=%1 dialogs=%2")
                  .arg(panel.modalCredentialDialogs())
                  .arg(panel.modalDialogCount()));
        check("vcs.modal.credential-secret-not-in-ui",
              !panel.messageText().contains(secretMod) && !panel.errorText().contains(secretMod));
    }

    // --- F2, cancel: the dialog answers nothing, the operation aborts --------
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        auto *raw = stub.get();
        VcsPanel panel;
        panel.setRoot("/tmp/repo-git");
        panel.setSource(std::move(stub));
        panel.setModalCredentialDialogForProbe(true);
        raw->setWriteOutcome(false, authFailure.toStdString());
        raw->setWriteStderr(authFailure.toStdString());

        bool opened = false;
        ModalDriver driver(QStringLiteral("vcs-credential-dialog"), [&](QDialog *dialog) {
            opened = dialog->isVisible();
            dialog->reject();
        });
        const std::size_t before = raw->writes().size();
        check("vcs.modal.credential-cancel-issued", panel.push());
        check("vcs.modal.credential-cancel-dialog-opened", opened && driver.driven() == 1);
        check("vcs.modal.credential-cancel-no-retry", raw->writes().size() == before + 1,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        check("vcs.modal.credential-cancel-real-reason",
              panel.errorText().contains(QStringLiteral("terminal prompts disabled")),
              panel.errorText().left(140));
        check("vcs.modal.credential-cancel-says-aborted",
              panel.messageText().contains(QStringLiteral("cancelled")), panel.messageText());
        check("vcs.modal.credential-cancel-no-secret-retained", panel.credentialSecretCleared());
        check("vcs.modal.credential-cancel-dialog-destroyed", driver.dialog() == nullptr);
        check("vcs.modal.credential-cancel-secret-not-in-ui",
              !panel.messageText().contains(secretMod) && !panel.errorText().contains(secretMod));
    }

    // --- F3: branch / tag / remote dialogs ----------------------------------
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        auto *raw = stub.get();
        VcsPanel panel;
        panel.setRoot("/tmp/repo-git");
        panel.setSource(std::move(stub));
        check("vcs.dialog.select-ref", panel.selectRef(QStringLiteral("main")));
        check("vcs.dialog.selected-ref", panel.selectedRefName() == QStringLiteral("main")
                                            && panel.selectedRefKind() == RefKind::Branch,
              panel.selectedRefName());

        // branch: prefill from the selection, cancel writes nothing
        QString branchStart;
        ModalDriver branchCancel(QStringLiteral("vcs-branch-dialog"), [&](QDialog *dialog) {
            auto *start = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-branch-start"));
            branchStart = start != nullptr ? start->text() : QString();
            dialog->reject();
        });
        const std::size_t afterRefs = raw->writes().size();
        panel.promptBranch();
        check("vcs.dialog.branch-prefill-from-selection", branchStart == QStringLiteral("main"), branchStart);
        check("vcs.dialog.branch-cancel-zero-write", raw->writes().size() == afterRefs,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        check("vcs.dialog.branch-cancel-message",
              panel.messageText().contains(QStringLiteral("cancelled")), panel.messageText());

        // branch: accept creates it from the prefilled start point (and switches)
        ModalDriver branchAccept(QStringLiteral("vcs-branch-dialog"), [&](QDialog *dialog) {
            auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-branch-name"));
            if (name != nullptr) {
                name->setText(QStringLiteral("feature/from-dialog"));
            }
            dialog->accept();
        });
        const std::size_t beforeAccept = raw->writes().size();
        panel.promptBranch();
        check("vcs.dialog.branch-accept-request",
              raw->writes().size() == beforeAccept + 2
                  && raw->writes()[beforeAccept].op == WriteOp::GitBranchCreate
                  && raw->writes()[beforeAccept].name == "feature/from-dialog"
                  && raw->writes()[beforeAccept].target == "main"
                  && raw->writes()[beforeAccept + 1].op == WriteOp::GitBranchSwitch,
              QStringLiteral("writes=%1").arg(raw->writes().size()));

        // tag: prefill the target from the selection, cancel writes nothing
        check("vcs.dialog.select-tag", panel.selectRef(QStringLiteral("v1.0")));
        QString tagTarget;
        ModalDriver tagCancel(QStringLiteral("vcs-tag-dialog"), [&](QDialog *dialog) {
            auto *target = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-tag-target"));
            tagTarget = target != nullptr ? target->text() : QString();
            dialog->reject();
        });
        const std::size_t beforeTag = raw->writes().size();
        panel.promptTag();
        check("vcs.dialog.tag-prefill-from-selection", tagTarget == QStringLiteral("v1.0"), tagTarget);
        check("vcs.dialog.tag-cancel-zero-write", raw->writes().size() == beforeTag,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        // tag: accept creates it at the prefilled target
        ModalDriver tagAccept(QStringLiteral("vcs-tag-dialog"), [&](QDialog *dialog) {
            auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-tag-name"));
            if (name != nullptr) {
                name->setText(QStringLiteral("v-from-dialog"));
            }
            dialog->accept();
        });
        const std::size_t beforeTagAccept = raw->writes().size();
        panel.promptTag();
        check("vcs.dialog.tag-accept-request",
              raw->writes().size() == beforeTagAccept + 1
                  && raw->writes().back().op == WriteOp::GitTagCreate
                  && raw->writes().back().name == "v-from-dialog"
                  && raw->writes().back().target == "v1.0",
              QStringLiteral("writes=%1").arg(raw->writes().size()));

        // remote add: accept into the documented request
        ModalDriver remoteAdd(QStringLiteral("vcs-remote-dialog"), [&](QDialog *dialog) {
            auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-remote-name"));
            auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-remote-url"));
            if (name != nullptr) {
                name->setText(QStringLiteral("backup"));
            }
            if (url != nullptr) {
                url->setText(QStringLiteral("https://example.com/backup.git"));
            }
            dialog->accept();
        });
        const std::size_t beforeRemoteAdd = raw->writes().size();
        panel.promptRemote();
        check("vcs.dialog.remote-add-request",
              raw->writes().size() == beforeRemoteAdd + 1
                  && raw->writes().back().op == WriteOp::GitRemoteAdd
                  && raw->writes().back().name == "backup"
                  && raw->writes().back().url == "https://example.com/backup.git");

        // remote edit: prefilled name + URL, cancel writes nothing
        QString editName;
        QString editUrl;
        ModalDriver remoteEditCancel(QStringLiteral("vcs-remote-url-dialog"), [&](QDialog *dialog) {
            auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-remote-name"));
            auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-remote-url"));
            editName = name != nullptr ? name->text() : QString();
            editUrl = url != nullptr ? url->text() : QString();
            dialog->reject();
        });
        const std::size_t beforeEdit = raw->writes().size();
        const bool editOpened = panel.editRemote(QStringLiteral("origin"));
        check("vcs.dialog.edit-remote-opened", remoteEditCancel.driven() == 1 && !editOpened,
              QStringLiteral("driven=%1 returned=%2")
                  .arg(remoteEditCancel.driven())
                  .arg(editOpened ? "true" : "false"));
        check("vcs.dialog.edit-remote-prefill",
              editName == QStringLiteral("origin") && editUrl == QStringLiteral("https://example.com/vedit.git"),
              editName + QStringLiteral(" ") + editUrl);
        check("vcs.dialog.edit-remote-cancel-zero-write", raw->writes().size() == beforeEdit,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        // remote edit: accept sends the new URL
        ModalDriver remoteEditAccept(QStringLiteral("vcs-remote-url-dialog"), [&](QDialog *dialog) {
            auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("vcs-remote-url"));
            if (url != nullptr) {
                url->setText(QStringLiteral("https://example.com/moved.git"));
            }
            dialog->accept();
        });
        const std::size_t beforeEditAccept = raw->writes().size();
        check("vcs.dialog.edit-remote-accept", panel.editRemote(QStringLiteral("origin")));
        check("vcs.dialog.edit-remote-request",
              raw->writes().size() == beforeEditAccept + 1
                  && raw->writes().back().op == WriteOp::GitRemoteSetUrl
                  && raw->writes().back().name == "origin"
                  && raw->writes().back().url == "https://example.com/moved.git");

        // every modal dialog was counted, and the tree selection is probe-readable
        check("vcs.dialog.modal-count", panel.modalDialogCount() == 7,
              QStringLiteral("dialogs=%1").arg(panel.modalDialogCount()));

        // --- invalid names are refused BEFORE a job exists ------------------
        const std::size_t guard = raw->writes().size();
        check("vcs.dialog.reject-empty-branch", !panel.createBranch(QString()));
        check("vcs.dialog.reject-space-branch", !panel.createBranch(QStringLiteral("bad name")));
        check("vcs.dialog.reject-dash-branch", !panel.createBranch(QStringLiteral("-bad")));
        check("vcs.dialog.reject-dotdot-tag", !panel.createTag(QStringLiteral("../escape")));
        check("vcs.dialog.reject-slash-branch", !panel.switchBranch(QStringLiteral("feature/")));
        check("vcs.dialog.reject-lock-branch", !panel.deleteBranch(QStringLiteral("main.lock")));
        check("vcs.dialog.reject-bad-rename", !panel.renameBranch(QStringLiteral("ok"), QStringLiteral("two words")));
        check("vcs.dialog.reject-bad-upstream", !panel.setUpstream(QStringLiteral("main"), QStringLiteral("bad upstream")));
        check("vcs.dialog.reject-bad-url", !panel.addRemote(QStringLiteral("backup"), QStringLiteral("has space")));
        check("vcs.dialog.reject-dash-url", !panel.setRemoteUrl(QStringLiteral("origin"), QStringLiteral("-flag")));
        check("vcs.dialog.validation-zero-writes", raw->writes().size() == guard,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        check("vcs.dialog.validation-reason",
              panel.messageText().contains(QStringLiteral("Invalid")), panel.messageText());
    }
}


// ==========================================================================
// Batch 4b (AC-4.3): commit list / detail / file history / blame / compare
// ==========================================================================

namespace {

CommitEntry cannedCommit(int index, bool current = false)
{
    CommitEntry entry;
    entry.hash = QStringLiteral("%1%2").arg(QStringLiteral("0123456789abcdef0123456789abcdef0123456"),
                                            QString::number(index))
                     .toStdString()
                     .substr(0, 40);
    entry.shortHash = entry.hash.substr(0, 7);
    entry.author = "Alice";
    entry.email = "alice@example.com";
    entry.time = 1700000000 + index;
    entry.subject = QStringLiteral("commit %1").arg(index).toStdString();
    (void)current;
    return entry;
}

HistoryData cannedCommitPage(int count, int offset = 0, bool hasMore = false)
{
    HistoryData data;
    data.kind = HistoryKind::Commits;
    data.backend = Backend::Git;
    data.offset = offset;
    data.hasMore = hasMore;
    for (int index = 0; index < count; ++index) {
        data.commits.push_back(cannedCommit(index));
    }
    return data;
}

HistoryData cannedCommitDetail()
{
    HistoryData data;
    data.kind = HistoryKind::Commit;
    data.backend = Backend::Git;
    data.hash = "0123456789abcdef0123456789abcdef01234567";
    data.author = "Alice";
    data.email = "alice@example.com";
    data.subject = "add the ALU";
    data.body = "The body explains the change.";
    data.time = 1700000000;
    data.files = {FileStat{"rtl/top.sv", 3, 1}};
    data.patch = "diff --git a/rtl/top.sv b/rtl/top.sv\n+module top;\n";
    return data;
}

HistoryData cannedFileHistory()
{
    HistoryData data;
    data.kind = HistoryKind::FileHistory;
    data.backend = Backend::Git;
    data.path = "rtl/top.sv";
    for (int index = 0; index < 3; ++index) {
        data.commits.push_back(cannedCommit(index));
    }
    data.hasMore = true;
    return data;
}

HistoryData cannedBlame()
{
    HistoryData data;
    data.kind = HistoryKind::Blame;
    data.backend = Backend::Git;
    data.path = "rtl/top.sv";
    data.blame = {BlameLine{1, "aaaa", "aaaaaaa", "Alice", 1700000000, "module top;"},
                  BlameLine{2, "bbbb", "bbbbbbb", "Bob", 1700000500, "endmodule"}};
    return data;
}

HistoryData cannedCompare()
{
    HistoryData data;
    data.kind = HistoryKind::Compare;
    data.backend = Backend::Git;
    data.from = "aaaa";
    data.to = "bbbb";
    data.files = {FileStat{"rtl/top.sv", 2, 0}};
    data.patch = "diff --git a/rtl/top.sv b/rtl/top.sv\n+// added\n";
    return data;
}

} // namespace

void checkHistoryStub()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    auto *raw = stub.get();
    raw->setHistoryData(HistoryKind::Commits, cannedCommitPage(3, 0, true));
    raw->setHistoryData(HistoryKind::Commit, cannedCommitDetail());
    raw->setHistoryData(HistoryKind::FileHistory, cannedFileHistory());
    raw->setHistoryData(HistoryKind::Blame, cannedBlame());
    raw->setHistoryData(HistoryKind::Compare, cannedCompare());
    VcsHistoryPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));

    // --- the commit list --------------------------------------------------
    check("vcs.history.rows", panel.commitCount() == 3,
          QStringLiteral("rows=%1").arg(panel.commitCount()));
    check("vcs.history.row-columns",
          panel.commitText(0, 0) == QStringLiteral("0123456")
              && panel.commitText(0, 1).contains(QStringLiteral("2023"))
              && panel.commitText(0, 2) == QStringLiteral("Alice")
              && panel.commitText(0, 3) == QStringLiteral("commit 0"),
          QStringLiteral("%1 | %2 | %3").arg(panel.commitText(0, 0), panel.commitText(0, 2),
                                             panel.commitText(0, 3)));
    check("vcs.history.hashes-in-data",
          panel.commitHashes().size() == 3 && panel.commitHashes().front().size() == 40,
          panel.commitHashes().join(QStringLiteral(",")));
    check("vcs.history.page-label", panel.pageLabel().contains(QStringLiteral("1")),
          panel.pageLabel());
    check("vcs.history.request-kind",
          panel.lastRequest().kind == HistoryKind::Commits && panel.lastRequest().limit == 50,
          QString::fromStdString(vedit::workspace::vcs::historyKindName(panel.lastRequest().kind)));
    check("vcs.history.state-ready",
          !panel.emptyStateVisible() && !panel.errorStateVisible() && !panel.loadingStateVisible());

    // --- paging -----------------------------------------------------------
    raw->setHistoryData(HistoryKind::Commits, cannedCommitPage(2, 150, false));
    panel.loadPage(150);
    check("vcs.history.next-page-offset", panel.lastRequest().offset == 150,
          QStringLiteral("offset=%1").arg(panel.lastRequest().offset));
    check("vcs.history.next-page-rows", panel.commitCount() == 2);
    check("vcs.history.last-page-no-more", !panel.hasMore());
    raw->setHistoryData(HistoryKind::Commits, cannedCommitPage(2, 50, true));
    panel.loadPage(50);
    panel.nextPage();
    check("vcs.history.next-page-uses-offset", panel.lastRequest().offset == 100,
          QStringLiteral("offset=%1").arg(panel.lastRequest().offset));
    panel.previousPage();
    check("vcs.history.previous-page-uses-offset", panel.lastRequest().offset == 50,
          QStringLiteral("offset=%1").arg(panel.lastRequest().offset));
    check("vcs.history.previous-never-negative", panel.offset() >= 0);

    // --- search -----------------------------------------------------------
    panel.search(QStringLiteral("alu carry"));
    check("vcs.history.search-request",
          panel.lastRequest().kind == HistoryKind::Commits && panel.lastRequest().query == "alu carry"
              && panel.lastRequest().offset == 0,
          QString::fromStdString(panel.lastRequest().query));
    check("vcs.history.search-box-kept", panel.searchText() == QStringLiteral("alu carry"));
    panel.search(QString());
    check("vcs.history.search-cleared", panel.lastRequest().query.empty());

    // --- selection loads the detail ---------------------------------------
    panel.selectCommit(panel.commitHashes().value(0));
    check("vcs.history.detail-request",
          panel.lastRequest().kind == HistoryKind::Commit
              && panel.lastRequest().hash.size() == 40,
          QString::fromStdString(panel.lastRequest().hash));
    check("vcs.history.detail-text",
          panel.detailText().contains(QStringLiteral("add the ALU"))
              && panel.detailText().contains(QStringLiteral("The body explains the change."))
              && panel.detailText().contains(QStringLiteral("rtl/top.sv"))
              && panel.detailText().contains(QStringLiteral("+module top;")),
          panel.detailText().left(120));
    check("vcs.history.tabs",
          panel.tabTitles()
              == QStringList{QStringLiteral("Commit"), QStringLiteral("File history"),
                             QStringLiteral("Blame"), QStringLiteral("Compare")},
          panel.tabTitles().join(QStringLiteral(",")));

    // --- file history -----------------------------------------------------
    panel.openFileHistory(QStringLiteral("rtl/top.sv"));
    check("vcs.history.file-request",
          panel.lastRequest().kind == HistoryKind::FileHistory
              && panel.lastRequest().path == "rtl/top.sv",
          QString::fromStdString(panel.lastRequest().path));
    check("vcs.history.file-text",
          panel.fileHistoryText().contains(QStringLiteral("History of rtl/top.sv"))
              && panel.fileHistoryText().contains(QStringLiteral("commit 0")),
          panel.fileHistoryText().left(120));
    panel.openFileHistory(QString());
    check("vcs.history.file-empty-path-ignored", panel.lastRequest().kind == HistoryKind::FileHistory);

    // --- blame ------------------------------------------------------------
    panel.showBlame(QStringLiteral("rtl/top.sv"));
    check("vcs.history.blame-request",
          panel.lastRequest().kind == HistoryKind::Blame
              && panel.lastRequest().path == "rtl/top.sv");
    check("vcs.history.blame-text",
          panel.blameText().contains(QStringLiteral("Blame of rtl/top.sv"))
              && panel.blameText().contains(QStringLiteral("module top;"))
              && panel.blameText().contains(QStringLiteral("Alice"))
              && panel.blameText().contains(QStringLiteral("Bob")),
          panel.blameText().left(140));

    // --- comparison needs two selected commits ---------------------------
    panel.compareSelected();
    check("vcs.history.compare-needs-two", panel.messageText().contains(QStringLiteral("two commits")),
          panel.messageText());
    // Select the first two rows through the panel's own row API.
    {
        // A probe drives the list through selectCommit(); the comparison needs two
        // SELECTED rows, so select them through the tree the widget exposes.
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("vcs-history-commits"));
        check("vcs.history.tree-exists", tree != nullptr);
        if (tree != nullptr) {
            tree->clearSelection();
            tree->setCurrentItem(tree->topLevelItem(0));
            tree->topLevelItem(1)->setSelected(true);
            panel.compareSelected();
        }
    }
    check("vcs.history.compare-request",
          panel.lastRequest().kind == HistoryKind::Compare
              && panel.lastRequest().from == panel.commitHashes().value(1).toStdString()
              && panel.lastRequest().to == panel.commitHashes().value(0).toStdString(),
          QString::fromStdString(panel.lastRequest().from) + QStringLiteral(" -> ")
              + QString::fromStdString(panel.lastRequest().to));
    check("vcs.history.compare-text",
          panel.compareText().contains(QStringLiteral("rtl/top.sv"))
              && panel.compareText().contains(QStringLiteral("+// added")),
          panel.compareText().left(140));

    // --- empty / no-commits / error states --------------------------------
    raw->setHistoryData(HistoryKind::Commits, [] {
        HistoryData empty;
        empty.kind = HistoryKind::Commits;
        empty.backend = Backend::Git;
        empty.note = "this repository has no commits yet";
        return empty;
    }());
    panel.refresh();
    check("vcs.history.empty-state", panel.emptyStateVisible() && panel.commitCount() == 0,
          panel.emptyText());
    check("vcs.history.empty-text",
          panel.emptyText().contains(QStringLiteral("no commits")), panel.emptyText());

    raw->setHistoryError(HistoryKind::Commits, "fatal: not a git repository");
    panel.refresh();
    check("vcs.history.error-state", panel.errorStateVisible() && !panel.emptyStateVisible(),
          panel.errorText());
    check("vcs.history.error-text",
          panel.errorText().contains(QStringLiteral("not a git repository")), panel.errorText());

    // A source without a history path answers with a note, never a crash.
    VcsHistoryPanel bare;
    bare.setRoot("/tmp/none");
    bare.refresh();
    check("vcs.history.no-source", bare.commitCount() == 0, bare.emptyText());
}

#if defined(VEDIT_VCS_HAVE_FFI)
namespace {

QByteArray fastImportStream(int commits)
{
    QByteArray stream;
    for (int index = 0; index < commits; ++index) {
        const QByteArray message = QStringLiteral("commit %1").arg(index, 4, 10, QLatin1Char('0')).toUtf8();
        const QByteArray content =
            QStringLiteral("module a;\n// %1\nendmodule\n").arg(index).toUtf8();
        stream += "commit refs/heads/master\n";
        stream += "committer VEdit History <history@example.com> " + QByteArray::number(1600000000 + index)
            + " +0000\n";
        stream += "data " + QByteArray::number(message.size()) + "\n" + message + "\n";
        stream += "M 100644 inline rtl/a.sv\n";
        stream += "data " + QByteArray::number(content.size()) + "\n" + content + "\n";
    }
    return stream;
}

/// Build a repository with `commits` commits through ONE `git fast-import`
/// process (a per-commit `git commit` loop would make the fixture, not the panel,
/// the slow part).
bool fastImportRepository(const QString &git, const QString &repo, int commits)
{
    if (!QDir().mkpath(repo + QStringLiteral("/rtl"))) {
        return false;
    }
    if (!writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"))) {
        return false;
    }
    if (!runProcess(git, {"init", "-q"}, repo)
        || !runProcess(git, {"config", "user.email", "history@example.com"}, repo)
        || !runProcess(git, {"config", "user.name", "VEdit History"}, repo)) {
        return false;
    }
    QProcess process;
    process.setWorkingDirectory(repo);
    process.start(git, {QStringLiteral("fast-import"), QStringLiteral("--quiet"),
                        QStringLiteral("--date-format=raw")});
    if (!process.waitForStarted(10000)) {
        return false;
    }
    process.write(fastImportStream(commits));
    process.closeWriteChannel();
    if (!process.waitForFinished(120000)) {
        process.kill();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

/// A 5 ms timer that counts its own ticks, so an asynchronous read can PROVE the
/// event loop kept running while it was in flight (and a synchronous source
/// proves the same metric detects blocking).
class TickCounter {
public:
    TickCounter()
    {
        timer_ = new QTimer();
        timer_->setInterval(5);
        QObject::connect(timer_, &QTimer::timeout, timer_, [this]() { ++ticks_; });
        timer_->start();
    }
    ~TickCounter()
    {
        timer_->stop();
        timer_->deleteLater();
    }
    int ticks() const { return ticks_; }
    void reset() { ticks_ = 0; }

private:
    QTimer *timer_ = nullptr;
    int ticks_ = 0;
};

} // namespace

void checkHistoryE2E()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        for (const char *name : {"vcs.history.e2e.p1000", "vcs.history.e2e.page",
                                 "vcs.history.e2e.search", "vcs.history.e2e.detail",
                                 "vcs.history.e2e.file-history", "vcs.history.e2e.blame",
                                 "vcs.history.e2e.compare", "vcs.history.e2e.empty-repo",
                                 "vcs.history.nonblocking.ticks", "vcs.history.nonblocking.sync-control"}) {
            skip(name, "git is not on PATH");
        }
        return;
    }
    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/history-1000");
    const bool imported = fastImportRepository(git, repo, 1000);
    check("vcs.history.e2e.p1000", imported, repo);
    if (!imported) {
        return;
    }
    check("vcs.history.e2e.not-worktree",
          QFileInfo(repo).canonicalFilePath() != QFileInfo(QDir::currentPath()).canonicalFilePath()
              && repo.startsWith(QDir::tempPath()),
          QStringLiteral("fixture=%1 cwd=%2").arg(repo, QDir::currentPath()));

    // Direct ABI read first: it isolates an ABI/layout defect from a panel bug.
    {
        VdService *service = vd_service_create();
        const std::string repoPath = repo.toStdString();
        const unsigned long long job =
            vd_vcs_history_commits_job(service, repoPath.c_str(), 0, 2, nullptr);
        VdVcsHistoryResult raw{};
        int state = VD_STATUS_PENDING;
        for (int attempt = 0; attempt < 4000 && state == VD_STATUS_PENDING; ++attempt) {
            state = vd_vcs_history_poll(service, job, &raw);
            if (state == VD_STATUS_PENDING) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const std::string rawHash = raw.commit_count > 0 && raw.commits[0].hash != nullptr
            ? std::string(raw.commits[0].hash)
            : std::string();
        check("vcs.history.abi.raw-page",
              state == VD_STATUS_READY && raw.kind == 1 && raw.commit_count == 2
                  && rawHash.size() == 40,
              QStringLiteral("state=%1 kind=%2 count=%3 hash0=%4")
                  .arg(state)
                  .arg(raw.kind)
                  .arg(raw.commit_count)
                  .arg(QString::fromStdString(rawHash)));
        vd_vcs_history_free(&raw);
        vd_service_destroy(service);
    }

    VcsHistoryPanel panel;
    panel.setRoot(repo.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    // The first page arrives asynchronously.
    const bool first = waitUntil([&panel]() { return panel.commitCount() == 50; }, 30000);
    check("vcs.history.e2e.page", first && panel.hasMore(),
          QStringLiteral("rows=%1 more=%2").arg(panel.commitCount()).arg(panel.hasMore()));
    check("vcs.history.e2e.page-newest-first",
          panel.commitText(0, 3).contains(QStringLiteral("commit 0999")), panel.commitText(0, 3));
    check("vcs.history.e2e.page-label", panel.pageLabel().contains(QStringLiteral("1")),
          panel.pageLabel());

    // --- the 1000-commit fixture proves the read is asynchronous -----------
    // A FRESH panel loads its first page inside setSource(), so the measurement
    // starts before the request exists (a warm panel would be a no-op).
    {
        VcsHistoryPanel timed;
        timed.setRoot(repo.toStdString());
        TickCounter ticks;
        QElapsedTimer timer;
        timer.start();
        timed.setSource(std::make_unique<FfiVcsSource>());
        const qint64 returnedMs = timer.elapsed();
        const bool arrived = waitUntil([&timed]() { return timed.commitCount() == 50; }, 30000);
        const qint64 readMs = timer.elapsed();
        check("vcs.history.nonblocking.returns-fast", returnedMs < 200,
              QStringLiteral("setSource() returned in %1 ms").arg(returnedMs));
        check("vcs.history.nonblocking.ticks", arrived && ticks.ticks() >= 2,
              QStringLiteral("%1 ticks of 5 ms while the read was in flight (%2 ms, rows=%3)")
                  .arg(ticks.ticks())
                  .arg(readMs)
                  .arg(timed.commitCount()));
        std::printf("HISTORY-TIMING p1000 rows=50 read_ms=%lld ticks=%d\n",
                    static_cast<long long>(readMs), ticks.ticks());
    }

    // --- paging over the real repository -----------------------------------
    panel.loadPage(950);
    const bool tail = waitUntil([&panel]() { return panel.commitCount() == 50 && !panel.hasMore(); }, 30000);
    check("vcs.history.e2e.last-page", tail,
          QStringLiteral("rows=%1 more=%2").arg(panel.commitCount()).arg(panel.hasMore()));
    panel.loadPage(0);
    waitUntil([&panel]() { return panel.commitCount() == 50; }, 30000);

    // --- search ------------------------------------------------------------
    panel.search(QStringLiteral("commit 0777"));
    const bool searched = waitUntil([&panel]() { return panel.commitCount() == 1; }, 30000);
    check("vcs.history.e2e.search", searched && panel.commitText(0, 3).contains(QStringLiteral("commit 0777")),
          panel.commitText(0, 3));
    panel.search(QStringLiteral("no such message anywhere"));
    const bool noHits = waitUntil([&panel]() { return panel.commitCount() == 0; }, 30000);
    check("vcs.history.e2e.search-empty", noHits && panel.emptyStateVisible(),
          panel.emptyText());
    panel.search(QString());
    waitUntil([&panel]() { return panel.commitCount() == 50; }, 30000);

    // --- commit detail -----------------------------------------------------
    panel.selectCommit(panel.commitHashes().value(0));
    const bool detail = waitUntil(
        [&panel]() { return panel.detailText().contains(QStringLiteral("commit 0999")); }, 30000);
    check("vcs.history.e2e.detail", detail,
          panel.detailText().left(120));
    check("vcs.history.e2e.detail-patch",
          panel.detailText().contains(QStringLiteral("+// 999"))
              && panel.detailText().contains(QStringLiteral("rtl/a.sv")),
          panel.detailText().left(200));

    // --- file history ------------------------------------------------------
    panel.openFileHistory(QStringLiteral("rtl/a.sv"));
    const bool history = waitUntil(
        [&panel]() { return panel.fileHistoryText().contains(QStringLiteral("History of rtl/a.sv")); }, 30000);
    check("vcs.history.e2e.file-history", history,
          panel.fileHistoryText().left(120));
    check("vcs.history.e2e.file-history-rows",
          panel.fileHistoryText().contains(QStringLiteral("commit 0999")), QStringLiteral("row present"));

    // --- blame -------------------------------------------------------------
    panel.showBlame(QStringLiteral("rtl/a.sv"));
    const bool blamed = waitUntil(
        [&panel]() { return panel.blameText().contains(QStringLiteral("module a;")); }, 30000);
    check("vcs.history.e2e.blame", blamed, panel.blameText().left(120));
    check("vcs.history.e2e.blame-author",
          panel.blameText().contains(QStringLiteral("VEdit History")), QStringLiteral("author present"));

    // --- comparison --------------------------------------------------------
    {
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("vcs-history-commits"));
        if (tree != nullptr && tree->topLevelItemCount() >= 2) {
            tree->clearSelection();
            tree->setCurrentItem(tree->topLevelItem(1));
            tree->topLevelItem(0)->setSelected(true);
            panel.compareSelected();
        }
    }
    const bool compared = waitUntil(
        [&panel]() { return panel.compareText().contains(QStringLiteral("rtl/a.sv")); }, 30000);
    check("vcs.history.e2e.compare", compared, panel.compareText().left(160));
    check("vcs.history.e2e.compare-patch",
          panel.compareText().contains(QStringLiteral("// 999"))
              || panel.compareText().contains(QStringLiteral("// 998")),
          panel.compareText().left(200));

    // --- the negative control: a synchronous source fires no ticks ---------
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        stub->setHistoryData(HistoryKind::Commits, cannedCommitPage(50));
        VcsHistoryPanel sync;
        sync.setRoot(repo.toStdString());
        TickCounter ticks;
        sync.setSource(std::move(stub));
        check("vcs.history.nonblocking.sync-control", ticks.ticks() == 0,
              QStringLiteral("%1 ticks for a synchronous source (the metric detects blocking)")
                  .arg(ticks.ticks()));
    }

    // --- an empty repository is a visible empty state ----------------------
    {
        const QString emptyRepo = temp.path() + QStringLiteral("/empty-repo");
        QDir().mkpath(emptyRepo);
        const bool inited = runProcess(git, {"init", "-q"}, emptyRepo);
        VcsHistoryPanel empty;
        empty.setRoot(emptyRepo.toStdString());
        empty.setSource(std::make_unique<FfiVcsSource>());
        const bool shown = waitUntil([&empty]() { return empty.emptyStateVisible(); }, 30000);
        check("vcs.history.e2e.empty-repo", inited && shown && !empty.errorStateVisible(),
              empty.emptyText());
        check("vcs.history.e2e.empty-repo-text",
              empty.emptyText().contains(QStringLiteral("no commits")), empty.emptyText());
    }
}
#else
void checkHistoryE2E()
{
    skip("vcs.history.e2e.p1000", "lane built without vd-ffi (stub-only build)");
}
#endif


// ==========================================================================
// Batch 5a (AC-5.1 / AC-5.3): history operations and conflict resolution
// ==========================================================================

void checkHistoryOpsStub()
{
    auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });

    // --- the operation requests -------------------------------------------
    check("vcs.ops.merge-requested", panel.merge(QStringLiteral("feature/x")));
    check("vcs.ops.merge-request",
          raw->writes().back().op == WriteOp::GitMerge && raw->writes().back().target == "feature/x",
          QString::fromStdString(raw->writes().back().target));
    check("vcs.ops.rebase-requested", panel.rebase(QStringLiteral("main")));
    check("vcs.ops.rebase-request",
          raw->writes().back().op == WriteOp::GitRebase && raw->writes().back().target == "main");
    // Batch 7 (user ruling): a rebase rewrites history, so it asks ONCE with the
    // consequence spelled out (the reset confirmation style); a decline writes
    // nothing.
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const std::size_t beforeRebaseDecline = raw->writes().size();
    const int rebaseCalls = panel.confirmCalls();
    check("vcs.ops.rebase-declined", !panel.rebase(QStringLiteral("main")));
    check("vcs.ops.rebase-declined-zero-write",
          raw->writes().size() == beforeRebaseDecline,
          QStringLiteral("writes=%1").arg(raw->writes().size()));
    check("vcs.ops.rebase-single-confirm", panel.confirmCalls() == rebaseCalls + 1,
          QStringLiteral("confirm calls=%1").arg(panel.confirmCalls() - rebaseCalls));
    check("vcs.ops.rebase-preview-spells-consequence",
          panel.lastConfirmText().contains(QStringLiteral("rewrites"))
              && panel.lastConfirmText().contains(QStringLiteral("force-pushed")),
          panel.lastConfirmText().left(160));
    check("vcs.ops.rebase-empty-target-refused", !panel.rebase(QStringLiteral("   ")));
    check("vcs.ops.rebase-empty-target-zero-write",
          raw->writes().size() == beforeRebaseDecline);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.ops.rebase-confirmed-requested", panel.rebase(QStringLiteral("main")));
    check("vcs.ops.rebase-confirmed-request",
          raw->writes().back().op == WriteOp::GitRebase
              && raw->writes().back().target == "main");
    check("vcs.ops.cherry-pick-requested", panel.cherryPick(QStringLiteral("abc1234")));
    check("vcs.ops.cherry-pick-request",
          raw->writes().back().op == WriteOp::GitCherryPick
              && raw->writes().back().target == "abc1234");
    check("vcs.ops.revert-requested", panel.revertCommit(QStringLiteral("abc1234")));
    check("vcs.ops.revert-request",
          raw->writes().back().op == WriteOp::GitRevert
              && raw->writes().back().target == "abc1234");

    // --- reset: mode validation + double confirmation for hard -------------
    const std::size_t beforeReset = raw->writes().size();
    check("vcs.ops.reset-invalid-mode-refused",
          !panel.resetTo(QStringLiteral("abc1234"), QStringLiteral("harder")));
    check("vcs.ops.reset-invalid-mode-zero-write", raw->writes().size() == beforeReset,
          QStringLiteral("writes=%1").arg(raw->writes().size()));
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const int callsBefore = panel.confirmCalls();
    check("vcs.ops.reset-hard-declined",
          !panel.resetTo(QStringLiteral("abc1234"), QStringLiteral("hard")));
    check("vcs.ops.reset-hard-declined-zero-write", raw->writes().size() == beforeReset,
          QStringLiteral("writes=%1").arg(raw->writes().size()));
    check("vcs.ops.reset-hard-first-confirm", panel.confirmCalls() == callsBefore + 1);
    check("vcs.ops.reset-preview-mentions-target",
          panel.lastConfirmText().contains(QStringLiteral("abc1234")),
          panel.lastConfirmText().left(120));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.ops.reset-hard-requested",
          panel.resetTo(QStringLiteral("abc1234"), QStringLiteral("hard")));
    check("vcs.ops.reset-hard-double-confirmed", panel.confirmCalls() == callsBefore + 3,
          QStringLiteral("calls=%1").arg(panel.confirmCalls()));
    check("vcs.ops.reset-request",
          raw->writes().back().op == WriteOp::GitReset
              && raw->writes().back().mode == "hard"
              && raw->writes().back().target == "abc1234",
          QString::fromStdString(raw->writes().back().mode));
    check("vcs.ops.reset-soft-single-confirm", panel.resetTo(QStringLiteral("abc1234"),
                                                             QStringLiteral("soft")));
    check("vcs.ops.reset-soft-request",
          raw->writes().back().mode == "soft" && panel.confirmCalls() == callsBefore + 4,
          QStringLiteral("calls=%1").arg(panel.confirmCalls()));

    // --- clean: double confirmation ---------------------------------------
    const std::size_t beforeClean = raw->writes().size();
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    check("vcs.ops.clean-declined", !panel.cleanFiles({QStringLiteral("scratch.txt")}));
    check("vcs.ops.clean-declined-zero-write", raw->writes().size() == beforeClean);
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.ops.clean-requested", panel.cleanFiles({QStringLiteral("scratch.txt")}));
    check("vcs.ops.clean-request",
          raw->writes().back().op == WriteOp::GitClean
              && raw->writes().back().paths.size() == 1
              && raw->writes().back().paths.front() == "scratch.txt");
    check("vcs.ops.clean-empty-refused", !panel.cleanFiles({}));

    // --- NIT from the 5a/5b/6b reviews: confirmTwice's SECOND step ----------
    // A provider that accepts the first question and declines the second proves
    // the second-step refusal really exists (it used to be only reached on the
    // first step). The discard path uses confirmTwice, so nothing may be written.
    panel.selectFile("rtl/top.sv");
    {
        int step = 0;
        panel.setConfirmProvider(
            [&step](const QString &, const QString &) { return ++step == 1; });
        const std::size_t beforeSecondStep = raw->writes().size();
        const int callsBeforeSecondStep = panel.confirmCalls();
        check("vcs.ops.discard-second-step-declined", !panel.discardSelected());
        check("vcs.ops.discard-second-step-zero-write",
              raw->writes().size() == beforeSecondStep,
              QStringLiteral("writes=%1").arg(raw->writes().size()));
        check("vcs.ops.discard-second-step-two-confirms",
              panel.confirmCalls() == callsBeforeSecondStep + 2,
              QStringLiteral("confirm calls=%1").arg(panel.confirmCalls() - callsBeforeSecondStep));
        check("vcs.ops.discard-second-step-message",
              panel.messageText().contains(QStringLiteral("cancelled")), panel.messageText());
        panel.setConfirmProvider([](const QString &, const QString &) { return true; });
        check("vcs.ops.discard-first-step-accepted-after",
              panel.discardSelected() && raw->writes().back().op == WriteOp::GitDiscard);
    }

    // --- continue/abort need an operation in progress ----------------------
    const std::size_t beforeRef = raw->writes().size();
    check("vcs.ops.continue-without-operation", !panel.continueOperation());
    check("vcs.ops.abort-without-operation", !panel.abortOperation());
    check("vcs.ops.continue-without-operation-zero-write", raw->writes().size() == beforeRef);
}

void checkConflictStateStub()
{
    // A conflict state: the info read reports the operation and the status lists
    // the conflicted file.
    Status status = gitStatus();
    status.changes = {Change{"rtl/top.sv", 5}, Change{"rtl/added.sv", 2}};
    Info info = gitInfo();
    info.operation = GitOperation::Merge;
    auto stub = std::make_unique<StubVcsSource>(info, status, gitDiffs());
    auto *raw = stub.get();
    VcsPanel panel;
    panel.setRoot("/tmp/repo-git");
    panel.setSource(std::move(stub));
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });

    check("vcs.conflict.operation-reported", panel.operation() == GitOperation::Merge,
          QString::fromLatin1(vedit::workspace::vcs::gitOperationName(panel.operation())));
    check("vcs.conflict.banner-visible", !panel.conflictBannerText().isEmpty(),
          panel.conflictBannerText());
    check("vcs.conflict.banner-names-operation-and-count",
          panel.conflictBannerText().contains(QStringLiteral("Merge"))
              && panel.conflictBannerText().contains(QStringLiteral("1")),
          panel.conflictBannerText());
    check("vcs.conflict.continue-available", panel.continueAvailable());
    check("vcs.conflict.abort-available", panel.abortAvailable());
    check("vcs.conflict.file-row-shows-conflict",
          panel.fileText(0).contains(QStringLiteral("conflict")), panel.fileText(0));

    check("vcs.conflict.continue-requested", panel.continueOperation());
    check("vcs.conflict.continue-request",
          raw->writes().back().op == WriteOp::GitContinue
              && raw->writes().back().target == "merge",
          QString::fromStdString(raw->writes().back().target));
    check("vcs.conflict.abort-requested", panel.abortOperation());
    check("vcs.conflict.abort-request",
          raw->writes().back().op == WriteOp::GitAbort
              && raw->writes().back().target == "merge",
          QString::fromStdString(raw->writes().back().target));

    // No operation: no banner, both entry points disabled.
    VcsPanel clean;
    clean.setRoot("/tmp/repo-git");
    clean.setSource(std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs()));
    check("vcs.conflict.no-banner-when-idle", clean.conflictBannerText().isEmpty());
    check("vcs.conflict.no-continue-when-idle", !clean.continueAvailable());
    check("vcs.conflict.no-abort-when-idle", !clean.abortAvailable());
}

void checkHistoryOpsE2E()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        skip("vcs.ops.e2e.merge-conflict", "git is not on PATH");
        skip("vcs.ops.e2e.continue", "git is not on PATH");
        skip("vcs.ops.e2e.reset-hard", "git is not on PATH");
        skip("vcs.ops.e2e.clean", "git is not on PATH");
        return;
    }
    QTemporaryDir temp;
    const QString repo = temp.path() + QStringLiteral("/ops-repo");
    QDir().mkpath(repo + QStringLiteral("/rtl"));
    const bool fixture =
        writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nbase\nendmodule\n"))
        && runProcess(git, {"init", "-q"}, repo)
        && runProcess(git, {"config", "user.email", "ops@example.com"}, repo)
        && runProcess(git, {"config", "user.name", "VEdit Ops"}, repo)
        && runProcess(git, {"add", "rtl/a.sv"}, repo)
        && runProcess(git, {"commit", "-q", "-m", "base"}, repo)
        && runProcess(git, {"checkout", "-q", "-b", "feature"}, repo)
        && writeText(repo + QStringLiteral("/rtl/a.sv"),
                     QStringLiteral("module a;\nfeature\nendmodule\n"))
        && runProcess(git, {"commit", "-q", "-am", "feature change"}, repo)
        && runProcess(git, {"checkout", "-q", "master"}, repo)
        && writeText(repo + QStringLiteral("/rtl/a.sv"),
                     QStringLiteral("module a;\nmaster\nendmodule\n"))
        && runProcess(git, {"commit", "-q", "-am", "master change"}, repo);
    check("vcs.ops.e2e.fixture", fixture, repo);
    if (!fixture) {
        return;
    }

    VcsPanel panel;
    panel.setRoot(repo.toStdString());
    panel.setSource(std::make_unique<FfiVcsSource>());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    const auto writeDone = [&panel]() {
        return waitUntil([&panel]() { return !panel.writeInFlight(); }, 30000);
    };
    waitUntil([&panel]() { return panel.fileCount() >= 0 && !panel.headerText().isEmpty(); }, 30000);

    // --- merge conflict → conflict state → resolve → continue --------------
    check("vcs.ops.e2e.merge-conflict", panel.merge(QStringLiteral("feature")));
    writeDone();
    check("vcs.ops.e2e.merge-conflict-failed", !panel.lastWriteOk(), panel.messageText());
    const bool conflictVisible = waitUntil([&panel]() {
        return panel.operation() == GitOperation::Merge && panel.continueAvailable();
    }, 30000);
    check("vcs.ops.e2e.conflict-state", conflictVisible,
          QStringLiteral("op=%1 banner='%2'")
              .arg(QString::fromLatin1(vedit::workspace::vcs::gitOperationName(panel.operation())),
                   panel.conflictBannerText()));
    check("vcs.ops.e2e.conflict-banner", panel.conflictBannerText().contains(QStringLiteral("1")),
          panel.conflictBannerText());
    check("vcs.ops.e2e.conflict-file-row",
          waitUntil([&panel]() { return panel.fileCount() >= 1
                                    && panel.fileText(0).contains(QStringLiteral("conflict")); }, 20000),
          panel.fileCount() >= 1 ? panel.fileText(0) : QStringLiteral("no rows"));

    // Resolve: write the resolution, select the conflicted row and stage it.
    writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nresolved\nendmodule\n"));
    panel.selectFile("rtl/a.sv");
    check("vcs.ops.e2e.stage-resolution", panel.stageSelected());
    writeDone();
    check("vcs.ops.e2e.continue-requested", panel.continueOperation());
    writeDone();
    check("vcs.ops.e2e.continue-ok", panel.lastWriteOk(), panel.messageText());
    const bool idleAgain = waitUntil([&panel]() { return panel.operation() == GitOperation::None; }, 30000);
    check("vcs.ops.e2e.operation-cleared", idleAgain,
          QString::fromLatin1(vedit::workspace::vcs::gitOperationName(panel.operation())));
    QString parents;
    runProcess(git, {"rev-list", "--parents", "-n", "1", "HEAD"}, repo, &parents);
    check("vcs.ops.e2e.merge-commit-created", parents.split(QLatin1Char(' ')).size() == 3,
          parents.trimmed());

    // --- reset --hard: double confirmation, real effect --------------------
    writeText(repo + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nlocal edit\nendmodule\n"));
    QString beforeReset;
    runProcess(git, {"rev-parse", "HEAD"}, repo, &beforeReset);
    panel.setConfirmProvider([](const QString &, const QString &) { return false; });
    const std::size_t beforeCalls = static_cast<std::size_t>(panel.confirmCalls());
    check("vcs.ops.e2e.reset-hard-declined",
          !panel.resetTo(QStringLiteral("HEAD~1"), QStringLiteral("hard")));
    check("vcs.ops.e2e.reset-hard-declined-zero-write",
          static_cast<std::size_t>(panel.confirmCalls()) == beforeCalls + 1,
          QStringLiteral("calls=%1").arg(panel.confirmCalls()));
    QString unchanged;
    runProcess(git, {"rev-parse", "HEAD"}, repo, &unchanged);
    check("vcs.ops.e2e.reset-hard-declined-head", unchanged.trimmed() == beforeReset.trimmed());
    panel.setConfirmProvider([](const QString &, const QString &) { return true; });
    check("vcs.ops.e2e.reset-hard-requested",
          panel.resetTo(QStringLiteral("HEAD~1"), QStringLiteral("hard")));
    writeDone();
    check("vcs.ops.e2e.reset-hard-ok", panel.lastWriteOk(), panel.errorText().left(120));
    QString headAfter;
    runProcess(git, {"rev-parse", "HEAD"}, repo, &headAfter);
    check("vcs.ops.e2e.reset-hard-moved-head", headAfter.trimmed() != beforeReset.trimmed());
    QString statusAfter;
    runProcess(git, {"status", "--porcelain"}, repo, &statusAfter);
    check("vcs.ops.e2e.reset-hard-clean-tree",
          !statusAfter.contains(QStringLiteral("rtl/a.sv")), statusAfter.trimmed());

    // --- clean: double confirmation, real effect --------------------------
    writeText(repo + QStringLiteral("/scratch.txt"), QStringLiteral("temporary"));
    check("vcs.ops.e2e.clean-requested", panel.cleanFiles({QStringLiteral("scratch.txt")}));
    writeDone();
    check("vcs.ops.e2e.clean-ok", panel.lastWriteOk(), panel.errorText().left(120));
    check("vcs.ops.e2e.clean-removed", !QFileInfo::exists(repo + QStringLiteral("/scratch.txt")));
}

// AC-3.7 / AC-3.4: the visible strings really translate through the compiled
// catalogue. This runs LAST because it installs the translator process-wide.
void checkI18n()
{
    const QString qm = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../translations/vedit_zh_CN.qm");
    QTranslator translator;
    if (!QFileInfo::exists(qm) || !translator.load(qm)) {
        skip("vcs.i18n.zh-cn-catalogue",
             "the built vedit_zh_CN.qm was not found next to the lane binary");
        skip("vcs.i18n.action-bar", "no compiled catalogue");
        skip("vcs.i18n.confirmations", "no compiled catalogue");
        skip("vcs.i18n.states", "no compiled catalogue");
        skip("vcs.i18n.ops", "no compiled catalogue");
        skip("vcs.i18n.history", "no compiled catalogue");
        skip("vcs.i18n.credentials", "no compiled catalogue");
        skip("vcs.i18n.workspace", "no compiled catalogue");
        skip("vcs.i18n.source-literals", "no compiled catalogue");
        skip("vcs.i18n.change-row-chinese", "no compiled catalogue");
        skip("vcs.i18n.svn", "no compiled catalogue");
        skip("vcs.i18n.svn-button-labels", "no compiled catalogue");
        return;
    }
    check("vcs.i18n.zh-cn-catalogue", true, qm);
    QCoreApplication::installTranslator(&translator);
    const auto zh = [](const char *source) {
        return QCoreApplication::translate("VEdit", source);
    };
    check("vcs.i18n.action-bar",
          zh("Stage") != QStringLiteral("Stage") && zh("Commit…") != QStringLiteral("Commit…")
              && zh("Discard…") != QStringLiteral("Discard…")
              && zh("Fetch") != QStringLiteral("Fetch")
              && zh("Push") != QStringLiteral("Push") && zh("Branch…") != QStringLiteral("Branch…"),
          zh("Stage") + QStringLiteral(" / ") + zh("Fetch") + QStringLiteral(" / ") + zh("Branch…"));
    check("vcs.i18n.confirmations",
          zh("Discard changes?") != QStringLiteral("Discard changes?")
              && zh("Amend the last commit?") != QStringLiteral("Amend the last commit?")
              && zh("Delete the branch?") != QStringLiteral("Delete the branch?")
              && zh("Delete the tag?") != QStringLiteral("Delete the tag?"),
          zh("Discard changes?"));
    check("vcs.i18n.states",
          zh("Loading…") != QStringLiteral("Loading…")
              && zh("No tags") != QStringLiteral("No tags")
              && zh("No remotes configured") != QStringLiteral("No remotes configured")
              && zh("Branches") != QStringLiteral("Branches"),
          zh("Loading…"));
    check("vcs.i18n.ops",
          zh("Continue") != QStringLiteral("Continue") && zh("Abort") != QStringLiteral("Abort")
              && zh("%1 in progress: %2 conflicted file(s). Resolve them, stage the result, then "
                    "Continue (or Abort).")
                  != QStringLiteral("%1 in progress: %2 conflicted file(s). Resolve them, stage the "
                                    "result, then Continue (or Abort).")
              && zh("Reset the branch?") != QStringLiteral("Reset the branch?"),
          zh("Continue") + QStringLiteral(" / ") + zh("Abort"));
    check("vcs.i18n.history",
          zh("Blame") != QStringLiteral("Blame")
              && zh("This repository has no commits yet.")
                  != QStringLiteral("This repository has no commits yet.")
              && zh("Compare selected") != QStringLiteral("Compare selected")
              && zh("Working tree") != QStringLiteral("Working tree")
              && zh("History") != QStringLiteral("History"),
          zh("Blame") + QStringLiteral(" / ") + zh("History"));
    // Batch 7 (7a i18n gap): backendName()/changeKindName() used to return bare
    // English literals; they must now resolve through the catalogue ("No
    // repository" and "modified" have DIFFERENT Chinese text on purpose, so the
    // check cannot pass by the source string alone).
    check("vcs.i18n.source-literals",
          vedit::workspace::vcs::backendName(Backend::None) != QStringLiteral("No repository")
              && vedit::workspace::vcs::changeKindName(2) != QStringLiteral("modified")
              && vedit::workspace::vcs::changeKindName(2) == QStringLiteral("修改")
              && vedit::workspace::vcs::backendName(Backend::None) == QStringLiteral("无仓库"),
          vedit::workspace::vcs::backendName(Backend::None) + QStringLiteral(" / ")
              + vedit::workspace::vcs::changeKindName(2));
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        VcsPanel panel;
        panel.setRoot("/tmp/repo-zh");
        panel.setSource(std::move(stub));
        check("vcs.i18n.change-row-chinese",
              panel.fileText(0).contains(QStringLiteral("修改"))
                  || panel.fileText(0).contains(QStringLiteral("新增")),
              panel.fileText(0));
    }
    check("vcs.i18n.workspace",
          zh("Git workspace") != QStringLiteral("Git workspace")
              && zh("Save stash") != QStringLiteral("Save stash")
              && zh("Add worktree") != QStringLiteral("Add worktree")
              && zh("Remove worktree") != QStringLiteral("Remove worktree")
              && zh("Initialize submodules") != QStringLiteral("Initialize submodules")
              && zh("Update submodules") != QStringLiteral("Update submodules")
              && zh("Drop the stash?") != QStringLiteral("Drop the stash?")
              && zh("Remove the worktree?") != QStringLiteral("Remove the worktree?"),
          zh("Git workspace") + QStringLiteral(" / ") + zh("Remove worktree"));
    check("vcs.i18n.svn",
          zh("Subversion has no staging area: changed files are committed directly from the "
             "working copy.")
                  != QStringLiteral("Subversion has no staging area: changed files are committed "
                                    "directly from the working copy.")
              && zh("Revert changes?") != QStringLiteral("Revert changes?")
              && zh("Remove the file?") != QStringLiteral("Remove the file?")
              && zh("Added %1.") != QStringLiteral("Added %1.")
              && zh("Subversion: add, commit, revert, remove and update are available. Staging, "
                   "fetches, pushes, branches, tags, remotes, stashes, worktrees and conflict "
                   "continuation are not — hover a disabled action for its reason.")
                  != QStringLiteral("Subversion: add, commit, revert, remove and update are "
                                    "available. Staging, fetches, pushes, branches, tags, "
                                    "remotes, stashes, worktrees and conflict continuation are "
                                    "not — hover a disabled action for its reason."),
          zh("Revert changes?") + QStringLiteral(" / ") + zh("Remove the file?"));
    // The SVN button labels are set at runtime (Stage↔Add, Discard↔Revert,
    // Pull↔Update); they MUST be in the compiled catalogue, which is only true
    // when the literal reaches QCoreApplication::translate directly.
    check("vcs.i18n.svn-button-labels",
          zh("Revert…") != QStringLiteral("Revert…")
              && zh("Update") != QStringLiteral("Update")
              && zh("Add") != QStringLiteral("Add"),
          zh("Revert…") + QStringLiteral(" / ") + zh("Update"));
    check("vcs.i18n.credentials",
          zh("Credentials required") != QStringLiteral("Credentials required")
              && zh("The credentials are kept in memory for this operation only. They are not written to "
                    "disk, not stored in the repository configuration and not written to any log.")
                  != QStringLiteral("The credentials are kept in memory for this operation only. They are "
                                    "not written to disk, not stored in the repository configuration and not "
                                    "written to any log."),
          zh("Credentials required"));
    QCoreApplication::removeTranslator(&translator);
}

// AC-3.4: 1500x1000 screenshots of the four states plus the commit dialog. Run
// under xcb (Xvfb) by evidence/vcs-ui/screenshots.sh; `--require-xcb` turns a
// wrong platform into a failure instead of a silently offscreen render.
int runScreenshots(const QString &directory, bool requireXcb)
{
    QDir().mkpath(directory);
    if (requireXcb && QGuiApplication::platformName() != QStringLiteral("xcb")) {
        std::printf("CHECK vcs.screenshot.platform FAIL platform=%s (expected xcb)\n",
                    QGuiApplication::platformName().toUtf8().constData());
        return 1;
    }
    check("vcs.screenshot.platform", !requireXcb
              || QGuiApplication::platformName() == QStringLiteral("xcb"),
          QGuiApplication::platformName());

    // A screenshot is evidence only when it really exists, has the requested
    // dimensions and is not a flat single-colour rectangle: each shot asserts its
    // bytes, its size and the number of distinct sampled colours (F1).
    const auto shoot = [&directory](QWidget *widget, const QString &name, const QSize &size) {
        widget->resize(size);
        widget->show();
        QCoreApplication::processEvents();
        pump(120);
        const QString path = directory + QStringLiteral("/") + name;
        const bool saved = widget->grab().save(path);
        const qint64 bytes = QFileInfo(path).size();
        const QImage image(path);
        int colors = 0;
        {
            QSet<QRgb> distinct;
            for (int y = 0; y < image.height(); y += 7) {
                for (int x = 0; x < image.width(); x += 7) {
                    distinct.insert(image.pixel(x, y));
                }
            }
            colors = distinct.size();
        }
        const QByteArray bytesName = (name + QStringLiteral(".bytes")).toUtf8();
        const QByteArray sizeName = (name + QStringLiteral(".dimensions")).toUtf8();
        const QByteArray pixelsName = (name + QStringLiteral(".pixels")).toUtf8();
        check(bytesName.constData(), saved && bytes > 4096,
              QStringLiteral("%1 bytes=%2").arg(path).arg(bytes));
        check(sizeName.constData(), image.size() == size,
              QStringLiteral("%1x%2").arg(image.width()).arg(image.height()));
        check(pixelsName.constData(), colors >= 8,
              QStringLiteral("distinct sampled colours=%1").arg(colors));
        widget->hide();
    };

    // The ready / staged / loading shots need a real repository.
    QTemporaryDir temp;
    QString repo;
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
#if defined(VEDIT_VCS_HAVE_FFI)
    if (!git.isEmpty()) {
        repo = temp.path() + QStringLiteral("/ui-repo");
        QDir().mkpath(repo + QStringLiteral("/rtl"));
        writeText(repo + QStringLiteral("/rtl/top.sv"),
                  QStringLiteral("module top;\n  assign a = b;\nendmodule\n"));
        writeText(repo + QStringLiteral("/rtl/helper.v"),
                  QStringLiteral("module helper;\nendmodule\n"));
        runProcess(git, {"init", "-q"}, repo);
        runProcess(git, {"config", "user.email", "ui@example.com"}, repo);
        runProcess(git, {"config", "user.name", "VEdit UI"}, repo);
        runProcess(git, {"add", "."}, repo);
        runProcess(git, {"commit", "-m", "ui fixture"}, repo);
        writeText(repo + QStringLiteral("/rtl/top.sv"),
                  QStringLiteral("module top;\n  assign a = c;\nendmodule\n"));
        writeText(repo + QStringLiteral("/rtl/new_file.sv"),
                  QStringLiteral("module new_file;\nendmodule\n"));
    }
    if (!repo.isEmpty()) {
        {
            VcsPanel panel;
            panel.setRoot(repo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            waitUntil([&panel]() { return panel.fileCount() == 2; }, 20000);
            panel.selectFile("rtl/top.sv");
            waitUntil([&panel]() { return !panel.diffText().isEmpty(); }, 20000);
            shoot(&panel, QStringLiteral("10-vcs-three-columns-1500x1000.png"), QSize(1500, 1000));
            panel.setConfirmProvider([](const QString &, const QString &) { return true; });
            panel.stageSelected();
            waitUntil([&panel]() { return !panel.writeInFlight(); }, 20000);
            panel.flushRefreshForProbe();
            waitUntil([&panel]() { return panel.stagedCount() == 1; }, 6000);
            shoot(&panel, QStringLiteral("11-vcs-staged-1500x1000.png"), QSize(1500, 1000));
        }
        // loading: a fake git that sleeps, so the state stays visible long enough
        // to be rendered.
        {
            const QString binDir = temp.path() + QStringLiteral("/bin");
            QDir().mkpath(binDir);
            const QString slowGit = binDir + QStringLiteral("/git");
            writeText(slowGit, QStringLiteral("#!/bin/sh\nsleep 1.5\nexec \"%1\" \"$@\"\n").arg(git));
            QFile::setPermissions(slowGit, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                | QFileDevice::ExeOwner | QFileDevice::ReadUser
                                                | QFileDevice::ExeUser | QFileDevice::ReadGroup
                                                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                                | QFileDevice::ExeOther);
            const QByteArray savedPath = qgetenv("PATH");
            qputenv("PATH", binDir.toUtf8() + ':' + savedPath);
            VcsPanel panel;
            panel.setRoot(repo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            pump(80);
            shoot(&panel, QStringLiteral("13-vcs-loading-1500x1000.png"), QSize(1500, 1000));
            qputenv("PATH", savedPath);
            waitUntil([&panel]() { return !panel.loadingStateVisible(); }, 15000);
        }
    }
#else
    Q_UNUSED(git);
    Q_UNUSED(repo);
    skip("vcs.screenshot.ready", "lane built without vd-ffi (stub-only build)");
#endif
    // empty state
    {
        Status empty;
        empty.note = QCoreApplication::translate("VEdit", "not a Git or Subversion working tree")
                         .toStdString();
        auto stub = std::make_unique<StubVcsSource>(Info{}, empty, std::vector<Diff>{});
        VcsPanel panel;
        panel.setRoot(temp.path().toStdString());
        panel.setSource(std::move(stub));
        shoot(&panel, QStringLiteral("12-vcs-empty-state-1500x1000.png"), QSize(1500, 1000));
    }
    // error state
    {
        Status broken;
        broken.backend = Backend::Git;
        broken.error = true;
        broken.note = "git: fatal: not a git repository (or any parent)";
        auto stub = std::make_unique<StubVcsSource>(Info{}, broken, std::vector<Diff>{});
        VcsPanel panel;
        panel.setRoot("/tmp/broken");
        panel.setSource(std::move(stub));
        shoot(&panel, QStringLiteral("14-vcs-error-state-1500x1000.png"), QSize(1500, 1000));
    }
    // commit dialog
    {
        CommitDialog dialog;
        dialog.setFields(QStringLiteral("rtl: fix the top-level assignment"),
                         QStringLiteral("VEdit Test <test@vedit.local>"), false);
        shoot(&dialog, QStringLiteral("15-vcs-commit-dialog-1500x1000.png"), QSize(900, 360));
    }
    // batch 4a (F1): the refs column with real branches/tags/remote-tracking refs
    // and a configured remote, the remote action row, and the four dialogs.
#if defined(VEDIT_VCS_HAVE_FFI)
    if (!git.isEmpty() && !repo.isEmpty()) {
        const QString base = temp.path() + QStringLiteral("/ui-remote");
        const QString bare = base + QStringLiteral("/origin.git");
        const QString work = base + QStringLiteral("/work");
        QDir().mkpath(work + QStringLiteral("/rtl"));
        writeText(work + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a;\nendmodule\n"));
        const bool fixture =
            runProcess(git, {"init", "-q"}, work)
            && runProcess(git, {"config", "user.email", "ui@example.com"}, work)
            && runProcess(git, {"config", "user.name", "VEdit UI"}, work)
            && runProcess(git, {"add", "rtl/a.sv"}, work)
            && runProcess(git, {"commit", "-q", "-m", "refs fixture"}, work)
            && runProcess(git, {"init", "--bare", "-q", bare}, base)
            && runProcess(git, {"remote", "add", "origin", bare}, work);
        QString branch;
        runProcess(git, {"symbolic-ref", "--short", "HEAD"}, work, &branch);
        branch = branch.trimmed();
        const bool refsFixture = fixture && !branch.isEmpty()
            && runProcess(git, {"push", "-q", "-u", "origin", branch}, work)
            && runProcess(git, {"branch", "feature/refs"}, work)
            && runProcess(git, {"tag", "v1.0"}, work)
            && runProcess(git, {"tag", "v0.9"}, work);
        check("vcs.screenshot.refs-fixture", refsFixture, work);
        if (refsFixture) {
            VcsPanel panel;
            panel.setRoot(work.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            const bool ready = waitUntil(
                [&panel]() { return !panel.refs().empty() && !panel.remotes().empty(); }, 30000);
            check("vcs.screenshot.refs-info", ready,
                  QStringLiteral("refs=%1 remotes=%2")
                      .arg(panel.refs().size())
                      .arg(panel.remotes().size()));
            panel.selectRef(QStringLiteral("feature/refs"));
            shoot(&panel, QStringLiteral("20-vcs-refs-and-remotes-1500x1000.png"), QSize(1500, 1000));
            // The remote action row legibly: the same panel with the columns hidden.
            if (auto *columns = panel.findChild<QWidget *>(QStringLiteral("vcs-columns"))) {
                columns->setVisible(false);
            }
            shoot(&panel, QStringLiteral("21-vcs-remote-action-row-1500x1000.png"), QSize(1500, 1000));
        }
    }
#endif
    // The dialogs (F1) render on the same xcb platform, at 1500x1000.
    {
        CredentialDialog dialog(QCoreApplication::translate(
            "VEdit", "The push operation needs credentials for %1. They are kept in memory for this "
                     "operation only and are never written to disk.").arg(QStringLiteral("https://example.com/vedit.git")));
        dialog.setFields(QStringLiteral("alice"), QString());
        shoot(&dialog, QStringLiteral("22-vcs-credential-dialog-1500x1000.png"), QSize(1500, 1000));
    }
    {
        BranchDialog dialog(BranchDialog::Mode::Create);
        dialog.setFields(QStringLiteral("feature/remote-sync"), QStringLiteral("origin/main"), true);
        shoot(&dialog, QStringLiteral("23-vcs-branch-dialog-1500x1000.png"), QSize(1500, 1000));
    }
    {
        TagDialog dialog;
        dialog.setFields(QStringLiteral("v1.0-rc1"), QStringLiteral("HEAD"));
        shoot(&dialog, QStringLiteral("24-vcs-tag-dialog-1500x1000.png"), QSize(1500, 1000));
    }
    {
        RemoteDialog dialog(RemoteDialog::Mode::Edit);
        dialog.setFields(QStringLiteral("origin"), QStringLiteral("https://example.com/vedit.git"));
        shoot(&dialog, QStringLiteral("25-vcs-remote-dialog-1500x1000.png"), QSize(1500, 1000));
    }
#if defined(VEDIT_VCS_HAVE_FFI)
    // batch 4b (AC-4.3): the history view with a real repository, blame, and the
    // empty / error states.
    if (!git.isEmpty()) {
        const QString historyRepo = temp.path() + QStringLiteral("/history-ui");
        const bool imported = fastImportRepository(git, historyRepo, 40);
        if (imported) {
            VcsHistoryPanel panel;
            panel.setRoot(historyRepo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            const bool ready = waitUntil([&panel]() { return panel.commitCount() == 40; }, 30000);
            check("vcs.screenshot.history-ready", ready,
                  QStringLiteral("rows=%1").arg(panel.commitCount()));
            if (ready) {
                panel.selectCommit(panel.commitHashes().value(0));
                waitUntil([&panel]() { return panel.detailText().contains(QStringLiteral("commit 0039")); }, 30000);
                shoot(&panel, QStringLiteral("30-vcs-history-commits-1500x1000.png"), QSize(1500, 1000));
                panel.showBlame(QStringLiteral("rtl/a.sv"));
                waitUntil([&panel]() { return panel.blameText().contains(QStringLiteral("module a;")); }, 30000);
                shoot(&panel, QStringLiteral("31-vcs-history-blame-1500x1000.png"), QSize(1500, 1000));
            }
        }
        // empty state: a repository with no commits
        const QString emptyRepo = temp.path() + QStringLiteral("/history-empty");
        QDir().mkpath(emptyRepo);
        if (runProcess(git, {"init", "-q"}, emptyRepo)) {
            VcsHistoryPanel panel;
            panel.setRoot(emptyRepo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            waitUntil([&panel]() { return panel.emptyStateVisible(); }, 20000);
            shoot(&panel, QStringLiteral("32-vcs-history-empty-1500x1000.png"), QSize(1500, 1000));
        }
    }
#endif
#if defined(VEDIT_VCS_HAVE_FFI)
    // batch 5a (AC-5.1): the conflict state with its banner and the conflicted row.
    if (!git.isEmpty()) {
        const QString conflictRepo = temp.path() + QStringLiteral("/ops-conflict");
        QDir().mkpath(conflictRepo + QStringLiteral("/rtl"));
        const bool built =
            writeText(conflictRepo + QStringLiteral("/rtl/a.sv"),
                      QStringLiteral("module a;\nbase\nendmodule\n"))
            && runProcess(git, {"init", "-q"}, conflictRepo)
            && runProcess(git, {"config", "user.email", "ui@example.com"}, conflictRepo)
            && runProcess(git, {"config", "user.name", "VEdit UI"}, conflictRepo)
            && runProcess(git, {"add", "rtl/a.sv"}, conflictRepo)
            && runProcess(git, {"commit", "-q", "-m", "base"}, conflictRepo)
            && runProcess(git, {"checkout", "-q", "-b", "feature"}, conflictRepo)
            && writeText(conflictRepo + QStringLiteral("/rtl/a.sv"),
                         QStringLiteral("module a;\nfeature\nendmodule\n"))
            && runProcess(git, {"commit", "-q", "-am", "feature"}, conflictRepo)
            && runProcess(git, {"checkout", "-q", "master"}, conflictRepo)
            && writeText(conflictRepo + QStringLiteral("/rtl/a.sv"),
                         QStringLiteral("module a;\nmaster\nendmodule\n"))
            && runProcess(git, {"commit", "-q", "-am", "master"}, conflictRepo)
            && runProcess(git, {"merge", "feature"}, conflictRepo) == false;
        check("vcs.screenshot.conflict-fixture", built, conflictRepo);
        if (built) {
            VcsPanel panel;
            panel.setRoot(conflictRepo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            const bool ready = waitUntil(
                [&panel]() { return panel.operation() == GitOperation::Merge; }, 30000);
            check("vcs.screenshot.conflict-state", ready, panel.conflictBannerText());
            if (ready) {
                waitUntil([&panel]() { return panel.fileCount() >= 1; }, 20000);
                panel.selectFile("rtl/a.sv");
                shoot(&panel, QStringLiteral("40-vcs-conflict-state-1500x1000.png"), QSize(1500, 1000));
            }
        }
    }
#endif
#if defined(VEDIT_VCS_HAVE_FFI)
    // batch 5b (AC-5.2): the workspace dialog with a REAL stash and a REAL linked
    // worktree listed. git 1.8.3.1 (the delivery baseline) has no `worktree`
    // command, so the worktree row is only rendered when this git supports it;
    // the stash row and the dialog itself are always rendered.
    if (!git.isEmpty()) {
        const QString workspaceRepo = temp.path() + QStringLiteral("/workspace-ui");
        QDir().mkpath(workspaceRepo + QStringLiteral("/rtl"));
        const bool built =
            writeText(workspaceRepo + QStringLiteral("/rtl/a.sv"),
                      QStringLiteral("module a;\nbase\nendmodule\n"))
            && runProcess(git, {"init", "-q"}, workspaceRepo)
            && runProcess(git, {"config", "user.email", "ui@example.com"}, workspaceRepo)
            && runProcess(git, {"config", "user.name", "VEdit UI"}, workspaceRepo)
            && runProcess(git, {"add", "rtl/a.sv"}, workspaceRepo)
            && runProcess(git, {"commit", "-q", "-m", "workspace ui base"}, workspaceRepo)
            && writeText(workspaceRepo + QStringLiteral("/rtl/a.sv"),
                         QStringLiteral("module a;\nstashed ui change\nendmodule\n"))
            && runProcess(git, {"stash", "save", "ui stash"}, workspaceRepo);
        QString worktreeProbe;
        const bool worktreeSupported =
            built && runProcess(git, {"worktree", "list"}, workspaceRepo, &worktreeProbe);
        const QString linked = temp.path() + QStringLiteral("/linked worktree");
        const bool linkedAdded = worktreeSupported
            && runProcess(git, {"branch", "feature/ui"}, workspaceRepo)
            && runProcess(git, {"worktree", "add", linked, "feature/ui"}, workspaceRepo);
        check("vcs.screenshot.workspace-fixture", built, workspaceRepo);
        if (built) {
            VcsPanel panel;
            panel.setRoot(workspaceRepo.toStdString());
            panel.setSource(std::make_unique<FfiVcsSource>());
            waitUntil([&panel]() { return !panel.headerText().isEmpty(); }, 30000);
            panel.refreshWorkspace();
            const bool listed = waitUntil(
                [&panel, &linkedAdded]() {
                    return panel.stashCount() == 1
                        && (!linkedAdded || panel.worktreeCount() == 2);
                },
                30000);
            check("vcs.screenshot.workspace-listings", listed,
                  QStringLiteral("stashes=%1 worktrees=%2")
                      .arg(panel.stashCount())
                      .arg(panel.worktreeCount()));
            WorkspaceDialog dialog;
            dialog.setStashes(panel.stashes().entries);
            dialog.setWorktrees(panel.worktrees().entries);
            dialog.setFields(QStringLiteral("ui stash message"), linked,
                             QStringLiteral("feature/ui"));
            dialog.setStatusText(panel.workspaceStatusText());
            shoot(&dialog, QStringLiteral("50-vcs-workspace-dialog-1500x1000.png"),
                  QSize(1500, 1000));
            if (linkedAdded) {
                QDir(linked).removeRecursively();
            }
        }
    }
#endif
    // batch 6b (AC-6.3): the SAME three-column widget driven by Subversion — the
    // Subversion verbs on the action bar, the capability line, the grayed actions
    // with their real reasons, and the Staged column gone (no index). The stub
    // render is always available; the real-working-copy render needs the bundled
    // svn tools in the environment.
    {
        auto stub = std::make_unique<StubVcsSource>(svnInfo(), svnStatus(), svnDiffs());
        VcsPanel panel;
        panel.setRoot("/tmp/repo-svn");
        panel.setSource(std::move(stub));
        panel.selectFile("rtl/top.sv");
        shoot(&panel, QStringLiteral("60-vcs-svn-panel-1500x1000.png"), QSize(1500, 1000));
    }
#if defined(VEDIT_VCS_HAVE_FFI)
    {
        const QString svnBin = qEnvironmentVariable("VEDIT_SVN_BIN");
        const QString svnAdmin = qEnvironmentVariable("VEDIT_SVNADMIN");
        if (!svnBin.isEmpty() && !svnAdmin.isEmpty() && QFileInfo(svnBin).isExecutable()
            && QFileInfo(svnAdmin).isExecutable()) {
            QTemporaryDir svnTemp;
            const QString svnRepo = svnTemp.path() + QStringLiteral("/repo");
            const QString svnWc = svnTemp.path() + QStringLiteral("/wc");
            const bool built =
                runProcess(svnAdmin, {"create", svnRepo}, svnTemp.path())
                && runProcess(svnBin, {"checkout", "-q", QStringLiteral("file://") + svnRepo, svnWc},
                              svnTemp.path());
            if (built) {
                QDir().mkpath(svnWc + QStringLiteral("/rtl"));
                writeText(svnWc + QStringLiteral("/rtl/a.sv"),
                          QStringLiteral("module a;\n// svn working copy\nendmodule\n"));
                // `svn add rtl` adds the directory AND its file: adding the file
                // alone while its parent directory is still unversioned fails, and
                // the row would then be the unversioned directory (measured).
                const bool svnFixture =
                    runProcess(svnBin, {"add", "rtl"}, svnWc)
                    && runProcess(svnBin, {"commit", "-q", "-m", "svn ui base"}, svnWc);
                check("vcs.screenshot.svn-fixture", svnFixture, svnWc);
                writeText(svnWc + QStringLiteral("/rtl/a.sv"),
                          QStringLiteral("module a;\n// svn change\nendmodule\n"));
                const QByteArray previousSvn = qgetenv("VEDIT_SVN");
                qputenv("VEDIT_SVN", svnBin.toUtf8());
                VcsPanel panel;
                panel.setRoot(svnWc.toStdString());
                panel.setSource(std::make_unique<FfiVcsSource>());
                const bool ready = waitUntil([&panel]() {
                    return panel.backend() == Backend::Subversion && panel.fileCount() >= 1;
                }, 30000);
                check("vcs.screenshot.svn-real-ready", ready, panel.headerText());
                if (ready) {
                    panel.selectFile("rtl/a.sv");
                    waitUntil([&panel]() {
                        return panel.diffText().contains(QStringLiteral("// svn change"));
                    }, 20000);
                    shoot(&panel, QStringLiteral("61-vcs-svn-real-1500x1000.png"), QSize(1500, 1000));
                }
                if (previousSvn.isEmpty()) {
                    qunsetenv("VEDIT_SVN");
                } else {
                    qputenv("VEDIT_SVN", previousSvn);
                }
            }
        }
    }
#else
    skip("vcs.screenshot.svn-real-ready", "lane built without vd-ffi (stub-only build)");
#endif
    // error state (a stub that reports a failed read, so the state is deterministic)
    {
        auto stub = std::make_unique<StubVcsSource>(gitInfo(), gitStatus(), gitDiffs());
        stub->setHistoryError(HistoryKind::Commits, "fatal: not a git repository (or any parent)");
        VcsHistoryPanel panel;
        panel.setRoot("/tmp/broken-history");
        panel.setSource(std::move(stub));
        shoot(&panel, QStringLiteral("33-vcs-history-error-1500x1000.png"), QSize(1500, 1000));
    }
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    bool requireXcb = false;
    QString screenshotDir;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        } else if (std::strcmp(argv[index], "--require-xcb") == 0) {
            requireXcb = true;
        } else if (std::strcmp(argv[index], "--ui-screenshots") == 0 && index + 1 < argc) {
            screenshotDir = QString::fromLocal8Bit(argv[index + 1]);
            ++index;
        }
    }
    if (!selfcheck && screenshotDir.isEmpty()) {
        std::printf("usage: %s --selfcheck | --ui-screenshots <dir> [--require-xcb]\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    std::printf("WORKSPACE-VCS-SELFTEST start\n");

    if (!screenshotDir.isEmpty()) {
        const int result = runScreenshots(screenshotDir, requireXcb);
        std::printf("WORKSPACE-VCS-SELFTEST screenshots ok=%d checks=%d failures=%d\n",
                    g_failures == 0 ? 1 : 0, g_checks, g_failures);
        std::fflush(stdout);
        return result;
    }

    checkEmptyState();
    checkGitPanel();
    checkDiff();
    checkSvnPanel();
    checkTransitions();
    checkFfi();
    checkReadCompat();
    checkToolchain();
    checkNonBlocking();
    checkThreeColumnLayout();
    checkFourStates();
    checkWriteConfirmations();
    checkRefsAndRefOperations();
    checkGitWriteE2E();
    checkGitRemoteE2E();
    checkModalDialogs();
    checkHistoryStub();
    checkHistoryE2E();
    checkHistoryOpsStub();
    checkConflictStateStub();
    checkHistoryOpsE2E();
    checkWorkspaceStub();
    checkWorkspaceDialog();
    checkWorkspaceE2E();
    checkSvnCapabilities();
    checkSvnE2E();
    checkAutoRefresh();
    checkCloseReopen();
    checkI18n();

    std::printf("WORKSPACE-VCS-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
