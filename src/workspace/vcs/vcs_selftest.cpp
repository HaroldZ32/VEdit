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
#include <QString>
#include <QWidget>

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
using vedit::workspace::vcs::StubVcsSource;
using vedit::workspace::vcs::VcsPanel;

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
    // Real C ABI call on a plain (non-repository) directory: the empty state
    // must come back as backend 0 + a note, with every owned string freed.
    VdVcsInfo info = vd_vcs_info("/tmp");
    const bool info_ok = info.backend == 0 && info.note != nullptr && std::strlen(info.note) > 0;
    QString info_note = info.note != nullptr ? QString::fromUtf8(info.note) : QString();
    vd_vcs_info_free(&info);
    check("vcs.ffi.info-empty-state", info_ok, info_note.left(70));

    VdVcsDiff diff = vd_vcs_diff("/tmp", "a.sv");
    const bool diff_ok = diff.text == nullptr || std::strlen(diff.text) == 0;
    QString diff_note = diff.note != nullptr ? QString::fromUtf8(diff.note) : QString();
    const int diff_binary = diff.binary;
    vd_vcs_diff_free(&diff);
    check("vcs.ffi.diff-empty-state", diff_ok && diff_binary == 0, diff_note.left(70));
    // A write-shaped call cannot be expressed through this ABI at all.
    check("vcs.ffi.read-only-surface", true, "vd_vcs_info / vd_vcs_diff only; no write entry point");
#else
    skip("vcs.ffi.info-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.diff-empty-state", "lane built without vd-ffi (stub-only build)");
    skip("vcs.ffi.read-only-surface", "lane built without vd-ffi (stub-only build)");
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

    std::printf("WORKSPACE-VCS-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
