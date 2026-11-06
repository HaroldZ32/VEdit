// Batch 1a acceptance probe — the REAL product shell with EVERY workspace lane
// installed (AC-1.1 / AC-1.2). It is NOT the shell selfcheck: --selfcheck returns
// before installPanels(), so it can never observe a panel. This probe builds the
// product shell exactly like src/app/main.cpp does (constructor -> installPanels
// -> startSession), seeds a session archive that names a project root, starts
// WITHOUT --project, and then reads what the four panels really show.
//
// It also carries the batch 1b assertions (AC-1.3 panels recoverable through the
// View menu, AC-1.4 Save As for an untitled tab) — see runPanelsScenario().
//
//   batch1-shell-probe --checks                  batch 1a assertions (offscreen)
//   batch1-shell-probe --screenshots <dir>       batch 1a assertions + PNGs (xcb)
//   batch1-shell-probe --panels-checks           batch 1b assertions (offscreen)
//   batch1-shell-probe --panels-screenshots <dir> batch 1b assertions + PNGs (xcb)
//
// Fixtures live in a QTemporaryDir and are two REAL git working copies on
// distinct branches, so the VCS header can only describe the new root after the
// switch. The search assertions read the panel's own state AND the lane's log
// lines, which is how "the old root is no longer queried" becomes checkable.
#include "app_shell.h"
#include "command_registry.h"
#include "diagnostics/toolchain.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "integration.h"
#include "panel_registry.h"

#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
#include "ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "module_tree_panel.h"
#include "project_model.h"
#include "search_panel.h"
#include "search_types.h"
#include "vcs_panel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScreen>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>
#include <functional>

using vedit::app::ApplicationShell;
using vedit::app::EditorPane;
using vedit::app::PanelRegistry;
using vedit::app::ShellOptions;
using vedit::workspace::FileTreeModel;
using vedit::workspace::FileTreeWidget;
using vedit::workspace::module_tree::ModuleTreePanel;
using vedit::workspace::project::ProjectModel;
using vedit::workspace::search::SearchPanel;
using vedit::workspace::search::SearchState;
using vedit::workspace::vcs::VcsPanel;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *id, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", id,
                detail.isEmpty() ? "" : " — ", qPrintable(detail));
    std::fflush(stdout);
}

void pump(int rounds = 6)
{
    for (int index = 0; index < rounds; ++index) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return predicate();
}

// ---------------------------------------------------------------------------
// log capture (the lanes log the root they act on; AC-1.2 reads those lines)
// ---------------------------------------------------------------------------
QMutex g_logMutex;
QStringList g_log;

void messageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    {
        QMutexLocker locker(&g_logMutex);
        g_log.append(message);
    }
    std::fprintf(stderr, "%s\n", qPrintable(message));
    std::fflush(stderr);
}

int logMark()
{
    QMutexLocker locker(&g_logMutex);
    return g_log.size();
}

QStringList logSince(int mark)
{
    QMutexLocker locker(&g_logMutex);
    return g_log.mid(mark);
}

// ---------------------------------------------------------------------------
// filesystem + git helpers
// ---------------------------------------------------------------------------
bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

bool appendLine(const QString &path, const QByteArray &line, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot append to %1").arg(path);
        }
        return false;
    }
    file.write(line);
    return true;
}

bool runGit(const QString &directory, const QStringList &arguments, QString *error)
{
    QProcess process;
    process.setProgram(QStringLiteral("git"));
    const QStringList baseArguments{QStringLiteral("-c"), QStringLiteral("user.name=VEdit Probe"),
                                    QStringLiteral("-c"),
                                    QStringLiteral("user.email=probe@example.invalid"),
                                    QStringLiteral("-C"), directory};
    process.setArguments(baseArguments + arguments);
    process.start();
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("git %1 in %2 failed: %3 %4")
                         .arg(arguments.join(QLatin1Char(' ')), directory,
                              QString::fromUtf8(process.readAllStandardError()),
                              process.exitCode() == 0 ? QStringLiteral("(timeout)")
                                                      : QString::number(process.exitCode()));
        }
        return false;
    }
    return true;
}

struct Fixture {
    QString rootA;
    QString rootB;
};

const QByteArray kAlphaSource =
    "module alpha_child;\n"
    "endmodule\n"
    "\n"
    "module alpha_top;\n"
    "  alpha_child u_child ();\n"
    "endmodule\n"
    "// TOKEN_ALPHA_1 lives only in the alpha project\n";

const QByteArray kBetaSource =
    "module beta_child;\n"
    "endmodule\n"
    "\n"
    "module beta_top;\n"
    "  beta_child u_child ();\n"
    "endmodule\n"
    "// TOKEN_BETA_2 lives only in the beta project\n";

bool buildFixture(const QString &base, Fixture *fixture, QString *error)
{
    fixture->rootA = base + QStringLiteral("/alpha-project");
    fixture->rootB = base + QStringLiteral("/beta-project");
    const QByteArray alphaExtra = "module alpha_second;\nendmodule\n// TOKEN_ALPHA_2\n";
    if (!writeFile(fixture->rootA + QStringLiteral("/rtl/top.sv"), kAlphaSource)
        || !writeFile(fixture->rootA + QStringLiteral("/rtl/second.v"), alphaExtra)
        || !writeFile(fixture->rootB + QStringLiteral("/rtl/other.sv"), kBetaSource)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write the fixture sources");
        }
        return false;
    }
    // Two real working copies on DISTINCT branches, each with one uncommitted
    // change, so the panel header differs in branch AND change count after the
    // switch (the zero-cost way to read "this is another root" from the UI).
    for (const QString &root : {fixture->rootA, fixture->rootB}) {
        const QString branch = QFileInfo(root).fileName();
        if (!runGit(root, {QStringLiteral("init"), QStringLiteral("--quiet")}, error)
            || !runGit(root, {QStringLiteral("checkout"), QStringLiteral("--quiet"),
                              QStringLiteral("-b"), branch},
                       error)
            || !runGit(root, {QStringLiteral("add"), QStringLiteral(".")}, error)
            || !runGit(root, {QStringLiteral("commit"), QStringLiteral("--quiet"),
                              QStringLiteral("-m"), QStringLiteral("fixture")},
                       error)) {
            return false;
        }
    }
    if (!appendLine(fixture->rootA + QStringLiteral("/rtl/second.v"),
                    QByteArray("// uncommitted alpha change\n"), error)
        || !appendLine(fixture->rootB + QStringLiteral("/rtl/other.sv"),
                       QByteArray("// uncommitted beta change\n"), error)) {
        return false;
    }
    return true;
}

bool seedSession(const QString &stateDir, const QString &projectRoot)
{
    QDir().mkpath(stateDir);
    QJsonObject archive;
    archive.insert(QStringLiteral("version"), 2);
    archive.insert(QStringLiteral("project"), projectRoot);
    // The panels the ORDINARY start restores. They are created during
    // startSession() — i.e. through the factories installPanels() registered with
    // whatever root the shell resolved before any lane existed.
    archive.insert(QStringLiteral("panels"),
                   QJsonArray{QStringLiteral("file-tree"), QStringLiteral("search"),
                              QStringLiteral("vcs"), QStringLiteral("module-tree")});
    QFile file(stateDir + QStringLiteral("/session.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size();
}

// ---------------------------------------------------------------------------
// panel lookups (typed: the probe links the product's own lane libraries)
// ---------------------------------------------------------------------------
FileTreeWidget *fileTreeOf(ApplicationShell &shell)
{
    return shell.findChild<FileTreeWidget *>();
}

SearchPanel *searchPanelOf(ApplicationShell &shell)
{
    return shell.findChild<SearchPanel *>();
}

VcsPanel *vcsPanelOf(ApplicationShell &shell)
{
    return shell.findChild<VcsPanel *>();
}

QLabel *vcsHeaderOf(ApplicationShell &shell)
{
    auto *panel = vcsPanelOf(shell);
    return panel != nullptr ? panel->findChild<QLabel *>(QStringLiteral("vcs-header")) : nullptr;
}

ModuleTreePanel *moduleTreeOf(ApplicationShell &shell)
{
    return shell.findChild<ModuleTreePanel *>();
}

QObject *moduleTreeHolderOf(ApplicationShell &shell)
{
    return shell.findChild<QObject *>(QStringLiteral("module-tree-holder"));
}

ProjectModel *projectModelOf(ApplicationShell &shell)
{
    return shell.findChild<ProjectModel *>();
}

QString vcsHeaderText(ApplicationShell &shell)
{
    QLabel *header = vcsHeaderOf(shell);
    return header != nullptr ? header->text() : QStringLiteral("<missing header>");
}

QString vcsRootProperty(ApplicationShell &shell)
{
    auto *panel = vcsPanelOf(shell);
    return panel != nullptr ? panel->property("veditProjectRoot").toString() : QString();
}

QString moduleTreePollRoot(ApplicationShell &shell)
{
    QObject *holder = moduleTreeHolderOf(shell);
    return holder != nullptr ? holder->property("projectRoot").toString() : QString();
}

QString fileTreeRootText(ApplicationShell &shell)
{
    auto *tree = fileTreeOf(shell);
    return tree != nullptr ? tree->rootPath() : QStringLiteral("<missing tree>");
}

bool searchHitsInside(SearchPanel *panel, const QString &root)
{
    for (const auto &hit : panel->hits()) {
        if (!hit.filePath.startsWith(root + QLatin1Char('/'))) {
            return false;
        }
    }
    return true;
}

void grab(ApplicationShell &shell, const QString &path)
{
    const bool ok = shell.grab().save(path);
    std::printf("%s SCREENSHOT %s\n", ok ? "PASS" : "FAIL", qPrintable(path));
    if (!ok) {
        ++g_failures;
    }
    ++g_checks;
    std::fflush(stdout);
}

bool waitForSearch(SearchPanel *panel, int timeoutMs = 30000)
{
    return waitFor([panel]() { return panel != nullptr && !panel->isSearching(); }, timeoutMs);
}


// ---------------------------------------------------------------------------
// batch 1b: panels recoverable through View (AC-1.3) + Save As (AC-1.4)
// ---------------------------------------------------------------------------

QMenu *menuContaining(QMenuBar *bar, QAction *action)
{
    if (bar == nullptr || action == nullptr) {
        return nullptr;
    }
    for (QAction *topLevel : bar->actions()) {
        QMenu *menu = topLevel != nullptr ? topLevel->menu() : nullptr;
        if (menu != nullptr && menu->actions().contains(action)) {
            return menu;
        }
    }
    return nullptr;
}

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QByteArray readRaw(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// The archived editor layout, as the session store wrote it. Used to assert that
// a Save As really changed the path the NEXT start will open (AC-1.4).
QString archivedDocumentPath(const QString &sessionPath, const QString &needle)
{
    QJsonObject archive;
    {
        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        archive = QJsonDocument::fromJson(file.readAll()).object();
    }
    const QJsonObject layout = archive.value(QStringLiteral("editorLayout")).toObject();
    const QJsonArray documents = layout.value(QStringLiteral("documents")).toArray();
    for (const QJsonValue &value : documents) {
        const QString path = value.toObject().value(QStringLiteral("path")).toString();
        if (path.endsWith(needle)) {
            return path;
        }
    }
    // Layout v2 nests leaves under "root"; walk it too.
    const auto walk = [&needle](const QJsonObject &node, const auto &self) -> QString {
        const QJsonArray nested = node.value(QStringLiteral("documents")).toArray();
        for (const QJsonValue &value : nested) {
            const QString path = value.toObject().value(QStringLiteral("path")).toString();
            if (path.endsWith(needle)) {
                return path;
            }
        }
        for (const QJsonValue &child : node.value(QStringLiteral("children")).toArray()) {
            const QString found = self(child.toObject(), self);
            if (!found.isEmpty()) {
                return found;
            }
        }
        return QString();
    };
    return walk(layout.value(QStringLiteral("root")).toObject(), walk);
}

int runPanelsScenario(const QString &screenshotDirectory)
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("BATCH1B-PROBE ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString work = temp.path();
    const QString projectRoot = work + QStringLiteral("/panel-project");
    QDir().mkpath(projectRoot + QStringLiteral("/rtl"));
    if (!writeFile(projectRoot + QStringLiteral("/rtl/panel.sv"),
                   QByteArray("module panel_top;\nendmodule\n// TOKEN_PANEL_1\n"))) {
        std::printf("BATCH1B-PROBE ok=0 reason=fixture\n");
        return 1;
    }
    const QString stateDir = work + QStringLiteral("/state");
    std::printf("PROBE-PANELS work=%s platform=%s\n", qPrintable(work),
                qPrintable(QApplication::platformName()));

    QString restartError;
    {
        // ---- start 1: lanes install, session starts -------------------------
        ShellOptions options;
        options.stateDir = stateDir;
        options.projectRoot = projectRoot; // a root so the PROJECT dock exists
        options.restoreSession = true;
        options.deferSessionRestore = true;
        auto shell = std::make_unique<ApplicationShell>(options);
        vedit::workspace::installPanels(*shell, {shell->projectRoot(), options.stateDir});
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
        // The product provider: the Save As assertions below must exercise the
        // real KTextEditor document, not the demo placeholder.
        shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
#endif
        shell->startSession();
        shell->resize(1500, 1000);
        shell->show();
        pump(10);

        // ---- AC-1.3: both closed panels come back from the View menu --------
        QAction *moduleTreeToggle =
            shell->commands()->action(QStringLiteral("view.panel.module-tree"));
        QDockWidget *moduleTreeDock = shell->panels()->dock(QStringLiteral("module-tree"));
        check("a13.module-tree.command", moduleTreeToggle != nullptr);
        check("a13.module-tree.in-view-menu",
              menuContaining(shell->menuBar(), moduleTreeToggle) != nullptr);
        QDockWidget *projectDock = shell->findChild<QDockWidget *>(QStringLiteral("dock-project"));
        check("a13.project.dock-exists", projectDock != nullptr);
        QAction *projectToggle =
            projectDock != nullptr
                ? shell->commands()->action(QStringLiteral("view.dock.") + projectDock->objectName())
                : nullptr;
        check("a13.project.toggle", projectToggle != nullptr,
              projectDock != nullptr ? projectDock->objectName() : QStringLiteral("<no dock>"));
        check("a13.project.toggle-in-view-menu",
              menuContaining(shell->menuBar(), projectToggle) != nullptr);
        if (moduleTreeDock != nullptr && projectDock != nullptr && moduleTreeToggle != nullptr
            && projectToggle != nullptr) {
            // The user closes both with the dock's own X button.
            moduleTreeDock->close();
            projectDock->close();
            pump();
            check("a13.closed.both-hidden",
                  !moduleTreeDock->isVisible() && !projectDock->isVisible());
            check("a13.closed.action-unchecked",
                  !moduleTreeToggle->isChecked() && !projectToggle->isChecked());
            moduleTreeToggle->trigger();
            projectToggle->trigger();
            pump();
            check("a13.reopened.visible",
                  moduleTreeDock->isVisible() && projectDock->isVisible());
            check("a13.reopened.action-checked",
                  moduleTreeToggle->isChecked() && projectToggle->isChecked());
        }
        if (!screenshotDirectory.isEmpty()) {
            QDir().mkpath(screenshotDirectory);
            QMenu *viewMenu = menuContaining(shell->menuBar(), moduleTreeToggle);
            if (viewMenu != nullptr) {
                viewMenu->popup(shell->mapToGlobal(QPoint(40, 50)));
                pump(10);
                const QPixmap shot = QGuiApplication::primaryScreen()->grabWindow(0);
                const QString path = screenshotDirectory + QStringLiteral("/30-view-menu-panels.png");
                const bool ok = !shot.isNull() && shot.save(path);
                check("a13.screenshot.view-menu", ok, path);
                viewMenu->hide();
                pump();
            }
        }

        // ---- AC-1.4: Save As for an untitled tab ----------------------------
        EditorPane *pane = shell->editorArea() != nullptr ? shell->editorArea()->activePane()
                                                          : nullptr;
        check("a14.pane.available", pane != nullptr);
        if (pane != nullptr) {
            check("a14.untitled.start", pane->currentFilePath().isEmpty(),
                  pane->currentFilePath());
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
            KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
            check("a14.editor.is-ktexteditor", view != nullptr);
            if (view != nullptr && view->document() != nullptr) {
                view->document()->setText(QStringLiteral("module saved_by_save_as;\nendmodule\n"));
                view->document()->setModified(true);
            }
#endif
            const QString target = work + QStringLiteral("/saved-as.sv");

            // (a) Ctrl+S on an untitled document IS the Save As entry point.
            QAction *save = shell->commands()->action(QStringLiteral("file.save"));
            QAction *saveAs = shell->commands()->action(QStringLiteral("file.save-as"));
            check("a14.save-as.command", saveAs != nullptr);
            check("a14.save-as.shortcut",
                  saveAs != nullptr
                      && saveAs->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                  saveAs != nullptr ? saveAs->shortcut().toString() : QStringLiteral("<none>"));
            check("a14.save-as.in-file-menu",
                  menuContaining(shell->menuBar(), saveAs) != nullptr);
            // Ctrl+S is bound to the action the untitled path goes through, so the
            // keyboard entry point and the menu entry are the same code path.
            check("a14.ctrl-s.bound",
                  save != nullptr
                      && save->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")),
                  save != nullptr ? save->shortcut().toString() : QStringLiteral("<none>"));
            shell->setSaveAsDialogProvider([target](const QString &) { return target; });
            save->trigger();
            pump();
            const QByteArray written = readRaw(target);
            check("a14.ctrl-s.wrote-file", !written.isEmpty(),
                  QStringLiteral("%1 bytes").arg(written.size()));
            check("a14.ctrl-s.content",
                  QString::fromUtf8(written)
                      == QStringLiteral("module saved_by_save_as;\nendmodule\n"),
                  QString::fromUtf8(written).left(60));
            check("a14.ctrl-s.tab-path", pane->currentFilePath() == target,
                  pane->currentFilePath());
            check("a14.ctrl-s.tab-title",
                  pane->tabText(pane->activeTabIndex()).contains(QStringLiteral("saved-as.sv")),
                  pane->tabText(pane->activeTabIndex()));
            check("a14.ctrl-s.not-dirty", !pane->isActiveTabModified(),
                  pane->tabText(pane->activeTabIndex()));
            // The session archive must carry the new path (AC-1.4 "会话路径更新").
            QString saveError;
            check("a14.ctrl-s.session-saved", shell->saveSession(&saveError), saveError);
            const QString archived =
                archivedDocumentPath(stateDir + QStringLiteral("/session.json"),
                                     QStringLiteral("saved-as.sv"));
            check("a14.ctrl-s.session-path", archived == target, archived);

            // (b) Cancelling the dialog writes nothing and keeps the identity.
            EditorPane *second = shell->editorArea()->splitActive(Qt::Horizontal);
            check("a14.second-pane", second != nullptr);
            if (second != nullptr) {
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
                KTextEditor::View *secondView = vedit::editor::viewFor(second->editorWidget());
                if (secondView != nullptr && secondView->document() != nullptr) {
                    secondView->document()->setText(QStringLiteral("module second;\nendmodule\n"));
                    secondView->document()->setModified(true);
                }
#endif
                const QString cancelledTarget = work + QStringLiteral("/cancelled.sv");
                shell->setSaveAsDialogProvider([cancelledTarget](const QString &) {
                    Q_UNUSED(cancelledTarget);
                    return QString(); // the user pressed Cancel
                });
                saveAs->trigger();
                pump();
                check("a14.cancel.no-file", !QFileInfo::exists(cancelledTarget));
                check("a14.cancel.path-unchanged", second->currentFilePath().isEmpty(),
                      second->currentFilePath());
                check("a14.cancel.still-dirty", second->isActiveTabModified(),
                      second->tabText(second->activeTabIndex()));

                // (c) An existing target asks first: declining leaves it byte-identical,
                //     accepting replaces it.
                const QString existing = work + QStringLiteral("/existing.sv");
                writeFile(existing, QByteArray("// OLD CONTENT\n"));
                shell->setSaveAsDialogProvider([existing](const QString &) { return existing; });
                shell->setSaveAsOverwriteConfirmProvider([](const QString &) { return false; });
                saveAs->trigger();
                pump();
                check("a14.overwrite.declined-content",
                      readRaw(existing) == QByteArray("// OLD CONTENT\n"),
                      QString::fromUtf8(readRaw(existing)));
                check("a14.overwrite.declined-path-unchanged",
                      second->currentFilePath().isEmpty(), second->currentFilePath());
                shell->setSaveAsOverwriteConfirmProvider([](const QString &) { return true; });
                saveAs->trigger();
                pump();
                check("a14.overwrite.accepted-content",
                      QString::fromUtf8(readRaw(existing))
                          == QStringLiteral("module second;\nendmodule\n"),
                      QString::fromUtf8(readRaw(existing)).left(60));
                check("a14.overwrite.accepted-path", second->currentFilePath() == existing,
                      second->currentFilePath());
            }

            // (d) The REAL product dialog still opens (the seam above only replaces
            //     it for the assertions): a dialog appears and rejecting it writes
            //     nothing. Under xcb this is the Qt widget dialog, headless it is the
            //     same widget — a timer rejects it in both cases.
            if (pane != nullptr) {
                EditorPane *third = shell->editorArea()->splitActive(Qt::Vertical);
                if (third != nullptr) {
                    const QString realTarget = work + QStringLiteral("/real-dialog.sv");
                    shell->setSaveAsDialogProvider({}); // back to QFileDialog
                    bool dialogSeen = false;
                    QTimer::singleShot(700, QCoreApplication::instance(), [&dialogSeen]() {
                        for (QWidget *widget : QApplication::topLevelWidgets()) {
                            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                                dialogSeen = true;
                                dialog->reject();
                            }
                        }
                    });
                    saveAs->trigger();
                    pump(10);
                    check("a14.real-dialog.opened", dialogSeen);
                    check("a14.real-dialog.no-write", !QFileInfo::exists(realTarget));
                }
            }
        }

        // ---- restart: the same actions must exist and still reopen ----------
        QString saveError;
        if (moduleTreeDock != nullptr && projectDock != nullptr) {
            moduleTreeDock->close();
            projectDock->close();
            pump();
        }
        check("a13.restart.session-saved", shell->saveSession(&saveError), saveError);
    }

    {
        ShellOptions options;
        options.stateDir = stateDir;
        options.projectRoot = projectRoot;
        options.restoreSession = true;
        options.deferSessionRestore = true;
        auto shell = std::make_unique<ApplicationShell>(options);
        vedit::workspace::installPanels(*shell, {shell->projectRoot(), options.stateDir});
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
        shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
#endif
        shell->startSession();
        shell->show();
        pump(10);
        QAction *moduleTreeToggle =
            shell->commands()->action(QStringLiteral("view.panel.module-tree"));
        QDockWidget *moduleTreeDock = shell->panels()->dock(QStringLiteral("module-tree"));
        QDockWidget *projectDock = shell->findChild<QDockWidget *>(QStringLiteral("dock-project"));
        QAction *projectToggle =
            projectDock != nullptr
                ? shell->commands()->action(QStringLiteral("view.dock.") + projectDock->objectName())
                : nullptr;
        check("a13.restart.commands-exist", moduleTreeToggle != nullptr && projectToggle != nullptr);
        // Informational (not a check): what the restore left visible. The project
        // lane re-asserts its own dock's visibility from the async expansion
        // result, which is its behaviour and outside this task's write scope.
        std::printf("INFO a13.restart.visibility module-tree=%d project=%d\n",
                    moduleTreeDock != nullptr && moduleTreeDock->isVisible() ? 1 : 0,
                    projectDock != nullptr && projectDock->isVisible() ? 1 : 0);
        if (moduleTreeToggle != nullptr && projectToggle != nullptr && moduleTreeDock != nullptr
            && projectDock != nullptr) {
            // AC-1.3's requirement, exercised in the order the user does it after
            // the restart: close the panel, then bring it back from View.
            moduleTreeDock->close();
            projectDock->close();
            pump();
            check("a13.restart.closed-hidden",
                  !moduleTreeDock->isVisible() && !projectDock->isVisible());
            moduleTreeToggle->trigger();
            projectToggle->trigger();
            pump();
            check("a13.restart.reopened",
                  moduleTreeDock->isVisible() && projectDock->isVisible());
        }
        if (!screenshotDirectory.isEmpty()) {
            QDir().mkpath(screenshotDirectory);
            const QString path =
                screenshotDirectory + QStringLiteral("/31-panels-restored.png");
            const bool ok = shell->grab().save(path);
            check("a13.screenshot.panels-restored", ok, path);
        }
    }

    // The demo provider takes the SAME path: Save As must not depend on which
    // provider is installed (a headless shell or a lane without edit-core).
    {
        ShellOptions options;
        options.stateDir = work + QStringLiteral("/demo-state");
        options.projectRoot = projectRoot;
        options.restoreSession = false;
        auto shell = std::make_unique<ApplicationShell>(options);
        shell->show();
        pump();
        EditorPane *pane = shell->editorArea() != nullptr ? shell->editorArea()->activePane()
                                                          : nullptr;
        auto *text = pane != nullptr ? qobject_cast<QPlainTextEdit *>(pane->editorWidget())
                                     : nullptr;
        check("a14.demo.editor", text != nullptr);
        if (text != nullptr && pane != nullptr) {
            check("a14.demo.untitled", pane->currentFilePath().isEmpty(),
                  pane->currentFilePath());
            text->setPlainText(QStringLiteral("demo content\n"));
            text->document()->setModified(true);
            const QString target = work + QStringLiteral("/demo-saved.sv");
            shell->setSaveAsDialogProvider([target](const QString &) { return target; });
            if (QAction *saveAs = shell->commands()->action(QStringLiteral("file.save-as"))) {
                saveAs->trigger();
            }
            pump();
            check("a14.demo.wrote", readRaw(target) == QByteArray("demo content\n"),
                  QString::fromUtf8(readRaw(target)));
            check("a14.demo.tab-path", pane->currentFilePath() == target,
                  pane->currentFilePath());
            check("a14.demo.not-dirty", !pane->isActiveTabModified(),
                  pane->tabText(pane->activeTabIndex()));
        }
    }

    std::printf("BATCH1B-PROBE ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0, g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("batch1-shell-probe"));

    QString screenshotDirectory;
    QString mode = QStringLiteral("--checks");
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--screenshots") && index + 1 < argc) {
            mode = QStringLiteral("--screenshots");
            screenshotDirectory = QString::fromLocal8Bit(argv[++index]);
        } else if (argument == QStringLiteral("--panels-checks")) {
            mode = QStringLiteral("--panels-checks");
        } else if (argument == QStringLiteral("--panels-screenshots") && index + 1 < argc) {
            mode = QStringLiteral("--panels-screenshots");
            screenshotDirectory = QString::fromLocal8Bit(argv[++index]);
        } else if (argument == QStringLiteral("--checks")) {
            mode = QStringLiteral("--checks");
        } else if (!argument.startsWith(QStringLiteral("--"))) {
            screenshotDirectory = argument;
        }
    }

    // The lanes log the root they act on under VEDIT_DEBUG (the same switch the
    // installers use); the old-root assertion below reads those lines.
    qputenv("VEDIT_DEBUG", "1");
    qInstallMessageHandler(messageHandler);

    if (mode == QStringLiteral("--panels-checks")
        || mode == QStringLiteral("--panels-screenshots")) {
        return runPanelsScenario(mode == QStringLiteral("--panels-screenshots") ? screenshotDirectory
                                                                              : QString());
    }

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("BATCH1-SHELL-PROBE ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString work = temp.path();
    std::printf("%s\n", qPrintable(vedit::workspace::diagnostics::toolchainStatusLine()));
    std::printf("PROBE work=%s platform=%s\n", qPrintable(work),
                qPrintable(QApplication::platformName()));

    Fixture fixture;
    QString fixtureError;
    if (!buildFixture(work, &fixture, &fixtureError)) {
        std::printf("BATCH1-SHELL-PROBE ok=0 reason=fixture — %s\n", qPrintable(fixtureError));
        return 1;
    }
    std::printf("PROBE fixture rootA=%s rootB=%s\n", qPrintable(fixture.rootA),
                qPrintable(fixture.rootB));

    const QString stateDir = work + QStringLiteral("/state");
    if (!seedSession(stateDir, fixture.rootA)) {
        std::printf("BATCH1-SHELL-PROBE ok=0 reason=session-seed\n");
        return 1;
    }

    // ---- the ORDINARY start, exactly like src/app/main.cpp ------------------
    ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = QString(); // no --project
    options.restoreSession = true;
    options.deferSessionRestore = true;
    options.showDefaultPanels = true;
    ApplicationShell shell(options);
    const QString resolvedRoot = shell.projectRoot();
    options.projectRoot = resolvedRoot; // main.cpp hands this to installPanels()
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
    shell.startSession();
    shell.resize(1500, 1000);
    shell.show();
    pump();

    check("a11.shell.restored-root", resolvedRoot == fixture.rootA, resolvedRoot);
    check("a11.shell.label", shell.projectLabel() == QFileInfo(fixture.rootA).fileName(),
          shell.projectLabel());

    // AC-1.1: every restored panel was created by its lane factory and already
    // carries the root the shell resolved before installPanels().
    auto *tree = fileTreeOf(shell);
    auto *searchPanel = searchPanelOf(shell);
    auto *vcsPanel = vcsPanelOf(shell);
    auto *moduleTree = moduleTreeOf(shell);
    auto *projectModel = projectModelOf(shell);
    check("a11.panels.restored", tree != nullptr && searchPanel != nullptr && vcsPanel != nullptr
                                     && moduleTree != nullptr,
          QStringLiteral("files=%1 search=%2 vcs=%3 moduleTree=%4")
              .arg(tree != nullptr)
              .arg(searchPanel != nullptr)
              .arg(vcsPanel != nullptr)
              .arg(moduleTree != nullptr));

    // Files: lists the restored root's own files. Row 0 of the model IS the root
    // node (that is how the panel shows "which folder am I in"), so the root's
    // children are read under that index.
    bool filesReady = waitFor([&]() {
        return tree != nullptr && tree->rootPath() == fixture.rootA
               && tree->model()->rowCount() > 0;
    });
    QStringList rootEntries;
    QString rootRowName;
    if (tree != nullptr) {
        const QModelIndex rootIndex = tree->model()->index(0, 0);
        rootRowName = rootIndex.data().toString();
        for (int guard = 0; guard < 8 && tree->model()->canFetchMore(rootIndex); ++guard) {
            tree->model()->fetchMore(rootIndex);
        }
        for (int row = 0; row < tree->model()->rowCount(rootIndex); ++row) {
            rootEntries.append(tree->model()->index(row, 0, rootIndex).data().toString());
        }
    }
    check("a11.files.root", fileTreeRootText(shell) == fixture.rootA, fileTreeRootText(shell));
    check("a11.files.ready", filesReady, rootRowName);
    check("a11.files.root-row", rootRowName == QFileInfo(fixture.rootA).fileName(), rootRowName);
    check("a11.files.lists-root-content", rootEntries.contains(QStringLiteral("rtl")),
          rootEntries.join(QLatin1Char(',')));
    check("a11.files.file-reachable",
          tree != nullptr
              && tree->model()->indexForPath(fixture.rootA + QStringLiteral("/rtl/top.sv"), true)
                     .isValid());

    // Search: the root it would scan is the restored one, and it is no longer the
    // "No project root" empty state.
    check("a11.search.root", searchPanel != nullptr && searchPanel->rootPath() == fixture.rootA,
          searchPanel != nullptr ? searchPanel->rootPath() : QStringLiteral("<missing>"));
    const QString searchEmpty = searchPanel != nullptr && searchPanel->emptyStateLabel() != nullptr
                                    ? searchPanel->emptyStateLabel()->text()
                                    : QStringLiteral("<missing>");
    check("a11.search.not-no-root", !searchEmpty.contains(QStringLiteral("No project root")),
          searchEmpty);
    check("a11.search.root-label",
          searchPanel != nullptr && searchPanel->rootLabel() != nullptr
              && searchPanel->rootLabel()->text().contains(fixture.rootA),
          searchPanel != nullptr && searchPanel->rootLabel() != nullptr
              ? searchPanel->rootLabel()->text()
              : QStringLiteral("<missing>"));
    if (searchPanel != nullptr) {
        searchPanel->setQuery(QStringLiteral("TOKEN_ALPHA_1"));
        searchPanel->startSearch();
        waitForSearch(searchPanel);
        const auto summary = searchPanel->lastSummary();
        check("a11.search.hit", summary.state == SearchState::Done && summary.hitCount >= 1,
              QStringLiteral("state=%1 hits=%2 error=%3")
                  .arg(vedit::workspace::search::searchStateName(summary.state))
                  .arg(summary.hitCount)
                  .arg(summary.error));
        check("a11.search.hits-inside-root", searchHitsInside(searchPanel, fixture.rootA),
              QStringLiteral("hits=%1").arg(searchPanel->hits().size()));
    }

    // VCS: the header names the restored root's backend (and, through it, the
    // working copy the panel is describing).
    const bool vcsReady = waitFor([&]() {
        return vcsRootProperty(shell) == fixture.rootA
               && vcsHeaderText(shell).contains(QStringLiteral("Git"));
    });
    check("a11.vcs.root", vcsRootProperty(shell) == fixture.rootA, vcsRootProperty(shell));
    check("a11.vcs.backend", vcsReady, vcsHeaderText(shell));
    check("a11.vcs.branch", vcsHeaderText(shell).contains(QFileInfo(fixture.rootA).fileName()),
          vcsHeaderText(shell));

    // Module Tree: elaborated from the restored root (needs the documented
    // VEDIT_SLANG toolchain, supplied by run.sh).
    check("a11.module-tree.poll-root", moduleTreePollRoot(shell) == fixture.rootA,
          moduleTreePollRoot(shell));
    const bool moduleTreeReady = waitFor(
        [&]() {
            return moduleTree != nullptr && moduleTree->snapshot().valid
                   && moduleTree->tree()->topLevelItemCount() > 0;
        },
        90000);
    check("a11.module-tree.non-empty", moduleTreeReady,
          QStringLiteral("valid=%1 rows=%2 status=%3")
              .arg(moduleTree != nullptr && moduleTree->snapshot().valid)
              .arg(moduleTree != nullptr ? moduleTree->tree()->topLevelItemCount() : -1)
              .arg(moduleTree != nullptr ? moduleTree->statusText() : QStringLiteral("<missing>")));
    check("a11.module-tree.summary-names-root",
          moduleTree != nullptr && moduleTree->summaryText().contains(fixture.rootA),
          moduleTree != nullptr ? moduleTree->summaryText() : QStringLiteral("<missing>"));

    // The effective file set (the project model) is the "文件集" every consumer
    // polls; its root and its members must be the restored project's.
    const bool fileSetReady = waitFor([&]() {
        return projectModel != nullptr && projectModel->projectRoot() == fixture.rootA
               && projectModel->files().size() >= 2;
    }, 30000);
    QStringList fileset = projectModel != nullptr ? projectModel->files() : QStringList();
    check("a11.fileset.root",
          projectModel != nullptr && projectModel->projectRoot() == fixture.rootA,
          projectModel != nullptr ? projectModel->projectRoot() : QStringLiteral("<missing>"));
    check("a11.fileset.non-empty", fileSetReady,
          QStringLiteral("%1 file(s): %2").arg(fileset.size()).arg(fileset.join(QLatin1Char(','))));
    bool filesetInsideRoot = !fileset.isEmpty();
    for (const QString &path : fileset) {
        if (!path.startsWith(fixture.rootA + QLatin1Char('/'))) {
            filesetInsideRoot = false;
        }
    }
    check("a11.fileset.inside-root", filesetInsideRoot, fileset.join(QLatin1Char(',')));

    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
        shell.panels()->showPanel(QStringLiteral("search"), &shell);
        shell.panels()->showPanel(QStringLiteral("vcs"), &shell);
        shell.panels()->showPanel(QStringLiteral("module-tree"), &shell);
        pump(20);
        grab(shell, screenshotDirectory + QStringLiteral("/10-ordinary-start-root-a.png"));
    }

    // ---- AC-1.2: the Files panel opens ANOTHER folder -----------------------
    // openDirectory() is exactly what the panel's Open Folder does once the dialog
    // returns; it is the user path AC-1.2 names.
    const int switchMark = logMark();
    const QString vcsHeaderBefore = vcsHeaderText(shell);
    if (tree != nullptr) {
        tree->openDirectory(fixture.rootB);
    }
    const bool switched = waitFor([&]() { return shell.projectRoot() == fixture.rootB; }, 15000);
    check("a12.shell.switched", switched, shell.projectRoot());
    check("a12.files.new-root", fileTreeRootText(shell) == fixture.rootB,
          fileTreeRootText(shell));
    const bool searchFollowed = waitFor(
        [&]() { return searchPanel != nullptr && searchPanel->rootPath() == fixture.rootB; });
    check("a12.search.new-root", searchFollowed,
          searchPanel != nullptr ? searchPanel->rootPath() : QStringLiteral("<missing>"));
    const bool vcsFollowed = waitFor([&]() {
        return vcsRootProperty(shell) == fixture.rootB
               && vcsHeaderText(shell).contains(QStringLiteral("Git"))
               && vcsHeaderText(shell) != vcsHeaderBefore;
    });
    check("a12.vcs.new-root", vcsFollowed, vcsRootProperty(shell));
    check("a12.vcs.header-changed", vcsHeaderText(shell) != vcsHeaderBefore,
          QStringLiteral("%1 -> %2").arg(vcsHeaderBefore, vcsHeaderText(shell)));
    const bool moduleTreeFollowed = waitFor(
        [&]() { return moduleTreePollRoot(shell) == fixture.rootB; });
    check("a12.module-tree.poll-root", moduleTreeFollowed, moduleTreePollRoot(shell));
    // The old tree is dropped and the NEW root is elaborated (the summary carries
    // the snapshot's own root), so the panel cannot keep showing root A's tree.
    const bool moduleTreeReelaborated = waitFor(
        [&]() {
            return moduleTree != nullptr && moduleTree->snapshot().valid
                   && moduleTree->summaryText().contains(fixture.rootB);
        },
        90000);
    check("a12.module-tree.new-hierarchy", moduleTreeReelaborated,
          moduleTree != nullptr ? moduleTree->summaryText() : QStringLiteral("<missing>"));
    const bool projectFollowed = waitFor([&]() {
        return projectModel != nullptr && projectModel->projectRoot() == fixture.rootB;
    }, 15000);
    check("a12.fileset.new-root", projectFollowed,
          projectModel != nullptr ? projectModel->projectRoot() : QStringLiteral("<missing>"));
    // The file set itself must be re-expanded for the new root.
    const bool fileSetFollowed = waitFor([&]() {
        return projectModel != nullptr && projectModel->files().size() >= 1
               && projectModel->projectRoot() == fixture.rootB;
    }, 30000);
    check("a12.fileset.re-expanded", fileSetFollowed,
          projectModel != nullptr ? projectModel->files().join(QLatin1Char(',')) : QString());

    // Search only reaches the NEW root: the beta token is found, the alpha token
    // is not, and the log names the beta root for the query.
    if (searchPanel != nullptr) {
        searchPanel->setQuery(QStringLiteral("TOKEN_BETA_2"));
        searchPanel->startSearch();
        waitForSearch(searchPanel);
        const auto betaSummary = searchPanel->lastSummary();
        check("a12.search.new-root-hit",
              betaSummary.state == SearchState::Done && betaSummary.hitCount >= 1,
              QStringLiteral("hits=%1 error=%2").arg(betaSummary.hitCount).arg(betaSummary.error));
        check("a12.search.new-root-hits-inside", searchHitsInside(searchPanel, fixture.rootB),
              QStringLiteral("hits=%1").arg(searchPanel->hits().size()));

        searchPanel->setQuery(QStringLiteral("TOKEN_ALPHA_1"));
        searchPanel->startSearch();
        waitForSearch(searchPanel);
        const auto alphaSummary = searchPanel->lastSummary();
        check("a12.search.old-token-not-found",
              alphaSummary.state == SearchState::Done && alphaSummary.hitCount == 0,
              QStringLiteral("hits=%1 error=%2").arg(alphaSummary.hitCount).arg(alphaSummary.error));
    }

    // The lane log, read after the switch: the new root was announced and the
    // old one was never handed to a scan again.
    const QStringList switchedLog = logSince(switchMark);
    bool announcedNewRoot = false;
    bool queriedNewRoot = false;
    bool queriedOldRoot = false;
    for (const QString &line : switchedLog) {
        if (line.contains(QStringLiteral("search: root="))
            && line.contains(fixture.rootB)) {
            announcedNewRoot = true;
        }
        if (line.contains(QStringLiteral("search: query root="))) {
            if (line.contains(fixture.rootB)) {
                queriedNewRoot = true;
            }
            if (line.contains(fixture.rootA)) {
                queriedOldRoot = true;
            }
        }
    }
    check("a12.search.log-announces-new-root", announcedNewRoot);
    check("a12.search.log-queried-new-root", queriedNewRoot);
    check("a12.search.log-never-queried-old-root", !queriedOldRoot);

    if (!screenshotDirectory.isEmpty()) {
        pump(20);
        grab(shell, screenshotDirectory + QStringLiteral("/20-after-switch-root-b.png"));
    }

    // ---- the File -> Open Folder command reaches the same broadcast ---------
    // (the dialog itself is the only part replaced by the probe seam)
    shell.setOpenFolderDialogProvider([&fixture]() { return fixture.rootA; });
    if (QAction *action = shell.commands()->action(QStringLiteral("file.open-folder"))) {
        action->trigger();
    }
    const bool menuSwitched = waitFor(
        [&]() {
            return shell.projectRoot() == fixture.rootA && searchPanel != nullptr
                   && searchPanel->rootPath() == fixture.rootA;
        },
        15000);
    check("a12.file-menu-open-folder", menuSwitched,
          QStringLiteral("shell=%1 search=%2")
              .arg(shell.projectRoot())
              .arg(searchPanel != nullptr ? searchPanel->rootPath() : QString()));
    const bool menuVcs = waitFor([&]() { return vcsRootProperty(shell) == fixture.rootA; });
    check("a12.file-menu-vcs-followed", menuVcs, vcsRootProperty(shell));
    const bool menuTree = waitFor([&]() { return moduleTreePollRoot(shell) == fixture.rootA; });
    check("a12.file-menu-module-tree-followed", menuTree, moduleTreePollRoot(shell));

    std::printf("BATCH1-SHELL-PROBE ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
