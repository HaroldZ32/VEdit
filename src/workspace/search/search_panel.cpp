#include "search_panel.h"

#include "jump/jump_registry.h"
#include "search_query.h"

#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace vedit::workspace::search {

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    connectSignals();
    updateRootLabel();
    showEmptyState(tr("No project root. Open a project to search its files."));

    // The dock is the ONE project-search endpoint of the Ctrl+F bar's
    // "whole project" scope (ux-batch-2 B2): the bar publishes the query + the
    // shared options and asks for a project search; it never searches the tree
    // from inside a document.
    auto &hub = vedit::search::SearchHub::instance();
    hub.setProjectSearchHandler(this, [this](const vedit::search::SearchHandoff &handoff) {
        applyHandoff(handoff);
        return true;
    });
    // Two-way sync: a change made in the dock follows the same model. b2-repair-r3
    // item 2: while a 「全部替换」 owns the panel its frozen request must not be
    // retargeted from the other surface either — the published snapshot is
    // ignored here (the operation's own controls are disabled for its duration)
    // and applyHandoff() reports the refusal with a message.
    hubSubscription_ = hub.subscribe(
        [this](const vedit::search::SearchHandoff &handoff, const void *source) {
            if (source == this) {
                return;
            }
            if (operationBusy()) {
                return;
            }
            adoptFromHub(handoff);
        });
}

SearchPanel::~SearchPanel()
{
    auto &hub = vedit::search::SearchHub::instance();
    hub.unsubscribe(hubSubscription_);
    hub.removeProjectSearchHandler(this);
}

SearchPanel *createSearchPanel(const QString &rootPath)
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

    // The SAME option set the Ctrl+F bar exposes (search_query.h): whole word
    // and regex complete the four options the two surfaces share.
    wholeButton_ = new QToolButton(this);
    wholeButton_->setObjectName(QStringLiteral("search-whole"));
    wholeButton_->setText(QStringLiteral("W"));
    wholeButton_->setCheckable(true);
    wholeButton_->setToolTip(tr("Whole words"));
    toolbar->addWidget(wholeButton_);

    regexButton_ = new QToolButton(this);
    regexButton_->setObjectName(QStringLiteral("search-regex"));
    regexButton_->setText(QStringLiteral(".*"));
    regexButton_->setCheckable(true);
    regexButton_->setToolTip(tr("Regular expression (project-wide: RE2 style, no backreferences "
                                "or lookaround)"));
    toolbar->addWidget(regexButton_);

    // The scope selector, item 4 of the shared model. The dock is a project
    // search panel, so its natural start is the whole project; the Ctrl+F bar
    // starts on the current file (a UI policy, not a semantic difference).
    scopeCombo_ = new QComboBox(this);
    scopeCombo_->setObjectName(QStringLiteral("search-scope"));
    scopeCombo_->addItem(tr("Current File"),
                         static_cast<int>(vedit::search::SearchScope::CurrentFile));
    scopeCombo_->addItem(tr("Whole Project"),
                         static_cast<int>(vedit::search::SearchScope::WholeProject));
    scopeCombo_->setCurrentIndex(1);
    scopeCombo_->setToolTip(tr("Search the active document or the whole project"));
    toolbar->addWidget(scopeCombo_);

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

    // The replace row (ux-batch-2 B2): 「替换」 replaces the selected hit and
    // advances; 「全部替换」 confirms file/hit counts first, then applies every
    // range grouped per file. Both are plain actions, not a preview dialog.
    replaceRow_ = new QWidget(this);
    replaceRow_->setObjectName(QStringLiteral("search-replace-row"));
    auto *replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(4);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setObjectName(QStringLiteral("search-replacement"));
    replaceEdit_->setPlaceholderText(tr("Replace with"));
    replaceLayout->addWidget(replaceEdit_, 1);
    replaceButton_ = new QPushButton(tr("Replace"), replaceRow_);
    replaceButton_->setObjectName(QStringLiteral("search-replace"));
    replaceButton_->setToolTip(tr("Replace the current match and advance to the next one"));
    replaceLayout->addWidget(replaceButton_);
    replaceAllButton_ = new QPushButton(tr("Replace All"), replaceRow_);
    replaceAllButton_->setObjectName(QStringLiteral("search-replace-all"));
    replaceAllButton_->setToolTip(tr("Replace every match (asks for confirmation with the "
                                     "file and hit counts first)"));
    replaceLayout->addWidget(replaceAllButton_);
    layout->addWidget(replaceRow_);

    replaceStatus_ = new QLabel(this);
    replaceStatus_->setObjectName(QStringLiteral("search-replace-status"));
    replaceStatus_->setWordWrap(true);
    replaceStatus_->hide();
    layout->addWidget(replaceStatus_);

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
    // Medium 7: keyboard navigation changes the CURRENT row without a click;
    // 「替换」 must follow it, so the current item is the selection authority.
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (current == nullptr) {
                    return;
                }
                const QVariant data = current->data(0, Qt::UserRole);
                if (data.isValid()) {
                    currentHitIndex_ = data.toInt();
                }
            });

    // Every query/option change invalidates the result rows (b2-repair-r2
    // item 4): a 「替换」 must never act on a row produced by an older query,
    // an older option set or an older document state. The same handler also
    // follows the shared option model to the other surface.
    connect(query_, &QLineEdit::textChanged, this,
            [this](const QString &) { onQueryControlsChanged(); });
    for (QToolButton *toggle : {caseButton_, wholeButton_, regexButton_, verilogButton_}) {
        connect(toggle, &QToolButton::toggled, this, [this](bool) { onQueryControlsChanged(); });
    }
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { onQueryControlsChanged(); });

    connect(replaceButton_, &QPushButton::clicked, this, [this]() { replaceCurrentHit(); });
    connect(replaceAllButton_, &QPushButton::clicked, this,
            [this]() { replaceAllRequested(); });
    connect(replaceEdit_, &QLineEdit::returnPressed, this,
            [this]() { replaceCurrentHit(); });
    connect(&replacer_, &ProjectReplacer::finished, this, &SearchPanel::onReplaceFinished);

    // Medium 8: a newly created dock must NOT publish its own defaults — that
    // wiped a live Ctrl+F bar's query and options. It seeds itself from the
    // bar's snapshot (subscribe-with-replay); with no bar live it keeps its own
    // defaults (and never inherits another dock's last search).
    auto &hub = vedit::search::SearchHub::instance();
    if (hub.hasFindBarSnapshot()) {
        adoptFromHub(hub.findBarSnapshot());
    }
}

void SearchPanel::setRootPath(const QString &rootPath)
{
    const QString normalized = rootPath.trimmed();
    rootPath_ = normalized.isEmpty() ? QString() : QFileInfo(normalized).absoluteFilePath();
    // AC-1.1/AC-1.2 evidence hook (unconditional on purpose: the acceptance reads
    // it, so it must not depend on a debug switch): the panel states the ONE root
    // it will search, and the previous job is cancelled before the new root is
    // stored, so no later scan can name the old root.
    qInfo("search: root='%s' (%s)", rootPath_.isEmpty() ? "<none>" : qPrintable(rootPath_),
          rootPath_.isEmpty() ? "no project root" : "ready");
    search_.cancel();
    searching_ = false;
    replaceScanPhase_ = ReplaceScanPhase::None;
    operationActive_ = false;
    operationInputChanged_ = false;
    scanInputChanged_ = false;
    clearResults();
    summaryLabel_->clear();
    updateControlState();
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

void SearchPanel::setWholeWords(bool on)
{
    wholeButton_->setChecked(on);
}

bool SearchPanel::wholeWords() const
{
    return wholeButton_->isChecked();
}

void SearchPanel::setRegex(bool on)
{
    regexButton_->setChecked(on);
}

bool SearchPanel::regex() const
{
    return regexButton_->isChecked();
}

vedit::search::QueryOptions SearchPanel::queryOptions() const
{
    vedit::search::QueryOptions options;
    options.caseSensitive = caseButton_->isChecked();
    options.wholeWords = wholeButton_->isChecked();
    options.regex = regexButton_->isChecked();
    options.scope = scope();
    return options;
}

void SearchPanel::setQueryOptions(const vedit::search::QueryOptions &options)
{
    caseButton_->setChecked(options.caseSensitive);
    wholeButton_->setChecked(options.wholeWords);
    regexButton_->setChecked(options.regex);
    setScope(options.scope);
}

void SearchPanel::setScope(vedit::search::SearchScope scope)
{
    const int index = scopeCombo_->findData(static_cast<int>(scope));
    if (index >= 0) {
        scopeCombo_->setCurrentIndex(index);
    }
}

void SearchPanel::setCurrentFileProvider(std::function<QString()> provider)
{
    currentFileProvider_ = std::move(provider);
}

QString SearchPanel::resolveCurrentFile() const
{
    if (currentFileProvider_) {
        // High 5: the product provider resolves the ACTIVE pane's document at
        // the moment of the search/replace, so a tab or pane switch is always
        // followed. An empty result means "no current file" — never a stale
        // path from the last bar/result click.
        const QString active = currentFileProvider_();
        if (!active.isEmpty()) {
            vedit::search::SearchHub::instance().setCurrentFilePath(active);
        }
        return active;
    }
    return vedit::search::SearchHub::instance().currentFilePath();
}

vedit::search::SearchScope SearchPanel::scope() const
{
    return static_cast<vedit::search::SearchScope>(scopeCombo_->currentData().toInt());
}

vedit::search::SearchHandoff SearchPanel::handoff() const
{
    vedit::search::SearchHandoff handoff;
    handoff.text = query_->text();
    handoff.options = queryOptions();
    return handoff;
}

void SearchPanel::publishToHub()
{
    if (syncing_) {
        return;
    }
    vedit::search::SearchHub::instance().publish(handoff(), this);
}

void SearchPanel::adoptFromHub(const vedit::search::SearchHandoff &handoff)
{
    syncing_ = true;
    if (query_->text() != handoff.text) {
        query_->setText(handoff.text);
    }
    setQueryOptions(handoff.options);
    syncing_ = false;
}

void SearchPanel::applyHandoff(const vedit::search::SearchHandoff &handoff)
{
    // b2-repair-r3 item 2: the Ctrl+F bar's whole-project hand-off must not
    // retarget (or abandon) a replacement that is still counting or writing.
    // Refusing keeps the operation's frozen request intact, shows why, and —
    // because nothing is cleared here — the dock returns to a usable state when
    // the operation itself reaches its terminal state.
    if (operationBusy()) {
        setReplaceStatus(tr("A replace is still running, so the Ctrl+F hand-off was not applied. "
                            "Wait for it to finish and search again."));
        return;
    }
    adoptFromHub(handoff);
    // The bar hands off when the user selects the whole-project scope; the
    // dock runs the SAME query with the SAME options (never a second search
    // implementation inside the document).
    if (scope() != vedit::search::SearchScope::WholeProject) {
        return;
    }
    startSearch();
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

SearchOptions SearchPanel::buildOptions(bool allMatches) const
{
    SearchOptions options;
    options.rootPath = rootPath_;
    options.query = query_->text().trimmed();
    options.caseSensitive = caseButton_->isChecked();
    options.wholeWords = wholeButton_->isChecked();
    options.regex = regexButton_->isChecked();
    options.allMatches = allMatches;
    options.verilogOnly = verilogButton_->isChecked();
    options.maxResults = resultLimit_;
    options.maxFileBytes = maxFileBytes_;
    options.maxDurationMs = timeBudgetMs_;
    if (scope() == vedit::search::SearchScope::CurrentFile) {
        options.fileFilter = resolveCurrentFile();
    }
    return options;
}

bool SearchPanel::beginScan(const SearchOptions &options, ReplaceScanPhase phase)
{
    // b2-repair-r3 item 2: a browse scan must never start on top of a live
    // 「全部替换」. GlobalSearch::start would cancel the counting scan and bump
    // the generation, so its `finished` would be dropped and the operation would
    // be abandoned with `operationActive_` still set — the dock would end up
    // idle with every control disabled and no owner to clear it. Refuse instead;
    // the operation keeps running and this path reports why.
    if (phase == ReplaceScanPhase::None && operationBusy()) {
        setReplaceStatus(tr("A replace is still running, so the search was not started. Wait for "
                            "it to finish and run the search again."));
        return false;
    }
    clearResults();
    lastScan_ = FrozenScan();
    // Freeze the identity of THIS scan: every hit that arrives is tagged with
    // `activeScanId_`, and the query/options used here are the ones a later
    // replacement must match.
    activeScanId_ = ++scanGeneration_;
    activeScanQuery_ = ReplaceQuery{options.query, options.caseSensitive, options.wholeWords,
                                    options.regex};
    activeScanOptions_ = options;
    scanInputChanged_ = false;
    emptyLabel_->hide();
    tree_->show();
    summaryLabel_->setText(phase != ReplaceScanPhase::None ? tr("Counting matches…")
                                                           : tr("Searching…"));
    searching_ = true;
    replaceScanPhase_ = phase;
    updateControlState();
    // The scan window opens HERE, before GlobalSearch::start launches the walk:
    // a hit file whose mtime is later than this was written after the walk
    // began, so its hits and its stamp can describe different content.
    activeScanStartedMs_ = QDateTime::currentMSecsSinceEpoch();

    // AC-1.2 evidence hook (unconditional, see setRootPath): the root actually
    // handed to the scan, plus the shared options (ux-batch-2 B2).
    qInfo("search: query root='%s' query='%s' case=%d whole=%d regex=%d all=%d filter='%s'",
          qPrintable(options.rootPath), qPrintable(options.query),
          options.caseSensitive ? 1 : 0, options.wholeWords ? 1 : 0,
          options.regex ? 1 : 0, options.allMatches ? 1 : 0,
          qPrintable(options.fileFilter));

    if (!search_.start(options)) {
        searching_ = false;
        replaceScanPhase_ = ReplaceScanPhase::None;
        operationActive_ = false;
        updateControlState();
        showEmptyState(tr("The previous search did not stop in time; try again."));
        return false;
    }
    return true;
}

void SearchPanel::startSearch()
{
    // b2-repair-r3 item 2: the dock's own Search path obeys the same rule as the
    // Ctrl+F hand-off — a live 「全部替换」 keeps ownership of the panel until it
    // reaches a terminal state.
    if (operationBusy()) {
        setReplaceStatus(tr("A replace is still running, so the search was not started. Wait for "
                            "it to finish and run the search again."));
        return;
    }
    const QString queryText = query_->text().trimmed();
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
        return;
    }
    if (queryText.isEmpty()) {
        showEmptyState(tr("Enter a query and press Search."));
        return;
    }
    const SearchOptions options = buildOptions(false);
    if (options.fileFilter.isEmpty() &&
        scope() == vedit::search::SearchScope::CurrentFile) {
        showEmptyState(
            tr("No current file. Open a document or choose Whole Project."));
        return;
    }
    publishToHub();
    beginScan(options, ReplaceScanPhase::None);
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
    // The selected row is what 「替换」 acts on, and the jumped-to file becomes
    // the active document the Current-File scope searches.
    currentHitIndex_ = hitIndex;
    vedit::search::SearchHub::instance().setCurrentFilePath(hit.filePath);
    JumpRequest request;
    request.filePath = hit.filePath;
    request.line = hit.line;
    request.column = hit.column;
    emit jumpRequested(request);
    // The ONE jump seam (AC-7.7). With no target registered this is the
    // documented NoTarget no-op; the panel never opens a file itself.
    vedit::workspace::jump::jumpTo(request.filePath, request.line, request.column);
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

QLineEdit *SearchPanel::replaceEdit() const
{
    return replaceEdit_;
}

QLabel *SearchPanel::replaceStatusLabel() const
{
    return replaceStatus_;
}

QComboBox *SearchPanel::scopeCombo() const
{
    return scopeCombo_;
}

QToolButton *SearchPanel::wholeWordButton() const
{
    return wholeButton_;
}

QToolButton *SearchPanel::regexButton() const
{
    return regexButton_;
}

GlobalSearch *SearchPanel::search()
{
    return &search_;
}

ProjectReplacer *SearchPanel::replacer()
{
    return &replacer_;
}

bool SearchPanel::isReplacing() const
{
    return replacing_;
}

ReplaceOutcome SearchPanel::lastReplaceOutcome() const
{
    return lastReplaceOutcome_;
}

int SearchPanel::currentHitIndex() const
{
    return currentHitIndex_;
}

void SearchPanel::setReplaceText(const QString &text)
{
    replaceEdit_->setText(text);
}

QString SearchPanel::replaceText() const
{
    return replaceEdit_->text();
}

void SearchPanel::setReplaceConfirmHandler(ReplaceConfirmHandler handler)
{
    confirmHandler_ = std::move(handler);
}

void SearchPanel::setFileRewriter(FileRewriter rewriter)
{
    rewriter_ = std::move(rewriter);
}

void SearchPanel::clearResults()
{
    hits_.clear();
    fileItems_.clear();
    fileHitCounts_.clear();
    currentHitIndex_ = -1;
    tree_->clear();
}

void SearchPanel::appendBatch(const QList<SearchHit> &hits)
{
    for (SearchHit hit : hits) {
        // The matcher identity: this hit can only ever be applied through the
        // snapshot of the scan that produced it.
        hit.scanId = activeScanId_;
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
    const ReplaceScanPhase phase = replaceScanPhase_;
    replaceScanPhase_ = ReplaceScanPhase::None;
    searching_ = false;
    lastSummary_ = summary;
    summaryLabel_->setText(summaryText(summary));

    if (phase != ReplaceScanPhase::None) {
        // The all-matches scan that feeds 「全部替换」. It is an internal
        // counting pass: the normal result view is restored after the replace.
        //
        // b2-repair-r2: the scan's identity was frozen in beginScan. If any
        // query/option changed while it was running, the hits no longer belong
        // to the operation and are refused BEFORE the save/confirmation path.
        if (scanInputChanged_ || operationInputChanged_ ||
            activeScanQuery_ != operationQuery_) {
            finishReplaceRefusal(
                tr("Replace All refused: the query or options changed while the counting scan "
                   "was running; nothing was replaced. Run the search and Replace All again."));
            return;
        }
        if (summary.state != SearchState::Done) {
            const QString reason = summary.state == SearchState::Failed
                                       ? summary.error
                                       : tr("the counting scan was cancelled");
            finishReplaceRefusal(tr("Replace All refused: %1").arg(reason));
            return;
        }
        if (summary.truncated) {
            finishReplaceRefusal(tr("Replace All refused: the result limit (%1) was reached; "
                                    "narrow the query first")
                                     .arg(resultLimit_));
            return;
        }
        // High 3: a scan that stopped at the time budget is a PARTIAL subset.
        // Replacing it would silently cover only the scanned prefix and present
        // that prefix's count as the total, so it is refused exactly like a
        // truncated scan (the browse path already reports the budget).
        if (summary.timedOut) {
            finishReplaceRefusal(tr("Replace All refused: the counting scan stopped at the %1 ms "
                                    "time budget, so the hit list is incomplete; narrow the query "
                                    "first")
                                     .arg(timeBudgetMs_));
            return;
        }
        // Freeze the scan identity and the per-file document stamps TOGETHER
        // with the hits; every later step uses this value, never the widgets.
        lastScan_ = makeFrozenScan(activeScanOptions_, activeScanId_, activeScanQuery_);
        // b2-repair-r3 item 1 (the scan window): a hit file written while the
        // walk was reading it makes the hit list and the stamps describe
        // different content. Refused BEFORE the save-first step and before the
        // confirmation, so nothing is written and no buffer is saved for it.
        const QString writtenDuringScan = frozenScanWindowFile(lastScan_);
        if (!writtenDuringScan.isEmpty()) {
            finishReplaceRefusal(
                tr("Replace All refused: \"%1\" was written while the counting scan was running, "
                   "so the hit positions may be stale; nothing was replaced. Run the search and "
                   "Replace All again.")
                    .arg(writtenDuringScan));
            return;
        }
        if (phase == ReplaceScanPhase::Initial) {
            finishInitialReplaceScan();
        } else {
            finishPostSaveReplaceScan();
        }
        return;
    }

    if (summary.state == SearchState::Failed) {
        showEmptyState(tr("Search failed: %1").arg(summary.error));
        updateControlState();
        emit searchFinished(0, false);
        return;
    }
    if (scanInputChanged_) {
        // The browse scan was started with a query the user has since changed;
        // its rows must not stay clickable or replaceable.
        clearResults();
        lastScan_ = FrozenScan();
        summaryLabel_->clear();
        showEmptyState(tr("The query or options changed while the search was running; run the "
                          "search again."));
        updateControlState();
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
        lastScan_ = makeFrozenScan(activeScanOptions_, activeScanId_, activeScanQuery_);
        updateControlState();
        emit searchFinished(0, summary.truncated);
        return;
    }
    // The browse scan's immutable identity, captured with its hits; a single
    // 「替换」 uses THIS and refuses if the live query/document moved on.
    lastScan_ = makeFrozenScan(activeScanOptions_, activeScanId_, activeScanQuery_);
    tree_->show();
    emptyLabel_->hide();
    // After a single 「替换」 the refreshed list is shown and the selection
    // advances to the match that now occupies the replaced position.
    if (advanceAfterRefresh_ >= 0) {
        const int target = std::min(advanceAfterRefresh_, static_cast<int>(hits_.size()) - 1);
        advanceAfterRefresh_ = -1;
        if (target >= 0) {
            activateHit(target);
        }
    }
    updateControlState();
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

// --- replace (ux-batch-2 B2) ----------------------------------------------

void SearchPanel::setReplaceStatus(const QString &text)
{
    replaceStatus_->setText(text);
    replaceStatus_->setVisible(!text.isEmpty());
}

void SearchPanel::setBusy(bool busy)
{
    Q_UNUSED(busy);
    updateControlState();
}

// Item 3: the query field and every option toggle stay disabled for the whole
// counting scan, the confirmation and the write — not just the Search button.
// `operationActive_` covers the gaps between the two counting scans and the
// confirmation, when neither `searching_` nor `replacing_` is set.
void SearchPanel::updateControlState()
{
    const bool idle = !searching_ && !replacing_ && !operationActive_;
    query_->setEnabled(idle);
    caseButton_->setEnabled(idle);
    wholeButton_->setEnabled(idle);
    regexButton_->setEnabled(idle);
    verilogButton_->setEnabled(idle);
    scopeCombo_->setEnabled(idle);
    searchButton_->setEnabled(idle);
    replaceButton_->setEnabled(idle);
    replaceAllButton_->setEnabled(idle);
    replaceEdit_->setEnabled(idle);
    cancelButton_->setVisible(searching_);
}

// Item 4: a query/option change invalidates the rows the user could otherwise
// replace. While a scan or an operation is in flight it is recorded and the
// operation refuses at the next safe point (the widgets are disabled, so this
// is the programmatic/hub-change backstop).
void SearchPanel::onQueryControlsChanged()
{
    if (replacing_ || operationActive_ || replaceScanPhase_ != ReplaceScanPhase::None) {
        operationInputChanged_ = true;
        if (replaceScanPhase_ != ReplaceScanPhase::None) {
            // Stop the now-meaningless counting scan instead of letting it run
            // to completion; onSearchFinished still refuses the operation.
            search_.cancel();
        }
    } else if (searching_) {
        scanInputChanged_ = true;
        search_.cancel();
    } else {
        invalidateResults(tr("The query or options changed; run the search again."));
    }
    publishToHub();
}

void SearchPanel::invalidateResults(const QString &reason)
{
    clearResults();
    lastScan_ = FrozenScan();
    summaryLabel_->clear();
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
    } else if (query_->text().trimmed().isEmpty()) {
        showEmptyState(tr("Enter a query and press Search."));
    } else {
        showEmptyState(reason);
    }
}

QStringList SearchPanel::hitFiles() const
{
    QStringList files;
    for (const SearchHit &hit : hits_) {
        const QString absolute = QFileInfo(hit.filePath).absoluteFilePath();
        if (!files.contains(absolute)) {
            files.append(absolute);
        }
    }
    files.sort();
    return files;
}

void SearchPanel::captureReloadTokens(const QStringList &files)
{
    reloadTokens_.clear();
    OpenDocumentBridge *bridge = openDocumentBridge();
    if (bridge == nullptr) {
        return;
    }
    for (const QString &file : files) {
        const QString absolute = QFileInfo(file).absoluteFilePath();
        if (bridge->isOpen(absolute)) {
            reloadTokens_.insert(absolute, bridge->contentToken(absolute));
        }
    }
}

ReplaceFileStamp SearchPanel::captureFileStamp(const QString &file, qint64 scanStartedMs) const
{
    ReplaceFileStamp stamp;
    const QFileInfo info(file);
    if (!info.exists()) {
        return stamp;
    }
    stamp.size = info.size();
    stamp.lastModifiedMs = info.lastModified().toMSecsSinceEpoch();
    // Strictly AFTER the scan started and not in the future: a file written in
    // the same millisecond as the scan start is not refused (a test writes its
    // fixture and searches in the same millisecond), and a file whose mtime is
    // ahead of the clock is not treated as "written during the scan" forever
    // (the per-file stamp still guards it). The reviewer's probes save at
    // hitsReady — well after the walk began. The per-hit line fingerprint is the
    // backstop for the sub-millisecond window this can miss.
    stamp.writtenDuringScan = scanStartedMs > 0 && stamp.lastModifiedMs > scanStartedMs &&
                              stamp.lastModifiedMs <= QDateTime::currentMSecsSinceEpoch();
    return stamp;
}

// The first hit file whose stamp says it was written during the scan, as a
// user-facing label (the basename, like every other replace message), or an
// empty string when the scan window is clean.
QString SearchPanel::frozenScanWindowFile(const FrozenScan &frozen) const
{
    // Sorted, so a message that names one file is deterministic.
    QStringList files = frozen.files.keys();
    files.sort();
    for (const QString &file : files) {
        if (frozen.files.value(file).writtenDuringScan) {
            return QFileInfo(file).fileName();
        }
    }
    return QString();
}

bool SearchPanel::operationBusy() const
{
    return operationActive_ || replacing_ || replacer_.isRunning() ||
           replaceScanPhase_ != ReplaceScanPhase::None;
}

bool SearchPanel::bufferMayContainQuery(const QString &text, const ReplaceQuery &query) const
{
    if (query.pattern.isEmpty()) {
        return false;
    }
    if (query.regex) {
        // The project matcher is the Rust regex engine; without a second
        // in-memory matcher a regex cannot be pre-filtered, so the document is
        // saved and re-scanned rather than skipped.
        return true;
    }
    for (const QChar character : query.pattern) {
        if (character.unicode() > 0x7f) {
            // Case folding outside ASCII is engine-specific; save and re-scan
            // instead of risking a false negative.
            return true;
        }
    }
    return text.contains(query.pattern,
                         query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
}

QStringList SearchPanel::bufferOnlyModifiedCandidates(const QStringList &scanFiles) const
{
    QStringList candidates;
    OpenDocumentBridge *bridge = openDocumentBridge();
    if (bridge == nullptr) {
        return candidates;
    }
    const QString filter =
        operationOptions_.fileFilter.isEmpty()
            ? QString()
            : QFileInfo(operationOptions_.fileFilter).absoluteFilePath();
    QString rootPrefix = rootPath_;
    if (!rootPrefix.endsWith(QLatin1Char('/'))) {
        rootPrefix += QLatin1Char('/');
    }
    const QStringList modified = bridge->modifiedPaths();
    for (const QString &file : modified) {
        const QString absolute = QFileInfo(file).absoluteFilePath();
        if (scanFiles.contains(absolute)) {
            continue; // already in the disk scan; saved by the affected set
        }
        if (!filter.isEmpty()) {
            if (absolute != filter) {
                continue;
            }
        } else if (!absolute.startsWith(rootPrefix)) {
            continue; // outside the project root: not part of this scan
        }
        if (operationOptions_.verilogOnly && !isVerilogFileName(absolute)) {
            continue;
        }
        if (bufferMayContainQuery(bridge->bufferText(absolute), operationQuery_)) {
            candidates.append(absolute);
        }
    }
    candidates.sort();
    return candidates;
}

SearchPanel::FrozenScan SearchPanel::makeFrozenScan(const SearchOptions &options, quint64 scanId,
                                                    const ReplaceQuery &query) const
{
    FrozenScan frozen;
    frozen.query = query;
    frozen.options = options;
    frozen.scanId = scanId;
    // The identity covers the WHOLE scan window (b2-repair-r3): the start time
    // is part of the frozen value, so a hit file written after it is refused
    // even when its late-captured size/mtime would agree with the writer.
    frozen.scanStartedMs = activeScanStartedMs_;
    frozen.valid = scanId != 0;
    for (const QString &file : hitFiles()) {
        frozen.files.insert(file, captureFileStamp(file, activeScanStartedMs_));
    }
    return frozen;
}

ReplaceQuery SearchPanel::liveReplaceQuery() const
{
    ReplaceQuery query;
    query.pattern = query_->text().trimmed();
    query.caseSensitive = caseButton_->isChecked();
    query.wholeWords = wholeButton_->isChecked();
    query.regex = regexButton_->isChecked();
    return query;
}

bool SearchPanel::sameLiveIdentity(const FrozenScan &frozen, QString *error) const
{
    if (!frozen.valid || frozen.scanId == 0) {
        *error = tr("the result list has no frozen scan identity; run the search again before "
                    "replacing");
        return false;
    }
    if (frozen.query.pattern.isEmpty()) {
        *error = tr("the operation has no query, so the ranges cannot be verified; refusing to "
                    "write without verification");
        return false;
    }
    const ReplaceQuery live = liveReplaceQuery();
    if (live.pattern.isEmpty()) {
        *error = tr("the query is empty, so the ranges cannot be verified; run the search again "
                    "before replacing");
        return false;
    }
    if (live != frozen.query) {
        *error = tr("the query or match options changed since the scan; run the search again "
                    "before replacing");
        return false;
    }
    const SearchOptions liveOptions = buildOptions(frozen.options.allMatches);
    if (liveOptions.rootPath != frozen.options.rootPath ||
        liveOptions.query != frozen.options.query ||
        liveOptions.caseSensitive != frozen.options.caseSensitive ||
        liveOptions.wholeWords != frozen.options.wholeWords ||
        liveOptions.regex != frozen.options.regex ||
        liveOptions.verilogOnly != frozen.options.verilogOnly ||
        liveOptions.fileFilter != frozen.options.fileFilter) {
        *error = tr("the search scope or options changed since the scan; run the search again "
                    "before replacing");
        return false;
    }
    return true;
}

QString SearchPanel::savedDocumentsSuffix() const
{
    if (savedBeforeReplace_.isEmpty()) {
        return tr(" Nothing was replaced.");
    }
    return tr(" %1 document(s) were already saved and remain on disk; nothing was replaced.")
        .arg(savedBeforeReplace_.size());
}

void SearchPanel::finishReplaceRefusal(const QString &message)
{
    ReplaceOutcome refused;
    refused.error = message + savedDocumentsSuffix();
    lastReplaceOutcome_ = refused;
    setReplaceStatus(refused.error);
    reloadTokens_.clear();
    savedBeforeReplace_.clear();
    operationActive_ = false;
    operationInputChanged_ = false;
    updateControlState();
    emit replaceFinished(refused);
    startSearch(); // restore the normal browse view
}

bool SearchPanel::ensureBuffersSaved(const QStringList &files, QStringList *saved,
                                     QString *error)
{
    if (files.isEmpty()) {
        return true;
    }
    OpenDocumentBridge *bridge = openDocumentBridge();
    if (bridge == nullptr) {
        *error = tr("the editor bridge is unavailable, so the unsaved buffer state cannot be "
                    "checked");
        return false;
    }
    for (const QString &file : files) {
        if (!bridge->isOpen(file)) {
            continue;
        }
        if (!bridge->save(file, error)) {
            // Medium 6: the failing file is named, not just the reason.
            *error = tr("\"%1\": %2").arg(QFileInfo(file).fileName(), *error);
            return false;
        }
        saved->append(file);
    }
    return true;
}

bool SearchPanel::replaceAllRequested()
{
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
        return false;
    }
    if (query_->text().trimmed().isEmpty()) {
        showEmptyState(tr("Enter a query and press Search."));
        return false;
    }
    if (operationBusy()) {
        // Also covers the gap between the two counting scans and the
        // confirmation: a second 「全部替换」 must not take ownership of the
        // first one's operation state (b2-repair-r3 item 2).
        return false;
    }
    if (scope() == vedit::search::SearchScope::CurrentFile &&
        resolveCurrentFile().isEmpty()) {
        showEmptyState(tr("No current file. Open a document or choose Whole Project."));
        return false;
    }

    // Medium 9: the save-first step happens AFTER the first counting scan, so
    // only the modified documents the replace will actually touch are saved —
    // not every dirty buffer in the project. The scan is repeated on the saved
    // content, and the confirmation shows those counts.
    savedBeforeReplace_.clear();
    reloadTokens_.clear();
    // b2-repair-r2 item 1: freeze the operation's query + the EXACT scan
    // request (root, scope, file filter, limits) before the first scan. Both
    // counting scans use this value and every later step compares the live
    // widgets against it; the write side never reads the widgets.
    const SearchOptions options = buildOptions(true);
    operationActive_ = true;
    operationInputChanged_ = false;
    operationQuery_ = ReplaceQuery{options.query, options.caseSensitive, options.wholeWords,
                                   options.regex};
    operationOptions_ = options;
    if (!beginScan(options, ReplaceScanPhase::Initial)) {
        operationActive_ = false;
        updateControlState();
        return false;
    }
    return true;
}

void SearchPanel::finishInitialReplaceScan()
{
    const QStringList files = hitFiles();
    OpenDocumentBridge *bridge = openDocumentBridge();
    if (bridge == nullptr) {
        // b2-repair-r2 residual: a null bridge used to be treated as "no
        // affected documents" and the operation proceeded without the
        // unsaved-buffer rule. Refuse instead of silently skipping it.
        finishReplaceRefusal(tr("Replace All refused: the editor bridge is unavailable, so the "
                                "unsaved-buffer state cannot be checked."));
        return;
    }
    QStringList affected;
    const QStringList modified = bridge->modifiedPaths();
    for (const QString &file : files) {
        if (modified.contains(file)) {
            affected.append(file);
        }
    }
    // Residual closure (b2-repair-r2): a modified open document whose disk copy
    // has no hit can still contain a match that exists only in the buffer. It
    // is saved first, so the post-save re-scan sees it — never silently
    // skipped. `bufferMayContainQuery` is a conservative prefilter (exact for
    // ASCII literals; save-and-rescan for regex/non-ASCII).
    for (const QString &candidate : bufferOnlyModifiedCandidates(files)) {
        if (!affected.contains(candidate)) {
            affected.append(candidate);
        }
    }
    affected.sort();

    if (affected.isEmpty()) {
        captureReloadTokens(files);
        confirmAndReplace();
        return;
    }

    QStringList saved;
    QString error;
    if (!ensureBuffersSaved(affected, &saved, &error)) {
        // Accurate wording (Medium 9): saves run one by one, so a later failure
        // does NOT undo the documents already saved; only the replacement never
        // starts.
        QString message =
            tr("Replace All refused: an unsaved editor document could not be saved: %1")
                .arg(error);
        if (!saved.isEmpty()) {
            message += QLatin1Char(' ') +
                       tr("The %1 document(s) already saved remain on disk; nothing was "
                          "replaced.")
                           .arg(saved.size());
        } else {
            message += QLatin1Char(' ') + tr("Nothing was replaced.");
        }
        ReplaceOutcome refused;
        refused.error = message;
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        savedBeforeReplace_.clear();
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
        startSearch();
        return;
    }
    savedBeforeReplace_ = saved;
    captureReloadTokens(affected);
    // Re-count on the saved content: the first scan saw the disk, which the
    // user's buffers may just have changed. The FROZEN request is reused —
    // never buildOptions() again, which could re-resolve CurrentFile and
    // silently widen the operation to the whole project.
    if (!beginScan(operationOptions_, ReplaceScanPhase::PostSave)) {
        savedBeforeReplace_.clear();
        reloadTokens_.clear();
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
    }
}

void SearchPanel::finishPostSaveReplaceScan()
{
    // The tokens were captured right after the saves (before this scan), so a
    // buffer edited while the re-count runs is detected at reload time.
    confirmAndReplace();
}

bool SearchPanel::replaceCurrentHit()
{
    if (rootPath_.isEmpty()) {
        showEmptyState(tr("No project root. Open a project to search its files."));
        return false;
    }
    if (operationBusy()) {
        return false;
    }
    if (hits_.isEmpty()) {
        setReplaceStatus(tr("No match to replace; run the search first."));
        return false;
    }
    // b2-repair-r2 item 2/4: the rows belong to the last scan; if the live
    // query/options (or the current-file scope) moved on, refuse instead of
    // applying a row produced by an older request.
    QString identityError;
    if (!sameLiveIdentity(lastScan_, &identityError)) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: %1").arg(identityError);
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        invalidateResults(tr("The query or options changed; run the search again."));
        return false;
    }
    // Medium 7: the SELECTED row is the target. Arrow-key navigation changes
    // the tree's current item without an itemClicked, so the tree wins over the
    // last clicked index.
    int index = currentHitIndex_;
    if (tree_ != nullptr && tree_->currentItem() != nullptr) {
        const QVariant selected = tree_->currentItem()->data(0, Qt::UserRole);
        if (selected.isValid()) {
            index = selected.toInt();
        }
    }
    if (index < 0 || index >= hits_.size()) {
        index = 0;
    }
    const SearchHit hit = hits_.at(index);
    if (hit.scanId != lastScan_.scanId) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: this row belongs to an older search; run the "
                           "search again before replacing.");
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        invalidateResults(tr("The result list is out of date; run the search again."));
        return false;
    }
    if (hit.matchLength <= 0) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: this match reports no replaceable range.");
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        return false;
    }

    // --- b2-repair-r3: the DOCUMENT identity, checked before anything is saved
    //
    // The save-first step below writes a modified buffer. That write is the
    // panel's own, so checking the frozen stamps afterwards would blame the
    // document for the panel's save (Medium/probe N3) and, worse, could let a
    // buffer whose edit moved the match be written to disk before the refusal.
    // Everything that can be known before the save is checked here.
    const QString absolute = QFileInfo(hit.filePath).absoluteFilePath();
    const auto stampEntry = lastScan_.files.constFind(absolute);
    if (stampEntry == lastScan_.files.constEnd()) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: \"%1\" is not covered by the frozen scan snapshot; "
                           "nothing was written (run the search again).")
                            .arg(QFileInfo(absolute).fileName());
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        return false;
    }
    const ReplaceFileStamp &frozenStamp = stampEntry.value();
    if (frozenStamp.writtenDuringScan) {
        // The scan window (item 1): the file was written while the walk read it,
        // so this hit's line/column describes the pre-save content.
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: \"%1\" was written while the search was running, so "
                           "this match may have moved; nothing was written. Run the search "
                           "again.")
                            .arg(QFileInfo(absolute).fileName());
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        return true; // handled: the refusal is the outcome of this request
    }
    OpenDocumentBridge *hitBridge = openDocumentBridge();
    const bool bufferModified =
        hitBridge != nullptr && hitBridge->modifiedPaths().contains(absolute);
    if (bufferModified) {
        // The save-first step below writes this buffer. Check the disk stamp
        // BEFORE that write: a change since the scan freeze is not ours, and the
        // save would overwrite it and hide that the coordinates belong to older
        // text. (With no modified buffer there is nothing to save, so the
        // worker's own stamp check keeps handling a change after the scan.)
        const ReplaceFileStamp diskStamp = captureFileStamp(absolute, 0);
        if (diskStamp.size != frozenStamp.size ||
            diskStamp.lastModifiedMs != frozenStamp.lastModifiedMs) {
            ReplaceOutcome refused;
            refused.error = tr("Replace refused: \"%1\" changed after the search; nothing was "
                               "written. Run the search again.")
                                .arg(QFileInfo(absolute).fileName());
            lastReplaceOutcome_ = refused;
            setReplaceStatus(refused.error);
            emit replaceFinished(refused);
            return true;
        }
        // And check the SELECTED line in the BUFFER, before it is written: if the
        // user's own edit moved the match, saving it would put text on disk that
        // the stale coordinates must not be applied to.
        const QString bufferText = hitBridge->bufferText(absolute);
        if (!hit.lineFingerprint.isEmpty()) {
            const QStringList bufferLines =
                bufferText.isEmpty() ? QStringList() : bufferText.split(QLatin1Char('\n'));
            const QString bufferLine = hit.line >= 1 && hit.line <= bufferLines.size()
                                           ? bufferLines.at(hit.line - 1)
                                           : QString();
            if (!bufferText.isEmpty() &&
                matchedLineFingerprint(bufferLine) != hit.lineFingerprint) {
                ReplaceOutcome refused;
                refused.error =
                    tr("Replace refused: the unsaved edits in \"%1\" moved line %2, so this "
                       "match is no longer where the search found it; the document was not saved "
                       "and nothing was replaced. Save it and run the search again.")
                        .arg(QFileInfo(absolute).fileName())
                        .arg(hit.line);
                lastReplaceOutcome_ = refused;
                setReplaceStatus(refused.error);
                emit replaceFinished(refused);
                return true;
            }
            // b2-repair-r4 (review P5): the fingerprint above covers only the
            // scanner's TRIMMED 400-CHARACTER PREVIEW. Beyond that cap it cannot
            // tell the scanned line from a line identical in its first 400
            // characters, so a dirty buffer that pastes such a line above the hit
            // used to have its own text saved and THEN get the replacement written
            // into the inserted line. For a capped preview the position is now
            // proven with the WHOLE line the scan read — the per-file stamp checked
            // above says the file still holds what the matcher saw — before anything
            // is saved. No buffer text means no proof, so that is refused too.
            if (matchedLinePreviewTruncated(hit.text)) {
                const QString scannedLine =
                    bufferText.isEmpty()
                        ? QString()
                        : readDocumentLine(absolute, hit.line, nullptr);
                const bool sameLine =
                    !bufferText.isEmpty() && !scannedLine.isNull() &&
                    normalizedLineIdentity(bufferLine) == normalizedLineIdentity(scannedLine);
                if (!sameLine) {
                    ReplaceOutcome refused;
                    refused.error =
                        bufferText.isEmpty()
                            ? tr("Replace refused: the unsaved edits in \"%1\" cannot be checked "
                                 "against line %2 (the editor did not provide the buffer text), "
                                 "and that line is longer than the 400-character search preview, "
                                 "so the match cannot be re-identified; nothing was replaced. Save "
                                 "the document and run the search again.")
                                  .arg(QFileInfo(absolute).fileName())
                                  .arg(hit.line)
                            : tr("Replace refused: the unsaved edits in \"%1\" changed where line "
                                 "%2 sits in the buffer, and that line is longer than the "
                                 "400-character search preview, so the match cannot be "
                                 "re-identified from the preview alone; the document was not saved "
                                 "and nothing was replaced. Save it and run the search again.")
                                  .arg(QFileInfo(absolute).fileName())
                                  .arg(hit.line);
                    lastReplaceOutcome_ = refused;
                    setReplaceStatus(refused.error);
                    emit replaceFinished(refused);
                    return true;
                }
            }
        }
    }

    QStringList saved;
    QString error;
    if (!ensureBuffersSaved(QStringList{hit.filePath}, &saved, &error)) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: \"%1\" has unsaved changes that could not be "
                           "saved: %2")
                            .arg(hit.filePath, error);
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        return false;
    }
    if (bufferModified) {
        // The panel just wrote the buffer to disk; that write is the content the
        // replacement applies to, so its stamp replaces the pre-save one. The
        // per-hit line fingerprint still refuses a moved target inside the
        // saved content.
        lastScan_.files.insert(absolute, captureFileStamp(absolute, 0));
    }
    // Blocker 1: the token of the content the replacement is applied to. The
    // reload after the write compares the buffer against it.
    captureReloadTokens(QStringList{hit.filePath});

    QList<ReplaceHit> targets;
    targets.append(ReplaceHit{hit.filePath, hit.line, hit.column, hit.matchLength, hit.scanId,
                              hit.lineFingerprint});
    // After the write the list is refreshed; the match that now sits at the
    // replaced index is the "next" one.
    return startReplace(targets, replaceEdit_->text(), index, lastScan_);
}

void SearchPanel::confirmAndReplace()
{
    // The scan that produced `hits_` is the operation's identity; copy it
    // BEFORE the confirmation dialog, because the dialog is a re-entrancy
    // point where the query/options can change.
    const FrozenScan frozen = lastScan_;
    if (hits_.isEmpty() || !frozen.valid) {
        finishReplaceRefusal(tr("Replace All: no match to replace."));
        return;
    }
    ReplacePlan plan;
    QStringList files;
    for (const SearchHit &hit : hits_) {
        if (!files.contains(hit.filePath)) {
            files.append(hit.filePath);
        }
    }
    files.sort();
    plan.files = files;
    plan.fileCount = files.size();
    plan.hitCount = hits_.size();

    QList<ReplaceHit> targets;
    targets.reserve(hits_.size());
    for (const SearchHit &hit : hits_) {
        targets.append(ReplaceHit{hit.filePath, hit.line, hit.column, hit.matchLength,
                                  hit.scanId, hit.lineFingerprint});
    }

    if (!confirmReplace(plan, savedBeforeReplace_)) {
        QString message = tr("Replace All cancelled;");
        if (savedBeforeReplace_.isEmpty()) {
            message += tr(" nothing was written.");
        } else {
            // Residual (Medium 9 wording): the save-first step already wrote
            // the affected buffers, so "nothing was written" would contradict
            // the disk. Say exactly what happened.
            message += tr(" %1 document(s) were already saved and remain on disk; nothing was "
                          "replaced.")
                           .arg(savedBeforeReplace_.size());
        }
        ReplaceOutcome cancelled;
        cancelled.cancelled = true;
        cancelled.error = message;
        lastReplaceOutcome_ = cancelled;
        setReplaceStatus(message);
        emit replaceFinished(cancelled);
        savedBeforeReplace_.clear();
        reloadTokens_.clear();
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
        startSearch(); // restore the normal (one row per line) result view
        return;
    }

    // b2-repair-r2 item 2: after the confirmation (which can run arbitrary
    // user code) the live query/options must still equal the frozen scan.
    // Otherwise the old ranges are refused instead of being applied with a
    // different validator.
    QString identityError;
    if (operationInputChanged_ || !sameLiveIdentity(frozen, &identityError)) {
        const QString reason =
            identityError.isEmpty()
                ? tr("the query or options changed while the confirmation was open")
                : identityError;
        finishReplaceRefusal(
            tr("Replace All refused: %1; nothing was replaced. Run the search and Replace All "
               "again.")
                .arg(reason));
        return;
    }

    savedBeforeReplace_.clear();
    startReplace(targets, replaceEdit_->text(), -1, frozen);
}

bool SearchPanel::confirmReplace(const ReplacePlan &plan, const QStringList &savedDocuments)
{
    if (confirmHandler_) {
        return confirmHandler_(plan, savedDocuments);
    }
    QString text = tr("Replace \"%1\" with \"%2\"?\n\n%3 occurrence(s) in %4 file(s) will be "
                      "replaced.")
                       .arg(query_->text().trimmed(), replaceEdit_->text())
                       .arg(plan.hitCount)
                       .arg(plan.fileCount);
    if (!savedDocuments.isEmpty()) {
        // Medium 9: only the affected documents were saved, and they are named
        // so the user knows exactly which buffers were written.
        QStringList names;
        for (const QString &file : savedDocuments) {
            names.append(QFileInfo(file).fileName());
        }
        text += tr("\n\n%1 affected unsaved editor document(s) were saved first: %2")
                    .arg(savedDocuments.size())
                    .arg(names.join(QStringLiteral(", ")));
    }
    QMessageBox box(QMessageBox::Warning, tr("Replace All"), text,
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDetailedText(plan.files.join(QLatin1Char('\n')));
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

bool SearchPanel::startReplace(const QList<ReplaceHit> &hits, const QString &replacement,
                               int advanceTo, const FrozenScan &frozen)
{
    if (hits.isEmpty() || replacing_ || replacer_.isRunning()) {
        return false;
    }
    if (frozen.query.pattern.isEmpty()) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: the operation has no query, so the ranges cannot be "
                           "verified; nothing was written.");
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
        return false;
    }
    QString identityError;
    if (!sameLiveIdentity(frozen, &identityError)) {
        ReplaceOutcome refused;
        refused.error = tr("Replace refused: %1; nothing was written. Run the search again.")
                            .arg(identityError);
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
        return false;
    }
    for (const ReplaceHit &hit : hits) {
        if (hit.scanId != frozen.scanId) {
            ReplaceOutcome refused;
            refused.error = tr("Replace refused: a hit does not belong to the scan being "
                               "replaced; nothing was written.");
            lastReplaceOutcome_ = refused;
            setReplaceStatus(refused.error);
            emit replaceFinished(refused);
            operationActive_ = false;
            operationInputChanged_ = false;
            updateControlState();
            return false;
        }
    }
    FileRewriter writer = rewriter_;
    if (!writer) {
        writer = atomicSaveRewriter();
    }
    if (!writer) {
        ReplaceOutcome refused;
        refused.error = tr("Replace is unavailable: this build does not link the atomic save "
                           "path.");
        lastReplaceOutcome_ = refused;
        setReplaceStatus(refused.error);
        emit replaceFinished(refused);
        operationActive_ = false;
        operationInputChanged_ = false;
        updateControlState();
        return false;
    }
    replacing_ = true;
    advanceAfterRefresh_ = advanceTo;
    setBusy(true);
    summaryLabel_->setText(tr("Replacing…"));
    setReplaceStatus(tr("Replacing %1 occurrence(s)…").arg(hits.size()));
    // High 2 (b2-repair-r2): the FROZEN snapshot travels with the ranges; the
    // worker builds its RangeMatcher from that snapshot only and never reads
    // the query field or the option buttons.
    const ReplaceSnapshot snapshot{frozen.query, frozen.scanId, frozen.files};
    if (!replacer_.start(hits, replacement, writer, snapshot)) {
        replacing_ = false;
        advanceAfterRefresh_ = -1;
        operationActive_ = false;
        operationInputChanged_ = false;
        setBusy(false);
        return false;
    }
    return true;
}

void SearchPanel::onReplaceFinished(const ReplaceOutcome &outcome)
{
    replacing_ = false;
    operationActive_ = false;
    operationInputChanged_ = false;
    scanInputChanged_ = false;
    updateControlState();

    // Blocker 1: the files were rewritten under any open editor, but a reload
    // must only happen while the buffer still holds the content that was
    // written. `refresh()` owns the guard (content token captured at save time
    // + the modified check); a buffer that changed meanwhile is left untouched
    // and named to the user.
    ReplaceOutcome result = outcome;
    result.notReloadedFiles.clear();
    OpenDocumentBridge *bridge = openDocumentBridge();
    if (bridge != nullptr) {
        for (const QString &file : result.writtenFiles) {
            const QString absolute = QFileInfo(file).absoluteFilePath();
            QString error;
            const RefreshStatus status =
                bridge->refresh(absolute, reloadTokens_.value(absolute), &error);
            if (status == RefreshStatus::Changed) {
                result.notReloadedFiles.append(absolute);
            }
        }
    }
    reloadTokens_.clear();
    lastReplaceOutcome_ = result;

    if (result.ok) {
        QString text = tr("Replaced %1 occurrence(s) in %2 file(s).")
                           .arg(result.replacements)
                           .arg(result.filesChanged);
        if (!result.skippedFiles.isEmpty()) {
            text += QLatin1Char(' ') +
                    tr("%1 file(s) skipped (not valid UTF-8 or unreadable).")
                        .arg(result.skippedFiles.size());
        }
        setReplaceStatus(text);
    } else if (result.cancelled) {
        setReplaceStatus(tr("Replace cancelled; %1 occurrence(s) in %2 file(s) were written.")
                             .arg(result.replacements)
                             .arg(result.filesChanged));
    } else if (result.filesChanged > 0) {
        // Medium 6: a mid-batch failure still reports what was already written,
        // so the user knows the tree is half-replaced.
        setReplaceStatus(tr("Replace failed after writing %1 occurrence(s) in %2 file(s): %3")
                             .arg(result.replacements)
                             .arg(result.filesChanged)
                             .arg(result.error));
    } else {
        setReplaceStatus(tr("Replace failed: %1").arg(result.error));
    }

    if (!result.notReloadedFiles.isEmpty()) {
        QStringList names;
        for (const QString &file : result.notReloadedFiles) {
            names.append(QFileInfo(file).fileName());
        }
        setReplaceStatus(
            replaceStatus_->text() + QLatin1Char('\n') +
            tr("%1 file(s) changed during replace — not reloaded: %2")
                .arg(result.notReloadedFiles.size())
                .arg(names.join(QStringLiteral(", "))));
    }

    emit replaceFinished(result);
    // Refresh the results (the normal one-row-per-line query) so the tree
    // matches the new disk content.
    startSearch();
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
