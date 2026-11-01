#include "fold_breadcrumb.h"

#include "ksyntax_fold_source.h"
#include "syntax_breadcrumb_source.h"
#include "syntax_fold_source.h"
#include "syntax_service.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

namespace vedit::editor {
namespace {

// Same walk-up the toolchain resolver uses for a bare build: find the checkout's
// resources/syntax next to the source tree. The delivered AppRun narrows the
// search path and exports VEDIT_SYNTAX_DIR, which wins.
QString findSyntaxDirectory(const QString &startDir)
{
    QDir dir(startDir);
    for (int depth = 0; depth < 8; ++depth) {
        const QString candidate = dir.filePath(QStringLiteral("resources/syntax"));
        if (QFileInfo(candidate + QStringLiteral("/vedit-systemverilog.xml")).isFile()) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

} // namespace

FoldBreadcrumbBinding::FoldBreadcrumbBinding(KTextEditor::View *view, QObject *parent)
    : QObject(parent)
    , view_(view)
    , injector_(view)
{
    setObjectName(QStringLiteral("fold-breadcrumb-binding"));

    breadcrumbDebounce_ = new QTimer(this);
    breadcrumbDebounce_->setObjectName(QStringLiteral("breadcrumbDebounce"));
    breadcrumbDebounce_->setSingleShot(true);
    breadcrumbDebounce_->setInterval(150);
    connect(breadcrumbDebounce_, &QTimer::timeout, this, [this]() { refreshBreadcrumb(); });

    if (view_ == nullptr) {
        return;
    }
    view_->installEventFilter(this);
    connect(view_, &KTextEditor::View::cursorPositionChanged, this,
            [this](KTextEditor::View *, KTextEditor::Cursor) {
                // A2.1: cursor movement is a counted no-op for folding; it only
                // moves the breadcrumb (the source reuses its cached parse).
                injector_.onCursorMoved();
                refreshBreadcrumb();
            });
    if (view_->document() != nullptr) {
        connect(view_->document(), &KTextEditor::Document::textChanged, this,
                [this](KTextEditor::Document *) {
                    // NEVER rebuild folds here: typing is not an allowed rebuild
                    // trigger. The breadcrumb is debounced to one semantic update
                    // per typing pause.
                    if (breadcrumbDebounce_ != nullptr) {
                        breadcrumbDebounce_->start();
                    }
                });
    }
}

FoldBreadcrumbBinding::~FoldBreadcrumbBinding()
{
    if (view_ != nullptr) {
        view_->removeEventFilter(this);
    }
}

FoldBreadcrumbBinding *FoldBreadcrumbBinding::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<FoldBreadcrumbBinding *>(QString(),
                                                           Qt::FindDirectChildrenOnly);
}

QString FoldBreadcrumbBinding::definitionDirectory() const
{
    const QString fromEnvironment = qEnvironmentVariable("VEDIT_SYNTAX_DIR");
    if (!fromEnvironment.isEmpty() && QFileInfo(fromEnvironment).isDir()) {
        return fromEnvironment;
    }
    return findSyntaxDirectory(QCoreApplication::applicationDirPath());
}

QString FoldBreadcrumbBinding::definitionFileName() const
{
    const QString mode = view_ != nullptr && view_->document() != nullptr
        ? view_->document()->highlightingMode()
        : QString();
    return mode == QStringLiteral("Verilog") ? QStringLiteral("vedit-verilog.xml")
                                             : QStringLiteral("vedit-systemverilog.xml");
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
    const QString directory = definitionDirectory();
    const QString definitionPath = directory.isEmpty()
        ? QString()
        : directory + QLatin1Char('/') + definitionFileName();
    closedSource_ = std::make_unique<vedit::folding::KsyntaxFoldSource>(definitionPath);
    treeSitterSource_ = std::make_unique<vedit::folding::SyntaxFoldSource>(grammarId());
    breadcrumbSource_ = std::make_unique<vedit::breadcrumb::SyntaxBreadcrumbSource>(grammarId());
}

void FoldBreadcrumbBinding::onDocumentOpened()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    ensureSources();
    opened_ = true;
    rebuildFor(vedit::folding::RebuildTrigger::FirstOpen);
    refreshBreadcrumb();
}

void FoldBreadcrumbBinding::releaseFolds()
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    if (injector_.registry().empty()) {
        return;
    }
    // Full rebuild with an empty range set: the injector unfolds everything it
    // registered (same path the TreeSitterToggled release uses). This runs on the
    // OLD text, before the provider replaces it.
    injector_.rebuild(vedit::folding::RebuildTrigger::FirstOpen, {});
}

void FoldBreadcrumbBinding::setTreeSitterEnabled(bool enabled)
{
    vedit::syntax::SyntaxService::instance().setTreeSitterEnabled(enabled);
    rebuildFor(vedit::folding::RebuildTrigger::TreeSitterToggled);
    refreshBreadcrumb();
}

bool FoldBreadcrumbBinding::treeSitterEnabled() const
{
    return vedit::syntax::SyntaxService::instance().treeSitterEnabled();
}

void FoldBreadcrumbBinding::notifyReparsed()
{
    rebuildFor(vedit::folding::RebuildTrigger::Reparsed);
    refreshBreadcrumb();
}

void FoldBreadcrumbBinding::rebuildFor(vedit::folding::RebuildTrigger trigger)
{
    if (view_ == nullptr || view_->document() == nullptr) {
        return;
    }
    ensureSources();
    vedit::folding::FoldSource *source = nullptr;
    if (vedit::syntax::SyntaxService::instance().treeSitterEnabled()) {
        source = treeSitterSource_.get();
    } else {
        source = closedSource_.get();
    }
    if (source == nullptr) {
        // No source available: release the registered folds through the same
        // rebuild path (the injector unfolds its registry first).
        injector_.rebuild(trigger, {});
        return;
    }
    injector_.rebuildFrom(*source, trigger);
}

void FoldBreadcrumbBinding::refreshBreadcrumb()
{
    if (view_ == nullptr || view_->document() == nullptr || !opened_) {
        return;
    }
    ensureSources();
    textCache_ = view_->document()->text();
    // Defensive clamp: a trailing newline yields an extra empty "line" and a
    // stale cursor can point past the text; the breadcrumb source must never be
    // handed an out-of-range line (the lane's pathAt assumes a valid line and a
    // byte offset inside the document).
    int lastLine = static_cast<int>(textCache_.count(QLatin1Char('\n')));
    if (textCache_.endsWith(QLatin1Char('\n')) && lastLine > 0) {
        --lastLine;
    }
    const int line = std::clamp(view_->cursorPosition().line(), 0, lastLine);
    std::vector<vedit::breadcrumb::Entry> path;
    if (breadcrumbSource_ != nullptr && breadcrumbSource_->semanticActive()) {
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

bool FoldBreadcrumbBinding::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == view_ && event != nullptr) {
        if (event->type() == QEvent::KeyPress) {
            // A2.1: counted, never a rebuild.
            injector_.onKeyPress();
        } else if (event->type() == QEvent::Wheel) {
            injector_.onScroll();
        }
    }
    return false;
}

} // namespace vedit::editor
