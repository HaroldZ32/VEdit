#pragma once

// Search panel (workspace lane, task T23 / acceptance A2c).
//
// The content of the shell's "search" dock. It owns the query field, the
// result tree (file + line + snippet) and the empty states, and delegates the
// actual scan to GlobalSearch (worker thread, cancellable).
//
// Empty states are real and distinguishable, as the acceptance requires:
//   * no project root            -> "No project root. Open a project ..."
//   * empty query                -> "Enter a query and press Search."
//   * no results                 -> "No results. Nothing matched ..."
//   * unreadable root / failure  -> "Search failed: search root is not readable: ..."
//   * cancelled before any hit   -> "Search cancelled before any hit."
//
// Jumping: clicking a result emits jumpRequested() and forwards the request to
// the IJumpTarget installed on the panel. In the product installSearch() installs
// EditorJumpTarget (search_jump_target.h), which opens the hit through the
// workspace open seam and puts the cursor on the hit line; the edit-core lane
// replaces that implementation, not this call site.
#include "global_search.h"
#include "search_types.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vedit::workspace::search {

class SearchPanel final : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);
    ~SearchPanel() override;

    SearchPanel(const SearchPanel &) = delete;
    SearchPanel &operator=(const SearchPanel &) = delete;

    void setRootPath(const QString &rootPath);
    QString rootPath() const;

    void setJumpTarget(IJumpTarget *target);
    IJumpTarget *jumpTarget() const;

    void setQuery(const QString &query);
    QString query() const;
    void setCaseSensitive(bool on);
    bool caseSensitive() const;
    void setVerilogOnly(bool on);
    bool verilogOnly() const;

    // Limits are configuration, not test hooks: the settings lane will own
    // their persisted values.
    void setResultLimit(int limit);
    int resultLimit() const;
    void setMaxFileBytes(qint64 bytes);
    qint64 maxFileBytes() const;
    void setTimeBudgetMs(qint64 ms);
    qint64 timeBudgetMs() const;

    void startSearch();
    void cancelSearch();
    bool isSearching() const;
    SearchSummary lastSummary() const;

    // Same code path as clicking a result; returns false for an unknown index.
    bool activateHit(int hitIndex);

    const QList<SearchHit> &hits() const;
    QTreeWidget *resultsView() const;
    QLabel *emptyStateLabel() const;
    QLabel *summaryLabel() const;
    QLabel *rootLabel() const;
    QLineEdit *queryEdit() const;
    GlobalSearch *search();

signals:
    void jumpRequested(const vedit::workspace::search::JumpRequest &request);
    void searchFinished(int hitCount, bool truncated);

private:
    void buildUi();
    void connectSignals();
    void clearResults();
    void appendBatch(const QList<SearchHit> &hits);
    void onSearchFinished(const SearchSummary &summary);
    void activateItem(QTreeWidgetItem *item);
    void showEmptyState(const QString &text);
    void updateRootLabel();
    QString summaryText(const SearchSummary &summary) const;

    GlobalSearch search_;

    QLineEdit *query_ = nullptr;
    QToolButton *caseButton_ = nullptr;
    QToolButton *verilogButton_ = nullptr;
    QPushButton *searchButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLabel *rootLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QTreeWidget *tree_ = nullptr;

    QList<SearchHit> hits_;
    QHash<QString, QTreeWidgetItem *> fileItems_;
    QHash<QString, int> fileHitCounts_;
    QString rootPath_;
    IJumpTarget *jumpTarget_ = nullptr;
    SearchSummary lastSummary_;
    bool searching_ = false;
    int resultLimit_ = 2000;
    qint64 maxFileBytes_ = 8LL * 1024 * 1024;
    qint64 timeBudgetMs_ = 15000;
};

// Factory used by the app-shell seam (PanelRegistry::setFactory("search", ...)):
// a panel with the search root already applied. A free function on purpose: the
// panel contract stays usable without src/app, and the lane entry point
// (search_integration.cpp) is the only place that knows about the shell.
QWidget *createSearchPanel(const QString &rootPath);

} // namespace vedit::workspace::search
