#include "filetree_integration.h"

#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)

#include "file_tree_widget.h"
#include "filetree_panel.h"

#include "app_shell.h"
#include "panel_registry.h"

#include <QDebug>
#include <QDockWidget>
#include <QStatusBar>
#include <QString>

#endif

namespace vedit::workspace {

void installFileTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)
    const QString projectRoot = options.projectRoot;
    const auto factory = [projectRoot]() { return makeFileTreePanel(projectRoot); };

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
            auto *tree = qobject_cast<FileTreeWidget *>(panel);
            if (tree != nullptr) {
                // Double-click reporting seam: the editor lane replaces this
                // bridge; until then the path is visible in the status bar.
                QObject::connect(tree, &FileTreeWidget::openFileRequested, &shell,
                                 [&shell](const QString &path) {
                                     shell.statusBar()->showMessage(
                                         QStringLiteral("Open %1").arg(path), 5000);
                                 });
            }
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
