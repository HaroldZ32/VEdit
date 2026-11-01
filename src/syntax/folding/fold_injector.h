#pragma once

// Fold injection over the ONLY public KTextEditor folding interface (task T19 /
// AC-S2.2 / AMENDMENTS A1-A3).
//
// KTextEditor 6.24.0 exposes no folding model: the public API can only configure
// the folding bar, not add/query/remove ranges (evidence/r2-fold/decision.md
// §2.1). What is public is the editor command interface: KatePart registers
// `fold`, `tfold` and `unfold`, and `KTextEditor::Command::exec(view, cmd, msg,
// range)` accepts an explicit range. This class is the application-side
// injector around that interface:
//
//   * A1: every range is validated by fold_range.h BEFORE exec; rejected ranges
//     never reach the command, the registry or the counters, and are logged with
//     reason + raw range + the selection at that moment. The exec() return value
//     is recorded for diagnostics and is NEVER used to decide "folded";
//   * A2: a full rebuild only happens for the three allowed triggers (first open,
//     tree-sitter toggle, reparse complete). The key/cursor/scroll paths are
//     explicit no-ops. Counters are exposed as `folding.rebuild.full_count` and
//     `folding.inject.count`;
//   * A3: observation goes through observeView() only (firstDisplayedLine
//     RealLine/VisibleLine + verticalScrollBar()->maximum()); the registry is
//     never an assertion source.
#include "fold_range.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

namespace KTextEditor {
class Command;
class View;
}

namespace vedit::folding {

class FoldSource;

// A2.1: the ONLY events allowed to rebuild the whole fold set.
enum class RebuildTrigger {
    FirstOpen,          // (c) the file was just opened
    TreeSitterToggled,  // (a) the runtime tree-sitter switch changed
    Reparsed,           // (b) the fold source finished a reparse
};

const char *rebuildTriggerName(RebuildTrigger trigger);

// A3.1 observation of the real view. Two of the three allowed measures:
// firstDisplayedLine(RealLine) vs (VisibleLine), and scroll_max.
struct ViewObservation {
    int firstDisplayedRealLine = -1;
    int lastDisplayedRealLine = -1;
    int firstDisplayedVisibleLine = -1;
    int lastDisplayedVisibleLine = -1;
    // Lines the viewport spans in real coordinates minus visible coordinates:
    // > 0 iff some displayed real lines are hidden by folds.
    int hiddenInViewport = 0;
    int scrollMax = -1;
    int cursorLine = -1;
};

ViewObservation observeView(KTextEditor::View *view);

struct RebuildOutcome {
    bool performed = false;
    RebuildTrigger trigger = RebuildTrigger::FirstOpen;
    int injected = 0;   // ranges accepted and handed to `fold`
    int rejected = 0;   // ranges refused by A1's validation
    int unfolded = 0;   // registry entries an `unfold` was issued for
    qint64 elapsedMs = 0;
};

class FoldInjector {
public:
    // The view is owned by the caller (the editor); the injector never deletes
    // it and every entry point is a no-op when it is null.
    explicit FoldInjector(KTextEditor::View *view);
    ~FoldInjector();

    FoldInjector(const FoldInjector &) = delete;
    FoldInjector &operator=(const FoldInjector &) = delete;

    // Validate + inject a batch (A1). Returns the number of accepted ranges
    // (i.e. `fold` executions issued), not the number of exec() true returns.
    int inject(const std::vector<FoldRange> &ranges);

    // A2.1: perform a FULL rebuild (unfold the registered set, then inject the
    // new one) for an allowed trigger only. `folding.rebuild.full_count` is
    // incremented exactly once per performed rebuild.
    RebuildOutcome rebuild(RebuildTrigger trigger, const std::vector<FoldRange> &ranges);
    RebuildOutcome rebuildFrom(FoldSource &source, RebuildTrigger trigger);

    // A2.1: the key path / cursor moves / scrolling must never rebuild folds.
    // They are observable (counted) so the acceptance test can prove the path
    // was exercised while full_count stayed flat.
    void onKeyPress();
    void onCursorMoved();
    void onScroll();

    quint64 fullCount() const;   // `folding.rebuild.full_count`
    quint64 injectCount() const; // `folding.inject.count`
    quint64 rejectedCount() const;
    quint64 keyEventCount() const;
    quint64 cursorEventCount() const;
    QString diagnostics() const;
    // The accepted ranges this injector believes are folded. For diagnostics and
    // for the rebuild's unfold pass; NEVER an acceptance assertion source (A3.3).
    const std::vector<FoldRange> &registry() const;
    // Exec return values of the `fold`/`unfold` calls we issued, in order. They
    // are evidence for A1.2 (the value is not a success signal), not a decision.
    const std::vector<bool> &execResults() const;

private:
    int injectUnchecked(const std::vector<FoldRange> &ranges);
    int unfoldRegistered();
    void log(const QString &line);

    KTextEditor::View *view_ = nullptr;
    KTextEditor::Command *foldCommand_ = nullptr;
    KTextEditor::Command *unfoldCommand_ = nullptr;

    std::vector<FoldRange> registry_;
    std::vector<bool> execResults_;
    QStringList diagnostics_;
    quint64 fullCount_ = 0;
    quint64 injectCount_ = 0;
    quint64 rejectedCount_ = 0;
    quint64 keyEventCount_ = 0;
    quint64 cursorEventCount_ = 0;
};

} // namespace vedit::folding
