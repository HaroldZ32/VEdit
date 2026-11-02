#pragma once

// The file tree panel (lane: workspace, task T21): a dock widget that browses a
// folder, expands/collapses lazily, opens a file on double click, and performs
// New / Rename / Delete on the real filesystem with a diagnosable failure path.
//
// The widget is self-contained: it owns its model, its toolbar and its status
// line. The shell plugs it in through the panel seam (PanelRegistry id
// "file-tree"); the only signal it needs from the host is openFileRequested().

#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QLabel;
class QModelIndex;
class QPoint;
class QStackedWidget;
class QToolBar;
class QTreeView;

namespace vedit::workspace {

class FileTreeModel;

class FileTreeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit FileTreeWidget(QWidget *parent = nullptr);
    ~FileTreeWidget() override;

    // Opening a folder is the panel's "project" input; an empty path resets the
    // panel to its "no folder open" empty state.
    void openDirectory(const QString &path);
    void closeDirectory();
    QString rootPath() const;

    FileTreeModel *model() const;
    QTreeView *view() const;

    // Operations, used both by the toolbar/context menu and directly by the
    // demo and the tests. On failure `error` (when given) receives the same
    // one-line diagnostic the status line shows; nothing is changed on disk and
    // the tree is not touched.
    bool createFile(const QString &parentDirectory, const QString &name, QString *error = nullptr);
    bool createFolder(const QString &parentDirectory, const QString &name, QString *error = nullptr);
    bool renameEntry(const QString &path, const QString &newName, QString *error = nullptr);
    bool deleteEntry(const QString &path, bool recursive, QString *error = nullptr);

    QString statusText() const;
    QString emptyStateText() const; // "" when the panel is not showing an empty state
    bool emptyStateVisible() const;
    QStringList diagnostics() const;

    // Modal dialogs are what the real UI uses; tests and the screenshot demo
    // turn them off and call the operations with explicit names.
    void setInteractiveDialogs(bool enabled);
    bool interactiveDialogs() const;

    // Same behaviour as a double click, for callers that have an index instead
    // of a mouse event (the model view still handles the real double click).
    void activateIndex(const QModelIndex &index);
    QModelIndex currentIndex() const;
    void selectPath(const QString &path);

signals:
    void openFileRequested(const QString &path);
    void directoryOpened(const QString &path);
    void operationFailed(const QString &message);

private:
    void buildUi();
    void applyRootState();
    void setStatus(const QString &message, bool error = false);
    void reportFailure(const QString &error, const QString &detail);
    void refreshAfterOperation(const QString &directory, const QString &selectPath);
    QString targetDirectory() const;
    QString targetEntryPath() const;

    void onOpenFolder();
    void onNewFile();
    void onNewFolder();
    void onRename();
    void onDelete();
    void onRefresh();
    void onCollapseAll();
    void onActivated(const QModelIndex &index);
    void onCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void showKindForIndex(const QModelIndex &index);
    void onContextMenu(const QPoint &pos);

    FileTreeModel *model_ = nullptr;
    QTreeView *view_ = nullptr;
    QToolBar *toolBar_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *treePage_ = nullptr;
    QWidget *emptyPage_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QLabel *emptyPageLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QAction *openAction_ = nullptr;
    QAction *newFileAction_ = nullptr;
    QAction *newFolderAction_ = nullptr;
    QAction *renameAction_ = nullptr;
    QAction *deleteAction_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *collapseAction_ = nullptr;
    bool interactive_ = true;
    QString emptyText_;
    bool emptyVisible_ = false;
    QStringList diagnostics_;
};

} // namespace vedit::workspace
