#pragma once

// Shared, cached snapshot of the files of a workspace root.
//
// This is the single file index the Ctrl+P palette consumes. Disk discovery is
// owned by the file-tree lane (ws-filetree); the palette must never walk the
// disk itself. Two sharing modes are supported:
//
//   * publish: the file tree hands its snapshot in through setFiles() after its
//     own scan (the palette then filters a copy it does not own the scan of);
//   * own: scan() performs exactly one disk walk, and every later palette open
//     reuses the cache (scanCount() stays at 1 - this is asserted by the
//     selftest and by the 10k benchmark).
//
// addFile()/removeFile() are the incremental update path for file-tree events
// (create / delete / rename), so neither lane rescans a 10k tree on a rename.
//
// The index is a plain value object (no QObject), and is not thread-safe: it is
// populated and read on the GUI thread.
#include <QString>
#include <QStringList>

#include <functional>

namespace vedit::workspace {

class WorkspaceFileIndex {
public:
    explicit WorkspaceFileIndex(QString root = QString());

    // Changing the root clears the snapshot (the old files belong to the old
    // root) and resets the scan counter; the next scan() walks the new root.
    void setRoot(const QString &root);
    QString root() const;

    // One cached walk of `root`. A second call without forceRefresh reuses the
    // snapshot and does NOT touch the disk. Returns false (with *error) only
    // when the root is empty or not a readable directory.
    bool scan(QString *error = nullptr, bool forceRefresh = false);

    // External snapshot (file-tree publish path). Accepts absolute or
    // root-relative paths; duplicates are dropped and the result is sorted by
    // display path. Does not touch the disk and does not bump scanCount().
    void setFiles(const QStringList &paths);

    // Incremental updates (file-tree create / delete / rename).
    void addFile(const QString &path);
    void removeFile(const QString &path);

    bool isEmpty() const;
    int size() const;

    const QStringList &displayPaths() const; // relative, '/' separated, sorted
    const QStringList &basenames() const;
    QString absolutePath(int index) const;
    QString displayPath(int index) const;

    int scanCount() const; // number of real disk walks performed by this index
    int revision() const;  // bumped on every snapshot change

    bool truncated() const; // true when a scan hit the maxFiles cap
    void setMaxFiles(int maxFiles);
    int maxFiles() const;

    // Directory names the scan does not descend into (defaults: VCS metadata,
    // caches and dependency trees, plus every hidden directory).
    void setIgnoredDirectories(const QStringList &names);
    QStringList ignoredDirectories() const;

    void setOnChanged(std::function<void()> callback);

private:
    QString absoluteOf(const QString &path) const;
    QString relativeOf(const QString &absolute) const;
    void rebuildDerived();
    void notifyChanged();

    QString root_;
    QStringList absolute_;
    QStringList display_;
    QStringList basename_;
    QStringList ignored_;
    int maxFiles_ = 200000;
    int scanCount_ = 0;
    int revision_ = 0;
    bool truncated_ = false;
    std::function<void()> onChanged_;
};

} // namespace vedit::workspace
