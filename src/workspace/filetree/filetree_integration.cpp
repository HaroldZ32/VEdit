#include "filetree_integration.h"

#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)

#include "file_tree_widget.h"
#include "filetree_panel.h"

#include "app_shell.h"
#include "editor_area.h"
#include "panel_registry.h"

#include <QDebug>
#include <QDockWidget>
#include <QStatusBar>
#include <QString>

#endif

namespace vedit::workspace {

bool openFileInEditor(vedit::app::ApplicationShell &shell, const QString &path, QString *error)
{
#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)
    const auto fail = [&shell, error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        shell.statusBar()->showMessage(message, 5000);
        return false;
    };

    // The editor pane owns the open seam now: it dispatches to whatever
    // EditorProvider is installed (the KTextEditor kernel in the product, the
    // demo QPlainTextEdit in a provider-less build). The lane must not cast the
    // widget itself - that is what broke double click when the product editor
    // moved from the demo widget to KTextEditor (regression T60).
    vedit::app::EditorPane *pane =
        shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
    if (pane == nullptr) {
        return fail(QStringLiteral("Cannot open %1: no active editor pane").arg(path));
    }
    QString paneError;
    if (!pane->openFile(path, &paneError)) {
        return fail(paneError.isEmpty() ? QStringLiteral("Cannot open %1").arg(path) : paneError);
    }
    shell.statusBar()->showMessage(QStringLiteral("Opened %1").arg(path), 5000);
    return true;
#else
    (void)shell;
    (void)path;
    if (error != nullptr) {
        *error = QStringLiteral("openFileInEditor: the application shell is not part of this build");
    }
    return false;
#endif
}

void installFileTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)
    const QString projectRoot = options.projectRoot;

    // The factory is the one place a panel instance is born, so every instance -
    // the one replacing the startup placeholder and a dock re-inserted later -
    // carries the double-click open bridge. The tree itself keeps emitting
    // openFileRequested for a *file* only; a folder stays an expand/collapse.
    const auto factory = [projectRoot, &shell]() {
        QWidget *panel = makeFileTreePanel(projectRoot);
        if (auto *tree = qobject_cast<FileTreeWidget *>(panel)) {
            QObject::connect(tree, &FileTreeWidget::openFileRequested, &shell,
                             [&shell](const QString &path) { openFileInEditor(shell, path); });
        }
        return panel;
    };

    shell.panels()->setFactory(QStringLiteral("file-tree"), factory);

    // The shell inserts the file-tree dock at first start with placeholder
    // content; setFactory() only affects future inserts, so replace the
    // placeholder that is already in the dock (QDockWidget::setWidget keeps
    // the old widget alive, so it is deleted explicitly).
    bool placeholderReplaced = false;
    if (QDockWidget *dock = shell.panels()->dock(QStringLiteral("file-tree"))) {
        QWidget *placeholder = dock->widget();
        if (placeholder != nullptr
            && placeholder->objectName().startsWith(QStringLiteral("panel-placeholder-"))) {
            QWidget *panel = factory();
            dock->setWidget(panel);
            delete placeholder;
            placeholderReplaced = true;
        }
    }
    qInfo("filetree: installFileTree projectRoot='%s' dockInserted=%d placeholderReplaced=%d",
          qPrintable(projectRoot), shell.panels()->dock(QStringLiteral("file-tree")) != nullptr ? 1 : 0,
          placeholderReplaced ? 1 : 0);
#else
    (void)shell;
    (void)options;
#endif
}

FileTreeWidget *fileTreeWidgetForShell(const vedit::app::ApplicationShell &shell)
{
#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)
    return shell.findChild<FileTreeWidget *>();
#else
    (void)shell;
    return nullptr;
#endif
}

} // namespace vedit::workspace
