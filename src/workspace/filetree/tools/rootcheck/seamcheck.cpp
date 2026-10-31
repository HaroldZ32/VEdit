// Root-context seam check (task T21): proves the file tree panel really becomes
// the shell's `file-tree` dock content through installFileTree(), without the
// repository's src/app files being modified by this lane.

#include "app_shell.h"
#include "panel_registry.h"

#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "filetree_integration.h"
#include "integration.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStatusBar>
#include <QTreeView>

#include <cstdio>

using vedit::app::ApplicationShell;
using vedit::app::ShellOptions;
using vedit::workspace::FileTreeWidget;
using vedit::workspace::IntegrationOptions;

namespace {

void pump(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-filetree-seamcheck"));

    QString project;
    QString stateDir;
    QString screenshot;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const auto value = [&args, &i]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (arg == QLatin1String("--project")) {
            project = value();
        } else if (arg == QLatin1String("--state")) {
            stateDir = value();
        } else if (arg == QLatin1String("--screenshot")) {
            screenshot = value();
        } else {
            std::printf("unknown argument: %s\n", qPrintable(arg));
            return 2;
        }
    }
    if (project.isEmpty() || stateDir.isEmpty()) {
        std::printf("usage: vedit-filetree-seamcheck --project <dir> --state <dir>"
                    " [--screenshot <png>]\n");
        return 2;
    }
    QDir().mkpath(stateDir);

    ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = true;
    ApplicationShell shell(options);
    shell.resize(1280, 800);

    QDockWidget *dock = shell.panels()->dock(QStringLiteral("file-tree"));
    const QWidget *beforeInstall = dock != nullptr ? dock->widget() : nullptr;
    std::printf("SEAM dock-inserted=%d content-before-install=%s\n", dock != nullptr ? 1 : 0,
                beforeInstall != nullptr ? qPrintable(beforeInstall->objectName()) : "none");

    IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = stateDir;
    // The production entry point: src/app/main.cpp calls exactly this, and it
    // dispatches to installFileTree() through src/workspace/integration.cpp.
    vedit::workspace::installPanels(shell, integration);
    vedit::workspace::installPanels(shell, integration); // idempotent by design

    auto *panel = qobject_cast<FileTreeWidget *>(dock != nullptr ? dock->widget() : nullptr);
    auto *found = vedit::workspace::fileTreeWidgetForShell(shell);
    std::printf("SEAM panel-installed=%d dock-content-is-panel=%d find-child=%d root=%s\n",
                panel != nullptr ? 1 : 0, panel != nullptr ? 1 : 0, found != nullptr ? 1 : 0,
                panel != nullptr ? qPrintable(panel->rootPath()) : "none");

    shell.show();
    pump(250);
    if (panel != nullptr) {
        shell.panels()->hidePanel(QStringLiteral("outline"));
        pump(120);
    }

    // The double-click bridge: the panel reports a path, the shell shows it.
    bool bridge = false;
    if (panel != nullptr) {
        const QModelIndex fileIndex = panel->model()->indexForPath(
            QDir(project).filePath(QStringLiteral("rtl/top.v")), true);
        std::printf("SEAM file-index-valid=%d\n", fileIndex.isValid() ? 1 : 0);
        panel->activateIndex(fileIndex);
        pump(80);
        const QString message = shell.statusBar()->currentMessage();
        bridge = message.contains(QStringLiteral("Open "))
            && message.contains(QStringLiteral("top.v"));
        std::printf("SEAM status-after-activate=\"%s\"\n", qPrintable(message));

        // Laziness is observable from the real shell too.
        const QModelIndex rootIndex = panel->model()->index(0, 0, QModelIndex());
        std::printf("SEAM rows=%d scanned-dirs=%d\n",
                    panel->model()->rowCount(rootIndex), panel->model()->scannedDirectories());
    }

    bool ok = panel != nullptr && found != nullptr
        && panel->rootPath() == QDir(project).absolutePath() && bridge;

    shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
    pump(150);
    if (!screenshot.isEmpty()) {
        const bool saved = shell.grab().save(screenshot);
        std::printf("SCREENSHOT ok=%d %s\n", saved ? 1 : 0, qPrintable(screenshot));
        ok = saved && ok;
    }

    std::printf("FILETREE-SEAMCHECK ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
