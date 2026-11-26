#include "vcs_history.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace vedit::workspace::vcs {

namespace {

QString formatTime(long long seconds)
{
    if (seconds <= 0) {
        return {};
    }
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// One commit read rendered as text (detail tab / comparison tab share it).
QString commitMetaText(const HistoryData &data)
{
    QStringList lines;
    if (!data.hash.empty()) {
        lines << QStringLiteral("%1 %2")
                     .arg(QCoreApplication::translate("VEdit", "Commit"), QString::fromStdString(data.hash));
    }
    if (!data.author.empty()) {
        QString line = QStringLiteral("%1 %2").arg(QCoreApplication::translate("VEdit", "Author"), QString::fromStdString(data.author));
        if (!data.email.empty()) {
            line += QStringLiteral(" <%1>").arg(QString::fromStdString(data.email));
        }
        lines << line;
    }
    if (data.time > 0) {
        lines << QStringLiteral("%1 %2").arg(QCoreApplication::translate("VEdit", "Date"), formatTime(data.time));
    }
    if (!data.subject.empty()) {
        lines << QStringLiteral("%1 %2").arg(QCoreApplication::translate("VEdit", "Subject"), QString::fromStdString(data.subject));
    }
    if (!data.body.empty()) {
        lines << QString();
        lines << QString::fromStdString(data.body);
    }
    return lines.join(QLatin1Char('\n'));
}

QString fileStatText(const std::vector<FileStat> &files)
{
    QStringList lines;
    for (const FileStat &file : files) {
        if (file.additions < 0 || file.deletions < 0) {
            lines << QStringLiteral("%1  %2").arg(QCoreApplication::translate("VEdit", "binary"), QString::fromStdString(file.path));
        } else {
            lines << QStringLiteral("+%1  -%2  %3")
                         .arg(file.additions)
                         .arg(file.deletions)
                         .arg(QString::fromStdString(file.path));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString commitRowsText(const std::vector<CommitEntry> &commits)
{
    QStringList lines;
    for (const CommitEntry &commit : commits) {
        lines << QStringLiteral("%1  %2  %3  %4")
                     .arg(QString::fromStdString(commit.shortHash),
                          formatTime(commit.time),
                          QString::fromStdString(commit.author),
                          QString::fromStdString(commit.subject));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

VcsHistoryPanel::VcsHistoryPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    showState(QCoreApplication::translate("VEdit", "Open a working tree to browse its history."), false);
}

VcsHistoryPanel::~VcsHistoryPanel() = default;

void VcsHistoryPanel::buildUi()
{
    setObjectName(QStringLiteral("vcs-history-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    header_ = new QLabel(this);
    header_->setObjectName(QStringLiteral("vcs-history-header"));
    QFont headerFont = header_->font();
    headerFont.setBold(true);
    header_->setFont(headerFont);
    layout->addWidget(header_);

    loading_ = new QLabel(QCoreApplication::translate("VEdit", "Loading…"), this);
    loading_->setObjectName(QStringLiteral("vcs-history-loading"));
    loading_->setVisible(false);
    layout->addWidget(loading_);

    empty_ = new QLabel(this);
    empty_->setObjectName(QStringLiteral("vcs-history-empty"));
    empty_->setWordWrap(true);
    empty_->setVisible(false);
    layout->addWidget(empty_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("vcs-history-error"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #b00020;"));
    error_->setVisible(false);
    layout->addWidget(error_);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("vcs-history-message"));
    message_->setWordWrap(true);
    message_->setVisible(false);
    layout->addWidget(message_);

    auto *repositories = new QHBoxLayout();
    repository_ = new QComboBox(this);
    repository_->setObjectName(QStringLiteral("svn-saved-repositories"));
    repository_->addItem(tr("Working-copy repository"), QString());
    addRepository_ = new QPushButton(tr("Add SVN Repository…"), this);
    addRepository_->setObjectName(QStringLiteral("svn-repository-add"));
    removeRepository_ = new QPushButton(tr("Remove Saved Repository"), this);
    removeRepository_->setObjectName(QStringLiteral("svn-repository-remove"));
    repositories->addWidget(repository_, 1);
    repositories->addWidget(addRepository_);
    repositories->addWidget(removeRepository_);
    layout->addLayout(repositories);
    connect(addRepository_, &QPushButton::clicked, this, [this] { addSavedRepository(); });
    connect(removeRepository_, &QPushButton::clicked, this, [this] { removeSavedRepository(); });
    connect(repository_, &QComboBox::currentIndexChanged, this, [this] {
        clearEditorFileHistory();
        ++generation_;
        ++svnSelectionEpoch_;
        svnHistoryPath_.clear();
        svnDetailRevision_.clear();
        svnRevisionFiles_.clear();
        path_->clear();
        commits_->clear();
        detail_->clear();
        loadPage(0);
    });

    // --- search + paging ---------------------------------------------------
    auto *searchRow = new QHBoxLayout();
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("vcs-history-search"));
    search_->setPlaceholderText(QCoreApplication::translate("VEdit", "Search commit messages"));
    search_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Search"), this);
    search_button_->setObjectName(QStringLiteral("vcs-history-action-search"));
    search_button_->setToolTip(QCoreApplication::translate("VEdit", "Search the commit messages (literal, case-insensitive)"));
    refresh_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Refresh"), this);
    refresh_button_->setObjectName(QStringLiteral("vcs-history-action-refresh"));
    searchRow->addWidget(search_);
    searchRow->addWidget(search_button_);
    searchRow->addWidget(refresh_button_);
    svnRevisionsButton_ = new QPushButton(tr("Compare Revisions…"), this);
    svnRevisionsButton_->setObjectName(QStringLiteral("svn-history-compare-revisions"));
    searchRow->addWidget(svnRevisionsButton_);
    searchRow->addStretch(1);
    layout->addLayout(searchRow);

    auto *pageRow = new QHBoxLayout();
    previous_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Previous"), this);
    previous_button_->setObjectName(QStringLiteral("vcs-history-action-previous"));
    next_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Next"), this);
    next_button_->setObjectName(QStringLiteral("vcs-history-action-next"));
    page_ = new QLabel(this);
    page_->setObjectName(QStringLiteral("vcs-history-page"));
    pageRow->addWidget(previous_button_);
    pageRow->addWidget(next_button_);
    pageRow->addWidget(page_);
    pageRow->addSpacing(12);
    path_ = new QLineEdit(this);
    path_->setObjectName(QStringLiteral("vcs-history-path"));
    path_->setClearButtonEnabled(true);
    path_->setPlaceholderText(QCoreApplication::translate("VEdit", "File path (repository-relative)"));
    file_button_ = new QPushButton(QCoreApplication::translate("VEdit", "File history"), this);
    file_button_->setObjectName(QStringLiteral("vcs-history-action-file"));
    blame_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Blame"), this);
    blame_button_->setObjectName(QStringLiteral("vcs-history-action-blame"));
    compare_button_ = new QPushButton(QCoreApplication::translate("VEdit", "Compare selected"), this);
    compare_button_->setObjectName(QStringLiteral("vcs-history-action-compare"));
    pageRow->addWidget(path_, 1);
    pageRow->addWidget(file_button_);
    pageRow->addWidget(blame_button_);
    layout->addLayout(pageRow);

    auto *compareRow = new QHBoxLayout();
    compareRow->addWidget(compare_button_);
    svnWorkingButton_ = new QPushButton(tr("Compare With Working Copy"), this);
    svnWorkingButton_->setObjectName(QStringLiteral("svn-history-compare-working"));
    compareRow->addWidget(svnWorkingButton_);
    compareRow->addStretch();
    layout->addLayout(compareRow);

    // --- commits + detail tabs --------------------------------------------
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setObjectName(QStringLiteral("vcs-history-columns"));
    commits_ = new QTreeWidget(splitter_);
    commits_->setObjectName(QStringLiteral("vcs-history-commits"));
    commits_->setHeaderLabels({QCoreApplication::translate("VEdit", "Commit"), QCoreApplication::translate("VEdit", "Date"), QCoreApplication::translate("VEdit", "Author"), QCoreApplication::translate("VEdit", "Subject")});
    commits_->setRootIsDecorated(false);
    commits_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    commits_->header()->setStretchLastSection(true);
    // Fixed-content columns keep the date/author readable at 1500 px; the subject
    // takes the remaining width.
    commits_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    commits_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    commits_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    commits_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    splitter_->addWidget(commits_);

    tabs_ = new QTabWidget(splitter_);
    tabs_->setObjectName(QStringLiteral("vcs-history-tabs"));
    detail_ = new QPlainTextEdit(tabs_);
    detail_->setObjectName(QStringLiteral("vcs-history-detail"));
    detail_->setReadOnly(true);
    detail_->setFont(QFont(QStringLiteral("monospace")));
    file_history_ = new QPlainTextEdit(tabs_);
    file_history_->setObjectName(QStringLiteral("vcs-history-file"));
    file_history_->setReadOnly(true);
    file_history_->setFont(QFont(QStringLiteral("monospace")));
    blame_ = new QPlainTextEdit(tabs_);
    blame_->setObjectName(QStringLiteral("vcs-history-blame"));
    blame_->setReadOnly(true);
    blame_->setFont(QFont(QStringLiteral("monospace")));
    compare_ = new QPlainTextEdit(tabs_);
    compare_->setObjectName(QStringLiteral("vcs-history-compare"));
    compare_->setReadOnly(true);
    compare_->setFont(QFont(QStringLiteral("monospace")));
    tabs_->addTab(detail_, QCoreApplication::translate("VEdit", "Commit"));
    tabs_->addTab(file_history_, QCoreApplication::translate("VEdit", "File history"));
    tabs_->addTab(blame_, QCoreApplication::translate("VEdit", "Blame"));
    tabs_->addTab(compare_, QCoreApplication::translate("VEdit", "Compare"));
    splitter_->addWidget(tabs_);
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 3);
    layout->addWidget(splitter_, 1);

    connect(refresh_button_, &QPushButton::clicked, this, [this]() { refresh(); });
    connect(search_button_, &QPushButton::clicked, this, [this]() { search(search_->text()); });
    connect(search_, &QLineEdit::returnPressed, this, [this]() { search(search_->text()); });
    connect(next_button_, &QPushButton::clicked, this, [this]() { nextPage(); });
    connect(previous_button_, &QPushButton::clicked, this, [this]() { previousPage(); });
    connect(file_button_, &QPushButton::clicked, this, [this]() { openFileHistory(path_->text()); });
    connect(blame_button_, &QPushButton::clicked, this, [this]() { showBlame(path_->text()); });
    connect(compare_button_, &QPushButton::clicked, this, [this]() { compareSelected(); });
    commits_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(commits_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (backend_ != Backend::Subversion || !editorFileHistoryCurrent()) { return; }
        auto *item = commits_->itemAt(pos);
        if (item == nullptr) { return; }
        if (!item->isSelected()) { commits_->setCurrentItem(item); }
        const QString revision = item->data(0, Qt::UserRole).toString();
        const QString message = item->data(0, Qt::UserRole + 2).toString();
        const QString selectedAddress = repository_->currentData().toString();
        const QString remote = selectedAddress.isEmpty() ? svnDetailRepository_ : selectedAddress;
        const auto epoch = svnSelectionEpoch_;
        QMenu menu(this);
        menu.addAction(tr("Copy Revision Number"), this, [revision] { QGuiApplication::clipboard()->setText(revision); });
        menu.addAction(tr("Copy Commit Message"), this, [message] { QGuiApplication::clipboard()->setText(message); });
        if (!svnHistoryPath_.isEmpty()) {
            auto *comparison = menu.addAction(tr("Compare Revisions"), this, [this] { compareSvnSelection(false); });
            const auto count = commits_->selectedItems().size();
            comparison->setEnabled(count == 1 || count == 2);
            menu.addAction(tr("Compare This Revision With BASE"), this, [this, revision, epoch] {
                if (epoch == svnSelectionEpoch_) { emit svnBaseComparisonRequested(svnHistoryPath_, revision.toULongLong(), svnPeg_); }
            });
            menu.addAction(tr("Open This Revision"), this, [this, revision, epoch] {
                if (epoch == svnSelectionEpoch_) { emit svnSnapshotRequested(svnHistoryPath_, revision.toULongLong(), svnPeg_); }
            });
            menu.addAction(tr("Open Working File"), this, [this, epoch] {
                if (epoch == svnSelectionEpoch_) { emit svnWorkingFileRequested(svnHistoryPath_); }
            });
        } else if (svnDetailRevision_ == revision) {
            auto *files = menu.addMenu(tr("Changed Files"));
            for (const auto &path : svnRevisionFiles_) {
                auto *file = files->addMenu(path);
                if (selectedAddress.isEmpty() && !svnCheckoutPrefix_.isEmpty() && path.startsWith(svnCheckoutPrefix_)) {
                    const auto relative = path.sliced(svnCheckoutPrefix_.size());
                    file->addAction(tr("Open Working File"), this, [this, relative, epoch] {
                        if (epoch == svnSelectionEpoch_) { emit svnWorkingFileRequested(relative); }
                    });
                }
                file->addAction(tr("Open This Revision"), this, [this, path, revision, remote, epoch] {
                    if (epoch != svnSelectionEpoch_) { return; }
                    if (!remote.isEmpty()) { emit svnRemoteFileRequested(remote, path, revision.toULongLong(), svnDeletedRevisionFiles_.contains(path), false); }
                });
                file->addAction(tr("Compare Revisions"), this, [this, path, revision, remote, epoch] {
                    if (epoch != svnSelectionEpoch_) { return; }
                    if (!remote.isEmpty()) { emit svnRemoteFileRequested(remote, path, revision.toULongLong(), svnDeletedRevisionFiles_.contains(path), true); }
                });
            }
            files->setEnabled(!svnRevisionFiles_.isEmpty());
        }
        menu.exec(commits_->viewport()->mapToGlobal(pos));
    });
    connect(svnWorkingButton_, &QPushButton::clicked, this, [this] { compareSvnSelection(true); });
    connect(svnRevisionsButton_, &QPushButton::clicked, this, [this] { promptSvnRevisions(); });
    connect(path_, &QLineEdit::textChanged, this, [this] {
        clearEditorFileHistory();
        ++svnSelectionEpoch_;
        ++fileRequestEpoch_;
        updateActions();
    });
    connect(path_, &QLineEdit::returnPressed, this, [this] { openFileHistory(path_->text()); });
    connect(commits_, &QTreeWidget::itemSelectionChanged, this, [this]() { onSelectionChanged(); });
    updateActions();
}

void VcsHistoryPanel::setSource(std::unique_ptr<VcsSource> source, bool deferRefresh)
{
    source_ = std::move(source);
    ++generation_;
    if (!deferRefresh) { refresh(); }
}

void VcsHistoryPanel::setRoot(const std::string &root)
{
    if (root_ == root) {
        return;
    }
    root_ = root;
    clearEditorFileHistory();
    ++generation_;
    ++svnSelectionEpoch_;
    backend_ = Backend::None;
    svnHistoryPath_.clear();
    svnDetailRevision_.clear();
    svnRevisionFiles_.clear();
    path_->clear();
    svnPeg_ = kDefaultSvnPeg;
    commits_->clear();
    detail_->clear();
    file_history_->clear();
    blame_->clear();
    compare_->clear();
    offset_ = 0;
    has_more_ = false;
    ready_ = false;
    empty_state_ = false;
    failed_ = false;
    header_->clear();
    updateActions();
}

void VcsHistoryPanel::setPageSize(int size)
{
    page_size_ = size > 0 ? size : 1;
    if (page_size_ > kMaxHistoryPage) {
        page_size_ = kMaxHistoryPage;
    }
}

void VcsHistoryPanel::issue(const HistoryRequest &request)
{
    ++request_count_;
    last_request_ = request;
    ++in_flight_;
    loading_->setVisible(true);
    updateActions();
    if (!source_) {
        --in_flight_;
        loading_->setVisible(in_flight_ > 0);
        HistoryData result;
        result.kind = request.kind;
        result.note = "no VCS backend available";
        if (request.kind == HistoryKind::Commits) {
            applyCommits(std::move(result));
        }
        return;
    }
    if (!source_->historyCapable()) {
        --in_flight_;
        loading_->setVisible(in_flight_ > 0);
        HistoryData result;
        result.kind = request.kind;
        result.note = "this VCS source has no history path";
        if (request.kind == HistoryKind::Commits) {
            applyCommits(std::move(result));
        }
        return;
    }
    const quint64 generation = generation_;
    const quint64 fileRequest = request.kind == HistoryKind::FileHistory ? ++fileRequestEpoch_ : 0;
    const quint64 pageRequest = request.kind == HistoryKind::Commits || request.kind == HistoryKind::SvnRemoteLog ? ++historyPageEpoch_ : 0;
    const quint64 detail = request.kind == HistoryKind::Commit || request.kind == HistoryKind::SvnRemoteDetail ? ++detailEpoch_ : 0;
    const auto guard = fileRequest != 0 ? historySelectionGuard_ : std::function<bool()>{};
    source_->requestHistory(request, [this, generation, fileRequest, pageRequest, detail, guard, kind = request.kind](HistoryData data) {
        --in_flight_;
        loading_->setVisible(in_flight_ > 0);
        updateActions();
        if (generation != generation_ || (guard && !guard()) || (fileRequest != 0 && fileRequest != fileRequestEpoch_) ||
            (pageRequest != 0 && pageRequest != historyPageEpoch_) || (detail != 0 && detail != detailEpoch_)) {
            return;
        }
        data.kind = kind;
        switch (kind) {
        case HistoryKind::Commits:
        case HistoryKind::SvnRemoteLog:
            applyCommits(std::move(data));
            break;
        case HistoryKind::Commit:
        case HistoryKind::SvnRemoteDetail:
            applyCommit(std::move(data));
            break;
        case HistoryKind::FileHistory:
            applyFileHistory(std::move(data));
            break;
        case HistoryKind::Blame:
            applyBlame(std::move(data));
            break;
        case HistoryKind::Compare:
            applyCompare(std::move(data));
            break;
        case HistoryKind::SvnSnapshot:
        case HistoryKind::SvnPrevious:
        case HistoryKind::SvnPatch:
        case HistoryKind::SvnIncoming:
        case HistoryKind::SvnRemoteSnapshot:
        case HistoryKind::SvnRemotePrevious:
            break;
        }
    });
}

void VcsHistoryPanel::refresh()
{
    if ((backend_ == Backend::Subversion || hasEditorFileHistory()) && !path_->text().isEmpty()) {
        openFileHistory(path_->text());
    } else {
        loadPage(0);
    }
}

void VcsHistoryPanel::loadPage(int offset)
{
    if (offset < 0) {
        offset = 0;
    }
    HistoryRequest request;
    request.url = repository_->currentData().toString().toStdString();
    request.kind = request.url.empty() ? HistoryKind::Commits : HistoryKind::SvnRemoteLog;
    request.root = root_;
    request.offset = offset;
    request.limit = page_size_;
    request.query = current_query_.toStdString();
    issue(request);
}

void VcsHistoryPanel::search(const QString &query)
{
    current_query_ = query.trimmed();
    // Keep the box in step when the search is driven programmatically (probe).
    if (search_->text() != current_query_) {
        search_->setText(current_query_);
    }
    if ((backend_ == Backend::Subversion || hasEditorFileHistory()) && !path_->text().isEmpty()) {
        openFileHistory(path_->text());
    } else {
        loadPage(0);
    }
}

void VcsHistoryPanel::nextPage()
{
    if (has_more_) {
        loadPage(offset_ + page_size_);
    }
}

void VcsHistoryPanel::previousPage()
{
    if (offset_ > 0) {
        loadPage(offset_ - page_size_ < 0 ? 0 : offset_ - page_size_);
    }
}

void VcsHistoryPanel::selectCommit(const QString &hash)
{
    if (hash.isEmpty()) {
        return;
    }
    const int row = commitRow(hash);
    if (row >= 0 && commits_->currentItem() != commits_->topLevelItem(row)) {
        commits_->setCurrentItem(commits_->topLevelItem(row));
        return; // the selection change issued the detail read
    }
    HistoryRequest request;
    request.url = repository_->currentData().toString().toStdString();
    request.kind = request.url.empty() ? HistoryKind::Commit : HistoryKind::SvnRemoteDetail;
    request.revision.number = hash.toULongLong();
    request.root = root_;
    request.hash = hash.toStdString();
    issue(request);
}

void VcsHistoryPanel::onSelectionChanged()
{
    ++svnSelectionEpoch_;
    const QTreeWidgetItem *item = commits_->currentItem();
    if (item == nullptr || item->data(0, Qt::UserRole).toString().isEmpty()) {
        updateActions();
        return;
    }
    selected_hash_ = item->data(0, Qt::UserRole).toString();
    if (backend_ != Backend::Subversion || svnHistoryPath_.isEmpty()) { selectCommit(selected_hash_); }
    updateActions();
}

void VcsHistoryPanel::openEditorFileHistory(const std::string &root, const QString &path,
                                           std::function<bool()> isActive)
{
    editorHistoryRoot_ = root;
    editorFileIsActive_ = std::move(isActive);
    ++generation_;
    svnHistoryPath_.clear();
    svnDetailRevision_.clear();
    svnPeg_ = kDefaultSvnPeg;
    {
        const QSignalBlocker block(path_);
        path_->setText(path);
    }
    backend_ = Backend::Subversion;
    current_query_.clear();
    search_->clear();
    commits_->clear();
    detail_->clear();
    empty_->hide();
    error_->hide();
    header_->setText(tr("Subversion File History"));
    header_->setToolTip(QString::fromStdString(root));
    openFileHistory(path);
}

bool VcsHistoryPanel::editorFileHistoryCurrent() const
{
    return !editorFileIsActive_ || editorFileIsActive_();
}

void VcsHistoryPanel::editorFileChanged()
{
    if (hasEditorFileHistory()) {
        ++fileRequestEpoch_;
        ++svnSelectionEpoch_;
        updateActions();
    }
}

void VcsHistoryPanel::clearEditorFileHistory()
{
    if (hasEditorFileHistory()) {
        editorHistoryRoot_.clear();
        editorFileIsActive_ = {};
        svnHistoryPath_.clear();
        ++generation_;
        ++svnSelectionEpoch_;
        commits_->clear();
        header_->setToolTip({});
    }
}

void VcsHistoryPanel::openFileHistory(const QString &path, std::function<bool()> stillCurrent)
{
    if (stillCurrent || path != path_->text()) { clearEditorFileHistory(); }
    if (!editorFileHistoryCurrent()) { return; }
    if (repository_->currentIndex() > 0) {
        const QSignalBlocker block(repository_);
        repository_->setCurrentIndex(0);
        ++generation_;
    }
    historySelectionGuard_ = hasEditorFileHistory() ? editorFileIsActive_ : std::move(stillCurrent);
    if (path.isEmpty() || !source_) {
        return;
    }
    if (path_->text() != path) {
        path_->setText(path);
    }
    ++svnSelectionEpoch_;
    HistoryRequest request;
    request.kind = HistoryKind::FileHistory;
    request.root = root();
    request.path = path.toStdString();
    request.offset = 0;
    request.limit = backend_ == Backend::Subversion ? kMaxHistoryPage : page_size_;
    issue(request);
}

void VcsHistoryPanel::showBlame(const QString &path)
{
    const QString value = path.trimmed();
    if (value.isEmpty() || !source_) {
        return;
    }
    HistoryRequest request;
    request.kind = HistoryKind::Blame;
    request.root = root_;
    request.path = value.toStdString();
    issue(request);
}

void VcsHistoryPanel::compareSvnSelection(bool workingCopy)
{
    if (backend_ != Backend::Subversion || !editorFileHistoryCurrent() || svnHistoryPath_ != path_->text()) {
        return;
    }
    QList<quint64> revisions;
    for (const auto *item : commits_->selectedItems()) {
        bool ok = false;
        const quint64 revision = item->data(0, Qt::UserRole).toString().toULongLong(&ok);
        if (ok) {
            revisions.append(revision);
        }
    }
    if (revisions.isEmpty() || revisions.size() > (workingCopy ? 1 : 2)) {
        return;
    }
    std::sort(revisions.begin(), revisions.end());
    emit svnComparisonRequested(svnHistoryPath_, revisions, svnPeg_, workingCopy);
}

void VcsHistoryPanel::promptSvnRevisions()
{
    if (!editorFileHistoryCurrent()) { return; }
    const QString path = path_->text();
    const quint64 epoch = svnSelectionEpoch_;
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("Compare SVN Revisions"),
        tr("Enter two revisions separated by a comma, for example r12, r34."), QLineEdit::Normal, {}, &accepted);
    if (!accepted || epoch != svnSelectionEpoch_) {
        return;
    }
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*[rR]?(\d+)\s*,\s*[rR]?(\d+)\s*$)"));
    const auto match = pattern.match(text);
    bool leftOk = false;
    bool rightOk = false;
    const auto left = match.capturedView(1).toULongLong(&leftOk);
    const auto right = match.capturedView(2).toULongLong(&rightOk);
    if (!leftOk || !rightOk || left == kDefaultSvnPeg || right == kDefaultSvnPeg || left == right) {
        QMessageBox::information(this, tr("Compare SVN Revisions"), tr("Enter two different revision numbers."));
        return;
    }
    const auto peg = path == svnHistoryPath_ ? svnPeg_ : kDefaultSvnPeg;
    emit svnComparisonRequested(path, {left, right}, peg, false);
}

void VcsHistoryPanel::compareSelected()
{
    if (backend_ == Backend::Subversion) {
        compareSvnSelection(false);
        return;
    }
    if (!source_) {
        return;
    }
    QStringList selected;
    for (const QTreeWidgetItem *item : commits_->selectedItems()) {
        selected << item->data(0, Qt::UserRole).toString();
    }
    if (selected.size() < 2) {
        showState(QCoreApplication::translate("VEdit", "Select two commits to compare."), true);
        return;
    }
    // The list is newest first, so the LATER row is the older commit: compare
    // oldest -> newest, which is what a user reads as "what changed".
    HistoryRequest request;
    request.kind = HistoryKind::Compare;
    request.root = root_;
    request.from = selected.at(1).toStdString();
    request.to = selected.at(0).toStdString();
    issue(request);
}

void VcsHistoryPanel::showState(const QString &message, bool error)
{
    message_->setText(message);
    message_->setVisible(!message.isEmpty());
    if (error) {
        failed_ = true;
    }
}

void VcsHistoryPanel::applyCommits(HistoryData data)
{
    backend_ = data.backend;
    const bool svn = backend_ == Backend::Subversion;
    if (svn && !path_->text().trimmed().isEmpty()) {
        if (svnHistoryPath_ != path_->text()) { openFileHistory(path_->text()); }
        return;
    }
    if (svn) {
        svnHistoryPath_.clear();
        selected_hash_.clear();
        detail_->clear();
        search_->setPlaceholderText(tr("Search text or revision (r123)"));
    }
    const QString title = svn ? tr("Subversion Repository History") : QCoreApplication::translate("VEdit", "Git history");
    offset_ = data.offset;
    has_more_ = data.hasMore;
    error_->setVisible(false);
    failed_ = false;
    if (data.error) {
        failed_ = true;
        commits_->clear();
        splitter_->setVisible(false);
        error_->setText(data.note.empty() ? QCoreApplication::translate("VEdit", "The history read failed.") : QCoreApplication::translate("VEdit", "The history read failed: %1")
                              .arg(QString::fromStdString(data.note)));
        error_->setVisible(true);
        header_->setText(title);
        page_->setText(QString());
        empty_->setVisible(false);
        updateActions();
        return;
    }
    empty_state_ = data.commits.empty();
    ready_ = true;
    header_->setText(svn || data.commits.empty() ? title
        : QCoreApplication::translate("VEdit", "Git history · %1 commit(s) on this page").arg(static_cast<int>(data.commits.size())));
    // Newest first; the hash rides in UserRole so a probe never parses the text.
    commits_->clear();
    commits_->setColumnCount(4);
    commits_->setHeaderLabels({QCoreApplication::translate("VEdit", "Commit"), QCoreApplication::translate("VEdit", "Date"), QCoreApplication::translate("VEdit", "Author"), QCoreApplication::translate("VEdit", "Subject")});
    for (const CommitEntry &commit : data.commits) {
        auto *item = new QTreeWidgetItem(commits_);
        item->setText(0, QString::fromStdString(commit.shortHash));
        item->setText(1, formatTime(commit.time));
        item->setText(2, QString::fromStdString(commit.author));
        item->setText(3, QString::fromStdString(commit.subject));
        item->setData(0, Qt::UserRole, QString::fromStdString(commit.hash));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(commit.shortHash));
        item->setData(0, Qt::UserRole + 2, QString::fromStdString(commit.subject));
    }
    const int first = data.commits.empty() ? 0 : offset_ + 1;
    const int last = offset_ + static_cast<int>(data.commits.size());
    page_->setText(data.commits.empty() ? QString()
                                        : QCoreApplication::translate("VEdit", "Commits %1–%2").arg(first).arg(last));
    splitter_->setVisible(!data.commits.empty());
    empty_->setVisible(data.commits.empty());
    if (data.commits.empty()) {
        if (offset_ > 0) {
            empty_->setText(QCoreApplication::translate("VEdit", "No more commits."));
        } else if (!data.note.empty()) {
            empty_->setText(QCoreApplication::translate("VEdit", "No commits to show: %1").arg(QString::fromStdString(data.note)));
        } else if (!current_query_.isEmpty()) {
            empty_->setText(QCoreApplication::translate("VEdit", "No commit matches \"%1\".").arg(current_query_));
        } else {
            empty_->setText(QCoreApplication::translate("VEdit", "This repository has no commits yet."));
        }
    }
    message_->setVisible(false);
    updateActions();
}

void VcsHistoryPanel::applyCommit(HistoryData data)
{
    if (data.error) {
        detail_->setPlainText(data.note.empty() ? QCoreApplication::translate("VEdit", "The commit could not be read.")
                                                : QCoreApplication::translate("VEdit", "The commit could not be read: %1")
                                                      .arg(QString::fromStdString(data.note)));
        tabs_->setCurrentIndex(0);
        return;
    }
    QString text = commitMetaText(data);
    if (data.backend == Backend::Subversion) {
        const QString revision = QString::fromStdString(data.hash);
        svnDetailRevision_ = revision.startsWith(u'r') ? revision.sliced(1) : revision;
        svnDetailRepository_ = QString::fromStdString(data.from);
        svnCheckoutPrefix_ = QString::fromStdString(data.to);
        if (!svnCheckoutPrefix_.isEmpty() && !svnCheckoutPrefix_.endsWith(u'/')) { svnCheckoutPrefix_.append(u'/'); }
        svnRevisionFiles_.clear();
        svnDeletedRevisionFiles_.clear();
        for (const auto &file : data.files) {
            const auto path = QString::fromStdString(file.path);
            svnRevisionFiles_.append(path);
            if (file.deletions > 0) { svnDeletedRevisionFiles_.insert(path); }
        }
    }
    if (data.backend != Backend::Subversion && !data.files.empty()) {
        text += QLatin1Char('\n') + QCoreApplication::translate("VEdit", "Files") + QLatin1Char('\n') + fileStatText(data.files);
    }
    if (!data.patch.empty()) {
        text += QStringLiteral("\n\n") + QString::fromStdString(data.patch);
    }
    detail_->setPlainText(text);
    tabs_->setCurrentIndex(0);
    message_->setVisible(false);
}

void VcsHistoryPanel::applyFileHistory(HistoryData data)
{
    if (data.backend == Backend::Subversion) {
        backend_ = data.backend;
        svnHistoryPath_ = QString::fromStdString(data.path);
        bool pegOk = false;
        svnPeg_ = QString::fromStdString(data.hash).toULongLong(&pegOk);
        if (!pegOk) {
            svnPeg_ = kDefaultSvnPeg;
        }
        header_->setText(tr("Subversion File History"));
        commits_->clear();
        commits_->setColumnCount(4);
        commits_->setHeaderLabels({tr("Revision"), QCoreApplication::translate("VEdit", "Date"),
                                  QCoreApplication::translate("VEdit", "Author"), QCoreApplication::translate("VEdit", "Subject")});
        const QString query = current_query_;
        const QRegularExpression rev(QStringLiteral("^r?([0-9]+)$"));
        const auto revisionQuery = rev.match(query);
        for (const auto &revision : data.commits) {
            const auto id = QString::fromStdString(revision.hash);
            const auto author = QString::fromStdString(revision.author);
            const auto message = QString::fromStdString(revision.subject);
            if (!query.isEmpty() && (revisionQuery.hasMatch() ? id != revisionQuery.captured(1)
                : !message.contains(query, Qt::CaseInsensitive) && !author.contains(query, Qt::CaseInsensitive))) { continue; }
            auto *item = new QTreeWidgetItem(commits_);
            item->setText(0, QString::fromStdString(revision.shortHash));
            item->setText(1, formatTime(revision.time));
            item->setText(2, author);
            item->setText(3, message.section(u'\n', 0, 0));
            item->setData(0, Qt::UserRole, id);
            item->setData(0, Qt::UserRole + 2, message);
        }
        const bool hasRevisions = commits_->topLevelItemCount() > 0;
        splitter_->setVisible(hasRevisions);
        empty_->setVisible(!data.error && !hasRevisions);
        empty_->setText(query.isEmpty() ? tr("No revisions touch this file.") : tr("No file revisions match the search."));
        error_->setVisible(data.error);
        error_->setText(QString::fromStdString(data.note));
        error_->setTextFormat(Qt::PlainText);
        tabs_->hide();
        updateActions();
        return;
    }
    if (data.error) {
        file_history_->setPlainText(data.note.empty() ? QCoreApplication::translate("VEdit", "The file history could not be read.")
                                                      : QCoreApplication::translate("VEdit", "The file history could not be read: %1")
                                                            .arg(QString::fromStdString(data.note)));
        tabs_->setCurrentIndex(1);
        return;
    }
    QString text = QCoreApplication::translate("VEdit", "History of %1").arg(QString::fromStdString(data.path));
    if (data.commits.empty()) {
        text += QStringLiteral("\n\n") + QCoreApplication::translate("VEdit", "No commit touches this path.");
    } else {
        text += QStringLiteral("\n\n") + commitRowsText(data.commits);
    }
    file_history_->setPlainText(text);
    tabs_->setCurrentIndex(1);
}

void VcsHistoryPanel::applyBlame(HistoryData data)
{
    if (data.error) {
        blame_->setPlainText(data.note.empty() ? QCoreApplication::translate("VEdit", "Blame could not be read.")
                                               : QCoreApplication::translate("VEdit", "Blame could not be read: %1")
                                                     .arg(QString::fromStdString(data.note)));
        tabs_->setCurrentIndex(2);
        return;
    }
    QStringList lines;
    lines << QCoreApplication::translate("VEdit", "Blame of %1").arg(QString::fromStdString(data.path));
    lines << QString();
    for (const BlameLine &line : data.blame) {
        lines << QStringLiteral("%1 %2 %3  %4")
                     .arg(line.line, 5)
                     .arg(QString::fromStdString(line.shortHash).left(7).leftJustified(7),
                          QString::fromStdString(line.author).leftJustified(16),
                          QString::fromStdString(line.text));
    }
    if (data.blame.empty() && !data.note.empty()) {
        lines << QString::fromStdString(data.note);
    }
    blame_->setPlainText(lines.join(QLatin1Char('\n')));
    tabs_->setCurrentIndex(2);
}

void VcsHistoryPanel::applyCompare(HistoryData data)
{
    if (data.error) {
        compare_->setPlainText(data.note.empty() ? QCoreApplication::translate("VEdit", "The comparison could not be read.")
                                                 : QCoreApplication::translate("VEdit", "The comparison could not be read: %1")
                                                       .arg(QString::fromStdString(data.note)));
        tabs_->setCurrentIndex(3);
        return;
    }
    QString text = QCoreApplication::translate("VEdit", "%1 → %2").arg(QString::fromStdString(data.from),
                                    QString::fromStdString(data.to));
    if (!data.files.empty()) {
        text += QLatin1Char('\n') + fileStatText(data.files);
    }
    if (!data.note.empty() && data.files.empty()) {
        text += QStringLiteral("\n\n") + QString::fromStdString(data.note);
    }
    if (!data.patch.empty()) {
        text += QStringLiteral("\n\n") + QString::fromStdString(data.patch);
    }
    compare_->setPlainText(text);
    tabs_->setCurrentIndex(3);
}

void VcsHistoryPanel::updateActions()
{
    const bool idle = in_flight_ == 0;
    const bool hasSource = source_ != nullptr;
    const bool svn = backend_ == Backend::Subversion;
    const qsizetype selected = commits_->selectedItems().size();
    const bool currentFile = editorFileHistoryCurrent();
    const bool svnSelection = currentFile && hasSource && idle && !svnHistoryPath_.isEmpty() && svnHistoryPath_ == path_->text();
    svnWorkingButton_->setVisible(svn);
    svnWorkingButton_->setEnabled(svnSelection && selected == 1);
    svnRevisionsButton_->setVisible(svn);
    svnRevisionsButton_->setEnabled(svn && currentFile && hasSource && idle && !path_->text().isEmpty());
    const bool repositoryHistory = !svn || svnHistoryPath_.isEmpty();
    const bool remoteHistory = repository_->currentIndex() > 0;
    removeRepository_->setEnabled(remoteHistory);
    path_->setEnabled(!remoteHistory);
    previous_button_->setVisible(repositoryHistory);
    next_button_->setVisible(repositoryHistory);
    page_->setVisible(repositoryHistory);
    blame_button_->setVisible(!svn);
    tabs_->setVisible(repositoryHistory);
    for (int index = 1; index < tabs_->count(); ++index) { tabs_->setTabVisible(index, !svn); }
    compare_button_->setText(svn && selected == 1 ? tr("Compare With Previous Revision") : tr("Compare selected"));
    search_button_->setEnabled(currentFile && hasSource && idle);
    refresh_button_->setEnabled(currentFile && hasSource && idle);
    previous_button_->setEnabled(hasSource && idle && offset_ > 0);
    next_button_->setEnabled(hasSource && idle && has_more_);
    file_button_->setEnabled(currentFile && hasSource && idle && !remoteHistory && !path_->text().isEmpty());
    blame_button_->setEnabled(hasSource && idle && !path_->text().trimmed().isEmpty());
    compare_button_->setEnabled(svn ? svnSelection && (selected == 1 || selected == 2)
                                   : hasSource && idle && selected >= 2);
}

int VcsHistoryPanel::commitCount() const
{
    return commits_->topLevelItemCount();
}

QString VcsHistoryPanel::commitText(int row, int column) const
{
    const QTreeWidgetItem *item = commits_->topLevelItem(row);
    return item != nullptr ? item->text(column) : QString();
}

QStringList VcsHistoryPanel::commitHashes() const
{
    QStringList hashes;
    for (int row = 0; row < commits_->topLevelItemCount(); ++row) {
        hashes << commits_->topLevelItem(row)->data(0, Qt::UserRole).toString();
    }
    return hashes;
}

int VcsHistoryPanel::commitRow(const QString &hash) const
{
    for (int row = 0; row < commits_->topLevelItemCount(); ++row) {
        if (commits_->topLevelItem(row)->data(0, Qt::UserRole).toString() == hash) {
            return row;
        }
    }
    return -1;
}

QString VcsHistoryPanel::pageLabel() const
{
    return page_->text();
}

QString VcsHistoryPanel::searchText() const
{
    return search_->text();
}

QStringList VcsHistoryPanel::tabTitles() const
{
    QStringList titles;
    for (int index = 0; index < tabs_->count(); ++index) {
        titles << tabs_->tabText(index);
    }
    return titles;
}

QString VcsHistoryPanel::detailText() const
{
    return detail_->toPlainText();
}

QString VcsHistoryPanel::fileHistoryText() const
{
    return file_history_->toPlainText();
}

QString VcsHistoryPanel::blameText() const
{
    return blame_->toPlainText();
}

QString VcsHistoryPanel::compareText() const
{
    return compare_->toPlainText();
}

QString VcsHistoryPanel::messageText() const
{
    return message_->text();
}

QString VcsHistoryPanel::emptyText() const
{
    return empty_->text();
}

QString VcsHistoryPanel::loadingText() const
{
    return loading_->text();
}

QString VcsHistoryPanel::errorText() const
{
    return error_->text();
}

bool VcsHistoryPanel::emptyStateVisible() const
{
    return empty_->isVisibleTo(this) && !empty_->text().isEmpty();
}

bool VcsHistoryPanel::loadingStateVisible() const
{
    return loading_->isVisibleTo(this) && in_flight_ > 0;
}

bool VcsHistoryPanel::errorStateVisible() const
{
    return error_->isVisibleTo(this);
}

} // namespace vedit::workspace::vcs
