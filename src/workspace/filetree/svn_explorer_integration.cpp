#if defined(VEDIT_WORKSPACE_HAVE_FILETREE) && defined(VEDIT_WORKSPACE_HAVE_VCS)

#include "svn_explorer_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "vcs/vcs_integration.h"
#include "vcs/vcs_panel.h"
#include "vcs/vcs_source.h"

#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QStatusBar>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace vedit::workspace {
namespace {

QString text(const char *source)
{
    return QCoreApplication::translate("VEdit", source);
}

enum class Operation { IgnoreFile, IgnorePattern, Rename, Revert, History, Base, Head };

struct MenuAction {
    Operation operation;
    const char *name;
    const char *label;
};

constexpr std::array kActions{
    MenuAction{Operation::IgnoreFile, "svn-explorer-ignore-file", QT_TRANSLATE_NOOP("VEdit", "Ignore File…")},
    MenuAction{Operation::IgnorePattern, "svn-explorer-ignore-pattern", QT_TRANSLATE_NOOP("VEdit", "Ignore Pattern…")},
    MenuAction{Operation::Rename, "svn-explorer-rename", QT_TRANSLATE_NOOP("VEdit", "Rename With SVN…")},
    MenuAction{Operation::Revert, "svn-explorer-revert", QT_TRANSLATE_NOOP("VEdit", "Revert SVN Changes…")},
    MenuAction{Operation::History, "svn-explorer-history", QT_TRANSLATE_NOOP("VEdit", "File History")},
    MenuAction{Operation::Base, "svn-explorer-base", QT_TRANSLATE_NOOP("VEdit", "Compare With BASE")},
    MenuAction{Operation::Head, "svn-explorer-head", QT_TRANSLATE_NOOP("VEdit", "Compare With HEAD")},
};

struct FileStatus {
    bool known = false;
    bool tracked = false;
    bool changed = false;
    bool unversioned = false;
    bool hasBase = false;
};

bool enabled(Operation operation, const FileStatus &status, bool writable)
{
    switch (operation) {
    case Operation::IgnoreFile:
    case Operation::IgnorePattern: return writable && status.unversioned;
    case Operation::Rename: return writable && status.tracked;
    case Operation::Revert: return writable && status.tracked && status.changed;
    case Operation::History:
    case Operation::Base:
    case Operation::Head: return status.hasBase;
    }
    return false;
}

class ExplorerMenu final : public QObject {
public:
    explicit ExplorerMenu(vedit::app::ApplicationShell &shell) : QObject(&shell), shell_(shell)
    {
        connect(&shell_, &vedit::app::ApplicationShell::projectRootChanged, this,
                [this] { ++rootEpoch_; });
    }

    void add(QMenu &menu, const QString &path, std::function<bool()> sameRow)
    {
        const QFileInfo file(path);
        if (!file.isFile() || file.isSymLink()) {
            return;
        }
        const QString root = svnWorkingCopyRoot(shell_.projectRoot());
        if (root.isEmpty() || svnWorkingCopyRoot(file.absolutePath()) != root) {
            return;
        }
        const QString relative = QDir(root).relativeFilePath(path);
        if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) {
            return;
        }
        auto *svn = menu.addMenu(QCoreApplication::translate("VEdit", "Subversion"));
        svn->setObjectName(QStringLiteral("filetree-svn-menu"));
        auto *hint = svn->addAction(QCoreApplication::translate("VEdit", "Checking SVN status…"));
        hint->setEnabled(false);
        const auto current = [self = QPointer(this), epoch = rootEpoch_, sameRow = std::move(sameRow)] {
            return self && self->rootEpoch_ == epoch && sameRow();
        };
        for (const auto &spec : kActions) {
            auto *action = svn->addAction(text(spec.label), this, [this, path, current, spec] {
                if (!current()) {
                    return;
                }
                const auto id = QStringLiteral("vcs");
                shell_.panels()->showPanel(id, &shell_);
                if (auto *dock = shell_.panels()->dock(id)) {
                    if (auto *panel = dock->findChild<vcs::VcsPanel *>()) {
                        connect(panel, &vcs::VcsPanel::svnFileRenamed, this,
                                &ExplorerMenu::refreshRename, Qt::UniqueConnection);
                        dispatch(*panel, path, spec.operation, current);
                    }
                }
            });
            action->setObjectName(QString::fromLatin1(spec.name));
            action->setEnabled(false);
        }
        auto finish = [this, svn = QPointer(svn), hint = QPointer(hint), current](FileStatus status) {
            if (!svn || !hint) {
                return;
            }
            if (!current()) {
                hint->setText(QCoreApplication::translate("VEdit", "SVN status changed; reopen the menu to retry."));
                return;
            }
            if (!status.known) {
                hint->setText(QCoreApplication::translate("VEdit", "SVN status is unavailable for this item."));
                return;
            }
            hint->setVisible(false);
            for (const auto &spec : kActions) {
                if (auto *action = svn->findChild<QAction *>(QString::fromLatin1(spec.name))) {
                    action->setEnabled(enabled(spec.operation, status, source_.writeCapable()));
                }
            }
        };
        inspect(root, relative, [menu = QPointer(svn), current] { return menu && current(); },
                std::move(finish));
    }

private:
    void refreshRename(const QString &oldPath, const QString &newPath)
    {
        for (auto *tree : shell_.findChildren<FileTreeWidget *>()) {
            if (tree->rootPath() == shell_.projectRoot()) {
                tree->refreshMovedFile(oldPath, newPath);
            }
        }
    }

    void inspect(const QString &root, const QString &path, std::function<bool()> current,
                 std::function<void(FileStatus)> done)
    {
        source_.requestStatus(root.toStdString(), [this, root, path, current, done = std::move(done)](vcs::Status result) {
            if (!current() || result.error || result.backend != vcs::Backend::Subversion) {
                done({});
                return;
            }
            const auto hit = std::find_if(result.changes.cbegin(), result.changes.cend(), [&path](const vcs::Change &change) {
                return QDir::cleanPath(QString::fromStdString(change.path)) == path;
            });
            if (hit != result.changes.cend()) {
                if (hit->kind == 7) {
                    done({true, false, false, true, false});
                } else if (hit->kind >= 1 && hit->kind <= 5) {
                    done({true, true, true, false, hit->kind != 1});
                } else {
                    done({});
                }
                return;
            }
            // Plain SVN status omits both clean and ignored files. A successful
            // BASE read distinguishes a clean tracked file from an ignored path.
            vcs::HistoryRequest request;
            request.kind = vcs::HistoryKind::SvnSnapshot;
            request.root = root.toStdString();
            request.path = path.toStdString();
            request.revision = {vcs::SvnRevisionKind::Base, 0};
            source_.requestHistory(request, [done](vcs::HistoryData data) {
                const bool tracked = !data.error && !data.snapshotMissing;
                done({!data.error, tracked, false, false, tracked});
            });
        });
    }

    static void dispatch(vcs::VcsPanel &panel, const QString &path, Operation operation,
                         const std::function<bool()> &current)
    {
        switch (operation) {
        case Operation::IgnoreFile:
        case Operation::IgnorePattern:
            panel.promptSvnFileAction(vcs::WriteOp::SvnIgnore, path, current,
                                      operation == Operation::IgnorePattern);
            break;
        case Operation::Rename:
            panel.promptSvnFileAction(vcs::WriteOp::SvnRename, path, current);
            break;
        case Operation::Revert:
            panel.promptSvnFileAction(vcs::WriteOp::SvnRevert, path, current);
            break;
        case Operation::History:
            panel.showSvnFileHistory(path, current);
            break;
        case Operation::Base:
        case Operation::Head:
            panel.compareSvnFile(path, {operation == Operation::Base ? vcs::SvnRevisionKind::Base
                                                                   : vcs::SvnRevisionKind::Head, 0}, current);
            break;
        }
    }

    vedit::app::ApplicationShell &shell_;
    vcs::FfiVcsSource source_;
    quint64 rootEpoch_ = 0;
};

} // namespace

FileTreeWidget::ContextMenuExtension svnExplorerMenu(vedit::app::ApplicationShell &shell)
{
    auto *bridge = new ExplorerMenu(shell);
    return [bridge](QMenu &menu, const QString &path, std::function<bool()> current) {
        bridge->add(menu, path, std::move(current));
    };
}

} // namespace vedit::workspace

#endif
