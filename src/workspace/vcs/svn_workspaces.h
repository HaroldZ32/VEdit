#pragma once

#include "vcs_source.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;
class QFutureWatcherBase;

namespace vedit::workspace {

namespace vcs { class VcsPanel; }

class SvnWorkspaces final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(SvnWorkspaces)

public:
    using RootSelected = std::function<void(const QString &)>;
    using RemoteRefresh = std::function<void()>;

    SvnWorkspaces(vcs::VcsPanel &panel, QString settingsFile, RootSelected selectRoot,
                  RemoteRefresh refreshRemote, std::unique_ptr<vcs::VcsSource> monitor,
                  int monitorIntervalMs = 12000);
    ~SvnWorkspaces() override;
    void setProject(const QString &projectRoot, const QString &primaryWorkingCopy);
    bool isClosed(const QString &root) const { return closed_.contains(root); }

private:
    struct Copy {
        qsizetype changed = -1;
        quint64 epoch = 0;
        bool unavailable = false;
    };

    void scheduleDiscovery();
    void discover();
    void rebuildSelector();
    void choose(const QString &root);
    void closeOrReopen();
    void storeClosed() const;
    void recordActiveStatus();
    void setRemoteTimer();
    void monitorNext();
    QString labelFor(const QString &root) const;
    bool validLocalRoot(const QString &root) const;

    vcs::VcsPanel &panel_;
    const QString settingsFile_;
    const RootSelected selectRoot_;
    const RemoteRefresh refreshRemote_;
    std::unique_ptr<vcs::VcsSource> monitor_;
    QWidget *strip_ = nullptr;
    QComboBox *selector_ = nullptr;
    QPushButton *close_ = nullptr;
    QLabel *message_ = nullptr;
    QAction *wider_ = nullptr;
    QAction *remote_ = nullptr;
    QTimer *discoverTimer_ = nullptr;
    QTimer *rescanTimer_ = nullptr;
    QTimer *monitorTimer_ = nullptr;
    QTimer *remoteTimer_ = nullptr;
    QPointer<QFutureWatcherBase> discovery_;
    QString project_;
    QString primary_;
    QString current_;
    QString monitoredRemotely_;
    QStringList order_;
    QHash<QString, Copy> copies_;
    QSet<QString> closed_;
    qsizetype cursor_ = 0;
    quint64 generation_ = 0;
    quint64 copyEpoch_ = 0;
    bool pendingInitialDiscovery_ = false;
    bool projectRepositorySelected_ = false;
    bool monitorBusy_ = false;
    bool limited_ = false;
};

} // namespace vedit::workspace
