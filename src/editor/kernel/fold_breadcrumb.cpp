#include "fold_breadcrumb.h"

#include "syntax_breadcrumb_source.h"
#include "syntax_service.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QTimer>

namespace vedit::editor {

FoldBreadcrumbBinding::FoldBreadcrumbBinding(KTextEditor::View *view, QObject *parent)
    : QObject(parent)
    , view_(view)
{
    setObjectName(QStringLiteral("breadcrumb-binding"));

    breadcrumbDebounce_ = new QTimer(this);
    breadcrumbDebounce_->setObjectName(QStringLiteral("breadcrumbDebounce"));
    breadcrumbDebounce_->setSingleShot(true);
    breadcrumbDebounce_->setInterval(150);
    connect(breadcrumbDebounce_, &QTimer::timeout, this, [this]() { refreshBreadcrumb(); });

    if (view_ == nullptr) {
        return;
    }
    connect(view_, &KTextEditor::View::cursorPositionChanged, this,
            [this](KTextEditor::View *, KTextEditor::Cursor) { refreshBreadcrumb(); });
    if (view_->document() != nullptr) {
        connect(view_->document(), &KTextEditor::Document::textChanged, this,
                [this](KTextEditor::Document *) {
                    // Debounce to one semantic update per typing pause.
                    if (breadcrumbDebounce_ != nullptr) {
                        breadcrumbDebounce_->start();
                    }
                });
    }
}

FoldBreadcrumbBinding::~FoldBreadcrumbBinding() = default;

FoldBreadcrumbBinding *FoldBreadcrumbBinding::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<FoldBreadcrumbBinding *>(QString(),
                                                           Qt::FindDirectChildrenOnly);
}

QString FoldBreadcrumbBinding::grammarId() const
{
    const QString mode = view_ != nullptr && view_->document() != nullptr
        ? view_->document()->highlightingMode()
        : QString();
    return mode == QStringLiteral("Verilog") ? QStringLiteral("verilog")
                                             : QStringLiteral("systemverilog");
}

void FoldBreadcrumbBinding::ensureSources()
{
    if (sourcesReady_ || view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    sourcesReady_ = true;
    breadcrumbSource_ = std::make_unique<vedit::breadcrumb::SyntaxBreadcrumbSource>(grammarId());
}

void FoldBreadcrumbBinding::onDocumentOpened()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    opened_ = true;
    ensureSources();
    textCache_.clear();
    refreshBreadcrumb();
}

void FoldBreadcrumbBinding::setTreeSitterEnabled(bool enabled)
{
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(enabled);
    refreshBreadcrumb();
}

bool FoldBreadcrumbBinding::treeSitterEnabled() const
{
    return vedit::syntax::SyntaxService::instance().treeSitterEnabled();
}

void FoldBreadcrumbBinding::notifyReparsed()
{
    refreshBreadcrumb();
}

void FoldBreadcrumbBinding::refreshBreadcrumb()
{
    if (view_ == nullptr || view_->document() == nullptr || !opened_ || breadcrumbSource_ == nullptr) {
        return;
    }
    const QString text = view_->document()->text();
    textCache_ = text;
    const int line = view_->cursorPosition().line();
    std::vector<vedit::breadcrumb::Entry> path;
    if (breadcrumbSource_->semanticActive()) {
        path = breadcrumbSource_->pathAt(textCache_, line);
    }
    const bool changed = !breadcrumbModel_.matches(path);
    breadcrumbModel_.setPath(std::move(path));
    if (changed) {
        emit breadcrumbChanged(breadcrumbModel_.display());
        if (statusListener_) {
            statusListener_();
        }
    }
}

QString FoldBreadcrumbBinding::breadcrumbDisplay() const
{
    return breadcrumbModel_.display();
}

QString FoldBreadcrumbBinding::breadcrumbDescribe() const
{
    return breadcrumbModel_.describe();
}

quint64 FoldBreadcrumbBinding::breadcrumbUpdates() const
{
    return breadcrumbModel_.updateCount();
}

quint64 FoldBreadcrumbBinding::breadcrumbChanges() const
{
    return breadcrumbModel_.changeCount();
}

bool FoldBreadcrumbBinding::semanticActive() const
{
    return breadcrumbSource_ != nullptr && breadcrumbSource_->semanticActive();
}

void FoldBreadcrumbBinding::setStatusListener(std::function<void()> listener)
{
    statusListener_ = std::move(listener);
}

} // namespace vedit::editor
