#pragma once

// Global search backend (workspace lane, task T23 / acceptance A2c).
//
// One scan walks the root directory on a worker thread, matches the query
// literally line by line, and streams hits back in batches. start() returns
// immediately; cancel() is safe to call from the UI thread at any time.
//
// Observable limits are part of the contract, not hidden behaviour:
//   * files above SearchOptions::maxFileBytes are skipped and counted,
//   * binary files (NUL byte in the content) are skipped and counted,
//   * unreadable files/directories are skipped and counted,
//   * a scan stops at maxResults (truncated) or maxDurationMs (timedOut).
#include "search_types.h"

#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>

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
    void runScan(const SearchOptions &options, quint64 generation);

    QPointer<QThread> thread_;
    std::atomic_bool running_{false};
    std::atomic_bool cancelRequested_{false};
    std::atomic_bool ranOnWorkerThread_{false};
    quint64 generation_ = 0;
    SearchSummary lastSummary_;
};

} // namespace vedit::workspace::search
