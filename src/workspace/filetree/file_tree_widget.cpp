#include "file_tree_widget.h"

#include "file_kind.h"
#include "file_ops.h"
#include "file_status_delegate.h"
#include "file_tree_model.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QCoreApplication>

namespace vedit::workspace {
namespace {

constexpr int kMaxDiagnostics = 200;

} // namespace

FileTreeWidget::FileTreeWidget(QWidget *parent)
    : QWidget(parent)
{
    model_ = new FileTreeModel(this);
    buildUi();
}

FileTreeWidget::~FileTreeWidget() = default;

void FileTreeWidget::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    toolBar_ = new QToolBar(this);
    toolBar_->setObjectName(QStringLiteral("fileTreeToolBar"));
    toolBar_->setMovable(false);
    toolBar_->setFloatable(false);
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar_->setIconSize(QSize(16, 16));

    openAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "Open..."));
    openAction_->setToolTip(QCoreApplication::translate("VEdit", "Open a folder"));
    openAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));

    newFileAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "New File"));
    newFileAction_->setToolTip(QCoreApplication::translate("VEdit", "Create a file here (Ctrl+N)"));
    newFileAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));

    newFolderAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "New Folder"));
    newFolderAction_->setToolTip(QCoreApplication::translate("VEdit", "Create a folder here (Ctrl+Shift+N)"));
    newFolderAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));

    renameAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "Rename"));
    renameAction_->setToolTip(QCoreApplication::translate("VEdit", "Rename the selection (F2)"));
    renameAction_->setShortcut(QKeySequence(Qt::Key_F2));

    deleteAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "Delete"));
    deleteAction_->setToolTip(QCoreApplication::translate("VEdit", "Delete the selection (Del)"));
    deleteAction_->setShortcut(QKeySequence(Qt::Key_Delete));

    refreshAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "Refresh"));
    refreshAction_->setToolTip(QCoreApplication::translate("VEdit", "Re-read the folder from disk (F5)"));
    refreshAction_->setShortcut(QKeySequence(Qt::Key_F5));

    collapseAction_ = toolBar_->addAction(QCoreApplication::translate("VEdit", "Collapse"));
    collapseAction_->setToolTip(QCoreApplication::translate("VEdit", "Collapse every expanded folder"));

    for (QAction *action : {openAction_, newFileAction_, newFolderAction_, renameAction_,
                            deleteAction_, refreshAction_, collapseAction_}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

    stack_ = new QStackedWidget(this);

    treePage_ = new QWidget(stack_);
    auto *treeLayout = new QVBoxLayout(treePage_);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);

    view_ = new QTreeView(treePage_);
    view_->setObjectName(QStringLiteral("fileTreeView"));
    view_->setModel(model_);
    view_->setHeaderHidden(true);
    view_->setUniformRowHeights(true);
    view_->setAnimated(false);
    view_->setExpandsOnDoubleClick(true);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setTextElideMode(Qt::ElideMiddle);
    // item 10: the status dot is painted between the icon and the name.
    statusDelegate_ = new FileStatusDotDelegate(this);
    view_->setItemDelegate(statusDelegate_);
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        if (model_ == nullptr) {
            return;
        }
        // RM-9: the project set moves rarely (one revision per expansion). The
        // timer reads it only when the revision changed - never a rescan per
        // tick - and the synthetic group then carries the same status dots.
        if (projectFilesSource_ && projectFilesRevisionSource_) {
            const quint64 revision = projectFilesRevisionSource_();
            if (revision != projectFilesRevision_) {
                projectFilesRevision_ = revision;
                model_->setFilelistFiles(projectFilesSource_());
            }
        }
        model_->syncFileStatuses();
    });
    statusTimer_->start();
    treeLayout->addWidget(view_, 1);

    hintLabel_ = new QLabel(treePage_);
    hintLabel_->setObjectName(QStringLiteral("fileTreeEmptyHint"));
    hintLabel_->setAlignment(Qt::AlignCenter);
    hintLabel_->setWordWrap(true);
    hintLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hintLabel_->setContentsMargins(8, 8, 8, 8);
    hintLabel_->hide();
    treeLayout->addWidget(hintLabel_);
    stack_->addWidget(treePage_);

    emptyPage_ = new QWidget(stack_);
    auto *emptyLayout = new QVBoxLayout(emptyPage_);
    emptyPageLabel_ = new QLabel(emptyPage_);
    emptyPageLabel_->setObjectName(QStringLiteral("fileTreeEmptyState"));
    emptyPageLabel_->setAlignment(Qt::AlignCenter);
    emptyPageLabel_->setWordWrap(true);
    // A long diagnostic (a full path) must wrap inside the dock, not be clipped:
    // an ignored horizontal policy lets the layout width win over sizeHint().
    emptyPageLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyPageLabel_);
    emptyLayout->addStretch(1);
    stack_->addWidget(emptyPage_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("fileTreeStatus"));
    statusLabel_->setContentsMargins(6, 3, 6, 3);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(toolBar_);
    layout->addWidget(stack_, 1);
    layout->addWidget(statusLabel_);

    connect(openAction_, &QAction::triggered, this, &FileTreeWidget::onOpenFolder);
    connect(newFileAction_, &QAction::triggered, this, &FileTreeWidget::onNewFile);
    connect(newFolderAction_, &QAction::triggered, this, &FileTreeWidget::onNewFolder);
    connect(renameAction_, &QAction::triggered, this, &FileTreeWidget::onRename);
    connect(deleteAction_, &QAction::triggered, this, &FileTreeWidget::onDelete);
    connect(refreshAction_, &QAction::triggered, this, &FileTreeWidget::onRefresh);
    connect(collapseAction_, &QAction::triggered, this, &FileTreeWidget::onCollapseAll);
    connect(view_, &QTreeView::doubleClicked, this, &FileTreeWidget::onActivated);
    connect(view_, &QTreeView::customContextMenuRequested, this, &FileTreeWidget::onContextMenu);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &FileTreeWidget::onCurrentChanged);
    connect(model_, &FileTreeModel::rootStateChanged, this, &FileTreeWidget::applyRootState);
    connect(model_, &FileTreeModel::loadFailed, this,
            [this](const QString &path, const QString &error) {
                const QString message = QCoreApplication::translate("VEdit", "Cannot read '%1': %2").arg(path, error);
                diagnostics_.append(message);
                while (diagnostics_.size() > kMaxDiagnostics) {
                    diagnostics_.removeFirst();
                }
                setStatus(message, true);
            });

    applyRootState();
}

// ---------------------------------------------------------------------------
// opening folders
// ---------------------------------------------------------------------------

void FileTreeWidget::openDirectory(const QString &path)
{
    diagnostics_.clear();
    model_->setRootPath(path);
    view_->setCurrentIndex(QModelIndex());
    statusLabel_->clear();
    applyRootState();

    const QModelIndex rootIndex = model_->index(0, 0, QModelIndex());
    if (rootIndex.isValid()) {
        view_->setExpanded(rootIndex, true);
        model_->ensureLoaded(rootIndex);
    }
    applyRootState();
    if (model_->rootState() == FileTreeModel::RootState::Ready) {
        setStatus(QCoreApplication::translate("VEdit", "Opened '%1'").arg(QFileInfo(model_->rootPath()).fileName()));
    }
    emit directoryOpened(model_->rootPath());
}

void FileTreeWidget::closeDirectory()
{
    openDirectory(QString());
}

QString FileTreeWidget::rootPath() const
{
    return model_->rootPath();
}

FileTreeModel *FileTreeWidget::model() const
{
    return model_;
}

void FileTreeWidget::setFileStatusSource(std::function<int(const QString &)> status,
                                         std::function<quint64()> revision)
{
    if (model_ != nullptr) {
        model_->setFileStatusSource(std::move(status), std::move(revision));
    }
}

void FileTreeWidget::setProjectFilesSource(std::function<QStringList()> files,
                                           std::function<quint64()> revision)
{
    projectFilesSource_ = std::move(files);
    projectFilesRevisionSource_ = std::move(revision);
    // Pull once now so a tree that already exists shows the group without
    // waiting for the next timer tick.
    if (projectFilesSource_ && projectFilesRevisionSource_ && model_ != nullptr) {
        projectFilesRevision_ = projectFilesRevisionSource_();
        model_->setFilelistFiles(projectFilesSource_());
    }
}

FileStatusDotDelegate *FileTreeWidget::statusDelegate() const
{
    return statusDelegate_;
}

QTreeView *FileTreeWidget::view() const
{
    return view_;
}

// ---------------------------------------------------------------------------
// operations
// ---------------------------------------------------------------------------

bool FileTreeWidget::createFile(const QString &parentDirectory, const QString &name, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    const QString directory = parentDirectory.isEmpty() ? targetDirectory() : parentDirectory;
    if (directory.isEmpty()) {
        reportFailure(QCoreApplication::translate("VEdit", "Open a folder first."), QString());
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Open a folder first.");
        }
        return false;
    }

    const fileops::FileOpResult result = fileops::createFile(directory, name);
    if (!result.ok) {
        reportFailure(result.error, result.detail);
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }

    refreshAfterOperation(directory, result.path);
    setStatus(QCoreApplication::translate("VEdit", "Created file '%1' (%2)")
                  .arg(name, fileKindLabel(fileKindForPath(result.path, false))));
    return true;
}

bool FileTreeWidget::createFolder(const QString &parentDirectory, const QString &name, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (error != nullptr) {
        error->clear();
    }
    const QString directory = parentDirectory.isEmpty() ? targetDirectory() : parentDirectory;
    if (directory.isEmpty()) {
        reportFailure(QCoreApplication::translate("VEdit", "Open a folder first."), QString());
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Open a folder first.");
        }
        return false;
    }

    const fileops::FileOpResult result = fileops::createDirectory(directory, name);
    if (!result.ok) {
        reportFailure(result.error, result.detail);
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }

    refreshAfterOperation(directory, result.path);
    setStatus(QCoreApplication::translate("VEdit", "Created folder '%1'").arg(name));
    return true;
}

bool FileTreeWidget::renameEntry(const QString &path, const QString &newName, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (path.isEmpty()) {
        reportFailure(QCoreApplication::translate("VEdit", "Select a file or folder first."), QString());
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Select a file or folder first.");
        }
        return false;
    }

    const QString oldName = QFileInfo(path).fileName();
    const QString directory = QFileInfo(path).dir().path();
    const fileops::FileOpResult result = fileops::renameEntry(path, newName);
    if (!result.ok) {
        reportFailure(result.error, result.detail);
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }

    refreshAfterOperation(directory, result.path);
    setStatus(QCoreApplication::translate("VEdit", "Renamed '%1' to '%2'").arg(oldName, newName));
    return true;
}

bool FileTreeWidget::deleteEntry(const QString &path, bool recursive, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (path.isEmpty()) {
        reportFailure(QCoreApplication::translate("VEdit", "Select a file or folder first."), QString());
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "Select a file or folder first.");
        }
        return false;
    }
    if (path == model_->rootPath()) {
        const QString message = QCoreApplication::translate("VEdit", "Cannot delete the folder that is open. Close it first.");
        reportFailure(message, QString());
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    const QFileInfo info(path);
    const QString name = info.fileName();
    const QString directory = info.dir().path();
    const fileops::FileOpResult result = fileops::removeEntry(path, recursive);
    if (!result.ok) {
        reportFailure(result.error, result.detail);
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }

    refreshAfterOperation(directory, QString());
    setStatus(QCoreApplication::translate("VEdit", "Deleted '%1'").arg(name));
    return true;
}

void FileTreeWidget::refreshAfterOperation(const QString &directory, const QString &entryToSelect)
{
    model_->refreshDirectory(directory);
    if (!entryToSelect.isEmpty()) {
        selectPath(entryToSelect);
    }
}

void FileTreeWidget::reportFailure(const QString &error, const QString &detail)
{
    QString message = error;
    if (!detail.isEmpty()) {
        message += QStringLiteral(" (") + detail + QLatin1Char(')');
    }
    setStatus(message, true);
    diagnostics_.append(message);
    while (diagnostics_.size() > kMaxDiagnostics) {
        diagnostics_.removeFirst();
    }
    emit operationFailed(message);
}

// ---------------------------------------------------------------------------
// selection helpers
// ---------------------------------------------------------------------------

QString FileTreeWidget::targetDirectory() const
{
    const QModelIndex current = view_->currentIndex();
    if (current.isValid()) {
        if (model_->isDirectory(current)) {
            return model_->pathForIndex(current);
        }
        return QFileInfo(model_->pathForIndex(current)).dir().path();
    }
    return model_->rootPath();
}

QString FileTreeWidget::targetEntryPath() const
{
    const QModelIndex current = view_->currentIndex();
    return current.isValid() ? model_->pathForIndex(current) : QString();
}

QModelIndex FileTreeWidget::currentIndex() const
{
    return view_->currentIndex();
}

void FileTreeWidget::selectPath(const QString &path)
{
    const QModelIndex index = model_->indexForPath(path, true);
    if (!index.isValid()) {
        return;
    }
    for (QModelIndex ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
        view_->setExpanded(ancestor, true);
    }
    view_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect
                                                       | QItemSelectionModel::Rows);
    view_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    showKindForIndex(index);
}

void FileTreeWidget::activateIndex(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    if (model_->isDirectory(index)) {
        view_->setExpanded(index, true);
        model_->ensureLoaded(index);
        return;
    }
    emit openFileRequested(model_->pathForIndex(index));
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------

void FileTreeWidget::onOpenFolder()
{
    if (!interactive_) {
        return;
    }
    const QString start = model_->rootPath().isEmpty() ? QDir::homePath() : model_->rootPath();
    const QString directory =
        QFileDialog::getExistingDirectory(this, tr("Open Folder"), start);
    if (directory.isEmpty()) {
        return;
    }
    openDirectory(directory);
}

void FileTreeWidget::onNewFile()
{
    const QString directory = targetDirectory();
    if (directory.isEmpty()) {
        setStatus(QCoreApplication::translate("VEdit", "Open a folder first."), true);
        return;
    }
    QString name = QStringLiteral("untitled.v");
    if (interactive_) {
        bool accepted = false;
        name = QInputDialog::getText(this, tr("New File"), tr("File name:"), QLineEdit::Normal, name,
                                     &accepted);
        if (!accepted) {
            return;
        }
    }
    createFile(directory, name, nullptr);
}

void FileTreeWidget::onNewFolder()
{
    const QString directory = targetDirectory();
    if (directory.isEmpty()) {
        setStatus(QCoreApplication::translate("VEdit", "Open a folder first."), true);
        return;
    }
    QString name = QStringLiteral("new_folder");
    if (interactive_) {
        bool accepted = false;
        name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal,
                                     name, &accepted);
        if (!accepted) {
            return;
        }
    }
    createFolder(directory, name, nullptr);
}

void FileTreeWidget::onRename()
{
    const QString path = targetEntryPath();
    if (path.isEmpty()) {
        setStatus(QCoreApplication::translate("VEdit", "Select a file or folder first."), true);
        return;
    }
    QString name = QFileInfo(path).fileName();
    if (interactive_) {
        bool accepted = false;
        name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, name,
                                     &accepted);
        if (!accepted) {
            return;
        }
    }
    renameEntry(path, name, nullptr);
}

void FileTreeWidget::onDelete()
{
    const QString path = targetEntryPath();
    if (path.isEmpty()) {
        setStatus(QCoreApplication::translate("VEdit", "Select a file or folder first."), true);
        return;
    }
    if (path == model_->rootPath()) {
        reportFailure(QCoreApplication::translate("VEdit", "Cannot delete the folder that is open. Close it first."),
                      QString());
        return;
    }
    const bool recursive = QFileInfo(path).isDir();
    if (interactive_) {
        const QString question =
            recursive ? tr("Delete the folder '%1' and everything inside it?")
                            .arg(QFileInfo(path).fileName())
                      : tr("Delete the file '%1'?").arg(QFileInfo(path).fileName());
        if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes) {
            return;
        }
    }
    deleteEntry(path, recursive, nullptr);
}

void FileTreeWidget::onRefresh()
{
    if (model_->rootPath().isEmpty()) {
        return;
    }
    const QString path = model_->rootPath();
    model_->refresh();
    const QModelIndex rootIndex = model_->index(0, 0, QModelIndex());
    if (rootIndex.isValid()) {
        view_->setExpanded(rootIndex, true);
        model_->ensureLoaded(rootIndex);
    }
    applyRootState();
    setStatus(QCoreApplication::translate("VEdit", "Refreshed '%1'").arg(QFileInfo(path).fileName()));
}

void FileTreeWidget::onCollapseAll()
{
    view_->collapseAll();
}

void FileTreeWidget::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    if (model_->isDirectory(index)) {
        // QTreeView already expanded/collapsed the row for the mouse event.
        model_->ensureLoaded(index);
        return;
    }
    showKindForIndex(index);
    emit openFileRequested(model_->pathForIndex(index));
}

void FileTreeWidget::onCurrentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    showKindForIndex(current);
}

void FileTreeWidget::showKindForIndex(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    const QString path = model_->pathForIndex(index);
    const bool directory = model_->isDirectory(index);
    setStatus(QCoreApplication::translate("VEdit", "%1 - %2")
                  .arg(QFileInfo(path).fileName(), fileKindLabel(fileKindForPath(path, directory))));
}

void FileTreeWidget::onContextMenu(const QPoint &pos)
{
    const QModelIndex index = view_->indexAt(pos);
    if (index.isValid()) {
        view_->setCurrentIndex(index);
    }
    QMenu menu(this);
    const bool directory = index.isValid() && model_->isDirectory(index);
    if (directory) {
        menu.addAction(newFileAction_);
        menu.addAction(newFolderAction_);
        menu.addSeparator();
    }
    if (index.isValid()) {
        menu.addAction(renameAction_);
        menu.addAction(deleteAction_);
        menu.addSeparator();
    }
    menu.addAction(refreshAction_);
    menu.addAction(collapseAction_);
    menu.exec(view_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// empty states and status
// ---------------------------------------------------------------------------

void FileTreeWidget::applyRootState()
{
    QString text;
    bool treePage = true;
    bool errorState = false;

    switch (model_->rootState()) {
    case FileTreeModel::RootState::Ready:
        break;
    case FileTreeModel::RootState::EmptyFolder:
        text = tr("This folder is empty.");
        break;
    case FileTreeModel::RootState::NoRoot:
        treePage = false;
        text = tr("No folder open.\nUse \"Open...\" to browse a project.");
        break;
    case FileTreeModel::RootState::Missing:
        treePage = false;
        errorState = true;
        text = tr("The folder does not exist: '%1'")
                   .arg(QFileInfo(model_->rootPath()).fileName());
        break;
    case FileTreeModel::RootState::PermissionDenied:
        treePage = false;
        errorState = true;
        text = tr("Cannot read folder '%1': permission denied.")
                   .arg(QFileInfo(model_->rootPath()).fileName());
        break;
    case FileTreeModel::RootState::NotAFolder:
        treePage = false;
        errorState = true;
        text = tr("'%1' is not a folder.").arg(QFileInfo(model_->rootPath()).fileName());
        break;
    }

    emptyText_ = text;
    emptyVisible_ = !text.isEmpty();

    if (treePage) {
        hintLabel_->setText(text);
        hintLabel_->setVisible(!text.isEmpty());
        emptyPageLabel_->clear();
        stack_->setCurrentWidget(treePage_);
    } else {
        hintLabel_->hide();
        emptyPageLabel_->setText(text);
        emptyPageLabel_->setToolTip(model_->rootError());
        stack_->setCurrentWidget(emptyPage_);
        if (!text.isEmpty()) {
            // The centered page stays short (a dock is narrow); the selectable
            // status line carries the full path for copy/paste diagnosis.
            setStatus(model_->rootError().isEmpty() ? text : model_->rootError(), errorState);
        }
    }
}

void FileTreeWidget::setStatus(const QString &message, bool error)
{
    statusLabel_->setText(message);
    statusLabel_->setToolTip(message);
    statusLabel_->setStyleSheet(error ? QStringLiteral("color: #b3261e;") : QString());
}

QString FileTreeWidget::statusText() const
{
    return statusLabel_->text();
}

QString FileTreeWidget::emptyStateText() const
{
    return emptyVisible_ ? emptyText_ : QString();
}

bool FileTreeWidget::emptyStateVisible() const
{
    return emptyVisible_;
}

QStringList FileTreeWidget::diagnostics() const
{
    return diagnostics_;
}

void FileTreeWidget::setInteractiveDialogs(bool enabled)
{
    interactive_ = enabled;
}

bool FileTreeWidget::interactiveDialogs() const
{
    return interactive_;
}

} // namespace vedit::workspace
