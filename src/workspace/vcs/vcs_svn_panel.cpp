#include "vcs_panel.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

namespace vedit::workspace::vcs {
namespace {

std::vector<std::string> pathArguments(const QStringList &paths)
{
    std::vector<std::string> result;
    result.reserve(paths.size());
    for (const auto &path : paths) {
        result.push_back(path.toStdString());
    }
    return result;
}

} // namespace

QStringList VcsPanel::selectedSvnPaths() const
{
    QStringList paths;
    for (const auto *item : changes_->selectedItems()) {
        paths.push_back(item->data(Qt::UserRole).toString());
    }
    return paths;
}

QString VcsPanel::svnChangelist(const QString &path) const
{
    for (const auto &entry : changes_snapshot_) {
        if (entry.path == path.toStdString()) {
            return QString::fromStdString(entry.changelist);
        }
    }
    return {};
}

int VcsPanel::svnChangeKind(const QString &path) const
{
    const std::string wanted = path.toStdString();
    for (const auto &entry : changes_snapshot_) {
        if (entry.path == wanted) { return entry.kind; }
    }
    return 0;
}

void VcsPanel::updateSvnActions()
{
    const bool svn = backend_ == Backend::Subversion;
    changes_->setSelectionMode(svn ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    svnActions_->setVisible(svn || backend_ == Backend::None);
    svnActions_->setEnabled(source_ && !write_in_flight_);
}

void VcsPanel::buildSvnMenu(QMenu &menu, bool context)
{
    menu.clear();
    const bool writable = source_ && source_->writeCapable() && !write_in_flight_;
    const auto epoch = rootEpoch_;
    const auto action = [&menu, this, epoch](const QString &title, const char *id, bool enabled, auto run) {
        auto *item = menu.addAction(title, this, [this, epoch, run] {
            if (epoch == rootEpoch_) { run(); }
        });
        item->setObjectName(QString::fromLatin1(id));
        item->setEnabled(enabled);
    };
    if (!context) {
        action(tr("Check Out SVN Repository…"), "svn-checkout", writable, [this] { promptSvnCheckout(); });
    }
    if (backend_ != Backend::Subversion) {
        return;
    }
    const auto selected = selectedSvnPaths();
    const QString path = selected.size() == 1 ? selected.front() : QString();
    const QString list = svnChangelist(path.isEmpty() ? selected.value(0) : path);
    QStringList commitPaths, changedPaths, conflicts, selectedConflicts, unversioned, selectedUnversioned;
    for (const auto &entry : changes_snapshot_) {
        const auto candidate = QString::fromStdString(entry.path);
        if (entry.kind == 5) {
            conflicts.append(candidate);
            if (selected.contains(candidate)) {
                selectedConflicts.append(candidate);
            }
        }
        if (entry.kind == 7) {
            unversioned.append(candidate);
            if (selected.contains(candidate)) { selectedUnversioned.append(candidate); }
        } else {
            changedPaths.append(candidate);
            if (selected.contains(candidate)) {
                commitPaths.append(candidate);
            }
        }
    }
    const bool versionedFile = !path.isEmpty() && !unversioned.contains(path);
    if (context) {
        action(tr("Open Working File"), "svn-open-working", !path.isEmpty(), [this, path] { emit svnOpenFileRequested(path); });
        action(tr("File History"), "svn-file-log", versionedFile, [this, path] { emit svnFileHistoryRequested(path); });
        action(tr("Compare With BASE"), "svn-compare-base", versionedFile, [this, path] {
            emit svnWorkingComparisonRequested(path, {SvnRevisionKind::Base, 0});
        });
        action(tr("Compare With HEAD"), "svn-compare-head", versionedFile, [this, path] {
            emit svnWorkingComparisonRequested(path, {SvnRevisionKind::Head, 0});
        });
        action(tr("Open HEAD Revision"), "svn-open-head", versionedFile, [this, path] {
            emit svnWorkingSnapshotRequested(path, {SvnRevisionKind::Head, 0});
        });
        menu.addSeparator();
    }
    action(tr("Commit Selected…"), "svn-commit-selected", writable && !commitPaths.isEmpty() && selectedConflicts.isEmpty(),
           [this, commitPaths] { promptSvnCommit(commitPaths); });
    action(tr("Commit Changelist…"), "svn-commit-changelist", writable && !list.isEmpty(),
           [this, list] { promptSvnCommit({}, list); });
    action(tr("Set Changelist…"), "svn-changelist", writable && !commitPaths.isEmpty(), [this, commitPaths, epoch] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Set Changelist"), tr("Changelist name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty() && epoch == rootEpoch_) {
            WriteRequest request;
            request.op = WriteOp::SvnChangelist;
            request.root = root_;
            request.name = name.toStdString();
            request.paths = pathArguments(commitPaths);
            issueWrite(request);
        }
    });
    action(tr("Remove From Changelist"), "svn-changelist-remove", writable && !list.isEmpty(), [this, commitPaths] {
        confirmSvnPaths(WriteOp::SvnChangelistRemove, commitPaths);
    });
    menu.addSeparator();
    action(tr("Resolve Selected…"), "svn-resolve", writable && !selectedConflicts.isEmpty(), [this, selectedConflicts] {
        confirmSvnPaths(WriteOp::SvnResolve, selectedConflicts);
    });
    action(tr("Resolve All Conflicts…"), "svn-resolve-all", writable && !conflicts.isEmpty(), [this, conflicts] {
        confirmSvnPaths(WriteOp::SvnResolve, conflicts);
    });
    action(tr("Revert All Changes…"), "svn-revert-all", writable && !changedPaths.isEmpty(), [this, changedPaths] {
        confirmSvnPaths(WriteOp::SvnRevert, changedPaths);
    });
    action(tr("Rename With SVN…"), "svn-rename", writable && versionedFile, [this, path] {
        promptSvnFileAction(WriteOp::SvnRename, path);
    });
    action(tr("Delete Unversioned Paths…"), "svn-delete-unversioned", writable && !selectedUnversioned.isEmpty(), [this, selectedUnversioned] {
        confirmSvnPaths(WriteOp::SvnDeleteUnversioned, selectedUnversioned);
    });
    action(tr("Ignore File or Pattern…"), "svn-ignore", writable && unversioned.contains(path), [this, path] {
        promptSvnFileAction(WriteOp::SvnIgnore, path);
    });
    menu.addSeparator();
    action(tr("Show Patch for Selected"), "svn-patch-selected", !commitPaths.isEmpty(), [this, commitPaths] {
        readSvnWorking(HistoryKind::SvnPatch, commitPaths);
    });
    action(tr("Show Patch for All Changes"), "svn-patch-all", true, [this] { readSvnWorking(HistoryKind::SvnPatch); });
    action(tr("Show Patch for Changelist"), "svn-patch-changelist", !list.isEmpty(), [this, list] {
        readSvnWorking(HistoryKind::SvnPatch, {}, list);
    });
    if (context) {
        return;
    }
    menu.addSeparator();
    action(tr("Create Branch…"), "svn-branch-create", writable, [this] { promptSvnBranch(WriteOp::SvnBranchCreate); });
    action(tr("Switch Branch…"), "svn-switch", writable, [this] { promptSvnBranch(WriteOp::SvnSwitch); });
    action(tr("Merge Branch…"), "svn-merge", writable, [this] { promptSvnBranch(WriteOp::SvnMerge); });
    action(tr("Clean Up Working Copy"), "svn-cleanup", writable, [this] { confirmSvnPaths(WriteOp::SvnCleanup, {}); });
    action(tr("Refresh Remote Changes"), "svn-remote-refresh", !svnRemoteRead_, [this] { readSvnWorking(HistoryKind::SvnIncoming); });
}

void VcsPanel::promptSvnFileAction(WriteOp op, const QString &path, std::function<bool()> stillCurrent, bool promptIgnorePattern)
{
    if (root_.empty() || (stillCurrent && !stillCurrent()) || !source_ || !source_->writeCapable()) { return; }
    const QDir root(QString::fromStdString(root_));
    const QString relative = root.relativeFilePath(QDir::isAbsolutePath(path) ? path : root.absoluteFilePath(path));
    const auto epoch = rootEpoch_;
    const auto current = [&] { return epoch == rootEpoch_ && (!stillCurrent || stillCurrent()); };
    WriteRequest request;
    request.op = op;
    request.root = root_;
    request.paths = {relative.toStdString()};
    bool ok = false;
    switch (op) {
    case WriteOp::SvnRename: {
        const auto destination = QInputDialog::getText(this, tr("Rename With SVN"), tr("New working-copy relative path"),
                                                       QLineEdit::Normal, relative, &ok);
        if (!ok || destination.isEmpty() || destination == relative) { return; }
        request.target = destination.toStdString();
        break;
    }
    case WriteOp::SvnIgnore: {
        if (!promptIgnorePattern) { break; }
        const auto pattern = QInputDialog::getText(this, tr("Ignore in SVN"), tr("Pattern in this directory (blank for this exact name)"),
                                                   QLineEdit::Normal, {}, &ok);
        if (!ok) { return; }
        request.name = pattern.toStdString();
        break;
    }
    case WriteOp::SvnRevert:
        if (!askConfirm(tr("Revert SVN Changes?"), tr("Discard local changes in these paths?\n%1").arg(relative)) || !current()
            || !askConfirm(tr("Confirm Permanent Revert"), tr("The reverted local edits cannot be recovered from SVN."))) { return; }
        break;
    default: return;
    }
    if (current()) { issueWrite(request); }
}

void VcsPanel::showSvnFileHistory(const QString &path, std::function<bool()> stillCurrent)
{
    if (root_.empty() || (stillCurrent && !stillCurrent())) { return; }
    const QDir root(QString::fromStdString(root_));
    emit svnFileHistoryRequested(QDir::isAbsolutePath(path) ? root.relativeFilePath(path) : path, std::move(stillCurrent));
}

void VcsPanel::compareSvnFile(const QString &path, SvnRevision revision, std::function<bool()> stillCurrent)
{
    if (root_.empty() || (stillCurrent && !stillCurrent())) { return; }
    const QDir root(QString::fromStdString(root_));
    selected_path_ = QDir::isAbsolutePath(path) ? root.relativeFilePath(path) : path;
    externalSelection_ = std::move(stillCurrent);
    ++svnSelectionEpoch_;
    emit svnWorkingComparisonRequested(selected_path_, revision);
}

void VcsPanel::promptSvnCheckout()
{
    const quint64 epoch = rootEpoch_;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Check Out SVN Repository"));
    dialog.setMinimumWidth(540);
    auto *form = new QFormLayout(&dialog);
    auto *url = new QLineEdit(&dialog);
    auto *directory = new QLineEdit(&dialog);
    auto *browse = new QPushButton(tr("Choose Parent Directory…"), &dialog);
    auto *name = new QLineEdit(&dialog);
    url->setObjectName(QStringLiteral("svn-checkout-url"));
    directory->setObjectName(QStringLiteral("svn-checkout-parent"));
    name->setObjectName(QStringLiteral("svn-checkout-name"));
    form->addRow(tr("Repository URL"), url);
    form->addRow(tr("Local parent directory"), directory);
    form->addRow(browse);
    form->addRow(tr("New directory name"), name);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(browse, &QPushButton::clicked, &dialog, [directory, &dialog] {
        const auto path = QFileDialog::getExistingDirectory(&dialog, tr("Choose Parent Directory"), directory->text());
        if (!path.isEmpty()) { directory->setText(path); }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || epoch != rootEpoch_) {
        return;
    }
    WriteRequest request;
    request.op = WriteOp::SvnCheckout;
    request.root = directory->text().toStdString();
    request.target = name->text().toStdString();
    request.url = url->text().trimmed().toStdString();
    issueWrite(request);
}

void VcsPanel::promptSvnCommit(QStringList paths, QString changelist)
{
    const quint64 epoch = rootEpoch_;
    const QString scope = !changelist.isEmpty() ? tr("Changelist %1").arg(changelist)
        : paths.isEmpty() ? tr("Entire working copy") : paths.join(u'\n');
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Commit to SVN"));
    dialog.setLabelText(tr("Commit message for\n%1").arg(scope));
    dialog.setOption(QInputDialog::UsePlainTextEditForTextInput);
    dialog.setMinimumWidth(440);
    if (auto *buttons = dialog.findChild<QDialogButtonBox *>()) {
        auto *previous = buttons->addButton(tr("Use Previous Message…"), QDialogButtonBox::ActionRole);
        previous->setObjectName(QStringLiteral("svn-previous-message"));
        connect(previous, &QPushButton::clicked, &dialog, [this, epoch, previous, &dialog] {
            if (epoch != rootEpoch_ || !source_) { return; }
            previous->setEnabled(false);
            HistoryRequest history;
            history.kind = HistoryKind::Commits;
            history.root = root_;
            history.limit = 30;
            source_->requestHistory(history, [this, epoch, dialog = QPointer(&dialog), button = QPointer(previous)](HistoryData data) {
                if (!dialog || epoch != rootEpoch_) { return; }
                button->setEnabled(true);
                if (data.error || data.commits.empty()) {
                    dialog->setLabelText(data.error ? QString::fromStdString(data.note) : tr("No previous commit messages."));
                    return;
                }
                QStringList labels;
                for (const auto &entry : data.commits) {
                    labels.append(QStringLiteral("%1  %2").arg(QString::fromStdString(entry.shortHash),
                                  QString::fromStdString(entry.subject).section(u'\n', 0, 0)));
                }
                bool ok = false;
                const QString chosen = QInputDialog::getItem(dialog, tr("Previous SVN Commit Messages"), tr("Commit message"), labels, 0, false, &ok);
                const int index = labels.indexOf(chosen);
                if (ok && dialog && epoch == rootEpoch_ && index >= 0) {
                    dialog->setTextValue(QString::fromStdString(data.commits.at(static_cast<std::size_t>(index)).subject));
                }
            });
        });
    }
    if (dialog.exec() != QDialog::Accepted || epoch != rootEpoch_) { return; }
    WriteRequest request;
    request.op = WriteOp::SvnCommit;
    request.root = root_;
    request.message = dialog.textValue().toStdString();
    request.name = changelist.toStdString();
    request.paths = pathArguments(paths);
    issueWrite(request);
}

void VcsPanel::promptSvnBranch(WriteOp op)
{
    const quint64 epoch = rootEpoch_;
    const QString title = op == WriteOp::SvnBranchCreate ? tr("Create SVN Branch")
        : op == WriteOp::SvnSwitch ? tr("Switch SVN Branch") : tr("Merge SVN Branch");
    bool ok = false;
    const auto target = QInputDialog::getText(this, title, tr("Repository-relative URL (for example ^/branches/topic)"),
                                             QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || target.isEmpty() || epoch != rootEpoch_) { return; }
    QString message;
    if (op == WriteOp::SvnBranchCreate) {
        message = QInputDialog::getMultiLineText(this, title, tr("Commit message for the repository copy"), {}, &ok);
    } else {
        ok = askConfirm(title, tr("Apply %1 to this working copy? Local changes may conflict.").arg(target));
    }
    if (!ok || epoch != rootEpoch_) { return; }
    WriteRequest request;
    request.op = op;
    request.root = root_;
    request.target = target.toStdString();
    request.message = message.toStdString();
    issueWrite(request);
}

void VcsPanel::confirmSvnPaths(WriteOp op, const QStringList &paths)
{
    const quint64 epoch = rootEpoch_;
    if (op == WriteOp::SvnResolve && !confirmTwice(tr("Mark SVN Conflicts Resolved?"),
        tr("Use the current working contents of these paths?\n%1").arg(paths.join(u'\n')),
        tr("Confirm Conflict Resolution"), tr("Confirm you have reviewed the working contents and resolved the conflicts."))) {
        return;
    }
    if (op == WriteOp::SvnRevert && !confirmTwice(tr("Revert SVN Changes?"),
        tr("Discard local changes in these paths?\n%1").arg(paths.join(u'\n')),
        tr("Confirm Permanent Revert"), tr("The reverted local edits cannot be recovered from SVN."))) {
        return;
    }
    if (op == WriteOp::SvnDeleteUnversioned && !confirmTwice(tr("Delete Unversioned Paths?"),
        tr("Permanently delete these selected paths and any contained files?\n%1").arg(paths.join(u'\n')),
        tr("Confirm Permanent Deletion"), tr("SVN cannot recover unversioned files."))) {
        return;
    }
    if (epoch != rootEpoch_) { return; }
    WriteRequest request;
    request.op = op;
    request.root = root_;
    request.paths = pathArguments(paths);
    issueWrite(request);
}

void VcsPanel::refreshSvnRemote(bool interactive)
{
    readSvnWorking(HistoryKind::SvnIncoming, {}, {}, interactive);
}

void VcsPanel::readSvnWorking(HistoryKind kind, const QStringList &paths, const QString &changelist, bool interactive)
{
    if (!source_ || backend_ != Backend::Subversion || (kind == HistoryKind::SvnIncoming && svnRemoteRead_)) {
        return;
    }
    HistoryRequest request;
    request.kind = kind;
    request.root = root_;
    request.paths = pathArguments(paths);
    request.query = changelist.toStdString();
    svnRemoteRead_ |= kind == HistoryKind::SvnIncoming;
    if (interactive) {
        message_->setText(kind == HistoryKind::SvnIncoming ? tr("Checking for incoming changes…") : tr("Generating SVN patch…"));
        message_->show();
    }
    const quint64 epoch = rootEpoch_;
    source_->requestHistory(request, [this, epoch, kind, interactive](HistoryData data) {
        if (epoch != rootEpoch_) { return; }
        if (kind == HistoryKind::SvnIncoming) { svnRemoteRead_ = false; }
        if (interactive) { message_->hide(); }
        if (data.error && interactive) {
            message_->setText(QString::fromStdString(data.note));
            message_->show();
        } else if (!data.error && kind == HistoryKind::SvnIncoming) {
            showSvnIncoming(data, interactive);
        } else if (!data.error) {
            showSvnPatch(QString::fromStdString(data.patch));
        }
    });
}

void VcsPanel::showSvnIncoming(const HistoryData &data, bool interactive)
{
    svnIncomingCount_ = static_cast<int>(data.files.size());
    renderHeader();
    if (!interactive) { return; }
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Incoming SVN Changes"));
    auto *layout = new QVBoxLayout(dialog);
    auto *files = new QListWidget(dialog);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto &file : data.files) {
        files->addItem(QString::fromStdString(file.path));
    }
    if (data.files.empty()) { layout->addWidget(new QLabel(tr("The working copy is up to date."), dialog)); }
    layout->addWidget(files);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *compare = buttons->addButton(tr("Compare With HEAD"), QDialogButtonBox::ActionRole);
    compare->setEnabled(false);
    auto *update = buttons->addButton(tr("Update Selected"), QDialogButtonBox::ActionRole);
    update->setEnabled(false);
    const quint64 epoch = rootEpoch_;
    connect(files, &QListWidget::itemSelectionChanged, dialog, [this, epoch, files, update, compare] {
        const auto selected = files->selectedItems();
        const bool valid = epoch == rootEpoch_ && !selected.isEmpty();
        compare->setEnabled(valid && selected.size() == 1 && selected.front()->text() != QStringLiteral("."));
        update->setEnabled(valid && !write_in_flight_);
    });
    connect(compare, &QPushButton::clicked, dialog, [this, files, epoch] {
        if (epoch != rootEpoch_ || files->selectedItems().size() != 1) { return; }
        const auto path = files->selectedItems().front()->text();
        compareSvnFile(path, {SvnRevisionKind::Head, 0}, [files = QPointer(files), path] {
            return files && files->selectedItems().size() == 1 && files->selectedItems().front()->text() == path;
        });
    });
    connect(update, &QPushButton::clicked, dialog, [this, dialog, files, epoch] {
        if (epoch != rootEpoch_) { dialog->close(); return; }
        QStringList paths;
        for (const auto *item : files->selectedItems()) { paths.append(item->text()); }
        if (paths.isEmpty() || !askConfirm(tr("Update Selected SVN Paths?"), paths.join(u'\n')) || epoch != rootEpoch_) { return; }
        WriteRequest request;
        request.op = WriteOp::SvnUpdate;
        request.root = root_;
        if (!paths.contains(QStringLiteral("."))) { request.paths = pathArguments(paths); }
        issueWrite(request);
        dialog->close();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->resize(550, 360);
    dialog->show();
}

void VcsPanel::showSvnPatch(const QString &patch)
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("SVN Patch"));
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(patch.isEmpty() ? tr("There are no versioned changes in this scope.") : patch);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *copy = buttons->addButton(tr("Copy Patch"), QDialogButtonBox::ActionRole);
    auto *save = buttons->addButton(tr("Save Patch…"), QDialogButtonBox::ActionRole);
    copy->setEnabled(!patch.isEmpty());
    save->setEnabled(!patch.isEmpty());
    connect(copy, &QPushButton::clicked, dialog, [patch] { QGuiApplication::clipboard()->setText(patch); });
    connect(save, &QPushButton::clicked, dialog, [dialog, patch] {
        const auto path = QFileDialog::getSaveFileName(dialog, tr("Save SVN Patch"), {}, tr("Patch files (*.patch *.diff)"));
        if (path.isEmpty()) { return; }
        QSaveFile file(path);
        const QByteArray bytes = patch.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            dialog->setWindowTitle(tr("Could not save SVN patch: %1").arg(file.errorString()));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->resize(800, 520);
    dialog->show();
}

} // namespace vedit::workspace::vcs
