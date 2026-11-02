// Headless self-check of the project panel lane (task T6, acceptance AC-S1.3).
//
//   vedit-workspace-project-selftest --selfcheck <fixture-project>
//   vedit-workspace-project-selftest --seed <fixture-project> <state-dir>
//   vedit-workspace-project-selftest --restore <state-dir> [expected-files]
//
// `--selfcheck` runs every in-process check (service round trip, file count and
// list, diagnostics for bad entries, panel rendering, empty state, corrupt
// state file, persistence round trip). `--seed` and `--restore` are meant to be
// run as TWO PROCESSES by the evidence script: the first expands the fixture
// and writes the state file, the second starts from that directory only — that
// is the headless proof of "重启后工程与成员仍在" (a real restart, not a
// same-process round trip).
//
// This machine has no display server: the binary must be run with
// QT_QPA_PLATFORM=offscreen (the ctest entry sets it). It never asserts anything
// about how the widget looks on screen.
#include "project_model.h"
#include "project_panel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

using vedit::workspace::project::ProjectModel;
using vedit::workspace::project::ProjectPanel;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

bool waitForCompletion(ProjectModel &model, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (model.isPending() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return !model.isPending();
}

QStringList readLines(const QString &path)
{
    QStringList out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            out.append(trimmed);
        }
    }
    return out;
}

void writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
}

bool anyContains(const QStringList &values, const QString &needle)
{
    for (const QString &value : values) {
        if (value.contains(needle)) {
            return true;
        }
    }
    return false;
}

int runSelfCheck(const QString &fixture)
{
    if (!QFileInfo(fixture + QStringLiteral("/project.f")).isFile()) {
        std::printf("PROJECT-MODEL-SELFTEST ok=0 reason=fixture-missing fixture=%s\n",
                    qPrintable(fixture));
        return 1;
    }

    const QString fixtureAbsolute = QFileInfo(fixture).absoluteFilePath();
    QTemporaryDir stateDir;
    check("state.temp-dir", stateDir.isValid());

    // 1/6 One live expansion through the C ABI.
    ProjectModel model(stateDir.path());
    QElapsedTimer startTimer;
    startTimer.start();
    model.open(fixture);
    check("service.start.non-blocking", startTimer.elapsed() < 250);
    check("service.start.pending", model.isPending());
    check("service.finishes", waitForCompletion(model, 20000));
    check("files.count", model.files().size() == 7);
    check("files.match-fixture-expectations",
          model.displayFiles() == readLines(fixtureAbsolute + QStringLiteral("/expected/files.txt")));
    check("incdirs.count", model.includeDirs().size() == 4);
    check("defines.count", model.defines().size() == 4);
    check("diagnostics.duplicate-entry",
          anyContains(model.diagnostics(), QStringLiteral("filelist.duplicate_entry")));
    check("diagnostics.unknown-token",
          anyContains(model.diagnostics(), QStringLiteral("filelist.unknown_token")));
    check("diagnostics.name-the-filelist",
          anyContains(model.diagnostics(), QStringLiteral("project.f:")));
    check("summary.states-the-count", model.summary().contains(QStringLiteral("7 files")));
    check("state.file-written", QFile::exists(model.statePath()));

    // 2/6 The widget shows what the model holds (what the user would read).
    ProjectPanel panel(&model);
    panel.show();
    QCoreApplication::processEvents();
    check("panel.summary", panel.summaryText().contains(QStringLiteral("7 files")));
    check("panel.file-count", panel.fileCount() == 7);
    check("panel.files-match-model", panel.displayedFiles() == model.displayFiles());
    check("panel.diagnostics-match", panel.displayedDiagnostics() == model.diagnostics());

    // 3/6 Persistence round trip in one process (the two-process proof is
    // --seed/--restore, run by evidence/project-model/run.sh).
    ProjectModel restored(stateDir.path());
    QString restoreError;
    check("restart.restore", restored.restoreFromDisk(&restoreError));
    check("restart.restore-no-error", restoreError.isEmpty());
    check("restart.files", restored.files() == model.files());
    check("restart.root", restored.projectRoot() == fixtureAbsolute);
    ProjectPanel restoredPanel(&restored);
    check("restart.panel.file-count", restoredPanel.fileCount() == 7);
    check("restart.summary.restored",
          restoredPanel.summaryText().contains(QStringLiteral("restored from last session")));

    // 4/6 A project with bad entries still opens and reports actionable lines.
    QTemporaryDir badTemp;
    check("bad.temp-dir", badTemp.isValid());
    const QString badRoot = badTemp.path() + QStringLiteral("/bad-project");
    writeFile(badRoot + QStringLiteral("/project.f"),
              QStringLiteral("+define+1BAD=1\n+libext+.v\n-f missing.f\nrtl/a.sv\n"));
    writeFile(badRoot + QStringLiteral("/rtl/a.sv"), QStringLiteral("module a; endmodule\n"));
    QTemporaryDir badState;
    ProjectModel bad(badState.path());
    bad.open(badRoot);
    check("bad.finishes", waitForCompletion(bad, 20000));
    check("bad.files", bad.files().size() == 1);
    check("bad.define-invalid",
          anyContains(bad.diagnostics(), QStringLiteral("filelist.define_invalid")));
    check("bad.unknown-token",
          anyContains(bad.diagnostics(), QStringLiteral("filelist.unknown_token")));
    check("bad.missing-filelist",
          anyContains(bad.diagnostics(), QStringLiteral("filelist.missing")));
    check("bad.names-file-and-line",
          anyContains(bad.diagnostics(), QStringLiteral("project.f:")));

    // 5/6 Empty state stays usable.
    QTemporaryDir emptyState;
    ProjectModel empty(emptyState.path());
    check("empty.no-restore", !empty.restoreFromDisk());
    check("empty.no-project", !empty.hasProject());
    ProjectPanel emptyPanel(&empty);
    check("empty.panel.file-count", emptyPanel.fileCount() == 0);
    check("empty.summary", emptyPanel.summaryText() == QStringLiteral("No project open"));

    // 6/6 A corrupt state file is a diagnostic, not a crash.
    QTemporaryDir corruptState;
    writeFile(corruptState.path() + QStringLiteral("/project-model.json"),
              QStringLiteral("{ this is not json"));
    ProjectModel corrupt(corruptState.path());
    QString corruptError;
    check("corrupt.restore-refused", !corrupt.restoreFromDisk(&corruptError));
    check("corrupt.reported", !corruptError.isEmpty());

    std::printf("PROJECT-MODEL-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

int runSeed(const QString &fixture, const QString &stateDir)
{
    ProjectModel model(stateDir);
    model.open(fixture);
    const bool finished = waitForCompletion(model, 20000);
    std::printf("SEED phase=seed ok=%d files=%d root=%s state=%s\n", finished ? 1 : 0,
                static_cast<int>(model.files().size()), qPrintable(model.projectRoot()),
                qPrintable(model.statePath()));
    if (!finished) {
        std::printf("SEED-ERROR %s\n", qPrintable(model.lastError()));
        return 1;
    }
    return model.files().size() == 7 ? 0 : 1;
}

int runRestore(const QString &stateDir, int expectedFiles)
{
    ProjectModel model(stateDir);
    QString error;
    const bool restored = model.restoreFromDisk(&error);
    std::printf("RESTORE phase=restore ok=%d files=%d root=%s error=%s\n", restored ? 1 : 0,
                static_cast<int>(model.files().size()), qPrintable(model.projectRoot()),
                qPrintable(error));
    if (!restored) {
        return 1;
    }
    return model.files().size() == expectedFiles ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--selfcheck")) {
        return runSelfCheck(args.at(2));
    }
    if (args.size() >= 4 && args.at(1) == QStringLiteral("--seed")) {
        return runSeed(args.at(2), args.at(3));
    }
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--restore")) {
        const int expected = args.size() >= 4 ? args.at(3).toInt() : 7;
        return runRestore(args.at(2), expected);
    }
    std::printf("usage: %s --selfcheck <fixture> | --seed <fixture> <state-dir> | "
                "--restore <state-dir> [expected-files]\n",
                qPrintable(args.value(0)));
    return 2;
}
