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

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void skip(const char *name, const char *reason)
{
    std::printf("CHECK %s SKIP (%s)\n", name, reason);
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
// FIX 2 end to end: the REAL ApplicationShell::fileOpened signal -> project lane
// -> ProjectModel -> the Rust parser. The shell starts with no project, so the
// first `.f` has to set the root itself; a second filelist (project now open)
// must be imported into the CURRENT root, never by re-opening it.
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
    writeFile(badFilelist, QStringLiteral("+incdir+no_such_dir\n"));

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

    QStringList opened;
    const QMetaObject::Connection openedConnection =
        QObject::connect(&shell, &vedit::app::ApplicationShell::fileOpened, &shell,
                         [&opened](const QString &path) { opened.append(path); });

    // 1. No project open: the filelist's directory becomes the root, then the
    //    filelist is imported (entries parsed by the Rust parser).
    QString openError;
    check("filelist-open.first-open", shell.openPaths({designFilelist}, &openError));
    check("filelist-open.signal",
          opened == QStringList{QFileInfo(designFilelist).absoluteFilePath()});
    check("filelist-open.first-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.root-is-filelist-directory",
          model->projectRoot() == QFileInfo(projectDirectory).absoluteFilePath());
    check("filelist-open.filelist-imported",
          model->filelists() == QStringList{QFileInfo(designFilelist).absoluteFilePath()});
    check("filelist-open.file-set-imported",
          model->files().contains(QFileInfo(projectFile).absoluteFilePath()));
    check("filelist-open.panel-relative",
          panel->displayedFiles().contains(QStringLiteral("top.sv")));
    check("filelist-open.document-tab",
          shell.editorArea() != nullptr && shell.editorArea()->activePane() != nullptr
              && shell.editorArea()->activePane()->currentFilePath()
                     == QFileInfo(designFilelist).absoluteFilePath());

    // 2. Idempotent: opening the same filelist again adds no entry and no file.
    const int filesAfterFirst = model->files().size();
    check("filelist-open.second-open-same", shell.openPaths({designFilelist}, &openError));
    check("filelist-open.same-import-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.idempotent-filelists",
          model->filelists() == QStringList{QFileInfo(designFilelist).absoluteFilePath()});
    check("filelist-open.idempotent-files", model->files().size() == filesAfterFirst);

    // 3. A project IS open: a second filelist elsewhere is imported into the
    //    current root, and the root does not change.
    const QString rootAfterFirst = model->projectRoot();
    check("filelist-open.second-open", shell.openPaths({secondFilelist}, &openError));
    check("filelist-open.second-import-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.second-root-unchanged", model->projectRoot() == rootAfterFirst);
    check("filelist-open.second-filelist-kept",
          model->filelists().size() == 2
              && model->filelists().contains(QFileInfo(secondFilelist).absoluteFilePath()));

    // 4. The Files-tree / Ctrl+P / search-jump / project-panel seam is
    //    EditorPane::openFile, not openPaths: it must reach the same lane path.
    check("filelist-open.opened-through-pane",
          shell.editorArea() != nullptr && shell.editorArea()->activePane() != nullptr
              && shell.editorArea()->activePane()->openFile(badFilelist, &openError));
    check("filelist-open.pane-import-finishes", waitForCompletion(*model, 20000));
    check("filelist-open.pane-filelist-kept",
          model->filelists().contains(QFileInfo(badFilelist).absoluteFilePath()));

    // 5. Parse diagnostics are shown by the PROJECT PANEL (the existing
    //    surface), never by a modal raised from the app layer.
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
    check("directoryOpened.pending", model.isPending());
    check("directoryOpened.finishes", waitForCompletion(model, 20000));
    check("directoryOpened.root", model.projectRoot() == fixtureAbsolute);
    check("directoryOpened.panel-visible?",
          panel.summaryText().contains(QStringLiteral("11 files")));

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

    // 6. Import / remove a filelist (AC-2.2), atomic on a bad path.
    const QString importedPath = stateDir.path() + QStringLiteral("/imported.f");
    // An imported `.f` is resolved relative to its OWN directory; an absolute
    // entry that names a file already in the project proves the union de-dupes.
    writeFile(importedPath, fixtureAbsolute + QStringLiteral("/rtl/counter.v\n"));
    check("filelist.import-bad-path", !panel.importFilelists({QStringLiteral("/nonexistent/x.f")}));
    check("filelist.import-bad-path-keeps-list", model.filelists().isEmpty());
    check("filelist.import", panel.importFilelists({importedPath}));
    check("filelist.import-finishes", waitForCompletion(model, 20000));
    check("filelist.import-model", model.filelists() == QStringList{importedPath});
    check("filelist.import-panel", panel.displayedFilelists() == QStringList{importedPath});
    // FIX 4 (F4): re-opening an already-imported `.f` is a no-op in the model;
    // the panel must say so instead of claiming "Imported".
    check("filelist.import-status",
          panel.statusText().contains(QStringLiteral("Imported 1 filelist")));
    check("filelist.reimport-panel-noop", panel.importFilelists({importedPath}));
    check("filelist.reimport-status-honest",
          panel.statusText().contains(QStringLiteral("Already imported")));
    check("filelist.import-no-duplicate", !model.importFilelists({importedPath})
              ? false
              : model.filelists().size() == 1);
    check("filelist.import-keeps-union", model.files().size() == 11);
    check("filelist.remove", panel.removeFilelist(importedPath));
    check("filelist.remove-finishes", waitForCompletion(model, 20000));
    check("filelist.remove-model", model.filelists().isEmpty());
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
        displayModel.open(displayRoot);
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
        check("display.filelists.outside-absolute",
              displayModel.displayFilelists()
                  == QStringList{QFileInfo(outsideList).absoluteFilePath()});
        check("display.panel.filelists-outside-absolute",
              displayPanel.displayedFilelists()
                  == QStringList{QFileInfo(outsideList).absoluteFilePath()});

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
    bad.open(badRoot);
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
    a9.open(a9Root);
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
    a17.open(a17Root);
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
    a17Escape.open(a17EscapeRoot);
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
    a17Missing.open(a17MissingRoot);
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
