#pragma once

// Product wiring of the FIM inline-completion lane (task T104).
//
// T27 delivered `src/editor/fim` (prefix/suffix split + the suggestion state
// machine + the Rust C-ABI backend) and said honestly that it was not wired
// into the editor. This class is the integration point the live editor needs:
//
//   * ONE instance per KTextEditor::View. It is a child of the view, so it dies
//     with the pane's editor (same lifetime model as the folding/breadcrumb
//     binding, T94).
//   * `Alt+\` asks the state machine for a completion. `Tab` accepts and only
//     then edits the document (exactly once); `Esc` drops it. Tab/Esc shortcuts
//     exist ONLY while a suggestion is pending, so with FIM disabled (the
//     product default) they keep their normal editor meaning.
//   * The suggestion is rendered as ghost text: a translucent QLabel overlay
//     positioned with View::cursorToCoordinate(). The document is NOT modified
//     until the user accepts.
//   * Typing or moving the cursor invalidates the pending suggestion through
//     the lane's own `InlineCompletion::onTextEdited()` (the state machine is
//     never bypassed), which is what stops a stale suggestion from being
//     inserted by a later Tab.
//
// Cost, stated honestly: `InlineCompletion::request()` is synchronous in T27's
// lane (its Rust backend polls the C ABI on the calling thread, budget 15 s), so
// pressing Alt+\ blocks the GUI thread until the request settles. This class
// calls it exactly as the lane's README prescribes; making the trigger
// asynchronous would require changing the lane's request() contract, which is
// outside this task's write scope.
#include <QElapsedTimer>
#include <QKeySequence>
#include <QObject>
#include <QString>

#include <memory>

class QLabel;
class QShortcut;
class QTimer;

namespace KTextEditor {
class View;
}

namespace vedit::editor::fim {
class Backend;
class InlineCompletion;
}

namespace vedit::editor {

class FimBinding final : public QObject {
    Q_OBJECT

public:
    explicit FimBinding(KTextEditor::View *view, QObject *parent = nullptr);
    ~FimBinding() override;

    FimBinding(const FimBinding &) = delete;
    FimBinding &operator=(const FimBinding &) = delete;

    // The binding installed on this editor widget, or nullptr.
    static FimBinding *instanceFor(QWidget *editorWidget);

    // The `Alt+\` action (also what the shortcut calls). T106: it starts an
    // asynchronous request and returns immediately; the suggestion arrives via a
    // QTimer poll (`pollJob`) and is rendered when it does.
    bool requestCompletion();
    // The `Tab` action: inserts the pending suggestion, returns true iff text
    // was inserted. A second call inserts nothing (the suggestion is gone).
    bool acceptSuggestion();
    // The `Esc` action.
    void cancelSuggestion();

    bool hasSuggestion() const;
    QString suggestion() const;
    QString statusText() const;
    quint64 requestCount() const;

    // Ghost-text overlay (never edits the document).
    bool ghostVisible() const;
    QString ghostText() const;
    QLabel *ghostWidget() const { return ghost_; }

    // T106 async state: exactly one job is in flight while a request runs.
    int inFlightJobs() const;
    int pollRounds() const { return pollRounds_; }
    // The poll budget (default 15 s). Tests shrink it to prove the timeout is a
    // cancel + diagnostic, not a frozen window.
    void setPollBudgetMs(int milliseconds);

    // Shortcut wiring, exposed for the lane selftest.
    QShortcut *triggerShortcut() const { return trigger_; }
    QShortcut *acceptShortcut() const { return accept_; }
    QShortcut *cancelShortcut() const { return cancel_; }
    bool acceptShortcutActive() const;
    bool cancelShortcutActive() const;

signals:
    void suggestionChanged(bool available);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onDocumentEdited();
    void onCursorMoved();
    void pollJob();
    void showGhost();
    void hideGhost();
    void installAcceptShortcuts();
    void removeAcceptShortcuts();

    KTextEditor::View *view_ = nullptr;
    // InlineCompletion borrows the backend (its API takes a raw pointer and does
    // not own it), so the binding owns it and keeps it alive for the view.
    std::unique_ptr<fim::Backend> backend_;
    std::unique_ptr<fim::InlineCompletion> completion_;
    QLabel *ghost_ = nullptr;
    QShortcut *trigger_ = nullptr;
    QShortcut *accept_ = nullptr;
    QShortcut *cancel_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    quint64 activeJob_ = 0;
    QElapsedTimer jobTimer_;
    int pollBudgetMs_ = 15'000;
    int pollRounds_ = 0;
    QString timeoutStatus_;
    QString shownText_;
};

} // namespace vedit::editor
