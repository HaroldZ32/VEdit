#pragma once

// One binding per KTextEditor view. Requests are manual and asynchronous.
// A suggestion stays tied to that view's text, caret and editor focus.
#include <QElapsedTimer>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QLabel;
class QTimer;

namespace KTextEditor {
class Message;
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
    // UI watchdog. Tests shrink it to exercise the timeout path.
    void setPollBudgetMs(int milliseconds);

    static QKeySequence triggerKey();

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
    void publishStatus(const QString &message, int milliseconds = 5000);
    void clearStatus();

    KTextEditor::View *view_ = nullptr;
    // InlineCompletion borrows the backend (its API takes a raw pointer and does
    // not own it), so the binding owns it and keeps it alive for the view.
    std::unique_ptr<fim::Backend> backend_;
    std::unique_ptr<fim::InlineCompletion> completion_;
    QLabel *ghost_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    bool acceptKeyDown_ = false;
    quint64 activeJob_ = 0;
    QElapsedTimer jobTimer_;
    // The Rust transport enforces the configured deadline (up to 60 s).
    int pollBudgetMs_ = 61'000;
    int pollRounds_ = 0;
    QString timeoutStatus_;
    QPointer<KTextEditor::Message> notice_;
};

} // namespace vedit::editor
