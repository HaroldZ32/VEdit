#include "fim_wiring.h"

#include "fim_backend.h"
#include "fim_inline_completion.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QLabel>
#include <QShortcut>
#include <QTimer>

#include <utility>

namespace vedit::editor {

namespace {

// The ghost shows the first line of the suggestion; the full text is inserted on
// accept. A multi-line suggestion keeps its line break in the document.
QString ghostPreview(const QString &suggestion)
{
    int newline = suggestion.indexOf(QLatin1Char('\n'));
    if (newline < 0) {
        return suggestion.left(120);
    }
    return suggestion.left(newline) + QStringLiteral(" \u21b5");
}

} // namespace

FimBinding::FimBinding(KTextEditor::View *view, QObject *parent)
    : QObject(parent)
    , view_(view)
    , backend_(fim::makeRustBackend())
    , completion_(std::make_unique<fim::InlineCompletion>(backend_.get()))
{
    if (view_ == nullptr) {
        return;
    }
    ghost_ = new QLabel(view_);
    ghost_->setObjectName(QStringLiteral("fim-ghost"));
    ghost_->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost_->setFocusPolicy(Qt::NoFocus);
    ghost_->setTextInteractionFlags(Qt::NoTextInteraction);
    ghost_->setStyleSheet(QStringLiteral(
        "color: gray; background-color: rgba(160, 160, 160, 40); border-radius: 2px;"
        " padding: 0px 2px;"));
    QFont ghostFont = view_->font();
    ghostFont.setItalic(true);
    ghost_->setFont(ghostFont);
    ghost_->hide();
    ghost_->setToolTip(QStringLiteral(
        "FIM ghost text: previews the first line only; Tab inserts the full suggestion."));

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(15);
    connect(pollTimer_, &QTimer::timeout, this, [this]() { pollJob(); });

    trigger_ = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Backslash), view_);
    trigger_->setContext(Qt::WidgetWithChildrenShortcut);
    connect(trigger_, &QShortcut::activated, this, [this]() { requestCompletion(); });

    if (KTextEditor::Document *document = view_->document(); document != nullptr) {
        connect(document, &KTextEditor::Document::textChanged, this,
                [this](KTextEditor::Document *) { onDocumentEdited(); });
    }
    connect(view_, &KTextEditor::View::cursorPositionChanged, this,
            [this](KTextEditor::View *, KTextEditor::Cursor) { onCursorMoved(); });
    view_->installEventFilter(this);
}

FimBinding::~FimBinding()
{
    if (pollTimer_ != nullptr) {
        pollTimer_->stop();
    }
    if (completion_ != nullptr && activeJob_ != 0) {
        completion_->cancelRequest(activeJob_);
        activeJob_ = 0;
    }
    removeAcceptShortcuts();
    if (view_ != nullptr) {
        view_->removeEventFilter(this);
    }
}

FimBinding *FimBinding::instanceFor(QWidget *editorWidget)
{
    auto *view = qobject_cast<KTextEditor::View *>(editorWidget);
    if (view == nullptr) {
        return nullptr;
    }
    return view->findChild<FimBinding *>(QString(), Qt::FindDirectChildrenOnly);
}

bool FimBinding::requestCompletion()
{
    if (view_ == nullptr || completion_ == nullptr) {
        return false;
    }
    KTextEditor::Document *document = view_->document();
    if (document == nullptr) {
        return false;
    }
    const KTextEditor::Cursor cursor = view_->cursorPosition();
    // T106: start the job and return. The lane's state machine splits the text,
    // starts the Rust job (with FIM disabled the Rust side makes no request at
    // all) and keeps ONE job in flight; the QTimer poll below applies the
    // suggestion when it arrives. Nothing here waits on the network.
    cancelSuggestion();
    timeoutStatus_.clear();
    const quint64 job = completion_->requestJob(document->text(), cursor.line(), cursor.column());
    if (job == 0) {
        hideGhost();
        removeAcceptShortcuts();
        emit suggestionChanged(false);
        return false;
    }
    activeJob_ = job;
    jobTimer_.start();
    // The QTimer is what makes the request asynchronous: start it BEFORE the
    // first poll so a pending job keeps being polled without any blocking wait.
    pollTimer_->start();
    pollJob(); // a backend without an async seam is already done
    return completion_->hasSuggestion();
}

bool FimBinding::acceptSuggestion()
{
    if (view_ == nullptr || completion_ == nullptr || !completion_->hasSuggestion()) {
        return false;
    }
    if (activeJob_ != 0) {
        completion_->cancelRequest(activeJob_);
        activeJob_ = 0;
        pollTimer_->stop();
    }
    QString text = completion_->accept();
    removeAcceptShortcuts();
    hideGhost();
    emit suggestionChanged(false);
    if (text.isEmpty()) {
        return false;
    }
    KTextEditor::Document *document = view_->document();
    if (document == nullptr) {
        return false;
    }
    // The document is edited HERE and only here: the ghost was pure overlay, so
    // nothing was written before the user pressed Tab.
    document->insertText(view_->cursorPosition(), text);
    return true;
}

void FimBinding::cancelSuggestion()
{
    if (completion_ != nullptr) {
        if (activeJob_ != 0) {
            completion_->cancelRequest(activeJob_);
            activeJob_ = 0;
        }
        completion_->cancel();
    }
    if (pollTimer_ != nullptr) {
        pollTimer_->stop();
    }
    removeAcceptShortcuts();
    hideGhost();
}

bool FimBinding::hasSuggestion() const
{
    return completion_ != nullptr && completion_->hasSuggestion();
}

QString FimBinding::suggestion() const
{
    return completion_ != nullptr ? completion_->suggestion() : QString();
}

QString FimBinding::statusText() const
{
    if (!timeoutStatus_.isEmpty()) {
        return timeoutStatus_;
    }
    return completion_ != nullptr ? completion_->statusText() : QString();
}

int FimBinding::inFlightJobs() const
{
    return activeJob_ != 0 ? 1 : 0;
}

void FimBinding::setPollBudgetMs(int milliseconds)
{
    pollBudgetMs_ = qMax(1, milliseconds);
}

quint64 FimBinding::requestCount() const
{
    return completion_ != nullptr ? completion_->requestCount() : 0;
}

bool FimBinding::ghostVisible() const
{
    // isVisibleTo(view_) asks whether the overlay is shown up to the view,
    // independent of whether the view's own window happens to be visible (a
    // headless test shows no top-level window).
    return ghost_ != nullptr && ghost_->isVisibleTo(view_);
}

QString FimBinding::ghostText() const
{
    return ghost_ != nullptr ? ghost_->text() : QString();
}

bool FimBinding::acceptShortcutActive() const
{
    return accept_ != nullptr;
}

bool FimBinding::cancelShortcutActive() const
{
    return cancel_ != nullptr;
}

void FimBinding::onDocumentEdited()
{
    if (completion_ == nullptr) {
        return;
    }
    const bool hadJob = activeJob_ != 0;
    if (hadJob) {
        completion_->cancelRequest(activeJob_);
        activeJob_ = 0;
        pollTimer_->stop();
    }
    if (!completion_->hasSuggestion() && !hadJob) {
        return;
    }
    // T27's invalidation: typing makes the pending suggestion (and an in-flight
    // request) stale.
    completion_->onTextEdited();
    removeAcceptShortcuts();
    hideGhost();
    emit suggestionChanged(false);
}

void FimBinding::onCursorMoved()
{
    if (completion_ == nullptr) {
        return;
    }
    if (!completion_->hasSuggestion() && activeJob_ == 0) {
        return;
    }
    // A suggestion (and a request) belongs to the cursor position it was made at.
    cancelSuggestion();
    emit suggestionChanged(false);
}

void FimBinding::pollJob()
{
    if (activeJob_ == 0 || completion_ == nullptr) {
        if (pollTimer_ != nullptr) {
            pollTimer_->stop();
        }
        return;
    }
    ++pollRounds_;
    if (!completion_->pollRequest(activeJob_)) {
        if (jobTimer_.elapsed() >= pollBudgetMs_) {
            completion_->cancelRequest(activeJob_);
            activeJob_ = 0;
            pollTimer_->stop();
            hideGhost();
            removeAcceptShortcuts();
            timeoutStatus_ = QStringLiteral("FIM 超时（%1 ms 无结果，已取消）").arg(pollBudgetMs_);
            emit suggestionChanged(false);
        }
        return;
    }
    activeJob_ = 0;
    pollTimer_->stop();
    if (completion_->hasSuggestion()) {
        showGhost();
        installAcceptShortcuts();
        emit suggestionChanged(true);
    } else {
        hideGhost();
        removeAcceptShortcuts();
        emit suggestionChanged(false);
    }
}

void FimBinding::showGhost()
{
    if (view_ == nullptr || ghost_ == nullptr || completion_ == nullptr) {
        return;
    }
    const QString suggestion = completion_->suggestion();
    if (suggestion.isEmpty()) {
        return;
    }
    shownText_ = ghostPreview(suggestion);
    ghost_->setText(shownText_);
    ghost_->adjustSize();
    const QPoint origin = view_->cursorToCoordinate(view_->cursorPosition());
    if (origin.x() < 0 || origin.y() < 0) {
        // The cursor is scrolled out of view: showing the ghost anywhere would
        // point at the wrong place.
        ghost_->hide();
        return;
    }
    ghost_->move(origin + QPoint(2, 0));
    ghost_->show();
    ghost_->raise();
}

void FimBinding::hideGhost()
{
    if (ghost_ != nullptr) {
        ghost_->hide();
        ghost_->clear();
    }
    shownText_.clear();
}

void FimBinding::installAcceptShortcuts()
{
    if (view_ == nullptr) {
        return;
    }
    if (accept_ == nullptr) {
        accept_ = new QShortcut(QKeySequence(Qt::Key_Tab), view_);
        accept_->setContext(Qt::WidgetWithChildrenShortcut);
        connect(accept_, &QShortcut::activated, this, [this]() { acceptSuggestion(); });
    }
    if (cancel_ == nullptr) {
        cancel_ = new QShortcut(QKeySequence(Qt::Key_Escape), view_);
        cancel_->setContext(Qt::WidgetWithChildrenShortcut);
        connect(cancel_, &QShortcut::activated, this, [this]() {
            cancelSuggestion();
            emit suggestionChanged(false);
        });
    }
}

void FimBinding::removeAcceptShortcuts()
{
    delete accept_;
    accept_ = nullptr;
    delete cancel_;
    cancel_ = nullptr;
}

bool FimBinding::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == view_ && event != nullptr) {
        switch (event->type()) {
        case QEvent::Wheel:
            // Scrolling moves the text under the overlay: hide it (the pending
            // suggestion stays accepted-able until the cursor moves or typing
            // invalidates it).
            hideGhost();
            break;
        case QEvent::Resize:
            hideGhost();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace vedit::editor
