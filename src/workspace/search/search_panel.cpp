#include "search_panel.h"

#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <utility>

namespace vedit::workspace::search {

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    connectSignals();
    updateRootLabel();
    showEmptyState(tr("No project root. Open a project to search its files."));
}

SearchPanel::~SearchPanel() = default;

QWidget *createSearchPanel(const QString &rootPath)
{
    auto *panel = new SearchPanel();
    panel->setRootPath(rootPath);
    return panel;
}

void SearchPanel::buildUi()
{
    setObjectName(QStringLiteral("search-panel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(4);

    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("search-query"));
    query_->setPlaceholderText(tr("Search text in the project…"));
    query_->setClearButtonEnabled(true);
    toolbar->addWidget(query_, 1);

    caseButton_ = new QToolButton(this);
    caseButton_->setObjectName(QStringLiteral("search-case"));
    caseButton_->setText(QStringLiteral("Aa"));
    caseButton_->setCheckable(true);
    caseButton_->setToolTip(tr("Match case"));
    toolbar->addWidget(caseButton_);

    verilogButton_ = new QToolButton(this);
    verilogButton_->setObjectName(QStringLiteral("search-verilog-only"));
    verilogButton_->setText(QStringLiteral("Verilog"));
    verilogButton_->setCheckable(true);
    verilogButton_->setToolTip(tr("Only .v/.vh/.sv/.svh files (explicit extension table)"));
    toolbar->addWidget(verilogButton_);

    searchButton_ = new QPushButton(tr("Search"), this);
    searchButton_->setObjectName(QStringLiteral("search-run"));
    toolbar->addWidget(searchButton_);

    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setObjectName(QStringLiteral("search-cancel"));
    cancelButton_->hide();
    toolbar->addWidget(cancelButton_);

    layout->addLayout(toolbar);

    rootLabel_ = new QLabel(this);
    rootLabel_->setObjectName(QStringLiteral("search-root"));
    rootLabel_->setWordWrap(true);
    layout->addWidget(rootLabel_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("search-summary"));
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("search-results"));
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("File"), tr("Line"), tr("Match")});
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setColumnWidth(0, 260);
    tree_->setColumnWidth(1, 60);
    tree_->hide();
    layout->addWidget(tree_, 1);

    emptyLabel_ = new QLabel(this);
    emptyLabel_->setObjectName(QStringLiteral("search-empty-state"));
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(emptyLabel_, 1);
}

void SearchPanel::connectSignals()
{
    connect(searchButton_, &QPushButton::clicked, this, &SearchPanel::startSearch);
    connect(cancelButton_, &QPushButton::clicked, this, &SearchPanel::cancelSearch);
    connect(query_, &QLineEdit::returnPressed, this, &SearchPanel::startSearch);

    connect(&search_, &GlobalSearch::hitsReady, this, &SearchPanel::appendBatch);
    connect(&search_, &GlobalSearch::finished, this, &SearchPanel::onSearchFinished);

    connect(tree_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) { activateItem(item); });
    connect(tree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *item, int) { activateItem(item); });
}

void SearchPanel::setRootPath(const QString &rootPath)
{
    const QString normalized = rootPath.trimmed();
    rootPath_ = normalized.isEmpty() ? QString() : QFileInfo(normalized).absoluteFilePath();
    search_.cancel();
    searching_ = false;
    cancelButton_->hide();
    searchButton_->setEnabled(true);
    clearResults();
    summaryLabel_->clear();
    updateRootLabel();
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
    } else {
        showEmptyState(tr("Enter a query and press Search."));
    }
}

QString SearchPanel::rootPath() const
{
    return rootPath_;
}

void SearchPanel::setJumpTarget(IJumpTarget *target)
{
    jumpTarget_ = target;
}

IJumpTarget *SearchPanel::jumpTarget() const
{
    return jumpTarget_;
}

void SearchPanel::setQuery(const QString &query)
{
    query_->setText(query);
}

QString SearchPanel::query() const
{
    return query_->text();
}

void SearchPanel::setCaseSensitive(bool on)
{
    caseButton_->setChecked(on);
}

bool SearchPanel::caseSensitive() const
{
    return caseButton_->isChecked();
}

void SearchPanel::setVerilogOnly(bool on)
{
    verilogButton_->setChecked(on);
}

bool SearchPanel::verilogOnly() const
{
    return verilogButton_->isChecked();
}

void SearchPanel::setResultLimit(int limit)
{
    resultLimit_ = limit > 0 ? limit : 2000;
}

int SearchPanel::resultLimit() const
{
    return resultLimit_;
}

void SearchPanel::setMaxFileBytes(qint64 bytes)
{
    maxFileBytes_ = bytes > 0 ? bytes : 8LL * 1024 * 1024;
}

qint64 SearchPanel::maxFileBytes() const
{
    return maxFileBytes_;
}

void SearchPanel::setTimeBudgetMs(qint64 ms)
{
    timeBudgetMs_ = ms > 0 ? ms : 15000;
}

qint64 SearchPanel::timeBudgetMs() const
{
    return timeBudgetMs_;
}

void SearchPanel::startSearch()
{
    const QString queryText = query_->text().trimmed();
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
        return;
    }
    if (queryText.isEmpty()) {
        showEmptyState(tr("Enter a query and press Search."));
        return;
    }

    clearResults();
    emptyLabel_->hide();
    tree_->show();
    summaryLabel_->setText(tr("Searching…"));
    cancelButton_->show();
    searchButton_->setEnabled(false);
    searching_ = true;

    SearchOptions options;
    options.rootPath = rootPath_;
    options.query = queryText;
    options.caseSensitive = caseButton_->isChecked();
    options.verilogOnly = verilogButton_->isChecked();
    options.maxResults = resultLimit_;
    options.maxFileBytes = maxFileBytes_;
    options.maxDurationMs = timeBudgetMs_;

    if (!search_.start(options)) {
        searching_ = false;
        cancelButton_->hide();
        searchButton_->setEnabled(true);
        showEmptyState(tr("The previous search did not stop in time; try again."));
    }
}

void SearchPanel::cancelSearch()
{
    search_.cancel();
    if (searching_) {
        summaryLabel_->setText(tr("Cancelling…"));
    }
}

bool SearchPanel::isSearching() const
{
    return searching_;
}

SearchSummary SearchPanel::lastSummary() const
{
    return lastSummary_;
}

bool SearchPanel::activateHit(int hitIndex)
{
    if (hitIndex < 0 || hitIndex >= hits_.size()) {
        return false;
    }
    const SearchHit &hit = hits_.at(hitIndex);
    JumpRequest request;
    request.filePath = hit.filePath;
    request.line = hit.line;
    request.column = hit.column;
    emit jumpRequested(request);
    if (jumpTarget_ != nullptr) {
        jumpTarget_->jumpTo(request);
    }
    return true;
}

const QList<SearchHit> &SearchPanel::hits() const
{
    return hits_;
}

QTreeWidget *SearchPanel::resultsView() const
{
    return tree_;
}

QLabel *SearchPanel::emptyStateLabel() const
{
    return emptyLabel_;
}

QLabel *SearchPanel::summaryLabel() const
{
    return summaryLabel_;
}

QLabel *SearchPanel::rootLabel() const
{
    return rootLabel_;
}

QLineEdit *SearchPanel::queryEdit() const
{
    return query_;
}

GlobalSearch *SearchPanel::search()
{
    return &search_;
}

void SearchPanel::clearResults()
{
    hits_.clear();
    fileItems_.clear();
    fileHitCounts_.clear();
    tree_->clear();
}

void SearchPanel::appendBatch(const QList<SearchHit> &hits)
{
    for (const SearchHit &hit : hits) {
        hits_.append(hit);

        QTreeWidgetItem *fileItem = fileItems_.value(hit.filePath, nullptr);
        if (fileItem == nullptr) {
            fileItem = new QTreeWidgetItem(tree_);
            fileItem->setText(0, hit.relativePath);
            QFont font = fileItem->font(0);
            font.setBold(true);
            fileItem->setFont(0, font);
            fileItem->setExpanded(true);
            fileItems_.insert(hit.filePath, fileItem);
        }
        const int count = fileHitCounts_.value(hit.filePath, 0) + 1;
        fileHitCounts_.insert(hit.filePath, count);
        fileItem->setText(2, QString::number(count));

        auto *item = new QTreeWidgetItem(fileItem);
        item->setText(1, QString::number(hit.line));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, hit.text.trimmed());
        item->setToolTip(2, hit.text);
        item->setData(0, Qt::UserRole, hits_.size() - 1);
    }
}

void SearchPanel::onSearchFinished(const SearchSummary &summary)
{
    searching_ = false;
    lastSummary_ = summary;
    cancelButton_->hide();
    searchButton_->setEnabled(true);
    summaryLabel_->setText(summaryText(summary));

    if (summary.state == SearchState::Failed) {
        showEmptyState(tr("Search failed: %1").arg(summary.error));
        emit searchFinished(0, false);
        return;
    }
    if (hits_.isEmpty()) {
        if (summary.state == SearchState::Cancelled) {
            showEmptyState(tr("Search cancelled before any hit."));
        } else if (summary.timedOut) {
            showEmptyState(tr("No results within the %1 ms time budget.").arg(timeBudgetMs_));
        } else {
            showEmptyState(tr("No results. Nothing matched \"%1\" in %2.")
                               .arg(query_->text().trimmed(), rootPath_));
        }
        emit searchFinished(0, summary.truncated);
        return;
    }
    tree_->show();
    emptyLabel_->hide();
    emit searchFinished(summary.hitCount, summary.truncated);
}

void SearchPanel::activateItem(QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        return; // a file row or a header: nothing to jump to
    }
    activateHit(data.toInt());
}

void SearchPanel::showEmptyState(const QString &text)
{
    emptyLabel_->setText(text);
    emptyLabel_->show();
    tree_->hide();
}

void SearchPanel::updateRootLabel()
{
    rootLabel_->setText(rootPath_.isEmpty() ? tr("No project root.") : tr("In %1").arg(rootPath_));
}

QString SearchPanel::summaryText(const SearchSummary &summary) const
{
    if (summary.state == SearchState::Failed) {
        return tr("Search failed.");
    }

    QString text = tr("%1 hits in %2 of %3 files — %4 ms")
                       .arg(summary.hitCount)
                       .arg(summary.filesMatched)
                       .arg(summary.filesScanned)
                       .arg(summary.elapsedMs);
    if (summary.state == SearchState::Cancelled) {
        text.prepend(tr("Cancelled: "));
    }

    QStringList notes;
    if (summary.truncated) {
        notes.append(tr("result limit %1 reached").arg(resultLimit_));
    }
    if (summary.timedOut) {
        notes.append(tr("time budget %1 ms reached").arg(timeBudgetMs_));
    }
    if (summary.skippedTooLarge > 0) {
        notes.append(tr("%1 too large skipped").arg(summary.skippedTooLarge));
    }
    if (summary.skippedBinary > 0) {
        notes.append(tr("%1 binary skipped").arg(summary.skippedBinary));
    }
    if (summary.skippedUnreadable > 0) {
        notes.append(tr("%1 unreadable skipped").arg(summary.skippedUnreadable));
    }
    if (!notes.isEmpty()) {
        text += QStringLiteral(" (") + notes.join(QStringLiteral(", ")) + QLatin1Char(')');
    }
    return text;
}

} // namespace vedit::workspace::search
