#include "fim_wiring.h"

#include "fim_backend.h"
#include "fim_inline_completion.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QLabel>
#include <QShortcut>

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
    // The lane's state machine: it splits the text, calls the backend (Rust C
    // ABI; with FIM disabled the Rust side makes no request at all) and keeps
    // the suggestion until it is accepted, cancelled or invalidated by typing.
    cancelSuggestion();
    const bool has = completion_->request(document->text(), cursor.line(), cursor.column());
    if (has && !completion_->suggestion().isEmpty()) {
        showGhost();
        installAcceptShortcuts();
        emit suggestionChanged(true);
        return true;
    }
    hideGhost();
    removeAcceptShortcuts();
    emit suggestionChanged(false);
    return false;
}

bool FimBinding::acceptSuggestion()
{
    if (view_ == nullptr || completion_ == nullptr || !completion_->hasSuggestion()) {
        return false;
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
        completion_->cancel();
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
    return completion_ != nullptr ? completion_->statusText() : QString();
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
    if (completion_ == nullptr || !completion_->hasSuggestion()) {
        return;
    }
    // T27's invalidation: typing makes the pending suggestion stale.
    completion_->onTextEdited();
    removeAcceptShortcuts();
    hideGhost();
    emit suggestionChanged(false);
}

void FimBinding::onCursorMoved()
{
    if (completion_ == nullptr || !completion_->hasSuggestion()) {
        return;
    }
    // A suggestion belongs to the cursor position it was requested at.
    cancelSuggestion();
    emit suggestionChanged(false);
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
