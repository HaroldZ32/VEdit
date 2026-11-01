#pragma once

// Editor-side inline-completion state machine for FIM (task T27 / AC-S3.2).
//
// The kernel hook that renders ghost text calls this client; keeping the policy
// here (headless, Qt-only) means it can be tested without a view.
//
// Two shapes (T106 made the asynchronous one the product path):
//
//   * blocking `request(text, line, column)` — the lane selftest and any caller
//     that explicitly wants the result now. It waits on the job (15 s budget) and
//     MUST NOT run on the GUI thread.
//   * asynchronous `requestJob()` + `pollRequest()` + `cancelRequest()` — what
//     the editor binding uses: `requestJob` returns a token immediately, the
//     caller polls once per QTimer tick and applies the suggestion when the job
//     finishes. At most one job is in flight: starting a new one cancels the
//     previous one, and `onTextEdited()` cancels it too (typing aborts a request
//     the same way it invalidates a pending suggestion).
//
//   * typing invalidates the suggestion (`onTextEdited`), which is what makes a
//     stale completion disappear instead of being accepted by a later Tab;
//   * accept() returns the pending text once and clears it; cancel() drops it;
//   * statusText() is the short status-bar sentence (never a key or endpoint).
#include "fim_backend.h"

#include <QString>

namespace vedit::editor::fim {

class InlineCompletion {
public:
    explicit InlineCompletion(Backend *backend = nullptr);

    /// Replace the source (the selftest injects a fake; production uses
    /// `makeRustBackend()`).
    void setBackend(Backend *backend);
    Backend *backend() const { return backend_; }

    /// Blocking ask for a completion at the cursor (15 s budget). Returns true
    /// iff a suggestion arrived. Never call this on the GUI thread.
    bool request(const QString &text, int zeroBasedLine, int zeroBasedColumn);

    /// Starts an asynchronous request; returns its token (0 = no backend or the
    /// request was rejected). Any earlier in-flight job is cancelled first.
    quint64 requestJob(const QString &text, int zeroBasedLine, int zeroBasedColumn);
    /// One non-blocking poll. Returns true when the job finished (success or
    /// failure); false while it is still pending. A stale token returns true.
    bool pollRequest(quint64 job);
    /// Cancels a pending job (safe after completion, and safe twice).
    void cancelRequest(quint64 job);
    bool inFlight() const { return inFlight_ != 0; }
    quint64 activeJob() const { return inFlight_; }

    /// The text edit path: any pending suggestion and in-flight request become
    /// stale.
    void onTextEdited();

    bool hasSuggestion() const { return pending_; }
    QString suggestion() const { return suggestion_; }

    /// Take the pending suggestion (empty when there is none) and clear it.
    QString accept();
    /// Drop the pending suggestion.
    void cancel();

    QString statusText() const { return status_; }
    quint64 requestCount() const { return requests_; }

private:
    void finishJob();
    void applyReply(const Reply &reply);

    Backend *backend_ = nullptr;
    QString suggestion_;
    QString status_;
    bool pending_ = false;
    quint64 requests_ = 0;

    quint64 inFlight_ = 0;
    quint64 nextToken_ = 0;
    // Backends without an asynchronous seam (test fakes) complete inside
    // requestJob; the first poll then reports the already-applied result.
    bool immediateDone_ = false;
};

} // namespace vedit::editor::fim
