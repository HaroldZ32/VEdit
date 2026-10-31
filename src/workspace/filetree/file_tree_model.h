#pragma once

// Lazy file-system model behind the workspace file tree (lane: workspace, T21).
//
// Design points that the acceptance contract (A2a) and evidence depend on:
//   * Lazy: a directory is read only when it is expanded. A directory scan is
//     one readdir + one stat per entry, then the rows are handed to the view in
//     batches of `batchSize()` through canFetchMore()/fetchMore(); nothing is
//     recursed into and a 10k-entry folder never materialises 10k rows at open.
//   * Disk is the source of truth: refreshDirectory() re-reads the directory,
//     so after a create/rename/delete the tree cannot disagree with the disk.
//   * Explicit root states: no root, empty folder, permission denied, missing,
//     not a folder -- the panel turns these into an empty state instead of an
//     unexplained blank view.
//   * Honest diagnostics: a failed read is recorded on the node and emitted as
//     loadFailed(); it is never turned into a silent empty folder.

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace vedit::workspace {

class FileTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum class RootState {
        NoRoot,
        Ready,
        EmptyFolder,
        PermissionDenied,
        Missing,
        NotAFolder,
    };

    enum Role {
        PathRole = Qt::UserRole + 1,
        KindIdRole,
        KindLabelRole,
        IsDirRole,
        ReadableRole,
        LoadedRole,
    };

    explicit FileTreeModel(QObject *parent = nullptr);
    ~FileTreeModel() override;

    // ---- root ------------------------------------------------------------
    void setRootPath(const QString &path);
    QString rootPath() const;
    RootState rootState() const;
    QString rootError() const; // diagnostic for the empty state ("" when fine)

    // ---- QAbstractItemModel ---------------------------------------------
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // ---- helpers used by the panel and the tests -------------------------
    QString pathForIndex(const QModelIndex &index) const;
    // Resolve a path to a row. `loadMissing` walks/loads the levels on the way,
    // which is what "reveal the file I just created" needs; without it the
    // lookup only sees already materialised rows and never touches the disk.
    QModelIndex indexForPath(const QString &path, bool loadMissing = false);
    bool isDirectory(const QModelIndex &index) const;
    bool isPopulated(const QModelIndex &index) const;  // readdir done for this folder
    bool isFullyLoaded(const QModelIndex &index) const; // every batch handed out
    void ensureLoaded(const QModelIndex &index);       // hand out one batch
    void ensureFullyLoaded(const QModelIndex &index);  // drain the batches
    int loadedChildren(const QModelIndex &index) const;

    void refresh();                             // re-read from the root
    void refreshDirectory(const QString &path);  // re-read one folder from disk

    void setBatchSize(int size);
    int batchSize() const;

    // Evidence counters. `loadedRows()` is the number of rows materialised right
    // now (the laziness proof); `scannedDirectories()` counts readdir calls that
    // succeeded since the model was created.
    int loadedRows() const;
    int scannedDirectories() const;
    QStringList diagnostics() const;
    void clearDiagnostics();

signals:
    void rootStateChanged();
    void directoryLoaded(const QString &path);
    void loadFailed(const QString &path, const QString &error);

private:
    struct Entry {
        QString name;
        bool isDir = false;
        bool isSymlink = false;
        bool readable = true;
    };

    struct Node {
        QString name;
        QString path;
        bool isDir = false;
        bool isSymlink = false;
        bool readable = true;
        bool populated = false; // readdir finished
        bool complete = true;   // no pending entries left
        int row = 0;
        QString error;
        std::vector<Entry> pending;
        std::size_t nextPending = 0;
        std::vector<std::unique_ptr<Node>> children;
        Node *parent = nullptr;
    };

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(const Node *node, int column = 0) const;
    QString pathForNode(const Node *node) const;
    Node *findChild(Node *parent, const QString &name) const;
    Node *nodeForPath(const QString &path, bool loadMissing);
    void readDirectory(Node *node);
    void appendBatch(Node *node);
    void clearChildren(Node *node);
    void loadOneBatch(Node *node);
    void buildRoot(const QString &path);
    void resetRoot();
    void setRootState(RootState state, const QString &error);
    void recordDiagnostic(const QString &message);

    std::unique_ptr<Node> root_;
    QString rootPath_;
    RootState rootState_ = RootState::NoRoot;
    QString rootError_;
    int batchSize_ = 250;
    int loadedRows_ = 0;
    int scannedDirectories_ = 0;
    QStringList diagnostics_;
};

} // namespace vedit::workspace
