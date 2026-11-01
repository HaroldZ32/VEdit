#pragma once

// Editor-side inline-completion state machine for FIM (task T27 / AC-S3.2).
//
// The kernel hook that renders ghost text calls this client; keeping the policy
// here (headless, Qt-only) means it can be tested without a view:
//
//   * request(text, line, column) — splits the text at the cursor and asks the
//     backend. With no Rust service or with FIM unconfigured the backend returns
//     the disabled state: no suggestion is produced and no request is made.
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

    /// Ask for a completion at the cursor. Returns true iff a suggestion arrived.
    bool request(const QString &text, int zeroBasedLine, int zeroBasedColumn);
    /// The text edit path: any pending suggestion becomes stale.
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
    Backend *backend_ = nullptr;
    QString suggestion_;
    QString status_;
    bool pending_ = false;
    quint64 requests_ = 0;
};

} // namespace vedit::editor::fim
