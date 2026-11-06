#pragma once

// Search panel (workspace lane, task T23 / acceptance A2c; project-wide replace,
// ux-batch-2 B2).
//
// The content of the shell's "search" dock. It owns the query field, the shared
// option set (case / whole word / regex / scope — the SAME model as the Ctrl+F
// bar, src/workspace/search/search_query.h), the replace row and the result
// tree (file + line + snippet), and delegates the actual scan to GlobalSearch
// (worker thread, cancellable) and the writes to ProjectReplacer (worker
// thread, one atomic write per file).
//
// Empty states are real and distinguishable, as the acceptance requires:
//   * no project root            -> "No project root. Open a project ..."
//   * empty query                -> "Enter a query and press Search."
//   * no results                 -> "No results. Nothing matched ..."
//   * unreadable root / failure  -> "Search failed: search root is not readable: ..."
//   * cancelled before any hit   -> "Search cancelled before any hit."
//
// Jumping: clicking a result emits jumpRequested() and forwards the request to
// the ONE application-wide jump seam (vedit::workspace::jump::jumpTo, AC-7.7).
// The product target is registered by installSearch() (search_jump_target.h,
// EditorJumpTarget); a standalone lane build with no target gets the seam's
// documented NoTarget no-op instead of a second jump implementation (A-1/T20).
//
// Replacement is TWO actions, per the user decision (batch 2, decision 6):
//   * 「替换」 (replaceCurrentHit) replaces exactly the SELECTED hit, then
//     advances to the next one;
//   * 「全部替换」 (replaceAllRequested) runs an all-matches counting scan to
//     find the files it will touch, saves only their modified buffers, re-scans
//     the saved content, asks for confirmation with the real file/hit counts,
//     then applies every range grouped per file (one atomic write per file,
//     each range re-validated against the query) and refreshes the results.
//     A buffer edited after its save is never reloaded over the newer text.
// Nothing in this class walks a directory or matches a line on the GUI thread.
#include "document_sync.h"
#include "global_search.h"
#include "project_replace.h"
#include "search_query.h"
#include "search_types.h"

#include <QHash>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vedit::workspace::search {

// The phase of the asynchronous counting scan that feeds 「全部替换」: the
// initial pass finds the files the operation will touch; when one of them has a
// modified buffer it is saved and a post-save pass re-counts the saved content.
enum class ReplaceScanPhase { None, Initial, PostSave };

class SearchPanel final : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);
    ~SearchPanel() override;

    SearchPanel(const SearchPanel &) = delete;
    SearchPanel &operator=(const SearchPanel &) = delete;

    void setRootPath(const QString &rootPath);
    QString rootPath() const;

    void setQuery(const QString &query);
    QString query() const;
    void setCaseSensitive(bool on);
    bool caseSensitive() const;
    void setWholeWords(bool on);
    bool wholeWords() const;
    void setRegex(bool on);
    bool regex() const;
    void setVerilogOnly(bool on);
    bool verilogOnly() const;

    // The shared option model (ux-batch-2 B2 item 1): the same four options
    // the Ctrl+F bar exposes, read from / written to this panel's widgets.
    vedit::search::QueryOptions queryOptions() const;
    void setQueryOptions(const vedit::search::QueryOptions &options);
    void setScope(vedit::search::SearchScope scope);
    vedit::search::SearchScope scope() const;
    vedit::search::SearchHandoff handoff() const;

    // What the dock's CurrentFile scope searches (b2-repair High 5). The
    // product factory installs a provider that resolves the ACTIVE pane's
    // document at search time, so the scope follows tab/pane switches; with no
    // provider (lane tests, standalone use) the shared hub's path is used.
    void setCurrentFileProvider(std::function<QString()> provider);

    // Hand-off from the Ctrl+F bar: adopt its query + options AND run the
    // project search (the bar never searches the project itself).
    void applyHandoff(const vedit::search::SearchHandoff &handoff);

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

    // --- replace (ux-batch-2 B2) -------------------------------------------
    void setReplaceText(const QString &text);
    QString replaceText() const;

    // 「替换」: replaces the current hit and advances to the next match.
    // Returns false when the request could not even be attempted (no root, a
    // scan/replace already running, no hit, or a result row from a stale
    // query). A DOCUMENT-identity refusal (the file changed during the scan,
    // since the scan, or the unsaved buffer moved the match) is the outcome of
    // an accepted request: it returns true and is reported through
    // replaceFinished() / lastReplaceOutcome(), exactly like a worker-side
    // refusal. Nothing is written in either case.
    bool replaceCurrentHit();
    // 「全部替换」: all-matches scan -> save only the affected modified buffers
    // -> re-scan -> confirmation -> apply. Returns false when the request could
    // not start (no root/query/current file, another scan or replace running);
    // a later refusal (save failure, truncated or timed-out scan) is reported
    // through replaceFinished()/lastReplaceOutcome().
    bool replaceAllRequested();
    bool isReplacing() const;
    ReplaceOutcome lastReplaceOutcome() const;
    int currentHitIndex() const;

    // The default confirmation is a modal QMessageBox listing file and hit
    // counts; tests inject a handler so the flow stays offscreen.
    using ReplaceConfirmHandler =
        std::function<bool(const ReplacePlan &plan, const QStringList &savedDocuments)>;
    void setReplaceConfirmHandler(ReplaceConfirmHandler handler);
    // Overrides the per-file write (default: atomicSaveText). Tests count the
    // writes per file with a thread-safe double.
    void setFileRewriter(FileRewriter rewriter);

    const QList<SearchHit> &hits() const;
    QTreeWidget *resultsView() const;
    QLabel *emptyStateLabel() const;
    QLabel *summaryLabel() const;
    QLabel *rootLabel() const;
    QLabel *replaceStatusLabel() const;
    QLineEdit *queryEdit() const;
    QLineEdit *replaceEdit() const;
    QComboBox *scopeCombo() const;
    QToolButton *wholeWordButton() const;
    QToolButton *regexButton() const;
    GlobalSearch *search();
    ProjectReplacer *replacer();

signals:
    void jumpRequested(const vedit::workspace::search::JumpRequest &request);
    void searchFinished(int hitCount, bool truncated);
    void replaceFinished(const vedit::workspace::search::ReplaceOutcome &outcome);

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

    // Options actually sent to the scan (root + query + shared options +
    // limits + the current-file filter when that scope is selected).
    SearchOptions buildOptions(bool allMatches) const;
    QString resolveCurrentFile() const;
    bool beginScan(const SearchOptions &options, ReplaceScanPhase phase);
    void publishToHub();
    void adoptFromHub(const vedit::search::SearchHandoff &handoff);

    // The immutable identity of one completed scan (b2-repair-r2 High 2): the
    // query/options that produced `hits_`, the scan id tagged onto every hit,
    // the WALL-CLOCK START of the scan (b2-repair-r3: a file whose modification
    // time is after it was written while the walk was reading, so its hits and
    // its stamp describe different content) and the per-file stamps captured as
    // the scan finished. A replacement is built from THIS value only — never
    // from the live widgets.
    struct FrozenScan {
        ReplaceQuery query;
        SearchOptions options;
        quint64 scanId = 0;
        qint64 scanStartedMs = 0;
        QHash<QString, ReplaceFileStamp> files;
        bool valid = false;
    };
    FrozenScan makeFrozenScan(const SearchOptions &options, quint64 scanId,
                              const ReplaceQuery &query) const;
    bool sameLiveIdentity(const FrozenScan &frozen, QString *error) const;
    ReplaceFileStamp captureFileStamp(const QString &file, qint64 scanStartedMs) const;
    // The first hit file whose stamp says it was written DURING the scan, or an
    // empty string. A counted operation refuses at once; a single 「替换」
    // refuses that file (its coordinates may be stale).
    QString frozenScanWindowFile(const FrozenScan &frozen) const;
    // True while a 「全部替换」 owns the panel (counting scan, save-first step,
    // confirmation or write). The Ctrl+F hand-off and any other path that would
    // start a new scan must refuse instead of orphaning that operation.
    bool operationBusy() const;
    // Modified open documents outside `scanFiles` whose buffer may contain a
    // match that exists only in memory. They are saved before the re-scan, so
    // a buffer-only match is replaced instead of silently missed.
    QStringList bufferOnlyModifiedCandidates(const QStringList &scanFiles) const;
    bool bufferMayContainQuery(const QString &text, const ReplaceQuery &query) const;
    // Only used to compare the live widgets with a frozen snapshot; it never
    // reaches RangeMatcher or ProjectReplacer.
    ReplaceQuery liveReplaceQuery() const;
    void onQueryControlsChanged();
    void invalidateResults(const QString &reason);
    void updateControlState();
    void finishReplaceRefusal(const QString &message);
    QString savedDocumentsSuffix() const;

    // 「全部替换」 runs in two counting scans: the first tells which files the
    // replace will touch, only their modified buffers are saved, then the scan
    // is repeated on the saved content and the confirmation uses those counts.
    // The second phase is skipped when no affected buffer was modified.
    void finishInitialReplaceScan();
    void finishPostSaveReplaceScan();

    void confirmAndReplace();
    bool confirmReplace(const ReplacePlan &plan, const QStringList &savedDocuments);
    bool ensureBuffersSaved(const QStringList &files, QStringList *saved, QString *error);
    QStringList hitFiles() const;
    // Content tokens of the open buffers that hold the written content, so the
    // post-batch reload never discards text typed meanwhile (Blocker 1).
    void captureReloadTokens(const QStringList &files);
    bool startReplace(const QList<ReplaceHit> &hits, const QString &replacement,
                      int advanceTo, const FrozenScan &frozen);
    void onReplaceFinished(const ReplaceOutcome &outcome);
    void setReplaceStatus(const QString &text);
    void setBusy(bool busy);

    GlobalSearch search_;
    ProjectReplacer replacer_;
    FileRewriter rewriter_;
    ReplaceConfirmHandler confirmHandler_;
    std::function<QString()> currentFileProvider_;

    QLineEdit *query_ = nullptr;
    QToolButton *caseButton_ = nullptr;
    QToolButton *wholeButton_ = nullptr;
    QToolButton *regexButton_ = nullptr;
    QToolButton *verilogButton_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QPushButton *searchButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLabel *rootLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QWidget *replaceRow_ = nullptr;
    QLineEdit *replaceEdit_ = nullptr;
    QPushButton *replaceButton_ = nullptr;
    QPushButton *replaceAllButton_ = nullptr;
    QLabel *replaceStatus_ = nullptr;
    QTreeWidget *tree_ = nullptr;

    QList<SearchHit> hits_;
    QHash<QString, QTreeWidgetItem *> fileItems_;
    QHash<QString, int> fileHitCounts_;
    QString rootPath_;
    SearchSummary lastSummary_;
    ReplaceOutcome lastReplaceOutcome_;
    QStringList savedBeforeReplace_; // open buffers saved by the last 「全部替换」
    QHash<QString, QString> reloadTokens_; // written file -> saved buffer token
    // b2-repair-r2: the frozen identity of the scan that produced `hits_`.
    FrozenScan lastScan_;
    // The identity frozen when 「全部替换」 starts; both counting scans use it
    // and every later step compares the live widgets against it.
    ReplaceQuery operationQuery_;
    SearchOptions operationOptions_;
    bool operationActive_ = false;
    bool operationInputChanged_ = false;
    bool scanInputChanged_ = false;
    quint64 scanGeneration_ = 0;
    quint64 activeScanId_ = 0;
    // When the running scan started (wall clock, before GlobalSearch::start).
    // Every hit file whose mtime is later was written while the walk read it.
    qint64 activeScanStartedMs_ = 0;
    ReplaceQuery activeScanQuery_;
    SearchOptions activeScanOptions_;
    bool searching_ = false;
    bool replacing_ = false;
    ReplaceScanPhase replaceScanPhase_ = ReplaceScanPhase::None;
    bool syncing_ = false;       // adopting a hub snapshot must not re-publish
    int currentHitIndex_ = -1;
    int advanceAfterRefresh_ = -1;
    int hubSubscription_ = -1;
    int resultLimit_ = 2000;
    qint64 maxFileBytes_ = 8LL * 1024 * 1024;
    qint64 timeBudgetMs_ = 15000;
};

// Factory used by the app-shell seam (PanelRegistry::setFactory("search", ...)):
// a panel with the search root already applied. A free function on purpose: the
// panel contract stays usable without src/app, and the lane entry point
// (search_integration.cpp) is the only place that knows about the shell.
SearchPanel *createSearchPanel(const QString &rootPath);

} // namespace vedit::workspace::search
