#include "file_tree_model.h"

#include "file_kind.h"
#include "file_status.h" // src/common: the ONE status vocabulary (RM-9)
#include "path_display.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIODevice>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <utility>
#include <QCoreApplication>

namespace vedit::workspace {
namespace {

constexpr int kMaxDiagnostics = 200;

} // namespace

FileTreeModel::FileTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

FileTreeModel::~FileTreeModel() = default;

// ---------------------------------------------------------------------------
// root
// ---------------------------------------------------------------------------

void FileTreeModel::setRootPath(const QString &path)
{
    beginResetModel();
    resetRoot();
    // RM-9: the project set belongs to the root being left; the diagnostics
    // lane republishes the new root's set (with a new revision) right after.
    filelistFiles_.clear();
    if (!path.trimmed().isEmpty()) {
        buildRoot(path);
    } else {
        setRootState(RootState::NoRoot, QString());
    }
    endResetModel();
    emit rootStateChanged();
}

QString FileTreeModel::rootPath() const
{
    return rootPath_;
}

FileTreeModel::RootState FileTreeModel::rootState() const
{
    return rootState_;
}

QString FileTreeModel::rootError() const
{
    return rootError_;
}

void FileTreeModel::resetRoot()
{
    root_.reset();
    rootPath_.clear();
    // The group node died with the root's children; only the pointer is reset
    // here. `filelistFiles_` survives a refresh() of the same root (it is only
    // cleared on a root CHANGE, in setRootPath()).
    filelistGroup_ = nullptr;
    loadedRows_ = 0;
    scannedDirectories_ = 0;
    setRootState(RootState::NoRoot, QString());
}

void FileTreeModel::buildRoot(const QString &path)
{
    // FIX 1: the tree's root is the workspace root (the shell keeps them equal),
    // and it is stored in the ONE normalised spelling so a tooltip can decide
    // "inside the root" with the shared rule.
    const QString absolute = vedit::paths::normalizedRoot(path);
    const QFileInfo info(absolute);
    rootPath_ = absolute;

    if (!info.exists()) {
        setRootState(RootState::Missing,
                     QCoreApplication::translate("VEdit", "The folder '%1' does not exist.").arg(absolute));
        recordDiagnostic(rootError_);
        return;
    }
    if (!info.isDir()) {
        setRootState(RootState::NotAFolder,
                     QStringLiteral("'%1' is not a folder.").arg(absolute));
        recordDiagnostic(rootError_);
        return;
    }
    if (!info.isReadable()) {
        setRootState(RootState::PermissionDenied,
                     QCoreApplication::translate("VEdit", "The folder '%1' cannot be read (permission denied).")
                         .arg(absolute));
        recordDiagnostic(rootError_);
        return;
    }

    auto node = std::make_unique<Node>();
    node->name = info.fileName().isEmpty() ? absolute : info.fileName();
    node->path = absolute;
    node->isDir = true;
    node->isSymlink = info.isSymbolicLink();
    node->readable = true;
    root_ = std::move(node);
    setRootState(RootState::Ready, QString());
}

void FileTreeModel::setRootState(RootState state, const QString &error)
{
    rootState_ = state;
    rootError_ = error;
}

void FileTreeModel::recordDiagnostic(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }
    diagnostics_.append(message);
    while (diagnostics_.size() > kMaxDiagnostics) {
        diagnostics_.removeFirst();
    }
    qWarning("filetree: %s", qPrintable(message));
}

// ---------------------------------------------------------------------------
// QAbstractItemModel
// ---------------------------------------------------------------------------

int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0) {
        return 0;
    }
    if (!parent.isValid()) {
        return root_ != nullptr ? 1 : 0;
    }
    const Node *node = nodeForIndex(parent);
    if (node == nullptr || !node->isDir) {
        return 0;
    }
    return static_cast<int>(node->children.size());
}

int FileTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeForIndex(index);
    if (node == nullptr) {
        return QVariant();
    }

    const FileKind kind = fileKindForPath(node->path, node->isDir);
    switch (role) {
    case Qt::DisplayRole:
        return node->name;
    case Qt::DecorationRole: {
        static QFileIconProvider provider;
        return provider.icon(node->isDir ? QFileIconProvider::Folder : QFileIconProvider::File);
    }
    case Qt::ToolTipRole: {
        if (node->synthetic) {
            // RM-9: the group is not a directory on disk; say where its members
            // come from instead of inventing a path for it.
            return node->name + QLatin1String("\n")
                   + QCoreApplication::translate(
                       "VEdit",
                       "Files imported through a .f filelist; they live outside this folder tree");
        }
        // FIX 1: the tooltip renders the path with the ONE display rule
        // (relative to the tree/workspace root when inside it, absolute
        // otherwise); the PathRole below keeps the absolute path for opening.
        QString tip = vedit::paths::displayPath(rootPath_, node->path);
        tip += QLatin1String("\n") + fileKindLabel(kind);
        if (!node->error.isEmpty()) {
            tip += QLatin1String("\n") + node->error;
        }
        // RM-9: the tooltip carries the MEANING of the mark (not only in code):
        // a hollow ring is "not analysed yet / excluded", and the user can read
        // which one it is.
        if (!node->isDir && statusSource_ != nullptr) {
            const int status = statusSource_(node->path);
            if (status > 0) {
                tip += QLatin1String("\n") + vedit::common::fileStatusHint(status);
            }
        }
        return tip;
    }
    case PathRole:
        return node->path;
    case KindIdRole:
        return fileKindId(kind);
    case KindLabelRole:
        return fileKindLabel(kind);
    case IsDirRole:
        return node->isDir;
    case ReadableRole:
        return node->readable;
    case LoadedRole:
        return node->isDir ? node->complete : true;
    case FileStatusRole:
        // item 10: folders carry no status; a file asks the installed merged
        // view source (cheap: one hash lookup), never a project rescan.
        if (node->isDir) {
            return 0;
        }
        return statusSource_ != nullptr ? statusSource_(node->path) : 0;
    default:
        break;
    }
    return QVariant();
}

QVariant FileTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return QStringLiteral("Name");
    }
    return QAbstractItemModel::headerData(section, orientation, role);
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= 1) {
        return QModelIndex();
    }
    if (!parent.isValid()) {
        if (root_ == nullptr || row != 0) {
            return QModelIndex();
        }
        return createIndex(0, column, root_.get());
    }
    Node *node = nodeForIndex(parent);
    if (node == nullptr || !node->isDir || row >= static_cast<int>(node->children.size())) {
        return QModelIndex();
    }
    return createIndex(row, column, node->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    const Node *node = nodeForIndex(child);
    if (node == nullptr || node->parent == nullptr) {
        return QModelIndex();
    }
    return indexForNode(node->parent);
}

bool FileTreeModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return root_ != nullptr;
    }
    const Node *node = nodeForIndex(parent);
    if (node == nullptr || !node->isDir) {
        return false;
    }
    if (!node->populated) {
        return true;
    }
    if (!node->children.empty()) {
        return true;
    }
    return node->nextPending < node->pending.size();
}

bool FileTreeModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = nodeForIndex(parent);
    if (node == nullptr || !node->isDir) {
        return false;
    }
    return !node->populated || node->nextPending < node->pending.size();
}

void FileTreeModel::fetchMore(const QModelIndex &parent)
{
    Node *node = nodeForIndex(parent);
    if (node == nullptr || !node->isDir) {
        return;
    }
    loadOneBatch(node);
}

Qt::ItemFlags FileTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

FileTreeModel::Node *FileTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex FileTreeModel::indexForNode(const Node *node, int column) const
{
    if (node == nullptr) {
        return QModelIndex();
    }
    if (node->parent == nullptr) {
        return createIndex(0, column, const_cast<Node *>(node));
    }
    return createIndex(node->row, column, const_cast<Node *>(node));
}

QString FileTreeModel::pathForIndex(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    return node == nullptr ? QString() : node->path;
}

bool FileTreeModel::isDirectory(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    return node != nullptr && node->isDir;
}

bool FileTreeModel::isPopulated(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    return node != nullptr && node->isDir && node->populated;
}

bool FileTreeModel::isFullyLoaded(const QModelIndex &index) const
{
    const Node *node = nodeForIndex(index);
    return node != nullptr && node->isDir && node->populated && node->complete;
}

int FileTreeModel::loadedChildren(const QModelIndex &index) const
{
    return rowCount(index);
}

void FileTreeModel::ensureLoaded(const QModelIndex &index)
{
    Node *node = nodeForIndex(index);
    if (node == nullptr || !node->isDir) {
        return;
    }
    if (node->populated && node->nextPending >= node->pending.size()) {
        return;
    }
    loadOneBatch(node);
}

void FileTreeModel::ensureFullyLoaded(const QModelIndex &index)
{
    Node *node = nodeForIndex(index);
    if (node == nullptr || !node->isDir) {
        return;
    }
    int guard = 0;
    while (guard++ < 100000) {
        if (node->populated && node->nextPending >= node->pending.size()) {
            break;
        }
        loadOneBatch(node);
    }
}

QModelIndex FileTreeModel::indexForPath(const QString &path, bool loadMissing)
{
    Node *node = nodeForPath(path, loadMissing);
    if (node == nullptr) {
        return QModelIndex();
    }
    return indexForNode(node);
}

FileTreeModel::Node *FileTreeModel::findChild(Node *parent, const QString &name) const
{
    for (const auto &child : parent->children) {
        if (child->name == name) {
            return child.get();
        }
    }
    return nullptr;
}

FileTreeModel::Node *FileTreeModel::nodeForPath(const QString &path, bool loadMissing)
{
    if (root_ == nullptr) {
        return nullptr;
    }
    const QString absolute = QDir(path).absolutePath();
    if (absolute == root_->path) {
        return root_.get();
    }
    const QString relative = QDir(root_->path).relativeFilePath(absolute);
    if (relative.isEmpty() || relative.startsWith(QLatin1String(".."))) {
        return nullptr;
    }
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    Node *current = root_.get();
    for (const QString &part : parts) {
        if (loadMissing) {
            Node *found = nullptr;
            while (current->isDir) {
                found = findChild(current, part);
                if (found != nullptr) {
                    break;
                }
                if (current->populated && current->nextPending >= current->pending.size()) {
                    break;
                }
                loadOneBatch(current);
            }
            if (found == nullptr) {
                return nullptr;
            }
            current = found;
        } else {
            Node *found = findChild(current, part);
            if (found == nullptr) {
                return nullptr;
            }
            current = found;
        }
    }
    return current;
}

void FileTreeModel::readDirectory(Node *node)
{
    node->populated = true;
    node->complete = true;
    node->pending.clear();
    node->nextPending = 0;

    const QFileInfo info(node->path);
    if (!info.isReadable()) {
        node->readable = false;
        node->error = QCoreApplication::translate("VEdit", "permission denied: %1").arg(node->path);
        if (node == root_.get()) {
            setRootState(RootState::PermissionDenied,
                         QCoreApplication::translate("VEdit", "The folder '%1' cannot be read (permission denied).")
                             .arg(node->path));
            emit rootStateChanged();
        }
        recordDiagnostic(node->error);
        emit loadFailed(node->path, node->error);
        return;
    }

    QDir directory(node->path);
    const QStringList names =
        directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::NoSort);

    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(names.size()));
    for (const QString &name : names) {
        const QFileInfo entryInfo(directory.filePath(name));
        Entry entry;
        entry.name = name;
        entry.isDir = entryInfo.isDir();
        entry.isSymlink = entryInfo.isSymbolicLink();
        entry.readable = entryInfo.isReadable();
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        const int insensitive = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (insensitive != 0) {
            return insensitive < 0;
        }
        return QString::compare(a.name, b.name, Qt::CaseSensitive) < 0;
    });

    ++scannedDirectories_;
    node->pending = std::move(entries);
    node->complete = node->pending.empty();

    if (node == root_.get()) {
        const RootState next = node->pending.empty() ? RootState::EmptyFolder : RootState::Ready;
        if (next != rootState_) {
            setRootState(next, QString());
            emit rootStateChanged();
        }
    }
    if (node->pending.empty()) {
        emit directoryLoaded(node->path);
        if (node == root_.get()) {
            // An EMPTY root still shows the "From filelist" group: the members
            // live outside it, so the folder having no entries is not a reason
            // to hide them.
            syncFilelistGroup();
        }
    }
}

void FileTreeModel::appendBatch(Node *node)
{
    const int remaining = static_cast<int>(node->pending.size()) - static_cast<int>(node->nextPending);
    if (remaining <= 0) {
        node->complete = true;
        return;
    }
    const int count = std::min(std::max(batchSize_, 1), remaining);
    const int first = static_cast<int>(node->children.size());
    const QModelIndex parentIndex = indexForNode(node);

    beginInsertRows(parentIndex, first, first + count - 1);
    for (int i = 0; i < count; ++i) {
        const Entry &entry = node->pending[node->nextPending++];
        auto child = std::make_unique<Node>();
        child->name = entry.name;
        child->path = QDir(node->path).filePath(entry.name);
        child->isDir = entry.isDir;
        child->isSymlink = entry.isSymlink;
        child->readable = entry.readable;
        child->parent = node;
        child->row = first + i;
        node->children.push_back(std::move(child));
    }
    endInsertRows();
    loadedRows_ += count;

    if (node->nextPending >= node->pending.size()) {
        node->complete = true;
        node->pending.clear();
        node->pending.shrink_to_fit();
        node->nextPending = 0;
        emit directoryLoaded(node->path);
        if (node == root_.get()) {
            // The group is appended only once the root's own listing is
            // complete, so it is always the LAST row of the root folder.
            syncFilelistGroup();
        }
    }
}

void FileTreeModel::loadOneBatch(Node *node)
{
    if (!node->populated) {
        readDirectory(node);
        if (node->pending.empty()) {
            return;
        }
    }
    appendBatch(node);
}

void FileTreeModel::clearChildren(Node *node)
{
    if (node == root_.get()) {
        // The synthetic group is one of the root's children; the pointer must
        // never outlive the vector that owns it.
        filelistGroup_ = nullptr;
    }
    const int count = static_cast<int>(node->children.size());
    if (count == 0) {
        return;
    }
    const QModelIndex parentIndex = indexForNode(node);

    const auto subtreeRows = [](const Node *root) {
        int rows = 0;
        const auto visit = [&rows](const Node *current, auto &&self) -> void {
            ++rows;
            for (const auto &child : current->children) {
                self(child.get(), self);
            }
        };
        visit(root, visit);
        return rows;
    };

    beginRemoveRows(parentIndex, 0, count - 1);
    for (const auto &child : node->children) {
        loadedRows_ -= subtreeRows(child.get());
    }
    node->children.clear();
    endRemoveRows();
}

void FileTreeModel::refreshDirectory(const QString &path)
{
    if (root_ == nullptr) {
        return;
    }
    Node *node = nodeForPath(path, false);
    if (node == nullptr) {
        node = nodeForPath(QFileInfo(path).dir().path(), false);
    }
    if (node == nullptr || !node->isDir || !node->populated) {
        return;
    }

    clearChildren(node);
    node->pending.clear();
    node->nextPending = 0;
    node->complete = true;
    node->populated = false;
    node->error.clear();
    node->readable = true;

    readDirectory(node);
    if (!node->pending.empty()) {
        appendBatch(node);
    }
}

void FileTreeModel::refresh()
{
    const QString path = rootPath_;
    beginResetModel();
    resetRoot();
    rootPath_.clear();
    if (!path.isEmpty()) {
        buildRoot(path);
    }
    endResetModel();
    emit rootStateChanged();
}

void FileTreeModel::setBatchSize(int size)
{
    batchSize_ = std::max(size, 1);
}

void FileTreeModel::setFilelistFiles(const QStringList &files)
{
    if (files == filelistFiles_) {
        return;
    }
    filelistFiles_ = files;
    syncFilelistGroup();
}

int FileTreeModel::filelistRowCount() const
{
    if (filelistGroup_ == nullptr) {
        return 0;
    }
    return 1 + static_cast<int>(filelistGroup_->children.size());
}

void FileTreeModel::syncFilelistGroup()
{
    if (root_ == nullptr) {
        return;
    }
    // Drop the previous synthetic group first (it is always the last child).
    if (filelistGroup_ != nullptr) {
        int row = -1;
        for (std::size_t index = 0; index < root_->children.size(); ++index) {
            if (root_->children[index].get() == filelistGroup_) {
                row = static_cast<int>(index);
                break;
            }
        }
        if (row >= 0) {
            const int rows = 1 + static_cast<int>(filelistGroup_->children.size());
            beginRemoveRows(indexForNode(root_.get()), row, row);
            loadedRows_ -= rows;
            filelistGroup_ = nullptr;
            root_->children.erase(root_->children.begin() + row);
            endRemoveRows();
        } else {
            filelistGroup_ = nullptr;
        }
    }
    // Only when the root's OWN listing is complete: the group must be the last
    // row, so it may not jump in front of entries still being handed out.
    if (!root_->populated || !root_->complete || filelistFiles_.isEmpty()) {
        return;
    }
    // Keep only the members the directory scan cannot show. A path inside the
    // root is already a normal row and must not be duplicated in the group.
    const QString rootPath = QDir(root_->path).absolutePath();
    QStringList outside;
    for (const QString &file : filelistFiles_) {
        if (file.trimmed().isEmpty()) {
            continue;
        }
        const QString absolute = QDir(file).absolutePath();
        if (absolute == rootPath) {
            continue;
        }
        const QString relative = QDir(rootPath).relativeFilePath(absolute);
        if (relative.isEmpty() || relative.startsWith(QLatin1String(".."))) {
            outside.append(absolute);
        }
    }
    outside.removeDuplicates();
    if (outside.isEmpty()) {
        return;
    }

    const int first = static_cast<int>(root_->children.size());
    beginInsertRows(indexForNode(root_.get()), first, first);
    auto group = std::make_unique<Node>();
    group->name = QCoreApplication::translate("VEdit", "From filelist (%1)").arg(outside.size());
    group->isDir = true;
    group->readable = true;
    group->populated = true;
    group->complete = true;
    group->synthetic = true;
    group->parent = root_.get();
    group->row = first;
    for (int index = 0; index < outside.size(); ++index) {
        const QFileInfo info(outside.at(index));
        auto child = std::make_unique<Node>();
        child->name = info.fileName();
        child->path = outside.at(index);
        child->isDir = info.isDir();
        child->readable = true;
        child->parent = group.get();
        child->row = index;
        group->children.push_back(std::move(child));
    }
    loadedRows_ += 1 + static_cast<int>(group->children.size());
    filelistGroup_ = group.get();
    root_->children.push_back(std::move(group));
    endInsertRows();
}

int FileTreeModel::batchSize() const
{
    return batchSize_;
}

void FileTreeModel::setFileStatusSource(std::function<int(const QString &)> status,
                                        std::function<quint64()> revision)
{
    statusSource_ = std::move(status);
    statusRevisionSource_ = std::move(revision);
    statusRevision_ = 0;
    // A new source (or none) invalidates every dot already painted.
    if (root_ != nullptr) {
        emit dataChanged(index(0, 0), index(0, 0), {FileStatusRole});
    }
}

bool FileTreeModel::syncFileStatuses()
{
    const quint64 revision = statusRevisionSource_ != nullptr ? statusRevisionSource_() : 0;
    if (revision == statusRevision_) {
        return false;
    }
    statusRevision_ = revision;
    ++statusRepaints_;
    if (root_ == nullptr) {
        return true;
    }
    // RM-9 (T20 item 5): ONE dataChanged per LOADED DIRECTORY RANGE - never one
    // per file. A 1176-file tree must not emit (and thereby repaint) per file
    // per pass: the emission count is bounded by the directories the user
    // expanded, and Qt paints only the visible rows of each range anyway.
    const QModelIndex rootIndex = index(0, 0, QModelIndex());
    if (rootIndex.isValid()) {
        emit dataChanged(rootIndex, rootIndex, {FileStatusRole});
    }
    const std::function<void(Node *)> walk = [&](Node *node) {
        const int children = static_cast<int>(node->children.size());
        if (children > 0) {
            const QModelIndex parentIndex = indexForNode(node);
            const QModelIndex firstIndex = index(0, 0, parentIndex);
            const QModelIndex lastIndex = index(children - 1, 0, parentIndex);
            if (firstIndex.isValid() && lastIndex.isValid()) {
                emit dataChanged(firstIndex, lastIndex, {FileStatusRole});
            }
        }
        for (const auto &child : node->children) {
            if (child->isDir && child->populated) {
                walk(child.get());
            }
        }
    };
    walk(root_.get());
    return true;
}

int FileTreeModel::loadedRows() const
{
    return loadedRows_;
}

int FileTreeModel::scannedDirectories() const
{
    return scannedDirectories_;
}

QStringList FileTreeModel::diagnostics() const
{
    return diagnostics_;
}

void FileTreeModel::clearDiagnostics()
{
    diagnostics_.clear();
}

} // namespace vedit::workspace
