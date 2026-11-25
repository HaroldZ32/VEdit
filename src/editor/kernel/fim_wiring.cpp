#include "fim_wiring.h"
#include <QCoreApplication>

#include "fim_backend.h"
#include "fim_inline_completion.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Message>
#include <KTextEditor/View>

#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QTimer>

namespace vedit::editor {

namespace {

// The ghost shows the first line of the suggestion; the full text is inserted on
// accept. A multi-line suggestion keeps its line break in the document.
QString ghostPreview(const QString &suggestion)
{
    const qsizetype newline = suggestion.indexOf(QLatin1Char('\n'));
    const QString firstLine = newline < 0 ? suggestion : suggestion.left(newline);
    const QString preview = firstLine.size() > 120 ? firstLine.left(120) + QChar(0x2026) : firstLine;
    return newline < 0 ? preview : preview + QStringLiteral(" \u21b5");
}

// The Rust C ABI reports these short, sanitized reasons in Chinese.
QString localizedStatus(const QString &message)
{
    static constexpr struct {
        const char *reason;
        const char *source;
    } reasons[] = {
        {"FIM 未启用", QT_TRANSLATE_NOOP("VEdit", "FIM disabled (off by default)")},
        {"FIM 未配置端点", QT_TRANSLATE_NOOP("VEdit", "FIM endpoint is missing")},
        {"FIM 未配置模型", QT_TRANSLATE_NOOP("VEdit", "FIM model is missing")},
        {"FIM 端点无效", QT_TRANSLATE_NOOP("VEdit", "FIM endpoint is invalid")},
        {"FIM 响应过大", QT_TRANSLATE_NOOP("VEdit", "FIM response is too large")},
        {"FIM 响应格式错误", QT_TRANSLATE_NOOP("VEdit", "FIM response format is invalid")},
        {"FIM 连接被拒绝", QT_TRANSLATE_NOOP("VEdit", "FIM connection refused")},
        {"FIM 域名解析失败", QT_TRANSLATE_NOOP("VEdit", "FIM host could not be resolved")},
        {"FIM TLS 失败", QT_TRANSLATE_NOOP("VEdit", "FIM TLS failure")},
        {"FIM 网络错误", QT_TRANSLATE_NOOP("VEdit", "FIM network error")},
        {"FIM 传输失败", QT_TRANSLATE_NOOP("VEdit", "FIM transport error")},
    };
    for (const auto &reason : reasons) {
        if (message == QString::fromUtf8(reason.reason)) {
            return QCoreApplication::translate("VEdit", reason.source);
        }
    }
    if (message.startsWith(QStringLiteral("FIM 超时（"))) {
        return QCoreApplication::translate("VEdit", "FIM request timed out");
    }
    return message;
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
    ghost_->setTextFormat(Qt::PlainText);
    ghost_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ghost_->setMargin(2);
    ghost_->hide();

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(15);
    connect(pollTimer_, &QTimer::timeout, this, [this]() { pollJob(); });

    if (QWidget *editor = view_->focusProxy()) {
        editor->installEventFilter(this);
    }

    if (KTextEditor::Document *document = view_->document(); document != nullptr) {
        connect(document, &KTextEditor::Document::textChanged, this,
                [this](KTextEditor::Document *) { onDocumentEdited(); });
    }
    connect(view_, &KTextEditor::View::cursorPositionChanged, this,
            [this](KTextEditor::View *, KTextEditor::Cursor) { onCursorMoved(); });
    connect(view_, &KTextEditor::View::selectionChanged, this, &FimBinding::onCursorMoved);
    connect(view_, &KTextEditor::View::verticalScrollPositionChanged, this,
            &FimBinding::onCursorMoved);
    connect(view_, &KTextEditor::View::horizontalScrollPositionChanged, this,
            &FimBinding::onCursorMoved);
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
    if (document == nullptr || !document->isReadWrite()) {
        publishStatus(QCoreApplication::translate("VEdit", "FIM needs an editable document"));
        return false;
    }
    if (view_->selection() || view_->cursorPositions().size() > 1) {
        publishStatus(QCoreApplication::translate("VEdit", "FIM needs a single caret without a selection"));
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
        publishStatus(statusText());
        emit suggestionChanged(false);
        return false;
    }
    activeJob_ = job;
    publishStatus(statusText(), 0);
    jobTimer_.start();
    // The QTimer is what makes the request asynchronous: start it BEFORE the
    // first poll so a pending job keeps being polled without any blocking wait.
    pollTimer_->start();
    pollJob(); // a backend without an async seam is already done
    return completion_->hasSuggestion();
}

bool FimBinding::acceptSuggestion()
{
    if (!ghostVisible() || !hasSuggestion()) {
        return false;
    }
    const QString text = completion_->accept();
    clearStatus();
    hideGhost();
    emit suggestionChanged(false);
    return view_->document()->insertText(view_->cursorPosition(), text);
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
    clearStatus();
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

QKeySequence FimBinding::triggerKey()
{
    return QKeySequence(Qt::AltModifier | Qt::Key_Backslash);
}

void FimBinding::onDocumentEdited()
{
    onCursorMoved();
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
            timeoutStatus_ = QCoreApplication::translate("VEdit", "FIM timed out (%1 ms without a result; cancelled)").arg(pollBudgetMs_);
            publishStatus(timeoutStatus_);
            emit suggestionChanged(false);
        }
        return;
    }
    activeJob_ = 0;
    pollTimer_->stop();
    if (hasSuggestion()) {
        showGhost();
        if (ghostVisible()) {
            publishStatus(QCoreApplication::translate("VEdit", "FIM suggestion ready. Tab inserts it. Esc dismisses it."), 0);
            emit suggestionChanged(true);
            return;
        }
        cancelSuggestion();
        publishStatus(QCoreApplication::translate("VEdit", "FIM suggestion could not be shown at this caret"));
    } else {
        publishStatus(statusText());
    }
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
    const auto caret = view_->cursorPosition();
    const QPoint origin = view_->cursorToCoordinate(caret);
    QWidget *editor = view_->focusProxy();
    if (editor == nullptr || !editor->hasFocus()) {
        return;
    }
    const QRect viewport(editor->mapTo(view_, QPoint()), editor->size());
    if (!viewport.contains(origin)) {
        return;
    }

    const bool inlineGhost = view_->window()->property("vedit.fim.ghostText").toBool()
        && caret.column() == view_->document()->lineLength(caret.line());
    QFont font = ghost_->font();
    font.setItalic(inlineGhost);
    ghost_->setFont(font);
    ghost_->setStyleSheet(inlineGhost
        ? QStringLiteral("color: palette(placeholder-text); background: transparent;")
        : QStringLiteral("color: palette(tool-tip-text); background: palette(tool-tip-base);"
                         " border: 1px solid palette(mid);"));
    const int availableWidth = inlineGhost ? viewport.right() - origin.x() : viewport.width();
    const QString preview = ghost_->fontMetrics().elidedText(
        ghostPreview(suggestion), Qt::ElideRight, availableWidth - 2 * ghost_->margin() - 2);
    if (preview.isEmpty()) {
        return;
    }
    ghost_->setText(preview);
    ghost_->adjustSize();
    QPoint position = origin;
    if (!inlineGhost) {
        position.rx() = qMin(position.x(), viewport.right() - ghost_->width() + 1);
        const QPoint nextLine = view_->cursorToCoordinate(KTextEditor::Cursor(caret.line() + 1, 0));
        const int lineHeight = nextLine.y() > origin.y() ? nextLine.y() - origin.y()
                                                        : ghost_->fontMetrics().height();
        position.ry() += lineHeight;
        if (position.y() + ghost_->height() > viewport.bottom()) {
            position.ry() = origin.y() - ghost_->height();
        }
    }
    if (!viewport.contains(QRect(position, ghost_->size()))) {
        return;
    }
    ghost_->move(position);
    ghost_->show();
    ghost_->raise();
}

void FimBinding::hideGhost()
{
    if (ghost_ != nullptr) {
        ghost_->hide();
        ghost_->clear();
    }
}

void FimBinding::publishStatus(const QString &message, int milliseconds)
{
    clearStatus();
    auto *document = view_->document();
    if (document == nullptr) {
        return;
    }
    auto *notice = new KTextEditor::Message(localizedStatus(message).toHtmlEscaped());
    notice->setView(view_);
    notice->setPosition(KTextEditor::Message::BottomInView);
    notice->setAutoHide(milliseconds == 0 ? -1 : milliseconds);
    notice->setAutoHideMode(KTextEditor::Message::Immediate);
    notice_ = notice;
    if (!document->postMessage(notice)) {
        clearStatus();
    }
}

void FimBinding::clearStatus()
{
    delete notice_.data();
}

bool FimBinding::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == view_) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Hide) {
            onCursorMoved();
        }
        return false;
    }
    if (watched != view_->focusProxy()) {
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        acceptKeyDown_ = false;
        onCursorMoved();
        return false;
    }
    if (event->type() == QEvent::KeyRelease
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab
        && !static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
        acceptKeyDown_ = false;
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) {
        return false;
    }
    auto *key = static_cast<QKeyEvent *>(event);
    const bool request = key->keyCombination() == triggerKey()[0];
    const bool accept = key->keyCombination() == QKeyCombination(Qt::Key_Tab)
        && (acceptKeyDown_ || (ghostVisible() && !view_->isCompletionActive()));
    const bool dismiss = key->keyCombination() == QKeyCombination(Qt::Key_Escape)
        && (activeJob_ != 0 || hasSuggestion()) && !view_->isCompletionActive();
    if (!request && !accept && !dismiss) {
        return false;
    }
    key->accept();
    if (event->type() == QEvent::ShortcutOverride || key->isAutoRepeat()) {
        return true;
    }
    if (request) {
        requestCompletion();
    } else if (accept) {
        acceptKeyDown_ = acceptSuggestion();
    } else {
        onCursorMoved();
    }
    return true;
}

} // namespace vedit::editor
