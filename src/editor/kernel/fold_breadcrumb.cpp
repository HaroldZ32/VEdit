#include "fold_breadcrumb.h"

#include "editor_analysis_pool.h"
#include "syntax_breadcrumb_source.h"
#include "syntax_service.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QEvent>
#include <QPromise>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace vedit::editor {
namespace {

constexpr int kLargeDocumentLines = 2000;
constexpr int kLargeDocumentChars = 65536;
constexpr int kWorkerRetryMs = 40;

} // namespace

FoldBreadcrumbBinding::FoldBreadcrumbBinding(KTextEditor::View *view, QObject *parent)
    : QObject(parent)
    , view_(view)
{
    setObjectName(QStringLiteral("breadcrumb-binding"));

    breadcrumbDebounce_ = new QTimer(this);
    breadcrumbDebounce_->setObjectName(QStringLiteral("breadcrumbDebounce"));
    breadcrumbDebounce_->setSingleShot(true);
    breadcrumbDebounce_->setInterval(150);
    connect(breadcrumbDebounce_, &QTimer::timeout, this, &FoldBreadcrumbBinding::refreshBreadcrumb);
    connect(&breadcrumbWorker_, &QFutureWatcherBase::finished, this, &FoldBreadcrumbBinding::finishBreadcrumb);

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
    breadcrumbSource_ = std::make_shared<vedit::breadcrumb::SyntaxBreadcrumbSource>(grammarId());
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
    ++sourceGeneration_;
    if (view_->document()->lines() <= kLargeDocumentLines) {
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
    ++sourceGeneration_;
    forceNextRefresh_ = true;
    refreshBreadcrumb();
}

bool FoldBreadcrumbBinding::treeSitterEnabled() const
{
    return vedit::syntax::SyntaxService::instance().treeSitterEnabled();
}

void FoldBreadcrumbBinding::notifyReparsed()
{
    ++sourceGeneration_;
    forceNextRefresh_ = true;
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
    if (!breadcrumbSource_->semanticActive()) {
        applyPath({}, view_->cursorPosition().line());
        return;
    }
    if (workerRunning_) {
        return;
    }
    KTextEditor::Document *document = view_->document();
    const bool manyLines = document->lines() > kLargeDocumentLines;
    if (manyLines && editorAnalysisPool().activeThreadCount() != 0) {
        breadcrumbDebounce_->start(kWorkerRetryMs);
        return;
    }
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
    if (!manyLines && textCache_.size() <= kLargeDocumentChars) {
        applyPath(breadcrumbSource_->pathAt(textCache_, line), line);
        return;
    }
    QPromise<std::vector<vedit::breadcrumb::Entry>> promise;
    const auto future = promise.future();
    if (!editorAnalysisPool().tryStart([source = breadcrumbSource_, text = textCache_, line,
                                        promise = std::move(promise)]() mutable {
            promise.start();
            promise.addResult(source->pathAt(text, line));
            promise.finish();
        })) {
        // A busy worker has no queued snapshots. The retry gets the latest revision.
        forceNextRefresh_ = true;
        breadcrumbDebounce_->start(kWorkerRetryMs);
        return;
    }
    workerRunning_ = true;
    workerGeneration_ = sourceGeneration_;
    workerRevision_ = revision;
    workerLine_ = line;
    breadcrumbWorker_.setFuture(future);
}

void FoldBreadcrumbBinding::finishBreadcrumb()
{
    workerRunning_ = false;
    if (view_ != nullptr && view_->document() != nullptr && workerGeneration_ == sourceGeneration_
        && view_->document()->revision() == workerRevision_ && view_->cursorPosition().line() == workerLine_
        && semanticActive()) {
        applyPath(breadcrumbWorker_.result(), workerLine_);
    } else if (!breadcrumbDebounce_->isActive()) {
        refreshBreadcrumb();
    }
}

void FoldBreadcrumbBinding::applyPath(std::vector<vedit::breadcrumb::Entry> path, int line)
{
    forceNextRefresh_ = false;
    lastLine_ = line;
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
