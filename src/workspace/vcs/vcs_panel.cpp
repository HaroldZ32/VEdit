#include "vcs_panel.h"

#include "vcs_capabilities.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace vedit::workspace::vcs {

namespace {

// Every user-visible string in this lane uses the "VEdit" translation context,
// the same one the rest of the workspace lanes use (and the one
// translations/vedit_zh_CN.ts already carries).
QString t(const char *text)
{
    return QCoreApplication::translate("VEdit", text);
}

QLabel *makeTitle(const QString &text, QWidget *parent, const char *objectName)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// User-facing name of the operation in progress.
QString operationDisplayName(GitOperation operation)
{
    switch (operation) {
    case GitOperation::Merge:
        return QCoreApplication::translate("VEdit", "Merge");
    case GitOperation::Rebase:
        return QCoreApplication::translate("VEdit", "Rebase");
    case GitOperation::CherryPick:
        return QCoreApplication::translate("VEdit", "Cherry-pick");
    case GitOperation::Revert:
        return QCoreApplication::translate("VEdit", "Revert");
    case GitOperation::None:
        break;
    }
    return {};
}

// A ref name the panel refuses BEFORE a job exists. This is a deliberate SUBSET of
// the backend rules (crates/vd-vcs/src/write.rs still validates the rest and
// refuses without spawning): a name with a space, a leading '-' or a '..' should
// not even start a `git branch`, and the reason is shown to the user.
QString refNameProblem(const QString &name)
{
    const QString value = name.trimmed();
    if (value.isEmpty()) {
        return QCoreApplication::translate("VEdit", "the name is empty");
    }
    if (value.startsWith(QLatin1Char('-'))) {
        return QCoreApplication::translate("VEdit", "it starts with '-'");
    }
    for (const QChar character : value) {
        if (character.isSpace() || character.unicode() < 0x20 || character.unicode() == 0x7F) {
            return QCoreApplication::translate("VEdit", "it contains a space or a control character");
        }
    }
    if (value.contains(QStringLiteral(".."))) {
        return QCoreApplication::translate("VEdit", "it contains '..'");
    }
    if (value.startsWith(QLatin1Char('/')) || value.endsWith(QLatin1Char('/'))) {
        return QCoreApplication::translate("VEdit", "it starts or ends with '/'");
    }
    if (value.endsWith(QLatin1Char('.')) || value.endsWith(QStringLiteral(".lock"))) {
        return QCoreApplication::translate("VEdit", "it ends with '.' or '.lock'");
    }
    for (const QChar character : value) {
        if (QStringLiteral("~^:?*[\\").contains(character)) {
            return QCoreApplication::translate("VEdit", "it contains a character git refuses in a ref");
        }
    }
    return {};
}

// A URL the panel refuses before a job exists: no leading '-' (it would be read as
// an option) and no whitespace or control character (it would split the argument).
QString urlProblem(const QString &url)
{
    const QString value = url.trimmed();
    if (value.isEmpty()) {
        return QCoreApplication::translate("VEdit", "the URL is empty");
    }
    if (value.startsWith(QLatin1Char('-'))) {
        return QCoreApplication::translate("VEdit", "it starts with '-'");
    }
    for (const QChar character : value) {
        if (character.isSpace() || character.unicode() < 0x20 || character.unicode() == 0x7F) {
            return QCoreApplication::translate("VEdit", "it contains a space or a control character");
        }
    }
    return {};
}

} // namespace

// --- CommitDialog ---------------------------------------------------------

CommitDialog::CommitDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("vcs-commit-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Commit"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    message_ = new QPlainTextEdit(this);
    message_->setObjectName(QStringLiteral("vcs-commit-message"));
    message_->setPlaceholderText(QCoreApplication::translate("VEdit", "Commit message"));
    message_->setMinimumHeight(90);
    author_ = new QLineEdit(this);
    author_->setObjectName(QStringLiteral("vcs-commit-author"));
    author_->setPlaceholderText(QCoreApplication::translate("VEdit", "Author (optional, e.g. Name <mail@example.com>)"));
    amend_ = new QCheckBox(QCoreApplication::translate("VEdit", "Amend the previous commit"), this);
    amend_->setObjectName(QStringLiteral("vcs-commit-amend"));
    form->addRow(QCoreApplication::translate("VEdit", "Message"), message_);
    form->addRow(QCoreApplication::translate("VEdit", "Author"), author_);
    layout->addLayout(form);
    layout->addWidget(amend_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("VEdit", "Commit"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("VEdit", "Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(520, 260);
}

QString CommitDialog::message() const
{
    return message_->toPlainText();
}

QString CommitDialog::author() const
{
    return author_->text();
}

bool CommitDialog::amend() const
{
    return amend_->isChecked();
}

void CommitDialog::setFields(const QString &message, const QString &author, bool amend)
{
    message_->setPlainText(message);
    author_->setText(author);
    amend_->setChecked(amend);
}

// --- BranchDialog ---------------------------------------------------------

BranchDialog::BranchDialog(Mode mode, QWidget *parent)
    : QDialog(parent)
    , mode_(mode)
{
    const bool create = mode == Mode::Create;
    setObjectName(create ? QStringLiteral("vcs-branch-dialog") : QStringLiteral("vcs-branch-rename-dialog"));
    setWindowTitle(create ? QCoreApplication::translate("VEdit", "New branch")
                          : QCoreApplication::translate("VEdit", "Rename branch"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("vcs-branch-name"));
    name_->setPlaceholderText(QCoreApplication::translate("VEdit", "Branch name"));
    form->addRow(QCoreApplication::translate("VEdit", "Branch"), name_);
    if (create) {
        start_ = new QLineEdit(this);
        start_->setObjectName(QStringLiteral("vcs-branch-start"));
        start_->setPlaceholderText(QCoreApplication::translate("VEdit", "Start point (optional, defaults to HEAD)"));
        form->addRow(QCoreApplication::translate("VEdit", "Start point"), start_);
    }
    layout->addLayout(form);
    if (create) {
        switch_ = new QCheckBox(QCoreApplication::translate("VEdit", "Switch to the new branch"), this);
        switch_->setObjectName(QStringLiteral("vcs-branch-switch"));
        switch_->setChecked(true);
        layout->addWidget(switch_);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(create ? QCoreApplication::translate("VEdit", "Create")
                                                          : QCoreApplication::translate("VEdit", "Rename"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("VEdit", "Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, 200);
}

QString BranchDialog::name() const
{
    return name_->text().trimmed();
}

QString BranchDialog::startPoint() const
{
    return start_ != nullptr ? start_->text().trimmed() : QString();
}

bool BranchDialog::switchAfterCreate() const
{
    return switch_ != nullptr && switch_->isChecked();
}

void BranchDialog::setFields(const QString &name, const QString &startPoint, bool switchAfterCreate)
{
    name_->setText(name);
    if (start_ != nullptr) {
        start_->setText(startPoint);
    }
    if (switch_ != nullptr) {
        switch_->setChecked(switchAfterCreate);
    }
}

// --- TagDialog ------------------------------------------------------------

TagDialog::TagDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("vcs-tag-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "New tag"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("vcs-tag-name"));
    name_->setPlaceholderText(QCoreApplication::translate("VEdit", "Tag name"));
    target_ = new QLineEdit(this);
    target_->setObjectName(QStringLiteral("vcs-tag-target"));
    target_->setPlaceholderText(QCoreApplication::translate("VEdit", "Target commit (optional, defaults to HEAD)"));
    form->addRow(QCoreApplication::translate("VEdit", "Tag"), name_);
    form->addRow(QCoreApplication::translate("VEdit", "Target"), target_);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("VEdit", "Create"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("VEdit", "Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, 180);
}

QString TagDialog::name() const
{
    return name_->text().trimmed();
}

QString TagDialog::target() const
{
    return target_->text().trimmed();
}

void TagDialog::setFields(const QString &name, const QString &target)
{
    name_->setText(name);
    target_->setText(target);
}

// --- RemoteDialog ---------------------------------------------------------

RemoteDialog::RemoteDialog(Mode mode, QWidget *parent)
    : QDialog(parent)
    , mode_(mode)
{
    const bool add = mode == Mode::Add;
    setObjectName(add ? QStringLiteral("vcs-remote-dialog") : QStringLiteral("vcs-remote-url-dialog"));
    setWindowTitle(add ? QCoreApplication::translate("VEdit", "Add remote")
                       : QCoreApplication::translate("VEdit", "Change remote URL"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("vcs-remote-name"));
    name_->setPlaceholderText(QStringLiteral("origin"));
    name_->setEnabled(add);
    url_ = new QLineEdit(this);
    url_->setObjectName(QStringLiteral("vcs-remote-url"));
    url_->setPlaceholderText(QStringLiteral("https://example.com/repo.git"));
    form->addRow(QCoreApplication::translate("VEdit", "Remote"), name_);
    form->addRow(QCoreApplication::translate("VEdit", "URL"), url_);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(add ? QCoreApplication::translate("VEdit", "Add")
                                                        : QCoreApplication::translate("VEdit", "Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("VEdit", "Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(480, 180);
}

QString RemoteDialog::name() const
{
    return name_->text().trimmed();
}

QString RemoteDialog::url() const
{
    return url_->text().trimmed();
}

void RemoteDialog::setFields(const QString &name, const QString &url)
{
    name_->setText(name);
    url_->setText(url);
}

// --- CredentialDialog -----------------------------------------------------

CredentialDialog::CredentialDialog(const QString &prompt, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("vcs-credential-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Credentials required"));
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(prompt, this);
    label->setObjectName(QStringLiteral("vcs-credential-prompt"));
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *form = new QFormLayout();
    user_ = new QLineEdit(this);
    user_->setObjectName(QStringLiteral("vcs-credential-user"));
    user_->setPlaceholderText(QCoreApplication::translate("VEdit", "User name"));
    password_ = new QLineEdit(this);
    password_->setObjectName(QStringLiteral("vcs-credential-password"));
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QCoreApplication::translate("VEdit", "Password or token"));
    form->addRow(QCoreApplication::translate("VEdit", "User"), user_);
    form->addRow(QCoreApplication::translate("VEdit", "Password"), password_);
    layout->addLayout(form);
    auto *note = new QLabel(
        QCoreApplication::translate("VEdit", "The credentials are kept in memory for this operation only. "
                                             "They are not written to disk, not stored in the repository "
                                             "configuration and not written to any log."),
        this);
    note->setObjectName(QStringLiteral("vcs-credential-note"));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("VEdit", "Retry"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("VEdit", "Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(520, 260);
}

QString CredentialDialog::userName() const
{
    return user_->text();
}

QString CredentialDialog::password() const
{
    return password_->text();
}

void CredentialDialog::setFields(const QString &userName, const QString &password)
{
    user_->setText(userName);
    password_->setText(password);
}

// --- WorkspaceDialog (batch 5b) ------------------------------------------

WorkspaceDialog::WorkspaceDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("vcs-workspace-dialog"));
    setWindowTitle(QCoreApplication::translate("VEdit", "Git workspace"));
    auto *layout = new QVBoxLayout(this);

    layout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Stashes"), this,
                                "vcs-workspace-stashes-title"));
    stashes_ = new QListWidget(this);
    stashes_->setObjectName(QStringLiteral("vcs-workspace-stashes"));
    stashes_->setSelectionMode(QAbstractItemView::SingleSelection);
    stashes_->setMaximumHeight(170);
    layout->addWidget(stashes_, 1);
    auto *stashForm = new QFormLayout();
    stash_message_ = new QLineEdit(this);
    stash_message_->setObjectName(QStringLiteral("vcs-workspace-stash-message"));
    stash_message_->setPlaceholderText(
        QCoreApplication::translate("VEdit", "Message (optional)"));
    stashForm->addRow(QCoreApplication::translate("VEdit", "Stash message"), stash_message_);
    layout->addLayout(stashForm);
    auto *stashButtons = new QHBoxLayout();
    const auto makeActionButton = [this, stashButtons](const QString &text, const QString &name,
                                                       Action action) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(name);
        stashButtons->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, action]() { choose(action); });
        return button;
    };
    makeActionButton(QCoreApplication::translate("VEdit", "Save stash"),
                     QStringLiteral("vcs-workspace-stash-save"), Action::StashSave);
    makeActionButton(QCoreApplication::translate("VEdit", "Pop"),
                     QStringLiteral("vcs-workspace-stash-pop"), Action::StashPop);
    makeActionButton(QCoreApplication::translate("VEdit", "Apply"),
                     QStringLiteral("vcs-workspace-stash-apply"), Action::StashApply);
    makeActionButton(QCoreApplication::translate("VEdit", "Drop"),
                     QStringLiteral("vcs-workspace-stash-drop"), Action::StashDrop);
    stashButtons->addStretch(1);
    layout->addLayout(stashButtons);

    layout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Worktrees"), this,
                                "vcs-workspace-worktrees-title"));
    worktrees_ = new QListWidget(this);
    worktrees_->setObjectName(QStringLiteral("vcs-workspace-worktrees"));
    worktrees_->setSelectionMode(QAbstractItemView::SingleSelection);
    worktrees_->setMaximumHeight(170);
    layout->addWidget(worktrees_, 1);
    auto *worktreeForm = new QFormLayout();
    worktree_path_ = new QLineEdit(this);
    worktree_path_->setObjectName(QStringLiteral("vcs-workspace-worktree-path"));
    worktree_path_->setPlaceholderText(
        QCoreApplication::translate("VEdit", "Path (absolute, or beside the repository)"));
    worktree_branch_ = new QLineEdit(this);
    worktree_branch_->setObjectName(QStringLiteral("vcs-workspace-worktree-branch"));
    worktree_branch_->setPlaceholderText(
        QCoreApplication::translate("VEdit", "Existing branch (optional, defaults to the current commit)"));
    worktreeForm->addRow(QCoreApplication::translate("VEdit", "Worktree path"), worktree_path_);
    worktreeForm->addRow(QCoreApplication::translate("VEdit", "Branch"), worktree_branch_);
    layout->addLayout(worktreeForm);
    auto *worktreeButtons = new QHBoxLayout();
    auto *addWorktree = new QPushButton(QCoreApplication::translate("VEdit", "Add worktree"), this);
    addWorktree->setObjectName(QStringLiteral("vcs-workspace-worktree-add"));
    connect(addWorktree, &QPushButton::clicked, this, [this]() { choose(Action::WorktreeAdd); });
    worktreeButtons->addWidget(addWorktree);
    auto *removeWorktree = new QPushButton(QCoreApplication::translate("VEdit", "Remove worktree"),
                                           this);
    removeWorktree->setObjectName(QStringLiteral("vcs-workspace-worktree-remove"));
    connect(removeWorktree, &QPushButton::clicked, this,
            [this]() { choose(Action::WorktreeRemove); });
    worktreeButtons->addWidget(removeWorktree);
    worktreeButtons->addStretch(1);
    layout->addLayout(worktreeButtons);

    layout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Submodules"), this,
                                "vcs-workspace-submodules-title"));
    auto *submoduleButtons = new QHBoxLayout();
    auto *submoduleInitButton =
        new QPushButton(QCoreApplication::translate("VEdit", "Initialize submodules"), this);
    submoduleInitButton->setObjectName(QStringLiteral("vcs-workspace-submodule-init"));
    connect(submoduleInitButton, &QPushButton::clicked, this,
            [this]() { choose(Action::SubmoduleInit); });
    submoduleButtons->addWidget(submoduleInitButton);
    auto *submoduleUpdateButton =
        new QPushButton(QCoreApplication::translate("VEdit", "Update submodules"), this);
    submoduleUpdateButton->setObjectName(QStringLiteral("vcs-workspace-submodule-update"));
    connect(submoduleUpdateButton, &QPushButton::clicked, this,
            [this]() { choose(Action::SubmoduleUpdate); });
    submoduleButtons->addWidget(submoduleUpdateButton);
    submoduleButtons->addStretch(1);
    layout->addLayout(submoduleButtons);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("vcs-workspace-status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    // The dialog is a FORM (the two lists plus the action buttons); a trailing
    // stretch keeps the four groups at the top instead of spreading them over a
    // tall window.
    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(
        QCoreApplication::translate("VEdit", "Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(720, 640);
}

void WorkspaceDialog::choose(Action action)
{
    action_ = action;
    accept();
}

QString WorkspaceDialog::stashMessage() const
{
    return stash_message_->text().trimmed();
}

QString WorkspaceDialog::worktreePath() const
{
    return worktree_path_->text().trimmed();
}

QString WorkspaceDialog::worktreeBranch() const
{
    return worktree_branch_->text().trimmed();
}

QString WorkspaceDialog::selectedStash() const
{
    const QListWidgetItem *item = stashes_->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

QString WorkspaceDialog::selectedWorktree() const
{
    const QListWidgetItem *item = worktrees_->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

void WorkspaceDialog::setFields(const QString &stashMessage, const QString &worktreePath,
                                const QString &worktreeBranch)
{
    stash_message_->setText(stashMessage);
    worktree_path_->setText(worktreePath);
    worktree_branch_->setText(worktreeBranch);
}

void WorkspaceDialog::setStashes(const std::vector<WorkspaceEntry> &stashes)
{
    stashes_->clear();
    for (const WorkspaceEntry &entry : stashes) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2")
                .arg(QString::fromStdString(entry.refName),
                     QString::fromStdString(entry.subject)),
            stashes_);
        item->setData(Qt::UserRole, QString::fromStdString(entry.refName));
    }
    if (stashes_->count() > 0) {
        stashes_->setCurrentRow(0);
    }
}

void WorkspaceDialog::setWorktrees(const std::vector<WorkspaceEntry> &worktrees)
{
    worktrees_->clear();
    for (const WorkspaceEntry &entry : worktrees) {
        QString row = QString::fromStdString(entry.path);
        if (!entry.branch.empty()) {
            row += QStringLiteral("  [%1]").arg(QString::fromStdString(entry.branch));
        } else if (entry.detached) {
            row += QStringLiteral("  (%1)").arg(QCoreApplication::translate("VEdit", "detached"));
        }
        if (entry.mainWorktree) {
            row += QStringLiteral("  (%1)").arg(QCoreApplication::translate("VEdit", "main"));
        }
        auto *item = new QListWidgetItem(row, worktrees_);
        item->setData(Qt::UserRole, QString::fromStdString(entry.path));
    }
    if (worktrees_->count() > 0) {
        worktrees_->setCurrentRow(0);
    }
}

void WorkspaceDialog::setStatusText(const QString &text)
{
    status_->setText(text);
}

// --- VcsPanel -------------------------------------------------------------

VcsPanel::VcsPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    connect(coalesce_, &QTimer::timeout, this, [this]() { onRefreshTimer(); });

    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // A save implemented with a rename (QSaveFile and friends) replaces the
        // file, which silently drops the watch: re-add it so the NEXT save is
        // still noticed.
        if (!watcher_->files().contains(path) && QFileInfo::exists(path)) {
            watcher_->addPath(path);
        }
        noteFileSaved(path);
    });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        scheduleRefresh(QStringLiteral("filesystem"));
    });

    // AC-3.5: coming back to the window refreshes the panel. The application
    // level signal fires when the whole app regains focus; eventFilter() covers
    // a single window regaining activation.
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                noteActivated();
            }
        });
    }

    // A panel that is created as a top-level window (probes, standalone use)
    // filters activation itself; showEvent() re-targets the filter once a dock
    // reparents it.
    filtered_window_ = window();
    if (filtered_window_ != nullptr) {
        filtered_window_->installEventFilter(this);
    }

    showEmptyState(Status{});
}

VcsPanel::~VcsPanel() = default;

void VcsPanel::buildUi()
{
    setObjectName(QStringLiteral("vcs-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    // The state labels live in their own top-anchored box: when the columns are
    // hidden (empty / loading / error) the trailing stretch keeps the text at the
    // top instead of letting a QLabel absorb the whole panel height and centre it.
    auto *stateBox = new QWidget(this);
    stateBox->setObjectName(QStringLiteral("vcs-state"));
    auto *stateLayout = new QVBoxLayout(stateBox);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(4);

    header_ = new QLabel(stateBox);
    header_->setObjectName(QStringLiteral("vcs-header"));
    QFont headerFont = header_->font();
    headerFont.setBold(true);
    header_->setFont(headerFont);
    stateLayout->addWidget(header_);

    loading_ = new QLabel(QCoreApplication::translate("VEdit", "Loading…"), stateBox);
    loading_->setObjectName(QStringLiteral("vcs-loading"));
    loading_->setVisible(false);
    stateLayout->addWidget(loading_);

    empty_ = new QLabel(stateBox);
    empty_->setObjectName(QStringLiteral("vcs-empty-state"));
    empty_->setWordWrap(true);
    stateLayout->addWidget(empty_);

    error_ = new QLabel(stateBox);
    error_->setObjectName(QStringLiteral("vcs-error"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #b00020;"));
    error_->setVisible(false);
    stateLayout->addWidget(error_);
    stateLayout->addStretch(1);
    layout->addWidget(stateBox);

    // Batch 5a: the conflict banner sits above the columns and is only visible
    // while a merge/rebase/cherry-pick/revert is in progress.
    conflict_banner_ = new QLabel(this);
    conflict_banner_->setObjectName(QStringLiteral("vcs-conflict-banner"));
    conflict_banner_->setWordWrap(true);
    conflict_banner_->setStyleSheet(
        QStringLiteral("background: #fff3cd; color: #664d03; padding: 4px; border: 1px solid #ffe69c;"));
    conflict_banner_->setVisible(false);
    layout->addWidget(conflict_banner_);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setObjectName(QStringLiteral("vcs-columns"));
    splitter_->setChildrenCollapsible(false);

    // --- left column: branches / tags --------------------------------------
    auto *refsColumn = new QWidget(splitter_);
    refsColumn->setObjectName(QStringLiteral("vcs-column-refs"));
    auto *refsLayout = new QVBoxLayout(refsColumn);
    refsLayout->setContentsMargins(0, 0, 0, 0);
    refsLayout->setSpacing(2);
    refsLayout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Branches and tags"), refsColumn, "vcs-refs-title"));
    refs_ = new QTreeWidget(refsColumn);
    refs_->setObjectName(QStringLiteral("vcs-refs"));
    refs_->setHeaderHidden(true);
    refs_->setRootIsDecorated(true);
    refsLayout->addWidget(refs_, 1);
    splitter_->addWidget(refsColumn);

    // --- middle column: changes / staged -----------------------------------
    auto *changesColumn = new QWidget(splitter_);
    changesColumn->setObjectName(QStringLiteral("vcs-column-changes"));
    auto *changesLayout = new QVBoxLayout(changesColumn);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changesLayout->setSpacing(2);
    changesLayout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Changes"), changesColumn, "vcs-changes-title"));
    changes_ = new QListWidget(changesColumn);
    changes_->setObjectName(QStringLiteral("vcs-changes"));
    changes_->setSelectionMode(QAbstractItemView::SingleSelection);
    changesLayout->addWidget(changes_, 3);
    staged_title_ = makeTitle(QCoreApplication::translate("VEdit", "Staged"), changesColumn, "vcs-staged-title");
    changesLayout->addWidget(staged_title_);
    staged_ = new QListWidget(changesColumn);
    staged_->setObjectName(QStringLiteral("vcs-staged"));
    staged_->setSelectionMode(QAbstractItemView::SingleSelection);
    changesLayout->addWidget(staged_, 2);
    splitter_->addWidget(changesColumn);

    // --- right column: diff ------------------------------------------------
    auto *diffColumn = new QWidget(splitter_);
    diffColumn->setObjectName(QStringLiteral("vcs-column-diff"));
    auto *diffLayout = new QVBoxLayout(diffColumn);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->setSpacing(2);
    diffLayout->addWidget(makeTitle(QCoreApplication::translate("VEdit", "Diff"), diffColumn, "vcs-diff-title"));
    diff_ = new QPlainTextEdit(diffColumn);
    diff_->setObjectName(QStringLiteral("vcs-diff"));
    diff_->setReadOnly(true);
    diff_->setFont(QFont(QStringLiteral("monospace")));
    diffLayout->addWidget(diff_, 1);
    splitter_->addWidget(diffColumn);

    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 2);
    splitter_->setStretchFactor(2, 3);
    layout->addWidget(splitter_, 1);

    // --- operation bar -----------------------------------------------------
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("vcs-message"));
    message_->setWordWrap(true);
    message_->setVisible(false);
    layout->addWidget(message_);

    // AC-6.3: the capability summary names what this backend can do and — for a
    // disabled action — says to hover it for the real reason.
    capability_line_ = new QLabel(this);
    capability_line_->setObjectName(QStringLiteral("vcs-capability-line"));
    capability_line_->setWordWrap(true);
    capability_line_->setVisible(false);
    layout->addWidget(capability_line_);

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    refresh_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Refresh"), this);
    refresh_button_->setObjectName(QStringLiteral("vcs-action-refresh"));
    refresh_button_->setToolTip(QCoreApplication::translate("VEdit", "Re-read the working tree now"));
    stage_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Stage"), this);
    stage_button_->setObjectName(QStringLiteral("vcs-action-stage"));
    stage_button_->setToolTip(QCoreApplication::translate("VEdit", "Stage the selected file"));
    unstage_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Unstage"), this);
    unstage_button_->setObjectName(QStringLiteral("vcs-action-unstage"));
    unstage_button_->setToolTip(QCoreApplication::translate("VEdit", "Unstage the selected file"));
    discard_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Discard…"), this);
    discard_button_->setObjectName(QStringLiteral("vcs-action-discard"));
    discard_button_->setToolTip(QCoreApplication::translate("VEdit", "Throw the selected file's working-tree changes away"));
    commit_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Commit…"), this);
    commit_button_->setObjectName(QStringLiteral("vcs-action-commit"));
    commit_button_->setToolTip(QCoreApplication::translate("VEdit", "Commit the staged changes"));
    // Batch 6b: the Subversion-only Remove action. It is visible for both
    // backends and disabled (with Git's real reason) where it does not apply.
    remove_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Remove…"), this);
    remove_button_->setObjectName(QStringLiteral("vcs-action-remove"));
    remove_button_->setToolTip(QCoreApplication::translate("VEdit", "Remove the selected file from the working copy"));
    actions->addWidget(refresh_button_);
    actions->addWidget(stage_button_);
    actions->addWidget(unstage_button_);
    actions->addWidget(discard_button_);
    actions->addWidget(commit_button_);
    actions->addWidget(remove_button_);
    actions->addStretch(1);
    layout->addLayout(actions);

    // Batch 4a: remote synchronisation and ref operations get their own row so
    // the file operations above keep the same shape (and the same probes).
    auto *remoteActions = new QHBoxLayout();
    remoteActions->setContentsMargins(0, 0, 0, 0);
    fetch_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Fetch"), this);
    fetch_button_->setObjectName(QStringLiteral("vcs-action-fetch"));
    fetch_button_->setToolTip(QCoreApplication::translate("VEdit", "Fetch from the configured remote"));
    pull_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Pull"), this);
    pull_button_->setObjectName(QStringLiteral("vcs-action-pull"));
    pull_button_->setToolTip(QCoreApplication::translate("VEdit", "Fast-forward the current branch from its upstream"));
    push_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Push"), this);
    push_button_->setObjectName(QStringLiteral("vcs-action-push"));
    push_button_->setToolTip(QCoreApplication::translate("VEdit", "Push the current branch (tracks it on the first push)"));
    branch_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Branch…"), this);
    branch_button_->setObjectName(QStringLiteral("vcs-action-branch"));
    branch_button_->setToolTip(QCoreApplication::translate("VEdit", "Create a branch"));
    tag_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Tag…"), this);
    tag_button_->setObjectName(QStringLiteral("vcs-action-tag"));
    tag_button_->setToolTip(QCoreApplication::translate("VEdit", "Create a tag"));
    remote_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Remote…"), this);
    remote_button_->setObjectName(QStringLiteral("vcs-action-remote"));
    remote_button_->setToolTip(QCoreApplication::translate("VEdit", "Add a remote"));
    // Batch 5b: the stash / worktree / submodule entry point.
    workspace_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Workspace…"), this);
    workspace_button_->setObjectName(QStringLiteral("vcs-action-workspace"));
    workspace_button_->setToolTip(QCoreApplication::translate(
        "VEdit", "Stashes, linked worktrees and submodules"));
    remoteActions->addWidget(fetch_button_);
    remoteActions->addWidget(pull_button_);
    remoteActions->addWidget(push_button_);
    remoteActions->addWidget(branch_button_);
    remoteActions->addWidget(tag_button_);
    continue_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Continue"), this);
    continue_button_->setObjectName(QStringLiteral("vcs-action-continue"));
    continue_button_->setToolTip(
        QCoreApplication::translate("VEdit", "Continue the operation in progress once the resolution is staged"));
    abort_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Abort"), this);
    abort_button_->setObjectName(QStringLiteral("vcs-action-abort"));
    abort_button_->setToolTip(QCoreApplication::translate("VEdit", "Abort the operation in progress and restore the branch"));
    remoteActions->addWidget(remote_button_);
    remoteActions->addWidget(workspace_button_);
    remoteActions->addWidget(continue_button_);
    remoteActions->addWidget(abort_button_);
    remoteActions->addStretch(1);
    layout->addLayout(remoteActions);

    connect(refresh_button_, &QPushButton::clicked, this, [this]() { manualRefresh(); });
    connect(stage_button_, &QPushButton::clicked, this, [this]() { stageSelected(); });
    connect(unstage_button_, &QPushButton::clicked, this, [this]() { unstageSelected(); });
    connect(discard_button_, &QPushButton::clicked, this, [this]() { discardSelected(); });
    connect(commit_button_, &QPushButton::clicked, this, [this]() { promptCommit(); });
    connect(fetch_button_, &QPushButton::clicked, this, [this]() { fetch(); });
    connect(pull_button_, &QPushButton::clicked, this, [this]() { pull(); });
    connect(push_button_, &QPushButton::clicked, this, [this]() { push(); });
    connect(branch_button_, &QPushButton::clicked, this, [this]() { promptBranch(); });
    connect(tag_button_, &QPushButton::clicked, this, [this]() { promptTag(); });
    connect(remote_button_, &QPushButton::clicked, this, [this]() { promptRemote(); });
    connect(workspace_button_, &QPushButton::clicked, this, [this]() { promptWorkspace(); });
    connect(remove_button_, &QPushButton::clicked, this, [this]() { removeSelected(); });
    // Remember the built-in hints before any capability reason replaces them.
    for (QPushButton *button : {refresh_button_, stage_button_, unstage_button_, discard_button_,
                                commit_button_, remove_button_, fetch_button_, pull_button_,
                                push_button_, branch_button_, tag_button_, remote_button_,
                                workspace_button_, continue_button_, abort_button_}) {
        rememberToolTip(button);
    }
    connect(continue_button_, &QPushButton::clicked, this, [this]() { continueOperation(); });
    connect(abort_button_, &QPushButton::clicked, this, [this]() { abortOperation(); });

    // The refs tree is the interactive surface for switch / checkout / delete /
    // rename / tracking; the panel methods behind the context menu are the same
    // ones the probe drives.
    refs_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(refs_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showRefContextMenu(position); });
    connect(refs_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item == nullptr) {
            return;
        }
        const QString name = item->data(0, Qt::UserRole).toString();
        const auto kind = static_cast<RefKind>(item->data(0, Qt::UserRole + 1).toInt());
        if (name.isEmpty()) {
            return;
        }
        if (kind == RefKind::Branch) {
            switchBranch(name);
        } else {
            checkoutRef(name);
        }
    });

    const auto onCurrent = [this](QListWidgetItem *current, QListWidgetItem *) {
        if (current == nullptr) {
            return;
        }
        selected_path_ = current->data(Qt::UserRole).toString();
        updateActions();
        showDiff(selected_path_.toStdString());
    };
    connect(changes_, &QListWidget::currentItemChanged, this,
            [onCurrent](QListWidgetItem *current, QListWidgetItem *previous) {
                onCurrent(current, previous);
            });
    connect(staged_, &QListWidget::currentItemChanged, this,
            [onCurrent](QListWidgetItem *current, QListWidgetItem *previous) {
                onCurrent(current, previous);
            });

    updateActions();
}

void VcsPanel::setSource(std::unique_ptr<VcsSource> source)
{
    source_ = std::move(source);
    refresh();
}

void VcsPanel::setRoot(const std::string &root)
{
    if (root_ == root) {
        return;
    }
    root_ = root;
    // A different working copy has its own index: the session-local staging
    // overlay must not leak across it.
    staged_overlay_.clear();
    selected_path_.clear();
    backend_ = Backend::None;
    info_snapshot_ = Info{};
    // The two workspace listings belong to the previous working copy too.
    stashes_snapshot_ = WorkspaceData{};
    stashes_snapshot_.kind = WorkspaceKind::Stashes;
    worktrees_snapshot_ = WorkspaceData{};
    worktrees_snapshot_.kind = WorkspaceKind::Worktrees;
    // The refs column belongs to the previous working copy; the next read fills it
    // from the new one.
    if (refs_ != nullptr) {
        refs_->clear();
    }
    header_->setText(QString());
    if (watcher_ != nullptr) {
        const QStringList paths = watcher_->files() + watcher_->directories();
        if (!paths.isEmpty()) {
            watcher_->removePaths(paths);
        }
    }
    diff_->clear();
}

void VcsPanel::refresh()
{
    ++refresh_count_;
    if (!source_) {
        loading_->setVisible(false);
        showEmptyState(Status{});
        return;
    }
    // T99: returns immediately; the result arrives in applyStatus() (the stub
    // source calls it synchronously, the FFI source from its QTimer poll).
    loading_->setVisible(true);
    empty_->setVisible(false);
    error_->setVisible(false);
    if (backend_ == Backend::None) {
        // Nothing is known about the working copy yet, so claiming "No
        // repository" while the first read runs would be a lie.
        header_->setText(QString());
    }
    source_->requestStatus(root_, [this](Status status) { applyStatus(std::move(status)); });
}

void VcsPanel::applyStatus(Status status)
{
    loading_->setVisible(false);
    if (status.error) {
        showErrorState(status);
        return;
    }
    if (status.backend == Backend::None) {
        showEmptyState(status);
        return;
    }

    backend_ = status.backend;
    empty_->setVisible(false);
    error_->setVisible(false);
    splitter_->setVisible(true);

    changes_snapshot_ = status.changes;
    {
        QSet<QString> present;
        present.reserve(static_cast<int>(changes_snapshot_.size()));
        for (const Change &change : changes_snapshot_) {
            present.insert(QString::fromStdString(change.path));
        }
        staged_overlay_.intersect(present);
    }
    renderChanges();
    renderConflictBanner();
    // Batch 4a: the refs column is driven by the info read that follows; until
    // it answers, keep the last snapshot rather than blanking the column.
    renderRefs(info_snapshot_);
    watchPaths(status);
    if (source_) {
        source_->requestInfo(root_, [this](Info info) { applyInfo(std::move(info)); });
    }
    updateActions();
}

void VcsPanel::applyInfo(Info info)
{
    if (info.backend != Backend::None) {
        backend_ = info.backend;
    }
    info_snapshot_ = info;
    QStringList parts;
    parts << backendName(backend_);
    if (!info.branch.empty()) {
        parts << QString::fromStdString(info.branch);
    }
    if (!info.upstream.empty()) {
        parts << QStringLiteral("→ ") + QString::fromStdString(info.upstream);
    }
    if (!info.revision.empty()) {
        parts << QStringLiteral("@ ") + QString::fromStdString(info.revision);
    }
    parts << QCoreApplication::translate("VEdit", "· %1 changed file(s)").arg(fileCount());
    header_->setText(parts.join(QStringLiteral(" ")));
    renderRefs(info);
    renderConflictBanner();
    updateActions();
}

void VcsPanel::renderChanges()
{
    const QString keep = selected_path_;
    {
        // Rebuilding the rows must not fire a diff request per row; the diff for
        // the kept selection is requested once, below.
        const QSignalBlocker blockChanges(changes_);
        const QSignalBlocker blockStaged(staged_);
        changes_->clear();
        staged_->clear();
        for (const Change &change : changes_snapshot_) {
            const QString path = QString::fromStdString(change.path);
            auto *item = new QListWidgetItem(
                QStringLiteral("%1  %2").arg(path, changeKindName(change.kind)));
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            if (staged_overlay_.contains(path)) {
                staged_->addItem(item);
            } else {
                changes_->addItem(item);
            }
        }
        bool restored = keep.isEmpty();
        if (!keep.isEmpty()) {
            for (QListWidget *list : {changes_, staged_}) {
                for (int row = 0; row < list->count(); ++row) {
                    if (list->item(row)->data(Qt::UserRole).toString() == keep) {
                        list->setCurrentRow(row);
                        restored = true;
                        break;
                    }
                }
                if (restored) {
                    break;
                }
            }
        }
        if (!restored) {
            selected_path_.clear();
            diff_->clear();
        }
    }
    if (!selected_path_.isEmpty()) {
        showDiff(selected_path_.toStdString());
    } else if (!keep.isEmpty()) {
        // The selected file left the change set (committed or discarded).
        diff_->clear();
    }
    updateActions();
}

void VcsPanel::renderRefs(const Info &info)
{
    refs_->clear();

    // Batch 4a: the read ABI reports the real listing, so each section is filled
    // from the entry kinds instead of a single branch string. A backend that
    // reports no branch list at all still shows the current branch, and every
    // empty section says so explicitly.
    const auto section = [this](const QString &title, const char *objectName) {
        auto *top = new QTreeWidgetItem(refs_);
        top->setText(0, title);
        top->setFlags(Qt::ItemIsEnabled);
        top->setData(0, Qt::UserRole, QString::fromLatin1(objectName));
        top->setExpanded(true);
        return top;
    };
    const auto leaf = [](QTreeWidgetItem *parent, const RefEntry *ref, const QString &text) {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, text);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (ref != nullptr) {
            item->setData(0, Qt::UserRole, QString::fromStdString(ref->name));
            item->setData(0, Qt::UserRole + 1, static_cast<int>(ref->kind));
            item->setToolTip(0, ref->target.empty() ? QString::fromStdString(ref->name)
                                                    : QStringLiteral("%1 · %2")
                                                          .arg(QString::fromStdString(ref->name),
                                                               QString::fromStdString(ref->target)));
        } else {
            item->setFlags(Qt::ItemIsEnabled);
        }
        return item;
    };

    // --- local branches ---------------------------------------------------
    auto *branches = section(QCoreApplication::translate("VEdit", "Branches"), "branches");
    int branchCount = 0;
    for (const RefEntry &ref : info.refs) {
        if (ref.kind != RefKind::Branch) {
            continue;
        }
        ++branchCount;
        const QString text = ref.current
            ? QCoreApplication::translate("VEdit", "* %1").arg(QString::fromStdString(ref.name))
            : QString::fromStdString(ref.name);
        leaf(branches, &ref, text);
    }
    if (branchCount == 0) {
        // A backend that only reports the current branch name (the batch-3a form)
        // must still show it instead of an empty section.
        const QString name = QString::fromStdString(info.branch);
        leaf(branches, nullptr,
             name.isEmpty() ? QCoreApplication::translate("VEdit", "no branch reported")
                            : QCoreApplication::translate("VEdit", "* %1").arg(name));
    }

    // --- tags -------------------------------------------------------------
    auto *tags = section(QCoreApplication::translate("VEdit", "Tags"), "tags");
    int tagCount = 0;
    for (const RefEntry &ref : info.refs) {
        if (ref.kind != RefKind::Tag) {
            continue;
        }
        ++tagCount;
        leaf(tags, &ref, QString::fromStdString(ref.name));
    }
    if (tagCount == 0) {
        leaf(tags, nullptr, QCoreApplication::translate("VEdit", "No tags"));
    }

    // --- remote-tracking branches ----------------------------------------
    auto *tracking = section(QCoreApplication::translate("VEdit", "Remote branches"), "remote-branches");
    int trackingCount = 0;
    for (const RefEntry &ref : info.refs) {
        if (ref.kind != RefKind::RemoteBranch) {
            continue;
        }
        ++trackingCount;
        leaf(tracking, &ref, QString::fromStdString(ref.name));
    }
    if (trackingCount == 0) {
        leaf(tracking, nullptr, QCoreApplication::translate("VEdit", "No remote branches"));
    }

    // --- configured remotes ----------------------------------------------
    auto *remotes = section(QCoreApplication::translate("VEdit", "Remotes"), "remotes");
    for (const RemoteEntry &remote : info.remotes) {
        auto *item = leaf(remotes, nullptr,
                          QStringLiteral("%1  %2").arg(QString::fromStdString(remote.name),
                                                      sanitizeUrl(QString::fromStdString(remote.url))));
        item->setData(0, Qt::UserRole, QString::fromStdString(remote.name));
        item->setData(0, Qt::UserRole + 1, static_cast<int>(RefKind::Other));
        // A configured remote is not a ref: the marker keeps the context menu and
        // the selection helpers from treating it as one.
        item->setData(0, Qt::UserRole + 2, QStringLiteral("remote"));
    }
    if (info.remotes.empty()) {
        leaf(remotes, nullptr, QCoreApplication::translate("VEdit", "No remotes configured"));
    }
}

bool VcsPanel::selectRef(const QString &name)
{
    for (int top = 0; top < refs_->topLevelItemCount(); ++top) {
        QTreeWidgetItem *sectionItem = refs_->topLevelItem(top);
        for (int child = 0; child < sectionItem->childCount(); ++child) {
            QTreeWidgetItem *item = sectionItem->child(child);
            if (item->data(0, Qt::UserRole).toString() == name
                && item->data(0, Qt::UserRole + 2).toString().isEmpty()) {
                refs_->setCurrentItem(item);
                return true;
            }
        }
    }
    return false;
}

QString VcsPanel::selectedRefName() const
{
    const QTreeWidgetItem *item = refs_->currentItem();
    if (item == nullptr || !item->data(0, Qt::UserRole + 2).toString().isEmpty()) {
        return {};
    }
    return item->data(0, Qt::UserRole).toString();
}

RefKind VcsPanel::selectedRefKind() const
{
    const QTreeWidgetItem *item = refs_->currentItem();
    if (item == nullptr) {
        return RefKind::Other;
    }
    return static_cast<RefKind>(item->data(0, Qt::UserRole + 1).toInt());
}

void VcsPanel::watchPaths(const Status &status)
{
    if (watcher_ == nullptr || root_.empty()) {
        return;
    }
    QStringList wanted;
    const QDir root(QString::fromStdString(root_));
    wanted << root.absolutePath();
    int considered = 0;
    for (const Change &change : status.changes) {
        // Bounded watch count: a status with thousands of files must not exhaust
        // the inotify descriptors of the desktop session.
        if (++considered > 200) {
            break;
        }
        const QFileInfo info(root.filePath(QString::fromStdString(change.path)));
        if (info.exists()) {
            wanted << info.absoluteFilePath();
        }
        wanted << info.absolutePath();
    }
    wanted.removeDuplicates();

    QStringList current = watcher_->files();
    current += watcher_->directories();
    for (const QString &path : current) {
        if (!wanted.contains(path)) {
            watcher_->removePath(path);
        }
    }
    for (const QString &path : wanted) {
        if (!current.contains(path)) {
            watcher_->addPath(path);
        }
    }
}

void VcsPanel::showEmptyState(const Status &status)
{
    backend_ = Backend::None;
    info_snapshot_ = Info{};
    changes_snapshot_.clear();
    staged_overlay_.clear();
    selected_path_.clear();
    changes_->clear();
    staged_->clear();
    diff_->clear();
    loading_->setVisible(false);
    error_->setVisible(false);
    splitter_->setVisible(false);
    empty_->setVisible(true);
    header_->setText(backendName(Backend::None));
    const QString note = status.note.empty()
        ? QCoreApplication::translate("VEdit", "not a Git or Subversion working tree")
        : QString::fromStdString(status.note);
    empty_->setText(QCoreApplication::translate("VEdit", "%1\n\nOpen a directory that is a Git or Subversion working "
                      "tree to see its branch, changed files and diffs.")
                        .arg(note));
    renderRefs(Info{});
    if (watcher_ != nullptr) {
        const QStringList paths = watcher_->files() + watcher_->directories();
        if (!paths.isEmpty()) {
            watcher_->removePaths(paths);
        }
    }
    updateActions();
}

void VcsPanel::showErrorState(const Status &status)
{
    backend_ = status.backend;
    changes_snapshot_.clear();
    staged_overlay_.clear();
    selected_path_.clear();
    changes_->clear();
    staged_->clear();
    diff_->clear();
    loading_->setVisible(false);
    empty_->setVisible(false);
    splitter_->setVisible(false);
    const QString reason = status.note.empty() ? QCoreApplication::translate("VEdit", "the VCS backend reported a failure")
                                               : QString::fromStdString(status.note);
    error_->setText(QCoreApplication::translate("VEdit", "The VCS backend failed: %1").arg(reason));
    error_->setVisible(true);
    header_->setText(backendName(status.backend));
    updateActions();
}

void VcsPanel::showDiff(const std::string &path)
{
    if (!source_) {
        diff_->setPlainText(QCoreApplication::translate("VEdit", "No VCS backend available."));
        return;
    }
    source_->requestDiff(root_, path, [this, path](Diff diff) { applyDiff(path, std::move(diff)); });
}

void VcsPanel::applyDiff(const std::string &path, Diff diff)
{
    if (!diff.text.empty()) {
        diff_->setPlainText(QString::fromStdString(diff.text));
        return;
    }
    const std::string note = diff.note.empty() ? std::string("no diff to show") : diff.note;
    diff_->setPlainText(QStringLiteral("%1: %2")
                            .arg(QString::fromStdString(path), QString::fromStdString(note)));
}

void VcsPanel::selectFile(const std::string &path)
{
    const QString wanted = QString::fromStdString(path);
    selected_path_ = wanted;
    for (QListWidget *list : {changes_, staged_}) {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == wanted) {
                list->setCurrentRow(row); // currentItemChanged -> showDiff()
                return;
            }
        }
    }
    updateActions();
    showDiff(path);
}

void VcsPanel::rememberToolTip(QPushButton *button)
{
    if (button != nullptr) {
        default_tooltips_.insert(button->objectName(), button->toolTip());
    }
}

void VcsPanel::applyCapability(QPushButton *button, PanelAction action, bool extra)
{
    if (button == nullptr) {
        return;
    }
    const bool hasSource = source_ != nullptr;
    const bool writable = hasSource && source_->writeCapable();
    const bool idle = !write_in_flight_;
    const PanelCapability capability = capabilityFor(backend_, action);
    button->setEnabled(hasSource && writable && idle && capability.supported && extra);
    if (capability.supported) {
        const QString normal = default_tooltips_.value(button->objectName());
        if (!normal.isEmpty()) {
            button->setToolTip(normal);
        }
    } else if (!capability.reason.isEmpty()) {
        // AC-6.3: the disabled action carries the REAL reason, not a blank hint.
        button->setToolTip(capability.reason);
    }
}

void VcsPanel::updateActions()
{
    const bool hasSource = source_ != nullptr;
    const bool selection = !selected_path_.isEmpty();
    const bool pending = info_snapshot_.operation != GitOperation::None;
    const bool svn = backend_ == Backend::Subversion;

    // The pull button is Git's `Pull` and Subversion's `Update`.
    refresh_button_->setEnabled(hasSource);
    applyCapability(stage_button_, PanelAction::Add, selection);
    applyCapability(unstage_button_, PanelAction::Unstage, selection);
    applyCapability(discard_button_, PanelAction::Revert, selection);
    applyCapability(remove_button_, PanelAction::Remove, selection);
    applyCapability(commit_button_, PanelAction::Commit, true);
    applyCapability(fetch_button_, PanelAction::Fetch, true);
    applyCapability(pull_button_, svn ? PanelAction::Update : PanelAction::Pull, true);
    applyCapability(push_button_, PanelAction::Push, true);
    applyCapability(branch_button_, PanelAction::Branch, true);
    applyCapability(tag_button_, PanelAction::Tag, true);
    applyCapability(remote_button_, PanelAction::Remote, true);
    applyCapability(workspace_button_, PanelAction::Workspace, true);
    applyCapability(continue_button_, PanelAction::Continue, pending);
    applyCapability(abort_button_, PanelAction::Abort, pending);

    // The SAME three-column widget, with the backend's own verbs on the buttons.
    // These are DIRECT translate() calls (not the t() helper) because lupdate only
    // extracts literals passed straight to QCoreApplication::translate: a `t("…")`
    // label would silently stay untranslated in the compiled catalogue.
    stage_button_->setText(svn ? QCoreApplication::translate("VEdit", "Add")
                               : QCoreApplication::translate("VEdit", "Stage"));
    discard_button_->setText(svn ? QCoreApplication::translate("VEdit", "Revert…")
                                 : QCoreApplication::translate("VEdit", "Discard…"));
    pull_button_->setText(svn ? QCoreApplication::translate("VEdit", "Update")
                              : QCoreApplication::translate("VEdit", "Pull"));

    // Subversion has no index: the Staged column is meaningless there, so it is
    // hidden together with its title (not silently emptied).
    const bool showStaged = !svn;
    if (staged_title_ != nullptr) {
        staged_title_->setVisible(showStaged);
    }
    if (staged_ != nullptr) {
        staged_->setVisible(showStaged);
    }

    if (capability_line_ != nullptr) {
        capability_line_->setText(capabilitySummary(backend_));
        capability_line_->setVisible(hasSource);
    }
}

QString VcsPanel::capabilitySummaryText() const
{
    return capabilitySummary(backend_);
}

QString VcsPanel::capabilityReportText() const
{
    return capabilityReport(backend_);
}

QStringList VcsPanel::disabledActionsText() const
{
    return disabledActionReasons(backend_);
}

QString VcsPanel::actionLabel(const QString &objectName) const
{
    const auto *button = findChild<QPushButton *>(objectName);
    return button == nullptr ? QString() : button->text();
}

bool VcsPanel::actionEnabled(const QString &objectName) const
{
    const auto *button = findChild<QPushButton *>(objectName);
    return button != nullptr && button->isEnabled();
}

QString VcsPanel::actionToolTip(const QString &objectName) const
{
    const auto *button = findChild<QPushButton *>(objectName);
    return button == nullptr ? QString() : button->toolTip();
}

bool VcsPanel::stagedColumnVisible() const
{
    return staged_ != nullptr && staged_->isVisibleTo(this);
}

void VcsPanel::renderConflictBanner()
{
    const GitOperation operation = info_snapshot_.operation;
    if (operation == GitOperation::None) {
        conflict_banner_->clear();
        conflict_banner_->setVisible(false);
        return;
    }
    int conflicted = 0;
    for (const Change &change : changes_snapshot_) {
        if (change.kind == 5) {
            ++conflicted;
        }
    }
    const QString name = operationDisplayName(operation);
    conflict_banner_->setText(
        conflicted > 0
            ? QCoreApplication::translate(
                  "VEdit", "%1 in progress: %2 conflicted file(s). Resolve them, stage the result, "
                           "then Continue (or Abort).")
                  .arg(name)
                  .arg(conflicted)
            : QCoreApplication::translate(
                  "VEdit", "%1 in progress. Continue when the resolution is staged, or Abort.")
                  .arg(name));
    conflict_banner_->setVisible(true);
}

// --- writes ---------------------------------------------------------------

void VcsPanel::setConfirmProvider(ConfirmProvider provider)
{
    confirm_provider_ = std::move(provider);
}

bool VcsPanel::askConfirm(const QString &title, const QString &text)
{
    ++confirm_calls_;
    last_confirm_text_ = text;
    if (confirm_provider_) {
        return confirm_provider_(title, text);
    }
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        // Headless has nobody to ask; silently destroying data is the one answer
        // that must never be assumed, so refuse and say why (the same rule the
        // shell's Save As overwrite confirmation follows).
        message_->setText(QCoreApplication::translate("VEdit", "Confirmation is unavailable headless; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
        == QMessageBox::Yes;
}

bool VcsPanel::confirmTwice(const QString &title, const QString &text, const QString &secondTitle,
                            const QString &secondText)
{
    // AC-5.3: an irreversible operation asks twice, and a decline at either step
    // writes nothing.
    if (!askConfirm(title, text)) {
        return false;
    }
    return askConfirm(secondTitle, secondText);
}

bool VcsPanel::merge(const QString &ref)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitMerge;
    request.root = root_;
    request.target = ref.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::rebase(const QString &onto)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString target = onto.trimmed();
    if (target.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "A rebase target is required; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    // Batch 7 (user ruling): a rebase REWRITES history, so it asks once — the same
    // confirmation seam (and the same consequence-first wording) the reset path
    // uses. A decline writes nothing.
    const QString branch = info_snapshot_.branch.empty()
        ? QCoreApplication::translate("VEdit", "the current branch")
        : QString::fromStdString(info_snapshot_.branch);
    const QString preview = QCoreApplication::translate(
                                "VEdit",
                                "Rebase %1 onto %2?\n\nRebase rewrites the commits of %1: every "
                                "commit after %2 is replayed on top of it and gets a new id. A "
                                "published branch must be force-pushed afterwards. Conflicts stop "
                                "the rebase until you Continue or Abort.")
                                .arg(branch, target);
    if (!askConfirm(QCoreApplication::translate("VEdit", "Rebase the branch?"), preview)) {
        message_->setText(
            QCoreApplication::translate("VEdit", "Rebase cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitRebase;
    request.root = root_;
    request.target = target.toStdString();
    return issueWrite(request);
}

bool VcsPanel::cherryPick(const QString &commit)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitCherryPick;
    request.root = root_;
    request.target = commit.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::revertCommit(const QString &commit)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitRevert;
    request.root = root_;
    request.target = commit.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::resetTo(const QString &commit, const QString &mode)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString normalised = mode.trimmed().toLower();
    if (normalised != QStringLiteral("soft") && normalised != QStringLiteral("mixed")
        && normalised != QStringLiteral("hard")) {
        message_->setText(QCoreApplication::translate(
            "VEdit", "The reset mode must be soft, mixed or hard; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    // Preview: what the reset does, on which branch and to which commit.
    const QString preview = QCoreApplication::translate(
                                "VEdit", "Reset %1 to %2 (%3)?\n\nCommits after %2 are removed from the "
                                         "branch; %3 decides whether the index and the working tree follow.")
                                .arg(QString::fromStdString(info_snapshot_.branch), commit, normalised);
    if (normalised == QStringLiteral("hard")) {
        if (!confirmTwice(
                QCoreApplication::translate("VEdit", "Reset the branch?"), preview,
                QCoreApplication::translate("VEdit", "Discard the working tree as well?"),
                QCoreApplication::translate(
                    "VEdit", "A hard reset also throws away every uncommitted change in the working "
                             "tree. This cannot be undone from the panel. Continue?"))) {
            message_->setText(
                QCoreApplication::translate("VEdit", "Reset cancelled; nothing was written."));
            message_->setVisible(true);
            return false;
        }
    } else if (!askConfirm(QCoreApplication::translate("VEdit", "Reset the branch?"), preview)) {
        message_->setText(QCoreApplication::translate("VEdit", "Reset cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitReset;
    request.root = root_;
    request.target = commit.trimmed().toStdString();
    request.mode = normalised.toStdString();
    return issueWrite(request);
}

bool VcsPanel::cleanFiles(const QStringList &paths)
{
    if (backend_ != Backend::Git || !source_ || paths.isEmpty()) {
        return false;
    }
    if (!confirmTwice(
            QCoreApplication::translate("VEdit", "Delete untracked files?"),
            QCoreApplication::translate("VEdit", "Delete %1 untracked file(s)?\n\n%2")
                .arg(paths.size())
                .arg(paths.join(QLatin1Char('\n'))),
            QCoreApplication::translate("VEdit", "Delete them permanently?"),
            QCoreApplication::translate("VEdit", "The files are not in the repository; deleting them "
                                                 "cannot be undone from the panel. Continue?"))) {
        message_->setText(QCoreApplication::translate("VEdit", "Clean cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitClean;
    request.root = root_;
    for (const QString &path : paths) {
        request.paths.push_back(path.toStdString());
    }
    return issueWrite(request);
}

bool VcsPanel::continueOperation()
{
    if (backend_ != Backend::Git || !source_ || info_snapshot_.operation == GitOperation::None) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitContinue;
    request.root = root_;
    request.target = gitOperationToken(info_snapshot_.operation);
    return issueWrite(request);
}

bool VcsPanel::abortOperation()
{
    if (backend_ != Backend::Git || !source_ || info_snapshot_.operation == GitOperation::None) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitAbort;
    request.root = root_;
    request.target = gitOperationToken(info_snapshot_.operation);
    return issueWrite(request);
}

QString VcsPanel::conflictBannerText() const
{
    return conflict_banner_->isVisibleTo(const_cast<VcsPanel *>(this)) ? conflict_banner_->text()
                                                                      : QString();
}

bool VcsPanel::continueAvailable() const
{
    return info_snapshot_.operation != GitOperation::None && !write_in_flight_;
}

bool VcsPanel::abortAvailable() const
{
    return info_snapshot_.operation != GitOperation::None && !write_in_flight_;
}

bool VcsPanel::stageSelected()
{
    // The same button is Git's Stage and Subversion's `svn add` (AC-6.3).
    if ((backend_ != Backend::Git && backend_ != Backend::Subversion) || !source_) {
        return false;
    }
    const QString path = selected_path_;
    if (path.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Select a changed file first."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = backend_ == Backend::Subversion ? WriteOp::SvnAdd : WriteOp::GitStage;
    request.root = root_;
    request.paths.push_back(path.toStdString());
    return issueWrite(request);
}

bool VcsPanel::unstageSelected()
{
    if (backend_ == Backend::Subversion) {
        // AC-6.3: a grayed action still explains itself; the message is the same
        // real reason the tooltip carries.
        message_->setText(capabilityFor(backend_, PanelAction::Unstage).reason);
        message_->setVisible(true);
        return false;
    }
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString path = selected_path_;
    if (path.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Select a staged file first."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitUnstage;
    request.root = root_;
    request.paths.push_back(path.toStdString());
    return issueWrite(request);
}

bool VcsPanel::discardSelected()
{
    if (backend_ == Backend::Subversion) {
        // Subversion's verb for the same slot is `svn revert`; same double
        // confirmation policy, its own wording.
        if (!source_) {
            return false;
        }
        const QString path = selected_path_;
        if (path.isEmpty()) {
            message_->setText(QCoreApplication::translate("VEdit", "Select a changed file first."));
            message_->setVisible(true);
            return false;
        }
        if (!confirmTwice(
                QCoreApplication::translate("VEdit", "Revert changes?"),
                QCoreApplication::translate("VEdit", "Revert the working-copy changes of %1?\n\n"
                                                     "This cannot be undone.")
                    .arg(path),
                QCoreApplication::translate("VEdit", "Revert the changes permanently?"),
                QCoreApplication::translate("VEdit", "The working-copy changes of %1 are lost and "
                                                     "cannot be restored from the panel. Continue?")
                    .arg(path))) {
            message_->setText(QCoreApplication::translate("VEdit", "Revert cancelled; nothing was written."));
            message_->setVisible(true);
            return false;
        }
        WriteRequest request;
        request.op = WriteOp::SvnRevert;
        request.root = root_;
        request.paths.push_back(path.toStdString());
        return issueWrite(request);
    }
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString path = selected_path_;
    if (path.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Select a changed file first."));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(QCoreApplication::translate("VEdit", "Discard changes?"),
                    QCoreApplication::translate("VEdit", "Discard the working-tree changes of %1?\n\nThis cannot be undone.")
                        .arg(path),
                    QCoreApplication::translate("VEdit", "Discard the changes permanently?"),
                    QCoreApplication::translate("VEdit", "The working-tree changes of %1 are lost and "
                                                         "cannot be restored from the panel. Continue?")
                        .arg(path))) {
        message_->setText(QCoreApplication::translate("VEdit", "Discard cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitDiscard;
    request.root = root_;
    request.paths.push_back(path.toStdString());
    return issueWrite(request);
}

bool VcsPanel::commit(const QString &message, const QString &author, bool amend)
{
    if (backend_ == Backend::Subversion) {
        if (!source_) {
            return false;
        }
        if (message.trimmed().isEmpty()) {
            message_->setText(QCoreApplication::translate("VEdit", "A commit message is required."));
            message_->setVisible(true);
            return false;
        }
        if (amend) {
            message_->setText(QCoreApplication::translate(
                "VEdit", "Subversion has no amend: commit a new revision instead; nothing was written."));
            message_->setVisible(true);
            return false;
        }
        WriteRequest request;
        request.op = WriteOp::SvnCommit;
        request.root = root_;
        request.message = message.toStdString();
        // The whole working copy is committed; Subversion has no staging area to
        // limit the path list to (the Staged column is hidden for this backend).
        return issueWrite(request);
    }
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (message.trimmed().isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "A commit message is required."));
        message_->setVisible(true);
        return false;
    }
    if (amend && !askConfirm(QCoreApplication::translate("VEdit", "Amend the last commit?"),
                             QCoreApplication::translate("VEdit", "Amending rewrites the previous commit with this message "
                               "and the currently staged changes. Continue?"))) {
        message_->setText(QCoreApplication::translate("VEdit", "Amend cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitCommit;
    request.root = root_;
    request.message = message.toStdString();
    request.author = author.toStdString();
    request.amend = amend;
    return issueWrite(request);
}

bool VcsPanel::removeSelected()
{
    if (backend_ != Backend::Subversion || !source_) {
        // Git records a deletion by staging the deleted file; the button is
        // disabled there, and this guard keeps a programmatic call honest.
        if (backend_ == Backend::Git) {
            message_->setText(capabilityFor(backend_, PanelAction::Remove).reason);
            message_->setVisible(true);
        }
        return false;
    }
    const QString path = selected_path_;
    if (path.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Select a changed file first."));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(
            QCoreApplication::translate("VEdit", "Remove the file?"),
            QCoreApplication::translate(
                "VEdit", "Remove %1 from the working copy?\n\nThe file is scheduled for "
                         "deletion and deleted by the next commit; revert restores it.")
                .arg(path),
            QCoreApplication::translate("VEdit", "Remove the file permanently?"),
            QCoreApplication::translate(
                "VEdit", "The working copy deletes %1 now. The removal is only recorded in the "
                         "repository by the next commit. Continue?")
                .arg(path))) {
        message_->setText(
            QCoreApplication::translate("VEdit", "Removal cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::SvnRemove;
    request.root = root_;
    request.paths.push_back(path.toStdString());
    return issueWrite(request);
}

bool VcsPanel::issueWrite(const WriteRequest &request)
{
    return issueWriteInternal(request, false);
}

bool VcsPanel::issueWriteInternal(const WriteRequest &request, bool is_retry)
{
    if (!source_) {
        message_->setText(QCoreApplication::translate("VEdit", "No VCS backend available."));
        message_->setVisible(true);
        return false;
    }
    if (!source_->writeCapable()) {
        message_->setText(QCoreApplication::translate("VEdit", "This build has no VCS write support."));
        message_->setVisible(true);
        return false;
    }
    write_in_flight_ = true;
    retrying_with_credentials_ = is_retry;
    pending_op_ = request.op;
    pending_request_ = request;
    if (!is_retry) {
        // Never keep credentials from an earlier operation around; only the
        // explicit retry below adds them, and clearCredentials() wipes them.
        pending_request_.credential_user.clear();
        pending_request_.credential_password.clear();
    }
    pending_path_ = request.paths.empty() ? QString() : QString::fromStdString(request.paths.front());
    error_->setVisible(false);
    message_->setText(QCoreApplication::translate("VEdit", "Working…"));
    message_->setVisible(true);
    updateActions();
    // A stub answers synchronously (so write_in_flight_ is already false when
    // this returns); the production source answers from its poll timer. The
    // request WAS issued either way, which is what the callers report.
    source_->requestWrite(request, [this](WriteResult result) { applyWriteResult(std::move(result)); });
    return true;
}

void VcsPanel::clearCredentials()
{
    // Wipe the in-memory copy: the secret must not outlive the operation that
    // needed it, not even in a heap string the panel still owns.
    if (!pending_request_.credential_password.empty()) {
        std::fill(pending_request_.credential_password.begin(),
                  pending_request_.credential_password.end(), '\0');
    }
    pending_request_.credential_user.clear();
    pending_request_.credential_password.clear();
}

void VcsPanel::applyWriteResult(WriteResult result)
{
    const WriteOp op = result.op == WriteOp::None ? pending_op_ : result.op;
    last_write_op_ = op;
    last_write_ok_ = result.ok;

    // AC-4.5: a network operation that failed because the transport could not
    // obtain credentials gets ONE retry with the credentials the user enters.
    // Cancelling ends the operation with git's real reason (below), never with a
    // fabricated success and never with a second attempt.
    if (!result.ok && writeOpIsNetwork(op) && !retrying_with_credentials_
        && writeResultNeedsCredentials(result)) {
        credential_prompt_pending_ = true;
        const QString prompt = credentialPromptText(op);
        QString user;
        QString password;
        const bool accepted = askCredentials(op, prompt, user, password);
        credential_prompt_pending_ = false;
        if (accepted && !user.isEmpty() && !password.isEmpty()) {
            WriteRequest retry = pending_request_;
            retry.op = op;
            retry.credential_user = user.toStdString();
            retry.credential_password = password.toStdString();
            user.fill(QLatin1Char('\0'));
            password.fill(QLatin1Char('\0'));
            // The operation stays in flight; only the request changes.
            issueWriteInternal(retry, true);
            return;
        }
        if (!accepted) {
            message_->setText(QCoreApplication::translate(
                "VEdit", "Authentication cancelled; the operation was aborted and nothing was written."));
            message_->setVisible(true);
        }
        if (!user.isEmpty() || !password.isEmpty()) {
            message_->setText(QCoreApplication::translate(
                "VEdit", "Authentication needs both a user name and a password; nothing was written."));
            message_->setVisible(true);
        }
        // Fall through: the failure below reports what git actually said.
    }

    if (result.ok && has_followup_) {
        // A chained operation (create a branch, then switch to it) runs now; the
        // panel stays in the working state until the chain finishes.
        WriteRequest next = followup_request_;
        has_followup_ = false;
        followup_request_ = WriteRequest{};
        issueWriteInternal(next, false);
        return;
    }
    has_followup_ = false;
    followup_request_ = WriteRequest{};
    write_in_flight_ = false;
    retrying_with_credentials_ = false;
    clearCredentials();

    if (result.ok) {
        switch (op) {
        case WriteOp::GitStage:
            staged_overlay_.insert(pending_path_);
            message_->setText(QCoreApplication::translate("VEdit", "Staged %1").arg(pending_path_));
            break;
        case WriteOp::GitUnstage:
            staged_overlay_.remove(pending_path_);
            message_->setText(QCoreApplication::translate("VEdit", "Unstaged %1").arg(pending_path_));
            break;
        case WriteOp::GitDiscard:
            staged_overlay_.remove(pending_path_);
            message_->setText(QCoreApplication::translate("VEdit", "Discarded the working-tree changes of %1").arg(pending_path_));
            break;
        case WriteOp::GitCommit:
            staged_overlay_.clear();
            message_->setText(QCoreApplication::translate("VEdit", "Commit written."));
            break;
        case WriteOp::GitFetch:
            message_->setText(QCoreApplication::translate("VEdit", "Fetched from the remote."));
            break;
        case WriteOp::GitPull:
            message_->setText(QCoreApplication::translate("VEdit", "Pulled the current branch."));
            break;
        case WriteOp::GitPush:
            message_->setText(QCoreApplication::translate("VEdit", "Pushed the current branch."));
            break;
        case WriteOp::GitBranchCreate:
            message_->setText(QCoreApplication::translate("VEdit", "Branch %1 created.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitBranchSwitch:
            message_->setText(QCoreApplication::translate("VEdit", "Switched to %1.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitBranchDelete:
            message_->setText(QCoreApplication::translate("VEdit", "Branch %1 deleted.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitBranchRename:
            message_->setText(QCoreApplication::translate("VEdit", "Branch %1 renamed to %2.")
                                  .arg(QString::fromStdString(pending_request_.name),
                                       QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitTagCreate:
            message_->setText(QCoreApplication::translate("VEdit", "Tag %1 created.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitTagDelete:
            message_->setText(QCoreApplication::translate("VEdit", "Tag %1 deleted.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitCheckout:
            message_->setText(QCoreApplication::translate("VEdit", "Checked out %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitRemoteAdd:
            message_->setText(QCoreApplication::translate("VEdit", "Remote %1 added.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitRemoteRemove:
            message_->setText(QCoreApplication::translate("VEdit", "Remote %1 removed.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitRemoteSetUrl:
            message_->setText(QCoreApplication::translate("VEdit", "Remote %1 URL updated.")
                                  .arg(QString::fromStdString(pending_request_.name)));
            break;
        case WriteOp::GitMerge:
            message_->setText(QCoreApplication::translate("VEdit", "Merged %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitRebase:
            message_->setText(QCoreApplication::translate("VEdit", "Rebased onto %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitCherryPick:
            message_->setText(QCoreApplication::translate("VEdit", "Cherry-picked %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitRevert:
            message_->setText(QCoreApplication::translate("VEdit", "Reverted %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitReset:
            message_->setText(QCoreApplication::translate("VEdit", "Reset to %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitClean:
            message_->setText(QCoreApplication::translate("VEdit", "Deleted %1 untracked file(s).")
                                  .arg(static_cast<int>(pending_request_.paths.size())));
            break;
        case WriteOp::GitContinue:
            message_->setText(QCoreApplication::translate("VEdit", "Continued the %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitAbort:
            message_->setText(QCoreApplication::translate("VEdit", "Aborted the %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitSetUpstream:
            message_->setText(QCoreApplication::translate("VEdit", "%1 now tracks %2.")
                                  .arg(QString::fromStdString(pending_request_.name),
                                       QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitStashSave:
            message_->setText(QCoreApplication::translate("VEdit", "The working tree was stashed."));
            break;
        case WriteOp::GitStashPop:
            message_->setText(QCoreApplication::translate("VEdit", "The stash was applied and dropped."));
            break;
        case WriteOp::GitStashApply:
            message_->setText(QCoreApplication::translate("VEdit", "The stash was applied."));
            break;
        case WriteOp::GitStashDrop:
            message_->setText(QCoreApplication::translate("VEdit", "The stash was dropped."));
            break;
        case WriteOp::GitWorktreeAdd:
            message_->setText(QCoreApplication::translate("VEdit", "Worktree added at %1.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitWorktreeRemove:
            message_->setText(QCoreApplication::translate("VEdit", "Worktree %1 removed.")
                                  .arg(QString::fromStdString(pending_request_.target)));
            break;
        case WriteOp::GitSubmoduleInit:
            message_->setText(QCoreApplication::translate("VEdit", "Submodules initialized."));
            break;
        case WriteOp::GitSubmoduleUpdate:
            message_->setText(QCoreApplication::translate("VEdit", "Submodules updated."));
            break;
        case WriteOp::SvnAdd:
        case WriteOp::SvnRevert:
        case WriteOp::SvnRemove: {
            const QString path = pending_request_.paths.empty()
                ? QString()
                : QString::fromStdString(pending_request_.paths.front());
            const char *verb = op == WriteOp::SvnAdd      ? QT_TRANSLATE_NOOP("VEdit", "Added %1.")
                               : op == WriteOp::SvnRevert ? QT_TRANSLATE_NOOP("VEdit", "Reverted %1.")
                                                          : QT_TRANSLATE_NOOP("VEdit", "Removed %1.");
            message_->setText(QCoreApplication::translate("VEdit", verb).arg(path));
            break;
        }
        case WriteOp::SvnCommit:
            message_->setText(QCoreApplication::translate("VEdit", "Committed the working copy."));
            break;
        case WriteOp::SvnUpdate:
            message_->setText(QCoreApplication::translate("VEdit", "Working copy updated."));
            break;
        default:
            message_->setText(QCoreApplication::translate("VEdit", "VCS operation completed."));
            break;
        }
        message_->setVisible(true);
        // The staging overlay changed, so the rows move NOW; the backend truth
        // still arrives through the coalesced refresh below.
        renderChanges();
        // AC-3.5: a finished VCS operation refreshes the panel through the same
        // coalescer, so a burst of operations still produces one read.
        scheduleRefresh(QStringLiteral("write"));
        // Batch 5b: a workspace operation changes one of the two listings, so
        // re-read them (asynchronously) for the tooltip / the next dialog open.
        if (op == WriteOp::GitStashSave || op == WriteOp::GitStashPop
            || op == WriteOp::GitStashApply || op == WriteOp::GitStashDrop
            || op == WriteOp::GitWorktreeAdd || op == WriteOp::GitWorktreeRemove
            || op == WriteOp::GitSubmoduleInit || op == WriteOp::GitSubmoduleUpdate) {
            refreshWorkspace();
        }
    } else {
        const QString reason = QString::fromStdString(result.summary());
        error_->setText(reason.isEmpty() ? QCoreApplication::translate("VEdit", "The VCS operation failed.")
                                         : QCoreApplication::translate("VEdit", "The VCS operation failed: %1").arg(reason));
        error_->setVisible(true);
        // The first stderr line can be empty even when the tool failed; the
        // summary is the honest one-line reason then.
        if (message_->text().isEmpty() || message_->text().startsWith(
                QCoreApplication::translate("VEdit", "Working…"))) {
            message_->setText(QCoreApplication::translate("VEdit", "Nothing was written."));
        }
        message_->setVisible(true);
        // The tool may have half-applied a change; re-read rather than assume.
        scheduleRefresh(QStringLiteral("write-failed"));
    }
    pending_path_.clear();
    updateActions();
}

void VcsPanel::promptCommit()
{
    CommitDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        message_->setText(QCoreApplication::translate("VEdit", "Commit cancelled; nothing was written."));
        message_->setVisible(true);
        return;
    }
    commit(dialog.message(), dialog.author(), dialog.amend());
}

// --- batch 4a: remote synchronisation, refs, credentials ------------------

void VcsPanel::setCredentialProvider(CredentialProvider provider)
{
    credential_provider_ = std::move(provider);
}

QString VcsPanel::sanitizeUrl(const QString &url) const
{
    // Strip a userinfo segment (https://user:token@host/... -> https://host/...).
    // The panel never renders a secret, not even one somebody typed into the
    // configured remote URL, and the credential prompt shows this form only.
    const int scheme = url.indexOf(QStringLiteral("://"));
    if (scheme < 0) {
        return url;
    }
    const int start = scheme + 3;
    const int at = url.indexOf(QLatin1Char('@'), start);
    if (at < 0) {
        return url;
    }
    const int slash = url.indexOf(QLatin1Char('/'), start);
    if (slash >= 0 && slash < at) {
        return url; // the '@' belongs to a path segment, not to userinfo
    }
    return url.left(start) + url.mid(at + 1);
}

QString VcsPanel::remoteUrl(const QString &name) const
{
    for (const RemoteEntry &remote : info_snapshot_.remotes) {
        if (QString::fromStdString(remote.name) == name) {
            return QString::fromStdString(remote.url);
        }
    }
    return {};
}

QString VcsPanel::defaultRemote() const
{
    // One configured remote is the unambiguous choice; with several, git's own
    // resolution (upstream / origin) is better than a guess.
    if (info_snapshot_.remotes.size() == 1) {
        return QString::fromStdString(info_snapshot_.remotes.front().name);
    }
    return {};
}

int VcsPanel::refRowCount() const
{
    int rows = 0;
    for (int top = 0; top < refs_->topLevelItemCount(); ++top) {
        rows += refs_->topLevelItem(top)->childCount();
    }
    return rows;
}

QString VcsPanel::credentialPromptText(WriteOp op) const
{
    QString remote = QString::fromStdString(pending_request_.target);
    if (remote.isEmpty()) {
        remote = defaultRemote();
    }
    QString url = sanitizeUrl(remoteUrl(remote));
    if (url.isEmpty()) {
        url = remote;
    }
    const QString action = t(writeOpName(op));
    if (url.isEmpty()) {
        return QCoreApplication::translate(
                   "VEdit", "The %1 operation needs credentials for the remote. "
                            "They are kept in memory for this operation only and are never written to disk.")
            .arg(action);
    }
    return QCoreApplication::translate(
               "VEdit", "The %1 operation needs credentials for %2. "
                        "They are kept in memory for this operation only and are never written to disk.")
        .arg(action, url);
}

bool VcsPanel::askCredentials(WriteOp op, const QString &prompt, QString &userName, QString &password)
{
    Q_UNUSED(op);
    ++credential_prompt_count_;
    last_credential_prompt_ = prompt;
    if (credential_provider_) {
        return credential_provider_(prompt, userName, password);
    }
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")
        && !modal_credential_dialog_) {
        // Headless has nobody to ask: never invent credentials, abort and say why.
        // A probe that wants the REAL modal dialog (F2) sets the hook above and
        // answers it with a QTimer.
        message_->setText(QCoreApplication::translate(
            "VEdit", "Authentication is required but no credential dialog is available headless; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    CredentialDialog dialog(prompt, this);
    ++modal_credential_dialogs_;
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    userName = dialog.userName();
    password = dialog.password();
    return true;
}

bool VcsPanel::credentialSecretCleared() const
{
    return pending_request_.credential_user.empty() && pending_request_.credential_password.empty();
}

bool VcsPanel::fetch()
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitFetch;
    request.root = root_;
    request.target = defaultRemote().toStdString();
    return issueWrite(request);
}

bool VcsPanel::pull()
{
    if (backend_ == Backend::Subversion) {
        // The same slot is `svn update` for Subversion (the button says so).
        if (!source_) {
            return false;
        }
        WriteRequest update;
        update.op = WriteOp::SvnUpdate;
        update.root = root_;
        return issueWrite(update);
    }
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitPull;
    request.root = root_;
    // With a configured upstream, plain `git pull --ff-only` is exact; without
    // one, name the remote and the branch explicitly (and omit the branch when no
    // remote is known, so git's own resolution applies).
    if (info_snapshot_.upstream.empty()) {
        const QString remote = defaultRemote();
        request.target = remote.toStdString();
        if (!remote.isEmpty()) {
            request.name = info_snapshot_.branch;
        }
    }
    return issueWrite(request);
}

bool VcsPanel::push()
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    // The first push of a branch publishes and tracks it; later pushes use the
    // configured upstream. An explicit remote is passed when one is known, so the
    // args are unambiguous; with no remote known the branch name is omitted too,
    // because `git push <branch>` would treat the branch as the remote.
    const bool track = info_snapshot_.upstream.empty();
    const QString remote = defaultRemote();
    if (track && remote.isEmpty() && info_snapshot_.remotes.empty()) {
        // Nothing to push to: refuse with a reason instead of letting git read the
        // branch name as a remote ("fatal: 'master' does not appear to be a git
        // repository"). The refs column shows the same state.
        message_->setText(QCoreApplication::translate("VEdit", "No remote is configured; add a remote first."));
        message_->setVisible(true);
        return false;
    }
    const QString branch = remote.isEmpty() ? QString() : QString::fromStdString(info_snapshot_.branch);
    return pushBranch(remote, branch, track);
}

bool VcsPanel::pushBranch(const QString &remote, const QString &branch, bool setUpstream)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitPush;
    request.root = root_;
    request.target = remote.toStdString();
    request.name = branch.toStdString();
    request.set_upstream = setUpstream;
    return issueWrite(request);
}

bool VcsPanel::createBranch(const QString &name, const QString &startPoint, bool switchAfterCreate)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = refNameProblem(startPoint); !startPoint.trimmed().isEmpty()
                                                           && !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid start point: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitBranchCreate;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    request.target = startPoint.trimmed().toStdString();
    if (switchAfterCreate) {
        // `git branch` does not switch; the switch is a second operation that only
        // runs when the first one succeeded (never half-apply a creation).
        WriteRequest next;
        next.op = WriteOp::GitBranchSwitch;
        next.root = root_;
        next.name = request.name;
        followup_request_ = next;
        has_followup_ = true;
    }
    const bool issued = issueWrite(request);
    if (!issued) {
        has_followup_ = false;
        followup_request_ = WriteRequest{};
    }
    return issued;
}

bool VcsPanel::switchBranch(const QString &name)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitBranchSwitch;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::deleteBranch(const QString &name)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(QCoreApplication::translate("VEdit", "Delete the branch?"),
                    QCoreApplication::translate(
                        "VEdit", "Delete branch %1?\n\nThe commits it points at become unreachable from "
                                 "the branch list. This cannot be undone from the panel.")
                        .arg(name),
                    QCoreApplication::translate("VEdit", "Delete the branch permanently?"),
                    QCoreApplication::translate("VEdit", "The branch %1 is removed; this cannot be undone "
                                                         "from the panel. Continue?")
                        .arg(name))) {
        message_->setText(QCoreApplication::translate("VEdit", "Branch deletion cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitBranchDelete;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::renameBranch(const QString &from, const QString &to)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(from); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = refNameProblem(to); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid new branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitBranchRename;
    request.root = root_;
    request.name = from.trimmed().toStdString();
    request.target = to.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::createTag(const QString &name, const QString &target)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid tag name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = refNameProblem(target); !target.trimmed().isEmpty()
                                                        && !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid target: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitTagCreate;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    request.target = target.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::deleteTag(const QString &name)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid tag name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(QCoreApplication::translate("VEdit", "Delete the tag?"),
                    QCoreApplication::translate("VEdit", "Delete tag %1?\n\nThis cannot be undone from the panel.")
                        .arg(name),
                    QCoreApplication::translate("VEdit", "Delete the tag permanently?"),
                    QCoreApplication::translate("VEdit", "The tag %1 is removed; this cannot be undone "
                                                         "from the panel. Continue?")
                        .arg(name))) {
        message_->setText(QCoreApplication::translate("VEdit", "Tag deletion cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitTagDelete;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::checkoutRef(const QString &ref)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(ref); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid ref: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitCheckout;
    request.root = root_;
    request.target = ref.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::addRemote(const QString &name, const QString &url)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid remote name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = urlProblem(url); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid URL: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitRemoteAdd;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    request.url = url.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::removeRemote(const QString &name)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid remote name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(QCoreApplication::translate("VEdit", "Remove the remote?"),
                    QCoreApplication::translate(
                        "VEdit", "Remove the remote %1?\n\nThe remote-tracking branches it owns are removed "
                                 "with it; the commits stay in the repository.")
                        .arg(name),
                    QCoreApplication::translate("VEdit", "Remove the remote permanently?"),
                    QCoreApplication::translate("VEdit", "The remote %1 and its remote-tracking branches "
                                                         "are removed. Continue?")
                        .arg(name))) {
        message_->setText(QCoreApplication::translate("VEdit", "Remote removal cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitRemoteRemove;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::setRemoteUrl(const QString &name, const QString &url)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(name); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid remote name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = urlProblem(url); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid URL: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitRemoteSetUrl;
    request.root = root_;
    request.name = name.trimmed().toStdString();
    request.url = url.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::setUpstream(const QString &branch, const QString &upstream)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    if (const QString problem = refNameProblem(branch); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid branch name: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    if (const QString problem = refNameProblem(upstream); !problem.isEmpty()) {
        message_->setText(QCoreApplication::translate("VEdit", "Invalid upstream: %1; nothing was written.")
                              .arg(problem));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitSetUpstream;
    request.root = root_;
    request.name = branch.trimmed().toStdString();
    request.target = upstream.trimmed().toStdString();
    return issueWrite(request);
}

// --- batch 5b: workspace (stash / worktree / submodule) -------------------

void VcsPanel::refreshWorkspace()
{
    // A completed listing is KEPT until its replacement arrives: the dialog is
    // filled from these snapshots, so clearing them first would render an empty
    // dialog on every open.
    const auto set_unavailable = [this](const QString &note) {
        stashes_snapshot_ = WorkspaceData{};
        stashes_snapshot_.kind = WorkspaceKind::Stashes;
        stashes_snapshot_.note = note.toStdString();
        worktrees_snapshot_ = WorkspaceData{};
        worktrees_snapshot_.kind = WorkspaceKind::Worktrees;
        worktrees_snapshot_.note = note.toStdString();
        renderWorkspace();
    };
    if (!source_ || !source_->workspaceCapable()) {
        set_unavailable(
            QCoreApplication::translate("VEdit", "This VCS source has no workspace listing."));
        return;
    }
    if (backend_ != Backend::Git) {
        set_unavailable(QCoreApplication::translate(
            "VEdit", "Stashes and worktrees are Git concepts; this working tree uses another backend."));
        return;
    }
    ++workspace_request_count_;
    source_->requestWorkspace(workspaceRequest(WorkspaceKind::Stashes),
                              [this](WorkspaceData data) { applyWorkspaceData(std::move(data)); });
    source_->requestWorkspace(workspaceRequest(WorkspaceKind::Worktrees),
                              [this](WorkspaceData data) { applyWorkspaceData(std::move(data)); });
}

WorkspaceRequest VcsPanel::workspaceRequest(WorkspaceKind kind) const
{
    WorkspaceRequest request;
    request.kind = kind;
    request.root = root_;
    return request;
}

void VcsPanel::applyWorkspaceData(WorkspaceData data)
{
    if (data.kind == WorkspaceKind::Worktrees) {
        worktrees_snapshot_ = std::move(data);
    } else {
        stashes_snapshot_ = std::move(data);
    }
    renderWorkspace();
}

void VcsPanel::renderWorkspace()
{
    if (workspace_button_ != nullptr) {
        workspace_button_->setToolTip(
            QCoreApplication::translate("VEdit", "Stashes: %1, worktrees: %2")
                .arg(stashCount())
                .arg(worktreeCount()));
    }
}

QString VcsPanel::workspaceErrorText() const
{
    const QString stashes = QString::fromStdString(stashes_snapshot_.note);
    const QString worktrees = QString::fromStdString(worktrees_snapshot_.note);
    if (stashes.isEmpty()) {
        return worktrees;
    }
    if (worktrees.isEmpty() || worktrees == stashes) {
        return stashes;
    }
    return stashes + QStringLiteral(" / ") + worktrees;
}

QString VcsPanel::workspaceStatusText() const
{
    if (stashes_snapshot_.error || worktrees_snapshot_.error) {
        return workspaceErrorText();
    }
    return QCoreApplication::translate("VEdit", "%1 stash(es), %2 worktree(s)")
        .arg(stashCount())
        .arg(worktreeCount());
}

int VcsPanel::stashCount() const
{
    return static_cast<int>(stashes_snapshot_.entries.size());
}

QString VcsPanel::stashText(int row) const
{
    if (row < 0 || row >= stashCount()) {
        return QString();
    }
    const WorkspaceEntry &entry = stashes_snapshot_.entries[static_cast<std::size_t>(row)];
    return QStringLiteral("%1  %2")
        .arg(QString::fromStdString(entry.refName), QString::fromStdString(entry.subject));
}

int VcsPanel::worktreeCount() const
{
    return static_cast<int>(worktrees_snapshot_.entries.size());
}

QString VcsPanel::worktreeText(int row) const
{
    if (row < 0 || row >= worktreeCount()) {
        return QString();
    }
    const WorkspaceEntry &entry = worktrees_snapshot_.entries[static_cast<std::size_t>(row)];
    QString text = QString::fromStdString(entry.path);
    if (!entry.branch.empty()) {
        text += QStringLiteral(" [%1]").arg(QString::fromStdString(entry.branch));
    }
    return text;
}

bool VcsPanel::stashSave(const QString &message)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitStashSave;
    request.root = root_;
    request.message = message.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::stashPop(const QString &stash)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitStashPop;
    request.root = root_;
    request.target = stash.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::stashApply(const QString &stash)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitStashApply;
    request.root = root_;
    request.target = stash.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::stashDrop(const QString &stash)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString name = stash.trimmed().isEmpty()
        ? QCoreApplication::translate("VEdit", "the newest stash")
        : stash.trimmed();
    if (!confirmTwice(
            QCoreApplication::translate("VEdit", "Drop the stash?"),
            QCoreApplication::translate(
                "VEdit", "Drop %1?\n\nThe stash entry is deleted; its changes are lost unless "
                         "another reference holds the commit.")
                .arg(name),
            QCoreApplication::translate("VEdit", "Drop the stash permanently?"),
            QCoreApplication::translate(
                "VEdit", "The stash entry cannot be restored from the panel after it is dropped. "
                         "Continue?"))) {
        message_->setText(
            QCoreApplication::translate("VEdit", "Stash drop cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitStashDrop;
    request.root = root_;
    request.target = stash.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::worktreeAdd(const QString &path, const QString &branch)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        message_->setText(QCoreApplication::translate(
            "VEdit", "A worktree path is required; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    if (trimmed.startsWith(QLatin1Char('-'))) {
        message_->setText(QCoreApplication::translate(
            "VEdit", "The worktree path cannot start with '-'; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitWorktreeAdd;
    request.root = root_;
    request.target = trimmed.toStdString();
    request.name = branch.trimmed().toStdString();
    return issueWrite(request);
}

bool VcsPanel::worktreeRemove(const QString &path)
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        message_->setText(QCoreApplication::translate(
            "VEdit", "A worktree path is required; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    if (!confirmTwice(
            QCoreApplication::translate("VEdit", "Remove the worktree?"),
            QCoreApplication::translate(
                "VEdit", "Remove the worktree at %1?\n\nThe checkout is deleted. The branch and "
                         "its commits stay in the repository.")
                .arg(trimmed),
            QCoreApplication::translate("VEdit", "Remove the worktree permanently?"),
            QCoreApplication::translate(
                "VEdit", "The worktree directory is deleted. Git refuses a worktree with modified "
                         "or untracked files, so run it again after cleaning it up. Continue?"))) {
        message_->setText(QCoreApplication::translate(
            "VEdit", "Worktree removal cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitWorktreeRemove;
    request.root = root_;
    request.target = trimmed.toStdString();
    return issueWrite(request);
}

bool VcsPanel::submoduleInit()
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitSubmoduleInit;
    request.root = root_;
    return issueWrite(request);
}

bool VcsPanel::submoduleUpdate()
{
    if (backend_ != Backend::Git || !source_) {
        return false;
    }
    WriteRequest request;
    request.op = WriteOp::GitSubmoduleUpdate;
    request.root = root_;
    return issueWrite(request);
}

void VcsPanel::promptWorkspace()
{
    // Fill the dialog from the CURRENT snapshots; a read is started so the next
    // open (and the panel tooltip) is current. The dialog never blocks a read.
    refreshWorkspace();
    WorkspaceDialog dialog(this);
    dialog.setStashes(stashes_snapshot_.entries);
    dialog.setWorktrees(worktrees_snapshot_.entries);
    dialog.setStatusText(workspaceStatusText());
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    switch (dialog.action()) {
    case WorkspaceDialog::Action::StashSave:
        stashSave(dialog.stashMessage());
        break;
    case WorkspaceDialog::Action::StashPop:
        stashPop(dialog.selectedStash());
        break;
    case WorkspaceDialog::Action::StashApply:
        stashApply(dialog.selectedStash());
        break;
    case WorkspaceDialog::Action::StashDrop:
        stashDrop(dialog.selectedStash());
        break;
    case WorkspaceDialog::Action::WorktreeAdd:
        worktreeAdd(dialog.worktreePath(), dialog.worktreeBranch());
        break;
    case WorkspaceDialog::Action::WorktreeRemove:
        worktreeRemove(dialog.selectedWorktree().isEmpty() ? dialog.worktreePath()
                                                           : dialog.selectedWorktree());
        break;
    case WorkspaceDialog::Action::SubmoduleInit:
        submoduleInit();
        break;
    case WorkspaceDialog::Action::SubmoduleUpdate:
        submoduleUpdate();
        break;
    case WorkspaceDialog::Action::None:
        break;
    }
}

void VcsPanel::promptBranch()
{
    BranchDialog dialog(BranchDialog::Mode::Create, this);
    // F3: prefill the start point from the ref selected in the tree ("create a
    // branch from here"), so the dialog is never a blank form when the user
    // already pointed at where they want to start.
    const QString selected = selectedRefName();
    if (!selected.isEmpty()) {
        dialog.setFields(QString(), selected, true);
    }
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        message_->setText(QCoreApplication::translate("VEdit", "Branch creation cancelled; nothing was written."));
        message_->setVisible(true);
        return;
    }
    createBranch(dialog.name(), dialog.startPoint(), dialog.switchAfterCreate());
}

void VcsPanel::promptTag()
{
    TagDialog dialog(this);
    // F3: the selected ref is the tag's target when there is one (a branch, a tag
    // or a remote-tracking ref all name a commit).
    const QString selected = selectedRefName();
    if (!selected.isEmpty()) {
        dialog.setFields(QString(), selected);
    }
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        message_->setText(QCoreApplication::translate("VEdit", "Tag creation cancelled; nothing was written."));
        message_->setVisible(true);
        return;
    }
    createTag(dialog.name(), dialog.target());
}

void VcsPanel::promptRemote()
{
    RemoteDialog dialog(RemoteDialog::Mode::Add, this);
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        message_->setText(QCoreApplication::translate("VEdit", "Remote creation cancelled; nothing was written."));
        message_->setVisible(true);
        return;
    }
    addRemote(dialog.name(), dialog.url());
}

bool VcsPanel::editRemote(const QString &name)
{
    if (backend_ != Backend::Git || !source_ || remoteUrl(name).isEmpty()) {
        return false;
    }
    RemoteDialog dialog(RemoteDialog::Mode::Edit, this);
    // F3: prefilled from the remote the menu was opened on.
    dialog.setFields(name, sanitizeUrl(remoteUrl(name)));
    ++modal_dialogs_;
    if (dialog.exec() != QDialog::Accepted) {
        message_->setText(QCoreApplication::translate("VEdit", "Remote change cancelled; nothing was written."));
        message_->setVisible(true);
        return false;
    }
    return setRemoteUrl(name, dialog.url());
}

void VcsPanel::showRefContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = refs_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const QString name = item->data(0, Qt::UserRole).toString();
    if (name.isEmpty()) {
        return;
    }
    const auto kind = static_cast<RefKind>(item->data(0, Qt::UserRole + 1).toInt());
    QMenu menu(this);
    if (kind == RefKind::Branch) {
        menu.addAction(QCoreApplication::translate("VEdit", "Switch to this branch"),
                       [this, name]() { switchBranch(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Merge into the current branch"),
                       [this, name]() { merge(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Rebase the current branch onto this"),
                       [this, name]() { rebase(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Reset the current branch here (soft)"),
                       [this, name]() { resetTo(name, QStringLiteral("soft")); });
        menu.addAction(QCoreApplication::translate("VEdit", "Reset the current branch here (mixed)"),
                       [this, name]() { resetTo(name, QStringLiteral("mixed")); });
        menu.addAction(QCoreApplication::translate("VEdit", "Reset the current branch here (hard)"),
                       [this, name]() { resetTo(name, QStringLiteral("hard")); });
        menu.addAction(QCoreApplication::translate("VEdit", "Create a branch here…"), [this, name]() {
            bool accepted = false;
            const QString branch = QInputDialog::getText(
                this, QCoreApplication::translate("VEdit", "New branch"),
                QCoreApplication::translate("VEdit", "Branch name (from %1)").arg(name),
                QLineEdit::Normal, QString(), &accepted);
            if (accepted && !branch.trimmed().isEmpty()) {
                createBranch(branch, name, true);
            }
        });
        menu.addAction(QCoreApplication::translate("VEdit", "Rename…"), [this, name]() {
            bool accepted = false;
            const QString renamed = QInputDialog::getText(
                this, QCoreApplication::translate("VEdit", "Rename branch"),
                QCoreApplication::translate("VEdit", "New name for %1").arg(name),
                QLineEdit::Normal, name, &accepted);
            if (accepted && !renamed.trimmed().isEmpty() && renamed != name) {
                renameBranch(name, renamed);
            }
        });
        menu.addAction(QCoreApplication::translate("VEdit", "Delete branch…"),
                       [this, name]() { deleteBranch(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Set as upstream"),
                       [this, name]() { setUpstream(QString::fromStdString(info_snapshot_.branch), name); });
    } else if (kind == RefKind::Tag) {
        menu.addAction(QCoreApplication::translate("VEdit", "Check out this tag"),
                       [this, name]() { checkoutRef(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Delete tag…"),
                       [this, name]() { deleteTag(name); });
    } else if (kind == RefKind::RemoteBranch) {
        menu.addAction(QCoreApplication::translate("VEdit", "Track this branch"),
                       [this, name]() { setUpstream(QString::fromStdString(info_snapshot_.branch), name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Check out (detached HEAD)"),
                       [this, name]() { checkoutRef(name); });
    }
    if (item->data(0, Qt::UserRole + 2).toString() == QStringLiteral("remote")) {
        // A configured remote row: edit its URL (prefilled) or remove it.
        menu.addAction(QCoreApplication::translate("VEdit", "Change URL…"),
                       [this, name]() { editRemote(name); });
        menu.addAction(QCoreApplication::translate("VEdit", "Remove remote…"),
                       [this, name]() { removeRemote(name); });
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.exec(refs_->viewport()->mapToGlobal(position));
}

// --- automatic refresh ----------------------------------------------------

void VcsPanel::scheduleRefresh()
{
    scheduleRefresh(QStringLiteral("scheduled"));
}

void VcsPanel::scheduleRefresh(const QString &reason)
{
    ++refresh_request_count_;
    if (coalesce_ == nullptr) {
        return;
    }
    if (coalesce_->isActive()) {
        // AC-3.5: everything inside one window merges into the ONE refresh that
        // window already has scheduled.
        ++merged_refresh_count_;
        return;
    }
    pending_reason_ = reason;
    coalesce_->start(coalesce_ms_);
}

void VcsPanel::noteFileSaved(const QString &path)
{
    Q_UNUSED(path);
    scheduleRefresh(QStringLiteral("file-saved"));
}

void VcsPanel::noteActivated()
{
    scheduleRefresh(QStringLiteral("activated"));
}

void VcsPanel::manualRefresh()
{
    last_refresh_reason_ = QStringLiteral("manual");
    refresh();
}

void VcsPanel::onRefreshTimer()
{
    last_refresh_reason_ = pending_reason_.isEmpty() ? QStringLiteral("scheduled") : pending_reason_;
    pending_reason_.clear();
    refresh();
}

void VcsPanel::setCoalesceWindowMs(int milliseconds)
{
    coalesce_ms_ = milliseconds > 0 ? milliseconds : 1;
}

bool VcsPanel::refreshPending() const
{
    return coalesce_ != nullptr && coalesce_->isActive();
}

void VcsPanel::flushRefreshForProbe()
{
    if (coalesce_ != nullptr && coalesce_->isActive()) {
        coalesce_->stop();
        onRefreshTimer();
    }
}

bool VcsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::ApplicationActivate) {
        noteActivated();
    }
    return QWidget::eventFilter(watched, event);
}

void VcsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QWidget *top = window();
    if (top != nullptr && filtered_window_ != top) {
        if (filtered_window_ != nullptr) {
            filtered_window_->removeEventFilter(this);
        }
        filtered_window_ = top;
        top->installEventFilter(this);
    }
}

// --- probe-readable queries ----------------------------------------------

int VcsPanel::fileCount() const
{
    return changes_->count() + staged_->count();
}

int VcsPanel::changeCount() const
{
    return changes_->count();
}

int VcsPanel::stagedCount() const
{
    return staged_->count();
}

QString VcsPanel::fileText(int row) const
{
    const QListWidgetItem *item = changes_->item(row);
    return item != nullptr ? item->text() : QString();
}

QString VcsPanel::stagedText(int row) const
{
    const QListWidgetItem *item = staged_->item(row);
    return item != nullptr ? item->text() : QString();
}

QString VcsPanel::headerText() const
{
    return header_->text();
}

QString VcsPanel::emptyText() const
{
    return empty_->text();
}

QString VcsPanel::loadingText() const
{
    return loading_->text();
}

QString VcsPanel::errorText() const
{
    return error_->text();
}

QString VcsPanel::messageText() const
{
    return message_->text();
}

QString VcsPanel::diffText() const
{
    return diff_->toPlainText();
}

QStringList VcsPanel::refTexts() const
{
    QStringList texts;
    for (int top = 0; top < refs_->topLevelItemCount(); ++top) {
        QTreeWidgetItem *item = refs_->topLevelItem(top);
        texts << item->text(0);
        for (int child = 0; child < item->childCount(); ++child) {
            texts << item->child(child)->text(0);
        }
    }
    return texts;
}

QStringList VcsPanel::actionNames() const
{
    return {refresh_button_->objectName(), stage_button_->objectName(),
            unstage_button_->objectName(), discard_button_->objectName(),
            commit_button_->objectName(), fetch_button_->objectName(),
            pull_button_->objectName(), push_button_->objectName(),
            branch_button_->objectName(), tag_button_->objectName(),
            remote_button_->objectName(), workspace_button_->objectName(),
            remove_button_->objectName()};
}

bool VcsPanel::emptyStateVisible() const
{
    return empty_->isVisibleTo(this);
}

bool VcsPanel::loadingStateVisible() const
{
    return loading_->isVisibleTo(this);
}

bool VcsPanel::errorStateVisible() const
{
    return error_->isVisibleTo(this);
}

} // namespace vedit::workspace::vcs

