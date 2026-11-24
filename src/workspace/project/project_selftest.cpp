// Headless self-check of the project panel lane (task T6, acceptance
// AC-2.1–2.5, AC-7.4).
//
//   vedit-workspace-project-selftest --selfcheck <fixture-project>
//   vedit-workspace-project-selftest --seed <fixture-project> <state-dir>
//   vedit-workspace-project-selftest --restore <state-dir> [expected-files]
//
// `--selfcheck` runs every in-process check:
//
//   * opening a folder through the `directoryOpened` seam makes it the ROOT
//     (AC-2.1), and the file set is the `.f` expansion ∪ directory scan (11
//     files for the repository fixture: 7 sources + 4 `.vh` headers);
//   * the panel shows the count, the list, the include dirs, the defines and the
//     `.f` diagnostics (AC-2.2);
//   * importing/removing a `.f` re-expands and persists (AC-2.2/2.4);
//   * `tools.include_dirs`/`tools.defines` reach the model, appear in
//     `backendArguments()` (the exact `-I`/`-D` the backends receive, AC-2.5),
//     and a settings save re-expands without a restart (the directory watch);
//   * a project file row jumps through the process-wide IJumpTarget registry
//     with the ABSOLUTE path and 1-based position (AC-7.4), while the `.f` form
//     (`relativeFiles()`) stays root-relative for portability. The rule that
//     actually fixes slang-server jump URIs is cwd/rootUri = project root; the
//     model exposes that root as one value (`projectRoot()`).
//
// `--seed` and `--restore` are meant to be run as TWO PROCESSES by the evidence
// script: the first expands the fixture with one imported `.f` and one `-I`/`-D`
// pair and writes the state file, the second starts from that directory only —
// that is the headless proof of AC-2.4 (a real restart, not a same-process
// round trip).
//
// This machine has no display server: the binary must be run with
// QT_QPA_PLATFORM=offscreen (the ctest entry sets it). It never asserts anything
// about how the widget looks on screen.
#include "project_filelist.h"
#include "project_model.h"
#include "project_panel.h"
#include "project_subscription.h"
#include "project_variables_dialog.h"

#include "runtime_init.h"

#if defined(VEDIT_PROJECT_HAVE_APP_SHELL)
#include "app_shell.h"
#include "editor_area.h"
#include "project_integration.h"
#endif

#if defined(VEDIT_PROJECT_HAVE_JUMP)
#include "jump/jump_registry.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

// A stand-in for the file tree panel: the subscription seam is name-based, so
// the self-check proves it without the application shell.
class FakeDirectoryOpener : public QObject {
    Q_OBJECT
public:
    void opened(const QString &path)
    {
        emit directoryOpened(path);
    }

signals:
    void directoryOpened(const QString &path);
};

#if defined(VEDIT_PROJECT_HAVE_JUMP)
class RecordingJumpTarget : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++count;
        path = filePath;
        row = line;
        column_ = column;
        return true;
    }

    int count = 0;
    QString path;
    int row = 0;
    int column_ = 0;
};
#endif

using vedit::workspace::project::ProjectModel;
using vedit::workspace::project::ProjectPanel;
using vedit::workspace::project::ProjectVariablesDialog;

namespace {

int g_checks = 0;
int g_failures = 0;
/// batch-20261113 T4: Qt's message handler is a plain function pointer, so the
/// "No such signal" counter it increments has to live at file scope.
int g_noSuchSignalMessages = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

/// A failing assertion that carries the observed value, so a red gate names the
/// state it saw instead of only the check id (batch-20261115: the negative
/// "opening a folder expands nothing" assertion is unreadable without it).
void check(const char *name, bool ok, const QString &detail)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const char *reason)
{
    std::printf("CHECK %s SKIP (%s)\n", name, reason);
    std::fflush(stdout);
}

/// A61.2 (batch-20261115): opening a folder no longer expands it - a project is
/// a folder with an EXPLICITLY imported filelist. Every fixture below carries its
/// own root-level `.f`, so this harness imports what the user would import
/// (Project > Import Filelist...), keeping each expansion assertion intact.
bool openWithFilelists(ProjectModel &model, const QString &root)
{
    model.open(root);
    QStringList lists;
    const QFileInfoList entries =
        QDir(root).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.suffix().compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0) {
            lists.append(entry.absoluteFilePath());
        }
    }
    if (lists.isEmpty()) {
        return true;
    }
    QString error;
    if (!model.importFilelists(lists, &error)) {
        std::printf("IMPORT-ERROR %s\n", qPrintable(error));
        return false;
    }
    return true;
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

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return predicate();
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

// One TOML basic-string body: a raw newline is not allowed there, so the list
// separators the store uses are written as the two characters `\n`.
QString tomlValue(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return value;
}

// One flat settings store, the shape the T10 settings page writes. The save is
// ATOMIC (temp file + rename), like the real store, so the directory watch sees
// an entry change rather than an in-place file modification.
void writeSettings(const QString &stateDirectory, const QString &includeDirs, const QString &defines)
{
    const QString path = stateDirectory + QStringLiteral("/settings.toml");
    const QString temporary = path + QStringLiteral(".tmp");
    writeFile(temporary,
              QStringLiteral("\"tools.include_dirs\" = \"%1\"\n\"tools.defines\" = \"%2\"\n")
                  .arg(tomlValue(includeDirs), tomlValue(defines)));
    QFile::remove(path);
    QFile::rename(temporary, path);
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

bool allRelative(const QStringList &values)
{
    for (const QString &value : values) {
        if (QFileInfo(value).isAbsolute()) {
            return false;
        }
    }
    return true;
}

#if defined(VEDIT_PROJECT_HAVE_APP_SHELL)
// G1-a end to end through the REAL ApplicationShell::fileOpened signal and the
// REAL project lane. The rule under test (contract §10 + AGENT.md "Project mode
// is filelist-gated"): opening a `.f` as a document NEVER imports it.
//
//   1. no project at all + "Open Only"  -> nothing imported, nothing re-rooted;
//   2. no project at all + "Import"     -> the explicit old behaviour (root = the
//      filelist's directory, then the import), which is also what
//      Project ▸ Import Filelist does;
//   3. project ALREADY has a filelist   -> `filelists()` and `files()` are
//      bit-identical before and after; the question is not even asked, even when
//      the injected answer says "Import";
//   4. the Files-tree / Ctrl+P / search-jump / project-panel seam
//      (EditorPane::openFile) obeys the same rule;
//   5. the EXPLICIT import (the panel's Import Filelist action) still imports and
//      its parse diagnostics still reach the panel.
// Check 3 is the regression lock: with the old auto-import it must FAIL.
void runFilelistOpenChecks()
{
    QTemporaryDir temp;
    check("filelist-open.temp-dir", temp.isValid());

    const QString projectDirectory = temp.path() + QStringLiteral("/design");
    QDir().mkpath(projectDirectory);
    const QString projectFile = projectDirectory + QStringLiteral("/top.sv");
    const QString designFilelist = projectDirectory + QStringLiteral("/design.f");
    writeFile(projectFile, QStringLiteral("module top; endmodule\n"));
    writeFile(designFilelist, QStringLiteral("top.sv\n"));

    // A second filelist in another directory: the project is open by then, so it
    // must be imported into the CURRENT root (changing the root would clear the
    // first import through ProjectModel::open()). The spelling is a real filelist
    // extension: `.flist` is NOT one (see the ruling in 6c below).
    const QString otherDirectory = temp.path() + QStringLiteral("/lists");
    QDir().mkpath(otherDirectory);
    const QString secondFilelist = otherDirectory + QStringLiteral("/extra.F");
    writeFile(secondFilelist, projectFile + QLatin1Char('\n'));

    // A broken filelist: the parser diagnostics must reach the project panel,
    // not a modal raised by the app layer.
    const QString badFilelist = otherDirectory + QStringLiteral("/broken.F");
    // `+incdir+` naming a missing directory is a `filelist.missing` diagnostic
    // (a plain missing source entry is deliberately silent: generated files are
    // a normal workflow), so this proves the parse diagnostics reach the panel.
    // It also names one real source the project does NOT have yet, so importing
    // it would move `files()` too - the regression lock below asserts that an
    // OPEN of this filelist moves neither `filelists()` nor `files()`.
    writeFile(otherDirectory + QStringLiteral("/extra_top.sv"),
              QStringLiteral("module extra_top; endmodule\n"));
    writeFile(badFilelist, QStringLiteral("+incdir+no_such_dir\nextra_top.sv\n"));

    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = temp.path() + QStringLiteral("/state");
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    shellOptions.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(shellOptions);
    shell.show();
    QCoreApplication::processEvents();
    vedit::workspace::installProject(shell, vedit::workspace::IntegrationOptions{});
    shell.startSession();
    QCoreApplication::processEvents();

    ProjectModel *model = vedit::workspace::projectModelForShell(shell);
    auto *panel = shell.findChild<ProjectPanel *>();
    check("filelist-open.model", model != nullptr);
    check("filelist-open.panel", panel != nullptr);
    check("filelist-open.starts-without-project", shell.projectRoot().isEmpty());
    if (model == nullptr || panel == nullptr) {
        return;
    }

    // batch-20261113 T4: `subscribeDirectoryOpened` walks the shell's WHOLE
    // QObject tree and binds BY NAME, so the pre-fix code asked Qt for a
    // `directoryOpened(QString)` signal on every object that cannot have one and
    // Qt printed one "QObject::connect: No such signal ..." warning per object
    // (197 per run in the product). Count those warnings across the call.
    {
        g_noSuchSignalMessages = 0;
        const QtMessageHandler previous =
            qInstallMessageHandler([](QtMsgType, const QMessageLogContext &,
                                      const QString &message) {
                if (message.contains(QStringLiteral("No such signal"))) {
                    ++g_noSuchSignalMessages;
                }
            });
        const int subscriptions =
            vedit::workspace::project::subscribeDirectoryOpened(&shell, model);
        qInstallMessageHandler(previous);
        std::printf("T4 directoryOpened.shell-subscriptions=%d no-such-signal-messages=%d\n",
                    subscriptions, g_noSuchSignalMessages);
        std::fflush(stdout);
        // The guard must be SILENT: no object without the signal may be asked
        // for it. (`directoryOpened.subscribed` in runSelfCheck still proves the
        // positive path — FakeDirectoryOpener HAS the signal and binds.)
        check("directoryOpened.no-such-signal-warnings", g_noSuchSignalMessages == 0);
    }

    QStringList opened;
    const QMetaObject::Connection openedConnection =
        QObject::connect(&shell, &vedit::app::ApplicationShell::fileOpened, &shell,
                         [&opened](const QString &path) { opened.append(path); });

    const QString designPath = QFileInfo(designFilelist).absoluteFilePath();
    const QString secondPath = QFileInfo(secondFilelist).absoluteFilePath();
    const QString brokenPath = QFileInfo(badFilelist).absoluteFilePath();
    // A filelist nobody has opened yet: the pane seam below must reach the lane
    // path even though the document is brand new.
    const QString thirdFilelist = otherDirectory + QStringLiteral("/third.F");
    writeFile(thirdFilelist, projectFile + QLatin1Char('\n'));
    const QString thirdPath = QFileInfo(thirdFilelist).absoluteFilePath();
    const auto activePane = [&shell]() -> vedit::app::EditorPane * {
        return shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
    };

    // 0. The unattended answer is "Open Only": with no injected callback and no
    //    interactive dialogs this lane imports nothing and raises no modal, so a
    //    headless run can never widen the project file set by opening a file.
    {
        QTemporaryDir localState;
        ProjectModel bareModel(localState.path());
        ProjectPanel barePanel(&bareModel);
        barePanel.setInteractiveDialogs(false);
        check("filelist-open.noninteractive-open-only",
              barePanel.confirmFilelistImport(designPath)
                  == vedit::workspace::project::FilelistOpenChoice::OpenOnly);
    }

    // 1. No project yet + "Open Only" (the modal's default button): the document
    //    opens as text, the project does NOT appear. Nothing imported, nothing
    //    re-rooted. This is the accident that polluted a real user's file set.
    int openOnlyAsked = 0;
    panel->setFilelistOpenConfirmation([&openOnlyAsked](const QString &) {
        ++openOnlyAsked;
        return vedit::workspace::project::FilelistOpenChoice::OpenOnly;
    });
    QString openError;
    check("filelist-open.open-only-accepted", shell.openPaths({designFilelist}, &openError),
          openError);
    // An import is ASYNCHRONOUS: let any expansion the open started settle, or a
    // bare read of `files()` right here would pass even on the old code.
    check("filelist-open.open-only-settled", waitForCompletion(*model, 20000));
    check("filelist-open.signal", opened == QStringList{designPath}, opened.join(QLatin1Char(',')));
    check("filelist-open.open-only-asked-once", openOnlyAsked == 1,
          QStringLiteral("asked=%1").arg(openOnlyAsked));
    check("filelist-open.open-only-no-filelist", model->filelists().isEmpty(),
          model->filelists().join(QLatin1Char(',')));
    check("filelist-open.open-only-no-files", model->files().isEmpty(),
          model->files().join(QLatin1Char(',')));
    check("filelist-open.open-only-not-a-project",
          !model->hasProject() && model->projectRoot().isEmpty(),
          QStringLiteral("model-root=[%1]").arg(model->projectRoot()));
    check("filelist-open.open-only-root-unchanged", shell.projectRoot().isEmpty(),
          QStringLiteral("shell-root=[%1]").arg(shell.projectRoot()));
    check("filelist-open.open-only-document-opened",
          activePane() != nullptr && activePane()->currentFilePath() == designPath,
          activePane() != nullptr ? activePane()->currentFilePath() : QStringLiteral("no-pane"));
    check("filelist-open.open-only-hint",
          anyContains(shell.diagnostics(), QStringLiteral("Import Filelist")),
          shell.diagnostics().join(QStringLiteral(" | ")));

    // 2. No project yet + "Import": the explicit answer, equivalent to
    //    Project ▸ Import Filelist — the filelist's own directory becomes the
    //    root, then the list is imported by the Rust parser.
    panel->setFilelistOpenConfirmation([](const QString &) {
        return vedit::workspace::project::FilelistOpenChoice::Import;
    });
    check("filelist-open.import-accepted", shell.openPaths({secondFilelist}, &openError),
          openError);
    check("filelist-open.import-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.import-root-is-filelist-directory",
          model->projectRoot() == QFileInfo(otherDirectory).absoluteFilePath()
              && shell.projectRoot() == QFileInfo(otherDirectory).absoluteFilePath(),
          QStringLiteral("model-root=[%1] shell-root=[%2]")
              .arg(model->projectRoot(), shell.projectRoot()));
    check("filelist-open.import-filelist-imported",
          model->filelists() == QStringList{secondPath}, model->filelists().join(QLatin1Char(',')));
    check("filelist-open.import-file-set",
          model->files().contains(QFileInfo(projectFile).absoluteFilePath()),
          model->files().join(QLatin1Char(',')));
    check("filelist-open.import-document-opened",
          activePane() != nullptr && activePane()->currentFilePath() == secondPath,
          activePane() != nullptr ? activePane()->currentFilePath() : QStringLiteral("no-pane"));

    // 3. THE REGRESSION LOCK: a project that ALREADY has a filelist. Opening a
    //    `.f` must change neither `filelists()` nor `files()` — and the question
    //    must not even be asked, so an injected "Import" cannot leak through.
    const QStringList filelistsBefore = model->filelists();
    const QStringList filesBefore = model->files();
    const QString rootBefore = model->projectRoot();
    int existingAsked = 0;
    panel->setFilelistOpenConfirmation([&existingAsked](const QString &) {
        ++existingAsked;
        return vedit::workspace::project::FilelistOpenChoice::Import;
    });
    check("filelist-open.existing-open-through-shell",
          shell.openPaths({badFilelist}, &openError), openError);
    check("filelist-open.existing-settled", waitForCompletion(*model, 20000));
    check("filelist-open.existing-no-question", existingAsked == 0,
          QStringLiteral("asked=%1").arg(existingAsked));
    check("filelist-open.existing-filelists-unchanged", model->filelists() == filelistsBefore,
          QStringLiteral("before=[%1] after=[%2]")
              .arg(filelistsBefore.join(QLatin1Char(',')), model->filelists().join(QLatin1Char(','))));
    check("filelist-open.existing-files-unchanged", model->files() == filesBefore,
          QStringLiteral("before=%1 after=%2")
              .arg(filesBefore.size())
              .arg(model->files().size()));
    check("filelist-open.existing-root-unchanged",
          model->projectRoot() == rootBefore && shell.projectRoot() == rootBefore,
          QStringLiteral("model-root=[%1]").arg(model->projectRoot()));
    // Re-opening the filelist document itself is the same rule (it is already a
    // tab, so only the state is asserted; the signal may not be re-raised).
    check("filelist-open.existing-reopen-same", shell.openPaths({designFilelist}, &openError),
          openError);
    check("filelist-open.existing-reopen-unchanged",
          model->filelists() == filelistsBefore && model->files() == filesBefore,
          QStringLiteral("filelists=%1 files=%2")
              .arg(model->filelists().size())
              .arg(model->files().size()));

    // 4. The Files-tree / Ctrl+P / search-jump / project-panel seam is
    //    EditorPane::openFile, not openPaths: it must obey the same rule.
    vedit::app::EditorPane *pane = activePane();
    check("filelist-open.opened-through-pane",
          pane != nullptr && pane->openFile(thirdFilelist, &openError), openError);
    check("filelist-open.pane-document-opened",
          activePane() != nullptr && activePane()->currentFilePath() == thirdPath,
          activePane() != nullptr ? activePane()->currentFilePath() : QStringLiteral("no-pane"));
    check("filelist-open.pane-no-import",
          model->filelists() == filelistsBefore && model->files() == filesBefore,
          QStringLiteral("filelists=%1 files=%2")
              .arg(model->filelists().size())
              .arg(model->files().size()));

    // 5. The EXPLICIT import action (Project ▸ Import Filelist…) still imports,
    //    and its parse diagnostics still reach the project panel — the existing
    //    surface, never a modal raised from the app layer.
    check("filelist-open.explicit-import", panel->importFilelists({badFilelist}));
    check("filelist-open.explicit-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.explicit-filelist-added",
          model->filelists().contains(brokenPath),
          model->filelists().join(QLatin1Char(',')));
    bool parseDiagnostic = false;
    for (const QString &diagnostic : model->diagnostics()) {
        if (diagnostic.contains(QStringLiteral("filelist."))) {
            parseDiagnostic = true;
        }
    }
    check("filelist-open.parse-diagnostic", parseDiagnostic);
    check("filelist-open.panel-shows-diagnostics",
          !panel->displayedDiagnostics().isEmpty()
              && panel->displayedDiagnostics() == model->diagnostics());
    QObject::disconnect(openedConnection);
}
#endif

} // namespace

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
    const QString extraInclude = stateDir.path() + QStringLiteral("/extra_inc");
    QDir().mkpath(extraInclude);

    // 1. The `directoryOpened` seam (AC-2.1): a folder opened by the File Tree
    //    becomes the root — no --project needed.
    ProjectModel model(stateDir.path());
    ProjectPanel panel(&model);
    panel.show();
    check("empty.no-project", !model.hasProject());
    check("empty.summary", panel.summaryText().contains(QCoreApplication::translate("VEdit", "No project open")));
    check("empty.import-disabled", !panel.importFilelists(QStringList{}));

    FakeDirectoryOpener opener;
    const int subscriptions = vedit::workspace::project::subscribeDirectoryOpened(&opener, &model);
    check("directoryOpened.subscribed", subscriptions == 1);
    QElapsedTimer startTimer;
    startTimer.start();
    opener.opened(fixture);
    check("directoryOpened.non-blocking", startTimer.elapsed() < 250);
    // A61.2 (batch-20261115, supersedes the old "an OPEN is an expansion"):
    // opening a FOLDER is not a project. Nothing is scanned, no service job runs
    // and the file set stays empty until the user imports a filelist. The
    // coverage the old checks carried is kept by the assertions below, which now
    // run after the explicit import.
    check("directoryOpened.no-expansion",
          !model.isPending() && model.files().isEmpty() && !model.hasFilelist(),
          QStringLiteral("pending=%1 files=%2")
              .arg(model.isPending() ? 1 : 0)
              .arg(model.files().size()));
    check("directoryOpened.root", model.projectRoot() == fixtureAbsolute);
    check("directoryOpened.reports-no-filelist",
          panel.summaryText().contains(QStringLiteral("no filelist")), panel.summaryText());
    // The explicit import is what turns the folder into a project (AC-2.2).
    const QString fixtureFilelist = fixtureAbsolute + QStringLiteral("/project.f");
    check("directoryOpened.import", panel.importFilelists({fixtureFilelist}));
    check("directoryOpened.finishes", waitForCompletion(model, 20000));
    check("directoryOpened.panel-visible?",
          panel.summaryText().contains(QStringLiteral("11 files")), panel.summaryText());

    // 2. The file set is the union (AC-2.2): the 7 filelist sources plus the 4
    //    `.vh` headers the directory scan finds.
    const QStringList expectedSources =
        readLines(fixtureAbsolute + QStringLiteral("/expected/files.txt"));
    check("files.count", model.files().size() == 11);
    bool sourcesPresent = !expectedSources.isEmpty();
    for (const QString &source : expectedSources) {
        if (!model.displayFiles().contains(source)) {
            sourcesPresent = false;
        }
    }
    check("files.carry-every-filelist-entry", sourcesPresent);
    check("files.scan-adds-headers",
          model.displayFiles().contains(QStringLiteral("rtl/include/rtl_defs.vh")));
    check("files.relative-for-generated-filelist", allRelative(model.relativeFiles()));
    check("files.relative-matches-display", model.relativeFiles() == model.displayFiles());
    check("incdirs.count", model.includeDirs().size() == 4);
    check("defines.count", model.defines().size() == 4);
    check("diagnostics.duplicate-entry",
          anyContains(model.diagnostics(), QStringLiteral("filelist.duplicate_entry")));
    check("diagnostics.unknown-token",
          anyContains(model.diagnostics(), QStringLiteral("filelist.unknown_token")));
    check("diagnostics.name-the-filelist",
          anyContains(model.diagnostics(), QStringLiteral("project.f:")));
    check("summary.states-the-count", model.summary().contains(QStringLiteral("11 files")));
    check("state.file-written", QFile::exists(model.statePath()));

    // 3/6 The widget shows what the model holds (what the user would read).
    check("panel.summary", panel.summaryText().contains(QStringLiteral("11 files")));
    check("panel.file-count", panel.fileCount() == 11);
    check("panel.files-match-model", panel.displayedFiles() == model.displayFiles());
    check("panel.include-dirs", panel.displayedIncludeDirs() == model.includeDirs());
    check("panel.defines", panel.displayedDefines() == model.defines());
    check("panel.diagnostics-match", panel.displayedDiagnostics() == model.diagnostics());

    // 4. -I/-D from the tools page (AC-2.5): the settings store is read by the
    //    worker, appended to the project's own values, and is exactly what the
    //    backend arguments carry.
    writeSettings(stateDir.path(), extraInclude, QStringLiteral("BOARD=7"));
    model.reloadSettings();
    check("settings.finishes", waitForCompletion(model, 20000));
    check("settings.include-dir-reaches-model", model.includeDirs().contains(extraInclude));
    check("settings.define-reaches-model", model.defines().contains(QStringLiteral("BOARD=7")));
    check("settings.backend-arguments",
          model.backendArguments().contains(QStringLiteral("-I%1").arg(extraInclude))
              && model.backendArguments().contains(QStringLiteral("-DBOARD=7")));
    check("settings.persisted", [&]() {
        QFile file(model.statePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QString text = QString::fromUtf8(file.readAll());
        return text.contains(QStringLiteral("BOARD=7")) && text.contains(extraInclude);
    }());

    // 5. A settings save re-expands without a restart (the Preferences dialog
    //    writes atomically; the model watches the directory).
    const QString secondInclude = stateDir.path() + QStringLiteral("/second_inc");
    QDir().mkpath(secondInclude);
    QFileSystemWatcher probe;
    const bool watchable = probe.addPath(stateDir.path());
    if (watchable) {
        writeSettings(stateDir.path(), extraInclude + QLatin1Char('\n') + secondInclude,
                      QStringLiteral("BOARD=9"));
        std::printf("SETTINGS-WATCH settings=%s exists=%d defines-before=[%s]\n",
                    qPrintable(model.settingsPath()),
                    QFile::exists(model.settingsPath()) ? 1 : 0,
                    qPrintable(model.defines().join(QLatin1Char(','))));
        const bool changed = waitFor(
            [&model]() { return model.defines().contains(QStringLiteral("BOARD=9")); }, 8000);
        std::printf("SETTINGS-WATCH defines-after=[%s] last-error=[%s]\n",
                    qPrintable(model.defines().join(QLatin1Char(','))),
                    qPrintable(model.lastError()));
        check("settings.watch-reloads", changed);
        check("settings.watch-second-include", model.includeDirs().contains(secondInclude));
    } else {
        skip("settings.watch-reloads", "QFileSystemWatcher cannot watch the state directory");
        check("settings.watch-second-include", true);
    }

    // 5b. AC-1.5: the project panel's OWN editable `-I`/`-D` entries. They are
    //     persisted in project-model.json in the user's spelling, validated, and
    //     an edit re-expands immediately (no restart, no manual refresh), so the
    //     new value is in the effective set and in the backend argument
    //     projection the lint/LSP/rename children are built from.
    check("entries.empty-at-start", model.projectIncludeDirs().isEmpty()
                                        && model.projectDefines().isEmpty());
    QString entryError;
    check("entries.reject-empty", !panel.addIncludeDir(QString(), &entryError)
                                      && !entryError.isEmpty());
    entryError.clear();
    check("entries.reject-whitespace", !panel.addIncludeDir(QStringLiteral("   "), &entryError)
                                           && !entryError.isEmpty());
    entryError.clear();
    check("entries.reject-control-character",
          !panel.addIncludeDir(QStringLiteral("inc\nsub"), &entryError)
              && entryError.contains(QStringLiteral("control")));
    entryError.clear();
    check("entries.reject-double-quote",
          !panel.addIncludeDir(QStringLiteral("in\"c"), &entryError)
              && entryError.contains(QStringLiteral("double quote")));
    entryError.clear();
    check("entries.reject-bad-define",
          !panel.addDefine(QStringLiteral("1BAD"), &entryError)
              && entryError.contains(QStringLiteral("identifier")));
    entryError.clear();
    check("entries.reject-empty-define", !panel.addDefine(QStringLiteral(" = "), &entryError));
    entryError.clear();

    // A relative include dir and a define are accepted, kept exactly as typed.
    check("entries.add-include", panel.addIncludeDir(QStringLiteral("panel_inc"), &entryError));
    check("entries.add-define", panel.addDefine(QStringLiteral("PANEL=1")));
    check("entries.typed-spelling", model.projectIncludeDirs() == QStringList{QStringLiteral("panel_inc")}
                                        && model.projectDefines() == QStringList{QStringLiteral("PANEL=1")});
    entryError.clear();
    check("entries.reject-duplicate-include",
          !panel.addIncludeDir(QStringLiteral("./panel_inc"), &entryError)
              && entryError.contains(QStringLiteral("already")));
    entryError.clear();
    check("entries.reject-duplicate-define", !panel.addDefine(QStringLiteral("PANEL=1"), &entryError));

    // Immediate effect: the edit itself started the expansion.
    check("entries.expansion-finishes", waitForCompletion(model, 20000));
    const QString resolvedInclude = QDir::cleanPath(fixtureAbsolute + QStringLiteral("/panel_inc"));
    check("entries.relative-resolved", model.includeDirs().contains(resolvedInclude));
    check("entries.resolved-once", model.includeDirs().count(resolvedInclude) == 1);
    check("entries.backend-arguments",
          model.backendArguments().contains(QStringLiteral("-I%1").arg(resolvedInclude))
              && model.backendArguments().contains(QStringLiteral("-DPANEL=1")));
    check("entries.resolved-projection",
          model.resolvedIncludeDirs() == QStringList{resolvedInclude});
    check("entries.display-keeps-typed-spelling",
          panel.displayedIncludeDirs().contains(QStringLiteral("panel_inc"))
              && panel.displayedDefines().contains(QStringLiteral("PANEL=1")));

    // Persistence (AC-1.5 round trip): project-model.json carries the TYPED
    // spellings, and a fresh model over the same directory loads them back.
    check("entries.state-file-typed", [&]() {
        QFile file(model.statePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QString text = QString::fromUtf8(file.readAll());
        return text.contains(QStringLiteral("\"projectIncludeDirs\""))
            && text.contains(QStringLiteral("\"panel_inc\""))
            && text.contains(QStringLiteral("\"PANEL=1\""));
    }());
    {
        ProjectModel restored(stateDir.path());
        check("entries.restore-round-trip", restored.restoreFromDisk()
                                                && restored.projectIncludeDirs()
                                                       == QStringList{QStringLiteral("panel_inc")}
                                                && restored.projectDefines()
                                                       == QStringList{QStringLiteral("PANEL=1")});
    }

    // Removing the project's own entry takes it out of the next expansion.
    check("entries.remove-include", panel.removeIncludeDir(QStringLiteral("panel_inc")));
    check("entries.remove-finishes", waitForCompletion(model, 20000));
    check("entries.gone-from-effective", !model.includeDirs().contains(resolvedInclude));
    check("entries.remove-define", panel.removeDefine(QStringLiteral("PANEL=1")));
    check("entries.remove-define-finishes", waitForCompletion(model, 20000));
    check("entries.define-gone", !model.defines().contains(QStringLiteral("PANEL=1")));
    // An effective row that the filelist/vedit.toml contributed is not this
    // project's entry: the panel refuses with a reason instead of silently
    // doing nothing.
    if (!model.includeDirs().isEmpty()) {
        check("entries.refuse-foreign-remove",
              !panel.removeIncludeDir(model.includeDirs().first())
                  && !panel.statusText().isEmpty());
    } else {
        skip("entries.refuse-foreign-remove", "the fixture contributes no include dir");
    }

    // E2 tolerant load (G1-e, batch-20261119): a version-4 project-model.json
    // still LOADS (the panel must never be blocked by an old file), but its
    // `files` array is NOT adopted any more: up to version 5 that list could be
    // an `auto_discover` folder scan, so it is dropped and flagged `filesStale`
    // for the next live expansion. Everything the user AUTHORED survives.
    {
        QTemporaryDir oldStateDir;
        check("entries.old-file.temp-dir", oldStateDir.isValid());
        writeFile(oldStateDir.path() + QStringLiteral("/project-model.json"),
                  QStringLiteral("{\n"
                                 "    \"version\": 4,\n"
                                 "    \"projectRoot\": \"/tmp/old-project\",\n"
                                 "    \"capturedAtUtc\": \"2026-11-01T00:00:00Z\",\n"
                                 "    \"filelists\": [\"/tmp/old-project/a.f\"],\n"
                                 "    \"variables\": [\"V=1\"],\n"
                                 "    \"variablesByRoot\": {},\n"
                                 "    \"top\": \"old_top\",\n"
                                 "    \"topByRoot\": {},\n"
                                 "    \"files\": [\"/tmp/old-project/stale-a.sv\",\n"
                                 "                \"/tmp/old-project/stale-b.sv\"],\n"
                                 "    \"includeDirs\": [\"/tmp/inc\"],\n"
                                 "    \"defines\": [\"OLD=1\"],\n"
                                 "    \"diagnostics\": []\n"
                                 "}\n"));
        vedit::workspace::project::ProjectStateStore oldStore(oldStateDir.path());
        bool oldOk = false;
        QString oldError;
        const vedit::workspace::project::ProjectState old = oldStore.load(&oldOk, &oldError);
        check("entries.old-file-loads", oldOk && old.root == QStringLiteral("/tmp/old-project"));
        check("entries.old-file-empty-entries",
              old.projectIncludeDirs.isEmpty() && old.projectDefines.isEmpty());
        check("entries.old-file-keeps-effective",
              old.includeDirs == QStringList{QStringLiteral("/tmp/inc")}
                  && old.defines == QStringList{QStringLiteral("OLD=1")});
        // G1-e: the derived file set is gone and the state says why.
        check("entries.old-file-files-dropped", old.files.isEmpty(),
              QStringLiteral("files=%1").arg(old.files.join(QLatin1Char(','))));
        check("entries.old-file-files-stale", old.filesStale);
        // The AUTHORED inputs are kept, so the next expansion uses the same ones.
        check("entries.old-file-keeps-authored-inputs",
              old.filelists == QStringList{QStringLiteral("/tmp/old-project/a.f")}
                  && old.top == QStringLiteral("old_top")
                  && old.variables == QStringList{QStringLiteral("V=1")});

        // The complement: a file THIS build wrote (version 6) is trusted, so the
        // gate is a version check and not "always stale".
        QString savedError;
        check("entries.current-file-saves", oldStore.save(old, &savedError), savedError);
        const vedit::workspace::project::ProjectState roundTrip = oldStore.load(&oldOk, nullptr);
        QFile versionFile(oldStateDir.path() + QStringLiteral("/project-model.json"));
        int writtenVersion = -1;
        if (versionFile.open(QIODevice::ReadOnly)) {
            writtenVersion = QJsonDocument::fromJson(versionFile.readAll())
                                 .object()
                                 .value(QStringLiteral("version"))
                                 .toInt();
        }
        check("entries.current-file-version",
              writtenVersion == vedit::workspace::project::ProjectState::kCurrentVersion,
              QString::number(writtenVersion));
        check("entries.current-file-trusted", oldOk && !roundTrip.filesStale,
              QStringLiteral("stale=%1").arg(roundTrip.filesStale));
    }

    // G1-e (review, batch-20261119): a version-6 file is NOT trusted on "root
    // matches + files non-empty" alone. The authored-inputs fingerprint must
    // still reproduce: editing the `.f` (the review's exact scenario), changing
    // `[project] auto_discover`, changing a variable, or a file that records no
    // fingerprint at all must all make the persisted file set stale.
    {
        QTemporaryDir fingerprintDir;
        check("entries.fingerprint.temp-dir", fingerprintDir.isValid());
        const QString stateDir = fingerprintDir.filePath(QStringLiteral("state"));
        const QString projectRoot = fingerprintDir.filePath(QStringLiteral("project"));
        QDir().mkpath(projectRoot);
        const QString filelist = QDir(projectRoot).absoluteFilePath(QStringLiteral("sources.f"));
        const QString veditToml = QDir(projectRoot).absoluteFilePath(QStringLiteral("vedit.toml"));
        const QString firstFile = QDir(projectRoot).absoluteFilePath(QStringLiteral("first.sv"));
        const QString secondFile = QDir(projectRoot).absoluteFilePath(QStringLiteral("second.sv"));
        writeFile(filelist, QStringLiteral("first.sv\n"));
        writeFile(firstFile, QStringLiteral("module first; endmodule\n"));
        writeFile(secondFile, QStringLiteral("module second; endmodule\n"));

        vedit::workspace::project::ProjectState state;
        state.root = projectRoot;
        state.filelists = QStringList{filelist};
        state.variables = QStringList{QStringLiteral("V=1")};
        state.variablesByRoot.insert(projectRoot, state.variables);
        state.projectIncludeDirs = QStringList{QStringLiteral("inc")};
        state.projectDefines = QStringList{QStringLiteral("A=1")};
        state.includeDirs = QStringList{QDir(projectRoot).absoluteFilePath(QStringLiteral("inc"))};
        state.defines = QStringList{QStringLiteral("A=1")};
        state.files = QStringList{firstFile};

        vedit::workspace::project::ProjectStateStore store(stateDir);
        QString error;
        check("entries.fingerprint.saves", store.save(state, &error), error);
        bool ok = false;
        const auto loaded = store.load(&ok, &error);
        check("entries.fingerprint.persisted", [&store]() {
            QFile file(store.path());
            if (!file.open(QIODevice::ReadOnly)) {
                return false;
            }
            return QString::fromUtf8(file.readAll())
                .contains(QStringLiteral("\"inputsFingerprint\": \"fnv1a64:"));
        }(), store.path());
        check("entries.fingerprint.trusted",
              ok && !loaded.filesStale && loaded.files == state.files,
              QStringLiteral("stale=%1 files=%2").arg(loaded.filesStale).arg(loaded.files.size()));

        // 1) the review's scenario: the `.f` is edited outside VEdit (a file is
        //    added), so the persisted `files` no longer matches the declaration.
        writeFile(filelist, QStringLiteral("first.sv\nsecond.sv\n"));
        const auto afterEdit = store.load(&ok, &error);
        check("entries.fingerprint.filelist-edit-stale", ok && afterEdit.filesStale,
              QStringLiteral("stale=%1").arg(afterEdit.filesStale));
        check("entries.fingerprint.filelist-edit-drops-files", afterEdit.files.isEmpty(),
              QStringLiteral("files=%1").arg(afterEdit.files.size()));
        check("entries.fingerprint.filelist-edit-keeps-authored",
              afterEdit.filelists == QStringList{filelist}
                  && afterEdit.variables == QStringList{QStringLiteral("V=1")}
                  && afterEdit.projectIncludeDirs == QStringList{QStringLiteral("inc")}
                  && !afterEdit.inputsFingerprint.isEmpty());

        // Undoing the edit makes the SAME state reproducible again: the check
        // hashes the content, it does not blanket-invalidate.
        writeFile(filelist, QStringLiteral("first.sv\n"));
        const auto reverted = store.load(&ok, &error);
        check("entries.fingerprint.reverted-trusted", ok && !reverted.filesStale,
              QStringLiteral("stale=%1").arg(reverted.filesStale));

        // 2) the project's own vedit.toml switch is a fingerprinted input.
        writeFile(veditToml, QStringLiteral("[project]\nauto_discover = true\n"));
        const auto autoChanged = store.load(&ok, &error);
        check("entries.fingerprint.auto-discover-stale",
              ok && autoChanged.filesStale && autoChanged.files.isEmpty(),
              QStringLiteral("stale=%1 files=%2")
                  .arg(autoChanged.filesStale)
                  .arg(autoChanged.files.size()));
        QFile::remove(veditToml);

        // 3) a version-6 file from before this key existed is not trusted.
        QJsonObject withoutKey = loaded.toJson();
        withoutKey.remove(QStringLiteral("inputsFingerprint"));
        writeFile(store.path(), QString::fromUtf8(QJsonDocument(withoutKey).toJson()));
        const auto missingKey = store.load(&ok, &error);
        check("entries.fingerprint.missing-key-stale",
              ok && missingKey.filesStale && missingKey.files.isEmpty(),
              QStringLiteral("stale=%1 files=%2")
                  .arg(missingKey.filesStale)
                  .arg(missingKey.files.size()));

        // 4) the other authored inputs are covered too: keep the recorded
        //    fingerprint but change the variable table.
        QJsonObject changed = loaded.toJson();
        changed.insert(QStringLiteral("variables"),
                       QJsonArray::fromStringList(
                           QStringList{QStringLiteral("V=1"), QStringLiteral("W=2")}));
        writeFile(store.path(), QString::fromUtf8(QJsonDocument(changed).toJson()));
        const auto variableChanged = store.load(&ok, &error);
        check("entries.fingerprint.variables-stale",
              ok && variableChanged.filesStale && variableChanged.files.isEmpty(),
              QStringLiteral("stale=%1 files=%2")
                  .arg(variableChanged.filesStale)
                  .arg(variableChanged.files.size()));
    }

    // AC-7.2 (batch-20261115): choosing the SAME top again is not a silent
    // no-op - it must re-announce itself so the module tree forces its catalog
    // run (the tree may not be running that top after a failed run).
    {
        int stateChanges = 0;
        const QMetaObject::Connection counter =
            QObject::connect(&model, &ProjectModel::stateChanged, &model,
                             [&stateChanges]() { ++stateChanges; });
        model.setTop(QStringLiteral("auto_top_probe"));
        const int afterFirst = stateChanges;
        model.setTop(QStringLiteral("auto_top_probe"));
        QObject::disconnect(counter);
        check("ac7.2.same-top-reannounces", stateChanges > afterFirst,
              QStringLiteral("first=%1 second=%2").arg(afterFirst).arg(stateChanges));
        check("ac7.2.top-persisted", model.top() == QStringLiteral("auto_top_probe"),
              model.top());
        model.setTop(QString());
        check("ac7.2.top-cleared", model.top().isEmpty(), model.top());
    }

    // 6. Import / remove a filelist (AC-2.2), atomic on a bad path.
    const QString importedPath = stateDir.path() + QStringLiteral("/imported.f");
    // An imported `.f` is resolved relative to its OWN directory; an absolute
    // entry that names a file already in the project proves the union de-dupes.
    writeFile(importedPath, fixtureAbsolute + QStringLiteral("/rtl/counter.v\n"));
    check("filelist.import-bad-path", !panel.importFilelists({QStringLiteral("/nonexistent/x.f")}));
    check("filelist.import-bad-path-keeps-list",
          model.filelists() == QStringList{fixtureFilelist});
    check("filelist.import", panel.importFilelists({importedPath}));
    check("filelist.import-finishes", waitForCompletion(model, 20000));
    check("filelist.import-model",
          model.filelists() == QStringList{fixtureFilelist, importedPath});
    check("filelist.import-panel",
          panel.displayedFilelists()
              == QStringList{QStringLiteral("project.f"), importedPath});
    // FIX 4 (F4): re-opening an already-imported `.f` is a no-op in the model;
    // the panel must say so instead of claiming "Imported".
    check("filelist.import-status",
          panel.statusText().contains(QStringLiteral("Imported 1 filelist")));
    check("filelist.reimport-panel-noop", panel.importFilelists({importedPath}));
    check("filelist.reimport-status-honest",
          panel.statusText().contains(QStringLiteral("Already imported")));
    check("filelist.import-no-duplicate", !model.importFilelists({importedPath})
              ? false
              : model.filelists().size() == 2);
    check("filelist.import-keeps-union", model.files().size() == 11);
    check("filelist.remove", panel.removeFilelist(importedPath));
    check("filelist.remove-finishes", waitForCompletion(model, 20000));
    check("filelist.remove-model", model.filelists() == QStringList{fixtureFilelist});
    check("filelist.remove-keeps-union", model.files().size() == 11);
    check("filelist.remove-unknown-rejected", !panel.removeFilelist(importedPath));

    // 6b. FIX 1: the project panel renders paths by the product's ONE display
    //     rule — relative inside the root, absolute outside, and a filelist
    //     under the root is displayed relative while the model keeps it absolute.
    {
        QTemporaryDir displayState;
        const QString displayRoot = displayState.path() + QStringLiteral("/display-project");
        QDir().mkpath(displayRoot + QStringLiteral("/rtl"));
        writeFile(displayRoot + QStringLiteral("/rtl/inside.sv"),
                  QStringLiteral("module inside; endmodule\n"));
        writeFile(displayRoot + QStringLiteral("/top.f"), QStringLiteral("rtl/inside.sv\n"));
        const QString elseWhere = displayState.path() + QStringLiteral("/elsewhere");
        QDir().mkpath(elseWhere);
        writeFile(elseWhere + QStringLiteral("/outside.sv"),
                  QStringLiteral("module outside; endmodule\n"));
        const QString outsideList = elseWhere + QStringLiteral("/outside.f");
        writeFile(outsideList, elseWhere + QStringLiteral("/outside.sv\n"));

        ProjectModel displayModel(displayState.path());
        ProjectPanel displayPanel(&displayModel);
        check("display.open-with-filelist", openWithFilelists(displayModel, displayRoot));
        check("display.files.under-root-finishes", waitForCompletion(displayModel, 20000));
        check("display.files.under-root",
              displayModel.displayFiles().contains(QStringLiteral("rtl/inside.sv")));
        check("display.files.open-key-absolute",
              displayModel.files().contains(
                  QFileInfo(displayRoot + QStringLiteral("/rtl/inside.sv")).absoluteFilePath()));
        check("display.panel.files-under-root",
              displayPanel.displayedFiles().contains(QStringLiteral("rtl/inside.sv")));

        check("display.filelist.outside-import",
              displayModel.importFilelists({outsideList}));
        check("display.filelist.outside-finishes", waitForCompletion(displayModel, 20000));
        check("display.files.outside-absolute",
              displayModel.displayFiles().contains(
                  QFileInfo(elseWhere + QStringLiteral("/outside.sv")).absoluteFilePath()));
        // A61.2: the fixture's own `top.f` is imported through the same action,
        // so the list holds BOTH; the assertion keeps its point (a filelist
        // OUTSIDE the root is displayed ABSOLUTE) and now also pins the root one's
        // RELATIVE spelling in the same list.
        check("display.filelists.outside-absolute",
              displayModel.displayFilelists()
                  == QStringList{QStringLiteral("top.f"),
                                 QFileInfo(outsideList).absoluteFilePath()},
              displayModel.displayFilelists().join(QLatin1Char(',')));
        check("display.panel.filelists-outside-absolute",
              displayPanel.displayedFilelists()
                  == QStringList{QStringLiteral("top.f"),
                                 QFileInfo(outsideList).absoluteFilePath()},
              displayPanel.displayedFilelists().join(QLatin1Char(',')));

        const QString insideList = displayRoot + QStringLiteral("/rtl/inside.f");
        writeFile(insideList, QStringLiteral("inside.sv\n"));
        check("display.filelist.inside-import", displayModel.importFilelists({insideList}));
        check("display.filelist.inside-finishes", waitForCompletion(displayModel, 20000));
        check("display.filelist.inside-absolute-in-model",
              displayModel.filelists().contains(QFileInfo(insideList).absoluteFilePath()));
        check("display.panel.filelist-inside-relative",
              displayPanel.displayedFilelists().contains(QStringLiteral("rtl/inside.f")));
        check("display.panel.files-inside-relative",
              displayPanel.displayedFiles().contains(QStringLiteral("rtl/inside.sv")));
    }

    // 6c. FIX 2: the pure filelist-open decision (no app shell needed).
    {
        const vedit::workspace::project::OpenedFilelistPlan noProject =
            vedit::workspace::project::planOpenedFilelist(
                QStringLiteral("/p/design.f"), /*hasProject=*/false);
        check("filelist.plan.lowercase-f",
              noProject.isFilelist && noProject.needsRoot
                  && noProject.root == QStringLiteral("/p")
                  && noProject.absolutePath == QFileInfo(QStringLiteral("/p/design.f")).absoluteFilePath());
        const auto openProject =
            vedit::workspace::project::planOpenedFilelist(QStringLiteral("/p/DESIGN.F"), true);
        check("filelist.plan.uppercase-f",
              openProject.isFilelist && !openProject.needsRoot && openProject.root.isEmpty());
        // Ruling: `.flist` is not a filelist extension; only `f` case-insensitive,
        // decided by the parser (crates/vd-config/src/project.rs). The planner
        // takes the set from src/common/file_dialog_filters.h, the same source
        // the file dialogs offer, so the long spelling plans as a plain document.
        const auto flist =
            vedit::workspace::project::planOpenedFilelist(QStringLiteral("/p/run.flist"), true);
        check("filelist.plan.flist-not-a-filelist",
              !flist.isFilelist && !flist.needsRoot && flist.absolutePath.isEmpty());
        check("filelist.plan.non-filelist",
              !vedit::workspace::project::planOpenedFilelist(QStringLiteral("/p/top.sv"), false)
                   .isFilelist);
        check("filelist.plan.empty",
              !vedit::workspace::project::planOpenedFilelist(QString(), true).isFilelist);
    }

#if defined(VEDIT_PROJECT_HAVE_APP_SHELL)
    // 6d. FIX 2 end to end through the real shell signal.
    runFilelistOpenChecks();
#endif

    // 7. Double click jumps through the ONE registry (AC-7.4/AC-7.7).
#if defined(VEDIT_PROJECT_HAVE_JUMP)
    RecordingJumpTarget target;
    vedit::workspace::jump::setTarget(&target);
    panel.activateFile(0);
    check("jump.delivered", target.count == 1);
    check("jump.absolute-path", target.path == model.files().value(0));
    check("jump.position-1-based", target.row == 1 && target.column_ == 1);
    vedit::workspace::jump::clearTarget();
#else
    skip("jump.delivered", "this build has no jump lane");
    skip("jump.absolute-path", "this build has no jump lane");
    skip("jump.position-1-based", "this build has no jump lane");
#endif

    // 7b. Three broken `.f` entries are named with file:line and reason in the
    //     panel (AC-2.3) — never silently dropped.
    QTemporaryDir badStateDir;
    const QString badRoot = badStateDir.path() + QStringLiteral("/bad-project");
    const QString badFilelist = badRoot + QStringLiteral("/bad.f");
    writeFile(badFilelist,
              QStringLiteral("ok.sv\n+incdir+no_such_dir\n+libext+.v\n-f missing_nested.f\n"));
    writeFile(badRoot + QStringLiteral("/ok.sv"), QStringLiteral("module ok; endmodule\n"));
    ProjectModel bad(badStateDir.path());
    check("bad.open-with-filelist", openWithFilelists(bad, badRoot));
    check("bad.finishes", waitForCompletion(bad, 20000));
    check("bad.files", bad.files().size() == 1);
    check("bad.incdir-line", anyContains(bad.diagnostics(), badFilelist + QStringLiteral(":2:")));
    check("bad.unknown-line", anyContains(bad.diagnostics(), badFilelist + QStringLiteral(":3:")));
    check("bad.nested-line", anyContains(bad.diagnostics(), badFilelist + QStringLiteral(":4:")));
    check("bad.reasons",
          anyContains(bad.diagnostics(), QStringLiteral("include directory"))
              && anyContains(bad.diagnostics(), QStringLiteral("unsupported filelist token"))
              && anyContains(bad.diagnostics(), QStringLiteral("does not exist")));
    ProjectPanel badPanel(&bad);
    check("bad.panel-shows",
          anyContains(badPanel.displayedDiagnostics(), badFilelist + QStringLiteral(":2:")));

    // 7c. ADDENDUM A-9: variable expansion ($VAR / ${VAR} / $(VAR) / ~), the
    //     project table winning over the environment, two-level relative nested
    //     -f, a reported cycle, the one-dialog collection and the skipped count.
    QTemporaryDir a9State;
    const QString a9Root = a9State.path() + QStringLiteral("/a9-project");
    const QString a9Home = a9State.path() + QStringLiteral("/home");
    for (const char *name : {"a.sv", "b.sv", "c.sv", "mem.sv", "e.sv"}) {
        writeFile(QStringLiteral("%1/rtl/%2").arg(a9Root, QString::fromLatin1(name)),
                  QStringLiteral("module m; endmodule\n"));
    }
    QDir().mkpath(a9Home + QStringLiteral("/a9home"));
    writeFile(a9Home + QStringLiteral("/a9home/d.sv"), QStringLiteral("module d; endmodule\n"));
    writeFile(a9Root + QStringLiteral("/lists/sub.f"), QStringLiteral("-f nested/sub2.f\n"));
    writeFile(a9Root + QStringLiteral("/lists/nested/sub2.f"), QStringLiteral("../../rtl/e.sv\n"));
    writeFile(a9Root + QStringLiteral("/lists/cycle_a.f"), QStringLiteral("-f cycle_b.f\n"));
    writeFile(a9Root + QStringLiteral("/lists/cycle_b.f"), QStringLiteral("-f cycle_a.f\n"));
    writeFile(a9Root + QStringLiteral("/project.f"),
              QStringLiteral("// ADDENDUM A-9 fixture\n"
                             "+incdir+inc\n"
                             "+incdir+no_such_dir\n"
                             "$RTL/a.sv\n"
                             "${RTL}/b.sv\n"
                             "$(RTL)/c.sv\n"
                             "~/a9home/d.sv\n"
                             "rtl/$MISSING_VAR.sv\n"
                             "-f lists/sub.f\n"
                             "-f lists/cycle_a.f\n"
                             "+define+WHICH=$PRECEDENCE\n"));
    qputenv("A9_PRECEDENCE", "environment");
    ProjectModel a9(a9State.path() + QStringLiteral("/state"));
    // Open first (what the File Tree does), then let the one dialog fill the
    // table: the first expansion reports the variables, the second resolves them.
    check("a9.open-with-filelist", openWithFilelists(a9, a9Root));
    check("a9.finishes", waitForCompletion(a9, 20000));
    a9.setVariables({QStringLiteral("RTL=rtl"), QStringLiteral("PRECEDENCE=project")});
    check("a9.expands-with-table", waitForCompletion(a9, 20000));
    check("a9.expands-dollar-var", a9.displayFiles().contains(QStringLiteral("rtl/a.sv")));
    check("a9.expands-braced-var", a9.displayFiles().contains(QStringLiteral("rtl/b.sv")));
    check("a9.expands-parenthesized-var", a9.displayFiles().contains(QStringLiteral("rtl/c.sv")));
    check("a9.expands-tilde",
          a9.files().contains(QDir(qEnvironmentVariable("HOME")).filePath(
              QStringLiteral("a9home/d.sv"))));
    check("a9.two-level-nested-f", a9.displayFiles().contains(QStringLiteral("rtl/e.sv")));
    check("a9.project-table-wins-over-environment",
          a9.defines().contains(QStringLiteral("WHICH=project")));
    check("a9.cycle-names-both-files",
          anyContains(a9.diagnostics(), QStringLiteral("nested -f cycle"))
              && anyContains(a9.diagnostics(), QStringLiteral("cycle_a.f -> "))
              && anyContains(a9.diagnostics(), QStringLiteral("cycle_b.f")));
    check("a9.missing-collected", a9.missingVariables() == QStringList{QStringLiteral("MISSING_VAR")});
    check("a9.skipped-count", a9.skippedCount() >= 2);
    check("a9.missing-file-and-line", anyContains(a9.diagnostics(), QStringLiteral("project.f:8")));
    check("a9.summary-shows-unresolved", a9.summary().contains(QStringLiteral("unresolved")));

    ProjectPanel a9Panel(&a9);
    // Headless: never open the modal batch dialog from a self-check; the dialog
    // is exercised directly as a data probe below.
    a9Panel.setInteractiveDialogs(false);
    a9Panel.show();
    check("a9.panel.variables-shown",
          a9Panel.displayedVariables().contains(QStringLiteral("RTL=rtl")));
    // Headless: the modal dialog is replaced by its data probe (row per name).
    ProjectVariablesDialog a9Dialog(a9.missingVariables(),
                                    ProjectVariablesDialog::parseTable(a9.variables()));
    // RM-3: the dialog now shows the WHOLE table - the requested missing name AND
    // the known ones - because the panel REPLACES the project table with what it
    // returns, and a replace must never drop a variable the user did not see. The
    // A-9 intent (one fillable row for every unresolved name) is unchanged.
    check("a9.dialog.one-row-per-variable", a9Dialog.rowCount() == 3);
    check("a9.dialog.names",
          a9Dialog.names().contains(QStringLiteral("MISSING_VAR"))
              && a9Dialog.names().contains(QStringLiteral("RTL"))
              && a9Dialog.names().contains(QStringLiteral("PRECEDENCE")));
    a9Dialog.setValue(QStringLiteral("MISSING_VAR"), QStringLiteral("mem"));
    check("a9.dialog.variable-table",
          a9Dialog.variableTable().contains(QStringLiteral("MISSING_VAR=mem"))
              && a9Dialog.variableTable().contains(QStringLiteral("RTL=rtl")));
    a9.setVariables(a9.variables() + a9Dialog.variableTable());
    check("a9.re-expands-after-dialog", waitForCompletion(a9, 20000));
    check("a9.dialog-value-used", a9.displayFiles().contains(QStringLiteral("rtl/mem.sv")));
    check("a9.unresolved-cleared", a9.missingVariables().isEmpty());
    check("a9.variables-persisted-in-state", [&]() {
        QFile file(a9.statePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QString text = QString::fromUtf8(file.readAll());
        return text.contains(QStringLiteral("RTL=rtl"))
            && text.contains(QStringLiteral("MISSING_VAR=mem"));
    }());

    // 7d. ADDENDUM A-17: a literal `$` in a path is not a broken variable
    //     reference (before the fix these two lines were hard errors and the
    //     project held one file), `$$` stays the escape, and an expanded path
    //     that does not exist is reported with file:line AND kept.
    QTemporaryDir a17State;
    const QString a17Root = a17State.path() + QStringLiteral("/a17-project");
    const QString a17Outside = a17State.path() + QStringLiteral("/a17-outside");
    writeFile(a17Root + QStringLiteral("/project.f"),
              QStringLiteral("// ADDENDUM A-17 fixture: literal dollars\n"
                             "../a17-outside/out_dollar$.sv\n"
                             "../a17-outside/out_tail.sv$\n"
                             "rtl/ok.sv\n"));
    writeFile(a17Root + QStringLiteral("/rtl/ok.sv"), QStringLiteral("module ok; endmodule\n"));
    writeFile(a17Outside + QStringLiteral("/out_dollar$.sv"),
              QStringLiteral("module out_dollar; endmodule\n"));
    writeFile(a17Outside + QStringLiteral("/out_tail.sv$"),
              QStringLiteral("module out_tail; endmodule\n"));
    ProjectModel a17(a17State.path() + QStringLiteral("/state"));
    check("a17.open-with-filelist", openWithFilelists(a17, a17Root));
    check("a17.finishes", waitForCompletion(a17, 20000));
    check("a17.files", a17.files().size() == 3);
    check("a17.literal-dollar-kept", anyContains(a17.files(), QStringLiteral("out_dollar$.sv")));
    check("a17.literal-trailing-dollar-kept",
          anyContains(a17.files(), QStringLiteral("out_tail.sv$")));
    check("a17.scanned-entry-kept", anyContains(a17.files(), QStringLiteral("rtl/ok.sv")));
    check("a17.no-expansion-error",
          !anyContains(a17.diagnostics(), QStringLiteral("cannot expand this line")));

    // 7e. A-17 escape control: `$$` yields exactly one literal `$` and is not an
    //     expansion that demands the file exist.
    const QString a17EscapeRoot = a17State.path() + QStringLiteral("/a17-escape");
    writeFile(a17EscapeRoot + QStringLiteral("/project.f"),
              QStringLiteral("../a17-escape-outside/escaped$$.sv\n"));
    writeFile(a17State.path() + QStringLiteral("/a17-escape-outside/escaped$.sv"),
              QStringLiteral("module escaped; endmodule\n"));
    ProjectModel a17Escape(a17State.path() + QStringLiteral("/escape-state"));
    check("a17.escape-open-with-filelist", openWithFilelists(a17Escape, a17EscapeRoot));
    check("a17.escape-finishes", waitForCompletion(a17Escape, 20000));
    check("a17.escape-one-dollar",
          a17Escape.files().size() == 1
              && anyContains(a17Escape.files(), QStringLiteral("/escaped$.sv")));

    // 7f. A-17 diagnostics: an expanded path (and a leading `~` that stayed
    //     literal) that does not exist names the path actually looked up; the
    //     entry is kept, so the file set never loses it silently.
    const QString a17MissingRoot = a17State.path() + QStringLiteral("/a17-missing");
    const QString a17MissingList = a17MissingRoot + QStringLiteral("/project.f");
    writeFile(a17MissingList,
              QStringLiteral("$A17_RTL/missing_after_expansion.sv\n"
                             "~nosuchuser/never_home.sv\n"
                             "rtl/ok.sv\n"));
    writeFile(a17MissingRoot + QStringLiteral("/rtl/ok.sv"),
              QStringLiteral("module ok; endmodule\n"));
    ProjectModel a17Missing(a17State.path() + QStringLiteral("/missing-state"));
    check("a17.missing-open-with-filelist", openWithFilelists(a17Missing, a17MissingRoot));
    check("a17.missing-finishes", waitForCompletion(a17Missing, 20000));
    a17Missing.setVariables({QStringLiteral("A17_RTL=rtl")});
    check("a17.missing-re-expands", waitForCompletion(a17Missing, 20000));
    check("a17.missing-files-kept", a17Missing.files().size() == 3);
    check("a17.missing-expanded-reported",
          anyContains(a17Missing.diagnostics(), a17MissingList + QStringLiteral(":1:"))
              && anyContains(a17Missing.diagnostics(),
                             QStringLiteral("missing_after_expansion.sv")));
    check("a17.missing-home-reported",
          anyContains(a17Missing.diagnostics(), a17MissingList + QStringLiteral(":2:"))
              && anyContains(a17Missing.diagnostics(), QStringLiteral("never_home.sv")));
    check("a17.missing-kept-not-dropped",
          anyContains(a17Missing.diagnostics(),
                      QStringLiteral("the path is kept so a generated file is picked up")));
    check("a17.missing-literal-not-reported",
          !anyContains(a17Missing.diagnostics(), a17MissingList + QStringLiteral(":3:")));

    // 8. Restart from the state file only (AC-2.4).
    ProjectModel restored(stateDir.path());
    QString restoreError;
    check("restore.ok", restored.restoreFromDisk(&restoreError));
    check("restore.root", restored.projectRoot() == fixtureAbsolute);
    check("restore.files", restored.files().size() == 11);
    check("restore.include-dirs", !restored.includeDirs().isEmpty());
    // Whatever the live model holds after the settings edits is what the restart
    // must reproduce (AC-2.4) - read it instead of assuming which edit won.
    check("restore.defines", restored.defines() == model.defines());
    check("restore.relative-files", allRelative(restored.relativeFiles()));

    // 9. A root that no longer exists is a visible failure, never an empty tree.
    QTemporaryDir goneState;
    ProjectModel gone(goneState.path());
    gone.open(goneState.path() + QStringLiteral("/moved-away"));
    check("missing-root.finishes", waitForCompletion(gone, 20000));
    check("missing-root.reports", !gone.lastError().isEmpty());
    check("missing-root.diagnostics", anyContains(gone.diagnostics(), QStringLiteral("service:")));
    check("missing-root.no-members", gone.files().isEmpty());

    // 10. A corrupt state file is reported, not fatal.
    QTemporaryDir corruptState;
    writeFile(corruptState.path() + QStringLiteral("/project-model.json"),
              QStringLiteral("this is not json"));
    ProjectModel corrupt(corruptState.path());
    QString corruptError;
    check("corrupt.reports", !corrupt.restoreFromDisk(&corruptError));
    check("corrupt.error-text", !corruptError.isEmpty());

    std::printf("PROJECT-MODEL-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

int runSeed(const QString &fixture, const QString &stateDir)
{
    // One imported `.f` and one tools-page `-I`/`-D` pair: the restart proof has
    // to carry all of them across the process boundary (AC-2.4).
    const QString extraInclude = stateDir + QStringLiteral("/seed_inc");
    QDir().mkpath(extraInclude);
    writeSettings(stateDir, extraInclude, QStringLiteral("BOARD=7\nSIM"));
    const QString importedPath = stateDir + QStringLiteral("/seed.f");
    writeFile(importedPath, QFileInfo(fixture).absoluteFilePath() + QStringLiteral("/rtl/mem.sv\n"));

    ProjectModel model(stateDir);
    model.open(fixture);
    if (!waitForCompletion(model, 20000)) {
        std::printf("SEED-ERROR %s\n", qPrintable(model.lastError()));
        return 1;
    }
    model.setVariables({QStringLiteral("SEED_VAR=seed")});
    QString error;
    if (!model.importFilelists({importedPath}, &error)) {
        std::printf("SEED phase=import ok=0 error=%s\n", qPrintable(error));
        return 1;
    }
    const bool finished = waitForCompletion(model, 20000);
    std::printf(
        "SEED phase=seed ok=%d files=%d filelists=%d incdirs=%d defines=%d root=%s state=%s\n",
        finished ? 1 : 0, static_cast<int>(model.files().size()),
        static_cast<int>(model.filelists().size()), static_cast<int>(model.includeDirs().size()),
        static_cast<int>(model.defines().size()), qPrintable(model.projectRoot()),
        qPrintable(model.statePath()));
    std::printf("SEED backend-arguments %s\n", qPrintable(model.backendArguments()));
    if (!finished) {
        std::printf("SEED-ERROR %s\n", qPrintable(model.lastError()));
        return 1;
    }
    return model.files().size() == 11 && model.filelists().size() == 1
            && model.variables().contains(QStringLiteral("SEED_VAR=seed"))
        ? 0
        : 1;
}

int runRestore(const QString &stateDir, int expectedFiles)
{
    ProjectModel model(stateDir);
    QString error;
    const bool restored = model.restoreFromDisk(&error);
    std::printf(
        "RESTORE phase=restore ok=%d files=%d filelists=%d incdirs=%d defines=%d root=%s error=%s\n",
        restored ? 1 : 0, static_cast<int>(model.files().size()),
        static_cast<int>(model.filelists().size()), static_cast<int>(model.includeDirs().size()),
        static_cast<int>(model.defines().size()), qPrintable(model.projectRoot()),
        qPrintable(error));
    std::printf("RESTORE backend-arguments %s\n", qPrintable(model.backendArguments()));
    if (!restored) {
        return 1;
    }
    const bool ok = model.files().size() == expectedFiles && model.filelists().size() == 1
        && !model.includeDirs().isEmpty() && model.defines().contains(QStringLiteral("SIM"))
        && model.variables().contains(QStringLiteral("SEED_VAR=seed"));
    return ok ? 0 : 1;
}

int runExpansion(const QString &root, const QStringList &variables)
{
    QTemporaryDir state;
    ProjectModel model(state.path());
    model.open(root);
    // A61.2: `--expansion <dir>` means "import this directory's filelists and
    // show me the result", so the CLI imports what the product would import -
    // this is a diagnostic mode, never a product path.
    QStringList discovered;
    const QFileInfoList entries = QDir(root).entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.suffix().compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0) {
            discovered.append(entry.absoluteFilePath());
        }
    }
    if (!discovered.isEmpty()) {
        QString importError;
        if (!model.importFilelists(discovered, &importError)) {
            std::printf("EXPANSION-ERROR %s\n", qPrintable(importError));
            return 1;
        }
    }
    bool finished = waitForCompletion(model, 20000);
    if (!variables.isEmpty()) {
        // The table is set AFTER opening (a different root resets project
        // state); this is also the dialog flow: first expansion collects, then
        // the filled table re-expands.
        model.setVariables(variables);
        finished = waitForCompletion(model, 20000) && finished;
    }
    std::printf("EXPANSION ok=%d root=%s files=%d skipped=%d missing=[%s]\n",
                finished && model.lastError().isEmpty() ? 1 : 0,
                qPrintable(model.projectRoot()), static_cast<int>(model.files().size()),
                model.skippedCount(), qPrintable(model.missingVariables().join(QLatin1Char(','))));
    for (const QString &file : model.displayFiles()) {
        std::printf("FILE %s\n", qPrintable(file));
    }
    for (const QString &dir : model.includeDirs()) {
        std::printf("INCDIR %s\n", qPrintable(dir));
    }
    for (const QString &define : model.defines()) {
        std::printf("DEFINE %s\n", qPrintable(define));
    }
    for (const QString &name : model.missingVariables()) {
        std::printf("MISSING %s\n", qPrintable(name));
    }
    for (const QString &diagnostic : model.diagnostics()) {
        std::printf("DIAG %s\n", qPrintable(diagnostic));
    }
    if (!finished) {
        std::printf("EXPANSION-ERROR %s\n", qPrintable(model.lastError()));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    // T134 / AGENT.md §4: the project lane links the Rust service layer (vd_ffi)
    // through vedit_workspace_project, so install the host's process defaults
    // before Qt and any FFI call.
    vedit_runtime_init();
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--selfcheck")) {
        return runSelfCheck(args.at(2));
    }
    if (args.size() >= 4 && args.at(1) == QStringLiteral("--seed")) {
        return runSeed(args.at(2), args.at(3));
    }
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--restore")) {
        const int expected = args.size() >= 4 ? args.at(3).toInt() : 11;
        return runRestore(args.at(2), expected);
    }
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--expansion")) {
        return runExpansion(args.at(2), args.mid(3));
    }
    std::printf("usage: %s --selfcheck <fixture> | --seed <fixture> <state-dir> | "
                "--restore <state-dir> [expected-files] | --expansion <dir> [NAME=VALUE ...]\n",
                qPrintable(args.value(0)));
    return 2;
}

#include "project_selftest.moc"
