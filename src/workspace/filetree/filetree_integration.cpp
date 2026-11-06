#include "filetree_integration.h"

#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)

#include "file_tree_widget.h"
#include "filetree_panel.h"

#include "app_shell.h"
#include "editor_area.h"
#include "panel_registry.h"

#include <QDebug>
#include <QVariantMap>

#include <memory>
#include <QDockWidget>
#include <QStatusBar>
#include <QString>
#include <QCoreApplication>

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
        return fail(QCoreApplication::translate("VEdit", "Cannot open %1: no active editor pane").arg(path));
    }
    QString paneError;
    if (!pane->openFile(path, &paneError)) {
        return fail(paneError.isEmpty() ? QCoreApplication::translate("VEdit", "Cannot open %1").arg(path) : paneError);
    }
    shell.statusBar()->showMessage(
        QCoreApplication::translate("VEdit", "Opened %1").arg(path), 5000);
    return true;
#else
    (void)shell;
    (void)path;
    if (error != nullptr) {
        *error = QCoreApplication::translate("VEdit", "openFileInEditor: the application shell is not part of this build");
    }
    return false;
#endif
}

void installFileTree(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
#if defined(VEDIT_FILETREE_HAVE_APP_SHELL)
    (void)options;

    // The factory is the one place a panel instance is born, so every instance -
    // the one replacing the startup placeholder and a dock re-inserted later -
    // carries the double-click open bridge. The tree itself keeps emitting
    // openFileRequested for a *file* only; a folder stays an expand/collapse.
    //
    // AC-1.1/AC-1.2 (batch 1): the root is read from the shell INSIDE the
    // factory, never captured from IntegrationOptions. The shell resolves the
    // persisted session root before the lanes install (so an ordinary start has
    // it), and a dock re-inserted after a runtime switch must start at the NEW
    // root. The folder this panel opens becomes the workspace root through
    // setProjectRoot(), which is the switch AC-1.2 observes.
    const auto factory = [&shell]() {
        QWidget *panel = makeFileTreePanel(shell.projectRoot());
        if (auto *tree = qobject_cast<FileTreeWidget *>(panel)) {
            QObject::connect(tree, &FileTreeWidget::openFileRequested, &shell,
                             [&shell](const QString &path) { openFileInEditor(shell, path); });
            QObject::connect(tree, &FileTreeWidget::directoryOpened, &shell,
                             [&shell](const QString &path) { shell.setProjectRoot(path); });
            // item 10: the dots come from the diagnostics lane's R2 merged view,
            // published through the shell-property seam (`vedit.diagnostics.*`).
            // The lane never links the file tree and the tree never rescans the
            // project: a status is one hash lookup, the repaint follows a
            // revision counter.
            // The shell property is a whole map; converting it per ROW would be
            // O(files) per painted item. The cached copy is rebuilt only when
            // the diagnostics view revision moves, so a repaint is one hash
            // lookup and the project is never rescanned.
            struct StatusCache {
                QHash<QString, int> map;
                quint64 revision = 0;
            };
            auto cache = std::make_shared<StatusCache>();
            tree->setFileStatusSource(
                [&shell, cache](const QString &path) -> int {
                    const quint64 revision =
                        shell.property("vedit.diagnostics.viewRevision").toULongLong();
                    if (revision != cache->revision) {
                        cache->revision = revision;
                        cache->map.clear();
                        const QVariantMap map =
                            shell.property("vedit.diagnostics.fileStatus").toMap();
                        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                            cache->map.insert(it.key(), it.value().toInt());
                        }
                    }
                    return cache->map.value(path, 0);
                },
                [&shell]() -> quint64 {
                    return shell.property("vedit.diagnostics.viewRevision").toULongLong();
                });
        }
        return panel;
    };

    shell.panels()->setFactory(QStringLiteral("file-tree"), factory);

    // A runtime switch rebinds a tree that ALREADY exists (the dock keeps its
    // widget): openDirectory() is the same load the panel's own Open Folder runs.
    // It emits directoryOpened again, but setProjectRoot() ignores an unchanged
    // root, so this cannot loop. The factory above covers a later re-insert.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [&shell](const QString &root) {
                         for (FileTreeWidget *tree : shell.findChildren<FileTreeWidget *>()) {
                             if (tree->rootPath() != root) {
                                 tree->openDirectory(root);
                             }
                         }
                     });

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
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("filetree: installFileTree projectRoot='%s' dockInserted=%d placeholderReplaced=%d",
              qPrintable(shell.projectRoot()),
              shell.panels()->dock(QStringLiteral("file-tree")) != nullptr ? 1 : 0,
              placeholderReplaced ? 1 : 0);
    }
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
