#include "fold_injector.h"

#include "fold_source.h"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QElapsedTimer>
#include <QScrollBar>
#include <QStringList>

#include <algorithm>

namespace vedit::folding {

namespace {

QString rangeText(const FoldRange &range)
{
    return QStringLiteral("[%1,%2]").arg(range.startLine).arg(range.endLine);
}

} // namespace

const char *rebuildTriggerName(RebuildTrigger trigger)
{
    switch (trigger) {
    case RebuildTrigger::FirstOpen:
        return "first-open";
    case RebuildTrigger::TreeSitterToggled:
        return "tree-sitter-toggled";
    case RebuildTrigger::Reparsed:
        return "reparsed";
    }
    return "unknown";
}

ViewObservation observeView(KTextEditor::View *view)
{
    ViewObservation observation;
    if (view == nullptr) {
        return observation;
    }
    observation.firstDisplayedRealLine =
        view->firstDisplayedLine(KTextEditor::View::RealLine);
    observation.lastDisplayedRealLine =
        view->lastDisplayedLine(KTextEditor::View::RealLine);
    observation.firstDisplayedVisibleLine =
        view->firstDisplayedLine(KTextEditor::View::VisibleLine);
    observation.lastDisplayedVisibleLine =
        view->lastDisplayedLine(KTextEditor::View::VisibleLine);
    observation.hiddenInViewport = (observation.lastDisplayedRealLine
                                    - observation.firstDisplayedRealLine)
                                   - (observation.lastDisplayedVisibleLine
                                      - observation.firstDisplayedVisibleLine);
    if (QScrollBar *scroll = view->verticalScrollBar()) {
        observation.scrollMax = scroll->maximum();
    }
    observation.cursorLine = view->cursorPosition().line();
    return observation;
}

FoldInjector::FoldInjector(KTextEditor::View *view)
    : view_(view)
{
    if (KTextEditor::Editor *editor = KTextEditor::Editor::instance()) {
        foldCommand_ = editor->queryCommand(QStringLiteral("fold"));
        unfoldCommand_ = editor->queryCommand(QStringLiteral("unfold"));
    }
    if (foldCommand_ == nullptr) {
        log(QStringLiteral("folding.command fold=missing"));
    }
    if (unfoldCommand_ == nullptr) {
        log(QStringLiteral("folding.command unfold=missing"));
    }
}

FoldInjector::~FoldInjector() = default;

int FoldInjector::inject(const std::vector<FoldRange> &ranges)
{
    if (view_ == nullptr) {
        rejectedCount_ += ranges.size();
        log(QStringLiteral("folding.reject reason=no-view ranges=%1").arg(ranges.size()));
        return 0;
    }
    if (foldCommand_ == nullptr) {
        rejectedCount_ += ranges.size();
        log(QStringLiteral("folding.reject reason=no-command ranges=%1").arg(ranges.size()));
        return 0;
    }
    int lineCount = 0;
    if (const KTextEditor::Document *document = view_->document()) {
        lineCount = document->lines();
    }
    SelectionRange selection;
    const KTextEditor::Range currentSelection = view_->selectionRange();
    if (currentSelection.isValid() && currentSelection.start().line() != currentSelection.end().line()) {
        selection.startLine = currentSelection.start().line();
        selection.endLine = currentSelection.end().line();
    }

    // A1: validation happens BEFORE any command is executed. A rejected range
    // never reaches `fold`, the registry or the counters.
    const Validation validation = validateFoldRanges(ranges, lineCount, selection);
    for (const Rejection &rejection : validation.rejected) {
        ++rejectedCount_;
        log(QStringLiteral("folding.reject reason=%1 %2")
                .arg(QString::fromUtf8(rejectReasonName(rejection.reason)),
                     QString::fromStdString(rejection.detail)));
    }
    return injectUnchecked(validation.accepted);
}

int FoldInjector::injectUnchecked(const std::vector<FoldRange> &ranges)
{
    int injected = 0;
    for (const FoldRange &range : ranges) {
        if (std::find(registry_.begin(), registry_.end(), range) != registry_.end()) {
            continue; // already folded by this injector
        }
        QString message;
        const bool returnValue =
            foldCommand_->exec(view_, QStringLiteral("fold"), message,
                               KTextEditor::Range(range.startLine, 0, range.endLine, 0));
        // A1.2: the return value is recorded, never consulted. The command
        // returns true for ranges that fold nothing and for inverted ranges
        // (evidence/r2-fold/decision.md §3.3, A1 finding), so "accepted by
        // validation + command issued" is the only honest bookkeeping.
        execResults_.push_back(returnValue);
        registry_.push_back(range);
        ++injectCount_;
        ++injected;
        if (!returnValue) {
            log(QStringLiteral("folding.exec.returned-false range=%1 message=%2")
                    .arg(rangeText(range), message));
        }
    }
    return injected;
}

int FoldInjector::unfoldRegistered()
{
    if (view_ == nullptr || unfoldCommand_ == nullptr || registry_.empty()) {
        return 0;
    }
    // `unfold` has no range support: it acts on the fold starting at the cursor
    // line (evidence/r2-fold/decision.md §2.2). A full rebuild therefore has to
    // place the cursor on every registered start line, then restore it.
    const KTextEditor::Cursor original = view_->cursorPosition();
    int unfolded = 0;
    for (auto it = registry_.rbegin(); it != registry_.rend(); ++it) {
        view_->setCursorPosition(KTextEditor::Cursor(it->startLine, 0));
        QString message;
        const bool returnValue = unfoldCommand_->exec(
            view_, QStringLiteral("unfold"), message, KTextEditor::Range::invalid());
        execResults_.push_back(returnValue);
        ++unfolded;
    }
    view_->setCursorPosition(original);
    return unfolded;
}

RebuildOutcome FoldInjector::rebuild(RebuildTrigger trigger, const std::vector<FoldRange> &ranges)
{
    RebuildOutcome outcome;
    outcome.trigger = trigger;

    QElapsedTimer timer;
    timer.start();
    outcome.unfolded = unfoldRegistered();
    registry_.clear();
    const quint64 rejectedBefore = rejectedCount_;
    outcome.injected = inject(ranges);
    outcome.rejected = static_cast<int>(rejectedCount_ - rejectedBefore);
    ++fullCount_;
    outcome.elapsedMs = timer.elapsed();
    outcome.performed = true;

    log(QStringLiteral("folding.rebuild trigger=%1 injected=%2 rejected=%3 unfolded=%4 elapsed_ms=%5")
            .arg(QString::fromUtf8(rebuildTriggerName(trigger)))
            .arg(outcome.injected)
            .arg(outcome.rejected)
            .arg(outcome.unfolded)
            .arg(outcome.elapsedMs));
    log(QStringLiteral("folding.counters full_count=%1 inject_count=%2 rejected=%3")
            .arg(fullCount_)
            .arg(injectCount_)
            .arg(rejectedCount_));
    return outcome;
}

RebuildOutcome FoldInjector::rebuildFrom(FoldSource &source, RebuildTrigger trigger)
{
    QString text;
    if (view_ != nullptr && view_->document() != nullptr) {
        text = view_->document()->text();
    }
    return rebuild(trigger, source.ranges(text));
}

void FoldInjector::onKeyPress()
{
    ++keyEventCount_;
    if (keyEventCount_ == 1) {
        // A2.1: a key press may never rebuild the whole fold set.
        log(QStringLiteral("folding.rebuild.refused event=keypress (A2.1)"));
    }
}

void FoldInjector::onCursorMoved()
{
    ++cursorEventCount_;
    if (cursorEventCount_ == 1) {
        // A2.1: a cursor move may never rebuild the whole fold set.
        log(QStringLiteral("folding.rebuild.refused event=cursor-move (A2.1)"));
    }
}

void FoldInjector::onScroll()
{
    log(QStringLiteral("folding.rebuild.refused event=scroll (A2.1)"));
}

quint64 FoldInjector::fullCount() const
{
    return fullCount_;
}

quint64 FoldInjector::injectCount() const
{
    return injectCount_;
}

quint64 FoldInjector::rejectedCount() const
{
    return rejectedCount_;
}

quint64 FoldInjector::keyEventCount() const
{
    return keyEventCount_;
}

quint64 FoldInjector::cursorEventCount() const
{
    return cursorEventCount_;
}

QString FoldInjector::diagnostics() const
{
    return diagnostics_.join(QLatin1Char('\n'));
}

const std::vector<FoldRange> &FoldInjector::registry() const
{
    return registry_;
}

const std::vector<bool> &FoldInjector::execResults() const
{
    return execResults_;
}

void FoldInjector::log(const QString &line)
{
    diagnostics_.append(line);
}

} // namespace vedit::folding
