// VEdit application shell entry point (task T13).
//
// Modes:
//   vedit-app                       run the shell (interactive)
//   vedit-app --selfcheck           non-interactive checks, machine-readable output
//   vedit-app --screenshots <dir>   capture the two split states + empty state
//
// The shell has no project model and no editor of its own: those arrive through
// the seams in src/app (EditorProvider / PanelRegistry / IShortcutStore). See
// evidence/app-shell/ for the acceptance evidence of this task.
#include "app_shell.h"

#include "command_registry.h"
#include "editor_area.h"
#include "integration.h"
#include "../workspace/diagnostics/toolchain.h"
#include "panel_registry.h"
#include "terminal_panel.h"
#include "theme_manager.h"
#if defined(VEDIT_HAVE_EDIT_CORE)
#include "kernel/cross_boundary.h"
#include "kernel/edit_core_selftest.h"
#include "kernel/ktexteditor_provider.h"
#include <KTextEditor/Document>
#endif
#ifdef VEDIT_HAVE_RUST_FFI
#include "vd_ffi.h"
#endif

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QString>
#include <QStatusBar>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstdio>
#include <memory>
#include <string>

using vedit::app::ApplicationShell;
using vedit::app::CommandRegistry;
using vedit::app::EditorArea;
using vedit::app::EditorPane;
using vedit::app::PanelRegistry;
using vedit::app::ShellOptions;
using vedit::app::ThemeManager;

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
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
}

bool sameJson(const QJsonObject &left, const QJsonObject &right)
{
    return QJsonDocument(left).toJson(QJsonDocument::Compact) ==
           QJsonDocument(right).toJson(QJsonDocument::Compact);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
}

void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

// --- Ctrl+S duplicate-binding detection (task T80) -------------------------
// Qt reports "Ambiguous shortcut overload" and triggers NEITHER action when two
// enabled actions carry the same shortcut in the same window context. The
// product had exactly that: the shell's file.save action (Ctrl+S) plus the
// KTextEditor view's own standard save action (KStandardAction::save). The
// helpers below enumerate what Qt can see, so the selfcheck can assert the
// invariant "exactly ONE Ctrl+S binding" instead of trusting one simulated key
// press (the simulated press missed the ambiguity in T71).
QList<QAction *> collectShortcutActions(QWidget *root)
{
    QList<QAction *> actions;
    if (root == nullptr) {
        return actions;
    }
    const auto add = [&actions](const QList<QAction *> &list) {
        for (QAction *action : list) {
            if (action != nullptr && !actions.contains(action)) {
                actions.append(action);
            }
        }
    };
    add(root->actions());
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        add(widget->actions());
    }
    add(root->findChildren<QAction *>());
    return actions;
}

bool hasCtrlSShortcut(const QAction *action)
{
    if (action == nullptr) {
        return false;
    }
    const QKeySequence ctrlS(QStringLiteral("Ctrl+S"));
    for (const QKeySequence &sequence : action->shortcuts()) {
        if (sequence == ctrlS) {
            return true;
        }
    }
    return false;
}

QList<QAction *> ctrlSActions(QWidget *root)
{
    QList<QAction *> found;
    for (QAction *action : collectShortcutActions(root)) {
        if (hasCtrlSShortcut(action)) {
            found.append(action);
        }
    }
    return found;
}

void printCtrlSBindings(QWidget *root, const char *tag)
{
    for (QAction *action : ctrlSActions(root)) {
        QStringList shortcuts;
        for (const QKeySequence &sequence : action->shortcuts()) {
            shortcuts.append(sequence.toString(QKeySequence::PortableText));
        }
        QString chain;
        for (QObject *object = action->parent(); object != nullptr; object = object->parent()) {
            chain.append(QString::fromLatin1(object->metaObject()->className()));
            chain.append(QLatin1Char('<'));
        }
        QString associated;
        for (QObject *object : action->associatedObjects()) {
            associated.append(QString::fromLatin1(object->metaObject()->className()));
            if (auto *widget = qobject_cast<QWidget *>(object)) {
                associated.append(QLatin1Char('(') + widget->objectName() + QLatin1Char(')'));
            }
            associated.append(QLatin1Char(' '));
        }
        QString holders;
        if (root != nullptr) {
            for (QWidget *widget : root->findChildren<QWidget *>()) {
                if (widget->actions().contains(action)) {
                    holders.append(QString::fromLatin1(widget->metaObject()->className()));
                    holders.append(QLatin1Char('(') + widget->objectName() + QLatin1Char(')'));
                    holders.append(QLatin1Char(' '));
                }
            }
        }
        std::printf("CTRL-S-BINDING %s objectName=%s class=%s shortcut=%s context=%d "
                    "associated=[%s] holders=[%s] chain=%s\n",
                    tag, qPrintable(action->objectName()), action->metaObject()->className(),
                    qPrintable(shortcuts.join(QLatin1Char(','))),
                    static_cast<int>(action->shortcutContext()), qPrintable(associated),
                    qPrintable(holders), qPrintable(chain));
    }
}

// Cross-boundary counter (AC-S1.1 / C1): the Rust service layer is the only C
// ABI; the edit kernel links no `vd_*` symbol, and every ABI call site in this
// file is instrumented with this helper. The edit-core selfcheck asserts the
// counter does not move while editing, and that the counter is live.
void noteAbiCall()
{
#if defined(VEDIT_HAVE_EDIT_CORE)
    vedit::editor::noteCrossBoundaryCall();
#endif
}

// ---------------------------------------------------------------------------
// self-check
// ---------------------------------------------------------------------------

int runSelfCheck()
{
    // T82: toolchain observability is part of the self-check output. The line
    // names each tool's source (bundled > PATH > env) and the missing list.
    std::printf("%s\n", qPrintable(vedit::workspace::diagnostics::toolchainStatusLine()));
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("APP-SHELL-SELFTEST ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString stateDir = temp.path() + QStringLiteral("/state");

    ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    auto shell = std::make_unique<ApplicationShell>(options);
    shell->show();
    pump();

    check("empty.no-project-label", shell->projectLabel() == QStringLiteral("no project"));
    check("empty.one-pane", shell->editorArea()->paneCount() == 1);
    check("empty.window-title", shell->windowTitle().contains(QStringLiteral("no project")));

    const QStringList expectedPanels{QStringLiteral("file-tree"), QStringLiteral("search"),
                                     QStringLiteral("terminal"), QStringLiteral("vcs"),
                                     QStringLiteral("settings"), QStringLiteral("outline")};
    check("panels.registry.six", shell->panels()->ids().size() == expectedPanels.size());
    for (const QString &id : expectedPanels) {
        check(("panels.registered." + id.toStdString()).c_str(), shell->panels()->contains(id));
    }
    check("panels.none-inserted-by-default", shell->panels()->insertedIds().isEmpty());

    // On-demand insertion + toggle for every panel.
    for (const QString &id : expectedPanels) {
        shell->panels()->showPanel(id, shell.get());
        pump();
        check(("panels.show." + id.toStdString()).c_str(), shell->panels()->isVisible(id));
        const bool hidden = shell->panels()->toggle(id, shell.get());
        pump();
        check(("panels.toggle-hide." + id.toStdString()).c_str(), !hidden && !shell->panels()->isVisible(id));
        const bool shown = shell->panels()->toggle(id, shell.get());
        pump();
        check(("panels.toggle-show." + id.toStdString()).c_str(), shown && shell->panels()->isVisible(id));
    }
    check("panels.six-docks", shell->panels()->docks().size() == expectedPanels.size());

    // Menu / shortcut framework.
    check("menus.file", shell->commands()->menus().contains(QStringLiteral("file")));
    check("menus.view", shell->commands()->menus().contains(QStringLiteral("view")));
    check("menus.help", shell->commands()->menus().contains(QStringLiteral("help")));
    const char *commandIds[] = {"file.new-layout", "file.close-pane", "file.quit",
                                "view.split.horizontal", "view.split.vertical",
                                "view.focus.next", "view.layout.reset", "help.about",
                                "view.panel.file-tree", "view.panel.outline"};
    for (const char *id : commandIds) {
        check((std::string("commands.action.") + id).c_str(),
              shell->commands()->action(QString::fromLatin1(id)) != nullptr);
    }
    check("commands.file-menu-actions",
          shell->commands()->actionsInMenu(QStringLiteral("file")).size() >= 4);
    check("commands.view-panel-actions-checkable",
          shell->commands()->action(QStringLiteral("view.panel.file-tree"))->isCheckable());

    // Horizontal split.
    shell->applyHorizontalSplitPreset(3);
    pump();
    check("split.horizontal.pane-count", shell->editorArea()->paneCount() == 3);
    check("split.horizontal.root-orientation",
          shell->editorArea()->saveLayout()
                  .value(QStringLiteral("root"))
                  .toObject()
                  .value(QStringLiteral("orientation"))
                  .toString() == QStringLiteral("horizontal"));

    // Vertical split.
    shell->applyVerticalSplitPreset(3);
    pump();
    check("split.vertical.pane-count", shell->editorArea()->paneCount() == 3);
    check("split.vertical.root-orientation",
          shell->editorArea()->saveLayout()
                  .value(QStringLiteral("root"))
                  .toObject()
                  .value(QStringLiteral("orientation"))
                  .toString() == QStringLiteral("vertical"));

    // Focus switching.
    const auto panes = shell->editorArea()->panes();
    check("panes.three", panes.size() == 3);
    shell->editorArea()->setActivePane(panes.at(1));
    check("focus.set-second", shell->editorArea()->activePane() == panes.at(1));
    shell->editorArea()->focusNextPane();
    check("focus.next-wraps", shell->editorArea()->activePane() == panes.at(2));

    // Close / re-layout.
    shell->editorArea()->closeActivePane();
    pump();
    check("close.pane-count", shell->editorArea()->paneCount() == 2);
    shell->editorArea()->resetLayout();
    pump();
    check("reset.one-pane", shell->editorArea()->paneCount() == 1);
    shell->editorArea()->closeActivePane();
    pump();
    check("close.last-pane.keeps-one", shell->editorArea()->paneCount() == 1);
    check("close.last-pane.active", shell->editorArea()->activePane() != nullptr);

    // Persistence round-trip.
    shell->applyMixedSplitPreset();
    shell->panels()->showPanel(QStringLiteral("file-tree"), shell.get());
    shell->panels()->showPanel(QStringLiteral("search"), shell.get());
    pump();
    QStringList availableThemes = shell->themes()->availableThemes();
    check("theme.catalogue", availableThemes.size() >= 5);
    QString chosenTheme;
    for (const QString &name : availableThemes) {
        if (name != shell->themes()->currentTheme()) {
            chosenTheme = name;
            break;
        }
    }
    QString themeError;
    check("theme.apply", !chosenTheme.isEmpty() && shell->themes()->applyTheme(chosenTheme, &themeError));
    const QColor paletteBefore = shell->themes()->windowColor();
    QString invalidThemeError;
    const bool invalidApplied =
        shell->themes()->applyTheme(QStringLiteral("__definitely-not-a-theme__"), &invalidThemeError);
    check("theme.invalid.rejected", !invalidApplied && !invalidThemeError.isEmpty());
    check("theme.invalid.keeps-palette", shell->themes()->windowColor() == paletteBefore);

    shell->shortcutStore()->setShortcut(QStringLiteral("view.split.horizontal"),
                                        QKeySequence(QStringLiteral("Ctrl+Shift+J")));
    shell->commands()->refreshShortcuts();
    check("shortcut.override.applied",
          shell->commands()->action(QStringLiteral("view.split.horizontal"))->shortcut() ==
              QKeySequence(QStringLiteral("Ctrl+Shift+J")));

    const QJsonObject layoutBefore = shell->editorArea()->saveLayout();
    const int panesBefore = shell->editorArea()->paneCount();
    const QString activeBefore = layoutBefore.value(QStringLiteral("active")).toString();
    const QString themeBefore = shell->themes()->currentTheme();
    const bool fileTreeVisibleBefore = shell->panels()->isVisible(QStringLiteral("file-tree"));
    QString saveError;
    check("session.save", shell->saveSession(&saveError));

    ShellOptions restoredOptions = options;
    restoredOptions.restoreSession = true;
    auto restored = std::make_unique<ApplicationShell>(restoredOptions);
    restored->show();
    pump();
    check("restore.pane-count", restored->editorArea()->paneCount() == panesBefore);
    check("restore.layout-tree",
          sameJson(restored->editorArea()->saveLayout().value(QStringLiteral("root")).toObject(),
                   layoutBefore.value(QStringLiteral("root")).toObject()));
    check("restore.active-pane",
          restored->editorArea()->saveLayout().value(QStringLiteral("active")).toString() ==
              activeBefore);
    check("restore.theme", restored->themes()->currentTheme() == themeBefore);
    check("restore.panel-visible",
          restored->panels()->isVisible(QStringLiteral("file-tree")) == fileTreeVisibleBefore);
    check("restore.shortcut",
          restored->commands()->action(QStringLiteral("view.split.horizontal"))->shortcut() ==
              QKeySequence(QStringLiteral("Ctrl+Shift+J")));

    // --- a restored dock gets its lane content (review M1) ------------------
    // A session that lists the terminal and search panels is restored AFTER the
    // lanes registered their factories (the product sequence: deferSessionRestore
    // + startSession). Before that fix the restore ran during construction, when
    // both factories were still empty, and the docks kept the shell placeholder
    // forever: this check fails there and passes here.
    {
        const QString seamStateDir = temp.path() + QStringLiteral("/panel-seam");
        ShellOptions seamOptions;
        seamOptions.stateDir = seamStateDir;
        seamOptions.showDefaultPanels = false;
        seamOptions.restoreSession = false;
        auto seed = std::make_unique<ApplicationShell>(seamOptions);
        seed->panels()->showPanel(QStringLiteral("terminal"), seed.get());
        seed->panels()->showPanel(QStringLiteral("search"), seed.get());
        // A non-default editor layout as well: the deferred start must restore it
        // exactly like the constructor-time restore did.
        seed->editorArea()->splitActive(Qt::Horizontal);
        pump();
        QString seamSaveError;
        check("panel-seam.seed-session", seed->saveSession(&seamSaveError), seamSaveError);
        seed.reset();

        ShellOptions seamRestored = seamOptions;
        seamRestored.restoreSession = true;
        seamRestored.deferSessionRestore = true;
        auto seamShell = std::make_unique<ApplicationShell>(seamRestored);
        seamShell->show();
        pump();
        check("panel-seam.deferred-nothing-inserted", seamShell->panels()->insertedIds().isEmpty());
        // The factories are registered here, exactly as main() does, and only then
        // the session starts.
        seamShell->panels()->setFactory(QStringLiteral("terminal"),
                                        vedit::terminal::terminalPanelFactory());
        seamShell->panels()->setFactory(QStringLiteral("search"), []() -> QWidget * {
            auto *probe = new QLabel(QStringLiteral("search panel from a registered factory"));
            probe->setObjectName(QStringLiteral("search-panel-probe"));
            return probe;
        });
        seamShell->startSession();
        pump();
        QDockWidget *terminalDock = seamShell->panels()->dock(QStringLiteral("terminal"));
        QDockWidget *searchDock = seamShell->panels()->dock(QStringLiteral("search"));
        check("panel-seam.dock-restored", terminalDock != nullptr && searchDock != nullptr);
        check("panel-seam.terminal-content",
              terminalDock != nullptr &&
                  qobject_cast<vedit::terminal::TerminalPanel *>(terminalDock->widget()) != nullptr);
        check("panel-seam.search-content",
              searchDock != nullptr && searchDock->widget() != nullptr &&
                  searchDock->widget()->objectName() == QStringLiteral("search-panel-probe"));
        check("panel-seam.no-placeholder",
              terminalDock != nullptr && terminalDock->widget() != nullptr &&
                  !terminalDock->widget()->objectName().startsWith(
                      QStringLiteral("panel-placeholder-")));
        check("panel-seam.editor-layout-restored", seamShell->editorArea()->paneCount() == 2,
              QStringLiteral("panes=%1").arg(seamShell->editorArea()->paneCount()));
    }

    // Bad configuration must never prevent startup.
    const QString brokenStateDir = temp.path() + QStringLiteral("/broken");
    writeFile(brokenStateDir + QStringLiteral("/session.json"), QByteArray("{not json at all"));
    writeFile(brokenStateDir + QStringLiteral("/shortcuts.json"), QByteArray("[also broken"));
    ShellOptions brokenOptions;
    brokenOptions.stateDir = brokenStateDir;
    brokenOptions.restoreSession = true;
    auto broken = std::make_unique<ApplicationShell>(brokenOptions);
    broken->show();
    pump();
    check("broken.config.survives", broken->editorArea()->paneCount() >= 1);
    check("broken.config.diagnostics", !broken->diagnostics().isEmpty());

    // Empty state stress: no project, panels toggled, panes closed repeatedly.
    ShellOptions emptyOptions;
    emptyOptions.stateDir = temp.path() + QStringLiteral("/empty");
    emptyOptions.restoreSession = false;
    auto empty = std::make_unique<ApplicationShell>(emptyOptions);
    empty->show();
    pump();
    check("stress.one-pane-initial", empty->editorArea()->paneCount() == 1);
    for (const QString &id : empty->panels()->ids()) {
        empty->panels()->toggle(id, empty.get());
    }
    pump();
    for (int i = 0; i < 5; ++i) {
        empty->editorArea()->closeActivePane();
    }
    pump();
    check("stress.pane-invariant", empty->editorArea()->paneCount() >= 1);
    check("stress.non-null-active", empty->editorArea()->activePane() != nullptr);

    // --- save entry: File > Save / Ctrl+S (task T71) ------------------------
    // Stop 1's acceptance edits a file and saves it. The entry must exist both in
    // the File menu and on Ctrl+S, and both paths must go through the ACTIVE
    // pane's EditorProvider seam (never a QPlainTextEdit cast in the shell), so
    // the installed provider decides how the document is written. The
    // KTextEditor provider is exercised further down; the demo provider first.
    QAction *saveAction = shell->commands()->action(QStringLiteral("file.save"));
    check("save.command.registered", saveAction != nullptr);
    check("save.shortcut.ctrl-s",
          saveAction != nullptr
              && saveAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")));
    bool saveInFileMenu = false;
    for (QAction *topLevelAction : shell->menuBar()->actions()) {
        QMenu *menu = topLevelAction != nullptr ? topLevelAction->menu() : nullptr;
        if (menu != nullptr && menu->actions().contains(saveAction)) {
            saveInFileMenu = true;
        }
    }
    check("save.menu.file-item", saveInFileMenu);

    const auto readFileBytes = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };
    const auto leftoverSaveTemps = [](const QString &directory) {
        return QDir(directory).entryList(QStringList{QStringLiteral("*.vedit-tmp-*")},
                                         QDir::Files);
    };

#if defined(VEDIT_HAVE_EDIT_CORE)
    // The shell still runs the demo provider here: the same entry must save a
    // demo document too (the entry is provider-agnostic by construction).
    EditorPane *saveDemoPane = shell->editorArea()->activePane();
    if (saveDemoPane != nullptr && saveAction != nullptr) {
        const QString demoPath = temp.path() + QStringLiteral("/save-entry-demo.v");
        writeFile(demoPath, QByteArrayLiteral("module demo_before;\nendmodule\n"));
        QString demoError;
        check("save.demo.open", saveDemoPane->openFile(demoPath, &demoError));
        if (auto *demoEditor = qobject_cast<QPlainTextEdit *>(saveDemoPane->editorWidget())) {
            demoEditor->setPlainText(QStringLiteral("module demo_after;\nendmodule\n"));
        }
        saveAction->trigger();
        pump();
        check("save.demo.action.disk-bytes",
              readFileBytes(demoPath) == QByteArrayLiteral("module demo_after;\nendmodule\n"));
        check("save.demo.action.path-unchanged",
              saveDemoPane->currentFilePath() == QFileInfo(demoPath).absoluteFilePath());
        check("save.demo.action.no-temp-file",
              leftoverSaveTemps(QFileInfo(demoPath).absolutePath()).isEmpty());
    }

    // --- edit-core kernel in the product (Slice 1, AC-S1.1) ----------------
    // The provider is swapped on a live shell: every pane must get the real
    // KTextEditor widget, and the pane's document seam must open/save a file.
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    pump();
    check("editor.provider.id",
          shell->editorProvider() != nullptr
              && shell->editorProvider()->id() == QStringLiteral("ktexteditor"));
    EditorPane *editPane = shell->editorArea()->activePane();
    check("editor.pane.ktexteditor-widget",
          editPane != nullptr && vedit::editor::isEditCoreWidget(editPane->editorWidget()));

    // --- T80: exactly ONE authority for Ctrl+S ------------------------------
    // The user hit "Ambiguous shortcut overload" on the real machine: the shell
    // registered Ctrl+S for file.save while the KTextEditor view brought its own
    // standard save action. Two enabled Ctrl+S actions in one window make Qt
    // trigger neither, so the invariant is asserted directly.
    printCtrlSBindings(shell.get(), "after-provider-install");
    {
        const QList<QAction *> ctrlS = ctrlSActions(shell.get());
        check("save.shortcut.unique", ctrlS.size() == 1,
              QStringLiteral("bindings=%1").arg(ctrlS.size()));
        check("save.shortcut.authority", ctrlS.size() == 1 && ctrlS.first() == saveAction);
    }
    if (editPane != nullptr) {
        const QString paneFixture = temp.path() + QStringLiteral("/pane-open.v");
        writeFile(paneFixture, QByteArrayLiteral("module pane_open;\nendmodule\n"));
        QString paneError;
        const bool paneOpened = editPane->openFile(paneFixture, &paneError);
        check("editor.pane.open", paneOpened);
        if (!paneError.isEmpty()) {
            std::printf("EDITOR-PANE-ERROR %s\n", qPrintable(paneError));
        }
        check("editor.pane.open-path",
              editPane->currentFilePath() == QFileInfo(paneFixture).absoluteFilePath(),
              editPane->currentFilePath());
        check("editor.pane.save", editPane->saveFile(&paneError));

        // --- T71: Ctrl+S and File > Save on the KTextEditor provider -------
        // Both paths run end-to-end: the document text is changed, the entry is
        // invoked, and the bytes on disk must change while the pane keeps its
        // path and the edit-core lane's atomic save leaves no temporary file.
        KTextEditor::Document *document = vedit::editor::documentFor(editPane->editorWidget());
        check("save.ktexteditor.document", document != nullptr);
        const QString savePath = editPane->currentFilePath();
        if (document != nullptr && saveAction != nullptr) {
            // 1) Keyboard path: a real Ctrl+S key event sent to the shell's
            //    window goes through Qt's shortcut map to the QAction. T80: the
            //    editor view is FOCUSED first - that is the real user path, and
            //    it is exactly what exposes a duplicate binding (Qt then sees
            //    both the shell action and the editor view's own save action).
            document->setText(QStringLiteral("module saved_by_ctrl_s;\nendmodule\n"));
            document->setModified(true);
            shell->activateWindow();
            editPane->editorWidget()->setFocus(Qt::OtherFocusReason);
            pump();
            QWindow *shellWindow = shell->windowHandle();
            check("save.shortcut.window", shellWindow != nullptr);
            if (shellWindow != nullptr) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier);
                QApplication::sendEvent(shellWindow, &press);
                QApplication::sendEvent(shellWindow, &release);
            }
            pump();
            check("save.shortcut.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module saved_by_ctrl_s;\nendmodule\n"));
            check("save.shortcut.modified-cleared", !document->isModified());
            check("save.shortcut.path-unchanged", editPane->currentFilePath() == savePath);

            // 2) Menu path: the very QAction the File menu holds, triggered.
            document->setText(QStringLiteral("module saved_from_menu;\nendmodule\n"));
            document->setModified(true);
            saveAction->trigger();
            pump();
            check("save.menu.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module saved_from_menu;\nendmodule\n"));
            check("save.menu.path-unchanged", editPane->currentFilePath() == savePath);
            check("save.no-temp-file",
                  leftoverSaveTemps(QFileInfo(savePath).absolutePath()).isEmpty());

            // 3) A failing save must be visible and diagnosable, never silent.
            //    atomic_save's documented test fault injection is used so the
            //    failure runs through the real KTextEditor/atomic path.
            document->setText(QStringLiteral("module must_not_reach_disk;\nendmodule\n"));
            document->setModified(true);
            const QByteArray beforeFailure = readFileBytes(savePath);
            qputenv("VEDIT_ATOMIC_FAULT", QByteArrayLiteral("write"));
            saveAction->trigger();
            qunsetenv("VEDIT_ATOMIC_FAULT");
            pump();
            check("save.failure.disk-unchanged", readFileBytes(savePath) == beforeFailure);
            check("save.failure.status-bar",
                  shell->statusBar()->currentMessage().contains(QStringLiteral("Save failed")));
            bool failureDiagnostic = false;
            for (const QString &diagnostic : shell->diagnostics()) {
                if (diagnostic.contains(QStringLiteral("Save failed"))) {
                    failureDiagnostic = true;
                }
            }
            check("save.failure.diagnostic", failureDiagnostic);
            check("save.failure.still-modified", document->isModified());
            // 4) ... and the same entry saves again once the fault is gone.
            saveAction->trigger();
            pump();
            check("save.recovery.disk-bytes",
                  readFileBytes(savePath)
                      == QByteArrayLiteral("module must_not_reach_disk;\nendmodule\n"));
            check("save.recovery.not-modified", !document->isModified());
        }
    }

    // T80: a split creates ANOTHER editor view, so the "exactly one Ctrl+S"
    // invariant must cover every pane, not only the first one. This is what the
    // per-widget cleanup in EditorPane::replaceEditorWidget guarantees.
    {
        EditorPane *extraPane = shell->editorArea()->splitActive(Qt::Horizontal);
        pump();
        const QList<QAction *> ctrlS = ctrlSActions(shell.get());
        printCtrlSBindings(shell.get(), "after-split");
        check("save.shortcut.unique.multi-pane", ctrlS.size() == 1,
              QStringLiteral("bindings=%1").arg(ctrlS.size()));
        if (extraPane != nullptr) {
            shell->editorArea()->closeActivePane();
            pump();
        }
    }

    vedit::editor::BoundaryHooks hooks;
    hooks.calls = []() { return vedit::editor::crossBoundaryCallCount(); };
#if defined(VEDIT_HAVE_RUST_FFI)
    hooks.positiveControl = []() {
        noteAbiCall();
        (void)vd_abi_version();
    };
#endif
    const vedit::editor::SelfCheckResult editCore = vedit::editor::runSelfChecks(hooks);
    g_checks += editCore.checks;
    g_failures += editCore.failures;
#endif

    std::printf("APP-SHELL-SELFTEST ok=%d checks=%d failures=%d state=%s\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, qPrintable(stateDir));
    return g_failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Rust service layer (C ABI) self-check — Slice 0
// ---------------------------------------------------------------------------
//
// This runs the C side of the vertical slice: it calls the generated header from
// a real C++ caller linked against libvd_ffi.a, and asserts the project/.f
// expansion, the FIM default-off state, VCS backend detection and a settings
// round-trip. Every call is start-then-poll; nothing here waits on the ABI.
namespace ffi {

#ifdef VEDIT_HAVE_RUST_FFI

QString errorText(VdService *service)
{
    VdError error{};
    if (vd_service_error(service, &error) != 1) {
        return QString();
    }
    return QString::fromUtf8(error.message);
}

QStringList stringList(const VdStringList &list)
{
    QStringList values;
    for (size_t index = 0; index < list.count; ++index) {
        values.append(QString::fromUtf8(list.items[index]));
    }
    return values;
}

int runSelfCheck(const QString &fixtureDir)
{
    std::printf("FFI-ABI fixture=%s\n", qPrintable(fixtureDir));
    const QByteArray fixturePath = QFile::encodeName(fixtureDir);

    noteAbiCall();
    VdService *service = vd_service_create();
    check("ffi.service.create", service != nullptr);
    if (service == nullptr) {
        std::printf("FFI-ABI-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    noteAbiCall();
    check("ffi.abi-version", vd_abi_version() == VD_ABI_VERSION);

    // 1/7 project model + .f. The start call must return immediately (C2).
    QElapsedTimer startTimer;
    startTimer.start();
    noteAbiCall();
    const quint64 projectJob = vd_project_parse(service, fixturePath.constData());
    const qint64 startMs = startTimer.elapsed();
    check("ffi.project.start.returns-job", projectJob != 0);
    check("ffi.project.start.non-blocking", startMs < 200);
    std::printf("FFI-TIMING project-start-ms=%lld\n", static_cast<long long>(startMs));

    VdProjectInfo project{};
    QElapsedTimer projectTimer;
    projectTimer.start();
    qint32 projectStatus = VD_STATUS_PENDING;
    while (projectTimer.elapsed() < 20000 &&
           (projectStatus = vd_project_poll(service, projectJob, &project)) == VD_STATUS_PENDING) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const bool projectReady = projectStatus == VD_STATUS_READY;
    check("ffi.project.ready", projectReady);
    if (!projectReady) {
        std::printf("FFI-ERROR project: %s\n", qPrintable(errorText(service)));
    }
    const QStringList files = stringList(project.files);
    check("ffi.project.file-count", files.size() == 3);
    QStringList basenames;
    for (const QString &path : files) {
        basenames.append(QFileInfo(path).fileName());
    }
    check("ffi.project.file-order",
          basenames == QStringList{QStringLiteral("rtl_a.sv"), QStringLiteral("rtl_b.sv"),
                                   QStringLiteral("rtl_c.sv")});
    const QStringList includeDirs = stringList(project.include_dirs);
    check("ffi.project.include-dir",
          includeDirs.size() == 1 && includeDirs.first().endsWith(QStringLiteral("inc")));
    const QStringList defines = stringList(project.defines);
    check("ffi.project.defines",
          defines.contains(QStringLiteral("WIDTH=8")) && defines.contains(QStringLiteral("SIM")));
    noteAbiCall();
    vd_project_info_free(&project);

    // Unknown job ids are rejected instead of silently accepted.
    VdProjectInfo rejected{};
    noteAbiCall();
    check("ffi.project.unknown-job-rejected", vd_project_poll(service, 424242, &rejected) == -1);

    // 4/7 FIM: the product default is off, and the call makes no request.
    noteAbiCall();
    const quint64 fimJob = vd_fim_complete(service, "module m;", "");
    check("ffi.fim.start", fimJob != 0);
    VdFimResult fim{};
    QElapsedTimer fimTimer;
    fimTimer.start();
    qint32 fimStatus = VD_STATUS_PENDING;
    while (fimTimer.elapsed() < 5000 &&
           (fimStatus = vd_fim_poll(service, fimJob, &fim)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.fim.disabled-by-default", fimStatus == VD_STATUS_READY && fim.state == 0);
    noteAbiCall();
    vd_fim_result_free(&fim);

    // 6/7 VCS: the fixture lives inside this repository, so Git is detected.
    noteAbiCall();
    const quint64 vcsJob = vd_vcs_status(service, fixturePath.constData());
    check("ffi.vcs.start", vcsJob != 0);
    VdVcsStatus vcs{};
    QElapsedTimer vcsTimer;
    vcsTimer.start();
    qint32 vcsStatus = VD_STATUS_PENDING;
    while (vcsTimer.elapsed() < 10000 &&
           (vcsStatus = vd_vcs_poll(service, vcsJob, &vcs)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.vcs.backend-detected", vcsStatus == VD_STATUS_READY && vcs.backend == 1);
    noteAbiCall();
    vd_vcs_status_free(&vcs);

    // 7/7 settings: write and read back through the ABI.
    QTemporaryDir stateDir;
    check("ffi.settings.temp-dir", stateDir.isValid());
    const QByteArray settingsPath =
        QFile::encodeName(stateDir.path() + QStringLiteral("/settings.toml"));
    const QByteArray settingKey = QByteArrayLiteral("theme");
    const QByteArray settingValue = QByteArrayLiteral("Breeze Dark");
    const VdSettingInput setting{settingKey.constData(), settingValue.constData()};
    noteAbiCall();
    const quint64 saveJob = vd_settings_save(service, settingsPath.constData(), &setting, 1);
    check("ffi.settings.save.start", saveJob != 0);
    VdSettings saved{};
    QElapsedTimer settingsTimer;
    settingsTimer.start();
    qint32 saveStatus = VD_STATUS_PENDING;
    while (settingsTimer.elapsed() < 10000 &&
           (saveStatus = vd_settings_poll(service, saveJob, &saved)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.settings.save.ready", saveStatus == VD_STATUS_READY);
    noteAbiCall();
    vd_settings_free(&saved);

    noteAbiCall();
    const quint64 loadJob = vd_settings_load(service, settingsPath.constData());
    check("ffi.settings.load.start", loadJob != 0);
    VdSettings loaded{};
    settingsTimer.restart();
    qint32 loadStatus = VD_STATUS_PENDING;
    while (settingsTimer.elapsed() < 10000 &&
           (loadStatus = vd_settings_poll(service, loadJob, &loaded)) == VD_STATUS_PENDING) {
        QThread::msleep(1);
    }
    check("ffi.settings.load.ready", loadStatus == VD_STATUS_READY && loaded.count == 1);
    if (loaded.count == 1) {
        check("ffi.settings.round-trip",
              QString::fromUtf8(loaded.items[0].key) == QStringLiteral("theme") &&
                  QString::fromUtf8(loaded.items[0].value) == QStringLiteral("Breeze Dark"));
    } else {
        check("ffi.settings.round-trip", false);
    }
    noteAbiCall();
    vd_settings_free(&loaded);

    noteAbiCall();
    vd_service_destroy(service);
    std::printf("FFI-ABI-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else // VEDIT_HAVE_RUST_FFI

int runSelfCheck(const QString &)
{
    std::printf("FFI-ABI-SELFTEST ok=0 reason=built-without-rust-ffi\n");
    return 1;
}

#endif // VEDIT_HAVE_RUST_FFI

} // namespace ffi

// ---------------------------------------------------------------------------
// screenshots
// ---------------------------------------------------------------------------

int captureScreenshots(const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        std::printf("SCREENSHOT-FAIL cannot create %s\n", qPrintable(directory));
        return 1;
    }
    ShellOptions options;
    options.stateDir = directory + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = QString();
    ApplicationShell shell(options);
    shell.resize(1440, 900);
    shell.show();
    pump();

    const auto save = [&shell, &directory](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name;
        const bool ok = shell.grab().save(path);
        std::printf("SCREENSHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
        return ok;
    };

    bool ok = true;
    // Two split states, each with the two default docks visible.
    shell.panels()->hidePanel(QStringLiteral("search"));
    shell.panels()->hidePanel(QStringLiteral("terminal"));
    shell.panels()->hidePanel(QStringLiteral("vcs"));
    shell.panels()->hidePanel(QStringLiteral("settings"));
    shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);

    shell.applyHorizontalSplitPreset(3);
    pump();
    ok = save(QStringLiteral("10-horizontal-split.png")) && ok;

    shell.applyVerticalSplitPreset(3);
    pump();
    ok = save(QStringLiteral("11-vertical-split.png")) && ok;

    // Panels and docks, all six on screen.
    shell.applyHorizontalSplitPreset(2);
    shell.insertAllPanelsForDemo();
    pump();
    ok = save(QStringLiteral("12-panels-and-docks.png")) && ok;

    // Empty state: no project, no panel content, one pane.
    for (const QString &id : shell.panels()->ids()) {
        shell.panels()->hidePanel(id);
    }
    shell.editorArea()->resetLayout();
    pump();
    ok = save(QStringLiteral("13-empty-state-no-project.png")) && ok;

    std::printf("APP-SHELL-SCREENSHOTS ok=%d dir=%s\n", ok ? 1 : 0, qPrintable(directory));
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-app"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("VEdit"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VEdit application shell (Qt6/KTextEditor rewrite)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("state-dir"),
                      QStringLiteral("Directory for session.json and shortcuts.json."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("project"), QStringLiteral("Project root to display (optional)."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("theme"), QStringLiteral("KSyntaxHighlighting theme name."),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("selfcheck"),
                      QStringLiteral("Run non-interactive layout/theme/persistence checks and exit.")});
    parser.addOption({QStringLiteral("selfcheck-ffi"),
                      QStringLiteral("Run the Rust C ABI checks against this project fixture and exit."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("screenshots"),
                      QStringLiteral("Capture split-state screenshots into this directory and exit."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("no-session-restore"),
                      QStringLiteral("Ignore an existing session file at startup.")});
    parser.addOption({QStringLiteral("hold"), QStringLiteral("Keep the window for this many ms."),
                      QStringLiteral("ms"), QStringLiteral("0")});
    parser.process(app);

    ShellOptions options;
    options.stateDir = parser.value(QStringLiteral("state-dir"));
    options.projectRoot = parser.value(QStringLiteral("project"));
    options.themeName = parser.value(QStringLiteral("theme"));
    options.restoreSession = !parser.isSet(QStringLiteral("no-session-restore"));

    if (parser.isSet(QStringLiteral("selfcheck"))) {
        return runSelfCheck();
    }
    if (parser.isSet(QStringLiteral("selfcheck-ffi"))) {
        return ffi::runSelfCheck(parser.value(QStringLiteral("selfcheck-ffi")));
    }
    if (parser.isSet(QStringLiteral("screenshots"))) {
        return captureScreenshots(parser.value(QStringLiteral("screenshots")));
    }

    // Session restore is deferred until the lanes registered their panel
    // factories: setFactory() only affects FUTURE inserts, so restoring during
    // construction left every restored dock a shell placeholder (review M1 --
    // terminal and search). startSession() runs once the factories are ready.
    options.deferSessionRestore = true;
    ApplicationShell shell(options);
#if defined(VEDIT_HAVE_EDIT_CORE)
    // Slice 1 (AC-S1.1): the product editor is the KTextEditor editing kernel.
    // A deferred start makes the restored panes use it directly (no re-creation
    // of the demo pane).
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
#endif
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
    // Terminal panel lane (T35): the shell created the "terminal" panel spec with
    // an empty factory; the widget lives in the terminal lane.
    shell.panels()->setFactory(QStringLiteral("terminal"),
                               vedit::terminal::terminalPanelFactory());
    shell.startSession();
    shell.show();

    const int holdMs = parser.value(QStringLiteral("hold")).toInt();
    if (holdMs > 0) {
        QTimer::singleShot(holdMs, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
