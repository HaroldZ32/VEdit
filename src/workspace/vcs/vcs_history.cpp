#include "vcs_history.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

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
    pageRow->addWidget(compare_button_);
    layout->addLayout(pageRow);

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
    connect(commits_, &QTreeWidget::itemSelectionChanged, this, [this]() { onSelectionChanged(); });
    updateActions();
}

void VcsHistoryPanel::setSource(std::unique_ptr<VcsSource> source)
{
    source_ = std::move(source);
    ++generation_;
    refresh();
}

void VcsHistoryPanel::setRoot(const std::string &root)
{
    if (root_ == root) {
        return;
    }
    root_ = root;
    ++generation_;
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
    source_->requestHistory(request, [this, generation, kind = request.kind](HistoryData data) {
        --in_flight_;
        loading_->setVisible(in_flight_ > 0);
        if (generation != generation_) {
            return; // the panel moved to another working copy / source
        }
        data.kind = kind;
        switch (kind) {
        case HistoryKind::Commits:
            applyCommits(std::move(data));
            break;
        case HistoryKind::Commit:
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
        }
    });
}

void VcsHistoryPanel::refresh()
{
    loadPage(0);
}

void VcsHistoryPanel::loadPage(int offset)
{
    if (offset < 0) {
        offset = 0;
    }
    HistoryRequest request;
    request.kind = HistoryKind::Commits;
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
    loadPage(0);
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
    request.kind = HistoryKind::Commit;
    request.root = root_;
    request.hash = hash.toStdString();
    issue(request);
}

void VcsHistoryPanel::onSelectionChanged()
{
    const QTreeWidgetItem *item = commits_->currentItem();
    if (item == nullptr || item->data(0, Qt::UserRole).toString().isEmpty()) {
        updateActions();
        return;
    }
    selected_hash_ = item->data(0, Qt::UserRole).toString();
    selectCommit(selected_hash_);
    updateActions();
}

void VcsHistoryPanel::openFileHistory(const QString &path)
{
    const QString value = path.trimmed();
    if (value.isEmpty() || !source_) {
        return;
    }
    HistoryRequest request;
    request.kind = HistoryKind::FileHistory;
    request.root = root_;
    request.path = value.toStdString();
    request.offset = 0;
    request.limit = page_size_;
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

void VcsHistoryPanel::compareSelected()
{
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
        header_->setText(QCoreApplication::translate("VEdit", "Git history"));
        page_->setText(QString());
        empty_->setVisible(false);
        updateActions();
        return;
    }
    empty_state_ = data.commits.empty();
    ready_ = true;
    header_->setText(data.commits.empty()
        ? QCoreApplication::translate("VEdit", "Git history")
        : QCoreApplication::translate("VEdit", "Git history · %1 commit(s) on this page").arg(static_cast<int>(data.commits.size())));
    // Newest first; the hash rides in UserRole so a probe never parses the text.
    commits_->clear();
    for (const CommitEntry &commit : data.commits) {
        auto *item = new QTreeWidgetItem(commits_);
        item->setText(0, QString::fromStdString(commit.shortHash));
        item->setText(1, formatTime(commit.time));
        item->setText(2, QString::fromStdString(commit.author));
        item->setText(3, QString::fromStdString(commit.subject));
        item->setData(0, Qt::UserRole, QString::fromStdString(commit.hash));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(commit.shortHash));
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
    if (!data.files.empty()) {
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
    search_button_->setEnabled(hasSource && idle);
    refresh_button_->setEnabled(hasSource && idle);
    previous_button_->setEnabled(hasSource && idle && offset_ > 0);
    next_button_->setEnabled(hasSource && idle && has_more_);
    file_button_->setEnabled(hasSource && idle && !path_->text().trimmed().isEmpty());
    blame_button_->setEnabled(hasSource && idle && !path_->text().trimmed().isEmpty());
    compare_button_->setEnabled(hasSource && idle && commits_->selectedItems().size() >= 2);
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
