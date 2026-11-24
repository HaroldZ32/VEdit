#include "svn_workspaces.h"

#include "vcs_panel.h"

#include <QAction>
#include <QBoxLayout>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenu>
#include <QPromise>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace vedit::workspace {
namespace {

struct Discovery {
    QStringList roots;
    bool limited = false;
};

bool workingCopyMarker(const QString &root)
{
    const QFileInfo admin(QDir(root).filePath(QStringLiteral(".svn")));
    const QFileInfo database(QDir(admin.absoluteFilePath()).filePath(QStringLiteral("wc.db")));
    return !admin.isSymLink() && admin.isDir() && !database.isSymLink() && database.isFile();
}

Discovery findWorkingCopies(const QString &project, bool wider, const QPromise<Discovery> &promise)
{
    const int maxDepth = wider ? 12 : 3;
    const int maxEntries = wider ? 40000 : 4096;
    const int maxCopies = wider ? 24 : 8;
    QList<QPair<QString, int>> queue{{project, 0}};
    Discovery result;
    int inspected = 0;
    while (!queue.isEmpty() && !promise.isCanceled()) {
        const auto [path, depth] = queue.takeFirst();
        if (QFileInfo(path).canonicalFilePath() != path) {
            continue;
        }
        if (workingCopyMarker(path)) {
            result.roots.append(path);
            if (result.roots.size() == maxCopies) {
                result.limited = true;
                break;
            }
        }
        if (depth >= maxDepth || inspected >= maxEntries) {
            continue;
        }
        QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::System |
                                  QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (entries.hasNext() && !promise.isCanceled()) {
            const QFileInfo entry(entries.next());
            if (++inspected == maxEntries) {
                result.limited = true;
                break;
            }
            if (entry.isDir() && entry.fileName() != QStringLiteral(".svn") &&
                entry.fileName() != QStringLiteral(".git")) {
                queue.append({entry.absoluteFilePath(), depth + 1});
            }
        }
    }
    result.roots.sort();
    return result;
}

QString settingsKey(const QString &project)
{
    return QStringLiteral("closed/") + QString::fromLatin1(
        QCryptographicHash::hash(project.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace

SvnWorkspaces::SvnWorkspaces(vcs::VcsPanel &panel, QString settingsFile, RootSelected selectRoot,
                             RemoteRefresh refreshRemote, std::unique_ptr<vcs::VcsSource> monitor,
                             int monitorIntervalMs)
    : QObject(&panel), panel_(panel), settingsFile_(std::move(settingsFile)), selectRoot_(std::move(selectRoot)),
      refreshRemote_(std::move(refreshRemote)), monitor_(std::move(monitor))
{
    strip_ = new QWidget(&panel_);
    strip_->setObjectName(QStringLiteral("svn-workspaces"));
    auto *layout = new QVBoxLayout(strip_);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    auto *label = new QLabel(tr("Working copy:"), strip_);
    selector_ = new QComboBox(strip_);
    selector_->setObjectName(QStringLiteral("svn-working-copy-selector"));
    selector_->setAccessibleName(tr("SVN working copies"));
    selector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    selector_->setMinimumContentsLength(10);
    label->setBuddy(selector_);
    row->addWidget(label);
    row->addWidget(selector_, 1);
    close_ = new QPushButton(strip_);
    close_->setObjectName(QStringLiteral("svn-working-copy-close-reopen"));
    row->addWidget(close_);
    auto *options = new QToolButton(strip_);
    options->setObjectName(QStringLiteral("svn-working-copy-options"));
    options->setText(tr("Working copy options"));
    options->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(options);
    auto *rescan = menu->addAction(tr("Rescan for working copies"));
    rescan->setObjectName(QStringLiteral("svn-working-copy-rescan"));
    wider_ = menu->addAction(tr("Search more project folders"));
    wider_->setObjectName(QStringLiteral("svn-working-copy-search-wider"));
    wider_->setCheckable(true);
    remote_ = menu->addAction(tr("Check incoming changes every 5 minutes"));
    remote_->setObjectName(QStringLiteral("svn-working-copy-periodic-remote"));
    remote_->setCheckable(true);
    options->setMenu(menu);
    row->addWidget(options);
    message_ = new QLabel(strip_);
    message_->setObjectName(QStringLiteral("svn-working-copy-message"));
    message_->setWordWrap(true);
    layout->addWidget(message_);
    qobject_cast<QBoxLayout *>(panel_.layout())->insertWidget(0, strip_);
    strip_->hide();

    discoverTimer_ = new QTimer(this);
    discoverTimer_->setSingleShot(true);
    discoverTimer_->setInterval(350);
    connect(discoverTimer_, &QTimer::timeout, this, [this] { discover(); });
    rescanTimer_ = new QTimer(this);
    rescanTimer_->setInterval(45000);
    connect(rescanTimer_, &QTimer::timeout, this, [this] {
        if (strip_->isVisible() && !discovery_) { discover(); }
    });
    monitorTimer_ = new QTimer(this);
    monitorTimer_->setInterval(monitorIntervalMs);
    connect(monitorTimer_, &QTimer::timeout, this, [this] {
        cursor_ = 0;
        monitorNext();
    });
    remoteTimer_ = new QTimer(this);
    remoteTimer_->setObjectName(QStringLiteral("svn-working-copy-remote-timer"));
    remoteTimer_->setInterval(5 * 60 * 1000);
    connect(remoteTimer_, &QTimer::timeout, this, [this] {
        if (strip_->isVisible() && !panel_.writeInFlight() && current_ == monitoredRemotely_ &&
            !closed_.contains(current_) && validLocalRoot(current_)) {
            refreshRemote_();
        }
    });
    connect(selector_, &QComboBox::activated, this, [this](int index) {
        choose(selector_->itemData(index).toString());
    });
    connect(close_, &QPushButton::clicked, this, [this] { closeOrReopen(); });
    connect(rescan, &QAction::triggered, this, [this] { discover(); });
    connect(wider_, &QAction::toggled, this, [this] { discover(); });
    connect(remote_, &QAction::toggled, this, [this](bool enabled) {
        monitoredRemotely_ = enabled ? current_ : QString();
        setRemoteTimer();
    });
    connect(&panel_, &vcs::VcsPanel::svnStatusReady, this, [this] { recordActiveStatus(); });
}

SvnWorkspaces::~SvnWorkspaces()
{
    if (discovery_) {
        discovery_->cancel();
    }
}

void SvnWorkspaces::setProject(const QString &projectRoot, const QString &primaryWorkingCopy)
{
    ++generation_;
    if (discovery_) {
        discovery_->cancel();
    }
    discoverTimer_->stop();
    rescanTimer_->stop();
    monitorTimer_->stop();
    project_ = projectRoot.isEmpty() ? QString() : QFileInfo(projectRoot).canonicalFilePath();
    primary_ = primaryWorkingCopy.isEmpty() ? QString() : QFileInfo(primaryWorkingCopy).canonicalFilePath();
    current_ = primary_;
    projectRepositorySelected_ = false;
    monitoredRemotely_.clear();
    closed_.clear();
    if (!settingsFile_.isEmpty() && !project_.isEmpty() && QFileInfo::exists(settingsFile_)) {
        QSettings settings(settingsFile_, QSettings::IniFormat);
        const auto saved = settings.value(settingsKey(project_)).toStringList();
        closed_ = QSet<QString>(saved.cbegin(), saved.cend());
    }
    limited_ = false;
    order_.clear();
    copies_.clear();
    if (!primary_.isEmpty()) {
        order_.append(primary_);
        copies_.insert(primary_, Copy{.epoch = ++copyEpoch_});
    }
    {
        const QSignalBlocker blocker(wider_);
        wider_->setChecked(false);
    }
    pendingInitialDiscovery_ = !project_.isEmpty();
    strip_->setVisible(pendingInitialDiscovery_);
    rebuildSelector();
    selectRoot_(closed_.contains(primary_) ? QString() : primary_.isEmpty() ? projectRoot : primary_);
    if (primary_.isEmpty() || closed_.contains(primary_)) {
        scheduleDiscovery();
    } else if (pendingInitialDiscovery_) {
        discoverTimer_->start(2500);
    }
}

void SvnWorkspaces::scheduleDiscovery()
{
    if (pendingInitialDiscovery_) {
        pendingInitialDiscovery_ = false;
        discoverTimer_->start(350);
    }
}

void SvnWorkspaces::discover()
{
    if (project_.isEmpty()) {
        return;
    }
    pendingInitialDiscovery_ = false;
    discoverTimer_->stop();
    if (discovery_) {
        discovery_->cancel();
    }
    auto *watcher = new QFutureWatcher<Discovery>(this);
    discovery_ = watcher;
    const quint64 generation = generation_;
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != generation_ || discovery_ != watcher || watcher->isCanceled()) {
            return;
        }
        discovery_ = nullptr;
        if (!rescanTimer_->isActive()) { rescanTimer_->start(); }
        const auto result = watcher->result();
        QStringList roots = result.roots;
        if (validLocalRoot(primary_)) {
            roots.removeAll(primary_);
            roots.prepend(primary_);
        }
        if (roots.size() > (wider_->isChecked() ? 24 : 8)) {
            roots.removeLast();
        }
        for (const auto &known : order_) {
            if (!roots.contains(known)) {
                copies_.remove(known);
            }
        }
        order_ = roots;
        limited_ = result.limited;
        for (const auto &root : order_) {
            if (!copies_.contains(root)) {
                copies_.insert(root, Copy{.epoch = ++copyEpoch_});
            }
        }
        if ((current_.isEmpty() && !projectRepositorySelected_ && panel_.backend() != vcs::Backend::Git) ||
            (!current_.isEmpty() && !copies_.contains(current_))) {
            const auto available = std::find_if(order_.cbegin(), order_.cend(), [this](const QString &root) {
                return !closed_.contains(root);
            });
            if (available != order_.cend()) {
                choose(*available);
            } else if (!order_.isEmpty()) {
                choose(order_.front());
            } else if (!current_.isEmpty()) {
                current_.clear();
                selectRoot_(project_);
            }
        }
        rebuildSelector();
        if (order_.size() > 1 && !monitorTimer_->isActive()) {
            monitorTimer_->start();
            QTimer::singleShot(350, this, [this, generation] {
                if (generation == generation_) {
                    cursor_ = 0;
                    monitorNext();
                }
            });
        }
    });
    QPromise<Discovery> promise;
    promise.start();
    watcher->setFuture(promise.future());
    QThreadPool::globalInstance()->start([project = project_, wider = wider_->isChecked(), promise = std::move(promise)]() mutable {
        promise.addResult(findWorkingCopies(project, wider, promise));
        promise.finish();
    });
}

bool SvnWorkspaces::validLocalRoot(const QString &root) const
{
    const QFileInfo info(root);
    const QString relative = QDir(project_).relativeFilePath(root);
    const bool inside = QDir::isRelativePath(relative) && relative != QStringLiteral("..") &&
                        !relative.startsWith(QStringLiteral("../"));
    return !root.isEmpty() && (root == primary_ || inside) && !info.isSymLink() &&
           info.canonicalFilePath() == root && workingCopyMarker(root);
}

QString SvnWorkspaces::labelFor(const QString &root) const
{
    const QString relative = QDir(project_).relativeFilePath(root);
    const QString name = relative == QStringLiteral(".") || relative.startsWith(QStringLiteral(".."))
        ? QDir(root).dirName() : relative;
    if (closed_.contains(root)) {
        return tr("%1 (closed)").arg(name);
    }
    const Copy copy = copies_.value(root);
    if (copy.unavailable) {
        return tr("%1 (status unavailable)").arg(name);
    }
    return copy.changed < 0 ? name : tr("%1 (%2 changed)").arg(name).arg(copy.changed);
}

void SvnWorkspaces::rebuildSelector()
{
    const QSignalBlocker blocker(selector_);
    selector_->clear();
    if (primary_.isEmpty() && !order_.isEmpty()) {
        selector_->addItem(tr("Project repository"), QString());
    }
    for (const auto &root : order_) {
        selector_->addItem(labelFor(root), root);
        selector_->setItemData(selector_->count() - 1, root, Qt::ToolTipRole);
    }
    selector_->setCurrentIndex(selector_->findData(current_));
    selector_->setEnabled(!order_.isEmpty());
    close_->setEnabled(!current_.isEmpty());
    const bool closed = closed_.contains(current_);
    close_->setText(closed ? tr("Reopen working copy") : tr("Close working copy"));
    message_->setText(closed ? tr("This working copy is closed.") : limited_ ? tr("Working-copy search reached its limit.") :
                      !pendingInitialDiscovery_ && !discoverTimer_->isActive() && order_.isEmpty() ? tr("No SVN working copies found.") : QString());
    message_->setVisible(!message_->text().isEmpty());
    setRemoteTimer();
}

void SvnWorkspaces::choose(const QString &root)
{
    if (panel_.writeInFlight()) {
        rebuildSelector();
        message_->setText(tr("An SVN write is still running."));
        message_->show();
        return;
    }
    if (root.isEmpty() && primary_.isEmpty()) {
        current_.clear();
        projectRepositorySelected_ = true;
        rebuildSelector();
        selectRoot_(project_);
    } else if (copies_.contains(root) && validLocalRoot(root)) {
        current_ = root;
        copies_[root].epoch = ++copyEpoch_;
        rebuildSelector();
        selectRoot_(closed_.contains(root) ? QString() : root);
    } else {
        rebuildSelector();
    }
}

void SvnWorkspaces::closeOrReopen()
{
    if (current_.isEmpty() || panel_.writeInFlight()) {
        return;
    }
    if (closed_.contains(current_)) {
        if (!validLocalRoot(current_)) {
            discover();
            return;
        }
        closed_.remove(current_);
    } else {
        closed_.insert(current_);
        if (monitoredRemotely_ == current_) {
            monitoredRemotely_.clear();
        }
    }
    storeClosed();
    choose(current_);
}

void SvnWorkspaces::storeClosed() const
{
    if (!settingsFile_.isEmpty()) {
        QSettings settings(settingsFile_, QSettings::IniFormat);
        settings.setValue(settingsKey(project_), closed_.values());
    }
}

void SvnWorkspaces::recordActiveStatus()
{
    const QString root = QString::fromStdString(panel_.root());
    auto copy = copies_.find(root);
    if (root != current_ || copy == copies_.end() || closed_.contains(root)) {
        return;
    }
    copy->changed = panel_.fileCount();
    copy->unavailable = false;
    scheduleDiscovery();
    const int row = selector_->findData(root);
    if (row >= 0) {
        selector_->setItemText(row, labelFor(root));
    }
}

void SvnWorkspaces::setRemoteTimer()
{
    const bool selected = copies_.contains(current_) && !closed_.contains(current_);
    const bool enabled = selected && !monitoredRemotely_.isEmpty() && monitoredRemotely_ == current_;
    const QSignalBlocker blocker(remote_);
    remote_->setEnabled(selected);
    remote_->setChecked(enabled);
    if (enabled && !remoteTimer_->isActive()) {
        remoteTimer_->start();
    } else if (!enabled) {
        remoteTimer_->stop();
    }
}

void SvnWorkspaces::monitorNext()
{
    if (!monitor_ || monitorBusy_ || !strip_->isVisible()) {
        return;
    }
    while (cursor_ < order_.size()) {
        const QString root = order_.at(cursor_++);
        if (root == current_ || closed_.contains(root) || !validLocalRoot(root)) {
            continue;
        }
        const quint64 generation = generation_;
        const quint64 epoch = copies_.value(root).epoch;
        monitorBusy_ = true;
        monitor_->requestStatus(root.toStdString(), [guard = QPointer(this), root, generation, epoch](vcs::Status status) {
            if (!guard) {
                return;
            }
            guard->monitorBusy_ = false;
            auto copy = guard->copies_.find(root);
            if (generation == guard->generation_ && copy != guard->copies_.end() && epoch == copy->epoch &&
                root != guard->current_ && !guard->closed_.contains(root)) {
                copy->unavailable = status.error || status.backend != vcs::Backend::Subversion;
                copy->changed = static_cast<qsizetype>(status.changes.size());
                const int row = guard->selector_->findData(root);
                if (row >= 0) {
                    guard->selector_->setItemText(row, guard->labelFor(root));
                }
            }
            QTimer::singleShot(0, guard, [guard] { guard->monitorNext(); });
        });
        return;
    }
}

} // namespace vedit::workspace
