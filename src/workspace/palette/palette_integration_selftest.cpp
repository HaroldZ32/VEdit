// Integration selftest: the palette inside the REAL ApplicationShell (T22).
//
// Proves the acceptance parts that need the product shell:
//   * Ctrl+P is registered through the shell's CommandRegistry (so IShortcutStore
//     rebinding works) and appears in the File menu;
//   * triggering the command opens the palette over the shell window;
//   * typing filters the shared file index, Enter activates the file and the
//     editor bridge receives it;
//   * opening the palette twice walks the disk exactly once (shared index);
//   * the explicit "No matching files" empty state renders.
//
// Built only when `vedit_app_shell` is part of the same CMake build (the root
// build does, a standalone palette configure does not).
#include "command_palette.h"
#include "file_index.h"
#include "palette_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include <cstdio>

namespace {

using namespace vedit::workspace;

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", qPrintable(name), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
}

bool saveWidget(QWidget *widget, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool ok = widget->grab().save(path);
    std::printf("SCREENSHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    std::fflush(stdout);
    return ok;
}

void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

bool actionInFileMenu(vedit::app::ApplicationShell &shell, QAction *action)
{
    QMenuBar *menuBar = shell.menuBar();
    if (menuBar == nullptr || action == nullptr) {
        return false;
    }
    const QString label = shell.commands()->menuLabel(QStringLiteral("file"));
    const QList<QAction *> barActions = menuBar->actions();
    for (QAction *menuAction : barActions) {
        if (menuAction->text() == label && menuAction->menu() != nullptr) {
            return menuAction->menu()->actions().contains(action);
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-palette-integration-selftest"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Ctrl+P palette integration selftest inside the VEdit shell."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("screenshots"),
                      QStringLiteral("Write shell/palette screenshots into this directory."),
                      QStringLiteral("dir")});
    parser.process(app);

    QTemporaryDir temp;
    check(QStringLiteral("fixture.temp-dir"), temp.isValid());
    const QString projectRoot = temp.path() + QStringLiteral("/project");
    writeFile(projectRoot + QStringLiteral("/src/main.cpp"),
              QByteArray("int main() { return 0; } // src/main.cpp\n"));
    writeFile(projectRoot + QStringLiteral("/src/util/helper.cpp"), QByteArray("// helper\n"));
    writeFile(projectRoot + QStringLiteral("/docs/readme.md"), QByteArray("# readme\n"));
    writeFile(projectRoot + QStringLiteral("/top.v"), QByteArray("module top; endmodule\n"));
    writeFile(projectRoot + QStringLiteral("/.git/hidden.cpp"), QByteArray("// hidden\n"));

    vedit::app::ShellOptions options;
    options.stateDir = temp.path() + QStringLiteral("/state");
    options.projectRoot = projectRoot;
    options.restoreSession = false;
    options.showDefaultPanels = false;

    vedit::app::ApplicationShell shell(options);
    shell.resize(1280, 800);
    shell.show();
    pump();

    auto *integration = vedit::workspace::PaletteIntegration::install(shell, options.projectRoot);
    check(QStringLiteral("integration.installed"), integration != nullptr);
    if (integration == nullptr) {
        std::printf("INTEGRATION-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    QAction *action = integration->action();
    check(QStringLiteral("integration.command-registered"),
          shell.commands()->action(QStringLiteral("file.goto-file")) != nullptr);
    check(QStringLiteral("integration.command-in-registry"),
          shell.commands()->ids().contains(QStringLiteral("file.goto-file")));
    check(QStringLiteral("integration.action"), action != nullptr);
    check(QStringLiteral("integration.shortcut-ctrl-p"),
          action != nullptr && action->shortcut() == QKeySequence(QStringLiteral("Ctrl+P")));
    check(QStringLiteral("integration.in-file-menu"), actionInFileMenu(shell, action));
    check(QStringLiteral("integration.idempotent"),
          vedit::workspace::PaletteIntegration::install(shell, options.projectRoot) == integration);

    // Ctrl+P (the QAction is what the window shortcut triggers).
    action->trigger();
    pump();
    vedit::workspace::CommandPalette *palette = integration->palette();
    check(QStringLiteral("integration.ctrl-p-opens"), palette->isVisible());
    check(QStringLiteral("integration.index-count"), integration->fileIndex()->size() == 4);
    check(QStringLiteral("integration.hidden-skipped"),
          !integration->fileIndex()->displayPaths().contains(QStringLiteral(".git/hidden.cpp")));

    palette->setQueryText(QStringLiteral("main"));
    pump();
    check(QStringLiteral("integration.filters"),
          palette->resultCount() == 1 &&
              palette->visibleResults().first() == QStringLiteral("src/main.cpp"));

    QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(palette->queryEdit(), &returnKey);
    pump();
    check(QStringLiteral("integration.enter-closes"), !palette->isVisible());
    const QString mainPath = projectRoot + QStringLiteral("/src/main.cpp");
    check(QStringLiteral("integration.enter-opened"), integration->openedPath() == mainPath);

    // The editor seam is not connected yet: the adapter loads the demo editor.
    QPlainTextEdit *editor = nullptr;
    if (vedit::app::EditorPane *pane = shell.editorArea()->activePane()) {
        editor = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    }
    check(QStringLiteral("integration.demo-editor"), editor != nullptr);
    check(QStringLiteral("integration.demo-content"),
          editor != nullptr && editor->toPlainText().contains(QStringLiteral("src/main.cpp")));
    check(QStringLiteral("integration.demo-property"),
          editor != nullptr &&
              editor->property("vedit.openFilePath").toString() == mainPath);

    // Second palette open: the shared index must not walk the disk again.
    action->trigger();
    pump();
    palette->setQueryText(QStringLiteral("helper"));
    pump();
    QApplication::sendEvent(palette->queryEdit(), &returnKey);
    pump();
    check(QStringLiteral("integration.second-open"),
          integration->openedPath() == projectRoot + QStringLiteral("/src/util/helper.cpp"));
    check(QStringLiteral("integration.single-scan"),
          integration->fileIndex()->scanCount() == 1);

    // Explicit empty state.
    action->trigger();
    pump();
    palette->setQueryText(QStringLiteral("zzzzz"));
    pump();
    check(QStringLiteral("integration.empty-state"),
          palette->emptyStateVisible() &&
              palette->emptyStateText() == vedit::workspace::CommandPalette::noResultsText());

    const QString screenshotDir = parser.value(QStringLiteral("screenshots"));
    if (!screenshotDir.isEmpty()) {
        palette->resize(820, 460);
        saveWidget(palette, screenshotDir + QStringLiteral("/20-integration-empty.png"));
        palette->setQueryText(QStringLiteral("ma"));
        pump();
        saveWidget(palette, screenshotDir + QStringLiteral("/21-integration-palette.png"));
        palette->closePalette();
        pump();
        saveWidget(&shell, screenshotDir + QStringLiteral("/22-integration-shell.png"));
    }

    // Shortcut rebinding flows through the shell's IShortcutStore seam.
    vedit::app::IShortcutStore *store = shell.shortcutStore();
    check(QStringLiteral("integration.shortcut-store"), store != nullptr);
    if (store != nullptr) {
        store->setShortcut(QStringLiteral("file.goto-file"),
                           QKeySequence(QStringLiteral("Ctrl+Shift+P")));
        shell.commands()->refreshShortcuts();
        check(QStringLiteral("integration.rebinding"),
              action->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+P")));
        store->clearShortcut(QStringLiteral("file.goto-file"));
        shell.commands()->refreshShortcuts();
        check(QStringLiteral("integration.rebinding-restored"),
              action->shortcut() == QKeySequence(QStringLiteral("Ctrl+P")));
    }

    std::printf("INTEGRATION-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
