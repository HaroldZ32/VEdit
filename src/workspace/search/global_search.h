#pragma once

// Global search backend (workspace lane, task T23 / acceptance A2c).
//
// The scan itself belongs to the Rust search service (T21): this class starts
// `vd_search_tree_query`, polls it from a worker thread, and converts the hits
// and summary into Qt types. Qt performs no directory walk and no line matching
// — there is exactly one implementation, in `crates/vd-search`.
//
// Observable limits are part of the contract, not hidden behaviour:
//   * files above SearchOptions::maxFileBytes are skipped and counted,
//   * binary files (NUL byte in the content) are skipped and counted,
//   * unreadable files/directories are skipped and counted,
//   * a scan stops at maxResults (truncated) or maxDurationMs (timedOut).
// The counters come back in the same poll as the hits (VdSearchSummary).
#include "search_types.h"

#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <memory>

namespace vedit::workspace::search {

class GlobalSearch final : public QObject {
    Q_OBJECT

public:
    explicit GlobalSearch(QObject *parent = nullptr);
    ~GlobalSearch() override;

    GlobalSearch(const GlobalSearch &) = delete;
    GlobalSearch &operator=(const GlobalSearch &) = delete;

    // Cancels a still running scan, waits for it briefly and starts a new one.
    // Returns false when a previous scan refused to stop in time (nothing is
    // started in that case).
    bool start(const SearchOptions &options);
    void cancel();
    bool isRunning() const;
    SearchSummary summary() const;

    // Diagnostic: true when the last scan ran off the GUI thread. The
    // self-check asserts this rather than trusting the implementation comment.
    bool ranOnWorkerThread() const;

signals:
    // Emitted in the GUI thread (the worker posts them), in file order.
    void hitsReady(const QList<vedit::workspace::search::SearchHit> &hits);
    void finished(const vedit::workspace::search::SearchSummary &summary);

private:
    void runScan(const SearchOptions &options, quint64 generation,
                 const std::shared_ptr<std::atomic_bool> &cancelTag);

    QPointer<QThread> thread_;
    std::atomic_bool running_{false};
    // Per-scan cancellation token. The worker thread holds its own shared_ptr
    // copy, so it never touches memory owned by this QObject; the destructor
    // additionally joins the worker before any member is destroyed (verdict
    // ws-search M2, evidence/search-uaf/).
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::atomic_bool ranOnWorkerThread_{false};
    quint64 generation_ = 0;
    SearchSummary lastSummary_;
};

} // namespace vedit::workspace::search
