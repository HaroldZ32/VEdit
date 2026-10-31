#include "file_index.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

namespace vedit::workspace {
namespace {

struct Entry {
    QString absolute;
    QString display;
};

constexpr int kDefaultMaxFiles = 200000;

} // namespace

WorkspaceFileIndex::WorkspaceFileIndex(QString root)
    : ignored_{QStringLiteral(".git"),  QStringLiteral(".svn"),       QStringLiteral(".hg"),
               QStringLiteral(".bzr"), QStringLiteral("node_modules"), QStringLiteral("__pycache__"),
               QStringLiteral(".cache")}
{
    setRoot(std::move(root));
}

void WorkspaceFileIndex::setRoot(const QString &root)
{
    QString cleaned;
    if (!root.trimmed().isEmpty()) {
        cleaned = QDir::cleanPath(QDir(root).absolutePath());
    }
    if (cleaned == root_) {
        return;
    }
    root_ = cleaned;
    absolute_.clear();
    display_.clear();
    basename_.clear();
    truncated_ = false;
    scanCount_ = 0;
    ++revision_;
    notifyChanged();
}

QString WorkspaceFileIndex::root() const
{
    return root_;
}

QString WorkspaceFileIndex::absoluteOf(const QString &path) const
{
    const QString cleaned = QDir::cleanPath(path);
    if (QDir::isAbsolutePath(cleaned)) {
        return cleaned;
    }
    if (root_.isEmpty()) {
        return cleaned;
    }
    return QDir::cleanPath(root_ + u'/' + cleaned);
}

QString WorkspaceFileIndex::relativeOf(const QString &absolute) const
{
    if (root_.isEmpty()) {
        return absolute;
    }
    const QString prefix = root_.endsWith(u'/') ? root_ : root_ + u'/';
    if (absolute.startsWith(prefix)) {
        return absolute.mid(prefix.size());
    }
    return absolute;
}

bool WorkspaceFileIndex::scan(QString *error, bool forceRefresh)
{
    if (root_.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no workspace root configured");
        }
        return false;
    }
    const QFileInfo rootInfo(root_);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (error != nullptr) {
            *error = QStringLiteral("workspace root is not a directory: %1").arg(root_);
        }
        return false;
    }
    if (!forceRefresh && scanCount_ > 0) {
        return true; // cached: no disk walk, no rescan
    }

    const QSet<QString> ignored(ignored_.begin(), ignored_.end());
    QStringList pending;
    pending.append(root_);
    QStringList found;
    truncated_ = false;
    while (!pending.isEmpty() && found.size() < maxFiles_) {
        const QString directory = pending.takeLast();
        const QFileInfoList children = QDir(directory).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
        for (const QFileInfo &child : children) {
            const QString name = child.fileName();
            if (name.startsWith(u'.')) {
                continue; // hidden files and hidden directories
            }
            if (child.isDir()) {
                if (child.isSymLink() || ignored.contains(name)) {
                    continue; // never follow directory symlinks (loops)
                }
                pending.append(child.absoluteFilePath());
                continue;
            }
            if (!child.isFile()) {
                continue;
            }
            found.append(child.absoluteFilePath());
            if (found.size() >= maxFiles_) {
                truncated_ = true;
                break;
            }
        }
    }

    std::sort(found.begin(), found.end());
    absolute_ = found;
    rebuildDerived();
    ++scanCount_;
    ++revision_;
    notifyChanged();
    return true;
}

void WorkspaceFileIndex::setFiles(const QStringList &paths)
{
    QVector<Entry> entries;
    entries.reserve(paths.size());
    QSet<QString> seen;
    seen.reserve(paths.size());
    for (const QString &path : paths) {
        if (path.trimmed().isEmpty()) {
            continue;
        }
        const QString absolute = absoluteOf(path);
        if (absolute.isEmpty() || seen.contains(absolute)) {
            continue;
        }
        seen.insert(absolute);
        entries.append(Entry{absolute, relativeOf(absolute)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        return left.display < right.display;
    });

    absolute_.resize(entries.size());
    display_.resize(entries.size());
    basename_.resize(entries.size());
    for (qsizetype index = 0; index < entries.size(); ++index) {
        absolute_[index] = entries.at(index).absolute;
        display_[index] = entries.at(index).display;
        basename_[index] = entries.at(index).display.section(u'/', -1);
    }
    truncated_ = false;
    ++revision_;
    notifyChanged();
}

void WorkspaceFileIndex::addFile(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    const QString absolute = absoluteOf(path);
    const QString display = relativeOf(absolute);
    const auto position = std::lower_bound(display_.begin(), display_.end(), display);
    if (position != display_.end() && *position == display) {
        return;
    }
    const qsizetype index = position - display_.begin();
    absolute_.insert(index, absolute);
    display_.insert(index, display);
    basename_.insert(index, display.section(u'/', -1));
    ++revision_;
    notifyChanged();
}

void WorkspaceFileIndex::removeFile(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    const QString display = relativeOf(absoluteOf(path));
    const auto position = std::lower_bound(display_.begin(), display_.end(), display);
    if (position == display_.end() || *position != display) {
        return;
    }
    const qsizetype index = position - display_.begin();
    absolute_.removeAt(index);
    display_.removeAt(index);
    basename_.removeAt(index);
    ++revision_;
    notifyChanged();
}

bool WorkspaceFileIndex::isEmpty() const
{
    return absolute_.isEmpty();
}

int WorkspaceFileIndex::size() const
{
    return static_cast<int>(absolute_.size());
}

const QStringList &WorkspaceFileIndex::displayPaths() const
{
    return display_;
}

const QStringList &WorkspaceFileIndex::basenames() const
{
    return basename_;
}

QString WorkspaceFileIndex::absolutePath(int index) const
{
    if (index < 0 || index >= absolute_.size()) {
        return QString();
    }
    return absolute_.at(index);
}

QString WorkspaceFileIndex::displayPath(int index) const
{
    if (index < 0 || index >= display_.size()) {
        return QString();
    }
    return display_.at(index);
}

int WorkspaceFileIndex::scanCount() const
{
    return scanCount_;
}

int WorkspaceFileIndex::revision() const
{
    return revision_;
}

bool WorkspaceFileIndex::truncated() const
{
    return truncated_;
}

void WorkspaceFileIndex::setMaxFiles(int maxFiles)
{
    maxFiles_ = std::max(1, maxFiles);
}

int WorkspaceFileIndex::maxFiles() const
{
    return maxFiles_;
}

void WorkspaceFileIndex::setIgnoredDirectories(const QStringList &names)
{
    ignored_ = names;
}

QStringList WorkspaceFileIndex::ignoredDirectories() const
{
    return ignored_;
}

void WorkspaceFileIndex::setOnChanged(std::function<void()> callback)
{
    onChanged_ = std::move(callback);
}

void WorkspaceFileIndex::rebuildDerived()
{
    display_.clear();
    basename_.clear();
    display_.reserve(absolute_.size());
    basename_.reserve(absolute_.size());
    for (const QString &absolute : absolute_) {
        const QString display = relativeOf(absolute);
        display_.append(display);
        basename_.append(display.section(u'/', -1));
    }
}

void WorkspaceFileIndex::notifyChanged()
{
    if (onChanged_) {
        onChanged_();
    }
}

} // namespace vedit::workspace
