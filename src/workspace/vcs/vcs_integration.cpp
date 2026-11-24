#include "vcs_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "file_compare.h"
#include "panel_registry.h"
#include "session_store.h"
#include "svn_workspaces.h"
#include "vcs_history.h"
#include "vcs_panel.h"
#include "vcs_source.h"
#include "vcs_toolchain.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace vedit::workspace {

namespace {

QString svnWorkingRoot(const QString &path);

// AC-1.1/AC-1.2 (batch 1): the ONE place a VCS panel is pointed at the workspace
// root. The panel's own API has setRoot() + refresh() as two steps (setRoot()
// only stores the string), so both go through here.
//
// The header widget shows backend / branch / revision — the ROOT itself is
// carried in its tooltip and in a probe-readable property, so a user hovering the
// header (and the acceptance probe reading it) can tell which working copy this
// snapshot describes after a folder switch.
void applyRoot(vcs::VcsPanel *panel, const QString &root)
{
    if (panel == nullptr) {
        return;
    }
    panel->setRoot(root.toStdString());
    if (auto *header = panel->findChild<QLabel *>(QStringLiteral("vcs-header"))) {
        header->setToolTip(root);
    }
    panel->setProperty("veditProjectRoot", root);
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("vcs: root='%s'", root.isEmpty() ? "<none>" : qPrintable(root));
    }
    panel->refresh();
}

QString svnWorkingRoot(const QString &path)
{
    QDir parent(path);
    for (int depth = 0; depth < 64; ++depth) {
        if (parent.exists(QStringLiteral(".git"))) { return {}; }
        if (parent.exists(QStringLiteral(".svn/wc.db"))) { return parent.absolutePath(); }
        if (!parent.cdUp()) { break; }
    }
    return {};
}

QList<vedit::app::RevisionMark> revisionMarks(const std::string &patch)
{
    static const QRegularExpression hunk(QStringLiteral(R"(^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@)"));
    QList<vedit::app::RevisionMark> result;
    int line = 0;
    bool inHunk = false;
    for (const auto &text : QString::fromStdString(patch).split(u'\n')) {
        const auto header = hunk.match(text);
        if (header.hasMatch()) {
            line = header.captured(1).toInt() - 1;
            inHunk = true;
        } else if (inHunk && text.startsWith(u'+')) {
            result.append({line++, false});
        } else if (inHunk && text.startsWith(u'-')) {
            if (result.isEmpty() || result.back().line != line || !result.back().deletion) {
                result.append({line, true});
            }
        } else if (inHunk && text.startsWith(u' ')) {
            ++line;
        } else if (!text.startsWith(u'\\')) {
            inHunk = false;
        }
        if (result.size() == 1024) { break; }
    }
    return result;
}

class SvnGutter final : public QObject {
public:
    explicit SvnGutter(vedit::app::ApplicationShell &shell) : QObject(&shell), shell_(shell)
    {
        connect(&shell_, &vedit::app::ApplicationShell::fileOpened, this, [this](const QString &path) { refresh(path); });
        connect(&shell_, &vedit::app::ApplicationShell::projectRootChanged, this, [this] {
            ++epoch_;
            for (auto *pane : shell_.editorArea()->panes()) {
                for (int index = 0; index < pane->tabCount(); ++index) {
                    pane->provider()->setRevisionMarks(pane->tabEditorWidget(index), {});
                }
            }
            QTimer::singleShot(0, this, [this] { refresh(); });
        });
    }

    void setWorkspaces(SvnWorkspaces *workspaces) { workspaces_ = workspaces; }

    void clearClosedMarks()
    {
        if (!workspaces_) { return; }
        for (auto *pane : shell_.editorArea()->panes()) {
            for (int index = 0; index < pane->tabCount(); ++index) {
                if (workspaces_->isClosed(svnWorkingCopyRoot(pane->tabFilePath(index)))) {
                    pane->provider()->setRevisionMarks(pane->tabEditorWidget(index), {});
                }
            }
        }
    }

    void refresh(const QString &opened = {})
    {
        auto *pane = shell_.editorArea()->activePane();
        if (!pane) { return; }
        const QString path = opened.isEmpty() ? pane->currentFilePath() : opened;
        if (path.isEmpty() || shell_.projectRoot().isEmpty()) { return; }
        const QString project = QFileInfo(shell_.projectRoot()).canonicalFilePath();
        const QString full = QFileInfo(path).canonicalFilePath();
        if (project.isEmpty() || !full.startsWith(project + u'/')) { return; }
        const QString working = svnWorkingCopyRoot(full);
        if (working.isEmpty() || (workspaces_ && workspaces_->isClosed(working))) { return; }
        const QString relative = QDir(working).relativeFilePath(full);
        vcs::HistoryRequest request;
        request.kind = vcs::HistoryKind::SvnPatch;
        request.root = working.toStdString();
        request.paths = {relative.toStdString()};
        const auto epoch = epoch_;
        const auto sequence = ++sequence_;
        latest_.insert(path, sequence);
        source_.requestHistory(request, [this, path, working, epoch, sequence](vcs::HistoryData data) {
            if (epoch != epoch_ || latest_.value(path) != sequence) { return; }
            latest_.remove(path);
            if (workspaces_ && workspaces_->isClosed(working)) { return; }
            const auto marks = data.error ? QList<vedit::app::RevisionMark>{} : revisionMarks(data.patch);
            for (auto *pane : shell_.editorArea()->panes()) {
                for (int index = 0; index < pane->tabCount(); ++index) {
                    auto *editor = pane->tabEditorWidget(index);
                    if (pane->tabFilePath(index) == path && !pane->provider()->isModified(editor)) {
                        pane->provider()->setRevisionMarks(editor, marks);
                    }
                }
            }
        });
    }

private:
    vedit::app::ApplicationShell &shell_;
    QPointer<SvnWorkspaces> workspaces_;
    vcs::FfiVcsSource source_;
    QHash<QString, quint64> latest_;
    quint64 epoch_ = 0;
    quint64 sequence_ = 0;
};

class SvnComparisonLoader final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(SvnComparison)

public:
    SvnComparisonLoader(vedit::app::ApplicationShell &shell, QObject *parent)
        : QObject(parent), shell_(shell)
    {
        connect(&shell_, &vedit::app::ApplicationShell::projectRootChanged, this,
                [this](const QString &) { ++generation_; });
    }

    void start(QString root, QString path, QList<vcs::SvnRevision> revisions, quint64 peg, bool workingCopy,
               std::function<bool()> selectionStillCurrent)
    {
        if (root.isEmpty() || path.isEmpty() || revisions.isEmpty()) {
            return;
        }
        auto request = std::make_shared<Request>();
        request->root = std::move(root);
        request->path = std::move(path);
        request->revisions = std::move(revisions);
        request->peg = peg;
        request->selectionStillCurrent = std::move(selectionStillCurrent);
        request->generation = ++generation_;
        request->workingCopy = workingCopy;
        shell_.statusBar()->showMessage(tr("Reading SVN revisions…"), 5000);
        if (workingCopy) {
            fetch(request);
        } else if (request->revisions.size() == 2) {
            fetch(request);
        } else {
            const auto before = request->revisions.front();
            auto spec = specification(*request, vcs::HistoryKind::SvnPrevious, before);
            source_.requestHistory(spec, [this, request](vcs::HistoryData data) {
                if (!accept(*request, data)) {
                    return;
                }
                const quint64 previous = data.commits.empty() ? 0 :
                    QString::fromStdString(data.commits.front().hash).toULongLong();
                request->revisions.prepend({vcs::SvnRevisionKind::Number, previous});
                fetch(request);
            });
        }
    }

    void openSnapshot(const QString &root, const QString &path, vcs::SvnRevision revision, quint64 peg, std::function<bool()> selected)
    {
        if (root.isEmpty()) { return; }
        auto request = std::make_shared<Request>();
        request->root = root;
        request->path = path;
        request->peg = peg;
        request->generation = ++generation_;
        request->selectionStillCurrent = std::move(selected);
        const auto spec = specification(*request, vcs::HistoryKind::SvnSnapshot, revision);
        source_.requestHistory(spec, [this, request, revision](vcs::HistoryData data) {
            if (!accept(*request, data)) { return; }
            showSnapshot(*request, revision, data);
        });
    }

    void openRemote(const QString &root, const QString &url, const QString &path, quint64 number,
                    bool deleted, bool compare, std::function<bool()> selected)
    {
        auto request = std::make_shared<Request>();
        request->root = root;
        request->url = url;
        request->path = path;
        request->peg = deleted && number > 0 ? number - 1 : number;
        request->generation = ++generation_;
        request->selectionStillCurrent = std::move(selected);
        const vcs::SvnRevision revision{vcs::SvnRevisionKind::Number, number};
        const auto kind = compare ? vcs::HistoryKind::SvnRemotePrevious : vcs::HistoryKind::SvnRemoteSnapshot;
        shell_.statusBar()->showMessage(tr("Reading SVN revisions…"), 5000);
        source_.requestHistory(specification(*request, kind, revision), [this, request, revision, compare](vcs::HistoryData data) {
            if (!accept(*request, data)) { return; }
            if (compare) {
                const auto previous = QString::fromStdString(data.hash).toULongLong();
                request->revisions = {{vcs::SvnRevisionKind::Number, previous}, revision};
                fetch(request);
            } else {
                showSnapshot(*request, revision, data);
            }
        });
    }

private:
    struct Request {
        QString root;
        QString url;
        QString path;
        QList<vcs::SvnRevision> revisions;
        QList<vedit::app::CompareInput> snapshots;
        std::function<bool()> selectionStillCurrent;
        quint64 generation = 0;
        quint64 peg = vcs::kDefaultSvnPeg;
        bool workingCopy = false;
    };

    static vcs::HistoryRequest specification(const Request &request, vcs::HistoryKind kind, vcs::SvnRevision revision)
    {
        vcs::HistoryRequest spec;
        spec.kind = kind;
        spec.root = request.root.toStdString();
        spec.path = request.path.toStdString();
        spec.url = request.url.toStdString();
        spec.revision = revision;
        spec.peg = request.peg;
        return spec;
    }

    static QString revisionName(vcs::SvnRevision revision)
    {
        switch (revision.kind) {
        case vcs::SvnRevisionKind::Base: return QStringLiteral("BASE");
        case vcs::SvnRevisionKind::Head: return QStringLiteral("HEAD");
        case vcs::SvnRevisionKind::Number: return QStringLiteral("r%1").arg(revision.number);
        }
        return {};
    }

    bool accept(const Request &request, const vcs::HistoryData &data)
    {
        if (request.generation != generation_ || !request.selectionStillCurrent()) {
            return false;
        }
        if (data.error) {
            shell_.statusBar()->showMessage(tr("Cannot read SVN comparison: %1").arg(QString::fromStdString(data.note)), 12000);
            return false;
        }
        return true;
    }

    void showSnapshot(const Request &request, vcs::SvnRevision revision, const vcs::HistoryData &data)
    {
            auto *dialog = new QDialog(&shell_);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(tr("%1 at %2").arg(request.path, revisionName(revision)));
            auto *layout = new QVBoxLayout(dialog);
            auto *text = new QPlainTextEdit(dialog);
            text->setReadOnly(true);
            text->setPlainText(data.snapshotMissing ? tr("This file did not exist at that revision.")
                                                     : QString::fromStdString(data.patch));
            layout->addWidget(text);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            layout->addWidget(buttons);
            dialog->resize(800, 560);
            dialog->show();
    }

    void fetch(const std::shared_ptr<Request> &request)
    {
        const auto revision = request->revisions.at(request->snapshots.size());
        const auto kind = request->url.isEmpty() ? vcs::HistoryKind::SvnSnapshot : vcs::HistoryKind::SvnRemoteSnapshot;
        const auto spec = specification(*request, kind, revision);
        source_.requestHistory(spec, [this, request, revision](vcs::HistoryData data) {
            if (!accept(*request, data)) {
                return;
            }
            const QString label = QStringLiteral("%1 · %2").arg(request->path, revisionName(revision));
            request->snapshots.append({{}, label, QByteArray::fromStdString(data.patch), data.snapshotMissing});
            if (request->snapshots.size() < request->revisions.size()) {
                fetch(request);
                return;
            }
            if (request->workingCopy) {
                QDir repository(request->root);
                while (!repository.exists(QStringLiteral(".svn")) && repository.cdUp()) {
                }
                const QString path = repository.absoluteFilePath(request->path);
                const QString working = tr("%1 · Working Copy").arg(request->path);
                const QFileInfo info(path);
                const bool exists = info.exists();
                const auto canonical = info.canonicalFilePath();
                if (exists && (!info.isFile() || !canonical.startsWith(QFileInfo(repository.absolutePath()).canonicalFilePath() + u'/'))) {
                    shell_.statusBar()->showMessage(tr("The working file is outside the SVN working copy or is not a regular file."), 8000);
                    return;
                }
                request->snapshots.append({exists ? path : QString(), working, {}, !exists});
            }
            shell_.statusBar()->clearMessage();
            vedit::app::showFileComparison(shell_, request->snapshots.at(0), request->snapshots.at(1));
        });
    }

    vedit::app::ApplicationShell &shell_;
    vcs::FfiVcsSource source_;
    quint64 generation_ = 0;
};

} // namespace

QString svnWorkingCopyRoot(const QString &path)
{
    return path.isEmpty() ? QString() : svnWorkingRoot(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
}

void installVcs(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    const QString workingCopiesFile = options.stateDir.isEmpty()
        ? QString() : QDir(options.stateDir).filePath(QStringLiteral("svn-working-copies.ini"));
    // Batch 6a (rev3): the lane's init is the observable point for the bundled
    // VCS toolchain — one `VCS-TOOLCHAIN ...` line, printed once per process
    // (the panel factory reuses the cached values for its properties).
    (void)vcs::reportToolchainOnce();
    // The root is read from the shell INSIDE the factory (never captured): the
    // shell resolves the persisted session root before the lanes install, and a
    // dock re-inserted after a runtime switch must start at the NEW root.
    // Batch 4b: the dock carries two tabs — the working tree (batch 3b/4a) and the
    // history view (commit list / detail / file history / blame / comparison).
    // Both are pointed at the same workspace root and both read asynchronously.
    auto *gutter = new SvnGutter(shell);
    const auto factory = [&shell, gutter, workingCopiesFile]() {
        auto *tabs = new QTabWidget();
        tabs->setObjectName(QStringLiteral("vcs-tabs"));
        auto *panel = new vcs::VcsPanel(tabs);
        QObject::connect(panel, &vcs::VcsPanel::svnStatusReady, gutter, [gutter] { gutter->refresh(); });
        auto *history = new vcs::VcsHistoryPanel(tabs);
        history->setSource(std::make_unique<vcs::FfiVcsSource>(), true);
        history->setSavedRepositoriesFile(QFileInfo(shell.sessionStore()->path()).dir().filePath(QStringLiteral("svn-repositories.json")));
        auto *comparison = new SvnComparisonLoader(shell, tabs);
        QObject::connect(panel, &vcs::VcsPanel::svnWorkingComparisonRequested, comparison,
                         [comparison, panel](const QString &path, vcs::SvnRevision revision) {
                             const auto selection = panel->svnSelectionEpoch();
                             comparison->start(QString::fromStdString(panel->root()), path, {revision}, vcs::kDefaultSvnPeg, true,
                                               [panel = QPointer(panel), selection] {
                                                   return panel && selection == panel->svnSelectionEpoch() && panel->svnSelectionCurrent();
                                               });
                         });
        QObject::connect(panel, &vcs::VcsPanel::svnFileHistoryRequested, history,
                         [tabs, history](const QString &path, std::function<bool()> stillCurrent) {
                             const QSignalBlocker block(tabs);
                             tabs->setCurrentWidget(history);
                             history->openFileHistory(path, std::move(stillCurrent));
                         });
        QObject::connect(history, &vcs::VcsHistoryPanel::svnSnapshotRequested, comparison,
                         [comparison, history](const QString &path, quint64 revision, quint64 peg) {
                             const auto selection = history->svnSelectionEpoch();
                             comparison->openSnapshot(QString::fromStdString(history->root()), path, {vcs::SvnRevisionKind::Number, revision}, peg, [history = QPointer(history), selection] {
                                 return history && history->svnSelectionEpoch() == selection;
                             });
                         });
        QObject::connect(panel, &vcs::VcsPanel::svnWorkingSnapshotRequested, comparison,
                         [comparison, panel](const QString &path, vcs::SvnRevision revision) {
                             const auto selection = panel->svnSelectionEpoch();
                             comparison->openSnapshot(QString::fromStdString(panel->root()), path, revision, vcs::kDefaultSvnPeg, [panel = QPointer(panel), selection] {
                                 return panel && panel->svnSelectionEpoch() == selection;
                             });
                         });
        QObject::connect(history, &vcs::VcsHistoryPanel::svnBaseComparisonRequested, comparison,
                         [comparison, history](const QString &path, quint64 revision, quint64 peg) {
                             const auto selection = history->svnSelectionEpoch();
                             comparison->start(QString::fromStdString(history->root()), path, {{vcs::SvnRevisionKind::Number, revision}, {vcs::SvnRevisionKind::Base, 0}}, peg, false,
                                               [history = QPointer(history), selection] { return history && history->svnSelectionEpoch() == selection; });
                         });
        QObject::connect(history, &vcs::VcsHistoryPanel::svnRemoteFileRequested, comparison,
                         [comparison, history](const QString &url, const QString &path, quint64 revision, bool deleted, bool compare) {
                             const auto selection = history->svnSelectionEpoch();
                             comparison->openRemote(QString::fromStdString(history->root()), url, path, revision, deleted, compare,
                                                    [history = QPointer(history), selection] { return history && history->svnSelectionEpoch() == selection; });
                         });
        QObject::connect(history, &vcs::VcsHistoryPanel::svnComparisonRequested, comparison,
                         [comparison, history](const QString &path, const QList<quint64> &selected, quint64 peg, bool workingCopy) {
                             QList<vcs::SvnRevision> revisions;
                             for (const quint64 revision : selected) {
                                 revisions.append({vcs::SvnRevisionKind::Number, revision});
                             }
                             const auto selection = history->svnSelectionEpoch();
                             comparison->start(QString::fromStdString(history->root()), path, revisions, peg, workingCopy, [history = QPointer(history), selection] {
                                 return history && selection == history->svnSelectionEpoch();
                             });
                         });
        const auto openWorkingFile = [&shell](const QString &root, const QString &path) {
            const QString full = QFileInfo(QDir(root).absoluteFilePath(path)).canonicalFilePath();
            if (root.isEmpty() || !full.startsWith(root + u'/') || !QFileInfo(full).isFile()) {
                shell.statusBar()->showMessage(vcs::VcsPanel::tr("The working file is unavailable."), 8000);
                return;
            }
            QString error;
            if (!shell.editorArea()->activePane()->openFile(full, &error)) { shell.statusBar()->showMessage(error, 8000); }
        };
        QObject::connect(panel, &vcs::VcsPanel::svnOpenFileRequested, panel, [panel, openWorkingFile](const QString &path) {
            openWorkingFile(QString::fromStdString(panel->root()), path);
        });
        QObject::connect(history, &vcs::VcsHistoryPanel::svnWorkingFileRequested, history, [history, openWorkingFile](const QString &path) {
            openWorkingFile(QString::fromStdString(history->root()), path);
        });
        QObject::connect(panel, &vcs::VcsPanel::svnOpenCheckoutRequested, &shell,
                         [&shell](const QString &path) { shell.setProjectRoot(path); });
        tabs->addTab(panel, QCoreApplication::translate("VEdit", "Working tree"));
        tabs->addTab(history, QCoreApplication::translate("VEdit", "History"));
        auto *workspaces = new SvnWorkspaces(*panel, workingCopiesFile, [panel, history, gutter](const QString &root) {
            history->setRoot(root.toStdString());
            if (!root.isEmpty() && history->isVisible()) { history->refresh(); }
            applyRoot(panel, root);
            if (root.isEmpty()) { gutter->clearClosedMarks(); }
        }, [panel] { panel->refreshSvnRemote(false); }, std::make_unique<vcs::FfiVcsSource>());
        gutter->setWorkspaces(workspaces);
        const auto setProject = [workspaces](const QString &root) {
            workspaces->setProject(root, svnWorkingCopyRoot(root));
        };
        setProject(shell.projectRoot());
        panel->setSource(std::make_unique<vcs::FfiVcsSource>());
        QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, panel, setProject);
        QObject::connect(tabs, &QTabWidget::currentChanged, history, [tabs, history](int) {
            if (tabs->currentWidget() == history) { history->refresh(); }
        });
        // Batch 6a (rev3): the chosen VCS toolchain is probe-visible. The line
        // itself is printed once by the lane; here it becomes a property so an
        // acceptance probe can assert `source=bundled` and the versions.
        const vcs::ToolchainStatus &toolchain = vcs::reportToolchainOnce();
        for (QWidget *widget : {static_cast<QWidget *>(tabs), static_cast<QWidget *>(panel), static_cast<QWidget *>(history)}) {
            // Batch 7: one shared helper — the lane asserts the SAME properties.
            vcs::applyToolchainProperties(widget, toolchain);
        }
        return tabs;
    };
    shell.panels()->setFactory(QStringLiteral("vcs"), factory);
    const QString checkoutId = QStringLiteral("file.svn-checkout");
    vedit::app::CommandSpec checkout;
    checkout.id = checkoutId;
    checkout.text = vcs::VcsPanel::tr("Check Out SVN Repository…");
    checkout.menu = QStringLiteral("file");
    checkout.order = 34;
    shell.commands()->add(std::move(checkout));
    shell.commands()->setHandler(checkoutId, [&shell] {
        const auto id = QStringLiteral("vcs");
        shell.panels()->showPanel(id, &shell);
        if (auto *dock = shell.panels()->dock(id)) {
            if (auto *panel = dock->findChild<vcs::VcsPanel *>()) { panel->promptSvnCheckout(); }
        }
    });

    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("vcs: installVcs projectRoot='%s'", qPrintable(shell.projectRoot()));
    }
}

} // namespace vedit::workspace
