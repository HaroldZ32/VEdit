#include "fold_breadcrumb.h"

#include "syntax_breadcrumb_source.h"
#include "syntax_service.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

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
    // T9: a cursor move is a trigger too - debounced, exactly like typing. The
    // old immediate refresh copied the WHOLE document on every cursor step.
    connect(view_, &KTextEditor::View::cursorPositionChanged, this,
            [this](KTextEditor::View *, KTextEditor::Cursor) {
                if (breadcrumbDebounce_ != nullptr) {
                    breadcrumbDebounce_->start();
                }
            });
    if (view_->document() != nullptr) {
        connect(view_->document(), &KTextEditor::Document::textChanged, this,
                [this](KTextEditor::Document *) {
                    // Debounce to one semantic update per typing pause.
                    textDirty_ = true;
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
    textCached_ = false;
    textDirty_ = true;
    forceNextRefresh_ = true;
    // batch-20261115 AC-5.4: the seed refresh copies the whole document and runs
    // the full semantic scan (measured 192 ms at 10k lines,
    // evidence/large-file/REPORT.md). Inside openFile() that blocks the first
    // paint for a path the user cannot read yet. Small documents keep the
    // synchronous seed (no visible breadcrumb flash); above the threshold the
    // SAME work runs from the existing debounce timer, i.e. after the open has
    // returned to the caller.
    constexpr int kMaxSynchronousSeedLines = 2000;
    if (view_->document()->lines() <= kMaxSynchronousSeedLines) {
        refreshBreadcrumb();
        return;
    }
    // A newly loaded document must never keep the PREVIOUS document's path on
    // the bar while the deferred scan is pending.
    breadcrumbModel_.setPath(std::vector<vedit::breadcrumb::Entry>{});
    publishEntries();
    pathPublished_ = false;
    lastLine_ = -1;
    if (statusListener_) {
        statusListener_();
    }
    if (breadcrumbDebounce_ != nullptr) {
        breadcrumbDebounce_->start(0);
    }
}

void FoldBreadcrumbBinding::setTreeSitterEnabled(bool enabled)
{
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(enabled);
    forceNextRefresh_ = true; // the semantic source changed, the text did not
    refreshBreadcrumb();
}

bool FoldBreadcrumbBinding::treeSitterEnabled() const
{
    return vedit::syntax::SyntaxService::instance().treeSitterEnabled();
}

void FoldBreadcrumbBinding::notifyReparsed()
{
    forceNextRefresh_ = true; // a reparse can change the path with the same text
    refreshBreadcrumb();
}

void FoldBreadcrumbBinding::refreshBreadcrumb()
{
    if (view_ == nullptr || view_->document() == nullptr || !opened_ || breadcrumbSource_ == nullptr) {
        // Keep the pane bar in step with the (empty) model instead of leaving a
        // previous document's path on the view.
        publishEntries();
        return;
    }
    KTextEditor::Document *document = view_->document();
    // T9: copy the document ONCE per revision, never per cursor step. The
    // revision is the document's own change counter, so a signal we missed
    // cannot leave a stale cache behind.
    const qint64 revision = document->revision();
    const bool textChanged =
        textDirty_ || !textCached_ || cachedDocument_ != document || cachedRevision_ != revision;
    if (textChanged) {
        textCache_ = document->text();
        cachedDocument_ = document;
        cachedRevision_ = revision;
        textCached_ = true;
        textDirty_ = false;
        ++textCopies_;
    }
    const int line = view_->cursorPosition().line();
    if (!textChanged && !forceNextRefresh_ && pathPublished_ && line == lastLine_) {
        // Same text, same line: the semantic path cannot have moved.
        return;
    }
    forceNextRefresh_ = false;
    lastLine_ = line;
    std::vector<vedit::breadcrumb::Entry> path;
    if (breadcrumbSource_->semanticActive()) {
        path = breadcrumbSource_->pathAt(textCache_, line);
    }
    const bool changed = !breadcrumbModel_.matches(path);
    breadcrumbModel_.setPath(std::move(path));
    // The pane bar reads this property, so it must follow every update (not
    // only the ones that changed the display string: a same-text path with a
    // different line still has to move the click targets).
    publishEntries();
    pathPublished_ = true;
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

std::vector<vedit::breadcrumb::Entry> FoldBreadcrumbBinding::pathEntries() const
{
    return breadcrumbModel_.path();
}

void FoldBreadcrumbBinding::publishEntries()
{
    if (view_ == nullptr) {
        return;
    }
    QVariantList entries;
    for (const vedit::breadcrumb::Entry &entry : breadcrumbModel_.path()) {
        QVariantMap item;
        item.insert(QStringLiteral("kind"), QString::fromStdString(entry.kind));
        item.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
        item.insert(QStringLiteral("line"), entry.line);
        entries.append(item);
    }
    // The app shell has no KTextEditor dependency; a QObject property is the
    // documented hand-off for the pane's Bar (see fold_breadcrumb.h).
    view_->setProperty("veditBreadcrumbEntries", entries);
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

bool FoldBreadcrumbBinding::hasStatusListener() const
{
    return static_cast<bool>(statusListener_);
}

} // namespace vedit::editor
