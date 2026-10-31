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
#include "panel_registry.h"
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

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

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
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

// ---------------------------------------------------------------------------
// self-check
// ---------------------------------------------------------------------------

int runSelfCheck()
{
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

    std::printf("APP-SHELL-SELFTEST ok=%d checks=%d failures=%d state=%s\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, qPrintable(stateDir));
    return g_failures == 0 ? 0 : 1;
}

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
    if (parser.isSet(QStringLiteral("screenshots"))) {
        return captureScreenshots(parser.value(QStringLiteral("screenshots")));
    }

    ApplicationShell shell(options);
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir});
    shell.show();

    const int holdMs = parser.value(QStringLiteral("hold")).toInt();
    if (holdMs > 0) {
        QTimer::singleShot(holdMs, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
