#include "file_tree_model.h"

#include "file_kind.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIODevice>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <utility>

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
    loadedRows_ = 0;
    scannedDirectories_ = 0;
    setRootState(RootState::NoRoot, QString());
}

void FileTreeModel::buildRoot(const QString &path)
{
    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();
    rootPath_ = absolute;

    if (!info.exists()) {
        setRootState(RootState::Missing,
                     QStringLiteral("The folder '%1' does not exist.").arg(absolute));
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
                     QStringLiteral("The folder '%1' cannot be read (permission denied).")
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
        QString tip = node->path;
        tip += QLatin1String("\n") + fileKindLabel(kind);
        if (!node->error.isEmpty()) {
            tip += QLatin1String("\n") + node->error;
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
        node->error = QStringLiteral("permission denied: %1").arg(node->path);
        if (node == root_.get()) {
            setRootState(RootState::PermissionDenied,
                         QStringLiteral("The folder '%1' cannot be read (permission denied).")
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

int FileTreeModel::batchSize() const
{
    return batchSize_;
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
